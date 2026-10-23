// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "observation/live_acquisition.hpp"
#include <QtTest>
#include <QJsonDocument>
#include <atomic>
#include <thread>
#include <cmath>
#include <random>
using namespace s7studio::observation;
namespace {
void append(LiveAcquisition& a, std::int64_t step, double value) { const double values[]{value, -value}; a.append(step, static_cast<double>(step)/1024, values); }
TriggerSettings settings(TriggerMode mode = TriggerMode::Normal) { TriggerSettings s; s.mode = mode; return s; }
}
class TriggerTest : public QObject {
    Q_OBJECT
private slots:
    void noisySineNeedsHysteresisForOneFlank() {
        // 2 Hz sine plus Gaussian noise (sigma 0.15) at 1 kHz, as in examples/demo_diagram2.
        auto fallingCaptures = [](double hysteresis) {
            LiveAcquisition a(1, .001, {{"x", "V", "f64"}, {"y", "A", "f64"}}, {}, 1000, 1);
            auto s = settings(TriggerMode::Normal); s.hysteresis = hysteresis; a.configureTrigger(s);
            std::mt19937_64 rng(42); std::normal_distribution<double> noise(0, .15);
            std::uint64_t seen = 0; int captures = 0, falling = 0;
            for (int i = 1; i <= 120000; ++i) {
                const double v[]{std::sin(2*M_PI*2*i*.001)+noise(rng), 0}; a.append(i, i*.001, v);
                const auto id = a.triggerStatus().captureId;
                if (id == seen) continue;
                seen = id; const auto f = a.triggerSnapshot(); if (!f || !f->trigger->triggered) continue;
                ++captures; if (std::cos(2*M_PI*2*static_cast<double>(f->trigger->step)*.001) < 0) ++falling;
            }
            return std::pair{captures, falling};
        };
        const auto [noisy, wrong] = fallingCaptures(0); QVERIFY(noisy > 20); QVERIFY(wrong > noisy/4); // noise crossings on the falling flank
        const auto [clean, none] = fallingCaptures(1); QVERIFY(clean > 20); QCOMPARE(none, 0);       // band wider than the noise
    }
    void presetJsonValidatesAndRoundTrips() {
        QString error;
        auto defaults = triggerSettingsFromJson({}, &error); QVERIFY(defaults); QVERIFY(error.isEmpty());
        QVERIFY(*defaults == TriggerSettings{});
        TriggerSettings s; s.mode = TriggerMode::Single; s.channel = 2; s.rising = false; s.level = std::nextafter(.25, 1.);
        s.hysteresis = 1e-3; s.pretriggerPercent = 40; s.holdoffSteps = 7; s.autoTimeoutSteps = 900000000;
        const auto json = triggerSettingsToJson(s);
        QCOMPARE(json["mode"].toString(), QString("single")); QCOMPARE(json["edge"].toString(), QString("falling"));
        const auto text = QJsonDocument(json).toJson(QJsonDocument::Compact);
        const auto back = triggerSettingsFromJson(QJsonDocument::fromJson(text).object()); QVERIFY(back); QVERIFY(*back == s);
        const auto partial = triggerSettingsFromJson({{"mode", "auto"}, {"hysteresis", 1}}); QVERIFY(partial);
        QCOMPARE(partial->mode, TriggerMode::Auto); QCOMPARE(partial->hysteresis, 1.); QCOMPARE(partial->pretriggerPercent, 25);
        const QList<QJsonObject> bad{{{"mode", "free"}}, {{"edge", 1}}, {{"channel", 1.5}}, {{"channel", 8}}, {{"level", "0"}},
            {{"hysteresis", -1}}, {{"pretrigger_percent", 101}}, {{"auto_timeout_steps", 0}}, {{"holdoff_steps", 2e9}}};
        for (const auto& json : bad) {
            QVERIFY2(!triggerSettingsFromJson(json, &error), QJsonDocument(json).toJson().constData());
            QVERIFY(!error.isEmpty());
        }
    }
    void periodicFramesStayAlignedWithoutGuiPolling() {
        LiveAcquisition a(1, 1./1024, {{"x", "V", "f64"}, {"y", "A", "f64"}}, {}, 1024, 1);
        a.setCapacity(9); QVERIFY(a.configureTrigger(settings())); const auto allocated = a.allocatedBytes();
        for (int i = 0; i < 200000; ++i) append(a, i, i%16-8);
        auto f = a.triggerSnapshot(); QVERIFY(f); QVERIFY(f->trigger->triggered); QVERIFY(!f->trigger->partial);
        QCOMPARE(f->steps.size(), std::size_t(9)); QCOMPARE(f->trigger->step%16, std::int64_t(8));
        QCOMPARE(f->steps[2], f->trigger->step); QCOMPARE(f->plottedTime(2), 0.);
        QCOMPARE(f->plottedTime(0), -2./1024); QCOMPARE(f->plottedTime(8), 6./1024);
        QCOMPARE(f->values[4], 0.); QCOMPARE(f->times[2], static_cast<double>(f->trigger->step)/1024);
        for (std::size_t i = 0; i < 9; ++i) { QCOMPARE(f->values[2*i], static_cast<double>(static_cast<int>(i)-2)); QCOMPARE(f->values[2*i+1], -f->values[2*i]); }
        QCOMPARE(a.allocatedBytes(), allocated); QCOMPARE(*f->nearestPlotted(0), std::size_t(2));
        QCOMPARE(f->envelope(0, 100).at(2).x(), 0.);
        const auto old = *f;
        for (int i = 200000; i < 201000; ++i) append(a, i, i%16-8);
        f = a.triggerSnapshot(); QVERIFY(f); QVERIFY(f->trigger->id > old.trigger->id);
        QCOMPARE(f->values, old.values); QCOMPARE(f->plottedTime(2), old.plottedTime(2));
    }
    void hysteresisFallingPlateausAndPrehistory() {
        LiveAcquisition a(1, 1./1024, {{"x", {}, "f64"}, {"y", {}, "f64"}});
        a.setCapacity(3); auto s = settings(TriggerMode::Single); s.pretriggerPercent = 0; s.hysteresis = 2;
        QVERIFY(a.configureTrigger(s));
        const double input[]{-.2, .2, -.1, .1, -1, .3, .8, 1, 2, 3};
        for (int i = 0; i < 10; ++i) append(a, i, input[i]);
        auto f = a.triggerSnapshot(); QVERIFY(f); QCOMPARE(f->trigger->step, std::int64_t(7));
        QCOMPARE(a.triggerStatus().state, TriggerState::Held);
        s.rising = false; QVERIFY(a.configureTrigger(s));
        const double fall[]{0, .2, -.2, 1, .5, -1, -2, -3};
        for (int i = 0; i < 8; ++i) append(a, 10+i, fall[i]);
        f = a.triggerSnapshot(); QVERIFY(f); QCOMPARE(f->trigger->step, std::int64_t(15));
        s.rising = true; s.hysteresis = 0; s.pretriggerPercent = 100; QVERIFY(a.configureTrigger(s));
        append(a, 18, -1); append(a, 19, 1); QVERIFY(!a.triggerSnapshot()); // Two pre-trigger positions not yet available.
        append(a, 20, -1); append(a, 21, 0);
        f = a.triggerSnapshot(); QVERIFY(f); QCOMPARE(f->trigger->step, std::int64_t(21)); QCOMPARE(f->steps.back(), f->trigger->step);
        a.rearmTrigger();
        for (int i = 22; i < 100; ++i) append(a, i, 0);
        QCOMPARE(a.triggerSnapshot()->trigger->id, f->trigger->id); // Equal-to-level plateau cannot retrigger.
    }
    void singleRearmAndModelStepHoldoff() {
        LiveAcquisition a(1, 1./1024, {{"x", {}, "f64"}, {"y", {}, "f64"}});
        a.setCapacity(2); auto s = settings(TriggerMode::Single); s.pretriggerPercent = 0;
        QVERIFY(a.configureTrigger(s));
        for (int i = 0; i < 20; ++i) append(a, i, i%2 ? 1 : -1);
        auto first = a.triggerSnapshot(); QVERIFY(first); QCOMPARE(first->trigger->step, std::int64_t(1));
        QCOMPARE(a.snapshot()->steps.back(), std::int64_t(19));
        a.rearmTrigger();
        for (int i = 20; i < 30; ++i) append(a, i, i%2 ? 1 : -1);
        auto second = a.triggerSnapshot(); QVERIFY(second); QCOMPARE(second->trigger->step, std::int64_t(21));
        QVERIFY(second->trigger->id > first->trigger->id);
        s.mode = TriggerMode::Normal; s.holdoffSteps = 5; QVERIFY(a.configureTrigger(s));
        std::vector<std::int64_t> triggers;
        std::uint64_t id = 0;
        for (int i = 30; i < 100; ++i) {
            append(a, i, i%2 ? 1 : -1);
            if (auto f = a.triggerSnapshot(); f && f->trigger->id != id) { triggers.push_back(f->trigger->step); id = f->trigger->id; }
        }
        QVERIFY(triggers.size() > 3);
        for (std::size_t i = 1; i < triggers.size(); ++i) QVERIFY(triggers[i]-(triggers[i-1]+1) > 5);
    }
    void autoTimeoutNormalWaitAndPartialStop() {
        LiveAcquisition a(1, 1./1024, {{"x", {}, "f64"}, {"y", {}, "f64"}});
        a.setCapacity(8); auto s = settings(); s.level = 10; QVERIFY(a.configureTrigger(s));
        for (int i = 0; i < 50; ++i) append(a, i, 1);
        QVERIFY(!a.triggerSnapshot());
        s.mode = TriggerMode::Auto; s.autoTimeoutSteps = 12; QVERIFY(a.configureTrigger(s));
        for (int i = 50; i < 62; ++i) append(a, i, 1);
        QVERIFY(!a.triggerSnapshot()); append(a, 62, 1);
        auto f = a.triggerSnapshot(); QVERIFY(f); QVERIFY(!f->trigger->triggered); QCOMPARE(f->steps.front(), std::int64_t(55));
        QCOMPARE(f->plottedTime(0), f->times.front());
        s.mode = TriggerMode::Single; s.level = 0; s.pretriggerPercent = 25; QVERIFY(a.configureTrigger(s));
        append(a, 63, -1); append(a, 64, 1); append(a, 65, 2); QVERIFY(!a.triggerSnapshot()); a.finish();
        f = a.triggerSnapshot(); QVERIFY(f); QVERIFY(f->trigger->partial); QVERIFY(f->finished);
        QCOMPARE(f->steps.size(), std::size_t(3)); QCOMPARE(f->capacity, std::size_t(8)); QCOMPARE(f->plottedTime(1), 0.);
    }
    void gapsNonfiniteAndConfigurationDiscardPendingCaptures() {
        LiveAcquisition a(1, 1./1024, {{"x", {}, "f64"}, {"y", {}, "f64"}});
        a.setCapacity(8); auto s = settings(); s.pretriggerPercent = 0; QVERIFY(a.configureTrigger(s));
        append(a, 0, -1); append(a, 1, 1); append(a, 5, 2); QVERIFY(!a.triggerSnapshot()); QCOMPARE(a.triggerStatus().aborted, std::uint64_t(1));
        append(a, 6, -1); append(a, 7, 1); append(a, 8, std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(a.triggerStatus().aborted, std::uint64_t(2));
        for (int i = 9; i < 30; ++i) append(a, i, i%2 ? -1 : 1);
        QVERIFY(a.triggerSnapshot()); auto invalid = s; invalid.hysteresis = -1; QString error;
        QVERIFY(!a.configureTrigger(invalid, &error)); QVERIFY(!error.isEmpty()); QVERIFY(a.triggerSnapshot());
        a.setCapacity(4); QVERIFY(!a.triggerSnapshot());
        for (int i = 30; i < 40; ++i) append(a, i, i%2 ? -1 : 1);
        QCOMPARE(a.triggerSnapshot()->steps.size(), std::size_t(4));
    }
    void concurrentPublicationNeverMixesCaptureBanks() {
        LiveAcquisition a(1, 1./1024, {{"x", {}, "f64"}, {"y", {}, "f64"}});
        a.setCapacity(1024); auto s = settings(); s.pretriggerPercent = 25; QVERIFY(a.configureTrigger(s));
        std::atomic<bool> done{false};
        std::thread producer([&] { for (int i = 0; i < 300000; ++i) append(a, i, i%128-64); done = true; });
        bool coherent = true; std::size_t captures = 0;
        while (!done.load()) if (auto f = a.triggerSnapshot()) {
            ++captures;
            for (std::size_t i = 0; i < f->steps.size(); ++i)
                if (f->values[2*i] != static_cast<double>(f->steps[i]%128-64) || f->values[2*i+1] != -f->values[2*i]) coherent = false;
        }
        producer.join(); QVERIFY(coherent); QVERIFY(captures > 0);
    }
};
QTEST_GUILESS_MAIN(TriggerTest)
#include "live_trigger_test.moc"
