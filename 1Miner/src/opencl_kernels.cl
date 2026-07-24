// 1Miner fast OpenCL SHA256 classic miner (hasher-5.2 message layout, clean-room).
// Message (ASCII hex): previous_blockhash(78) + header_hash(64) + nonce(16) = 158 bytes.
// Host supplies midstate after the first 128 bytes; this kernel finishes the last block.
//
// Optimizations vs naive path:
//  - final block built as uint words (no uchar[64] packing)
//  - HEX_PAIRS LUT for nonce encoding
//  - AMD bitselect / bitalign when available
//  - fully unrolled SHA rounds with 16-word circular W schedule
//  - optional precomputed working state after fixed early rounds (r2 / r4)
//  - sparse polling of result->found to cut global-memory traffic

#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable

__constant uint K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

// byte -> two ASCII hex nibbles packed as 0xHILOxxxx in low 16 bits
__constant ushort HEX_PAIRS[256] = {
    0x3030u,0x3031u,0x3032u,0x3033u,0x3034u,0x3035u,0x3036u,0x3037u,0x3038u,0x3039u,0x3061u,0x3062u,0x3063u,0x3064u,0x3065u,0x3066u,
    0x3130u,0x3131u,0x3132u,0x3133u,0x3134u,0x3135u,0x3136u,0x3137u,0x3138u,0x3139u,0x3161u,0x3162u,0x3163u,0x3164u,0x3165u,0x3166u,
    0x3230u,0x3231u,0x3232u,0x3233u,0x3234u,0x3235u,0x3236u,0x3237u,0x3238u,0x3239u,0x3261u,0x3262u,0x3263u,0x3264u,0x3265u,0x3266u,
    0x3330u,0x3331u,0x3332u,0x3333u,0x3334u,0x3335u,0x3336u,0x3337u,0x3338u,0x3339u,0x3361u,0x3362u,0x3363u,0x3364u,0x3365u,0x3366u,
    0x3430u,0x3431u,0x3432u,0x3433u,0x3434u,0x3435u,0x3436u,0x3437u,0x3438u,0x3439u,0x3461u,0x3462u,0x3463u,0x3464u,0x3465u,0x3466u,
    0x3530u,0x3531u,0x3532u,0x3533u,0x3534u,0x3535u,0x3536u,0x3537u,0x3538u,0x3539u,0x3561u,0x3562u,0x3563u,0x3564u,0x3565u,0x3566u,
    0x3630u,0x3631u,0x3632u,0x3633u,0x3634u,0x3635u,0x3636u,0x3637u,0x3638u,0x3639u,0x3661u,0x3662u,0x3663u,0x3664u,0x3665u,0x3666u,
    0x3730u,0x3731u,0x3732u,0x3733u,0x3734u,0x3735u,0x3736u,0x3737u,0x3738u,0x3739u,0x3761u,0x3762u,0x3763u,0x3764u,0x3765u,0x3766u,
    0x3830u,0x3831u,0x3832u,0x3833u,0x3834u,0x3835u,0x3836u,0x3837u,0x3838u,0x3839u,0x3861u,0x3862u,0x3863u,0x3864u,0x3865u,0x3866u,
    0x3930u,0x3931u,0x3932u,0x3933u,0x3934u,0x3935u,0x3936u,0x3937u,0x3938u,0x3939u,0x3961u,0x3962u,0x3963u,0x3964u,0x3965u,0x3966u,
    0x6130u,0x6131u,0x6132u,0x6133u,0x6134u,0x6135u,0x6136u,0x6137u,0x6138u,0x6139u,0x6161u,0x6162u,0x6163u,0x6164u,0x6165u,0x6166u,
    0x6230u,0x6231u,0x6232u,0x6233u,0x6234u,0x6235u,0x6236u,0x6237u,0x6238u,0x6239u,0x6261u,0x6262u,0x6263u,0x6264u,0x6265u,0x6266u,
    0x6330u,0x6331u,0x6332u,0x6333u,0x6334u,0x6335u,0x6336u,0x6337u,0x6338u,0x6339u,0x6361u,0x6362u,0x6363u,0x6364u,0x6365u,0x6366u,
    0x6430u,0x6431u,0x6432u,0x6433u,0x6434u,0x6435u,0x6436u,0x6437u,0x6438u,0x6439u,0x6461u,0x6462u,0x6463u,0x6464u,0x6465u,0x6466u,
    0x6530u,0x6531u,0x6532u,0x6533u,0x6534u,0x6535u,0x6536u,0x6537u,0x6538u,0x6539u,0x6561u,0x6562u,0x6563u,0x6564u,0x6565u,0x6566u,
    0x6630u,0x6631u,0x6632u,0x6633u,0x6634u,0x6635u,0x6636u,0x6637u,0x6638u,0x6639u,0x6661u,0x6662u,0x6663u,0x6664u,0x6665u,0x6666u
};

