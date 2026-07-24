#include "backend.hpp"
#include "pool_nats.hpp"
#include "pool_rabbit.hpp"
#include "pow.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

constexpr const char* kVersion = "1.0.0";
}  // namespace

static void usage(const char* argv0) {
  std::cout
      << "1Miner v" << kVersion << " — Saseul GPU/CPU miner (AMD OpenCL + CPU)\n"
      << "Usage: " << argv0 << " [options]\n\n"
      << "Pool modes:\n"
      << "  --pool HOST:PORT[,HOST:PORT...]   RabbitMiner-style login/getjob (TLS auto by port)\n"
      << "  --nats URL[,URL...]               saseulpool.com NATS mode\n"
      << "  --wallet ADDR[.WORKER]            Wallet (worker optional after '.')\n"
      << "  --worker NAME                     Worker name (default: hostname)\n"
      << "  --nonce-mode classic|latehex      Default: classic\n"
      << "  --use-cpu                         Enable CPU backend\n"
      << "  --threads N                       CPU threads (default: hardware concurrency)\n"
      << "  --opencl / --amd-ocl              Enable OpenCL GPU backend (AMD/NVIDIA)\n"
      << "  --device LIST                     Comma GPU indices (default: all)\n"
      << "  --stats-file PATH                 Default: /tmp/saseul-miner-stats.json\n"
      << "  --help\n\n"
      << "HiveOS: use the hiveos/ wrapper scripts. Example:\n"
      << "  ./1miner --pool nl.rabbitminer.cc:1901 --wallet WALLET.worker --amd-ocl\n"
      << "  ./1miner --nats nats://nats.saseulpool.com:4222 --wallet WALLET --id rig1 --use-cpu\n";
}

int main(int argc, char** argv) {
  using namespace oneminer;
  std::string pool_list;
  std::string nats_list;
  std::string wallet;
  std::string worker;
  std::string nonce_mode_s = "classic";
  std::string stats_file = "/tmp/saseul-miner-stats.json";
  bool use_cpu = false;
  bool use_opencl = false;
  int threads = static_cast<int>(std::thread::hardware_concurrency());
  if (threads < 1) threads = 1;
  std::vector<int> devices;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](std::string& dst) {
      if (i + 1 >= argc) return false;
      dst = argv[++i];
      return true;
    };
    if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
    else if (a == "--pool") { if (!need(pool_list)) return 2; }
    else if (a == "--nats") { if (!need(nats_list)) return 2; }
    else if (a == "--wallet") { if (!need(wallet)) return 2; }
    else if (a == "--worker" || a == "--id") { if (!need(worker)) return 2; }
    else if (a == "--nonce-mode") { if (!need(nonce_mode_s)) return 2; }
    else if (a == "--stats-file") { if (!need(stats_file)) return 2; }
    else if (a == "--threads") {
      std::string v; if (!need(v)) return 2; threads = std::stoi(v);
    } else if (a == "--use-cpu") { use_cpu = true; }
    else if (a == "--opencl" || a == "--amd-ocl" || a == "--nvidia-ocl") { use_opencl = true; }
    else if (a == "--device") {
      std::string v; if (!need(v)) return 2;
      for (const auto& t : split(v, ',')) devices.push_back(std::stoi(t));
    } else if (a == "--cuda") {
      log_warn("--cuda ignored in 1Miner 1.0; use --opencl or NVIDIA OpenCL ICD");
    } else {
      log_error("unknown arg: " + a);
      return 2;
    }
  }

  if (wallet.empty()) {
    log_error("set --wallet");
    return 2;
  }
  if (wallet.find('.') != std::string::npos && worker.empty()) {
    worker = wallet.substr(wallet.find('.') + 1);
    wallet = wallet.substr(0, wallet.find('.'));
  }
  if (worker.empty()) {
    char host[256] = {0};
    gethostname(host, sizeof(host) - 1);
    worker = host[0] ? host : "rig1";
  }
  if (pool_list.empty() && nats_list.empty()) {
    log_error("set --pool or --nats");
    return 2;
  }
  if (!use_cpu && !use_opencl) {
    // Auto: prefer OpenCL if available, else CPU.
    use_opencl = true;
    use_cpu = true;
  }

  const NonceMode mode = parse_nonce_mode(nonce_mode_s);
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  log_info(std::string("1Miner v") + kVersion + " nonce_mode=" + nonce_mode_name(mode));

  std::unique_ptr<CpuBackend> cpu;
  if (use_cpu) cpu = std::make_unique<CpuBackend>(threads);

#ifdef ONE_MINER_HAS_OPENCL
  std::unique_ptr<OpenClBackend> ocl;
  if (use_opencl) {
    ocl = std::make_unique<OpenClBackend>();
    std::string err;
    if (!ocl->init(err)) {
      log_warn("OpenCL init failed: " + err);
      ocl.reset();
      if (!cpu) {
        log_info("falling back to CPU");
        cpu = std::make_unique<CpuBackend>(threads);
      }
    }
  }
#else
  if (use_opencl) log_warn("OpenCL support not compiled; using CPU only");
  if (!cpu) cpu = std::make_unique<CpuBackend>(threads);
