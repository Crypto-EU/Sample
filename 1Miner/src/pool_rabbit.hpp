#pragma once
#include "pow.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace oneminer {

struct RabbitEndpoint {
  std::string host;
  uint16_t port = 0;
  bool use_tls = false;
};

class RabbitPoolClient {
 public:
  using JobHandler = std::function<void(const MiningJob&)>;

  explicit RabbitPoolClient(std::vector<RabbitEndpoint> endpoints,
                            std::string wallet, std::string worker);

  bool connect_and_login(std::string& err);
  bool get_job(MiningJob& job, std::string& err);
  bool submit(const MiningJob& job, const ShareCandidate& share, std::string& err);
  void close();

  const RabbitEndpoint& active() const { return endpoints_[index_]; }

 private:
  bool open_socket(const RabbitEndpoint& ep, std::string& err);
  bool send_line(const std::string& line, std::string& err);
  bool recv_line(std::string& line, std::string& err, int timeout_sec = 15);

  std::vector<RabbitEndpoint> endpoints_;
  size_t index_ = 0;
  std::string wallet_;
  std::string worker_;
  int fd_ = -1;
  void* ssl_ = nullptr;       // SSL*
  void* ssl_ctx_ = nullptr;   // SSL_CTX*
  std::mutex mu_;
  std::string recv_buf_;
};

std::vector<RabbitEndpoint> parse_rabbit_endpoints(const std::string& list, bool default_tls);

}  // namespace oneminer
