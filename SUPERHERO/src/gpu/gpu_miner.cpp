#include "gpu/gpu_miner.hpp"

#include "crypto/sha256.hpp"
#include "util/log.hpp"

#define CL_TARGET_OPENCL_VERSION 120
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include <array>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <unistd.h>

namespace superhero::gpu {
namespace {

#define CL_CHECK(expr, msg)                                                                                    \
    do {                                                                                                       \
        const cl_int _err = (expr);                                                                            \
        if (_err != CL_SUCCESS) {                                                                              \
            if (error) *error = std::string(msg) + " (cl=" + std::to_string(_err) + ")";                     \
            return false;                                                                                      \
        }                                                                                                      \
    } while (0)

void append_platform_devices(cl_platform_id platform, cl_device_type dtype, std::vector<cl_device_id>& out,
                             std::string& diag) {
    char pname[256]{}, pvendor[256]{};
    clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(pname), pname, nullptr);
    clGetPlatformInfo(platform, CL_PLATFORM_VENDOR, sizeof(pvendor), pvendor, nullptr);

    cl_uint count = 0;
    const cl_int err = clGetDeviceIDs(platform, dtype, 0, nullptr, &count);
    if (err != CL_SUCCESS || count == 0) return;

    const size_t before = out.size();
    out.resize(before + count);
    if (clGetDeviceIDs(platform, dtype, count, out.data() + before, nullptr) != CL_SUCCESS) {
        out.resize(before);
        return;
    }

    for (cl_uint i = 0; i < count; ++i) {
        char dname[256]{}, dvendor[256]{};
        cl_device_type dt = 0;
        clGetDeviceInfo(out[before + i], CL_DEVICE_NAME, sizeof(dname), dname, nullptr);
        clGetDeviceInfo(out[before + i], CL_DEVICE_VENDOR, sizeof(dvendor), dvendor, nullptr);
        clGetDeviceInfo(out[before + i], CL_DEVICE_TYPE, sizeof(dt), &dt, nullptr);
        const char* kind = (dt & CL_DEVICE_TYPE_GPU)        ? "GPU"
                           : (dt & CL_DEVICE_TYPE_ACCELERATOR) ? "ACC"
                           : (dt & CL_DEVICE_TYPE_CPU)         ? "CPU"
                                                               : "DEV";
        diag += std::string("  [") + kind + "] " + pvendor + " / " + pname + ": " + dname + " (" + dvendor + ")\n";
    }
}

int device_score(cl_device_id device) {
    char vendor[256]{};
    cl_device_type dt = 0;
    clGetDeviceInfo(device, CL_DEVICE_VENDOR, sizeof(vendor), vendor, nullptr);
    clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof(dt), &dt, nullptr);

    if (dt & CL_DEVICE_TYPE_CPU) return -1;

    int score = 0;
    const std::string v(vendor);
    if (v.find("AMD") != std::string::npos || v.find("Advanced Micro") != std::string::npos) score += 100;
    if (dt & CL_DEVICE_TYPE_GPU) score += 10;
    if (dt & CL_DEVICE_TYPE_ACCELERATOR) score += 5;
    return score;
}

cl_device_id pick_gpu(cl_platform_id* out_platform, std::string* error) {
    cl_uint platform_count = 0;
    if (clGetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0) {
        if (error) {
            *error = "no OpenCL platform (install amdgpu drivers, check /etc/OpenCL/vendors/amdocl64.icd)";
        }
        return nullptr;
    }

    std::vector<cl_platform_id> platforms(platform_count);
    clGetPlatformIDs(platform_count, platforms.data(), nullptr);

    std::vector<cl_device_id> candidates;
    std::string diag = "OpenCL scan:\n";
    for (cl_platform_id platform : platforms) {
        append_platform_devices(platform, CL_DEVICE_TYPE_GPU, candidates, diag);
        append_platform_devices(platform, CL_DEVICE_TYPE_ACCELERATOR, candidates, diag);
    }

    cl_device_id best = nullptr;
    cl_platform_id best_platform = nullptr;
    int best_score = -1;

    for (cl_platform_id platform : platforms) {
        for (cl_device_type dtype : {CL_DEVICE_TYPE_GPU, CL_DEVICE_TYPE_ACCELERATOR}) {
            cl_uint count = 0;
            if (clGetDeviceIDs(platform, dtype, 0, nullptr, &count) != CL_SUCCESS || count == 0) continue;
            std::vector<cl_device_id> devs(count);
            if (clGetDeviceIDs(platform, dtype, count, devs.data(), nullptr) != CL_SUCCESS) continue;
            for (cl_device_id dev : devs) {
                const int score = device_score(dev);
                if (score > best_score) {
                    best_score = score;
                    best = dev;
                    best_platform = platform;
                }
            }
        }
    }

    if (!best) {
        if (error) {
            *error = "no OpenCL GPU device\n" + diag +
                     "Hints: export HSA_OVERRIDE_GFX_VERSION=10.1.0 (RX5700XT) or 10.3.0 (RX6800XT); "
                     "check clinfo; verify /opt/amdgpu/lib64/libamdocl64.so";
        }
        return nullptr;
    }

    if (out_platform) *out_platform = best_platform;
    return best;
}

