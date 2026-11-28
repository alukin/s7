// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <s7/core.hpp>
#include <s7/json_parse.hpp>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <zlib.h>
#include <iostream>
#include <stdexcept>
#include <tuple>

namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
void corrupt_gzip_library() {
    const auto path = std::filesystem::temp_directory_path() / ("s7-corrupt-library-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".gz");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove(path); } } cleanup{path};
    auto file = gzopen(path.string().c_str(), "wb");
    require(file != nullptr, "Create compressed fixture");
    require(gzwrite(file, "{}", 2) == 2, "Write compressed fixture");
    require(gzclose(file) == Z_OK, "Close compressed fixture");
    std::fstream bytes(path, std::ios::in | std::ios::out | std::ios::binary);
    bytes.seekg(-8, std::ios::end); char crc{}; bytes.get(crc);
    bytes.seekp(-8, std::ios::end); bytes.put(crc ^ 0xff); bytes.close();
    bool rejected = false;
    try { (void)s7::SchemaLibraryLoader::load_from_file(path); }
    catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("compressed element library") != std::string::npos; }
    require(rejected, "Corrupt gzip reports a valid error without reading freed zlib storage");
}
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
                e["params"]=boost::json::object{{"initial_value",pid ? 0 : 10},{"final_value",pid ? .2 : 0},{"start_time",pid ? .1 : 1.0}};
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
    auto stored_dt=model("FirstOrderLag",{2,.1});
    auto nested=*s7::ElementRegistry::instance().find("FirstOrderLag")->metadata().schema_definition;
    for(auto& e : nested.as_object().at("elements").as_array())
        if(e.as_object().at("id").as_string()=="state") e.as_object()["params"].as_object()["dt"]=.002;
    stored_dt.as_object().at("elements").as_array()[0].as_object()["schema"]=nested;
    const auto validation=s7::Validator::validate(s7::SchemaLibraryLoader::build_schema(stored_dt));
    require(!validation.ok(),"Nested integrators reject obsolete element dt");
    // ADR-024 checks the flattened paths: FirstOrderLag's integrator closes
    // this same-node loop. Seed the whole graph, including the original input
    // constant that becomes disconnected when its wire is replaced.
    require(schema.elements().size()>0,"Schema resource materializes");
    auto feedback=model("FirstOrderLag",{2,.1});
    auto& connections=feedback.as_object().at("connections").as_array();
    connections[0].as_object()["from_element"]="component";
    s7::configure_schema_discretization(feedback,.001);
    auto cycle=s7::SchemaLibraryLoader::build_schema(feedback);
    require(s7::Validator::validate(cycle).ok(),"Feedback through nested causal memory is valid");
    s7::NodeRuntime closed_loop(s7::Compiler::compile(cycle));
    for(int n=0;n<10;++n) {
        closed_loop.step();
        require(closed_loop.real_output_of("component")==0,"Self-fed lag retains its zero equilibrium");
    }

    auto direct=model("PhaseDifference",{4,-4});
    direct.as_object().at("connections").as_array()[0].as_object()["from_element"]="component";
    s7::configure_schema_discretization(direct,.001);
    const auto direct_validation=s7::Validator::validate(s7::SchemaLibraryLoader::build_schema(direct));
    bool direct_cycle=false;
    for(const auto& error : direct_validation.errors)
        direct_cycle |= error.find("Cycle detected without causal memory.")!=std::string::npos;
    require(direct_cycle,"A direct nested path still cannot close a feedback cycle");
}
void lead_lag() {
    for(const auto& [scale,zero,pole]:{std::tuple{16.,.8,.2},std::tuple{16.,.2,.2},std::tuple{1.,.2,.8},std::tuple{1.,.2,1.2}}) {
        auto json=model("LeadLagController",{0,scale,zero,pole});
        auto& input=json.as_object().at("elements").as_array()[1].as_object();
        input["type"]="StepGenerator";
        input["params"]=boost::json::object{{"initial_value",1},{"final_value",-.25},{"start_time",.017}};
        s7::configure_schema_discretization(json,.001);
        auto schema=s7::SchemaLibraryLoader::build_schema(json);
        require(s7::Validator::validate(schema).ok(),"Lead/lag resource validates");
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));long double previous_error=0,previous_control=0;
        for(int n=0;n<80;++n) {
            const long double error=n<17 ? 1.L:-.25L;
            const long double control=pole*previous_control+scale*(error-zero*previous_error);
            runtime.step();
            const auto check=[&](std::size_t port,long double expected) {
                require(std::abs(runtime.real_output_of("component",port)-expected)<1e-10L*std::max(1.L,std::abs(expected)),"Lead/lag independent transition and memory publication");
            };
            check(0,control);check(1,previous_error);check(2,previous_control);
            if(zero==pole) require(std::abs(control-scale*error)<1e-10,"Exact pole/zero cancellation gives proportional control");
            previous_error=error;previous_control=control;
        }
    }
    auto direct=model("LeadLagController",{1,16,.8,.2});
    direct.as_object().at("connections").as_array()[0].as_object()["from_element"]="component";
    s7::configure_schema_discretization(direct,.001);
    require(!s7::Validator::validate(s7::SchemaLibraryLoader::build_schema(direct)).ok(),"Lead/lag direct error path cannot close an algebraic loop");
}

