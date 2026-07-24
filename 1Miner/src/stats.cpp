#include "backend.hpp"

#include "util.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace oneminer {

void write_stats_file(const MinerStats& stats, const std::string& path) {
  std::ostringstream js;
  js << "{";
  js << "\"version\":\"" << stats.version << "\",";
  js << "\"nonce_mode\":\"" << stats.nonce_mode << "\",";
  js << "\"hashrate_mhs\":" << stats.total_mhs << ",";
  js << "\"uptime_seconds\":" << stats.uptime_seconds << ",";
  js << "\"shares_session\":" << stats.accepted << ",";
  js << "\"accepted\":" << stats.accepted << ",";
  js << "\"rejected\":" << stats.rejected << ",";
  js << "\"stale\":" << stats.stale << ",";
  js << "\"gpus\":[";
  for (size_t i = 0; i < stats.devices.size(); ++i) {
    const auto& d = stats.devices[i];
    if (i) js << ",";
    js << "{\"index\":" << d.index
       << ",\"backend\":\"" << d.backend
       << "\",\"name\":\"" << d.name
       << "\",\"hashrate_mhs\":" << d.hashrate_mhs
       << ",\"accepted\":" << d.accepted
       << ",\"rejected\":" << d.rejected
       << ",\"stale\":" << d.stale << "}";
  }
  js << "]}";
  std::ofstream f(path);
  f << js.str();
}

std::string format_gpu_status_table(const MinerStats& stats) {
  std::ostringstream o;
  o << "GPU status\n";
  o << "BACKEND  GPU  NAME                           POWER      HASH          TOTAL HASHES  HASH/W(MH)  SHARES(acc/st/rej)\n";
  o << "-------  ---  -----------------------------  ---------  ------------  ------------  ----------  ------------------\n";
  for (const auto& d : stats.devices) {
    char line[512];
    std::snprintf(line, sizeof(line),
                  "%-7s  %-3d  %-29.29s  %9.1f  %8.2f MH/s  %12llu  %10.2f  %llu/%llu/%llu\n",
                  d.backend.c_str(), d.index, d.name.c_str(), d.power_w, d.hashrate_mhs,
                  static_cast<unsigned long long>(d.total_hashes),
                  d.power_w > 0 ? d.hashrate_mhs / d.power_w : 0.0,
                  static_cast<unsigned long long>(d.accepted),
                  static_cast<unsigned long long>(d.stale),
                  static_cast<unsigned long long>(d.rejected));
    o << line;
  }
  char total[512];
  std::snprintf(total, sizeof(total),
                "%-7s  %-3s  %-29s  %9s  %8.2f MH/s  %12s  %10s  %llu/%llu/%llu\n",
                "TOTAL", "-", "all devices", "N/A", stats.total_mhs, "-", "N/A",
                static_cast<unsigned long long>(stats.accepted),
                static_cast<unsigned long long>(stats.stale),
                static_cast<unsigned long long>(stats.rejected));
  o << total;
  return o.str();
}

}  // namespace oneminer
