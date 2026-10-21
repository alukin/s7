// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/base64url.hpp"

#include <array>

namespace s7 {

namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr auto make_decode_table()
{
    std::array<std::int8_t, 256> table{};
    table.fill(-1);
    for (std::size_t i = 0; i < kAlphabet.size(); ++i) {
        table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::int8_t>(i);
    }
    return table;
}

constexpr auto kDecode = make_decode_table();

} // namespace

auto encode_base64url(const std::span<const std::uint8_t> bytes) -> std::string
{
    std::string out;
    out.reserve((bytes.size() * 4 + 2) / 3);
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t v = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (const auto rest = bytes.size() - i; rest > 0) {
        const std::uint32_t v = (bytes[i] << 16) | (rest == 2 ? bytes[i + 1] << 8 : 0);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        if (rest == 2) {
            out += kAlphabet[(v >> 6) & 63];
        }
    }
    return out;
}

auto decode_base64url(const std::string_view text) -> std::optional<std::vector<std::uint8_t>>
{
    if (text.size() % 4 == 1) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text) {
        const auto value = kDecode[static_cast<unsigned char>(c)];
        if (value < 0) {
            return std::nullopt;
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    if ((buffer & ((1u << bits) - 1)) != 0) {
        return std::nullopt; // non-canonical: spare bits set
    }
    return out;
}

} // namespace s7
