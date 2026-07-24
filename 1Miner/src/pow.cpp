#include "pow.hpp"

#include <algorithm>
#include <cstring>

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

Hash256 target_from_difficulty(uint64_t difficulty) {
  Hash256 out{};
  out.fill(0xff);
  if (difficulty <= 1) return out;

  // Compute floor(2^256 / difficulty) as a 256-bit big-endian target.
  // dividend is 2^256 represented with 9 little-endian 32-bit limbs (limb[8] = 1).
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
  // quotient[8] is the 2^256 place and is 0 for difficulty > 1.
  // quotient[7]..quotient[0] are the 256-bit result (little-endian limbs).
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

bool prepare_job(const MiningJob& job, NonceMode mode, PreparedJob& out, std::string& err) {
  if (job.previous_blockhash.size() != 78) {
    err = "previous_blockhash must be 78 hex chars";
    return false;
  }
  if (job.digest64.size() != 64) {
    err = "digest must be 64 hex chars";
    return false;
  }
  out.job = job;
  out.mode = mode;
  out.prefix_ascii = job.previous_blockhash + job.digest64;
  out.prefix_len = out.prefix_ascii.size();
  out.target = target_from_difficulty(job.share_difficulty == 0 ? 1 : job.share_difficulty);

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

}  // namespace oneminer
