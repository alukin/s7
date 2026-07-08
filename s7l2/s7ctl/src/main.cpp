#include <iostream>
#include <memory>

#include "s7/core.hpp"
#include "s7/io.hpp"
#include "s7/net.hpp"
#include "s7/observe.hpp"

namespace {

s7::Schema build_schema() {
    s7::Schema schema{"s7ctl_demo"};
    schema.add_element("unit", [] { return std::make_unique<s7::Constant>(1.0); });
    schema.add_element("sum", [] { return std::make_unique<s7::Adder>(); });
    schema.add_element("memory", [] { return std::make_unique<s7::Delay>(0.0); });

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
        std::cerr << "s7ctl: schema validation failed\n";
        return 1;
    }

    s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
    runtime.step();

    s7::ObservationService observation{runtime};
    s7::IoCatalog io_catalog;
    io_catalog.register_input({"stdin.control", "signal<double>"});
    io_catalog.register_output({"stdout.sum", "signal<double>"});

    s7::NetTopology topology;
    topology.add_node({"local", "127.0.0.1"});
    topology.add_boundary({schema.name(), "sum", "egress"});

    std::cout << "s7ctl starter\n";
    std::cout << "schema=" << schema.name() << '\n';
    std::cout << "sum=" << observation.sample_output("sum").as_real() << '\n';
    std::cout << "io.inputs=" << io_catalog.inputs().size() << '\n';
    std::cout << "io.outputs=" << io_catalog.outputs().size() << '\n';
    std::cout << "net.nodes=" << topology.nodes().size() << '\n';
    std::cout << "net.boundaries=" << topology.boundaries().size() << '\n';

    return 0;
}

