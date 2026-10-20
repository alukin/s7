// Exercises the saved teaching graphs, including embedded controller edits.
#include "diagram_harness.hpp"
#include <array>
#include <iostream>
#include <limits>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

std::unique_ptr<s7::NodeRuntime> load(const std::string& path, double dt) {
    auto document = s7tests::read_json_object(std::filesystem::path(S7_EDUCATIONAL_DIR) / path);
    boost::json::value json = document.at("schema");
    // Source event times and PWM carrier frequency must stay fixed under refinement.
    // configure_schema_discretization updates rates, not sample-count parameters.
    const double original_dt = document.at("dt").to_number<double>();
    for (auto& value : json.as_object().at("elements").as_array()) {
        auto& params = value.as_object().at("params").as_object();
        for (const char* count : {"start_sample", "period_samples"})
            if (auto* v = params.if_contains(count))
                *v = std::llround(v->to_number<double>() * original_dt / dt);
    }
    if (dt != original_dt) s7::configure_schema_discretization(json, dt);
    auto schema = s7::SchemaLibraryLoader::build_schema(json);
    const auto validation = s7::Validator::validate(schema);
    for (const auto& error : validation.errors) std::cerr << error << '\n';
    require(validation.ok(), path + ": graph validation");
    auto runtime = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    runtime->set_model_time_step_ns(std::llround(dt * 1e9));
    return runtime;
}

struct ControlResult { double output, recovery, reference_error; };

// RK4 is a test reference only. Input is held over this local reference step.
using State = std::array<double, 2>;
template<class Derivative>
State rk4(State x, double h, Derivative f) {
    auto add = [](State a, State b, double scale) {return State{a[0]+scale*b[0], a[1]+scale*b[1]};};
    const auto a=f(x), b=f(add(x,a,h/2)), c=f(add(x,b,h/2)), d=f(add(x,c,h));
    return {x[0]+h*(a[0]+2*b[0]+2*c[0]+d[0])/6,
            x[1]+h*(a[1]+2*b[1]+2*c[1]+d[1])/6};
}

ControlResult control(const std::string& plant, const std::string& mode, double dt) {
    const auto name = plant + "_" + mode;
    auto runtime = load("control/" + name + ".s7diagram", dt);
    const auto out = [&](const char* id, std::size_t port = 0) {
        return runtime->real_output_of(id, port);
    };
    // Independent scalar discrete recurrence; never evaluates an S7 reference graph.
    double y = 0, current = 0, speed = 0, integral = 0, filter = 0;
    double max_error = 0, recovery = 0, plant_error = 0, self_error = 0;
    State plant_reference{}, fine_reference{};
    const auto steps = static_cast<std::size_t>(std::llround(12/dt));
    for (std::size_t n = 0; n < steps; ++n) {
        const double t = n*dt;
        const double ref = n < std::llround(.5/dt) ? 0 :
                           n < std::llround(2/dt) ? .6 : n < std::llround(4/dt) ? 2 : .6;
        const double load = n < std::llround(6/dt) ? 0 : plant == "dc_motor" ? .1 : .2;
        const double error = ref-y;
        const double rate = (y-filter)/.03;
        const double raw = mode == "open_loop" ? ref : mode == "p" ? 1.2*error :
                           1.2*error+integral-(mode == "pid" ? .02*rate : 0);
        const double u = std::clamp(raw, -1.0, 1.0);
        runtime->step();
        require(runtime->model_time_ns() == std::llround((n+1)*dt*1e9), name + ": timestamp");
        auto compare = [&](double actual, double expected) {
            require(std::isfinite(actual), name + ": finite output");
            max_error = std::max(max_error, std::abs(actual-expected));
        };
        compare(out("reference"), ref);
        compare(out("load"), load);
        compare(out("output"), y);
        compare(out("error"), error);
        compare(out("controller"), u);
        if (mode == "pi" || mode == "pid" || mode == "pi_no_aw") compare(out("controller", 1), raw);
        require(out("controller") >= -1 && out("controller") <= 1, name + ": actuator limits");
        if (t >= 4 && t < 6) recovery += dt*std::abs(out("output")-.6);
        const State actual = plant == "dc_motor" ? State{out("current"),out("speed")} : State{out("output"),0};
        plant_error=std::max({plant_error,std::abs(actual[0]-plant_reference[0]),std::abs(actual[1]-plant_reference[1])});
        self_error=std::max({self_error,std::abs(fine_reference[0]-plant_reference[0]),std::abs(fine_reference[1]-plant_reference[1])});
        const auto derivative = [&](State x) {
            return plant == "dc_motor" ? State{(4*u-x[0]-.5*x[1])/.1,(.5*x[0]-.1*x[1]-load)/.1} :
                                         State{(u-x[0]-load)/.5,0};
        };
        plant_reference=rk4(plant_reference,dt,derivative);
        fine_reference=rk4(fine_reference,dt/2,derivative);
        fine_reference=rk4(fine_reference,dt/2,derivative);
        // Current and speed use the same old states in explicit Euler.
        if (plant == "dc_motor") {
            compare(out("current"), current);
            compare(out("speed"), speed);
            const double next_current = current+dt*(4*u-current-.5*speed)/.1;
            speed += dt*(.5*current-.1*speed-load)/.1;
            current = next_current;
            y = speed/5;
        } else y += dt*(u-y-load)/.5;
        integral += dt*(2*error+(mode == "pi_no_aw" ? 0 : 10)*(u-raw));
        filter += dt*rate;
    }
    require(max_error < 1e-9, name + ": discrete controller/plant reference mismatch");
    require(self_error < 1e-8 && plant_error < .05,name + ": held-input continuous plant reference");
    if (mode == "pi" || mode == "pid")
        require(std::abs(out("output")-.6) < .015, name + ": disturbance rejection");
    std::cout << name << " dt=" << dt << " discrete_error=" << max_error
              << " final=" << out("output") << " recovery_IAE=" << recovery
              << " plant_ref_error=" << plant_error << '\n';
    return {out("output"), recovery, plant_error};
}

