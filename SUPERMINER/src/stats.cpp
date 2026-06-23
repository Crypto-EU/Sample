#include "superminer.hpp"

#include <fstream>
#include <sstream>

namespace sm {

void export_hive_stats_file(const GpuStats& stats) {
  std::ostringstream json;
  json << "{\"hs\":[";
  for (size_t i = 0; i < stats.hs_khs.size(); ++i) {
    if (i) {
      json << ',';
    }
    json << stats.hs_khs[i];
  }
  json << "],\"hs_units\":\"khs\",\"total_khs\":";
  double total = 0;
  for (double h : stats.hs_khs) {
    total += h;
  }
  json << total << ",\"temp\":[";
  for (size_t i = 0; i < stats.temp.size(); ++i) {
    if (i) {
      json << ',';
    }
    json << stats.temp[i];
  }
  json << "],\"fan\":[";
  for (size_t i = 0; i < stats.fan.size(); ++i) {
    if (i) {
      json << ',';
    }
    json << stats.fan[i];
  }
  json << "],\"bus_numbers\":[";
  for (size_t i = 0; i < stats.bus.size(); ++i) {
    if (i) {
      json << ',';
    }
    json << stats.bus[i];
  }
  json << "],\"ar\":[" << stats.accepted << ',' << stats.rejected << "],\"uptime\":"
       << static_cast<int>(stats.uptime_secs) << ",\"ver\":\"" << kVersion
       << "\",\"algo\":\"pearl\"}";
  std::ofstream f("/var/run/hive-miner-superminer.stats.json");
  f << json.str();
}

}  // namespace sm
