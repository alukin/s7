// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace s7;

void require(bool condition, const char* description)
{
    if (!condition) throw std::runtime_error(description);
}

void check_frame(const CommittedArraySample& sample, ImagePixelFormat format, std::uint64_t source,
                 std::uint64_t step, std::span<const std::uint8_t> expected)
{
    require(sample.domain_step == step && sample.value.origin == ArraySourceOccurrence{source, 0, step},
            "frame sequence or source identity changed");
    require(sample.value.image && sample.value.image->pixel_format == format && !sample.value.sample_window,
            "frame format or provenance disagrees with the producer");
    const auto view = sample.value.lease.view();
    require(view.stride_bytes(0) == expected.size() / 2, "frame is not compact and row-major");
    require(std::ranges::equal(sample.value.lease.values<SignalType::U8>(), expected),
            "pixels differ from the independent checkerboard and bar oracle");
}

void check_frames(const char* filename)
{
    std::ifstream file(filename);
    require(file.is_open(), "cannot open the saved frame schema");
    const std::string json(std::istreambuf_iterator<char>{file}, {});
    auto& registry = ElementRegistry::instance();
    register_builtin_elements(registry);
    auto schema = SchemaLibraryLoader::build_schema(boost::json::parse(json), registry);
    auto graph = Compiler::compile(schema);
    auto gray = static_cast<TestPatternFrame&>(*graph.elements.at("gray")).make_retention();
    auto rgb = static_cast<TestPatternFrame&>(*graph.elements.at("rgb")).make_retention();
    NodeRuntime runtime(std::move(graph));

    const std::array<std::uint8_t, 8> gray1{255, 32, 208, 208, 255, 32, 208, 208};
    const std::array<std::uint8_t, 8> gray2{32, 255, 208, 208, 32, 255, 208, 208};
    const std::array<std::uint8_t, 24> rgb1{
        255, 0, 0, 32, 32, 32, 208, 208, 208, 208, 208, 208,
        255, 0, 0, 32, 32, 32, 208, 208, 208, 208, 208, 208
    };
    const std::array<std::uint8_t, 24> rgb2{
        32, 32, 32, 255, 0, 0, 208, 208, 208, 208, 208, 208,
        32, 32, 32, 255, 0, 0, 208, 208, 208, 208, 208, 208
    };
    runtime.step();
    {
        const auto retained_gray = runtime.try_retain_array_output("gray", 0, gray).value();
        const auto retained_rgb = runtime.try_retain_array_output("rgb", 0, rgb).value();
        check_frame(retained_gray, ImagePixelFormat::Gray8, 11, 1, gray1);
        check_frame(retained_rgb, ImagePixelFormat::RGB8, 12, 1, rgb1);
        runtime.step();
        require(runtime.output_of<SignalType::U64>("gray", 1) == 2 &&
                    runtime.output_of<SignalType::U64>("rgb", 1) == 2, "retention blocked the frame producers");
        check_frame(retained_gray, ImagePixelFormat::Gray8, 11, 1, gray1);
        check_frame(retained_rgb, ImagePixelFormat::RGB8, 12, 1, rgb1);
    }
    check_frame(runtime.try_retain_array_output("gray", 0, gray).value(), ImagePixelFormat::Gray8, 11, 2, gray2);
    check_frame(runtime.try_retain_array_output("rgb", 0, rgb).value(), ImagePixelFormat::RGB8, 12, 2, rgb2);

    const auto* descriptor = registry.find("TestPatternFrame");
    for (const auto& params : {boost::json::object{{"width", 0}}, boost::json::object{{"format", "bgr8"}}}) {
        Schema invalid("invalid_frame");
        invalid.set_rate(Rate::from_hz(25));
        invalid.add_element(make_element_spec("frame", *descriptor, params));
        require(!Validator::validate(invalid).ok(), "invalid frame configuration was accepted");
    }

    const ArrayDescriptor wrong(SignalType::U8, std::array<std::int64_t, 3>{2, 4, 4});
    ArrayPool pool(wrong, 1, 0, std::make_shared<ArrayMemoryBudget>(wrong.byte_size()));
    auto write = pool.try_acquire().value();
    ArrayOutputSlot slot(wrong);
    try { static_cast<void>(slot.exchange({std::move(write).seal(), {}, {}, {}, ArrayImage{ImagePixelFormat::RGB8}})); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("RGB metadata was accepted on an incompatible shape");
}
} // namespace

int main(int argc, char** argv) try {
    require(argc == 2, "pass the saved frame schema path");
    check_frames(argv[1]);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