struct BuckResult { double energy_defect, reference_error, mean, ripple; };
BuckResult buck(bool closed_loop, double dt) {
    const std::string name = closed_loop ? "buck_pid_load_step" : "buck_open_loop_load_step";
    auto runtime = load("buck/" + name + ".s7diagram", dt);
    const auto out = [&](const char* id, std::size_t p = 0) {return runtime->real_output_of(id,p);};
    State reference{}, half_reference{};
    double previous_energy=0, previous_power=0, defect=0, input_energy=0;
    double max_reference_error=0, reference_self_error=0, peak=0, pre_sum=0, post_sum=0;
    double ripple_low=100, ripple_high=-100;
    std::size_t pre_count=0, post_count=0;
    double latched_duty=0;
    const auto period = static_cast<std::size_t>(std::llround(.001/dt));
    const auto steps = static_cast<std::size_t>(std::llround(1/dt));
    for (std::size_t n=0; n<steps; ++n) {
        runtime->step();
        const double i=out("inductor_current"), v=out("output_voltage"), gate=out("pwm");
        const double r=n<std::llround(.5/dt) ? 8 : 4;
        require(out("load_resistance")==r, name+": load step timing");
        const double duty=out("controller");
        require(duty>=0 && duty<=1, name+": bounded duty");
        if (n%period==0) latched_duty=duty;
        require(gate==double(double(n%period)/period<latched_duty), name+": latched PWM reference");
        require(std::isfinite(i) && std::isfinite(v), name+": finite electrical state");
        if (n==0) require(i==0 && v==0, name+": zero startup state");
        max_reference_error=std::max({max_reference_error,std::abs(i-reference[0]),std::abs(v-reference[1])});
        reference_self_error=std::max({reference_self_error,std::abs(reference[0]-half_reference[0]),
                                      std::abs(reference[1]-half_reference[1])});
        const auto derivative=[&](State x) {
            return State{(24*gate-x[1]-.2*x[0])/.01,(x[0]-x[1]/r)/.001};
        };
        reference=rk4(reference,dt,derivative);
        half_reference=rk4(half_reference,dt/2,derivative);
        half_reference=rk4(half_reference,dt/2,derivative);
        const double energy=.01*i*i/2+.001*v*v/2;
        if (n) defect += energy-previous_energy-dt*previous_power;
        previous_energy=energy;
        // Ideal synchronous switch: gate=0 ties the switch node to return;
        // reverse current is allowed. No diode clipping or extra unit delay.
        previous_power=24*gate*i-.2*i*i-v*v/r;
        input_energy += dt*24*gate*i;
        peak=std::max(peak,v);
        if (n>=std::llround(.3/dt) && n<std::llround(.45/dt)) {pre_sum+=v; ++pre_count;}
        if (n>=std::llround(.8/dt)) {post_sum+=v; ++post_count;}
        if (n>=std::llround(.9/dt)) {ripple_low=std::min(ripple_low,v);ripple_high=std::max(ripple_high,v);}
    }
    const double pre=pre_sum/pre_count, mean=post_sum/post_count, ripple=ripple_high-ripple_low;
    std::cout << name << " dt=" << dt << " mean=" << mean << " ripple=" << ripple
              << " peak=" << peak << " switched_ref_error=" << max_reference_error
              << " energy_defect_J=" << defect << " fractional=" << defect/input_energy << '\n';
    require(reference_self_error<1e-8, name+": RK4 reference self-refinement");
    require(max_reference_error<.25, name+": switched reference accuracy");
    require(peak<(closed_loop ? 16 : 24) && ripple<1.2, name+": startup/ripple envelope");
    require(defect>=0 && defect/input_energy<.01, name+": integrated energy-balance defect");
    if (closed_loop) require(std::abs(pre-12)<.2 && std::abs(mean-12)<.2, name+": load-step regulation");
    else {
        require(std::abs(pre-12*8/8.2)<.15 && std::abs(mean-12*4/4.2)<.15, name+": DC load reference");
        require(pre-mean>.15, name+": open-loop load droop");
    }
    return {defect,max_reference_error,mean,ripple};
}

