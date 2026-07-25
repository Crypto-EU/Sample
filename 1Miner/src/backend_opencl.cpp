#include "backend.hpp"

#include "util.hpp"

#include <CL/cl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

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

  std::vector<std::string> paths = {
      "opencl_kernels.cl",
      "src/opencl_kernels.cl",
      "/usr/local/share/1miner/opencl_kernels.cl",
  };
#if defined(__linux__)
  char exe[4096] = {0};
  const ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n > 0) {
    std::string p(exe, static_cast<size_t>(n));
    const auto slash = p.find_last_of('/');
    if (slash != std::string::npos) {
      paths.insert(paths.begin(), p.substr(0, slash + 1) + "opencl_kernels.cl");
    }
  }
#endif

  for (const auto& path : paths) {
    std::ifstream f(path);
    if (!f) continue;
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
  return {};
}

static inline uint32_t rotr32(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32u - n));
}
static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}
static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}
static inline uint32_t bsig0(uint32_t x) {
  return rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22);
}
static inline uint32_t bsig1(uint32_t x) {
  return rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25);
}
static inline uint32_t ssig0(uint32_t x) {
  return rotr32(x, 7) ^ rotr32(x, 18) ^ (x >> 3);
}
static inline uint32_t ssig1(uint32_t x) {
  return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10);
}

