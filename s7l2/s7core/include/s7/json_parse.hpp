// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7CORE_INCLUDE_S7_JSON_PARSE_HPP
#define S7CORE_INCLUDE_S7_JSON_PARSE_HPP

#include <boost/json.hpp>

#include <string_view>

namespace s7 {

// Every S7 document, library, configuration and message is parsed with this:
// numbers are correctly rounded, so a parameter is the double nearest to its
// text. Boost.JSON's default parsing is not: "0.11512925464970229" becomes
// the neighbouring double, and a value serialized by Boost.JSON does not
// always parse back to itself. Results then depend on which program wrote
// the text (found by the G2 audio chain: a node fed a deployed graph and a
// local run of the same file differed in the last bit).
[[nodiscard]] inline auto json_parse_options() -> boost::json::parse_options
{
    boost::json::parse_options options;
    options.numbers = boost::json::number_precision::precise;
    return options;
}

[[nodiscard]] inline auto parse_json(const std::string_view text) -> boost::json::value
{
    return boost::json::parse(text, {}, json_parse_options());
}

[[nodiscard]] inline auto parse_json(const std::string_view text, boost::system::error_code& ec) -> boost::json::value
{
    return boost::json::parse(text, ec, {}, json_parse_options());
}

} // namespace s7

#endif // S7CORE_INCLUDE_S7_JSON_PARSE_HPP
