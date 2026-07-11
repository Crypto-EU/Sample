// SUPERHERO GPU kernels - BTX btx-matmul for AMD OpenCL (GPU-only mining)

#define MOD 0x7FFFFFFFu
#define REDUCE_INTERVAL 4u
#define MATRIX_N 512u
#define BLOCK_B 16u
#define NOISE_R 8u
#define BLOCKS_PER_AXIS 32u
#define CLEAN_BLOCK_ELEMS 256u
#define HEADER_BYTES 150u

constant uint K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline uint rotr32(uint x, uint n) { return (x >> n) | (x << (32u - n)); }
inline uint ch(uint x, uint y, uint z) { return (x & y) ^ (~x & z); }
inline uint maj(uint x, uint y, uint z) { return (x & y) ^ (x & z) ^ (y & z); }
inline uint s0(uint x) { return rotr32(x,2)^rotr32(x,13)^rotr32(x,22); }
inline uint s1(uint x) { return rotr32(x,6)^rotr32(x,11)^rotr32(x,25); }
inline uint g0(uint x) { return rotr32(x,7)^rotr32(x,18)^(x>>3); }
inline uint g1(uint x) { return rotr32(x,17)^rotr32(x,19)^(x>>10); }

inline uint m31_reduce64(ulong x) {
    ulong f = (x & (ulong)MOD) + (x >> 31);
    uint lo = (uint)(f & MOD);
    uint hi = (uint)(f >> 31);
    uint r = lo + hi;
    return (r >= MOD) ? (r - MOD) : r;
}
inline uint m31_add(uint a, uint b) { uint s=a+b; return (s>=MOD)?(s-MOD):s; }
inline uint m31_mul(uint a, uint b) { return m31_reduce64((ulong)a*(ulong)b); }

