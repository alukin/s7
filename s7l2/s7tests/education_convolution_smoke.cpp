// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_convolution_reference.hpp"
#include <s7/convolution.hpp>
#include <s7/schema_library.hpp>
#include <algorithm>
#include <array>
#include <iostream>

namespace {
using namespace s7tests::education;
using C=ConvolutionSample;
using Trace=std::array<std::vector<C>,7>;
void require(bool ok,const std::string& what) {if(!ok) throw std::runtime_error(what);}
void close(std::span<const C> actual,std::span<const C> expected,const std::string& what) {
    require(actual.size()==expected.size(),what+" size");
    for(std::size_t n=0;n<actual.size();++n)
        require(std::isfinite(actual[n].real()) && std::isfinite(actual[n].imag()) &&
                std::abs(actual[n]-expected[n])<1e-10*std::max(1.,std::abs(expected[n])),what+" n="+std::to_string(n));
}
Trace run(bool edited) {
    auto doc=s7tests::read_json_object(S7_SP10_DIAGRAM);
    require(doc.at("dt").to_number<double>()==.125 && doc.at("studio_run").at("runtime_seconds").to_number<double>()==2,"saved profile");
    if(edited) for(auto& e:doc.at("schema").at("elements").as_array())
        if(e.at("id").as_string()=="last_tap") e.at("params").as_object()["value"]=.5;
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(8));
    require(s7::Validator::validate(schema).ok(),"SP10 validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(8));
    s7::ObservationService observe(runtime);
    const std::array<std::string,7> names{"linear_input","cyclic4_input","padded8_input","linear","cyclic4","padded8","difference"};
    std::vector<s7::ProbePoint> probes;for(auto& name:names) probes.push_back({name,0});
    Trace trace;double worst=0;
    for(int n=0;n<16;++n) {
        runtime.step();const auto snap=observe.snapshot(probes);
        require(snap.step==std::size_t(n+1) && *snap.model_time_seconds==(n+1)/8. && snap.model_time_ns==(n+1)*125000000LL,"SP10 observation time");
        for(std::size_t j=0;j<names.size();++j) {
            const auto actual=snap.samples[j].to_display_double(),expected=convolution_signal(names[j],n,edited);
            require(std::isfinite(actual) && std::abs(actual-expected)<1e-12,"SP10 graph "+names[j]+" n="+std::to_string(n));
            worst=std::max(worst,std::abs(actual-expected));trace[j].emplace_back(actual,0);
        }
    }
    std::cout<<"SP10 edited="<<edited<<" max graph residual="<<worst<<" V\n";return trace;
}
template<class Processor> void processor_bridge(const Trace& trace,bool edited,const std::string& name) {
    const std::vector<C> h{1,.5,edited ? .5 : -.25};
    const std::vector<C> word(trace[0].begin()+8,trace[0].begin()+12);
    const auto expected=convolution_sum(word,h);
    Processor full(4,h);close(full.step(word),expected,name+" saved graph word / full tail");
    // Processor implements linear convolution. Fold its actual full output to
    // compare with circular graph; never truncate it and call that circular.
    const auto output=full.step(word);std::vector<C> folded(4);
    for(std::size_t i=0;i<output.size();++i) folded[i%4]+=output[i];
    close(folded,std::span<const C>(trace[4]).subspan(8,4),name+" fold matches cyclic graph");
    for(std::size_t channel=0;channel<3;++channel) for(const std::size_t n:{4U,8U}) {
        Processor stream(n,h,s7::ConvolutionMode::ContinuousStream);
        std::vector<C> all;
        for(std::size_t start=0;start<16;start+=n) {
            const auto actual=stream.step(std::span<const C>(trace[channel]).subspan(start,n));
            all.insert(all.end(),actual.begin(),actual.end());
        }
        close(all,trace[channel+3],name+" continuous matches graph row "+std::to_string(channel)+" with boundary at n=8");
    }
    // Drain the tail of the unpadded word across a frame boundary, then reset.
    Processor stream(4,h,s7::ConvolutionMode::ContinuousStream);
    std::vector<C> drained;for(const auto& frame:{word,std::vector<C>(4)}) {
        const auto outputFrame=stream.step(frame);drained.insert(drained.end(),outputFrame.begin(),outputFrame.end());
    }
    auto padded=expected;padded.resize(8);close(drained,padded,name+" stream tail flush");
    stream.reset();close(stream.step(std::vector<C>(4)),std::vector<C>(4),name+" zero after reset");
    close(stream.step(word),std::span<const C>(expected).first(4),name+" kernel retained by reset");
}
void lesson(bool edited) {
    const auto trace=run(edited);require(trace==run(edited),"fresh graph repeat equality");
    const std::vector<C> word(trace[0].begin()+8,trace[0].begin()+12),h{1,.5,edited ? .5 : -.25};
    auto linear=convolution_sum(word,h);linear.resize(8);
    const auto cyclic=convolution_sum(word,h,4);
    close(std::span<const C>(trace[3]).subspan(8),linear,"isolated graph linear result");
    close(std::span<const C>(trace[5]).subspan(8),linear,"padded graph matches linear");
    close(std::span<const C>(trace[4]).subspan(8,4),cyclic,"cyclic graph modulo sum");
    close(convolution_dft_product(word,h,4),cyclic,"independent DFT product / circular");
    close(convolution_dft_product(word,h,8),linear,"independent padded DFT product / linear");
    // Minimum sufficient padding is 6, not necessarily a power of two.
    close(convolution_dft_product(word,h,6),std::span<const C>(linear).first(6),"minimum sufficient N=6");
    require(std::abs(convolution_dft_product(word,h,5)[0]-linear[0])>.1,"N=5 still wraps nonzero tail");
    double aliasError=0;for(std::size_t i=0;i<4;++i) aliasError=std::max(aliasError,std::abs(cyclic[i]-linear[i]));
    require(aliasError>.4,"insufficient padding visibly corrupts the initial block");
    double startupError=0;for(std::size_t i=0;i<4;++i) startupError=std::max(startupError,std::abs(trace[4][i]-cyclic[i]));
    require(startupError>.4,"zero-state startup is not yet circular steady state");
    for(int n=8;n<16;++n) require(trace[6][std::size_t(n)]==C{},"padded difference zero in selected block");
    processor_bridge<s7::FirConvolution>(trace,edited,"production FIR");
    processor_bridge<s7::FftConvolution>(trace,edited,"production FFT");
    std::cout<<"SP10 edited="<<edited<<" linear:";for(auto v:linear) std::cout<<' '<<v.real();
    std::cout<<"; circular4:";for(auto v:cyclic) std::cout<<' '<<v.real();std::cout<<'\n';
}
}
int main() {try {s7::register_builtin_elements(s7::ElementRegistry::instance());lesson(false);lesson(true);}
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
