// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The saved diagram is the system under test. Oracles below use finite sums
// and trigonometric identities, never a second S7 graph or FIR implementation.
#include "diagram_harness.hpp"
#include <s7/schema_library.hpp>

#include <array>
#include <complex>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>

namespace {
constexpr double pi = std::numbers::pi;
constexpr double rate = 300;
constexpr std::size_t steps = 1200;
constexpr double tolerance = 1e-10; // F64 source phase and finite-sum roundoff, not model error.

void require(bool ok, const std::string& message)
{
    if (!ok) throw std::runtime_error("SP06: " + message);
}

boost::json::object& element(boost::json::object& document, const std::string& id)
{
    for (auto& value : document.at("schema").at("elements").as_array())
        if (value.at("id").as_string() == id) return value.as_object();
    throw std::runtime_error("Missing diagram element: " + id);
}

std::unique_ptr<s7::NodeRuntime> compile(const boost::json::object& document)
{
    // Use the production schema loader, including embedded schema support.
    auto schema = s7::SchemaLibraryLoader::build_schema(document.at("schema"));
    const auto model_rate = s7::Rate::from_seconds(document.at("dt").to_number<double>());
    schema.set_rate(model_rate);
    const auto validation = s7::Validator::validate(schema);
    require(validation.ok(), "production-loaded saved graph must validate");
    auto runtime = std::make_unique<s7::NodeRuntime>(s7::Compiler::compile(schema));
    runtime->set_model_time_rate(model_rate);
    return runtime;
}

double input(const std::ptrdiff_t n)
{
    if (n < 0) return 0;
    const double t = static_cast<double>(n) / rate;
    return .25 + std::sin(2*pi*10*t + .2) + .5*std::sin(2*pi*100*t + .7);
}

double convolution(const std::ptrdiff_t n)
{
    return (input(n) + input(n-1) + input(n-2)) / 3;
}

// Independently factored analytic response; sign carries the pi phase flip.
std::complex<double> response(const double omega)
{
    return ((1 + 2*std::cos(omega)) / 3) * std::polar(1.0, -omega);
}

struct Trace { std::vector<double> x, y; };

Trace check_saved(const boost::json::object& document, const std::filesystem::path& trace_path)
{
    require(std::abs(document.at("dt").to_number<double>() - 1/rate) < 1e-18,
            "reference envelope requires the saved 300 Hz rate");
    auto runtime = compile(document);
    s7::ObservationService observer(*runtime);
    const std::vector<s7::ProbePoint> points{{"source", 0}, {"filtered", 0}, {"delay1", 0}, {"delay2", 0}};
    Trace trace;
    std::ofstream file;
    if (!trace_path.empty()) {
        file.open(trace_path);
        require(bool(file), "cannot open requested trace");
        file << std::setprecision(std::numeric_limits<double>::max_digits10)
             << "step model_time_s input_V filtered_V reference_V residual_V\n";
    }
    double worst = 0;
    for (std::size_t n = 0; n < steps; ++n) {
        runtime->step();
        const auto sample = observer.snapshot(points);
        require(sample.step == n+1 && sample.model_time_seconds == static_cast<double>(n+1)/rate,
                "step/sample origin and rational model time");
        require(sample.model_time_ns == std::llround(static_cast<double>(n+1)/rate*1e9),
                "absolute nanosecond timestamp rounding");
        const double x = sample.samples[0].to_display_double(), y = sample.samples[1].to_display_double();
        require(std::isfinite(x) && std::isfinite(y), "finite samples");
        require(std::abs(x-input(n)) < tolerance, "independent two-tone input formula");
        for (int delay = 1; delay <= 2; ++delay)
            require(std::abs(sample.samples[delay+1].to_display_double()-input(static_cast<std::ptrdiff_t>(n)-delay)) < tolerance,
                    "explicit delay startup/publication");
        worst = std::max(worst, std::abs(y-convolution(n)));
        trace.x.push_back(x); trace.y.push_back(y);
        if (file.is_open()) file << n+1 << ' ' << *sample.model_time_seconds << ' ' << x << ' ' << y << ' '
                                 << convolution(n) << ' ' << y-convolution(n) << '\n';
    }
    if (file.is_open()) { file.flush(); require(bool(file), "trace write failed"); }
    require(worst < tolerance, "saved graph versus direct convolution, including first/last samples");
    std::cout << "saved graph: " << steps << " samples, max convolution error=" << worst << '\n';
    return trace;
}

void check_multitone(const Trace& trace)
{
    // 600 samples = 20 low-tone cycles and 200 rejected-tone cycles.
    // Exclude startup, use the same sample origin in both complex coefficients.
    for (const double frequency : {0., 10., 100.}) {
        std::complex<double> x{}, y{};
        for (std::size_t n = 300; n < 900; ++n) {
            const auto basis = std::polar(1.0, -2*pi*frequency*static_cast<double>(n)/rate);
            x += trace.x[n]*basis; y += trace.y[n]*basis;
        }
        require(std::abs(x) > 1, "nonzero input coefficient");
        const auto measured = y/x;
        require(std::abs(measured-response(2*pi*frequency/rate)) < tolerance,
                "complex measured response, magnitude AND phase");
        std::cout << "tone " << frequency << " Hz: H=" << measured << '\n';
    }
}

void check_impulse(boost::json::object document)
{
    auto& source = element(document, "source");
    source["type"] = "ImpulseGenerator";
    // This generator specifies area, so 1/Fs creates a unit-height sample.
    source["params"] = boost::json::object{{"area", 1/rate}, {"start_time", 0}};
    auto& connections = document.at("schema").at("connections").as_array();
    connections.erase(std::remove_if(connections.begin(), connections.end(),
        [](const auto& value) {return value.at("to_element").as_string() == "source";}), connections.end());
    auto runtime = compile(document);
    std::array<double, 8> h{};
    for (std::size_t n = 0; n < h.size(); ++n) {
        runtime->step();
        require(std::abs(runtime->real_output_of("source") - (n == 0 ? 1. : 0.)) < 1e-14, "unit sample input");
        h[n] = runtime->real_output_of("filtered");
        require(std::abs(h[n]-(n < 3 ? 1./3 : 0.)) < 1e-14, "exact three-tap impulse response");
    }
    for (const double omega : {0., pi/15, pi/5, 2*pi/3, pi, 4*pi/3}) {
        std::complex<double> measured{};
        for (std::size_t n = 0; n < h.size(); ++n)
            measured += h[n]*std::polar(1.0, -omega*static_cast<double>(n));
        require(std::abs(measured-response(omega)) < 1e-14, "impulse transform: DC, both zeros, interior, Nyquist");
    }
    std::cout << "impulse: [1/3,1/3,1/3,0,...]; six complex response checks passed\n";
}

void check_failure(boost::json::object document)
{
    // A legal model with the middle tap removed. It must execute, but violates
    // the intended moving-average contract. This teaches a modeling error.
    element(document, "tap1")["params"].as_object()["factor"] = 0;
    auto runtime = compile(document);
    double reference_error = 0, correct_error = 0, mean = 0;
    std::complex<double> rejected{};
    for (std::size_t n = 0; n < steps; ++n) {
        runtime->step();
        const double y = runtime->real_output_of("filtered");
        reference_error = std::max(reference_error, std::abs(y-(input(n)+input(static_cast<std::ptrdiff_t>(n)-2))/3));
        correct_error = std::max(correct_error, std::abs(y-convolution(n)));
        if (n >= 300 && n < 900) {
            mean += y/600;
            rejected += y*std::polar(1.0, -2*pi*100*static_cast<double>(n)/rate)/300.;
        }
    }
    require(reference_error < tolerance, "wrong-tap graph must match its own independent recurrence");
    require(correct_error > .1, "wrong tap must fail the intended moving-average reference");
    require(std::abs(mean-1./6) < tolerance && std::abs(std::abs(rejected)-1./6) < tolerance,
            "wrong tap changes DC and leaves a predicted 100 Hz residual");
    std::cout << "negative teaching case: tap1=0; max intended-reference error=" << correct_error
              << ", mean=" << mean << ", 100 Hz amplitude=" << std::abs(rejected) << '\n';
}
} // namespace

int main(int argc, char** argv) try
{
    std::filesystem::path diagram_path = S7_SP06_DIAGRAM, trace_path;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        require((option == "--diagram" || option == "--trace") && i+1 < argc,
                "usage: s7tests_education_sp06 [--diagram path] [--trace path]");
        (option == "--diagram" ? diagram_path : trace_path) = argv[++i];
    }
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    const auto document = s7tests::read_json_object(diagram_path);
    const auto trace = check_saved(document, trace_path);
    check_multitone(trace);
    check_impulse(document);
    check_failure(document);
    const auto repeat = check_saved(document, {});
    require(trace.x == repeat.x && trace.y == repeat.y, "same-process repeated-run determinism");
    return 0;
} catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
}