void write_target_limbs(const crypto::ArithUint256& target, std::array<uint32_t, 8>& out) {
    const crypto::Uint256 u = target.to_uint256();
    std::memcpy(out.data(), u.data(), 32);
}

std::string exe_directory() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    const std::string path(buf);
    const auto pos = path.rfind('/');
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}

}  // namespace

GpuMiner& GpuMiner::instance() {
    static GpuMiner g;
    return g;
}

std::string GpuMiner::read_kernel_source() {
    std::vector<std::string> paths;
    const std::string edir = exe_directory();
    if (!edir.empty()) {
        paths.push_back(edir + "/opencl/superhero.cl");
        paths.push_back(edir + "/../opencl/superhero.cl");
    }
    paths.emplace_back("opencl/superhero.cl");
    paths.emplace_back("../opencl/superhero.cl");
    paths.emplace_back("/hive/miners/custom/superhero/opencl/superhero.cl");
    paths.emplace_back("/hive/custom/superhero/opencl/superhero.cl");

    for (const std::string& p : paths) {
        std::ifstream in(p);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            util::log(util::LogLevel::Info, "OpenCL kernel: %s", p.c_str());
            return ss.str();
        }
    }
    throw std::runtime_error("superhero.cl not found (searched next to binary and Hive paths)");
}

bool GpuMiner::init(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_) return true;

    cl_platform_id platform = nullptr;
    cl_device_id device = pick_gpu(&platform, error);
    if (!device) {
        return false;
    }

    char devname[256]{};
    clGetDeviceInfo(device, CL_DEVICE_NAME, sizeof(devname), devname, nullptr);
    device_name_ = devname;

    cl_int err = 0;
    auto* ctx = new cl_context(clCreateContext(nullptr, 1, &device, nullptr, nullptr, &err));
    if (err != CL_SUCCESS) {
        if (error) *error = "clCreateContext failed";
        delete ctx;
        return false;
    }
    cl_context_ = ctx;

    auto* queue = new cl_command_queue(clCreateCommandQueue(*ctx, device, CL_QUEUE_PROFILING_ENABLE, &err));
    if (err != CL_SUCCESS) {
        if (error) *error = "clCreateCommandQueue failed";
        return false;
    }
    cl_queue_ = queue;

    if (!load_kernels(error)) return false;

    auto create_buf = [&](size_t size, cl_mem_flags flags) -> cl_mem* {
        auto* buf = new cl_mem(clCreateBuffer(*ctx, flags, size, nullptr, &err));
        if (err != CL_SUCCESS) throw std::runtime_error("clCreateBuffer failed");
        return buf;
    };

    try {
        constexpr size_t matrix_elems = 512u * 512u;
        constexpr size_t clean_elems = 32768u * 256u;
        buf_a_ = create_buf(matrix_elems * sizeof(uint32_t), CL_MEM_READ_ONLY);
        buf_b_ = create_buf(matrix_elems * sizeof(uint32_t), CL_MEM_READ_ONLY);
        buf_clean_ = create_buf(clean_elems * sizeof(uint32_t), CL_MEM_READ_ONLY);
        buf_header_ = create_buf(header_template_.size(), CL_MEM_READ_ONLY);
        buf_target_ = create_buf(8 * sizeof(uint32_t), CL_MEM_READ_ONLY);
        buf_found_ = create_buf(sizeof(int), CL_MEM_READ_WRITE);
        buf_nonce_ = create_buf(sizeof(uint64_t), CL_MEM_READ_WRITE);
        buf_digest_ = create_buf(32, CL_MEM_READ_WRITE);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }

    ready_ = true;
    util::log(util::LogLevel::Info, "GPU initialized: %s", device_name_.c_str());
    return true;
}

