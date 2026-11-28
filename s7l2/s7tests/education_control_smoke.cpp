// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_assets.hpp"
#include "education_control_reference.hpp"
#include <s7/schema_library.hpp>
#include <array>
#include <iostream>

namespace {
using namespace s7tests::education;
void require(bool ok,const std::string& why) {if(!ok) throw std::runtime_error(why);}
boost::json::object document(bool windup,bool edited,double rate) {
    auto doc=s7tests::read_json_object(s7tests::education_diagram_path(S7_CONTROL_LABS_DIR, windup ? "ct05_actuator_windup.s7diagram":"ct04_controller_comparison.s7diagram"));
    require(doc.at("dt").to_number<double>()==.01,"saved 100 Hz rate");
    require(doc.at("studio_run").at("runtime_seconds").to_number<double>()==(windup ? 12:16),"saved duration");
    for(auto& e:doc.at("schema").at("elements").as_array()) {
        if(edited && !windup && e.at("id").as_string()=="kd") e.at("params").as_object()["value"]=0;
        if(edited && windup && e.at("id").as_string()=="aw")
            for(auto& inner:e.at("schema").at("elements").as_array())
                if(inner.at("id").as_string()=="i_rate") inner.at("params").as_object()["weights"]=boost::json::array{1,0};
    }
    s7::configure_schema_discretization(doc.at("schema"),1/rate);return doc;
}
ControlTrace run(bool windup,bool edited=false,double rate=100) {
    auto doc=document(windup,edited,rate);auto schema=s7::SchemaLibraryLoader::build_schema(doc.at("schema"));
    schema.set_rate(s7::Rate::from_hz(rate));const auto validation=s7::Validator::validate(schema);
    for(const auto& e:validation.errors) std::cerr<<e<<'\n';
    require(validation.ok(),"control lab validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));runtime.set_model_time_rate(s7::Rate::from_hz(rate));
    s7::ObservationService observe(runtime);
    std::vector<s7::ProbePoint> probes;
    for(const auto& e:doc.at("schema").at("elements").as_array()) {
        const auto id=std::string(e.at("id").as_string());probes.push_back({id,0});
        if(e.at("type").as_string()=="PIController" || e.at("type").as_string()=="PIDController")
            for(std::size_t port=1;port<e.at("schema").at("exposed_outputs").as_array().size();++port) probes.push_back({id,port});
    }
    const auto expected=control_reference(windup,edited,rate);ControlTrace actual;double worst=0;
    for(std::size_t n=0;n<expected.size();++n) {
        runtime.step();const auto snap=observe.snapshot(probes);
        require(snap.step==n+1 && std::abs(*snap.model_time_seconds-(n+1)/rate)<1e-12,"control step/model time");
        require(snap.model_time_ns==std::llround((n+1)*1e9/rate),"rounded control timestamp");
        ControlSample sample;
        for(std::size_t j=0;j<probes.size();++j) {
            const auto key=control_key(probes[j].element_id,probes[j].port);const auto value=snap.samples[j].to_display_double();
            const auto target=expected[n].at(key);const auto error=std::abs(value-target);worst=std::max(worst,error);
            require(std::isfinite(value) && error<1e-10*std::max(1.,std::abs(target)),"control recurrence "+key+" n="+std::to_string(n));
            sample[key]=value;
        }
        actual.push_back(std::move(sample));
    }
    std::cout<<(windup ? "CT05":"CT04")<<" edited="<<edited<<" rate="<<rate<<" max discrete residual="<<worst<<'\n';return actual;
}
double at(const ControlTrace& t,std::size_t n,const std::string& id,std::size_t port=0) {return t.at(n).at(control_key(id,port));}
}
int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for(bool windup:{false,true}) for(double rate:{100.,200.}) {
        const auto base=run(windup,false,rate),edit=run(windup,true,rate);
        require(base==run(windup,false,rate),"fresh-run same-build repeatability");
        const auto onset=std::size_t(rate),release=std::size_t(4*rate);
        for(std::size_t n=0;n<base.size();++n) {
            for(const std::string& mode:windup ? std::vector<std::string>{"aw","free"}:std::vector<std::string>{"pi","pid"})
                require(std::abs(at(base,n,mode))<=1,"bounded applied actuator");
            if(windup) {
                for(std::size_t port=0;port<3;++port)
                    require(std::abs(at(edit,n,"aw",port)-at(edit,n,"free",port))<1e-10,"disabling tracking makes both PI paths identical");
                require(at(base,n,"aw_y")<=1 && at(base,n,"free_y")<=1,"unreachable 1.5 reference cannot raise bounded plant past 1");
            } else {
                require(std::abs(at(base,n,"p_limit"))<=1,"bounded P actuator");
                for(const auto& id:{"y","error"}) require(std::abs(at(edit,n,std::string("pid_")+id)-at(edit,n,std::string("pi_")+id))<1e-10,"zero Kd gives PI exactly");
                const double h=1/rate,a=1-3*h;
                const double before=n<onset ? 0:(1-std::pow(a,double(n-onset)))/3;
                const auto loadOnset=std::size_t(8*rate);
                const double pExpected=n<loadOnset ? before:4./15+( (1-std::pow(a,7*rate))/3-4./15)*std::pow(a,double(n-loadOnset));
                require(std::abs(at(base,n,"p_y")-pExpected)<1e-10,"P closed-form equilibrium/transient");
            }
        }
        if(windup) {
            require(at(base,onset,"aw",1)==2.25 && at(base,onset,"aw")==1,"initial saturation demand/control");
            require(std::abs(at(base,onset+1,"aw",2)+4.5/rate)<1e-12,"back-calculation can initially drive integral negative");
            double awError=0,freeError=0;
            for(std::size_t n=release;n<release+std::size_t(4*rate);++n) {
                awError+=std::abs(at(base,n,"aw_y")-.4)/rate;freeError+=std::abs(at(base,n,"free_y")-.4)/rate;
            }
            require(awError<freeError*.6,"back-calculation reduces fixed recovery IAE by at least 40%");
            require(at(base,release-1,"free",2)>at(base,release-1,"aw",2)+2,"unlimited integral winds up behind actuator");
            require(std::abs(at(base,base.size()-1,"aw_y")-.4)<.005,"anti-windup recovery");
            std::cout<<"CT05 recovery IAE [4,8) aw="<<awError<<" free="<<freeError<<'\n';
        } else {
            require(at(base,onset,"pid",1)==1 && at(base,onset,"pid",4)==0,"no derivative kick on reference step");
            require(std::abs(at(base,base.size()-1,"p_y")-4./15)<1e-9,"P retains 7/30 error under load");
            for(const std::string mode:{"pi","pid"}) require(std::abs(at(base,base.size()-1,mode+"_y")-.5)<.002,"integral load rejection");
        }
    }
    const auto coarse=run(false,false,4);require(std::abs(at(coarse,coarse.size()-1,"pid",3))>100,"coarse h=.25 destabilizes explicit derivative filter");
    auto bad=document(false,false,100);
    for(auto& e:bad.at("schema").at("elements").as_array()) if(e.at("id").as_string()=="p_y") {e.as_object()["type"]="Gain";e.as_object()["params"]=boost::json::object{{"factor",1}};}
    require(!s7::Validator::validate(s7::SchemaLibraryLoader::build_schema(bad.at("schema"))).ok(),"removing plant memory must reject combinational P feedback");
    std::cout<<"CT04/CT05 saved graphs, primary edits and declared limit/invalid cases passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
