#include "crypto/uint256.hpp"

#include "util/hex.hpp"

namespace superhero::crypto {

std::optional<Uint256> Uint256::from_hex(std::string_view hex) {
    auto bytes = util::decode_hex_vec(hex);
    if (!bytes) return std::nullopt;
    if (bytes->size() != kSize) return std::nullopt;
    Uint256 out;
    for (size_t i = 0; i < kSize; ++i) {
        out.bytes_[i] = (*bytes)[kSize - 1 - i];
    }
    return out;
}

std::string Uint256::to_hex() const {
    std::array<uint8_t, kSize> be{};
    for (size_t i = 0; i < kSize; ++i) {
        be[i] = bytes_[kSize - 1 - i];
    }
    return util::encode_hex(be);
}

Uint256 Uint256::from_be_bytes(const uint8_t* be) {
    Uint256 out;
    for (size_t i = 0; i < kSize; ++i) {
        out.bytes_[i] = be[kSize - 1 - i];
    }
    return out;
}

void Uint256::to_canonical_bytes(uint8_t out[32]) const {
    for (size_t i = 0; i < kSize; ++i) {
        out[i] = bytes_[kSize - 1 - i];
    }
}

}  // namespace superhero::crypto
