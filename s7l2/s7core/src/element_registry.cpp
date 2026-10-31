// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/element_registry.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <optional>

namespace s7 {

// ============================================================================
// ElementCreationContext Implementation
// ============================================================================

double ElementCreationContext::get_real(const std::string& name) const {
    if (!has_param(name)) {
        throw std::invalid_argument("Required parameter not found: " + name);
    }

    const auto& value = params.at(name);
    if (value.is_double()) {
        return value.as_double();
    }
    if (value.is_int64()) {
        return static_cast<double>(value.as_int64());
    }
    if (value.is_uint64()) {
        return static_cast<double>(value.as_uint64());
    }

    throw std::invalid_argument("Parameter '" + name + "' is not a real number");
}

int64_t ElementCreationContext::get_int(const std::string& name) const {
    if (!has_param(name)) {
        throw std::invalid_argument("Required parameter not found: " + name);
    }

    const auto& value = params.at(name);
    if (value.is_int64()) {
        return value.as_int64();
    }
    if (value.is_uint64()) {
        auto u = value.as_uint64();
        if (u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
            throw std::invalid_argument("Parameter '" + name + "' exceeds int64 range");
        }
        return static_cast<int64_t>(u);
    }
    if (value.is_double()) {
        auto d = value.as_double();
        // int64 max converts up to 2^63, so compare with the exact bounds; this
        // also rejects NaN, which every ordered comparison fails.
        if (!(d >= -0x1p63 && d < 0x1p63)) {
            throw std::invalid_argument("Parameter '" + name + "' out of int64 range");
        }
        return static_cast<int64_t>(d);
    }

    throw std::invalid_argument("Parameter '" + name + "' is not an integer");
}

std::string ElementCreationContext::get_string(const std::string& name) const {
    if (!has_param(name)) {
        throw std::invalid_argument("Required parameter not found: " + name);
    }

    const auto& value = params.at(name);
    if (!value.is_string()) {
        throw std::invalid_argument("Parameter '" + name + "' is not a string");
    }

    return std::string(value.as_string());
}

bool ElementCreationContext::has_param(const std::string& name) const {
    return params.find(name) != params.end();
}

SignalType ElementCreationContext::type(const std::string_view variable) const {
    for (std::size_t index = 0; index < metadata.type_variables.size(); ++index) {
        if (metadata.type_variables[index].name == variable) {
            if (index >= types.size()) {
                throw std::logic_error("Element '" + metadata.type_name + "' was created without resolved types.");
            }
            return types[index];
        }
    }
    throw std::invalid_argument("Element '" + metadata.type_name + "' has no type variable '" + std::string(variable) + "'.");
}

namespace {

auto is_integer(const boost::json::value& value) -> bool
{
    if (value.is_int64() || value.is_uint64()) {
        return true;
    }
    return value.is_double() && std::isfinite(value.as_double()) && std::trunc(value.as_double()) == value.as_double();
}

// Why a value does not fit its declared parameter; empty if it does.
auto parameter_problem(const ParameterSpec& spec, const boost::json::value& value) -> std::string
{
    const auto in_range = [&spec](const double number) -> std::string {
        if (spec.min_value && number < *spec.min_value) {
            return std::format("must be at least {}", *spec.min_value);
        }
        if (spec.max_value && number > *spec.max_value) {
            return std::format("must be at most {}", *spec.max_value);
        }
        return {};
    };
    if (spec.type == "int") {
        return is_integer(value) ? in_range(value.to_number<double>()) : "must be an integer";
    }
    if (spec.type == "real") {
        if (!value.is_number()) return "must be a number";
        // A JSON literal beyond double's range (1e400) parses as infinity.
        if (!std::isfinite(value.to_number<double>())) return "must be a finite number";
        return in_range(value.to_number<double>());
    }
    if (spec.type == "bool") {
        return value.is_bool() ? "" : "must be true or false";
    }
    if (spec.type == "string") {
        return value.is_string() ? "" : "must be a string";
    }
    if (spec.type == "enum") {
        if (!value.is_string()) {
            return "must be a string";
        }
        if (!spec.enum_values.empty() &&
            std::find(spec.enum_values.begin(), spec.enum_values.end(), std::string_view(value.as_string())) == spec.enum_values.end()) {
            std::string allowed;
            for (const auto& option : spec.enum_values) {
                allowed += (allowed.empty() ? "" : ", ") + option;
            }
            return "must be one of " + allowed;
        }
        return {};
    }
    if (spec.type == "type") {
        if (!value.is_string()) {
            return "must be a type name such as \"F64\"";
        }
        try {
            static_cast<void>(parse_signal_type(std::string_view(value.as_string())));
        } catch (const std::exception& ex) {
            return ex.what();
        }
        return {};
    }
    if (spec.type == "array") {
        return value.is_array() ? "" : "must be an array";
    }
    if (spec.type == "value") {
        // A value of the element's signal type, checked when its type is known.
        return value.is_number() || value.is_string() || value.is_array() || value.is_bool() ? "" : "must be a signal value";
    }
    return {};
}

} // namespace

boost::json::object checked_params(const std::string& id, const ElementMetadata& metadata, const boost::json::object& params)
{
    std::vector<std::string> problems;
    for (const auto& [name, value] : params) {
        const auto spec = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
                                       [&](const ParameterSpec& candidate) { return candidate.name == name; });
        if (spec == metadata.parameters.end()) {
            std::string known;
            for (const auto& parameter : metadata.parameters) {
                known += (known.empty() ? "" : ", ") + parameter.name;
            }
            problems.push_back("unknown parameter '" + std::string(name) + "'" + (known.empty() ? " (it has none)" : " (known: " + known + ")"));
        } else if (const auto problem = parameter_problem(*spec, value); !problem.empty()) {
            problems.push_back("parameter '" + std::string(name) + "' " + problem);
        }
    }
    auto result = params;
    for (const auto& spec : metadata.parameters) {
        if (result.contains(spec.name)) {
            continue;
        }
        if (spec.required) {
            problems.push_back("parameter '" + spec.name + "' is required");
        } else if (!spec.default_value.is_null()) {
            result[spec.name] = spec.default_value;
        }
    }
    if (!problems.empty()) {
        std::string message = "Element '" + id + "' (" + metadata.type_name + "): ";
        for (std::size_t index = 0; index < problems.size(); ++index) {
            message += (index == 0 ? "" : "; ") + problems[index];
        }
        throw std::invalid_argument(message + ".");
    }
    return result;
}

