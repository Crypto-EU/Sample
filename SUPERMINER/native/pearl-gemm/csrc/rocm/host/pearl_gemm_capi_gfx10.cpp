// pearl_gemm_capi_gfx10.cpp — RDNA2 portable backend (gfx1030/gfx1031, RX 6000 series)
#include "../pearl_kernels_gfx10.cuh"
#include "capi/pearl_gemm_capi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define HSTREAM(s) static_cast<hipStream_t>(s)
static inline int rc_at(hipError_t e, const char* w) {
  if (e == hipSuccess) return 0;
  fprintf(stderr, "[pearl_gfx10] %s: %s\n", w, hipGetErrorString(e));
  return -100;
}
static inline int rc_ok(hipError_t e) { return rc_at(e, "hip"); }

static uint8_t* g_seedA = nullptr;
static uint8_t* g_seedB = nullptr;
static void ensure_seeds() {
  if (g_seedA) return;
  uint8_t a[32] = {}, b[32] = {};
  memcpy(a, "A_tensor", 8);
  memcpy(b, "B_tensor", 8);
  hipMalloc(&g_seedA, 32);
  hipMalloc(&g_seedB, 32);
  hipMemcpy(g_seedA, a, 32, hipMemcpyHostToDevice);
  hipMemcpy(g_seedB, b, 32, hipMemcpyHostToDevice);
}
static constexpr int kEAL = -1, kEBR = -4;

struct Gfx10Workspace {
  int m, n, k, r, ntiles;
  int8_t* Bn;
  int32_t* gemmScratch;
  uint8_t* dHeader;
  PearlCapiWorkspaceParams params;
  bool installed = false;
};

static void parallel_tensor_hash(const uint8_t* data, long len, const u32* key, u32* scratch,
                                 uint8_t* out, hipStream_t s, uint8_t* leaf_cvs = nullptr) {
  long nchunks = (len + 1023) / 1024;
  if (nchunks < 1) nchunks = 1;
  u32* bufA = scratch;
  u32* bufB = scratch + nchunks * 8;
  const int tpb = 256;
  hipLaunchKernelGGL(pk::k_blake_leaves, dim3((nchunks + tpb - 1) / tpb), dim3(tpb), 0, s, data,
                     len, nchunks, key, bufA);
  if (leaf_cvs)
    hipMemcpyAsync(leaf_cvs, bufA, (size_t)nchunks * 32, hipMemcpyDeviceToDevice, s);
  if (nchunks == 1) {
    hipMemcpyAsync(out, bufA, 32, hipMemcpyDeviceToDevice, s);
    return;
  }
  u32* in = bufA;
  u32* ob = bufB;
  long npairs = nchunks / 2;
  while (true) {
    hipLaunchKernelGGL(pk::k_blake_reduce, dim3((npairs + tpb - 1) / tpb), dim3(tpb), 0, s, in, ob,
                       npairs, key, 1);
    if (npairs == 1) {
      hipMemcpyAsync(out, ob, 32, hipMemcpyDeviceToDevice, s);
      break;
    }
    u32* t = in;
    in = ob;
    ob = t;
    npairs /= 2;
  }
}

