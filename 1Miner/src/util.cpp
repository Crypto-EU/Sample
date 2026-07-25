#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <sstream>

namespace oneminer {

std::string trim(std::string s) {
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

bool parse_host_port(const std::string& endpoint, std::string& host, uint16_t& port) {
  std::string e = trim(endpoint);
  // strip schemes
  for (const char* scheme : {"stratum+tls://", "stratum+ssl://", "stratum+tcp://",
                             "tls://", "ssl://", "tcp://", "nats://"}) {
    const size_t n = std::char_traits<char>::length(scheme);
    if (e.size() >= n && e.compare(0, n, scheme) == 0) {
      e = e.substr(n);
      break;
    }
  }
  const auto pos = e.rfind(':');
  if (pos == std::string::npos) return false;
  host = e.substr(0, pos);
  try {
    int p = std::stoi(e.substr(pos + 1));
    if (p <= 0 || p > 65535) return false;
    port = static_cast<uint16_t>(p);
  } catch (...) {
    return false;
  }
  return !host.empty();
}

int64_t now_us() {
  using namespace std::chrono;
  return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

std::string now_log_time() {
  using namespace std::chrono;
  const auto t = system_clock::now();
  const std::time_t sec = system_clock::to_time_t(t);
  std::tm tm{};
  gmtime_r(&sec, &tm);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

void log_info(const std::string& msg) {
  std::cout << now_log_time() << " | INFO | " << msg << std::endl;
}
void log_warn(const std::string& msg) {
  std::cout << now_log_time() << " | WARN | " << msg << std::endl;
}
void log_error(const std::string& msg) {
  std::cerr << now_log_time() << " | ERR | " << msg << std::endl;
}

bool endpoint_should_use_tls(uint16_t port, bool default_tls) {
  if (port == 1901 || port == 1921) return true;
  if (port == 1911 || port == 1931) return false;
  return default_tls;
}

}  // namespace oneminer
