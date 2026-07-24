#include "sha256.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace oneminer {
namespace {

constexpr uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};

inline uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

}  // namespace

void sha256_init(Sha256State& state) {
  state = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
           0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
}

void sha256_transform(Sha256State& state, const uint8_t block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
           (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; ++i) {
    const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; ++i) {
    const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const uint32_t ch = (e & f) ^ ((~e) & g);
    const uint32_t t1 = h + S1 + ch + K[i] + w[i];
    const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t t2 = S0 + maj;
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_finalize_into(Sha256State state, const uint8_t* tail, size_t tail_len,
                          uint64_t total_len, Hash256& out) {
  uint8_t block[64];
  size_t offset = 0;
  while (offset + 64 <= tail_len) {
    sha256_transform(state, tail + offset);
    offset += 64;
  }
  const size_t rem = tail_len - offset;
  std::memcpy(block, tail + offset, rem);
  block[rem] = 0x80;
  if (rem >= 56) {
    std::memset(block + rem + 1, 0, 64 - rem - 1);
    sha256_transform(state, block);
    std::memset(block, 0, 56);
  } else {
    std::memset(block + rem + 1, 0, 56 - rem - 1);
  }
  const uint64_t bitlen = total_len * 8ull;
  for (int i = 0; i < 8; ++i) {
    block[63 - i] = static_cast<uint8_t>((bitlen >> (8 * i)) & 0xffu);
  }
  sha256_transform(state, block);
  for (int i = 0; i < 8; ++i) {
    out[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xffu);
    out[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xffu);
    out[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xffu);
    out[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xffu);
  }
}

void sha256(const uint8_t* data, size_t len, Hash256& out) {
  Sha256State st;
  sha256_init(st);
  size_t offset = 0;
  uint8_t block[64];
  while (offset + 64 <= len) {
    sha256_transform(st, data + offset);
    offset += 64;
  }
  const size_t rem = len - offset;
  std::memcpy(block, data + offset, rem);
  block[rem] = 0x80;
  if (rem >= 56) {
    std::memset(block + rem + 1, 0, 64 - rem - 1);
    sha256_transform(st, block);
    std::memset(block, 0, 56);
  } else {
    std::memset(block + rem + 1, 0, 56 - rem - 1);
  }
  const uint64_t bitlen = static_cast<uint64_t>(len) * 8ull;
  for (int i = 0; i < 8; ++i) {
    block[63 - i] = static_cast<uint8_t>((bitlen >> (8 * i)) & 0xffu);
  }
  sha256_transform(st, block);
  for (int i = 0; i < 8; ++i) {
    out[i * 4 + 0] = static_cast<uint8_t>((st[i] >> 24) & 0xffu);
    out[i * 4 + 1] = static_cast<uint8_t>((st[i] >> 16) & 0xffu);
    out[i * 4 + 2] = static_cast<uint8_t>((st[i] >> 8) & 0xffu);
    out[i * 4 + 3] = static_cast<uint8_t>(st[i] & 0xffu);
  }
}

Hash256 sha256(const std::string& s) {
  Hash256 out{};
  sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out);
  return out;
}

Hash256 sha256(const std::vector<uint8_t>& v) {
  Hash256 out{};
  sha256(v.data(), v.size(), out);
  return out;
}

std::string hash_to_hex(const Hash256& h) {
  static const char* kHex = "0123456789abcdef";
  std::string out(64, '0');
  for (size_t i = 0; i < 32; ++i) {
    out[i * 2] = kHex[(h[i] >> 4) & 0xf];
    out[i * 2 + 1] = kHex[h[i] & 0xf];
  }
  return out;
}

bool hex_to_bytes(const std::string& hex, std::vector<uint8_t>& out) {
  if (hex.size() % 2) return false;
  out.resize(hex.size() / 2);
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < out.size(); ++i) {
    const int hi = nibble(hex[i * 2]);
    const int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

}  // namespace oneminer
