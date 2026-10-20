/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatechoosermodel.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QLocale>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>
#include <limits>
#include <utility>

class CertificateChooserModelTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void candidateIdentityRole()
    {
        CertificateChooserModel model;
        XccChooserCandidateView rows[2] { };
        for (int i = 0; i < 2; ++i) {
            rows[i].struct_size = sizeof(rows[i]);
            rows[i].abi_version = XCC_ABI_VERSION;
            rows[i].id = std::numeric_limits<uint64_t>::max() - i;
        }
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.candidates = rows;
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QCOMPARE(model.roleNames().value(CertificateChooserModel::CandidateId), QByteArray("candidateId"));
        const auto firstId = model.data(model.index(0), CertificateChooserModel::CandidateId);
        QCOMPARE(firstId.metaType(), QMetaType::fromType<QString>());
        QCOMPARE(firstId.toString(), QStringLiteral("18446744073709551615"));
        QCOMPARE(model.data(model.index(1), CertificateChooserModel::CandidateId).toString(), QStringLiteral("18446744073709551614"));
        QVERIFY(!model.hasSelection());
        QCOMPARE(model.rowForCandidateId(QStringLiteral("18446744073709551615")), 0);

        // Focus can follow an unselected identity even when the rows reorder.
        std::swap(rows[0], rows[1]);
        ++view.revision;
        QVERIFY(model.update(&view));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::CandidateId).toString(), QStringLiteral("18446744073709551614"));
        QCOMPARE(model.data(model.index(1), CertificateChooserModel::CandidateId), firstId);
        QVERIFY(!model.hasSelection());
        QCOMPARE(model.rowForCandidateId(QStringLiteral("18446744073709551615")), 1);
        QCOMPARE(model.rowForCandidateId(QStringLiteral("missing")), -1);
    }

    void announcements()
    {
        CertificateChooserModel model;
        QSignalSpy pending(&model, &CertificateChooserModel::announcementPending);
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        const auto insertToken = i18n("Insert your security token. Certificates on it will appear here.");
        const auto noMatch = i18n("No usable certificates on this token. Try a different security token.");
        const auto available = i18n("Certificates available on your security tokens");
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 1);
        // Initial state remains pending across snapshots before presentation.
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 1);
        QCOMPARE(model.takeAnnouncement(), insertToken);
        QVERIFY(model.takeAnnouncement().isEmpty()); // A remap cannot repeat it.
        pending.clear();
        ++view.revision;
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 0);
        QVERIFY(model.takeAnnouncement().isEmpty());

        view.tokens_present = true;
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 1);
        QCOMPARE(model.takeAnnouncement(), noMatch);
        QVERIFY(model.update(&view));
        QVERIFY(model.takeAnnouncement().isEmpty());

        XccChooserCandidateView rows[2] { };
        for (int i = 0; i < 2; ++i) {
            rows[i].struct_size = sizeof(rows[i]);
            rows[i].abi_version = XCC_ABI_VERSION;
            rows[i].id = i + 1;
        }
        view.candidates = rows;
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QCOMPARE(model.takeAnnouncement(), available);
        pending.clear();
        QVERIFY(model.update(&view));
        std::swap(rows[0], rows[1]);
        ++view.revision;
        QVERIFY(model.update(&view));
        rows[0].subject = "Updated display text";
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 0);
        QVERIFY(model.takeAnnouncement().isEmpty());

        // A replacement is an arrival even though the count stays at two.
        rows[0].id = 3;
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 1);
        QCOMPARE(model.takeAnnouncement(), available);
        pending.clear();
        view.n_candidates = 1; // A removal alone does not announce availability.
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 0);
        QVERIFY(model.takeAnnouncement().isEmpty());
        view.n_candidates = 2; // Reappearance of the removed ID is an arrival.
        QVERIFY(model.update(&view));
        QCOMPARE(model.takeAnnouncement(), available);

        // Invalid snapshots must not change the deduplication baseline.
        rows[0].id = 4;
        rows[1].id = 4;
        QVERIFY(!model.update(&view));
        QVERIFY(model.takeAnnouncement().isEmpty());
        rows[0].id = 3;
        rows[1].id = 1;
        QVERIFY(model.update(&view));
        QVERIFY(model.takeAnnouncement().isEmpty());

        view.candidates = nullptr;
        view.n_candidates = 0;
        QVERIFY(model.update(&view));
        QCOMPARE(model.takeAnnouncement(), noMatch);
        view.tokens_present = false;
        QVERIFY(model.update(&view));
        QCOMPARE(model.takeAnnouncement(), insertToken);
        // Hidden updates coalesce to the latest state when presentation resumes.
        view.tokens_present = true;
        QVERIFY(model.update(&view));
        view.tokens_present = false;
        QVERIFY(model.update(&view));
        QCOMPARE(model.takeAnnouncement(), insertToken);
        QVERIFY(model.takeAnnouncement().isEmpty());
    }

    void reading()
    {
        CertificateChooserModel model;
        QSignalSpy pending(&model, &CertificateChooserModel::announcementPending);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        QVERIFY(model.update(&view));
        model.takeAnnouncement();
        pending.clear();
        view.tokens_reading = true;
        ++view.revision;
        QVERIFY(model.update(&view));
        QVERIFY(model.context().value(QStringLiteral("tokensReading")).toBool());
        QVERIFY(!model.hasSelection());
        QCOMPARE(pending.count(), 1);
        QCOMPARE(model.takeAnnouncement(), i18n("Reading security token… This can take a few seconds."));
        QVERIFY(model.update(&view));
        QCOMPARE(pending.count(), 1);
        QVERIFY(model.takeAnnouncement().isEmpty());
        view.tokens_reading = false;
        view.tokens_present = true;
        ++view.revision;
        QVERIFY(model.update(&view));
        QVERIFY(!model.context().value(QStringLiteral("tokensReading")).toBool());
        QCOMPARE(model.takeAnnouncement(), i18n("No usable certificates on this token. Try a different security token."));
        view.tokens_reading = true;
        QVERIFY(model.update(&view));
        QCOMPARE(model.takeAnnouncement(), i18n("Reading security token… This can take a few seconds."));
        view.struct_size = offsetof(XccChooserView, tokens_reading);
        QVERIFY(model.update(&view));
        QVERIFY(!model.context().value(QStringLiteral("tokensReading")).toBool());
        QCOMPARE(model.takeAnnouncement(), i18n("No usable certificates on this token. Try a different security token."));
        view.struct_size = XCC_CHOOSER_VIEW_V1_SIZE;
        QVERIFY(model.update(&view));
        QVERIFY(!model.context().value(QStringLiteral("tokensReading")).toBool());
        QVERIFY(model.takeAnnouncement().isEmpty());

        XccChooserCandidateView row { };
        row.struct_size = sizeof(row);
        row.abi_version = XCC_ABI_VERSION;
        row.id = 1;
        view.struct_size = sizeof(view);
        view.tokens_reading = false;
        view.candidates = &row;
        view.n_candidates = 1;
        ++view.revision;
        QVERIFY(model.update(&view));
        model.selectRow(0);
        QCOMPARE(model.takeAnnouncement(), i18n("Certificates available on your security tokens"));
        reset.clear();
        pending.clear();
        QSignalSpy snapshot(&model, &CertificateChooserModel::snapshotAboutToChange);
        QSignalSpy selection(&model, &CertificateChooserModel::selectionChanged);
        const QPersistentModelIndex retained(model.index(0));
        XccChooserCandidateView refreshed = row;
        int transition = 0;
        for (const bool reading : { true, false, true, false }) {
            // Reading on/off, then a new snapshot completing the next read.
            view.tokens_reading = reading;
            ++view.revision;
            if (++transition == 4) {
                view.candidates = &refreshed;
            }
            QVERIFY(model.update(&view));
            QCOMPARE(model.revision(), view.revision);
            QCOMPARE(model.context().value(QStringLiteral("tokensReading")).toBool(), reading);
            QCOMPARE(reset.count(), 0);
            QCOMPARE(snapshot.count(), 0);
            QCOMPARE(selection.count(), 0);
            QVERIFY(retained.isValid());
            QCOMPARE(pending.count(), 0);
            QCOMPARE(model.rowCount(), 1);
            QCOMPARE(model.selectedId(), uint64_t(1));
            QVERIFY(model.hasSelection());
            QVERIFY(model.takeAnnouncement().isEmpty());
        }

        // Details and identity changes must still reset, even during reading.
        view.tokens_reading = true;
        refreshed.fingerprint = "AB:CD";
        ++view.revision;
        QVERIFY(model.update(&view));
        QCOMPARE(reset.count(), 1);
        QCOMPARE(snapshot.count(), 1);
        QVERIFY(!retained.isValid());
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Fingerprint).toString(), QStringLiteral("AB:CD"));
        QCOMPARE(model.selectedId(), uint64_t(1));
        refreshed.id = 2;
        ++view.revision;
        QVERIFY(model.update(&view));
        QCOMPARE(reset.count(), 2);
        QCOMPARE(model.selectedId(), uint64_t(0));
        QCOMPARE(model.rowForCandidateId(QStringLiteral("2")), 0);
    }

    void structuredFields()
    {
        const auto previousLocale = QLocale();
        struct RestoreLocale {
            QLocale locale;
            ~RestoreLocale() { QLocale::setDefault(locale); }
        } restore { previousLocale };
        XccChooserCandidateView row { };
        row.struct_size = sizeof(row);
        row.abi_version = XCC_ABI_VERSION;
        row.id = 1;
        row.expiry = "English expiry must be ignored";
        row.detail = "English detail must be ignored";
        row.not_before = 1704067200;
        row.not_after = 1893456000;
        QByteArray keyType("RSA");
        row.key_type = keyType.constData();
        row.key_size = 2048;
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.candidates = &row;
        view.n_candidates = 1;
        view.generic_purpose = true;
        view.purpose_text = "English purpose must be ignored";
        CertificateChooserModel model;
        for (const auto &locale : { QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::German, QLocale::Germany) }) {
            QLocale::setDefault(locale);
            const auto date = locale.toString(QDateTime::fromSecsSinceEpoch(row.not_after).date(),
                                             i18nc("@info certificate expiry date format, see QDate::toString", "d MMM yyyy"));
            row.validity = XCC_VALIDITY_VALID;
            QVERIFY(model.update(&view));
            QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), i18n("Expires %1", date));
            QCOMPARE(model.data(model.index(0), CertificateChooserModel::Detail).toString(), i18n("%1 %2-bit", QStringLiteral("RSA"), uint32_t(2048)));
            QCOMPARE(model.context().value(QStringLiteral("heading")).toString(), i18n("%1 wants to use a certificate", i18n("An unknown app")));
            row.validity = XCC_VALIDITY_EXPIRED;
            QVERIFY(model.update(&view));
            QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), i18n("Expired on %1", date));
            row.validity = XCC_VALIDITY_NOT_YET_VALID;
            QVERIFY(model.update(&view));
            QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), i18n("Not yet valid"));
            row.validity = XCC_VALIDITY_UNKNOWN;
            QVERIFY(model.update(&view));
            QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), i18n("Unknown validity date"));
        }
        row.validity = XCC_VALIDITY_VALID;
        row.not_after = 0;
        row.key_type = nullptr;
        QVERIFY(model.update(&view));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), i18n("Expires %1", i18n("an unknown date")));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Detail).toString(), i18n("%1 %2-bit", i18n("Unknown key type"), uint32_t(2048)));
        row.key_type = keyType.constData();
        row.display_flags = XCC_DISPLAY_SUBJECT_FALLBACK | XCC_DISPLAY_ISSUER_FALLBACK | XCC_DISPLAY_TOKEN_FALLBACK;
        view.display_flags = XCC_DISPLAY_CALLER_FALLBACK;
        QVERIFY(model.update(&view));
        keyType.fill('x'); // The model owns the formatted result.
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Detail).toString(), i18n("%1 %2-bit", QStringLiteral("RSA"), uint32_t(2048)));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Subject).toString(), i18n("Unnamed certificate"));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Issuer).toString(), i18n("Unnamed issuer"));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Token).toString(), i18n("Unnamed token"));
        QCOMPARE(model.context().value(QStringLiteral("caller")).toString(), i18n("An unknown app"));
    }

    void disclosure()
    {
        CertificateChooserModel model;
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.caller_name = "<b>Caller</b>";
        view.identity_level = XCC_IDENTITY_VERIFIED_SANDBOXED;
        view.may_sign = true;
        view.lifetime_seconds = 300;
        view.later_prompt = "SDK later-prompt signal";
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("heading")).toString(), i18n("%1 wants to identify you with a certificate", QStringLiteral("<b>Caller</b>")));
        const auto body = i18n("It can prove who you are to websites and servers for %1.", i18np("%1 minute", "%1 minutes", 5));
        QCOMPARE(model.context().value(QStringLiteral("body")).toString(), body);
        view.display_flags = XCC_DISPLAY_CALLER_FALLBACK;
        view.later_prompt = "";
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("heading")).toString(), i18n("%1 wants to identify you with a certificate", i18n("An unknown app")));
        QCOMPARE(model.context().value(QStringLiteral("body")).toString(), body);
        view.caller_name = nullptr;
        view.display_flags = 0;
        view.later_prompt = nullptr;
        view.struct_size = XCC_CHOOSER_VIEW_V1_SIZE;
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("heading")).toString(), i18n("%1 wants to identify you with a certificate", i18n("An unknown app")));
        QVERIFY(!model.context().value(QStringLiteral("body")).toString().contains(QStringLiteral("PIN")));
    }

    void purposeAndIdentity()
    {
        CertificateChooserModel model;
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.caller_name = "Firefox";
        view.identity_level = XCC_IDENTITY_VERIFIED_SANDBOXED;
        view.may_sign = true;
        view.purpose = XCC_PURPOSE_SIGNING;
        view.lifetime_seconds = 3600;
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("heading")).toString(), i18n("%1 wants to sign with your certificate", QStringLiteral("Firefox")));
        QCOMPARE(model.context().value(QStringLiteral("body")).toString(), i18n("It can sign documents and data for %1.", i18np("%1 hour", "%1 hours", 1)));
        QVERIFY(model.context().value(QStringLiteral("identity")).toString().isEmpty());
        view.purpose = XCC_PURPOSE_EMAIL;
        view.may_decrypt = true;
        view.lifetime_seconds = 61;
        view.identity_level = XCC_IDENTITY_DERIVED_HOST;
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("body")).toString(), i18n("It can sign and decrypt email for %1.",
            i18n("%1 and %2", i18np("%1 minute", "%1 minutes", 1), i18np("%1 second", "%1 seconds", 1))));
        QCOMPARE(model.context().value(QStringLiteral("identity")).toString(), i18n("The app name is reported by the app."));
        view.may_sign = false;
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("body")).toString(), i18n("It can decrypt email for %1.",
            i18n("%1 and %2", i18np("%1 minute", "%1 minutes", 1), i18np("%1 second", "%1 seconds", 1))));
        view.identity_level = XCC_IDENTITY_UNKNOWN;
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("caller")).toString(), i18n("An unknown app"));
    }

    void appIdentity()
    {
        CertificateChooserModel model;
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.caller_name = "Example";
        QByteArray appId("org.example.<b>App</b>");
        view.app_id = appId.constData();
        for (auto level : { XCC_IDENTITY_VERIFIED_SANDBOXED, XCC_IDENTITY_DERIVED_HOST }) {
            view.identity_level = level;
            QVERIFY(model.update(&view));
            QCOMPARE(model.context().value(QStringLiteral("appId")).toString(), QString::fromUtf8(appId));
        }
        appId.fill('x');
        QCOMPARE(model.context().value(QStringLiteral("appId")).toString(), QStringLiteral("org.example.<b>App</b>"));
        view.identity_level = XCC_IDENTITY_UNKNOWN;
        QVERIFY(model.update(&view));
        QVERIFY(model.context().value(QStringLiteral("appId")).toString().isEmpty());
        QCOMPARE(model.context().value(QStringLiteral("identity")).toString(), i18n("This app could not be identified. Continue only if you started this."));
        view.identity_level = XCC_IDENTITY_VERIFIED_SANDBOXED;
        view.display_flags = XCC_DISPLAY_CALLER_FALLBACK;
        QVERIFY(model.update(&view));
        QVERIFY(model.context().value(QStringLiteral("appId")).toString().isEmpty());
        view.display_flags = 0;
        view.caller_name = "";
        QVERIFY(model.update(&view));
        QVERIFY(model.context().value(QStringLiteral("appId")).toString().isEmpty());
        view.caller_name = "Example";
        view.app_id = nullptr;
        QVERIFY(model.update(&view));
        QVERIFY(model.context().value(QStringLiteral("appId")).toString().isEmpty());
        view.app_id = "org.example.App";
        view.struct_size = XCC_CHOOSER_VIEW_V1_SIZE;
        QVERIFY(model.update(&view));
        QCOMPARE(model.context().value(QStringLiteral("appId")).toString(), QStringLiteral("org.example.App"));
    }

    void olderAndPartialLayouts()
    {
        // A real original array: the second row begins at the old producer stride.
        struct OriginalRow {
            uint32_t struct_size, abi_version;
            uint64_t id;
            const char *subject, *issuer, *token, *reader, *expiry, *detail, *fingerprint;
            bool invalid;
        } rows[2] { };
        static_assert(sizeof(OriginalRow) == XCC_CHOOSER_CANDIDATE_VIEW_V1_SIZE);
        for (int i = 0; i < 2; ++i) {
            rows[i].struct_size = sizeof(OriginalRow);
            rows[i].abi_version = XCC_ABI_VERSION;
            rows[i].id = i + 1;
            rows[i].expiry = "Old English expiry";
            rows[i].detail = "Old English detail";
        }
        XccChooserView view { };
        view.struct_size = XCC_CHOOSER_VIEW_V1_SIZE;
        view.abi_version = XCC_ABI_VERSION;
        view.candidates = reinterpret_cast<const XccChooserCandidateView *>(rows);
        view.n_candidates = 2;
        view.purpose_text = "Old English purpose";
        view.purpose_detail = "Old English disclosure";
        CertificateChooserModel model;
        QVERIFY(model.update(&view));
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(1), CertificateChooserModel::Expiry).toString(), QStringLiteral("Old English expiry"));
        QCOMPARE(model.data(model.index(1), CertificateChooserModel::Detail).toString(), i18n("Unknown key type"));
        QCOMPARE(model.context().value(QStringLiteral("body")).toString(), i18n("%1 Access lasts %2.", QStringLiteral("Old English disclosure"), i18np("%1 second", "%1 seconds", 0)));
        XccChooserCandidateView partial { };
        partial.struct_size = offsetof(XccChooserCandidateView, key_type);
        partial.abi_version = XCC_ABI_VERSION;
        partial.id = 3;
        partial.validity = XCC_VALIDITY_NOT_YET_VALID;
        partial.expiry = "Partial English expiry";
        partial.detail = "Partial English detail";
        view.candidates = &partial;
        view.n_candidates = 1;
        QVERIFY(model.update(&view));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), i18n("Not yet valid"));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Detail).toString(), i18n("Unknown key type"));
        partial.struct_size = offsetof(XccChooserCandidateView, validity) + sizeof(partial.validity) - 1;
        QVERIFY(model.update(&view));
        QCOMPARE(model.data(model.index(0), CertificateChooserModel::Expiry).toString(), QStringLiteral("Partial English expiry"));
        partial.struct_size = XCC_CHOOSER_CANDIDATE_VIEW_V1_SIZE - 1;
        QVERIFY(!model.update(&view));
    }

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
        QCOMPARE(model.context().value(QStringLiteral("reason")).toString(), i18n("The app says “%1”", QStringLiteral("<a href='external'>application reason</a>")));
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
