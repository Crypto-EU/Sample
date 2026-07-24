#include "pool_nats.hpp"

#include "util.hpp"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>

namespace oneminer {
namespace {

bool json_get_string(const std::string& json, const std::string& key, std::string& out) {
  const std::string pat = "\"" + key + "\"";
  size_t p = json.find(pat);
  if (p == std::string::npos) return false;
  p = json.find(':', p + pat.size());
  if (p == std::string::npos) return false;
  p = json.find('"', p + 1);
  if (p == std::string::npos) return false;
  size_t e = p + 1;
  while (e < json.size()) {
    if (json[e] == '\\' && e + 1 < json.size()) { e += 2; continue; }
    if (json[e] == '"') break;
    ++e;
  }
  if (e >= json.size()) return false;
  out = json.substr(p + 1, e - p - 1);
  return true;
}

bool json_get_number(const std::string& json, const std::string& key, int64_t& out) {
  const std::string pat = "\"" + key + "\"";
  size_t p = json.find(pat);
  if (p == std::string::npos) return false;
  p = json.find(':', p + pat.size());
  if (p == std::string::npos) return false;
  ++p;
  while (p < json.size() && (json[p] == ' ' || json[p] == '"')) {
    if (json[p] == '"') {
      size_t s = p + 1;
      size_t e = json.find('"', s);
      if (e == std::string::npos) return false;
      try { out = std::stoll(json.substr(s, e - s)); return true; } catch (...) { return false; }
    }
    ++p;
  }
  size_t e = p;
  while (e < json.size() && (isdigit(static_cast<unsigned char>(json[e])) || json[e] == '-')) ++e;
  if (e == p) return false;
  try { out = std::stoll(json.substr(p, e - p)); return true; } catch (...) { return false; }
}

}  // namespace

NatsPoolClient::NatsPoolClient(std::vector<std::string> urls, std::string client_id, std::string wallet)
    : urls_(std::move(urls)), client_id_(std::move(client_id)), wallet_(std::move(wallet)) {}

NatsPoolClient::~NatsPoolClient() { stop(); }

void NatsPoolClient::stop() {
  running_ = false;
  if (thr_.joinable()) thr_.join();
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool NatsPoolClient::connect_one(const std::string& url, std::string& err) {
  std::string host;
  uint16_t port = 4222;
  if (!parse_host_port(url, host, port)) {
    // allow host without scheme defaulting to 4222
    if (url.find(':') == std::string::npos) {
      host = url;
      port = 4222;
    } else {
      err = "bad nats url: " + url;
      return false;
    }
  }
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) {
    err = "nats dns failed";
    return false;
  }
  int fd = -1;
  for (auto* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    err = "nats connect failed " + host + ":" + std::to_string(port);
    return false;
  }
  fd_ = fd;

  // Read INFO
  char buf[2048];
  const int n = static_cast<int>(::recv(fd_, buf, sizeof(buf) - 1, 0));
  if (n <= 0) {
    err = "nats INFO missing";
    return false;
  }
  std::string connect = "{\"verbose\":false,\"pedantic\":false,\"lang\":\"c++\",\"version\":\"1.0.0\",\"protocol\":1,\"name\":\"" +
                        client_id_ + "\"}";
  std::string msg = "CONNECT " + connect + "\r\nPING\r\n";
  if (!send_raw(msg, err)) return false;
  // Expect PONG
  recv_buf_.append(buf, buf + n);
  return true;
}

bool NatsPoolClient::send_raw(const std::string& s, std::string& err) {
  size_t off = 0;
  while (off < s.size()) {
    const int n = static_cast<int>(::send(fd_, s.data() + off, s.size() - off, 0));
    if (n <= 0) {
      err = "nats send failed";
      return false;
    }
    off += static_cast<size_t>(n);
  }
  return true;
}

bool NatsPoolClient::parse_job_json(const std::string& json, MiningJob& job) {
  json_get_string(json, "job_id", job.job_id);
  json_get_string(json, "previous_blockhash", job.previous_blockhash);
  std::string digest;
  json_get_string(json, "job_digest", digest);
  if (digest.empty()) json_get_string(json, "digest", digest);
  const auto at = digest.find('@');
  job.digest64 = at == std::string::npos ? digest : digest.substr(0, at);
  json_get_string(json, "main_blockhash", job.main_blockhash);
  json_get_string(json, "validator", job.validator);
  json_get_string(json, "miner", job.miner);
  int64_t v = 0;
  if (json_get_number(json, "height", v)) job.height = v;
  if (json_get_number(json, "main_height", v)) job.main_height = v;
  if (json_get_number(json, "share_difficulty", v)) job.share_difficulty = static_cast<uint64_t>(v);
  if (json_get_number(json, "difficulty", v) && job.share_difficulty == 0) {
    job.share_difficulty = static_cast<uint64_t>(v);
  }
  if (job.job_id.empty()) job.job_id = job.previous_blockhash;
  return job.previous_blockhash.size() == 78 && job.digest64.size() == 64;
}

bool NatsPoolClient::start(std::string& err) {
  for (const auto& url : urls_) {
    if (connect_one(url, err)) {
      log_info("NATS connected: " + url);
      // Subscribe to job/cancel/config
      std::string sub =
          "SUB saseul.mining.job 1\r\n"
          "SUB saseul.mining.cancel 2\r\n"
          "SUB saseul.mining.config 3\r\n"
          "PING\r\n";
      if (!send_raw(sub, err)) return false;
      running_ = true;
      thr_ = std::thread([this] { io_loop(); });
      return true;
    }
    log_warn(err);
  }
  err = "all NATS urls failed";
  return false;
}

void NatsPoolClient::io_loop() {
  char buf[8192];
  while (running_) {
    timeval tv{};
    tv.tv_sec = 2;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    const int n = static_cast<int>(::recv(fd_, buf, sizeof(buf), 0));
    if (n <= 0) {
      // keepalive ping
      std::string err;
      send_raw("PING\r\n", err);
      continue;
    }
    recv_buf_.append(buf, buf + n);
    while (true) {
      auto pos = recv_buf_.find("\r\n");
      if (pos == std::string::npos) break;
      std::string line = recv_buf_.substr(0, pos);
      recv_buf_.erase(0, pos + 2);
      if (line == "PING") {
        std::string err;
        send_raw("PONG\r\n", err);
        continue;
      }
      if (line == "PONG" || line.empty()) continue;
      if (line.rfind("MSG ", 0) == 0) {
        // MSG subject sid [reply] size
        std::istringstream iss(line);
        std::string tag, subject, sid, maybe_reply_or_size;
        iss >> tag >> subject >> sid >> maybe_reply_or_size;
        std::string size_s = maybe_reply_or_size;
        std::string reply;
        // If another token exists, previous was reply
        std::string extra;
        if (iss >> extra) {
          reply = maybe_reply_or_size;
          size_s = extra;
        }
        const int size = std::stoi(size_s);
        while (static_cast<int>(recv_buf_.size()) < size + 2) {
          const int m = static_cast<int>(::recv(fd_, buf, sizeof(buf), 0));
          if (m <= 0) break;
          recv_buf_.append(buf, buf + m);
        }
        if (static_cast<int>(recv_buf_.size()) < size + 2) continue;
        std::string payload = recv_buf_.substr(0, size);
        recv_buf_.erase(0, size + 2);
        if (subject == "saseul.mining.job" && job_handler_) {
          MiningJob job;
          if (parse_job_json(payload, job)) job_handler_(job);
        } else if (subject == "saseul.mining.cancel") {
          log_info("NATS job cancel received");
        }
      }
    }
  }
}

bool NatsPoolClient::publish_share(const MiningJob& job, const ShareCandidate& share, std::string& err) {
  std::ostringstream js;
  js << "{"
     << "\"client_id\":\"" << client_id_ << "\","
     << "\"wallet\":\"" << wallet_ << "\","
     << "\"difficulty\":" << job.network_difficulty << ","
     << "\"height\":" << job.height << ","
     << "\"main_blockhash\":\"" << job.main_blockhash << "\","
     << "\"main_height\":" << job.main_height << ","
     << "\"miner\":\"" << job.miner << "\","
     << "\"nonce\":\"" << share.nonce_hex << "\","
     << "\"previous_blockhash\":\"" << job.previous_blockhash << "\","
     << "\"receipts\":[],"
     << "\"share_difficulty\":" << job.share_difficulty << ","
     << "\"timestamp\":" << share.timestamp_us << ","
     << "\"validator\":\"" << job.validator << "\","
     << "\"blockhash\":\"" << share.blockhash_hex << "\""
     << "}";
  const std::string body = js.str();
  std::ostringstream msg;
  msg << "PUB saseul.mining.share " << body.size() << "\r\n" << body << "\r\n";
  std::lock_guard<std::mutex> lock(mu_);
  return send_raw(msg.str(), err);
}

}  // namespace oneminer
