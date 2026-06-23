#include "gpu_worker.hpp"

#include "device.hpp"

#include <dlfcn.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

extern "C" {
#include "../../native/pearl-gemm/csrc/capi/pearl_gemm_capi.h"
}

namespace {

using hipError_t = int;
constexpr int hipSuccess = 0;
constexpr int hipMemcpyHostToDevice = 1;
constexpr int hipMemcpyDeviceToHost = 2;
constexpr unsigned hipHostMallocDefault = 0;

using hipInitFn = hipError_t (*)(unsigned);
using hipSetDeviceFn = hipError_t (*)(int);
using hipStreamCreateFn = hipError_t (*)(void**);
using hipStreamDestroyFn = hipError_t (*)(void*);
using hipStreamSynchronizeFn = hipError_t (*)(void*);
using hipMallocFn = hipError_t (*)(void**, size_t);
using hipFreeFn = hipError_t (*)(void*);
using hipMemcpyFn = hipError_t (*)(void*, const void*, size_t, int);
using hipHostMallocFn = hipError_t (*)(void**, size_t, unsigned);
using hipHostFreeFn = hipError_t (*)(void*);

struct HipRt {
  void* lib = nullptr;
  hipInitFn init = nullptr;
  hipSetDeviceFn set_device = nullptr;
  hipStreamCreateFn stream_create = nullptr;
  hipStreamDestroyFn stream_destroy = nullptr;
  hipStreamSynchronizeFn stream_sync = nullptr;
  hipMallocFn malloc_fn = nullptr;
  hipFreeFn free_fn = nullptr;
  hipMemcpyFn memcpy_fn = nullptr;
  hipHostMallocFn host_malloc = nullptr;
  hipHostFreeFn host_free = nullptr;

  bool load() {
    const char* paths[] = {"/opt/rocm/lib/libamdhip64.so.6", "/opt/rocm-6.4.3/lib/libamdhip64.so.6",
                           "libamdhip64.so.6", "libamdhip64.so", nullptr};
    for (const char** p = paths; *p; ++p) {
      lib = dlopen(*p, RTLD_LAZY | RTLD_GLOBAL);
      if (lib) {
        break;
      }
    }
    if (!lib) {
      return false;
    }
#define LOAD(name, sym) name = reinterpret_cast<decltype(name)>(dlsym(lib, sym))
    LOAD(init, "hipInit");
    LOAD(set_device, "hipSetDevice");
    LOAD(stream_create, "hipStreamCreate");
    LOAD(stream_destroy, "hipStreamDestroy");
    LOAD(stream_sync, "hipStreamSynchronize");
    LOAD(malloc_fn, "hipMalloc");
    LOAD(free_fn, "hipFree");
    LOAD(memcpy_fn, "hipMemcpy");
    LOAD(host_malloc, "hipHostMalloc");
    LOAD(host_free, "hipHostFree");
#undef LOAD
    return init && set_device && stream_create && malloc_fn && memcpy_fn && host_malloc;
  }
};

static void* load_sym(void* lib, const char* name) { return dlsym(lib, name); }

static uint64_t sigma_seed_from_bytes(const std::vector<uint8_t>& sigma) {
  uint64_t seed = 0;
  for (size_t i = 0; i < 8 && i < sigma.size(); ++i) {
    seed |= static_cast<uint64_t>(sigma[i]) << (8 * i);
  }
  return seed;
}

static HipRt hip_from_lib(void* lib) {
  HipRt hip;
  hip.lib = lib;
  hip.memcpy_fn = reinterpret_cast<hipMemcpyFn>(dlsym(lib, "hipMemcpy"));
  hip.stream_sync = reinterpret_cast<hipStreamSynchronizeFn>(dlsym(lib, "hipStreamSynchronize"));
  hip.malloc_fn = reinterpret_cast<hipMallocFn>(dlsym(lib, "hipMalloc"));
  hip.free_fn = reinterpret_cast<hipFreeFn>(dlsym(lib, "hipFree"));
  hip.host_malloc = reinterpret_cast<hipHostMallocFn>(dlsym(lib, "hipHostMalloc"));
  hip.host_free = reinterpret_cast<hipHostFreeFn>(dlsym(lib, "hipHostFree"));
  hip.stream_destroy = reinterpret_cast<hipStreamDestroyFn>(dlsym(lib, "hipStreamDestroy"));
  return hip;
}

}  // namespace

