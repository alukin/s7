// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/pair_analysis.hpp"
#include "analysis/worker_pool.hpp"
#include <QtTest>
#include <QSemaphore>
#include <QThreadPool>
#include <cmath>
#include <numbers>
#include <numeric>

using namespace s7studio;
using namespace analysis;
namespace {
observation::LiveFrame frame(std::size_t count, std::int64_t first = 0)
{
    observation::LiveFrame f;
    f.runId = 1; f.generation = 1; f.rateNumerator = 1024; f.dt = 1./1024;
    f.capacity = count; f.received = static_cast<std::uint64_t>(first)+count;
    f.channels = {{"I", "V", "f64"}, {"Q", "A", "f64"}};
    for (std::size_t i = 0; i < count; ++i) {
        const auto step = first+static_cast<std::int64_t>(i);
        const double phase = 2*std::numbers::pi*static_cast<double>(step)/64;
        f.steps.push_back(step); f.times.push_back(static_cast<double>(step)/1024);
        f.values.push_back(std::cos(phase)); f.values.push_back(std::sin(phase));
    }
    return f;
}
}
class PairTest : public QObject {
    Q_OBJECT
private slots:
    void correlationMatchesIndependentDirectSum_data() {
        QTest::addColumn<int>("normalization"); QTest::addColumn<bool>("removeMean");
        for (int norm = 0; norm < 4; ++norm) for (bool mean : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(norm).arg(mean))) << norm << mean;
    }
    void correlationMatchesIndependentDirectSum() {
        QFETCH(int, normalization); QFETCH(bool, removeMean);
        auto input = frame(64);
        for (std::size_t i = 0; i < 64; ++i) {
            input.values[2*i] = 2+.3*std::sin(static_cast<double>(i)*.27);
            input.values[2*i+1] = -.7+std::cos(static_cast<double>(i)*.19);
        }
        PairSettings s; s.size = 64; s.maxLag = 63; s.removeMean = removeMean;
        s.normalization = static_cast<CorrelationNormalization>(normalization);
        PairAnalyzer analyzer; const auto actual = analyzer.analyze(PairKind::Correlation, input, s);
        QCOMPARE(actual.state, PairResult::State::Ready);
        QCOMPARE(actual.firstUnit, QString("V")); QCOMPARE(actual.secondUnit, QString("A"));
        long double a[64], b[64], ma = 0, mb = 0, ea = 0, eb = 0;
        for (std::size_t i = 0; i < 64; ++i) { a[i] = input.values[2*i]; b[i] = input.values[2*i+1]; ma += a[i]/64; mb += b[i]/64; }
        for (std::size_t i = 0; i < 64; ++i) { if (removeMean) { a[i] -= ma; b[i] -= mb; } ea += a[i]*a[i]; eb += b[i]*b[i]; }
        for (std::size_t k = 0; k < actual.lags.size(); ++k) {
            const auto lag = actual.lags[k]; long double expected = 0;
            for (int i = 0; i < 64; ++i) if (i+lag >= 0 && i+lag < 64) expected += a[i]*b[i+lag];
            if (normalization == 1) expected /= 64;
            if (normalization == 2) expected /= 64-std::abs(lag);
            if (normalization == 3) expected /= std::sqrt(ea*eb);
            QVERIFY2(std::abs(actual.correlations[k]-static_cast<double>(expected)) < 3e-13, qPrintable(QString("lag %1").arg(lag)));
        }
    }
    void impulsesEstablishSignedLinearLagAndPeak() {
        PairSettings s; s.size = 64; s.maxLag = 63; s.normalization = CorrelationNormalization::Raw;
        PairAnalyzer analyzer;
        for (const int delay : {-40, -6, 0, 6, 40}) {
            auto input = frame(64); std::fill(input.values.begin(), input.values.end(), 0);
            const int a = delay < 0 ? 50 : 5, b = a+delay;
            input.values[static_cast<std::size_t>(2*a)] = 2; input.values[static_cast<std::size_t>(2*b+1)] = 3;
            auto r = analyzer.analyze(PairKind::Correlation, input, s);
            QCOMPARE(r.state, PairResult::State::Ready); QCOMPARE(r.lags[r.peakIndex], delay);
            for (std::size_t k = 0; k < r.lags.size(); ++k) QVERIFY(std::abs(r.correlations[k]-(r.lags[k] == delay ? 6 : 0)) < 2e-14);
            input.values[static_cast<std::size_t>(2*b+1)] = -3; s.absolutePeak = true;
            r = analyzer.analyze(PairKind::Correlation, input, s); QCOMPARE(r.lags[r.peakIndex], delay);
            QVERIFY(std::abs(r.correlations[r.peakIndex]+6) < 2e-14); s.absolutePeak = false;
        }
    }
    void autocorrelationZeroEnergyAndExtremeScales() {
        PairSettings s; s.size = 64; s.maxLag = 8; s.autoCorrelation = true;
        PairAnalyzer analyzer; auto input = frame(64);
        auto r = analyzer.analyze(PairKind::Correlation, input, s);
        QCOMPARE(r.lags[r.peakIndex], 0); QVERIFY(std::abs(r.correlations[r.peakIndex]-1) < 1e-14);
        s.autoCorrelation = false;
        for (const double scale : {1e-290, 1e290}) {
            input = frame(64); for (auto& v : input.values) v *= scale;
            r = analyzer.analyze(PairKind::Correlation, input, s); QCOMPARE(r.state, PairResult::State::Ready);
            for (const auto v : r.correlations) QVERIFY(std::isfinite(v) && std::abs(v) <= 1+1e-14);
        }
        input = frame(64); std::fill(input.values.begin(), input.values.end(), 2);
        s.removeMean = true; QCOMPARE(analyzer.analyze(PairKind::Correlation, input, s).state, PairResult::State::Unavailable);
        s.normalization = CorrelationNormalization::Raw;
        r = analyzer.analyze(PairKind::Correlation, input, s); QCOMPARE(r.lags[r.peakIndex], 0);
        for (auto v : r.correlations) QCOMPARE(v, 0.);
        s.removeMean = false; for (auto& v : input.values) v = 1e200;
        QCOMPARE(analyzer.analyze(PairKind::Correlation, input, s).state, PairResult::State::Unavailable);
        // Reciprocal channel scales have a finite physical product.
        for (std::size_t i = 0; i < 64; ++i) { input.values[2*i] = 1e200; input.values[2*i+1] = 1e-200; }
        r = analyzer.analyze(PairKind::Correlation, input, s); QVERIFY(std::abs(r.correlations[r.peakIndex]-64) < 1e-12);
    }
    void invalidIntervalsAndCancellation() {
        PairAnalyzer analyzer; PairSettings s; s.size = 64; s.maxLag = 16;
        QCOMPARE(analyzer.analyze(PairKind::Correlation, frame(63), s).state, PairResult::State::Filling);
        auto input = frame(64); ++input.steps[32];
        QCOMPARE(analyzer.analyze(PairKind::Correlation, input, s).state, PairResult::State::Unavailable);
        input = frame(64); input.values[10] = std::numeric_limits<double>::infinity();
        QCOMPARE(analyzer.analyze(PairKind::Correlation, input, s).state, PairResult::State::Unavailable);
        input = frame(64); input.times.pop_back();
        QCOMPARE(analyzer.analyze(PairKind::Correlation, input, s).state, PairResult::State::Unavailable);
        s.size = 65536; s.maxLag = 65535; input = frame(65536);
        QCOMPARE(analyzer.analyze(PairKind::Correlation, input, s, [] { return true; }).state, PairResult::State::Cancelled);
        const auto result = analyzer.analyze(PairKind::Correlation, input, s);
        QCOMPARE(result.state, PairResult::State::Ready); QCOMPARE(result.lags.size(), std::size_t(131071));
    }
    void xyCircleClustersAndDensityAreRawSameStepPairs() {
        PairAnalyzer analyzer; PairSettings s;
        auto r = analyzer.analyze(PairKind::Constellation, frame(1024), s);
        QCOMPARE(r.state, PairResult::State::Ready); QCOMPARE(r.points.size(), std::size_t(1024));
        for (std::size_t i = 0; i < r.points.size(); ++i) {
            const auto& p = r.points[i]; QCOMPARE(p.step, static_cast<std::int64_t>(i));
            QVERIFY(std::abs(p.x*p.x+p.y*p.y-1) < 1e-14); QCOMPARE(p.time, static_cast<double>(i)/1024);
        }
        QCOMPARE(std::accumulate(r.density.begin(), r.density.end(), std::uint64_t(0)), std::uint64_t(1024));
        auto input = frame(64); for (std::size_t i = 0; i < 64; ++i) { input.values[2*i] = i%2 ? 1 : -1; input.values[2*i+1] = i%4 < 2 ? 1 : -1; }
        r = analyzer.analyze(PairKind::Constellation, input, s);
        QCOMPARE(std::count(r.density.begin(), r.density.end(), 16u), 4);
        std::fill(input.values.begin(), input.values.end(), std::numeric_limits<double>::denorm_min());
        r = analyzer.analyze(PairKind::Constellation, input, s);
        QCOMPARE(r.state, PairResult::State::Ready);
        QCOMPARE(std::accumulate(r.density.begin(), r.density.end(), std::uint64_t(0)), std::uint64_t(64));
        s.second = 0; QCOMPARE(analyzer.analyze(PairKind::Constellation, input, s).state, PairResult::State::Unavailable);
    }
    void xyPersistenceUsesStepsDeduplicatesAndResets() {
        PairAnalyzer analyzer; PairSettings s; s.persistenceSteps = 100;
        auto r = analyzer.analyze(PairKind::Constellation, frame(64), s);
        QCOMPARE(r.points.front().step, std::int64_t(0));
        r = analyzer.analyze(PairKind::Constellation, frame(64, 32), s); QCOMPARE(r.points.size(), std::size_t(96));
        for (int i = 0; i < 10; ++i) r = analyzer.analyze(PairKind::Constellation, frame(64, 32), s);
        QCOMPARE(r.points.size(), std::size_t(96)); QCOMPARE(r.evicted, std::uint64_t(0));
        r = analyzer.analyze(PairKind::Constellation, frame(64, 64), s);
        QCOMPARE(r.points.size(), std::size_t(100)); QCOMPARE(r.firstStep, std::int64_t(28)); QCOMPARE(r.evicted, std::uint64_t(28));
        auto input = frame(64, 128); input.runId = 2;
        r = analyzer.analyze(PairKind::Constellation, input, s); QCOMPARE(r.points.size(), std::size_t(64));
        input.generation = 2; input.steps.front() = 127;
        r = analyzer.analyze(PairKind::Constellation, input, s); QCOMPARE(r.points.size(), std::size_t(64)); QCOMPARE(r.gaps, std::uint64_t(1));
        s.persistenceSteps = 0; r = analyzer.analyze(PairKind::Constellation, frame(32, 160), s); QCOMPARE(r.points.size(), std::size_t(32));
    }
    void xyGapsNeverProduceFalseLineSegmentsAndStorageIsBounded() {
        PairAnalyzer analyzer; PairSettings s; s.persistenceSteps = 1000000;
        auto input = frame(64); input.values[20] = std::numeric_limits<double>::quiet_NaN();
        for (std::size_t i = 32; i < 64; ++i) input.steps[i] += 3;
        auto r = analyzer.analyze(PairKind::Constellation, input, s);
        QCOMPARE(r.invalid, std::uint64_t(1)); QCOMPARE(r.gaps, std::uint64_t(1));
        QCOMPARE(r.points.size(), std::size_t(63)); QVERIFY(r.points[10].segmentStart); QVERIFY(r.points[31].segmentStart);
        analyzer.reset();
        for (int i = 0; i < 20; ++i) r = analyzer.analyze(PairKind::Constellation, frame(8192, i*8192), s);
        QCOMPARE(r.points.size(), std::size_t(65536)); QCOMPARE(r.evicted, std::uint64_t(20*8192-65536));
        QCOMPARE(r.firstStep, std::int64_t(20*8192-65536));
    }
    void workerReplacesCancelsAndOwnsItsFrame() {
        // Hold both pool threads to make replacement/cancellation deterministic.
        QSemaphore entered, release;
        for (int i = 0; i < 2; ++i) analysisPool().start([&] { entered.release(); release.acquire(); });
        entered.acquire(2);
        PairSettings s; s.size = 64; s.maxLag = 16;
        PairWorker worker(PairKind::Correlation);
        auto input = std::make_shared<observation::LiveFrame>(frame(64));
        worker.submit(input, s, 1); worker.submit(input, s, 2); const auto replaced = worker.replacedRequests();
        worker.cancel(); worker.submit(input, s, 3); release.release(2);
        QVERIFY(replaced >= 1); QTRY_VERIFY(!worker.busy());
        auto result = worker.takeResult(); QVERIFY(result); QCOMPARE(result->revision, std::uint64_t(3)); QCOMPARE(result->frame, input);
        QVERIFY(!worker.takeResult());
        { PairWorker closing(PairKind::Constellation); closing.submit(input, s, 1); }
        QVERIFY(analysisPool().waitForDone(5000));
    }
};
QTEST_GUILESS_MAIN(PairTest)
#include "pair_analysis_test.moc"
