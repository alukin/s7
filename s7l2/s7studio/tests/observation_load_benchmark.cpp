// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

// Opt-in measurement of how probes and viewers slow a local Studio run
// (docs/SIMULATION_OBSERVATION_PERFORMANCE_PLAN.md, WP0). Drives the real
// LocalSchemaRunner headlessly. Viewer emulation polls the same observation
// calls at Studio's refresh intervals: Live Oscilloscope 16 ms
// (timeSnapshot), Scope repaint 33 ms (tail), Scope/analysis journal paging
// 40 ms (read), and the UI's Scope mailbox drain on every notification.
// Prints one JSON object. Run through tools/run_observation_benchmark.py.

#include "local_schema_runner.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>

#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QTimer>

#include <boost/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace s7studio;
using Clock = std::chrono::steady_clock;

namespace {

struct Options {
    std::string graph = "chain";
    int gains = 64;
    long ticks = 1'000'000;
    int probes = 4;
    bool record = true;
    bool live = false;
    bool viewers = false;
};

[[noreturn]] void usage(const std::string& error = {})
{
    if (!error.empty()) std::cerr << "error: " << error << "\n";
    std::cerr << "usage: s7studio_observation_load_benchmark [--graph chain|oscillator] [--gains N]\n"
                 "         [--ticks N] [--probes N] [--record 0|1] [--live 0|1] [--viewers 0|1]\n";
    std::exit(error.empty() ? 0 : 2);
}

Options parse(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help" || key == "-h") usage();
        if (i + 1 >= argc) usage("missing value for " + key);
        const std::string value = argv[++i];
        try {
            if (key == "--graph") options.graph = value;
            else if (key == "--gains") options.gains = std::stoi(value);
            else if (key == "--ticks") options.ticks = std::stol(value);
            else if (key == "--probes") options.probes = std::stoi(value);
            else if (key == "--record") options.record = std::stoi(value) != 0;
            else if (key == "--live") options.live = std::stoi(value) != 0;
            else if (key == "--viewers") options.viewers = std::stoi(value) != 0;
            else usage("unknown option " + key);
        } catch (const std::logic_error&) {
            usage("invalid value for " + key);
        }
    }
    if (options.graph != "chain" && options.graph != "oscillator") usage("unknown graph " + options.graph);
    if (options.gains < 1 || options.gains > 4096) usage("--gains must be 1..4096");
    if (options.ticks < 1) usage("--ticks must be positive");
    if (options.probes < 0 || options.probes > 8) usage("--probes must be 0..8 (live views take at most 8)");
    if (options.probes > 0 && !options.record && !options.live) usage("probes need --record 1 or --live 1");
    return options;
}

boost::json::object element(const std::string& id, const std::string& type, boost::json::object params = {})
{
    return {{"id", id}, {"type", type}, {"params", std::move(params)}};
}

boost::json::object wire(const std::string& from, const std::string& to, int toPort = 0)
{
    return {{"from_element", from}, {"from_port", 0}, {"to_element", to}, {"to_port", toPort}};
}

// The measured graph and the ids probes may observe (spread along it).
std::pair<boost::json::value, std::vector<std::string>> graph(const Options& options)
{
    boost::json::array elements, connections;
    std::string previous;
    if (options.graph == "chain") {
        elements.push_back(element("source", "Constant", {{"value", 1.0}}));
        previous = "source";
    } else {
        // x'' = -x: two forward-Euler integrators closed by a gain of -1.
        elements.push_back(element("velocity", "EulerIntegrator", {{"initial_value", 1.0}, {"substeps", 1}}));
        elements.push_back(element("position", "EulerIntegrator", {{"initial_value", 0.0}, {"substeps", 1}}));
        elements.push_back(element("spring", "Gain", {{"factor", -1.0}}));
        connections.push_back(wire("velocity", "position"));
        connections.push_back(wire("position", "spring"));
        connections.push_back(wire("spring", "velocity"));
        previous = "position";
    }
    std::vector<std::string> gains;
    for (int i = 0; i < options.gains; ++i) {
        const auto id = "gain_stage_" + std::to_string(i);
        elements.push_back(element(id, "Gain", {{"factor", 1.00001}}));
        connections.push_back(wire(previous, id));
        gains.push_back(id);
        previous = id;
    }
    std::vector<std::string> probed;
    for (int i = 0; i < options.probes; ++i)
        probed.push_back(gains[static_cast<std::size_t>((i + 1) * options.gains / (options.probes + 1))]);
    boost::json::object schema{{"name", "observation_load"}, {"elements", elements}, {"connections", connections}};
    return {schema, probed};
}

