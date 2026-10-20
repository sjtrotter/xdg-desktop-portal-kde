/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificate.h"
#include "certificateui.h"
#include "certificatepinfield.h"
#include "certificatepinprompt.h"
#include "certificatetesthelpers.h"
#include "certificateuitesthelpers.h"

#include <QCryptographicHash>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <optional>

using namespace Qt::StringLiterals;

// Capture the public constructor's result for the injected UI without adding a
// production accessor. Limit the borrowed info to exercise older SDK producers.
static XccContext *testContext = nullptr;
static uint32_t candidateInfoLimit = UINT32_MAX;
static QVariantMap legacyTokenDisplay;
extern "C" XccContext *__real_xcc_context_new(const XccConfig *, const XccUiV1 *, const XccDispatch *, XccError *);
extern "C" XccContext *__wrap_xcc_context_new(const XccConfig *config, const XccUiV1 *ui, const XccDispatch *dispatch, XccError *error)
{
    testContext = __real_xcc_context_new(config, ui, dispatch, error);
    return testContext;
}
extern "C" const XccCandidateInfo *__real_xcc_candidate_info(const XccCandidate *);
extern "C" const XccCandidateInfo *__wrap_xcc_candidate_info(const XccCandidate *candidate)
{
    const auto info = __real_xcc_candidate_info(candidate);
    if (!info || candidateInfoLimit == UINT32_MAX) {
        return info;
    }
    static XccCandidateInfo prefix;
    prefix = *info;
    prefix.struct_size = candidateInfoLimit;
    legacyTokenDisplay = { { u"label"_s, QString::fromUtf8(info->token) }, { u"manufacturer"_s, QString() }, { u"model"_s, QString() },
        { u"reader"_s, QString::fromUtf8(info->reader) }, { u"protected_authentication_path"_s, info->protected_path } };
    prefix.token_label = prefix.token_manufacturer = prefix.token_model = prefix.token_reader = nullptr;
    return &prefix;
}

class CertificateAdaptorTest : public QObject
{
    Q_OBJECT
    QProcess daemon;
    std::optional<QDBusConnection> backend, frontend, stranger, successor;
    QObject *object = nullptr;
    CertificatePortal *portal = nullptr;
    int shown = 0, closed = 0;
    int pinPrompts = 0;
    bool chooseFirst = false;
    bool nativePin = false;
    CertificateUi nativeUi;
    QString busAddress;

    QDBusPendingCall call(const QDBusConnection &sender, const QString &method, const QVariantList &args,
        const QString &path = u"/org/freedesktop/portal/desktop"_s, const QString &interface = u"org.freedesktop.impl.portal.experimental.Certificate1"_s)
    {
        auto message = QDBusMessage::createMethodCall(backend->baseService(), path, interface, method);
        message.setArguments(args);
        return sender.asyncCall(message, 10000);
    }

    QDBusMessage wait(QDBusPendingCall pending)
    {
        QDBusPendingCallWatcher watcher(pending);
        if (!watcher.isFinished()) {
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(11000);
            loop.exec();
        }
        return watcher.isFinished() ? watcher.reply() : QDBusMessage::createError(QDBusError::Timeout, u"Test call timed out"_s);
    }

    QVariantList createArgs(const QString &suffix)
    {
        return { QVariant::fromValue(QDBusObjectPath(u"/request/create_"_s + suffix)), QVariant::fromValue(QDBusObjectPath(u"/session/"_s + suffix)),
            u"org.example.CertificateTest"_s, QVariantMap{ } };
    }

    QVariantList acquireArgs(const QString &suffix, QVariantMap options = { { u"purpose"_s, u"client_auth"_s } })
    {
        return { QVariant::fromValue(QDBusObjectPath(u"/request/acquire_"_s + suffix)), QVariant::fromValue(QDBusObjectPath(u"/session/"_s + suffix)),
            u"org.example.CertificateTest"_s, QString(), options };
    }

