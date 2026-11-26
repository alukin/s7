// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "lifecycle_test_support.hpp"

namespace {
struct HardwareTrace {
    std::thread::id preparedOn;
    std::atomic<bool> closedOnWorker{};
};
HardwareTrace hardwareTrace;

class TestHardwareRun final : public s7::hardware::LiveRun {
public:
    TestHardwareRun(s7::ExecutableGraph graph, s7::Rate rate) : runtime_(std::move(graph)) {
        runtime_.set_model_time_rate(rate);
    }
    ~TestHardwareRun() override {
        hardwareTrace.closedOnWorker.store(hardwareTrace.preparedOn == std::this_thread::get_id());
    }
    s7::NodeRuntime& runtime() noexcept override { return runtime_; }
    std::string prepare() override {
        hardwareTrace.preparedOn = std::this_thread::get_id();
        return "test hardware";
    }
    void execute(std::uint64_t, std::stop_token stop, const s7::hardware::Run::StepObserver& observe) override {
        runtime_.step();
        observe(1);
        std::mutex mutex;
        std::condition_variable_any condition;
        std::unique_lock lock(mutex);
        condition.wait(lock, stop, [] { return false; });
    }
private:
    s7::NodeRuntime runtime_;
};
}

class LifecycleTest : public QObject {
    Q_OBJECT
    std::shared_ptr<Gate> gate_;
    std::unique_ptr<s7studio::LocalSchemaRunner> runner_;
    const std::vector<s7studio::RunProbe> probes_{{"scope/value", "source", 0}};
private slots:
    void initTestCase() { registerLifecycleElements(); }
    void init() {
        gate_ = currentGate = std::make_shared<Gate>();
        runner_ = std::make_unique<s7studio::LocalSchemaRunner>();
    }
    void cleanup() {
        gate_->releaseAll();
        if (runner_) runner_->reset();
        QVERIFY(!gate_->destroyedInStep.load());
        runner_.reset();
        QCoreApplication::sendPostedEvents();
    }
    void pauseAcknowledgesStepAndStopWakesWorker() {
        using enum s7studio::RunState;
        s7studio::ui::ModelRunController controller(runner_.get());
        QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
        QSignalSpy states(runner_.get(), &s7studio::LocalSchemaRunner::stateChanged);
        runner_->run(schema(), .001, -1, probes_);
        const auto configuration = runner_->activeConfiguration();
        QVERIFY(configuration);
        QTRY_COMPARE(gate_->entered.load(), 1);
        runner_->pause();
        QCOMPARE(runner_->state(), Pausing);
        QVERIFY(!runner_->isPaused());
        QVERIFY(!controller.actionState().pauseEnabled && !controller.actionState().resumeEnabled);
        QCOMPARE(runner_->completedTicks(), 0L);
        gate_->allow();
        QTRY_COMPARE(runner_->state(), Paused);
        QTRY_VERIFY(controller.actionState().resumeEnabled);
        QCOMPARE(runner_->completedTicks(), 1L);
        // An acknowledged pause has delivered the paused step to the recording.
        const auto recording = runner_->recordedSource();
        QVERIFY(recording);
        QCOMPARE(recording->runId(), configuration->id);
        QCOMPARE(recording->status().samples, std::uint64_t(1));
        QCOMPARE(recording->read(0).samples.front().step, std::int64_t(1));
        QCOMPARE(gate_->entered.load(), 1); // acknowledged pause has no in-flight step
        runner_->stop();
        QTRY_COMPARE(runner_->state(), Stopped);
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.front().at(0).toBool());
        QVERIFY(controller.actionState().runEnabled);
        QVERIFY(!controller.actionState().stopEnabled);
        QVERIFY(states.count() >= 5);
        runner_->reset();
        QCOMPARE(runner_->state(), Idle);
        QVERIFY(!runner_->activeConfiguration());
        QCOMPARE(configuration->dt, .001); // retained const snapshots survive reset
    }
    void configurationAndSamplesStayWithTheirRun() {
        auto json = schema(2.5);
        auto probes = probes_;
        QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
        runner_->run(json, .001, 1, probes);
        const auto first = runner_->activeConfiguration();
        QTRY_COMPARE(gate_->entered.load(), 1);
        json.as_object().at("elements").as_array()[0].as_object().at("params").as_object()["value"] = 9.0;
        probes[0].name = "wrong/value";
        gate_->allow();
        QTRY_COMPARE(completed.count(), 1);
        auto source = runner_->recordedSource();
        QVERIFY(source);
        auto page = source->read(0);
        QCOMPARE(page.samples.size(), qsizetype(1));
        QCOMPARE(page.samples[0].value, 2.5);
        QCOMPARE(source->channels().at(page.samples[0].channel).name, QStringLiteral("scope/value"));
        QVERIFY(first->schemaJson != json);
        runner_->run(json, .001, 1, probes_);
        const auto second = runner_->activeConfiguration();
        QVERIFY(second->id > first->id);
        QTRY_COMPARE(gate_->entered.load(), 2);
        gate_->allow();
        QTRY_COMPARE(completed.count(), 2);
        source = runner_->recordedSource();
        QVERIFY(source);
        QCOMPARE(source->runId(), second->id);
        page = source->read(0);
        QCOMPARE(page.samples.size(), qsizetype(1));
        QCOMPARE(page.samples[0].step, std::int64_t(1));
        QCOMPARE(page.samples[0].value, 9.0);
        QCOMPARE(runner_->state(), s7studio::RunState::Completed);
    }
    void queuedEventsAreInvalidAfterResetOrRerun() {
        QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
        QSignalSpy samples(runner_.get(), &s7studio::LocalSchemaRunner::recordingAdvanced);
        // QTRY's event pumping would deliver notifications: block only on a
        // condition-variable gate and state predicate on a helper thread instead.
        gate_->releaseAll();
        runner_->run(schema(), .001, 1, probes_);
        std::mutex mutex;
        std::condition_variable changed;
        bool finished = false;
        std::thread wait([&] {
            while (runner_->isRunning()) std::this_thread::yield();
            { std::lock_guard lock(mutex); finished = true; }
            changed.notify_one();
        });
        { std::unique_lock lock(mutex); const bool done = changed.wait_for(lock, std::chrono::seconds(5), [&] { return finished; });
          if (!done) { runner_->stop(); } }
        wait.join();
        runner_->reset(); // joins the worker and invalidates queued owner events
        QCoreApplication::sendPostedEvents();
        QCOMPARE(completed.count(), 0);
        QCOMPARE(samples.count(), 0);
        QVERIFY(!runner_->recordedSource());
        runner_->run(schema(), .001, 1, probes_);
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(samples.count(), 1);
    }
    void failureAndRecovery_data() {
        QTest::addColumn<bool>("unknown");
        QTest::newRow("standard-exception") << false;
        QTest::newRow("unknown-exception") << true;
    }
    void failureAndRecovery() {
        QFETCH(bool, unknown);
        QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
        gate_->fail.store(!unknown); gate_->unknownFailure.store(unknown);
        runner_->run(schema(), .001, 1, probes_);
        QTRY_COMPARE(gate_->entered.load(), 1);
        gate_->allow();
        QTRY_COMPARE(completed.count(), 1);
        QCOMPARE(runner_->state(), s7studio::RunState::Error);
        QVERIFY(!completed.front().at(0).toBool());
        QVERIFY(!completed.front().at(1).toString().isEmpty());
        gate_->fail.store(false); gate_->unknownFailure.store(false);
        runner_->run(schema(7), .001, 1, probes_);
        QTRY_COMPARE(gate_->entered.load(), 2);
        gate_->allow();
        QTRY_COMPARE(completed.count(), 2);
        QVERIFY(completed.last().at(0).toBool());
        QVERIFY(runner_->recordedSource());
        QCOMPARE(runner_->recordedSource()->read(0).samples.front().value, 7.0);
    }
    void invalidPreparationClearsPriorSamples() {
        QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
        gate_->releaseAll();
        runner_->run(schema(), .001, 1, probes_);
        QTRY_COMPARE(completed.count(), 1);
        const auto oldId = runner_->activeConfiguration()->id;
        runner_->run(boost::json::object{}, .001, 1, probes_);
        QCOMPARE(runner_->state(), s7studio::RunState::Error);
        QVERIFY(runner_->activeConfiguration()->id > oldId);
        QVERIFY(!runner_->recordedSource() || runner_->recordedSource()->status().samples == 0);
        QVERIFY(!completed.last().at(0).toBool());
        runner_->run(schema(), std::numeric_limits<double>::quiet_NaN(), 1, probes_);
        QCOMPARE(runner_->state(), s7studio::RunState::Error);
        runner_->run(schema(), .001, -2, probes_);
        QCOMPARE(runner_->state(), s7studio::RunState::Error);
    }
    void cancellationDuringPreparation() {
        using enum s7studio::RunState;
        {
            QObject lifetime;
            QObject::connect(runner_.get(), &s7studio::LocalSchemaRunner::stateChanged, &lifetime,
                [&](s7studio::RunState state) { if (state == Preparing) runner_->reset(); });
            runner_->run(schema(), .001, -1, probes_);
            QCOMPARE(runner_->state(), Idle);
            QVERIFY(!runner_->activeConfiguration());
            QCOMPARE(gate_->entered.load(), 0);
        }
        {
            QObject lifetime;
            QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
            QObject::connect(runner_.get(), &s7studio::LocalSchemaRunner::stateChanged, &lifetime,
                [&](s7studio::RunState state) { if (state == Prepared) runner_->stop(); });
            runner_->run(schema(), .001, -1, probes_);
            QCOMPARE(runner_->state(), Stopped);
            QTRY_COMPARE(completed.count(), 1);
            QCOMPARE(gate_->entered.load(), 0);
        }
    }
    void destructionJoinsInFlightStep() {
        runner_->run(schema(), .001, -1, probes_);
        QTRY_COMPARE(gate_->entered.load(), 1);
        QObject lifetime;
        bool stopping = false;
        QObject::connect(runner_.get(), &s7studio::LocalSchemaRunner::stateChanged, &lifetime,
            [&](s7studio::RunState state) {
                if (state == s7studio::RunState::Stopping) { stopping = true; gate_->releaseAll(); }
            }, Qt::DirectConnection);
        runner_.reset();
        QVERIFY(stopping);
        QCOMPARE(gate_->inStep.load(), 0);
        QVERIFY(!gate_->destroyedInStep.load());
        QCoreApplication::sendPostedEvents();
    }
    void hardwareStopsAndReleasesOnWorker() {
        using enum s7studio::RunState;
        hardwareTrace.preparedOn = {};
        hardwareTrace.closedOnWorker.store(false);
        runner_->setHardwareFactory([](s7::ExecutableGraph& graph, s7::Rate rate) -> s7::hardware::LiveRun* {
            return new TestHardwareRun(std::move(graph), rate);
        });
        s7studio::ui::ModelRunController controller(runner_.get());
        QSignalSpy completed(runner_.get(), &s7studio::LocalSchemaRunner::runCompleted);
        runner_->run(schema(), .001, -1, probes_);
        gate_->allow();
        QTRY_COMPARE(runner_->completedTicks(), 1L);
        QVERIFY(hardwareTrace.preparedOn != std::this_thread::get_id());
        QVERIFY(!controller.actionState().pauseEnabled);
        runner_->pause();
        QCOMPARE(runner_->state(), Running);
        QVERIFY(controller.actionState().stopEnabled);
        runner_->stop();
        QTRY_COMPARE(completed.count(), 1);
        QVERIFY(completed.front().at(0).toBool());
        QCOMPARE(runner_->state(), Stopped);
        QVERIFY(hardwareTrace.closedOnWorker.load());
        QVERIFY(runner_->recordedSource());
        const auto page = runner_->recordedSource()->read(0);
        QCOMPARE(page.samples.size(), qsizetype(1));
        QCOMPARE(page.samples.front().step, std::int64_t(1));
    }
    void rapidTransitions() {
        using enum s7studio::RunState;
        for (int iteration = 0; iteration < 20; ++iteration) {
            gate_ = currentGate = std::make_shared<Gate>();
            runner_->run(schema(), .001, -1, probes_);
            QTRY_COMPARE(gate_->entered.load(), 1);
            runner_->pause(); runner_->resume(); runner_->pause();
            gate_->allow();
            QTRY_COMPARE(runner_->state(), Paused);
            runner_->resume();
            QTRY_COMPARE(gate_->entered.load(), 2);
            runner_->stop();
            QCOMPARE(runner_->state(), Stopping);
            gate_->allow();
            QTRY_COMPARE(runner_->state(), Stopped);
            runner_->reset();
            QCOMPARE(runner_->state(), Idle);
        }
    }
};
QTEST_GUILESS_MAIN(LifecycleTest)
#include "run_lifecycle_test.moc"