namespace sm {

GpuWorker::GpuWorker(int device_id, GpuProfile profile, StratumClient* pool, int batch_size)
    : device_id_(device_id),
      profile_(std::move(profile)),
      pool_(pool),
      batch_size_(batch_size > 0 ? batch_size : 8) {}

GpuWorker::~GpuWorker() { stop(); }

void GpuWorker::load_libraries() {
  const char* paths[] = {"./libpearl_gemm_capi.so", "libpearl_gemm_capi.so", nullptr};
  for (const char** p = paths; *p; ++p) {
    gemm_lib_ = dlopen(*p, RTLD_LAZY | RTLD_GLOBAL);
    if (gemm_lib_) {
      break;
    }
  }
  share_lib_ = dlopen("./libsuperminer_share.so", RTLD_LAZY | RTLD_GLOBAL);
  if (!share_lib_) {
    share_lib_ = dlopen("libsuperminer_share.so", RTLD_LAZY | RTLD_GLOBAL);
  }
  if (!gemm_lib_) {
    log_error("GPU %d: libpearl_gemm_capi.so not found — build with ROCm (./build.sh)", device_id_);
  }
}

void GpuWorker::start() {
  load_libraries();
  running_ = true;
  thread_ = std::thread([this] { mining_loop(); });
}

void GpuWorker::stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  free_buffers();
}

