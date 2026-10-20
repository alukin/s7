#include "diagram_harness.hpp"
#include <array>
#include <iostream>
#include <numbers>

namespace {
constexpr double pi=std::numbers::pi;
void require(bool ok,const std::string& message) {if(!ok) throw std::runtime_error(message);}

std::unique_ptr<s7::NodeRuntime> load(const std::string& file,double dt) {
    auto document=s7tests::read_json_object(std::filesystem::path(S7_MEASUREMENT_DIR)/file);
    boost::json::value json=document.at("schema");
    const double native=document.at("dt").to_number<double>();
    if(dt!=native) {
        for(auto& value:json.as_object().at("elements").as_array()) {
            auto& params=value.as_object().at("params").as_object();
            if(auto* count=params.if_contains("start_sample"))
                *count=std::llround(count->to_number<double>()*native/dt);
        }
        s7::configure_schema_discretization(json,dt);
    }
    auto schema=s7::SchemaLibraryLoader::build_schema(json);
    const auto checked=s7::Validator::validate(schema);
    for(const auto& error:checked.errors) std::cerr<<error<<'\n';
    require(checked.ok(),file+": validation");
    auto runtime=std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    runtime->set_model_time_step_ns(std::llround(dt*1e9));
    return runtime;
}
double wrap(double x) {return x-2*pi*std::floor((x+pi)/(2*pi));}

// Absolute interpolated crossing positions, independent of the meter's elapsed-counter implementation.
struct Meter {
    double previous=0,last=0,frequency=0;
    bool crossing_seen=false,valid=false;
    void sample(std::size_t n,double x,double dt) {
        if(previous<0 && x>=0) {
            const double at=double(n)-1-previous/(x-previous);
            if(crossing_seen) {frequency=1/((at-last)*dt);valid=true;}
            last=at;crossing_seen=true;
        }
        previous=x;
    }
};

using Pair=std::array<double,2>;
template<class F> Pair rk4(Pair state,double dt,F derivative) {
    const auto add=[&](Pair x,double h){return Pair{state[0]+h*x[0],state[1]+h*x[1]};};
    const auto a=derivative(state),b=derivative(add(a,dt/2)),c=derivative(add(b,dt/2)),d=derivative(add(c,dt));
    return {state[0]+dt*(a[0]+2*b[0]+2*c[0]+d[0])/6,state[1]+dt*(a[1]+2*b[1]+2*c[1]+d[1])/6};
}

double pll(const std::string& mode,double dt) {
    auto runtime=load("pll/pll_"+mode+".s7diagram",dt);
    const auto out=[&](const char* id,std::size_t port=0){return runtime->real_output_of(id,port);};
    double reference_phase=.8,vco_phase=0,integral=0,recurrence_error=0,short_error=0,self_error=0;
    double acquired=0,settled=0,measured_error=0,previous_error=.8;
    std::size_t slips=0;
    Pair continuous{.8,0},fine=continuous;
    Meter ref_meter,vco_meter;
    const auto steps=static_cast<std::size_t>(std::llround(6/dt));
    for(std::size_t n=0;n<steps;++n) {
        const double t=n*dt;
        const bool after=n>=static_cast<std::size_t>(std::llround(2/dt));
        const double frequency=after?(mode=="out_of_range"?35:mode=="frequency_step"?22:20):20;
        const double jump=after && mode=="phase_step"?1:0;
        const double error=wrap(reference_phase+jump-vco_phase),raw=.1*error+integral;
        const double u=std::clamp(raw,-1.,1.),vco_frequency=18+10*u;
        runtime->step();
        require(runtime->model_time_ns()==std::llround((n+1)*dt*1e9),"PLL timestamp");
        auto compare=[&](double actual,double expected){
            require(std::isfinite(actual),"PLL finite output");
            recurrence_error=std::max(recurrence_error,std::abs(actual-expected));
        };
        compare(out("reference_phase"),reference_phase);compare(out("vco_phase"),vco_phase);
        compare(out("reference_frequency"),frequency);compare(out("phase_step"),jump);
        compare(out("phase_error"),error);compare(out("controller"),u);compare(out("controller",1),raw);
        compare(out("vco_frequency"),vco_frequency);
        compare(out("reference_signal"),std::sin(reference_phase+jump));
        compare(out("vco_signal"),std::sin(vco_phase));
        ref_meter.sample(n,out("reference_signal"),dt);vco_meter.sample(n,out("vco_signal"),dt);
        require(out("reference_meter",1)==double(ref_meter.valid) && out("vco_meter",1)==double(vco_meter.valid),
                "PLL measurement validity");
        require(std::abs(out("reference_meter")-ref_meter.frequency)<1e-7 &&
                std::abs(out("vco_meter")-vco_meter.frequency)<1e-7,"PLL interpolated crossing reference");
        require(out("reference_wrapped")>=-pi && out("reference_wrapped")<pi &&
                out("vco_wrapped")>=-pi && out("vco_wrapped")<pi,"PLL wrapped phase interval");
        require(vco_frequency>=8 && vco_frequency<=28,"PLL tuning range");
        if(t<2) {
            short_error=std::max(short_error,std::hypot(error-continuous[0],integral-continuous[1]));
            self_error=std::max(self_error,std::hypot(continuous[0]-fine[0],continuous[1]-fine[1]));
            const auto derivative=[](Pair x){return Pair{2*pi*(2-10*(.1*x[0]+x[1])),x[0]};};
            continuous=rk4(continuous,dt,derivative);
            fine=rk4(rk4(fine,dt/2,derivative),dt/2,derivative);
        }
        if(t>=1.5 && t<1.9) acquired=std::max(acquired,std::abs(error));
        if(t>=4.5) {
            settled=std::max(settled,std::abs(error));
            measured_error=std::max(measured_error,std::abs(out("vco_meter")-frequency));
            require(vco_meter.valid,"PLL meter acquired");
        }
        if(after && std::abs(error-previous_error)>pi) ++slips;
        previous_error=error;
        reference_phase+=dt*(2*pi*frequency);vco_phase+=dt*(2*pi*vco_frequency);
        integral+=dt*(error+10*(u-raw));
    }
    std::cout<<"pll_"<<mode<<" dt="<<dt<<" recurrence_error="<<recurrence_error
             <<" short_error="<<short_error<<" acquired_error="<<acquired
             <<" late_phase="<<settled<<" late_meter_error="<<measured_error<<" slips="<<slips<<'\n';
    require(recurrence_error<1e-8 && self_error<1e-7,"PLL independent state/reference checks");
    require(short_error<.04 && acquired<.02,"PLL acquisition envelope");
    if(mode=="out_of_range") require(slips>=3 && settled>1 && measured_error>5,"PLL cannot lock outside range");
    else require(settled<.02 && measured_error<.1,"PLL reacquires phase/frequency lock");
    return short_error;
}

double sine_response(double w,double t,double tau) {
    const double a=w*tau;
    return (std::sin(w*t)-a*std::cos(w*t)+a*std::exp(-t/tau))/(1+a*a);
}
double cosine_response(double w,double t,double tau) {
    const double a=w*tau;
    return (std::cos(w*t)+a*std::sin(w*t)-std::exp(-t/tau))/(1+a*a);
}
double am(double dt) {
    auto runtime=load("signal_measurement/am_coherent_detection.s7diagram",dt);
    const auto out=[&](const char* id){return runtime->real_output_of(id);};
    double maximum_error=0,discrete=0,discrete_error=0,message_error=0;
    std::size_t count=0;
    for(std::size_t n=0;n<static_cast<std::size_t>(std::llround(2/dt));++n) {
        const double t=n*dt,message=std::sin(4*pi*t),carrier=std::sin(200*pi*t);
        const double signal=(1+.6*message)*carrier,mixer=2*signal*carrier;
        const double exact=1-std::exp(-t/.02)+.6*sine_response(4*pi,t,.02)-cosine_response(400*pi,t,.02)
            -.3*(sine_response(404*pi,t,.02)+sine_response(-396*pi,t,.02));
        runtime->step();
        require(runtime->model_time_ns()==std::llround((n+1)*dt*1e9),"AM timestamp");
        require(std::abs(out("am_signal")-signal)<1e-9,"AM source analytical reference");
        maximum_error=std::max(maximum_error,std::abs(out("lowpass")-exact));
        discrete_error=std::max(discrete_error,std::abs(out("lowpass")-discrete));
        require(std::abs(out("recovered")-(out("lowpass")-1)/.6)<1e-12,"AM normalization");
        if(t>=.3) {message_error+=std::pow(out("recovered")-message,2);++count;}
        discrete+=dt*(mixer-discrete)/.02;
    }
    const double rmse=std::sqrt(message_error/count);
    std::cout<<"am dt="<<dt<<" filter_error="<<maximum_error<<" message_RMSE="<<rmse<<'\n';
    require(discrete_error<1e-9 && maximum_error<.02 && rmse<.25,"AM detection/reference envelope");
    return maximum_error;
}

double fm(double dt) {
    auto runtime=load("signal_measurement/fm_frequency_detection.s7diagram",dt);
    const auto out=[&](const char* id,std::size_t port=0){return runtime->real_output_of(id,port);};
    Meter meter;
    long double phase=0;
    double maximum_error=0,filter=0,discrete_error=0,frequency_error=0,message_error=0;
    std::size_t count=0;
    for(std::size_t n=0;n<static_cast<std::size_t>(std::llround(2/dt));++n) {
        const double t=n*dt,message=std::sin(4*pi*t),frequency=100+20*message;
        const double signal=static_cast<double>(std::sin(phase));
        const double continuous=std::sin(200*pi*t+10*(1-std::cos(4*pi*t)));
        runtime->step();
        require(runtime->model_time_ns()==std::llround((n+1)*dt*1e9),"FM timestamp");
        require(std::abs(out("fm_signal")-signal)<1e-9 && std::abs(out("frequency")-frequency)<1e-10,
                "FM source discrete phase reference");
        meter.sample(n,signal,dt);
        require(out("meter",1)==double(meter.valid) && std::abs(out("meter")-meter.frequency)<1e-6,
                "FM crossing period/validity reference");
        const double raw=meter.valid?(meter.frequency-100)/20:0;
        require(std::abs(out("valid_message")-raw)<1e-7,"FM invalid estimate suppression");
        discrete_error=std::max(discrete_error,std::abs(out("lowpass")-filter));
        maximum_error=std::max(maximum_error,std::abs(out("fm_signal")-continuous));
        if(t>=.3) {
            frequency_error=std::max(frequency_error,std::abs(out("meter")-frequency));
            message_error+=std::pow(out("lowpass")-message,2);++count;
        }
        filter+=dt*(raw-filter)/.02;
        phase+=2*std::numbers::pi_v<long double>*frequency*dt;
    }
    const double rmse=std::sqrt(message_error/count);
    std::cout<<"fm dt="<<dt<<" waveform_error="<<maximum_error<<" meter_error_Hz="<<frequency_error
             <<" message_RMSE="<<rmse<<'\n';
    require(discrete_error<1e-7 && maximum_error<.02 && frequency_error<4 && rmse<.3,"FM measurement envelope");
    return maximum_error;
}

double rms(bool dc,double dt) {
    const std::string name=dc?"rms_dc_step":"rms_sine_step";
    auto runtime=load("signal_measurement/"+name+".s7diagram",dt);
    const auto out=[&](const char* id){return runtime->real_output_of(id);};
    const auto response=[&](double t) {return dc?1-std::exp(-t/.05):.5*(1-std::exp(-t/.05)-cosine_response(200*pi,t,.05));};
    double q=0,maximum_error=0,discrete_error=0,settled_error=0;
    for(std::size_t n=0;n<static_cast<std::size_t>(std::llround(2/dt));++n) {
        const double t=n*dt;
        const bool after=n>=static_cast<std::size_t>(std::llround(1/dt));
        const double amplitude=after?1:dc?-2:2;
        const double signal=dc?amplitude:amplitude*std::sin(100*pi*t);
        // At t=1, the 50 Hz sine is at zero phase modulo 2*pi.
        const double exact_q=after?4*response(1)*std::exp(-(t-1)/.05)+response(t-1):4*response(t);
        runtime->step();
        require(runtime->model_time_ns()==std::llround((n+1)*dt*1e9),"RMS timestamp");
        require(out("amplitude")==amplitude && std::abs(out("signal")-signal)<1e-9,"RMS input schedule");
        const double result=out("rms");
        require(std::isfinite(result) && result>=0,"RMS nonnegative finite result");
        discrete_error=std::max(discrete_error,std::abs(result-std::sqrt(q)));
        maximum_error=std::max(maximum_error,std::abs(result-std::sqrt(std::max(0.,exact_q))));
        if(t>=1.5) settled_error=std::max(settled_error,std::abs(result-(dc?1:1/std::sqrt(2.))));
        q+=dt*(signal*signal-q)/.05;
    }
    std::cout<<name<<" dt="<<dt<<" analytical_error="<<maximum_error<<" settled_error="<<settled_error<<'\n';
    require(discrete_error<1e-9 && maximum_error<.03 && settled_error<.02,"RMS analytical/discrete envelope");
    return maximum_error;
}

void refinement(double coarse,double fine,const std::string& name) {
    require(coarse/fine>1.7 && coarse/fine<2.3,name+": first-order refinement");
}
}

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for(const std::string mode:{"frequency_step","phase_step","out_of_range"}) {
        const auto coarse=pll(mode,.001),fine=pll(mode,.0005);refinement(coarse,fine,"PLL");
    }
    {const auto coarse=am(.0001),fine=am(.00005);refinement(coarse,fine,"AM filter");}
    {const auto coarse=fm(.0001),fine=fm(.00005);refinement(coarse,fine,"FM source");}
    for(bool dc:{false,true}) {
        const auto coarse=rms(dc,.0001),fine=rms(dc,.00005);refinement(coarse,fine,"RMS");
    }
    return 0;
} catch(const std::exception& ex) {std::cerr<<ex.what()<<'\n';return 1;}
