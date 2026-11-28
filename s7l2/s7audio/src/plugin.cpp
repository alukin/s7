// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/audio.hpp>
#include <s7/element_plugin.hpp>
#include <s7/hardware_live_run.hpp>
#if defined(S7_WITH_SDR)
#include <s7/sdr.hpp>
#endif
#include <algorithm>

namespace {
using namespace s7::hardware;

std::unique_ptr<Provider> resolve(std::string_view name) {
    const auto audio = s7::audio::backends();
    if (std::ranges::find(audio, name) != audio.end()) return s7::audio::provider(name);
#if defined(S7_WITH_SDR)
    const auto sdr = s7::sdr::backends();
    if (std::ranges::find(sdr, name) != sdr.end()) return s7::sdr::provider(name);
#endif
    throw std::invalid_argument("hardware backend unavailable: " + std::string(name));
}

class PublicLiveRun final : public LiveRun {
public:
    explicit PublicLiveRun(s7::ExecutableGraph graph) : run_(std::move(graph)) {}
    s7::NodeRuntime& runtime() noexcept override { return run_.runtime(); }
    std::string prepare() override {
        run_.prepare(resolve);
        const auto& d = run_.descriptor();
        auto description = d.actual.backend + " " + d.actual.device + " at " + d.actual.rate.to_string();
        for (const auto& [key, value] : d.backend_details) {
            // Requested and realized values (ADR-028).
            if (key.ends_with("frequency_hz") || key.ends_with("sample_rate_hz") ||
                key.ends_with("sample_rate_deviation_ppm"))
                description += ", " + key + "=" + value;
        }
        return description;
    }
    void execute(std::uint64_t frames, std::stop_token stop, const Run::StepObserver& observe) override {
        const auto stats = run_.execute(frames, std::chrono::seconds(1), stop, observe);
        if (stats.status == Status::Ok || (stats.status == Status::Cancelled && stop.stop_requested())) return;
        auto error = std::string("Hardware run: ") + to_string(stats.status);
        if (stats.native_error) error += " (driver code " + std::to_string(stats.native_error) + ")";
        throw std::runtime_error(error);
    }
private:
    Run run_;
};
}

extern "C" s7::hardware::LiveRun* s7_create_live_run_v1(s7::ExecutableGraph& graph, s7::Rate rate) {
    if (!has_hardware_boundary(graph)) return nullptr;
    const auto config = graph_profile(graph);
    if (config.request.direction == Direction::Playback && config.boundary_type != "AudioSink")
        throw std::invalid_argument("Use s7hw for explicit SDR transmission.");
    if (config.graph_rate != rate)
        throw std::invalid_argument("Set the model step rate to " + config.graph_rate.to_string() + " for this hardware run.");
    return new PublicLiveRun(std::move(graph));
}

namespace s7::audio {
std::vector<hardware::AudioInputDevice> capture_devices();
}

extern "C" void s7_audio_inputs_v1(std::vector<s7::hardware::AudioInputDevice>& devices) {
    devices = s7::audio::capture_devices();
}

extern "C" const S7PluginInfo* s7_plugin_query() {
    static const S7PluginInfo info{S7_PLUGIN_API_VERSION,3,"s7audio","0.1.0","S7 Project"}; return &info;
}
extern "C" const char* s7_plugin_manifest_json() {
    return R"({"plugin_name":"s7audio","plugin_version":"0.1.0","author":"S7 Project","description":"Explicitly activated PortAudio and native PipeWire boundaries","element_count":3,"elements":["AudioSource","AudioSink","AudioBlockSource"]})";
}
extern "C" void s7_register_elements(void* registry) { s7::audio::register_elements(*static_cast<s7::ElementRegistry*>(registry)); }
