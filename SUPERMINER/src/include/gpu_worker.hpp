#pragma once

#include "superminer.hpp"
#include "stratum.hpp"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

struct PearlCapiWorkspaceParams;

namespace sm {

class GpuWorker {
 public:
  GpuWorker(int device_id, GpuProfile profile, StratumClient* pool);
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
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::atomic<double> hashrate_khs_{0};
  std::atomic<int> accepted_{0};
  std::atomic<int> rejected_{0};

  void* gemm_lib_ = nullptr;
  void* mining_lib_ = nullptr;
  void* share_lib_ = nullptr;

  void load_libraries();
  void mining_loop();
  bool install_sigma(const StratumJob& job, void* workspace, void* stream);
  bool run_batch(void* workspace, void* stream, uint64_t seed_start, int count,
                 std::vector<void*>& headers);
  bool handle_hit(const StratumJob& job, void* header_pinned, int m, int n, int k, int r);
};

class MinerApp {
 public:
  explicit MinerApp(MiningConfig cfg);
  int run();

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