#if defined(SASEUL_AMD_OPENCL)
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#define ROTR32(x,n) amd_bitalign((x), (x), ((uint)(n) & 31u))
#define Ch(x,y,z) bitselect((z), (y), (x))
#define Maj(x,y,z) bitselect((x), (y), ((x) ^ (z)))
#else
#define ROTR32(x,n) (((x) >> ((uint)(n) & 31u)) | ((x) << ((32u - ((uint)(n) & 31u)) & 31u)))
#define Ch(x,y,z) (((x)&(y))^((~(x))&(z)))
#define Maj(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#endif

#define BSIG0(x) (ROTR32((x), 2u) ^ ROTR32((x), 13u) ^ ROTR32((x), 22u))
#define BSIG1(x) (ROTR32((x), 6u) ^ ROTR32((x), 11u) ^ ROTR32((x), 25u))
#define SSIG0(x) (ROTR32((x), 7u) ^ ROTR32((x), 18u) ^ ((x) >> 3))
#define SSIG1(x) (ROTR32((x), 17u) ^ ROTR32((x), 19u) ^ ((x) >> 10))

#define RSTEP(WI, KI) do { \
  uint t1 = h + BSIG1(e) + Ch(e,f,g) + (KI) + (WI); \
  uint t2 = BSIG0(a) + Maj(a,b,c); \
  h = g; g = f; f = e; e = d + t1; \
  d = c; c = b; b = a; a = t1 + t2; \
} while (0)

typedef struct {
  uint midstate[8];
  uint block0[16];       // template: rem[0..13] + padding; nonce words overwritten
  uint target[8];
  uint work_after_r2[8]; // working regs after rounds 0..2 (fixed w0..w2)
  uint work_after_r4[8]; // working regs after rounds 0..4 when hi32 fixed (optional)
  uint flags;            // bit0: work_after_r2 valid, bit1: work_after_r4 valid
  uint _pad;
} JobBlob;

typedef struct {
  volatile uint found;
  uint _pad;
  ulong counter;
  uint hash[8];
} ResultBlob;

static inline uint hex_pair_lut(uint byte) {
  return (uint)HEX_PAIRS[byte & 255u];
}

static inline void encode_counter_words(ulong x, uint* h0, uint* h1, uint* h2, uint* h3) {
  uint hi = (uint)(x >> 32);
  uint lo = (uint)x;
  uint p0 = hex_pair_lut((hi >> 24) & 255u);
  uint p1 = hex_pair_lut((hi >> 16) & 255u);
  uint p2 = hex_pair_lut((hi >> 8) & 255u);
  uint p3 = hex_pair_lut(hi & 255u);
  uint p4 = hex_pair_lut((lo >> 24) & 255u);
  uint p5 = hex_pair_lut((lo >> 16) & 255u);
  uint p6 = hex_pair_lut((lo >> 8) & 255u);
  uint p7 = hex_pair_lut(lo & 255u);
  *h0 = (p0 << 16) | p1;
  *h1 = (p2 << 16) | p3;
  *h2 = (p4 << 16) | p5;
  *h3 = (p6 << 16) | p7;
}

