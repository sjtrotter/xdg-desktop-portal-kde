/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatechoosermodel.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QLocale>
#include <QSet>
#include <utility>

using namespace Qt::StringLiterals;

static QString formatValidity(const XccChooserCandidateView *row)
{
    if (row->struct_size < offsetof(XccChooserCandidateView, validity) + sizeof(row->validity)) {
        return QString::fromUtf8(row->expiry);
    }
    switch (row->validity) {
    case XCC_VALIDITY_NOT_YET_VALID:
        return i18n("Not yet valid");
    case XCC_VALIDITY_VALID:
    case XCC_VALIDITY_EXPIRED: {
        const auto expiry = row->not_after > 0 ? QDateTime::fromSecsSinceEpoch(row->not_after) : QDateTime();
        const auto date = expiry.isValid()
            ? QLocale().toString(expiry.date(), i18nc("@info certificate expiry date format, see QDate::toString", "d MMM yyyy"))
            : i18n("an unknown date");
        return row->validity == XCC_VALIDITY_EXPIRED ? i18n("Expired on %1", date) : i18n("Expires %1", date);
    }
    default:
        return i18n("Unknown validity date");
    }
}

static QString formatKey(const XccChooserCandidateView *row)
{
    if (row->struct_size < offsetof(XccChooserCandidateView, key_size) + sizeof(row->key_size)) {
        return i18n("Unknown key type");
    }
    auto keyType = QString::fromUtf8(row->key_type);
    if (keyType.isEmpty()) {
        keyType = i18n("Unknown key type");
    } else if (keyType == "unsupported"_L1) {
        keyType = i18n("Unsupported key type");
    }
    return i18n("%1 %2-bit", keyType, row->key_size);
}

static QString formatDuration(uint32_t seconds)
{
    if (seconds && seconds % 3600 == 0) {
        return i18np("%1 hour", "%1 hours", seconds / 3600);
    }
    if (seconds >= 60) {
        const auto minutes = i18np("%1 minute", "%1 minutes", seconds / 60);
        return seconds % 60 ? i18n("%1 and %2", minutes, i18np("%1 second", "%1 seconds", seconds % 60)) : minutes;
    }
    return i18np("%1 second", "%1 seconds", seconds);
}

static QString formatBody(const XccChooserView *view)
{
    const auto duration = formatDuration(view->lifetime_seconds);
    QString body;
    if (view->struct_size < offsetof(XccChooserView, generic_purpose) + sizeof(view->generic_purpose)) {
        body = i18n("%1 Access lasts %2.", QString::fromUtf8(view->purpose_detail), duration).trimmed();
    } else if (!view->may_sign && !view->may_decrypt) {
        body = i18n("Access lasts %1. It cannot sign or decrypt with your certificate.", duration);
    } else if (view->generic_purpose) {
        body = view->may_sign && view->may_decrypt ? i18n("It can sign and decrypt data for %1.", duration)
            : view->may_decrypt ? i18n("It can decrypt data for %1.", duration) : i18n("It can sign data for %1.", duration);
    } else if (view->purpose == XCC_PURPOSE_EMAIL) {
        body = view->may_sign && view->may_decrypt ? i18n("It can sign and decrypt email for %1.", duration)
            : view->may_decrypt ? i18n("It can decrypt email for %1.", duration) : i18n("It can sign email for %1.", duration);
    } else if (!view->may_sign) {
        body = i18n("It can decrypt data for %1.", duration);
    } else if (view->purpose == XCC_PURPOSE_CLIENT_AUTH) {
        body = view->may_decrypt ? i18n("It can prove who you are to websites and servers and decrypt data for %1.", duration)
            : i18n("It can prove who you are to websites and servers for %1.", duration);
    } else if (view->purpose == XCC_PURPOSE_SSH) {
        body = view->may_decrypt ? i18n("It can sign in to SSH servers and decrypt data for %1.", duration)
            : i18n("It can sign in to SSH servers for %1.", duration);
    } else {
        body = view->may_decrypt ? i18n("It can sign documents and data and decrypt data for %1.", duration)
            : i18n("It can sign documents and data for %1.", duration);
    }
    return body;
}

