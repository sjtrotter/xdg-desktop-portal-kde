/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QVariantMap>
#include <xdp-certificate/ui.h>

class CertificateChooserModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap context READ context NOTIFY viewChanged)
public:
    enum Role { Subject = Qt::UserRole + 1, Issuer, Token, Reader, Expiry, Detail, Fingerprint, Enabled, Invalid, TextFormat, CandidateId };
    explicit CertificateChooserModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = { }) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool update(const XccChooserView *view);
    Q_INVOKABLE void selectRow(int row);
    // The UI consumes pending state only after the chooser is presented.
    Q_INVOKABLE QString takeAnnouncement();
    int selectedRow() const;
    bool hasSelection() const;
    uint64_t selectedId() const;
    uint64_t revision() const;
    QVariantMap context() const;
Q_SIGNALS:
    void snapshotAboutToChange();
    void selectionChanged();
    void viewChanged();
    void announcementPending();

private:
    struct Row {
        uint64_t id;
        QString subject, issuer, token, reader, expiry, detail, fingerprint;
        bool enabled, invalid;
    };
    QList<Row> m_rows;
    uint64_t m_selected = 0, m_revision = 0;
    QVariantMap m_context;
    QSet<uint64_t> m_candidateIds;
    QString m_announcement;
    bool m_announcementPending = false;
};
