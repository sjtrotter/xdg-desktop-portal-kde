/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatepinfield.h"
#include "certificatepintestcore.h"
#include "certificateuitesthelpers.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMetaProperty>
#include <QTest>

using namespace Qt::StringLiterals;

class CertificatePinFieldTest : public QObject
{
    Q_OBJECT
    void type(CertificatePinField &field, const QString &text, int key = Qt::Key_unknown, Qt::KeyboardModifiers modifiers = { })
    {
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        QCoreApplication::sendEvent(&field, &event);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY2(certificateTestBusHasNoActivation(QDBusConnection::sessionBus()), "Run through certificate-test-session.sh");
    }
    void init() { fakePin = { }; }
    void typingAndMultibyteBackspace()
    {
        CertificatePinField field;
        type(field, u"1é\U0001f511"_s);
        fakePin.expected = "1\xc3\xa9\xf0\x9f\x94\x91";
        QVERIFY(field.submit(fakePinContext(), 8));
        QVERIFY(fakePin.matched);
        QCOMPARE(fakePin.length, size_t(7));
        field.reset();
        type(field, u"1é\U0001f511"_s);
        QCOMPARE(field.maskedCount(), 3);
        type(field, { }, Qt::Key_Backspace);
        QCOMPARE(field.maskedCount(), 2);
        type(field, { }, Qt::Key_Backspace);
        QCOMPARE(field.maskedCount(), 1);
        fakePin.expected = "1";
        QVERIFY(field.submit(fakePinContext(), 8));
        QVERIFY(fakePin.matched);
        QCOMPARE(fakePin.length, size_t(1));
    }
    void limitAndNul()
    {
        CertificatePinField field;
        type(field, QString(510, u'a'));
        type(field, u"é"_s); // Reject whole code point at the byte limit.
        QCOMPARE(field.maskedCount(), 510);
        type(field, u"b"_s);
        type(field, u"c"_s);
        QCOMPARE(field.maskedCount(), 511);
        QVERIFY(field.submit(fakePinContext(), 1));
        QCOMPARE(fakePin.length, size_t(511));
        field.reset();
        type(field, QString::fromLatin1("x\0y", 3));
        QCOMPARE(field.maskedCount(), 0);
        QVERIFY(!field.submit(fakePinContext(), 1));
        type(field, QString(512, u'a'));
        QCOMPARE(field.maskedCount(), 0);
        type(field, u"12"_s);
        type(field, QString::fromLatin1("x\0y", 3));
        QCOMPARE(field.maskedCount(), 2);
        fakePin.expected = "12";
        QVERIFY(field.submit(fakePinContext(), 1));
        QVERIFY(fakePin.matched);
    }
    void noTextExposureOrClipboardOrIme()
    {
        CertificatePinField field;
        type(field, u"123456"_s);
        const auto meta = field.metaObject();
        QCOMPARE(meta->indexOfProperty("text"), -1);
        QCOMPARE(meta->indexOfProperty("displayText"), -1);
        QCOMPARE(meta->indexOfProperty("secret"), -1);
        QVERIFY(field.dynamicPropertyNames().isEmpty());
        for (int i = 0; i < meta->propertyCount(); ++i) {
            const auto value = meta->property(i).read(&field);
            if (value.metaType() == QMetaType::fromType<QString>() || value.metaType() == QMetaType::fromType<QByteArray>()) {
                QVERIFY(value.toString() != u"123456"_s);
            }
        }
        QVERIFY(!field.flags().testFlag(QQuickItem::ItemAcceptsInputMethod));
        const auto hints = field.inputMethodQuery(Qt::ImHints).toInt();
        QVERIFY(hints & Qt::ImhHiddenText);
        QVERIFY(hints & Qt::ImhSensitiveData);
        QVERIFY(hints & Qt::ImhNoPredictiveText);
        QVERIFY(!field.inputMethodQuery(Qt::ImEnabled).toBool());
        QVERIFY(!field.inputMethodQuery(Qt::ImSurroundingText).isValid());
        QVERIFY(!field.inputMethodQuery(Qt::ImCurrentSelection).isValid());
        auto clipboard = QGuiApplication::clipboard();
        clipboard->setText(u"clipboard sentinel"_s);
        for (auto key : { Qt::Key_C, Qt::Key_X, Qt::Key_V, Qt::Key_Z, Qt::Key_Y, Qt::Key_A }) {
            type(field, { }, key, Qt::ControlModifier);
        }
        type(field, { }, Qt::Key_Insert, Qt::ShiftModifier);
        QCOMPARE(field.maskedCount(), 6);
        QCOMPARE(clipboard->text(), u"clipboard sentinel"_s);
        QInputMethodEvent ime;
        ime.setCommitString(u"injected"_s);
        QCoreApplication::sendEvent(&field, &ime);
        QCOMPARE(field.maskedCount(), 6);
        QVERIFY(field.submit(fakePinContext(), 1));
        QVERIFY(fakePin.matched);
    }
    void transferAndFreshRetry()
    {
        {
            CertificatePinField field;
            QCOMPARE(fakePin.allocated, 1);
            type(field, u"123456"_s);
            QVERIFY(field.submit(fakePinContext(), 17));
            QVERIFY(fakePin.matched);
            QCOMPARE(fakePin.freed, 1);
            QCOMPARE(field.maskedCount(), 0);
            QVERIFY(!field.submit(fakePinContext(), 17));
            type(field, u"9"_s);
            QCOMPARE(field.maskedCount(), 0);
            field.reset();
            QCOMPARE(fakePin.allocated, 2);
            type(field, u"123456"_s);
            QVERIFY(field.submit(fakePinContext(), 17));
            QCOMPARE(fakePin.submitted, 2);
            QVERIFY(fakePin.matched);
            field.reset();
            type(field, u"unsubmitted"_s);
        }
        QCOMPARE(fakePin.allocated, fakePin.freed);
    }
};
QTEST_MAIN(CertificatePinFieldTest)
#include "certificatepinfieldtest.moc"
