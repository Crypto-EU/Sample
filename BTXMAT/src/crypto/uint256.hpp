#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace superhero::crypto {

class Uint256 {
public:
    static constexpr size_t kSize = 32;

    Uint256() = default;
    explicit Uint256(std::array<uint8_t, kSize> bytes) : bytes_(bytes) {}

    const uint8_t* data() const { return bytes_.data(); }
    uint8_t* data() { return bytes_.data(); }
    std::span<const uint8_t> span() const { return bytes_; }

    bool operator==(const Uint256& other) const { return bytes_ == other.bytes_; }
    bool operator!=(const Uint256& other) const { return !(*this == other); }

    static std::optional<Uint256> from_hex(std::string_view hex);
    std::string to_hex() const;

    static Uint256 from_be_bytes(const uint8_t* be);
    void to_canonical_bytes(uint8_t out[32]) const;

private:
    std::array<uint8_t, kSize> bytes_{};
};

}  // namespace superhero::crypto
