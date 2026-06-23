#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sm {

constexpr const char* kVersion = "SUPERMINER-1.0.1";
constexpr const char* kAgent = "SUPERMINER/1.0.1";

struct GpuProfile {
  std::string name;
  std::string arch;   // gfx1100, gfx1030, gfx942, ...
  int m, n, k, r;
  int bm, bn, bk;
  bool contiguous_tile;
};

struct MiningConfig {
  std::string pool_host;
  int pool_port = 9000;
  bool pool_tls = false;
  std::string wallet;
  std::string worker;
  std::string password = "x";
  long requested_diff = 0;
  std::vector<int> devices;
  int m = 0, n = 0, k = 0, r = 256;
  int batch_size = 8;
  bool disable_pong = false;
};

struct GpuStats {
  std::vector<double> hs_khs;
  std::vector<int> temp;
  std::vector<int> fan;
  std::vector<int> bus;
  int accepted = 0;
  int rejected = 0;
  double uptime_secs = 0;
};

void log_info(const char* fmt, ...);
void log_error(const char* fmt, ...);
std::string base64_encode(const uint8_t* data, size_t len);

}  // namespace sm
