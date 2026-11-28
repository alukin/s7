// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/hardware.hpp>
#include <s7/json_parse.hpp>

namespace s7::hardware {
Request request_from_context(const ElementCreationContext& c, Direction direction) {
    return request_from_context(c,direction,c.type());
}
Request request_from_context(const ElementCreationContext& c, Direction direction, SignalType type) {
    Request r; r.direction=direction; r.type=type;
    r.backend=c.get_string("backend"); r.device=c.get_string("device");
    r.block_frames=c.get_int("block_frames"); r.queue_frames=c.get_int("queue_frames");
    r.channels.clear();
    for (const auto& v:c.params.at("channels").as_array()) {
        if (!v.is_string()) throw std::invalid_argument("hardware channels must be strings");
        r.channels.emplace_back(v.as_string());
    }
    const auto& rate=c.params.at("frame_rate").as_array();
    auto positive=[](const boost::json::value& v)->std::uint64_t {
        if (v.is_uint64() && v.as_uint64()) return v.as_uint64();
        if (v.is_int64() && v.as_int64()>0) return v.as_int64();
        throw std::invalid_argument("frame_rate requires positive integers");
    };
    if (rate.size()!=2) throw std::invalid_argument("frame_rate must be [numerator, denominator]");
    r.rate=Rate::reduced(positive(rate[0]),positive(rate[1]));
    const auto opts=s7::parse_json(c.get_string("options"));
    if (!opts.is_object()) throw std::invalid_argument("hardware options must encode a JSON object of strings");
    for (const auto& kv:opts.as_object()) {
        if (!kv.value().is_string()) throw std::invalid_argument("hardware option values must be strings");
        r.options.emplace(std::string(kv.key()),std::string(kv.value().as_string()));
    }
    r.validate(); return r;
}
void register_elements(ElementRegistry& registry, std::string_view library, std::string_view source,
                       std::string_view sink, std::vector<SignalType> types, std::string_view backend) {
    register_elements(registry, library, source, sink, std::move(types), backend, "io");
}
void register_elements(ElementRegistry& registry, std::string_view library, std::string_view source,
                       std::string_view sink, std::vector<SignalType> types, std::string_view backend,
                       std::string_view category) {
    // Names passed here are static plugin literals, as required by Element's
    // non-owning type_name. Factories/metadata contain no live device resources.
    for (const auto name:{source,sink}) {
        if (registry.find(std::string(name))) continue;
        const bool capture=name==source;
        ElementMetadata m;
        m.type_name=name; m.display_name=name; m.library=library; m.category=category; m.version="0.1.0";
        m.description="Explicitly activated hardware boundary. One simultaneous frame per graph step; use hardware::Run. Strict loss policy, no fallback or implicit rate conversion.";
        m.tags={"hardware",capture?"source":"sink","stateful"};
        auto param=[&](std::string n,std::string t,boost::json::value v,std::string d) {
            ParameterSpec p; p.name=std::move(n); p.type=std::move(t); p.default_value=std::move(v); p.description=std::move(d);
            m.parameters.push_back(std::move(p));
        };
        param("type","type",{},"Explicit application value type");
        param("backend","string",boost::json::value(backend),"Explicit provider; missing providers fail");
        param("device","string","default","Backend selector; default explicitly follows OS policy");
        param("channels","array",boost::json::array{"0"},"Ordered backend channel IDs, 1..64");
        param("frame_rate","array",boost::json::array{48000,1},"Exact nominal frames/second [numerator,denominator]");
        param("block_frames","int",256,"Maximum transfer block, independent of graph steps");
        m.parameters.back().min_value=1; m.parameters.back().max_value=1048576;
        param("queue_frames","int",4096,"Bounded application handoff/prefill capacity in frames");
        m.parameters.back().min_value=1; m.parameters.back().max_value=16777216;
        param("options","string","{}","Backend options as a JSON object of string values; unknown keys fail");
        m.type_variables.push_back({"T",types,"type",{}});
        VariadicInputSpec ports; ports.enabled=true; ports.min_count=1; ports.max_count=64;
        ports.port_name_template="{channel}"; ports.type_rule="T"; ports.count_parameter="channels";
        if (capture) m.variadic_outputs=ports; else m.variadic_inputs=ports;
        m.visual={capture?"circle":"box","#65A5C5",180,120,""};
        registry.register_element(ElementDescriptor(std::string(name),[name,capture](const ElementCreationContext& c)->std::unique_ptr<Element> {
            auto r=request_from_context(c,capture?Direction::Capture:Direction::Playback);
            if (capture) return std::make_unique<Source>(std::move(r),name);
            return std::make_unique<Sink>(std::move(r),name);
        },std::move(m)));
    }
}
} // namespace s7::hardware