static inline void encode_lo32_words(uint lo, uint* h2, uint* h3) {
  uint p4 = hex_pair_lut((lo >> 24) & 255u);
  uint p5 = hex_pair_lut((lo >> 16) & 255u);
  uint p6 = hex_pair_lut((lo >> 8) & 255u);
  uint p7 = hex_pair_lut(lo & 255u);
  *h2 = (p4 << 16) | p5;
  *h3 = (p6 << 16) | p7;
}

static inline void claim_hash(__global ResultBlob* result, ulong ctr,
                              uint d0, uint d1, uint d2, uint d3,
                              uint d4, uint d5, uint d6, uint d7) {
  if (atomic_cmpxchg(&result->found, 0u, 1u) == 0u) {
    result->counter = ctr;
    result->hash[0] = d0; result->hash[1] = d1; result->hash[2] = d2; result->hash[3] = d3;
    result->hash[4] = d4; result->hash[5] = d5; result->hash[6] = d6; result->hash[7] = d7;
  }
}

static inline int digest_le_target_claim(__global ResultBlob* result, ulong ctr, const uint t[8],
                                         uint d0, uint d1, uint d2, uint d3,
                                         uint d4, uint d5, uint d6, uint d7) {
  if (d0 < t[0]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d0 > t[0]) return 0;
  if (d1 < t[1]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d1 > t[1]) return 0;
  if (d2 < t[2]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d2 > t[2]) return 0;
  if (d3 < t[3]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d3 > t[3]) return 0;
  if (d4 < t[4]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d4 > t[4]) return 0;
  if (d5 < t[5]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d5 > t[5]) return 0;
  if (d6 < t[6]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d6 > t[6]) return 0;
  if (d7 <= t[7]) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  return 0;
}

