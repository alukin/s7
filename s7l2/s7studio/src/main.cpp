#include <iostream>
#include <memory>

#include "s7/core.hpp"
#include "s7/io.hpp"
#include "s7/net.hpp"
#include "s7/observe.hpp"

namespace {

s7::Schema build_schema() {
    s7::Schema schema{"s7studio_demo"};
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(2.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("memory", [] { return std::make_unique<s7::Delay>(1.0); });

    schema.connect("unit", 0, "sum", 0);
    schema.connect("memory", 0, "sum", 1);
    schema.connect("sum", 0, "memory", 0);
    return schema;
}

} // namespace

int main() {
    const auto schema = build_schema();
    const auto validation = s7::Validator::validate(schema);
    if (!validation.ok()) {
        std::cerr << "s7studio: schema validation failed\n";
        return 1;
    }

    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    s7::ObservationService observation{runtime};

    s7::IoCatalog io_catalog;
    io_catalog.register_output({"scope.sum", "signal<double>"});

    s7::NetTopology topology;
    topology.add_node({"local", "127.0.0.1"});
    topology.add_boundary({schema.name(), "sum", "observation"});

    runtime.step();
    runtime.step();

    const auto samples = observation.sample_outputs({
        {"memory", 0},
        {"sum", 0},
    });

    std::cout << "s7studio starter\n";
    std::cout << "role=runtime client\n";
    std::cout << "observed.step=" << runtime.step_count() << '\n';
    for (const auto& sample : samples) {
        std::cout << sample.element_id << '[' << sample.port << "]=";
        if (sample.is_complex()) {
            std::cout << sample.as_complex();
        } else {
            std::cout << sample.as_real();
        }
        std::cout << '\n';
    }
    std::cout << "observation.outputs=" << io_catalog.outputs().size() << '\n';
    std::cout << "net.nodes=" << topology.nodes().size() << '\n';

    return 0;
}