extern "C" {

int pearl_capi_abi_version(void) { return 2; }
const char* pearl_capi_build_profile(void) { return "gfx1030"; }
int pearl_capi_supports_sm(int, int) { return 1; }
int pearl_capi_get_host_signal_sync_size(void) { return 8; }
int pearl_capi_get_host_signal_header_size(void) { return 640; }
int64_t pearl_capi_get_required_scratchpad_bytes(int64_t matrix_bytes, int) {
  int64_t nchunks = (matrix_bytes + 1023) / 1024;
  if (nchunks < 1) nchunks = 1;
  return 2 * nchunks * 32 + 4096;
}

int pearl_capi_lcg_int7_fill(void* dst, int64_t n, uint64_t seed_lo, uint64_t seed_hi, void* stream) {
  int blk = (int)((n / 8 + 63) / 64);
  if (blk < 1) blk = 1;
  hipLaunchKernelGGL(pk::k_lcg, dim3(blk), dim3(64), 0, HSTREAM(stream), (int8_t*)dst, (long)n,
                     seed_lo, seed_hi);
  return rc_at(hipGetLastError(), "lcg");
}
int pearl_capi_lcg_int7_fill_indirect(void*, int64_t, const void*, uint64_t, uint64_t, void*) {
  return -1;
}

int pearl_capi_tensor_hash(const uint8_t* data, uint32_t data_size, uint8_t* out, const uint8_t* key,
                           uint32_t, uint32_t, uint32_t, uint32_t, uint8_t* roots, int, void* stream) {
  hipLaunchKernelGGL(pk::k_tensor_hash, dim3(1), dim3(1), 0, HSTREAM(stream), data, (long)data_size,
                     (const u32*)key, 1, (u32*)roots, out);
  return rc_at(hipGetLastError(), "tensor_hash");
}
int pearl_capi_tensor_hash_leaf_cvs(const uint8_t* d, uint32_t s, uint8_t* o, const uint8_t* k,
                                    uint32_t, uint32_t, uint32_t, uint32_t, uint8_t* r,
                                    uint8_t* leaf_cvs, int, void* st) {
  parallel_tensor_hash(d, (long)s, (const u32*)k, (u32*)r, o, HSTREAM(st), leaf_cvs);
  return rc_at(hipGetLastError(), "tensor_hash_leaf_cvs");
}

int pearl_capi_commitment_hash_from_merkle_roots(const uint8_t* A, const uint8_t* B,
                                                 const uint8_t* key, uint8_t* CA, uint8_t* CB,
                                                 int, void* stream) {
  hipLaunchKernelGGL(pk::k_commitment, dim3(1), dim3(1), 0, HSTREAM(stream), A, B, key, CA, CB);
  return rc_at(hipGetLastError(), "commitment");
}

int pearl_capi_noise_gen(int R, int m, int n, int k, void* EAL, void* EAL_fp16, void* EAR_R,
                         void* EAR_K, void* EBL_R, void* EBL_K, void* EBR, void* EBR_fp16,
                         const uint8_t* key_A, const uint8_t* key_B, void* stream) {
  ensure_seeds();
  hipStream_t s = HSTREAM(stream);
  if (EAL) {
    int nh = ((m * R) + 31) / 32;
    hipLaunchKernelGGL(pk::k_uniform, dim3((nh + 63) / 64), dim3(64), 0, s, (int8_t*)EAL,
                     (__half*)EAL_fp16, m, R, kEAL, g_seedA, (const u32*)key_A);
  }
  if (EAR_K || EAR_R) {
    int dr = (k * 4 + 31) / 32;
    if (EAR_K) hipMemsetAsync(EAR_K, 0, (size_t)R * k, s);
    if (EAR_R) hipMemsetAsync(EAR_R, 0, (size_t)k * R, s);
    hipLaunchKernelGGL(pk::k_perm, dim3((dr + 63) / 64), dim3(64), 0, s, (int8_t*)EAR_K,
                       (int8_t*)EAR_R, k, R, g_seedA, (const u32*)key_A);
  }
  if (EBR) {
    int nh = ((n * R) + 31) / 32;
    hipLaunchKernelGGL(pk::k_uniform, dim3((nh + 63) / 64), dim3(64), 0, s, (int8_t*)EBR,
                     (__half*)EBR_fp16, n, R, kEBR, g_seedB, (const u32*)key_B);
  }
  if (EBL_K || EBL_R) {
    int dr = (k * 4 + 31) / 32;
    if (EBL_K) hipMemsetAsync(EBL_K, 0, (size_t)R * k, s);
    if (EBL_R) hipMemsetAsync(EBL_R, 0, (size_t)k * R, s);
    hipLaunchKernelGGL(pk::k_perm, dim3((dr + 63) / 64), dim3(64), 0, s, (int8_t*)EBL_K,
                       (int8_t*)EBL_R, k, R, g_seedB, (const u32*)key_B);
  }
  return rc_at(hipGetLastError(), "noise_gen");
}

int pearl_capi_bseed_expand_raw_device(const uint8_t* bseed, void* dst, int64_t n, void* stream) {
  u32 sw[8];
  for (int i = 0; i < 8; ++i)
    sw[i] = (u32)bseed[i * 4] | ((u32)bseed[i * 4 + 1] << 8) | ((u32)bseed[i * 4 + 2] << 16) |
            ((u32)bseed[i * 4 + 3] << 24);
  u32* dsw;
  hipMalloc(&dsw, 32);
  hipMemcpyAsync(dsw, sw, 32, hipMemcpyHostToDevice, HSTREAM(stream));
  long nblk = (n + 63) / 64;
  hipLaunchKernelGGL(pk::k_bseed, dim3((nblk + 63) / 64), dim3(64), 0, HSTREAM(stream), dsw,
                   (int8_t*)dst, (long)n);
  return rc_at(hipGetLastError(), "bseed");
}
int pearl_capi_bseed_expand_range_raw_device(const uint8_t*, uint64_t, void*, int64_t, void*) {
  return -1;
}

int pearl_capi_workspace_alloc(int32_t m, int32_t n, int32_t k, int32_t r, int, int, void** out,
                               void*) {
  auto* w = new Gfx10Workspace();
  w->m = m;
  w->n = n;
  w->k = k;
  w->r = r;
  w->ntiles = (m / 16) * (n / 16);
  hipMalloc(&w->Bn, (size_t)k * n);
  hipMalloc(&w->dHeader, 640);
  hipMalloc(&w->gemmScratch, (size_t)m * k * 4);
  *out = w;
  return 0;
}
int pearl_capi_workspace_free(void* ws, void*) {
  auto* w = (Gfx10Workspace*)ws;
  if (!w) return -1;
  hipFree(w->Bn);
  hipFree(w->dHeader);
  hipFree(w->gemmScratch);
  delete w;
  return 0;
}
int pearl_capi_workspace_install_params(void* ws, const PearlCapiWorkspaceParams* p) {
  auto* w = (Gfx10Workspace*)ws;
  if (!w || !p) return -1;
  w->params = *p;
  w->installed = true;
  hipLaunchKernelGGL(pk::k_transpose_i8, dim3((w->n * w->k + 255) / 256), dim3(256), 0, 0,
                     (const int8_t*)p->BpEB, w->Bn, w->n, w->k);
  hipDeviceSynchronize();
  return 0;
}

int pearl_capi_iter(void* ws, uint64_t seed_lo, void* host_signal_header_pinned, void* stream) {
  auto* w = (Gfx10Workspace*)ws;
  if (!w || !w->installed) return -3;
  const PearlCapiWorkspaceParams& p = w->params;
  hipStream_t s = HSTREAM(stream);
  int m = w->m, n = w->n, k = w->k, r = w->r;
  pearl_capi_lcg_int7_fill(p.A, (int64_t)m * k, seed_lo, p.sigma_seed, stream);
  parallel_tensor_hash((const uint8_t*)p.A, (long)m * k, (const u32*)p.Key, (u32*)p.Roots,
                       (uint8_t*)p.AHash, s);
  hipLaunchKernelGGL(pk::k_commitment, dim3(1), dim3(1), 0, s, (const uint8_t*)p.AHash,
                     (const uint8_t*)p.BHash, (const uint8_t*)p.Key, (uint8_t*)p.CommitA,
                     (uint8_t*)p.CommitB);
  pearl_capi_noise_gen(r, m, n, k, p.EAL, p.EAL_fp16, p.EAR_R_major, p.EAR_K_major, nullptr,
                       nullptr, nullptr, nullptr, (const uint8_t*)p.CommitA, nullptr, stream);
  {
    dim3 g((k / 16), (m / 16));
    dim3 b(16, 16);
    hipLaunchKernelGGL(pk10::k_wmma_gemm, g, b, 0, s, (const int8_t*)p.EAL,
                       (const int8_t*)p.EAR_K_major, w->gemmScratch, m, k, r);
  }
  hipLaunchKernelGGL(pk::k_add_i8, dim3((m * k + 255) / 256), dim3(256), 0, s, (const int8_t*)p.A,
                     w->gemmScratch, (int8_t*)p.ApEA, m * k);
  if (p.host_signal_sync) hipMemsetAsync(p.host_signal_sync, 0, 8, s);
  hipMemsetAsync(w->dHeader, 0, 640, s);
  {
    int nt = w->ntiles;
    hipLaunchKernelGGL(pk10::k_tgemm_pow, dim3((nt + 255) / 256), dim3(256), 0, s,
                       (const int8_t*)p.ApEA, w->Bn, m, n, k, r, (const u32*)p.pow_key,
                       (const u32*)p.pow_target, (int*)p.host_signal_sync, w->dHeader);
  }
  if (host_signal_header_pinned) {
    hipMemcpyAsync(host_signal_header_pinned, w->dHeader, 640, hipMemcpyDeviceToHost, s);
  }
  return rc_ok(hipGetLastError());
}
int pearl_capi_iter_batch(void* ws, uint64_t seed_lo_start, void* const* hdrs, int32_t count,
                          void* stream) {
  for (int i = 0; i < count; ++i) {
    int rc = pearl_capi_iter(ws, seed_lo_start + (uint64_t)i, hdrs ? hdrs[i] : nullptr, stream);
    if (rc) return rc;
  }
  return 0;
}
int pearl_capi_iter_batch_graph_prepare(void*, void* const*, int32_t, void*) { return -1; }
int pearl_capi_iter_batch_graph_launch(void*, uint64_t, void*) { return -1; }

int pearl_capi_noise_B(const PearlCapiNoiseBParams* p, void* stream) {
  if (!p) return -1;
  hipStream_t s = HSTREAM(stream);
  int n = p->n, k = p->k, r = p->r;
  int8_t *EBRt, *Bkn, *Bnoi;
  int32_t* EB;
  hipMalloc(&EBRt, (size_t)r * n);
  hipMalloc(&Bkn, (size_t)k * n);
  hipMalloc(&Bnoi, (size_t)k * n);
  hipMalloc(&EB, (size_t)k * n * 4);
  hipLaunchKernelGGL(pk::k_transpose_i8, dim3((n * r + 255) / 256), dim3(256), 0, s, (const int8_t*)p->EBR,
                     EBRt, n, r);
  {
    dim3 g((n / 16), (k / 16));
    dim3 b(16, 16);
    hipLaunchKernelGGL(pk10::k_wmma_gemm, g, b, 0, s, (const int8_t*)p->EBL_R_major, EBRt, EB, k,
                       n, r);
  }
  hipLaunchKernelGGL(pk::k_transpose_i8, dim3((n * k + 255) / 256), dim3(256), 0, s, (const int8_t*)p->B,
                     Bkn, n, k);
  hipLaunchKernelGGL(pk::k_add_i8, dim3((k * n + 255) / 256), dim3(256), 0, s, Bkn, EB, Bnoi, k * n);
  hipLaunchKernelGGL(pk::k_transpose_i8, dim3((k * n + 255) / 256), dim3(256), 0, s, Bnoi,
                     (int8_t*)p->BpEB, k, n);
  hipStreamSynchronize(s);
  hipFree(EBRt);
  hipFree(Bkn);
  hipFree(Bnoi);
  hipFree(EB);
  return rc_ok(hipGetLastError());
}

int pearl_capi_install_B(const PearlCapiInstallBParams* p, void* stream) {
  if (!p) return -1;
  hipStream_t s = HSTREAM(stream);
  if (p->expand_bseed && p->bseed) {
    int rc = pearl_capi_bseed_expand_raw_device((const uint8_t*)p->bseed, p->B, (int64_t)p->n * p->k,
                                                stream);
    if (rc) return rc;
    if (rc_at(hipStreamSynchronize(s), "install_B.bseed")) return -100;
  }
  parallel_tensor_hash((const uint8_t*)p->B, (long)p->n * p->k, (const u32*)p->Key, (u32*)p->Roots,
                       (uint8_t*)p->BHash, s, (uint8_t*)p->LeafCvs);
  if (rc_at(hipStreamSynchronize(s), "install_B.tensor_hash")) return -100;
  int rc = pearl_capi_commitment_hash_from_merkle_roots(
      (const uint8_t*)p->AHash, (const uint8_t*)p->BHash, (const uint8_t*)p->Key,
      (uint8_t*)p->CommitA, (uint8_t*)p->CommitB, p->device_id, stream);
  if (rc) return rc;
  if (rc_at(hipStreamSynchronize(s), "install_B.commitment")) return -100;
  rc = pearl_capi_noise_gen(p->r, p->m, p->n, p->k, nullptr, nullptr, nullptr, p->EAR_K_major,
                            p->EBL_R_major, p->EBL_K_major, p->EBR, p->EBR_fp16,
                            (const uint8_t*)p->CommitA, (const uint8_t*)p->CommitB, stream);
  if (rc) return rc;
  if (rc_at(hipStreamSynchronize(s), "install_B.noise_gen")) return -100;
  PearlCapiNoiseBParams nb{};
  nb.n = p->n;
  nb.k = p->k;
  nb.r = p->r;
  nb.B = p->B;
  nb.EAR_K_major = p->EAR_K_major;
  nb.EBL_R_major = p->EBL_R_major;
  nb.EBR = p->EBR;
  nb.EARxBpEB = p->EARxBpEB;
  nb.BpEB = p->BpEB;
  nb.workspace = p->workspace;
  return pearl_capi_noise_B(&nb, stream);
}

int pearl_capi_noisy_gemm(const PearlCapiNoisyGemmParams*, void*) { return -1; }

}  // extern "C"
