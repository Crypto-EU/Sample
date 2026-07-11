#pragma once

#include "matmul/solver.hpp"
#include "stratum/json.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace superhero::stratum {

struct MinerConfig {
    std::string pool_host{"stratum.minebtx.com"};
    int pool_port{3333};
    std::string wallet;
    std::string worker_name{"rig"};
    std::string password;
    uint64_t batch_size{262144};
    uint32_t workgroup_size{256};
};

struct MinerStats {
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<double> hashrate{0};
};

class StratumClient {
public:
    explicit StratumClient(MinerConfig config);
    ~StratumClient();

    void start();
    void stop();
    const MinerStats& stats() const { return stats_; }

private:
    void run();
    bool connect();
    bool handshake();
    bool read_line(std::string& line);
    bool send_rpc(int id, const std::string& method, const std::string& params_json);
    bool wait_for_rpc(int expected_id, JsonValue* result, std::string* error_msg);
    void handle_notify(const std::vector<JsonValue>& params);
    void handle_message(const JsonValue& msg);
    void mine_job();

    MinerConfig config_;
    MinerStats stats_;
    int sock_{-1};
    std::atomic<bool> running_{false};
    std::thread thread_;
    int msg_id_{1};
    std::string extranonce1_;
    int extranonce2_size_{4};
    double difficulty_{1.0};

    matmul::PowState current_state_{};
    matmul::PowConfig pow_config_{};
    std::string job_id_;
    std::string share_target_hex_;
    std::string current_parent_;
    uint64_t nonce_start_{0};
    bool has_job_{false};
    bool gpu_job_ready_{false};
};

int run_miner(const MinerConfig& config);

}  // namespace superhero::stratum