static constexpr uint32_t kK256[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

// Run the first `nrounds` SHA256 rounds; out_work = working regs (not yet + mid).
static void sha256_partial_work(const uint32_t mid[8], const uint32_t w_in[16], int nrounds,
                                uint32_t out_work[8]) {
  uint32_t w[16];
  for (int i = 0; i < 16; ++i) w[i] = w_in[i];
  uint32_t a = mid[0], b = mid[1], c = mid[2], d = mid[3];
  uint32_t e = mid[4], f = mid[5], g = mid[6], h = mid[7];
  for (int i = 0; i < nrounds; ++i) {
    uint32_t wi;
    if (i < 16) {
      wi = w[i];
    } else {
      const uint32_t s0 = rotr32(w[(i - 15) & 15], 7) ^ rotr32(w[(i - 15) & 15], 18) ^
                          (w[(i - 15) & 15] >> 3);
      const uint32_t s1 = rotr32(w[(i - 2) & 15], 17) ^ rotr32(w[(i - 2) & 15], 19) ^
                          (w[(i - 2) & 15] >> 10);
      wi = w[i & 15] = s1 + w[(i - 7) & 15] + s0 + w[i & 15];
    }
    const uint32_t t1 = h + bsig1(e) + ch(e, f, g) + kK256[i] + wi;
    const uint32_t t2 = bsig0(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  out_work[0] = a;
  out_work[1] = b;
  out_work[2] = c;
  out_work[3] = d;
  out_work[4] = e;
  out_work[5] = f;
  out_work[6] = g;
  out_work[7] = h;
}

static uint32_t hex_pair_byte(uint8_t byte) {
  auto nib = [](uint8_t n) -> uint32_t {
    return static_cast<uint32_t>(n) + 0x30u + (((static_cast<uint32_t>(n) + 6u) >> 4) * 0x27u);
  };
  return (nib(static_cast<uint8_t>((byte >> 4) & 15u)) << 8) | nib(static_cast<uint8_t>(byte & 15u));
}

static void encode_hi32_words(uint32_t hi, uint32_t& h0, uint32_t& h1) {
  const uint32_t p0 = hex_pair_byte(static_cast<uint8_t>((hi >> 24) & 255u));
  const uint32_t p1 = hex_pair_byte(static_cast<uint8_t>((hi >> 16) & 255u));
  const uint32_t p2 = hex_pair_byte(static_cast<uint8_t>((hi >> 8) & 255u));
  const uint32_t p3 = hex_pair_byte(static_cast<uint8_t>(hi & 255u));
  h0 = (p0 << 16) | p1;
  h1 = (p2 << 16) | p3;
}

}  // namespace

// Must match opencl_kernels.cl ResultBlob. JobBlobHost lives in backend.hpp.
struct ResultBlobHost {
  uint32_t found;
  uint32_t _pad;
  uint64_t counter;
  uint32_t hash[8];
};

static_assert(sizeof(JobBlobHost) == 200, "JobBlobHost size");
static_assert(sizeof(ResultBlobHost) == 48, "ResultBlobHost size");
static_assert(sizeof(Hi32BlobHost) == 160, "Hi32BlobHost size");

static unsigned normalize_unroll(unsigned u) {
  if (u == 14) return 14;  // sequential mine_classic_hi32 (u4)
  if (u == 3) return 3;    // hasher-like constant blob u32
  if (u >= 8) return 8;
  if (u >= 4) return 4;  // ilp4
  if (u == 2) return 2;  // ilp2
  return 1;
}

// Work-item nonce stride / alignment period.
static unsigned unroll_period(unsigned u) {
  u = normalize_unroll(u);
  if (u == 14) return 4u;
  if (u == 3) return 1u;
  return u;
}

static const char* unroll_tag(unsigned u) {
  u = normalize_unroll(u);
  if (u == 2) return " (ilp2)";
  if (u == 3) return " (u32/hasher)";
  if (u == 4) return " (ilp4)";
  if (u == 8) return " (u8)";
  if (u == 14) return " (u4)";
  return " (u1)";
}

static uint64_t align_count_to_unroll(uint64_t count, unsigned unroll) {
  const unsigned period = unroll_period(unroll);
  return count - (count % period);
}

OpenClBackend::OpenClBackend() = default;

OpenClBackend::~OpenClBackend() {
  for (auto& d : devices_) {
    if (!d) continue;
    if (d->job_mem) clReleaseMemObject(static_cast<cl_mem>(d->job_mem));
    if (d->hi_mem) clReleaseMemObject(static_cast<cl_mem>(d->hi_mem));
    if (d->res_mem) clReleaseMemObject(static_cast<cl_mem>(d->res_mem));
    if (d->res_mem_b) clReleaseMemObject(static_cast<cl_mem>(d->res_mem_b));
    if (d->kernel_hi32_u1) clReleaseKernel(static_cast<cl_kernel>(d->kernel_hi32_u1));
    if (d->kernel_hi32) clReleaseKernel(static_cast<cl_kernel>(d->kernel_hi32));
    if (d->kernel_hi32_u8) clReleaseKernel(static_cast<cl_kernel>(d->kernel_hi32_u8));
    if (d->kernel_hi32_ilp2) clReleaseKernel(static_cast<cl_kernel>(d->kernel_hi32_ilp2));
    if (d->kernel_hi32_ilp4) clReleaseKernel(static_cast<cl_kernel>(d->kernel_hi32_ilp4));
    if (d->kernel_hi32_c_u32) clReleaseKernel(static_cast<cl_kernel>(d->kernel_hi32_c_u32));
    if (d->kernel) clReleaseKernel(static_cast<cl_kernel>(d->kernel));
    if (d->program) clReleaseProgram(static_cast<cl_program>(d->program));
    if (d->queue) clReleaseCommandQueue(static_cast<cl_command_queue>(d->queue));
    if (d->context) clReleaseContext(static_cast<cl_context>(d->context));
  }
}

bool OpenClBackend::init(std::string& err) {
  kernel_source_ = load_kernel_source();
  if (kernel_source_.empty()) {
    err = "opencl_kernels.cl not found (set ONE_MINER_KERNEL_PATH or place opencl_kernels.cl next to binary)";
    return false;
  }

  auto lower = [](std::string s) {
    for (char& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    return s;
  };
  auto contains_any = [&](const std::string& hay, std::initializer_list<const char*> needles) {
    for (const char* n : needles) {
      if (hay.find(n) != std::string::npos) return true;
    }
    return false;
  };
  auto is_nvidia = [&](const std::string& v, const std::string& n) {
    return contains_any(v, {"nvidia"}) || contains_any(n, {"nvidia", "geforce", "quadro", "tesla", "rtx ", "gtx "});
  };
  auto is_intel = [&](const std::string& v, const std::string& n) {
    return contains_any(v, {"intel"}) || contains_any(n, {"intel"});
  };
  auto is_amdish = [&](const std::string& v, const std::string& n, const std::string& p) {
    return contains_any(v, {"amd", "advanced micro devices", "ati"}) ||
           contains_any(n, {"amd", "radeon", "gfx", "vega", "navi", "instinct"}) ||
           contains_any(p, {"amd", "rocm", "mesa"});
  };

  cl_uint nplat = 0;
  cl_int rc = clGetPlatformIDs(0, nullptr, &nplat);
  if (rc != CL_SUCCESS || nplat == 0) {
    err = "no OpenCL platforms";
    return false;
  }
  std::vector<cl_platform_id> plats(nplat);
  clGetPlatformIDs(nplat, plats.data(), nullptr);

  std::string diag;
  for (cl_platform_id plat : plats) {
    char pvendor[256] = {0};
    char pname[256] = {0};
    clGetPlatformInfo(plat, CL_PLATFORM_VENDOR, sizeof(pvendor), pvendor, nullptr);
    clGetPlatformInfo(plat, CL_PLATFORM_NAME, sizeof(pname), pname, nullptr);
    const std::string plat_l = lower(std::string(pvendor) + " " + pname);
    diag += std::string(" platform=") + pname;

    cl_uint ndev = 0;
    clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, nullptr, &ndev);
    if (ndev == 0) continue;
    std::vector<cl_device_id> devs(ndev);
    clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, ndev, devs.data(), nullptr);

    for (cl_device_id dev : devs) {
      char vendor[256] = {0};
      char name[256] = {0};
      clGetDeviceInfo(dev, CL_DEVICE_VENDOR, sizeof(vendor), vendor, nullptr);
      clGetDeviceInfo(dev, CL_DEVICE_NAME, sizeof(name), name, nullptr);
      const std::string vendor_l = lower(vendor);
      const std::string name_l = lower(name);
      log_info(std::string("OpenCL device: ") + name + " [" + vendor + "]");

      if (is_nvidia(vendor_l, name_l)) {
        log_info("  skip NVIDIA device");
        continue;
      }
      if (is_intel(vendor_l, name_l)) {
        log_info("  skip Intel device");
        continue;
      }
      if (!is_amdish(vendor_l, name_l, plat_l)) {
        if (contains_any(name_l, {"cpu", "pthread", "host"})) {
          log_info("  skip CPU-like device");
          continue;
        }
        log_warn(std::string("  accepting unclassified GPU on non-NVIDIA platform: ") + name);
      }

      cl_context_properties props[] = {CL_CONTEXT_PLATFORM, (cl_context_properties)plat, 0};
      cl_context ctx = clCreateContext(props, 1, &dev, nullptr, nullptr, &rc);
      if (rc != CL_SUCCESS) {
        log_warn(std::string("  clCreateContext failed for ") + name + " rc=" + std::to_string(rc));
        continue;
      }
      cl_command_queue q = clCreateCommandQueue(ctx, dev, CL_QUEUE_PROFILING_ENABLE, &rc);
      if (rc != CL_SUCCESS) {
        log_warn(std::string("  clCreateCommandQueue(profiling) failed for ") + name +
                 " rc=" + std::to_string(rc) + " — retry without profiling");
        q = clCreateCommandQueue(ctx, dev, 0, &rc);
      }
      if (rc != CL_SUCCESS) {
        log_warn(std::string("  clCreateCommandQueue failed for ") + name + " rc=" + std::to_string(rc));
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

      const char* opts_bitalign =
          "-cl-std=CL1.2 -cl-mad-enable -cl-no-signed-zeros "
          "-cl-uniform-work-group-size -DONE_MINER_BITALIGN=1";
      const char* opts_portable =
          "-cl-std=CL1.2 -cl-mad-enable -cl-no-signed-zeros "
          "-cl-uniform-work-group-size";
      const char* opts_portable_loose = "-cl-std=CL1.2 -cl-mad-enable -cl-no-signed-zeros";

      bool used_bitalign = true;
      rc = clBuildProgram(prog, 1, &dev, opts_bitalign, nullptr, nullptr);
      if (rc != CL_SUCCESS) {
        used_bitalign = false;
        // Recreate program — some ICDs leave a failed build sticky.
        clReleaseProgram(prog);
        prog = clCreateProgramWithSource(ctx, 1, &src, &src_len, &rc);
        if (rc != CL_SUCCESS) {
          clReleaseCommandQueue(q);
          clReleaseContext(ctx);
          continue;
        }
        rc = clBuildProgram(prog, 1, &dev, opts_portable, nullptr, nullptr);
        if (rc != CL_SUCCESS) {
          rc = clBuildProgram(prog, 1, &dev, opts_portable_loose, nullptr, nullptr);
        }
      }
      if (rc != CL_SUCCESS) {
        size_t log_size = 0;
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::string blog(log_size, '\0');
        clGetProgramBuildInfo(prog, dev, CL_PROGRAM_BUILD_LOG, log_size, blog.data(), nullptr);
        log_warn(std::string("  OpenCL build failed for ") + name + ": " + blog);
        err = "OpenCL build failed for " + std::string(name) + ": " + blog;
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      log_info(std::string("  ROTR32 path: ") +
               (used_bitalign ? "amd_bitalign (cl_amd_media_ops)" : "portable shift/or"));

      cl_kernel ker = clCreateKernel(prog, "mine_classic_fast", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker_hi = clCreateKernel(prog, "mine_classic_hi32", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker_hi_u1 = clCreateKernel(prog, "mine_classic_hi32_u1", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker_hi_u8 = clCreateKernel(prog, "mine_classic_hi32_u8", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker_hi_ilp2 = clCreateKernel(prog, "mine_classic_hi32_ilp2", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker_hi_ilp4 = clCreateKernel(prog, "mine_classic_hi32_ilp4", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseKernel(ker_hi_ilp2);
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_kernel ker_hi_c_u32 = clCreateKernel(prog, "mine_classic_hi32_c_u32", &rc);
      if (rc != CL_SUCCESS) {
        clReleaseKernel(ker_hi_ilp4);
        clReleaseKernel(ker_hi_ilp2);
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }

      JobBlobHost zjob{};
      Hi32BlobHost zhi{};
      ResultBlobHost zres{};
      cl_mem job_mem =
          clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(zjob), &zjob, &rc);
      if (rc != CL_SUCCESS || !job_mem) {
        clReleaseKernel(ker_hi_c_u32);
        clReleaseKernel(ker_hi_ilp4);
        clReleaseKernel(ker_hi_ilp2);
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_mem hi_mem =
          clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(zhi), &zhi, &rc);
      if (rc != CL_SUCCESS || !hi_mem) {
        clReleaseMemObject(job_mem);
        clReleaseKernel(ker_hi_c_u32);
        clReleaseKernel(ker_hi_ilp4);
        clReleaseKernel(ker_hi_ilp2);
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_mem res_mem =
          clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(zres), &zres, &rc);
      if (rc != CL_SUCCESS || !res_mem) {
        clReleaseMemObject(hi_mem);
        clReleaseMemObject(job_mem);
        clReleaseKernel(ker_hi_c_u32);
        clReleaseKernel(ker_hi_ilp4);
        clReleaseKernel(ker_hi_ilp2);
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }
      cl_mem res_mem_b =
          clCreateBuffer(ctx, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof(zres), &zres, &rc);
      if (rc != CL_SUCCESS || !res_mem_b) {
        clReleaseMemObject(res_mem);
        clReleaseMemObject(hi_mem);
        clReleaseMemObject(job_mem);
        clReleaseKernel(ker_hi_c_u32);
        clReleaseKernel(ker_hi_ilp4);
        clReleaseKernel(ker_hi_ilp2);
        clReleaseKernel(ker_hi_u8);
        clReleaseKernel(ker_hi_u1);
        clReleaseKernel(ker_hi);
        clReleaseKernel(ker);
        clReleaseProgram(prog);
        clReleaseCommandQueue(q);
        clReleaseContext(ctx);
        continue;
      }

      size_t max_wg = 256;
      clGetKernelWorkGroupInfo(ker_hi_c_u32, dev, CL_KERNEL_WORK_GROUP_SIZE, sizeof(max_wg), &max_wg,
                               nullptr);
      cl_uint cu = 0;
      clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cu), &cu, nullptr);

      auto d = std::make_unique<Dev>();
      d->device_id = dev;
      d->context = ctx;
      d->queue = q;
      d->program = prog;
      d->kernel = ker;
      d->kernel_hi32 = ker_hi;
      d->kernel_hi32_u1 = ker_hi_u1;
      d->kernel_hi32_u8 = ker_hi_u8;
      d->kernel_hi32_ilp2 = ker_hi_ilp2;
      d->kernel_hi32_ilp4 = ker_hi_ilp4;
      d->kernel_hi32_c_u32 = ker_hi_c_u32;
      d->job_mem = job_mem;
      d->hi_mem = hi_mem;
      d->res_mem = res_mem;
      d->res_mem_b = res_mem_b;
      d->name = name;
      d->max_work_group = max_wg ? max_wg : 256;
      d->compute_units = cu ? cu : 1;
      // hasher-like defaults: threads=256, blocks≈CU*16, batch=128M
      d->tune.local = std::min<size_t>(256, d->max_work_group);
      if (d->tune.local == 0) d->tune.local = 64;
      d->tune.intensity = 16;
      d->tune.unroll = 3;
      d->tune.chunks = 1;
      d->tune.null_local = false;
      d->tune.batch = 1ull << 27;
      devices_.push_back(std::move(d));
      log_info(std::string("AMD OpenCL GPU ready: ") + name + " cu=" + std::to_string(cu) +
               " max_wg=" + std::to_string(max_wg));
    }
  }
  if (devices_.empty()) {
    err = "no AMD OpenCL GPUs found (NVIDIA/CPU disabled). Run clinfo on the rig and check amdgpu OpenCL ICD. " +
          diag;
    return false;
  }
  return true;
}

std::string OpenClBackend::device_name(int i) const {
  if (i < 0 || i >= device_count()) return {};
  return devices_[static_cast<size_t>(i)]->name;
}

GpuTune OpenClBackend::tune(int i) const {
  if (i < 0 || i >= device_count()) return {};
  return devices_[static_cast<size_t>(i)]->tune;
}

uint64_t OpenClBackend::tuned_batch(int i) const {
  if (i < 0 || i >= device_count()) return 1ull << 24;
  return devices_[static_cast<size_t>(i)]->tune.batch;
}

size_t OpenClBackend::calc_global(const Dev& d, size_t local, unsigned intensity, uint64_t count,
                                  unsigned unroll) {
  if (local == 0) local = 64;
  const unsigned period = unroll_period(unroll);
  uint64_t units = count / period;
  if (units == 0) units = 1;
  size_t global;
  if (intensity == 0) {
    // Full-span: aim for ~1 work-item per unit, capped so the ICD stays happy.
    const size_t cap = static_cast<size_t>(d.compute_units) * local * 512u;
    global = static_cast<size_t>(std::min(units, static_cast<uint64_t>(cap)));
  } else {
    global = static_cast<size_t>(d.compute_units) * local * intensity;
  }
  if (global > units) global = static_cast<size_t>(units);
  if (global < local) global = local;
  global = (global / local) * local;
  if (global == 0) global = local;
  return global;
}

void* OpenClBackend::pick_kernel(Dev& d, unsigned unroll) const {
  unroll = normalize_unroll(unroll);
  if (unroll == 8) return d.kernel_hi32_u8;
  if (unroll == 14) return d.kernel_hi32;       // sequential u4
  if (unroll == 4) return d.kernel_hi32_ilp4;   // claim-only ILP4
  if (unroll == 3) return d.kernel_hi32_c_u32;  // hasher-like constant blob
  if (unroll == 2) return d.kernel_hi32_ilp2;
  return d.kernel_hi32_u1;
}

bool OpenClBackend::is_const_blob_kernel(unsigned /*unroll*/) const {
  // c_u32 no longer uses a constant/global Hi32 blob — scalar args only.
  return false;
}

int OpenClBackend::set_const_hi32_args(void* ker_v, void* hi_mem_v, uint64_t start, uint64_t count,
                                       void* res) {
  cl_kernel ker = static_cast<cl_kernel>(ker_v);
  cl_mem hi_mem = static_cast<cl_mem>(hi_mem_v);
  cl_mem res_mem = static_cast<cl_mem>(res);
  int arg = 0;
  cl_int rc = CL_SUCCESS;
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_mem), &hi_mem);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_ulong), &start);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_ulong), &count);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_mem), &res_mem);
  return static_cast<int>(rc);
}

