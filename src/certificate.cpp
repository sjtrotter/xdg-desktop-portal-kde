/* SPDX-License-Identifier: LGPL-2.1-or-later
 * Wire validation and lifetime rules follow xdg-desktop-portal-certificate's
 * certificate-impl.c and session-impl.c (2026 Stephen J. Trotter).
 */
#include "certificate.h"

#include "certificateconfig.h"
#include "certificatedispatcher.h"
#include "certificateui.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QDBusVirtualObject>
#include <QEventLoop>
#include <QGuiApplication>
#include <algorithm>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

using namespace Qt::StringLiterals;

namespace
{
constexpr auto frontendName = "org.freedesktop.portal.Desktop";

template <typename T> T input()
{
    T value{ };
    value.struct_size = sizeof(T);
    value.abi_version = XCC_ABI_VERSION;
    return value;
}

QVariantMap errorResults(const char *code) { return { { u"reason"_s, QString::fromLatin1(code) } }; }

QStringList strings(const char *const *values)
{
    QStringList result;
    if (values) {
        for (; *values; ++values) {
            result.append(QString::fromUtf8(*values));
        }
    }
    return result;
}

QByteArray bytes(const XccBytes &value) { return QByteArray(reinterpret_cast<const char *>(value.data), qsizetype(value.size)); }

// QtDBus leaves nested containers in QDBusArgument. Test the actual wire type
// before decoding: QVariant's converting accessors must never relax constraints.
bool wireType(const QVariant &value, QMetaType type, const char *signature)
{
    if (value.metaType() == type) {
        return true;
    }
    return value.metaType() == QMetaType::fromType<QDBusArgument>() && value.value<QDBusArgument>().currentSignature() == QLatin1String(signature);
}

struct Options {
    const QVariantMap &map;
    bool bad = false;
    bool string(const char *key, QByteArray &out)
    {
        const auto it = map.constFind(QString::fromLatin1(key));
        if (it == map.cend()) {
            return false;
        }
        if (it->metaType() != QMetaType::fromType<QString>()) {
            bad = true;
            return false;
        }
        out = it->toString().toUtf8();
        return true;
    }
    bool uint32(const char *key, uint32_t &out)
    {
        const auto it = map.constFind(QString::fromLatin1(key));
        if (it == map.cend()) {
            return false;
        }
        if (it->metaType() != QMetaType::fromType<uint>()) {
            bad = true;
            return false;
        }
        out = it->toUInt();
        return true;
    }
    bool dictionary(const char *key, QVariantMap &out)
    {
        const auto it = map.constFind(QString::fromLatin1(key));
        if (it == map.cend()) {
            return false;
        }
        if (!wireType(*it, QMetaType::fromType<QVariantMap>(), "a{sv}")) {
            bad = true;
            return false;
        }
        out = qdbus_cast<QVariantMap>(*it);
        return true;
    }
    bool known(std::initializer_list<const char *> keys) const
    {
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            bool found = false;
            for (auto key : keys) {
                found |= it.key() == QLatin1String(key);
            }
            if (!found) {
                return false;
            }
        }
        return true;
    }
};

struct StringArray {
    QList<QByteArray> storage;
    std::vector<const char *> pointers;
    bool read(Options &options, const char *key)
    {
        const auto it = options.map.constFind(QString::fromLatin1(key));
        if (it == options.map.cend()) {
            return false;
        }
        if (!wireType(*it, QMetaType::fromType<QStringList>(), "as")) {
            options.bad = true;
            return false;
        }
        for (const auto &value : qdbus_cast<QStringList>(*it)) {
            storage.append(value.toUtf8());
        }
        for (const auto &value : storage) {
            pointers.push_back(value.constData());
        }
        pointers.push_back(nullptr);
        return true;
    }
    const char *const *data() const { return pointers.data(); }
};

