#include "diagram_harness.hpp"
#include "linear_reference.hpp"
#include <s7/schema_library.hpp>
#include <iostream>

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

double check(const std::string& model, const std::string& method, double dt) {
    auto diagram = s7tests::read_json_object(std::filesystem::path(S7_LINEAR_DIR) /
                                           (model + "_" + method + ".s7diagram"));
    diagram["dt"] = dt;
    s7::configure_schema_discretization(diagram.at("schema"), dt);
    auto schema = s7tests::build_schema(diagram);
    const auto validation = s7::Validator::validate(schema);
    require(validation.ok(), model + ": validation failed");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    runtime.set_model_time_rate(s7::Rate::from_seconds(dt));
    s7::ObservationService observer(runtime);
    std::vector<s7::ProbePoint> points;
    for (const auto& value : diagram.at("probes").as_array()) {
        const auto& p = value.as_object();
        points.push_back({std::string(p.at("element").as_string()), p.at("port").to_number<std::size_t>()});
    }
    const bool rc = model.starts_with("rc_");
    const double duration = s7tests::linear_duration(model);
    const auto steps = static_cast<std::size_t>(std::llround(duration / dt));
    double error = 0;
    for (std::size_t k = 1; k <= steps; ++k) {
        runtime.step();
        const auto snapshot = observer.snapshot(points);
        require(snapshot.step == k && snapshot.model_time_seconds ==
                static_cast<double>(k) / (1 / dt), model + ": step/time mismatch");
        require(snapshot.model_time_ns == static_cast<std::int64_t>(std::llround(k * dt * 1e9)),
                model + ": model-time mismatch");
        const auto expected = s7tests::linear_reference(model, (k - 1) * dt);
        auto compare = [&](std::size_t i, double reference) {
            const double actual = snapshot.samples[i].to_display_double();
            require(std::isfinite(actual), model + ": nonfinite state");
            if (k == 1) require(std::abs(actual-reference) < 1e-12, model + ": initial state mismatch");
            error = std::max(error, std::abs(actual-reference));
        };
        compare(0, expected[0]);
        if (!rc) compare(1, expected[1]);
        if (model.starts_with("rlc_")) compare(2, expected[2]);
    }
    s7tests::check_linear_error(error, model);
    return error;
}
}

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    for (const std::string model : {"rc_charge", "rc_discharge", "rlc_step", "rlc_resonance", "mass_spring_damper"}) {
        for (const std::string method : {"euler", "symplectic"}) {
            const double coarse = check(model, method, .001);
            const double fine = check(model, method, .0005);
            s7tests::check_linear_refinement(coarse, fine, model);
            std::cout << model << ' ' << method << ": max errors " << coarse << ", " << fine << '\n';
        }
    }
    return 0;
} catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
