#include "superminer.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>

namespace sm {

void log_info(const char* fmt, ...) {
  std::time_t t = std::time(nullptr);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  std::fprintf(stderr, "[%s] ", ts);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

void log_error(const char* fmt, ...) {
  std::fprintf(stderr, "[ERROR] ");
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

}  // namespace sm

static const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string sm::base64_encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) {
      n |= static_cast<uint32_t>(data[i + 1]) << 8;
    }
    if (i + 2 < len) {
      n |= static_cast<uint32_t>(data[i + 2]);
    }
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back((i + 1 < len) ? kB64[(n >> 6) & 63] : '=');
    out.push_back((i + 2 < len) ? kB64[n & 63] : '=');
  }
  return out;
}