    void verifyResponse(const QDBusMessage &reply, uint response, const QString &reason = { })
    {
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.signature(), u"ua{sv}"_s);
        QCOMPARE(reply.arguments().size(), 2);
        QCOMPARE(reply.arguments()[0].metaType(), QMetaType::fromType<uint>());
        QCOMPARE(reply.arguments()[0].toUInt(), response);
        const auto results = qdbus_cast<QVariantMap>(reply.arguments()[1]);
        if (reason.isEmpty()) {
            QVERIFY(results.isEmpty());
        } else {
            QCOMPARE(results, QVariantMap({ { u"reason"_s, reason } }));
        }
    }

    void installPortal(const char *const *modules, bool selectFirst = false, bool useNativePin = false)
    {
        backend->unregisterObject(u"/org/freedesktop/portal/desktop"_s);
        delete object; // Drain the previous SDK context before creating another.
        chooseFirst = selectFirst;
        nativePin = useNativePin;
        auto ui = certificateTestUi();
        ui.user = this;
        ui.chooser_show = [](void *user, XccUiId id, const XccChooserView *view) {
            auto test = static_cast<CertificateAdaptorTest *>(user);
            ++test->shown;
            if (test->chooseFirst) {
                const auto row = xcc_chooser_view_candidate(view, 0);
                QVERIFY(row);
                xcc_choose(testContext, id, view->revision, row->id);
            } else {
                QCOMPARE(view->n_candidates, size_t(0));
            }
        };
        ui.chooser_update = ui.chooser_show;
        ui.pin_show = [](void *user, XccUiId id, const XccPinView *view) {
            auto test = static_cast<CertificateAdaptorTest *>(user);
            ++test->pinPrompts;
            if (test->nativePin) {
                test->nativeUi.attach(testContext);
                auto native = test->nativeUi.table();
                native.pin_show(native.user, id, view);
            } else {
                xcc_ui_failed(testContext, id, XCC_ERROR_DEVICE);
            }
        };
        ui.pin_update = [](void *user, XccUiId id, const XccPinView *view) {
            auto test = static_cast<CertificateAdaptorTest *>(user);
            if (test->nativePin) {
                auto native = test->nativeUi.table();
                native.pin_update(native.user, id, view);
            }
        };
        ui.warning = [](void *user, XccUiId id, const XccWarning *warning) {
            auto test = static_cast<CertificateAdaptorTest *>(user);
            auto native = test->nativeUi.table();
            native.warning(native.user, id, warning);
        };
        ui.hide = [](void *user, XccUiId id) {
            auto native = static_cast<CertificateAdaptorTest *>(user)->nativeUi.table();
            native.hide(native.user, id);
        };
        ui.close = [](void *user, XccUiId id) {
            auto test = static_cast<CertificateAdaptorTest *>(user);
            ++test->closed;
            auto native = test->nativeUi.table();
            native.close(native.user, id);
        };
        XccConfig config{ };
        config.struct_size = sizeof(config);
        config.abi_version = XCC_ABI_VERSION;
        config.module_paths = modules;
        config.allow_software = selectFirst;
        config.has_display = true;
        object = new QObject;
        portal = new CertificatePortal(object, *backend, config, &ui);
        QVERIFY(portal->isValid());
        QVERIFY(backend->registerObject(u"/org/freedesktop/portal/desktop"_s, object, QDBusConnection::ExportAdaptors));
    }

    // Unwrap containers for deep equality; keep scalar types for explicit checks.
    QVariantMap acquisitionResults(const QDBusMessage &reply)
    {
        auto result = qdbus_cast<QVariantMap>(reply.arguments().value(1));
        result[u"token_display"_s] = qdbus_cast<QVariantMap>(result.value(u"token_display"_s));
        result[u"chain_der"_s] = QVariant::fromValue(qdbus_cast<QList<QByteArray>>(result.value(u"chain_der"_s)));
        for (auto key : { u"supported_mechanisms"_s, u"permitted_operations"_s }) {
            result[key] = qdbus_cast<QStringList>(result.value(key));
        }
        return result;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(certificateTestBusHasNoActivation(QDBusConnection::sessionBus()), "Run through certificate-test-session.sh");
        const auto executable = QStandardPaths::findExecutable(u"dbus-daemon"_s);
        if (executable.isEmpty()) {
            QSKIP("dbus-daemon is unavailable");
        }
        daemon.setProgram(executable);
        daemon.setArguments({ u"--config-file="_s + QString::fromUtf8(CERTIFICATE_TEST_BUS_CONFIG), u"--nofork"_s, u"--print-address=1"_s });
        daemon.start();
        if (!daemon.waitForStarted(3000) || !daemon.waitForReadyRead(3000)) {
            QSKIP("Cannot start a private session bus");
        }
        const auto address = QString::fromUtf8(daemon.readLine()).trimmed();
        busAddress = address;
        backend.emplace(QDBusConnection::connectToBus(address, u"certificate-test-backend"_s));
        frontend.emplace(QDBusConnection::connectToBus(address, u"certificate-test-frontend"_s));
        stranger.emplace(QDBusConnection::connectToBus(address, u"certificate-test-stranger"_s));
        successor.emplace(QDBusConnection::connectToBus(address, u"certificate-test-successor"_s));
        if (!backend->isConnected() || !frontend->isConnected() || !stranger->isConnected() || !successor->isConnected()) {
            QSKIP("Cannot connect to the private session bus");
        }
        QVERIFY(certificateTestBusHasNoActivation(*backend));
        QVERIFY(frontend->registerService(u"org.freedesktop.portal.Desktop"_s));
        const char *modules[] = { nullptr };
        installPortal(modules);
    }

    void capabilitiesAndAuthorization()
    {
        const auto reply = wait(call(*frontend, u"GetCapabilities"_s, { QString(), QVariantMap{ } }));
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.signature(), u"a{sv}"_s);
        QCOMPARE(reply.arguments().size(), 1);
        const auto caps = qdbus_cast<QVariantMap>(reply.arguments()[0]);
        QCOMPARE(qdbus_cast<QStringList>(caps.value(u"purposes"_s)), QStringList({ u"client_auth"_s, u"signing"_s, u"email"_s, u"ssh"_s }));
        QCOMPARE(qdbus_cast<QStringList>(caps.value(u"operations"_s)), QStringList({ u"sign"_s }));
        QCOMPARE(qdbus_cast<QStringList>(caps.value(u"mechanisms"_s)), QStringList({ u"RSA_PKCS1_V1_5"_s, u"RSA_PSS"_s, u"RSA_OAEP"_s, u"ECDSA"_s }));
        QCOMPARE(caps.value(u"has_display"_s), QVariant(true));
        QCOMPARE(caps.value(u"protected_authentication_path"_s), QVariant(true));
        QCOMPARE(caps.size(), 5);
        const auto denied = wait(call(*stranger, u"AcquireCredential"_s, acquireArgs(u"unknown"_s)));
        QCOMPARE(denied.errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QCOMPARE(wait(call(*stranger, u"CreateSession"_s, createArgs(u"unknown"_s))).errorName(), denied.errorName());
        QCOMPARE(wait(call(*stranger, u"GetCapabilities"_s, { QString(), QVariantMap{ } })).errorName(), denied.errorName());
        QCOMPARE(wait(call(*stranger, u"Sign"_s, acquireArgs(u"unknown"_s))).errorName(), denied.errorName());
    }

    void requestCloseCancels()
    {
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"cancel"_s))), 0);
        const auto before = shown;
        auto acquisition = call(*frontend, u"AcquireCredential"_s, acquireArgs(u"cancel"_s));
        QTRY_COMPARE_WITH_TIMEOUT(shown, before + 1, 10000);
        const auto requestInterface = u"org.freedesktop.impl.portal.Request"_s;
        QCOMPARE(
            wait(call(*stranger, u"Close"_s, { }, u"/request/acquire_cancel"_s, requestInterface)).errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QCOMPARE(wait(call(*frontend, u"Close"_s, { }, u"/request/acquire_cancel"_s, requestInterface)).type(), QDBusMessage::ReplyMessage);
        verifyResponse(wait(acquisition), 1);
        QTRY_VERIFY(closed > 0);
        // The request has been removed before its method returns.
        QCOMPARE(wait(call(*frontend, u"Close"_s, { }, u"/request/acquire_cancel"_s, requestInterface)).type(), QDBusMessage::ErrorMessage);
    }

    void sessionCloseCloses()
    {
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"close"_s))), 0);
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"close"_s))), 2, u"invalid_request"_s);
        const auto before = shown;
        auto acquisition = call(*frontend, u"AcquireCredential"_s, acquireArgs(u"close"_s));
        QTRY_COMPARE_WITH_TIMEOUT(shown, before + 1, 10000);
        const auto sessionInterface = u"org.freedesktop.impl.portal.Session"_s;
        QCOMPARE(wait(call(*stranger, u"Close"_s, { }, u"/session/close"_s, sessionInterface)).errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        QCOMPARE(wait(call(*frontend, u"Close"_s, { }, u"/session/close"_s, sessionInterface)).type(), QDBusMessage::ReplyMessage);
        verifyResponse(wait(acquisition), 2, u"no_such_session"_s);
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"close"_s))), 2, u"no_such_session"_s);
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"close"_s))), 0);
    }

    void strictOptions()
    {
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"options"_s))), 0);
        auto options = QVariantMap{ { u"purpose"_s, u"client_auth"_s }, { u"lifetime"_s, QStringLiteral("300") } };
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"options"_s, options))), 2, u"invalid_request"_s);
        options = { { u"purpose"_s, u"client_auth"_s }, { u"operation_policy"_s, QVariantMap{ { u"decrypt"_s, true } } } };
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"options"_s, options))), 2, u"invalid_request"_s);
        options = { { u"purpose"_s, u"client_auth"_s }, { u"certificate_filter"_s, QVariantMap{ { u"issuers"_s, QStringList{ } } } } };
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"options"_s, options))), 2, u"invalid_filter"_s);
        options = { { u"purpose"_s, u"bad"_s } };
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"options"_s, options))), 2, u"invalid_purpose"_s);
        options = { { u"purpose"_s, u"client_auth"_s }, { u"interaction_mode"_s, u"forbidden"_s }, { u"future_option"_s, true } };
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"options"_s, options))), 2, u"interaction_required"_s);
        auto wrongApp = acquireArgs(u"options"_s);
        wrongApp[2] = u"org.example.Other"_s;
        verifyResponse(wait(call(*frontend, u"AcquireCredential"_s, wrongApp)), 2, u"no_such_session"_s);
        auto sign = acquireArgs(u"options"_s);
        sign[4] = QVariantMap{ { u"mechanism"_s, u"RSA_PKCS1_V1_5"_s }, { u"data"_s, QByteArray(32, '\0') },
            { u"parameters"_s, QVariantMap{ { u"unknown"_s, true } } } };
        // The reference checks the missing grant before malformed nested Sign
        // parameters; keep that precedence without its private SDK entry point.
        verifyResponse(wait(call(*frontend, u"Sign"_s, sign)), 2, u"device_error"_s);
        sign[4] = QVariantMap{ { u"mechanism"_s, u"RSA_PKCS1_V1_5"_s }, { u"data"_s, QString() } };
        verifyResponse(wait(call(*frontend, u"Sign"_s, sign)), 2, u"invalid_request"_s);
    }

    void successfulAcquisitionAndGrantedMalformedSign_data()
    {
        QTest::addColumn<uint>("infoSize");
        QTest::newRow("full-sdk") << uint(UINT32_MAX);
        QTest::newRow("original-prefix") << uint(offsetof(XccCandidateInfo, token_label));
        QTest::newRow("partial-extension") << uint(offsetof(XccCandidateInfo, token_reader));
    }

    void successfulAcquisitionAndGrantedMalformedSign()
    {
        QFETCH(uint, infoSize);
        const auto directory = qEnvironmentVariable("SOFTHSM_FIXTURE_DIR", QDir::tempPath() + u"/xdp-certificate-softhsm"_s);
        auto module = qgetenv("SOFTHSM_MODULE");
        if (module.isEmpty()) {
            QFile file(directory + u"/module-path"_s);
            if (file.open(QIODevice::ReadOnly)) {
                module = file.readAll().trimmed();
            }
        }
        const auto config = directory + u"/softhsm2.conf"_s;
        if (module.isEmpty() || !QFile::exists(QString::fromUtf8(module)) || !QFile::exists(config)) {
            QSKIP("No SoftHSM fixture; run autotests/certificate-softhsm-fixture.sh");
        }
        const auto referenceExecutable = qEnvironmentVariable("XDP_CERTIFICATE_REFERENCE_ADAPTER");
        if (referenceExecutable.isEmpty() || !QFile::exists(referenceExecutable)) {
            QSKIP("Set XDP_CERTIFICATE_REFERENCE_ADAPTER to the certificate repo's build/tests/certificate-reference-adapter");
        }
        QFile certificate(directory + u"/portal-test-rsa.der"_s);
        QVERIFY(certificate.open(QIODevice::ReadOnly));
        const auto expectedDer = certificate.readAll();
        QVERIFY(!expectedDer.isEmpty());
        const auto oldConfig = qgetenv("SOFTHSM2_CONF");
        const auto oldModule = qgetenv("SOFTHSM_MODULE");
        const auto restore = qScopeGuard([this, oldConfig, oldModule] {
            candidateInfoLimit = UINT32_MAX;
            const char *empty[] = { nullptr };
            installPortal(empty);
            if (oldConfig.isNull()) {
                qunsetenv("SOFTHSM2_CONF");
            } else {
                qputenv("SOFTHSM2_CONF", oldConfig);
            }
            if (oldModule.isNull()) {
                qunsetenv("SOFTHSM_MODULE");
            } else {
                qputenv("SOFTHSM_MODULE", oldModule);
            }
        });
        qputenv("SOFTHSM2_CONF", config.toUtf8());
        qputenv("SOFTHSM_MODULE", module);
        const char *modules[] = { module.constData(), nullptr };
        installPortal(modules, true);
        QVERIFY(portal->isValid());
        candidateInfoLimit = infoSize;
        pinPrompts = 0;

        QProcess reference;
        const auto stopReference = qScopeGuard([&reference] {
            if (reference.state() != QProcess::NotRunning) {
                reference.terminate();
                if (!reference.waitForFinished(5000)) {
                    reference.kill();
                    reference.waitForFinished(3000);
                }
            }
        });
        reference.start(referenceExecutable, { busAddress });
        QVERIFY2(reference.waitForStarted(3000), qPrintable(reference.errorString()));
        QVERIFY2(reference.waitForReadyRead(10000), reference.readAllStandardError().constData());
        const auto referenceService = QString::fromUtf8(reference.readLine()).trimmed();
        QVERIFY(referenceService.startsWith(u':'));
        const auto invoke = [this](const QString &service, const QString &method, const QVariantList &args) {
            auto message = QDBusMessage::createMethodCall(service, u"/org/freedesktop/portal/desktop"_s,
                u"org.freedesktop.impl.portal.experimental.Certificate1"_s, method);
            message.setArguments(args);
            return wait(frontend->asyncCall(message, 10000));
        };
        const auto referenceCreate = invoke(referenceService, u"CreateSession"_s, createArgs(u"success"_s));
        verifyResponse(referenceCreate, 0);
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"success"_s))), 0);
        const QVariantMap options{ { u"purpose"_s, u"client_auth"_s },
            { u"certificate_filter"_s, QVariantMap{ { u"key_algorithms"_s, QStringList{ u"RSA"_s } } } } };
        const auto args = acquireArgs(u"success"_s, options);
        const auto expectedReply = invoke(referenceService, u"AcquireCredential"_s, args);
        const auto actualReply = wait(call(*frontend, u"AcquireCredential"_s, args));
        for (const auto &reply : { expectedReply, actualReply }) {
            QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
            QCOMPARE(reply.signature(), u"ua{sv}"_s);
            QCOMPARE(reply.arguments().size(), 2);
            QCOMPARE(reply.arguments()[0].metaType(), QMetaType::fromType<uint>());
            QCOMPARE(reply.arguments()[0], QVariant(uint(0)));
        }
        auto expected = acquisitionResults(expectedReply);
        const auto actual = acquisitionResults(actualReply);
        const auto tokenDisplay = qdbus_cast<QVariantMap>(expected.value(u"token_display"_s));
        QCOMPARE(tokenDisplay.keys(), QStringList({ u"label"_s, u"manufacturer"_s, u"model"_s, u"protected_authentication_path"_s, u"reader"_s }));
        QVERIFY(!tokenDisplay.value(u"label"_s).toString().isEmpty());
        QVERIFY(!tokenDisplay.value(u"manufacturer"_s).toString().isEmpty());
        QVERIFY(!tokenDisplay.value(u"model"_s).toString().isEmpty());
        QCOMPARE(tokenDisplay.value(u"protected_authentication_path"_s), QVariant(false));
        if (infoSize != UINT32_MAX) {
            QCOMPARE(legacyTokenDisplay.size(), 5);
            expected[u"token_display"_s] = legacyTokenDisplay;
        }
        QCOMPARE(actual, expected); // Every key and value, including optional keys.
        QCOMPARE(actual.size(), 9);
        for (auto it = actual.cbegin(); it != actual.cend(); ++it) {
            QCOMPARE(it->metaType(), expected.value(it.key()).metaType());
        }
        const auto actualDisplay = qdbus_cast<QVariantMap>(actual.value(u"token_display"_s));
        QCOMPARE(actualDisplay.size(), 5);
        for (auto it = actualDisplay.cbegin(); it != actualDisplay.cend(); ++it) {
            QCOMPARE(it->metaType(), it.key() == u"protected_authentication_path"_s ? QMetaType::fromType<bool>() : QMetaType::fromType<QString>());
        }
        QCOMPARE(actual.value(u"certificate_der"_s), QVariant(expectedDer));
        QCOMPARE(qdbus_cast<QList<QByteArray>>(actual.value(u"chain_der"_s)), QList<QByteArray>{ });
        QCOMPARE(actual.value(u"chain_status"_s), QVariant(u"leaf_only"_s));
        QCOMPARE(actual.value(u"key_type"_s), QVariant(u"RSA"_s));
        QCOMPARE(actual.value(u"key_size"_s), QVariant(uint(2048)));
        QCOMPARE(actual.value(u"key_size"_s).metaType(), QMetaType::fromType<uint>());
        QCOMPARE(qdbus_cast<QStringList>(actual.value(u"supported_mechanisms"_s)), QStringList({ u"RSA_PKCS1_V1_5"_s, u"RSA_PSS"_s, u"RSA_OAEP"_s }));
        QCOMPARE(qdbus_cast<QStringList>(actual.value(u"permitted_operations"_s)), QStringList({ u"sign"_s }));
        QCOMPARE(actual.value(u"may_prompt_later"_s), QVariant(true));

        auto signArgs = acquireArgs(u"success"_s);
        signArgs[0] = QVariant::fromValue(QDBusObjectPath(u"/request/sign_success"_s));
        signArgs[4] = QVariantMap{ { u"mechanism"_s, u"RSA_PKCS1_V1_5"_s }, { u"data"_s, QByteArray(32, '\0') },
            { u"parameters"_s, QVariantMap{ { u"unknown"_s, true } } } };
        const auto expectedSign = invoke(referenceService, u"Sign"_s, signArgs);
        const auto actualSign = wait(call(*frontend, u"Sign"_s, signArgs));
        for (const auto &reply : { expectedSign, actualSign }) {
            QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
            QCOMPARE(reply.signature(), u"ua{sv}"_s);
            QCOMPARE(reply.arguments().size(), 2);
        }
        verifyResponse(expectedSign, 2, u"invalid_request"_s);
        verifyResponse(actualSign, 2, u"invalid_request"_s);
        QCOMPARE(actualSign.arguments()[0], expectedSign.arguments()[0]);
        QCOMPARE(qdbus_cast<QVariantMap>(actualSign.arguments()[1]), qdbus_cast<QVariantMap>(expectedSign.arguments()[1]));
        QCOMPARE(pinPrompts, 0);
        reference.terminate();
        QVERIFY(reference.waitForFinished(10000));
        QCOMPARE(reference.exitStatus(), QProcess::NormalExit);
        QCOMPARE(reference.exitCode(), 0);
    }

    void grantedSignWithNativePin()
    {
        const auto directory = qEnvironmentVariable("SOFTHSM_FIXTURE_DIR");
        const auto module = qgetenv("SOFTHSM_MODULE");
        const auto openssl = QStandardPaths::findExecutable(u"openssl"_s);
        if (directory.isEmpty() || module.isEmpty() || openssl.isEmpty()) {
            QSKIP("Run certificate-softhsm-fixture.sh with SoftHSM and openssl available");
        }
        const auto restore = qScopeGuard([this] {
            const char *empty[] = {nullptr};
            installPortal(empty);
        });
        const char *modules[] = {module.constData(), nullptr};
        installPortal(modules, true, true);
        QVERIFY(portal->isValid());
        pinPrompts = 0;
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"pinsign"_s))), 0);
        const auto acquired = wait(call(*frontend, u"AcquireCredential"_s, acquireArgs(u"pinsign"_s)));
        QCOMPARE(acquired.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(acquired.arguments()[0], QVariant(uint(0)));
        const auto result = acquisitionResults(acquired);
        const auto certificate = result.value(u"certificate_der"_s).toByteArray();
        QVERIFY(!certificate.isEmpty());
        QCOMPARE(pinPrompts, 0); // Acquisition grants access, never logs in.
        const auto digest = QCryptographicHash::hash("KDE native PIN signing test", QCryptographicHash::Sha256);
        auto args = acquireArgs(u"pinsign"_s);
        args[0] = QVariant::fromValue(QDBusObjectPath(u"/request/sign_pinsign"_s));
        args[4] = QVariantMap{
            {u"mechanism"_s, u"RSA_PKCS1_V1_5"_s}, {u"data"_s, digest},
            {u"parameters"_s, QVariantMap{{u"hash"_s, u"SHA256"_s}}},
        };
        auto signing = call(*frontend, u"Sign"_s, args);
        QTRY_COMPARE_WITH_TIMEOUT(pinPrompts, 1, 10000);
        auto prompt = nativeUi.findChild<CertificatePinPrompt *>();
        QVERIFY(prompt);
        QTRY_VERIFY(prompt->view().value(u"ready"_s).toBool());
        QQuickWindow *dialog = nullptr;
        for (auto window : QGuiApplication::allWindows()) {
            if (auto quick = qobject_cast<QQuickWindow *>(window); quick && quick->isVisible()) {
                dialog = quick;
                break;
            }
        }
        QVERIFY(dialog);
        auto field = dialog->findChild<CertificatePinField *>();
        QVERIFY(field);
        QKeyEvent wrong(QEvent::KeyPress, Qt::Key_unknown, {}, u"000000"_s);
        QCoreApplication::sendEvent(field, &wrong);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        QTRY_VERIFY_WITH_TIMEOUT(!prompt->view().value(u"retry"_s).toString().isEmpty(), 10000);
        QVERIFY(!prompt->view().value(u"busy"_s).toBool());
        QCOMPARE(field->maskedCount(), 0);
        QKeyEvent right(QEvent::KeyPress, Qt::Key_unknown, {}, u"123456"_s);
        QCoreApplication::sendEvent(field, &right);
        QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        const bool alwaysAuth = qEnvironmentVariableIntValue("CERTIFICATE_TEST_ALWAYS_AUTH") == 1;
        if (alwaysAuth) {
            QTRY_COMPARE_WITH_TIMEOUT(pinPrompts, 2, 10000);
            prompt = nativeUi.findChild<CertificatePinPrompt *>();
            QVERIFY(prompt);
            QTRY_VERIFY(prompt->view().value(u"ready"_s).toBool());
            QVERIFY(prompt->view().value(u"contextSpecific"_s).toBool());
            dialog = nullptr;
            for (auto window : QGuiApplication::allWindows()) {
                if (auto quick = qobject_cast<QQuickWindow *>(window); quick && quick->isVisible()) {
                    dialog = quick;
                    break;
                }
            }
            QVERIFY(dialog);
            field = dialog->findChild<CertificatePinField *>();
            QVERIFY(field);
            QCoreApplication::sendEvent(field, &right);
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        }
        const auto signedReply = wait(signing);
        QCOMPARE(signedReply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(signedReply.arguments()[0], QVariant(uint(0)));
        const auto signature = qdbus_cast<QVariantMap>(signedReply.arguments()[1]).value(u"signature"_s).toByteArray();
        QVERIFY(!signature.isEmpty());
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto write = [&temporary](const QString &name, const QByteArray &data) {
            QFile file(temporary.filePath(name));
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(u"certificate.der"_s, certificate));
        QVERIFY(write(u"digest"_s, digest));
        QVERIFY(write(u"signature"_s, signature));
        QProcess extract;
        extract.start(openssl, {u"x509"_s, u"-inform"_s, u"DER"_s, u"-in"_s, temporary.filePath(u"certificate.der"_s), u"-pubkey"_s, u"-noout"_s});
        QVERIFY(extract.waitForFinished(5000));
        QCOMPARE(extract.exitCode(), 0);
        QVERIFY(write(u"public.pem"_s, extract.readAllStandardOutput()));
        QProcess verify;
        verify.start(openssl, {u"pkeyutl"_s, u"-verify"_s, u"-pubin"_s, u"-inkey"_s, temporary.filePath(u"public.pem"_s),
            u"-sigfile"_s, temporary.filePath(u"signature"_s), u"-in"_s, temporary.filePath(u"digest"_s),
            u"-pkeyopt"_s, u"digest:sha256"_s, u"-pkeyopt"_s, u"rsa_padding_mode:pkcs1"_s});
        QVERIFY(verify.waitForFinished(5000));
        QCOMPARE(verify.exitStatus(), QProcess::NormalExit);
        QCOMPARE(verify.exitCode(), 0);
        QCOMPARE(pinPrompts, alwaysAuth ? 2 : 1);
        // A second Sign reuses only USER login. An always-authenticate key must
        // request a fresh CONTEXT_SPECIFIC PIN after operation initialization.
        args[0] = QVariant::fromValue(QDBusObjectPath(u"/request/sign_pinsign_again"_s));
        auto second = call(*frontend, u"Sign"_s, args);
        if (alwaysAuth) {
            QTRY_COMPARE_WITH_TIMEOUT(pinPrompts, 3, 10000);
            prompt = nativeUi.findChild<CertificatePinPrompt *>();
            QVERIFY(prompt);
            QTRY_VERIFY(prompt->view().value(u"ready"_s).toBool());
            QVERIFY(prompt->view().value(u"contextSpecific"_s).toBool());
            dialog = nullptr;
            for (auto window : QGuiApplication::allWindows()) {
                if (auto quick = qobject_cast<QQuickWindow *>(window); quick && quick->isVisible()) {
                    dialog = quick;
                    break;
                }
            }
            QVERIFY(dialog);
            field = dialog->findChild<CertificatePinField *>();
            QVERIFY(field);
            QCoreApplication::sendEvent(field, &right);
            QVERIFY(QMetaObject::invokeMethod(dialog, "accept"));
        }
        const auto secondReply = wait(second);
        QCOMPARE(secondReply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(secondReply.arguments()[0], QVariant(uint(0)));
        // PKCS#1 v1.5 is deterministic for this digest; both signatures verify.
        QCOMPARE(qdbus_cast<QVariantMap>(secondReply.arguments()[1]).value(u"signature"_s).toByteArray(), signature);
        QCOMPARE(pinPrompts, alwaysAuth ? 3 : 1);
    }

    void frontendOwnerChangeDropsSessions()
    {
        verifyResponse(wait(call(*frontend, u"CreateSession"_s, createArgs(u"owner"_s))), 0);
        const auto before = shown;
        auto acquisition = call(*frontend, u"AcquireCredential"_s, acquireArgs(u"owner"_s));
        QTRY_COMPARE_WITH_TIMEOUT(shown, before + 1, 10000);
        QVERIFY(frontend->unregisterService(u"org.freedesktop.portal.Desktop"_s));
        QVERIFY(successor->registerService(u"org.freedesktop.portal.Desktop"_s));
        verifyResponse(wait(acquisition), 2, u"owner_gone"_s);
        // A successor's first call may race the owner watch, as in the reference.
        QTRY_COMPARE(wait(call(*successor, u"GetCapabilities"_s, { QString(), QVariantMap{ } })).type(), QDBusMessage::ReplyMessage);
        verifyResponse(wait(call(*successor, u"AcquireCredential"_s, acquireArgs(u"owner"_s))), 2, u"no_such_session"_s);
        QCOMPARE(wait(call(*frontend, u"GetCapabilities"_s, { QString(), QVariantMap{ } })).errorName(), u"org.freedesktop.DBus.Error.AccessDenied"_s);
        verifyResponse(wait(call(*successor, u"CreateSession"_s, createArgs(u"owner"_s))), 0);
    }

    void cleanupTestCase()
    {
        if (backend) {
            backend->unregisterObject(u"/org/freedesktop/portal/desktop"_s);
        }
        delete object; // Drains the SDK before deleting its Qt dispatcher.
        object = nullptr;
        portal = nullptr;
        backend.reset();
        frontend.reset();
        stranger.reset();
        successor.reset();
        for (auto name : { "certificate-test-backend", "certificate-test-frontend", "certificate-test-stranger", "certificate-test-successor" }) {
            QDBusConnection::disconnectFromBus(QString::fromLatin1(name));
        }
        if (daemon.state() != QProcess::NotRunning) {
            daemon.terminate();
            if (!daemon.waitForFinished(3000)) {
                daemon.kill();
                daemon.waitForFinished(3000);
            }
        }
    }
};

QTEST_MAIN(CertificateAdaptorTest)
#include "certificateadaptortest.moc"
