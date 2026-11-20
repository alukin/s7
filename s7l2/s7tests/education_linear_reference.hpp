// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Independent closed forms for the education tests, never S7 element calls.
#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>
#include <vector>

namespace s7tests::education {
struct LinearLab {
    std::string id, file, slug;
    double h, seconds;
    std::string edit_id, parameter;
    double edited_value;
    std::vector<std::string> outputs;
};
inline const std::vector<LinearLab> linear_labs{
    {"FN04", "fn04_impulse_transfer", "FN04-impulse-transfer", .05, 10, "feedback_gain", "factor", .8, {"input", "output"}},
    {"FN05", "fn05_integration_rules", "FN05-integration-rules", .5, 6, "forward_pole", "factor", .75, {"forward", "backward", "trapezoid"}},
    {"FN06", "fn06_pole_maps", "FN06-pole-maps", 3, 30, "backward", "initial_value", 2, {"forward", "backward", "trapezoid", "exact"}},
    {"CT01", "ct01_first_order", "CT01-first-order", .1, 5, "tau", "value", 2, {"input", "voltage"}},
    {"CT02", "ct02_damped_modes", "CT02-damped-modes", .01, 8, "damping", "factor", 3, {"position", "velocity"}},
    {"CT03", "ct03_sample_time", "CT03-sample-time", .5, 10, "", "rate", .4, {"state"}},
};

inline double discrete(const std::string& lab, const std::string& channel, int n, double h, bool edited = false,
                       double damping = 1, double past = 0)
{
    if (lab == "FN04") {
        if (channel == "input") return n == 4 ? 1 : 0;
        const double a = edited ? .8 : .5;
        return past*std::pow(a,n+1)+(n < 4 ? 0 : .5*std::pow(a,n-4));
    }
    if (lab == "FN05" || lab == "FN06") {
        double pole;
        if (channel == "forward") pole = edited && lab == "FN05" ? .75 : 1-h;
        else if (channel == "backward") pole = 1/(1+h);
        else if (channel == "trapezoid") pole = (1-h/2)/(1+h/2);
        else if (channel == "exact") pole = std::exp(-h);
        else throw std::runtime_error("Unknown mode channel");
        return (edited && lab == "FN06" && channel == "backward" ? 2 : 1)*std::pow(pole, n);
    }
    if (lab == "CT01") return channel == "input" ? 1 : 1-std::pow(1-h/(edited ? 2 : 1), n);
    if (lab == "CT03") return std::pow(1-h, n);
    if (lab == "CT02") {
        const double b = edited ? 3 : damping, w = std::sqrt(4-b*b/4);
        const auto power = std::pow(std::complex<double>(1-b*h/2, w*h), n);
        return channel == "position" ? power.real()+b/(2*w)*power.imag() : -4/w*power.imag();
    }
    throw std::runtime_error("Unknown education reference: "+lab);
}

inline double continuous(const std::string& channel, double t, double damping = 1)
{
    const double w = std::sqrt(4-damping*damping/4), decay = std::exp(-damping*t/2);
    return channel == "position" ? decay*(std::cos(w*t)+damping/(2*w)*std::sin(w*t)) :
                                   -4/w*decay*std::sin(w*t);
}
} // namespace s7tests::education