#define SHA256_EXPAND_16_63 \
  w0  = SSIG1(w14) + w9  + SSIG0(w1)  + w0;  RSTEP(w0,  K256[16]); \
  w1  = SSIG1(w15) + w10 + SSIG0(w2)  + w1;  RSTEP(w1,  K256[17]); \
  w2  = SSIG1(w0)  + w11 + SSIG0(w3)  + w2;  RSTEP(w2,  K256[18]); \
  w3  = SSIG1(w1)  + w12 + SSIG0(w4)  + w3;  RSTEP(w3,  K256[19]); \
  w4  = SSIG1(w2)  + w13 + SSIG0(w5)  + w4;  RSTEP(w4,  K256[20]); \
  w5  = SSIG1(w3)  + w14 + SSIG0(w6)  + w5;  RSTEP(w5,  K256[21]); \
  w6  = SSIG1(w4)  + w15 + SSIG0(w7)  + w6;  RSTEP(w6,  K256[22]); \
  w7  = SSIG1(w5)  + w0  + SSIG0(w8)  + w7;  RSTEP(w7,  K256[23]); \
  w8  = SSIG1(w6)  + w1  + SSIG0(w9)  + w8;  RSTEP(w8,  K256[24]); \
  w9  = SSIG1(w7)  + w2  + SSIG0(w10) + w9;  RSTEP(w9,  K256[25]); \
  w10 = SSIG1(w8)  + w3  + SSIG0(w11) + w10; RSTEP(w10, K256[26]); \
  w11 = SSIG1(w9)  + w4  + SSIG0(w12) + w11; RSTEP(w11, K256[27]); \
  w12 = SSIG1(w10) + w5  + SSIG0(w13) + w12; RSTEP(w12, K256[28]); \
  w13 = SSIG1(w11) + w6  + SSIG0(w14) + w13; RSTEP(w13, K256[29]); \
  w14 = SSIG1(w12) + w7  + SSIG0(w15) + w14; RSTEP(w14, K256[30]); \
  w15 = SSIG1(w13) + w8  + SSIG0(w0)  + w15; RSTEP(w15, K256[31]); \
  w0  = SSIG1(w14) + w9  + SSIG0(w1)  + w0;  RSTEP(w0,  K256[32]); \
  w1  = SSIG1(w15) + w10 + SSIG0(w2)  + w1;  RSTEP(w1,  K256[33]); \
  w2  = SSIG1(w0)  + w11 + SSIG0(w3)  + w2;  RSTEP(w2,  K256[34]); \
  w3  = SSIG1(w1)  + w12 + SSIG0(w4)  + w3;  RSTEP(w3,  K256[35]); \
  w4  = SSIG1(w2)  + w13 + SSIG0(w5)  + w4;  RSTEP(w4,  K256[36]); \
  w5  = SSIG1(w3)  + w14 + SSIG0(w6)  + w5;  RSTEP(w5,  K256[37]); \
  w6  = SSIG1(w4)  + w15 + SSIG0(w7)  + w6;  RSTEP(w6,  K256[38]); \
  w7  = SSIG1(w5)  + w0  + SSIG0(w8)  + w7;  RSTEP(w7,  K256[39]); \
  w8  = SSIG1(w6)  + w1  + SSIG0(w9)  + w8;  RSTEP(w8,  K256[40]); \
  w9  = SSIG1(w7)  + w2  + SSIG0(w10) + w9;  RSTEP(w9,  K256[41]); \
  w10 = SSIG1(w8)  + w3  + SSIG0(w11) + w10; RSTEP(w10, K256[42]); \
  w11 = SSIG1(w9)  + w4  + SSIG0(w12) + w11; RSTEP(w11, K256[43]); \
  w12 = SSIG1(w10) + w5  + SSIG0(w13) + w12; RSTEP(w12, K256[44]); \
  w13 = SSIG1(w11) + w6  + SSIG0(w14) + w13; RSTEP(w13, K256[45]); \
  w14 = SSIG1(w12) + w7  + SSIG0(w15) + w14; RSTEP(w14, K256[46]); \
  w15 = SSIG1(w13) + w8  + SSIG0(w0)  + w15; RSTEP(w15, K256[47]); \
  w0  = SSIG1(w14) + w9  + SSIG0(w1)  + w0;  RSTEP(w0,  K256[48]); \
  w1  = SSIG1(w15) + w10 + SSIG0(w2)  + w1;  RSTEP(w1,  K256[49]); \
  w2  = SSIG1(w0)  + w11 + SSIG0(w3)  + w2;  RSTEP(w2,  K256[50]); \
  w3  = SSIG1(w1)  + w12 + SSIG0(w4)  + w3;  RSTEP(w3,  K256[51]); \
  w4  = SSIG1(w2)  + w13 + SSIG0(w5)  + w4;  RSTEP(w4,  K256[52]); \
  w5  = SSIG1(w3)  + w14 + SSIG0(w6)  + w5;  RSTEP(w5,  K256[53]); \
  w6  = SSIG1(w4)  + w15 + SSIG0(w7)  + w6;  RSTEP(w6,  K256[54]); \
  w7  = SSIG1(w5)  + w0  + SSIG0(w8)  + w7;  RSTEP(w7,  K256[55]); \
  w8  = SSIG1(w6)  + w1  + SSIG0(w9)  + w8;  RSTEP(w8,  K256[56]); \
  w9  = SSIG1(w7)  + w2  + SSIG0(w10) + w9;  RSTEP(w9,  K256[57]); \
  w10 = SSIG1(w8)  + w3  + SSIG0(w11) + w10; RSTEP(w10, K256[58]); \
  w11 = SSIG1(w9)  + w4  + SSIG0(w12) + w11; RSTEP(w11, K256[59]); \
  w12 = SSIG1(w10) + w5  + SSIG0(w13) + w12; RSTEP(w12, K256[60]); \
  w13 = SSIG1(w11) + w6  + SSIG0(w14) + w13; RSTEP(w13, K256[61]); \
  w14 = SSIG1(w12) + w7  + SSIG0(w15) + w14; RSTEP(w14, K256[62]); \
  w15 = SSIG1(w13) + w8  + SSIG0(w0)  + w15; RSTEP(w15, K256[63])

