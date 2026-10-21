// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7NET_INCLUDE_S7_CANONICAL_JSON_HPP
#define S7NET_INCLUDE_S7_CANONICAL_JSON_HPP

#include <string>
#include <string_view>

#include <boost/json.hpp>

namespace s7 {

// Serializes a JSON value per RFC 8785 (JSON Canonicalization Scheme):
// object members sorted by UTF-16 code units, no whitespace, minimal string
// escaping, and ECMAScript number formatting. Throws std::invalid_argument for
// non-finite numbers.
[[nodiscard]] auto canonical_json(const boost::json::value& value) -> std::string;

// Lower-case hex SHA-256 of the given bytes.
[[nodiscard]] auto sha256_hex(std::string_view bytes) -> std::string;

// Parses JSON with correctly rounded numbers. Digests must be computed from
// values parsed this way: Boost.JSON's default parsing is not correctly rounded.
[[nodiscard]] auto parse_json_precise(std::string_view text) -> boost::json::value;

// "sha256:<hex>" of the canonical JSON of value: the S7 configuration digest.
[[nodiscard]] auto configuration_digest(const boost::json::value& value) -> std::string;

} // namespace s7

#endif // S7NET_INCLUDE_S7_CANONICAL_JSON_HPP