int OpenClBackend::set_scalar_hi32_args(void* ker_v, const Hi32Launch& L, uint64_t start,
                                        uint64_t count, void* res) {
  cl_kernel ker = static_cast<cl_kernel>(ker_v);
  cl_mem res_mem = static_cast<cl_mem>(res);
  int arg = 0;
  cl_int rc = CL_SUCCESS;
  for (int i = 0; i < 8; ++i) {
    rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.mid[i]);
  }
  for (int i = 0; i < 8; ++i) {
    rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.wr[i]);
  }
  for (int i = 0; i < 8; ++i) {
    rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.tgt[i]);
  }
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.fw0);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.fw1);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.fw2);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.fw3);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.fw4);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.h1_lo);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.A5c);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.E5c);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.pre_w0);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.pre_w1);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.pre_w2);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.pre_w3);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.pre_c20);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_uint), &L.pre_s21);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_ulong), &start);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_ulong), &count);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_mem), &res_mem);
  return static_cast<int>(rc);
}

bool OpenClBackend::fill_hi32_launch(Dev& d, const PreparedJob& job, uint64_t start, uint64_t count,
                                     JobBlobHost* out_blob) {
  d.hi = {};
  if (job.prefix_ascii.size() < 142 || count == 0) return false;

  JobBlobHost blob{};
  for (int i = 0; i < 8; ++i) blob.midstate[i] = job.midstate[i];
  for (int i = 0; i < 8; ++i) {
    blob.target[i] = (uint32_t(job.target[i * 4]) << 24) | (uint32_t(job.target[i * 4 + 1]) << 16) |
                     (uint32_t(job.target[i * 4 + 2]) << 8) | uint32_t(job.target[i * 4 + 3]);
  }

  const auto* p = reinterpret_cast<const uint8_t*>(job.prefix_ascii.data());
  uint8_t rem[16] = {0};
  for (size_t i = 0; i < 14; ++i) rem[i] = p[128 + i];

  auto be4 = [](uint8_t a, uint8_t b, uint8_t c, uint8_t d4) -> uint32_t {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d4);
  };
  blob.block0[0] = be4(rem[0], rem[1], rem[2], rem[3]);
  blob.block0[1] = be4(rem[4], rem[5], rem[6], rem[7]);
  blob.block0[2] = be4(rem[8], rem[9], rem[10], rem[11]);
  blob.block0[3] = (uint32_t(rem[12]) << 24) | (uint32_t(rem[13]) << 16);  // low 16 = nonce
  for (int i = 4; i < 15; ++i) blob.block0[i] = 0;
  blob.block0[15] = 0x000004f0u;  // bit length 158*8

  {
    uint32_t w[16] = {};
    for (int i = 0; i < 16; ++i) w[i] = blob.block0[i];
    sha256_partial_work(blob.midstate, w, 3, blob.work_after_r2);
    blob.flags = 1u;
  }

  const bool use_hi32 =
      ((start >> 32) == ((start + count - 1) >> 32)) && count <= 0xffffffffull;
  if (!use_hi32) {
    if (out_blob) *out_blob = blob;
    return false;
  }

  uint32_t h0 = 0, h1 = 0;
  encode_hi32_words(static_cast<uint32_t>(start >> 32), h0, h1);
  uint32_t w[16] = {};
  for (int i = 0; i < 16; ++i) w[i] = blob.block0[i];
  w[3] = (blob.block0[3] & 0xFFFF0000u) | ((h0 >> 16) & 0xFFFFu);
  w[4] = ((h0 & 0xFFFFu) << 16) | ((h1 >> 16) & 0xFFFFu);
  sha256_partial_work(blob.midstate, w, 5, blob.work_after_r4);
  blob.flags |= 2u;

  for (int i = 0; i < 8; ++i) {
    d.hi.mid[i] = blob.midstate[i];
    d.hi.wr[i] = blob.work_after_r4[i];
    d.hi.tgt[i] = blob.target[i];
  }
  d.hi.fw0 = w[0];
  d.hi.fw1 = w[1];
  d.hi.fw2 = w[2];
  d.hi.fw3 = w[3];
  d.hi.fw4 = w[4];
  d.hi.h1_lo = h1 & 0xFFFFu;
  // Round 5 closed form from work_after_r4 (a0..h0); matches kernel Ch/Maj/ROTR.
  {
    const uint32_t a0 = blob.work_after_r4[0];
    const uint32_t b0 = blob.work_after_r4[1];
    const uint32_t c0 = blob.work_after_r4[2];
    const uint32_t d0 = blob.work_after_r4[3];
    const uint32_t e0 = blob.work_after_r4[4];
    const uint32_t f0 = blob.work_after_r4[5];
    const uint32_t g0 = blob.work_after_r4[6];
    const uint32_t h0 = blob.work_after_r4[7];
    const uint32_t F5 = h0 + bsig1(e0) + ch(e0, f0, g0) + kK256[5];
    const uint32_t G5 = bsig0(a0) + maj(a0, b0, c0);
    d.hi.A5c = F5 + G5;  // a1 = A5c + w5
    d.hi.E5c = d0 + F5;  // e1 = E5c + w5
  }
  // W[16..19] depend only on fw0..fw4 and bitlen (not nonce).
  d.hi.pre_w0 = ssig0(d.hi.fw1) + d.hi.fw0;
  d.hi.pre_w1 = ssig1(0x000004f0u) + ssig0(d.hi.fw2) + d.hi.fw1;
  d.hi.pre_w2 = ssig1(d.hi.pre_w0) + ssig0(d.hi.fw3) + d.hi.fw2;
  d.hi.pre_w3 = ssig1(d.hi.pre_w1) + ssig0(d.hi.fw4) + d.hi.fw3;
  // W20 = SSIG1(pre_w2) + SSIG0(w5) + fw4 → pre_c20 + SSIG0(w5)
  // W21 = SSIG1(pre_w3) + SSIG0(w6) + w5 → pre_s21 + SSIG0(w6) + w5
  d.hi.pre_c20 = ssig1(d.hi.pre_w2) + d.hi.fw4;
  d.hi.pre_s21 = ssig1(d.hi.pre_w3);
  d.hi.valid = true;

  // Upload packed constants for hasher-like c_u32 kernel.
  if (d.hi_mem) {
    Hi32BlobHost packed = d.hi.to_blob();
    cl_command_queue q = static_cast<cl_command_queue>(d.queue);
    clEnqueueWriteBuffer(q, static_cast<cl_mem>(d.hi_mem), CL_FALSE, 0, sizeof(packed), &packed, 0,
                         nullptr, nullptr);
  }

  if (out_blob) *out_blob = blob;
  return true;
}

