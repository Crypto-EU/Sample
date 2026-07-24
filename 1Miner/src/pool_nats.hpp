#pragma once
#include "pow.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace oneminer {

// Lightweight NATS client for saseulpool.com style subjects.
class NatsPoolClient {
 public:
  using JobHandler = std::function<void(const MiningJob&)>;

  NatsPoolClient(std::vector<std::string> urls, std::string client_id, std::string wallet);
  ~NatsPoolClient();

  bool start(std::string& err);
  void stop();
  bool publish_share(const MiningJob& job, const ShareCandidate& share, std::string& err);

  void set_job_handler(JobHandler h) { job_handler_ = std::move(h); }

 private:
  bool connect_one(const std::string& url, std::string& err);
  void io_loop();
  bool send_raw(const std::string& s, std::string& err);
  bool parse_job_json(const std::string& json, MiningJob& job);

  std::vector<std::string> urls_;
  std::string client_id_;
  std::string wallet_;
  int fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thr_;
  std::mutex mu_;
  JobHandler job_handler_;
  std::string recv_buf_;
};

}  // namespace oneminer
