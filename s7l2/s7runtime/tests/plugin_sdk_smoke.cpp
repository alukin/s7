// Loads a third-party plugin built out of tree against the installed SDK
// (tools/test_third_party_plugin.sh builds s7spectral) and runs its element.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <boost/json.hpp>

#include <s7/builtin_element_registry.hpp>
#include <s7/core.hpp>
#include <s7/element_registry.hpp>

#include "s7/plugin_loader.hpp"

namespace {

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

void add(s7::Schema& schema, const std::string& id, const std::string& type, boost::json::object params)
{
    const auto* descriptor = s7::ElementRegistry::instance().find(type);
    expect(descriptor != nullptr, "element type registered: " + type);
    schema.add_element(s7::make_element_spec(id, *descriptor, std::move(params)));
}

} // namespace

int main(int argc, char** argv)
try {
    if (argc != 2) {
        std::cerr << "usage: s7runtime_plugin_sdk_smoke <plugin.so>\n";
        return 2;
    }
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);

    s7::PluginLoader loader;
    expect(loader.load_plugin(argv[1], registry), "plugin loads");
    const auto info = loader.loaded_plugin_info();
    expect(info.size() == 1 && info[0].name == "s7spectral", "plugin reports its name");

    constexpr double sample_rate = 48000.0;
    constexpr double frequency = 1234.5;
    s7::Schema schema{"third_party_plugin"};
    schema.set_rate(s7::Rate::from_hz(1));
    add(schema, "sine", "AnalogOscillator", {{"amplitude", 0.5}, {"frequency_hz", frequency}, {"sample_rate_hz", sample_rate}});
    add(schema, "fft", "WindowedFFT", {{"fft_size", 1024}, {"hop_size", 256}, {"sample_rate_hz", sample_rate}});
    schema.connect("sine", 0, "fft", 0);

    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    for (int step = 0; step < 4096; ++step) {
        runtime.step();
    }
    const auto peak = runtime.real_output_of("fft", 0);
    const auto amplitude = runtime.real_output_of("fft", 1);
    std::cout << "peak_frequency_hz=" << peak << " peak_amplitude=" << amplitude << '\n';
    expect(std::abs(peak - frequency) < 5.0, "plugin element finds the sine frequency");
    expect(std::abs(amplitude - 0.5) < 0.05, "plugin element estimates the sine amplitude");
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7runtime plugin sdk smoke: " << ex.what() << '\n';
    return 1;
}
