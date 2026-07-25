#pragma once
#include "pow.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace oneminer {

struct DeviceStatus {
  std::string backend;  // CPU / OPENCL / CUDA
  int index = 0;
  std::string name;
  double hashrate_mhs = 0;
  uint64_t total_hashes = 0;
  uint64_t accepted = 0;
  uint64_t stale = 0;
  uint64_t rejected = 0;
  double power_w = 0;
};

struct MinerStats {
  std::vector<DeviceStatus> devices;
  uint64_t accepted = 0;
  uint64_t rejected = 0;
  uint64_t stale = 0;
  double total_mhs = 0;
  int64_t uptime_seconds = 0;
  std::string version;
  std::string nonce_mode;
};

class CpuBackend {
 public:
  explicit CpuBackend(int threads);

  void set_job(const PreparedJob& job);
  std::vector<ShareCandidate> scan(uint64_t start, uint64_t count, int device_index,
                                   std::atomic<bool>& stop_flag);
  std::vector<ShareCandidate> scan(uint64_t start, uint64_t count, int device_index,
                                   const PreparedJob& job, std::atomic<bool>& stop_flag);

  double last_mhs() const { return last_mhs_; }
  uint64_t hashes() const { return hashes_; }
  int threads() const { return threads_; }
  std::string name() const { return name_; }

 private:
  int threads_ = 1;
  PreparedJob job_{};
  std::atomic<uint64_t> hashes_{0};
  double last_mhs_ = 0;
  std::string name_ = "Host CPU";
};

#ifdef ONE_MINER_HAS_OPENCL
struct GpuTune {
  size_t local = 64;
  unsigned intensity = 64;  // global ≈ cu * local * intensity; 0 = full-span mode
  bool use_u4 = false;      // false = hi32_u1, true = hi32 (4-way)
  unsigned chunks = 1;      // split one batch into N back-to-back kernel launches
  bool null_local = false;  // let the ICD pick local size
  uint64_t batch = 1ull << 26;
  double mhs = 0;
};

class OpenClBackend {
 public:
  OpenClBackend();
  ~OpenClBackend();
  bool init(std::string& err);
  int device_count() const { return static_cast<int>(devices_.size()); }
  std::string device_name(int i) const;
  GpuTune tune(int i) const;
  uint64_t tuned_batch(int i) const;
  bool apply_tune_cache(const std::string& path);

  // Benchmark launch params per GPU; writes cache file when path non-empty.
  // If only_devices is non-empty, only those indices are tuned.
  void autotune(const PreparedJob& job, std::atomic<bool>& stop_flag,
                const std::string& cache_path, bool force,
                const std::vector<int>& only_devices = {});

  std::vector<ShareCandidate> scan(int device_index, uint64_t start, uint64_t count,
                                   const PreparedJob& job, std::atomic<bool>& stop_flag);
  double last_mhs(int device_index) const;
  uint64_t total_hashes(int device_index) const;

 private:
  struct Dev {
    void* device_id = nullptr;
    void* context = nullptr;
    void* queue = nullptr;
    void* program = nullptr;
    void* kernel = nullptr;         // mine_classic_fast
    void* kernel_hi32 = nullptr;    // mine_classic_hi32 (u4)
    void* kernel_hi32_u1 = nullptr; // mine_classic_hi32_u1
    void* job_mem = nullptr;
    void* res_mem = nullptr;
    std::string name;
    size_t max_work_group = 256;
    unsigned compute_units = 1;
    std::atomic<double> last_mhs{0};
    std::atomic<uint64_t> total_hashes{0};
    int64_t cached_ts = 0;
    uint32_t cached_hi32 = 0xffffffffu;
    std::string cached_header;
    bool blob_on_device = false;
    GpuTune tune{};
    bool tuned = false;
  };
  std::vector<std::unique_ptr<Dev>> devices_;
  std::string kernel_source_;

  // Timed kernel run; returns MH/s (0 on failure). Does not update share stats.
  double bench_launch(Dev& d, const PreparedJob& job, uint64_t start, uint64_t count,
                      const GpuTune& cfg, std::atomic<bool>& stop_flag);
  static size_t calc_global(const Dev& d, size_t local, unsigned intensity, uint64_t count,
                            bool use_u4);
  bool enqueue_hi32(Dev& d, void* ker, uint64_t start, uint64_t count, uint32_t h0, uint32_t h1,
                    const GpuTune& cfg);
  bool load_tune_cache(const std::string& path);
  void save_tune_cache(const std::string& path) const;
  void autotune_one(Dev& d, int di, const PreparedJob& job, std::atomic<bool>& stop_flag);
};
#endif

void write_stats_file(const MinerStats& stats, const std::string& path);
std::string format_gpu_status_table(const MinerStats& stats);

}  // namespace oneminer
