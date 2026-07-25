// Offline PoW self-check matching hasher 5.2 / SASEUL Enc path.
#include "pow.hpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

using namespace oneminer;

static int fail(const char* msg) {
  std::cerr << "FAIL: " << msg << "\n";
  return 1;
}

int main() {
  if (hextime_us(0x123) != "00000000000123") return fail("hextime");
  if (receipt_root_hex({}).size() != 64) return fail("empty receipt_root size");
  if (receipt_root_hex({}) !=
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")
    return fail("empty receipt_root");

  MiningJob job;
  job.height = 1;
  job.main_height = 2;
  job.main_blockhash = std::string(64, 'a');
  job.validator = "val";
  job.miner = "min";
  job.previous_blockhash = std::string(78, 'b');
  job.share_difficulty = 1;
  job.receipts.clear();

  const int64_t ts = 1700000000000000LL;
  const std::string hh = build_header_hash_hex(job, ts);
  if (hh.size() != 64) return fail("header_hash size");

  const std::string nonce = "0000000000000001";
  const std::string root = root_hex_from_parts(job.previous_blockhash, hh, nonce);
  if (root.size() != 64) return fail("root size");
  const std::string bh = blockhash_from_root(root, ts);
  if (bh.size() != 78) return fail("blockhash size");
  if (bh.substr(0, 14) != hextime_us(ts)) return fail("blockhash hextime prefix");

  PreparedJob prep;
  std::string err;
  if (!prepare_job(job, NonceMode::Classic, ts, prep, err)) return fail(err.c_str());
  if (prep.header_hash_hex != hh) return fail("prepare header_hash");
  if (prep.prefix_ascii != job.previous_blockhash + hh) return fail("prefix");

  Hash256 h{};
  if (!mine_hash_classic(prep, 1, h)) {
    // difficulty=1 always meets target
    return fail("mine_hash_classic should meet target diff=1");
  }
  const std::string made = make_share_blockhash(prep, nonce, h);
  if (made != bh) return fail("make_share_blockhash mismatch");

  // Target from difficulty: diff=2 must be half of 2^256-ish (high bit clear of first limb roughly)
  const Hash256 t1 = target_from_difficulty(1);
  const Hash256 t2 = target_from_difficulty(2);
  if (!hash_meets_target(t2, t1)) return fail("target(2) should be <= target(1)");

  std::cout << "pow_selftest OK\n";
  std::cout << "header_hash=" << hh << "\n";
  std::cout << "root=" << root << "\n";
  std::cout << "blockhash=" << bh << "\n";
  return 0;
}
