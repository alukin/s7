// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7NET_INCLUDE_S7_BASE64URL_HPP
#define S7NET_INCLUDE_S7_BASE64URL_HPP

// Base64url without padding (RFC 4648, section 5), as configuration chunks
// carry their bytes (S7_Control_Protocol_MVP.md, section 5.6).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace s7 {

[[nodiscard]] auto encode_base64url(std::span<const std::uint8_t> bytes) -> std::string;
// Nothing if the text is not canonical unpadded base64url (a character
// outside the alphabet, padding, an impossible length or non-zero spare bits).
[[nodiscard]] auto decode_base64url(std::string_view text) -> std::optional<std::vector<std::uint8_t>>;

} // namespace s7

#endif // S7NET_INCLUDE_S7_BASE64URL_HPP
