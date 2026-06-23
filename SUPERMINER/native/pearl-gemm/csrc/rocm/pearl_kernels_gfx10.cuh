#pragma once
// Portable RDNA (gfx10/11) kernels — tile-parallel int8 GEMM + fused PoW.
#include "pearl_kernels.cuh"

namespace pk10 {

using pk::rotl32;
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

// One 16x16 output tile per block; 256 threads each own one (a,b) output.
__global__ void k_wmma_gemm(const int8_t* A, const int8_t* B, int32_t* C, int M, int N, int K) {
  const int ti = blockIdx.y;
  const int tj = blockIdx.x;
  const int a = threadIdx.y;
  const int b = threadIdx.x;
  const int m0 = ti * 16 + a;
  const int n0 = tj * 16 + b;
  if (m0 >= M || n0 >= N) {
    return;
  }
  int32_t sum = 0;
  for (int kk = 0; kk < K; ++kk) {
    sum += (int)A[(size_t)m0 * K + kk] * (int)B[(size_t)kk * N + n0];
  }
  C[(size_t)m0 * N + n0] = sum;
}

__device__ __forceinline__ void write_hit_header(uint8_t* hdr, int ti, int tj) {
  if (!hdr) {
    return;
  }
  *(int*)(hdr + 0) = 1;
  ((unsigned*)(hdr + 40))[0] = (unsigned)ti;
  ((unsigned*)(hdr + 40))[1] = (unsigned)tj;
  ((unsigned*)(hdr + 40))[2] = 0;
  *(unsigned short*)(hdr + 64) = 16;
  for (int e = 0; e < 16; ++e) {
    hdr[66 + e] = (unsigned char)e;
    hdr[322 + e] = (unsigned char)e;
  }
  ((int*)(hdr + 592))[0] = 16;
  ((int*)(hdr + 592))[1] = 16;
  ((int*)(hdr + 592))[2] = 0;
}

__global__ void __launch_bounds__(256) k_tgemm_pow(const int8_t* __restrict__ A,
                            const int8_t* __restrict__ Bn, int m, int n, int k, int R,
                            const u32* __restrict__ pow_key, const u32* __restrict__ pow_target,
                            int* __restrict__ host_signal, uint8_t* __restrict__ hdr) {
  int tilesN = n / 16;
  int t = blockIdx.x * blockDim.x + threadIdx.x;
  int nt = (m / 16) * tilesN;
  if (t >= nt) {
    return;
  }
  int ti = t / tilesN;
  int tj = t % tilesN;
  int m0 = ti * 16;
  int n0 = tj * 16;
  u32 tr[16];
  for (int i = 0; i < 16; ++i) {
    tr[i] = 0;
  }
  for (int s = 0; s < k / R; ++s) {
    u32 h = 0;
    for (int a = 0; a < 16; ++a) {
      const int8_t* Ar = A + (size_t)(m0 + a) * k + s * R;
      for (int b = 0; b < 16; ++b) {
        int32_t sum = 0;
#pragma unroll 4
        for (int kk = 0; kk < R; ++kk) {
          sum += (int)Ar[kk] * (int)Bn[(size_t)(s * R + kk) * n + n0 + b];
        }
        h ^= (u32)sum;
      }
    }
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
  for (int i = 0; i < 8; ++i) {
    hw[i] = (u32)hh[i * 4] | ((u32)hh[i * 4 + 1] << 8) | ((u32)hh[i * 4 + 2] << 16) |
            ((u32)hh[i * 4 + 3] << 24);
  }
  int fnd = 1;
  for (int i = 7; i >= 0; --i) {
    if (hw[i] > pow_target[i]) {
      fnd = 0;
      break;
    }
    if (hw[i] < pow_target[i]) {
      break;
    }
  }
  if (fnd && host_signal && atomicCAS(host_signal, 0, 1) == 0) {
    write_hit_header(hdr, ti, tj);
  }
}

}  // namespace pk10
