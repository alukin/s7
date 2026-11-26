// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "lifecycle_test_support.hpp"
#include "observation/scope_results.hpp"
#include <QBuffer>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using namespace s7studio;
namespace {
class LimitedBuffer : public QBuffer {
    qint64 limit_;
public:
    explicit LimitedBuffer(qint64 limit) : limit_(limit) { open(QIODevice::ReadWrite); }
protected:
    qint64 writeData(const char* data, qint64 length) override {
        if (pos() >= limit_) { setErrorString("injected disk full"); return -1; }
        return QBuffer::writeData(data, std::min(length, limit_ - pos()));
    }
};
// Storage whose every write is slow, so the recording cannot keep up.
class SlowBuffer : public QBuffer {
public:
    SlowBuffer() { open(QIODevice::ReadWrite); }
protected:
    qint64 writeData(const char* data, qint64 length) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return QBuffer::writeData(data, length);
    }
};
observation::StorageOptions slowStorage() {
    observation::StorageOptions storage;
    storage.memoryLimit = 2;
    storage.createStorage = [] { return std::make_unique<SlowBuffer>(); };
    return storage;
}
bool logged(const QSignalSpy& log, const QString& text) {
    for (const auto& entry : log)
        if (entry[0].toString().contains(text)) return true;
    return false;
}
// A real-valued oscillator (forward Euler) for comparing recorded values.
boost::json::value oscillator() {
    auto element = [](const char* id, const char* type, boost::json::object params) {
        return boost::json::object{{"id", id}, {"type", type}, {"params", std::move(params)}};
    };
    auto wire = [](const char* from, const char* to) {
        return boost::json::object{{"from_element", from}, {"from_port", 0}, {"to_element", to}, {"to_port", 0}};
    };
    return boost::json::object{{"name", "oscillator"},
        {"elements", boost::json::array{element("velocity", "EulerIntegrator", {{"initial_value", 1.0}, {"substeps", 1}}),
                                        element("position", "EulerIntegrator", {{"initial_value", 0.0}, {"substeps", 1}}),
                                        element("spring", "Gain", {{"factor", -1.0}})}},
        {"connections", boost::json::array{wire("velocity", "position"), wire("position", "spring"), wire("spring", "velocity")}}};
}
std::vector<double> recordedValues(const observation::ObservationSource& source, observation::ChannelId channel) {
    std::vector<double> values;
    for (std::uint64_t cursor = 0; cursor < source.status().samples;) {
        const auto page = source.read(cursor);
        for (const auto& sample : page.samples) if (sample.channel == channel) values.push_back(sample.value);
        cursor = page.next;
    }
    return values;
}
std::atomic<int> complexSteps{0};
class ComplexElement : public s7::Element {
    s7::ComplexValue value_{1,2};
public:
    ComplexElement() : Element("CaptureComplex", true) { register_output(0, "out", s7::SignalType::C64, &value_); }
    void step() override { ++complexSteps; }
};
QJsonObject header(const QByteArray& data) {
    QBuffer buffer; buffer.setData(data); buffer.open(QIODevice::ReadOnly); buffer.seek(8);
    QDataStream stream(&buffer); stream.setByteOrder(QDataStream::LittleEndian);
    quint32 version = 0, size = 0; stream >> version >> size;
    return QJsonDocument::fromJson(buffer.read(size)).object();
}
}
class ObservationCaptureTest : public QObject {
    Q_OBJECT
    std::shared_ptr<Gate> gate_;
private slots:
    void initTestCase() {
        registerLifecycleElements();
        s7::ElementMetadata metadata; metadata.type_name = "CaptureComplex";
        metadata.output_ports = {{"out", "complex output", s7::SignalType::C64}};
        s7::ElementRegistry::instance().register_element(s7::ElementDescriptor("CaptureComplex",
            [](const s7::ElementCreationContext&) { return std::make_unique<ComplexElement>(); }, metadata));
    }
    void init() { gate_ = currentGate = std::make_shared<Gate>(); gate_->releaseAll(); }
    // Acknowledging re-arms the notification while the run continues.
    void acknowledgementRearmsNotification() {
        LocalSchemaRunner runner;
        QSignalSpy advanced(&runner, &LocalSchemaRunner::recordingAdvanced);
        runner.run(schema(1.), .001, -1, {{"scope/a", "source", 0}});
        QTRY_VERIFY_WITH_TIMEOUT(advanced.count() >= 1, 10000);
        QTest::qWait(50);
        QCOMPARE(advanced.count(), 1); // nothing more until acknowledged
        runner.acknowledgeRecordingAdvanced();
        QTRY_VERIFY_WITH_TIMEOUT(advanced.count() == 2, 10000);
        runner.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 10000);
        runner.reset();
    }
    // The UI never acknowledges: it is notified once, and the recording
    // still holds every sample.
    void capturesAllSamplesWithoutUiAcknowledgement() {
        LocalSchemaRunner runner;
        QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        QSignalSpy advanced(&runner, &LocalSchemaRunner::recordingAdvanced);
        const double value = std::nextafter(1., 2.);
        runner.run(schema(value), .001, 110000, {{"scope/a", "source", 0}, {"scope/b", "source", 0}});
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 15000);
        QCOMPARE(runner.state(), RunState::Completed);
        QCoreApplication::processEvents();
        QCOMPARE(advanced.count(), 1);
        const auto source = runner.recordedSource(); QVERIFY(source);
        QCOMPARE(source->status().samples, std::uint64_t(220000));
        QVERIFY(source->status().finished && !source->status().incomplete());
        std::uint64_t cursor = 0;
        while (cursor < source->status().samples) {
            const auto page = source->read(cursor); QVERIFY(page.error.isEmpty());
            for (const auto& sample : page.samples) {
                QCOMPARE(sample.step, static_cast<std::int64_t>(cursor / 2 + 1));
                QCOMPARE(sample.value, value); ++cursor;
            }
        }
        QString error;
        auto projection = observation::scopeProjection(*source, "scope", {0,1}, &error);
        QVERIFY2(projection, qPrintable(error));
        QBuffer saved; saved.open(QIODevice::WriteOnly);
        QVERIFY2(projection->writeRunResults(saved, &error), qPrintable(error));
        const auto metadata = header(saved.data());
        QVERIFY(!metadata.value("incomplete").toBool());
        QCOMPARE(metadata.value("dropped_samples").toString(), QString("0"));
        QCOMPARE(metadata.value("observation").toObject().value("run_id").toString(), QString::number(source->runId()));
        QBuffer input(&saved.buffer()); input.open(QIODevice::ReadOnly);
        ui::ScopeRecording reopened("saved"); QVERIFY(reopened.openRunResults(input, "overflow", &error));
        for (const auto& series : reopened.series()) QCOMPARE(series.receivedSamples, std::uint64_t(110000));
        runner.reset(); QCOMPARE(source->status().samples, std::uint64_t(220000));
    }
    void storageFailuresStopAndPreserveIncompletePrefix_data() {
        QTest::addColumn<bool>("flush");
        QTest::newRow("mid-write-disk-full") << false;
        QTest::newRow("final-flush") << true;
    }
    void storageFailuresStopAndPreserveIncompletePrefix() {
        QFETCH(bool, flush);
        bool inject = true;
        observation::StorageOptions storage;
        storage.memoryLimit = 2;
        storage.createStorage = [&]() -> std::unique_ptr<QIODevice> {
            return std::make_unique<LimitedBuffer>(inject && !flush ? 4 * sizeof(observation::Sample) + 5 : 1000000);
        };
        storage.flushStorage = [&](QIODevice&) { return !inject || !flush; };
        LocalSchemaRunner runner(nullptr, storage);
        QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        runner.run(schema(2), .001, 10, {{"scope/a", "source", 0}});
        QTRY_COMPARE(complete.count(), 1);
        QCOMPARE(runner.state(), RunState::Error); QVERIFY(!complete[0][0].toBool());
        const auto failed = runner.recordedSource();
        QVERIFY(failed->status().finished && failed->status().incomplete());
        QVERIFY(!failed->status().storageError.isEmpty());
        QCOMPARE(failed->status().samples, std::uint64_t(flush ? 10 : 4));
        QString error;
        auto prefix = observation::scopeProjection(*failed, "scope", {0}, &error);
        QVERIFY2(prefix, qPrintable(error)); QVERIFY(prefix->exportMayBeIncomplete());
        QBuffer saved; saved.open(QIODevice::WriteOnly); QVERIFY(prefix->writeRunResults(saved, &error));
        QVERIFY(header(saved.data()).value("incomplete").toBool());
        inject = false;
        runner.run(schema(9), .001, 3, {{"scope/a", "source", 0}});
        QTRY_COMPARE(complete.count(), 2); QCOMPARE(runner.state(), RunState::Completed);
        QCOMPARE(runner.recordedSource()->status().samples, std::uint64_t(3));
        QVERIFY(!runner.recordedSource()->status().incomplete());
        QVERIFY(failed->status().incomplete());
    }
    // D1: a recording that cannot keep up makes a lossless run wait (and the
    // log says how long), but loses nothing.
    void slowRecordingThrottlesLosslessRun() {
        LocalSchemaRunner runner(nullptr, slowStorage());
        QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        QSignalSpy log(&runner, &LocalSchemaRunner::logMessage);
        runner.run(schema(3), .001, 150000, {{"scope/a", "source", 0}});
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 30000);
        QCOMPARE(runner.state(), RunState::Completed);
        const auto source = runner.recordedSource();
        QCOMPARE(source->status().samples, std::uint64_t(150000));
        QVERIFY(!source->status().incomplete());
        QTRY_VERIFY(logged(log, "run throttled by recording"));
        QCOMPARE(source->provenance().value("recording_overflow").toString(), QString("lossless"));
    }
    // D1 opt-in: the run never waits; lost samples are counted and mark the
    // recording incomplete, but the run itself completes.
    void dropModeNeverWaitsAndMarksLoss() {
        LocalSchemaRunner runner(nullptr, slowStorage());
        runner.setRecordingOverflow(RecordingOverflow::Drop);
        QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        QSignalSpy log(&runner, &LocalSchemaRunner::logMessage);
        runner.run(schema(3), .001, 150000, {{"scope/a", "source", 0}});
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 30000);
        QCOMPARE(runner.state(), RunState::Completed);
        const auto status = runner.recordedSource()->status();
        QVERIFY(status.captureDropped > 0);
        QVERIFY(status.incomplete());
        QCOMPARE(status.samples + status.captureDropped, std::uint64_t(150000));
        QTRY_VERIFY(logged(log, "the recording is incomplete"));
        QVERIFY(!logged(log, "run throttled by recording"));
        QCOMPARE(runner.recordedSource()->provenance().value("recording_overflow").toString(), QString("drop"));
    }
    // Pausing hands over the partial block, so viewers show the latest step.
    void pausePublishesLatestSamples() {
        gate_ = currentGate = std::make_shared<Gate>(); // closed: steps need permits
        LocalSchemaRunner runner;
        runner.run(schema(4), .001, 1000, {{"scope/a", "source", 0}});
        gate_->allow(10);
        QTRY_COMPARE(gate_->entered.load(), 11); // blocked inside step 11
        runner.pause();
        gate_->allow(1);
        QTRY_COMPARE(runner.state(), RunState::Paused);
        QTRY_COMPARE(runner.recordedSource()->status().samples, std::uint64_t(11));
        gate_->releaseAll();
        runner.resume();
        QTRY_COMPARE(runner.state(), RunState::Completed);
        QCOMPARE(runner.recordedSource()->status().samples, std::uint64_t(1000));
    }
    // Observation is not computation: more probes record the same values.
    void probeCountDoesNotChangeResults() {
        std::vector<std::vector<double>> runs;
        for (const auto& probes : std::vector<std::vector<RunProbe>>{
                 {{"position", "position", 0}},
                 {{"position", "position", 0}, {"velocity", "velocity", 0}, {"spring", "spring", 0},
                  {"live/position", "position", 0, "", false, "live"}, {"live/velocity", "velocity", 0, "", false, "live"}}}) {
            LocalSchemaRunner runner;
            QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
            runner.run(oscillator(), .001, 5000, probes);
            QTRY_COMPARE(complete.count(), 1);
            QCOMPARE(runner.state(), RunState::Completed);
            runs.push_back(recordedValues(*runner.recordedSource(), 0));
        }
        QCOMPARE(runs[0].size(), std::size_t(5000));
        QVERIFY(std::memcmp(runs[0].data(), runs[1].data(), runs[0].size() * sizeof(double)) == 0);
    }
    void invalidOrComplexProbesFailBeforeStepping() {
        LocalSchemaRunner runner;
        auto model = schema(); model.as_object()["elements"].as_array()[0].as_object()["type"] = "CaptureComplex";
        model.as_object()["elements"].as_array()[0].as_object()["params"] = boost::json::object{};
        complexSteps.store(0);
        runner.run(model, .001, 10, {{"scope/a", "source", 0}});
        QCOMPARE(runner.state(), RunState::Error); QCOMPARE(complexSteps.load(), 0);
        QVERIFY2(runner.recordedSource()->status().runError.contains("complex"), qPrintable(runner.recordedSource()->status().runError));
        for (const auto& probes : std::vector<std::vector<RunProbe>>{
            {{"a", "source", -1}}, {{"a", "missing", 0}}, {{"a", "source", 0}, {"a", "source", 0}}}) {
            runner.run(schema(), .001, 10, probes);
            QCOMPARE(runner.state(), RunState::Error); QCOMPARE(gate_->entered.load(), 0);
        }
    }
};
QTEST_GUILESS_MAIN(ObservationCaptureTest)
#include "observation_capture_test.moc"
