#include "pow.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace oneminer {

NonceMode parse_nonce_mode(const std::string& s) {
  std::string v = s;
  std::transform(v.begin(), v.end(), v.begin(), ::tolower);
  if (v == "latehex" || v == "latehex24" || v == "latehex25") return NonceMode::Latehex;
  return NonceMode::Classic;
}

std::string nonce_mode_name(NonceMode m) {
  return m == NonceMode::Latehex ? "latehex" : "classic";
}

std::string hextime_us(int64_t utime) {
  // Matches hasher / SASEUL Enc.hextime: "%014llx" lower-case, clipped to 14 chars.
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%014llx", static_cast<unsigned long long>(utime));
  std::string out(buf);
  if (out.size() > 14) out = out.substr(out.size() - 14);
  return out;
}

std::string receipt_root_hex(const std::vector<std::string>& receipts) {
  // hasher: empty receipts → sha256("")
  if (receipts.empty()) {
    Hash256 h{};
    sha256(reinterpret_cast<const uint8_t*>(""), 0, h);
    return hash_to_hex(h);
  }
  // Non-empty: pairwise sha256 of receipt strings (same as hasher merkle fold).
  std::vector<std::string> layer;
  layer.reserve(receipts.size());
  for (const auto& r : receipts) {
    Hash256 h{};
    sha256(reinterpret_cast<const uint8_t*>(r.data()), r.size(), h);
    layer.push_back(hash_to_hex(h));
  }
  while (layer.size() > 1) {
    std::vector<std::string> next;
    next.reserve((layer.size() + 1) / 2);
    for (size_t i = 0; i < layer.size(); i += 2) {
      if (i + 1 < layer.size()) {
        const std::string cat = layer[i] + layer[i + 1];
        Hash256 h{};
        sha256(reinterpret_cast<const uint8_t*>(cat.data()), cat.size(), h);
        next.push_back(hash_to_hex(h));
      } else {
        next.push_back(layer[i]);
      }
    }
    layer.swap(next);
  }
  return layer.empty() ? std::string(64, '0') : layer[0];
}

std::string build_header_hash_hex(const MiningJob& job, int64_t timestamp_us) {
  // Exact hasher HeaderHashPlan / build_header_hash layout (no JSON spaces, raw ostringstream).
  const std::string rr = receipt_root_hex(job.receipts);
  std::ostringstream oss;
  oss << "{\"height\":" << job.height << ",\"timestamp\":" << timestamp_us
      << ",\"receipt_root\":\"" << rr << "\",\"main_height\":" << job.main_height
      << ",\"main_blockhash\":\"" << job.main_blockhash << "\",\"validator\":\"" << job.validator
      << "\",\"miner\":\"" << job.miner << "\"}";
  const std::string s = oss.str();
  Hash256 h{};
  sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), h);
  return hash_to_hex(h);
}

std::string root_hex_from_parts(const std::string& previous_blockhash,
                                const std::string& header_hash_hex,
                                const std::string& nonce16) {
  const std::string msg = previous_blockhash + header_hash_hex + nonce16;
  Hash256 h{};
  sha256(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), h);
  return hash_to_hex(h);
}

std::string blockhash_from_root(const std::string& root64_hex, int64_t timestamp_us) {
  // time_hash_hex(root, ts) = hextime(ts) + sha256_hex(root_ascii)
  Hash256 h{};
  sha256(reinterpret_cast<const uint8_t*>(root64_hex.data()), root64_hex.size(), h);
  return hextime_us(timestamp_us) + hash_to_hex(h);
}

