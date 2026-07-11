#include "matmul/solver.hpp"

#include "crypto/sha256.hpp"
#include "matmul/transcript.hpp"

namespace superhero::matmul {

crypto::Uint256 compute_header_hash(const PowState& state) {
    crypto::Sha256 hasher;
    uint8_t version_le[4];
    uint8_t time_le[4];
    uint8_t bits_le[4];
    uint8_t nonce64_le[8];
    uint8_t dim_le[2];
    crypto::write_le32(version_le, static_cast<uint32_t>(state.version));
    crypto::write_le32(time_le, state.time);
    crypto::write_le32(bits_le, state.bits);
    crypto::write_le64(nonce64_le, state.nonce);
    crypto::write_le16(dim_le, state.matmul_dim);

    hasher.write({version_le, sizeof(version_le)});
    hasher.write(state.previous_block_hash.span());
    hasher.write(state.merkle_root.span());
    hasher.write({time_le, sizeof(time_le)});
    hasher.write({bits_le, sizeof(bits_le)});
    hasher.write({nonce64_le, sizeof(nonce64_le)});
    hasher.write({dim_le, sizeof(dim_le)});
    hasher.write(state.seed_a.span());
    hasher.write(state.seed_b.span());

    std::array<uint8_t, 32> digest{};
    hasher.finalize(digest);
    return crypto::Uint256(digest);
}

crypto::Uint256 derive_sigma(const PowState& state) {
    const crypto::Uint256 header_hash = compute_header_hash(state);
    return crypto::Uint256(crypto::Sha256::hash(header_hash.span()));
}

crypto::ArithUint256 target_from_bits(uint32_t bits) {
    crypto::ArithUint256 target;
    target.set_compact(bits);
    return target;
}

crypto::ArithUint256 target_from_hex(std::string_view hex) {
    auto value = crypto::Uint256::from_hex(hex);
    if (!value) return {};
    return crypto::ArithUint256::from_uint256(*value);
}

JobContext prepare_job(const PowState& state, const PowConfig& config) {
    JobContext ctx{
        .a = from_seed(state.seed_a, config.n),
        .b = from_seed(state.seed_b, config.n),
        .clean_block_products = {},
    };
    ctx.clean_block_products = transcript::precompute_clean_block_products(ctx.a, ctx.b, config.b);
    return ctx;
}

crypto::Uint256 evaluate_nonce(const PowState& state, const PowConfig& config, const JobContext& job, uint64_t nonce) {
    PowState local = state;
    local.nonce = nonce;
    const crypto::Uint256 sigma = derive_sigma(local);
    const noise::NoisePair np = noise::generate(sigma, config.n, config.r);
    return transcript::replay_canonical_hash_with_reusable_clean_products(
        job.a, job.b, job.clean_block_products, np, config.b, sigma);
}

SolveResult solve_range(
    PowState state,
    const PowConfig& config,
    const JobContext& job,
    uint64_t nonce_start,
    uint64_t max_tries,
    const crypto::ArithUint256* share_target,
    std::atomic<bool>* stop) {
    SolveResult result{};
    state.nonce = nonce_start;

    for (uint64_t i = 0; i < max_tries; ++i) {
        if (stop && stop->load(std::memory_order_relaxed)) break;

        const crypto::Uint256 digest = evaluate_nonce(state, config, job, state.nonce);
        const crypto::ArithUint256 digest_arith = crypto::ArithUint256::from_uint256(digest);
        const crypto::ArithUint256* check_target = share_target ? share_target : &config.target;

        ++result.tries;
        if (digest_arith <= *check_target) {
            result.found = true;
            result.nonce = state.nonce;
            result.digest = digest;
            return result;
        }

        if (state.nonce == UINT64_MAX) break;
        ++state.nonce;
    }

    result.nonce = state.nonce;
    return result;
}

}  // namespace superhero::matmul
