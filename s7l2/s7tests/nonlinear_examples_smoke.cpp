// Independent references for the saved nonlinear teaching graphs. No runtime solver.
#include "diagram_harness.hpp"
#include <array>
#include <iostream>
#include <limits>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

template<std::size_t N> using State = std::array<long double,N>;
template<std::size_t N, class Function>
State<N> rk4(State<N> state, long double dt, Function f, int subdivisions=1) {
    const auto h=dt/subdivisions;
    for(int step=0;step<subdivisions;++step) {
        auto add=[&](State<N> derivative,long double scale) {
            State<N> result;
            for(std::size_t i=0;i<N;++i) result[i]=state[i]+scale*derivative[i];
            return result;
        };
        const auto a=f(state), b=f(add(a,h/2)), c=f(add(b,h/2)), d=f(add(c,h));
        for(std::size_t i=0;i<N;++i) state[i]+=h*(a[i]+2*b[i]+2*c[i]+d[i])/6;
    }
    return state;
}
template<std::size_t N> double distance(State<N> a, State<N> b) {
    long double sum=0;
    for(std::size_t i=0;i<N;++i) sum+=(a[i]-b[i])*(a[i]-b[i]);
    return static_cast<double>(std::sqrt(sum));
}

struct Case {
    std::unique_ptr<s7::NodeRuntime> runtime;
    std::vector<s7::ProbePoint> points;
    double dt;
    std::size_t steps=0;

    std::vector<double> sample() {
        runtime->step();
        const auto snapshot=s7::ObservationService(*runtime).snapshot(points);
        ++steps;
        require(snapshot.step==steps && snapshot.model_time_ns==std::llround(steps*dt*1e9),
                "Published step/timestamp mismatch");
        std::vector<double> values;
        for(const auto& value:snapshot.samples) {
            const double real=value.to_display_double();
            require(std::isfinite(real),"Nonfinite nonlinear state");
            values.push_back(real);
        }
        return values;
    }
};

Case load(const std::string& file,double dt,const std::vector<std::string>& ids,
          const std::vector<std::size_t>& ports={}) {
    const auto document=s7tests::read_json_object(std::filesystem::path(S7_NONLINEAR_EXAMPLES_DIR)/file);
    boost::json::value json=document.at("schema");
    // Compile the actual saved schema unchanged at its native rate.
    if(dt!=document.at("dt").to_number<double>()) s7::configure_schema_discretization(json,dt);
    auto schema=s7::SchemaLibraryLoader::build_schema(json);
    const auto checked=s7::Validator::validate(schema);
    for(const auto& error:checked.errors) std::cerr<<error<<'\n';
    require(checked.ok(),file+": graph validation");
    auto runtime=std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    runtime->set_model_time_step_ns(std::llround(dt*1e9));
    std::vector<s7::ProbePoint> points;
    for(std::size_t i=0;i<ids.size();++i) points.push_back({ids[i],ports.empty()?0:ports[i]});
    return {std::move(runtime),std::move(points),dt};
}

struct Cycle {
    double previous=0, previous_time=0, minimum=1e100, maximum=-1e100;
    std::vector<double> crossings;
    void sample(double t,double x,double start,double level=0) {
        if(t>=start) {
            minimum=std::min(minimum,x);maximum=std::max(maximum,x);
            if(previous_time>=start && previous<level && x>=level)
                crossings.push_back(previous_time+(t-previous_time)*(level-previous)/(x-previous));
        }
        previous=x;previous_time=t;
    }
    double period() const {
        require(crossings.size()>=3,"Insufficient cycles for period measurement");
        return (crossings.back()-crossings.front())/(crossings.size()-1);
    }
};

