// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/convert.hpp"

#include <memory>

namespace s7 {

ConvertMode convert_mode_from_name(const std::string_view name)
{
    if (name == "value") {
        return ConvertMode::Value;
    }
    if (name == "normalized") {
        return ConvertMode::Normalized;
    }
    throw std::invalid_argument("Convert mode must be 'value' or 'normalized', got '" + std::string(name) + "'.");
}

ConvertRounding convert_rounding_from_name(const std::string_view name)
{
    if (name == "half_even") {
        return ConvertRounding::HalfEven;
    }
    if (name == "toward_zero") {
        return ConvertRounding::TowardZero;
    }
    throw std::invalid_argument("Convert rounding must be 'half_even' or 'toward_zero', got '" + std::string(name) + "'.");
}

ConvertOverflow convert_overflow_from_name(const std::string_view name)
{
    if (name == "saturate") {
        return ConvertOverflow::Saturate;
    }
    if (name == "fault") {
        return ConvertOverflow::Fault;
    }
    throw std::invalid_argument("Convert overflow must be 'saturate' or 'fault', got '" + std::string(name) + "'.");
}

// All 27 x 27 type pairs are instantiated here, once.
std::unique_ptr<Element> make_convert(const SignalType from, const SignalType to, const ConvertOptions& options)
{
    return visit_signal_type(from, [&]<SignalType From>() -> std::unique_ptr<Element> {
        return visit_signal_type(to, [&]<SignalType To>() -> std::unique_ptr<Element> {
            if constexpr (signal_type_info(From).is_complex() != signal_type_info(To).is_complex()) {
                throw std::invalid_argument("Convert cannot change " + std::string(signal_type_name(From)) + " into " +
                                            std::string(signal_type_name(To)) +
                                            ": real <-> complex needs ReImToComplex or ComplexToReIm.");
            } else {
                return std::make_unique<ConvertT<From, To>>(options);
            }
        });
    });
}

} // namespace s7
