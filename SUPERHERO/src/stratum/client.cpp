#include "stratum/client.hpp"

#include "crypto/uint256.hpp"
#include "gpu/gpu_miner.hpp"
#include "stratum/json.hpp"
#include "util/hex.hpp"
#include "util/log.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <optional>
#include <sys/socket.h>
#include <unistd.h>

namespace superhero::stratum {
namespace {

std::string worker_name(const MinerConfig& cfg) {
    if (cfg.worker_name.empty()) return cfg.wallet;
    return cfg.wallet + "." + cfg.worker_name;
}

}  // namespace

StratumClient::StratumClient(MinerConfig config) : config_(std::move(config)) {
    std::string err;
    if (!gpu::GpuMiner::instance().init(&err)) {
        util::log(util::LogLevel::Error, "GPU init failed: %s", err.c_str());
    } else {
        gpu::GpuMiner::instance().set_batch_size(config_.batch_size);
        gpu::GpuMiner::instance().set_workgroup_size(config_.workgroup_size);
    }
}

StratumClient::~StratumClient() { stop(); }

void StratumClient::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(); });
}

void StratumClient::stop() {
    if (!running_.exchange(false)) return;
    if (sock_ >= 0) ::shutdown(sock_, SHUT_RDWR);
    if (thread_.joinable()) thread_.join();
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
}