// Certificate's Close methods require current-owner checks and invalidation
// tombstones. The shared KDE Request/Session classes cannot provide those rules.
class CloseObject : public QDBusVirtualObject
{
public:
    CloseObject(bool session, QObject *parent, std::function<void(const QDBusMessage &)> close)
        : QDBusVirtualObject(parent)
        , isSession(session)
        , closeCall(std::move(close))
    {
    }
    QString introspect(const QString &) const override
    {
        if (isSession) {
            return u"<interface name=\"org.freedesktop.impl.portal.Session\"><method name=\"Close\"/>"
                   "<signal name=\"Closed\"/><property name=\"version\" type=\"u\" access=\"read\"/></interface>"_s;
        }
        return u"<interface name=\"org.freedesktop.impl.portal.Request\"><method name=\"Close\"/></interface>"_s;
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.type() != QDBusMessage::MethodCallMessage) {
            return false;
        }
        const auto interface = isSession ? "org.freedesktop.impl.portal.Session" : "org.freedesktop.impl.portal.Request";
        if (message.interface() == QLatin1String(interface) && message.member() == "Close"_L1 && message.arguments().isEmpty()) {
            // Authorization can unexport this object on owner loss. Copy the
            // callback, and use deferred deletion for the dispatching object.
            auto call = closeCall;
            call(message);
            return true;
        }
        if (isSession && message.interface() == "org.freedesktop.DBus.Properties"_L1) {
            const auto args = message.arguments();
            if (message.member() == "Get"_L1 && args.size() == 2 && args[0].toString() == QLatin1String(interface) && args[1].toString() == "version"_L1) {
                return connection.send(message.createReply(QVariantList{ QVariant::fromValue(QDBusVariant(uint(1))) }));
            }
            if (message.member() == "GetAll"_L1 && args.size() == 1 && args[0].toString() == QLatin1String(interface)) {
                return connection.send(message.createReply(QVariantList{ QVariantMap{ { u"version"_s, uint(1) } } }));
            }
        }
        return false;
    }

private:
    bool isSession;
    std::function<void(const QDBusMessage &)> closeCall;
};
}

class CertificatePortal::State : public QObject
{
public:
    struct Session {
        XccSession *core = nullptr;
        QString path, appId, owner;
        CloseObject *object = nullptr;
        bool invalidated = false;
        std::optional<XccIdentityLevel> identity;
        ~Session()
        {
            if (core) {
                xcc_session_unref(core);
            }
        }
    };
    struct Transaction {
        State *state;
        QDBusMessage message;
        std::shared_ptr<Session> session;
        QString path;
        CloseObject *object = nullptr;
        XccRequest *core = nullptr;
        const char *cancelledCode = nullptr;
        bool capabilities = false;
    };

