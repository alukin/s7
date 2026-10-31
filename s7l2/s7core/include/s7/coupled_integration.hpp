// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_COUPLED_INTEGRATION_HPP
#define S7CORE_INCLUDE_S7_COUPLED_INTEGRATION_HPP

namespace s7 {

enum class CoupledMethod { None, EulerSubsteps, GaussLegendre2 };

// Declarative marker for F64 causal-memory states coordinated by a compiled
// numerical region. The element still owns its persistent output storage.
class CoupledIntegration {
public:
    virtual ~CoupledIntegration() = default;
    [[nodiscard]] virtual CoupledMethod coupled_method() const = 0;
    [[nodiscard]] virtual double coupled_step_seconds() const = 0;
    [[nodiscard]] virtual int coupled_substeps() const = 0;
};

} // namespace s7

#endif
