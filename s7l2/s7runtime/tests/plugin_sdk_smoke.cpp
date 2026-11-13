// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Loads a third-party plugin built out of tree against the installed SDK
// (tools/test_third_party_plugin.sh builds s7spectral) and runs its element.
#include <cmath>
#include <filesystem>
#include <fstream>
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
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: s7runtime_plugin_sdk_smoke <plugin.so>\n";
        return 2;
    }
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);

    s7::PluginLoader loader;
    expect(loader.load_plugin(argv[1], registry), "plugin loads");
    const auto info = loader.loaded_plugin_info();
    expect(info.size() == 1, "plugin reports its name");
    if(argc==3){std::ifstream file(argv[2]);expect(file.good(),"JSON example opens");const std::string text((std::istreambuf_iterator<char>(file)),{});const auto root=boost::json::parse(text).as_object();s7::Schema schema("plugin_json_example");schema.set_rate(s7::Rate::from_seconds(root.at("dt").as_double()));
        for(const auto& value:root.at("elements").as_array()){const auto& e=value.as_object();add(schema,std::string(e.at("id").as_string()),std::string(e.at("type").as_string()),e.at("params").as_object());}
        for(const auto& value:root.at("connections").as_array()){const auto& c=value.as_object();schema.connect(std::string(c.at("from_element").as_string()),std::size_t(c.at("from_port").as_int64()),std::string(c.at("to_element").as_string()),std::size_t(c.at("to_port").as_int64()));}
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));for(int i=0;i<1024;++i)runtime.step();expect(runtime.real_output_of("analysis",1)==1,"JSON analyzer update/valid flag");if(info[0].name=="s7spectral")expect(std::abs(runtime.real_output_of("parts",1)+1)<1e-9,"JSON Fourier coefficient amplitude/phase");else expect(std::abs(runtime.complex_output_of("analysis",0))>1,"JSON Morlet response");return 0;}

    if (info[0].name == "s7wavelet") {
        s7::Schema wavelet{"wavelet_plugin"};
        wavelet.set_rate(s7::Rate::from_hz(1024));
        add(wavelet, "sine", "AnalogOscillator", {{"frequency_hz", 32}});
        add(wavelet, "analysis", "WaveletCoefficient", {{"frequency_hz", 32}});
        wavelet.connect("sine", 0, "analysis", 0);
        s7::NodeRuntime runtime{s7::Compiler::compile(wavelet)};
        for (int i=0;i<1024;++i) runtime.step();
        expect(runtime.real_output_of("analysis",1)==1, "wavelet full-support validity");
        expect(runtime.real_output_of("analysis",2)>0, "wavelet explicit center delay");
        expect(std::abs(runtime.complex_output_of("analysis",0))>1, "wavelet responds to matched tone");
        return 0;
    }
    expect(info[0].name == "s7spectral", "spectral plugin name");
    expect(registry.find("FourierCoefficient") != nullptr, "Fourier element registered");

    constexpr double sample_rate = 48000.0;
    constexpr double frequency = 1234.5;
    s7::Schema schema{"third_party_plugin"};
    schema.set_rate(s7::Rate::from_hz(sample_rate)); // the oscillator follows the run's rate
    add(schema, "sine", "AnalogOscillator", {{"amplitude", 0.5}, {"frequency_hz", frequency}});
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
