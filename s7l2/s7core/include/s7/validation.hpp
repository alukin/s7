#ifndef S7CORE_INCLUDE_S7_VALIDATION_HPP
#define S7CORE_INCLUDE_S7_VALIDATION_HPP

#include "s7/schema.hpp"

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

} // namespace s7

#endif // S7CORE_INCLUDE_S7_VALIDATION_HPP
