/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatechoosermodel.h"
#include "certificatepinfield.h"
#include "certificatepinprompt.h"
#include "certificatepintestcore.h"
#include "certificateui.h"
#include "certificateuitesthelpers.h"

#include <KLocalizedString>
#include <QColor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QTest>
#include <QTextOption>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

// Observe propagation without copying or logging event text.
class PinKeyPropagationProbe : public QObject
{
public:
    int forwarded = 0;
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease || event->type() == QEvent::ShortcutOverride) {
            ++forwarded;
        }
        return false;
    }
};

class CertificatePinBridgeTest : public QObject
{
    Q_OBJECT
    XccPinView pinView(bool protectedPath = false)
    {
        XccPinView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.token_label = "<b>External token</b>";
        view.reader = "Reader";
        view.caller_display = "Example application";
        view.purpose_display = "sign a document or a piece of data";
        view.purpose = XCC_PURPOSE_SIGNING;
        view.protected_path = protectedPath;
        return view;
    }
    QQuickWindow *window()
    {
        for (auto window : QGuiApplication::allWindows()) {
            if (auto quick = qobject_cast<QQuickWindow *>(window); quick && quick->isVisible()) {
                return quick;
            }
        }
        return nullptr;
    }
    void type(CertificatePinField *field, const QString &text)
    {
        // Exercise delivery through the actual PIN window, including releases.
        QTRY_COMPARE(field->window()->activeFocusItem(), static_cast<QQuickItem *>(field));
        PinKeyPropagationProbe probe;
        field->parentItem()->installEventFilter(&probe);
        QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, { }, text);
        QCoreApplication::sendEvent(field->window(), &event);
        QVERIFY(event.isAccepted());
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, { }, text);
        QCoreApplication::sendEvent(field->window(), &release);
        QVERIFY(release.isAccepted());
        QCOMPARE(probe.forwarded, 0);
    }
