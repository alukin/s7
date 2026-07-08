#pragma once

#include "s7/graph.hpp"
#include "s7/schema.hpp"

namespace s7 {

class Compiler {
public:
    static ExecutableGraph compile(const Schema& schema);
};

} // namespace s7

