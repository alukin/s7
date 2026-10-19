#pragma once

#include "diagram_harness.hpp"
#include <boost/math/special_functions/ellint_1.hpp>
#include <boost/math/special_functions/jacobi_elliptic.hpp>

#include <array>
#include <iomanip>
#include <limits>
#include <numbers>
#include <string_view>

namespace s7tests::nonlinear_pendulum {
using State = std::array<double, 2>;
using ReferenceState = std::array<long double, 2>;
inline constexpr double gravity = 9.81;
inline constexpr double duration = 100;
inline constexpr std::array frequencies{1000, 10000};
inline constexpr std::array<std::string_view, 3> methods{"euler", "symplectic", "trapezoid"};

inline const boost::json::object& budgets() {
    static const auto value = read_json_object(std::filesystem::path(S7_SINGLE_PENDULUM_DIR)/"quality_budgets.json");
    return value;
}
inline const boost::json::object& budget(int frequency) {
    for (const auto& entry : budgets().at("frequency_budgets").as_array()) {
        if (entry.as_object().at("hz").to_number<int>() == frequency) return entry.as_object();
    }
    throw std::runtime_error("No declared budget for frequency " + std::to_string(frequency));
}
inline std::string name(std::string_view method) { return "nonlinear_pendulum_" + std::string(method); }
inline boost::json::object diagram(std::string_view method, int frequency) {
    auto root = read_json_object(std::filesystem::path(S7_SINGLE_PENDULUM_DIR)/(name(method)+".s7diagram"));
    root["dt"] = 1.0/frequency;
    for (auto& element : root.at("schema").as_object().at("elements").as_array()) {
        auto& params = element.as_object().at("params").as_object();
        if (params.contains("dt")) params["dt"] = 1.0/frequency;
    }
    return root;
}

struct Reference {
    long double k = std::sin(0.75L);
    long double complete = boost::math::ellint_1(k);
    long double period = 4*complete/std::sqrt(static_cast<long double>(gravity));
    double rk4_coarse_error{}, rk4_fine_error{};
    std::vector<State> grid;