ElementSpec make_element_spec(std::string id, const ElementDescriptor& descriptor, boost::json::object params) {
    // The spec keeps its own copy: registry storage may move when more
    // elements are registered.
    auto shared = std::make_shared<const ElementDescriptor>(descriptor);
    const auto& metadata = shared->metadata();
    if (metadata.type_variables.empty()) {
        return ElementSpec{id,
                           [shared, id, params = std::move(params)] {
                               // Named, not a temporary bound straight into ctx: ElementCreationContext
                               // only holds a reference, and ctx outlives the statement that builds it.
                               const auto checked = checked_params(id, shared->metadata(), params);
                               auto ctx = ElementCreationContext{shared->metadata(), checked};
                               try {
                                   return shared->create(ctx);
                               } catch (const std::exception& ex) {
                                   throw std::invalid_argument("Element '" + id + "' (" + shared->metadata().type_name + "): " + ex.what());
                               }
                           },
                           {}};
    }

    std::vector<std::string> names;
    for (const auto& variable : metadata.type_variables) {
        names.push_back(variable.name);
    }
    auto index_of = [&names, &id](const std::string& name) {
        const auto it = std::find(names.begin(), names.end(), name);
        if (it == names.end()) {
            throw std::invalid_argument("Element '" + id + "': unknown type variable '" + name + "'.");
        }
        return static_cast<std::size_t>(it - names.begin());
    };
    // An unpinned variable with defaults_to is an alias: its ports use the
    // other variable, and the factory receives that variable's type for it.
    std::vector<std::size_t> alias(names.size());
    GenericElement generic;
    for (std::size_t index = 0; index < metadata.type_variables.size(); ++index) {
        const auto& variable = metadata.type_variables[index];
        alias[index] = index;
        TypeVariable resolved{variable.name, {}, std::nullopt};
        for (const auto type : variable.allowed) {
            resolved.allowed.insert(type);
        }
        if (const auto* pin = params.if_contains(variable.pin_parameter)) {
            if (!pin->is_string()) {
                throw std::invalid_argument("Element '" + id + "': parameter '" + variable.pin_parameter +
                                            "' must be a type name such as \"F64\".");
            }
            try {
                resolved.pinned = parse_signal_type(std::string_view(pin->get_string()));
            } catch (const std::invalid_argument& ex) {
                throw std::invalid_argument("Element '" + id + "' (" + metadata.type_name + "): parameter '" +
                                            variable.pin_parameter + "': " + ex.what());
            }
        } else if (!variable.defaults_to.empty()) {
            alias[index] = index_of(variable.defaults_to);
            resolved.allowed = SignalTypeSet::all(); // unused: its ports follow the other variable
        }
        generic.variables.push_back(std::move(resolved));
    }
    auto rule_of = [&names, &alias](const std::string& rule, const SignalType fixed) {
        if (rule.empty()) {
            return PortTypeRule::of_fixed(fixed);
        }
        auto parsed = PortTypeRule::parse(rule, names);
        parsed.variable = alias[parsed.variable];
        return parsed;
    };
    for (const auto& port : metadata.input_ports) {
        generic.input_rules.push_back(rule_of(port.type_rule, port.type));
    }
    if (metadata.variadic_inputs.enabled) {
        generic.input_rules.push_back(rule_of(metadata.variadic_inputs.type_rule, metadata.variadic_inputs.signal_type));
    }
    for (const auto& port : metadata.output_ports) {
        generic.output_rules.push_back(rule_of(port.type_rule, port.type));
    }
    // The port count without creating the element: fixed, or the length of
    // the parameter that sets it.
    if (!metadata.variadic_inputs.enabled) {
        generic.input_count = metadata.input_ports.size();
    } else if (const auto& count = metadata.variadic_inputs.count_parameter; !count.empty()) {
        const boost::json::value* sizing = params.if_contains(count);
        if (!sizing) {
            const auto spec = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
                                           [&](const ParameterSpec& parameter) { return parameter.name == count; });
            sizing = spec != metadata.parameters.end() ? &spec->default_value : nullptr;
        }
        std::optional<std::uint64_t> requested;
        if (sizing && sizing->is_array()) {
            requested = sizing->as_array().size();
        } else if (sizing && sizing->is_int64() && sizing->get_int64() >= 0) {
            requested = static_cast<std::uint64_t>(sizing->get_int64());
        } else if (sizing && sizing->is_uint64()) {
            requested = sizing->get_uint64();
        }
        // Type inference allocates one rule per port before the parameter checks
        // report an oversized count, so cap the ports it sees just past the
        // maximum: still invalid, but bounded for any untrusted count.
        if (requested) {
            generic.input_count = static_cast<std::size_t>(
                std::min<std::uint64_t>(*requested, std::uint64_t{metadata.variadic_inputs.max_count} + 1));
        }
    }
    generic.create = [shared, alias, id, params = std::move(params)](const std::span<const SignalType> types) {
        std::vector<SignalType> effective(types.begin(), types.end());
        for (std::size_t index = 0; index < effective.size(); ++index) {
            effective[index] = types[alias[index]];
        }
        const auto checked = checked_params(id, shared->metadata(), params);
        auto ctx = ElementCreationContext{shared->metadata(), checked, effective};
        try {
            return shared->create(ctx);
        } catch (const std::exception& ex) {
            throw std::invalid_argument("Element '" + id + "' (" + shared->metadata().type_name + "): " + ex.what());
        }
    };
    return ElementSpec{std::move(id), {}, std::move(generic)};
}