    State(CertificatePortal *portal, const QDBusConnection &connection, XccConfig config, const XccUiV1 *testUi)
        : portal(portal)
        , bus(connection)
        , watcher(QString::fromLatin1(frontendName), bus, QDBusServiceWatcher::WatchForOwnerChange)
    {
        qDBusRegisterMetaType<QList<QByteArray>>();
        auto uiTable = testUi ? *testUi : ui.table();
        auto dispatch = dispatcher.table();
        config.struct_size = sizeof(config);
        config.abi_version = XCC_ABI_VERSION;
        config.session_invalidated
            = [](XccSession *core, const char *reason, void *user) { static_cast<State *>(user)->invalidated(core, QString::fromUtf8(reason)); };
        config.user = this;
        context = xcc_context_new(&config, &uiTable, &dispatch, nullptr);
        ui.attach(context);
        connect(&watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &owner) {
            setOwner(owner);
            ownerResolved = true;
        });
        // Drain while the application still owns its event dispatcher, before
        // QApplication's destructor deletes DesktopPortal and its adaptors.
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { shutdown(); });
    }

    ~State() override { shutdown(); }

    void shutdown()
    {
        if (!context || stopping) {
            return;
        }
        stopping = true;
        cancel({ }, "backend_gone", XCC_CANCEL_BACKEND_GONE);
        // All dispatcher/UI receivers and transaction state remain alive while
        // workers and queued callbacks drain. The core DSO is linked NODELETE.
        QEventLoop loop;
        shutdownLoop = &loop;
        xcc_context_shutdown_async(
            context,
            [](XccContext *, void *user) {
                auto state = static_cast<State *>(user);
                state->shutdownComplete = true;
                state->shutdownLoop->quit();
            },
            this);
        if (!shutdownComplete) {
            loop.exec();
        }
        shutdownLoop = nullptr;
        // Core invalidation was delivered while the Session skeletons were
        // still exported; keep them through shutdown just like the reference.
        const auto live = sessions;
        for (const auto &session : live) {
            forget(session);
        }
        ui.attach(nullptr);
        xcc_context_unref(context);
        context = nullptr;
    }

    bool authorize(const QDBusMessage &message)
    {
        bool accepted = false;
        if (context && !stopping) {
            if (!ownerResolved) {
                resolveOwner();
            }
            if (!frontendOwner.isEmpty() && frontendOwner == message.service()) {
                // Cached mismatches reject without a round trip. Every acceptance
                // resolves again: NameOwnerChanged is unordered against calls.
                resolveOwner();
                accepted = !frontendOwner.isEmpty() && frontendOwner == message.service();
            }
        }
        if (!accepted) {
            bus.send(message.createErrorReply(QDBusError::AccessDenied, u"Only xdg-desktop-portal may call this interface"_s));
        }
        return accepted;
    }

    void resolveOwner()
    {
        auto query = QDBusMessage::createMethodCall("org.freedesktop.DBus"_L1, "/org/freedesktop/DBus"_L1, "org.freedesktop.DBus"_L1, "GetNameOwner"_L1);
        query.setAutoStartService(false);
        query << QString::fromLatin1(frontendName);
        QDBusReply<QString> reply = bus.call(query, QDBus::Block, 1000);
        ownerResolved = true;
        setOwner(reply.isValid() ? reply.value() : QString());
    }

    void setOwner(const QString &owner)
    {
        if (stopping || frontendOwner == owner) {
            return;
        }
        if (context) {
            xcc_context_set_owner_generation(context, ++generation);
        }
        cancel({ }, "owner_gone", XCC_CANCEL_OWNER_GONE);
        const auto live = sessions;
        for (const auto &session : live) {
            if (session->owner != owner) {
                forget(session);
            }
        }
        frontendOwner = owner;
    }

    void reply(const QDBusMessage &message, uint response, const QVariantMap &results) { bus.send(message.createReply({ response, results })); }

    void unexport(const QString &path, CloseObject *&object)
    {
        if (object) {
            bus.unregisterObject(path);
            object->deleteLater();
            object = nullptr;
        }
    }

    void forget(const std::shared_ptr<Session> &session)
    {
        xcc_session_close(session->core);
        unexport(session->path, session->object);
        if (sessions.value(session->path) == session) {
            sessions.remove(session->path);
        }
    }

    void invalidated(XccSession *core, const QString &reason)
    {
        for (const auto &session : std::as_const(sessions)) {
            if (session->core == core && !session->invalidated) {
                session->invalidated = true;
                Q_EMIT portal->SessionInvalidated(QDBusObjectPath(session->path), reason);
                bus.send(QDBusMessage::createSignal(session->path, "org.freedesktop.impl.portal.Session"_L1, "Closed"_L1));
                // Keep the closed Session exported for the frontend's Close().
                return;
            }
        }
    }

    void cancel(const std::shared_ptr<Session> &session, const char *code, XccCancelReason reason)
    {
        for (auto transaction : std::as_const(transactions)) {
            if (!transaction->capabilities && (!session || transaction->session == session)) {
                transaction->cancelledCode = code;
                if (transaction->core) {
                    xcc_request_cancel(transaction->core, reason);
                }
            }
        }
    }

    std::shared_ptr<Session> lookup(const QDBusObjectPath &path, const QString &appId)
    {
        const auto session = sessions.value(path.path());
        return session && !session->invalidated && session->appId == appId ? session : nullptr;
    }

    Transaction *begin(const QDBusMessage &message, const QDBusObjectPath &handle, const std::shared_ptr<Session> &session)
    {
        auto transaction = std::make_shared<Transaction>(Transaction{ this, message, session, handle.path() });
        transaction->object = new CloseObject(false, this, [this, weak = std::weak_ptr(transaction)](const QDBusMessage &close) {
            const auto transaction = weak.lock();
            if (!authorize(close)) {
                return;
            }
            if (transaction && transaction->core) {
                xcc_request_cancel(transaction->core, XCC_CANCEL_USER);
                unexport(transaction->path, transaction->object);
            }
            bus.send(close.createReply());
        });
        if (!bus.registerVirtualObject(handle.path(), transaction->object, QDBusConnection::SingleNode)) {
            delete transaction->object;
            reply(message, 2, errorResults("invalid_request"));
            return nullptr;
        }
        transactions.insert(transaction.get(), transaction);
        return transaction.get();
    }

    static void done(XccRequest *, const XccResult *result, void *user)
    {
        auto transaction = static_cast<Transaction *>(user);
        auto state = transaction->state;
        // A queued Close may still hold this transaction across authorization.
        // Its weak export callback cannot retain or address a completed request.
        const auto retained = state->transactions.value(transaction);
        QVariantMap results;
        uint response = 0;
        if (transaction->capabilities) {
            results = { { u"purposes"_s, QStringList{ u"client_auth"_s, u"signing"_s, u"email"_s, u"ssh"_s } }, { u"operations"_s, QStringList{ u"sign"_s } },
                { u"mechanisms"_s, strings(result->mechanisms) }, { u"protected_authentication_path"_s, result->protected_path },
                { u"has_display"_s, result->has_display } };
        } else if (transaction->cancelledCode) {
            response = 2;
            results = errorResults(transaction->cancelledCode);
        } else if (result->error != XCC_OK) {
            response = xcc_error_wire_response(result->error);
            if (!(result->error == XCC_ERROR_CANCELLED && result->kind == XCC_RESULT_ACQUIRE)) {
                results = errorResults(xcc_error_wire_name(result->error));
            }
        } else if (result->kind == XCC_RESULT_ACQUIRE) {
            const auto info = xcc_candidate_info(result->candidate);
            if (!info || info->struct_size < offsetof(XccCandidateInfo, protected_path) + sizeof(info->protected_path)
                || info->abi_version != XCC_ABI_VERSION) {
                response = 2;
                results = errorResults("device_error");
            } else {
                // Older SDK producers have only the sanitized display prefix.
                const bool hasTokenMetadata = info->struct_size >= offsetof(XccCandidateInfo, token_reader) + sizeof(const char *);
                const QVariantMap tokenDisplay{
                    { u"label"_s, QString::fromUtf8(hasTokenMetadata ? info->token_label : info->token) },
                    { u"manufacturer"_s, hasTokenMetadata ? QString::fromUtf8(info->token_manufacturer) : QString() },
                    { u"model"_s, hasTokenMetadata ? QString::fromUtf8(info->token_model) : QString() },
                    { u"reader"_s, QString::fromUtf8(hasTokenMetadata ? info->token_reader : info->reader) },
                    { u"protected_authentication_path"_s, info->protected_path },
                };
                results = { { u"certificate_der"_s, bytes(xcc_candidate_certificate(result->candidate)) },
                    { u"chain_der"_s, QVariant::fromValue(QList<QByteArray>{ }) }, { u"chain_status"_s, u"leaf_only"_s },
                    { u"token_display"_s, tokenDisplay },
                    { u"key_type"_s, info->key_type ? QString::fromUtf8(info->key_type) : u"unknown"_s }, { u"key_size"_s, uint(info->key_size) },
                    { u"supported_mechanisms"_s, strings(info->mechanisms) },
                    { u"permitted_operations"_s, result->may_sign ? QStringList{ u"sign"_s } : QStringList{ } }, { u"may_prompt_later"_s, true } };
                if (info->key_curve_present) {
                    results.insert(u"key_curve"_s, QString::fromUtf8(info->key_curve));
                }
            }
        } else {
            results.insert(u"signature"_s, bytes(result->signature));
        }
        // Unexport before returning the terminal reply; completion releases the
        // request reference only after all borrowed result data has been copied.
        state->unexport(transaction->path, transaction->object);
        if (transaction->capabilities) {
            state->bus.send(transaction->message.createReply(QVariantList{ results }));
        } else {
            state->reply(transaction->message, response, results);
        }
        state->transactions.remove(transaction);
        xcc_request_unref(transaction->core);
        transaction->core = nullptr;
    }

    CertificatePortal *portal;
    QDBusConnection bus;
    QDBusServiceWatcher watcher;
    CertificateDispatcher dispatcher;
    CertificateUi ui;
    XccContext *context = nullptr;
    QString frontendOwner;
    uint64_t generation = 0;
    bool ownerResolved = false, stopping = false, shutdownComplete = false;
    QEventLoop *shutdownLoop = nullptr;
    QHash<QString, std::shared_ptr<Session>> sessions;
    QHash<Transaction *, std::shared_ptr<Transaction>> transactions;
};