    ReferenceState at(long double time) const {
        long double cn, dn;
        const auto sn = boost::math::jacobi_elliptic(k, complete-std::sqrt(static_cast<long double>(gravity))*time, &cn, &dn);
        return {2*std::asin(k*sn), -2*k*std::sqrt(static_cast<long double>(gravity))*cn};
    }
    static ReferenceState rk4(ReferenceState z, long double h) {
        const auto f = [](ReferenceState x) -> ReferenceState {
            return {x[1], -static_cast<long double>(gravity)*std::sin(x[0])};
        };
        const auto add = [](ReferenceState a, ReferenceState b, long double scale) -> ReferenceState {
            return {a[0]+scale*b[0], a[1]+scale*b[1]};
        };
        const auto a=f(z), b=f(add(z,a,h/2)), c=f(add(z,b,h/2)), d=f(add(z,c,h));
        return {z[0]+h*(a[0]+2*b[0]+2*c[0]+d[0])/6,
                z[1]+h*(a[1]+2*b[1]+2*c[1]+d[1])/6};
    }
    double rk4_error(int frequency) const {
        ReferenceState state{1.5L,0};
        long double worst = 0;
        for (int n=1; n<=100*frequency; ++n) {
            state=rk4(state,1.0L/frequency);
            if (n%frequency==0) {
                const auto exact=at(static_cast<long double>(n)/frequency);
                for (int i=0; i<2; ++i) worst=std::max(worst,std::abs(state[i]-exact[i]));
            }
        }
        return static_cast<double>(worst);
    }
    Reference() {
        long double a=1, b=std::sqrt(1-k*k);
        for (int i=0; i<32; ++i) {
            const auto next=(a+b)/2;
            b=std::sqrt(a*b); a=next;
        }
        const auto independent_period=2*std::numbers::pi_v<long double>/(a*std::sqrt(static_cast<long double>(gravity)));
        if (std::abs(period-independent_period)>1e-14L) throw std::runtime_error("AGM/elliptic period disagreement");
        const auto start=at(0);
        if (std::abs(start[0]-1.5L)>1e-14L || std::abs(start[1])>1e-14L) throw std::runtime_error("Reference initial state mismatch");
        rk4_coarse_error=rk4_error(500);
        rk4_fine_error=rk4_error(1000);
        if (rk4_fine_error>1e-8 || rk4_coarse_error<10*rk4_fine_error) {
            throw std::runtime_error("Independent RK4 refinement did not validate the elliptic reference");
        }
        grid.reserve(100001);
        for (int i=0; i<=100000; ++i) {
            const auto z=at(static_cast<long double>(i)/1000);
            grid.push_back({static_cast<double>(z[0]),static_cast<double>(z[1])});
        }
    }
};
inline const Reference& reference() { static const Reference value; return value; }
inline double energy(State z) { return z[1]*z[1]/2+gravity*(1-s7::math::cos(z[0])); }

struct Result {
    double max_angle{}, max_velocity{}, sum_angle_squared{}, short_angle{};
    double max_energy{}, final_energy_growth{}, max_endpoint_residual{};
    double mean_period{}, max_period_error{}, max_crossing_drift{};
    std::size_t measured{}, crossings{};
    State first{}, last{};
    bool quality_pass{};
    double rms_angle() const { return std::sqrt(sum_angle_squared/measured); }
};

// The optional native-double test stream stores (observation time, theta,
// omega) for EVERY step. Studio consumes it sequentially to verify all samples
// and all CSV rows without a full-resolution in-memory trace or a portable
// wire-format claim. CSV plot traces are independently decimated to 10 ms.
inline Result run(std::string_view method, int frequency, std::ostream* raw=nullptr,
                  const std::filesystem::path& plot_path={}) {
    const auto& ref=reference();
    const auto root=diagram(method,frequency);
    auto schema=build_schema(root);
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    const auto step_ns=std::llround(1e9/frequency);
    runtime.set_model_time_step_ns(step_ns);
    s7::ObservationService observation(runtime);
    std::array<s7::ProbePoint,2> points;
    const auto& probes=root.at("probes").as_array();
    for (int i=0; i<2; ++i) points[i]={std::string(probes[i].as_object().at("element").as_string()),
                                      probes[i].as_object().at("port").to_number<std::size_t>()};
    std::ofstream plot;
    if (!plot_path.empty()) {
        plot.open(plot_path);
        if (!plot) throw std::runtime_error("Cannot create plotting trace");
        plot<<std::setprecision(17)<<"step,time_s,state_time_s,theta,omega,ref_theta,ref_omega,relative_energy_error\n";
    }
    const long steps=static_cast<long>(duration*frequency), stride=frequency/1000;
    const double dt=1.0/frequency, initial_energy=energy({1.5,0});
    State previous{1.5,0};
    std::vector<double> crossings;
    Result result;
    for (long step=1; step<=steps; ++step) {
        runtime.step();
        const auto q=observation.sample_output(points[0].element_id,points[0].port);
        const auto v=observation.sample_output(points[1].element_id,points[1].port);
        if (q.step!=static_cast<std::size_t>(step) || v.step!=q.step || q.model_time_ns!=step*step_ns || v.model_time_ns!=q.model_time_ns) {
            throw std::runtime_error("Nonlinear pendulum observation time/step mismatch");
        }
        const State state{q.as_real(),v.as_real()};
        if (!std::isfinite(state[0]) || !std::isfinite(state[1])) throw std::runtime_error("Non-finite pendulum state");
        const double time=static_cast<double>(*q.model_time_ns)*1e-9;
        if (raw) {
            const std::array row{time,state[0],state[1]};
            raw->write(reinterpret_cast<const char*>(row.data()),sizeof(row));
            if (!*raw) throw std::runtime_error("Cannot write full-resolution comparison trace");
        }
        const double drift=energy(state)/initial_energy-1;
        result.max_energy=std::max(result.max_energy,std::abs(drift));
        result.final_energy_growth=drift;
        if (step==1) result.first=state;
        if (step>1) {
            if (previous[0]>0 && state[0]<=0) {
                crossings.push_back((step-2+previous[0]/(previous[0]-state[0]))*dt);
            }
            if (method=="trapezoid") {
                const double rq=state[0]-previous[0]-dt*(previous[1]+state[1])/2;
                const double rv=state[1]-previous[1]+dt*gravity*(s7::math::sin(previous[0])+s7::math::sin(state[0]))/2;
                result.max_endpoint_residual=std::max({result.max_endpoint_residual,std::abs(rq),std::abs(rv)});
            }
        }
        if ((step-1)%stride==0 || step==steps) {
            State exact;
            if ((step-1)%stride==0) exact=ref.grid[static_cast<std::size_t>((step-1)/stride)];
            else { const auto z=ref.at(static_cast<long double>(step-1)/frequency); exact={double(z[0]),double(z[1])}; }
            const double error=std::abs(state[0]-exact[0]);
            result.max_angle=std::max(result.max_angle,error);
            result.max_velocity=std::max(result.max_velocity,std::abs(state[1]-exact[1]));
            result.sum_angle_squared+=error*error;
            ++result.measured;
            if ((step-1)*dt<=budgets().at("short_time_convergence_window_s").to_number<double>()) result.short_angle=std::max(result.short_angle,error);
            if (plot.is_open() && ((step-1)%(10*stride)==0 || step==steps)) {
                plot<<step<<','<<time<<','<<(step-1)*dt<<','<<state[0]<<','<<state[1]<<','
                    <<exact[0]<<','<<exact[1]<<','<<drift<<'\n';
            }
        }
        previous=state;
        result.last=state;
    }
    if (result.first!=State{1.5,0}) throw std::runtime_error("Nonlinear pendulum initial publication mismatch");
    result.crossings=crossings.size();
    if (crossings.size()<2) { result.mean_period=result.max_period_error=std::numeric_limits<double>::infinity(); }
    else {
        for (std::size_t i=1; i<crossings.size(); ++i) {
            const double period=crossings[i]-crossings[i-1];
            result.mean_period+=period;
            result.max_period_error=std::max(result.max_period_error,std::abs(period-double(ref.period)));
        }
        result.mean_period/=crossings.size()-1;
    }
    for (std::size_t i=0; i<crossings.size(); ++i) {
        result.max_crossing_drift=std::max(result.max_crossing_drift,std::abs(crossings[i]-(double(ref.period)/4+i*double(ref.period))));
    }
    const auto& b=budget(frequency);
    const auto expected_crossings=static_cast<std::size_t>(std::floor((duration-dt)/double(ref.period)-0.25)+1);
    result.quality_pass=result.max_angle<=b.at("max_angle_rad").to_number<double>() &&
        result.max_velocity<=b.at("max_velocity_rad_s").to_number<double>() &&
        result.max_energy<=b.at("max_relative_energy_error").to_number<double>() &&
        result.max_period_error<=b.at("max_period_error_s").to_number<double>() &&
        result.crossings==expected_crossings;
    const bool expected=budgets().at("expected_quality_pass").as_object().at(method).as_bool();
    if (result.quality_pass!=expected) throw std::runtime_error(name(method)+": unexpected quality outcome at "+std::to_string(frequency)+" Hz");
    if (method=="euler" && result.final_energy_growth<=0.02) throw std::runtime_error("Euler drift baseline did not expose expected growth");
    if (method=="trapezoid" && (result.max_endpoint_residual>budgets().at("trapezoid_max_endpoint_residual").to_number<double>() ||
        result.max_energy>budgets().at("trapezoid_max_relative_energy_error_over_dt_squared").to_number<double>()*dt*dt)) {
        throw std::runtime_error("Nonlinear Trapezoid residual/energy budget exceeded");
    }
    return result;
}
} // namespace s7tests::nonlinear_pendulum