bool OpenClBackend::enqueue_hi32(Dev& d, void* ker_v, uint64_t start, uint64_t count, void* res,
                                 const GpuTune& cfg, void** out_event) {
  if (!ker_v || !res || !d.hi.valid) return false;
  cl_command_queue q = static_cast<cl_command_queue>(d.queue);
  cl_kernel ker = static_cast<cl_kernel>(ker_v);
  if (out_event) *out_event = nullptr;

  size_t local = cfg.local ? cfg.local : 256;
  if (local > d.max_work_group) local = d.max_work_group;
  if (local == 0) local = 64;

  unsigned chunks = cfg.chunks ? cfg.chunks : 1;
  if (chunks > 64) chunks = 64;

  const unsigned unroll = normalize_unroll(cfg.unroll);
  uint64_t work = align_count_to_unroll(count, unroll);
  if (work == 0) return false;

  // Precompute chunk sizes so we know which is last.
  uint64_t chunk_sizes[64] = {};
  unsigned nchunks = 0;
  uint64_t off = 0;
  for (unsigned c = 0; c < chunks; ++c) {
    uint64_t remain = work - off;
    uint64_t n = remain / (chunks - c);
    n = align_count_to_unroll(n, unroll);
    if (n == 0) continue;
    chunk_sizes[nchunks++] = n;
    off += n;
  }
  if (nchunks == 0) return false;

  off = 0;
  for (unsigned c = 0; c < nchunks; ++c) {
    const uint64_t n = chunk_sizes[c];
    const uint64_t chunk_start = start + off;
    size_t global = calc_global(d, local, cfg.intensity, n, unroll);

    if (is_const_blob_kernel(unroll)) {
      if (!d.hi_mem) return false;
      if (set_const_hi32_args(ker, d.hi_mem, chunk_start, n, res) != CL_SUCCESS) return false;
    } else {
      if (set_scalar_hi32_args(ker, d.hi, chunk_start, n, res) != CL_SUCCESS) return false;
    }

    const size_t* local_ptr = cfg.null_local ? nullptr : &local;
    const bool want_ev = out_event && (c + 1 == nchunks);
    cl_event ev = nullptr;
    cl_event* ev_ptr = want_ev ? &ev : nullptr;
    cl_int rc = clEnqueueNDRangeKernel(q, ker, 1, nullptr, &global, local_ptr, 0, nullptr, ev_ptr);
    if (rc != CL_SUCCESS && !cfg.null_local) {
      if (ev) {
        clReleaseEvent(ev);
        ev = nullptr;
      }
      rc = clEnqueueNDRangeKernel(q, ker, 1, nullptr, &global, nullptr, 0, nullptr, ev_ptr);
    }
    if (rc != CL_SUCCESS) {
      if (ev) clReleaseEvent(ev);
      return false;
    }
    if (want_ev && ev) *out_event = ev;
    off += n;
  }
  return true;
}

bool OpenClBackend::apply_tune_cache(const std::string& path) {
  if (path.empty()) return false;
  return load_tune_cache(path);
}

double OpenClBackend::last_mhs(int device_index) const {
  if (device_index < 0 || device_index >= device_count()) return 0;
  return devices_[static_cast<size_t>(device_index)]->last_mhs.load();
}

uint64_t OpenClBackend::total_hashes(int device_index) const {
  if (device_index < 0 || device_index >= device_count()) return 0;
  return devices_[static_cast<size_t>(device_index)]->total_hashes.load();
}

