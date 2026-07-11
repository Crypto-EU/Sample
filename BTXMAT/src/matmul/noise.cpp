#include "matmul/noise.hpp"

#include "crypto/sha256.hpp"

namespace superhero::matmul::noise {
namespace {

Matrix from_seed_rect(const crypto::Uint256& seed, uint32_t rows, uint32_t cols) {
    Matrix out(rows, cols);
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t col = 0; col < cols; ++col) {
            out.at(row, col) = field::from_oracle(seed, row * cols + col);
        }
    }
    return out;
}

}  // namespace

crypto::Uint256 derive_noise_seed(std::string_view domain_tag, const crypto::Uint256& sigma) {
    uint8_t sigma_bytes[32];
    sigma.to_canonical_bytes(sigma_bytes);
    crypto::Sha256 hasher;
    hasher.write({reinterpret_cast<const uint8_t*>(domain_tag.data()), domain_tag.size()});
    hasher.write({sigma_bytes, sizeof(sigma_bytes)});
    std::array<uint8_t, 32> digest{};
    hasher.finalize(digest);
    return crypto::Uint256::from_be_bytes(digest.data());
}

NoisePair generate(const crypto::Uint256& sigma, uint32_t n, uint32_t r) {
    return {
        .e_l = from_seed_rect(derive_noise_seed(kTagEl, sigma), n, r),
        .e_r = from_seed_rect(derive_noise_seed(kTagEr, sigma), r, n),
        .f_l = from_seed_rect(derive_noise_seed(kTagFl, sigma), n, r),
        .f_r = from_seed_rect(derive_noise_seed(kTagFr, sigma), r, n),
    };
}

}  // namespace superhero::matmul::noise