void GpuWorker::on_job(const StratumJob& job) {
  if (job.sigma.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(job_mutex_);
  current_job_ = job;
  if (job.m > 0) {
    profile_.m = job.m;
  }
  if (job.n > 0) {
    profile_.n = job.n;
  }
  if (job.k > 0) {
    profile_.k = job.k;
  }
  if (job.r > 0) {
    profile_.r = job.r;
  }
  job_ready_.store(true);
  job_seq_.fetch_add(1);
}

bool GpuWorker::setup_device(void* stream) {
  (void)stream;
  HipRt hip;
  if (!hip.load()) {
    log_error("GPU %d: libamdhip64.so not found", device_id_);
    return false;
  }
  hip_lib_ = hip.lib;
  if (hip.init(0) != hipSuccess || hip.set_device(device_id_) != hipSuccess ||
      hip.stream_create(&stream_) != hipSuccess) {
    log_error("GPU %d: HIP init failed", device_id_);
    return false;
  }
  return true;
}

bool GpuWorker::alloc_buffers(int m, int n, int k, int r, void* stream) {
  HipRt hip = hip_from_lib(hip_lib_);
  free_buffers();
  buf_m_ = m;
  buf_n_ = n;
  buf_k_ = k;
  buf_r_ = r;

  const size_t mk = static_cast<size_t>(m) * k;
  const size_t nk = static_cast<size_t>(n) * k;
  const size_t mr = static_cast<size_t>(m) * r;
  const size_t nr = static_cast<size_t>(n) * r;
  const size_t kr = static_cast<size_t>(k) * r;
  const size_t rk = static_cast<size_t>(r) * k;

  auto scratch_fn = reinterpret_cast<int64_t (*)(int64_t, int)>(
      load_sym(gemm_lib_, "pearl_capi_get_required_scratchpad_bytes"));
  roots_bytes_ = scratch_fn ? static_cast<size_t>(scratch_fn(static_cast<int64_t>(mk), 128)) : 4096;
  const size_t nk_scratch =
      scratch_fn ? static_cast<size_t>(scratch_fn(static_cast<int64_t>(nk), 128)) : 4096;
  if (nk_scratch > roots_bytes_) {
    roots_bytes_ = nk_scratch;
  }
  const size_t leaf_bytes = ((nk + 1023) / 1024) * 32;

  auto ma = [&](void** p, size_t sz) { return hip.malloc_fn(p, sz) == hipSuccess; };
  if (!ma(&d_A_, mk) || !ma(&d_B_, nk) || !ma(&d_A_hash_, 32) || !ma(&d_B_hash_, 32) ||
      !ma(&d_key_, 32) || !ma(&d_roots_, roots_bytes_) || !ma(&d_commit_a_, 32) ||
      !ma(&d_commit_b_, 32) || !ma(&d_eal_, mr) || !ma(&d_eal_fp16_, mr * 2) ||
      !ma(&d_ear_r_, kr) || !ma(&d_ear_k_, rk) || !ma(&d_ebl_r_, kr) || !ma(&d_ebl_k_, rk) ||
      !ma(&d_ebr_, nr) || !ma(&d_ebr_fp16_, nr * 2) || !ma(&d_earx_bpeb_, nr * 2) ||
      !ma(&d_bpeb_, nk) || !ma(&d_apea_, mk) || !ma(&d_leaf_cvs_, leaf_bytes) ||
      !ma(&d_host_signal_, 8) || !ma(&d_pow_target_, 32) || !ma(&d_pow_key_, 32) ||
      hip.host_malloc(&h_header_, 640, hipHostMallocDefault) != hipSuccess) {
    log_error("GPU %d: device buffer allocation failed", device_id_);
    free_buffers();
    return false;
  }
  std::memset(h_header_, 0, 640);
  (void)stream;
  return true;
}

void GpuWorker::free_buffers() {
  if (!hip_lib_) {
    return;
  }
  HipRt hip = hip_from_lib(hip_lib_);
  auto free_dev = [&](void*& p) {
    if (p && hip.free_fn) {
      hip.free_fn(p);
      p = nullptr;
    }
  };
  free_dev(d_A_);
  free_dev(d_B_);
  free_dev(d_A_hash_);
  free_dev(d_B_hash_);
  free_dev(d_key_);
  free_dev(d_roots_);
  free_dev(d_commit_a_);
  free_dev(d_commit_b_);
  free_dev(d_eal_);
  free_dev(d_eal_fp16_);
  free_dev(d_ear_r_);
  free_dev(d_ear_k_);
  free_dev(d_ebl_r_);
  free_dev(d_ebl_k_);
  free_dev(d_ebr_);
  free_dev(d_ebr_fp16_);
  free_dev(d_earx_bpeb_);
  free_dev(d_bpeb_);
  free_dev(d_apea_);
  free_dev(d_leaf_cvs_);
  free_dev(d_host_signal_);
  free_dev(d_pow_target_);
  free_dev(d_pow_key_);
  if (h_header_ && hip.host_free) {
    hip.host_free(h_header_);
    h_header_ = nullptr;
  }
  if (stream_ && hip.stream_destroy) {
    hip.stream_destroy(stream_);
    stream_ = nullptr;
  }
}

bool GpuWorker::install_sigma(const StratumJob& job, void* workspace, void* stream) {
  (void)stream;
  if (!gemm_lib_ || !hip_lib_) {
    return false;
  }
  HipRt hip = hip_from_lib(hip_lib_);

  const int m = profile_.m;
  const int n = profile_.n;
  const int k = profile_.k;
  const int r = profile_.r;
  if (m <= 0 || n <= 0 || k <= 0 || r <= 0) {
    return false;
  }
  if (!alloc_buffers(m, n, k, r, stream_)) {
    return false;
  }

  auto lcg = reinterpret_cast<decltype(&pearl_capi_lcg_int7_fill)>(
      load_sym(gemm_lib_, "pearl_capi_lcg_int7_fill"));
  auto th_leaf = reinterpret_cast<decltype(&pearl_capi_tensor_hash_leaf_cvs)>(
      load_sym(gemm_lib_, "pearl_capi_tensor_hash_leaf_cvs"));
  auto install_b = reinterpret_cast<decltype(&pearl_capi_install_B)>(
      load_sym(gemm_lib_, "pearl_capi_install_B"));
  auto ws_install = reinterpret_cast<decltype(&pearl_capi_workspace_install_params)>(
      load_sym(gemm_lib_, "pearl_capi_workspace_install_params"));
  if (!lcg || !th_leaf || !install_b || !ws_install) {
    log_error("GPU %d: pearl_capi install symbols missing", device_id_);
    return false;
  }

  std::vector<uint8_t> key = job.job_key;
  if (key.size() < 32) {
    key.resize(32, 0);
  }
  hip.memcpy_fn(d_key_, key.data(), 32, hipMemcpyHostToDevice);
  uint32_t target_host[8] = {};
  for (size_t i = 0; i < 8 && i < job.target.size(); ++i) {
    target_host[i] = job.target[i];
  }
  hip.memcpy_fn(d_pow_target_, target_host, sizeof(target_host), hipMemcpyHostToDevice);
  hip.memcpy_fn(d_pow_key_, d_key_, 32, hipMemcpyHostToDevice);
  hip.stream_sync(stream_);

  const uint64_t sigma_seed = sigma_seed_from_bytes(job.sigma);
  if (lcg(d_A_, static_cast<int64_t>(m) * k, 0, sigma_seed, stream_) != 0) {
    return false;
  }

  const uint32_t th_threads = 128;
  const uint32_t th_stages = 2;
  const uint32_t th_leaves = 512;
  const uint32_t th_blocks =
      static_cast<uint32_t>((static_cast<int64_t>(m) * k + th_threads * 1024 - 1) /
                            (th_threads * 1024));
  if (th_leaf(reinterpret_cast<const uint8_t*>(d_A_), static_cast<uint32_t>(m * k),
              reinterpret_cast<uint8_t*>(d_A_hash_), reinterpret_cast<const uint8_t*>(d_key_),
              th_blocks, th_threads, th_stages, th_leaves, reinterpret_cast<uint8_t*>(d_roots_),
              nullptr, device_id_, stream_) != 0) {
    return false;
  }

  if (job.b_seed.size() < 32) {
    log_error("GPU %d: pool job missing 32-byte b_seed", device_id_);
    return false;
  }

  PearlCapiInstallBParams ib{};
  ib.m = m;
  ib.n = n;
  ib.k = k;
  ib.r = r;
  ib.expand_bseed = 1;
  ib.th_num_blocks = th_blocks;
  ib.th_threads = th_threads;
  ib.th_stages = th_stages;
  ib.th_leaves = th_leaves;
  ib.device_id = device_id_;
  ib.bseed = job.b_seed.data();
  ib.B = d_B_;
  ib.BHash = d_B_hash_;
  ib.Key = d_key_;
  ib.Roots = d_roots_;
  ib.AHash = d_A_hash_;
  ib.CommitA = d_commit_a_;
  ib.CommitB = d_commit_b_;
  ib.EAR_K_major = d_ear_k_;
  ib.EBL_R_major = d_ebl_r_;
  ib.EBL_K_major = d_ebl_k_;
  ib.EBR = d_ebr_;
  ib.EBR_fp16 = d_ebr_fp16_;
  ib.EARxBpEB = d_earx_bpeb_;
  ib.BpEB = d_bpeb_;
  ib.workspace = workspace;
  ib.LeafCvs = d_leaf_cvs_;

  int rc = install_b(&ib, stream_);
  if (rc != 0) {
    log_error("GPU %d: pearl_capi_install_B rc=%d", device_id_, rc);
    return false;
  }

  PearlCapiWorkspaceParams wp{};
  wp.m = m;
  wp.n = n;
  wp.k = k;
  wp.r = r;
  wp.bM = 128;
  wp.bN = 128;
  wp.bK = 32;
  wp.cM = 16;
  wp.cN = 16;
  wp.th_num_blocks = th_blocks;
  wp.th_threads = th_threads;
  wp.th_stages = th_stages;
  wp.th_leaves = th_leaves;
  wp.sigma_seed = sigma_seed;
  wp.A = d_A_;
  wp.B = d_B_;
  wp.AHash = d_A_hash_;
  wp.BHash = d_B_hash_;
  wp.Key = d_key_;
  wp.Roots = d_roots_;
  wp.CommitA = d_commit_a_;
  wp.CommitB = d_commit_b_;
  wp.EAL = d_eal_;
  wp.EAL_fp16 = d_eal_fp16_;
  wp.EBR = d_ebr_;
  wp.EBR_fp16 = d_ebr_fp16_;
  wp.EAR_R_major = d_ear_r_;
  wp.EBL_R_major = d_ebl_r_;
  wp.EAR_K_major = d_ear_k_;
  wp.EBL_K_major = d_ebl_k_;
  wp.EARxBpEB_fp16 = d_earx_bpeb_;
  wp.ApEA = d_apea_;
  wp.BpEB = d_bpeb_;
  wp.host_signal_sync = d_host_signal_;
  wp.pow_target = d_pow_target_;
  wp.pow_key = d_pow_key_;

  rc = ws_install(workspace, &wp);
  if (rc != 0) {
    log_error("GPU %d: workspace_install_params rc=%d", device_id_, rc);
    return false;
  }
  log_info("GPU %d: sigma installed (m=%d n=%d k=%d r=%d)", device_id_, m, n, k, r);
  return true;
}

bool GpuWorker::run_batch(void* workspace, void* stream, uint64_t seed_start, int count) {
  auto iter_fn = reinterpret_cast<decltype(&pearl_capi_iter)>(load_sym(gemm_lib_, "pearl_capi_iter"));
  auto iter_batch = reinterpret_cast<decltype(&pearl_capi_iter_batch)>(
      load_sym(gemm_lib_, "pearl_capi_iter_batch"));
  auto memset_fn = reinterpret_cast<hipError_t (*)(void*, int, size_t, void*)>(
      dlsym(hip_lib_, "hipMemsetAsync"));
  HipRt hip = hip_from_lib(hip_lib_);
  if (!iter_fn || !hip.stream_sync || !hip.memcpy_fn) {
    return false;
  }

  if (memset_fn) {
    memset_fn(d_host_signal_, 0, 8, stream);
  }

  int rc = 0;
  if (iter_batch && count > 1) {
    rc = iter_batch(workspace, seed_start, nullptr, count, stream);
  } else {
    for (int i = 0; i < count; ++i) {
      rc = iter_fn(workspace, seed_start + static_cast<uint64_t>(i), nullptr, stream);
      if (rc != 0) {
        break;
      }
    }
  }
  if (rc != 0) {
    log_error("GPU %d: pearl_capi_iter rc=%d", device_id_, rc);
    return false;
  }
  hip.stream_sync(stream);

  int32_t sig = 0;
  hip.memcpy_fn(&sig, d_host_signal_, sizeof(sig), hipMemcpyDeviceToHost);
  if (sig == 0) {
    return true;
  }

  StratumJob job;
  {
    std::lock_guard<std::mutex> lock(job_mutex_);
    job = current_job_;
  }

  for (int i = 0; i < count; ++i) {
    const uint64_t nonce = seed_start + static_cast<uint64_t>(i);
    if (memset_fn) {
      memset_fn(d_host_signal_, 0, 8, stream);
    }
    std::memset(h_header_, 0, 640);
    rc = iter_fn(workspace, nonce, h_header_, stream);
    if (rc != 0) {
      return false;
    }
    hip.stream_sync(stream);
    if (static_cast<uint8_t*>(h_header_)[0] == 1) {
      handle_hit(job, h_header_, nonce, profile_.m, profile_.n, profile_.k, profile_.r, stream);
      break;
    }
  }
  return true;
}

bool GpuWorker::handle_hit(const StratumJob& job, void* header_pinned, uint64_t nonce, int m, int n,
                           int k, int r, void* stream) {
  (void)r;
  (void)stream;
  if (!pool_ || !share_lib_ || !header_pinned) {
    return false;
  }

  const auto* hdr = static_cast<const uint8_t*>(header_pinned);
  const unsigned* tile = reinterpret_cast<const unsigned*>(hdr + 40);
  const unsigned ti = tile[0];
  const unsigned tj = tile[1];

  HipRt hip = hip_from_lib(hip_lib_);
  std::vector<uint8_t> a_rows(static_cast<size_t>(16) * k);
  std::vector<uint8_t> b_rows(static_cast<size_t>(16) * k);
  for (int row = 0; row < 16; ++row) {
    hip.memcpy_fn(a_rows.data() + static_cast<size_t>(row) * k,
                  static_cast<const uint8_t*>(d_A_) + static_cast<size_t>(ti * 16 + row) * k,
                  static_cast<size_t>(k), hipMemcpyDeviceToHost);
    hip.memcpy_fn(b_rows.data() + static_cast<size_t>(row) * k,
                  static_cast<const uint8_t*>(d_bpeb_) + static_cast<size_t>(tj * 16 + row) * k,
                  static_cast<size_t>(k), hipMemcpyDeviceToHost);
  }

  uint32_t a_idx[16];
  uint32_t b_idx[16];
  for (int i = 0; i < 16; ++i) {
    a_idx[i] = ti * 16 + static_cast<uint32_t>(i);
    b_idx[i] = tj * 16 + static_cast<uint32_t>(i);
  }

  using build_fn_t = int (*)(uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*, size_t,
                             const uint8_t*, size_t, const uint8_t*, size_t, const uint32_t*,
                             size_t, const uint32_t*, size_t, uint8_t**, size_t*, char**);
  auto build = reinterpret_cast<build_fn_t>(
      load_sym(share_lib_, "superminer_share_build_plain_proof"));
  auto free_buf = reinterpret_cast<void (*)(uint8_t*, size_t)>(
      load_sym(share_lib_, "superminer_share_free_buffer"));
  if (!build || !free_buf) {
    return false;
  }

  std::vector<uint8_t> key = job.job_key;
  if (key.empty()) {
    key.resize(32, 0);
  }

  uint8_t* out = nullptr;
  size_t out_len = 0;
  char* err = nullptr;
  int rc = build(static_cast<uint32_t>(m), static_cast<uint32_t>(n), static_cast<uint32_t>(k),
                 static_cast<uint32_t>(profile_.r), key.data(), key.size(), a_rows.data(),
                 a_rows.size(), b_rows.data(), b_rows.size(), a_idx, 16, b_idx, 16, &out, &out_len,
                 &err);
  if (rc != 0) {
    log_error("GPU %d: plain_proof failed rc=%d", device_id_, rc);
    return false;
  }

  const std::string b64 = base64_encode(out, out_len);
  free_buf(out, out_len);
  pool_->submit_share(job.job_id.empty() ? "0" : job.job_id, b64);
  log_info("GPU %d: share submitted nonce=%llu tile=(%u,%u)", device_id_,
           static_cast<unsigned long long>(nonce), ti, tj);
  return true;
}

void GpuWorker::mining_loop() {
  if (!gemm_lib_) {
    return;
  }
  if (!setup_device(nullptr)) {
    return;
  }

  auto ws_alloc = reinterpret_cast<decltype(&pearl_capi_workspace_alloc)>(
      load_sym(gemm_lib_, "pearl_capi_workspace_alloc"));
  auto ws_free = reinterpret_cast<decltype(&pearl_capi_workspace_free)>(
      load_sym(gemm_lib_, "pearl_capi_workspace_free"));
  if (!ws_alloc || !ws_free) {
    return;
  }

  void* workspace = nullptr;
  uint64_t installed_seq = 0;
  uint64_t nonce = static_cast<uint64_t>(device_id_) << 48;
  auto t0 = std::chrono::steady_clock::now();
  uint64_t attempts = 0;
  auto wait_log = std::chrono::steady_clock::now();

  while (running_) {
    if (!job_ready_.load()) {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration<double>(now - wait_log).count() >= 30.0) {
        log_info("GPU %d: waiting for pearl.set_mining_params from pool...", device_id_);
        wait_log = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    const uint64_t seq = job_seq_.load();
    if (seq != installed_seq) {
      StratumJob job;
      {
        std::lock_guard<std::mutex> lock(job_mutex_);
        job = current_job_;
      }
      if (workspace) {
        ws_free(workspace, stream_);
        workspace = nullptr;
      }
      if (ws_alloc(profile_.m, profile_.n, profile_.k, profile_.r, 1, 1, &workspace, stream_) !=
          0) {
        break;
      }
      if (!install_sigma(job, workspace, stream_)) {
        break;
      }
      installed_seq = seq;
      nonce = static_cast<uint64_t>(device_id_) << 48;
    }

    if (!run_batch(workspace, stream_, nonce, batch_size_)) {
      break;
    }
    nonce += static_cast<uint64_t>(batch_size_);
    attempts += static_cast<uint64_t>(batch_size_);

    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    if (secs >= 5.0) {
      hashrate_khs_.store((attempts / secs) * 1000000.0);
      t0 = t1;
      attempts = 0;
    }
  }

  if (workspace) {
    ws_free(workspace, stream_);
  }
  free_buffers();
}

MinerApp::MinerApp(MiningConfig cfg) : cfg_(std::move(cfg)) {}

int MinerApp::self_test() {
  void* gemm = dlopen("./libpearl_gemm_capi.so", RTLD_LAZY);
  if (!gemm) {
    gemm = dlopen("libpearl_gemm_capi.so", RTLD_LAZY);
  }
  if (!gemm) {
    log_error("self-test: libpearl_gemm_capi.so not found");
    return 1;
  }
  HipRt hip;
  if (!hip.load()) {
    log_error("self-test: HIP runtime missing");
    return 1;
  }
  if (hip.init(0) != hipSuccess) {
    log_info("self-test: gemm libs OK (hipInit skipped — no ROCm driver/GPU here)");
    return 0;
  }
  auto device_count_fn = reinterpret_cast<hipError_t (*)(int*)>(dlsym(hip.lib, "hipGetDeviceCount"));
  int dev_count = 0;
  if (device_count_fn) {
    device_count_fn(&dev_count);
  }
  if (dev_count <= 0 || hip.set_device(0) != hipSuccess) {
    log_info("self-test: GPU libs OK (no ROCm device in this environment)");
    return 0;
  }
  void* stream = nullptr;
  void* ws = nullptr;
  auto ws_alloc = reinterpret_cast<decltype(&pearl_capi_workspace_alloc)>(
      dlsym(gemm, "pearl_capi_workspace_alloc"));
  auto ws_free =
      reinterpret_cast<decltype(&pearl_capi_workspace_free)>(dlsym(gemm, "pearl_capi_workspace_free"));
  if (!ws_alloc || !ws_free || hip.stream_create(&stream) != hipSuccess ||
      ws_alloc(4096, 32768, 4096, 256, 1, 1, &ws, stream) != 0) {
    log_error("self-test: workspace alloc failed");
    return 1;
  }
  ws_free(ws, stream);
  hip.stream_destroy(stream);
  log_info("self-test: OK");
  return 0;
}

void MinerApp::start_stats_exporter() {
  stats_thread_ = std::thread([this] {
    while (running_) {
      export_hive_stats();
      std::this_thread::sleep_for(std::chrono::seconds(3));
    }
  });
}

void MinerApp::export_hive_stats() {
  std::ostringstream json;
  json << "{\"hs\":[";
  double total = 0;
  for (size_t i = 0; i < workers_.size(); ++i) {
    if (i) {
      json << ',';
    }
    double h = workers_[i]->hashrate_khs();
    total += h;
    json << h;
  }
  json << "],\"hs_units\":\"khs\",\"total_khs\":" << total
       << ",\"ver\":\"" << kVersion << "\",\"algo\":\"pearl\"}";
  std::ofstream f("/var/run/hive-miner-superminer.stats.json");
  f << json.str();
}

int MinerApp::run() {
  auto profiles = detect_amd_devices(cfg_.devices);
  if (profiles.empty()) {
    log_error("no AMD GPUs detected");
    return 1;
  }
  for (auto& p : profiles) {
    if (cfg_.m > 0) {
      p.m = cfg_.m;
      p.n = cfg_.n;
      p.k = cfg_.k;
      p.r = cfg_.r;
    }
  }

  pool_ = std::make_unique<StratumClient>(cfg_);
  pool_->set_share_result_callback([](bool ok, const std::string& reason) {
    log_info("share %s (%s)", ok ? "accepted" : "rejected", reason.c_str());
  });

  std::vector<GpuWorker*> worker_ptrs;
  for (size_t i = 0; i < profiles.size(); ++i) {
    workers_.push_back(std::make_unique<GpuWorker>(static_cast<int>(i), profiles[i], pool_.get(),
                                                   cfg_.batch_size));
    worker_ptrs.push_back(workers_.back().get());
  }
  pool_->set_job_callback([worker_ptrs](const StratumJob& job) {
    for (auto* w : worker_ptrs) {
      w->on_job(job);
    }
  });

  if (!pool_->connect()) {
    return 1;
  }

  for (auto& w : workers_) {
    w->start();
  }

  running_ = true;
  start_stats_exporter();
  while (pool_->running()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  running_ = false;
  for (auto& w : workers_) {
    w->stop();
  }
  if (stats_thread_.joinable()) {
    stats_thread_.join();
  }
  return 0;
}

}  // namespace sm
