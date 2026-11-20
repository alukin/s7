// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_loop_reference.hpp"
#include <s7/schema_library.hpp>
#include <s7spectral/analysis.hpp>
#include <iostream>
namespace {
using namespace s7tests::education;
void require(bool ok,const std::string& why) {if(!ok) throw std::runtime_error(why);}
ControlTrace run(bool margins,double gain) {
    auto doc=s7tests::read_json_object(std::filesystem::path(S7_LOOP_LABS_DIR)/(margins ? "ct07_stability_margins.s7diagram":"ct06_closed_loop_poles.s7diagram"));
    require(doc.at("dt").to_number<double>()==.1 && doc.at("studio_run").at("runtime_seconds").to_number<double>()==(margins ? 64:32),"saved loop profile");
    std::vector<s7::ProbePoint> probes;
    for(auto& e:doc.at("schema").at("elements").as_array()) {
        const auto id=std::string(e.at("id").as_string());if(id=="gain") e.at("params").as_object()["value"]=gain;
        probes.push_back({id,0});
    }
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(10));
    require(s7::Validator::validate(schema).ok(),"loop production validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(10));s7::ObservationService observe(runtime);
    const auto expected=loop_reference(margins,gain);ControlTrace actual;double worst=0;
    for(std::size_t n=0;n<expected.size();++n) {
        runtime.step();const auto snap=observe.snapshot(probes);require(snap.step==n+1 && snap.model_time_ns==std::int64_t(n+1)*100000000,"loop causal timestamp");
        require(std::abs(*snap.model_time_seconds-(n+1)/10.)<1e-12,"loop model seconds");ControlSample sample;
        for(std::size_t j=0;j<probes.size();++j) {
            const auto key=control_key(probes[j].element_id);const auto value=snap.samples[j].to_display_double(),target=expected[n].at(key);
            worst=std::max(worst,std::abs(value-target));require(std::isfinite(value) && std::abs(value-target)<1e-10*std::max(1.,std::abs(target)),"loop independent mode "+key+" n="+std::to_string(n));sample[key]=value;
        }actual.push_back(std::move(sample));
    }
    std::cout<<(margins ? "CT07":"CT06")<<" K="<<gain<<" max mode residual="<<worst<<'\n';return actual;
}
}
int main() try {
    using namespace s7tests::education;s7::register_builtin_elements(s7::ElementRegistry::instance());
    for(double gain:{0.,1.,4.,16.,19.,25.}) {
        const auto trace=run(false,gain);if(gain>0) {
            const std::complex<double> pole(.9,.1*std::sqrt(gain));
            require(std::abs(pole*pole-1.8*pole+(.81+.01*gain))<1e-14,"characteristic root");
            const double steady=gain/(1+gain);
            const auto mode=[&](std::size_t n) {return std::complex<double>(trace[n].at("x:0")-steady,std::sqrt(gain)*(trace[n].at("y:0")-steady));};
            for(std::size_t n=11;n<trace.size();++n) require(std::abs(mode(n)-pole*mode(n-1))<1e-10*std::max(1.,std::abs(mode(n))),"measured mode follows declared pole without extra delay");
            if(gain==19) require(std::abs(std::abs(pole)-1)<1e-12,"marginal pole on unit circle");
            else require((std::abs(pole)<1)==(gain<19),"unit-circle stability boundary");
            if(gain==25) require(std::abs(trace.back().at("y:0"))>10,"unstable gain is a declared failure case");
        }
    }
    for(double gain:{4.,16.,25.}) {
        const auto trace=run(true,gain);std::vector<double> input,output;
        for(const auto& s:trace) {input.push_back(s.at("pulse:0"));output.push_back(s.at("open_y:0"));}
        const auto response=s7spectral::impulseResponse(input,output,10);double worst=0;
        for(std::size_t k=0;k<response.frequency.size();++k) {
            const auto expected=loop_transfer(response.frequency[k],gain);worst=std::max(worst,std::abs(response.values[k]-expected));
            require(std::abs(response.values[k]-expected)<1e-10,"independent rational open-loop response");
        }
        const auto m=loop_margins(gain);const auto gc=loop_transfer(m.gainCrossover,gain),pc=loop_transfer(m.phaseCrossover,gain);
        require(std::abs(std::abs(gc)-1)<1e-12 && std::abs(std::remainder(180+std::arg(gc)*180/std::numbers::pi-m.phaseMargin,360.))<1e-10,"independent gain crossover/phase margin");
        require(std::abs(pc.imag())<1e-12 && std::abs(pc.real()+gain/19)<1e-12,"Nyquist negative-real crossing");
        require((m.gainMargin>1)==(gain<19) && (m.phaseMargin>0)==(gain<19),"margins agree with actual loop stability");
        const auto shortResponse=s7spectral::impulseResponse(std::span(input).first(24),std::span(output).first(24),10);
        require(std::abs(shortResponse.values.front()-loop_transfer(0,gain))>.1,"truncated tail is not a valid DC/margin estimate");
        std::cout<<"CT07 K="<<gain<<" GM="<<m.gainMargin<<" PM="<<m.phaseMargin<<" f_gc="<<m.gainCrossover<<" f_pc="<<m.phaseCrossover<<" response residual="<<worst<<'\n';
    }
    require(run(false,4)==run(false,4),"fresh loop repeatability");
    std::cout<<"CT06/CT07 poles, mode evolution, response and margins passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