bool StratumClient::connect() {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string port = std::to_string(config_.pool_port);
    if (getaddrinfo(config_.pool_host.c_str(), port.c_str(), &hints, &res) != 0) {
        util::log(util::LogLevel::Error, "DNS lookup failed for %s", config_.pool_host.c_str());
        return false;
    }
    for (addrinfo* p = res; p; p = p->ai_next) {
        sock_ = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock_ < 0) continue;
        if (::connect(sock_, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(sock_);
        sock_ = -1;
    }
    freeaddrinfo(res);
    if (sock_ < 0) {
        util::log(util::LogLevel::Error, "connect failed to %s:%d", config_.pool_host.c_str(), config_.pool_port);
        return false;
    }
    util::log(util::LogLevel::Info, "connected to %s:%d", config_.pool_host.c_str(), config_.pool_port);
    return true;
}

bool StratumClient::read_line(std::string& line) {
    line.clear();
    char ch;
    while (running_) {
        const ssize_t n = ::recv(sock_, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') return true;
        line.push_back(ch);
    }
    return false;
}

bool StratumClient::send_rpc(int id, const std::string& method, const std::string& params_json) {
    std::string payload = "{\"id\":" + std::to_string(id) + ",\"method\":\"" + method + "\",\"params\":" + params_json + "}\n";
    const char* data = payload.c_str();
    size_t left = payload.size();
    while (left > 0) {
        const ssize_t sent = ::send(sock_, data, left, 0);
        if (sent <= 0) return false;
        data += sent;
        left -= static_cast<size_t>(sent);
    }
    return true;
}

bool StratumClient::handshake() {
    const std::string params = "[\"SUPERHERO/0.1.0\",{\"protocol_compliant\":[\"matmul-extended-v1\"]}]";
    if (!send_rpc(msg_id_++, "mining.subscribe", params)) return false;

    std::string line;
    while (read_line(line)) {
        auto parsed = parse_json(line);
        if (!parsed) continue;
        if (parsed->type != JsonValue::Type::Object) continue;
        if (json_get(*parsed, "method")) continue;
        if (const auto* result = json_get(*parsed, "result")) {
            if (result->type == JsonValue::Type::Array && result->array_value.size() >= 3) {
                extranonce1_ = json_string(result->array_value[1]);
                extranonce2_size_ = static_cast<int>(json_int(result->array_value[2]));
            }
            break;
        }
    }

    const std::string worker = worker_name(config_);
    const std::string auth_params = "[\"" + worker + "\",\"" + config_.password + "\"]";
    if (!send_rpc(msg_id_++, "mining.authorize", auth_params)) return false;

    while (read_line(line)) {
        auto parsed = parse_json(line);
        if (!parsed) continue;
        if (json_get(*parsed, "method")) continue;
        if (const auto* result = json_get(*parsed, "result")) {
            if (!json_bool(*result)) {
                util::log(util::LogLevel::Error, "authorize rejected");
                return false;
            }
            util::log(util::LogLevel::Info, "authorized as %s", worker.c_str());
            return true;
        }
    }
    return false;
}

void StratumClient::handle_notify(const std::vector<JsonValue>& params) {
    if (params.size() < 7) return;
    job_id_ = json_string(params[0]);
    current_state_.version = static_cast<int32_t>(json_int(params[1]));
    auto prev = crypto::Uint256::from_hex(json_string(params[2]));
    auto merkle = crypto::Uint256::from_hex(json_string(params[3]));
    if (!prev || !merkle) return;
    current_state_.previous_block_hash = *prev;
    current_state_.merkle_root = *merkle;
    current_state_.time = static_cast<uint32_t>(json_int(params[4]));
    auto bits = util::parse_uint32(json_string(params[5]));
    if (!bits) return;
    current_state_.bits = *bits;
    share_target_hex_ = json_string(params[6]);
    const bool clean = params.size() > 7 ? json_bool(params[7]) : true;

    if (params.size() > 8 && params[8].type == JsonValue::Type::Object) {
        for (const auto& [k, v] : params[8].object_value) {
            if (k == "seed_a") {
                if (auto s = crypto::Uint256::from_hex(json_string(v))) current_state_.seed_a = *s;
            } else if (k == "seed_b") {
                if (auto s = crypto::Uint256::from_hex(json_string(v))) current_state_.seed_b = *s;
            } else if (k == "matmul_n") {
                pow_config_.n = static_cast<uint32_t>(json_int(v));
            } else if (k == "matmul_b") {
                pow_config_.b = static_cast<uint32_t>(json_int(v));
            } else if (k == "matmul_r") {
                pow_config_.r = static_cast<uint32_t>(json_int(v));
            } else if (k == "nonce64_start") {
                nonce_start_ = static_cast<uint64_t>(json_int(v));
            }
        }
    }

    current_state_.matmul_dim = static_cast<uint16_t>(pow_config_.n);
    pow_config_.target = matmul::target_from_bits(current_state_.bits);

    if (!clean && current_parent_ == json_string(params[2])) {
        // keep nonce progress on same-parent rotations
    } else {
        current_parent_ = json_string(params[2]);
    }

    std::string err;
    if (!gpu::GpuMiner::instance().prepare_job(current_state_, pow_config_, &err)) {
        util::log(util::LogLevel::Error, "GPU job prep failed: %s", err.c_str());
        gpu_job_ready_ = false;
        return;
    }
    gpu_job_ready_ = true;
    has_job_ = true;
    util::log(util::LogLevel::Info, "new job %s prev=%s...", job_id_.c_str(), current_parent_.substr(0, 16).c_str());
}

void StratumClient::handle_message(const JsonValue& msg) {
    const auto* method_val = json_get(msg, "method");
    if (!method_val) return;
    const std::string method = json_string(*method_val);
    const auto* params_val = json_get(msg, "params");
    if (!params_val || params_val->type != JsonValue::Type::Array) return;
    const auto& params = params_val->array_value;

    if (method == "mining.notify") {
        handle_notify(params);
    } else if (method == "mining.set_difficulty" && !params.empty()) {
        difficulty_ = static_cast<double>(json_int(params[0]));
        util::log(util::LogLevel::Info, "difficulty=%.4f", difficulty_);
    } else if (method == "mining.set_extranonce" && params.size() >= 2) {
        extranonce1_ = json_string(params[0]);
        extranonce2_size_ = static_cast<int>(json_int(params[1]));
    }
}

void StratumClient::mine_job() {
    if (!has_job_ || !gpu_job_ready_) return;
    if (!gpu::GpuMiner::instance().available()) return;

    const auto share_target = share_target_hex_.empty()
        ? std::optional<crypto::ArithUint256>{}
        : std::optional<crypto::ArithUint256>{matmul::target_from_hex(share_target_hex_)};

    const auto start = std::chrono::steady_clock::now();
    auto result = gpu::gpu_mine_batch(current_state_, pow_config_, nonce_start_, config_.batch_size,
                                      share_target ? &*share_target : nullptr);

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (elapsed > 0) stats_.hashrate.store(result.tries / elapsed);

    nonce_start_ += config_.batch_size;

    if (result.found) {
        const std::string worker = worker_name(config_);
        char nonce_buf[17];
        std::snprintf(nonce_buf, sizeof(nonce_buf), "%016llx", static_cast<unsigned long long>(result.nonce));
        char ntime_buf[9];
        std::snprintf(ntime_buf, sizeof(ntime_buf), "%08x", current_state_.time);
        std::string en2(extranonce2_size_ * 2, '0');
        const std::string submit = "[\"" + worker + "\",\"" + job_id_ + "\",\"" + en2 + "\",\"" + ntime_buf + "\",\"" +
                                   nonce_buf + "\"]";
        send_rpc(msg_id_++, "mining.submit", submit);
        util::log(util::LogLevel::Info, "share found nonce=%llu", static_cast<unsigned long long>(result.nonce));
        stats_.accepted.fetch_add(1);
    }
}

void StratumClient::run() {
    while (running_) {
        if (!connect() || !handshake()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        std::string line;
        auto last_mine = std::chrono::steady_clock::now();
        while (running_ && read_line(line)) {
            auto parsed = parse_json(line);
            if (!parsed) continue;
            if (parsed->type != JsonValue::Type::Object) continue;
            if (json_get(*parsed, "method")) {
                handle_message(*parsed);
            }

            const auto now = std::chrono::steady_clock::now();
            if (has_job_ && std::chrono::duration<double>(now - last_mine).count() >= 0.1) {
                mine_job();
                last_mine = now;
            }
        }

        if (sock_ >= 0) {
            ::close(sock_);
            sock_ = -1;
        }
        util::log(util::LogLevel::Warn, "disconnected; reconnecting...");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

int run_miner(const MinerConfig& config) {
    StratumClient client(config);
    client.start();
    while (true) std::this_thread::sleep_for(std::chrono::seconds(60));
    return 0;
}

}  // namespace superhero::stratum
