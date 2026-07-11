#include "crypto/arith_uint256.hpp"

#include <cstring>

namespace superhero::crypto {

ArithUint256::ArithUint256(const Uint256& value) {
    std::memcpy(limbs_, value.data(), sizeof(limbs_));
}

ArithUint256 ArithUint256::from_uint256(const Uint256& value) {
    return ArithUint256(value);
}

Uint256 ArithUint256::to_uint256() const {
    Uint256 out;
    std::memcpy(out.data(), limbs_, sizeof(limbs_));
    return out;
}

int ArithUint256::compare(const ArithUint256& other) const {
    for (int i = 7; i >= 0; --i) {
        if (limbs_[i] < other.limbs_[i]) return -1;
        if (limbs_[i] > other.limbs_[i]) return 1;
    }
    return 0;
}

bool ArithUint256::operator==(const ArithUint256& other) const {
    return compare(other) == 0;
}

bool ArithUint256::operator<=(const ArithUint256& other) const {
    return compare(other) <= 0;
}

ArithUint256& ArithUint256::set_compact(uint32_t compact, bool* negative, bool* overflow) {
    if (negative) *negative = false;
    if (overflow) *overflow = false;

    for (auto& limb : limbs_) limb = 0;

    const int size = static_cast<int>(compact >> 24);
    uint32_t word = compact & 0x007fffff;
    if (size <= 3) {
        word >>= 8 * (3 - size);
        limbs_[0] = word;
    } else {
        limbs_[0] = word;
        const int shift_bytes = size - 3;
        if (shift_bytes < 8) {
            const int limb = shift_bytes / 4;
            const int offset = (shift_bytes % 4) * 8;
            limbs_[limb] |= word << offset;
            if (offset > 0 && limb + 1 < 8) {
                limbs_[limb + 1] |= word >> (32 - offset);
            }
        }
    }

    if (negative) {
        *negative = word != 0 && (compact & 0x00800000) != 0;
    }
    if (overflow) {
        *overflow = word != 0 && ((size > 34) || (word > 0xff && size > 33) || (word > 0xffff && size > 32));
    }
    return *this;
}

}  // namespace superhero::crypto
