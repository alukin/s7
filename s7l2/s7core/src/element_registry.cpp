#include "s7/element_registry.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <limits>

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
        if (d < std::numeric_limits<int64_t>::min() || d > std::numeric_limits<int64_t>::max()) {
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

ElementSpec make_element_spec(std::string id, const ElementDescriptor& descriptor, boost::json::object params) {
    // The spec keeps its own copy: registry storage may move when more
    // elements are registered.
    auto shared = std::make_shared<const ElementDescriptor>(descriptor);
    const auto& metadata = shared->metadata();
    if (metadata.type_variables.empty()) {
        return ElementSpec{std::move(id),
                           [shared, params = std::move(params)] {
                               return shared->create(ElementCreationContext{shared->metadata(), params});
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
            resolved.pinned = parse_signal_type(std::string_view(pin->get_string()));
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
    generic.create = [shared, alias, params = std::move(params)](const std::span<const SignalType> types) {
        std::vector<SignalType> effective(types.begin(), types.end());
        for (std::size_t index = 0; index < effective.size(); ++index) {
            effective[index] = types[alias[index]];
        }
        return shared->create(ElementCreationContext{shared->metadata(), params, effective});
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

void ElementRegistry::clear() {
    descriptors_.clear();
    name_to_index_.clear();
}

} // namespace s7

