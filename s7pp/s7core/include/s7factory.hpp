//
// s7factory.hpp — element type registry and factory utilities
// An ElementRegistry maps type-name strings to factory functions so that
// a SchemaBuilder can instantiate elements from JSON without depending on
// compile-time knowledge of every available type.
//

#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "s7.hpp"

namespace s7 {

    // Number is the factory/netlist layer's scalar: Real or Complex.
    // It is NOT used on signal pins — pins are typed via TypedInput<T>/TypedOutput<T>.
    using Number     = std::variant<Real, Complex>;
    using NumberList = std::vector<Number>;
    using ParamValue = std::variant<Real, Complex, std::string>;
    using ParamMap   = std::unordered_map<std::string, ParamValue>;

    using ElementFactoryFn = std::function<ElementPtr(const std::string& id, const ParamMap& params)>;

    // -------------------------------------------------------------------------
    // TypeDescriptor — metadata about a registered element type.
    // Used to populate the GUI palette and auto-create items.
    // -------------------------------------------------------------------------
    struct TypeDescriptor {
        std::string type_name;
        std::string category;            ///< e.g. "Sources", "Math", "Filters", "IO", "Sinks"
        std::string description;

        int  default_input_count  = 1;   ///< Shown to the user as the default port count
        int  default_output_count = 1;
        bool inputs_configurable  = false; ///< User can choose N at placement time
        bool outputs_configurable = false;

        ParamMap default_params;

        /// When true, this type is not shown in the GUI palette.
        /// (Use for engine-internal types like "Probe".)
        bool hidden = false;
    };

    // -------------------------------------------------------------------------
    // IElementPropertyProvider — interface for exposing configurable properties.
    // -------------------------------------------------------------------------
    class IElementPropertyProvider {
    public:
        virtual ~IElementPropertyProvider() = default;
        virtual ParamMap getProperties() const = 0;
        virtual void setProperties(const ParamMap& params) = 0;
    };

    class ElementRegistry {
    public:
        static ElementRegistry& instance()
        {
            static ElementRegistry registry;
            return registry;
        }

        /// Register a type with full metadata.
        void registerType(TypeDescriptor desc, ElementFactoryFn factory)
        {
            factories_[desc.type_name]    = std::move(factory);
            descriptors_[desc.type_name]  = std::move(desc);
        }

        /// Register a type without metadata (backward-compatible overload).
        void registerType(const std::string& type_name, ElementFactoryFn factory)
        {
            factories_[type_name] = std::move(factory);
            // Insert a minimal descriptor if not already present
            if (!descriptors_.contains(type_name))
            {
                descriptors_[type_name] = TypeDescriptor{ .type_name = type_name };
            }
        }

        [[nodiscard]] ElementPtr create(const std::string& type_name,
                                        const std::string& id,
                                        const ParamMap&    params) const
        {
            const auto it = factories_.find(type_name);
            if (it == factories_.end())
            {
                throw std::runtime_error(
                    "Unknown element type: '" + type_name + "'. "
                    "Did you call s7::registerBuiltins()?");
            }
            return it->second(id, params);
        }

        [[nodiscard]] bool hasType(const std::string& type_name) const
        {
            return factories_.contains(type_name);
        }

        [[nodiscard]] std::optional<TypeDescriptor> getDescriptor(const std::string& type_name) const
        {
            const auto it = descriptors_.find(type_name);
            if (it == descriptors_.end()) return std::nullopt;
            return it->second;
        }

        [[nodiscard]] std::vector<TypeDescriptor> allDescriptors() const
        {
            std::vector<TypeDescriptor> result;
            result.reserve(descriptors_.size());
            for (const auto& [name, desc] : descriptors_)
            {
                result.push_back(desc);
            }
            return result;
        }

        [[nodiscard]] std::vector<std::string> registeredTypes() const
        {
            std::vector<std::string> names;
            names.reserve(factories_.size());
            for (const auto& [name, _] : factories_)
            {
                names.push_back(name);
            }
            return names;
        }

    private:
        ElementRegistry() = default;
        std::unordered_map<std::string, ElementFactoryFn>  factories_;
        std::unordered_map<std::string, TypeDescriptor>    descriptors_;
    };

    inline ParamValue getParam(const ParamMap&     params,
                               const std::string&  key,
                               ParamValue          default_value = ParamValue{Real{0.0}})
    {
        const auto it = params.find(key);
        return it != params.end() ? it->second : std::move(default_value);
    }

    template <typename T>
    [[nodiscard]] inline bool isParamType(const ParamValue& value)
    {
        return std::holds_alternative<T>(value);
    }

    template <typename T>
    [[nodiscard]] inline T getParamAs(const ParamMap&    params,
                                      const std::string& key,
                                      const T&           default_value)
    {
        const auto it = params.find(key);
        if (it == params.end())
        {
            return default_value;
        }
        if (!std::holds_alternative<T>(it->second))
        {
            throw std::runtime_error("Parameter '" + key + "' has unexpected type");
        }
        return std::get<T>(it->second);
    }

    void registerBuiltins(ElementRegistry& registry = ElementRegistry::instance());
    void registerFileIO(ElementRegistry& registry = ElementRegistry::instance());
    void registerMemIO(ElementRegistry& registry = ElementRegistry::instance());
    void registerNetIO(ElementRegistry& registry = ElementRegistry::instance());
    void registerProbe(ElementRegistry& registry = ElementRegistry::instance());

} // namespace s7
