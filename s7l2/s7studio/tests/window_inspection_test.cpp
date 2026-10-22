// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/window_inspection.hpp"
#include <QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QTemporaryDir>
#include <cmath>
#include <limits>
#include <numbers>
using namespace s7studio;
using namespace analysis;
namespace {
std::shared_ptr<observation::LiveFrame> frame(std::size_t n = 64)
{
    auto f = std::make_shared<observation::LiveFrame>();
    f->runId = 9007199254740993ULL; f->generation = 12; f->received = n; f->capacity = n;
    f->rateNumerator = 1024; f->rateDenominator = 1; f->dt = 1./1024;
    f->channels = {{"I, \"quoted\"", "V", "f128"}, {"Q", "A", "f64"}};
    f->provenance = {{"schema", "test causal source"}, {"step_rate_hz", 1024}};
    for (std::size_t i = 0; i < n; ++i) {
        f->steps.push_back(9007199254740993LL+static_cast<std::int64_t>(i));
        f->times.push_back(static_cast<double>(i)/1024);
        f->values.push_back(static_cast<double>(i)); f->values.push_back(-static_cast<double>(i));
    }
    return f;
}
QByteArray read(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
void write(const QString& path, const QByteArray& bytes) { QFile file(path); if (file.open(QIODevice::WriteOnly)) file.write(bytes); }
}
class InspectionTest : public QObject {
    Q_OBJECT
private slots:
    void inclusiveStatisticsAndIndependentReference() {
        std::vector<double> values;
        for (int i = 0; i < 2048; ++i) values.push_back(3+2*std::sin(2*std::numbers::pi*static_cast<double>(i)/128));
        auto s = regionStatistics(values, 128, 383);
        QCOMPARE(s.count, std::size_t(256)); QCOMPARE(s.invalid, std::size_t(0));
        QVERIFY(std::abs(*s.mean-3) < 1e-14); QVERIFY(std::abs(*s.rms-std::sqrt(11.)) < 1e-14);
        QVERIFY(std::abs(*s.standardDeviation-std::sqrt(2.)) < 1e-14);
        QCOMPARE(*s.minimum, 1.); QCOMPARE(*s.maximum, 5.); QCOMPARE(*s.peakToPeak, 4.);
        QCOMPARE(s.minimumIndex, std::size_t(224)); QCOMPARE(s.maximumIndex, std::size_t(160));
        s = regionStatistics(values, 383, 128); QCOMPARE(s.count, std::size_t(256));
        long double sum = 0, squares = 0;
        for (std::size_t i = 11; i <= 31; ++i) { sum += values[i]; squares += static_cast<long double>(values[i])*values[i]; }
        s = regionStatistics(values, 11, 31);
        QVERIFY(std::abs(*s.mean-static_cast<double>(sum/21)) < 2e-15);
        QVERIFY(std::abs(*s.rms-static_cast<double>(std::sqrt(squares/21))) < 2e-15);
    }
    void extremeValuesSmallVariationAndInvalidCoverage() {
        std::vector<double> v{1e16, 1e16+2};
        auto s = regionStatistics(v, 0, 1); QVERIFY(std::abs(*s.standardDeviation-1) < 1e-14);
        v = {1e308, -1e308}; s = regionStatistics(v, 0, 1);
        QCOMPARE(*s.mean, 0.); QCOMPARE(*s.rms, 1e308); QCOMPARE(*s.standardDeviation, 1e308); QVERIFY(!s.peakToPeak);
        v = {1e308, 1, -1e308}; s = regionStatistics(v, 0, 2); QVERIFY(std::abs(*s.mean-1./3) < 1e-14);
        v = {1e-300, -1e-300}; s = regionStatistics(v, 0, 1); QCOMPARE(*s.rms, 1e-300);
        v = {0, -0.}; s = regionStatistics(v, 0, 0); QCOMPARE(s.count, std::size_t(1)); QCOMPARE(*s.rms, 0.); QCOMPARE(*s.standardDeviation, 0.);
        v = {1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
        s = regionStatistics(v, 0, 2); QCOMPARE(s.invalid, std::size_t(2)); QVERIFY(!s.mean && !s.minimum && !s.rms);
        QVERIFY_THROWS_EXCEPTION(std::out_of_range, regionStatistics(v, 0, 3));
    }
    void cursorsKeepIntegerStepsAndGapCoverage() {
        auto f = frame(); auto d = timeWindow(f);
        auto text = cursorReadout(d, 1, 3);
        QVERIFY(text.contains("step 9007199254740994")); QVERIFY(text.contains("Δstep=2"));
        QVERIFY(text.contains("1/Δt=512 Hz")); QVERIFY(text.contains("B−A=2"));
        QVERIFY(cursorReadout(d, 1, 1).contains("1/Δt=unavailable"));
        d.steps[2] += 1; for (std::size_t i = 3; i < d.steps.size(); ++i) d.steps[i] += 1;
        QVERIFY(measurementReadout(d, 0, 3).contains("missing steps 1"));
        d.columns[0].values[2] = std::numeric_limits<double>::infinity();
        QVERIFY(measurementReadout(d, 0, 3).contains("aggregates unavailable: nonfinite coverage"));
        d.columns[0].values[0] = -1e308; d.columns[0].values[1] = 1e308;
        QVERIFY(cursorReadout(d, 0, 1).contains("unavailable (overflow)"));
    }
    void buildersRetainAnalysisAndDistinguishDerivedMeasurements() {
        auto f = frame(); SpectrumResult r; r.state = SpectrumResult::State::Ready; r.frame = f;
        r.firstStep = f->steps.front(); r.lastStep = f->steps.back(); r.size = 64; r.unit = "V";
        r.frequencies = {0, 16, 32}; r.values = {1, 2, 3}; r.peakHold = {2, 3, 4};
        r.settings.quantity = SpectrumQuantity::Psd; r.averaged = 8; r.averageFirstStep = r.firstStep-128;
        auto d = spectrumWindowSnapshot(r);
        QCOMPARE(d.columns[0].unit, QString("V²/Hz")); QCOMPARE(d.columns.size(), std::size_t(2));
        QVERIFY(measurementReadout(d, 0, 2).contains("derived bins/lags"));
        QVERIFY(measurementReadout(d, 0, 2).contains("Raw latest-transform input"));
        QVERIFY(!d.settings["average_history_raw_available"].toBool());
        PairResult p; p.state = PairResult::State::Ready; p.frame = f; p.kind = PairKind::Correlation;
        p.firstStep = r.firstStep; p.lastStep = r.lastStep; p.lags = {-1, 0, 1}; p.correlations = {0, 1, .5}; p.peakIndex = 1;
        d = pairWindowSnapshot(p); QCOMPARE(d.axisUnit, QString("samples")); QVERIFY(cursorReadout(d, 0, 2).contains("Δlag_time=0.001953125 s"));
        p.kind = PairKind::Constellation; p.firstName = "I"; p.secondName = "Q"; p.firstUnit = "V"; p.secondUnit = "A";
        p.points = {{1, .1, 1, 2, true}, {2, .2, 3, 4, false}};
        d = pairWindowSnapshot(p); QCOMPARE(d.steps.front(), std::int64_t(1)); QCOMPARE(d.columns[1].values.back(), 4.);
        QVERIFY(measurementReadout(d, 0, 1).contains("inclusive raw samples/pairs"));
    }
    void jsonPreservesRawAndDerivedPrecisionAndMetadata() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); auto f = frame();
        f->values[0] = -0.; f->values[2] = std::nextafter(1., 2.);
        f->values[4] = std::numeric_limits<double>::quiet_NaN(); f->values[6] = std::numeric_limits<double>::infinity();
        const auto path = dir.filePath("window.s7window"); const auto d = timeWindow(f);
        auto result = exportWindow(path, d, 1, 4, WindowExportFormat::WindowJson); QVERIFY2(result.success, qPrintable(result.error));
        QJsonParseError error; auto doc = QJsonDocument::fromJson(read(path), &error); QCOMPARE(error.error, QJsonParseError::NoError);
        const auto meta = doc["metadata"].toObject(); QCOMPARE(meta["format"].toString(), QString("s7.window")); QVERIFY(!meta["complete_run"].toBool());
        QCOMPARE(meta["raw"].toObject()["run_id"].toString(), QString("9007199254740993"));
        QCOMPARE(meta["raw"].toObject()["channels"].toArray()[0].toObject()["source_type"].toString(), QString("f128"));
        const auto rows = doc["raw_samples"].toArray(); QCOMPARE(rows.size(), 64);
        QCOMPARE(rows[0].toObject()["values"].toArray()[0].toString(), QString("-0"));
        QCOMPARE(rows[1].toObject()["values"].toArray()[0].toString().toDouble(), std::nextafter(1., 2.));
        QCOMPARE(rows[2].toObject()["values"].toArray()[0].toString(), QString("NaN"));
        QCOMPARE(rows[3].toObject()["values"].toArray()[0].toString(), QString("Infinity"));
        QCOMPARE(doc["displayed_rows"].toArray().size(), 64);
    }
    void csvUsesLocaleIndependentQuotedColumnsAndFullWindow() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto before = QLocale(); QLocale::setDefault(QLocale(QLocale::German));
        const auto d = timeWindow(frame()); const auto path = dir.filePath("data.csv");
        const auto result = exportWindow(path, d, 2, 3, WindowExportFormat::RawCsv); QLocale::setDefault(before);
        QVERIFY2(result.success, qPrintable(result.error)); const auto bytes = read(path);
        QVERIFY(bytes.startsWith("# {")); QVERIFY(bytes.contains("\"I, \"\"quoted\"\" [V]\""));
        QVERIFY(bytes.contains("1,9007199254740994,0.0009765625,0.0009765625,1,-1\n"));
        QCOMPARE(bytes.count('\n'), 66); // A/B annotate measurements; export includes the entire pinned window.
        auto spectrum = d; spectrum.kind = WindowSnapshot::Kind::Spectrum; spectrum.steps.clear(); spectrum.times.clear();
        QVERIFY(exportWindow(path, spectrum, 2, 3, WindowExportFormat::DisplayedCsv).success);
        QVERIFY(read(path).contains("1,,,0.0009765625,1,-1\n"));
    }
    void cancellationAndErrorsPreserveExistingDestination() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("existing.s7window"); write(path, "original");
        auto d = timeWindow(frame(4096)); int calls = 0;
        const auto result = exportWindow(path, d, 0, 1, WindowExportFormat::WindowJson, [&] { return ++calls == 5; });
        QVERIFY(result.cancelled); QVERIFY(!result.success); QCOMPARE(read(path), QByteArray("original"));
        d.columns[0].values.pop_back(); const auto invalid = exportWindow(path, d, 0, 1, WindowExportFormat::WindowJson);
        QVERIFY(!invalid.success); QCOMPARE(read(path), QByteArray("original"));
        QVERIFY(!exportWindow(dir.path(), timeWindow(frame()), 0, 1, WindowExportFormat::RawCsv).success);
    }
    void workerOwnsSnapshotAndReportsCompletion() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("worker.s7window");
        WindowExportWorker worker; auto d = std::make_shared<const WindowSnapshot>(timeWindow(frame(4096)));
        QVERIFY(worker.start(path, d, 0, 10, WindowExportFormat::WindowJson)); d.reset();
        QTRY_VERIFY(!worker.busy()); const auto result = worker.takeResult(); QVERIFY(result && result->success);
        QCOMPARE(worker.progress(), 100); QVERIFY(!worker.takeResult());
        QVERIFY(QJsonDocument::fromJson(read(path)).isObject());
    }
};
QTEST_GUILESS_MAIN(InspectionTest)
#include "window_inspection_test.moc"