double vdp(int mu,bool symplectic,double dt) {
    const std::string name="van_der_pol_mu"+std::to_string(mu)+(symplectic?"_symplectic":"_euler");
    std::vector<std::string> ids;
    std::vector<std::size_t> ports;
    for(const std::string label:{"small","cycle","large"}) {
        ids.push_back(label+(symplectic?"_state":"_x")); ports.push_back(0);
        ids.push_back(label+(symplectic?"_state":"_v")); ports.push_back(symplectic?1:0);
    }
    auto model=load("van_der_pol/"+name+".s7diagram",dt,ids,ports);
    std::array<State<2>,3> reference{State<2>{.1L,0},State<2>{2,0},State<2>{4,0}}, fine=reference;
    std::array<Cycle,3> cycles{}, reference_cycles{};
    const auto derivative=[&](State<2> x) {return State<2>{x[1],mu*(1-x[0]*x[0])*x[1]-x[0]};};
    const double duration=mu==3?60:40,start=mu==3?30:20;
    double error=0,self_error=0;
    for(std::size_t n=0;n<static_cast<std::size_t>(std::llround(duration/dt));++n) {
        const auto values=model.sample();
        const double t=n*dt;
        for(std::size_t j=0;j<3;++j) {
            const State<2> actual{values[2*j],values[2*j+1]};
            if(n==0) require(distance(actual,reference[j])<1e-14,name+": initial publication");
            require(std::abs(actual[0])<5 && std::abs(actual[1])<10,name+": bounded oscillation");
            if(t<=5) error=std::max(error,distance(actual,reference[j]));
            self_error=std::max(self_error,distance(reference[j],fine[j]));
            cycles[j].sample(t,values[2*j],start);
            reference_cycles[j].sample(t,static_cast<double>(reference[j][0]),start);
            reference[j]=rk4(reference[j],dt,derivative);
            fine[j]=rk4(fine[j],dt,derivative,2);
        }
    }
    require(self_error<1e-7,name+": RK4 self-refinement");
    require(error<.1,name+": first-five-second state error");
    double shortest=100,longest=0,smallest=100,largest=0;
    for(std::size_t j=0;j<3;++j) {
        const double period=cycles[j].period(),reference_period=reference_cycles[j].period();
        require(std::abs(period-reference_period)/reference_period<.005,name+": reference cycle period");
        require(std::abs(cycles[j].maximum-reference_cycles[j].maximum)<.03 &&
                std::abs(cycles[j].minimum-reference_cycles[j].minimum)<.03,name+": reference extrema");
        require(cycles[j].maximum>1.9 && cycles[j].maximum<2.2 &&
                cycles[j].minimum< -1.9 && cycles[j].minimum> -2.2,name+": nonzero limit cycle");
        shortest=std::min(shortest,period);longest=std::max(longest,period);
        smallest=std::min(smallest,cycles[j].maximum);largest=std::max(largest,cycles[j].maximum);
    }
    require(longest-shortest<.05 && largest-smallest<.03,name+": initial-state convergence to cycle geometry");
    std::cout<<name<<" dt="<<dt<<" short_error="<<error<<" periods="<<shortest<<".."<<longest
             <<" maxima="<<smallest<<".."<<largest<<'\n';
    return error;
}

double lorenz(int rho,double dt) {
    const std::string name=rho==28?"lorenz_attractor":"lorenz_stable";
    auto model=load("lorenz/"+name+".s7diagram",dt,
                    {"base_x","base_y","base_z","nearby_x","nearby_y","nearby_z",
                     "delta_x","delta_y","delta_z"});
    std::array<State<3>,2> reference{State<3>{1,1,1},State<3>{1.000001L,1,1}},fine=reference;
    const auto derivative=[&](State<3> x) {
        return State<3>{10*(x[1]-x[0]),x[0]*(rho-x[2])-x[1],x[0]*x[1]-(8.L/3)*x[2]};
    };
    const double duration=rho==28?40:10;
    double error=0,self_error=0,separation=0,early_separation=0,sum_z=0,sum_x2=0;
    std::size_t positive=0,negative=0,samples=0;
    State<3> last{};
    for(std::size_t n=0;n<static_cast<std::size_t>(std::llround(duration/dt));++n) {
        const auto values=model.sample();
        const double t=n*dt;
        const State<3> base{values[0],values[1],values[2]},nearby{values[3],values[4],values[5]};
        last=base;
        if(n==0) require(distance(base,reference[0])<1e-14 && distance(nearby,reference[1])<1e-14,
                         name+": nearby initial publication");
        for(std::size_t i=0;i<3;++i)
            require(std::abs(values[6+i]-(values[3+i]-values[i]))<1e-12,name+": difference probes");
        for(const auto x:{base,nearby})
            require(std::abs(x[0])<30 && std::abs(x[1])<40 && x[2]>0 && x[2]<60,name+": bounded trajectory");
        if(t<=1) {
            early_separation=std::max(early_separation,distance(base,nearby));
            for(std::size_t j=0;j<2;++j) {
                error=std::max(error,distance(j==0?base:nearby,reference[j]));
                self_error=std::max(self_error,distance(reference[j],fine[j]));
                reference[j]=rk4(reference[j],dt,derivative,4);
                fine[j]=rk4(fine[j],dt,derivative,8);
            }
        }
        if(t>=20) separation=std::max(separation,distance(base,nearby));
        if(t>=10 && rho==28) {
            if(base[0]>2) ++positive;
            if(base[0]< -2) ++negative;
            sum_z+=static_cast<double>(base[2]);sum_x2+=static_cast<double>(base[0]*base[0]);++samples;
        }
    }
    std::cout<<name<<" dt="<<dt<<" short_error="<<error<<" reference_self_error="<<self_error
             <<" early_separation="<<early_separation<<" late_max_separation="<<separation;
    if(samples) std::cout<<" mean_z="<<sum_z/samples<<" rms_x="<<std::sqrt(sum_x2/samples);
    std::cout<<'\n';
    require(self_error<1e-8 && error<.25,name+": short-horizon reference accuracy");
    require(early_separation<.01,name+": nearby trajectories start close");
    if(rho==28) {
        require(positive>100 && negative>100,name+": both attractor lobes visited");
        require(separation>.1,name+": visible nearby-state separation");
        require(sum_z/samples>15 && sum_z/samples<35 && std::sqrt(sum_x2/samples)>4 &&
                std::sqrt(sum_x2/samples)<14,name+": coarse attractor geometry envelope");
    } else {
        const auto equilibrium=std::sqrt((8.L/3)*(rho-1));
        require(distance(last,State<3>{equilibrium,equilibrium,static_cast<long double>(rho-1)})<.05,
                name+": convergence to stable equilibrium");
    }
    return error;
}

