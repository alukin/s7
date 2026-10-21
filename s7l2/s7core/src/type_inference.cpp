// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/type_inference.hpp"

#include "s7/schema.hpp"

#include <stdexcept>
#include <unordered_map>

namespace s7 {

std::vector<SignalType> SignalTypeSet::types() const
{
    std::vector<SignalType> result;
    for (const auto type : kAllSignalTypes) {
        if (contains(type)) {
            result.push_back(type);
        }
    }
    return result;
}

std::string SignalTypeSet::to_string() const
{
    if (*this == all()) {
        return "any type";
    }
    std::string text = "{";
    for (const auto type : types()) {
        text += text.size() > 1 ? ", " : "";
        text += signal_type_name(type);
    }
    return text + "}";
}

PortTypeRule PortTypeRule::parse(const std::string_view rule, const std::span<const std::string> variable_names)
{
    auto variable_index = [&](const std::string_view name) {
        for (std::size_t index = 0; index < variable_names.size(); ++index) {
            if (variable_names[index] == name) {
                return index;
            }
        }
        throw std::invalid_argument("Port type rule '" + std::string(rule) + "' names an unknown type variable.");
    };
    auto argument = [&](const std::string_view prefix) -> std::optional<std::string_view> {
        if (rule.starts_with(prefix) && rule.ends_with(")")) {
            return rule.substr(prefix.size(), rule.size() - prefix.size() - 1);
        }
        return std::nullopt;
    };
    if (const auto name = argument("component(")) {
        return {Kind::ComponentOf, SignalType::F64, variable_index(*name)};
    }
    if (const auto name = argument("complex(")) {
        return {Kind::ComplexOf, SignalType::F64, variable_index(*name)};
    }
    return of_variable(variable_index(rule));
}

namespace {

// Image of a variable's set under a port rule.
SignalTypeSet apply_rule(const PortTypeRule& rule, const SignalTypeSet variable)
{
    switch (rule.kind) {
        case PortTypeRule::Kind::Fixed:
            return {rule.fixed};
        case PortTypeRule::Kind::Variable:
            return variable;
        case PortTypeRule::Kind::ComponentOf: {
            SignalTypeSet image;
            for (const auto type : variable.types()) {
                if (is_complex_signal_type(type)) {
                    image.insert(component_signal_type(type));
                }
            }
            return image;
        }
        case PortTypeRule::Kind::ComplexOf: {
            SignalTypeSet image;
            for (const auto type : variable.types()) {
                if (const auto complex = complex_signal_type_of(type); complex && !is_complex_signal_type(type)) {
                    image.insert(*complex);
                }
            }
            return image;
        }
    }
    return {};
}

// The variable types whose image under the rule lies in `allowed_image`.
SignalTypeSet preimage(const PortTypeRule& rule, const SignalTypeSet variable, const SignalTypeSet allowed_image)
{
    SignalTypeSet result;
    for (const auto type : variable.types()) {
        if (!apply_rule(rule, {type}).intersect(allowed_image).empty()) {
            result.insert(type);
        }
    }
    return result;
}

struct ElementTypes {
    const ElementSpec* spec{};
    bool generic{};
    std::vector<SignalTypeSet> domains;          // per type variable
    std::vector<PortTypeRule> inputs;            // per actual port
    std::vector<PortTypeRule> outputs;
    std::unique_ptr<Element> probe; // concrete elements: the instance their ports came from
};

std::string port_label(const std::string& element, const char* direction, const std::size_t port)
{
    return "'" + element + "' " + direction + " " + std::to_string(port);
}

class Solver {
public:
    explicit Solver(const Schema& schema) : schema_(schema) {}

    ResolvedTypes run()
    {
        collect();
        if (!result_.errors.empty()) {
            return std::move(result_);
        }
        propagate();
        // Fix open variables one at a time, in declaration order.
        for (auto& element : elements_) {
            for (std::size_t var = 0; var < element.domains.size() && result_.errors.empty(); ++var) {
                if (element.domains[var].size() <= 1) {
                    continue;
                }
                const auto& domain = element.domains[var];
                if (domain.contains(SignalType::F64)) {
                    element.domains[var] = {SignalType::F64};
                } else if (domain.contains(SignalType::C64)) {
                    element.domains[var] = {SignalType::C64};
                } else {
                    result_.errors.push_back("The type of '" + element.spec->id + "' (" +
                                             element.spec->generic.variables[var].name + ") cannot be determined; it may be " +
                                             domain.to_string() + ". Pin it with its type parameter.");
                    break;
                }
                propagate();
            }
        }
        if (result_.errors.empty()) {
            build_factories();
        }
        return std::move(result_);
    }

private:
    void collect()
    {
        for (const auto& spec : schema_.elements()) {
            ElementTypes element;
            element.spec = &spec;
            element.generic = static_cast<bool>(spec.generic.create);
            try {
                if (element.generic) {
                    collect_generic(spec, element);
                } else {
                    collect_concrete(spec, element);
                }
            } catch (const std::exception& ex) {
                const std::string message = ex.what();
                const auto prefix = "Element '" + spec.id + "'";
                result_.errors.push_back(message.starts_with(prefix) ? message : prefix + ": " + message);
            }
            index_[spec.id] = elements_.size();
            elements_.push_back(std::move(element));
        }
    }

    void collect_concrete(const ElementSpec& spec, ElementTypes& element)
    {
        if (!spec.factory) {
            throw std::invalid_argument("element has no factory.");
        }
        auto probe = spec.factory();
        if (!probe) {
            throw std::invalid_argument("element factory returned null.");
        }
        for (const auto& port : probe->input_ports()) {
            element.inputs.push_back(PortTypeRule::of_fixed(port.type));
        }
        for (const auto& port : probe->output_ports()) {
            element.outputs.push_back(PortTypeRule::of_fixed(port.type));
        }
        element.probe = std::move(probe);
    }