std::vector<ShareCandidate> OpenClBackend::scan(int device_index, uint64_t start, uint64_t count,
                                                const PreparedJob& job,
                                                std::atomic<bool>& stop_flag) {
  std::vector<ShareCandidate> found;
  if (device_index < 0 || device_index >= device_count() || stop_flag.load()) return found;
  if (job.mode != NonceMode::Classic || count == 0) return found;
  if (job.prefix_ascii.size() < 142) {
    log_warn("OpenCL scan: prefix too short");
    return found;
  }

  auto& d = *devices_[static_cast<size_t>(device_index)];
  cl_command_queue q = static_cast<cl_command_queue>(d.queue);
  const cl_uint found_zero = 0;
  ResultBlobHost zero_res{};
  (void)found_zero;

  auto process_result = [&](const ResultBlobHost& result) {
    if (!result.found) return;
    ShareCandidate s;
    s.nonce_hex = format_classic_nonce(result.counter);
    for (int i = 0; i < 8; ++i) {
      s.hash[i * 4 + 0] = static_cast<uint8_t>((result.hash[i] >> 24) & 0xff);
      s.hash[i * 4 + 1] = static_cast<uint8_t>((result.hash[i] >> 16) & 0xff);
      s.hash[i * 4 + 2] = static_cast<uint8_t>((result.hash[i] >> 8) & 0xff);
      s.hash[i * 4 + 3] = static_cast<uint8_t>(result.hash[i] & 0xff);
    }
    s.timestamp_us = job.timestamp_us;
    s.gpu_index = device_index;
    // AMD OpenCL only — trust GPU digest; no host/CPU re-hash.
    if (!hash_meets_target(s.hash, job.target)) {
      log_warn("OpenCL share rejected (GPU digest above target) nonce=" + s.nonce_hex);
      return;
    }
    s.blockhash_hex = make_share_blockhash(job, s.nonce_hex, s.hash);
    found.push_back(s);
  };

  auto update_hashrate = [&](uint64_t hashed, double sec) {
    d.total_hashes.fetch_add(hashed);
    if (sec > 1e-9) {
      const double mhs = (static_cast<double>(hashed) / sec) / 1e6;
      const double prev = d.last_mhs.load();
      d.last_mhs.store(prev > 0.0 ? (prev * 0.5 + mhs * 0.5) : mhs);
    }
  };

  JobBlobHost blob{};
  const bool use_hi32 = fill_hi32_launch(d, job, start, count, &blob);

  if (use_hi32) {
    const unsigned unroll = normalize_unroll(d.tune.unroll);
    count = align_count_to_unroll(count, unroll);
    if (count == 0) return found;

    void* ker = pick_kernel(d, unroll);
    if (!ker) {
      log_warn(std::string("pick_kernel failed on ") + d.name);
      return found;
    }

    cl_mem res_cur = static_cast<cl_mem>(d.res_ping ? d.res_mem_b : d.res_mem);
    cl_mem res_prev = static_cast<cl_mem>(d.res_ping ? d.res_mem : d.res_mem_b);

    cl_int rc =
        clEnqueueWriteBuffer(q, res_cur, CL_FALSE, 0, sizeof(ResultBlobHost), &zero_res, 0, nullptr,
                             nullptr);
    if (rc != CL_SUCCESS) {
      log_warn("clEnqueueWriteBuffer(res) rc=" + std::to_string(rc));
      return found;
    }

    // Keep GPU busy via ping-pong. Sample MH/s every 4th launch with profiling/clFinish;
    // other launches only enqueue + read previous result (no finish on current).
    const bool sample = ((d.scan_launches + 1u) % 4u) == 0u;
    void* ev_out = nullptr;
    const auto tw0 = std::chrono::steady_clock::now();
    if (!enqueue_hi32(d, ker, start, count, res_cur, d.tune, sample ? &ev_out : nullptr)) {
      log_warn(std::string("enqueue_hi32 failed on ") + d.name);
      return found;
    }
    cl_event ker_ev = static_cast<cl_event>(ev_out);
    ++d.scan_launches;

    if (sample) {
      clFinish(q);
      double sec = 0;
      if (ker_ev) {
        cl_ulong t0 = 0, t1 = 0;
        if (clGetEventProfilingInfo(ker_ev, CL_PROFILING_COMMAND_START, sizeof(t0), &t0, nullptr) ==
                CL_SUCCESS &&
            clGetEventProfilingInfo(ker_ev, CL_PROFILING_COMMAND_END, sizeof(t1), &t1, nullptr) ==
                CL_SUCCESS &&
            t1 > t0) {
          sec = static_cast<double>(t1 - t0) * 1e-9;
          const unsigned ch = d.tune.chunks ? d.tune.chunks : 1;
          // Event covers last chunk only; scale when multi-chunk (equal-ish splits).
          if (ch > 1) sec *= static_cast<double>(ch);
        }
        clReleaseEvent(ker_ev);
        ker_ev = nullptr;
      }
      if (sec <= 1e-9) {
        sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - tw0).count();
      }
      update_hashrate(count, sec);
    } else {
      d.total_hashes.fetch_add(count);
    }

    if (d.res_pending) {
      ResultBlobHost result{};
      rc = clEnqueueReadBuffer(q, res_prev, CL_TRUE, 0, sizeof(result), &result, 0, nullptr,
                               nullptr);
      if (rc != CL_SUCCESS) {
        log_warn("clEnqueueReadBuffer rc=" + std::to_string(rc));
      } else {
        process_result(result);
      }
    }

    d.res_ping ^= 1;
    d.res_pending = true;
    d.blob_on_device = false;
    return found;
  }

  // Fallback: JobBlob mine_classic_fast (batch crosses hi32 window).
  d.res_pending = false;
  cl_mem job_mem = static_cast<cl_mem>(d.job_mem);
  cl_mem res_mem = static_cast<cl_mem>(d.res_mem);

  const bool need_job_upload =
      !d.blob_on_device || d.cached_ts != job.timestamp_us || d.cached_header != job.header_hash_hex;
  cl_int rc = CL_SUCCESS;
  if (need_job_upload) {
    rc = clEnqueueWriteBuffer(q, job_mem, CL_FALSE, 0, sizeof(blob), &blob, 0, nullptr, nullptr);
    if (rc != CL_SUCCESS) {
      log_warn("clEnqueueWriteBuffer(job) rc=" + std::to_string(rc));
      return found;
    }
    d.blob_on_device = true;
    d.cached_ts = job.timestamp_us;
    d.cached_header = job.header_hash_hex;
    d.cached_hi32 = 0xffffffffu;
  }

  rc = clEnqueueWriteBuffer(q, res_mem, CL_FALSE, 0, sizeof(found_zero), &found_zero, 0, nullptr,
                            nullptr);
  if (rc != CL_SUCCESS) {
    log_warn("clEnqueueWriteBuffer(res) rc=" + std::to_string(rc));
    return found;
  }

  cl_kernel ker = static_cast<cl_kernel>(d.kernel);
  size_t local = d.tune.local ? d.tune.local : 64;
  if (local > d.max_work_group) local = d.max_work_group;
  size_t global = calc_global(d, local, d.tune.intensity, count, 1);
  int arg = 0;
  rc = clSetKernelArg(ker, arg++, sizeof(cl_mem), &job_mem);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_ulong), &start);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_ulong), &count);
  rc |= clSetKernelArg(ker, arg++, sizeof(cl_mem), &res_mem);
  if (rc != CL_SUCCESS) return found;

  const auto t0 = std::chrono::steady_clock::now();
  rc = clEnqueueNDRangeKernel(q, ker, 1, nullptr, &global, &local, 0, nullptr, nullptr);
  if (rc != CL_SUCCESS) {
    rc = clEnqueueNDRangeKernel(q, ker, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
  }
  if (rc != CL_SUCCESS) {
    log_warn(std::string("clEnqueueNDRangeKernel failed on ") + d.name +
             " rc=" + std::to_string(rc));
    return found;
  }

  ResultBlobHost result{};
  rc = clEnqueueReadBuffer(q, res_mem, CL_TRUE, 0, sizeof(result), &result, 0, nullptr, nullptr);
  const auto t1 = std::chrono::steady_clock::now();
  if (rc != CL_SUCCESS) {
    log_warn("clEnqueueReadBuffer rc=" + std::to_string(rc));
    return found;
  }

  update_hashrate(count, std::chrono::duration<double>(t1 - t0).count());
  process_result(result);
  return found;
}

