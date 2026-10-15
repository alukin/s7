#include "s7/core.hpp"

#include <cstring>
#include <stdexcept>

namespace s7 {

NodeRuntime::NodeRuntime(ExecutableGraph executable_graph) : graph_(std::move(executable_graph))
{
    memory_output_ranges_.reserve(graph_.memory_order.size());
    for (const auto& id : graph_.memory_order) {
        const auto& element = graph_.elements.at(id);
        const auto ports = element->output_ports();
        const auto begin = memory_outputs_.size();
        memory_outputs_.reserve(begin + ports.size());
        for (const auto& port : ports) {
            memory_outputs_.push_back(MemoryOutput{
                .value_ptr = port.value_ptr,
                .bytes = signal_type_info(port.type).storage_bytes,
            });
        }
        memory_output_ranges_.push_back(MemoryOutputRange{begin, memory_outputs_.size()});
    }

    for (const auto& id : graph_.causal_order) {
        const auto& element = graph_.elements.at(id);
        for (const auto& port : element->output_ports()) {
            causal_outputs_.push_back(CausalOutput{
                .value_ptr = port.value_ptr,
                .bytes = signal_type_info(port.type).storage_bytes,
            });
        }
    }
}

void NodeRuntime::set_model_time_step_ns(const std::optional<std::int64_t> step_ns) {
    if (step_ns && *step_ns <= 0) {
        throw std::invalid_argument("Model time step must be positive.");
    }
    model_time_step_ns_ = step_ns;
}

std::optional<std::int64_t> NodeRuntime::model_time_ns() const {
    if (!model_time_step_ns_) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(step_count_) * *model_time_step_ns_;
}

std::thread::id NodeRuntime::stepping_thread() const {
    return stepping_thread_.load();
}

void NodeRuntime::step() {
    stepping_thread_.store(std::this_thread::get_id());
    const bool publishing = memory_outputs_staged_;
    if (publishing) {
        // Back up each memory output's pre-publish (previous step's) value
        // in its MemoryOutput::snapshot, reused below only for this purpose:
        // the per-element catch further down overwrites it fresh, after
        // this function either returns normally or has already rethrown.
        for (auto& output : memory_outputs_) {
            std::memcpy(output.snapshot.data(), output.value_ptr, output.bytes);
            std::memcpy(output.value_ptr, output.staged.data(), output.bytes);
        }
        memory_outputs_staged_ = false;
    }

    for (auto& output : causal_outputs_) {
        std::memcpy(output.snapshot.data(), output.value_ptr, output.bytes);
    }
    try {
        for (const auto& id : graph_.causal_order) {
            graph_.elements.at(id)->step();
        }
    } catch (...) {
        // A causal-order element faulted partway through: restore every
        // causal output, and the memory outputs just published above, to
        // their pre-step values, so the step is all-or-nothing instead of a
        // mix of this step's and the previous step's outputs. The staged
        // values survive (only the flag was cleared) so a later successful
        // step() republishes the same values rather than skipping them.
        // This covers only elements' published outputs, not any further
        // private state (e.g. a filter's internal delay line) a causal
        // element may have already advanced before the fault.
        for (auto& output : causal_outputs_) {
            std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
        }
        if (publishing) {
            for (auto& output : memory_outputs_) {
                std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
            }
            memory_outputs_staged_ = true;
        }
        throw;
    }

    // Each causal-memory element computes its next output from this step's
    // inputs; its published output is restored right away so that other
    // causal-memory elements still read this step's values.
    for (std::size_t element_index = 0; element_index < graph_.memory_order.size(); ++element_index) {
        const auto range = memory_output_ranges_[element_index];
        for (std::size_t output_index = range.begin; output_index < range.end; ++output_index) {
            auto& output = memory_outputs_[output_index];
            std::memcpy(output.snapshot.data(), output.value_ptr, output.bytes);
        }

        try {
            graph_.elements.at(graph_.memory_order[element_index])->step();
        } catch (...) {
            for (std::size_t output_index = range.begin; output_index < range.end; ++output_index) {
                auto& output = memory_outputs_[output_index];
                std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
            }
            throw;
        }

        for (std::size_t output_index = range.begin; output_index < range.end; ++output_index) {
            auto& output = memory_outputs_[output_index];
            std::memcpy(output.staged.data(), output.value_ptr, output.bytes);
            std::memcpy(output.value_ptr, output.snapshot.data(), output.bytes);
        }
    }
    memory_outputs_staged_ = !memory_outputs_.empty();

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

void NodeRuntime::require_output_type(const std::string& element_id, const std::size_t port, const SignalType expected) const {
    const auto actual = output_type_of(element_id, port);
    if (actual != expected) {
        throw std::invalid_argument("Output " + std::to_string(port) + " of '" + element_id + "' has type " +
                                    std::string(signal_type_name(actual)) + ", not " +
                                    std::string(signal_type_name(expected)) + ".");
    }
}

RealValue NodeRuntime::real_output_of(const std::string& element_id, std::size_t port) const {
    return output_of<SignalType::F64>(element_id, port);
}

RealValue NodeRuntime::real_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return real_output_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

ComplexValue NodeRuntime::complex_output_of(const std::string& element_id, std::size_t port) const {
    return output_of<SignalType::C64>(element_id, port);
}

ComplexValue NodeRuntime::complex_output_of_instance_uuid(const std::string& element_instance_uuid, std::size_t port) const {
    return complex_output_of(element_id_of_instance_uuid(element_instance_uuid), port);
}

std::size_t NodeRuntime::step_count() const {
    return step_count_;
}

} // namespace s7
