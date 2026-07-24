#include "pool_rabbit.hpp"

#include "util.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace oneminer {
namespace {

std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '\\': o += "\\\\"; break;
      case '"': o += "\\\""; break;
      case '\n': o += "\\n"; break;
      default: o += c; break;
    }
  }
  return o;
}

// Minimal JSON string field extractor.
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
      // number encoded as string
      size_t s = p + 1;
      size_t e = json.find('"', s);
      if (e == std::string::npos) return false;
      try { out = std::stoll(json.substr(s, e - s)); return true; } catch (...) { return false; }
    }
    ++p;
  }
  size_t e = p;
  while (e < json.size() && (isdigit(static_cast<unsigned char>(json[e])) || json[e] == '-' )) ++e;
  if (e == p) return false;
  try { out = std::stoll(json.substr(p, e - p)); return true; } catch (...) { return false; }
}

}  // namespace

std::vector<RabbitEndpoint> parse_rabbit_endpoints(const std::string& list, bool default_tls) {
  std::vector<RabbitEndpoint> out;
  for (const auto& tok : split(list, ',')) {
    RabbitEndpoint ep;
    if (!parse_host_port(tok, ep.host, ep.port)) continue;
    const std::string lower = tok;
    const bool force_tls = lower.find("tls://") != std::string::npos ||
                           lower.find("ssl://") != std::string::npos ||
                           lower.find("stratum+tls://") != std::string::npos ||
                           lower.find("stratum+ssl://") != std::string::npos;
    const bool force_tcp = lower.find("tcp://") != std::string::npos ||
                           lower.find("stratum+tcp://") != std::string::npos;
    if (force_tls) ep.use_tls = true;
    else if (force_tcp) ep.use_tls = false;
    else ep.use_tls = endpoint_should_use_tls(ep.port, default_tls);
    out.push_back(ep);
  }
  return out;
}

RabbitPoolClient::RabbitPoolClient(std::vector<RabbitEndpoint> endpoints,
                                   std::string wallet, std::string worker)
    : endpoints_(std::move(endpoints)), wallet_(std::move(wallet)), worker_(std::move(worker)) {
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
}

void RabbitPoolClient::close() {
  std::lock_guard<std::mutex> lock(mu_);
  if (ssl_) {
    SSL_shutdown(static_cast<SSL*>(ssl_));
    SSL_free(static_cast<SSL*>(ssl_));
    ssl_ = nullptr;
  }
  if (ssl_ctx_) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  recv_buf_.clear();
}

bool RabbitPoolClient::open_socket(const RabbitEndpoint& ep, std::string& err) {
  close();
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  addrinfo* res = nullptr;
  const std::string port = std::to_string(ep.port);
  if (getaddrinfo(ep.host.c_str(), port.c_str(), &hints, &res) != 0) {
    err = "DNS resolve failed for " + ep.host;
    return false;
  }
  int fd = -1;
  for (auto* p = res; p; p = p->ai_next) {
    fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    timeval tv{};
    tv.tv_sec = 10;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
    ::close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    err = "TCP connect failed " + ep.host + ":" + std::to_string(ep.port);
    return false;
  }
  fd_ = fd;

  if (ep.use_tls) {
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
      err = "SSL_CTX_new failed";
      close();
      return false;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);
    SSL* ssl = SSL_new(ctx);
    SSL_set_tlsext_host_name(ssl, ep.host.c_str());
    SSL_set_fd(ssl, fd_);
    if (SSL_connect(ssl) != 1) {
      err = "TLS connect failed " + ep.host + ":" + std::to_string(ep.port);
      SSL_free(ssl);
      SSL_CTX_free(ctx);
      close();
      return false;
    }
    ssl_ = ssl;
    ssl_ctx_ = ctx;
  }
  return true;
}

bool RabbitPoolClient::send_line(const std::string& line, std::string& err) {
  std::string payload = line;
  if (payload.empty() || payload.back() != '\n') payload.push_back('\n');
  const char* data = payload.data();
  size_t left = payload.size();
  while (left > 0) {
    int n = 0;
    if (ssl_) {
      n = SSL_write(static_cast<SSL*>(ssl_), data, static_cast<int>(left));
    } else {
      n = static_cast<int>(::send(fd_, data, left, 0));
    }
    if (n <= 0) {
      err = "send failed";
      return false;
    }
    data += n;
    left -= static_cast<size_t>(n);
  }
  return true;
}

