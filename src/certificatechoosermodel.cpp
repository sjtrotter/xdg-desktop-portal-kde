/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatechoosermodel.h"

#include <KLocalizedString>
#include <QSet>
#include <utility>

using namespace Qt::StringLiterals;

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
    default:
        return { };
    }
}

QHash<int, QByteArray> CertificateChooserModel::roleNames() const
{
    return { { Subject, "subject" }, { Issuer, "issuer" }, { Token, "token" }, { Reader, "reader" }, { Expiry, "expiry" }, { Detail, "detail" },
        { Fingerprint, "fingerprint" }, { Enabled, "available" }, { Invalid, "invalid" }, { TextFormat, "textFormat" } };
}

Qt::ItemFlags CertificateChooserModel::flags(const QModelIndex &index) const
{
    return data(index, Enabled).toBool() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags;
}

bool CertificateChooserModel::update(const XccChooserView *view)
{
    if (!view || view->struct_size < sizeof(*view) || view->abi_version != XCC_ABI_VERSION) {
        return false;
    }
    QList<Row> rows;
    QSet<uint64_t> ids;
    for (size_t i = 0; i < view->n_candidates; ++i) {
        const auto *row = xcc_chooser_view_candidate(view, i);
        if (!row || row->struct_size < sizeof(*row) || row->abi_version != XCC_ABI_VERSION || !row->id || ids.contains(row->id)) {
            return false;
        }
        ids.insert(row->id);
        rows.append({ row->id, QString::fromUtf8(row->subject), QString::fromUtf8(row->issuer), QString::fromUtf8(row->token), QString::fromUtf8(row->reader),
            QString::fromUtf8(row->expiry), QString::fromUtf8(row->detail), QString::fromUtf8(row->fingerprint), true, row->invalid });
    }
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
    // ABI v1 supplies the module's generic-purpose distinction as trusted
    // backend wording rather than an enum. Recognize that wording for translation;
    // it affects disclosure only, never permissions or identity.
    if (QString::fromUtf8(view->purpose_text) == u"is asking to use a certificate on your security token.") {
        purpose = i18n("Use a certificate on your security token");
        purposeDetail = i18n("The application will be able to use this key until access expires. It cannot read the private key.");
    }
    const auto scope = view->may_sign && view->may_decrypt ? i18n("It allows signing and decryption.")
        : view->may_decrypt                                ? i18n("It allows decryption only.")
        : view->may_sign                                   ? i18n("It allows signing only.")
                                                           : i18n("It allows no operations.");
    const auto appId = QString::fromUtf8(view->app_id);
    m_context = { { u"caller"_s, appId.isEmpty() ? i18n("An unidentified application") : QString::fromUtf8(view->caller_name) }, { u"appId"_s, appId },
        { u"identity"_s, identity }, { u"purpose"_s, purpose }, { u"purposeDetail"_s, purposeDetail }, { u"scope"_s, scope },
        { u"duration"_s, i18np("Access lasts for %1 second.", "Access lasts for %1 seconds.", view->lifetime_seconds) },
        { u"laterPrompt"_s, i18n("You may be asked for your PIN the first time the key is used.") }, { u"reason"_s, QString::fromUtf8(view->untrusted_reason) },
        { u"tokensPresent"_s, view->tokens_present } };
    endResetModel();
    Q_EMIT selectionChanged();
    Q_EMIT viewChanged();
    return true;
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
