// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/display_curve.hpp"
#include "analysis/display_worker.hpp"
#include <QCoreApplication>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QtTest>
using namespace s7studio::analysis;
class DisplayWorkerTest : public QObject {
    Q_OBJECT
  private slots:
    void latestRequestAndUiResponsiveness() {
        QSemaphore entered, release;
        std::atomic<bool> separate{false};
        std::atomic<int> obsolete{0};
        DisplayWorker<int> worker;
        const auto cleanup = qScopeGuard([&] {
            release.release();
            analysisPool().waitForDone(5000);
        });
        worker.submit([&](const auto &) {
            separate = QThread::currentThread() != QCoreApplication::instance()->thread();
            entered.release();
            release.acquire();
            return 1;
        });
        QTRY_VERIFY(entered.available() > 0);
        QVERIFY(entered.tryAcquire());
        int ticks = 0;
        QTimer timer;
        timer.setInterval(1);
        connect(&timer, &QTimer::timeout, this, [&] { ++ticks; });
        timer.start();
        QTRY_VERIFY(ticks >= 3);
        QVERIFY(separate);
        worker.submit([&](const auto &) {
            ++obsolete;
            return 2;
        });
        worker.submit([](const auto &) { return 3; });
        QVERIFY(!worker.take());
        release.release();
        QTRY_VERIFY(!worker.busy());
        const auto ready = worker.take();
        QVERIFY(ready && ready->value);
        QCOMPARE(*ready->value, 3);
        QCOMPARE(obsolete.load(), 0);
    }
    void cancellationAndDestruction() {
        QSemaphore entered, release;
        const auto cleanup = qScopeGuard([&] {
            release.release();
            analysisPool().waitForDone(5000);
        });
        {
            DisplayWorker<int> worker;
            worker.submit([&](const auto &) {
                entered.release();
                release.acquire();
                return 7;
            });
            QTRY_VERIFY(entered.available() > 0);
            QVERIFY(entered.tryAcquire());
            worker.cancel();
            QVERIFY(!worker.take());
        } // Must return while the job is still blocked; no widget access later.
        release.release();
        QVERIFY(analysisPool().waitForDone(5000));
    }
    void errorsAndRecovery() {
        DisplayWorker<int> worker;
        worker.submit([](const auto &) -> int { throw std::invalid_argument("bad display"); });
        QTRY_VERIFY(!worker.busy());
        auto ready = worker.take();
        QVERIFY(ready);
        QCOMPARE(ready->error, QString("bad display"));
        QVERIFY(!ready->value);
        worker.submit([](const auto &) { return 42; });
        QTRY_VERIFY(!worker.busy());
        ready = worker.take();
        QVERIFY(ready && ready->value);
        QCOMPARE(*ready->value, 42);
    }
    void parametricReduction() {
        QList<QPointF> points;
        for (int i = 0; i < 30000; ++i)
            points.append({double(i % 99), double(i % 17)});
        points[12345] = {-1000, 5000};
        points[21555] = {3000, -4000};
        const auto reduced = displayCurve(points);
        QVERIFY(reduced.size() <= 4096);
        QCOMPARE(reduced.front(), points.front());
        QCOMPARE(reduced.back(), points.back());
        QVERIFY(reduced.contains(points[12345]));
        QVERIFY(reduced.contains(points[21555]));
    }
};
QTEST_GUILESS_MAIN(DisplayWorkerTest)
#include "display_worker_test.moc"
