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
  // Scan a counter range; returns found shares.
  std::vector<ShareCandidate> scan(uint64_t start, uint64_t count, int device_index,
                                   std::atomic<bool>& stop_flag);

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
class OpenClBackend {
 public:
  OpenClBackend();
  bool init(std::string& err);
  int device_count() const { return static_cast<int>(devices_.size()); }
  std::string device_name(int i) const;
  void set_job(const PreparedJob& job);
  std::vector<ShareCandidate> scan(int device_index, uint64_t start, uint64_t count,
                                   std::atomic<bool>& stop_flag);
  double last_mhs(int device_index) const;
  uint64_t total_hashes(int device_index) const;

 private:
  struct Dev {
    void* device_id = nullptr;
    void* context = nullptr;
    void* queue = nullptr;
    void* program = nullptr;
    void* kernel = nullptr;
    std::string name;
    size_t max_work_group = 256;
    unsigned compute_units = 1;
    std::atomic<double> last_mhs{0};
    std::atomic<uint64_t> total_hashes{0};
  };
  std::vector<std::unique_ptr<Dev>> devices_;
  PreparedJob job_{};
  mutable std::mutex job_mu_;
  std::string kernel_source_;
};
#endif

void write_stats_file(const MinerStats& stats, const std::string& path);
std::string format_gpu_status_table(const MinerStats& stats);

}  // namespace oneminer