bool GpuMiner::load_kernels(std::string* error) {
    std::string source;
    try {
        source = read_kernel_source();
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }

    const char* src = source.c_str();
    size_t len = source.size();
  cl_int err = 0;
    auto* ctx = static_cast<cl_context*>(cl_context_);
    auto* program = new cl_program(clCreateProgramWithSource(*ctx, 1, &src, &len, &err));
    if (err != CL_SUCCESS) {
        if (error) *error = "clCreateProgramWithSource failed";
        return false;
    }
    cl_program_ = program;

    const char* opts = "-cl-mad-enable -DMATRIX_N=512 -DBLOCK_B=16";
    err = clBuildProgram(*program, 0, nullptr, opts, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(*program, nullptr, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size);
        clGetProgramBuildInfo(*program, nullptr, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        if (error) *error = std::string("OpenCL build failed:\n") + log.data();
        return false;
    }

    auto mk = [&](const char* name) -> cl_kernel* {
        auto* k = new cl_kernel(clCreateKernel(*program, name, &err));
        if (err != CL_SUCCESS) {
            if (error) *error = std::string("kernel not found: ") + name;
            return nullptr;
        }
        return k;
    };

    k_build_matrix_ = mk("build_matrix_from_seed");
    if (!k_build_matrix_) return false;
    k_build_clean_ = mk("build_clean_block");
    if (!k_build_clean_) return false;
    k_mine_ = mk("superhero_mine");
    if (!k_mine_) return false;
    return true;
}

bool GpuMiner::build_header_template(const matmul::PowState& state) {
    uint8_t* h = header_template_.data();
    crypto::write_le32(h + 0, static_cast<uint32_t>(state.version));
    std::memcpy(h + 4, state.previous_block_hash.data(), 32);
    std::memcpy(h + 36, state.merkle_root.data(), 32);
    crypto::write_le32(h + 68, state.time);
    crypto::write_le32(h + 72, state.bits);
    std::memset(h + 76, 0, 8);
    crypto::write_le16(h + 84, state.matmul_dim);
    std::memcpy(h + 86, state.seed_a.data(), 32);
    std::memcpy(h + 118, state.seed_b.data(), 32);
    return true;
}

bool GpuMiner::prepare_job(const matmul::PowState& state, const matmul::PowConfig& config, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ && !init(error)) return false;

    build_header_template(state);
    const crypto::ArithUint256& tgt = config.target;
    write_target_limbs(tgt, target_limbs_);

    auto* queue = static_cast<cl_command_queue*>(cl_queue_);
    auto* ctx = static_cast<cl_context*>(cl_context_);
    cl_int err = 0;

    clEnqueueWriteBuffer(*queue, *static_cast<cl_mem*>(buf_header_), CL_TRUE, 0, header_template_.size(),
                         header_template_.data(), 0, nullptr, nullptr);
    clEnqueueWriteBuffer(*queue, *static_cast<cl_mem*>(buf_target_), CL_TRUE, 0, 32, target_limbs_.data(), 0, nullptr,
                         nullptr);

    const uint32_t n = config.n;
    const uint32_t b = config.b;

    // Build matrix A on GPU
    cl_mem seed_a_buf = clCreateBuffer(*ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 32,
                                       const_cast<uint8_t*>(state.seed_a.data()), &err);
    cl_kernel km = *static_cast<cl_kernel*>(k_build_matrix_);
    clSetKernelArg(km, 0, sizeof(cl_mem), &seed_a_buf);
    clSetKernelArg(km, 1, sizeof(cl_mem), static_cast<cl_mem*>(buf_a_));
    clSetKernelArg(km, 2, sizeof(uint32_t), &n);
    size_t g = n * n;
    clEnqueueNDRangeKernel(*queue, km, 1, nullptr, &g, nullptr, 0, nullptr, nullptr);

    cl_mem seed_b_buf = clCreateBuffer(*ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 32,
                                       const_cast<uint8_t*>(state.seed_b.data()), &err);
    clSetKernelArg(km, 0, sizeof(cl_mem), &seed_b_buf);
    clSetKernelArg(km, 1, sizeof(cl_mem), static_cast<cl_mem*>(buf_b_));
    clEnqueueNDRangeKernel(*queue, km, 1, nullptr, &g, nullptr, 0, nullptr, nullptr);

    cl_kernel kc = *static_cast<cl_kernel*>(k_build_clean_);
    clSetKernelArg(kc, 0, sizeof(cl_mem), static_cast<cl_mem*>(buf_a_));
    clSetKernelArg(kc, 1, sizeof(cl_mem), static_cast<cl_mem*>(buf_b_));
    clSetKernelArg(kc, 2, sizeof(cl_mem), static_cast<cl_mem*>(buf_clean_));
    clSetKernelArg(kc, 3, sizeof(uint32_t), &n);
    clSetKernelArg(kc, 4, sizeof(uint32_t), &b);
    size_t blocks = 32u * 32u * 32u;
    clEnqueueNDRangeKernel(*queue, kc, 1, nullptr, &blocks, nullptr, 0, nullptr, nullptr);
    clFinish(*queue);

    clReleaseMemObject(seed_a_buf);
    clReleaseMemObject(seed_b_buf);
    return true;
}