private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(certificateTestBusHasNoActivation(QDBusConnection::sessionBus()), "Run through certificate-test-session.sh");
    }
    void init() { fakePin = { }; }
    void chooserKeyboardAndRefresh()
    {
        CertificateChooserModel model;
        XccChooserCandidateView rows[2] { };
        for (int i = 0; i < 2; ++i) {
            rows[i].struct_size = sizeof(rows[i]);
            rows[i].abi_version = XCC_ABI_VERSION;
            rows[i].id = i + 1;
            rows[i].subject = "<b>Certificate</b>";
            rows[i].issuer = "Issuer";
            rows[i].token = "Token";
        }
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.candidates = rows;
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(u"qrc:/qt/qml/org/kde/xdgdesktopportal/CertificateChooserDialog.qml"_s));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties({ { u"candidateModel"_s, QVariant::fromValue(&model) } }));
        QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
        auto dialog = qobject_cast<QQuickWindow *>(object.data());
        QVERIFY(dialog);
        QCOMPARE(dialog->title(), i18nc("@title:window", "Choose Certificate"));
        QSignalSpy accepted(dialog, SIGNAL(accept()));
        const auto focusedId = [dialog] {
            auto item = dialog->activeFocusItem();
            return item ? item->property("candidateId").toString() : QString();
        };
        QVERIFY(QMetaObject::invokeMethod(dialog, "present"));
        dialog->requestActivate();
        QTRY_VERIFY(dialog->activeFocusItem());
        QTRY_COMPARE(focusedId(), QStringLiteral("1"));
        QVERIFY(!model.hasSelection());
        QTest::keyClick(dialog, Qt::Key_Down);
        QCOMPARE(model.selectedId(), uint64_t(1));
        QTest::keyClick(dialog, Qt::Key_Return);
        QCOMPARE(accepted.count(), 1); // The smoke driver's Down/Return pair.
        auto popup = dialog->findChild<QObject *>(u"certificateDetailsPopup"_s);
        QVERIFY(popup);
        for (const auto key : { Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space }) {
            QTest::keyClick(dialog, Qt::Key_Tab);
            QTRY_VERIFY(dialog->activeFocusItem());
            QTRY_COMPARE(dialog->activeFocusItem()->objectName(), QStringLiteral("certificateDetails"));
            QCOMPARE(dialog->activeFocusItem()->property("text").toString(), i18n("Certificate Details"));
            QTest::keyClick(dialog, key);
            QTRY_VERIFY(popup->property("opened").toBool());
            QCOMPARE(accepted.count(), 1); // Opening details must never grant.
            QTest::keyClick(dialog, Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("opened").toBool());
            QTRY_COMPARE(focusedId(), QStringLiteral("1"));
        }
        std::swap(rows[0], rows[1]);
        ++view.revision;
        QVERIFY(model.update(&view));
        QTRY_COMPARE(focusedId(), QStringLiteral("1"));
        QCOMPARE(model.selectedId(), uint64_t(1));
        QCOMPARE(model.selectedRow(), 1);
        view.n_candidates = 0;
        QVERIFY(model.update(&view));
        QTRY_VERIFY(dialog->activeFocusItem());
        QTRY_COMPARE(dialog->activeFocusItem()->objectName(), QStringLiteral("certificateCancel"));
        QVERIFY(!model.hasSelection());
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QTRY_COMPARE(focusedId(), QStringLiteral("2"));
        QVERIFY(!model.hasSelection());

        QTest::keyClick(dialog, Qt::Key_Down);
        QTest::keyClick(dialog, Qt::Key_Down);
        QTRY_COMPARE(focusedId(), QStringLiteral("1"));
        QCOMPARE(model.selectedId(), uint64_t(1));
        // Reinsertion before deferred focus restoration must also forget ID 1.
        view.n_candidates = 0;
        QVERIFY(model.update(&view));
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QTRY_COMPARE(focusedId(), QStringLiteral("2"));
        QVERIFY(!model.hasSelection());
    }

    void chooserDetailsPreserveSelection_data()
    {
        QTest::addColumn<Qt::Key>("key");
        QTest::newRow("return") << Qt::Key_Return;
        QTest::newRow("enter") << Qt::Key_Enter;
    }

    void chooserDetailsPreserveSelection()
    {
        QFETCH(Qt::Key, key);
        CertificateChooserModel model;
        const QByteArray subject = "<b>Certificate B " + QByteArray(2048, 'S') + "</b>";
        const QByteArray issuer = "<i>Issuer B " + QByteArray(2048, 'I') + "</i>";
        XccChooserCandidateView rows[2] { };
        for (int i = 0; i < 2; ++i) {
            rows[i].struct_size = sizeof(rows[i]);
            rows[i].abi_version = XCC_ABI_VERSION;
            rows[i].id = i + 1;
        }
        rows[0].subject = "Certificate A";
        rows[0].issuer = "Issuer A";
        rows[1].subject = subject.constData();
        rows[1].issuer = issuer.constData();
        rows[1].fingerprint = "AB:CD:EF:01";
        XccChooserView view { };
        view.struct_size = sizeof(view);
        view.abi_version = XCC_ABI_VERSION;
        view.candidates = rows;
        view.n_candidates = 2;
        QVERIFY(model.update(&view));
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl(u"qrc:/qt/qml/org/kde/xdgdesktopportal/CertificateChooserDialog.qml"_s));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.createWithInitialProperties({ { u"candidateModel"_s, QVariant::fromValue(&model) } }));
        QVERIFY2(!object.isNull(), qPrintable(component.errorString()));
        auto dialog = qobject_cast<QQuickWindow *>(object.data());
        QVERIFY(dialog);
        QSignalSpy accepted(dialog, SIGNAL(accept()));
        QVERIFY(QMetaObject::invokeMethod(dialog, "present"));
        dialog->requestActivate();
        QTRY_VERIFY(dialog->activeFocusItem());
        QTRY_COMPARE(dialog->activeFocusItem()->property("candidateId").toString(), u"1"_s);
        QTest::keyClick(dialog, Qt::Key_Down);
        QCOMPARE(model.selectedId(), uint64_t(1));
        QTest::keyClick(dialog, key);
        QCOMPARE(accepted.count(), 1);

        // Controls and styles can add Tab stops; a row's focused child does
        // not carry candidateId. Find B's details through its visual ancestors.
        const auto focusedCandidate = [dialog]() -> QQuickItem * {
            for (auto item = dialog->activeFocusItem(); item; item = item->parentItem()) {
                if (item->property("candidateId").isValid()) {
                    return item;
                }
            }
            return nullptr;
        };
        const auto focusedDetailsForB = [dialog, &focusedCandidate] {
            auto candidate = focusedCandidate();
            return candidate && candidate->property("candidateId").toString() == u"2"_s
                && dialog->activeFocusItem()->objectName() == u"certificateDetails"_s;
        };
        for (int tabs = 0; tabs < 16 && !focusedDetailsForB(); ++tabs) {
            QTest::keyClick(dialog, Qt::Key_Tab);
            QTRY_VERIFY(dialog->activeFocusItem());
            QCOMPARE(model.selectedId(), uint64_t(1));
            QCOMPARE(accepted.count(), 1);
        }
        QVERIFY2(focusedDetailsForB(), "Tab navigation did not reach certificate B's details");
        auto rowB = focusedCandidate();
        QTest::keyClick(dialog, key);
        auto popup = dialog->findChild<QObject *>(u"certificateDetailsPopup"_s);
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QCOMPARE(model.selectedId(), uint64_t(1));
        QCOMPARE(accepted.count(), 1);
        for (const auto &entry : { std::pair { u"certificateDetailsSubject"_s, QString::fromUtf8(subject) },
                                  std::pair { u"certificateDetailsIssuer"_s, QString::fromUtf8(issuer) } }) {
            auto label = popup->findChild<QQuickItem *>(entry.first);
            QVERIFY(label);
            QVERIFY(label->isVisible());
            QCOMPARE(label->property("text").toString(), entry.second);
            QCOMPARE(label->property("textFormat").toInt(), int(Qt::PlainText));
            QCOMPARE(label->property("wrapMode").toInt(), int(QTextOption::WrapAnywhere));
            QCOMPARE(label->property("elide").toInt(), int(Qt::ElideNone));
            QCOMPARE(label->property("maximumLineCount").toInt(), std::numeric_limits<int>::max());
        }

        auto scroll = popup->property("contentItem").value<QQuickItem *>();
        QVERIFY(scroll);
        QVERIFY(scroll->clip());
        auto flickable = scroll->property("contentItem").value<QQuickItem *>();
        QVERIFY(flickable);
        for (const int height : { dialog->height(), 320 }) {
            dialog->setHeight(height);
            QTRY_VERIFY(popup->property("height").toReal() < dialog->height());
            QTRY_VERIFY(popup->property("y").toReal() > 0);
            QTRY_VERIFY(popup->property("y").toReal() + popup->property("height").toReal() < dialog->height());
            QTRY_VERIFY(flickable->property("contentHeight").toReal() > flickable->height());
            QTRY_VERIFY(flickable->property("contentWidth").toReal() <= flickable->width());
        }
        QQuickItem *fingerprint = nullptr;
        const auto detailsItems = popup->findChildren<QQuickItem *>();
        for (auto item : detailsItems) {
            if (item->property("text").toString() == u"AB:CD:EF:01"_s) {
                fingerprint = item;
                break;
            }
        }
        QVERIFY(fingerprint);
        const auto fingerprintVisible = [fingerprint, flickable] {
            const auto top = fingerprint->mapToItem(flickable, QPointF()).y();
            return top >= 0 && top + fingerprint->height() <= flickable->height() + 1;
        };
        QTRY_VERIFY(!fingerprintVisible());
        QVERIFY(flickable->setProperty("contentY", flickable->property("contentHeight").toReal() - flickable->height()));
        QTRY_VERIFY(fingerprintVisible());
        fingerprint->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(dialog->activeFocusItem(), fingerprint);
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!popup->property("opened").toBool());
        QTRY_COMPARE(dialog->activeFocusItem(), rowB);
        QTest::keyClick(dialog, key);
        QCOMPARE(model.selectedId(), uint64_t(1));
        QCOMPARE(accepted.count(), 1); // B's focus must never grant A.

        QTest::keyClick(dialog, Qt::Key_Down);
        QCOMPARE(model.selectedId(), uint64_t(2));
        QTest::keyClick(dialog, key);
        QCOMPARE(accepted.count(), 2); // Matching row focus still grants.

        // The explicit Allow button can still confirm A while B is focused.
        model.selectRow(0);
        QQuickItem *allow = nullptr;
        const auto buttons = dialog->findChildren<QQuickItem *>();
        for (auto button : buttons) {
            if (button->property("text").toString() == i18nc("@action:button", "Allow")) {
                allow = button;
                break;
            }
        }
        QVERIFY(allow);
        allow->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(dialog->activeFocusItem(), allow);
        QTest::keyClick(dialog, key);
        QCOMPARE(accepted.count(), 3);
        QCOMPARE(model.selectedId(), uint64_t(1));
    }

    void pinFocusIndicatorFollowsSecureField()
    {
        CertificateUi bridge;
        bridge.attach(fakePinContext());
        auto ui = bridge.table();
        auto view = pinView();
        ui.pin_show(ui.user, 11, &view);
        QTRY_COMPARE(fakePin.ready, 1);
        auto dialog = window();
        QVERIFY(dialog);
        auto field = dialog->findChild<CertificatePinField *>();
        QVERIFY(field);
        auto indicator = field->findChild<QQuickItem *>(u"certificatePinFocusIndicator"_s);
        QVERIFY(indicator);
        QQuickItem *decoration = nullptr;
        for (auto child : field->childItems()) {
            if (child->property("placeholderText").toString() == i18n("PIN")) {
                decoration = child;
                break;
            }
        }
        QVERIFY(decoration);
        QCOMPARE(decoration->property("focusPolicy").toInt(), int(Qt::NoFocus));
        QVERIFY(!decoration->activeFocusOnTab());
        QVERIFY(!decoration->property("activeFocusOnPress").toBool());
        QVERIFY(!decoration->hasActiveFocus());
        QSignalSpy decorationFocusChanges(decoration, &QQuickItem::activeFocusChanged);
        QVERIFY(decorationFocusChanges.isValid());
        dialog->requestActivate();
        QTRY_COMPARE(dialog->activeFocusItem(), static_cast<QQuickItem *>(field));
        QTRY_VERIFY(indicator->isVisible());
        QVERIFY(indicator->width() > 0);
        QVERIFY(indicator->height() > 0);
        QVERIFY(QQmlProperty::read(indicator, u"border.width"_s).toReal() > 0);
        QVERIFY(QQmlProperty::read(indicator, u"border.color"_s).value<QColor>().alpha() > 0);
        QVERIFY(!decoration->hasActiveFocus());
        QTest::keyClick(dialog, Qt::Key_Tab);
        QTRY_VERIFY(dialog->activeFocusItem() != field);
        QTRY_VERIFY(!indicator->isVisible());
        QVERIFY(!decoration->hasActiveFocus());
        QTest::keyClick(dialog, Qt::Key_Backtab);
        QTRY_COMPARE(dialog->activeFocusItem(), static_cast<QQuickItem *>(field));
        QTRY_VERIFY(indicator->isVisible());
        type(field, u"1234"_s);
        QCOMPARE(field->maskedCount(), 4);
        QCOMPARE(decoration->property("text").toString(), QString(4, QChar(0x25cf)));
        QVERIFY(!decoration->hasActiveFocus());
        QTest::keyClick(dialog, Qt::Key_Tab);
        QTRY_VERIFY(dialog->activeFocusItem() != field);
        QTRY_VERIFY(!indicator->isVisible());
        QTest::mouseClick(dialog, Qt::LeftButton, Qt::NoModifier,
                          decoration->mapToScene(QPointF(decoration->width() / 2, decoration->height() / 2)).toPoint());
        QTRY_COMPARE(dialog->activeFocusItem(), static_cast<QQuickItem *>(field));
        QTRY_VERIFY(indicator->isVisible());
        QVERIFY(decorationFocusChanges.isEmpty()); // Even transient decoration focus is forbidden.
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        ui.close(ui.user, 11);
        QCOMPARE(fakePin.allocated, fakePin.freed);
    }

    void structuredTextAndOldPrefix()
    {
        CertificatePinPrompt prompt(fakePinContext(), 1);
        auto view = pinView();
        view.purpose_display = "English purpose must be ignored";
        view.status = "English status must be ignored";
        view.status_code = XCC_PIN_STATUS_MEMORY_UNAVAILABLE;
        view.display_flags = XCC_DISPLAY_TOKEN_FALLBACK;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"body"_s).toString(), i18n("%1 needs it to sign documents and data.", QStringLiteral("Example application")));
        QCOMPARE(prompt.view().value(u"retry"_s).toString(), i18n("The PIN could not be kept in secure memory."));
        QCOMPARE(prompt.view().value(u"tokenLine"_s).toString(), i18n("Security token “%1” in %2", i18n("Unnamed token"), QStringLiteral("Reader")));
        view.struct_size = XCC_PIN_VIEW_V1_SIZE;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"body"_s).toString(), i18n("%1 needs it to use your certificate.", QStringLiteral("Example application")));
        QCOMPARE(prompt.view().value(u"retry"_s).toString(), QString::fromUtf8(view.status));
    }
    void pinMessagePriority_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("locked");
        QTest::addColumn<bool>("finalTry");
        QTest::addColumn<bool>("countLow");
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("error");
        QTest::newRow("locked") << int(XCC_PIN_STATUS_INCORRECT) << true << true << true
            << i18n("The PIN is locked. Unlock it with your PUK or ask your administrator.") << true;
        QTest::newRow("incorrect-final") << int(XCC_PIN_STATUS_INCORRECT) << false << true << true
            << i18n("Wrong PIN. Another wrong PIN locks the token.") << true;
        QTest::newRow("final") << int(XCC_PIN_STATUS_NONE) << false << true << true
            << i18n("Another wrong PIN locks the token.") << true;
        QTest::newRow("incorrect-low") << int(XCC_PIN_STATUS_INCORRECT) << false << false << true
            << i18n("Wrong PIN. Few attempts may remain.") << true;
        QTest::newRow("incorrect") << int(XCC_PIN_STATUS_INCORRECT) << false << false << false
            << i18n("Wrong PIN. Try again.") << true;
        QTest::newRow("low") << int(XCC_PIN_STATUS_NONE) << false << false << true
            << i18n("A wrong PIN was entered before. Few attempts may remain.") << false;
        QTest::newRow("none") << int(XCC_PIN_STATUS_NONE) << false << false << false << QString() << false;
        for (bool finalTry : {false, true}) {
            const auto suffix = finalTry ? "-final" : "-low";
            QTest::newRow(qPrintable("required"_L1 + QLatin1StringView(suffix))) << int(XCC_PIN_STATUS_REQUIRED) << false << finalTry << true
                << i18n("Enter your PIN.") << true;
            QTest::newRow(qPrintable("too-long"_L1 + QLatin1StringView(suffix))) << int(XCC_PIN_STATUS_TOO_LONG) << false << finalTry << true
                << i18n("This PIN is too long.") << true;
            QTest::newRow(qPrintable("memory"_L1 + QLatin1StringView(suffix))) << int(XCC_PIN_STATUS_MEMORY_UNAVAILABLE) << false << finalTry << true
                << i18n("The PIN could not be kept in secure memory.") << true;
            QTest::newRow(qPrintable("submit-failed"_L1 + QLatin1StringView(suffix))) << (int(XCC_PIN_STATUS_MEMORY_UNAVAILABLE) + 1) << false << finalTry << true
                << i18n("The PIN could not be sent to the token. Try again.") << true;
        }
    }
    void pinMessagePriority()
    {
        QFETCH(int, status);
        QFETCH(bool, locked);
        QFETCH(bool, finalTry);
        QFETCH(bool, countLow);
        QFETCH(QString, message);
        QFETCH(bool, error);
        CertificatePinPrompt prompt(fakePinContext(), 1);
        auto view = pinView();
        view.status_code = static_cast<XccPinStatus>(status);
        view.locked = locked;
        view.final_try = finalTry;
        view.count_low = countLow;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"message"_s).toString(), message);
        QCOMPARE(prompt.view().value(u"messageError"_s).toBool(), error);
    }
    void pinHeadingPriority_data()
    {
        QTest::addColumn<bool>("locked");
        QTest::addColumn<bool>("finalTry");
        QTest::addColumn<bool>("protectedPath");
        QTest::addColumn<bool>("contextSpecific");
        QTest::addColumn<QString>("heading");
        for (bool protectedPath : {false, true}) {
            for (bool contextSpecific : {false, true}) {
                const QByteArray suffix = QByteArray(protectedPath ? "-reader" : "-entry") + (contextSpecific ? "-again" : "");
                QTest::newRow(QByteArray("normal" + suffix).constData()) << false << false << protectedPath << contextSpecific
                    << (protectedPath ? (contextSpecific ? i18n("Enter your PIN again on the reader") : i18n("Enter your PIN on the reader"))
                                      : (contextSpecific ? i18n("Enter your PIN again") : i18n("Enter your PIN")));
                QTest::newRow(QByteArray("final" + suffix).constData()) << false << true << protectedPath << contextSpecific
                    << (protectedPath ? i18n("Last PIN attempt on the reader") : i18n("Last PIN attempt"));
                QTest::newRow(QByteArray("locked" + suffix).constData()) << true << true << protectedPath << contextSpecific
                    << i18n("PIN locked");
                QTest::newRow(QByteArray("locked-only" + suffix).constData()) << true << false << protectedPath << contextSpecific
                    << i18n("PIN locked");
            }
        }
    }
    void pinHeadingPriority()
    {
        QFETCH(bool, locked);
        QFETCH(bool, finalTry);
        QFETCH(bool, protectedPath);
        QFETCH(bool, contextSpecific);
        QFETCH(QString, heading);
        CertificatePinPrompt prompt(fakePinContext(), 1);
        auto view = pinView(protectedPath);
        view.locked = locked;
        view.final_try = finalTry;
        view.count_low = true; // Count-low alone leaves the normal heading.
        view.context_specific = contextSpecific;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"heading"_s).toString(), heading);
        view.status_code = XCC_PIN_STATUS_INCORRECT;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"heading"_s).toString(), heading);
        view.locked = false;
        view.final_try = false;
        view.status_code = XCC_PIN_STATUS_NONE;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"heading"_s).toString(),
                 protectedPath ? (contextSpecific ? i18n("Enter your PIN again on the reader") : i18n("Enter your PIN on the reader"))
                               : (contextSpecific ? i18n("Enter your PIN again") : i18n("Enter your PIN")));
    }
    void finalTryWarningUpdatesHeading()
    {
        CertificatePinPrompt prompt(fakePinContext(), 1);
        auto view = pinView(true);
        view.context_specific = true;
        QVERIFY(prompt.update(&view));
        QCOMPARE(prompt.view().value(u"heading"_s).toString(), i18n("Enter your PIN again on the reader"));
        XccWarning warning { sizeof(XccWarning), XCC_ABI_VERSION, XCC_WARNING_FINAL_TRY, 42, "ignored policy text" };
        QVERIFY(prompt.warning(&warning));
        QCOMPARE(prompt.view().value(u"heading"_s).toString(), i18n("Last PIN attempt on the reader"));
        QCOMPARE(prompt.view().value(u"message"_s).toString(), i18n("Another wrong PIN locks the token."));
        QVERIFY(prompt.view().value(u"messageError"_s).toBool());
    }
    void wrongThenRightAndBusyCancellation()
    {
        CertificateUi bridge;
        bridge.attach(fakePinContext());
        auto ui = bridge.table();
        auto view = pinView();
        view.count_low = true;
        view.final_try = true;
        ui.pin_show(ui.user, 11, &view);
        auto prompt = bridge.findChild<CertificatePinPrompt *>();
        QVERIFY(prompt);
        // Both flags and the final-try hint exist synchronously, before input
        // or the queued presentation acknowledgement.
        QVERIFY(prompt->view().value(u"countLow"_s).toBool());
        QVERIFY(prompt->view().value(u"finalTry"_s).toBool());
        QCOMPARE(prompt->view().value(u"heading"_s).toString(), i18n("Last PIN attempt"));
        QVERIFY(!prompt->view().value(u"hint"_s).toString().isEmpty());
        QCOMPARE(fakePin.ready, 0);
        QTRY_COMPARE(fakePin.ready, 1);
        QCOMPARE(fakePin.failed, 0);
        QVERIFY(window());
        auto field = window()->findChild<CertificatePinField *>();
        QVERIFY(field);
        QCOMPARE(field->maskedCount(), 0);
        auto hint = window()->findChild<QObject *>(u"pinHint"_s);
        QVERIFY(hint);
        QCOMPARE(hint->property("plainText"), prompt->view().value(u"message"_s));
        QCOMPARE(window()->property("pinRetry").value<QObject *>(), hint);
        QCOMPARE(hint->property("type").toInt(), 3); // Kirigami.MessageType.Error
        auto token = window()->findChild<QObject *>(u"pinToken"_s);
        QVERIFY(token);
        QVERIFY(token->property("text").toString().contains(u"<b>External token</b>"_s));
        QCOMPARE(token->property("textFormat").toInt(), int(Qt::PlainText));
        fakePin.expected = "wrong";
        type(field, u"wrong"_s);
        QVERIFY(QMetaObject::invokeMethod(window(), "accept"));
        QCOMPARE(fakePin.submitted, 1);
        QVERIFY(fakePin.matched);
        QCOMPARE(field->maskedCount(), 0);
        QVERIFY(prompt->view().value(u"busy"_s).toBool());
        QVERIFY(QMetaObject::invokeMethod(window(), "accept"));
        QCOMPARE(fakePin.submitted, 1);
        view.busy = true;
        ui.pin_update(ui.user, 11, &view);
        view.busy = false;
        ui.pin_update(ui.user, 11, &view);
        view.status = "That PIN was not accepted. Try again.";
        view.status_code = XCC_PIN_STATUS_INCORRECT;
        ui.pin_update(ui.user, 11, &view);
        QVERIFY(!prompt->view().value(u"retry"_s).toString().isEmpty());
        QCOMPARE(hint->property("plainText").toString(), i18n("Wrong PIN. Another wrong PIN locks the token."));
        QCOMPARE(hint->property("type").toInt(), 3);
        QVERIFY(!prompt->view().value(u"busy"_s).toBool());
        QCOMPARE(field->maskedCount(), 0);
        QCOMPARE(fakePin.allocated, 2);
        fakePin.expected = "123456";
        type(field, u"123456"_s);
        QVERIFY(QMetaObject::invokeMethod(window(), "accept"));
        QCOMPARE(fakePin.submitted, 2);
        QVERIFY(fakePin.matched);
        auto dialog = window();
        QVERIFY(QMetaObject::invokeMethod(dialog, "reject"));
        QCOMPARE(fakePin.cancelled, 1);
        QVERIFY(!dialog->isVisible());
        ui.hide(ui.user, 11);
        ui.pin_update(ui.user, 11, &view); // Stale retry must not resurrect input.
        QCOMPARE(fakePin.allocated, 2);
        QPointer<CertificatePinPrompt> retained = prompt;
        ui.close(ui.user, 11);
        QVERIFY(retained.isNull());
        QCOMPARE(fakePin.allocated, fakePin.freed);
    }
    void countLowBeforeInputAndContextSpecific()
    {
        CertificateUi bridge;
        bridge.attach(fakePinContext());
        auto ui = bridge.table();
        auto view = pinView();
        view.count_low = true;
        ui.pin_show(ui.user, 12, &view);
        auto prompt = bridge.findChild<CertificatePinPrompt *>();
        QVERIFY(prompt);
        QVERIFY(prompt->view().value(u"countLow"_s).toBool());
        QVERIFY(!prompt->view().value(u"finalTry"_s).toBool());
        QVERIFY(!prompt->view().value(u"hint"_s).toString().isEmpty());
        const auto userWording = prompt->view().value(u"heading"_s);
        view.context_specific = true;
        ui.pin_update(ui.user, 12, &view);
        QVERIFY(prompt->view().value(u"heading"_s) != userWording);
        QTRY_COMPARE(fakePin.ready, 1);
        QVERIFY(window());
        auto hint = window()->findChild<QObject *>(u"pinHint"_s);
        QVERIFY(hint);
        QCOMPARE(hint->property("plainText"), prompt->view().value(u"message"_s));
        QCOMPARE(hint->property("type").toInt(), 2); // Kirigami.MessageType.Warning
        view.status_code = XCC_PIN_STATUS_INCORRECT;
        ui.pin_update(ui.user, 12, &view);
        QCOMPARE(hint->property("plainText").toString(), i18n("Wrong PIN. Few attempts may remain."));
        QCOMPARE(hint->property("type").toInt(), 3);
        view.struct_size = XCC_PIN_VIEW_V1_SIZE;
        view.status = "<b>Legacy status & text</b>";
        ui.pin_update(ui.user, 12, &view);
        QCOMPARE(hint->property("plainText").toString(), QString::fromUtf8(view.status));
        QCOMPARE(hint->property("text").toString(), QStringLiteral("<span>&lt;b&gt;Legacy status &amp; text&lt;/b&gt;</span>"));
        auto field = window()->findChild<CertificatePinField *>();
        QVERIFY(field);
        QCOMPARE(field->maskedCount(), 0);
        ui.close(ui.user, 12);
        ui.pin_show(ui.user, 16, &view);
        ui.hide(ui.user, 16); // A pre-presentation cancel must suppress ready.
        QCoreApplication::processEvents();
        QCOMPARE(fakePin.ready, 1);
        ui.close(ui.user, 16);
    }
    void protectedPathFinalTryRequiresContinue()
    {
        CertificateUi bridge;
        bridge.attach(fakePinContext());
        auto ui = bridge.table();
        auto view = pinView(true);
        view.final_try = true;
        ui.pin_show(ui.user, 13, &view);
        QTRY_COMPARE(fakePin.ready, 1);
        QCOMPARE(fakePin.submitted, 0);
        QCOMPARE(fakePin.warnings, 0);
        QVERIFY(window());
        QVERIFY(!window()->findChild<CertificatePinField *>());
        auto prompt = bridge.findChild<CertificatePinPrompt *>();
        QVERIFY(prompt);
        XccWarning warning { sizeof(XccWarning), XCC_ABI_VERSION, XCC_WARNING_FINAL_TRY, 42, "ignored policy text" };
        ui.warning(ui.user, 13, &warning);
        QVERIFY(prompt->view().value(u"warningPending"_s).toBool());
        QCOMPARE(prompt->view().value(u"heading"_s).toString(), i18n("Last PIN attempt on the reader"));
        QCOMPARE(fakePin.warnings, 0); // Presentation never accepts a warning.
        QVERIFY(QMetaObject::invokeMethod(window(), "accept"));
        QCOMPARE(fakePin.warnings, 1);
        QCOMPARE(fakePin.warning, uint64_t(42));
        QVERIFY(fakePin.accepted);
        QVERIFY(prompt->view().value(u"busy"_s).toBool());
        QVERIFY(QMetaObject::invokeMethod(window(), "accept"));
        QCOMPARE(fakePin.warnings, 1);
        ui.close(ui.user, 13);
        QCOMPARE(fakePin.allocated, 0);
    }
    void protectedPathAutomaticAndWarningCancel()
    {
        CertificateUi bridge;
        bridge.attach(fakePinContext());
        auto ui = bridge.table();
        auto view = pinView(true);
        ui.pin_show(ui.user, 14, &view);
        QTRY_COMPARE(fakePin.ready, 1); // The real core starts NULL login here.
        QCOMPARE(fakePin.warnings, 0);
        view.busy = true;
        ui.pin_update(ui.user, 14, &view);
        QVERIFY(window());
        QVERIFY(!window()->findChild<CertificatePinField *>());
        QVERIFY(QMetaObject::invokeMethod(window(), "reject"));
        QCOMPARE(fakePin.cancelled, 1);
        ui.close(ui.user, 14);
        view = pinView(true);
        view.final_try = true;
        ui.pin_show(ui.user, 15, &view);
        QTRY_COMPARE(fakePin.ready, 2);
        XccWarning warning { sizeof(XccWarning), XCC_ABI_VERSION, XCC_WARNING_FINAL_TRY, 43, "" };
        ui.warning(ui.user, 15, &warning);
        QVERIFY(QMetaObject::invokeMethod(window(), "reject"));
        QCOMPARE(fakePin.warnings, 1);
        QVERIFY(!fakePin.accepted);
        QCOMPARE(fakePin.submitted, 0);
        QCOMPARE(fakePin.cancelled, 2);
        ui.close(ui.user, 15);
    }
};
QTEST_MAIN(CertificatePinBridgeTest)
#include "certificatepinbridgetest.moc"
