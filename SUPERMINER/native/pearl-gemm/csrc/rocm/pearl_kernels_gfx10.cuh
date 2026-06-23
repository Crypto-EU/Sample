#pragma once
// Portable RDNA2 (gfx1030/gfx1031) kernels — no rocWMMA/MFMA required.
#include "pearl_kernels.cuh"

namespace pk10 {

using pk::rotl32;
using pk::xorreduce64;
using pk::k_lcg;
using pk::k_tensor_hash;
using pk::k_commitment;
using pk::k_uniform;
using pk::k_perm;
using pk::k_add_i8;
using pk::k_transpose_i8;
using pk::k_bseed;
using pk::k_blake_leaves;
using pk::k_blake_reduce;

__global__ void k_wmma_gemm(const int8_t* A, const int8_t* B, int32_t* C, int M, int N, int K) {
  int idx = blockIdx.x * blockDim.x + threadIdx.x;
  int total = (M / 16) * (N / 16) * 256;
  if (idx >= total) return;
  int tile = idx / 256;
  int lane = idx % 256;
  int ti = tile / (N / 16);
  int tj = tile % (N / 16);
  int a = lane / 16;
  int b = lane % 16;
  int m0 = ti * 16 + a;
  int n0 = tj * 16 + b;
  int32_t sum = 0;
  for (int kk = 0; kk < K; ++kk) {
    sum += (int)A[(size_t)m0 * K + kk] * (int)B[(size_t)kk * N + n0];
  }
  C[(size_t)ti * 16 * N + tj * 16 * 16 + a * 16 + b] = sum;
}

__global__ void k_transcript(const int8_t* ApEA, const int8_t* Bn, int m, int n, int k, int R,
                             const u32* pow_key, const u32* pow_target, u32* transcripts,
                             uint8_t* powhash, int32_t* found) {
  int tilesN = n / 16;
  int t = blockIdx.x * blockDim.x + threadIdx.x;
  int nt = (m / 16) * tilesN;
  if (t >= nt) return;
  int ti = t / tilesN, tj = t % tilesN;
  int m0 = ti * 16, n0 = tj * 16;
  int32_t acc[16][16];
  for (int a = 0; a < 16; ++a)
    for (int b = 0; b < 16; ++b) acc[a][b] = 0;
  u32 tr[16];
  for (int i = 0; i < 16; ++i) tr[i] = 0;
  for (int s = 0; s < k / R; ++s) {
    for (int a = 0; a < 16; ++a) {
      const int8_t* Ar = ApEA + (size_t)(m0 + a) * k + s * R;
      for (int b = 0; b < 16; ++b) {
        int32_t sum = 0;
        for (int kk = 0; kk < R; ++kk)
          sum += (int)Ar[kk] * (int)Bn[(size_t)(s * R + kk) * n + n0 + b];
        acc[a][b] += sum;
      }
    }
    u32 h = 0;
    for (int a = 0; a < 16; ++a)
      for (int b = 0; b < 16; ++b) h ^= (u32)acc[a][b];
    tr[s % 16] = rotl32(tr[s % 16], 13) ^ h;
  }
  for (int i = 0; i < 16; ++i) transcripts[(size_t)t * 16 + i] = tr[i];
  uint8_t tb[64];
  for (int i = 0; i < 16; ++i) {
    tb[i * 4] = tr[i] & 0xff;
    tb[i * 4 + 1] = (tr[i] >> 8) & 0xff;
    tb[i * 4 + 2] = (tr[i] >> 16) & 0xff;
    tb[i * 4 + 3] = (tr[i] >> 24) & 0xff;
  }
  uint8_t hh[32];
  b3::hash_small(tb, 64, pow_key, hh);
  for (int i = 0; i < 32; ++i) powhash[(size_t)t * 32 + i] = hh[i];
  u32 hw[8];
  for (int i = 0; i < 8; ++i)
    hw[i] = (u32)hh[i * 4] | ((u32)hh[i * 4 + 1] << 8) | ((u32)hh[i * 4 + 2] << 16) |
            ((u32)hh[i * 4 + 3] << 24);
  int fnd = 1;
  for (int i = 7; i >= 0; --i) {
    if (hw[i] > pow_target[i]) {
      fnd = 0;
      break;
    }
    if (hw[i] < pow_target[i]) break;
  }
  found[t] = fnd;
}

__global__ void k_tgemm_pow(const int8_t* __restrict__ A, const int8_t* __restrict__ Bn, int m,
                            int n, int k, int R, const u32* __restrict__ pow_key,
                            const u32* __restrict__ pow_target, int* __restrict__ host_signal) {
  int tilesN = n / 16;
  int t = blockIdx.x * blockDim.x + threadIdx.x;
  int nt = (m / 16) * tilesN;
  if (t >= nt) return;
  int ti = t / tilesN, tj = t % tilesN;
  int m0 = ti * 16, n0 = tj * 16;
  int32_t acc[16][16];
  for (int a = 0; a < 16; ++a)
    for (int b = 0; b < 16; ++b) acc[a][b] = 0;
  u32 tr[16];
  for (int i = 0; i < 16; ++i) tr[i] = 0;
  for (int s = 0; s < k / R; ++s) {
    for (int a = 0; a < 16; ++a) {
      const int8_t* Ar = A + (size_t)(m0 + a) * k + s * R;
      for (int b = 0; b < 16; ++b) {
        int32_t sum = 0;
        for (int kk = 0; kk < R; ++kk)
          sum += (int)Ar[kk] * (int)Bn[(size_t)(s * R + kk) * n + n0 + b];
        acc[a][b] += sum;
      }
    }
    u32 h = 0;
    for (int a = 0; a < 16; ++a)
      for (int b = 0; b < 16; ++b) h ^= (u32)acc[a][b];
    tr[s % 16] = rotl32(tr[s % 16], 13) ^ h;
  }
  uint8_t tb[64];
  for (int i = 0; i < 16; ++i) {
    tb[i * 4] = tr[i] & 0xff;
    tb[i * 4 + 1] = (tr[i] >> 8) & 0xff;
    tb[i * 4 + 2] = (tr[i] >> 16) & 0xff;
    tb[i * 4 + 3] = (tr[i] >> 24) & 0xff;
  }
  uint8_t hh[32];
  b3::hash_small(tb, 64, pow_key, hh);
  u32 hw[8];
  for (int i = 0; i < 8; ++i)
    hw[i] = (u32)hh[i * 4] | ((u32)hh[i * 4 + 1] << 8) | ((u32)hh[i * 4 + 2] << 16) |
            ((u32)hh[i * 4 + 3] << 24);
  int fnd = 1;
  for (int i = 7; i >= 0; --i) {
    if (hw[i] > pow_target[i]) {
      fnd = 0;
      break;
    }
    if (hw[i] < pow_target[i]) break;
  }
  if (fnd && host_signal) atomicExch(host_signal, 1);
}

}  // namespace pk10
