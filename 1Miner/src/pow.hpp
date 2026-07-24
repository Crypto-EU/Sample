#pragma once
#include "sha256.hpp"

#include <cstdint>
#include <string>

namespace oneminer {

enum class NonceMode { Classic, Latehex };

struct MiningJob {
  std::string job_id;
  std::string previous_blockhash;  // 78 hex (timehash)
  std::string digest64;            // 64 hex
  std::string main_blockhash;
  std::string validator;
  std::string miner;
  int64_t height = 0;
  int64_t main_height = 0;
  uint64_t share_difficulty = 1;
  uint64_t network_difficulty = 1;
  int64_t worktime = 0;
};

struct PreparedJob {
  MiningJob job;
  NonceMode mode = NonceMode::Classic;
  std::string prefix_ascii;  // prev78 + digest64
  Sha256State midstate{};
  size_t prefix_len = 0;
  Hash256 target{};
};

struct ShareCandidate {
  std::string nonce_hex;
  Hash256 hash{};
  std::string blockhash_hex;
  int64_t timestamp_us = 0;
  int gpu_index = 0;
};

NonceMode parse_nonce_mode(const std::string& s);
std::string nonce_mode_name(NonceMode m);

bool prepare_job(const MiningJob& job, NonceMode mode, PreparedJob& out, std::string& err);
bool hash_meets_target(const Hash256& hash, const Hash256& target);
Hash256 target_from_difficulty(uint64_t difficulty);

// Classic: nonce is 16 hex chars of a uint64 counter.
// Latehex: nonce is 40 hex chars = 24 hex prefix + 16 hex counter.
std::string format_classic_nonce(uint64_t counter);
std::string format_latehex_nonce(uint64_t prefix24_hex_as_u96_high, uint64_t counter);
std::string format_latehex_nonce_from_prefix(const std::string& prefix24, uint64_t counter);

bool mine_hash_classic(const PreparedJob& prep, uint64_t counter, Hash256& out_hash);
bool mine_hash_latehex(const PreparedJob& prep, const std::string& nonce40, Hash256& out_hash);

}  // namespace oneminer
