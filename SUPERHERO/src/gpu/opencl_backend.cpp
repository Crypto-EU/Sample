#include "gpu/opencl_backend.hpp"

#include <thread>

namespace superhero::gpu {

bool opencl_available() { return false; }

std::string opencl_device_name() { return "none"; }

matmul::SolveResult solve_opencl_batch(
    const matmul::PowState& state,
    const matmul::PowConfig& config,
    const matmul::JobContext& job,
    uint64_t nonce_start,
    uint64_t batch_size,
    const crypto::ArithUint256* share_target) {
    matmul::PowState local = state;
    return matmul::solve_range(local, config, job, nonce_start, batch_size, share_target, nullptr);
}

}  // namespace superhero::gpu
