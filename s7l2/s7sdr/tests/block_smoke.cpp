// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/plugin_loader.hpp>
#include <s7/schema_library.hpp>
#include <s7/sdr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
using namespace s7;
using namespace s7::hardware;
using namespace std::chrono_literals;
using IQ = signal_storage_t<SignalType::C32>;

constexpr std::size_t block_size = 8;
constexpr std::array<IQ, 4> tone{IQ{1, 0}, IQ{0, 1}, IQ{-1, 0}, IQ{0, -1}};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

class TestStream final : public Stream {
public:
    TestStream(const Request& request, std::size_t available)
        : Stream(describe(request)), available_(available) {}

private:
    static Descriptor describe(const Request& request)
    {
        Descriptor descriptor;
        descriptor.actual = request;
        descriptor.segment = 4;
        descriptor.backend_details["channel.0.sample_rate_hz"] = "7999";
        return descriptor;
    }

    void do_start() override {}
    void do_stop() noexcept override {}

    Transfer do_read(std::span<std::byte> destination, std::size_t requested, Deadline, std::stop_token) override
    {
        const auto count = std::min({requested, available_ - position_, std::size_t{3}});
        for (std::size_t i = 0; i < count; ++i) {
            const auto& sample = tone[(position_ + i) % tone.size()];
            std::memcpy(destination.data() + i * sizeof(IQ), &sample, sizeof(IQ));
        }
        position_ += count;
        if (position_ == available_) return {count, Status::Overflow, 0, {}, -77};
        return {count, Status::Ok, 0, {}};
    }

    const std::size_t available_;
    std::size_t position_{};
};

class TestProvider final : public Provider {
public:
    explicit TestProvider(std::size_t available) : available_(available) {}
    std::string_view id() const noexcept override { return "test"; }
    std::vector<DeviceInfo> devices() override { return {}; }
    std::unique_ptr<Stream> prepare(const Request& request) override
    {
        require(request.rate == Rate{8000, 1} && request.block_frames == block_size,
                "provider did not receive the requested sample rate and block size");
        return std::make_unique<TestStream>(request, available_);
    }

private:
    const std::size_t available_;
};

ExecutableGraph build_graph(bool spectral)
{
    using boost::json::array;
    using boost::json::object;
    const object model{
        {"name", "block-capture"},
        {"elements", array{
            object{{"id", "rx"}, {"type", "SdrBlockSource"}, {"params", object{
                {"backend", "test"}, {"block_frames", block_size}, {"frame_rate", array{8000, 1}},
                {"source_id", 11}}}},
            object{{"id", "out"}, {"type", spectral ? "VectorPSD" : "VectorPassThrough"},
                {"params", spectral ? object{{"components", block_size}, {"units", "per_hz"}}
                                    : object{{"components", block_size}, {"type", "C32"}}}}
        }},
        {"connections", array{object{
            {"from_element", "rx"}, {"from_port", 0}, {"to_element", "out"}, {"to_port", 0}}}}
    };
    return Compiler::compile(SchemaLibraryLoader::build_schema(model));
}

CommittedArraySample retain(NodeRuntime& runtime, const std::string& id, ArrayPool::Retention& reader)
{
    const auto* descriptor = runtime.array_output_descriptor(id, 0);
    require(descriptor && runtime.admit_array_output(id, 0, reader, 1, descriptor->byte_size()),
            "array output could not admit a reader");
    return runtime.try_retain_array_output(id, 0, reader).value();
}

Run::Resolve resolve(std::size_t available)
{
    return [available](std::string_view) { return std::make_unique<TestProvider>(available); };
}

void check_complete_blocks(bool spectral)
{
    auto graph = build_graph(spectral);
    require(has_hardware_boundary(graph), "hardware graph did not recognize SdrBlockSource");
    const auto profile = graph_profile(graph);
    require(profile.graph_rate == Rate{1000, 1} && profile.frames_per_step == block_size,
            "sample cadence and graph block cadence were conflated");
    Run run(std::move(graph), resolve(3 * block_size));
    ArrayPool::Retention input_reader, psd_reader;
    const auto stats = run.execute(2, 100ms, {}, [&](std::uint64_t step) {
        auto input = retain(run.runtime(), "rx", input_reader);
        const auto samples = input.value.lease.values<SignalType::C32>();
        require(samples.size() == block_size && std::ranges::equal(samples.first(4), tone) &&
                std::ranges::equal(samples.last(4), tone), "short device reads reordered IQ samples");
        require(input.rate == Rate{1000, 1} && input.domain_step == step &&
                input.value.origin == ArraySourceOccurrence{11, 4, step} &&
                input.value.sample_window == ArraySampleWindow{Rate{7999, 1}, (step - 1) * block_size,
                                                             block_size, false, false},
                "committed block lost its origin, sample position, or physical rate");
        if (spectral) {
            auto output = retain(run.runtime(), "out", psd_reader);
            const auto power = output.value.lease.values<SignalType::F32>();
            require(std::ranges::max_element(power) == power.begin() + 2 &&
                    output.value.spectrum && output.value.spectrum->sample_rate == Rate{7999, 1} &&
                    output.value.sample_window == input.value.sample_window,
                    "VectorPSD peak or physical sample rate differs from the independent expectation");
        }
    });
    require(stats.status == Status::Ok && stats.graph_frames == 2 && stats.transferred_frames == 2 * block_size,
            "run counts did not distinguish graph steps from RF samples");
}

void check_incomplete_block()
{
    Run run(build_graph(false), resolve(block_size + 2));
    const auto stats = run.execute(2, 100ms);
    require(stats.status == Status::Overflow && stats.native_error == -77 && stats.graph_frames == 1 &&
            stats.transferred_frames == block_size + 2, "an incomplete block was published or the overflow was hidden");
    ArrayPool::Retention reader;
    const auto last = retain(run.runtime(), "rx", reader);
    require(last.domain_step == 1 && last.value.sample_window->first_sample == 0,
            "a failed block replaced the previous committed output");
}

void check_cancellation()
{
    Run run(build_graph(false), resolve(3 * block_size));
    std::stop_source stop;
    const auto stats = run.execute(2, 100ms, stop.get_token(), [&](std::uint64_t) { stop.request_stop(); });
    require(stats.status == Status::Cancelled && stats.graph_frames == 1 && stats.transferred_frames == block_size,
            "stop did not cancel capture after the committed block");
}

} // namespace

int main(int argc, char** argv) try {
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::sdr::register_elements(registry);
    s7::PluginLoader plugins;
    if (argc == 2) require(plugins.load_plugin(argv[1], registry), "could not load VectorPSD plugin");
    check_complete_blocks(argc == 2);
    check_incomplete_block();
    check_cancellation();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
