#include "diagram_harness.hpp"
#include <iostream>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

double check(const std::string& model, const std::string& method, double dt) {
    auto diagram = s7tests::read_json_object(std::filesystem::path(S7_LINEAR_DIR) /
                                           (model + "_" + method + ".s7diagram"));
    diagram["dt"] = dt;
    for (auto& entry : diagram.at("schema").as_object().at("elements").as_array()) {
        auto& params = entry.as_object().at("params").as_object();
        if (params.if_contains("dt")) params["dt"] = dt;
        if (params.if_contains("sample_rate_hz")) params["sample_rate_hz"] = 1 / dt;
    }
    auto schema = s7tests::build_schema(diagram);
    const auto validation = s7::Validator::validate(schema);
    require(validation.ok(), model + ": validation failed");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_step_ns(std::llround(dt * 1e9));
    s7::ObservationService observer(runtime);
    std::vector<s7::ProbePoint> points;
    for (const auto& value : diagram.at("probes").as_array()) {
        const auto& p = value.as_object();
        points.push_back({std::string(p.at("element").as_string()), p.at("port").to_number<std::size_t>()});
    }
    const bool rc = model.starts_with("rc_");
    const double duration = model == "rlc_resonance" ? 30 : 10;
    const auto steps = static_cast<std::size_t>(std::llround(duration / dt));
    double error = 0;
    const double w = std::sqrt(3.75);
    for (std::size_t k = 1; k <= steps; ++k) {
        runtime.step();
        const auto snapshot = observer.snapshot(points);
        require(snapshot.model_time_ns == static_cast<std::int64_t>(std::llround(k * dt * 1e9)),
                model + ": model-time mismatch");
        const double t = (k - 1) * dt;
        const double decay = std::exp(-t / 2);
        const double free = decay * (std::cos(w*t) + .5/w*std::sin(w*t));
        double x, v = 0;
        if (rc) x = model == "rc_charge" ? 1-std::exp(-t) : std::exp(-t);
        else if (model == "mass_spring_damper") {
            x = free;
            v = -4/w*decay*std::sin(w*t);
        } else if (model == "rlc_step") {
            x = (1-free)/4;
            v = decay*std::sin(w*t)/w;
        } else {
            x = -.5*std::cos(2*t) + .5*free;
            v = std::sin(2*t) - 2/w*decay*std::sin(w*t);
        }
        auto compare = [&](std::size_t i, double reference) {
            const double actual = snapshot.samples[i].to_display_double();
            require(std::isfinite(actual), model + ": nonfinite state");
            if (k == 1) require(std::abs(actual-reference) < 1e-12, model + ": initial state mismatch");
            error = std::max(error, std::abs(actual-reference));
        };
        compare(0, x);
        if (!rc) compare(1, v);
        if (model.starts_with("rlc_")) compare(2, 4*x);
    }
    require(error < .05, model + ": exceeded absolute error budget");
    return error;
}
}

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for (const std::string model : {"rc_charge", "rc_discharge", "rlc_step", "rlc_resonance", "mass_spring_damper"}) {
        for (const std::string method : {"euler", "symplectic"}) {
            const double coarse = check(model, method, .001);
            const double fine = check(model, method, .0005);
            require(coarse/fine > 1.8 && coarse/fine < 2.2, model + ": first-order refinement failed");
            std::cout << model << ' ' << method << ": max errors " << coarse << ", " << fine << '\n';
        }
    }
    return 0;
} catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
