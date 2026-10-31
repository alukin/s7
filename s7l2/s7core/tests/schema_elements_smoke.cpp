// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
boost::json::object constant(const char* id,double value) {
    return {{"id",id},{"type","Constant"},{"params",{{"value",value}}}};
}
boost::json::value model(const char* type,const boost::json::array& values) {
    auto& registry=s7::ElementRegistry::instance();
    const auto* descriptor=registry.find(type);
    require(descriptor && descriptor->metadata().schema_definition.has_value(),"Bundled component is a schema element");
    boost::json::array elements{boost::json::object{{"id","component"},{"type",type}}},connections;
    for(std::size_t n=0;n<values.size();++n) {
        const auto id="input"+std::to_string(n);
        elements.push_back(constant(id.c_str(),values[n].to_number<double>()));
        connections.push_back(boost::json::object{{"from_element",id},{"from_port",0},{"to_element","component"},{"to_port",n}});
    }
    return boost::json::object{{"name","schema_component"},{"elements",elements},{"connections",connections}};
}
void references() {
    for(double dt : {.001,.0001}) {
        for(const char* name : {"FirstOrderLag","RMSDetector","PIController","PIDController"}) {
            const std::string type=name;
            auto json=model(name,type=="PIController" ? boost::json::array{1,.1,.2} :
                type=="PIDController" ? boost::json::array{1,0,.1,.2,.3,.1} : boost::json::array{2,.1});
            require(s7::configure_schema_discretization(json,dt),"Configure composite rate");
            auto schema=s7::SchemaLibraryLoader::build_schema(json);
            const auto checked=s7::Validator::validate(schema);
            for(const auto& error : checked.errors) std::cerr<<error<<'\n';
            require(checked.ok(),"Resource graph validates with its exposed ports");
            s7::NodeRuntime runtime(s7::Compiler::compile(schema));
            for(int n=0;n<500;++n) {
                runtime.step();
                const double expected=type=="FirstOrderLag" ? 2*(1-std::pow(1-dt/.1,n)) :
                    type=="RMSDetector" ? std::sqrt(4*(1-std::pow(1-dt/.1,n))) : .1+.2*dt*n;
                require(std::abs(runtime.real_output_of("component")-expected)<2e-12,"Composite agrees with independent discrete transition reference");
            }
        }
    }
    auto phase=model("PhaseDifference",{4,-4});s7::configure_schema_discretization(phase,.125);
    auto schema=s7::SchemaLibraryLoader::build_schema(phase);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.step();
    require(std::abs(runtime.real_output_of("component")-(8-2*s7::math::pi_v<double>))<1e-14,"Phase difference uses its graph and wrap primitive");
}
void controller_transients() {
    for (bool pid : {false,true}) {
        auto json=model(pid ? "PIDController" : "PIController",pid ? boost::json::array{1,0,.3,1,.01,.1} : boost::json::array{10,2,1});
        auto& elements=json.as_object().at("elements").as_array();
        for(auto& value : elements) {
            auto& e=value.as_object();
            if(e.at("id").as_string()==(pid ? "input1" : "input0")) {
                e["type"]="StepGenerator";
                e["params"]=boost::json::object{{"initial_value",pid ? 0 : 10},{"final_value",pid ? .2 : 0},{"start_sample",pid ? 100 : 1000}};
            }
        }
        s7::configure_schema_discretization(json,.001);
        auto schema=s7::SchemaLibraryLoader::build_schema(json);
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        double integral=0,filter=0;
        for(int n=0;n<(pid ? 500 : 2000);++n) {
            const double measurement=pid && n>=100 ? .2 : 0;
            const double error=pid ? 1-measurement : n<1000 ? 10 : 0;
            const double rate=(measurement-filter)/.1;
            const double raw=(pid ? .3 : 2)*error+integral-(pid ? .01*rate : 0);
            const double control=raw>1 ? 1 : raw< -1 ? -1 : raw;
            runtime.step();
            require(std::abs(runtime.real_output_of("component",0)-control)<2e-12,"Controller step/disturbance response");
            require(std::abs(runtime.real_output_of("component",1)-raw)<2e-12,"Controller internal-state/anti-windup reference");
            integral+=.001*(error+10*(control-raw));filter+=.001*rate;
        }
    }
}

