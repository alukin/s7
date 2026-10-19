#ifndef S7CORE_INCLUDE_S7_SCHEMA_HPP
#define S7CORE_INCLUDE_S7_SCHEMA_HPP

#include "s7/element.hpp"
#include "s7/rate.hpp"
#include "s7/type_inference.hpp"

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
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

// An element of a schema: either concrete (factory) or generic, with its
// port types resolved by inference (generic.create set).
struct ElementSpec {
    std::string id;
    ElementFactory factory;
    GenericElement generic;
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
    void add_element(std::string id, GenericElement element);
    void add_element(ElementSpec spec);
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

    // Schema-wide rate seed (doc/S7_Sample_Rate_Model.md section 3): used by
    // RateResolver for every connected set of elements with no rate of its
    // own (no element in it pins one). Unset by default.
    void set_rate(Rate rate);
    [[nodiscard]] std::optional<Rate> rate() const;
    // A nested schema with an explicit rate participates in parent rate validation.
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return rate_; }

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
    std::optional<Rate> rate_;
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
