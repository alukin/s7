#include "s7/io.hpp"

#include <utility>

namespace s7 {

void register_io_elements(ElementRegistry& registry) {
    for (const std::string name : {"RecordedSource", "RecordingSink"}) {
        if (registry.find(name)) continue;
        const bool sink=name=="RecordingSink";
        ElementMetadata m;
        m.type_name=name;
        m.display_name=sink ? "Recording Sink" : "Recorded Source";
        m.description=sink ? "Record samples with rate and description in a documented binary or text format" :
            "Replay a uniformly sampled recording using its header rate";
        m.category=sink ? "io" : "sources";
        m.version="1.0.0";
        m.visual={sink ? "box" : "circle", "#42A5F5",110,52,""};
        m.type_variables.push_back({"T",{SignalType::F64,SignalType::C64},"type"});
        if (sink) m.input_ports.push_back({"in","Sample to record",SignalType::F64,false,"T"});
        else m.output_ports.push_back({"out","Recorded sample",SignalType::F64,false,"T"});
        m.parameters.push_back({"type","type","Recording signal type: F64 or C64",nullptr,std::nullopt,std::nullopt,{},false});
        m.parameters.push_back({"file_path","string","Recording path on the executing node",nullptr,std::nullopt,std::nullopt,{},true});
        m.parameters.push_back({"sample_rate_hz","real",sink ? "Model samples per second, stored in header" : "Zero uses file rate; positive checks it (no resampling)",sink ? 1000 : 0,0,std::nullopt,{},false});
        m.parameters.push_back({"format","enum","S7 recording format","compact-binary",std::nullopt,std::nullopt,{"compact-binary","gnuplot-text"},false});
        if (sink) m.parameters.push_back({"description","string","Short UTF-8 description, at most 1024 bytes","",std::nullopt,std::nullopt,{},false});
        else m.parameters.push_back({"end_behavior","enum","Action after the last sample","error",std::nullopt,std::nullopt,{"error","hold","repeat"},false});
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
        },m));
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

