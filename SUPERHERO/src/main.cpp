#include "stratum/client.hpp"
#include "matmul/solver.hpp"
#include "util/hex.hpp"
#include "util/log.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static void print_usage() {
    std::fprintf(stderr,
        "SUPERHERO - BTX (btx-matmul) AMD miner for HiveOS\n\n"
        "Usage:\n"
        "  superhero --pool HOST:PORT --wallet WALLET [options]\n"
        "  superhero --benchmark [--threads N]\n"
        "  superhero --self-test\n\n"
        "Options:\n"
        "  --worker NAME        Worker suffix (default: rig)\n"
        "  --threads N          CPU threads (default: auto)\n"
        "  --batch-size N       Nonces per mining round (default: 500000)\n"
        "  --no-opencl          Disable OpenCL backend\n"
        "  --password PASS      Pool password (default: x)\n"
        "  -v                   Verbose logging\n\n"
        "Pools:\n"
        "  minebtx: stratum.minebtx.com:3333\n"
        "  SRBMiner-style: btx-eu.lproute.com:8660\n");
}

static bool parse_host_port(const std::string& spec, std::string& host, int& port) {
    const auto pos = spec.rfind(':');
    if (pos == std::string::npos) return false;
    host = spec.substr(0, pos);
    try {
        port = std::stoi(spec.substr(pos + 1));
    } catch (...) {
        return false;
    }
    return !host.empty() && port > 0;
}

static int run_benchmark(uint32_t threads) {
    superhero::matmul::PowState state{};
    state.version = 0x20000000;
    state.seed_a = *superhero::crypto::Uint256::from_hex(
        "376d8f3e225ed14f5614a884f822920360a7b021684bd74600aa5f88dbd32a27");
    state.seed_b = *superhero::crypto::Uint256::from_hex(
        "3609c5eaeae940efb3035712cd65b09f0330d77fdf852128a89069b3ac02f586");
    state.matmul_dim = 512;
    state.bits = 0x1d00ffff;

    superhero::matmul::PowConfig config{};
    config.n = 512;
    config.b = 16;
    config.r = 8;
    config.target = superhero::matmul::target_from_bits(state.bits);

    const auto job = superhero::matmul::prepare_job(state, config);
    const uint64_t batch = 100;
    const auto start = std::chrono::steady_clock::now();

    std::atomic<uint64_t> total{0};
    std::vector<std::thread> workers;
    for (uint32_t t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            superhero::matmul::PowState local = state;
            const uint64_t nonce_start = static_cast<uint64_t>(t) * batch;
            auto result = superhero::matmul::solve_range(local, config, job, nonce_start, batch, nullptr, nullptr);
            total += result.tries;
        });
    }
    for (auto& w : workers) w.join();

    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("Benchmark: %llu hashes in %.2fs => %.2f H/s (%u threads)\n",
                static_cast<unsigned long long>(total.load()), sec, total.load() / sec, threads);
    return 0;
}

int main(int argc, char** argv) {
    superhero::stratum::MinerConfig cfg{};
    cfg.password = "x";
    bool benchmark = false;
    bool self_test = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        }
        if (arg == "-v" || arg == "--verbose") {
            superhero::util::set_verbose(true);
        } else if (arg == "--benchmark") {
            benchmark = true;
        } else if (arg == "--self-test") {
            self_test = true;
        } else if (arg == "--no-opencl") {
            cfg.use_opencl = false;
        } else if (arg == "--pool" && i + 1 < argc) {
            if (!parse_host_port(argv[++i], cfg.pool_host, cfg.pool_port)) {
                std::fprintf(stderr, "invalid --pool value\n");
                return 1;
            }
        } else if (arg == "--wallet" && i + 1 < argc) {
            cfg.wallet = argv[++i];
        } else if (arg == "--worker" && i + 1 < argc) {
            cfg.worker_name = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            cfg.threads = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--batch-size" && i + 1 < argc) {
            cfg.batch_size = std::stoull(argv[++i]);
        } else if (arg == "--password" && i + 1 < argc) {
            cfg.password = argv[++i];
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            print_usage();
            return 1;
        }
    }

    if (self_test) {
        std::printf("Run build target superhero-test for vector validation.\n");
        return 0;
    }

    const uint32_t threads = cfg.threads ? cfg.threads : std::max(1u, std::thread::hardware_concurrency());
    if (benchmark) return run_benchmark(threads);

    if (cfg.wallet.empty()) {
        std::fprintf(stderr, "error: --wallet is required for mining\n");
        print_usage();
        return 1;
    }

    std::fprintf(stderr, "SUPERHERO v0.1.0 | BTX btx-matmul | threads=%u | pool=%s:%d\n",
                 threads, cfg.pool_host.c_str(), cfg.pool_port);
    return superhero::stratum::run_miner(cfg);
}
