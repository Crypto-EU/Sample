#pragma once

#include "crypto/arith_uint256.hpp"
#include "crypto/uint256.hpp"
#include "matmul/matrix.hpp"
#include "matmul/noise.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace superhero::matmul {

struct PowConfig {
    uint32_t n{512};
    uint32_t b{16};
    uint32_t r{8};
    crypto::ArithUint256 target;
};

struct PowState {
    int32_t version{0x20000000};
    crypto::Uint256 previous_block_hash;
    crypto::Uint256 merkle_root;
    uint32_t time{0};
    uint32_t bits{0};
    crypto::Uint256 seed_a;
    crypto::Uint256 seed_b;
    uint64_t nonce{0};
    uint16_t matmul_dim{512};
    crypto::Uint256 digest;
};

struct JobContext {
    Matrix a;
    Matrix b;
    std::vector<Matrix> clean_block_products;
};

struct SolveResult {
    bool found{false};
    uint64_t nonce{0};
    crypto::Uint256 digest;
    uint64_t tries{0};
};

crypto::Uint256 compute_header_hash(const PowState& state);
crypto::Uint256 derive_sigma(const PowState& state);
crypto::ArithUint256 target_from_bits(uint32_t bits);
crypto::ArithUint256 target_from_hex(std::string_view hex);

JobContext prepare_job(const PowState& state, const PowConfig& config);
crypto::Uint256 evaluate_nonce(const PowState& state, const PowConfig& config, const JobContext& job, uint64_t nonce);

SolveResult solve_range(
    PowState state,
    const PowConfig& config,
    const JobContext& job,
    uint64_t nonce_start,
    uint64_t max_tries,
    const crypto::ArithUint256* share_target,
    std::atomic<bool>* stop);

}  // namespace superhero::matmul