static inline void sha256_from_r3(
    uint w0, uint w1, uint w2, uint w3, uint w4, uint w5, uint w6, uint w7,
    uint w8, uint w9, uint w10, uint w11, uint w12, uint w13, uint w14, uint w15,
    uint* a, uint* b, uint* c, uint* d, uint* e, uint* f, uint* g, uint* h) {
  uint a0=*a,b0=*b,c0=*c,d0=*d,e0=*e,f0=*f,g0=*g,h0=*h;
  uint a=a0,b=b0,c=c0,d=d0,e=e0,f=f0,g=g0,h=h0;
  RSTEP(w3,  K256[3]);
  RSTEP(w4,  K256[4]);
  RSTEP(w5,  K256[5]);
  RSTEP(w6,  K256[6]);
  RSTEP(w7,  K256[7]);
  RSTEP(w8,  K256[8]);
  RSTEP(w9,  K256[9]);
  RSTEP(w10, K256[10]);
  RSTEP(w11, K256[11]);
  RSTEP(w12, K256[12]);
  RSTEP(w13, K256[13]);
  RSTEP(w14, K256[14]);
  RSTEP(w15, K256[15]);
  SHA256_EXPAND_16_63;
  *a=a; *b=b; *c=c; *d=d; *e=e; *f=f; *g=g; *h=h;
}

static inline void sha256_from_r5(
    uint w0, uint w1, uint w2, uint w3, uint w4, uint w5, uint w6, uint w7,
    uint w8, uint w9, uint w10, uint w11, uint w12, uint w13, uint w14, uint w15,
    uint* a, uint* b, uint* c, uint* d, uint* e, uint* f, uint* g, uint* h) {
  uint a0=*a,b0=*b,c0=*c,d0=*d,e0=*e,f0=*f,g0=*g,h0=*h;
  uint a=a0,b=b0,c=c0,d=d0,e=e0,f=f0,g=g0,h=h0;

  RSTEP(w5,  K256[5]);
  RSTEP(w6,  K256[6]);
  RSTEP(w7,  K256[7]);
  RSTEP(w8,  K256[8]);
  RSTEP(w9,  K256[9]);
  RSTEP(w10, K256[10]);
  RSTEP(w11, K256[11]);
  RSTEP(w12, K256[12]);
  RSTEP(w13, K256[13]);
  RSTEP(w14, K256[14]);
  RSTEP(w15, K256[15]);
  SHA256_EXPAND_16_63;

  *a=a; *b=b; *c=c; *d=d; *e=e; *f=f; *g=g; *h=h;
}

__attribute__((work_group_size_hint(256,1,1)))
__kernel void mine_classic_fast(__global const JobBlob* job,
                                ulong start_counter,
                                ulong count,
                                __global ResultBlob* result) {
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);

  uint mid[8], target[8], b0[16], wr2[8];
  #pragma unroll
  for (int i = 0; i < 8; ++i) {
    mid[i] = job->midstate[i];
    target[i] = job->target[i];
    wr2[i] = job->work_after_r2[i];
  }
  #pragma unroll
  for (int i = 0; i < 16; ++i) b0[i] = job->block0[i];

  uint found_poll = 0u;
  for (ulong idx = gid; idx < count; idx += stride) {
    if (((++found_poll) & 255u) == 0u && result->found) return;

    const ulong ctr = start_counter + idx;
    uint h0, h1, h2, h3;
    encode_counter_words(ctr, &h0, &h1, &h2, &h3);

    const uint w0 = b0[0];
    const uint w1 = b0[1];
    const uint w2 = b0[2];
    const uint w3 = (b0[3] & 0xFFFF0000u) | ((h0 >> 16) & 0xFFFFu);
    const uint w4 = ((h0 & 0xFFFFu) << 16) | ((h1 >> 16) & 0xFFFFu);
    const uint w5 = ((h1 & 0xFFFFu) << 16) | ((h2 >> 16) & 0xFFFFu);
    const uint w6 = ((h2 & 0xFFFFu) << 16) | ((h3 >> 16) & 0xFFFFu);
    const uint w7 = ((h3 & 0xFFFFu) << 16) | 0x00008000u;
    const uint w8 = 0u, w9 = 0u, w10 = 0u, w11 = 0u, w12 = 0u, w13 = 0u, w14 = 0u;
    const uint w15 = 0x000004f0u;

    uint a,b,c,d,e,f,g,h;
    a=wr2[0]; b=wr2[1]; c=wr2[2]; d=wr2[3];
    e=wr2[4]; f=wr2[5]; g=wr2[6]; h=wr2[7];
    sha256_from_r3(w0,w1,w2,w3,w4,w5,w6,w7,w8,w9,w10,w11,w12,w13,w14,w15,
                   &a,&b,&c,&d,&e,&f,&g,&h);

    const uint d0 = mid[0] + a;
    const uint d1 = mid[1] + b;
    const uint d2 = mid[2] + c;
    const uint d3 = mid[3] + d;
    const uint d4 = mid[4] + e;
    const uint d5 = mid[5] + f;
    const uint d6 = mid[6] + g;
    const uint d7 = mid[7] + h;
    if (digest_le_target_claim(result, ctr, target, d0,d1,d2,d3,d4,d5,d6,d7)) return;
  }
}