bool RabbitPoolClient::recv_line(std::string& line, std::string& err, int timeout_sec) {
  timeval tv{};
  tv.tv_sec = timeout_sec;
  setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  while (true) {
    auto pos = recv_buf_.find('\n');
    if (pos != std::string::npos) {
      line = recv_buf_.substr(0, pos);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      recv_buf_.erase(0, pos + 1);
      return true;
    }
    char buf[4096];
    int n = 0;
    if (ssl_) {
      n = SSL_read(static_cast<SSL*>(ssl_), buf, sizeof(buf));
    } else {
      n = static_cast<int>(::recv(fd_, buf, sizeof(buf), 0));
    }
    if (n <= 0) {
      err = ssl_ ? "TLS read timeout/failed" : "TCP read timeout/failed";
      return false;
    }
    recv_buf_.append(buf, buf + n);
  }
}

bool RabbitPoolClient::connect_and_login(std::string& err) {
  if (endpoints_.empty()) {
    err = "no pool endpoints";
    return false;
  }
  for (size_t i = 0; i < endpoints_.size(); ++i) {
    index_ = i;
    const auto& ep = endpoints_[i];
    log_info("connecting " + ep.host + ":" + std::to_string(ep.port) +
             (ep.use_tls ? " (tls)" : " (tcp)"));
    if (!open_socket(ep, err)) {
      log_warn(err);
      continue;
    }
    std::ostringstream req;
    req << "{\"id\":1,\"method\":\"login\",\"params\":[\""
        << json_escape(wallet_) << "\",\"x\",\"" << json_escape(worker_) << "\"]}";
    if (!send_line(req.str(), err)) {
      log_warn(err);
      continue;
    }
    std::string resp;
    if (!recv_line(resp, err, 15)) {
      log_warn(err);
      continue;
    }
    if (resp.find("\"ok\":true") == std::string::npos &&
        resp.find("\"result\":true") == std::string::npos) {
      err = "login rejected: " + resp;
      log_warn(err);
      continue;
    }
    log_info("login ok wallet=" + wallet_ + " worker=" + worker_);
    return true;
  }
  err = "all pool endpoints failed";
  return false;
}

bool RabbitPoolClient::get_job(MiningJob& job, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!send_line("{\"id\":2,\"method\":\"getjob\",\"params\":[]}", err)) return false;
  std::string resp;
  if (!recv_line(resp, err, 20)) return false;
  if (resp.find("\"ok\":true") == std::string::npos) {
    err = "getjob failed: " + resp;
    return false;
  }
  MiningJob j;
  json_get_string(resp, "job_id", j.job_id);
  json_get_string(resp, "previous_blockhash", j.previous_blockhash);
  std::string digest;
  json_get_string(resp, "job_digest", digest);
  const auto at = digest.find('@');
  j.digest64 = at == std::string::npos ? digest : digest.substr(0, at);
  json_get_string(resp, "main_blockhash", j.main_blockhash);
  json_get_string(resp, "validator", j.validator);
  json_get_string(resp, "miner", j.miner);
  int64_t v = 0;
  if (json_get_number(resp, "height", v)) j.height = v;
  if (json_get_number(resp, "main_height", v)) j.main_height = v;
  if (json_get_number(resp, "share_difficulty", v)) j.share_difficulty = static_cast<uint64_t>(v);
  if (json_get_number(resp, "network_difficulty", v)) j.network_difficulty = static_cast<uint64_t>(v);
  if (json_get_number(resp, "difficulty", v) && j.share_difficulty == 0) {
    j.share_difficulty = static_cast<uint64_t>(v);
  }
  if (j.job_id.empty()) j.job_id = j.previous_blockhash;
  job = std::move(j);
  return true;
}

bool RabbitPoolClient::submit(const MiningJob& job, const ShareCandidate& share, std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  std::ostringstream req;
  req << "{\"id\":3,\"method\":\"submit\",\"params\":{"
      << "\"job_id\":\"" << json_escape(job.job_id) << "\","
      << "\"nonce\":\"" << json_escape(share.nonce_hex) << "\","
      << "\"timestamp\":" << share.timestamp_us << ","
      << "\"blockhash\":\"" << json_escape(share.blockhash_hex) << "\"}}";
  if (!send_line(req.str(), err)) return false;
  std::string resp;
  if (!recv_line(resp, err, 20)) return false;
  if (resp.find("\"ok\":true") != std::string::npos ||
      resp.find("\"result\":true") != std::string::npos) {
    return true;
  }
  err = resp;
  return false;
}

}  // namespace oneminer
