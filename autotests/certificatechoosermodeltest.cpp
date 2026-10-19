/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatechoosermodel.h"

#include <QTest>

class CertificateChooserModelTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void refreshSequence()
    {
        CertificateChooserModel model;
        // Larger producer stride, borrowed strings, and HTML-looking input.
        struct ExtendedRow {
            XccChooserCandidateView row;
            uint64_t appended;
        };
        QByteArray subject("<b>Certificate</b>");
        ExtendedRow rows[2]{ };
        for (int i = 0; i < 2; ++i) {
            rows[i].row.struct_size = sizeof(ExtendedRow);
            rows[i].row.abi_version = XCC_ABI_VERSION;
            rows[i].row.id = i + 1;
            rows[i].row.subject = subject.constData();
            rows[i].row.issuer = "<img src='external'>";
            rows[i].row.token = "Token";
            rows[i].row.reader = "Reader";
            rows[i].row.expiry = "Expiry";
            rows[i].row.detail = "Detail";
            rows[i].row.fingerprint = "Fingerprint";
        }
        rows[1].row.invalid = true; // Date warnings do not disable consent.
        XccChooserView view{ };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.revision = 1;
        view.candidates = &rows[0].row;
        view.n_candidates = 2;
        view.untrusted_reason = "<a href='external'>application reason</a>";
        view.identity_level = XCC_IDENTITY_UNKNOWN;
        view.tokens_present = true;
        QVERIFY(model.update(&view));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Subject).toString(), QString::fromUtf8(subject));
        subject.fill('x'); // The model owns its copy.
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Subject).toString(), QStringLiteral("<b>Certificate</b>"));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::TextFormat).toInt(), int(Qt::PlainText));
        QCOMPARE(model.context().value(QStringLiteral("reason")).toString(), QStringLiteral("<a href='external'>application reason</a>"));
        QVERIFY(model.flags(model.index(1)).testFlag(Qt::ItemIsEnabled));
        model.selectRow(1);
        QCOMPARE(model.selectedId(), uint64_t(2));
        bool staleDisabled = false;
        connect(&model, &QAbstractItemModel::dataChanged, this, [&](const QModelIndex &first, const QModelIndex &, const QList<int> &roles) {
            if (roles.contains(CertificateChooserModel::Enabled)) {
                staleDisabled = !model.data(first, CertificateChooserModel::Enabled).toBool() && !model.flags(first).testFlag(Qt::ItemIsEnabled);
            }
        });
        view.revision = 2;
        view.candidates = &rows[1].row;
        view.n_candidates = 1;
        QVERIFY(model.update(&view));
        QVERIFY(staleDisabled);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.selectedId(), uint64_t(2));
        QCOMPARE(model.selectedRow(), 0);
        QCOMPARE(model.revision(), uint64_t(2));
        rows[0].row.id = 3;
        view.revision = 3;
        view.candidates = &rows[0].row;
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.selectedRow(), 1);
        QCOMPARE(model.selectedId(), uint64_t(2));
        view.revision = 4;
        view.candidates = nullptr;
        view.n_candidates = 0;
        QVERIFY(model.update(&view));
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.hasSelection());
        QCOMPARE(model.selectedId(), uint64_t(0));
        // A newly inserted row at the old index cannot inherit selection.
        view.revision = 5;
        view.candidates = &rows[0].row;
        view.n_candidates = 1;
        QVERIFY(model.update(&view));
        QVERIFY(!model.hasSelection());
        view.abi_version++;
        QVERIFY(!model.update(&view));
        QCOMPARE(model.rowCount(), 1);
    }
};

QTEST_GUILESS_MAIN(CertificateChooserModelTest)
#include "certificatechoosermodeltest.moc"
