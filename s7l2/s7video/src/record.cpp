// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include <s7/array_port.hpp>
#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/plugin_loader.hpp>
#include <s7/runtime.hpp>
#include <s7/schema_library.hpp>

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace {

class StagedRecording {
public:
    explicit StagedRecording(fs::path destination)
        : destination_(std::move(destination)), directory_(destination_), pending_(prepare()) {}

    ~StagedRecording()
    {
        std::error_code ignored;
        fs::remove(pending_, ignored);
        fs::remove(directory_, ignored);
    }

    const fs::path& path() const { return pending_; }
    void publish() const { fs::create_hard_link(pending_, destination_); }

private:
    fs::path prepare()
    {
        directory_ += ".s7-recording";
        const auto pending = directory_ / "frames.mjpeg";
        if (fs::exists(destination_) || !fs::create_directory(directory_)) {
            throw std::runtime_error("Recording destination or staging directory already exists.");
        }
        return pending;
    }

    fs::path destination_;
    fs::path directory_;
    fs::path pending_;
};

} // namespace

int main(int argc, char** argv) try {
    if (argc != 6) {
        std::cerr << "Usage: s7video_record PLUGIN SCHEMA OUTPUT_ELEMENT FRAME_COUNT DESTINATION.mjpeg\n";
        return 2;
    }

    const std::string_view countText(argv[4]);
    std::uint64_t count = 0;
    const auto countEnd = countText.data() + countText.size();
    const auto [end, error] = std::from_chars(countText.data(), countEnd, count);
    if (error != std::errc{} || end != countEnd || count == 0) {
        throw std::invalid_argument("FRAME_COUNT must be a positive integer.");
    }

    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::PluginLoader loader;
    if (!loader.load_plugin(argv[1], registry)) throw std::runtime_error("Cannot load the video plugin.");

    std::ifstream input(fs::path(argv[2]), std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open the recording schema.");
    const auto model = boost::json::parse(std::string(std::istreambuf_iterator<char>(input), {}));
    auto schema = s7::SchemaLibraryLoader::build_schema(model.as_object());
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));

    const std::string element(argv[3]);
    const auto* descriptor = runtime.array_output_descriptor(element, 0);
    s7::ArrayPool::Retention reader;
    if (!descriptor || !descriptor->mjpeg()) {
        throw std::invalid_argument("OUTPUT_ELEMENT port 0 must expose an MJPEG packet.");
    }

    const StagedRecording recording{fs::path(argv[5])};
    std::ofstream output(recording.path(), std::ios::binary);
    output.exceptions(std::ios::badbit | std::ios::failbit);
    for (std::uint64_t frame = 0; frame < count; ++frame) {
        runtime.step();
        if (frame == 0 && !runtime.admit_array_output(element, 0, reader, 1, descriptor->byte_size())) {
            throw std::runtime_error("Could not retain packets on the recording output.");
        }
        const auto sample = runtime.try_retain_array_output(element, 0, reader);
        if (!sample || sample->domain_step != runtime.step_count()) {
            throw std::runtime_error("Graph step did not commit a new recording packet.");
        }
        const auto payload = sample->value.encoded_bytes();
        output.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    }
    output.close();
    recording.publish();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