Hash256 target_from_difficulty(uint64_t difficulty) {
  Hash256 out{};
  out.fill(0xff);
  if (difficulty <= 1) return out;

  uint32_t dividend[9] = {0, 0, 0, 0, 0, 0, 0, 0, 1};
  uint32_t quotient[9] = {0};
  uint64_t remainder = 0;
  for (int i = 8; i >= 0; --i) {
    const unsigned __int128 cur =
        (static_cast<unsigned __int128>(remainder) << 32) | dividend[i];
    const uint64_t qdigit = static_cast<uint64_t>(cur / difficulty);
    remainder = static_cast<uint64_t>(cur % difficulty);
    quotient[i] = static_cast<uint32_t>(qdigit);
  }
  for (int i = 0; i < 8; ++i) {
    const uint32_t limb = quotient[7 - i];
    out[i * 4 + 0] = static_cast<uint8_t>((limb >> 24) & 0xff);
    out[i * 4 + 1] = static_cast<uint8_t>((limb >> 16) & 0xff);
    out[i * 4 + 2] = static_cast<uint8_t>((limb >> 8) & 0xff);
    out[i * 4 + 3] = static_cast<uint8_t>(limb & 0xff);
  }
  return out;
}

bool hash_meets_target(const Hash256& hash, const Hash256& target) {
  return std::memcmp(hash.data(), target.data(), 32) <= 0;
}

bool prepare_job(const MiningJob& job, NonceMode mode, int64_t timestamp_us, PreparedJob& out,
                 std::string& err) {
  if (job.previous_blockhash.size() != 78) {
    err = "previous_blockhash must be 78 hex chars";
    return false;
  }
  out.job = job;
  out.mode = mode;
  out.timestamp_us = timestamp_us;
  out.target = target_from_difficulty(job.share_difficulty == 0 ? 1 : job.share_difficulty);

  if (mode == NonceMode::Classic) {
    out.header_hash_hex = build_header_hash_hex(job, timestamp_us);
    if (out.header_hash_hex.size() != 64) {
      err = "header_hash size";
      return false;
    }
    // hasher classic: prev78 + header_hash64 (+ nonce16 while mining)
    out.prefix_ascii = job.previous_blockhash + out.header_hash_hex;
  } else {
    // latehex still uses digest64 prefix path until specialized
    if (job.digest64.size() != 64) {
      err = "digest must be 64 hex chars for latehex";
      return false;
    }
    out.header_hash_hex = job.digest64;
    out.prefix_ascii = job.previous_blockhash + job.digest64;
  }
  out.prefix_len = out.prefix_ascii.size();

  sha256_init(out.midstate);
  const auto* p = reinterpret_cast<const uint8_t*>(out.prefix_ascii.data());
  size_t off = 0;
  while (off + 64 <= out.prefix_len) {
    sha256_transform(out.midstate, p + off);
    off += 64;
  }
  return true;
}

std::string format_classic_nonce(uint64_t counter) {
  static const char* kHex = "0123456789abcdef";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i) {
    out[i] = kHex[counter & 0xf];
    counter >>= 4;
  }
  return out;
}

std::string format_latehex_nonce_from_prefix(const std::string& prefix24, uint64_t counter) {
  std::string p = prefix24;
  if (p.size() < 24) p = std::string(24 - p.size(), '0') + p;
  if (p.size() > 24) p = p.substr(p.size() - 24);
  return p + format_classic_nonce(counter);
}

std::string format_latehex_nonce(uint64_t /*prefix*/, uint64_t counter) {
  return format_latehex_nonce_from_prefix("000000000000000000000000", counter);
}

bool mine_hash_classic(const PreparedJob& prep, uint64_t counter, Hash256& out_hash) {
  const std::string nonce = format_classic_nonce(counter);
  const std::string msg = prep.prefix_ascii + nonce;
  sha256(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), out_hash);
  return hash_meets_target(out_hash, prep.target);
}

bool mine_hash_latehex(const PreparedJob& prep, const std::string& nonce40, Hash256& out_hash) {
  if (nonce40.size() != 40) return false;
  const std::string msg = prep.prefix_ascii + nonce40;
  sha256(reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), out_hash);
  return hash_meets_target(out_hash, prep.target);
}

std::string make_share_blockhash(const PreparedJob& prep, const std::string& nonce16,
                                 const Hash256& root) {
  const std::string root_hex = hash_to_hex(root);
  (void)nonce16;
  (void)prep;
  return blockhash_from_root(root_hex, prep.timestamp_us);
}

}  // namespace oneminer