// Fast path when start..(start+count) stays inside one uint32 counter window:
// high 32 bits of the counter (as hex words h0,h1) are fixed → rounds 0..4 precomputed.
__attribute__((work_group_size_hint(256,1,1)))
__kernel void mine_classic_hi32(__global const JobBlob* job,
                                ulong start_counter,
                                ulong count,
                                uint h0,
                                uint h1,
                                __global ResultBlob* result) {
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);

  uint mid[8], target[8], b0[16], wr4[8];
  #pragma unroll
  for (int i = 0; i < 8; ++i) {
    mid[i] = job->midstate[i];
    target[i] = job->target[i];
    wr4[i] = job->work_after_r4[i];
  }
  #pragma unroll
  for (int i = 0; i < 16; ++i) b0[i] = job->block0[i];

  const uint w0 = b0[0];
  const uint w1 = b0[1];
  const uint w2 = b0[2];
  const uint w3 = (b0[3] & 0xFFFF0000u) | ((h0 >> 16) & 0xFFFFu);
  const uint w4 = ((h0 & 0xFFFFu) << 16) | ((h1 >> 16) & 0xFFFFu);
  const uint w8 = 0u, w9 = 0u, w10 = 0u, w11 = 0u, w12 = 0u, w13 = 0u, w14 = 0u;
  const uint w15 = 0x000004f0u;

  uint found_poll = 0u;
  for (ulong idx = gid; idx < count; idx += stride) {
    if (((++found_poll) & 255u) == 0u && result->found) return;

    const ulong ctr = start_counter + idx;
    uint h2, h3;
    encode_lo32_words((uint)ctr, &h2, &h3);

    const uint w5 = ((h1 & 0xFFFFu) << 16) | ((h2 >> 16) & 0xFFFFu);
    const uint w6 = ((h2 & 0xFFFFu) << 16) | ((h3 >> 16) & 0xFFFFu);
    const uint w7 = ((h3 & 0xFFFFu) << 16) | 0x00008000u;

    uint a=wr4[0], b=wr4[1], c=wr4[2], d=wr4[3];
    uint e=wr4[4], f=wr4[5], g=wr4[6], h=wr4[7];
    sha256_from_r5(w0,w1,w2,w3,w4,w5,w6,w7,w8,w9,w10,w11,w12,w13,w14,w15,
                   &a,&b,&c,&d,&e,&f,&g,&h);

    const uint d0 = mid[0] + a;
    const uint d1 = mid[1] + b;
    const uint d2 = mid[2] + c;
    const uint d3 = mid[3] + d;
    const uint d4 = mid[4] + e;
    const uint d5 = mid[5] + f;
    const uint d6 = mid[6] + g;
    const uint d7 = mid[7] + h;
    if (digest_le_target_claim(result, ctr, target, d0,d1,d2,d3,d4,d5,d6,d7)) return;
  }
}
