#include "util/log.hpp"

#include <cstdarg>
#include <mutex>

namespace superhero::util {
namespace {

std::mutex g_log_mutex;
bool g_verbose = false;

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

}  // namespace

void set_verbose(bool enabled) { g_verbose = enabled; }

void log(LogLevel level, const char* fmt, ...) {
    if (level == LogLevel::Debug && !g_verbose) return;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::fprintf(stderr, "[SUPERHERO %s] ", level_name(level));
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

}  // namespace superhero::util
