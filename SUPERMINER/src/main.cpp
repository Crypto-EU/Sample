#include "gpu_worker.hpp"

#include "device.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

static void print_help() {
  std::cout
      << "SUPERMINER — AMD GPU miner for Pearl (PEARL / Pearlhash)\n\n"
      << "Usage: superminer --pearl-mine [options]\n\n"
      << "Options:\n"
      << "  --pearl-mine         Start mining Pearl on pool.pearlhash.xyz or custom pool\n"
      << "  --pool URI           stratum+tcp://host:port or host:port\n"
      << "  --wallet ADDR        Pearl wallet (prl1...)\n"
      << "  --worker NAME        Worker name (default: hostname)\n"
      << "  --password PW        Stratum password (default x; use x;d=N for static diff)\n"
      << "  --devices LIST       Comma list or 'all' (default all AMD GPUs)\n"
      << "  --pearl-m N          Matrix M (0=auto per GPU arch)\n"
      << "  --pearl-n N          Matrix N\n"
      << "  --pearl-k N          Matrix K\n"
      << "  --pearl-r N          Noise rank (default 256)\n"
      << "  --batch N            Iterations per GPU sync batch (default 8)\n"
      << "  --list-devices       List detected AMD GPUs and exit\n"
      << "  --self-test          Verify GPU libs and HIP, then exit\n"
      << "  -V, --version        Print version\n"
      << "  -h, --help           This help\n";
}

static bool parse_pool_uri(const std::string& uri, std::string& host, int& port, bool& tls) {
  std::string u = uri;
  tls = false;
  auto pos = u.find("://");
  if (pos != std::string::npos) {
    std::string scheme = u.substr(0, pos);
    u = u.substr(pos + 3);
    if (scheme.find("tls") != std::string::npos || scheme.find("ssl") != std::string::npos) {
      tls = true;
    }
  }
  pos = u.find(':');
  if (pos == std::string::npos) {
    host = u;
    port = 9000;
    return true;
  }
  host = u.substr(0, pos);
  port = std::atoi(u.substr(pos + 1).c_str());
  return !host.empty() && port > 0;
}

int main(int argc, char** argv) {
  using namespace sm;

  bool do_mine = false;
  MiningConfig cfg;
  cfg.pool_host = "pool.pearlhash.xyz";
  cfg.pool_port = 9000;
  char hostname[256] = "rig";
  gethostname(hostname, sizeof(hostname));
  cfg.worker = hostname;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_help();
      return 0;
    }
    if (arg == "-V" || arg == "--version") {
      std::cout << kVersion << "\n";
      return 0;
    }
    if (arg == "--pearl-mine" || arg == "--mine") {
      do_mine = true;
      continue;
    }
    if (arg == "--list-devices") {
      auto devs = detect_amd_devices({});
      for (size_t j = 0; j < devs.size(); ++j) {
        std::cout << j << ": " << devs[j].name << " arch=" << devs[j].arch << "\n";
      }
      return devs.empty() ? 1 : 0;
    }
    if (arg == "--self-test") {
      return MinerApp::self_test();
    }
    auto need = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        log_error("missing value for %s", flag);
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "--pool") {
      parse_pool_uri(need("--pool"), cfg.pool_host, cfg.pool_port, cfg.pool_tls);
    } else if (arg == "--wallet") {
      cfg.wallet = need("--wallet");
    } else if (arg == "--worker") {
      cfg.worker = need("--worker");
    } else if (arg == "--password") {
      cfg.password = need("--password");
      if (cfg.password.find("d=") != std::string::npos) {
        auto p = cfg.password.find("d=");
        cfg.requested_diff = std::atol(cfg.password.c_str() + p + 2);
      }
    } else if (arg == "--devices") {
      std::string list = need("--devices");
      if (list != "all") {
        size_t start = 0;
        while (start < list.size()) {
          auto comma = list.find(',', start);
          std::string part = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
          cfg.devices.push_back(std::atoi(part.c_str()));
          if (comma == std::string::npos) {
            break;
          }
          start = comma + 1;
        }
      }
    } else if (arg == "--pearl-m") {
      cfg.m = std::atoi(need("--pearl-m").c_str());
    } else if (arg == "--pearl-n") {
      cfg.n = std::atoi(need("--pearl-n").c_str());
    } else if (arg == "--pearl-k") {
      cfg.k = std::atoi(need("--pearl-k").c_str());
    } else if (arg == "--pearl-r") {
      cfg.r = std::atoi(need("--pearl-r").c_str());
    } else if (arg == "--batch") {
      cfg.batch_size = std::atoi(need("--batch").c_str());
    } else {
      log_error("unknown argument: %s", arg.c_str());
      return 2;
    }
  }

  if (!do_mine) {
    print_help();
    return 0;
  }
  if (cfg.wallet.empty()) {
    log_error("--wallet is required");
    return 2;
  }

  log_info("%s starting on %s:%d wallet=%s worker=%s", kVersion, cfg.pool_host.c_str(),
           cfg.pool_port, cfg.wallet.c_str(), cfg.worker.c_str());

  MinerApp app(std::move(cfg));
  return app.run();
}
