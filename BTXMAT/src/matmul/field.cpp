#include "matmul/field.hpp"

#include "crypto/sha256.hpp"

namespace superhero::matmul::field {
namespace {

Element reduce64(uint64_t x) {
    const uint64_t fold1 = (x & static_cast<uint64_t>(kModulus)) + (x >> 31);
    const uint32_t lo = static_cast<uint32_t>(fold1 & kModulus);
    const uint32_t hi = static_cast<uint32_t>(fold1 >> 31);
    uint32_t result = lo + hi;
    const uint32_t ge_mask = static_cast<uint32_t>(-static_cast<int32_t>(result >= kModulus));
    result -= (kModulus & ge_mask);
    return result;
}

Element scalar_dot(const Element* a, const Element* b, uint32_t len) {
    constexpr uint32_t kReduceInterval = 4;
    uint64_t acc = 0;
    uint32_t pending = 0;
    for (uint32_t i = 0; i < len; ++i) {
        acc += static_cast<uint64_t>(a[i]) * b[i];
        if (++pending == kReduceInterval) {
            acc = reduce64(acc);
            pending = 0;
        }
    }
    return reduce64(acc);
}

}  // namespace

Element add(Element a, Element b) {
    uint32_t s = a + b;
    if (s >= kModulus) s -= kModulus;
    return s;
}

Element sub(Element a, Element b) {
    if (a >= b) return a - b;
    return a + kModulus - b;
}

Element mul(Element a, Element b) {
    return reduce64(static_cast<uint64_t>(a) * b);
}

Element from_uint32(uint32_t x) { return reduce64(x); }

Element from_oracle(const crypto::Uint256& seed, uint32_t index) {
    uint8_t seed_bytes[32];
    seed.to_canonical_bytes(seed_bytes);

    for (uint32_t retry = 0; retry < 256; ++retry) {
        crypto::Sha256 hasher;
        hasher.write({seed_bytes, sizeof(seed_bytes)});
        uint8_t index_le[4];
        crypto::write_le32(index_le, index);
        hasher.write({index_le, sizeof(index_le)});
        if (retry > 0) {
            uint8_t retry_le[4];
            crypto::write_le32(retry_le, retry);
            hasher.write({retry_le, sizeof(retry_le)});
        }
        std::array<uint8_t, 32> hash{};
        hasher.finalize(hash);
        const uint32_t candidate = crypto::read_le32(hash.data()) & kModulus;
        if (candidate < kModulus) return candidate;
    }

    crypto::Sha256 fallback;
    fallback.write({seed_bytes, sizeof(seed_bytes)});
    uint8_t index_le[4];
    crypto::write_le32(index_le, index);
    fallback.write({index_le, sizeof(index_le)});
    static constexpr char kTag[] = "oracle-fallback";
    fallback.write({reinterpret_cast<const uint8_t*>(kTag), sizeof(kTag) - 1});
    std::array<uint8_t, 32> hash{};
    fallback.finalize(hash);
    return crypto::read_le32(hash.data()) % kModulus;
}

Element dot(const Element* a, const Element* b, uint32_t len) {
    return scalar_dot(a, b, len);
}

}  // namespace superhero::matmul::field
