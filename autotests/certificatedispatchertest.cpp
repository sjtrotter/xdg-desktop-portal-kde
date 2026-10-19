/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "certificatedispatcher.h"
#include "certificatetesthelpers.h"

#include <QTest>
#include <QThread>
#include <atomic>
#include <thread>
#include <vector>

class CertificateDispatcherTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void fifoAndNeverInline()
    {
        CertificateDispatcher receiver;
        const auto dispatch = receiver.table();
        QList<int> observed;
        struct Closure {
            QList<int> *observed;
            int value;
            QThread *thread;
        };
        std::vector<Closure> closures(128);
        auto function = [](void *user) {
            auto closure = static_cast<Closure *>(user);
            QCOMPARE(QThread::currentThread(), closure->thread);
            closure->observed->append(closure->value);
        };
        for (int i = 0; i < 128; ++i) {
            closures[i] = { &observed, i, receiver.thread() };
        }
        dispatch.post(dispatch.user, function, &closures[0]);
        QVERIFY(observed.isEmpty()); // Posting from the GUI/receiver thread is queued too.
        std::atomic<int> turn{ 1 };
        auto producer = [&](int parity) {
            for (int i = parity; i < 128; i += 2) {
                while (turn.load() != i) {
                    std::this_thread::yield();
                }
                dispatch.post(dispatch.user, function, &closures[i]);
                turn.store(i + 1);
            }
        };
        std::thread odd(producer, 1);
        std::thread even(producer, 2);
        odd.join();
        even.join();
        QVERIFY(observed.isEmpty());
        QTRY_COMPARE(observed.size(), 128);
        for (int i = 0; i < 128; ++i) {
            QCOMPARE(observed[i], i);
        }
    }

    void shutdownDrainsClosures()
    {
        CertificateDispatcher receiver;
        const auto dispatch = receiver.table();
        auto ui = certificateTestUi();
        const char *modules[] = { nullptr };
        XccConfig config{ };
        config.struct_size = sizeof(config);
        config.abi_version = XCC_ABI_VERSION;
        config.module_paths = modules;
        auto context = xcc_context_new(&config, &ui, &dispatch, nullptr);
        QVERIFY(context);
        struct State {
            int completions = 0;
            int closures = 0;
            bool stopped = false;
            QThread *thread;
        } state{ 0, 0, false, receiver.thread() };
        QList<XccRequest *> requests;
        for (int i = 0; i < 16; ++i) {
            requests.append(xcc_get_capabilities_async(
                context,
                [](XccRequest *, const XccResult *, void *user) {
                    auto state = static_cast<State *>(user);
                    QCOMPARE(QThread::currentThread(), state->thread);
                    QVERIFY(!state->stopped);
                    ++state->completions;
                },
                &state));
            dispatch.post(
                dispatch.user,
                [](void *user) {
                    auto state = static_cast<State *>(user);
                    QVERIFY(!state->stopped);
                    ++state->closures;
                },
                &state);
        }
        QCOMPARE(state.completions, 0);
        QCOMPARE(state.closures, 0);
        xcc_context_shutdown_async(
            context,
            [](XccContext *, void *user) {
                auto state = static_cast<State *>(user);
                QCOMPARE(QThread::currentThread(), state->thread);
                QCOMPARE(state->completions, 16);
                QCOMPARE(state->closures, 16);
                state->stopped = true;
            },
            &state);
        QTRY_VERIFY_WITH_TIMEOUT(state.stopped, 10000);
        for (auto request : requests) {
            xcc_request_unref(request);
        }
        xcc_context_unref(context);
        QCoreApplication::processEvents();
        QCOMPARE(state.completions, 16);
        QCOMPARE(state.closures, 16);
    }
};

QTEST_GUILESS_MAIN(CertificateDispatcherTest)
#include "certificatedispatchertest.moc"
