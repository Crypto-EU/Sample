#include "superminer.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>

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
