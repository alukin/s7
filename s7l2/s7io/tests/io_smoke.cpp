#include <cassert>
#include <array>
#include <chrono>
#include <complex>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "s7/io.hpp"

namespace {

auto make_temp_file(const std::string& suffix) -> std::filesystem::path {
    const auto stamp = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return std::filesystem::temp_directory_path() / ("s7l2-io-" + stamp + suffix);
}

auto remove_if_exists(const std::filesystem::path& path) -> void {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

auto main() -> int try {
    {
        const auto path = make_temp_file(".bin");
        remove_if_exists(path);

        const s7::RealValue first{1.25};
        const s7::RealValue second{-3.5};
        const std::array<s7::InputBinding, 1> first_binding{s7::bind_input<s7::SignalType::DOUBLE>(first)};
        const std::array<s7::InputBinding, 1> second_binding{s7::bind_input<s7::SignalType::DOUBLE>(second)};

        {
            s7::io::RealCompactBinaryFileSink sink(path);
            sink.bind_inputs({first_binding.data(), first_binding.size()});
            sink.step();
            sink.bind_inputs({second_binding.data(), second_binding.size()});
            sink.step();
        }

        {
            s7::io::RealCompactBinaryFileSource source(path);
            source.step();
            assert(s7::bound_value<s7::SignalType::DOUBLE>(source.outputs()[0]) == first);
            source.step();
            assert(s7::bound_value<s7::SignalType::DOUBLE>(source.outputs()[0]) == second);
        }

        remove_if_exists(path);
    }

    {
        const auto path = make_temp_file(".dat");
        remove_if_exists(path);

        const s7::ComplexValue first{3.0, 4.0};
        const s7::ComplexValue second{-1.5, 0.25};
        const std::array<s7::InputBinding, 1> first_binding{s7::bind_input<s7::SignalType::COMPLEX>(first)};
        const std::array<s7::InputBinding, 1> second_binding{s7::bind_input<s7::SignalType::COMPLEX>(second)};

        {
            s7::io::ComplexGnuplotFileSink sink(path);
            sink.bind_inputs({first_binding.data(), first_binding.size()});
            sink.step();
            sink.bind_inputs({second_binding.data(), second_binding.size()});
            sink.step();
        }

        {
            s7::io::ComplexGnuplotFileSource source(path);
            source.step();
            assert(s7::bound_value<s7::SignalType::COMPLEX>(source.outputs()[0]) == first);
            source.step();
            assert(s7::bound_value<s7::SignalType::COMPLEX>(source.outputs()[0]) == second);
        }

        remove_if_exists(path);
    }

    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7tests io: fatal error: " << ex.what() << '\n';
    return 1;
} catch (...) {
    std::cerr << "s7tests io: unknown fatal error\n";
    return 1;
}

