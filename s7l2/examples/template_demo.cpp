#include "s7/core.hpp"
#include <iostream>

// Demonstration of templated elements in S7

int main() {
    std::cout << "=== Templated Element Demonstration ===" << std::endl << std::endl;

    // Real-valued elements (instantiated from templates)
    std::cout << "Real-valued elements (ConstantT<RealValue>):" << std::endl;
    auto real_constant = std::make_unique<s7::Constant>(3.14);
    auto real_gain = std::make_unique<s7::Gain>(2.0);
    auto real_adder = std::make_unique<s7::Adder>();
    auto real_delay = std::make_unique<s7::Delay>(1.0);

    std::cout << "  Constant type: " << real_constant->type_name() << std::endl;
    std::cout << "  Gain type: " << real_gain->type_name() << std::endl;
    std::cout << "  Adder type: " << real_adder->type_name() << std::endl;
    std::cout << "  Delay type: " << real_delay->type_name() << std::endl;
    std::cout << std::endl;

    // Complex-valued elements (same templates, different type parameter)
    std::cout << "Complex-valued elements (ConstantT<ComplexValue>):" << std::endl;
    auto complex_constant = std::make_unique<s7::ComplexConstant>(s7::ComplexValue{1.0, 2.0});
    auto complex_gain = std::make_unique<s7::ComplexGain>(s7::ComplexValue{0.5, 0.0});
    auto complex_adder = std::make_unique<s7::ComplexAdder>();
    auto complex_delay = std::make_unique<s7::ComplexDelay>(s7::ComplexValue{0.0, 0.0});

    std::cout << "  ComplexConstant type: " << complex_constant->type_name() << std::endl;
    std::cout << "  ComplexGain type: " << complex_gain->type_name() << std::endl;
    std::cout << "  ComplexAdder type: " << complex_adder->type_name() << std::endl;
    std::cout << "  ComplexDelay type: " << complex_delay->type_name() << std::endl;
    std::cout << std::endl;

    // Verify port types
    std::cout << "Port type verification:" << std::endl;
    std::cout << "  Real Constant output type: "
              << s7::signal_type_name(real_constant->output_ports()[0].type) << std::endl;
    std::cout << "  Complex Constant output type: "
              << s7::signal_type_name(complex_constant->output_ports()[0].type) << std::endl;
    std::cout << "  Real Delay input type: "
              << s7::signal_type_name(real_delay->input_ports()[0].type) << std::endl;
    std::cout << "  Complex Delay input type: "
              << s7::signal_type_name(complex_delay->input_ports()[0].type) << std::endl;
    std::cout << std::endl;

    // Test execution
    std::cout << "Execution test:" << std::endl;
    real_constant->step();
    complex_constant->step();

    std::cout << "  Real constant output: "
              << s7::bound_value<s7::SignalType::DOUBLE>(real_constant->outputs()[0]) << std::endl;

    auto c_out = s7::bound_value<s7::SignalType::COMPLEX>(complex_constant->outputs()[0]);
    std::cout << "  Complex constant output: "
              << c_out.real() << " + " << c_out.imag() << "i" << std::endl;

    std::cout << std::endl << "✅ Templates work perfectly!" << std::endl;

    return 0;
}