CertificatePortal::CertificatePortal(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
    const CertificateConfig settings;
    auto config = settings.config();
    config.has_display = qGuiApp && (QGuiApplication::platformName() == "wayland"_L1 || QGuiApplication::platformName() == "xcb"_L1);
    d = std::make_unique<State>(this, QDBusConnection::sessionBus(), config, nullptr);
}

CertificatePortal::CertificatePortal(QObject *parent, const QDBusConnection &bus, const XccConfig &config, const XccUiV1 *ui)
    : QDBusAbstractAdaptor(parent)
    , d(std::make_unique<State>(this, bus, config, ui))
{
}

CertificatePortal::~CertificatePortal() = default;
bool CertificatePortal::isValid() const { return d->context != nullptr; }

void CertificatePortal::CreateSession(const QDBusObjectPath &handle, const QDBusObjectPath &session_handle, const QString &app_id, const QVariantMap &,
    const QDBusMessage &message, uint &, QVariantMap &)
{
    message.setDelayedReply(true);
    if (!d->authorize(message)) {
        return;
    }
    auto existing = d->sessions.value(session_handle.path());
    if (existing && !existing->invalidated) {
        d->reply(message, 2, errorResults("invalid_request"));
        return;
    }
    if (existing) {
        d->forget(existing);
    }
    // Even a noninteractive CreateSession briefly exports its Request.
    CloseObject request(false, nullptr, [](const QDBusMessage &) { });
    if (!d->bus.registerVirtualObject(handle.path(), &request, QDBusConnection::SingleNode)) {
        d->reply(message, 2, errorResults("invalid_request"));
        return;
    }
    d->bus.unregisterObject(handle.path());
    auto session = std::make_shared<State::Session>();
    session->path = session_handle.path();
    session->appId = app_id;
    session->owner = message.service();
    const auto app = app_id.toUtf8();
    const auto path = session->path.toUtf8();
    auto spec = input<XccSessionSpec>();
    spec.owner_generation = d->generation;
    spec.app_id = app.constData();
    spec.log_id = path.constData();
    session->core = xcc_session_new(d->context, &spec, nullptr);
    if (!session->core) {
        d->reply(message, 2, errorResults("no_such_session"));
        return;
    }
    // Retain locally across authorization that can discover owner loss and
    // remove the session. The exported callback itself must not form a cycle.
    session->object = new CloseObject(true, d.get(), [this, weak = std::weak_ptr(session)](const QDBusMessage &close) {
        const auto session = weak.lock();
        if (!d->authorize(close)) {
            return;
        }
        if (!session) {
            d->bus.send(close.createReply());
            return;
        }
        d->cancel(session, "no_such_session", XCC_CANCEL_SESSION_CLOSED);
        d->forget(session);
        d->bus.send(close.createReply());
    });
    if (!d->bus.registerVirtualObject(session->path, session->object, QDBusConnection::SingleNode)) {
        delete session->object;
        session->object = nullptr;
        xcc_session_close(session->core);
        d->reply(message, 2, errorResults("invalid_request"));
        return;
    }
    d->sessions.insert(session->path, session);
    d->reply(message, 0, { });
}

