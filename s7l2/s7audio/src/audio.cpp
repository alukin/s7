// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/audio.hpp>
#include <s7/hardware_live_run.hpp>
#include <algorithm>
#include <array>
namespace s7::audio {
#if S7_HAVE_PORTAUDIO
std::unique_ptr<hardware::Provider> portaudio_provider();
std::vector<hardware::AudioInputDevice> portaudio_inputs();
#endif
#if S7_HAVE_PIPEWIRE
std::unique_ptr<hardware::Provider> pipewire_provider();
std::vector<hardware::AudioInputDevice> pipewire_inputs();
#endif
std::vector<std::string> backends() {
    std::vector<std::string> result;
#if S7_HAVE_PORTAUDIO
    result.emplace_back("portaudio");
#endif
#if S7_HAVE_PIPEWIRE
    result.emplace_back("pipewire");
#endif
    return result;
}
std::unique_ptr<hardware::Provider> provider(std::string_view name) {
#if S7_HAVE_PORTAUDIO
    if (name=="portaudio") return portaudio_provider();
#endif
#if S7_HAVE_PIPEWIRE
    if (name=="pipewire") return pipewire_provider();
#endif
    throw std::invalid_argument("audio backend unavailable: "+std::string(name));
}
std::vector<hardware::AudioInputDevice> capture_devices() {
    std::vector<hardware::AudioInputDevice> result;
    for (const auto& backend : backends()) {
        result.push_back({backend, "default", backend + " default input"});
        try {
            std::vector<hardware::AudioInputDevice> devices;
#if S7_HAVE_PORTAUDIO
            if (backend == "portaudio") devices = portaudio_inputs();
#endif
#if S7_HAVE_PIPEWIRE
            if (backend == "pipewire") devices = pipewire_inputs();
#endif
            for (auto& device : devices) result.push_back(std::move(device));
        } catch (const std::runtime_error&) {
            // A backend without an active audio server still exposes its explicit default.
        }
    }
    return result;
}

void register_elements(ElementRegistry& r) {
    hardware::register_elements(r,"s7audio","AudioSource","AudioSink",{SignalType::F32,SignalType::I16,SignalType::I32},
#if defined(__linux__)
        "pipewire",
#else
        "portaudio",
#endif
        "sound_video");
    if (r.find("AudioBlockSource")) return;
    auto metadata = r.find("AudioSource")->metadata();
    metadata.type_name = "AudioBlockSource";
    metadata.display_name = "Audio Block Source";
    metadata.description = "Capture a planar F32 block from a mono or stereo audio input.";
    metadata.type_variables.clear();
    metadata.variadic_outputs = {};
    std::erase_if(metadata.parameters, [](const auto& parameter) { return parameter.name == "type"; });
    for (auto& parameter : metadata.parameters) {
        if (parameter.name == "channels") parameter.description = "Channel 0 for mono, or channels 0 and 1 for stereo";
        if (parameter.name == "block_frames") {
            parameter.default_value = 2048;
            parameter.description = "Samples per channel in each published block";
        }
    }
    metadata.output_ports.push_back({"out", "Audio block [channels, samples]", SignalType::F32, false, {},
                                     ArrayDescriptor(SignalType::F32, std::array<std::int64_t, 2>{1, 2048})});
    r.register_element(ElementDescriptor("AudioBlockSource", [](const ElementCreationContext& c) {
        auto request = hardware::request_from_context(c, hardware::Direction::Capture, SignalType::F32);
        const auto& channels = request.channels;
        if (channels != std::vector<std::string>{"0"} && channels != std::vector<std::string>{"0", "1"})
            throw std::invalid_argument("AudioBlockSource requires channels [\"0\"] or [\"0\",\"1\"]");
        return std::make_unique<hardware::CaptureBlockSource>("AudioBlockSource", std::move(request), 1);
    }, std::move(metadata), true));
}
} // namespace s7::audio