    void collect_generic(const ElementSpec& spec, ElementTypes& element)
    {
        const auto& generic = spec.generic;
        std::vector<SignalType> probe_types;
        for (const auto& variable : generic.variables) {
            auto domain = variable.allowed;
            if (variable.pinned) {
                if (!domain.contains(*variable.pinned)) {
                    throw std::invalid_argument("type " + std::string(signal_type_name(*variable.pinned)) +
                                                " is not supported; " + variable.name + " may be " +
                                                variable.allowed.to_string() + ".");
                }
                domain = {*variable.pinned};
            }
            if (domain.empty()) {
                throw std::invalid_argument("type variable " + variable.name + " allows no type.");
            }
            element.domains.push_back(domain);
            probe_types.push_back(domain.contains(SignalType::F64) ? SignalType::F64 : domain.types().front());
        }
        auto rule_for = [](const std::vector<PortTypeRule>& rules, const std::size_t port, const char* kind) {
            if (rules.empty()) {
                throw std::invalid_argument(std::string("generic element has ") + kind + " ports but no " + kind + " type rules.");
            }
            return rules[std::min(port, rules.size() - 1)];
        };
        if (generic.input_count) {
            // Ports known from the metadata: the element is created only once,
            // with its resolved types.
            for (std::size_t port = 0; port < *generic.input_count; ++port) {
                element.inputs.push_back(rule_for(generic.input_rules, port, "input"));
            }
            element.outputs = generic.output_rules;
            return;
        }
        // The probe gives the actual port count (variadic elements). If the
        // default probe types are invalid together (an accumulator pinned to
        // I32 cannot take F64 input), try each type of each variable.
        std::unique_ptr<Element> probe;
        std::string first_error;
        auto try_probe = [&](const std::vector<SignalType>& types) {
            try {
                probe = generic.create(types);
            } catch (const std::exception& ex) {
                if (first_error.empty()) {
                    first_error = ex.what();
                }
            }
            return probe != nullptr;
        };
        if (!try_probe(probe_types)) {
            for (std::size_t var = 0; var < element.domains.size() && !probe; ++var) {
                for (const auto type : element.domains[var].types()) {
                    auto types = probe_types;
                    types[var] = type;
                    if (try_probe(types)) {
                        break;
                    }
                }
            }
        }
        if (!probe) {
            throw std::invalid_argument(first_error.empty() ? "element factory returned null." : first_error);
        }
        for (std::size_t port = 0; port < probe->input_count(); ++port) {
            element.inputs.push_back(rule_for(generic.input_rules, port, "input"));
        }
        for (std::size_t port = 0; port < probe->output_count(); ++port) {
            element.outputs.push_back(rule_for(generic.output_rules, port, "output"));
        }
    }

    [[nodiscard]] SignalTypeSet image(const ElementTypes& element, const PortTypeRule& rule) const
    {
        if (rule.kind == PortTypeRule::Kind::Fixed) {
            return {rule.fixed};
        }
        return apply_rule(rule, element.domains[rule.variable]);
    }

    // Narrows the side's variable so that its port can take only `allowed`.
    bool restrict(ElementTypes& element, const PortTypeRule& rule, const SignalTypeSet allowed)
    {
        if (rule.kind == PortTypeRule::Kind::Fixed) {
            return false;
        }
        auto& domain = element.domains[rule.variable];
        const auto narrowed = preimage(rule, domain, allowed);
        if (narrowed == domain) {
            return false;
        }
        domain = narrowed;
        return true;
    }

    void propagate()
    {
        bool changed = true;
        while (changed && result_.errors.empty()) {
            changed = false;
            for (const auto& connection : schema_.connections()) {
                const auto from_it = index_.find(connection.from_element);
                const auto to_it = index_.find(connection.to_element);
                if (from_it == index_.end() || to_it == index_.end()) {
                    continue; // reported by validation
                }
                auto& from = elements_[from_it->second];
                auto& to = elements_[to_it->second];
                if (connection.from_port >= from.outputs.size() || connection.to_port >= to.inputs.size()) {
                    continue; // reported by validation
                }
                const auto& from_rule = from.outputs[connection.from_port];
                const auto& to_rule = to.inputs[connection.to_port];
                const auto from_types = image(from, from_rule);
                const auto to_types = image(to, to_rule);
                const auto common = from_types.intersect(to_types);
                if (common.empty()) {
                    result_.errors.push_back("Type conflict: " + port_label(connection.from_element, "output", connection.from_port) +
                                             " has type " + from_types.to_string() + " but " +
                                             port_label(connection.to_element, "input", connection.to_port) + " requires " +
                                             to_types.to_string() + ". Insert a Convert element.");
                    return;
                }
                changed |= restrict(from, from_rule, common);
                changed |= restrict(to, to_rule, common);
            }
        }
    }

    void build_factories()
    {
        for (auto& element : elements_) {
            result_.instances.push_back(std::move(element.probe));
            std::vector<SignalType> types;
            for (const auto& domain : element.domains) {
                types.push_back(*domain.single());
            }
            if (element.generic) {
                result_.factories.push_back([create = element.spec->generic.create, types] { return create(types); });
            } else {
                result_.factories.push_back(element.spec->factory);
            }
            result_.variables.push_back(std::move(types));
        }
    }

    const Schema& schema_;
    std::vector<ElementTypes> elements_;
    std::unordered_map<std::string, std::size_t> index_;
    ResolvedTypes result_;
};

} // namespace

ResolvedTypes TypeResolver::resolve(const Schema& schema)
{
    return Solver(schema).run();
}

} // namespace s7
