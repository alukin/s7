// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/core.hpp>
#include <s7/net.hpp>
#include <s7/deployment_client.hpp>
#include <s7/schema_library.hpp>
#include <boost/json.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <thread>

namespace j=boost::json;
using namespace s7;
const std::array<std::string,3> nodes{"aaaaaaaa-0000-4000-8000-000000000001","bbbbbbbb-0000-4000-8000-000000000002","cccccccc-0000-4000-8000-000000000003"};
void expect(bool ok,const std::string& message) {if(!ok)throw std::runtime_error(message);}
j::object element(const char* id,const char* type,const std::string& node,j::object params={}) {
    return {{"id",id},{"type",type},{"node",node},{"params",std::move(params)}};
}
j::object connection(const char* a,const char* b,std::size_t port=0) {
    return {{"from_element",a},{"from_port",0},{"to_element",b},{"to_port",port}};
}
j::object loop(std::size_t memory,bool nested,bool third) {
    j::array runtimes;
    for(const auto& node:nodes)runtimes.emplace_back(j::object{{"node_uuid",node},{"endpoint","ws://127.0.0.1:9000"}});
    auto state=element("state","Delay",nodes[memory],{{"initial_value",0.0}});
    if(nested) {
        state["type"]="Schema";
        state["schema"]=j::object{{"name","memory"},{"elements",j::array{j::object{{"id","hold"},{"type","Delay"}}}},{"connections",j::array{}},
            {"exposed_inputs",j::array{j::object{{"name","u"},{"type","F64"},{"element","hold"},{"port",0}}}},
            {"exposed_outputs",j::array{j::object{{"name","y"},{"type","F64"},{"element","hold"},{"port",0}}}}};
    }
    const auto other=(memory+1)%2;
    j::array elements{state,element("first","Gain",nodes[memory],{{"factor",1}}),
        element("one","Constant",nodes[other],{{"value",1}}),element("sum","Adder",nodes[other])};
    j::array connections{connection("state","first"),connection("first","sum"),connection("one","sum",1)};
    if(third) {elements.emplace_back(element("last","Gain",nodes[2],{{"factor",1}}));connections.emplace_back(connection("sum","last"));connections.emplace_back(connection("last","state"));}
    else connections.emplace_back(connection("sum","state"));
    return {{"runtime_nodes",runtimes},{"dt",.001},{"schema",j::object{{"name","loop"},{"dt",.001},{"elements",elements},{"connections",connections}}}};
}
void assign(j::object& d) {auto e=deployment::assign_streams(d,deployment::infer_output_types(d));expect(e.empty(),e.empty()?"":e.front());}
void plan(j::object& d) {assign(d);auto e=deployment::plan_causal_schedule(d);expect(e.empty(),e.empty()?"":e.front());}
void run(j::object d,const std::vector<std::string>& observed,const std::vector<double>& initial,bool ramp) {
    plan(d);
    // A one-packet window exercises consumption credit as well as ordering.
    for(auto& value:d.at("streams").as_array()) {
        auto& s=value.as_object();s["max_elements_per_packet"]=1;s["send_log"]=j::object{{"max_packets",1},{"max_block_ms",500}};s["max_wait_ms"]=500;
    }
    auto endpoint=std::make_shared<transport::InMemoryRuntimeNetEndpoint>();RuntimeNetContext::set_endpoint(endpoint);
    std::array<std::exception_ptr,3> failures{};std::vector<std::thread> owners;
    std::vector<ExecutableGraph> graphs;
    for(std::size_t i=0;i<observed.size();++i) {
        auto part=deployment::extract_node_schema(d,nodes[i]);
        auto schema=SchemaLibraryLoader::build_schema(part,ElementRegistry::instance());schema.set_rate(Rate::from_hz(1000));
        graphs.push_back(Compiler::compile(schema));
    }
    for(std::size_t i=0;i<observed.size();++i) {
        owners.emplace_back([&,i,graph=std::move(graphs[i])]()mutable {
            try {
                NodeRuntime r(std::move(graph));
                for(std::size_t n=0;n<128;++n) {
                    if(i==1 && n%13==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    r.step();expect(r.real_output_of(observed[i])==initial[i]+(ramp?double(n):0.),"reference changed at "+std::to_string(n));
                }
            } catch(...) {failures[i]=std::current_exception();}
        });
    }
    for(auto& owner:owners) owner.join();
    RuntimeNetContext::set_endpoint(nullptr);
    for(auto& failure:failures)if(failure)std::rethrow_exception(failure);
    for(const auto& status:endpoint->stream_statuses())expect(status.fault==stream::FaultCode::None && status.send_log_depth==0,"stream did not drain");
}
void execute(j::object d,std::size_t memory,bool third) {
    std::vector<std::string> observed;std::vector<double> initial;
    for(std::size_t i=0;i<(third?3u:2u);++i) {observed.push_back(i==memory?"first":i==2?"last":"sum");initial.push_back(i==memory?0.:1.);}
    run(std::move(d),observed,initial,true);
}
void independent() {
    auto d=loop(0,false,false);auto& schema=d.at("schema").as_object();
    schema["elements"]=j::array{element("a","Constant",nodes[0],{{"value",2}}),element("b","Constant",nodes[1],{{"value",3}}),
        element("a_in","Gain",nodes[0],{{"factor",1}}),element("b_in","Gain",nodes[1],{{"factor",1}})};
    schema["connections"]=j::array{connection("a","b_in"),connection("b","a_in")};plan(d);
    const auto before=d.at("causal_schedule");
    auto& reordered=d.at("schema").at("elements").as_array();
    std::reverse(reordered.begin(),reordered.end());plan(d);
    expect(before==d.at("causal_schedule"),"insertion order changed plan");
    for(const auto& node:{nodes[0],nodes[1]}) {
        const auto& order=d.at("causal_schedule").at("nodes").at(node).as_array();
        auto position=[&](const std::string& suffix){for(std::size_t n=0;n<order.size();++n)if(std::string(order[n].as_string()).ends_with(suffix))return n;return order.size();};
        expect(position("_out")<position("_in"),"ready outgoing work held behind receive");
    }
    run(d,{"a_in","b_in"},{3.,2.},false);
    // Local fan-out shares one incoming stream value rather than receiving it twice.
    auto fanout=loop(0,false,false);auto& f=fanout.at("schema").as_object();
    f["elements"]=j::array{element("a","Constant",nodes[0],{{"value",2}}),element("probe","Gain",nodes[0]),
        element("g1","Gain",nodes[1]),element("g2","Gain",nodes[1],{{"factor",3}}),element("sum","Adder",nodes[1])};
    f["connections"]=j::array{connection("a","g1"),connection("a","g2"),connection("g1","sum"),connection("g2","sum",1),connection("sum","probe")};
    run(fanout,{"probe","sum"},{8.,8.},false);
    auto tampered=d;
    for(auto& item:tampered.at("causal_schedule").at("nodes").as_object()) {
        auto& order=item.value().as_array();
        std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return std::string(a.as_string()).ends_with("_in")>std::string(b.as_string()).ends_with("_in");});
    }
    bool refused=false;try{static_cast<void>(deployment::extract_node_schema(tampered,nodes[0]));}catch(const std::invalid_argument&){refused=true;}
    expect(refused,"cross-node wait cycle in received schedule accepted");
}
int main() try {
    register_builtin_elements(ElementRegistry::instance());register_net_elements(ElementRegistry::instance());
    for(std::size_t memory=0;memory<2;++memory)for(bool nested:{false,true})execute(loop(memory,nested,false),memory,false);
    execute(loop(0,true,true),0,true);independent();
    auto invalid=loop(0,false,false);invalid.at("schema").at("elements").as_array()[0].as_object()["type"]="Gain";
    invalid.at("schema").at("elements").as_array()[0].as_object()["params"]=j::object{{"factor",1}};
    assign(invalid);const auto before=j::serialize(invalid);const auto errors=deployment::plan_causal_schedule(invalid);
    expect(!errors.empty() && j::serialize(invalid)==before,"invalid graph mutated or accepted");
    expect(errors.front().find("state")!=std::string::npos && errors.front().find("Cross-node streams")!=std::string::npos,"cycle diagnostic missing elements/streams");
    int requests=0;const auto result=deployment::deploy_diagram(invalid,deployment::infer_output_types(invalid),[&](const auto&,const auto&)->j::object{++requests;return {};});
    expect(!result.committed && !result.errors.empty() && requests==0,"invalid cycle reached a node");
    auto direct_state=loop(0,false,false);
    direct_state.at("schema").at("elements").as_array()[0].as_object()["type"]="Accumulator";
    assign(direct_state);expect(!deployment::plan_causal_schedule(direct_state).empty(),"stateful direct-feedthrough cycle accepted");
    auto bypass=loop(0,false,false);auto& edges=bypass.at("schema").at("connections").as_array();
    edges[0]=connection("sum","first");edges[2]=connection("state","sum",1);
    assign(bypass);expect(!deployment::plan_causal_schedule(bypass).empty(),"memory elsewhere in SCC accepted a direct cycle");
    auto nested_bad=loop(0,true,false);
    nested_bad.at("schema").at("elements").as_array()[0].at("schema").at("elements").as_array()[0].as_object()["type"]="Gain";
    assign(nested_bad);expect(!deployment::plan_causal_schedule(nested_bad).empty(),"nested direct cycle accepted");
    auto local=loop(0,false,false);for(auto& e:local.at("schema").at("elements").as_array())e.as_object()["node"]=nodes[0];plan(local);
    expect(!local.contains("causal_schedule"),"local graph scheduling path changed");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
