#pragma once

#include "matmul/solver.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace superhero::gpu {

class GpuMiner {
public:
    static GpuMiner& instance();

    bool init(std::string* error = nullptr);
    bool available() const { return ready_; }
    std::string device_name() const { return device_name_; }

    bool prepare_job(const matmul::PowState& state, const matmul::PowConfig& config, std::string* error = nullptr);

    matmul::SolveResult mine_batch(
        const matmul::PowState& state,
        const matmul::PowConfig& config,
        uint64_t nonce_start,
        uint64_t batch_size,
        const crypto::ArithUint256* share_target);

    void set_batch_size(uint64_t batch) { default_batch_ = batch; }
    void set_workgroup_size(uint32_t wg) { workgroup_size_ = wg; }

private:
    GpuMiner() = default;
    bool load_kernels(std::string* error);
    bool ensure_buffers(size_t scratch_threads, std::string* error);
    bool ensure_scratch(size_t scratch_threads, std::string* error);
    bool build_header_template(const matmul::PowState& state);
    static std::string read_kernel_source();

    static constexpr uint32_t kScratchWordsPerThread = 384;

    bool ready_{false};
    bool buffers_ready_{false};
    std::string device_name_;
    uint64_t default_batch_{262144};
    uint32_t workgroup_size_{256};
    size_t scratch_threads_{0};

    void* cl_context_{nullptr};
    void* cl_queue_{nullptr};
    void* cl_device_{nullptr};
    void* cl_program_{nullptr};
    void* k_build_matrix_{nullptr};
    void* k_build_clean_{nullptr};
    void* k_mine_{nullptr};

    void* buf_a_{nullptr};
    void* buf_b_{nullptr};
    void* buf_clean_{nullptr};
    void* buf_header_{nullptr};
    void* buf_target_{nullptr};
    void* buf_found_{nullptr};
    void* buf_nonce_{nullptr};
    void* buf_digest_{nullptr};
    void* buf_scratch_{nullptr};

    std::array<uint8_t, 150> header_template_{};
    std::array<uint32_t, 8> target_limbs_{};
    std::mutex mutex_;
};

bool gpu_available();
std::string gpu_device_name();
matmul::SolveResult gpu_mine_batch(
    const matmul::PowState& state,
    const matmul::PowConfig& config,
    uint64_t nonce_start,
    uint64_t batch_size,
    const crypto::ArithUint256* share_target);

}  // namespace superhero::gpu
