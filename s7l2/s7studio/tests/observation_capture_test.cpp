// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "lifecycle_test_support.hpp"
#include "observation/scope_results.hpp"
#include <QBuffer>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

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
    void capturesAllSamplesWhenPreviewOverflows() {
        LocalSchemaRunner runner;
        QSignalSpy complete(&runner, &LocalSchemaRunner::runCompleted);
        const double value = std::nextafter(1., 2.);
        runner.run(schema(value), .001, 110000, {{"scope/a", "source", 0}, {"scope/b", "source", 0}});
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 15000);
        QCOMPARE(runner.state(), RunState::Completed);
        const auto preview = runner.takeScopeSamples();
        QCOMPARE(preview.samples.size(), std::size_t(200000));
        QCOMPARE(preview.dropped, std::uint64_t(20000));
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