matmul::SolveResult GpuMiner::mine_batch(
    const matmul::PowState& state,
    const matmul::PowConfig& config,
    uint64_t nonce_start,
    uint64_t batch_size,
    const crypto::ArithUint256* share_target) {
    (void)state;
    (void)config;
    matmul::SolveResult result{};
    result.tries = batch_size;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_) return result;

    if (share_target) write_target_limbs(*share_target, target_limbs_);

    auto* queue = static_cast<cl_command_queue*>(cl_queue_);
    int found_zero = 0;
    uint64_t found_nonce = 0;
    std::array<uint8_t, 32> found_digest{};

    clEnqueueWriteBuffer(*queue, *static_cast<cl_mem*>(buf_target_), CL_TRUE, 0, 32, target_limbs_.data(), 0, nullptr,
                         nullptr);
    clEnqueueWriteBuffer(*queue, *static_cast<cl_mem*>(buf_found_), CL_TRUE, 0, sizeof(int), &found_zero, 0, nullptr,
                         nullptr);

    cl_kernel km = *static_cast<cl_kernel*>(k_mine_);
    clSetKernelArg(km, 0, sizeof(cl_mem), static_cast<cl_mem*>(buf_a_));
    clSetKernelArg(km, 1, sizeof(cl_mem), static_cast<cl_mem*>(buf_b_));
    clSetKernelArg(km, 2, sizeof(cl_mem), static_cast<cl_mem*>(buf_clean_));
    clSetKernelArg(km, 3, sizeof(cl_mem), static_cast<cl_mem*>(buf_header_));
    clSetKernelArg(km, 4, sizeof(cl_mem), static_cast<cl_mem*>(buf_target_));
    clSetKernelArg(km, 5, sizeof(uint64_t), &nonce_start);

    cl_mem found_buf = *static_cast<cl_mem*>(buf_found_);
    cl_mem nonce_buf = *static_cast<cl_mem*>(buf_nonce_);
    cl_mem digest_buf = *static_cast<cl_mem*>(buf_digest_);
    clSetKernelArg(km, 6, sizeof(cl_mem), &found_buf);
    clSetKernelArg(km, 7, sizeof(cl_mem), &nonce_buf);
    clSetKernelArg(km, 8, sizeof(cl_mem), &digest_buf);

    size_t global = batch_size;
    size_t local = workgroup_size_;
    if (global % local != 0) global = ((global / local) + 1) * local;

    clEnqueueNDRangeKernel(*queue, km, 1, nullptr, &global, &local, 0, nullptr, nullptr);
    clFinish(*queue);

    int found_flag = 0;
    clEnqueueReadBuffer(*queue, found_buf, CL_TRUE, 0, sizeof(int), &found_flag, 0, nullptr, nullptr);
    if (found_flag) {
        clEnqueueReadBuffer(*queue, nonce_buf, CL_TRUE, 0, sizeof(uint64_t), &found_nonce, 0, nullptr, nullptr);
        clEnqueueReadBuffer(*queue, digest_buf, CL_TRUE, 0, 32, found_digest.data(), 0, nullptr, nullptr);
        result.found = true;
        result.nonce = found_nonce;
        result.digest = crypto::Uint256(found_digest);
    }
    result.nonce = nonce_start + batch_size;
    return result;
}

bool gpu_available() {
    std::string err;
    return GpuMiner::instance().init(&err);
}

std::string gpu_device_name() {
    GpuMiner::instance().init();
    return GpuMiner::instance().device_name();
}

matmul::SolveResult gpu_mine_batch(
    const matmul::PowState& state,
    const matmul::PowConfig& config,
    uint64_t nonce_start,
    uint64_t batch_size,
    const crypto::ArithUint256* share_target) {
    return GpuMiner::instance().mine_batch(state, config, nonce_start, batch_size, share_target);
}

}  // namespace superhero::gpu
