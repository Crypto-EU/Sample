#pragma once

#include "crypto/uint256.hpp"

#include <cstdint>
#include <string>

namespace superhero::crypto {

class ArithUint256 {
public:
    ArithUint256() = default;
    explicit ArithUint256(const Uint256& value);

    bool operator<=(const ArithUint256& other) const;
    bool operator>(const ArithUint256& other) const { return other <= *this && !(*this == other); }

    ArithUint256& set_compact(uint32_t compact, bool* negative = nullptr, bool* overflow = nullptr);
    static ArithUint256 from_uint256(const Uint256& value);

    Uint256 to_uint256() const;

private:
    uint32_t limbs_[8]{};

    int compare(const ArithUint256& other) const;
    bool operator==(const ArithUint256& other) const;
};

}  // namespace superhero::crypto
