// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_spectral_reference.hpp"
#include <s7/schema_library.hpp>
#include <iostream>

namespace {
using namespace s7tests::education;
using Document=boost::json::object;
using Trace=std::vector<std::vector<double>>;
void require(bool ok,const std::string& text) {if (!ok) throw std::runtime_error(text);}
Document load(const std::string& lab) {return s7tests::read_json_object(std::filesystem::path(S7_SPECTRAL_LABS_DIR)/(lab=="SP08" ? "sp08_dft_leakage.s7diagram" : "sp09_window_normalization.s7diagram"));}
Trace run(const std::string& lab,bool edited=false) {
    auto doc=load(lab);
    if (edited) for (auto& e:doc.at("schema").at("elements").as_array())
        if (e.at("id").as_string()==(lab=="SP08" ? "test" : "weak"))
            e.at("params").as_object()[lab=="SP08" ? "frequency_hz" : "amplitude"]=lab=="SP08" ? 18. : .1;
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(128));
    require(s7::Validator::validate(schema).ok(),lab+" production validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(128));
    s7::ObservationService observe(runtime);
    const std::vector<std::string> channels=lab=="SP08" ? std::vector<std::string>{"coherent","test","offset","dc"} : std::vector<std::string>{"calibration","strong","weak","mixture"};
    std::vector<s7::ProbePoint> probes;for (const auto& id:channels) probes.push_back({id,0});
    Trace trace(4);double worst=0;
    for (int n=0;n<256;++n) {
        runtime.step();const auto snap=observe.snapshot(probes);
        require(snap.step==std::size_t(n+1) && std::abs(*snap.model_time_seconds-(n+1)/128.)<1e-12,"sample/time convention");
        require(snap.model_time_ns==std::llround((n+1)*1e9/128),"nanosecond rounding");
        for (std::size_t j=0;j<4;++j) {
            const auto value=snap.samples[j].to_display_double();
            const auto expected=spectral_signal(lab,channels[j],n,edited);
            worst=std::max(worst,std::abs(value-expected));require(std::isfinite(value) && std::abs(value-expected)<1e-10,lab+" "+channels[j]+" sample reference");
            trace[j].push_back(value);
        }
    }
    std::cout<<lab<<" edited="<<edited<<" max sample residual="<<worst<<'\n';return trace;
}
void leakage() {
    const auto trace=run("SP08"),edited=run("SP08",true);require(trace==run("SP08"),"SP08 repeatability");
    for (const auto& [first,n]:std::vector<std::pair<std::size_t,std::size_t>>{{0,128},{0,256},{128,128},{127,129}}) {
        for (int channel=0;channel<3;++channel) {
            const auto input=std::span(trace[channel]).subspan(first,n);const auto actual=signal_dft(input);
            for (std::size_t k=0;k<n;++k) {
                auto expected=cosine_coefficient(channel==0 ? 16 : 16.5,k,n,first);
                if (channel==2 && k==0) expected+=.25;
                require(std::abs(actual[k]-expected)<1e-10,"SP08 geometric-sum complex DFT, including negative frequencies");
            }
        }
    }
    const auto shortResult=lesson_spectrum(std::span(trace[1]).first(128),0,128);
    require(shortResult.quantity[0][16]>.62 && shortResult.quantity[0][16]<.64 && shortResult.quantity[0][17]>.64 && shortResult.quantity[0][17]<.65,"SP08 off-bin scalloping");
    const auto longer=lesson_spectrum(trace[1],0,128);
    require(std::abs(longer.quantity[0][33]-1)<1e-10,"SP08 same signal coherent in longer actual record");
    const auto fixed=lesson_spectrum(std::span(edited[1]).first(128),0,128);
    require(std::abs(fixed.quantity[0][18]-1)<1e-10,"SP08 frequency edit aligns with bin");
    const auto offset=lesson_spectrum(std::span(trace[2]).first(128),0,128);
    require(std::abs(offset.mean-(.25+1./128))<1e-11,"SP08 finite record mean includes the off-bin tone");
    const auto removed=lesson_spectrum(std::span(trace[2]).first(128),0,128,true);
    require(removed.quantity[0][0]<1e-10,"SP08 explicit sample-mean removal");
    std::cout<<"SP08 nearest-bin amplitudes "<<shortResult.quantity[0][16]<<", "<<shortResult.quantity[0][17]<<"; one-second offset mean="<<offset.mean<<'\n';
}
void windows() {
    const auto trace=run("SP09"),edited=run("SP09",true);require(trace==run("SP09"),"SP09 repeatability");
    const double gains[]{1,.5,.54},energies[]{1,.375,.3974};
    for (int window=0;window<3;++window) {
        const auto calibration=lesson_spectrum(std::span(trace[0]).first(128),window,128);
        require(std::abs(calibration.cg-gains[window])<1e-12 && std::abs(calibration.energy-energies[window])<1e-12,"SP09 independent periodic window moments");
        require(std::abs(calibration.enbw-energies[window]/(gains[window]*gains[window]))<1e-12,"SP09 equivalent noise bandwidth");
        require(std::abs(calibration.quantity[0][20]-2)<1e-10,"SP09 corrected coherent amplitude");
        require(std::abs(std::accumulate(calibration.quantity[1].begin(),calibration.quantity[1].end(),0.)-2)<1e-10,"SP09 coherent total power independent of window");
        for (const auto* variant:{&trace,&edited}) for (const auto& channel:*variant) for (bool remove:{false,true}) {
            const auto result=lesson_spectrum(std::span(channel).first(128),window,128,remove);
            const auto p=std::accumulate(result.quantity[1].begin(),result.quantity[1].end(),0.);
            require(std::abs(p-result.weightedPower)<1e-10,"SP09 window-weighted Parseval");
        }
        const auto mixture=lesson_spectrum(std::span(trace[3]).first(128),window,128);
        std::cout<<"SP09 window="<<window<<" CG="<<calibration.cg<<" ENBW="<<calibration.enbw<<" Hz, mixture 24 Hz bin="<<mixture.quantity[0][24]<<" V\n";
    }
    const auto hann=lesson_spectrum(std::span(trace[0]).first(128),1,128);
    require(std::abs(2*std::abs(hann.coefficients[20])-1)<1e-10,"SP09 missing coherent-gain correction halves the displayed amplitude");
    const auto rect=lesson_spectrum(std::span(trace[3]).first(128),0,128),taper=lesson_spectrum(std::span(trace[3]).first(128),1,128);
    require(rect.quantity[0][24]>.2 && taper.quantity[0][24]<.06,"SP09 leakage can obscure a real weak tone");
}
}
int main() {try {s7::register_builtin_elements(s7::ElementRegistry::instance());leakage();windows();} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
