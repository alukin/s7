// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_assets.hpp"
#include "education_signals_reference.hpp"
#include <s7/schema_library.hpp>
#include <iostream>
#include <numeric>

namespace {
using namespace s7tests::education;
using Document=boost::json::object;
using Trace=std::vector<std::vector<double>>;
void require(bool ok,const std::string& what) {if (!ok) throw std::runtime_error(what);}
Document load(const std::string& file) {return s7tests::read_json_object(s7tests::education_diagram_path(S7_SIGNAL_LABS_DIR, file+".s7diagram"));}
void edit(Document& doc,const std::string& id,const std::string& key,double value) {
    for (auto& e:doc.at("schema").at("elements").as_array())
        if (e.at("id").as_string()==id) {e.at("params").as_object()[key]=value;return;}
    throw std::runtime_error("Missing element "+id);
}
Trace run(const std::string& lab,const Document& doc,const std::vector<std::string>& outputs,double rate,bool edited=false) {
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));
    schema.set_rate(s7::Rate::from_hz(rate));
    require(s7::Validator::validate(schema).ok(),"production validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(rate));
    s7::ObservationService observe(runtime);
    std::vector<s7::ProbePoint> probes;for (const auto& id:outputs) probes.push_back({id,0});
    Trace trace(outputs.size()); double worst=0;
    for (int n=0;n<int(2*rate);++n) {
        runtime.step();const auto snap=observe.snapshot(probes);
        require(snap.step==std::size_t(n+1),"step");
        require(std::abs(*snap.model_time_seconds-(n+1)/rate)<1e-12,"timestamp");
        require(snap.model_time_ns==std::llround((n+1)*1e9/rate),"nanosecond rounding");
        for (std::size_t j=0;j<outputs.size();++j) {
            const auto value=snap.samples[j].to_display_double();
            const auto expected=signal_reference(lab,outputs[j],n,rate,edited);
            worst=std::max(worst,std::abs(value-expected));
            require(std::isfinite(value) && std::abs(value-expected)<1e-10,lab+" "+outputs[j]+" sample "+std::to_string(n));
            trace[j].push_back(value);
        }
    }
    std::cout<<lab<<" rate="<<rate<<" edited="<<edited<<" max sample error="<<worst<<'\n';return trace;
}
void synthesis() {
    auto doc=load("sp01_fourier_synthesis");
    const std::vector<std::string> outputs{"weight1","three","five","target"};
    const auto base=run("SP01",doc,outputs,128);
    require(base==run("SP01",doc,outputs,128),"SP01 repeatability");
    for (int j=0;j<3;++j) {
        const int count=j==0 ? 1 : j==1 ? 3 : 5;
        double expectedPower=0;for (int m=1;m<2*count;m+=2) expectedPower+=8/(std::numbers::pi*std::numbers::pi*m*m);
        const auto spectrum=signal_dft(base[j]);double spectralPower=0,timePower=0;
        for (auto z:spectrum) spectralPower+=std::norm(z);
        for (auto x:base[j]) timePower+=x*x/base[j].size();
        require(std::abs(timePower-expectedPower)<1e-11 && std::abs(spectralPower-expectedPower)<1e-11,"SP01 Parseval and independent coefficient power");
        for (std::size_t k=0;k<spectrum.size();++k) {
            std::complex<double> expected{};
            for (int m=1;m<2*count;m+=2) {
                if (k==std::size_t(4*m)) expected={0,-2/(std::numbers::pi*m)};
                if (k==spectrum.size()-4*m) expected={0,2/(std::numbers::pi*m)};
            }
            require(std::abs(spectrum[k]-expected)<1e-10,"SP01 signed Fourier coefficients, DC, empty bins");
        }
        std::cout<<"SP01 "<<count<<" terms mean-square="<<timePower<<'\n';
    }
    const auto peak=*std::max_element(base[2].begin(),base[2].end());
    require(peak>1.17 && peak<1.20 && std::abs(base[2][0])<1e-12 && base[3][0]==1,"SP01 ringing and jump midpoint");
    edit(doc,"weight9","factor",0);const auto missing=run("SP01",doc,outputs,128,true);
    require(std::abs(signal_dft(missing[2])[36])<1e-10,"SP01 missing ninth harmonic");
    doc=load("sp01_fourier_synthesis");const auto coarse=run("SP01",doc,outputs,32);
    const auto aliased=signal_dft(coarse[2]);
    require(std::abs(2*std::abs(aliased[28])-8/(63*std::numbers::pi))<1e-10,"SP01 ninth folds onto seventh at 32 Hz");
    std::cout<<"SP01 five-term sampled peak="<<peak<<'\n';
}
void aliases() {
    auto doc=load("sp03_aliasing");const std::vector<std::string> outputs{"low","shifted","reflected","residual"};
    const auto base=run("SP03",doc,outputs,64);
    require(base==run("SP03",doc,outputs,64),"SP03 repeatability");
    for (std::size_t n=0;n<base[0].size();++n)
        require(std::abs(base[0][n]-base[1][n])<1e-10 && std::abs(base[0][n]-base[2][n])<1e-10 && std::abs(base[3][n])<1e-10,"SP03 analytic alias equality");
    edit(doc,"shifted","frequency_hz",70);const auto changed=run("SP03",doc,outputs,64,true);
    const auto spectrum=signal_dft(changed[1]);
    require(std::abs(2*std::abs(spectrum[12])-1)<1e-10 && std::abs(spectrum[20])<1e-10,"SP03 edited tone aliases to 6 Hz, not 10");
    require(*std::max_element(changed[3].begin(),changed[3].end())>1,"SP03 wrong frequency breaks equality");
    doc=load("sp03_aliasing");const auto fast=run("SP03",doc,outputs,256);
    for (int j=0;j<3;++j) {
        const int f=j==0 ? 10 : j==1 ? 74 : 54;
        const auto bins=signal_dft(fast[j]);
        require(std::abs(2*std::abs(bins[2*f])-1)<1e-10,"SP03 faster rate resolves physical frequencies");
    }
    require(std::abs(fast[0][1]-fast[1][1])>.1,"SP03 continuous signals differ between coarse sample instants");
}
}
int main() {try {s7::register_builtin_elements(s7::ElementRegistry::instance());synthesis();aliases();} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
