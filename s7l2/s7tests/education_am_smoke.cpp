// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_am_reference.hpp"
#include <s7/schema_library.hpp>
#include <s7spectral/analysis.hpp>
#include <iostream>
namespace {
using namespace s7tests::education;
void require(bool ok,const std::string& why) {if(!ok) throw std::runtime_error(why);}
ControlTrace run(double phase,double rate=256) {
    auto doc=s7tests::read_json_object(S7_SP14_DIAGRAM);require(doc.at("dt").to_number<double>()==1./256 && doc.at("studio_run").at("runtime_seconds").to_number<double>()==4,"saved SP14 profile");
    std::vector<s7::ProbePoint> probes;for(auto& e:doc.at("schema").at("elements").as_array()) {
        const auto id=std::string(e.at("id").as_string());if(id=="local") e.at("params").as_object()["phase"]=phase;probes.push_back({id,0});
    }
    s7::configure_schema_discretization(doc.at("schema"),1/rate);auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(rate));
    require(s7::Validator::validate(schema).ok(),"SP14 production validation");s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(rate));s7::ObservationService observe(runtime);
    const auto expected=am_reference(phase,rate);ControlTrace actual;double worst=0;
    for(std::size_t n=0;n<expected.size();++n) {
        runtime.step();const auto snap=observe.snapshot(probes);require(snap.step==n+1 && snap.model_time_ns==std::llround((n+1)*1e9/rate),"SP14 step and nanosecond time");require(std::abs(*snap.model_time_seconds-(n+1)/rate)<1e-12,"SP14 model seconds");ControlSample sample;
        for(std::size_t j=0;j<probes.size();++j) {
            const auto key=control_key(probes[j].element_id);const auto value=snap.samples[j].to_display_double(),target=expected[n].at(key);
            worst=std::max(worst,std::abs(value-target));require(std::isfinite(value) && std::abs(value-target)<1e-10,"SP14 source/convolution "+key+" n="+std::to_string(n));sample[key]=value;
        }actual.push_back(std::move(sample));
    }
    const auto size=std::size_t(2*rate);double spectrumResidual=0;
    for(const std::string channel:{"transmit","envelope","mixed","recovered"}) {
        std::vector<double> values;std::vector<s7spectral::Complex> data;
        for(std::size_t n=size;n<2*size;++n) {values.push_back(actual[n].at(control_key(channel)));data.push_back(values.back());}
        s7spectral::Transform(size).apply(data,false);double integrated=0,meanSquare=0;
        for(double v:values) meanSquare+=v*v/size;
        for(std::size_t k=0;k<=size/2;++k) {
            const double fold=k==0 || k==size/2 ? 1:2,f=k*rate/size;const auto direct=am_bin(values,k);
            require(std::abs(data[k]/double(size)-direct)<1e-10,"SP14 independent direct DFT versus production transform");
            const auto error=std::abs(fold*direct-am_steady(channel,f,phase,rate));spectrumResidual=std::max(spectrumResidual,error);
            require(error<1e-10,"SP14 absolute complex harmonic, image and filter calibration");integrated+=fold*std::norm(direct);
        }require(std::abs(integrated-meanSquare)<1e-10,"SP14 one-sided Parseval");
    }
    require(actual[0].at("recovered:0")==0 && actual[1].at("recovered:0")==0 && actual[2].at("recovered:0")==0,"two causal low-pass states start with the declared delay");
    const auto expectedFirst=std::pow(50/rate,2)*actual[1].at("mixed:0");require(std::abs(actual[3].at("recovered:0")-expectedFirst)<1e-12,"first nonzero recovered state");
    std::cout<<"SP14 phase="<<phase<<" rate="<<rate<<" max source/state residual="<<worst<<" V max harmonic residual="<<spectrumResidual<<'\n';return actual;
}
}
int main() try {
    using namespace s7tests::education;s7::register_builtin_elements(s7::ElementRegistry::instance());
    const auto baseline=run(0),edited=run(std::numbers::pi/3),quadrature=run(std::numbers::pi/2);run(0,512);
    require(baseline==run(0),"fresh AM repeatability");
    for(std::size_t n=0;n<baseline.size();++n) require(baseline[n].at("transmit:0")==edited[n].at("transmit:0"),"local phase edit cannot alter RF source");
    require(std::abs(am_steady("recovered",4,std::numbers::pi/2))<1e-12 && std::abs(am_steady("recovered",64,std::numbers::pi/2))>.01,"quadrature removes baseband but finite low-pass images remain");
    const auto continuous=1./std::pow(std::complex<double>(1,2*std::numbers::pi*4*.02),2);
    const auto coarse=std::abs(am_filter(4,256)-continuous),fine=std::abs(am_filter(4,512)-continuous);
    require(coarse/fine>1.8 && coarse/fine<2.2,"sampled low-pass response approaches continuous filter at first order");
    std::cout<<"SP14 filter refinement ratio="<<coarse/fine<<" recovered 4 Hz amplitude="<<std::abs(am_steady("recovered",4))<<" V image64="<<std::abs(am_steady("recovered",64))<<" V\n";
    std::cout<<"SP14 sidebands, coherent phase, finite filter and initial transient passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
