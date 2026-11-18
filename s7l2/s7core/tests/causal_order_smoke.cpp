// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/core.hpp>
#include <s7/schema_library.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <array>

using namespace s7;
void expect(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }

Schema nested(bool memory) {
    Schema s("outer");
    s.set_rate(Rate::from_hz(1000));
    s.add_element("c",[]{return std::make_unique<Constant>(3.0);});
    s.add_element("box",[memory] {
        auto box=std::make_unique<Schema>("box");
        if (memory) box->add_element("inner",[]{return std::make_unique<Delay>(0.0);});
        else box->add_element("inner",[]{return std::make_unique<Gain>(2.0);});
        box->expose_input("u",SignalType::F64,"inner",0);
        box->expose_output("y",SignalType::F64,"inner",0);
        return box;
    });
    s.add_element("g",[]{return std::make_unique<Gain>(2.0);});
    s.connect("c",0,"box",0);s.connect("box",0,"g",0);
    return s;
}

int main() try {
    for (const auto& bad:std::vector<std::vector<std::string>>{
            {"c"},{"c","box/inner","missing"},{"c","c","g"},{"g","box/inner","c"},{"c","box","g"}}) {
        auto s=nested(false);s.set_causal_order(bad);bool refused=false;
        try { static_cast<void>(Compiler::compile(s)); } catch(const std::invalid_argument& e) {refused=std::string(e.what()).find("causal_order")!=std::string::npos;}
        expect(refused,"invalid order accepted");
    }
    {
        auto s=nested(false);s.set_causal_order({"c","box/inner","g"});
        auto graph=Compiler::compile(s);
        expect(graph.causal_order==*s.causal_order(),"flattened order not applied");
        NodeRuntime r(std::move(graph));r.step();expect(r.real_output_of("g")==12.,"nested port dependency lost");
    }
    {
        auto s=nested(true);s.set_causal_order({"g","c"});
        NodeRuntime r(Compiler::compile(s));
        r.step();expect(r.real_output_of("g")==0.,"initial memory changed");
        r.step();expect(r.real_output_of("g")==6.,"memory stage changed");
        s.set_causal_order({"box/inner","c"});bool refused=false;
        try {static_cast<void>(Compiler::compile(s));}catch(const std::invalid_argument&){refused=true;}
        expect(refused,"memory element accepted in causal order");
    }
    {
        Schema s("only_memory");s.set_rate(Rate::from_hz(1000));s.add_element("d",[]{return std::make_unique<Delay>(1.0);});s.connect("d",0,"d",0);
        s.set_causal_order({});NodeRuntime r(Compiler::compile(s));r.step();expect(r.real_output_of("d")==1.,"empty causal phase");
    }
    auto& registry=ElementRegistry::instance();register_builtin_elements(registry);
    auto value=boost::json::parse(R"({"name":"json","elements":[{"id":"c","type":"Constant","params":{"value":4}},{"id":"g","type":"Gain","params":{"factor":2}}],"connections":[{"from_element":"c","from_port":0,"to_element":"g","to_port":0}],"causal_order":["c","g"]})");
    auto loaded=SchemaLibraryLoader::build_schema(value,registry);
    loaded.set_rate(Rate::from_hz(1000));
    expect(loaded.causal_order().has_value(),"library loader dropped order");
    auto moved=std::move(loaded);NodeRuntime r(Compiler::compile(moved));r.step();expect(r.real_output_of("g")==8.,"loaded order");
    {
        Schema standalone("standalone");standalone.set_rate(Rate::from_hz(1000));
        standalone.add_element("g",[]{return std::make_unique<Gain>(2.0);});
        standalone.expose_input("u",SignalType::F64,"g",0);standalone.expose_output("y",SignalType::F64,"g",0);
        standalone.set_causal_order({"g"});
        const double input=3.0;const std::array<InputBinding,1> bindings{&input};
        standalone.bind_inputs(bindings);standalone.step();
        expect(standalone.output_ports()[0].as<double>()==6.0,"standalone schema dropped order or inputs");
        standalone.set_causal_order({"missing"});
        bool refused=false;
        try {standalone.step();}catch(const std::invalid_argument& e){refused=std::string(e.what()).find("causal_order")!=std::string::npos;}
        expect(refused,"standalone schema ignored an invalid requested order");
    }
    for (auto bad:std::vector<boost::json::value>{true,boost::json::array{1}}) {
        value.as_object()["causal_order"]=bad;bool refused=false;
        try {static_cast<void>(SchemaLibraryLoader::build_schema(value,registry));}catch(const std::invalid_argument&){refused=true;}
        expect(refused,"malformed JSON order accepted");
    }
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
