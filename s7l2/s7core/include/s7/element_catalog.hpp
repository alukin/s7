// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_ELEMENT_CATALOG_HPP
#define S7CORE_INCLUDE_S7_ELEMENT_CATALOG_HPP

#include "s7/element_registry.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace s7 {

// Catalog entry for element browsing
struct ElementCatalogEntry {
    std::string type_name;          // "Gain", "WhiteNoiseGenerator"
    std::string display_name;       // Human-readable name
    std::string description;        // Description
    std::string category;           // "basic", "filters", "spectral", "simulation"
    std::string version;            // Plugin version
    int input_count;                // Number of inputs
    int output_count;               // Number of outputs
};

// Service for element catalog queries
class ElementCatalogService {
public:
    explicit ElementCatalogService(const ElementRegistry& registry);

    // Get all elements
    [[nodiscard]] std::vector<ElementCatalogEntry> all_elements() const;

    // Get elements by category
    [[nodiscard]] std::vector<ElementCatalogEntry> elements_in_category(
        const std::string& category
    ) const;

    // Get all available categories
    [[nodiscard]] std::vector<std::string> categories() const;

    // Get element by type name; the pointer is valid until the registry's
    // registrations change.
    [[nodiscard]] const ElementCatalogEntry* find_element(
        const std::string& type_name
    ) const;

    // Export catalog as JSON
    [[nodiscard]] boost::json::value to_json() const;

private:
    const ElementRegistry& registry_;
    // Entries built from the registry at entries_generation_; rebuilt when
    // the registry's registrations have changed since (plugins, libraries).
    mutable std::vector<ElementCatalogEntry> entries_;
    mutable std::optional<std::uint64_t> entries_generation_;

    void build_entries() const;
    void refresh() const;
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_ELEMENT_CATALOG_HPP

