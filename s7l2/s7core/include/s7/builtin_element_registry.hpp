// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_BUILTIN_ELEMENT_REGISTRY_HPP
#define S7CORE_INCLUDE_S7_BUILTIN_ELEMENT_REGISTRY_HPP

#include "s7/element_registry.hpp"

namespace s7 {

// Registers core built-in elements (from s7core) into the provided registry.
void register_builtin_elements(ElementRegistry& registry);

} // namespace s7

#endif // S7CORE_INCLUDE_S7_BUILTIN_ELEMENT_REGISTRY_HPP