// AudioEqualizer (tools/generate_audio_equalizer.py): the measured response of
// the bundled graph against the documented band transfer functions, evaluated
// here in the frequency domain, independently of the graph's wiring.
struct EqualizerBand { bool shelf, low; double frequency, bandwidth; };
constexpr std::array<EqualizerBand,5> equalizer_bands{{{true,true,100,0},{false,false,400,400},{false,false,1600,1600},
                                                     {false,false,6000,6000},{true,false,12000,0}}};
constexpr double audio_rate=48000;
double equalizer_response(double frequency,const std::array<double,5>& gains_db) {
    using std::numbers::pi;
    const auto z1=std::polar(1.0,-2*pi*frequency/audio_rate);
    std::complex<double> h=1;
    for(std::size_t b=0;b<equalizer_bands.size();++b) {
        const auto& band=equalizer_bands[b];
        const double k=std::pow(10.0,gains_db[b]/20);
        std::complex<double> allpass;
        if(band.shelf) {
            const double t=std::tan(pi*band.frequency/audio_rate),a=(t-1)/(t+1);
            allpass=(a+z1)/(1.0+a*z1);
        } else {
            const double t=std::tan(pi*band.bandwidth/audio_rate),c=(t-1)/(t+1),d=-std::cos(2*pi*band.frequency/audio_rate);
            allpass=(-c+d*(1-c)*z1+z1*z1)/(1.0+d*(1-c)*z1-c*z1*z1);
        }
        h*=band.low ? (1+k)/2+(k-1)/2*allpass : (1+k)/2+(1-k)/2*allpass;
    }
    return std::abs(h);
}
boost::json::value equalizer_model(double frequency,const std::array<double,5>& gains_db) {
    auto json=model("AudioEqualizer",{0,gains_db[0],gains_db[1],gains_db[2],gains_db[3],gains_db[4]});
    auto& input=json.as_object().at("elements").as_array()[1].as_object();
    input["type"]="AnalogOscillator";
    input["params"]=boost::json::object{{"type","F64"},{"amplitude",.5},{"frequency_hz",frequency}};
    return json;
}
// Gain of the component at `frequency`: output and input projected on the
// tone over whole periods after the transient has decayed.
double measured_gain(s7::NodeRuntime& runtime,double frequency,int settle,int samples,double* peak=nullptr) {
    using std::numbers::pi;
    std::complex<double> in{},out{};
    for(int n=0;n<settle+samples;++n) {
        runtime.step();
        const double y=runtime.real_output_of("component",0);
        if(peak) *peak=std::max(*peak,std::abs(y));
        if(n<settle) continue;
        const auto tone=std::polar(1.0,-2*pi*frequency*n/audio_rate);
        in+=runtime.real_output_of("input0",0)*tone;
        out+=y*tone;
    }
    return std::abs(out)/std::abs(in);
}
void audio_equalizer() {
    const auto compile=[](boost::json::value json) {
        require(s7::configure_schema_discretization(json,1/audio_rate),"Configure the equalizer at 48 kHz");
        auto schema=s7::SchemaLibraryLoader::build_schema(json);
        const auto checked=s7::Validator::validate(schema);
        for(const auto& error : checked.errors) std::cerr<<error<<'\n';
        require(checked.ok(),"AudioEqualizer validates with its exposed ports");
        return s7::NodeRuntime(s7::Compiler::compile(schema));
    };
    {   // 0 dB everywhere: transparent, sample by sample.
        auto runtime=compile(equalizer_model(1000,{}));
        for(int n=0;n<2000;++n) {
            runtime.step();
            require(std::abs(runtime.real_output_of("component",0)-runtime.real_output_of("input0",0))<1e-12,"AudioEqualizer at 0 dB is transparent");
        }
    }
    for(std::size_t b=0;b<equalizer_bands.size();++b) {
        for(double db : {12.0,-12.0}) {
            std::array<double,5> gains{};gains[b]=db;
            for(double frequency : {20.0,equalizer_bands[b].frequency,1000.0,12000.0}) {
                auto runtime=compile(equalizer_model(frequency,gains));
                const double gain=measured_gain(runtime,frequency,2400,4800),expected=equalizer_response(frequency,gains);
                if(std::abs(20*std::log10(gain/expected))>1e-3) {
                    std::cerr<<"band "<<b<<' '<<db<<" dB at "<<frequency<<" Hz: "<<20*std::log10(gain)<<" dB, expected "<<20*std::log10(expected)<<'\n';
                    throw std::runtime_error("AudioEqualizer response differs from the band transfer functions");
                }
                // A peak's own centre frequency gets exactly its gain.
                if(!equalizer_bands[b].shelf && frequency==equalizer_bands[b].frequency)
                    require(std::abs(20*std::log10(gain)-db)<1e-3,"AudioEqualizer peak centre gain equals its dB input");
            }
        }
    }
    {   // A gain input jumping from +12 to -12 dB while running stays bounded
        // and settles to the new response.
        auto json=equalizer_model(400,{});
        auto& gain=json.as_object().at("elements").as_array()[3].as_object();
        gain["type"]="StepGenerator";
        gain["params"]=boost::json::object{{"initial_value",12},{"final_value",-12},{"start_time",.05}};
        auto runtime=compile(json);
        double peak=0;
        for(int n=0;n<2400;++n) {runtime.step();peak=std::max(peak,std::abs(runtime.real_output_of("component",0)));}
        const double settled=measured_gain(runtime,400,2400,4800,&peak);
        require(peak<.5*4.5,"AudioEqualizer stays bounded when a gain changes while running");
        require(std::abs(20*std::log10(settled)+12)<1e-3,"AudioEqualizer settles to the changed gain");
    }
    {   // The schema declares its 48 kHz design rate: a graph at another
        // rate is refused instead of silently shifting the bands.
        auto json=equalizer_model(1000,{});
        json.as_object().at("elements").as_array().push_back(boost::json::object{{"id","downstream"},{"type","IirFilter"},
            {"params",{{"design_rate_hz",44100}}}});
        json.as_object().at("connections").as_array().push_back(boost::json::object{{"from_element","component"},{"from_port",0},
            {"to_element","downstream"},{"to_port",0}});
        bool conflict=false;
        try {
            const auto checked=s7::Validator::validate(s7::SchemaLibraryLoader::build_schema(json));
            for(const auto& error : checked.errors) conflict=conflict || error.find("48000 Hz")!=std::string::npos;
            if(!conflict) for(const auto& error : checked.errors) std::cerr<<error<<'\n';
        } catch(const std::exception& error) {
            conflict=std::string(error.what()).find("48000")!=std::string::npos;
            if(!conflict) std::cerr<<error.what()<<'\n';
        }
        require(conflict,"AudioEqualizer at 44.1 kHz is refused with its design rate");
    }
    {   // Every band gain is a required input.
        boost::json::array elements{boost::json::object{{"id","component"},{"type","AudioEqualizer"}},constant("input0",0)};
        boost::json::array connections{boost::json::object{{"from_element","input0"},{"from_port",0},{"to_element","component"},{"to_port",0}}};
        boost::json::value json=boost::json::object{{"name","unconnected"},{"elements",elements},{"connections",connections}};
        s7::configure_schema_discretization(json,1/audio_rate);
        require(!s7::Validator::validate(s7::SchemaLibraryLoader::build_schema(json)).ok(),"AudioEqualizer without gain inputs is rejected");
    }
}

