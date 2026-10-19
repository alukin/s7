#ifndef S7CORE_INCLUDE_S7_VALIDATION_HPP
#define S7CORE_INCLUDE_S7_VALIDATION_HPP

#include "s7/schema.hpp"

#include <memory>
#include <string>
#include <vector>

namespace s7 {

struct ValidationResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] bool ok() const;
};

class Validator {
public:
    static ValidationResult validate(const Schema& schema);
};

namespace detail {

// Validation that keeps what it created: every element is created exactly
// once (elements[i] belongs to schema.elements()[i]; null where creation
// failed). The compiler builds the graph from these instances.
struct CheckedSchema {
    std::vector<std::string> errors;
    std::vector<std::unique_ptr<Element>> elements;
    std::vector<std::string> trapezoid_rhs;
};
[[nodiscard]] CheckedSchema check_schema(const Schema& schema);

} // namespace detail

} // namespace s7

#endif // S7CORE_INCLUDE_S7_VALIDATION_HPP
