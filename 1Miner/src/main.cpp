#include "backend.hpp"
#include "pool_nats.hpp"
#include "pool_rabbit.hpp"
#include "pow.hpp"
#include "util.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <functional>
#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

constexpr const char* kVersion = "1.0.21";
}  // namespace

static void usage(const char* argv0) {
  std::cout
      << "1Miner v" << kVersion << " — Saseul AMD-only OpenCL miner\n"
      << "Usage: " << argv0 << " [options]\n\n"
      << "This build mines only on AMD GPUs via OpenCL.\n"
      << "NVIDIA CUDA/OpenCL and CPU mining are disabled.\n\n"
      << "Pool modes:\n"
      << "  --pool HOST:PORT[,HOST:PORT...]   RabbitMiner-style login/getjob (TLS auto by port)\n"
      << "  --nats URL[,URL...]               saseulpool.com NATS mode\n"
      << "  --wallet ADDR[.WORKER]            Wallet (worker optional after '.')\n"
      << "  --worker NAME                     Worker name (default: hostname)\n"
      << "  --nonce-mode classic|latehex      Default: classic\n"
      << "  --amd-ocl                         Explicit AMD OpenCL (default, optional)\n"
      << "  --device LIST                     Comma AMD GPU indices (default: all)\n"
      << "  --stats-file PATH                 Default: /tmp/saseul-miner-stats.json\n"
      << "  --autotune                        Per-GPU OpenCL tune (default: on)\n"
      << "  --no-autotune                     Skip autotune; use defaults / cache\n"
      << "  --autotune-force                  Retune even if cache exists\n"
      << "  --autotune-cache PATH             Default: /tmp/1miner-autotune-1.0.21.json\n"
      << "  --help\n\n"
      << "HiveOS example:\n"
      << "  ./1miner --pool nl.rabbitminer.cc:1901 --wallet WALLET.worker\n"
      << "  Extra config: --autotune-force   (hasher-style retune; target ~3.4 GH/s/card)\n";
}

