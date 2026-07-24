#include "backend.hpp"

#include "util.hpp"

#include <chrono>
#include <thread>

namespace oneminer {

CpuBackend::CpuBackend(int threads) : threads_(threads < 1 ? 1 : threads) {
  name_ = "Host CPU x" + std::to_string(threads_);
}

void CpuBackend::set_job(const PreparedJob& job) { job_ = job; }

std::vector<ShareCandidate> CpuBackend::scan(uint64_t start, uint64_t count, int device_index,
                                             std::atomic<bool>& stop_flag) {
  std::vector<ShareCandidate> found;
  const auto t0 = std::chrono::steady_clock::now();
  uint64_t local_hashes = 0;

  auto worker = [&](uint64_t from, uint64_t to) {
    for (uint64_t c = from; c < to && !stop_flag.load(); ++c) {
      Hash256 h{};
      bool ok = false;
      std::string nonce;
      if (job_.mode == NonceMode::Latehex) {
        nonce = format_latehex_nonce(0, c);
        ok = mine_hash_latehex(job_, nonce, h);
      } else {
        nonce = format_classic_nonce(c);
        ok = mine_hash_classic(job_, c, h);
      }
      ++local_hashes;
      if (ok) {
        ShareCandidate s;
        s.nonce_hex = nonce;
        s.hash = h;
        s.blockhash_hex = hash_to_hex(h);
        s.timestamp_us = now_us();
        s.gpu_index = device_index;
        found.push_back(s);
      }
    }
  };

  if (threads_ == 1 || count < 10000) {
    worker(start, start + count);
  } else {
    std::vector<std::thread> pool;
    const uint64_t chunk = (count + static_cast<uint64_t>(threads_) - 1) / static_cast<uint64_t>(threads_);
    std::vector<std::vector<ShareCandidate>> parts(static_cast<size_t>(threads_));
    for (int t = 0; t < threads_; ++t) {
      const uint64_t from = start + static_cast<uint64_t>(t) * chunk;
      const uint64_t to = std::min(start + count, from + chunk);
      pool.emplace_back([&, t, from, to] {
        for (uint64_t c = from; c < to && !stop_flag.load(); ++c) {
          Hash256 h{};
          bool ok = false;
          std::string nonce;
          if (job_.mode == NonceMode::Latehex) {
            nonce = format_latehex_nonce(0, c);
            ok = mine_hash_latehex(job_, nonce, h);
          } else {
            nonce = format_classic_nonce(c);
            ok = mine_hash_classic(job_, c, h);
          }
          if (ok) {
            ShareCandidate s;
            s.nonce_hex = nonce;
            s.hash = h;
            s.blockhash_hex = hash_to_hex(h);
            s.timestamp_us = now_us();
            s.gpu_index = device_index;
            parts[static_cast<size_t>(t)].push_back(s);
          }
        }
      });
    }
    for (auto& th : pool) th.join();
    local_hashes = count;
    for (auto& p : parts) {
      found.insert(found.end(), p.begin(), p.end());
    }
  }

  hashes_ += local_hashes;
  const auto t1 = std::chrono::steady_clock::now();
  const double sec = std::chrono::duration<double>(t1 - t0).count();
  if (sec > 0) last_mhs_ = (static_cast<double>(local_hashes) / sec) / 1e6;
  return found;
}

}  // namespace oneminer
