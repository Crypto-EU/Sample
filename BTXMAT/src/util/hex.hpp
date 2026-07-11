#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace superhero::util {

std::optional<std::array<uint8_t, 32>> decode_hex(std::string_view hex);
std::optional<std::vector<uint8_t>> decode_hex_vec(std::string_view hex);
std::string encode_hex(std::span<const uint8_t> bytes);

std::optional<uint32_t> parse_uint32(std::string_view text);
std::optional<uint64_t> parse_uint64(std::string_view text);

}  // namespace superhero::util
