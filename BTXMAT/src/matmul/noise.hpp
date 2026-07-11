#pragma once

#include "matmul/matrix.hpp"
#include "crypto/uint256.hpp"

#include <cstdint>
#include <string_view>

namespace superhero::matmul::noise {

inline constexpr std::string_view kTagEl{"matmul_noise_EL_v1"};
inline constexpr std::string_view kTagEr{"matmul_noise_ER_v1"};
inline constexpr std::string_view kTagFl{"matmul_noise_FL_v1"};
inline constexpr std::string_view kTagFr{"matmul_noise_FR_v1"};

struct NoisePair {
    Matrix e_l;
    Matrix e_r;
    Matrix f_l;
    Matrix f_r;
};

crypto::Uint256 derive_noise_seed(std::string_view domain_tag, const crypto::Uint256& sigma);
NoisePair generate(const crypto::Uint256& sigma, uint32_t n, uint32_t r);

}  // namespace superhero::matmul::noise
