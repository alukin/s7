// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/io.hpp"
#include "s7/metadata_json.hpp"
#include "io_metadata.hpp"

#include <utility>

namespace s7 {

void register_io_elements(ElementRegistry& registry) {
    static const ElementMetadataLibrary metadata(resources::io_metadata);
    for (const std::string name : {"RecordedSource", "RecordingSink"}) {
        if (registry.find(name)) continue;
        const bool sink=name=="RecordingSink";
        registry.register_element(ElementDescriptor(name,[sink](const ElementCreationContext& ctx) -> std::unique_ptr<Element> {
            const auto format=ctx.get_string("format")=="gnuplot-text" ? io::RecordingFormat::Gnuplot : io::RecordingFormat::CompactBinary;
            const auto path=ctx.get_string("file_path");
            const auto rate=ctx.get_real("sample_rate_hz");
            if (sink) {
                if (ctx.type()==SignalType::F64) return std::make_unique<io::RealRecordingSink>(path,rate,format,ctx.get_string("description"));
                return std::make_unique<io::ComplexRecordingSink>(path,rate,format,ctx.get_string("description"));
            }
            const auto behavior=ctx.get_string("end_behavior");
            const auto end=behavior=="hold" ? io::ReplayEnd::Hold : behavior=="repeat" ? io::ReplayEnd::Repeat : io::ReplayEnd::Error;
            if (ctx.type()==SignalType::F64) return std::make_unique<io::RealRecordedSource>(path,rate,format,end);
            return std::make_unique<io::ComplexRecordedSource>(path,rate,format,end);
        },metadata.at(name)));
    }
}

void IoCatalog::register_input(IoChannel channel) {
    inputs_.push_back(std::move(channel));
}

void IoCatalog::register_output(IoChannel channel) {
    outputs_.push_back(std::move(channel));
}

const std::vector<IoChannel>& IoCatalog::inputs() const {
    return inputs_;
}

const std::vector<IoChannel>& IoCatalog::outputs() const {
    return outputs_;
}

} // namespace s7

