#pragma once

#include "matmul/solver.hpp"

#include <cstdint>
#include <optional>

namespace superhero::gpu {

bool opencl_available();
std::string opencl_device_name();

matmul::SolveResult solve_opencl_batch(
    const matmul::PowState& state,
    const matmul::PowConfig& config,
    const matmul::JobContext& job,
    uint64_t nonce_start,
    uint64_t batch_size,
    const crypto::ArithUint256* share_target);

}  // namespace superhero::gpu
