// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_TRAPEZOID_HPP
#define S7CORE_INCLUDE_S7_TRAPEZOID_HPP

namespace s7 {

enum class TrapezoidMode { Implicit, Sampled };

// Optional numerical-stage interface. NodeRuntime solves all enabled states
// together, reevaluating only their stateless derivative dependencies. Candidate
// values exist only inside step(); normal causal-memory publication is unchanged.
// Preparation and publication are separate so directly coupled states always
// read the same iterate, independent of element insertion order.
class TrapezoidIntegration {
public:
    virtual ~TrapezoidIntegration() = default;
    [[nodiscard]] virtual bool implicit_trapezoid_enabled() const = 0;
    virtual void prepare_trapezoid_step() = 0;
    [[nodiscard]] virtual bool prepare_trapezoid_correction() = 0;
    virtual void publish_trapezoid_candidate() = 0;
};

} // namespace s7

#endif
