#include "s7/core.hpp"

#include <stdexcept>

namespace s7 {

NodeRuntime::NodeRuntime(ExecutableGraph executable_graph) : graph_(std::move(executable_graph)) {}

void NodeRuntime::step() {
    for (const auto& id : graph_.causal_order) {
        graph_.elements.at(id)->step();
    }

    for (const auto& id : graph_.memory_order) {
        graph_.elements.at(id)->step();
    }

    ++step_count_;
}

const std::string& NodeRuntime::runtime_instance_uuid() const {
    return graph_.runtime_instance_uuid;
}

const std::string& NodeRuntime::schema_instance_uuid() const {
    return graph_.schema_instance_uuid;
}

const std::string& NodeRuntime::element_instance_uuid_of(const std::string& element_id) const {
    const auto it = graph_.id_to_instance_uuid.find(element_id);
    if (it == graph_.id_to_instance_uuid.end()) {
        throw std::out_of_range("Unknown element id requested: '" + element_id + "'.");
    }
    return it->second;
}

const std::string& NodeRuntime::element_id_of_instance_uuid(const std::string& element_instance_uuid) const {
    const auto it = graph_.instance_uuid_to_id.find(element_instance_uuid);
    if (it == graph_.instance_uuid_to_id.end()) {
        throw std::out_of_range("Unknown element instance UUID requested: '" + element_instance_uuid + "'.");
    }
    return it->second;
}

OutputBinding NodeRuntime::output_binding_of(const std::string& element_id, std::size_t port) const {
    const auto element_it = graph_.elements.find(element_id);
    if (element_it == graph_.elements.end()) {
        throw std::out_of_range("Unknown element output requested: '" + element_id + "'.");
    }

    const auto outputs = element_it->second->outputs();
    if (port >= outputs.size()) {
        throw std::out_of_range("Output port out of range for element '" + element_id + "'.");
    }

    return outputs[port];
}

SignalType NodeRuntime::output_type_of(const std::string& element_id, std::size_t port) const {
    const auto element_it = graph_.elements.find(element_id);
    if (element_it == graph_.elements.end()) {
        throw std::out_of_range("Unknown element output requested: '" + element_id + "'.");
    }

    const auto ports = element_it->second->output_ports();
    if (port >= ports.size()) {
        throw std::out_of_range("Output port out of range for element '" + element_id + "'.");
    }

    return ports[port].type;
}

SignalType NodeRuntime::output_type_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return output_type_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

RealValue NodeRuntime::real_output_of(const std::string& element_id, std::size_t port) const {
    const auto output = output_binding_of(element_id, port);
    if (is_complex_signal_type(output_type_of(element_id, port))) {
        throw std::invalid_argument("Requested real output from COMPLEX port of '" + element_id + "'.");
    }

    return *static_cast<const RealValue*>(output);
}

RealValue NodeRuntime::real_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return real_output_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

ComplexValue NodeRuntime::complex_output_of(const std::string& element_id, std::size_t port) const {
    return bound_value<SignalType::COMPLEX>(output_binding_of(element_id, port));
}

ComplexValue NodeRuntime::complex_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return complex_output_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

std::size_t NodeRuntime::step_count() const {
    return step_count_;
}

} // namespace s7
