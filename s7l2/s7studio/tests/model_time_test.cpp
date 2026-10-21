// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "lifecycle_test_support.hpp"
#include "diagram_harness.hpp"
#include "linear_reference.hpp"
#include "diagram_document.hpp"
#include <s7/schema_library.hpp>
#include <QTemporaryDir>
#include <fstream>

using namespace s7studio;
class ModelTimeTest : public QObject {
    Q_OBJECT
    std::shared_ptr<Gate> gate_;
    std::unique_ptr<LocalSchemaRunner> runner_;
private slots:
    void initTestCase() { registerLifecycleElements(); }
    void init() {
        gate_ = currentGate = std::make_shared<Gate>();
        runner_ = std::make_unique<LocalSchemaRunner>();
    }
    void cleanup() { gate_->releaseAll(); runner_->reset(); runner_.reset(); }
    void finiteBoundaries() {
        using Controller = ui::ModelRunController;
        QCOMPARE(Controller::computeTicks(1L, .3), 3L); // nearest rounding used to overshoot
        QCOMPARE(Controller::computeTicks(1L, .6), 1L);
        QCOMPARE(Controller::computeTicks(1L, 2.), 0L);
        QCOMPARE(Controller::computeTicks(0L, .001), 0L);
        QCOMPARE(Controller::computeTicks(0, 0, 0, .001), 0L);
        QCOMPARE(Controller::computeTicks(0, 0, 1, .3), 3L);
        QCOMPARE(Controller::computeTicks(1L, .1), 10L);
        QCOMPARE(Controller::computeTicks(1L, 1. / 44100), 44100L);
        QCOMPARE(Controller::computeTicks(1L, 1. / 48000), 48000L);
        QCOMPARE(Controller::computeTicks(-1L, .001), -1L);
        QVERIFY_EXCEPTION_THROWN(static_cast<void>(Controller::computeTicks(-2L, .001)), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(static_cast<void>(Controller::computeTicks(1L, 0)), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(static_cast<void>(Controller::computeTicks(1L, std::numeric_limits<double>::quiet_NaN())), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(static_cast<void>(Controller::computeTicks(std::numeric_limits<long>::max(), 1e-9)), std::overflow_error);
        gate_->releaseAll();
        ui::ModelRunController controller(runner_.get());
        QSignalSpy completed(runner_.get(), &LocalSchemaRunner::runCompleted);
        for (const auto dt : {.3, 2.}) {
            const auto previous = completed.count();
            controller.run(schema(), dt, ui::RunExecutionConfig{1}, {{"scope/v", "source", 0}});
            QTRY_COMPARE(completed.count(), previous + 1);
            QCOMPARE(runner_->state(), RunState::Completed);
            const auto source = runner_->recordedSource();
            const auto expected = dt == .3 ? 3 : 0;
            QCOMPARE(runner_->completedTicks(), static_cast<long>(expected));
            QCOMPARE(source->status().samples, static_cast<std::uint64_t>(expected));
            if (expected) {
                const auto page = source->read(0);
                QCOMPARE(page.samples.front().step, 1);
                QCOMPARE(page.samples.front().timeSeconds, .3);
                QCOMPARE(page.samples.back().step, 3);
                QCOMPARE(page.samples.back().timeSeconds, .9);
            }
        }
    }
    void runRateMustMatchIntegrator() {
        auto model = schema();
        model.as_object()["elements"].as_array().push_back(boost::json::object{
            {"id", "integral"}, {"type", "EulerIntegrator"}, {"params", {{"dt", .001}}}});
        model.as_object()["connections"].as_array().push_back(boost::json::object{
            {"from_element", "source"}, {"from_port", 0}, {"to_element", "integral"}, {"to_port", 0}});
        runner_->run(model, 1. / 44100, 3, {{"scope/v", "integral", 0}});
        QCOMPARE(runner_->state(), RunState::Error);
        QCOMPARE(gate_->entered.load(), 0);
        QVERIFY(runner_->recordedSource()->status().runError.contains("conflicts with element 'integral'"));
    }
    void fractionalPauseResumeAndRerun() {
        const double dt = 1. / 44100;
        auto model = schema();
        model.as_object()["elements"].as_array().push_back(boost::json::object{
            {"id", "integral"}, {"type", "EulerIntegrator"}, {"params", {{"dt", dt}, {"initial_value", 0.}}}});
        model.as_object()["connections"].as_array().push_back(boost::json::object{
            {"from_element", "source"}, {"from_port", 0}, {"to_element", "integral"}, {"to_port", 0}});
        const std::vector<RunProbe> probes{{"scope/v", "integral", 0}};
        QSignalSpy completed(runner_.get(), &LocalSchemaRunner::runCompleted);
        runner_->run(model, dt, 3, probes);
        const auto first = runner_->recordedSource();
        for (int step = 1; step <= 2; ++step) {
            QTRY_COMPARE(gate_->entered.load(), step);
            runner_->pause(); gate_->allow();
            QTRY_COMPARE(runner_->state(), RunState::Paused);
            QCOMPARE(runner_->completedTicks(), static_cast<long>(step));
            QCOMPARE(first->status().samples, static_cast<std::uint64_t>(step));
            const auto samples = first->read(0).samples;
            QCOMPARE(samples.back().step, step);
            QCOMPARE(samples.back().timeSeconds, static_cast<double>(step) / 44100);
            QCOMPARE(gate_->entered.load(), step); // no extra step while acknowledged paused
            runner_->resume();
        }
        QTRY_COMPARE(gate_->entered.load(), 3);
        gate_->allow();
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(runner_->state(), RunState::Completed);
        gate_->releaseAll();
        runner_->run(model, dt, 3, probes);
        QTRY_COMPARE(completed.count(), 2);
        const auto second = runner_->recordedSource();
        QVERIFY(second->runId() > first->runId());
        const auto a = first->read(0).samples, b = second->read(0).samples;
        QCOMPARE(a.size(), 3); QCOMPARE(a.size(), b.size());
        for (qsizetype i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].step, i + 1); QCOMPARE(a[i].step, b[i].step);
            QVERIFY(a[i].timeSeconds == b[i].timeSeconds);
            QVERIFY(a[i].value == b[i].value);
            QVERIFY(std::abs(a[i].value - static_cast<double>(i) * dt) < 1e-18);
        }
    }
    void linearMatrix_data() {
        QTest::addColumn<QString>("model");
        QTest::addColumn<QString>("method");
        for (const auto* model : {"rc_charge", "rc_discharge", "rlc_step", "rlc_resonance", "mass_spring_damper"})
            for (const auto* method : {"euler", "symplectic"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(model, method))) << QString(model) << QString(method);
    }
    void linearMatrix() {
        QFETCH(QString, model); QFETCH(QString, method);
        double errors[2]{};
        for (int refinement = 0; refinement < 2; ++refinement) {
            const double hz = refinement ? 2000. : 1000., dt = 1 / hz;
            const auto name = model.toStdString();
            auto json = s7tests::read_json_object(std::filesystem::path(S7_SOURCE_DIR) /
                "examples/linear" / (name + "_" + method.toStdString() + ".s7diagram"));
            json["dt"] = dt;
            s7::configure_schema_discretization(json.at("schema"), dt);
            QTemporaryDir directory; QVERIFY(directory.isValid());
            const auto path = std::filesystem::path(directory.path().toStdString());
            { std::ofstream out(path / "model.s7diagram"); out << boost::json::serialize(json); }
            { std::ofstream out(path / "model.probes.json"); out << boost::json::serialize(boost::json::object{{"probes", json.at("probes")}}); }
            auto headless = s7tests::load_and_compile(path / "model.s7diagram", path / "model.probes.json");
            const auto document = diagram::DiagramDocument::fromJson(s7tests::read_json_object(path / "model.s7diagram"));
            const auto duration = static_cast<long>(s7tests::linear_duration(name));
            const auto ticks = ui::ModelRunController::computeTicks(duration, document.dt);
            const auto reference = s7tests::run_and_sample(headless, static_cast<std::size_t>(ticks));
            std::vector<RunProbe> probes;
            for (const auto& probe : headless.probes)
                probes.push_back({probe.name, probe.point.element_id, static_cast<int>(probe.point.port)});
            QSignalSpy completed(runner_.get(), &LocalSchemaRunner::runCompleted);
            ui::ModelRunController controller(runner_.get());
            controller.run(document.runtimeSchema, document.dt, ui::RunExecutionConfig{duration}, probes);
            QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 30000);
            QVERIFY2(completed[0][0].toBool(), qPrintable(completed[0][1].toString()));
            const auto source = runner_->recordedSource();
            QCOMPARE(source->status().samples, static_cast<std::uint64_t>(ticks) * probes.size());
            QVERIFY(source->status().finished && !source->status().incomplete());
            std::uint64_t cursor = 0;
            while (cursor < source->status().samples) {
                const auto page = source->read(cursor);
                QVERIFY(page.error.isEmpty()); QVERIFY(page.next > cursor);
                for (const auto& sample : page.samples) {
                    const auto step = cursor / probes.size(), channel = cursor % probes.size();
                    const auto& snapshot = reference[step];
                    QCOMPARE(sample.step, static_cast<std::int64_t>(step + 1));
                    QCOMPARE(sample.channel, static_cast<observation::ChannelId>(channel));
                    QVERIFY(sample.timeSeconds == static_cast<double>(step + 1) / hz);
                    QVERIFY(sample.timeSeconds == *snapshot.model_time_seconds);
                    QCOMPARE(*snapshot.model_time_ns, std::llround(sample.timeSeconds * 1e9));
                    QVERIFY(sample.value == snapshot.samples[channel].as_real());
                    const auto expected = s7tests::linear_reference(name, static_cast<double>(step) / hz);
                    // First channels are physical states; remaining input is
                    // independently checked as the post-switch source signal.
                    const bool rc = name.starts_with("rc_");
                    const auto states = rc ? 1u : name.starts_with("rlc_") ? 3u : 2u;
                    double target;
                    if (channel < states) target = expected[channel];
                    else if (name == "rlc_resonance") target = std::sin(2 * static_cast<double>(step) / hz);
                    else target = name == "rc_charge" || name == "rlc_step" ? 1. : 0.;
                    QVERIFY(std::isfinite(sample.value));
                    const double error = std::abs(sample.value - target);
                    if (step == 0) QVERIFY(error < 1e-12);
                    errors[refinement] = std::max(errors[refinement], error);
                    ++cursor;
                }
                QCOMPARE(cursor, page.next);
            }
            QCOMPARE(reference.front().step, std::size_t(1));
            QCOMPARE(reference.back().step, static_cast<std::size_t>(ticks));
            QCOMPARE(*reference.back().model_time_seconds, static_cast<double>(duration));
            QVERIFY(errors[refinement] < .05);
        }
        QVERIFY(errors[0] / errors[1] > 1.8 && errors[0] / errors[1] < 2.2);
        qInfo("%s %s max absolute error: %.10g -> %.10g; refinement %.6f",
              qPrintable(model), qPrintable(method), errors[0], errors[1], errors[0] / errors[1]);
    }
    void sameSavedModel_data() {
        QTest::addColumn<QString>("file"); QTest::addColumn<double>("hz");
        QTest::newRow("linear-44.1kHz") << QString("s7tests/diagrams/euler_scalar_decay.s7diagram") << 44100.;
        QTest::newRow("linear-48kHz") << QString("s7tests/diagrams/euler_scalar_decay.s7diagram") << 48000.;
        QTest::newRow("nonlinear-44.1kHz") << QString("examples/pendulum/nonlinear_pendulum_euler.s7diagram") << 44100.;
    }
    void sameSavedModel() {
        QFETCH(QString, file); QFETCH(double, hz);
        auto model = s7tests::read_json_object(std::filesystem::path(S7_SOURCE_DIR) / file.toStdString());
        const double dt = 1 / hz;
        model["dt"] = dt;
        s7::configure_schema_discretization(model.at("schema"), dt);
        if (!model.contains("probes"))
            model["probes"] = s7tests::read_json_object(std::filesystem::path(S7_SOURCE_DIR) /
                "s7tests/diagrams/euler_scalar_decay.probes.json").at("probes");
        // Save once, then use the production document parser and the separate
        // headless loader on exactly the same file/configuration.
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = std::filesystem::path(directory.path().toStdString());
        { std::ofstream out(path / "model.s7diagram"); out << boost::json::serialize(model); }
        { std::ofstream out(path / "model.probes.json"); out << boost::json::serialize(boost::json::object{{"probes", model.at("probes")}}); }
        auto headless = s7tests::load_and_compile(path / "model.s7diagram", path / "model.probes.json");
        const auto saved = s7tests::read_json_object(path / "model.s7diagram");
        const auto document = diagram::DiagramDocument::fromJson(saved);
        const auto ticks = ui::ModelRunController::computeTicks(1L, document.dt);
        const auto reference = s7tests::run_and_sample(headless, static_cast<std::size_t>(ticks));
        std::vector<RunProbe> probes;
        for (const auto& probe : headless.probes) probes.push_back({probe.name, probe.point.element_id, static_cast<int>(probe.point.port)});
        QSignalSpy completed(runner_.get(), &LocalSchemaRunner::runCompleted);
        ui::ModelRunController controller(runner_.get());
        controller.run(document.runtimeSchema, document.dt, ui::RunExecutionConfig{1}, probes);
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
        QVERIFY2(completed[0][0].toBool(), qPrintable(completed[0][1].toString()));
        const auto source = runner_->recordedSource();
        QCOMPARE(source->status().samples, static_cast<std::uint64_t>(ticks) * probes.size());
        QVERIFY(source->status().finished && !source->status().incomplete());
        std::uint64_t cursor = 0;
        while (cursor < source->status().samples) {
            const auto page = source->read(cursor); QVERIFY(page.error.isEmpty()); QVERIFY(page.next > cursor);
            for (const auto& sample : page.samples) {
                const auto step = cursor / probes.size(), channel = cursor % probes.size();
                const auto& snapshot = reference[step];
                QCOMPARE(sample.step, static_cast<std::int64_t>(step + 1));
                QCOMPARE(sample.channel, static_cast<observation::ChannelId>(channel));
                QVERIFY(sample.timeSeconds == *snapshot.model_time_seconds);
                QVERIFY(sample.value == snapshot.samples[channel].as_real());
                // Independent timestamp reference; ns error is bounded per sample.
                const long double expected = static_cast<long double>(step + 1) / hz;
                QVERIFY(std::abs(sample.timeSeconds - expected) <= 3e-16L);
                QCOMPARE(*snapshot.model_time_ns, std::llround(expected * 1e9L));
                ++cursor;
            }
            QCOMPARE(cursor, page.next);
        }
        QCOMPARE(reference.front().step, std::size_t(1));
        QCOMPARE(reference.back().step, static_cast<std::size_t>(ticks));
        QCOMPARE(*reference.back().model_time_seconds, 1.);
        if (file.contains("scalar")) {
            // Existing causal-memory contract: step 1 publishes x0, step N
            // publishes N-1 Euler updates, while timestamps identify step N.
            QCOMPARE(reference.front().samples[0].as_real(), 1.);
            QVERIFY(std::abs(reference.back().samples[0].as_real() - std::pow(1 - dt, ticks - 1)) < 2e-12);
        } else {
            QCOMPARE(reference.front().samples[0].as_real(), 1.5);
            QCOMPARE(reference.front().samples[1].as_real(), 0.);
        }
    }
};
QTEST_GUILESS_MAIN(ModelTimeTest)
#include "model_time_test.moc"
