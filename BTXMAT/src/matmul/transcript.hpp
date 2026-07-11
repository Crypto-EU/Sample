#pragma once

#include "matmul/matrix.hpp"
#include "matmul/noise.hpp"
#include "crypto/uint256.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace superhero::matmul::transcript {

inline constexpr std::string_view kCompressTag{"matmul-compress-v1"};

std::vector<field::Element> derive_compression_vector(const crypto::Uint256& sigma, uint32_t b);
field::Element compress_block(const ConstMatrixView& block_bb, const std::vector<field::Element>& v);

struct CanonicalResult {
    Matrix c_prime;
    crypto::Uint256 transcript_hash;
};

CanonicalResult canonical_matmul(const Matrix& a_prime, const Matrix& b_prime, uint32_t b, const crypto::Uint256& sigma);
std::vector<Matrix> precompute_clean_block_products(const Matrix& a, const Matrix& b, uint32_t block_b);

crypto::Uint256 replay_canonical_hash_with_reusable_clean_products(
    const Matrix& a,
    const Matrix& b,
    const std::vector<Matrix>& clean_block_products,
    const noise::NoisePair& noise,
    uint32_t block_b,
    const crypto::Uint256& sigma);

}  // namespace superhero::matmul::transcript