void independence_and_limits() {
    const auto* pi=s7::ElementRegistry::instance().find("PIController");
    require(pi->metadata().schema_definition->as_object().at("dt").to_number<double>()==.001,"Resource retains default rate");
    auto a=model("PIController",{10,2,1}), b=a;
    s7::configure_schema_discretization(a,.0001);
    require(!b.as_object().at("elements").as_array()[0].as_object().contains("schema"),"Materialization does not modify another instance");
    require(pi->metadata().schema_definition->as_object().at("dt").to_number<double>()==.001,"Configuring a component never mutates registry templates");
    auto schema=s7::SchemaLibraryLoader::build_schema(a);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for(int n=0;n<2000;++n) {runtime.step();require(runtime.real_output_of("component")==1,"PI normalized saturation");}
    // A nested explicit rate must be visible to parent connection validation:
    // two nested schemas declaring different rates cannot be connected.
    auto mismatch=model("FirstOrderLag",{2,.1});
    auto slower=*s7::ElementRegistry::instance().find("FirstOrderLag")->metadata().schema_definition;
    slower.as_object()["dt"]=.002;
    mismatch.as_object().at("elements").as_array().push_back(boost::json::object{{"id","other"},{"type","FirstOrderLag"},{"schema",slower}});
    mismatch.as_object().at("elements").as_array().push_back(constant("other_tau",.1));
    mismatch.as_object().at("connections").as_array().push_back(boost::json::object{{"from_element","component"},{"from_port",0},{"to_element","other"},{"to_port",0}});
    mismatch.as_object().at("connections").as_array().push_back(boost::json::object{{"from_element","other_tau"},{"from_port",0},{"to_element","other"},{"to_port",1}});
    auto invalid=s7::SchemaLibraryLoader::build_schema(mismatch);
    require(!s7::Validator::validate(invalid).ok(),"Parent detects rate conflict with nested schema");
    // Inside a nested schema, elements follow its rate: a dt stored in its
    // document is a legacy field and cannot contradict the schema's rate.
    auto stored_dt=model("FirstOrderLag",{2,.1});
    auto nested=*s7::ElementRegistry::instance().find("FirstOrderLag")->metadata().schema_definition;
    for(auto& e : nested.as_object().at("elements").as_array())
        if(e.as_object().at("id").as_string()=="state") e.as_object()["params"].as_object()["dt"]=.002;
    stored_dt.as_object().at("elements").as_array()[0].as_object()["schema"]=nested;
    auto follows=s7::SchemaLibraryLoader::build_schema(stored_dt);
    s7::NodeRuntime following(s7::Compiler::compile(follows));
    auto reference=s7::SchemaLibraryLoader::build_schema(model("FirstOrderLag",{2,.1}));
    s7::NodeRuntime unchanged(s7::Compiler::compile(reference));
    for(int n=0;n<100;++n) {
        following.step(); unchanged.step();
        require(following.real_output_of("component")==unchanged.real_output_of("component"),
                "Nested elements follow the nested schema's rate, not a stored dt");
    }
    // A schema remains direct feedthrough; internal memory is not silently
    // promoted into a new outer-loop causal boundary.
    require(schema.elements().size()>0,"Schema resource materializes");
    auto feedback=model("FirstOrderLag",{2,.1});
    auto& connections=feedback.as_object().at("connections").as_array();
    connections[0].as_object()["from_element"]="component";
    auto cycle=s7::SchemaLibraryLoader::build_schema(feedback);
    require(!s7::Validator::validate(cycle).ok(),"Schema-is-element preserves existing direct-feedthrough cycle rules");
}
}
int main() {
    try {
        auto& registry=s7::ElementRegistry::instance();s7::register_builtin_elements(registry);
        const auto count=registry.element_count();s7::register_builtin_elements(registry);
        require(registry.element_count()==count,"Core and bundled resource registration is idempotent");
        references();controller_transients();independence_and_limits();
        std::cout<<"Schema resource: PI/PID, lag, RMS, phase difference, independent instances and nested rates passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
