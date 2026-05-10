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
    using ParamValue = Number;
    using ParamMap   = std::unordered_map<std::string, ParamValue>;

    using ElementFactoryFn = std::function<ElementPtr(const std::string& id, const ParamMap& params)>;

    class ElementRegistry {
    public:
        static ElementRegistry& instance()
        {
            static ElementRegistry registry;
            return registry;
        }

        void registerType(const std::string& type_name, ElementFactoryFn factory)
        {
            factories_[type_name] = std::move(factory);
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
        std::unordered_map<std::string, ElementFactoryFn> factories_;
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

} // namespace s7

