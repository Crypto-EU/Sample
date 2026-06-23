#pragma once

#include "superminer.hpp"
#include "stratum.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct PearlCapiWorkspaceParams;
struct PearlCapiInstallBParams;

namespace sm {

class GpuWorker {
 public:
  GpuWorker(int device_id, GpuProfile profile, StratumClient* pool, int batch_size = 8);
  ~GpuWorker();

  void start();
  void stop();
  void on_job(const StratumJob& job);

  double hashrate_khs() const { return hashrate_khs_.load(); }
  int accepted() const { return accepted_.load(); }
  int rejected() const { return rejected_.load(); }

 private:
  int device_id_;
  GpuProfile profile_;
  StratumClient* pool_;
  int batch_size_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::atomic<double> hashrate_khs_{0};
  std::atomic<int> accepted_{0};
  std::atomic<int> rejected_{0};

  std::mutex job_mutex_;
  StratumJob current_job_;
  std::atomic<bool> job_ready_{false};
  std::atomic<uint64_t> job_seq_{0};

  void* gemm_lib_ = nullptr;
  void* share_lib_ = nullptr;
  void* hip_lib_ = nullptr;

  void load_libraries();
  void mining_loop();
  bool setup_device(void* stream);
  bool alloc_buffers(int m, int n, int k, int r, void* stream);
  void free_buffers();
  bool install_sigma(const StratumJob& job, void* workspace, void* stream);
  bool run_batch(void* workspace, void* stream, uint64_t seed_start, int count);
  bool handle_hit(const StratumJob& job, void* header_pinned, uint64_t nonce, int m, int n, int k,
                  int r, void* stream);

  // Device pointers (HIP)
  void* stream_ = nullptr;
  void* d_A_ = nullptr;
  void* d_B_ = nullptr;
  void* d_A_hash_ = nullptr;
  void* d_B_hash_ = nullptr;
  void* d_key_ = nullptr;
  void* d_roots_ = nullptr;
  void* d_commit_a_ = nullptr;
  void* d_commit_b_ = nullptr;
  void* d_eal_ = nullptr;
  void* d_eal_fp16_ = nullptr;
  void* d_ear_r_ = nullptr;
  void* d_ear_k_ = nullptr;
  void* d_ebl_r_ = nullptr;
  void* d_ebl_k_ = nullptr;
  void* d_ebr_ = nullptr;
  void* d_ebr_fp16_ = nullptr;
  void* d_earx_bpeb_ = nullptr;
  void* d_bpeb_ = nullptr;
  void* d_apea_ = nullptr;
  void* d_leaf_cvs_ = nullptr;
  void* d_host_signal_ = nullptr;
  void* d_pow_target_ = nullptr;
  void* d_pow_key_ = nullptr;
  void* h_header_ = nullptr;
  size_t roots_bytes_ = 0;
  int buf_m_ = 0;
  int buf_n_ = 0;
  int buf_k_ = 0;
  int buf_r_ = 0;
};

class MinerApp {
 public:
  explicit MinerApp(MiningConfig cfg);
  int run();
  static int self_test();

 private:
  MiningConfig cfg_;
  std::unique_ptr<StratumClient> pool_;
  std::vector<std::unique_ptr<GpuWorker>> workers_;
  std::thread stats_thread_;
  std::atomic<bool> running_{false};

  void start_stats_exporter();
  void export_hive_stats();
};

}  // namespace sm