double OpenClBackend::bench_launch(Dev& d, const PreparedJob& job, uint64_t start, uint64_t count,
                                   const GpuTune& cfg, std::atomic<bool>& stop_flag,
                                   int timed_passes, int warm_passes) {
  if (stop_flag.load() || count == 0 || job.prefix_ascii.size() < 142) return 0;
  if (!fill_hi32_launch(d, job, start, count, nullptr)) return 0;

  const unsigned unroll = normalize_unroll(cfg.unroll);
  uint64_t work = align_count_to_unroll(count, unroll);
  if (work == 0) return 0;

  void* ker = pick_kernel(d, unroll);
  if (!ker) return 0;

  if (timed_passes < 1) timed_passes = 1;
  if (timed_passes > 9) timed_passes = 9;
  if (warm_passes < 0) warm_passes = 0;
  if (warm_passes > 4) warm_passes = 4;

  cl_command_queue q = static_cast<cl_command_queue>(d.queue);
  cl_mem res_mem = static_cast<cl_mem>(d.res_mem);
  const cl_uint found_zero = 0;

  auto one_pass = [&]() -> double {
    if (stop_flag.load()) return 0;
    clEnqueueWriteBuffer(q, res_mem, CL_FALSE, 0, sizeof(found_zero), &found_zero, 0, nullptr,
                         nullptr);
    const auto t0 = std::chrono::steady_clock::now();
    if (!enqueue_hi32(d, ker, start, work, res_mem, cfg)) return 0;
    clFinish(q);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    if (sec < 1e-6) return 0;
    return (static_cast<double>(work) / sec) / 1e6;
  };

  // Warmup (not timed) — bring clocks / power limit up.
  for (int w = 0; w < warm_passes; ++w) {
    if (stop_flag.load()) return 0;
    clEnqueueWriteBuffer(q, res_mem, CL_FALSE, 0, sizeof(found_zero), &found_zero, 0, nullptr,
                         nullptr);
    if (!enqueue_hi32(d, ker, start, work, res_mem, cfg)) return 0;
    clFinish(q);
  }
  if (stop_flag.load()) return 0;

  // Timed runs — median (stable vs GPU boost / noise).
  double samples[9] = {};
  int n = 0;
  for (int pass = 0; pass < timed_passes; ++pass) {
    const double mhs = one_pass();
    if (mhs <= 0) continue;
    samples[n++] = mhs;
  }
  d.blob_on_device = false;
  if (n == 0) return 0;
  std::sort(samples, samples + n);
  return samples[n / 2];
}

bool OpenClBackend::load_tune_cache(const std::string& path) {
  std::ifstream in(path);
  if (!in) return false;
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  int matched = 0;
  for (auto& dp : devices_) {
    auto& d = *dp;
    const std::string key = "\"name\":\"" + d.name + "\"";
    size_t pos = content.find(key);
    if (pos == std::string::npos) continue;
    auto find_num = [&](const char* field) -> uint64_t {
      const std::string pat = std::string("\"") + field + "\":";
      size_t p = content.find(pat, pos);
      if (p == std::string::npos || p > pos + 500) return 0;
      p += pat.size();
      return static_cast<uint64_t>(std::strtoull(content.c_str() + p, nullptr, 10));
    };
    auto find_bool = [&](const char* field) -> bool {
      const std::string pat = std::string("\"") + field + "\":";
      size_t p = content.find(pat, pos);
      if (p == std::string::npos || p > pos + 500) return false;
      p += pat.size();
      return content.compare(p, 4, "true") == 0;
    };
    const uint64_t local = find_num("local");
    const std::string ipat = "\"intensity\":";
    size_t ip = content.find(ipat, pos);
    if (ip == std::string::npos || ip > pos + 500 || local == 0) continue;
    const unsigned intensity =
        static_cast<unsigned>(std::strtoul(content.c_str() + ip + ipat.size(), nullptr, 10));
    const uint64_t batch = find_num("batch");
    if (batch == 0) continue;
    d.tune.local = static_cast<size_t>(local);
    d.tune.intensity = intensity;
    {
      const uint64_t un = find_num("unroll");
      if (un == 1 || un == 2 || un == 3 || un == 4 || un == 8 || un == 14) {
        d.tune.unroll = static_cast<unsigned>(un);
      } else if (find_bool("u4")) {
        d.tune.unroll = 14;  // legacy sequential u4
      } else {
        d.tune.unroll = 3;  // hasher-like default
      }
    }
    d.tune.null_local = find_bool("null_local");
    d.tune.chunks = static_cast<unsigned>(find_num("chunks"));
    if (d.tune.chunks == 0) d.tune.chunks = 1;
    d.tune.batch = batch;
    {
      const std::string pat = "\"mhs\":";
      size_t p = content.find(pat, pos);
      if (p != std::string::npos && p < pos + 500) {
        d.tune.mhs = std::strtod(content.c_str() + p + pat.size(), nullptr);
      }
    }
    d.tuned = true;
    ++matched;
    log_info("autotune cache hit gpu=" + d.name + " local=" + std::to_string(d.tune.local) +
             " intensity=" + std::to_string(d.tune.intensity) +
             " unroll=" + std::to_string(d.tune.unroll) +
             " chunks=" + std::to_string(d.tune.chunks) +
             " null_local=" + std::string(d.tune.null_local ? "1" : "0") +
             " batch=" + std::to_string(d.tune.batch) +
             " mhs=" + std::to_string(d.tune.mhs));
  }
  return matched > 0;
}

void OpenClBackend::save_tune_cache(const std::string& path) const {
  std::ostringstream ss;
  ss << "{\"version\":22,\"devices\":[";
  for (size_t i = 0; i < devices_.size(); ++i) {
    const auto& d = *devices_[i];
    if (i) ss << ",";
    ss << "{\"name\":\"" << d.name << "\",\"local\":" << d.tune.local
       << ",\"intensity\":" << d.tune.intensity << ",\"unroll\":" << d.tune.unroll
       << ",\"chunks\":" << d.tune.chunks
       << ",\"null_local\":" << (d.tune.null_local ? "true" : "false")
       << ",\"batch\":" << d.tune.batch << ",\"mhs\":" << d.tune.mhs << "}";
  }
  ss << "]}\n";
  std::ofstream out(path);
  if (!out) {
    log_warn("autotune: cannot write cache " + path);
    return;
  }
  out << ss.str();
  log_info("autotune cache saved " + path);
}

