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

    SignalType type{SignalType::DOUBLE};
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
                        type_ == SignalType::COMPLEX
                            ? static_cast<void*>(&state_->complex)
                            : static_cast<void*>(&state_->scalar));
    }

    void step() override {}

private:
    SignalType type_;
    std::shared_ptr<ExposedInputState> state_;
};

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

void Schema::bind_inputs(std::span<const InputBinding> bindings)
{
    Element::bind_inputs(bindings);
    ensure_compiled_runtime();
}

void Schema::step() {
    ensure_compiled_runtime();

    for (std::size_t index = 0; index < exposed_inputs_.size(); ++index) {
        const auto& input = exposed_inputs_[index];
        const auto& state = std::static_pointer_cast<ExposedInputState>(exposed_input_states_[index]);
        if (input.type == SignalType::COMPLEX) {
            state->complex = read_input<ComplexValue>(index);
        } else {
            state->scalar = read_input<RealValue>(index);
        }
    }

    compiled_runtime_->step();

    for (std::size_t index = 0; index < exposed_outputs_.size(); ++index) {
        const auto& output = exposed_outputs_[index];
        auto& value = exposed_output_values_[index];
        if (output.type == SignalType::COMPLEX) {
            value.complex = compiled_runtime_->complex_output_of(output.from_element, output.from_port);
        } else {
            value.scalar = compiled_runtime_->real_output_of(output.from_element, output.from_port);
        }
    }
}

void Schema::add_element(std::string id, ElementFactory factory) {
    elements_.push_back(ElementSpec{std::move(id), std::move(factory)});
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
                    type == SignalType::COMPLEX
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

void Schema::invalidate_compiled_runtime()
{
    compiled_runtime_.reset();
    exposed_input_states_.clear();
}

void Schema::ensure_compiled_runtime()
{
    if (compiled_runtime_) {
        return;
    }

    Schema executable{name_};
    for (const auto& spec : elements_) {
        executable.add_element(spec.id, spec.factory);
    }
    for (const auto& connection : connections_) {
        executable.connect(connection.from_element, connection.from_port, connection.to_element, connection.to_port);
    }

    exposed_input_states_.reserve(exposed_inputs_.size());
    for (std::size_t index = 0; index < exposed_inputs_.size(); ++index) {
        const auto& input = exposed_inputs_[index];
        auto state = std::make_shared<ExposedInputState>(input.type);
        const std::string proxyId = "__schema_input_" + std::to_string(index);
        executable.add_element(proxyId, [type = input.type, state] {
            return std::make_unique<SchemaInputProxy>(type, state);
        });
        executable.connect(proxyId, 0, input.to_element, input.to_port);
        exposed_input_states_.push_back(std::move(state));
    }

    auto graph = Compiler::compile(executable, random_uuid_string(), random_uuid_string());
    auto runtime = std::make_unique<NodeRuntime>(std::move(graph));

    for (const auto& output : exposed_outputs_) {
        const auto internalType = runtime->output_type_of(output.from_element, output.from_port);
        if (internalType != output.type) {
            throw std::runtime_error("Exposed output '" + output.name +
                                     "' type does not match internal source port type.");
        }
    }

    compiled_runtime_ = std::move(runtime);
}

bool ValidationResult::ok() const {
    return errors.empty();
}

} // namespace s7

