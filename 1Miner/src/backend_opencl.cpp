#include "backend.hpp"

#include "util.hpp"

#include <CL/cl.h>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace oneminer {
namespace {

std::string load_kernel_source() {
  if (const char* env = std::getenv("ONE_MINER_KERNEL_PATH")) {
    std::ifstream f(env);
    if (f) {
      std::ostringstream ss;
      ss << f.rdbuf();
      return ss.str();
    }
  }
  const char* paths[] = {
      "opencl_kernels.cl",
      "src/opencl_kernels.cl",
      "/usr/local/share/1miner/opencl_kernels.cl",
      nullptr};
  for (int i = 0; paths[i]; ++i) {
    std::ifstream f(paths[i]);
    if (!f) continue;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
  return {};
}

std::string cl_err(cl_int e) { return "OpenCL error " + std::to_string(e); }

}  // namespace

#pragma pack(push, 1)
struct JobBlobHost {
  uint32_t midstate[8];
  uint32_t prefix_tail[16];
  uint32_t prefix_tail_len;
  uint32_t target[8];
  uint64_t start_counter;
  uint64_t count;
};
struct ResultBlobHost {
  uint32_t found;
  uint32_t _pad;
  uint64_t counter;
  uint32_t hash[8];
};
#pragma pack(pop)

OpenClBackend::OpenClBackend() = default;

bool OpenClBackend::init(std::string& err) {
  kernel_source_ = load_kernel_source();
  if (kernel_source_.empty()) {
    err = "opencl_kernels.cl not found";
    return false;
  }

  cl_uint nplat = 0;
  cl_int rc = clGetPlatformIDs(0, nullptr, &nplat);
  if (rc != CL_SUCCESS || nplat == 0) {
    err = "no OpenCL platforms";
    return false;
  }
  std::vector<cl_platform_id> plats(nplat);
  clGetPlatformIDs(nplat, plats.data(), nullptr);

  for (auto plat : plats) {
    char plat_vendor[256] = {0};
    char plat_name[256] = {0};
    clGetPlatformInfo(plat, CL_PLATFORM_VENDOR, sizeof(plat_vendor), plat_vendor, nullptr);
    clGetPlatformInfo(plat, CL_PLATFORM_NAME, sizeof(plat_name), plat_name, nullptr);
    std::string pv = plat_vendor;
    std::string pn = plat_name;
    auto lower = [](std::string s) {
      for (char& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
      return s;
    };
    const std::string pv_l = lower(pv);
    const std::string pn_l = lower(pn);
    const bool amd_platform =
        pv_l.find("advanced micro devices") != std::string::npos ||
        pv_l.find("amd") != std::string::npos ||
        pn_l.find("amd") != std::string::npos ||
        pn_l.find("rocm") != std::string::npos;
    // Explicitly skip NVIDIA / Intel compute platforms.
    if (pv_l.find("nvidia") != std::string::npos || pn_l.find("nvidia") != std::string::npos ||
        pv_l.find("intel") != std::string::npos) {
      log_info(std::string("skip OpenCL platform: ") + plat_vendor + " / " + plat_name);
      continue;
    }
    if (!amd_platform) {
      log_info(std::string("skip non-AMD OpenCL platform: ") + plat_vendor + " / " + plat_name);
      continue;
    }

    cl_uint ndev = 0;
    rc = clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, nullptr, &ndev);
    if (rc != CL_SUCCESS || ndev == 0) continue;
    std::vector<cl_device_id> devs(ndev);
    clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, ndev, devs.data(), nullptr);
    for (auto dev : devs) {
      char name[256] = {0};
      char vendor[256] = {0};
      clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(name), name, nullptr);
      clGetDeviceInfo(dev, CL_DEVICE_VENDOR, sizeof(vendor), vendor, nullptr);
      const std::string vendor_l = lower(vendor);
      const std::string name_l = lower(name);
      if (vendor_l.find("nvidia") != std::string::npos || name_l.find("geforce") != std::string::npos ||
          name_l.find("quadro") != std::string::npos || name_l.find("tesla") != std::string::npos ||
          name_l.find("rtx") != std::string::npos || name_l.find("gtx") != std::string::npos) {
        log_info(std::string("skip NVIDIA device: ") + name);
        continue;
      }
      if (!(vendor_l.find("amd") != std::string::npos ||
            vendor_l.find("advanced micro devices") != std::string::npos ||
            name_l.find("radeon") != std::string::npos || name_l.find("gfx") != std::string::npos)) {
        log_info(std::string("skip non-AMD GPU: ") + vendor + " / " + name);
        continue;
      }

      cl_context_properties props[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)plat, 0};
      cl_context ctx = clCreateContext(props, 1, &dev, nullptr, nullptr, &rc);
      if (rc != CL_SUCCESS) continue;
      cl_command_queue q = clCreateCommandQueue(ctx, dev, 0, &rc);
      if (rc != CL_SUCCESS) {
        clReleaseContext(ctx);
        continue;
      }
      const char* src = kernel_source_.c_str();
      size_t src_len = kernel_source_.size();
      cl_program prog = clCreateProgramWithSource(ctx, 1, &src, &src_len, &rc);
      if (rc != CL_SUCCESS) {
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      rc = clBuildProgram(prog, 1, &dev, "-cl-std=CL1.2 -DSASEUL_AMD_OPENCL=1", nullptr, nullptr);
      if (rc != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::string log(log_size, '\0');
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        err = "OpenCL build failed for " + std::string(name) + ": " + log;
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker = clCreateKernel(prog, "mine_classic", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      Dev d;
      d.device_id = dev;
      d.context = ctx;
      d.queue = q;
      d.program = prog;
      d.kernel = ker;
      d.name = name;
      devices_.push_back(d);
      log_info(std::string("AMD OpenCL GPU: ") + name);
    }
  }
  if (devices_.empty()) {
    err = "no AMD OpenCL GPUs found (NVIDIA/CPU disabled)";
    return false;
  }
  return true;
}

std::string OpenClBackend::device_name(int i) const {
  if (i < 0 || i >= device_count()) return {};
  return devices_[static_cast<size_t>(i)].name;
}

void OpenClBackend::set_job(const PreparedJob& job) { job_ = job; }

double OpenClBackend::last_mhs(int device_index) const {
  if (device_index < 0 || device_index >= device_count()) return 0;
  return devices_[static_cast<size_t>(device_index)].last_mhs;
}

std::vector<ShareCandidate> OpenClBackend::scan(int device_index, uint64_t start, uint64_t count,
                                                std::atomic<bool>& stop_flag) {
  std::vector<ShareCandidate> found;
  if (device_index < 0 || device_index >= device_count() || stop_flag.load()) return found;
  if (job_.mode != NonceMode::Classic) {
    // Latehex OpenCL path not yet specialized; fall back is handled by caller via CPU.
    return found;
  }
  auto& d = devices_[static_cast<size_t>(device_index)];

  JobBlobHost blob{};
  for (int i = 0; i < 8; ++i) blob.midstate[i] = job_.midstate[i];
  for (int i = 0; i < 8; ++i) {
    blob.target[i] = (uint32_t(job_.target[i * 4]) << 24) | (uint32_t(job_.target[i * 4 + 1]) << 16) |
                     (uint32_t(job_.target[i * 4 + 2]) << 8) | uint32_t(job_.target[i * 4 + 3]);
  }
  // Remaining 14 ASCII bytes after 2 full SHA blocks of the 142-byte prefix.
  const auto* p = reinterpret_cast<const uint8_t*>(job_.prefix_ascii.data());
  const size_t rem_off = 128;
  uint8_t rem[64] = {0};
  for (size_t i = 0; i < 14; ++i) rem[i] = p[rem_off + i];
  for (int i = 0; i < 16; ++i) {
    blob.prefix_tail[i] = (uint32_t(rem[i * 4]) << 24) | (uint32_t(rem[i * 4 + 1]) << 16) |
                          (uint32_t(rem[i * 4 + 2]) << 8) | uint32_t(rem[i * 4 + 3]);
  }
  blob.prefix_tail_len = 14;
  blob.start_counter = start;
  blob.count = count;

  ResultBlobHost result{};
  cl_int rc = CL_SUCCESS;
  cl_mem job_buf = clCreateBuffer(static_cast<cl_context>(d.context), CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  sizeof(blob), &blob, &rc);
  cl_mem res_buf = clCreateBuffer(static_cast<cl_context>(d.context), CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                  sizeof(result), &result, &rc);
  cl_kernel ker = static_cast<cl_kernel>(d.kernel);
  clSetKernelArg(ker, 0, sizeof(cl_mem), &job_buf);
  clSetKernelArg(ker, 1, sizeof(cl_mem), &res_buf);

  size_t global = 65536;
  if (count < global) global = static_cast<size_t>(count);
  if (global == 0) global = 1;
  const auto t0 = std::chrono::steady_clock::now();
  rc = clEnqueueNDRangeKernel(static_cast<cl_command_queue>(d.queue), ker, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
  clFinish(static_cast<cl_command_queue>(d.queue));
  const auto t1 = std::chrono::steady_clock::now();
  const double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec > 0) d.last_mhs = (static_cast<double>(count) / sec) / 1e6;

  clEnqueueReadBuffer(static_cast<cl_command_queue>(d.queue), res_buf, CL_TRUE, 0, sizeof(result), &result, 0, nullptr, nullptr);
  clReleaseMemObject(job_buf);
  clReleaseMemObject(res_buf);

  if (result.found) {
    ShareCandidate s;
    s.nonce_hex = format_classic_nonce(result.counter);
    for (int i = 0; i < 8; ++i) {
      s.hash[i * 4 + 0] = static_cast<uint8_t>((result.hash[i] >> 24) & 0xff);
      s.hash[i * 4 + 1] = static_cast<uint8_t>((result.hash[i] >> 16) & 0xff);
      s.hash[i * 4 + 2] = static_cast<uint8_t>((result.hash[i] >> 8) & 0xff);
      s.hash[i * 4 + 3] = static_cast<uint8_t>(result.hash[i] & 0xff);
    }
    s.blockhash_hex = hash_to_hex(s.hash);
    s.timestamp_us = now_us();
    s.gpu_index = device_index;
    // Verify on CPU before submit.
    Hash256 verify{};
    if (mine_hash_classic(job_, result.counter, verify) || hash_meets_target(s.hash, job_.target)) {
      // Prefer CPU-verified hash.
      if (hash_meets_target(verify, job_.target)) {
        s.hash = verify;
        s.blockhash_hex = hash_to_hex(verify);
      }
      found.push_back(s);
    }
  }
  return found;
}

}  // namespace oneminer