struct PopulationCase { const char* name; long double alpha,prey,predator; double dt=.001; };
struct PopulationResult { double error,drift; };
PopulationResult population(PopulationCase spec,double dt) {
    auto model=load(std::string("lotka_volterra/")+spec.name+".s7diagram",dt,{"prey","predator"});
    State<2> reference{spec.prey,spec.predator},fine=reference;
    const auto derivative=[&](State<2> x) {
        return State<2>{spec.alpha*x[0]-.5L*x[0]*x[1],.5L*x[0]*x[1]-x[1]};
    };
    const auto invariant=[&](State<2> x) {return .5L*x[0]-std::log(x[0])+.5L*x[1]-spec.alpha*std::log(x[1]);};
    const auto initial_h=invariant(reference);
    double error=0,self_error=0,drift=0,reference_drift=0;
    Cycle cycle;
    for(std::size_t n=0;n<static_cast<std::size_t>(std::llround(40/dt));++n) {
        const auto values=model.sample();
        const State<2> actual{values[0],values[1]};
        if(n==0) require(distance(actual,reference)<1e-14,std::string(spec.name)+": initial publication");
        require(actual[0]>0 && actual[1]>0 && actual[0]<12 && actual[1]<12,
                std::string(spec.name)+": positive bounded populations");
        error=std::max(error,distance(actual,reference));
        self_error=std::max(self_error,distance(reference,fine));
        drift=std::max(drift,static_cast<double>(std::abs(invariant(actual)-initial_h)));
        reference_drift=std::max(reference_drift,static_cast<double>(std::abs(invariant(reference)-initial_h)));
        cycle.sample(n*dt,values[0],5,2);
        reference=rk4(reference,dt,derivative);
        fine=rk4(fine,dt,derivative,2);
    }
    std::cout<<spec.name<<" dt="<<dt<<" state_error="<<error<<" invariant_drift="<<drift;
    if(cycle.crossings.size()>=3) std::cout<<" period="<<cycle.period();
    std::cout<<'\n';
    require(self_error<1e-8 && reference_drift<1e-9,std::string(spec.name)+": independent RK4 reference");
    require(error<.5 && drift<.1,std::string(spec.name)+": state/invariant accuracy envelope");
    if(std::string(spec.name)=="predator_prey_equilibrium")
        require(error<1e-12 && drift<1e-12,"Exact coexistence equilibrium is preserved");
    else {
        const auto period=cycle.period();
        require(period>4 && period<10,std::string(spec.name)+": cyclic dynamics");
        if(std::string(spec.name)=="predator_prey_near_equilibrium")
            require(std::abs(period-2*std::acos(-1.0))<.05,"Near-equilibrium linearized period");
    }
    return {error,drift};
}

void first_order(double coarse,double fine,const std::string& name) {
    require(coarse/fine>1.7 && coarse/fine<2.3,name+": first-order timestep refinement");
}
}

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for(const auto [mu,symplectic]:{std::pair{1,false},std::pair{1,true},std::pair{3,true}}) {
        const auto coarse=vdp(mu,symplectic,.001),fine=vdp(mu,symplectic,.0005);
        first_order(coarse,fine,"Van der Pol");
    }
    for(const int rho:{28,10}) {
        const auto coarse=lorenz(rho,.00025),fine=lorenz(rho,.000125);
        first_order(coarse,fine,"Lorenz short horizon");
    }
    for(const PopulationCase spec: {
        PopulationCase{"predator_prey",1,4,1},
        PopulationCase{"predator_prey_near_equilibrium",1,2.2L,2},
        PopulationCase{"predator_prey_equilibrium",1,2,2},
        PopulationCase{"predator_prey_faster_growth",1.5L,4,1,.00025}}) {
        const auto coarse=population(spec,spec.dt),fine=population(spec,spec.dt/2);
        if(std::string(spec.name)!="predator_prey_equilibrium") {
            first_order(coarse.error,fine.error,spec.name);
            first_order(coarse.drift,fine.drift,std::string(spec.name)+" invariant");
        }
    }
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
