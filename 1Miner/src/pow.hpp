#pragma once
#include "sha256.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace oneminer {

enum class NonceMode { Classic, Latehex };

struct MiningJob {
  std::string job_id;
  std::string previous_blockhash;  // 78 hex (timehash)
  std::string digest64;            // pool job_digest (informational; classic mining uses header_hash)
  std::string main_blockhash;
  std::string validator;
  std::string miner;
  std::vector<std::string> receipts;
  int64_t height = 0;
  int64_t main_height = 0;
  uint64_t share_difficulty = 1;
  uint64_t network_difficulty = 1;
  int64_t worktime = 0;
};

struct PreparedJob {
  MiningJob job;
  NonceMode mode = NonceMode::Classic;
  // Classic hasher-compatible message: previous_blockhash(78) + header_hash(64) + nonce(16)
  std::string prefix_ascii;
  Sha256State midstate{};
  size_t prefix_len = 0;
  Hash256 target{};
  std::string header_hash_hex;  // 64 hex
  int64_t timestamp_us = 0;
};

struct ShareCandidate {
  std::string nonce_hex;
  Hash256 hash{};             // 32-byte POW root
  std::string blockhash_hex;  // 78-char timehash submitted to pool
  int64_t timestamp_us = 0;
  int gpu_index = 0;
};

NonceMode parse_nonce_mode(const std::string& s);
std::string nonce_mode_name(NonceMode m);

std::string hextime_us(int64_t utime);
std::string receipt_root_hex(const std::vector<std::string>& receipts);
std::string build_header_hash_hex(const MiningJob& job, int64_t timestamp_us);
std::string root_hex_from_parts(const std::string& previous_blockhash,
                                const std::string& header_hash_hex,
                                const std::string& nonce16);
std::string blockhash_from_root(const std::string& root64_hex, int64_t timestamp_us);

bool prepare_job(const MiningJob& job, NonceMode mode, int64_t timestamp_us, PreparedJob& out,
                 std::string& err);
bool hash_meets_target(const Hash256& hash, const Hash256& target);
Hash256 target_from_difficulty(uint64_t difficulty);

std::string format_classic_nonce(uint64_t counter);
std::string format_latehex_nonce(uint64_t prefix24_hex_as_u96_high, uint64_t counter);
std::string format_latehex_nonce_from_prefix(const std::string& prefix24, uint64_t counter);

bool mine_hash_classic(const PreparedJob& prep, uint64_t counter, Hash256& out_hash);
bool mine_hash_latehex(const PreparedJob& prep, const std::string& nonce40, Hash256& out_hash);

// Build submit blockhash (78 hex) for a classic share.
std::string make_share_blockhash(const PreparedJob& prep, const std::string& nonce16,
                                 const Hash256& root);

}  // namespace oneminer
