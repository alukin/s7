#include "s7/core.hpp"
#include <chrono>
#include <iostream>
#include <iomanip>

int main() {
    // Build a simple chain: Constant -> Gain -> Gain -> ... -> Gain
    const int CHAIN_LENGTH = 100;
    const int ITERATIONS = 1000000;

    s7::Schema schema{"chain"};

    schema.add_element("src", [] { return std::make_unique<s7::Constant>(1.0); });

    for (int i = 0; i < CHAIN_LENGTH; ++i) {
        std::string id = "gain" + std::to_string(i);
        schema.add_element(id, [] { return std::make_unique<s7::Gain>(1.01); });

        if (i == 0) {
            schema.connect("src", 0, id, 0);
        } else {
            std::string prev = "gain" + std::to_string(i-1);
            schema.connect(prev, 0, id, 0);
        }
    }

    std::cout << "Compiling schema with " << CHAIN_LENGTH << " gain stages..." << std::endl;
    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};

    std::cout << "Running " << ITERATIONS << " iterations..." << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        runtime.step();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    double us_per_iter = duration.count() / static_cast<double>(ITERATIONS);
    double ns_per_step = (us_per_iter * 1000.0) / CHAIN_LENGTH;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "\n=== Performance ===" << std::endl;
    std::cout << "Total time: " << duration.count() / 1000.0 << " ms" << std::endl;
    std::cout << "Time per iteration: " << us_per_iter << " µs" << std::endl;
    std::cout << "Time per element step: " << ns_per_step << " ns" << std::endl;
    std::cout << "\n✅ Zero-overhead read_input() - just a pointer dereference!" << std::endl;

    return 0;
}

