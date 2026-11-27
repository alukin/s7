// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "s7/element_metadata.hpp"
#include <map>
#include <string_view>

namespace s7 {
// A missing "shape" is the native scalar default. An explicit shape must be
// a nonempty list of positive fixed extents.
[[nodiscard]] std::optional<ArrayDescriptor> parse_port_array(const boost::json::object& port,
                                                             SignalType type, std::string_view context);
[[nodiscard]] boost::json::array array_shape_json(const ArrayDescriptor& descriptor);

// Complete metadata, independent of executable factories. Resources are embedded
// by their owning module; loading never reads files or resolves external links.
class ElementMetadataLibrary {
public:
    explicit ElementMetadataLibrary(std::string_view json);
    [[nodiscard]] const ElementMetadata& at(std::string_view type) const;
    [[nodiscard]] const auto& elements() const { return elements_; }
private:
    std::map<std::string, ElementMetadata, std::less<>> elements_;
};
}
