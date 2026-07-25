// Verify round-5 closed form (A5c/E5c) and W16-19 precompute vs full SHA path.
#include <cstdint>
#include <cstdio>
#include <random>

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32u - n)); }
inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}
inline uint32_t bsig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
inline uint32_t bsig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
inline uint32_t ssig0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
inline uint32_t ssig1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

void sha_rounds(const uint32_t mid[8], const uint32_t w_in[16], int from, int to,
                uint32_t out[8]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = w_in[i];
  for (int i = 16; i < 64; ++i) {
    w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
  }
  uint32_t a = mid[0], b = mid[1], c = mid[2], d = mid[3];
  uint32_t e = mid[4], f = mid[5], g = mid[6], h = mid[7];
  for (int i = 0; i < to; ++i) {
    const uint32_t t1 = h + bsig1(e) + ch(e, f, g) + K[i] + w[i];
    const uint32_t t2 = bsig0(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
    if (i + 1 == from) {
      // capture mid-state after `from` rounds into out temporarily if needed — unused
    }
  }
  out[0] = a;
  out[1] = b;
  out[2] = c;
  out[3] = d;
  out[4] = e;
  out[5] = f;
  out[6] = g;
  out[7] = h;
  (void)from;
}

void partial(const uint32_t mid[8], const uint32_t w_in[16], int nrounds, uint32_t out[8]) {
  sha_rounds(mid, w_in, 0, nrounds, out);
}

// Kernel-equivalent FAST path: start after r4 with A5c/E5c + pre_w, finish to r63.
void fast_finish(const uint32_t wr[8], const uint32_t msg[16], uint32_t A5c, uint32_t E5c,
                 const uint32_t pre[4], uint32_t out[8]) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i) w[i] = msg[i];
  // Host precomputes W16-19; remaining schedule as usual.
  w[16] = pre[0];
  w[17] = pre[1];
  w[18] = pre[2];
  w[19] = pre[3];
  for (int i = 20; i < 64; ++i) {
    w[i] = ssig1(w[i - 2]) + w[i - 7] + ssig0(w[i - 15]) + w[i - 16];
  }

  uint32_t a = A5c + w[5];
  uint32_t e = E5c + w[5];
  uint32_t b = wr[0], c = wr[1], d = wr[2];
  uint32_t f = wr[4], g = wr[5], h = wr[6];

  for (int i = 6; i < 64; ++i) {
    const uint32_t t1 = h + bsig1(e) + ch(e, f, g) + K[i] + w[i];
    const uint32_t t2 = bsig0(a) + maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  out[0] = a;
  out[1] = b;
  out[2] = c;
  out[3] = d;
  out[4] = e;
  out[5] = f;
  out[6] = g;
  out[7] = h;
}

}  // namespace

int main() {
  std::mt19937 rng(0x1017u);
  std::uniform_int_distribution<uint32_t> dist;

  for (int trial = 0; trial < 256; ++trial) {
    uint32_t mid[8], block[16];
    for (int i = 0; i < 8; ++i) mid[i] = dist(rng);
    for (int i = 0; i < 16; ++i) block[i] = dist(rng);
    // Match hi32 padded layout: w8..w14 = 0, w15 = bitlen.
    for (int i = 8; i < 15; ++i) block[i] = 0;
    block[15] = 0x000004f0u;

    uint32_t wr4[8], after5[8], full[8];
    partial(mid, block, 5, wr4);
    partial(mid, block, 6, after5);
    partial(mid, block, 64, full);

    const uint32_t a0 = wr4[0], b0 = wr4[1], c0 = wr4[2], d0 = wr4[3];
    const uint32_t e0 = wr4[4], f0 = wr4[5], g0 = wr4[6], h0 = wr4[7];
    const uint32_t w5 = block[5];
    const uint32_t F5 = h0 + bsig1(e0) + ch(e0, f0, g0) + K[5];
    const uint32_t G5 = bsig0(a0) + maj(a0, b0, c0);
    const uint32_t A5c = F5 + G5;
    const uint32_t E5c = d0 + F5;

    if (A5c + w5 != after5[0] || E5c + w5 != after5[4]) {
      std::fprintf(stderr, "FAIL trial %d: A5c/E5c vs round-5\n", trial);
      return 1;
    }
    if (after5[1] != a0 || after5[2] != b0 || after5[3] != c0 || after5[5] != e0 ||
        after5[6] != f0 || after5[7] != g0) {
      std::fprintf(stderr, "FAIL trial %d: post-r5 register rotate\n", trial);
      return 1;
    }

    const uint32_t fw0 = block[0], fw1 = block[1], fw2 = block[2], fw3 = block[3], fw4 = block[4];
    const uint32_t pre0 = ssig0(fw1) + fw0;
    const uint32_t pre1 = ssig1(0x000004f0u) + ssig0(fw2) + fw1;
    const uint32_t pre2 = ssig1(pre0) + ssig0(fw3) + fw2;
    const uint32_t pre3 = ssig1(pre1) + ssig0(fw4) + fw3;
    // Standard schedule W16..W19 with zero-pad + bitlen.
    uint32_t wsch[64];
    for (int i = 0; i < 16; ++i) wsch[i] = block[i];
    for (int i = 16; i < 20; ++i) {
      wsch[i] = ssig1(wsch[i - 2]) + wsch[i - 7] + ssig0(wsch[i - 15]) + wsch[i - 16];
    }
    // With w8..w14=0, w15=bitlen: W16=SSIG0(w1)+w0; W17=SSIG1(w15)+SSIG0(w2)+w1; ...
    if (pre0 != wsch[16] || pre1 != wsch[17] || pre2 != wsch[18] || pre3 != wsch[19]) {
      std::fprintf(stderr, "FAIL trial %d: pre_w vs schedule\n", trial);
      return 1;
    }

    // W20/W21 crumbs used by kernel: pre_c20 + SSIG0(w5), pre_s21 + SSIG0(w6) + w5.
    wsch[20] = ssig1(wsch[18]) + wsch[13] + ssig0(wsch[5]) + wsch[4];
    wsch[21] = ssig1(wsch[19]) + wsch[14] + ssig0(wsch[6]) + wsch[5];
    // With w8..w14=0: W20 = SSIG1(W18)+SSIG0(w5)+w4; W21 = SSIG1(W19)+SSIG0(w6)+w5.
    const uint32_t pre_c20 = ssig1(pre2) + fw4;
    const uint32_t pre_s21 = ssig1(pre3);
    if (pre_c20 + ssig0(block[5]) != wsch[20] ||
        pre_s21 + ssig0(block[6]) + block[5] != wsch[21]) {
      std::fprintf(stderr, "FAIL trial %d: pre_c20/pre_s21 vs schedule\n", trial);
      return 1;
    }
    if (ssig0(0x000004f0u) != 0xe13c0097u) {
      std::fprintf(stderr, "FAIL trial %d: SSIG0_BITLEN constant\n", trial);
      return 1;
    }

    const uint32_t pre[4] = {pre0, pre1, pre2, pre3};
    uint32_t fast[8];
    fast_finish(wr4, block, A5c, E5c, pre, fast);
    for (int i = 0; i < 8; ++i) {
      if (fast[i] != full[i]) {
        std::fprintf(stderr, "FAIL trial %d: digest limb %d old=%08x fast=%08x\n", trial, i,
                     full[i], fast[i]);
        return 1;
      }
    }
  }

  std::puts("r5_fast_selfcheck OK (256 trials)");
  return 0;
}
