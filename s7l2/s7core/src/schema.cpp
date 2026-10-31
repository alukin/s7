// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/core.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <memory>
#include <stdexcept>
#include <utility>

namespace s7 {

namespace {

struct ExposedInputState {
    explicit ExposedInputState(const SignalType t)
        : type(t)
    {
    }

    SignalType type{SignalType::F64};
    RealValue scalar{};
    ComplexValue complex{};
};

class SchemaInputProxy final : public Element
{
public:
    SchemaInputProxy(const SignalType type, std::shared_ptr<ExposedInputState> state)
        : Element("SchemaInputProxy")
        , type_(type)
        , state_(std::move(state))
    {
        if (!state_) {
            throw std::invalid_argument("SchemaInputProxy requires a non-null state handle.");
        }
        register_output(0,
                        port_names::out,
                        type_,
                        type_ == SignalType::C64
                            ? static_cast<void*>(&state_->complex)
                            : static_cast<void*>(&state_->scalar));
    }

    void step() override {}

private:
    SignalType type_;
    std::shared_ptr<ExposedInputState> state_;
};

// Exposed ports of a nested schema are backed by F64 or C64 storage
// (ExposedInputState, ExposedOutputValue), so no other type may cross the
// boundary of a schema used as an element. A top-level schema's exposed ports
// are only boundary metadata and accept any type.
void require_exposable_type(const char* direction, const std::string& name, const SignalType type)
{
    if (type != SignalType::F64 && type != SignalType::C64) {
        throw std::invalid_argument(std::string("Schema exposed ") + direction + " '" + name + "' has type " +
                                    std::string(signal_type_name(type)) +
                                    "; nested schemas expose only F64 and C64 ports.");
    }
}

auto random_uuid_string() -> std::string
{
    return boost::uuids::to_string(boost::uuids::random_generator()());
}

} // namespace

Schema::Schema(std::string name)
    : Element("Schema", true)
    , name_(std::move(name))
{
}

Schema::~Schema() = default;
Schema::Schema(Schema&&) = default;
Schema& Schema::operator=(Schema&&) noexcept = default;

void Schema::bind_inputs(std::span<const InputBinding> bindings)
{
    Element::bind_inputs(bindings);
    bound_input_count_ = bindings.size();
    ensure_compiled_runtime();
}

void Schema::step() {
    ensure_compiled_runtime();

    for (std::size_t index = 0; index < exposed_inputs_.size(); ++index) {
        const auto& input = exposed_inputs_[index];
        // Borrow the state; a shared_ptr copy would add two atomic updates per input.
        auto* const state = static_cast<ExposedInputState*>(exposed_input_states_[index].get());
        if (input.type == SignalType::C64) {
            state->complex = read_input<ComplexValue>(index);
        } else {
            state->scalar = read_input<RealValue>(index);
        }
    }

    compiled_runtime_->step();

    for (std::size_t index = 0; index < exposed_output_sources_.size(); ++index) {
        auto& value = exposed_output_values_[index];
        const auto source = exposed_output_sources_[index];
        if (value.type == SignalType::C64) {
            value.complex = bound_value<SignalType::C64>(source);
        } else {
            value.scalar = bound_value<SignalType::F64>(source);
        }
    }
}

void Schema::add_element(std::string id, ElementFactory factory) {
    add_element(ElementSpec{std::move(id), std::move(factory), {}});
}

void Schema::add_element(std::string id, GenericElement element) {
    add_element(ElementSpec{std::move(id), {}, std::move(element)});
}

void Schema::add_element(ElementSpec spec) {
    elements_.push_back(std::move(spec));
    invalidate_compiled_runtime();
}

void Schema::connect(std::string from_element,
                     std::size_t from_port,
                     std::string to_element,
                     std::size_t to_port) {
    connections_.push_back(Connection{std::move(from_element), from_port, std::move(to_element), to_port});
    invalidate_compiled_runtime();
}

void Schema::expose_input(std::string name,
                          const SignalType type,
                          std::string to_element,
                          const std::size_t to_port)
{
    if (name.empty()) {
        throw std::invalid_argument("Schema exposed input name must not be empty.");
    }

    for (const auto& existing : exposed_inputs_) {
        if (existing.name == name) {
            throw std::invalid_argument("Schema exposed input name is duplicated: '" + name + "'.");
        }
    }

    const std::size_t portIndex = exposed_inputs_.size();
    exposed_inputs_.push_back(ExposedInput{std::move(name), type, std::move(to_element), to_port});
    register_input(portIndex, exposed_inputs_.back().name, type, nullptr);
    invalidate_compiled_runtime();
}

void Schema::expose_output(std::string name,
                           const SignalType type,
                           std::string from_element,
                           const std::size_t from_port)
{
    if (name.empty()) {
        throw std::invalid_argument("Schema exposed output name must not be empty.");
    }

    for (const auto& existing : exposed_outputs_) {
        if (existing.name == name) {
            throw std::invalid_argument("Schema exposed output name is duplicated: '" + name + "'.");
        }
    }

    exposed_outputs_.push_back(ExposedOutput{std::move(name), type, std::move(from_element), from_port});
    exposed_output_values_.push_back(ExposedOutputValue{type, RealValue{}, ComplexValue{}});
    const std::size_t portIndex = exposed_outputs_.size() - 1;
    auto& storage = exposed_output_values_.back();
    register_output(portIndex,
                    exposed_outputs_.back().name,
                    type,
                    type == SignalType::C64
                        ? static_cast<void*>(&storage.complex)
                        : static_cast<void*>(&storage.scalar));
    invalidate_compiled_runtime();
}

const std::string& Schema::name() const {
    return name_;
}

const std::vector<ElementSpec>& Schema::elements() const {
    return elements_;
}

const std::vector<Connection>& Schema::connections() const {
    return connections_;
}

void Schema::set_rate(const Rate rate) {
    rate_ = rate;
    invalidate_compiled_runtime();
}

std::optional<Rate> Schema::rate() const {
    return rate_;
}

void Schema::invalidate_compiled_runtime()
{
    exposed_output_sources_.clear();
    compiled_runtime_.reset();
    exposed_input_states_.clear();
}

void Schema::ensure_compiled_runtime()
{
    if (compiled_runtime_) {
        return;
    }
    // Runs before a nested schema's first step: from bind_inputs() when a
    // parent graph is compiled, or from step() when used standalone.
    for (const auto& input : exposed_inputs_) require_exposable_type("input", input.name, input.type);
    for (const auto& output : exposed_outputs_) require_exposable_type("output", output.name, output.type);
    // Every exposed-input change invalidates the runtime, so checking here
    // covers inputs exposed after binding and a schema never bound.
    if (bound_input_count_ != exposed_inputs_.size()) {
        throw std::logic_error("Schema '" + name_ + "' has " + std::to_string(exposed_inputs_.size()) +
                               " exposed inputs but " + std::to_string(bound_input_count_) +
                               " input bindings; call bind_inputs() after exposing inputs.");
    }

    Schema executable{name_};
    if (rate_) {
        executable.set_rate(*rate_);
    }
    for (const auto& spec : elements_) {
        executable.add_element(spec);
    }
    for (const auto& connection : connections_) {
        executable.connect(connection.from_element, connection.from_port, connection.to_element, connection.to_port);
    }

    // Built locally and committed with the runtime, so a failed compilation
    // leaves no input state or output binding from a discarded runtime.
    std::vector<std::shared_ptr<void>> inputStates;
    inputStates.reserve(exposed_inputs_.size());
    for (std::size_t index = 0; index < exposed_inputs_.size(); ++index) {
        const auto& input = exposed_inputs_[index];
        auto state = std::make_shared<ExposedInputState>(input.type);
        const std::string proxyId = "__schema_input_" + std::to_string(index);
        executable.add_element(proxyId, [type = input.type, state] {
            return std::make_unique<SchemaInputProxy>(type, state);
        });
        executable.connect(proxyId, 0, input.to_element, input.to_port);
        inputStates.push_back(std::move(state));
    }

    auto graph = Compiler::compile(executable, random_uuid_string(), random_uuid_string());
    if (rate_) {
        for (const auto& [id, element] : graph.elements) {
            if (const auto declared=element->declared_rate(); declared && *declared!=*rate_)
                throw std::invalid_argument("Schema '"+name_+"' rate conflicts with internal element '"+id+"'.");
        }
    }
    auto runtime = std::make_unique<NodeRuntime>(std::move(graph));

    // Resolve each exposed output once. The binding addresses storage of an
    // element owned by runtime, which stays at its heap address until the
    // runtime is destroyed, including across moves of this Schema.
    std::vector<OutputBinding> outputSources;
    outputSources.reserve(exposed_outputs_.size());
    for (const auto& output : exposed_outputs_) {
        const auto internalType = runtime->output_type_of(output.from_element, output.from_port);
        if (internalType != output.type) {
            throw std::runtime_error("Exposed output '" + output.name +
                                     "' type does not match internal source port type.");
        }
        outputSources.push_back(runtime->output_binding_of(output.from_element, output.from_port));
    }

    exposed_input_states_ = std::move(inputStates);
    exposed_output_sources_ = std::move(outputSources);
    compiled_runtime_ = std::move(runtime);
}

bool ValidationResult::ok() const {
    return errors.empty();
}

} // namespace s7

