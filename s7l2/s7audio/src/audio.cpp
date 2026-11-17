// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/audio.hpp>
namespace s7::audio {
#if S7_HAVE_PORTAUDIO
std::unique_ptr<hardware::Provider> portaudio_provider();
#endif
#if S7_HAVE_PIPEWIRE
std::unique_ptr<hardware::Provider> pipewire_provider();
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
void register_elements(ElementRegistry& r) {
    hardware::register_elements(r,"s7audio","AudioSource","AudioSink",{SignalType::F32,SignalType::I16,SignalType::I32},
#if defined(__linux__)
        "pipewire"
#else
        "portaudio"
#endif
    );
}
} // namespace s7::audio
