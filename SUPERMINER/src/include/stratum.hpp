#pragma once

#include "superminer.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace sm {

struct StratumJob {
  std::string job_id;
  std::vector<uint8_t> sigma;
  std::vector<uint8_t> b_seed;
  std::vector<uint8_t> job_key;
  std::vector<uint32_t> target;  // 8 words, big-endian uint32
  uint32_t difficulty = 0;
  int m = 0, n = 0, k = 0, r = 256;
};

using JobCallback = std::function<void(const StratumJob&)>;
using ShareResultCallback = std::function<void(bool accepted, const std::string& reason)>;

class StratumClient {
 public:
  StratumClient(MiningConfig cfg);
  ~StratumClient();

  void set_job_callback(JobCallback cb);
  void set_share_result_callback(ShareResultCallback cb) { share_cb_ = std::move(cb); }

  bool connect();
  void disconnect();
  bool submit_share(const std::string& job_id, const std::string& plain_proof_b64);
  bool running() const { return running_; }

  static std::vector<uint32_t> difficulty_to_target(double diff);

 private:
  MiningConfig cfg_;
  JobCallback job_cb_;
  ShareResultCallback share_cb_;
  std::atomic<bool> running_{false};
  std::thread reader_;
  int sock_ = -1;
  int req_id_ = 1;
  bool pearl_v1_ = false;
  double last_difficulty_ = 32;
  std::string extranonce1_;
  StratumJob current_job_;
  uint64_t job_generation_ = 0;

  void dispatch_job_if_ready();
  bool write_line(const std::string& line);
  std::string read_line();
  bool handshake();
  bool solve_challenge(const std::string& seed_hex, int difficulty);
  void read_loop();
  void handle_line(const std::string& line);
  void parse_notify(const std::string& line);
  void parse_set_difficulty(const std::string& line);
  void parse_set_mining_params(const std::string& line);
};

}  // namespace sm
