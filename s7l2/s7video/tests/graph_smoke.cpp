// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include <s7/array_port.hpp>
#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/plugin_loader.hpp>
#include <s7/runtime.hpp>
#include <s7/schema_library.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {

void require(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}

boost::json::object readFixture(const char* path)
{
    std::ifstream in(path);
    require(in.good(), "saved MJPEG graph is missing");
    return boost::json::parse(std::string(std::istreambuf_iterator<char>(in), {})).as_object();
}

s7::CommittedArraySample sample(s7::NodeRuntime& runtime, const std::string& id, s7::ArrayPool::Retention& retention)
{
    const auto* descriptor = runtime.array_output_descriptor(id, 0);
    require(descriptor && runtime.admit_array_output(id, 0, retention, 1, descriptor->byte_size()),
            "array output did not admit an observation");
    return runtime.try_retain_array_output(id, 0, retention).value();
}

void checkSavedGraph(const boost::json::object& json)
{
    auto schema = s7::SchemaLibraryLoader::build_schema(json);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    s7::ArrayPool::Retention rawReader, packetReader, decodedReader;
    for (std::uint64_t step = 1; step <= 5; ++step) {
        runtime.step();
        const auto raw = sample(runtime, "camera_fixture", rawReader);
        const auto packet = sample(runtime, "encode", packetReader);
        const auto decoded = sample(runtime, "decode", decodedReader);
        require(packet.domain_step == step && decoded.domain_step == step &&
                packet.value.origin == raw.value.origin && decoded.value.origin == raw.value.origin &&
                raw.value.origin->sequence == step && !decoded.value.media_timing,
                "codec elements did not preserve committed steps, source occurrence or absent media time");
        const auto payload = packet.value.encoded_bytes();
        require(payload.size() < 65536 && payload[0] == std::byte{0xff} && payload[1] == std::byte{0xd8} &&
                payload.back() == std::byte{0xd9}, "encoded observer did not receive one JPEG payload");
        require(decoded.value.image && decoded.value.image->pixel_format == s7::ImagePixelFormat::Gray8,
                "decoded output did not carry the expected image format");
        const auto original = raw.value.lease.values<s7::SignalType::U8>();
        const auto pixels = decoded.value.lease.values<s7::SignalType::U8>();
        require(pixels.size() == original.size(), "decoded image changed geometry");
        std::size_t totalError = 0;
        for (std::size_t i = 0; i < pixels.size(); ++i)
            totalError += static_cast<std::size_t>(std::abs(int(pixels[i]) - int(original[i])));
        require(totalError <= 5 * pixels.size(), "moving-bar image exceeds the MJPEG lossy error limit");
    }
}

void checkReplay(const boost::json::object& recording, boost::json::object replay, const char* truncated)
{
    auto source = s7::SchemaLibraryLoader::build_schema(recording);
    auto playback = s7::SchemaLibraryLoader::build_schema(replay);
    s7::NodeRuntime original(s7::Compiler::compile(source));
    s7::NodeRuntime player(s7::Compiler::compile(playback));
    s7::ArrayPool::Retention originalReader, replayReader, frameReader;
    for (std::uint64_t step = 1; step <= 5; ++step) {
        original.step();
        player.step();
        const auto expected = sample(original, "encode", originalReader);
        const auto packet = sample(player, "file", replayReader);
        const auto frame = sample(player, "decode", frameReader);
        require(std::ranges::equal(expected.value.encoded_bytes(), packet.value.encoded_bytes()),
                "recorded bytes differ from the committed encoder packet");
        require(packet.domain_step == step && frame.domain_step == step &&
                frame.value.media_timing == s7::ArrayMediaTiming{s7::MediaPresentationTime{
                    static_cast<std::int64_t>(step - 1), {25, 1}}, false} &&
                frame.value.origin == s7::ArraySourceOccurrence{12, 0, step} &&
                frame.value.lease.view().descriptor() == s7::ArrayDescriptor(s7::SignalType::U8,
                    std::array<std::int64_t, 2>{33, 65}),
                "replay lost presentation time, source identity or dimensions");
        require(player.output_of<s7::SignalType::U8>("file", 1) == (step == 5), "incorrect EOF flag");
    }

    const auto rejectNext = [](s7::NodeRuntime& runtime, std::string_view error, std::uint64_t steps) {
        try { runtime.step(); }
        catch (const std::exception& failure) {
            require(std::string_view(failure.what()).contains(error), failure.what());
            require(runtime.step_count() == steps, "invalid file packet advanced the committed step");
            return;
        }
        throw std::runtime_error("file replay accepted an invalid next step");
    };
    rejectNext(player, "End of MJPEG file", 5);

    auto& params = replay.at("elements").as_array().front().as_object().at("params").as_object();
    params["file"] = truncated;
    auto broken = s7::SchemaLibraryLoader::build_schema(replay);
    s7::NodeRuntime partial(s7::Compiler::compile(broken));
    for (int i = 0; i < 4; ++i) partial.step();
    rejectNext(partial, "Truncated or invalid", 4);

    params["file"] = "recording.mjpeg";
    for (auto& node : replay.at("elements").as_array())
        node.as_object().at("params").as_object()["packet_capacity"] = 64;
    auto small = s7::SchemaLibraryLoader::build_schema(replay);
    s7::NodeRuntime overflow(s7::Compiler::compile(small));
    rejectNext(overflow, "exceeds the configured capacity", 0);
}

