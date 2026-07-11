#include "util/hex.hpp"

#include <cctype>

namespace superhero::util {
namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::optional<std::vector<uint8_t>> decode_hex_vec(std::string_view hex) {
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex.remove_prefix(2);
    }
    if (hex.size() % 2 != 0) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hex_value(hex[i]);
        const int lo = hex_value(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

std::optional<std::array<uint8_t, 32>> decode_hex(std::string_view hex) {
    auto vec = decode_hex_vec(hex);
    if (!vec || vec->size() != 32) return std::nullopt;
    std::array<uint8_t, 32> out{};
    std::copy(vec->begin(), vec->end(), out.begin());
    return out;
}

std::string encode_hex(std::span<const uint8_t> bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.resize(bytes.size() * 2);
    for (size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = kHex[bytes[i] >> 4];
        out[i * 2 + 1] = kHex[bytes[i] & 0x0f];
    }
    return out;
}

std::optional<uint32_t> parse_uint32(std::string_view text) {
    if (text.empty()) return std::nullopt;
    uint64_t value = 0;
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    for (char c : text) {
        const int digit = hex_value(c);
        if (digit < 0 || (base == 10 && digit > 9)) return std::nullopt;
        value = value * base + static_cast<uint64_t>(digit);
        if (value > 0xffffffffULL) return std::nullopt;
    }
    return static_cast<uint32_t>(value);
}

std::optional<uint64_t> parse_uint64(std::string_view text) {
    if (text.empty()) return std::nullopt;
    uint64_t value = 0;
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    for (char c : text) {
        const int digit = hex_value(c);
        if (digit < 0 || (base == 10 && digit > 9)) return std::nullopt;
        if (value > (UINT64_MAX - digit) / base) return std::nullopt;
        value = value * base + static_cast<uint64_t>(digit);
    }
    return value;
}

}  // namespace superhero::util
