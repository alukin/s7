// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_VALIDATION_HPP
#define S7CORE_INCLUDE_S7_VALIDATION_HPP

#include "s7/schema.hpp"
#include "s7/coupled_integration.hpp"

#include <memory>
#include <string>
#include <unordered_map>
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
    struct CoupledRegion {
        CoupledMethod method{CoupledMethod::None};
        Rate rate;
        double step_seconds{};
        int substeps{1};
        std::vector<std::string> states;
        std::vector<Connection> derivative_connections;
        std::vector<std::string> upstream_states;
        std::vector<std::string> rhs;
    };
    std::vector<std::string> errors;
    std::vector<std::unique_ptr<Element>> elements;
    std::vector<std::string> trapezoid_rhs;
    // Resolved rate of each element, when validation succeeded.
    std::unordered_map<std::string, Rate> rates;
    // Rate domains (ADR-022): one per distinct rate, in step order (every
    // Resampler's input domain before its output domain).
    std::vector<Rate> domain_rates;
    // Resamplers that change the rate: element id, input domain, output domain
    // (indices into domain_rates).
    struct DomainLink {
        std::string resampler;
        std::size_t input{};
        std::size_t output{};
    };
    std::vector<DomainLink> domain_links;
    std::vector<CoupledRegion> coupled_regions;
};
[[nodiscard]] CheckedSchema check_schema(const Schema& schema);

} // namespace detail

} // namespace s7

#endif // S7CORE_INCLUDE_S7_VALIDATION_HPP
