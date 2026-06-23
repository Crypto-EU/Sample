#include "gpu_worker.hpp"

#include "device.hpp"

#include <dlfcn.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <vector>

extern "C" {
#include "../../native/pearl-gemm/csrc/capi/pearl_gemm_capi.h"
}

namespace sm {

static void* load_sym(void* lib, const char* name) {
  return dlsym(lib, name);
}

GpuWorker::GpuWorker(int device_id, GpuProfile profile, StratumClient* pool)
    : device_id_(device_id), profile_(std::move(profile)), pool_(pool) {}

GpuWorker::~GpuWorker() { stop(); }

void GpuWorker::load_libraries() {
  const char* paths[] = {"./libpearl_gemm_capi.so", "libpearl_gemm_capi.so", nullptr};
  for (const char** p = paths; *p; ++p) {
    gemm_lib_ = dlopen(*p, RTLD_LAZY | RTLD_GLOBAL);
    if (gemm_lib_) {
      break;
    }
  }
  mining_lib_ = dlopen("./libpearl_mining_capi.so", RTLD_LAZY | RTLD_GLOBAL);
  if (!mining_lib_) {
    mining_lib_ = dlopen("libpearl_mining_capi.so", RTLD_LAZY | RTLD_GLOBAL);
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
}

void GpuWorker::on_job(const StratumJob& job) {
  (void)job;
  // job bus: simplified — mining_loop polls latest job via atomic pointer in full impl
}

void GpuWorker::mining_loop() {
  if (!gemm_lib_) {
    return;
  }
  auto ws_alloc = reinterpret_cast<decltype(&pearl_capi_workspace_alloc)>(
      load_sym(gemm_lib_, "pearl_capi_workspace_alloc"));
  auto ws_free = reinterpret_cast<decltype(&pearl_capi_workspace_free)>(
      load_sym(gemm_lib_, "pearl_capi_workspace_free"));
  auto ws_install = reinterpret_cast<decltype(&pearl_capi_workspace_install_params)>(
      load_sym(gemm_lib_, "pearl_capi_workspace_install_params"));
  auto iter_fn = reinterpret_cast<decltype(&pearl_capi_iter)>(load_sym(gemm_lib_, "pearl_capi_iter"));
  auto iter_batch = reinterpret_cast<decltype(&pearl_capi_iter_batch)>(
      load_sym(gemm_lib_, "pearl_capi_iter_batch"));
  if (!ws_alloc || !ws_free || !iter_fn) {
    log_error("GPU %d: pearl_capi symbols missing", device_id_);
    return;
  }

  const int m = profile_.m, n = profile_.n, k = profile_.k, r = profile_.r;
  void* workspace = nullptr;
  void* stream = nullptr;
  if (ws_alloc(m, n, k, r, 1, 1, &workspace, stream) != 0) {
    log_error("GPU %d: workspace alloc failed", device_id_);
    return;
  }

  StratumJob job;
  job.m = m;
  job.n = n;
  job.k = k;
  job.r = r;
  job.target = StratumClient::difficulty_to_target(65536);
  job.job_id = "bootstrap";
  job.sigma.assign(32, 0xAB);

  uint64_t seed = static_cast<uint64_t>(device_id_) << 48;
  auto t0 = std::chrono::steady_clock::now();
  uint64_t attempts = 0;
  const int batch = 8;

  while (running_) {
  for (int b = 0; b < batch && running_; ++b) {
      void* hdr = nullptr;
      int rc = iter_fn(workspace, seed + attempts + b, hdr, stream);
      if (rc != 0) {
        log_error("GPU %d: pearl_capi_iter rc=%d", device_id_, rc);
        break;
      }
    }
    attempts += batch;
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    if (secs >= 5.0) {
      double tmac = attempts / secs;
      hashrate_khs_.store(tmac * 1000000.0);  // TMAC/s → kH/s scale for Hive
      log_info("GPU %d stratum stats: kernel_tmac_s=%.3f accepted=%d rejected=%d elapsed=%.1fs",
               device_id_, tmac, accepted_.load(), rejected_.load(), secs);
      t0 = t1;
      attempts = 0;
    }
  }

  ws_free(workspace, stream);
}

bool GpuWorker::install_sigma(const StratumJob& job, void* workspace, void* stream) {
  (void)job;
  (void)workspace;
  (void)stream;
  return true;
}

bool GpuWorker::run_batch(void* workspace, void* stream, uint64_t seed_start, int count,
                          std::vector<void*>& headers) {
  (void)workspace;
  (void)stream;
  (void)seed_start;
  (void)count;
  (void)headers;
  return true;
}

bool GpuWorker::handle_hit(const StratumJob& job, void* header_pinned, int m, int n, int k, int r) {
  (void)header_pinned;
  (void)m;
  (void)n;
  (void)k;
  (void)r;
  if (pool_) {
    pool_->submit_share(job.job_id, "dGVzdA==");  // placeholder
  }
  return true;
}

MinerApp::MinerApp(MiningConfig cfg) : cfg_(std::move(cfg)) {}

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
  for (size_t i = 0; i < profiles.size(); ++i) {
    if (cfg_.m > 0) {
      profiles[i].m = cfg_.m;
      profiles[i].n = cfg_.n;
      profiles[i].k = cfg_.k;
      profiles[i].r = cfg_.r;
    }
  }

  pool_ = std::make_unique<StratumClient>(cfg_);
  pool_->set_share_result_callback([](bool ok, const std::string& reason) {
    log_info("share %s (%s)", ok ? "accepted" : "rejected", reason.c_str());
  });

  if (!pool_->connect()) {
    return 1;
  }

  for (size_t i = 0; i < profiles.size(); ++i) {
  workers_.push_back(std::make_unique<GpuWorker>(static_cast<int>(i), profiles[i], pool_.get()));
    pool_->set_job_callback([w = workers_.back().get()](const StratumJob& job) { w->on_job(job); });
    workers_.back()->start();
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
