// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_assets.hpp"
#include "education_lti_reference.hpp"
#include <s7/schema_library.hpp>
#include <iostream>
#include <vector>

namespace {
using namespace s7tests::education;
using Document=boost::json::object;
using Trace=std::vector<std::vector<double>>;
void require(bool ok,const std::string& message) {if (!ok) throw std::runtime_error(message);}
Document load(const std::string& file) {return s7tests::read_json_object(s7tests::education_diagram_path(S7_LTI_LABS_DIR, file+".s7diagram"));}
void edit(Document& doc,const std::string& id,const std::string& param,double value) {
    for (auto& e:doc.at("schema").at("elements").as_array()) if (e.at("id").as_string()==id) {e.at("params").as_object()[param]=value;return;}
    throw std::runtime_error("Missing element "+id);
}
Trace run(const std::string& lab,Document doc,double rate,bool edited=false,bool nonzero=false) {
    const std::vector<std::string> outputs=lab=="SP05" ? std::vector<std::string>{"input","output","first_response","second_response","sum","residual"} : std::vector<std::string>{"input","low","resonant","velocity"};
    s7::configure_schema_discretization(doc.at("schema"),1/rate);
    auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));schema.set_rate(s7::Rate::from_hz(rate));
    require(s7::Validator::validate(schema).ok(),lab+" production validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(rate));
    s7::ObservationService observe(runtime);std::vector<s7::ProbePoint> probes;for (const auto& id:outputs) probes.push_back({id,0});
    Trace trace(outputs.size());double worst=0;
    const int count=int((lab=="SP05" ? 2 : 64)*rate);
    for (int n=0;n<count;++n) {
        runtime.step();const auto snap=observe.snapshot(probes);
        require(snap.step==std::size_t(n+1),"step number");
        require(std::abs(*snap.model_time_seconds-(n+1)/rate)<1e-12,"model timestamp");
        require(snap.model_time_ns==std::llround((n+1)*1e9/rate),"nanosecond rounding");
        for (std::size_t j=0;j<outputs.size();++j) {
            const auto actual=snap.samples[j].to_display_double();const auto expected=lti_reference(lab,outputs[j],n,rate,edited,nonzero);
            worst=std::max(worst,std::abs(actual-expected));
            require(std::isfinite(actual) && std::abs(actual-expected)<1e-10*std::max(1.,std::abs(expected)),lab+" "+outputs[j]+" n="+std::to_string(n));
            trace[j].push_back(actual);
        }
    }
    std::cout<<lab<<" rate="<<rate<<" edited="<<edited<<" nonzero="<<nonzero<<" max absolute residual="<<worst<<'\n';return trace;
}
void convolution() {
    auto doc=load("sp05_convolution");const auto base=run("SP05",doc,20);
    require(base==run("SP05",doc,20),"SP05 repeated-run equality");
    const double h[]{1,.5,-.25};
    for (std::size_t n=0;n<base[0].size();++n) {
        double sum=0;for (std::size_t j=0;j<3 && j<=n;++j) sum+=h[j]*base[0][n-j];
        require(base[1][n]==sum && base[1][n]==base[4][n] && base[5][n]==0,"SP05 direct finite convolution and superposition");
    }
    require(base[1][6]==-.75,"SP05 overlapping responses");
    edit(doc,"weight","factor",.75);const auto changed=run("SP05",doc,20,true);
    require(changed[1][6]==.5 && changed[1][8]==-.1875,"SP05 edited overlap and tail");
    doc=load("sp05_convolution");edit(doc,"output_d2","initial_value",1);
    const auto bad=run("SP05",doc,20,false,true);
    require(bad[1][0]==-.25 && bad[5][0]==-.25,"SP05 zero-state assumption fails with stored history");
}
void transfer_check(const Trace& trace,double rate,bool edited) {
    // Direct complex sums on executed samples, independent of the viewer FFT.
    for (const auto& [channel,column]:std::vector<std::pair<std::string,int>>{{"low",1},{"resonant",2}}) {
        for (const double f:{0.,.125,.5,.625,1.,2.,8.,rate/2}) {
            std::complex<long double> x{},y{};
            for (std::size_t n=0;n<trace[0].size();++n) {
                const long double angle=-2*std::numbers::pi_v<long double>*f*n/rate;
                const std::complex<long double> basis(std::cos(angle),std::sin(angle));
                x+=static_cast<long double>(trace[0][n])*basis;y+=static_cast<long double>(trace[column][n])*basis;
            }
            require(std::abs(std::complex<double>(y/x)-lti_transfer(channel,f,rate,edited))<1e-9,"SP07 independent complex Y/X");
        }
    }
}
void filters() {
    auto doc=load("sp07_filter_transfer");const auto base=run("SP07",doc,64);transfer_check(base,64,false);
    require(std::abs(lti_phase("low",std::nextafter(32.,33.),64)+180)<1e-10 &&
            std::abs(lti_phase("resonant",32,64)+360)<1e-10,"SP07 independent phase boundary at Nyquist");
    require(base==run("SP07",doc,64),"SP07 repeated-run equality");
    require(base[0][8]==1 && base[1][9]==1./32 && base[2][9]==0 && base[2][10]==1./256,"SP07 causal startup includes one/two state stages");
    double shortDc=0;for (std::size_t n=0;n<128;++n) shortDc+=base[2][n];
    require(std::abs(shortDc-1)>.05,"SP07 a two-second capture misses the tail and biases DC");
    std::cout<<"SP07 two-second finite-record DC estimate "<<shortDc<<'\n';
    edit(doc,"damping","factor",3.2);const auto changed=run("SP07",doc,64,true);transfer_check(changed,64,true);
    require(std::abs(lti_transfer("resonant",.625,64))>2.9 && std::abs(lti_transfer("resonant",.625,64,true))<1.5,"SP07 damping lowers resonance");
    doc=load("sp07_filter_transfer");const auto fine=run("SP07",doc,128);transfer_check(fine,128,false);
    const std::complex<double> s(0,std::numbers::pi);
    for (const auto& channel:{std::string("low"),std::string("resonant")}) {
        const auto exact=channel=="low" ? 2./(s+2.) : 16./(s*s+1.6*s+16.);
        const auto coarseError=std::abs(lti_transfer(channel,.5,64)-exact),fineError=std::abs(lti_transfer(channel,.5,128)-exact);
        require(coarseError/fineError>1.8 && coarseError/fineError<2.2,"SP07 continuous-limit frequency refinement");
        std::cout<<"SP07 "<<channel<<" continuous transfer errors "<<coarseError<<", "<<fineError<<'\n';
    }
    const auto unstable=run("SP07",doc,8);
    require(std::abs(unstable[2].back())>100,"SP07 coarse Euler invents growth in the stable physical filter");
}
}
int main() {try {s7::register_builtin_elements(s7::ElementRegistry::instance());convolution();filters();} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