void OpenClBackend::autotune_one(Dev& d, int di, const PreparedJob& job,
                                 std::atomic<bool>& stop_flag) {
  // Hasher-aligned tune: threads(=local) × blocks(=CU*intensity) × batch × ocl variant(unroll).
  // hasher 5.2 defaults: threads=256, blocks=1024, batch=16M+, autotune ~30s+ grids.
  // Observed hasher ~3.4 GH/s/card — target that.
  constexpr double kTargetMhs = 3400.0;
  constexpr double kTargetGhs = kTargetMhs / 1000.0;

  log_info("autotune HASHER-STYLE gpu[" + std::to_string(di) + "]=" + d.name +
           " cu=" + std::to_string(d.compute_units) + " max_wg=" + std::to_string(d.max_work_group) +
           " — target ~" + std::to_string(kTargetGhs) + " GH/s (blocks×threads×batch like hasher 5.2)");

  // hasher threads grid (local size), filtered by max_wg
  std::vector<size_t> locals;
  for (size_t L : {256ull, 128ull, 192ull, 64ull, 384ull, 512ull, 96ull, 160ull, 224ull, 32ull}) {
    if (L <= d.max_work_group) locals.push_back(L);
  }
  if (locals.empty()) locals.push_back(std::min<size_t>(64, d.max_work_group));

  // Prefer hasher-like c_u32 first, then other kernels.
  const unsigned unrolls[] = {3, 1, 2, 4, 14, 8};
  // hasher CU multipliers → intensity in our model (global = CU * local * intensity)
  const unsigned block_mults[] = {2, 4, 6, 8, 12, 16, 24, 32, 48, 64};
  // absolute block counts (hasher also tries fixed blocks) → intensity ≈ blocks/CU
  const unsigned abs_blocks[] = {256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096};
  // hasher batch grid
  const uint64_t batches[] = {1ull << 24, 1ull << 25, 1ull << 26, 1ull << 27,
                              1ull << 28, 1ull << 29, 1ull << 30};
  const unsigned chunk_opts[] = {1, 2, 4};
  const uint64_t probe_batch = 1ull << 26;  // 64M probe (hasher starts 16–64M)
  const uint64_t fine_batch = 1ull << 27;   // 128M
  const uint64_t verify_batch = 1ull << 29; // 512M verify

  auto align_hi32 = [](uint64_t& start, uint64_t count) {
    const uint64_t room = (uint64_t{1} << 32) - (start & 0xffffffffull);
    if (room != 0 && room < count) start += room;
  };

  uint64_t ctr = static_cast<uint64_t>(now_us()) ^ (uint64_t(di) << 32);

  auto try_cfg = [&](GpuTune cfg, uint64_t count, int timed = 3, int warm = 2) -> double {
    if (stop_flag.load()) return 0;
    if (cfg.local == 0 || cfg.local > d.max_work_group) return 0;
    if (cfg.chunks == 0) cfg.chunks = 1;
    cfg.unroll = normalize_unroll(cfg.unroll);
    uint64_t start = ctr;
    align_hi32(start, count);
    const uint64_t room = (uint64_t{1} << 32) - (start & 0xffffffffull);
    if (count > room) count = align_count_to_unroll(room, cfg.unroll);
    if (count == 0) return 0;
    ctr = start;
    const double mhs = bench_launch(d, job, start, count, cfg, stop_flag, timed, warm);
    ctr += count;
    return mhs;
  };

  struct Cand {
    GpuTune cfg{};
    double mhs = 0;
  };
  std::vector<Cand> cands;
  cands.reserve(512);

  auto better_than = [](const Cand& a, const Cand& b) { return a.mhs > b.mhs; };
  auto same_shape = [](const GpuTune& a, const GpuTune& b) {
    return a.local == b.local && a.intensity == b.intensity && a.unroll == b.unroll &&
           a.chunks == b.chunks && a.null_local == b.null_local;
  };
  auto consider = [&](const GpuTune& cfg, double mhs) {
    if (mhs <= 0) return;
    for (auto& c : cands) {
      if (same_shape(c.cfg, cfg)) {
        if (mhs > c.mhs) {
          c.mhs = mhs;
          c.cfg = cfg;
          c.cfg.mhs = mhs;
        }
        return;
      }
    }
    GpuTune copy = cfg;
    copy.mhs = mhs;
    cands.push_back(Cand{copy, mhs});
  };
  auto top_unique = [&](size_t n) {
    std::sort(cands.begin(), cands.end(),
              [&](const Cand& a, const Cand& b) { return better_than(a, b); });
    std::vector<Cand> top;
    for (const auto& c : cands) {
      bool dup = false;
      for (const auto& t : top) {
        if (t.cfg.local == c.cfg.local && t.cfg.intensity == c.cfg.intensity &&
            t.cfg.unroll == c.cfg.unroll)
          dup = true;
      }
      if (!dup) top.push_back(c);
      if (top.size() >= n) break;
    }
    return top;
  };

  auto intensity_from_blocks = [&](unsigned blocks) -> unsigned {
    const unsigned cu = d.compute_units ? d.compute_units : 1;
    unsigned iv = blocks / cu;
    if (iv == 0) iv = 1;
    if (iv > 512) iv = 512;
    return iv;
  };

  // Phase 0: warm clocks (hasher does timed trials after brief warm)
  {
    GpuTune warm{};
    warm.local = std::min<size_t>(256, d.max_work_group);
    warm.intensity = 16;
    warm.unroll = 3;
    warm.chunks = 1;
    warm.null_local = false;
    log_info("  warm-up …");
    for (int i = 0; i < 3 && !stop_flag.load(); ++i) try_cfg(warm, 1ull << 25, 1, 0);
  }

  // Phase 1: hasher primary grid — unroll × threads × CU-mult (+ abs blocks)
  log_info("  phase1 hasher grid (probe=" + std::to_string(probe_batch) + ") …");
  for (unsigned unroll : unrolls) {
    if (stop_flag.load()) break;
    for (size_t local : locals) {
      if (stop_flag.load()) break;
      for (unsigned mult : block_mults) {
        if (stop_flag.load()) break;
        GpuTune cfg{};
        cfg.local = local;
        cfg.intensity = mult;  // = hasher blocks / CU
        cfg.unroll = unroll;
        cfg.chunks = 1;
        cfg.null_local = false;
        cfg.batch = probe_batch;
        const double mhs = try_cfg(cfg, probe_batch, 3, 1);
        if (mhs > 0) {
          const unsigned blocks = mult * d.compute_units;
          log_info("  try threads=" + std::to_string(local) + " blocks~" + std::to_string(blocks) +
                   " (×" + std::to_string(mult) + " CU) unroll=" + std::to_string(unroll) +
                   unroll_tag(unroll) + " => " + std::to_string(mhs) + " MH/s");
          consider(cfg, mhs);
        }
      }
      for (unsigned blocks : abs_blocks) {
        if (stop_flag.load()) break;
        GpuTune cfg{};
        cfg.local = local;
        cfg.intensity = intensity_from_blocks(blocks);
        cfg.unroll = unroll;
        cfg.chunks = 1;
        cfg.null_local = false;
        cfg.batch = probe_batch;
        const double mhs = try_cfg(cfg, probe_batch, 3, 1);
        if (mhs > 0) {
          log_info("  try threads=" + std::to_string(local) + " blocks=" + std::to_string(blocks) +
                   " intensity=" + std::to_string(cfg.intensity) + " unroll=" +
                   std::to_string(unroll) + unroll_tag(unroll) + " => " + std::to_string(mhs) +
                   " MH/s");
          consider(cfg, mhs);
        }
      }
      // full-span
      {
        GpuTune cfg{};
        cfg.local = local;
        cfg.intensity = 0;
        cfg.unroll = unroll;
        cfg.chunks = 1;
        cfg.batch = probe_batch;
        const double mhs = try_cfg(cfg, probe_batch, 3, 1);
        if (mhs > 0) consider(cfg, mhs);
      }
    }
  }

  if (cands.empty()) {
    log_warn("autotune failed for " + d.name + " — keeping defaults");
    d.tuned = true;
    return;
  }

  Cand best = top_unique(1).front();
  log_info("  phase1 best " + std::to_string(best.mhs / 1000.0) + " GH/s");

  // Phase 2: fine intensity around top-8 (±12 step 1) + null_local
  {
    auto top = top_unique(8);
    log_info("  phase2 fine refine …");
    for (Cand base : top) {
      if (stop_flag.load()) break;
      const unsigned bi = base.cfg.intensity;
      std::vector<unsigned> refine;
      if (bi == 0) {
        for (unsigned v : {1u, 2u, 4u, 8u, 12u, 16u, 24u, 32u, 48u, 64u, 0u}) refine.push_back(v);
      } else {
        const int lo = std::max(1, static_cast<int>(bi) - 12);
        const int hi = std::min(512, static_cast<int>(bi) + 12);
        for (int v = lo; v <= hi; ++v) refine.push_back(static_cast<unsigned>(v));
        refine.push_back(0);
      }
      std::vector<size_t> loc_try = {base.cfg.local};
      for (size_t L : {64ull, 128ull, 256ull}) {
        if (L <= d.max_work_group && L != base.cfg.local) loc_try.push_back(L);
      }
      for (size_t local : loc_try) {
        for (unsigned intensity : refine) {
          if (stop_flag.load()) break;
          GpuTune cfg = base.cfg;
          cfg.local = local;
          cfg.intensity = intensity;
          cfg.null_local = false;
          cfg.batch = fine_batch;
          const double mhs = try_cfg(cfg, fine_batch, 5, 2);
          if (mhs <= 0) continue;
          consider(cfg, mhs);
          Cand c{cfg, mhs};
          if (better_than(c, best)) {
            log_info("  refine threads=" + std::to_string(local) + " intensity=" +
                     std::to_string(intensity) + " unroll=" + std::to_string(cfg.unroll) +
                     unroll_tag(cfg.unroll) + " => " + std::to_string(mhs) + " MH/s (" +
                     std::to_string(mhs / 1000.0) + " GH/s)");
            best = c;
          }
        }
      }
      for (bool nl : {false, true}) {
        GpuTune cfg = best.cfg;
        cfg.null_local = nl;
        cfg.batch = fine_batch;
        const double mhs = try_cfg(cfg, fine_batch, 5, 2);
        if (mhs > 0) {
          consider(cfg, mhs);
          Cand c{cfg, mhs};
          if (better_than(c, best)) best = c;
        }
      }
    }
  }

  // Phase 3: chunks
  {
    Cand chunk_best = best;
    for (unsigned chunks : chunk_opts) {
      if (stop_flag.load()) break;
      GpuTune cfg = best.cfg;
      cfg.chunks = chunks;
      cfg.batch = fine_batch;
      const double mhs = try_cfg(cfg, fine_batch, 5, 2);
      if (mhs <= 0) continue;
      consider(cfg, mhs);
      Cand c{cfg, mhs};
      if (better_than(c, chunk_best)) {
        log_info("  try chunks=" + std::to_string(chunks) + " => " + std::to_string(mhs) + " MH/s");
        chunk_best = c;
      }
    }
    if (better_than(chunk_best, best)) best = chunk_best;
  }

  // Phase 4: hasher batch sweep on top-3
  {
    log_info("  phase4 batch sweep …");
    auto top = top_unique(3);
    Cand batch_best = best;
    batch_best.mhs = 0;
    for (Cand base : top) {
      for (uint64_t batch : batches) {
        if (stop_flag.load()) break;
        GpuTune cfg = base.cfg;
        cfg.batch = batch;
        const int passes = (batch >= (1ull << 28)) ? 5 : 3;
        const double mhs = try_cfg(cfg, batch, passes, 2);
        if (mhs <= 0) continue;
        consider(cfg, mhs);
        log_info("  try batch=" + std::to_string(batch) + " => " + std::to_string(mhs) + " MH/s");
        Cand c{cfg, mhs};
        if (batch_best.mhs <= 0 || better_than(c, batch_best)) batch_best = c;
      }
    }
    if (batch_best.mhs > 0 && better_than(batch_best, best)) best = batch_best;
  }

  // Phase 5: precision verify top-4 at 512M × 7 samples
  {
    log_info("  phase5 precision verify …");
    auto top = top_unique(4);
    Cand verify_best{};
    verify_best.mhs = 0;
    for (Cand base : top) {
      if (stop_flag.load()) break;
      GpuTune cfg = base.cfg;
      cfg.batch = verify_batch;
      const double mhs = try_cfg(cfg, verify_batch, 7, 3);
      if (mhs <= 0) continue;
      log_info("  verify threads=" + std::to_string(cfg.local) + " intensity=" +
               std::to_string(cfg.intensity) + " unroll=" + std::to_string(cfg.unroll) +
               unroll_tag(cfg.unroll) + " => " + std::to_string(mhs) + " MH/s (" +
               std::to_string(mhs / 1000.0) + " GH/s)");
      Cand c{cfg, mhs};
      if (verify_best.mhs <= 0 || better_than(c, verify_best)) verify_best = c;
    }
    if (verify_best.mhs > 0) best = verify_best;
  }

  // Phase 6: if below hasher~3.4, micro-polish intensity ±8 on winner
  if (best.mhs < kTargetMhs && !stop_flag.load()) {
    log_info("  phase6 below ~3.4 GH/s — micro-polish …");
    const unsigned bi = best.cfg.intensity;
    const int lo = (bi == 0) ? 1 : std::max(1, static_cast<int>(bi) - 8);
    const int hi = (bi == 0) ? 32 : std::min(512, static_cast<int>(bi) + 8);
    Cand polish = best;
    for (int v = lo; v <= hi; ++v) {
      if (stop_flag.load()) break;
      GpuTune cfg = best.cfg;
      cfg.intensity = static_cast<unsigned>(v);
      cfg.batch = verify_batch;
      const double mhs = try_cfg(cfg, verify_batch, 5, 2);
      if (mhs <= 0) continue;
      Cand c{cfg, mhs};
      if (better_than(c, polish)) {
        log_info("  polish intensity=" + std::to_string(v) + " => " + std::to_string(mhs) +
                 " MH/s");
        polish = c;
      }
    }
    if (better_than(polish, best)) best = polish;
    GpuTune cfg = best.cfg;
    cfg.batch = verify_batch;
    const double mhs = try_cfg(cfg, verify_batch, 7, 3);
    if (mhs > 0) {
      best.mhs = mhs;
      best.cfg.batch = verify_batch;
    }
  }

  best.cfg.mhs = best.mhs;
  d.tune = best.cfg;
  d.tuned = true;
  d.last_mhs.store(best.mhs);
  const double ghs = best.mhs / 1000.0;
  if (best.mhs >= kTargetMhs) {
    log_info("autotune OK ~hasher target gpu=" + d.name + " => " + std::to_string(ghs) +
             " GH/s (≥ " + std::to_string(kTargetGhs) + ")");
  } else {
    log_warn("autotune BEST gpu=" + d.name + " => " + std::to_string(ghs) +
             " GH/s — below hasher ~" + std::to_string(kTargetGhs) + " GH/s");
  }
  log_info("autotune best gpu=" + d.name + " threads=" + std::to_string(best.cfg.local) +
           " intensity=" + std::to_string(best.cfg.intensity) + " (blocks~" +
           std::to_string(best.cfg.intensity * d.compute_units) + ")" +
           " unroll=" + std::to_string(best.cfg.unroll) + unroll_tag(best.cfg.unroll) +
           " chunks=" + std::to_string(best.cfg.chunks) +
           " null_local=" + std::string(best.cfg.null_local ? "1" : "0") +
           " batch=" + std::to_string(best.cfg.batch) + " => " + std::to_string(best.mhs) +
           " MH/s (" + std::to_string(ghs) + " GH/s)");
}

