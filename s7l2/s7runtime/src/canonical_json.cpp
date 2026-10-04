#include "s7/canonical_json.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <openssl/evp.h>

namespace s7 {
namespace {

// Object keys are ordered by their UTF-16 code units (RFC 8785 section 3.2.3).
[[nodiscard]] auto utf16_units(const std::string_view utf8) -> std::u16string
{
    std::u16string out;
    out.reserve(utf8.size());
    for (std::size_t index = 0; index < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[index]);
        std::uint32_t code_point = 0;
        std::size_t length = 1;
        if (lead < 0x80) {
            code_point = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            code_point = lead & 0x1Fu;
            length = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            code_point = lead & 0x0Fu;
            length = 3;
        } else {
            code_point = lead & 0x07u;
            length = 4;
        }
        if (index + length > utf8.size()) {
            throw std::invalid_argument("Invalid UTF-8 in JSON object key.");
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            code_point = (code_point << 6) | (static_cast<unsigned char>(utf8[index + offset]) & 0x3Fu);
        }
        index += length;

        if (code_point >= 0x10000) {
            code_point -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (code_point >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (code_point & 0x3FFu)));
        } else {
            out.push_back(static_cast<char16_t>(code_point));
        }
    }
    return out;
}

void append_string(std::string& out, const std::string_view text)
{
    constexpr std::array<char, 16> hex{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    out.push_back('"');
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[byte >> 4]);
                    out.push_back(hex[byte & 0x0F]);
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

// ECMAScript Number::toString for a finite double (RFC 8785 section 3.2.2.3).
void append_number(std::string& out, double value)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument("Canonical JSON does not allow NaN or infinite numbers.");
    }
    if (value == 0.0) {
        out.push_back('0');
        return;
    }
    if (value < 0.0) {
        out.push_back('-');
        value = -value;
    }

    // Shortest round-trip digits in scientific form: d[.ddd]e±x.
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::scientific);
    const std::string_view scientific(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
    const auto exponent_pos = scientific.find('e');
    std::string digits;
    for (const char ch : scientific.substr(0, exponent_pos)) {
        if (ch != '.') {
            digits.push_back(ch);
        }
    }
    const int exponent = std::stoi(std::string(scientific.substr(exponent_pos + 1)));
    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;

    if (k <= n && n <= 21) {
        out += digits;
        out.append(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out += digits.substr(0, static_cast<std::size_t>(n));
        out.push_back('.');
        out += digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out.append(static_cast<std::size_t>(-n), '0');
        out += digits;
    } else {
        out.push_back(digits[0]);
        if (k > 1) {
            out.push_back('.');
            out += digits.substr(1);
        }
        out.push_back('e');
        out.push_back(n - 1 >= 0 ? '+' : '-');
        out += std::to_string(std::abs(n - 1));
    }
}

void append_integer(std::string& out, const auto value)
{
    // I-JSON: integers must be within the exactly representable range of a double.
    constexpr std::uint64_t max_safe = 1ULL << 53;
    bool in_range = false;
    if constexpr (std::is_signed_v<decltype(value)>) {
        in_range = value >= -static_cast<std::int64_t>(max_safe) && value <= static_cast<std::int64_t>(max_safe);
    } else {
        in_range = value <= max_safe;
    }
    if (!in_range) {
        throw std::invalid_argument("Canonical JSON integer exceeds the I-JSON safe range (2^53).");
    }
    append_number(out, static_cast<double>(value));
}

void append_value(std::string& out, const boost::json::value& value)
{
    switch (value.kind()) {
        case boost::json::kind::null: out += "null"; break;
        case boost::json::kind::bool_: out += value.get_bool() ? "true" : "false"; break;
        case boost::json::kind::int64: append_integer(out, value.get_int64()); break;
        case boost::json::kind::uint64: append_integer(out, value.get_uint64()); break;
        case boost::json::kind::double_: append_number(out, value.get_double()); break;
        case boost::json::kind::string: append_string(out, value.get_string()); break;
        case boost::json::kind::array: {
            out.push_back('[');
            bool first = true;
            for (const auto& item : value.get_array()) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                append_value(out, item);
            }
            out.push_back(']');
            break;
        }
        case boost::json::kind::object: {
            std::vector<std::pair<std::u16string, const boost::json::key_value_pair*>> members;
            for (const auto& member : value.get_object()) {
                members.emplace_back(utf16_units(member.key()), &member);
            }
            std::ranges::sort(members, {}, &decltype(members)::value_type::first);
            out.push_back('{');
            bool first = true;
            for (const auto& [_, member] : members) {
                if (!first) {
                    out.push_back(',');
                }
                first = false;
                append_string(out, member->key());
                out.push_back(':');
                append_value(out, member->value());
            }
            out.push_back('}');
            break;
        }
    }
}

} // namespace

auto canonical_json(const boost::json::value& value) -> std::string
{
    std::string out;
    append_value(out, value);
    return out;
}

auto parse_json_precise(const std::string_view text) -> boost::json::value
{
    boost::json::parse_options options;
    options.numbers = boost::json::number_precision::precise;
    return boost::json::parse(text, {}, options);
}

auto sha256_hex(const std::string_view bytes) -> std::string
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), digest.data(), &digest_len, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 computation failed.");
    }
    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digest_len; ++index) {
        hex << std::setw(2) << static_cast<int>(digest[index]);
    }
    return hex.str();
}

auto configuration_digest(const boost::json::value& value) -> std::string
{
    return "sha256:" + sha256_hex(canonical_json(value));
}

} // namespace s7
