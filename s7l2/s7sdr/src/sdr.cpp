// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/sdr.hpp>

#include <algorithm>
#include <array>

namespace s7::sdr {
#if S7_HAVE_SOAPYSDR
std::unique_ptr<hardware::Provider> soapy_provider();
#endif
std::vector<std::string> backends() {
#if S7_HAVE_SOAPYSDR
    return {"soapy"};
#else
    return {};
#endif
}
std::unique_ptr<hardware::Provider> provider(std::string_view name) {
#if S7_HAVE_SOAPYSDR
    if (name=="soapy") return soapy_provider();
#endif
    throw std::invalid_argument("SDR backend unavailable: "+std::string(name));
}
void register_elements(ElementRegistry& r) {
    hardware::register_elements(r,"s7sdr","SdrSource","SdrSink",{SignalType::C32,SignalType::CI12,SignalType::CI16},"soapy","radio");
    if (r.find("SdrBlockSource")) return;

    auto metadata = r.find("SdrSource")->metadata();
    metadata.type_name = "SdrBlockSource";
    metadata.display_name = "SDR IQ Block Source";
    metadata.description = "Receive a complete single-channel C32 IQ block per graph step. The graph rate is the nominal sample rate divided by block_frames.";
    metadata.tags.push_back("vector");
    metadata.type_variables.clear();
    metadata.variadic_outputs = {};
    std::erase_if(metadata.parameters, [](const auto& parameter) { return parameter.name == "type"; });
    for (auto& parameter : metadata.parameters) {
        if (parameter.name == "block_frames") {
            parameter.default_value = 1024;
            parameter.description = "Number of consecutive IQ samples in each output vector";
        } else if (parameter.name == "channels") {
            parameter.description = "Exactly one backend channel ID";
        }
    }
    ParameterSpec source_id;
    source_id.name = "source_id";
    source_id.type = "int";
    source_id.description = "Source identity in this graph run";
    source_id.default_value = 1;
    source_id.min_value = 0;
    metadata.parameters.push_back(std::move(source_id));
    metadata.output_ports.push_back({"out", "Complete IQ block", SignalType::C32, false, {},
                                    ArrayDescriptor(SignalType::C32, std::array<std::int64_t, 1>{1024})});
    r.register_element(ElementDescriptor("SdrBlockSource", [](const ElementCreationContext& context) {
        return std::make_unique<BlockSource>(
            hardware::request_from_context(context, hardware::Direction::Capture, SignalType::C32),
            static_cast<std::uint64_t>(context.get_int("source_id")));
    }, std::move(metadata), true));
}
} // namespace s7::sdr