int main(int argc, char** argv) {
  using namespace oneminer;
  std::string pool_list;
  std::string nats_list;
  std::string wallet;
  std::string worker;
  std::string nonce_mode_s = "classic";
  std::string stats_file = "/tmp/saseul-miner-stats.json";
  std::string autotune_cache = "/tmp/1miner-autotune-1.0.21.json";
  bool do_autotune = true;
  bool force_autotune = false;
  std::vector<int> devices;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto need = [&](std::string& dst) {
      if (i + 1 >= argc) return false;
      dst = argv[++i];
      return true;
    };
    if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return 0;
    } else if (a == "--pool") {
      if (!need(pool_list)) return 2;
    } else if (a == "--nats") {
      if (!need(nats_list)) return 2;
    } else if (a == "--wallet") {
      if (!need(wallet)) return 2;
    } else if (a == "--worker" || a == "--id") {
      if (!need(worker)) return 2;
    } else if (a == "--nonce-mode") {
      if (!need(nonce_mode_s)) return 2;
    } else if (a == "--stats-file") {
      if (!need(stats_file)) return 2;
    } else if (a == "--amd-ocl" || a == "--opencl") {
      // default / accepted alias
    } else if (a == "--device") {
      std::string v;
      if (!need(v)) return 2;
      for (const auto& t : split(v, ',')) devices.push_back(std::stoi(t));
    } else if (a == "--autotune") {
      do_autotune = true;
    } else if (a == "--no-autotune") {
      do_autotune = false;
    } else if (a == "--autotune-force") {
      force_autotune = true;
      do_autotune = true;
    } else if (a == "--autotune-cache") {
      if (!need(autotune_cache)) return 2;
    } else if (a == "--use-cpu" || a == "--threads" || a == "--cuda" || a == "--nvidia-ocl") {
      log_error(a + " is disabled: 1Miner is AMD-GPU-only");
      return 2;
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

  const NonceMode mode = parse_nonce_mode(nonce_mode_s);
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  log_info(std::string("1Miner v") + kVersion +
           " AMD-only OpenCL + per-GPU autotune (hasher 5.2 PoW), nonce_mode=" +
           nonce_mode_name(mode));
  log_info("peak class ~3 GH/s on Navi10-class; targeting higher with round-5 closed form + W16-19 host precompute");

#ifndef ONE_MINER_HAS_OPENCL
  log_error("OpenCL support was not compiled into this binary");
  return 1;
#else
  auto ocl = std::make_unique<OpenClBackend>();
  std::string err;
  if (!ocl->init(err)) {
    log_error("AMD OpenCL init failed: " + err);
    return 1;
  }
  if (ocl->device_count() < 1) {
    log_error("no AMD OpenCL GPUs found");
    return 1;
  }
#endif

  std::mutex job_mu;
  MiningJob current_job{};
  std::atomic<bool> has_job{false};
  std::atomic<uint64_t> counter{static_cast<uint64_t>(now_us())};
  // hasher-style pool clock: pool_now = local_now + offset
  std::atomic<int64_t> time_offset_us{0};
  MinerStats stats{};
  stats.version = kVersion;
  stats.nonce_mode = nonce_mode_name(mode);
  const auto started = std::chrono::steady_clock::now();

  auto pool_now_us = [&]() -> int64_t { return now_us() + time_offset_us.load(); };

  auto learn_offset_from_error = [&](const std::string& serr) {
    const auto pos = serr.find("now=");
    if (pos == std::string::npos) return;
    try {
      const int64_t pool_now = std::stoll(serr.substr(pos + 4));
      time_offset_us = pool_now - now_us();
      log_info("pool timestamp sync offset_us=" + std::to_string(time_offset_us.load()));
    } catch (...) {
    }
  };

  auto install_job = [&](const MiningJob& job) {
    // Sync miner clock to pool worktime when present (hasher timestamp sync).
    if (job.worktime > 0) {
      time_offset_us = job.worktime - now_us();
    }
    {
      std::lock_guard<std::mutex> lock(job_mu);
      current_job = job;
      has_job = true;
    }
    log_info("New Job: " + job.previous_blockhash.substr(0, 12) +
             " height=" + std::to_string(job.height) +
             " share_diff=" + std::to_string(job.share_difficulty) +
             " worktime=" + std::to_string(job.worktime));
  };

  std::unique_ptr<RabbitPoolClient> rabbit;
  std::unique_ptr<NatsPoolClient> nats;
  std::function<bool(const MiningJob&, const ShareCandidate&, std::string&)> submit_fn;

  if (!pool_list.empty()) {
    auto eps = parse_rabbit_endpoints(pool_list, /*default_tls=*/true);
    rabbit = std::make_unique<RabbitPoolClient>(eps, wallet, worker);
    if (!rabbit->connect_and_login(err)) {
      log_error(err);
      return 1;
    }
    submit_fn = [&](const MiningJob& j, const ShareCandidate& s, std::string& e) {
      return rabbit->submit(j, s, e);
    };
    // hasher-style: learn pool clock when worktime is absent from getjob
    {
      MiningJob probe;
      probe.job_id = "clock-sync";
      ShareCandidate s;
      s.nonce_hex = "0000000000000000";
      s.timestamp_us = 1;
      s.blockhash_hex = std::string(78, '0');
      std::string serr;
      submit_fn(probe, s, serr);
      learn_offset_from_error(serr);
    }
  } else {
    auto urls = split(nats_list, ',');
    nats = std::make_unique<NatsPoolClient>(urls, worker, wallet);
    nats->set_job_handler(install_job);
    if (!nats->start(err)) {
      log_error(err);
      return 1;
    }
    submit_fn = [&](const MiningJob& j, const ShareCandidate& s, std::string& e) {
      return nats->publish_share(j, s, e);
    };
  }

  std::thread job_thread;
  if (rabbit) {
    job_thread = std::thread([&] {
      int fail_streak = 0;
      while (!g_stop) {
        MiningJob job;
        std::string jerr;
        if (rabbit->get_job(job, jerr)) {
          fail_streak = 0;
          install_job(job);
        } else {
          ++fail_streak;
          log_warn("getjob failed: " + jerr +
                   (fail_streak >= 2 ? " — reconnecting" : " — retrying"));
          if (fail_streak >= 2) {
            fail_streak = 0;
            if (!rabbit->connect_and_login(jerr)) {
              log_error(jerr);
              std::this_thread::sleep_for(std::chrono::seconds(3));
            } else {
              MiningJob probe;
              probe.job_id = "clock-sync";
              ShareCandidate s;
              s.nonce_hex = "0000000000000000";
              s.timestamp_us = 1;
              s.blockhash_hex = std::string(78, '0');
              std::string serr;
              rabbit->submit(probe, s, serr);
              learn_offset_from_error(serr);
            }
          }
        }
        std::this_thread::sleep_for(std::chrono::seconds(3));
      }
    });
  }

  auto handle_shares = [&](const std::vector<ShareCandidate>& shares, const PreparedJob& prep) {
    for (const auto& s : shares) {
      std::string serr;
      if (submit_fn(prep.job, s, serr)) {
        ++stats.accepted;
        log_info("share accepted nonce=" + s.nonce_hex +
                 " blockhash=" + s.blockhash_hex.substr(0, 16));
      } else {
        ++stats.rejected;
        log_warn("share rejected: " + serr);
        learn_offset_from_error(serr);
      }
    }
  };

  auto device_enabled = [&](int i) {
    if (devices.empty()) return true;
    for (int sel : devices)
      if (sel == i) return true;
    return false;
  };

  // Wait for first valid job, then autotune each selected GPU before mining.
  log_info("waiting for first job…");
  while (!g_stop && !has_job.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (g_stop) {
    if (job_thread.joinable()) {
      // signal already set; join after miners not started
    }
    if (nats) nats->stop();
    if (rabbit) rabbit->close();
    if (job_thread.joinable()) job_thread.join();
    return 0;
  }

  PreparedJob tune_prep;
  {
    MiningJob job;
    {
      std::lock_guard<std::mutex> lock(job_mu);
      job = current_job;
    }
    std::string perr;
    if (!prepare_job(job, mode, pool_now_us(), tune_prep, perr)) {
      log_error("prepare_job for autotune failed: " + perr);
      g_stop = true;
    } else if (do_autotune) {
      log_info(std::string("autotune HASHER-STYLE ") + (force_autotune ? "FORCE " : "") +
               "starting target~3.4 GH/s/card like hasher 5.2 (cache=" + autotune_cache + ")");
      ocl->autotune(tune_prep, g_stop, autotune_cache, force_autotune, devices);
      for (int i = 0; i < ocl->device_count(); ++i) {
        if (!device_enabled(i)) continue;
        const auto t = ocl->tune(i);
        log_info("gpu[" + std::to_string(i) + "]=" + ocl->device_name(i) +
                 " tune local=" + std::to_string(t.local) +
                 " intensity=" + std::to_string(t.intensity) +
                 " unroll=" + std::to_string(t.unroll) +
                 " chunks=" + std::to_string(t.chunks) +
                 " null_local=" + std::string(t.null_local ? "1" : "0") +
                 " batch=" + std::to_string(t.batch) +
                 " ~" + std::to_string(t.mhs) + " MH/s");
      }
    } else {
      if (ocl->apply_tune_cache(autotune_cache)) {
        log_info("autotune disabled — applied cache " + autotune_cache);
      } else {
        log_info("autotune disabled — using launch defaults");
      }
    }
  }

  std::thread stats_thread([&] {
    while (!g_stop) {
      stats.uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      stats.devices.clear();
      stats.total_mhs = 0;
      for (int i = 0; i < ocl->device_count(); ++i) {
        if (!device_enabled(i)) continue;
        DeviceStatus d;
        d.backend = "AMD";
        d.index = i;
        d.name = ocl->device_name(i);
        d.hashrate_mhs = ocl->last_mhs(i);
        d.total_hashes = ocl->total_hashes(i);
        d.accepted = stats.accepted;
        d.rejected = stats.rejected;
        stats.devices.push_back(d);
        stats.total_mhs += d.hashrate_mhs;
      }
      write_stats_file(stats, stats_file);
      std::cout << format_gpu_status_table(stats) << std::flush;
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }
  });

  log_info("mining loop start (AMD GPUs=" + std::to_string(ocl->device_count()) + ")");

  // One worker thread per GPU. Hold a timestamp generation for ~1.5s (hasher-style) so
  // midstate/blob stay hot; refresh before the pool's 5s drift window.
  std::mutex share_mu;
  std::vector<std::thread> miners;
  for (int i = 0; i < ocl->device_count(); ++i) {
    if (!device_enabled(i)) continue;
    miners.emplace_back([&, i] {
      PreparedJob prep;
      std::string prep_job_id;
      int64_t prep_ts = 0;
      auto prep_at = std::chrono::steady_clock::now() - std::chrono::seconds(10);
      while (!g_stop) {
        if (!has_job.load()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        MiningJob job;
        {
          std::lock_guard<std::mutex> lock(job_mu);
          job = current_job;
        }
        if (job.previous_blockhash.size() != 78) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          continue;
        }
        const auto now_st = std::chrono::steady_clock::now();
        const bool job_changed = job.job_id != prep_job_id;
        const bool aged =
            std::chrono::duration_cast<std::chrono::milliseconds>(now_st - prep_at).count() > 2500;
        if (job_changed || aged || prep_ts == 0) {
          const int64_t ts = pool_now_us();
          std::string perr;
          if (!prepare_job(job, mode, ts, prep, perr)) {
            log_warn("prepare_job: " + perr);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
          }
          prep_job_id = job.job_id;
          prep_ts = ts;
          prep_at = now_st;
        }
        uint64_t tuned = ocl->tuned_batch(i);
        if (tuned < (uint64_t{1} << 20)) tuned = uint64_t{1} << 28;
        // Fill as much of the hi32 window as practical (up to ~1<<30) so the GPU
        // runs long without host round-trips. Floor at max(tuned, 256M).
        uint64_t start = counter.load();
        const uint64_t room = (uint64_t{1} << 32) - (start & 0xffffffffull);
        uint64_t this_batch = std::max(tuned, uint64_t{1} << 28);
        if (this_batch > (uint64_t{1} << 30)) this_batch = uint64_t{1} << 30;
        this_batch = std::min(this_batch, room);
        // Align to unroll 8 (also satisfies 1/2/4).
        this_batch &= ~uint64_t{7};
        if (this_batch == 0) {
          // Remnant too small — skip to next hi32 window.
          if (room > 0) counter.fetch_add(room);
          continue;
        }
        start = counter.fetch_add(this_batch);
        // After fetch_add, re-clamp if another thread crossed the window (rare).
        {
          const uint64_t room2 = (uint64_t{1} << 32) - (start & 0xffffffffull);
          if (room2 < this_batch) {
            this_batch = room2 & ~uint64_t{7};
            if (this_batch == 0) continue;
          }
        }
        auto shares = ocl->scan(i, start, this_batch, prep, g_stop);
        if (!shares.empty()) {
          std::lock_guard<std::mutex> lock(share_mu);
          handle_shares(shares, prep);
        }
      }
    });
  }
  for (auto& t : miners) {
    if (t.joinable()) t.join();
  }

  if (job_thread.joinable()) job_thread.join();
  if (stats_thread.joinable()) stats_thread.join();
  if (nats) nats->stop();
  if (rabbit) rabbit->close();
  log_info("stopped");
  return 0;
}
