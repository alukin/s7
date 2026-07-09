#ifndef S7CORE_INCLUDE_S7_COMPILER_HPP
#define S7CORE_INCLUDE_S7_COMPILER_HPP

#include "s7/graph.hpp"
#include "s7/schema.hpp"

namespace s7 {

class Compiler {
public:
    static ExecutableGraph compile(const Schema& schema);
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_COMPILER_HPP