void CertificatePortal::AcquireCredential(const QDBusObjectPath &handle, const QDBusObjectPath &session_handle, const QString &app_id,
    const QString &parent_window, const QVariantMap &options, const QDBusMessage &message, uint &, QVariantMap &)
{
    message.setDelayedReply(true);
    if (!d->authorize(message)) {
        return;
    }
    const auto session = d->lookup(session_handle, app_id);
    if (!session) {
        d->reply(message, 2, errorResults("no_such_session"));
        return;
    }
    Options parsed{ options };
    QByteArray purpose;
    static const QList<QByteArray> purposes{ "client_auth", "signing", "email", "ssh" };
    if (!parsed.string("purpose", purpose) || !purposes.contains(purpose)) {
        d->reply(message, 2, errorResults("invalid_purpose"));
        return;
    }
    auto spec = input<XccAcquireSpec>();
    spec.purpose = XccPurpose(purposes.indexOf(purpose));
    const auto app = app_id.toUtf8();
    const auto parent = parent_window.toUtf8();
    spec.principal = { sizeof(XccPrincipal), XCC_ABI_VERSION, d->generation, app.constData(), XCC_IDENTITY_UNKNOWN, true };
    QByteArray identity, reason, activation, interaction;
    if (parsed.string("app_identity_level", identity)) {
        if (identity == "sandboxed") {
            spec.principal.identity_level = XCC_IDENTITY_VERIFIED_SANDBOXED;
        } else if (identity == "host") {
            spec.principal.identity_level = XCC_IDENTITY_DERIVED_HOST;
        } else if (identity != "unidentified") {
            parsed.bad = true;
        }
    }
    parsed.string("reason", reason);
    parsed.string("activation_token", activation);
    if (parsed.string("interaction_mode", interaction) && interaction != "required" && interaction != "allowed" && interaction != "forbidden") {
        parsed.bad = true;
    }
    spec.interaction_present = true;
    spec.interaction_forbidden = interaction == "forbidden";
    spec.lifetime_present = true;
    spec.lifetime_seconds = 300;
    parsed.uint32("lifetime", spec.lifetime_seconds);
    parsed.bad |= spec.lifetime_seconds == 0;
    spec.lifetime_seconds = std::min(spec.lifetime_seconds, uint32_t(3600));
    spec.sign_present = true;
    spec.sign = true;
    QVariantMap policy;
    if (parsed.dictionary("operation_policy", policy)) {
        Options p{ policy };
        parsed.bad |= !p.known({ "sign" });
        if (policy.contains(u"sign"_s)) {
            const auto sign = policy.value(u"sign"_s);
            parsed.bad |= sign.metaType() != QMetaType::fromType<bool>();
            spec.sign = sign.toBool();
        }
    }
    if (parsed.bad) {
        d->reply(message, 2, errorResults("invalid_request"));
        return;
    }
    // The reference checks/latches assurance before parsing certificate_filter.
    // Keep that wire error precedence using only public SDK state; the core
    // independently enforces the same non-increasing principal on acquisition.
    if (session->identity && spec.principal.identity_level < *session->identity) {
        d->reply(message, 2, errorResults("no_such_session"));
        return;
    }
    session->identity = spec.principal.identity_level;
    QVariantMap filter;
    parsed.dictionary("certificate_filter", filter);
    Options f{ filter };
    f.bad = parsed.bad || !f.known({ "issuers", "key_usage", "eku", "key_algorithms", "token_label", "piv_slot" });
    StringArray usage, eku, algorithms;
    spec.key_usage_present = usage.read(f, "key_usage");
    spec.key_usage = usage.data();
    spec.eku_oids_present = eku.read(f, "eku");
    spec.eku_oids = eku.data();
    spec.key_algorithms_present = algorithms.read(f, "key_algorithms");
    spec.key_algorithms = algorithms.data();
    QByteArray token, slot;
    spec.token_label_present = f.string("token_label", token);
    spec.token_label = token.constData();
    spec.piv_slot_present = f.string("piv_slot", slot);
    spec.piv_slot = slot.constData();
    if (spec.piv_slot_present && slot != "authentication" && slot != "signature" && slot != "key_management" && slot != "card_authentication") {
        f.bad = true;
    }
    QList<QByteArray> issuerStorage;
    std::vector<XccBytes> issuers;
    if (filter.contains(u"issuers"_s)) {
        spec.issuers_present = true;
        const auto value = filter.value(u"issuers"_s);
        if (!wireType(value, QMetaType::fromType<QList<QByteArray>>(), "aay")) {
            f.bad = true;
        } else {
            issuerStorage = qdbus_cast<QList<QByteArray>>(value);
            for (const auto &issuer : issuerStorage) {
                issuers.push_back({ sizeof(XccBytes), XCC_ABI_VERSION, reinterpret_cast<const uint8_t *>(issuer.constData()), size_t(issuer.size()) });
            }
        }
    }
    if (f.bad) {
        d->reply(message, 2, errorResults("invalid_filter"));
        return;
    }
    spec.issuers = issuers.data();
    spec.n_issuers = issuers.size();
    spec.reason = reason.constData();
    spec.activation_token = activation.constData();
    spec.parent_window = parent.constData();
    const auto prefix = "/org/freedesktop/portal/desktop/request/"_L1;
    const auto path = handle.path();
    const auto separator = path.indexOf(u'/', prefix.size());
    spec.generic_purpose_present = true;
    spec.generic_purpose = path.startsWith(prefix) && separator >= 0 && QStringView(path).mid(separator + 1).startsWith(u"pkcs11a");
    if (auto transaction = d->begin(message, handle, session)) {
        transaction->core = xcc_acquire_async(session->core, &spec, State::done, transaction);
    }
}

