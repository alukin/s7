// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "observation/live_acquisition.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using namespace s7studio::observation;
using Clock = std::chrono::steady_clock;

namespace {
double percentile(std::vector<double> values, double p)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[std::min(values.size()-1, static_cast<std::size_t>(p*static_cast<double>(values.size()-1)))];
}
long peakRssMiB()
{
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) if (line.starts_with("VmHWM:")) {
        std::istringstream fields(line.substr(6)); long value = 0; std::string unit;
        if (fields >> value >> unit && unit == "kB") return value/1024;
    }
#endif
    return -1;
}
void updateMax(std::atomic<std::uint64_t>& target, std::uint64_t value)
{
    auto previous = target.load(std::memory_order_relaxed);
    while (previous < value && !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {}
}
}

int main(int argc, char** argv)
{
    int seconds = 60;
    std::uint64_t rate = 0;
    TriggerMode mode = TriggerMode::Normal;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--seconds" && i+1 < argc) seconds = std::stoi(argv[++i]);
        else if (arg == "--rate" && i+1 < argc) rate = std::stoull(argv[++i]);
        else if (arg == "--mode" && i+1 < argc) {
            const std::string value(argv[++i]);
            if (value == "rolling") mode = TriggerMode::Rolling;
            else if (value == "normal") mode = TriggerMode::Normal;
            else { std::cerr << "mode must be rolling or normal\n"; return 2; }
        } else { std::cerr << "usage: s7studio_live_trigger_benchmark [--seconds N] [--rate samples/s; 0=unthrottled] [--mode rolling|normal]\n"; return 2; }
    }
    if (seconds <= 0 || (rate && rate > 10'000'000)) { std::cerr << "invalid duration or rate\n"; return 2; }
    constexpr std::size_t channelCount = 8;
    const auto sampleRate = rate ? rate : 100'000;
    const double dt = 1.0/static_cast<double>(sampleRate);
    QVector<LiveChannel> channels;
    for (int c = 0; c < static_cast<int>(channelCount); ++c) channels.push_back({QStringLiteral("ch%1").arg(c+1), "V", "f64"});
    LiveAcquisition acquisition(1, dt, std::move(channels), {}, sampleRate, 1);
    acquisition.setCapacity(4096);
    TriggerSettings trigger; trigger.mode = mode; trigger.channel = 0; trigger.level = 0;
    trigger.hysteresis = 0; trigger.pretriggerPercent = 25;
    if (!acquisition.configureTrigger(trigger)) return 2;

    std::atomic<std::int64_t> latestStep{0};
    std::atomic<std::uint64_t> consumedFrames{0}, maxLag{0}, maxCopyNs{0};
    std::atomic<bool> done{false};
    std::vector<double> appendLatencyUs, copyLatencyMs, sampleLag;
    const auto cpuStart = std::clock();
    const auto started = Clock::now();
    std::thread consumer([&] {
        std::uint64_t lastCapture = 0;
        auto next = Clock::now();
        while (!done.load(std::memory_order_acquire)) {
            const auto copyStart = Clock::now();
            std::optional<LiveFrame> frame;
            if (mode == TriggerMode::Rolling) frame = acquisition.snapshot();
            else {
                const auto state = acquisition.triggerStatus();
                if (state.captureId && state.captureId != lastCapture) {
                    frame = acquisition.triggerSnapshot();
                    if (frame) lastCapture = state.captureId;
                }
            }
            if (frame && !frame->steps.empty()) {
                const auto copyNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-copyStart).count());
                if (copyLatencyMs.size() < 100'000) copyLatencyMs.push_back(static_cast<double>(copyNs)/1e6);
                const auto lag = static_cast<std::uint64_t>(std::max<std::int64_t>(0, latestStep.load(std::memory_order_relaxed)-frame->steps.back()));
                if (sampleLag.size() < 100'000) sampleLag.push_back(static_cast<double>(lag));
                updateMax(maxLag, lag); updateMax(maxCopyNs, copyNs);
                ++consumedFrames;
            }
            next += std::chrono::milliseconds(33);
            std::this_thread::sleep_until(next);
        }
    });

    const auto endAt = started + std::chrono::seconds(seconds);
    const std::uint64_t batch = rate ? std::max<std::uint64_t>(1, rate/100) : 0;
    auto nextProgress = started + std::chrono::seconds(10);
    std::int64_t step = 0;
    std::array<double, channelCount> values{};
    while (Clock::now() < endAt) {
        const auto batchStart = Clock::now();
        const std::uint64_t count = batch ? batch : 4096;
        for (std::uint64_t i = 0; i < count && Clock::now() < endAt; ++i) {
            ++step;
            values[0] = static_cast<double>(step%100-50)/50.0;
            for (std::size_t c = 1; c < channelCount; ++c)
                values[c] = static_cast<double>((static_cast<std::uint64_t>(step)*(c+3))%2048)/1024.0-1.0;
            const bool timed = step%1000 == 0;
            const auto appendStart = timed ? Clock::now() : Clock::time_point{};
            acquisition.append(step, static_cast<double>(step)*dt, values);
            if (timed && appendLatencyUs.size() < 100'000)
                appendLatencyUs.push_back(std::chrono::duration<double, std::micro>(Clock::now()-appendStart).count());
            latestStep.store(step, std::memory_order_relaxed);
        }
        if (batch) std::this_thread::sleep_until(batchStart+std::chrono::nanoseconds(static_cast<std::int64_t>(1e9*static_cast<double>(count)/static_cast<double>(rate))));
        if (Clock::now() >= nextProgress) {
            const auto elapsed = std::chrono::duration<double>(Clock::now()-started).count();
            std::cout << "progress_s=" << static_cast<int>(elapsed) << " samples=" << step
                      << " achieved_samples_s=" << static_cast<std::uint64_t>(static_cast<double>(step)/elapsed)
                      << " captures=" << acquisition.triggerStatus().captureId
                      << " consumer_frames=" << consumedFrames.load() << " max_consumer_lag_samples=" << maxLag.load() << std::endl;
            nextProgress += std::chrono::seconds(10);
        }
    }
    const auto finishStart = Clock::now(); acquisition.finish();
    const double finishMs = std::chrono::duration<double, std::milli>(Clock::now()-finishStart).count();
    done.store(true, std::memory_order_release); consumer.join();
    const double elapsed = std::chrono::duration<double>(Clock::now()-started).count();
    const double cpuSeconds = static_cast<double>(std::clock()-cpuStart)/CLOCKS_PER_SEC;
    std::cout << "mode=" << (mode == TriggerMode::Rolling ? "rolling" : "normal")
              << " target_rate=" << (rate ? std::to_string(rate) : "unthrottled")
              << " channels=8 capacity=4096 duration_s=" << elapsed << " samples=" << step
              << " achieved_samples_s=" << static_cast<std::uint64_t>(static_cast<double>(step)/elapsed)
              << " cpu_cores=" << cpuSeconds/elapsed << " peak_rss_MiB=" << peakRssMiB()
              << " reserved_acquisition_MiB=" << static_cast<double>(acquisition.allocatedBytes())/(1024*1024)
              << " append_latency_samples=" << appendLatencyUs.size()
              << " append_us_p50=" << percentile(appendLatencyUs, .50) << " append_us_p95=" << percentile(appendLatencyUs, .95)
              << " append_us_max=" << percentile(appendLatencyUs, 1.0)
              << " copy_ms_p50=" << percentile(copyLatencyMs, .50) << " copy_ms_p95=" << percentile(copyLatencyMs, .95)
              << " copy_ms_max=" << static_cast<double>(maxCopyNs.load())/1e6
              << " consumer_lag_samples_p95=" << percentile(sampleLag, .95) << " consumer_lag_samples_max=" << maxLag.load()
              << " consumer_frames=" << consumedFrames.load() << " finish_lock_ms=" << finishMs << '\n';
    return step > 0 && consumedFrames.load() > 0 ? 0 : 1;
}
