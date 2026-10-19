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
        const auto date = expiry.isValid() ? QLocale().toString(expiry.date(), QLocale::ShortFormat) : i18n("an unknown date");
        return row->validity == XCC_VALIDITY_EXPIRED ? i18n("Expired on %1", date) : i18n("Valid until %1", date);
    }
    default:
        return i18n("Unknown validity date");
    }
}

static QString formatKey(const XccChooserCandidateView *row)
{
    if (row->struct_size < offsetof(XccChooserCandidateView, key_size) + sizeof(row->key_size)) {
        return QString::fromUtf8(row->detail);
    }
    auto keyType = QString::fromUtf8(row->key_type);
    if (keyType.isEmpty()) {
        keyType = i18n("Unknown key type");
    } else if (keyType == "unsupported"_L1) {
        keyType = i18n("Unsupported key type");
    }
    return i18n("%1 %2-bit", keyType, row->key_size);
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
        ? (view->tokens_present ? i18n("Insert your security token. No certificate on the available tokens matches this request.")
                                : i18n("Insert your security token"))
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
    QString identity;
    switch (view->identity_level) {
    case XCC_IDENTITY_VERIFIED_SANDBOXED:
        identity = i18n("Sandboxed application, identity verified by the system");
        break;
    case XCC_IDENTITY_DERIVED_HOST:
        identity = i18n("Unsandboxed application. This name is a useful label, not proof of identity.");
        break;
    default:
        identity = i18n("This application could not be identified. Only continue if you know what asked.");
    }
    QString purpose;
    QString purposeDetail;
    switch (view->purpose) {
    case XCC_PURPOSE_CLIENT_AUTH:
        purpose = i18n("Authenticate to a server");
        purposeDetail = i18n("The application will be able to authenticate as you until access expires. It cannot read the private key.");
        break;
    case XCC_PURPOSE_SIGNING:
        purpose = i18n("Sign a document or data");
        purposeDetail = i18n("The application will be able to have data signed with this key until access expires. It cannot read the private key.");
        break;
    case XCC_PURPOSE_EMAIL:
        purpose = i18n("Use secure email");
        purposeDetail = i18n("The application will be able to use this key for secure email until access expires. It cannot read the private key.");
        break;
    case XCC_PURPOSE_SSH:
        purpose = i18n("Authenticate an SSH connection");
        purposeDetail = i18n("The application will be able to authenticate SSH connections as you until access expires. It cannot read the private key.");
        break;
    }
    if (view->struct_size < offsetof(XccChooserView, generic_purpose) + sizeof(view->generic_purpose)) {
        purpose = QString::fromUtf8(view->purpose_text);
        purposeDetail = QString::fromUtf8(view->purpose_detail);
    } else if (view->generic_purpose) {
        purpose = i18n("Use a certificate on your security token");
        purposeDetail = i18n("The application will be able to use this key until access expires. It cannot read the private key.");
    }
    const auto scope = view->may_sign && view->may_decrypt ? i18n("It allows signing and decryption.")
        : view->may_decrypt                                ? i18n("It allows decryption only.")
        : view->may_sign                                   ? i18n("It allows signing only.")
                                                           : i18n("It allows no operations.");
    const auto appId = QString::fromUtf8(view->app_id);
    const bool callerFallback = view->struct_size >= offsetof(XccChooserView, display_flags) + sizeof(view->display_flags)
        && (view->display_flags & XCC_DISPLAY_CALLER_FALLBACK);
    const auto heading = callerFallback || !view->caller_name ? i18n("An unidentified application wants to use a certificate")
        : i18n("%1 wants to use a certificate", QString::fromUtf8(view->caller_name));
    m_context = { { u"heading"_s, heading }, { u"caller"_s, callerFallback || !view->caller_name ? i18n("An unidentified application") : QString::fromUtf8(view->caller_name) }, { u"appId"_s, appId },
        { u"identity"_s, identity }, { u"purpose"_s, purpose }, { u"purposeDetail"_s, purposeDetail }, { u"scope"_s, scope },
        { u"duration"_s, i18np("Access lasts for %1 second.", "Access lasts for %1 seconds.", view->lifetime_seconds) },
        { u"laterPrompt"_s, view->later_prompt && *view->later_prompt ? i18n("After you choose, you will be asked for your security token PIN.") : QString() }, { u"reason"_s, QString::fromUtf8(view->untrusted_reason) },
        { u"tokensPresent"_s, view->tokens_present } };
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

bool CertificateChooserModel::hasSelection() const { return selectedRow() >= 0; }
uint64_t CertificateChooserModel::selectedId() const { return hasSelection() ? m_selected : 0; }
uint64_t CertificateChooserModel::revision() const { return m_revision; }
QVariantMap CertificateChooserModel::context() const { return m_context; }

#include "moc_certificatechoosermodel.cpp"
