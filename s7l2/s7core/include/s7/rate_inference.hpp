// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_RATE_INFERENCE_HPP
#define S7CORE_INCLUDE_S7_RATE_INFERENCE_HPP

// Rate inference (doc/S7_Sample_Rate_Model.md): a unification pass over
// Element::declared_rate() and the schema's connections, giving every
// connected set of elements one resolved Rate -- seeded by Schema::rate()
// where nothing in the set pins one -- and reporting a conflict where two
// elements connected (directly or transitively) through plain wires declare
// different rates.
//
// Current scope: one rate per element, shared by every one of its ports --
// true of every element that exists today (Gain, Adder, Delay, the
// integrators, AnalogOscillator, ...). A rate-changing element whose input
// and output ports sit at different, explicitly related rates
// (S7_Sample_Rate_Model.md section 3's "RatioOf" port rule, mirroring
// PortTypeRule's kinds) is intentionally not yet supported: build that into
// this resolver when such an element (DEVELOPMENT_PLAN.md Phase 5 item 7)
// actually exists, not speculatively before anything needs it.

#include "s7/rate.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace s7 {

class Schema;
class Element;

struct ResolvedRates {
    // Per element id: its resolved rate. Absent for an element whose
    // connected set could not be resolved (an error names it instead).
    std::unordered_map<std::string, Rate> rates;
    std::vector<std::string> errors;

    [[nodiscard]] bool ok() const { return errors.empty(); }
};

class RateResolver {
public:
    // `instances`: every schema element's already-created instance, keyed by
    // id. Validation (validation.cpp) builds this once for its own type/
    // connection/cycle checks; this resolver reuses it instead of creating
    // every element a second time.
    [[nodiscard]] static ResolvedRates resolve(const Schema& schema,
                                               const std::unordered_map<std::string, const Element*>& instances);
};

} // namespace s7

#endif // S7CORE_INCLUDE_S7_RATE_INFERENCE_HPP
