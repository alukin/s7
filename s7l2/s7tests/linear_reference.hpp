// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Closed-form continuous solutions, independent of the graph recurrence.
// These causal-memory graphs publish state at physical time (step-1)*dt.
#include <array>
#include <cmath>
#include <string>
#include <stdexcept>

namespace s7tests {
inline double linear_duration(const std::string& model) {
    return model == "rlc_resonance" ? 30. : 10.;
}
inline std::array<double, 3> linear_reference(const std::string& model, double t) {
    const double w = std::sqrt(3.75);
    const bool rc = model.starts_with("rc_");
    const double decay = std::exp(-t / 2);
    const double free = decay * (std::cos(w*t) + .5/w*std::sin(w*t));
    double x, v = 0;
    if (rc) x = model == "rc_charge" ? 1-std::exp(-t) : std::exp(-t);
    else if (model == "mass_spring_damper") {
        x = free;
        v = -4/w*decay*std::sin(w*t);
    } else if (model == "rlc_step") {
        x = (1-free)/4;
        v = decay*std::sin(w*t)/w;
    } else if (model == "rlc_resonance") {
        x = -.5*std::cos(2*t) + .5*free;
        v = std::sin(2*t) - 2/w*decay*std::sin(w*t);
    }
    else throw std::invalid_argument("Unknown linear reference model: " + model);
    return {x, v, 4*x};
}
inline void check_linear_error(double error, const std::string& context) {
    if (!std::isfinite(error) || error >= .05)
        throw std::runtime_error(context + ": exceeded 0.05 absolute error budget");
}
inline void check_linear_refinement(double coarse, double fine, const std::string& context) {
    if (!(coarse / fine > 1.8 && coarse / fine < 2.2))
        throw std::runtime_error(context + ": first-order refinement failed");
}
} // namespace s7tests
