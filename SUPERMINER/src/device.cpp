#include "superminer.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sm {

static GpuProfile profile_for_arch(const std::string& arch, const std::string& name) {
  // Tuned shapes: maximize VRAM residency + kernel occupancy per arch family.
  // MI300X: K=2048 unlocks hand-MFMA fast path (ARC-miner / SUPERMINER extension).
  if (arch.find("gfx942") != std::string::npos || arch.find("gfx950") != std::string::npos) {
    return {name, arch, 8192, 65536, 2048, 256, 256, 128, 32, true};
  }
  // RDNA3 / RDNA4 — Infinity Cache friendly N, K=4096 (Pearlhash default)
  if (arch.find("gfx11") != std::string::npos || arch.find("gfx12") != std::string::npos) {
    return {name, arch, 4096, 65536, 4096, 256, 128, 128, 32, true};
  }
  // RDNA2 — scale N to VRAM (16GB → 32768, 20GB+ → 65536)
  if (arch.find("gfx10") != std::string::npos) {
    int n = 32768;
    const char* env_n = std::getenv("SUPERMINER_N");
    if (env_n && env_n[0]) {
      n = std::atoi(env_n);
    }
    return {name, arch, 4096, n, 4096, 256, 128, 128, 32, true};
  }
  return {name, arch, 4096, 32768, 4096, 256, 128, 128, 32, true};
}

static std::string read_first_line(const char* path) {
  std::ifstream f(path);
  std::string line;
  std::getline(f, line);
  return line;
}

static std::string detect_arch_hip(int device) {
  char path[256];
  std::snprintf(path, sizeof(path),
                "/sys/class/drm/renderD%d/device/gpu_arch", 128 + device);
  auto arch = read_first_line(path);
  if (!arch.empty()) {
    return arch;
  }
  // rocm-smi fallback
  FILE* fp = popen("rocm-smi --showproductname 2>/dev/null | head -1", "r");
  if (fp) {
    char buf[256] = {};
    if (fgets(buf, sizeof(buf), fp)) {
      pclose(fp);
      std::string n(buf);
      if (n.find("7900") != std::string::npos || n.find("7800") != std::string::npos) {
        return "gfx1100";
      }
      if (n.find("6900") != std::string::npos || n.find("6800") != std::string::npos) {
        return "gfx1030";
      }
      if (n.find("MI300") != std::string::npos) {
        return "gfx942";
      }
    } else {
      pclose(fp);
    }
  }
  const char* env = std::getenv("SUPERMINER_ARCH");
  if (env && env[0]) {
    return env;
  }
  return "gfx1100";
}

std::vector<GpuProfile> detect_amd_devices(const std::vector<int>& requested) {
  std::vector<GpuProfile> out;
  int count = 0;
  FILE* fp = popen("rocm-smi --showid 2>/dev/null | grep -c 'GPU\\['", "r");
  if (fp) {
    if (fscanf(fp, "%d", &count) != 1) {
      count = 0;
    }
    pclose(fp);
  }
  if (count <= 0) {
    // HiveOS / amdgpu: assume one GPU if rocm-smi missing
    count = 1;
  }
  for (int i = 0; i < count; ++i) {
    if (!requested.empty()) {
      bool wanted = false;
      for (int d : requested) {
        if (d == i) {
          wanted = true;
          break;
        }
      }
      if (!wanted) {
        continue;
      }
    }
    std::ostringstream name;
    name << "AMD-GPU-" << i;
    auto arch = detect_arch_hip(i);
    out.push_back(profile_for_arch(arch, name.str()));
    log_info("device %d: arch=%s profile M=%d N=%d K=%d R=%d", i, arch.c_str(),
             out.back().m, out.back().n, out.back().k, out.back().r);
  }
  return out;
}

}  // namespace sm