void OpenClBackend::autotune(const PreparedJob& job, std::atomic<bool>& stop_flag,
                             const std::string& cache_path, bool force,
                             const std::vector<int>& only_devices) {
  auto device_wanted = [&](int di) {
    if (only_devices.empty()) return true;
    for (int sel : only_devices)
      if (sel == di) return true;
    return false;
  };

  for (int di = 0; di < device_count(); ++di) {
    if (!device_wanted(di)) devices_[static_cast<size_t>(di)]->tuned = true;
  }

  if (!force && !cache_path.empty() && load_tune_cache(cache_path)) {
    bool all = true;
    for (int di = 0; di < device_count(); ++di) {
      if (!device_wanted(di)) continue;
      if (!devices_[static_cast<size_t>(di)]->tuned) all = false;
    }
    if (all) {
      log_info("autotune: all selected GPUs loaded from cache (" + cache_path + ")");
      return;
    }
  }

  // Tune GPUs in parallel — each has its own OpenCL queue.
  std::vector<std::thread> workers;
  std::mutex log_mu;  // unused placeholder if we need later
  (void)log_mu;
  for (int di = 0; di < device_count(); ++di) {
    if (!device_wanted(di)) continue;
    auto& d = *devices_[static_cast<size_t>(di)];
    if (d.tuned && !force) continue;
    if (force) d.tuned = false;
    workers.emplace_back([&, di] {
      autotune_one(*devices_[static_cast<size_t>(di)], di, job, stop_flag);
    });
  }
  for (auto& t : workers) {
    if (t.joinable()) t.join();
  }

  if (!cache_path.empty()) save_tune_cache(cache_path);
}

}  // namespace oneminer