struct ThermalResult { double warm_error, period; };
ThermalResult thermostat(double band, double dt) {
    const std::string name=band==1 ? "thermostat" : "thermostat_narrow_band";
    auto runtime=load("relay/"+name+".s7diagram",dt);
    const auto out=[&](const char* id) {return runtime->real_output_of(id);};
    double temp=18, power=0, warm_error=0;
    bool on=false, previous_on=false, warming=true;
    std::vector<double> off_times;
    double lowest=100, highest=-100, post_low=100, post_high=-100;
    const auto steps=static_cast<std::size_t>(std::llround(1200/dt));
    for(std::size_t n=0;n<steps;++n) {
        const double t=n*dt, ambient=n<std::llround(600/dt) ? 15 : 10;
        const double error=20-temp;
        if(error>=band) on=true;
        else if(error<=-band) on=false;
        runtime->step();
        require(out("ambient")==ambient, name+": ambient step timing");
        require(std::abs(out("temperature")-temp)<1e-10 && std::abs(out("heater_power")-power)<1e-8,
                name+": independent thermal recurrence");
        require(out("relay")==double(on),name+": independent relay thresholds/memory");
        require(power>=0 && power<=2000,name+": bounded heater power");
        if(previous_on && !on) {
            require(temp>=20+band && temp<20+band+.03,name+": upper switching threshold");
            warming=false;
            if(t>200 && t<590) off_times.push_back(t);
        }
        if(!previous_on && on) require(temp<=20-band,name+": lower switching threshold");
        if(warming) {
            // Exact cascade response while the initially cold heater is continuously on.
            const double exact=35+(-17-20.0/9)*std::exp(-t/100)+(20.0/9)*std::exp(-t/10);
            warm_error=std::max(warm_error,std::abs(temp-exact));
        }
        if(t>200 && t<590) {lowest=std::min(lowest,temp);highest=std::max(highest,temp);}
        if(t>800) {post_low=std::min(post_low,temp);post_high=std::max(post_high,temp);}
        const double next_power=power+dt*(2000*double(on)-power)/10;
        temp+=dt*(power-100*(temp-ambient))/10000;
        power=next_power;
        previous_on=on;
    }
    require(off_times.size()>=3,name+": repeated limit cycle");
    require(highest>20+band+.02 && lowest<20-band-.02,name+": heater-lag overshoot/undershoot");
    require(post_low>17 && post_high<23,name+": bounded cycle after ambient disturbance");
    require(warm_error<.02,name+": analytical startup accuracy");
    const double period=(off_times.back()-off_times.front())/(off_times.size()-1);
    std::cout << name << " dt=" << dt << " startup_error=" << warm_error
              << " cycle_range=" << lowest << ".." << highest << " period_s=" << period << '\n';
    return {warm_error,period};
}
}

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for(const std::string plant : {"first_order","dc_motor"}) {
        std::array<double,5> coarse_errors{};
        for(double dt : {.001,.0005}) {
            const auto open=control(plant,"open_loop",dt);
            const auto p=control(plant,"p",dt);
            const auto pi=control(plant,"pi",dt);
            const auto pid=control(plant,"pid",dt);
            const auto no_aw=control(plant,"pi_no_aw",dt);
            require(std::abs(p.output-.6)>.05 && std::abs(open.output-.6)>.02,plant+": P/open-loop offset");
            require(pi.recovery<no_aw.recovery*.8,plant+": anti-windup improves recovery");
            const std::array errors{open.reference_error,p.reference_error,pi.reference_error,pid.reference_error,no_aw.reference_error};
            if(dt==.001) coarse_errors=errors;
            else for(std::size_t i=0;i<errors.size();++i)
                require(coarse_errors[i]/errors[i]>1.7 && coarse_errors[i]/errors[i]<2.3,
                        plant+": held-input plant error refines at first order");
        }
    }
    for(bool closed : {false,true}) {
        const auto coarse=buck(closed,1e-5), fine=buck(closed,5e-6);
        require(coarse.energy_defect/fine.energy_defect>1.7 && coarse.energy_defect/fine.energy_defect<2.3,
                "Buck energy-balance defect refines at first order");
        require(fine.reference_error<coarse.reference_error*.7,"Buck switched-state error refines");
        require(std::abs(coarse.mean-fine.mean)<.15,"Buck mean voltage refines");
    }
    for(double band : {1.0,.1}) {
        const auto coarse=thermostat(band,.1),fine=thermostat(band,.05);
        require(coarse.warm_error/fine.warm_error>1.8 && coarse.warm_error/fine.warm_error<2.2,
                "Thermal analytical startup refines at first order");
        require(std::abs(coarse.period-fine.period)<1,"Thermal limit-cycle period refines");
    }
    return 0;
} catch(const std::exception& ex) {std::cerr<<ex.what()<<'\n';return 1;}
