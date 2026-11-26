// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_COMPILER_HPP
#define S7CORE_INCLUDE_S7_COMPILER_HPP

#include "s7/graph.hpp"
#include "s7/schema.hpp"

#include <string>

namespace s7 {

class Compiler {
public:
    static ExecutableGraph compile(const Schema& schema);
    static ExecutableGraph compile(const Schema& schema,
                                   std::string runtime_instance_uuid,
                                   std::string schema_instance_uuid);

    // Internal C++ fixtures for direct array wiring. Schema loading and
    // deployment use compile() and cannot opt in.
    static ExecutableGraph compile_experimental_arrays(const Schema& schema);

private:
    static ExecutableGraph compile_impl(const Schema& schema,
                                       std::string runtime_instance_uuid,
                                       std::string schema_instance_uuid,
                                       bool experimental_arrays);
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_COMPILER_HPP
