#ifndef S7CORE_INCLUDE_S7_ELEMENT_REGISTRY_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_REGISTRY_HPP

#include "s7/element.hpp"
#include "s7/element_metadata.hpp"

#include <boost/json.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>

namespace s7 {

// Forward declarations
struct ElementCreationContext;

// Descriptor combining factory and metadata
class ElementDescriptor {
public:
    ElementDescriptor(
        std::string type_name,
        std::function<std::unique_ptr<Element>(const ElementCreationContext&)> factory,
        const ElementMetadata& metadata
    );

    [[nodiscard]] const ElementMetadata& metadata() const { return metadata_; }
    [[nodiscard]] std::unique_ptr<Element> create(const ElementCreationContext& ctx) const;
    [[nodiscard]] std::string type_name() const { return type_name_; }

private:
    std::string type_name_;
    std::function<std::unique_ptr<Element>(const ElementCreationContext&)> factory_;
    ElementMetadata metadata_;
};

// Context for element creation with parameter extraction
struct ElementCreationContext {
    const ElementMetadata& metadata;
    const boost::json::object& params;

    [[nodiscard]] double get_real(const std::string& name) const;
    [[nodiscard]] int64_t get_int(const std::string& name) const;
    [[nodiscard]] std::string get_string(const std::string& name) const;
    [[nodiscard]] bool has_param(const std::string& name) const;
};

// Singleton registry for all available elements
class ElementRegistry {
public:
    static ElementRegistry& instance();

    // Register element
    void register_element(const ElementDescriptor& descriptor);

    // Find element by type name
    [[nodiscard]] const ElementDescriptor* find(const std::string& type_name) const;

    // Get all descriptors
    [[nodiscard]] const std::vector<ElementDescriptor>& all_descriptors() const;

    // List all element type names
    [[nodiscard]] std::vector<std::string> element_types() const;

    // List element types in category
    [[nodiscard]] std::vector<std::string> element_types_in_category(
        const std::string& category
    ) const;

    // Get element count
    [[nodiscard]] size_t element_count() const;

    // Clear all registrations (for testing)
    void clear();

private:
    ElementRegistry() = default;
    ~ElementRegistry() = default;

    // Prevent copy/move
    ElementRegistry(const ElementRegistry&) = delete;
    ElementRegistry& operator=(const ElementRegistry&) = delete;

    std::vector<ElementDescriptor> descriptors_;
    std::unordered_map<std::string, size_t> name_to_index_;  // O(1) lookup
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENT_REGISTRY_HPP

