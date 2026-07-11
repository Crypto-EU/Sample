#include "stratum/client.hpp"
#include "gpu/gpu_miner.hpp"
#include "matmul/solver.hpp"
#include "util/hex.hpp"
#include "util/log.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

static void print_usage() {
    std::fprintf(stderr,
        "SUPERHERO v0.2.6 - BTX (btx-matmul) GPU-only AMD miner for HiveOS\n\n"
        "Usage:\n"
        "  superhero --pool HOST:PORT --wallet WALLET [options]\n"
        "  superhero --benchmark [--batch-size N]\n"
        "  superhero --list-gpus\n"
        "  superhero --self-test\n\n"
        "Options:\n"
        "  --worker NAME        Worker suffix (default: rig)\n"
        "  --batch-size N       GPU nonces per kernel launch (default: 262144)\n"
        "  --workgroup N        OpenCL workgroup size (default: 256)\n"
        "  --password PASS      Pool password (default: x)\n"
        "  -v                   Verbose logging\n\n"
        "GPU env (AMD):\n"
        "  RX 5700 XT: export HSA_OVERRIDE_GFX_VERSION=10.1.0\n"
        "  RX 6800 XT: export HSA_OVERRIDE_GFX_VERSION=10.3.0\n"
        "  export GPU_MAX_ALLOC_PERCENT=100\n\n"
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

static int run_benchmark(uint64_t batch_size) {
    std::string err;
    auto& gpu = superhero::gpu::GpuMiner::instance();
    if (!gpu.init(&err)) {
        std::fprintf(stderr, "GPU init failed: %s\n", err.c_str());
        return 1;
    }
    gpu.set_batch_size(batch_size);

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

    if (!gpu.prepare_job(state, config, &err)) {
        std::fprintf(stderr, "GPU job prep failed: %s\n", err.c_str());
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    auto result = gpu.mine_batch(state, config, 0, batch_size, nullptr);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("GPU Benchmark [%s]: %llu hashes in %.2fs => %.2f H/s\n", gpu.device_name().c_str(),
                static_cast<unsigned long long>(result.tries), sec, result.tries / sec);
    return 0;
}

int main(int argc, char** argv) {
    superhero::stratum::MinerConfig cfg{};
    cfg.password = "x";
    bool benchmark = false;
    bool self_test = false;
    bool list_gpus = false;

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
        } else if (arg == "--list-gpus") {
            list_gpus = true;
        } else if (arg == "--self-test") {
            self_test = true;
        } else if (arg == "--pool" && i + 1 < argc) {
            if (!parse_host_port(argv[++i], cfg.pool_host, cfg.pool_port)) {
                std::fprintf(stderr, "invalid --pool value\n");
                return 1;
            }
        } else if (arg == "--wallet" && i + 1 < argc) {
            cfg.wallet = argv[++i];
        } else if (arg == "--worker" && i + 1 < argc) {
            cfg.worker_name = argv[++i];
        } else if (arg == "--batch-size" && i + 1 < argc) {
            cfg.batch_size = std::stoull(argv[++i]);
        } else if (arg == "--workgroup" && i + 1 < argc) {
            cfg.workgroup_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--password" && i + 1 < argc) {
            cfg.password = argv[++i];
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            print_usage();
            return 1;
        }
    }

    if (self_test) {
        std::printf("Run build target superhero-test for CPU vector validation.\n");
        return 0;
    }

    if (list_gpus) {
        std::string err;
        if (!superhero::gpu::GpuMiner::instance().init(&err)) {
            std::fprintf(stderr, "GPU init failed:\n%s\n", err.c_str());
            return 1;
        }
        std::printf("Selected GPU: %s\n", superhero::gpu::GpuMiner::instance().device_name().c_str());
        return 0;
    }

    if (benchmark) return run_benchmark(cfg.batch_size);

    if (cfg.wallet.empty()) {
        std::fprintf(stderr, "error: --wallet is required for mining\n");
        print_usage();
        return 1;
    }

    std::string err;
    if (!superhero::gpu::GpuMiner::instance().init(&err)) {
        std::fprintf(stderr, "FATAL: GPU required — %s\n", err.c_str());
        return 1;
    }

    std::fprintf(stderr, "SUPERHERO v0.2.6 GPU-only | %s | batch=%llu | pool=%s:%d\n",
                 superhero::gpu::GpuMiner::instance().device_name().c_str(),
                 static_cast<unsigned long long>(cfg.batch_size), cfg.pool_host.c_str(), cfg.pool_port);
    return superhero::stratum::run_miner(cfg);
}