void CertificatePortal::Sign(const QDBusObjectPath &handle, const QDBusObjectPath &session_handle, const QString &app_id, const QString &parent_window,
    const QVariantMap &options, const QDBusMessage &message, uint &, QVariantMap &)
{
    message.setDelayedReply(true);
    if (!d->authorize(message)) {
        return;
    }
    const auto session = d->lookup(session_handle, app_id);
    if (!session) {
        d->reply(message, 2, errorResults("no_such_session"));
        return;
    }
    Options p{ options };
    QByteArray operationId, mechanism;
    p.string("operation_id", operationId);
    if (!p.string("mechanism", mechanism)) {
        p.bad = true;
    }
    QVariantMap parameters;
    p.dictionary("parameters", parameters);
    const auto payload = options.value(u"data"_s);
    p.bad |= payload.metaType() != QMetaType::fromType<QByteArray>();
    if (p.bad) {
        d->reply(message, 2, errorResults("invalid_request"));
        return;
    }
    Options params{ parameters };
    params.bad = !params.known({ "hash", "mgf", "salt_length", "signature_encoding" });
    const auto data = payload.toByteArray();
    const auto app = app_id.toUtf8();
    const auto parent = parent_window.toUtf8();
    auto spec = input<XccSignSpec>();
    spec.principal = { sizeof(XccPrincipal), XCC_ABI_VERSION, d->generation, app.constData(), XCC_IDENTITY_UNKNOWN, false };
    spec.parent_window = parent.constData();
    spec.operation_id = operationId.constData();
    spec.mechanism = mechanism.constData();
    spec.data = { sizeof(XccBytes), XCC_ABI_VERSION, reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()) };
    QByteArray hash, mgf, encoding;
    spec.hash_present = params.string("hash", hash);
    spec.hash = hash.constData();
    if (mechanism == "RSA_PSS") {
        spec.mgf_present = params.string("mgf", mgf);
        spec.mgf = mgf.constData();
        spec.salt_length_present = params.uint32("salt_length", spec.salt_length);
    }
    spec.encoding_present = params.string("signature_encoding", encoding);
    spec.signature_encoding = encoding.constData();
    if (params.bad) {
        // The reference defers malformed nested parameters until after core
        // principal/grant checks. Its private "valid_parameters" API is not in
        // the SDK. An explicitly invalid encoding preserves that ordering using
        // the public API and can never reach a device operation (only raw/der
        // are valid), including when another parameter was absent or mistyped.
        spec.encoding_present = true;
        spec.signature_encoding = "";
    }
    if (auto transaction = d->begin(message, handle, session)) {
        transaction->core = xcc_sign_async(session->core, &spec, State::done, transaction);
    }
}

void CertificatePortal::GetCapabilities(const QString &, const QVariantMap &, const QDBusMessage &message, QVariantMap &)
{
    message.setDelayedReply(true);
    if (!d->authorize(message)) {
        return;
    }
    auto transaction = std::make_shared<State::Transaction>(State::Transaction{ d.get(), message, { }, { } });
    transaction->capabilities = true;
    d->transactions.insert(transaction.get(), transaction);
    transaction->core = xcc_get_capabilities_async(d->context, State::done, transaction.get());
}

#include "moc_certificate.cpp"