void checkBadWires(boost::json::object json)
{
    const auto rejects = [](const auto& model) {
        auto schema = s7::SchemaLibraryLoader::build_schema(model);
        try { static_cast<void>(s7::Compiler::compile(schema)); }
        catch (const std::invalid_argument& error) {
            const std::string_view message = error.what();
            require(message.contains("Array port mismatch") && message.contains("MJPEG"), "wrong packet error");
            return;
        }
        throw std::runtime_error("invalid packet wire compiled");
    };
    auto& sink = json.at("elements").as_array()[2].as_object();
    sink.at("params").as_object()["packet_capacity"] = 32768;
    rejects(json);
    sink.at("params").as_object()["packet_capacity"] = 65536;
    sink.at("params").as_object()["height"] = 32;
    rejects(json);
    sink["type"] = "VectorPassThrough";
    sink["params"] = boost::json::object{{"type", "U8"}, {"components", 65536}};
    rejects(json);
}

void checkNestedPacket(boost::json::object json)
{
    using boost::json::array;
    using boost::json::object;
    auto& encoder = json.at("elements").as_array()[1];
    auto inner = encoder.as_object();
    inner["id"] = "codec";
    encoder = object{{"id", "encode"}, {"type", "Schema"}, {"schema", object{
        {"name", "wrapped_encoder"}, {"dt", 0.04}, {"elements", array{inner}}, {"connections", array{}},
        {"exposed_inputs", array{object{{"name", "frame"}, {"type", "U8"}, {"shape", array{33, 65}},
                                        {"element", "codec"}, {"port", 0}}}},
        {"exposed_outputs", array{object{{"name", "jpeg"}, {"type", "U8"}, {"shape", array{65536}},
            {"packet", object{{"codec", "mjpeg"}, {"width", 65}, {"height", 33}}}, {"element", "codec"}, {"port", 0}}}}
    }}};
    checkSavedGraph(json);
}

const s7::ArrayDescriptor imageShape(s7::SignalType::U8, std::array<std::int64_t, 2>{33, 65});
const s7::ArrayMediaTiming timestamp{s7::MediaPresentationTime{-2700, {90000, 1}}, true};

class TimedFrame final : public s7::ArrayElement {
public:
    TimedFrame() : ArrayElement("TimedFrame"),
        pool_(imageShape, 2, 0, std::make_shared<s7::ArrayMemoryBudget>(2 * imageShape.byte_size())),
        out_(register_array_output("out", imageShape)) {}

    void step() override
    {
        auto writer = pool_.try_acquire().value();
        std::ranges::fill(writer.bytes(), std::byte{96});
        static_cast<void>(out_.exchange({std::move(writer).seal(), s7::ArraySourceOccurrence{42, 2, 1},
            {}, {}, s7::ArrayImage{s7::ImagePixelFormat::Gray8}, {}, timestamp}));
    }

private:
    s7::ArrayPool pool_;
    s7::ArrayOutputSlot& out_;
};

class FaultSink final : public s7::ArrayElement {
public:
    explicit FaultSink(bool& fail) : ArrayElement("FaultSink"), fail_(fail)
    {
        register_array_input("in", imageShape);
    }
    void step() override
    {
        if (fail_) throw std::runtime_error("injected codec graph step failure");
    }
private:
    const bool& fail_;
};

void checkMediaTimingAndRollback(boost::json::object json)
{
    auto& source = json.at("elements").as_array()[0].as_object();
    source["type"] = "TimedFrame";
    source["params"] = boost::json::object{};
    auto& registry = s7::ElementRegistry::instance();
    s7::ElementMetadata metadata;
    metadata.type_name = "TimedFrame";
    registry.register_element(s7::ElementDescriptor("TimedFrame", [](const auto&) { return std::make_unique<TimedFrame>(); },
                                                   metadata, true));
    auto schema = s7::SchemaLibraryLoader::build_schema(json);
    bool fail = false;
    // The downstream test sink uses a non-registry array factory.
    schema.add_element("fault", [&] { return std::make_unique<FaultSink>(fail); });
    schema.connect("decode", 0, "fault", 0);
    s7::NodeRuntime runtime(s7::Compiler::compile_experimental_arrays(schema));
    runtime.step();
    s7::ArrayPool::Retention packetReader, imageReader;
    const auto keepPacket = sample(runtime, "encode", packetReader);
    const auto keepImage = sample(runtime, "decode", imageReader);
    const auto* oldPacket = keepPacket.value.encoded_bytes().data();
    const auto* oldImage = keepImage.value.lease.view().bytes().data();
    require(keepPacket.value.media_timing == timestamp && keepImage.value.media_timing == timestamp &&
            keepImage.value.origin == s7::ArraySourceOccurrence{42, 2, 1}, "explicit media provenance did not round-trip");

    fail = true;
    try { runtime.step(); }
    catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "injected codec graph step failure", "unexpected step failure");
    }
    require(runtime.step_count() == 1 && runtime.committed_array_step("encode", 0) == 1 &&
            runtime.committed_array_step("decode", 0) == 1, "failed step published a codec candidate");
    fail = false;
    runtime.step();
    require(runtime.step_count() == 2 && keepPacket.value.encoded_bytes().data() == oldPacket &&
            keepImage.value.lease.view().bytes().data() == oldImage && keepImage.value.media_timing == timestamp,
            "retry changed a retained output from the preceding step");
}

} // namespace

int main(int argc, char** argv) try {
    require(argc == 3 || argc == 5, "expected codec plugin and saved graph paths");
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::PluginLoader loader;
    require(loader.load_plugin(argv[1], registry), "cannot load the codec plugin");
    const auto models = readFixture(argv[2]);
    if (argc == 5) {
        checkReplay(models, readFixture(argv[3]), argv[4]);
        return 0;
    }
    checkSavedGraph(models);
    checkBadWires(models);
    checkNestedPacket(models);
    checkMediaTimingAndRollback(models);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
