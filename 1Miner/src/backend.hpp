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
struct JobBlobHost {
  uint32_t midstate[8];
  uint32_t block0[16];
  uint32_t target[8];
  uint32_t work_after_r2[8];
  uint32_t work_after_r4[8];
  uint32_t flags;
  uint32_t _pad;
};

struct GpuTune {
  size_t local = 64;
  unsigned intensity = 8;   // global ≈ cu * local * intensity; 0 = full-span
  unsigned unroll = 1;      // 1, 2=ilp2, 4=ilp4, 8=u8, 14=seq u4
  unsigned chunks = 1;
  bool null_local = false;
  uint64_t batch = 1ull << 28;
  double mhs = 0;
};

struct Hi32Launch {
  uint32_t mid[8]{};
  uint32_t wr[8]{};
  uint32_t tgt[8]{};
  uint32_t fw0 = 0, fw1 = 0, fw2 = 0, fw3 = 0, fw4 = 0, h1_lo = 0;
  // Round-5 closed form: a1 = A5c + w5, e1 = E5c + w5 (from work_after_r4).
  uint32_t A5c = 0, E5c = 0;
  // W schedule words 16..19 (depend only on fw0..fw4 + bitlen).
  uint32_t pre_w0 = 0, pre_w1 = 0, pre_w2 = 0, pre_w3 = 0;
  // W20/W21 crumbs: w20 = pre_c20 + SSIG0(w5); w21 = pre_s21 + SSIG0(w6) + w5.
  uint32_t pre_c20 = 0, pre_s21 = 0;
  bool valid = false;
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
    void* kernel = nullptr;          // mine_classic_fast (JobBlob fallback)
    void* kernel_hi32_u1 = nullptr;  // scalar u1
    void* kernel_hi32 = nullptr;     // scalar u4
    void* kernel_hi32_u8 = nullptr;  // scalar u8
    void* kernel_hi32_ilp2 = nullptr;  // dual-nonce ILP (unroll==2)
    void* kernel_hi32_ilp4 = nullptr;  // quad-nonce ILP (unroll==4)
    void* job_mem = nullptr;         // only for fast fallback
    void* res_mem = nullptr;
    void* res_mem_b = nullptr;       // ping-pong
    int res_ping = 0;
    bool res_pending = false;
    unsigned scan_launches = 0;      // for every-Nth profiling sample
    Hi32Launch hi{};
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

  double bench_launch(Dev& d, const PreparedJob& job, uint64_t start, uint64_t count,
                      const GpuTune& cfg, std::atomic<bool>& stop_flag, int timed_passes = 3,
                      int warm_passes = 2);
  static size_t calc_global(const Dev& d, size_t local, unsigned intensity, uint64_t count,
                            unsigned unroll);
  void* pick_kernel(Dev& d, unsigned unroll) const;
  bool fill_hi32_launch(Dev& d, const PreparedJob& job, uint64_t start, uint64_t count,
                        JobBlobHost* out_blob);
  bool enqueue_hi32(Dev& d, void* ker, uint64_t start, uint64_t count, void* res,
                    const GpuTune& cfg, void** out_event = nullptr);
  static int set_scalar_hi32_args(void* ker, const Hi32Launch& L, uint64_t start, uint64_t count,
                                  void* res);
  bool load_tune_cache(const std::string& path);
  void save_tune_cache(const std::string& path) const;
  void autotune_one(Dev& d, int di, const PreparedJob& job, std::atomic<bool>& stop_flag);
};
#endif

void write_stats_file(const MinerStats& stats, const std::string& path);
std::string format_gpu_status_table(const MinerStats& stats);

}  // namespace oneminer
