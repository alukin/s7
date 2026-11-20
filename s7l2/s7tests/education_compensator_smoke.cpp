// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_compensator_reference.hpp"
#include <s7/schema_library.hpp>
#include <s7spectral/analysis.hpp>
#include <iostream>
namespace {
using namespace s7tests::education;
void require(bool ok,const std::string& why) {if(!ok) throw std::runtime_error(why);}
ControlTrace run(int variant) {
    auto doc=s7tests::read_json_object(S7_CT08_DIAGRAM);require(doc.at("dt").to_number<double>()==.1 && doc.at("studio_run").at("runtime_seconds").to_number<double>()==64,"saved CT08 profile");
    std::vector<s7::ProbePoint> probes;
    for(auto& e:doc.at("schema").at("elements").as_array()) {
        const auto id=std::string(e.at("id").as_string());
        if((variant==1 || variant==2) && id=="zero") e.at("params").as_object()["value"]=.2;
        if(variant==2 && (id=="pole" || id=="scale")) e.at("params").as_object()["value"]=id=="pole" ? .8:1;
        if(variant==3 && id=="lead_error") e.at("params").as_object()["weights"]=boost::json::array{1,1};
        probes.push_back({id,0});if(id=="lead" || id=="open") {probes.push_back({id,1});probes.push_back({id,2});}
    }
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(10));
    const auto checked=s7::Validator::validate(schema);for(const auto& e:checked.errors) std::cerr<<e<<'\n';require(checked.ok(),"CT08 reusable schema loop validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(10));s7::ObservationService observe(runtime);
    const auto expected=compensator_reference(variant);ControlTrace actual;double worst=0,scaled=0;
    for(std::size_t n=0;n<expected.size();++n) {
        runtime.step();const auto snap=observe.snapshot(probes);require(snap.step==n+1 && snap.model_time_ns==std::int64_t(n+1)*100000000,"CT08 step/model time");ControlSample sample;
        for(std::size_t j=0;j<probes.size();++j) {
            const auto key=control_key(probes[j].element_id,probes[j].port);const auto value=snap.samples[j].to_display_double(),target=expected[n].at(key);
            worst=std::max(worst,std::abs(value-target));scaled=std::max(scaled,std::abs(value-target)/std::max(1.,std::abs(target)));require(std::isfinite(value) && std::abs(value-target)<1e-10*std::max(1.,std::abs(target)),"CT08 independent sample "+key+" n="+std::to_string(n));sample[key]=value;
        }actual.push_back(std::move(sample));
    }
    if(variant!=3) {
        std::vector<double> input,output;for(const auto& s:actual) {input.push_back(s.at("pulse:0"));output.push_back(s.at("open_y:0"));}
        const auto response=s7spectral::impulseResponse(input,output,10);double residual=0;
        for(std::size_t k=0;k<response.values.size();++k) {
            const auto error=std::abs(response.values[k]-compensator_transfer(response.frequency[k],variant));residual=std::max(residual,error);require(error<1e-10,"CT08 rational open-loop response");
        }std::cout<<"CT08 variant="<<variant<<" max response residual="<<residual<<'\n';
    }
    std::cout<<"CT08 variant="<<variant<<" max state residual="<<worst<<" max scaled residual="<<scaled<<'\n';return actual;
}
}
int main() try {
    using namespace s7tests::education;s7::register_builtin_elements(s7::ElementRegistry::instance());
    const auto base=run(0),edited=run(1),lag=run(2),wrong=run(3);require(base==run(0),"fresh CT08 repeatability");
    require(base[10].at("lead:0")==8 && base[10].at("base:0")==2,"lead transient has greater initial control effort");
    require(std::abs(base[80].at("lead_y:0")-.4)<1e-6 && std::abs(base[80].at("base_y:0")-.4)>1e-4,"declared lead settles faster toward the same equilibrium");
    for(std::size_t n=0;n<edited.size();++n) require(std::abs(edited[n].at("lead_y:0")-loop_states(int(n),16,.5).second)<1e-10,"primary pole/zero cancellation equals analytic P16 loop");
    require(std::abs(lag.back().at("lead_y:0")-.4)<.005,"DC-matched lag remains a distinct convergent experiment");
    require(std::abs(wrong.back().at("lead_y:0"))>100,"wrong feedback sign is a declared growing case");
    for(const auto z:{std::complex<double>(.5,0),std::complex<double>(.75,std::sqrt(.0175)),std::complex<double>(.75,-std::sqrt(.0175))})
        require(std::abs((z-.2)*(z-.9)*(z-.9)+.16*(z-.8))<1e-14 && std::abs(z)<1,"independent baseline cubic roots");
    std::cout<<"CT08 reusable lead/lag, cancellation, effort and wrong-sign checks passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
