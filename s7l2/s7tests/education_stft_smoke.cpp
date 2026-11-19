// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_stft_reference.hpp"
#include <s7/schema_library.hpp>
#include <s7spectral/analysis.hpp>
#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace s7tests::education;
void require(bool ok,const std::string& text) {if(!ok) throw std::runtime_error(text);}
using Trace=std::array<std::vector<double>,4>;
Trace run(bool edited) {
    auto doc=s7tests::read_json_object(S7_SP11_DIAGRAM);
    require(doc.at("dt").to_number<double>()==1./128 && doc.at("studio_run").at("runtime_seconds").to_number<double>()==4,"saved SP11 profile");
    if(edited) for(auto& e:doc.at("schema").at("elements").as_array())
        if(e.at("id").as_string()=="stop") e.at("params").as_object()["start_time"]=1.5;
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(128));
    require(s7::Validator::validate(schema).ok(),"SP11 production validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(128));
    s7::ObservationService observe(runtime);const std::array<std::string,4> names{"burst","chirp","mixture","gate"};
    std::vector<s7::ProbePoint> probes;for(auto& id:names) probes.push_back({id,0});
    Trace result;double worst=0;
    for(int n=0;n<512;++n) {
        runtime.step();const auto snap=observe.snapshot(probes);
        require(snap.step==std::size_t(n+1) && *snap.model_time_seconds==(n+1)/128.,"SP11 sample/model time");
        require(snap.model_time_ns==std::llround((n+1)*1e9/128),"SP11 rounded nanoseconds");
        for(std::size_t j=0;j<names.size();++j) {
            const auto actual=snap.samples[j].to_display_double(),expected=stft_signal(names[j],n,edited);
            require(std::isfinite(actual) && std::abs(actual-expected)<1e-10,"SP11 source "+names[j]+" n="+std::to_string(n));
            worst=std::max(worst,std::abs(actual-expected));result[j].push_back(actual);
        }
    }
    std::cout<<"SP11 edited="<<edited<<" max source residual="<<worst<<" V\n";return result;
}
void verify(std::span<const double> input,std::size_t n,std::size_t hop,double origin=1./128) {
    const auto actual=s7spectral::spectrogram(input,128,n,hop);
    const auto expected=stft_reference(input,n,hop,origin);
    const auto columns=expected.times.size(),bins=expected.frequencies.size();
    require(actual.times.size()==columns && actual.frequencies==expected.frequencies,"STFT dimensions/frequencies");
    double worst=0;
    for(std::size_t t=0;t<columns;++t) {
        require(std::abs(actual.times[t]+origin-expected.times[t])<1e-12,"window center uses both endpoint samples");
        double power=0,psd=0;
        for(std::size_t k=0;k<bins;++k) {
            const auto i=t*bins+k;const auto value=actual.coefficients[i];
            require(actual.valid[i],"every returned STFT frame has full support");
            const auto error=std::abs(value-expected.coefficients[k*columns+t]);worst=std::max(worst,error);
            require(s7spectral::finite(value) && error<1e-10,"independent complex STFT coefficient");
            const double p=s7spectral::spectrogramPower(value,k,n,128,false),d=s7spectral::spectrogramPower(value,k,n,128,true);
            require(std::abs(p-stft_quantity(expected.coefficients[k*columns+t],k,n,3))<1e-10 &&
                    std::abs(d-stft_quantity(expected.coefficients[k*columns+t],k,n,4))<1e-10,"STFT power/PSD normalization");
            power+=p;psd+=d*128./n;
        }
        require(std::abs(power-expected.power[t])<1e-10 && std::abs(psd-expected.power[t])<1e-10,"STFT weighted Parseval");
    }
    std::cout<<"SP11 samples="<<input.size()<<" N="<<n<<" hop="<<hop<<" frames="<<columns<<" max coefficient residual="<<worst<<'\n';
}
template<class F> void rejects(F f) {bool caught=false;try {f();} catch(const std::invalid_argument&) {caught=true;}require(caught,"invalid STFT rejected");}
}
int main() {try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    const auto baseline=run(false),edited=run(true);require(baseline==run(false),"fresh SP11 repeatability");
    for(const auto* variant:{&baseline,&edited}) for(int channel=0;channel<3;++channel)
        for(const auto [n,hop]:std::array<std::pair<std::size_t,std::size_t>,4>{{{64,32},{128,64},{64,16},{64,64}}})
            verify((*variant)[std::size_t(channel)],n,hop);
    verify(std::span(baseline[2]).first(501),64,32); // incomplete last window is discarded, not zero padded
    verify(std::span(baseline[2]).first(64),64,32); // one complete window
    verify(std::span(baseline[2]).subspan(32),64,32,33./128); // shifted origin
    const auto burst=stft_reference(baseline[0],64,32),shortBurst=stft_reference(edited[0],64,32);
    require(std::abs(burst.coefficients[16*15+4]-std::complex<double>(1,0))<1e-10,"fully supported coherent burst has peak amplitude 1 V");
    require(std::abs(shortBurst.coefficients[16*15+6])<1e-10 && std::abs(burst.coefficients[16*15+6])>.99,"duration edit removes later full window");
    const auto nearEdge=std::abs(burst.coefficients[16*15+3]);
    require(nearEdge>.4 && nearEdge<.6,"window overlapping onset spreads burst across its center time");
    const auto dense=stft_reference(baseline[2],64,16),coarse=stft_reference(baseline[2],64,32);
    for(std::size_t k=0;k<33;++k) for(std::size_t t=0;t<15;++t)
        require(std::abs(dense.coefficients[k*29+2*t]-coarse.coefficients[k*15+t])<1e-10,"smaller hop samples the same windows more densely");
    const auto chirp=s7spectral::spectrogram(baseline[1],128,64,32);
    for(std::size_t t=0;t<15;++t) {
        std::size_t peak=0;for(std::size_t k=1;k<33;++k)
            if(std::abs(chirp.coefficients[t*33+k])>std::abs(chirp.coefficients[t*33+peak])) peak=k;
        const double predicted=8+.0625*(double(t*32)+31.5);
        require(std::abs(chirp.frequencies[peak]-predicted)<1.1,"chirp ridge tracks the discrete frequency law within half a bin");
    }
    rejects([&]{(void)s7spectral::spectrogram(baseline[0],128,64,0);});
    rejects([&]{(void)s7spectral::spectrogram(baseline[0],128,64,65);});
    rejects([&]{(void)s7spectral::spectrogram(std::span(baseline[0]).first(63),128,64,32);});
    std::cout<<"SP11 full-support STFT, hop/window contrasts and declared failure cases passed\n";
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
