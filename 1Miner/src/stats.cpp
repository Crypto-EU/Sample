#include "backend.hpp"

#include "util.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace oneminer {

namespace {
std::string format_hashrate(double mhs) {
  char buf[64];
  if (mhs >= 1000.0) {
    std::snprintf(buf, sizeof(buf), "%8.2f GH/s", mhs / 1000.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%8.2f MH/s", mhs);
  }
  return buf;
}
}  // namespace

void write_stats_file(const MinerStats& stats, const std::string& path) {
  std::ostringstream js;
  js << "{";
  js << "\"version\":\"" << stats.version << "\",";
  js << "\"nonce_mode\":\"" << stats.nonce_mode << "\",";
  js << "\"hashrate_mhs\":" << stats.total_mhs << ",";
  js << "\"hashrate_ghs\":" << (stats.total_mhs / 1000.0) << ",";
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
       << ",\"hashrate_ghs\":" << (d.hashrate_mhs / 1000.0)
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
    const std::string hr = format_hashrate(d.hashrate_mhs);
    std::snprintf(line, sizeof(line),
                  "%-7s  %-3d  %-29.29s  %9.1f  %12s  %12llu  %10.2f  %llu/%llu/%llu\n",
                  d.backend.c_str(), d.index, d.name.c_str(), d.power_w, hr.c_str(),
                  static_cast<unsigned long long>(d.total_hashes),
                  d.power_w > 0 ? d.hashrate_mhs / d.power_w : 0.0,
                  static_cast<unsigned long long>(d.accepted),
                  static_cast<unsigned long long>(d.stale),
                  static_cast<unsigned long long>(d.rejected));
    o << line;
  }
  char total[512];
  const std::string thr = format_hashrate(stats.total_mhs);
  std::snprintf(total, sizeof(total),
                "%-7s  %-3s  %-29s  %9s  %12s  %12s  %10s  %llu/%llu/%llu\n",
                "TOTAL", "-", "all devices", "N/A", thr.c_str(), "-", "N/A",
                static_cast<unsigned long long>(stats.accepted),
                static_cast<unsigned long long>(stats.stale),
                static_cast<unsigned long long>(stats.rejected));
  o << total;
  return o.str();
}

}  // namespace oneminer
