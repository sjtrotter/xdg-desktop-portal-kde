/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatepinfield.h"
#include "certificatepinprompt.h"
#include "certificatepintestcore.h"
#include "certificateui.h"
#include "certificateuitesthelpers.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPointer>
#include <QQuickWindow>
#include <QTest>

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
        QCOMPARE(hint->property("text"), prompt->view().value(u"hint"_s));
        QCOMPARE(hint->property("textFormat").toInt(), int(Qt::PlainText));
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
        ui.pin_update(ui.user, 11, &view);
        QVERIFY(!prompt->view().value(u"retry"_s).toString().isEmpty());
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
        const auto userWording = prompt->view().value(u"authentication"_s);
        view.context_specific = true;
        ui.pin_update(ui.user, 12, &view);
        QVERIFY(prompt->view().value(u"authentication"_s) != userWording);
        QTRY_COMPARE(fakePin.ready, 1);
        QVERIFY(window());
        auto hint = window()->findChild<QObject *>(u"pinHint"_s);
        QVERIFY(hint);
        QCOMPARE(hint->property("text"), prompt->view().value(u"hint"_s));
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
