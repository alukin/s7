// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/hardware_live_run.hpp>

#include <pipewire/pipewire.h>

#include <cerrno>
#include <charconv>
#include <chrono>

namespace s7::audio {
using hardware::AudioInputDevice;
void initialize_pipewire();

std::vector<AudioInputDevice> pipewire_inputs() {
    initialize_pipewire();
    struct Scan {
        std::vector<AudioInputDevice> devices;
        int sequence{-1};
        bool complete{};
        bool failed{};
    } scan;
    auto loop = std::unique_ptr<pw_main_loop, decltype(&pw_main_loop_destroy)>(
        pw_main_loop_new(nullptr), pw_main_loop_destroy);
    if (!loop) return {};
    auto* driver = pw_main_loop_get_loop(loop.get());
    auto context = std::unique_ptr<pw_context, decltype(&pw_context_destroy)>(
        pw_context_new(driver, nullptr, 0), pw_context_destroy);
    if (!context) return {};
    auto core = std::unique_ptr<pw_core, decltype(&pw_core_disconnect)>(
        pw_context_connect(context.get(), nullptr, 0), pw_core_disconnect);
    if (!core) return {};
    auto destroy_registry = [](pw_registry* registry) { pw_proxy_destroy(reinterpret_cast<pw_proxy*>(registry)); };
    auto registry = std::unique_ptr<pw_registry, decltype(destroy_registry)>(
        pw_core_get_registry(core.get(), PW_VERSION_REGISTRY, 0), destroy_registry);
    if (!registry) return {};

    static const auto core_events = [] {
        pw_core_events events{};
        events.version = PW_VERSION_CORE_EVENTS;
        events.done = [](void* data, std::uint32_t id, int seq) {
            auto& s = *static_cast<Scan*>(data);
            if (id == PW_ID_CORE && seq == s.sequence) s.complete = true;
        };
        events.error = [](void* data, std::uint32_t id, int, int, const char*) {
            if (id == PW_ID_CORE) static_cast<Scan*>(data)->failed = true;
        };
        return events;
    }();
    static const auto registry_events = [] {
        pw_registry_events events{};
        events.version = PW_VERSION_REGISTRY_EVENTS;
        events.global = [](void* data, std::uint32_t, std::uint32_t, const char* type,
                           std::uint32_t, const spa_dict* props) noexcept {
            auto& s = *static_cast<Scan*>(data);
            try {
                if (!type || !props || std::string_view(type) != PW_TYPE_INTERFACE_Node) return;
                const auto property = [props](const char* key) {
                    const auto* value = spa_dict_lookup(props, key);
                    return value ? std::string_view(value) : std::string_view{};
                };
                const auto media_class = property(PW_KEY_MEDIA_CLASS);
                const bool monitor = media_class == "Audio/Sink";
                if (media_class != "Audio/Source" && media_class != "Audio/Source/Virtual" && !monitor) return;
                const auto serial = property(PW_KEY_OBJECT_SERIAL);
                const auto name = property(PW_KEY_NODE_NAME);
                if (serial.empty() && name.empty()) return;
                const auto description = property(PW_KEY_NODE_DESCRIPTION);
                const auto count = property(PW_KEY_AUDIO_CHANNELS);
                unsigned channels{};
                if (!count.empty()) {
                    const auto end = count.data() + count.size();
                    const auto parsed = std::from_chars(count.data(), end, channels);
                    if (parsed.ec != std::errc{} || parsed.ptr != end) channels = 0;
                }
                auto label = std::string(description.empty() ? name : description);
                if (monitor) label += " (monitor)";
                s.devices.push_back({"pipewire", std::string(serial.empty() ? name : serial),
                                     std::move(label), channels, monitor});
            } catch (...) {
                s.failed = true;
            }
        };
        return events;
    }();

    spa_hook core_hook{}, registry_hook{};
    pw_core_add_listener(core.get(), &core_hook, &core_events, &scan);
    pw_registry_add_listener(registry.get(), &registry_hook, &registry_events, &scan);
    scan.sequence = pw_core_sync(core.get(), PW_ID_CORE, 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    pw_loop_enter(driver);
    while (scan.sequence >= 0 && !scan.complete && !scan.failed &&
           std::chrono::steady_clock::now() < deadline) {
        const auto status = pw_loop_iterate(driver, 30);
        if (status < 0 && status != -EINTR && status != -ETIMEDOUT) break;
    }
    pw_loop_leave(driver);
    spa_hook_remove(&registry_hook);
    spa_hook_remove(&core_hook);
    return scan.devices;
}

} // namespace s7::audio
