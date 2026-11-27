// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_SCHEMA_HPP
#define S7CORE_INCLUDE_S7_SCHEMA_HPP

#include "s7/array_descriptor.hpp"
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
struct ElementMetadata;

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
    std::shared_ptr<const ElementMetadata> array_metadata{}; // Declaration-only array ports.
};

class Schema : public Element {
public:
    explicit Schema(std::string name);
    ~Schema();
    // Not noexcept: moving the exposed-port deques may allocate.
    Schema(Schema&&);
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
    struct ExposedPort {
        std::string name;
        SignalType type{SignalType::F64};
        std::string element;
        std::size_t port{};
        std::optional<ArrayDescriptor> array{};
    };
    void expose_input(ExposedPort port);
    void expose_output(ExposedPort port);
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

    // Optional complete order of real direct-feedthrough elements, using
    // flattened path IDs. Compiler checks membership and every dependency;
    // causal-memory publication/staging stays owned by NodeRuntime.
    void set_causal_order(std::vector<std::string> order);
    [[nodiscard]] const std::optional<std::vector<std::string>>& causal_order() const { return causal_order_; }

    // An exposed port: its name and type, and the internal element port it
    // feeds (inputs) or is fed by (outputs).
    [[nodiscard]] const std::deque<ExposedPort>& exposed_inputs() const { return exposed_inputs_; }
    [[nodiscard]] const std::deque<ExposedPort>& exposed_outputs() const { return exposed_outputs_; }
    [[nodiscard]] bool has_array_ports() const;

    // Schema-wide rate seed (doc/S7_Sample_Rate_Model.md section 3): used by
    // RateResolver for every connected set of elements with no rate of its
    // own (no element in it pins one). Unset by default.
    void set_rate(Rate rate);
    [[nodiscard]] std::optional<Rate> rate() const;
    // A nested schema with an explicit rate participates in parent rate validation.
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return rate_; }
    // Without a rate of its own, the parent's resolved rate is what every
    // internal rate is checked against when this schema compiles.
    void adopt_rate(const Rate& rate) override;

private:

    struct ExposedOutputValue {
        SignalType type{SignalType::F64};
        RealValue scalar{};
        ComplexValue complex{};
    };

    void invalidate_compiled_runtime();
    void require_standalone_scalar_ports() const;
    void ensure_compiled_runtime();

    std::string name_;
    std::optional<Rate> rate_;
    std::vector<ElementSpec> elements_;
    std::vector<Connection> connections_;
    std::optional<std::vector<std::string>> causal_order_;
    std::deque<ExposedPort> exposed_inputs_;
    std::deque<ExposedPort> exposed_outputs_;
    std::vector<std::shared_ptr<void>> exposed_input_states_;
    std::deque<ExposedOutputValue> exposed_output_values_;
    // Internal output storage of each exposed output, parallel to
    // exposed_outputs_. Resolved with compiled_runtime_ and pointing into
    // elements it owns, so it is replaced and cleared together with it.
    std::vector<OutputBinding> exposed_output_sources_;
    std::unique_ptr<NodeRuntime> compiled_runtime_;
    // Input bindings given to the last bind_inputs(); step() reads one per
    // exposed input, so exposing an input requires binding again.
    std::size_t bound_input_count_{};
    // Rate the parent graph resolved for this nested schema when it has none
    // of its own; internal elements that pin a rate must match it.
    std::optional<Rate> parent_rate_;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_SCHEMA_HPP