#endif

  if (!cpu
#ifdef ONE_MINER_HAS_OPENCL
      && !ocl
#endif
  ) {
    log_error("no mining backends available");
    return 1;
  }

  std::mutex job_mu;
  PreparedJob prepared{};
  std::atomic<bool> has_job{false};
  std::atomic<uint64_t> counter{static_cast<uint64_t>(now_us())};
  MinerStats stats{};
  stats.version = kVersion;
  stats.nonce_mode = nonce_mode_name(mode);
  const auto started = std::chrono::steady_clock::now();

  auto install_job = [&](const MiningJob& job) {
    PreparedJob prep;
    std::string err;
    if (!prepare_job(job, mode, prep, err)) {
      log_error("prepare_job: " + err);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(job_mu);
      prepared = prep;
      has_job = true;
    }
    if (cpu) cpu->set_job(prep);
#ifdef ONE_MINER_HAS_OPENCL
    if (ocl) ocl->set_job(prep);
#endif
    log_info("New Job: " + job.previous_blockhash.substr(0, 12) +
             " height=" + std::to_string(job.height) +
             " share_diff=" + std::to_string(job.share_difficulty));
  };

  std::unique_ptr<RabbitPoolClient> rabbit;
  std::unique_ptr<NatsPoolClient> nats;
  std::function<bool(const MiningJob&, const ShareCandidate&, std::string&)> submit_fn;

  if (!pool_list.empty()) {
    auto eps = parse_rabbit_endpoints(pool_list, /*default_tls=*/true);
    rabbit = std::make_unique<RabbitPoolClient>(eps, wallet, worker);
    std::string err;
    if (!rabbit->connect_and_login(err)) {
      log_error(err);
      return 1;
    }
    submit_fn = [&](const MiningJob& j, const ShareCandidate& s, std::string& e) {
      return rabbit->submit(j, s, e);
    };
  } else {
    auto urls = split(nats_list, ',');
    nats = std::make_unique<NatsPoolClient>(urls, worker, wallet);
    nats->set_job_handler(install_job);
    std::string err;
    if (!nats->start(err)) {
      log_error(err);
      return 1;
    }
    submit_fn = [&](const MiningJob& j, const ShareCandidate& s, std::string& e) {
      return nats->publish_share(j, s, e);
    };
  }

  // Job poller for rabbit mode
  std::thread job_thread;
  if (rabbit) {
    job_thread = std::thread([&] {
      while (!g_stop) {
        MiningJob job;
        std::string err;
        if (rabbit->get_job(job, err)) {
          install_job(job);
        } else {
          log_warn("getjob failed: " + err + " — reconnecting");
          if (!rabbit->connect_and_login(err)) {
            log_error(err);
            std::this_thread::sleep_for(std::chrono::seconds(3));
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    });
  }

  auto handle_shares = [&](const std::vector<ShareCandidate>& shares, PreparedJob prep_snapshot) {
    for (const auto& s : shares) {
      std::string err;
      if (submit_fn(prep_snapshot.job, s, err)) {
        ++stats.accepted;
        log_info("share accepted nonce=" + s.nonce_hex.substr(0, 16) + " hash=" + s.blockhash_hex.substr(0, 16));
      } else {
        ++stats.rejected;
        log_warn("share rejected: " + err);
      }
    }
  };

  std::thread stats_thread([&] {
    while (!g_stop) {
      stats.uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      stats.devices.clear();
      stats.total_mhs = 0;
      if (cpu) {
        DeviceStatus d;
        d.backend = "CPU";
        d.index = 0;
        d.name = cpu->name();
        d.hashrate_mhs = cpu->last_mhs();
        d.total_hashes = cpu->hashes();
        d.accepted = stats.accepted;
        d.rejected = stats.rejected;
        stats.devices.push_back(d);
        stats.total_mhs += d.hashrate_mhs;
      }
#ifdef ONE_MINER_HAS_OPENCL
      if (ocl) {
        for (int i = 0; i < ocl->device_count(); ++i) {
          if (!devices.empty()) {
            bool ok = false;
            for (int sel : devices) if (sel == i) ok = true;
            if (!ok) continue;
          }
          DeviceStatus d;
          d.backend = "OPENCL";
          d.index = i;
          d.name = ocl->device_name(i);
          d.hashrate_mhs = ocl->last_mhs(i);
          stats.devices.push_back(d);
          stats.total_mhs += d.hashrate_mhs;
        }
      }
#endif
      write_stats_file(stats, stats_file);
      std::cout << format_gpu_status_table(stats) << std::flush;
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  });

  log_info("mining loop start");
  while (!g_stop) {
    if (!has_job.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    PreparedJob prep;
    {
      std::lock_guard<std::mutex> lock(job_mu);
      prep = prepared;
    }

    const uint64_t batch = 1ull << 20;  // 1M
    const uint64_t start = counter.fetch_add(batch);

#ifdef ONE_MINER_HAS_OPENCL
    if (ocl) {
      for (int i = 0; i < ocl->device_count() && !g_stop; ++i) {
        if (!devices.empty()) {
          bool ok = false;
          for (int sel : devices) if (sel == i) ok = true;
          if (!ok) continue;
        }
        auto shares = ocl->scan(i, start, batch, g_stop);
        handle_shares(shares, prep);
      }
    }
#endif
    if (cpu && !g_stop) {
      // If OpenCL present, CPU gets a smaller batch to keep GPUs primary.
      const uint64_t cpu_batch =
#ifdef ONE_MINER_HAS_OPENCL
          ocl ? (batch / 8) :
#endif
          batch;
      auto shares = cpu->scan(start, cpu_batch, 0, g_stop);
      handle_shares(shares, prep);
    }
  }

  if (job_thread.joinable()) job_thread.join();
  if (stats_thread.joinable()) stats_thread.join();
  if (nats) nats->stop();
  if (rabbit) rabbit->close();
  log_info("stopped");
  return 0;
}