// Numbers in documents mean what their text says: Boost.JSON's default parser
// reads this decimal as the neighbouring double (found by the G2 audio chain).
void precise_numbers() {
    constexpr double exact=0.11512925464970229;
    require(s7::parse_json("0.11512925464970229").as_double()==exact,"parse_json rounds correctly");
    require(s7::parse_json("1.1512925464970229E-1").as_double()==exact,"a Boost.JSON-serialized number parses back to itself");
    const auto path=std::filesystem::temp_directory_path()/("s7-precise-library-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".s7library");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::filesystem::remove(path); } } cleanup{path};
    std::ofstream(path)<<R"({"document_kind":"s7.element_library","name":"precise","elements":[{"type_name":"PreciseGain",
        "display_name":"Precise gain","description":"","category":"test","version":"1.0.0","schema":{"name":"g",
        "elements":[{"id":"g","type":"Gain","params":{"factor":0.11512925464970229}}],"connections":[],
        "exposed_inputs":[{"name":"in","type":"F64","element":"g","port":0}],
        "exposed_outputs":[{"name":"out","type":"F64","element":"g","port":0}]}}]})";
    const auto library=s7::SchemaLibraryLoader::load_from_file(path);
    require(library.elements.at(0).schema.at("elements").at(0).at("params").at("factor").as_double()==exact,
            "An element library file keeps its numbers exactly");
}
}
int main() {
    try {
        auto& registry=s7::ElementRegistry::instance();s7::register_builtin_elements(registry);
        const auto count=registry.element_count();s7::register_builtin_elements(registry);
        require(registry.element_count()==count,"Core and bundled resource registration is idempotent");
        corrupt_gzip_library();
        references();controller_transients();independence_and_limits();lead_lag();audio_equalizer();precise_numbers();
        std::cout<<"Schema resource: PI/PID, lead/lag, plant lag, RMS, phase difference, independent instances, nested rates and the audio equalizer passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
