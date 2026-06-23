#include "stratum.hpp"

#include "challenge.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <sstream>

namespace {

std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
    }
    o.push_back(c);
  }
  return o;
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    unsigned v = 0;
    std::sscanf(hex.c_str() + i, "%02x", &v);
    out.push_back(static_cast<uint8_t>(v));
  }
  return out;
}

std::string extract_json_string(const std::string& json, const std::string& key) {
  const std::string pat = "\"" + key + "\":\"";
  auto pos = json.find(pat);
  if (pos == std::string::npos) {
    return {};
  }
  pos += pat.size();
  auto end = json.find('"', pos);
  if (end == std::string::npos) {
    return {};
  }
  return json.substr(pos, end - pos);
}

int extract_json_int(const std::string& json, const std::string& key) {
  const std::string pat = "\"" + key + "\":";
  auto pos = json.find(pat);
  if (pos == std::string::npos) {
    return 0;
  }
  pos += pat.size();
  return std::atoi(json.c_str() + pos);
}

}  // namespace

namespace sm {

std::vector<uint32_t> StratumClient::difficulty_to_target(double diff) {
  if (diff <= 0) {
    diff = 1;
  }
  // classic pdiff: target = (0xFFFF << 208) / diff — computed per 32-bit limb
  std::vector<uint32_t> out(8, 0);
  const uint64_t hi = 0xFFFFull;
  uint64_t rem = 0;
  for (int limb = 7; limb >= 0; --limb) {
    __uint128_t cur =
        (static_cast<__uint128_t>(rem) << 32) | (limb == 7 ? hi : 0);
    const uint64_t q = static_cast<uint64_t>(cur / static_cast<uint64_t>(diff));
    rem = static_cast<uint64_t>(cur % static_cast<uint64_t>(diff));
    out[limb] = static_cast<uint32_t>(q);
  }
  return out;
}

StratumClient::StratumClient(MiningConfig cfg) : cfg_(std::move(cfg)) {}

StratumClient::~StratumClient() { disconnect(); }

bool StratumClient::connect() {
  struct addrinfo hints {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  const std::string port = std::to_string(cfg_.pool_port);
  if (getaddrinfo(cfg_.pool_host.c_str(), port.c_str(), &hints, &res) != 0) {
    log_error("DNS failed for %s:%d", cfg_.pool_host.c_str(), cfg_.pool_port);
    return false;
  }
  sock_ = -1;
  for (auto* p = res; p; p = p->ai_next) {
    int s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (s < 0) {
      continue;
    }
    if (::connect(s, p->ai_addr, p->ai_addrlen) == 0) {
      sock_ = s;
      break;
    }
    ::close(s);
  }
  freeaddrinfo(res);
  if (sock_ < 0) {
    log_error("connect failed %s:%d", cfg_.pool_host.c_str(), cfg_.pool_port);
    return false;
  }
  int one = 1;
  setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  running_ = true;
  if (!handshake()) {
    disconnect();
    return false;
  }
  reader_ = std::thread([this] { read_loop(); });
  return true;
}

void StratumClient::disconnect() {
  running_ = false;
  if (sock_ >= 0) {
    ::shutdown(sock_, SHUT_RDWR);
    ::close(sock_);
    sock_ = -1;
  }
  if (reader_.joinable()) {
    reader_.join();
  }
}

bool StratumClient::write_line(const std::string& line) {
  std::string msg = line;
  if (msg.empty() || msg.back() != '\n') {
    msg.push_back('\n');
  }
  const char* p = msg.c_str();
  size_t left = msg.size();
  while (left > 0) {
    ssize_t n = ::send(sock_, p, left, 0);
    if (n <= 0) {
      return false;
    }
    p += n;
    left -= static_cast<size_t>(n);
  }
  return true;
}

std::string StratumClient::read_line() {
  std::string line;
  char ch;
  while (running_) {
    ssize_t n = ::recv(sock_, &ch, 1, 0);
    if (n <= 0) {
      return {};
    }
    if (ch == '\n') {
      break;
    }
    if (ch != '\r') {
      line.push_back(ch);
    }
  }
  return line;
}

bool StratumClient::solve_challenge(const std::string& seed_hex, int difficulty) {
  auto seed = hex_to_bytes(seed_hex);
  if (seed.size() != 32) {
    return false;
  }
  uint64_t nonce = 0;
  if (!sm::solve_pearl_challenge(seed.data(), difficulty, &nonce)) {
    return false;
  }
  char nonce_hex[17];
  std::snprintf(nonce_hex, sizeof(nonce_hex), "%016llx",
                static_cast<unsigned long long>(nonce));
  std::ostringstream oss;
  oss << "{\"id\":" << req_id_++
      << ",\"method\":\"pearl.challenge_response\",\"params\":{\"nonce\":\""
      << nonce_hex << "\",\"seed\":\"" << seed_hex << "\"}}";
  return write_line(oss.str());
}

bool StratumClient::handshake() {
  if (cfg_.requested_diff > 0) {
    last_difficulty_ = static_cast<double>(cfg_.requested_diff);
  }

  std::string first = read_line();
  if (!first.empty() && first.find("pearl.challenge") != std::string::npos) {
    pearl_v1_ = true;
    const auto seed = extract_json_string(first, "seed");
    const int diff = extract_json_int(first, "difficulty");
    if (!solve_challenge(seed, diff > 0 ? diff : 32)) {
      log_error("pearl.challenge solve failed");
      return false;
    }
    write_line("{\"id\":" + std::to_string(req_id_++) +
               ",\"method\":\"mining.configure\",\"params\":[[\"pearl/v1\"],{}]}");
  }

  write_line("{\"id\":" + std::to_string(req_id_++) +
             ",\"method\":\"mining.subscribe\",\"params\":[\"" + std::string(kAgent) +
             "\"]}");

  std::string worker = cfg_.wallet;
  if (!cfg_.worker.empty()) {
    worker += "." + cfg_.worker;
  }
  std::string pass = cfg_.password;
  if (cfg_.requested_diff > 0) {
    pass = "x;d=" + std::to_string(cfg_.requested_diff);
  }
  write_line("{\"id\":" + std::to_string(req_id_++) +
             ",\"method\":\"mining.authorize\",\"params\":[\"" + json_escape(worker) +
             "\",\"" + json_escape(pass) + "\"]}");

  for (int i = 0; i < 8; ++i) {
    std::string line = read_line();
    if (line.empty()) {
      break;
    }
    handle_line(line);
    if (line.find("\"result\":true") != std::string::npos &&
        line.find("mining.authorize") != std::string::npos) {
      break;
    }
  }
  log_info("stratum connected to %s:%d (pearl_v1=%d)", cfg_.pool_host.c_str(),
           cfg_.pool_port, pearl_v1_ ? 1 : 0);
  return true;
}

void StratumClient::read_loop() {
  while (running_) {
    std::string line = read_line();
    if (line.empty()) {
      running_ = false;
      break;
    }
    handle_line(line);
  }
}

void StratumClient::handle_line(const std::string& line) {
  if (line.find("pearl.challenge") != std::string::npos) {
    const auto seed = extract_json_string(line, "seed");
    const int diff = extract_json_int(line, "difficulty");
    solve_challenge(seed, diff > 0 ? diff : 32);
    return;
  }
  if (line.find("mining.set_difficulty") != std::string::npos) {
    parse_set_difficulty(line);
    return;
  }
  if (line.find("pearl.set_mining_params") != std::string::npos ||
      line.find("mining.notify") != std::string::npos) {
    if (line.find("mining.notify") != std::string::npos) {
      parse_notify(line);
    } else {
      parse_set_mining_params(line);
    }
    return;
  }
  if (line.find("\"error\"") != std::string::npos && line.find("mining.submit") != std::string::npos) {
    if (share_cb_) {
      share_cb_(false, line);
    }
    return;
  }
  if (line.find("\"result\":true") != std::string::npos && line.find("mining.submit") != std::string::npos) {
    if (share_cb_) {
      share_cb_(true, "accepted");
    }
  }
}

void StratumClient::parse_set_difficulty(const std::string& line) {
  auto pos = line.find('[');
  if (pos == std::string::npos) {
    return;
  }
  last_difficulty_ = std::atof(line.c_str() + pos + 1);
}

void StratumClient::parse_set_mining_params(const std::string& line) {
  StratumJob job;
  job.difficulty = static_cast<uint32_t>(last_difficulty_);
  job.target = difficulty_to_target(last_difficulty_);
  job.m = extract_json_int(line, "m");
  job.n = extract_json_int(line, "n");
  job.k = extract_json_int(line, "k");
  job.r = extract_json_int(line, "r");
  if (job.r <= 0) {
    job.r = 256;
  }
  const auto sigma_hex = extract_json_string(line, "sigma");
  if (!sigma_hex.empty()) {
    job.sigma = hex_to_bytes(sigma_hex);
  }
  const auto bseed_hex = extract_json_string(line, "b_seed");
  if (!bseed_hex.empty()) {
    job.b_seed = hex_to_bytes(bseed_hex);
  }
  const auto key_hex = extract_json_string(line, "key");
  if (!key_hex.empty()) {
    job.job_key = hex_to_bytes(key_hex);
  }
  if (job_cb_ && !job.sigma.empty()) {
    job_cb_(job);
  }
}

void StratumClient::parse_notify(const std::string& line) {
  StratumJob job;
  job.difficulty = static_cast<uint32_t>(last_difficulty_);
  job.target = difficulty_to_target(last_difficulty_);
  auto pos = line.find('[');
  if (pos == std::string::npos) {
    return;
  }
  std::string arr = line.substr(pos);
  // positional: [job_id, prev_hash, header/coinbase, height, ...]
  auto q1 = arr.find('"');
  auto q2 = arr.find('"', q1 + 1);
  if (q1 != std::string::npos && q2 != std::string::npos) {
    job.job_id = arr.substr(q1 + 1, q2 - q1 - 1);
  }
  if (job_cb_) {
    job_cb_(job);
  }
}

bool StratumClient::submit_share(const std::string& job_id, const std::string& plain_proof_b64) {
  std::string worker = cfg_.wallet;
  if (!cfg_.worker.empty()) {
    worker += "." + cfg_.worker;
  }
  std::ostringstream oss;
  oss << "{\"id\":" << req_id_++ << ",\"method\":\"mining.submit\",\"params\":["
      << "\"" << json_escape(worker) << "\","
      << "\"" << json_escape(job_id) << "\","
      << "\"" << plain_proof_b64 << "\"]}";
  return write_line(oss.str());
}

}  // namespace sm