CertificateChooserModel::CertificateChooserModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CertificateChooserModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : m_rows.size(); }

QVariant CertificateChooserModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return { };
    }
    const auto &row = m_rows[index.row()];
    switch (role) {
    case Subject:
        return row.subject;
    case Issuer:
        return row.issuer;
    case Token:
        return row.token;
    case Reader:
        return row.reader;
    case Expiry:
        return row.expiry;
    case Detail:
        return row.detail;
    case Fingerprint:
        return row.fingerprint;
    case Enabled:
        return row.enabled;
    case Invalid:
        return row.invalid;
    case TextFormat:
        return int(Qt::PlainText);
    case CandidateId:
        // QML numbers cannot represent every 64-bit candidate identity exactly.
        return QString::number(qulonglong(row.id));
    default:
        return { };
    }
}

QHash<int, QByteArray> CertificateChooserModel::roleNames() const
{
    return { { Subject, "subject" }, { Issuer, "issuer" }, { Token, "token" }, { Reader, "reader" }, { Expiry, "expiry" }, { Detail, "detail" },
        { Fingerprint, "fingerprint" }, { Enabled, "available" }, { Invalid, "invalid" }, { TextFormat, "textFormat" }, { CandidateId, "candidateId" } };
}

Qt::ItemFlags CertificateChooserModel::flags(const QModelIndex &index) const
{
    return data(index, Enabled).toBool() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

bool CertificateChooserModel::update(const XccChooserView *view)
{
    if (!view || view->struct_size < XCC_CHOOSER_VIEW_V1_SIZE || view->abi_version != XCC_ABI_VERSION) {
        return false;
    }
    QList<Row> rows;
    QSet<uint64_t> ids;
    for (size_t i = 0; i < view->n_candidates; ++i) {
        const auto *row = xcc_chooser_view_candidate(view, i);
        if (!row || row->struct_size < XCC_CHOOSER_CANDIDATE_VIEW_V1_SIZE || row->abi_version != XCC_ABI_VERSION || !row->id || ids.contains(row->id)) {
            return false;
        }
        ids.insert(row->id);
        const auto displayFlags = row->struct_size >= offsetof(XccChooserCandidateView, display_flags) + sizeof(row->display_flags) ? row->display_flags : 0;
        rows.append({ row->id, displayFlags & XCC_DISPLAY_SUBJECT_FALLBACK ? i18n("Unnamed certificate") : QString::fromUtf8(row->subject),
            displayFlags & XCC_DISPLAY_ISSUER_FALLBACK ? i18n("Unnamed issuer") : QString::fromUtf8(row->issuer),
            displayFlags & XCC_DISPLAY_TOKEN_FALLBACK ? i18n("Unnamed token") : QString::fromUtf8(row->token), QString::fromUtf8(row->reader),
            formatValidity(row), formatKey(row), QString::fromUtf8(row->fingerprint), true, row->invalid });
    }
    const auto announcement = rows.isEmpty()
        ? (view->tokens_present ? i18n("No usable certificates on this token. Try a different security token.")
                                : i18n("Insert your security token. Certificates on it will appear here."))
        : i18n("Certificates available on your security tokens");
    // Compare identity sets, not counts or row positions: replacement can add a
    // candidate without changing the count, while reordering adds nothing.
    const bool announcementChanged = announcement != m_announcement || !(ids - m_candidateIds).isEmpty();
    if (announcementChanged) {
        m_announcementPending = true;
    }
    m_announcement = announcement;
    m_candidateIds = ids;
    // Let the UI capture availability while the previous snapshot is intact.
    Q_EMIT snapshotAboutToChange();
    // Disable removed rows before notifying views of structural changes. Consent
    // and selection always check availability against this current snapshot.
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!ids.contains(m_rows[i].id)) {
            m_rows[i].enabled = false;
            Q_EMIT dataChanged(index(i), index(i), { Enabled });
        }
    }
    if (!ids.contains(m_selected)) {
        m_selected = 0;
    }
    beginResetModel();
    m_rows = std::move(rows);
    m_revision = view->revision;
    const bool unknown = view->identity_level == XCC_IDENTITY_UNKNOWN || !view->caller_name || !*view->caller_name
        || (view->struct_size >= offsetof(XccChooserView, display_flags) + sizeof(view->display_flags)
            && (view->display_flags & XCC_DISPLAY_CALLER_FALLBACK));
    const auto caller = unknown ? i18n("An unknown app") : QString::fromUtf8(view->caller_name);
    const bool generic = view->struct_size >= offsetof(XccChooserView, generic_purpose) + sizeof(view->generic_purpose) && view->generic_purpose;
    QString heading;
    if (generic) {
        heading = i18n("%1 wants to use a certificate", caller);
    } else {
        switch (view->purpose) {
        case XCC_PURPOSE_CLIENT_AUTH:
            heading = i18n("%1 wants to identify you with a certificate", caller);
            break;
        case XCC_PURPOSE_SIGNING:
            heading = i18n("%1 wants to sign with your certificate", caller);
            break;
        case XCC_PURPOSE_EMAIL:
            heading = i18n("%1 wants to use your email certificate", caller);
            break;
        case XCC_PURPOSE_SSH:
            heading = i18n("%1 wants to use your certificate for SSH", caller);
            break;
        default:
            heading = i18n("%1 wants to use a certificate", caller);
        }
    }
    const auto identity = unknown ? i18n("This app could not be identified. Continue only if you started this.")
        : view->identity_level == XCC_IDENTITY_DERIVED_HOST ? i18n("The app name is reported by the app.") : QString();
    QString reason;
    if (view->untrusted_reason && *view->untrusted_reason) {
        reason = unknown ? i18n("The app says “%1”", QString::fromUtf8(view->untrusted_reason))
            : i18n("%1 says “%2”", caller, QString::fromUtf8(view->untrusted_reason));
    }
    m_context = { { u"heading"_s, heading }, { u"caller"_s, caller }, { u"body"_s, formatBody(view) },
        { u"appId"_s, unknown ? QString() : QString::fromUtf8(view->app_id) },
        { u"identity"_s, identity }, { u"unknown"_s, unknown }, { u"reason"_s, reason }, { u"tokensPresent"_s, view->tokens_present } };
    endResetModel();
    Q_EMIT selectionChanged();
    Q_EMIT viewChanged();
    if (announcementChanged) {
        Q_EMIT announcementPending();
    }
    return true;
}

QString CertificateChooserModel::takeAnnouncement()
{
    if (!std::exchange(m_announcementPending, false)) {
        return {};
    }
    return m_announcement;
}

void CertificateChooserModel::selectRow(int row)
{
    const auto id = row >= 0 && row < m_rows.size() && m_rows[row].enabled ? m_rows[row].id : 0;
    if (m_selected != id) {
        m_selected = id;
        Q_EMIT selectionChanged();
    }
}

int CertificateChooserModel::selectedRow() const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].enabled && m_rows[i].id == m_selected) {
            return i;
        }
    }
    return -1;
}

int CertificateChooserModel::rowForCandidateId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].enabled && QString::number(qulonglong(m_rows[i].id)) == id) {
            return i;
        }
    }
    return -1;
}

bool CertificateChooserModel::hasSelection() const { return selectedRow() >= 0; }
uint64_t CertificateChooserModel::selectedId() const { return hasSelection() ? m_selected : 0; }
uint64_t CertificateChooserModel::revision() const { return m_revision; }
QVariantMap CertificateChooserModel::context() const { return m_context; }

#include "moc_certificatechoosermodel.cpp"