// ============================================================================
// ElementDescriptor Implementation
// ============================================================================

ElementDescriptor::ElementDescriptor(
    std::string type_name,
    std::function<std::unique_ptr<Element>(const ElementCreationContext&)> factory,
    const ElementMetadata& metadata
)
    : type_name_(std::move(type_name)),
      factory_(factory),
      metadata_(metadata)
{
    if (!factory_) {
        throw std::invalid_argument("Factory function cannot be null");
    }
}

std::unique_ptr<Element> ElementDescriptor::create(const ElementCreationContext& ctx) const {
    return factory_(ctx);
}

// ============================================================================
// ElementRegistry Implementation
// ============================================================================

ElementRegistry& ElementRegistry::instance() {
    static ElementRegistry registry;
    return registry;
}

void ElementRegistry::register_element(const ElementDescriptor& descriptor) {
    const auto& type_name = descriptor.type_name();

    // Check for duplicate
    if (name_to_index_.find(type_name) != name_to_index_.end()) {
        throw std::invalid_argument("Element type already registered: " + type_name);
    }

    // Add to registry
    size_t index = descriptors_.size();
    name_to_index_[type_name] = index;
    descriptors_.push_back(descriptor);
    ++generation_;
}

const ElementDescriptor* ElementRegistry::find(const std::string& type_name) const {
    auto it = name_to_index_.find(type_name);
    if (it == name_to_index_.end()) {
        return nullptr;
    }
    return &descriptors_[it->second];
}

const std::vector<ElementDescriptor>& ElementRegistry::all_descriptors() const {
    return descriptors_;
}

std::vector<std::string> ElementRegistry::element_types() const {
    std::vector<std::string> types;
    types.reserve(descriptors_.size());
    for (const auto& desc : descriptors_) {
        types.push_back(desc.type_name());
    }
    return types;
}

std::vector<std::string> ElementRegistry::element_types_in_category(
    const std::string& category
) const {
    std::vector<std::string> types;
    for (const auto& desc : descriptors_) {
        if (desc.metadata().category == category) {
            types.push_back(desc.type_name());
        }
    }
    return types;
}

size_t ElementRegistry::element_count() const {
    return descriptors_.size();
}

void ElementRegistry::unregister(const std::vector<std::string>& type_names) {
    std::erase_if(descriptors_, [&](const ElementDescriptor& descriptor) {
        return std::ranges::find(type_names, descriptor.type_name()) != type_names.end();
    });
    name_to_index_.clear();
    for (size_t index = 0; index < descriptors_.size(); ++index) {
        name_to_index_[descriptors_[index].type_name()] = index;
    }
    ++generation_;
}

void ElementRegistry::clear() {
    descriptors_.clear();
    name_to_index_.clear();
    ++generation_;
}

} // namespace s7

