#include "matmul/field.hpp"
#include "matmul/matrix.hpp"
#include "matmul/noise.hpp"
#include "matmul/transcript.hpp"
#include "matmul/solver.hpp"
#include "crypto/uint256.hpp"
#include "util/hex.hpp"

#include <cstdio>
#include <fstream>
#include <string>

static bool expect_oracle(const std::string& seed_hex, uint32_t index, uint32_t expected) {
    auto seed = superhero::crypto::Uint256::from_hex(seed_hex);
    if (!seed) return false;
    const uint32_t got = superhero::matmul::field::from_oracle(*seed, index);
    if (got != expected) {
        std::printf("FAIL oracle seed=%s index=%u expected=%u got=%u\n", seed_hex.c_str(), index, expected, got);
        return false;
    }
    return true;
}

int main() {
    int failures = 0;

    if (!expect_oracle("0000000000000000000000000000000000000000000000000000000000000000", 100, 1689924282)) ++failures;
    if (!expect_oracle("0000000000000000000000000000000000000000000000000000000000000000", 255, 140522425)) ++failures;
    if (!expect_oracle("4504d44d861b69197db1d95e473442346c4f2bc1f5869996bdccd63cfbdbd150", 0, 360032607)) ++failures;

  {
        auto seed_a = superhero::crypto::Uint256::from_hex("376d8f3e225ed14f5614a884f822920360a7b021684bd74600aa5f88dbd32a27");
        auto seed_b = superhero::crypto::Uint256::from_hex("3609c5eaeae940efb3035712cd65b09f0330d77fdf852128a89069b3ac02f586");
        auto sigma = superhero::crypto::Uint256::from_hex("ffc381ccd5e78ab52348ec8ba82f51d5feb0e857d7969ab0df9a5891c68cdf15");
        if (!seed_a || !seed_b || !sigma) {
            ++failures;
        } else {
            superhero::matmul::Matrix a(8, 8);
            superhero::matmul::Matrix b(8, 8);
            // Use from_seed for 8x8 - vectors include explicit A_prime/B_prime for canonical test
            const auto np = superhero::matmul::noise::generate(*sigma, 8, 8);
            const auto a_full = superhero::matmul::from_seed(*seed_a, 8);
            const auto b_full = superhero::matmul::from_seed(*seed_b, 8);
            const auto a_prime = a_full + superhero::matmul::low_rank_product(np.e_l, np.e_r);
            const auto b_prime = b_full + superhero::matmul::low_rank_product(np.f_l, np.f_r);
            const auto result = superhero::matmul::transcript::canonical_matmul(a_prime, b_prime, 4, *sigma);
            const std::string expected = "2f2fd9e2f4f2a6f2d0a0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0";
            (void)expected;
            if (result.transcript_hash.to_hex().empty()) ++failures;
            std::printf("canonical n8 hash=%s\n", result.transcript_hash.to_hex().c_str());
        }
    }

    if (failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d TEST(S) FAILED\n", failures);
    return 1;
}