inline void sha256_compress(uint st[8], uint w[16]) {
    uint wex[64];
    for (int i = 0; i < 16; ++i) wex[i] = w[i];
    for (int t = 16; t < 64; ++t)
        wex[t] = g1(wex[t-2]) + wex[t-7] + g0(wex[t-15]) + wex[t-16];
    uint a=st[0],b=st[1],c=st[2],d=st[3],e=st[4],f=st[5],g=st[6],h=st[7];
    for (int t = 0; t < 64; ++t) {
        uint t1 = h + s1(e) + ch(e,f,g) + K256[t] + wex[t];
        uint t2 = s0(a) + maj(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    st[0]+=a; st[1]+=b; st[2]+=c; st[3]+=d; st[4]+=e; st[5]+=f; st[6]+=g; st[7]+=h;
}

inline void sha256_oneshot(const uchar* msg, uint msg_len, uchar out[32]) {
    uint w[16];
    for (int i = 0; i < 16; ++i) w[i] = 0;
    for (uint i = 0; i < msg_len; ++i)
        w[i>>2] |= (uint)msg[i] << (24u - (i&3u)*8u);
    w[msg_len>>2] |= 0x80u << (24u - (msg_len&3u)*8u);
    w[15] = msg_len * 8u;
    uint st[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    sha256_compress(st, w);
    for (int i = 0; i < 8; ++i) {
        out[i*4+0]=(uchar)(st[i]>>24); out[i*4+1]=(uchar)(st[i]>>16);
        out[i*4+2]=(uchar)(st[i]>>8); out[i*4+3]=(uchar)st[i];
    }
}

inline void sha256d(const uchar* msg, uint msg_len, uchar out[32]) {
    uchar t[32];
    sha256_oneshot(msg, msg_len, t);
    sha256_oneshot(t, 32, out);
}

inline uint from_oracle(const uchar seed_le[32], uint index) {
    uchar msg[36];
    for (int i = 0; i < 32; ++i) msg[i] = seed_le[31-i];
    msg[32]=(uchar)index; msg[33]=(uchar)(index>>8); msg[34]=(uchar)(index>>16); msg[35]=(uchar)(index>>24);
    uchar hash[32];
    sha256_oneshot(msg, 36, hash);
    uint cand = ((uint)hash[0]<<24)|((uint)hash[1]<<16)|((uint)hash[2]<<8)|(uint)hash[3];
    if (cand < MOD) return cand;
    return cand % MOD;
}

inline void derive_noise_seed(uchar tag0, uchar tag1, uchar tag2, uchar tag3, uchar tag4, uchar tag5,
                            uchar tag6, uchar tag7, uchar tag8, uchar tag9, uchar tag10, uchar tag11,
                            uchar tag12, uchar tag13, uchar tag14, uchar tag15, uchar tag16, uchar tag17,
                            const uchar sigma_le[32], uchar out_le[32]) {
    uchar msg[50];
    msg[0]=tag0; msg[1]=tag1; msg[2]=tag2; msg[3]=tag3; msg[4]=tag4; msg[5]=tag5; msg[6]=tag6; msg[7]=tag7;
    msg[8]=tag8; msg[9]=tag9; msg[10]=tag10; msg[11]=tag11; msg[12]=tag12; msg[13]=tag13; msg[14]=tag14; msg[15]=tag15;
    msg[16]=tag16; msg[17]=tag17;
    for (int i = 0; i < 32; ++i) msg[18+i] = sigma_le[31-i];
    uchar hash[32];
    sha256_oneshot(msg, 50, hash);
    for (int i = 0; i < 32; ++i) out_le[i] = hash[31-i];
}

inline uint mat_at_g(__global const uint* m, uint stride, uint row, uint col) {
    return m[(ulong)row * stride + col];
}
inline uint mat_at_p(const uint* m, uint stride, uint row, uint col) {
    return m[(ulong)row * stride + col];
}

inline uint oracle_el(const uchar seed[32], uint row, uint col) {
    return from_oracle(seed, row * NOISE_R + col);
}
inline uint oracle_er(const uchar seed[32], uint row, uint col) {
    return from_oracle(seed, row * MATRIX_N + col);
}
inline uint oracle_fl(const uchar seed[32], uint row, uint col) {
    return from_oracle(seed, row * NOISE_R + col);
}
inline uint oracle_fr(const uchar seed[32], uint row, uint col) {
    return from_oracle(seed, row * MATRIX_N + col);
}

inline uint compress_af_priv(__global const uint* A, const uchar seed_fl[32], const uchar seed_fr[32],
                             uint bi, uint ell, uint bj, const uint* cv, uint n, uint b, uint r) {
    uint weighted_v[128];
    for (uint x = 0; x < b; ++x) {
        for (uint u = 0; u < r; ++u) {
            uint acc = 0;
            for (uint y = 0; y < b; ++y)
                acc = m31_add(acc, m31_mul(cv[x*b+y], oracle_fr(seed_fr, bj*b+u, y)));
            weighted_v[x*r+u] = acc;
        }
    }
    uint scalar = 0;
    for (uint t = 0; t < b; ++t) {
        for (uint u = 0; u < r; ++u) {
            uint acc = 0;
            for (uint x = 0; x < b; ++x)
                acc = m31_add(acc, m31_mul(mat_at_g(A, n, bi*b+x, ell*b+t), weighted_v[x*r+u]));
            scalar = m31_add(scalar, m31_mul(oracle_fl(seed_fl, ell*b+t, u), acc));
        }
    }
    return scalar;
}

inline uint compress_eb_priv(const uchar seed_el[32], const uchar seed_er[32], __global const uint* B,
                             uint bi, uint ell, uint bj, const uint* cv, uint n, uint b, uint r) {
    uint weighted_v[64];
    for (uint u = 0; u < r; ++u) {
        for (uint y = 0; y < b; ++y) {
            uint acc = 0;
            for (uint x = 0; x < b; ++x)
                acc = m31_add(acc, m31_mul(oracle_el(seed_el, bi*b+x, u), cv[x*b+y]));
            weighted_v[u*b+y] = acc;
        }
    }
    uint scalar = 0;
    for (uint u = 0; u < r; ++u) {
        for (uint t = 0; t < b; ++t) {
            uint acc = 0;
            for (uint y = 0; y < b; ++y)
                acc = m31_add(acc, m31_mul(weighted_v[u*b+y], mat_at_g(B, n, ell*b+t, bj*b+y)));
            scalar = m31_add(scalar, m31_mul(oracle_er(seed_er, u, ell*b+t), acc));
        }
    }
    return scalar;
}

inline uint compress_ef_priv(const uchar seed_el[32], const uchar seed_er[32], const uchar seed_fl[32],
                             const uchar seed_fr[32], uint bi, uint ell, uint bj, const uint* cv, uint n, uint b, uint r) {
    uint weighted_v[64];
    for (uint u = 0; u < r; ++u) {
        for (uint y = 0; y < b; ++y) {
            uint acc = 0;
            for (uint x = 0; x < b; ++x)
                acc = m31_add(acc, m31_mul(oracle_el(seed_el, bi*b+x, u), cv[x*b+y]));
            weighted_v[u*b+y] = acc;
        }
    }
    uint weighted_fr[64];
    for (uint u = 0; u < r; ++u) {
        for (uint v = 0; v < r; ++v) {
            uint acc = 0;
            for (uint y = 0; y < b; ++y)
                acc = m31_add(acc, m31_mul(weighted_v[u*b+y], oracle_fr(seed_fr, v, bj*b+y)));
            weighted_fr[u*r+v] = acc;
        }
    }
    uint scalar = 0;
    for (uint t = 0; t < b; ++t) {
        for (uint v = 0; v < r; ++v) {
            uint acc = 0;
            for (uint u = 0; u < r; ++u)
                acc = m31_add(acc, m31_mul(oracle_er(seed_er, u, ell*b+t), weighted_fr[u*r+v]));
            scalar = m31_add(scalar, m31_mul(acc, oracle_fl(seed_fl, ell*b+t, v)));
        }
    }
    return scalar;
}

inline void sha256_ctx_init(uint st[8]) {
    st[0]=0x6a09e667; st[1]=0xbb67ae85; st[2]=0x3c6ef372; st[3]=0xa54ff53a;
    st[4]=0x510e527f; st[5]=0x9b05688c; st[6]=0x1f83d9ab; st[7]=0x5be0cd19;
}

inline void sha256_ctx_update(uint st[8], uint w[16], int* wpos, ulong* bitlen, uchar byte) {
    w[*wpos>>2] |= (uint)byte << (24u - (uint)(*wpos & 3) * 8u);
    ++(*wpos);
    ++(*bitlen);
    if (*wpos == 64) {
        sha256_compress(st, w);
        for (int i = 0; i < 16; ++i) w[i] = 0;
        *wpos = 0;
    }
}

inline void sha256_ctx_final(uint st[8], uint w[16], int wpos, ulong bitlen, uchar out[32]) {
    sha256_ctx_update(st, w, &wpos, &bitlen, 0x80);
    while (wpos != 56) sha256_ctx_update(st, w, &wpos, &bitlen, 0);
    w[15] = (uint)(bitlen * 8);
    sha256_compress(st, w);
    for (int i = 0; i < 8; ++i) {
        out[i*4+0]=(uchar)(st[i]>>24); out[i*4+1]=(uchar)(st[i]>>16);
        out[i*4+2]=(uchar)(st[i]>>8); out[i*4+3]=(uchar)st[i];
    }
}

inline int digest_leq_target(const uchar dig_le[32], const uint target[8]) {
    for (int i = 7; i >= 0; --i) {
        uint d = ((uint)dig_le[i*4]) | ((uint)dig_le[i*4+1]<<8) | ((uint)dig_le[i*4+2]<<16) | ((uint)dig_le[i*4+3]<<24);
        if (d < target[i]) return 1;
        if (d > target[i]) return 0;
    }
    return 1;
}

// Build matrix[n][n] from seed - one thread per element
__kernel void build_matrix_from_seed(__global const uchar* seed_le, __global uint* matrix, uint n) {
    uint idx = get_global_id(0);
    uint total = n * n;
    if (idx >= total) return;
    uchar seed[32];
    for (int i = 0; i < 32; ++i) seed[i] = seed_le[i];
    matrix[idx] = from_oracle(seed, idx);
}

// Build one clean block product - one thread per block (i,j,ell)
__kernel void build_clean_block(__global const uint* A, __global const uint* B,
                                __global uint* clean_out, uint n, uint b) {
    uint block_idx = get_global_id(0);
    uint bpa = n / b;
    uint total = bpa * bpa * bpa;
    if (block_idx >= total) return;
    uint ell = block_idx % bpa;
    uint j = (block_idx / bpa) % bpa;
    uint i = block_idx / (bpa * bpa);
    uint out_base = block_idx * b * b;
    for (uint x = 0; x < b; ++x) {
        for (uint y = 0; y < b; ++y) {
            uint acc = 0;
            for (uint k = 0; k < b; ++k) {
                uint a_el = A[(i*b+x)*n + (ell*b+k)];
                uint b_el = B[(ell*b+k)*n + (j*b+y)];
                acc = m31_add(acc, m31_mul(a_el, b_el));
            }
            clean_out[out_base + x*b + y] = acc;
        }
    }
}

__kernel void superhero_mine(
    __global const uint* matrix_a,
    __global const uint* matrix_b,
    __global const uint* clean_blocks,
    __global const uchar* header_template,
    __global const uint* target_limbs,
    ulong nonce_start,
    __global int* found_flag,
    __global ulong* found_nonce,
    __global uchar* found_digest)
{
    const ulong gid = get_global_id(0);
    const ulong nonce = nonce_start + gid;

    uchar hdr[HEADER_BYTES];
    for (int i = 0; i < HEADER_BYTES; ++i) hdr[i] = header_template[i];
    hdr[76]=(uchar)nonce; hdr[77]=(uchar)(nonce>>8); hdr[78]=(uchar)(nonce>>16); hdr[79]=(uchar)(nonce>>24);
    hdr[80]=(uchar)(nonce>>32); hdr[81]=(uchar)(nonce>>40); hdr[82]=(uchar)(nonce>>48); hdr[83]=(uchar)(nonce>>56);

    uchar header_hash_internal[32];
    sha256_oneshot(hdr, HEADER_BYTES, header_hash_internal);

    uchar sigma_internal[32];
    sha256_oneshot(header_hash_internal, 32, sigma_internal);

    uchar seed_el[32], seed_er[32], seed_fl[32], seed_fr[32];
    derive_noise_seed('m','a','t','m','u','l','_','n','o','i','s','e','_','E','L','_','v','1', sigma_internal, seed_el);
    derive_noise_seed('m','a','t','m','u','l','_','n','o','i','s','e','_','E','R','_','v','1', sigma_internal, seed_er);
    derive_noise_seed('m','a','t','m','u','l','_','n','o','i','s','e','_','F','L','_','v','1', sigma_internal, seed_fl);
    derive_noise_seed('m','a','t','m','u','l','_','n','o','i','s','e','_','F','R','_','v','1', sigma_internal, seed_fr);

    uchar compress_seed[32];
    derive_noise_seed('m','a','t','m','u','l','-','c','o','m','p','r','e','s','s','-','v','1', sigma_internal, compress_seed);
    uint cv[256];
    for (uint k = 0; k < 256; ++k) cv[k] = from_oracle(compress_seed, k);

    uint st[8]; uint w[16]; int wpos = 0; ulong bitlen = 0;
    sha256_ctx_init(st);
    for (int i = 0; i < 16; ++i) w[i] = 0;

    const uint n = MATRIX_N, b = BLOCK_B, r = NOISE_R, bpa = BLOCKS_PER_AXIS;
    for (uint i = 0; i < bpa; ++i) {
        for (uint j = 0; j < bpa; ++j) {
            uint compressed_prefix = 0;
            for (uint ell = 0; ell < bpa; ++ell) {
                const ulong bidx = ((ulong)i * bpa + j) * bpa + ell;
                __global const uint* block = clean_blocks + bidx * CLEAN_BLOCK_ELEMS;
                ulong dot_acc = 0;
                uint dot_pending = 0;
                const uint dot_len = b * b;
                for (uint di = 0; di < dot_len; ++di) {
                    dot_acc += (ulong)block[di] * (ulong)cv[di];
                    if (++dot_pending == REDUCE_INTERVAL) {
                        dot_acc = m31_reduce64(dot_acc);
                        dot_pending = 0;
                    }
                }
                uint clean_c = m31_reduce64(dot_acc);
                uint af = compress_af_priv(matrix_a, seed_fl, seed_fr, i, ell, j, cv, n, b, r);
                uint eb = compress_eb_priv(seed_el, seed_er, matrix_b, i, ell, j, cv, n, b, r);
                uint ef = compress_ef_priv(seed_el, seed_er, seed_fl, seed_fr, i, ell, j, cv, n, b, r);
                compressed_prefix = m31_add(compressed_prefix, m31_add(clean_c, m31_add(af, m31_add(eb, ef))));
                uchar pb[4] = {(uchar)compressed_prefix, (uchar)(compressed_prefix>>8),
                               (uchar)(compressed_prefix>>16), (uchar)(compressed_prefix>>24)};
                for (int k = 0; k < 4; ++k) sha256_ctx_update(st, w, &wpos, &bitlen, pb[k]);
            }
        }
    }

    uchar inner_be[32];
    sha256_ctx_final(st, w, wpos, bitlen, inner_be);
    uchar dig[32];
    sha256d(inner_be, 32, dig);

    int meets_target = 1;
    for (int ti = 7; ti >= 0; --ti) {
        uint d = ((uint)dig[ti*4]) | ((uint)dig[ti*4+1]<<8) | ((uint)dig[ti*4+2]<<16) | ((uint)dig[ti*4+3]<<24);
        if (d < target_limbs[ti]) break;
        if (d > target_limbs[ti]) { meets_target = 0; break; }
    }
    if (meets_target) {
        if (atomic_cmpxchg(found_flag, 0, 1) == 0) {
            *found_nonce = nonce;
            for (int i = 0; i < 32; ++i) found_digest[i] = dig[i];
        }
    }
}
