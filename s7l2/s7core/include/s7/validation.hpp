#pragma once

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

