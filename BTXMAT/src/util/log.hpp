#pragma once

#include <cstdio>
#include <string>

namespace superhero::util {

enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel level, const char* fmt, ...);
void set_verbose(bool enabled);

}  // namespace superhero::util