// Records how long each viewer call held the caller (and so, potentially,
// the stepping thread waiting on the same lock).
struct CallStats {
    std::uint64_t calls = 0;
    double totalMs = 0, maxMs = 0;
    std::vector<double> samples;
    void add(double ms) { ++calls; totalMs += ms; maxMs = std::max(maxMs, ms); samples.push_back(ms); }
    boost::json::object json()
    {
        std::ranges::sort(samples);
        const auto p99 = samples.empty() ? 0.0 : samples[std::min(samples.size() - 1, samples.size() * 99 / 100)];
        return {{"calls", calls}, {"total_ms", totalMs}, {"max_ms", maxMs}, {"p99_ms", p99}};
    }
};

template <class F>
void poll(std::stop_token stop, std::chrono::milliseconds interval, CallStats& stats, F call)
{
    auto next = Clock::now();
    while (!stop.stop_requested()) {
        const auto start = Clock::now();
        call();
        stats.add(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        next += interval;
        std::this_thread::sleep_until(std::max(next, Clock::now()));
    }
}

} // namespace

int main(int argc, char** argv)
{
    const auto options = parse(argc, argv);
    QCoreApplication app(argc, argv);
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    QTemporaryDir storage;
    if (!storage.isValid()) { std::cerr << "cannot create temporary storage\n"; return 1; }

    observation::StorageOptions storageOptions;
    storageOptions.temporaryDirectory = storage.path();
    LocalSchemaRunner runner(nullptr, storageOptions);

    const auto [schema, probed] = graph(options);
    std::vector<RunProbe> probes;
    for (std::size_t i = 0; i < probed.size(); ++i) {
        RunProbe probe;
        probe.name = "probe_" + std::to_string(i);
        probe.element = probed[i];
        probe.record = options.record;
        if (options.live) probe.liveGroup = "live";
        probes.push_back(std::move(probe));
    }

    std::uint64_t mailboxSamples = 0, mailboxDropped = 0;
    CallStats mailbox, journal, tail, envelope;
    QObject::connect(&runner, &LocalSchemaRunner::scopeSamplesAvailable, &runner, [&] {
        const auto start = Clock::now();
        const auto batch = runner.takeScopeSamples();
        mailbox.add(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        mailboxSamples += batch.samples.size();
        mailboxDropped += batch.dropped;
    });

    bool success = false;
    QString error;
    QEventLoop loop;
    QObject::connect(&runner, &LocalSchemaRunner::runCompleted, &loop, [&](bool ok, const QString& message) {
        success = ok; error = message; loop.quit();
    });

    const auto start = Clock::now();
    runner.run(schema, .001, options.ticks, probes);
    std::vector<std::jthread> viewers;
    if (options.viewers) {
        // Snapshot handles on the owner thread, as Studio windows do.
        const auto recording = runner.recordedSource();
        const auto live = runner.liveSources();
        if (recording) {
            QVector<observation::ChannelId> ids;
            for (const auto& channel : recording->channels()) ids.push_back(channel.id);
            viewers.emplace_back([recording, &journal](std::stop_token stop) {
                std::uint64_t cursor = 0;
                poll(stop, std::chrono::milliseconds(40), journal, [&] {
                    const auto page = recording->read(cursor, 4096);
                    cursor = page.samples.isEmpty() ? 0 : page.next; // keep paging the history, as analysis previews do
                });
            });
            viewers.emplace_back([recording, ids, &tail](std::stop_token stop) {
                poll(stop, std::chrono::milliseconds(33), tail, [&] { static_cast<void>(recording->tail(ids)); });
            });
        }
        for (const auto& source : live) {
            viewers.emplace_back([source, &envelope](std::stop_token stop) {
                poll(stop, std::chrono::milliseconds(16), envelope, [&] { static_cast<void>(source->timeSnapshot()); });
            });
        }
    }
    if (runner.isRunning()) loop.exec();
    const auto wall = std::chrono::duration<double>(Clock::now() - start).count();
    viewers.clear(); // request stop and join

    const auto recording = runner.recordedSource();
    const auto status = recording ? recording->status() : observation::SourceStatus{};
    boost::json::object result{
        {"schema", "s7_observation_load_v1"},
        {"graph", options.graph}, {"gains", options.gains}, {"ticks", options.ticks},
        {"probes", options.probes}, {"record", options.record}, {"live", options.live}, {"viewers", options.viewers},
        {"success", success}, {"error", error.toStdString()},
        {"completed_ticks", runner.completedTicks()},
        {"wall_s", wall}, {"steps_per_s", static_cast<double>(runner.completedTicks()) / wall},
        {"recorded_samples", status.samples}, {"capture_dropped", status.captureDropped},
        {"mailbox_samples", mailboxSamples}, {"mailbox_dropped", mailboxDropped},
        {"calls", boost::json::object{{"mailbox_take", mailbox.json()}, {"journal_read", journal.json()},
                                      {"tail", tail.json()}, {"live_time_snapshot", envelope.json()}}}};
    std::cout << boost::json::serialize(result) << std::endl;
    return success && runner.completedTicks() == options.ticks ? 0 : 1;
}
