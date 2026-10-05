#ifndef S7CORE_INCLUDE_S7_SCHEMA_HPP
#define S7CORE_INCLUDE_S7_SCHEMA_HPP

#include "s7/element.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace s7 {

class NodeRuntime;

struct Connection {
    std::string from_element;
    std::size_t from_port{};
    std::string to_element;
    std::size_t to_port{};
};

struct ElementSpec {
    std::string id;
    ElementFactory factory;
};

class Schema : public Element {
public:
    explicit Schema(std::string name);
    ~Schema();
    Schema(Schema&&) noexcept;
    Schema& operator=(Schema&&) noexcept;
    Schema(const Schema&) = delete;
    Schema& operator=(const Schema&) = delete;

    void bind_inputs(std::span<const InputBinding> bindings) override;
    void step() override;

    void add_element(std::string id, ElementFactory factory);
    void connect(std::string from_element,
                 std::size_t from_port,
                 std::string to_element,
                 std::size_t to_port);
    void expose_input(std::string name,
                      SignalType type,
                      std::string to_element,
                      std::size_t to_port);
    void expose_output(std::string name,
                       SignalType type,
                       std::string from_element,
                       std::size_t from_port);

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const std::vector<ElementSpec>& elements() const;
    [[nodiscard]] const std::vector<Connection>& connections() const;

private:
    struct ExposedInput {
        std::string name;
        SignalType type{SignalType::F64};
        std::string to_element;
        std::size_t to_port{};
    };

    struct ExposedOutput {
        std::string name;
        SignalType type{SignalType::F64};
        std::string from_element;
        std::size_t from_port{};
    };

    struct ExposedOutputValue {
        SignalType type{SignalType::F64};
        RealValue scalar{};
        ComplexValue complex{};
    };

    void invalidate_compiled_runtime();
    void ensure_compiled_runtime();

    std::string name_;
    std::vector<ElementSpec> elements_;
    std::vector<Connection> connections_;
    std::deque<ExposedInput> exposed_inputs_;
    std::deque<ExposedOutput> exposed_outputs_;
    std::vector<std::shared_ptr<void>> exposed_input_states_;
    std::deque<ExposedOutputValue> exposed_output_values_;
    std::unique_ptr<NodeRuntime> compiled_runtime_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SCHEMA_HPP
