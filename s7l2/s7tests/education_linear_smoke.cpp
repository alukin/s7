// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "diagram_harness.hpp"
#include "education_assets.hpp"
#include "education_linear_reference.hpp"
#include <s7/schema_library.hpp>
#include <iostream>
#include <functional>
#include <numbers>

namespace {
using namespace s7tests::education;
using Document = boost::json::object;
using Trace = std::vector<std::vector<double>>;
void require(bool ok, const std::string& what) {if (!ok) throw std::runtime_error(what);}
Document load(const LinearLab& lab) {
    return s7tests::read_json_object(s7tests::education_diagram_path(S7_LINEAR_LABS_DIR, lab.file+".s7diagram"));
}
boost::json::object& element(Document& doc, const std::string& id) {
    for (auto& e : doc.at("schema").at("elements").as_array()) if (e.at("id").as_string() == id) return e.as_object();
    throw std::runtime_error("Missing element "+id);
}
void rate(Document& doc, double h) {
    doc["dt"] = h;
    s7::configure_schema_discretization(doc.at("schema"), h);
}
void poles(Document& doc, double h) {
    rate(doc, h);
    element(doc,"forward_pole")["params"].as_object()["factor"] = 1-h;
    element(doc,"backward_pole")["params"].as_object()["factor"] = 1/(1+h);
    element(doc,"trapezoid_pole")["params"].as_object()["factor"] = (1-h/2)/(1+h/2);
    if (doc.at("id").as_string() == "fn06_pole_maps")
        element(doc,"exact_pole")["params"].as_object()["factor"] = std::exp(-h);
}
Trace run(const LinearLab& lab, const Document& doc, bool edited = false, double damping = 1, double past = 0) {
    const double h = doc.at("dt").to_number<double>();
    auto schema = s7::SchemaLibraryLoader::build_schema(doc.at("schema"));
    schema.set_rate(s7::Rate::from_seconds(h));
    require(s7::Validator::validate(schema).ok(), lab.id+" production validation");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_rate(s7::Rate::from_seconds(h));
    s7::ObservationService observe(runtime);
    std::vector<s7::ProbePoint> probes;
    for (const auto& name : lab.outputs) probes.push_back({name, 0});
    const auto count = static_cast<int>(std::floor(lab.seconds/h+1e-10));
    Trace trace;
    double worst = 0;
    for (int n = 0; n < count; ++n) {
        runtime.step(); const auto snapshot = observe.snapshot(probes);
        require(snapshot.step == std::size_t(n+1), "step count");
        require(std::abs(*snapshot.model_time_seconds-(n+1)*h) < 1e-12, "sample timestamp");
        require(snapshot.model_time_ns == std::llround((n+1)*h*1e9), "absolute timestamp rounding");
        std::vector<double> row;
        for (std::size_t i=0; i<probes.size(); ++i) {
            const double actual = snapshot.samples[i].to_display_double();
            const double reference = discrete(lab.id, lab.outputs[i], n, h, edited, damping, past);
            require(std::isfinite(actual), "finite output");
            worst = std::max(worst, std::abs(actual-reference));
            require(std::abs(actual-reference) < 1e-10*std::max(1.,std::abs(reference)),
                    lab.id+" "+lab.outputs[i]+" n="+std::to_string(n)+" discrete reference");
            row.push_back(actual);
        }
        trace.push_back(std::move(row));
    }
    std::cout << lab.id << " h=" << h << " edited=" << edited << " samples=" << count << " discrete max=" << worst << '\n';
    return trace;
}
double approximation(const Trace& trace, double h, std::size_t column, const std::function<double(double)>& exact) {
    double worst=0;
    for (std::size_t n=0;n<trace.size();++n) worst=std::max(worst,std::abs(trace[n][column]-exact(n*h)));
    return worst;
}
void checks(const LinearLab& lab) {
    auto doc=load(lab);
    const auto baseline=run(lab,doc);
    require(baseline==run(lab,doc),lab.id+" fresh-run repeatability");
    if (lab.id=="CT03") rate(doc,2.5);
    else element(doc,lab.edit_id)["params"].as_object()[lab.parameter]=lab.edited_value;
    const auto changed=run(lab,doc,true);
    if (lab.id=="FN04") {
        for (int variant=0; variant<2; ++variant) {
            const auto& trace=variant ? changed : baseline;
            for (double w : {0.,std::numbers::pi/3,std::numbers::pi/2,std::numbers::pi}) {
                std::complex<double> x{},y{};
                for (std::size_t n=0;n<trace.size();++n) {
                    auto basis=std::polar(1.,-w*n); x+=trace[n][0]*basis; y+=trace[n][1]*basis;
                }
                const auto exact=.5/(1.-(variant ? .8 : .5)*std::polar(1.,-w));
                require(std::abs(y/x-exact)<1e-11,"FN04 independent complex H(z), including phase");
            }
        }
        require(changed[5][1]>.39 && baseline[5][1]==.25,"FN04 altered feedback changes transfer");
        doc=load(lab);element(doc,"memory")["params"].as_object()["initial_value"]=2;
        const auto nonzero=run(lab,doc,false,1,2);
        double dcInput=0,dcOutput=0;
        for (const auto& row : nonzero) {dcInput+=row[0];dcOutput+=row[1];}
        require(nonzero[0][1]==1 && std::abs(dcOutput/dcInput-3)<1e-11,
                "FN04 initial state adds natural response; Y/X no longer measures the zero-state transfer");
    } else if (lab.id=="FN05") {
        doc=load(lab); poles(doc,.25); const auto fine=run(lab,doc);
        const double limits[]{.12,.08,.009};
        for (std::size_t j=0;j<3;++j) {
            auto exact=[](double t){return std::exp(-t);};
            double a=approximation(baseline,.5,j,exact),b=approximation(fine,.25,j,exact);
            require(a<limits[j] && a/b>(j==2 ? 3.8 : 1.7) && a/b<(j==2 ? 4.5 : 2.5),"FN05 refinement envelope");
            std::cout << "FN05 method " << j << " physical errors " << a << ", " << b << '\n';
        }
        require(std::abs(changed[1][0]-.5)>.2,"FN05 wrong coefficient is not the declared method");
    } else if (lab.id=="FN06") {
        require(baseline.back()[0]<-500 && baseline[1][2]<0 && baseline[1][3]>0,
                "FN06 growing Euler and stable alternating trapezoid are distinct from exact decay");
        for (double h : {1.9,2.,2.1}) {doc=load(lab);poles(doc,h);run(lab,doc);}
        require(std::abs(1-1.9)<1 && std::abs(1-2.)==1 && std::abs(1-2.1)>1,"FN06 unit-circle boundary");
    } else if (lab.id=="CT01") {
        const auto exact=[](double t){return 1-std::exp(-t);};
        double a=approximation(baseline,.1,1,exact);
        doc=load(lab);rate(doc,.05);const auto fine=run(lab,doc);
        double b=approximation(fine,.05,1,exact);
        require(a<.02 && a/b>1.8 && a/b<2.2,"CT01 independent physical/reference refinement");
        doc=load(lab);rate(doc,2.5);auto large=run(lab,doc);
        require(large[1][1]>2,"CT01 oversized timestep violates passive charging prediction");
        std::cout << "CT01 physical errors " << a << ", " << b << '\n';
    } else if (lab.id=="CT02") {
        doc=load(lab);rate(doc,.005);const auto fine=run(lab,doc);
        for (std::size_t j=0;j<2;++j) {
            auto exact=[&](double t){return continuous(lab.outputs[j],t);};
            double a=approximation(baseline,.01,j,exact),b=approximation(fine,.005,j,exact);
            require(a<.04 && a/b>1.8 && a/b<2.2,"CT02 damped physical modes/refinement");
            std::cout << "CT02 " << lab.outputs[j] << " physical errors " << a << ", " << b << '\n';
        }
        doc=load(lab);element(doc,"damping")["params"].as_object()["factor"]=-1;
        const auto active=run(lab,doc,false,-1);
        require(std::abs(active.back()[0])>10,"CT02 negative damping is an active growing model");
    } else {
        require(changed.size()==4 && changed[1][0]==-1.5 && changed[3][0]==-3.375,"CT03 coarse Euler sequence");
        const auto exact=[](double t){return std::exp(-t);};
        require(approximation(baseline,.5,0,exact)<.12,"CT03 baseline approximation envelope");
        doc=load(lab);rate(doc,.25);const auto fine=run(lab,doc);
        require(approximation(fine,.25,0,exact)<.06,"CT03 refinement improves decay");
    }
}
} // namespace
int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for(const auto& lab:linear_labs) checks(lab);
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
