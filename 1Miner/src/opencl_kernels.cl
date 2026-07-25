// 1Miner fast OpenCL SHA256 classic miner (hasher-5.2 message layout, clean-room).
// Message (ASCII hex): previous_blockhash(78) + header_hash(64) + nonce(16) = 158 bytes.
//
// Hot path: mine_classic_hi32_* — precomputed rounds 0..4, zero-pad schedule specialize,
// immediate K constants, early d0 reject. Optional amd_bitalign ROTR via ONE_MINER_BITALIGN.

#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable

#if ONE_MINER_BITALIGN
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#define ROTR32(x,n) amd_bitalign((uint)(x), (uint)(x), (uint)(n))
#else
#define ROTR32(x,n) (((uint)(x) >> ((uint)(n) & 31u)) | ((uint)(x) << ((32u - ((uint)(n) & 31u)) & 31u)))
#endif

// HEX LUT kept for fallback full-counter encode; hot path uses arithmetic nibble→ASCII.
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

// bitselect forms match hasher AMD OpenCL (better ISA than generic Maj).
#define Ch(x,y,z) bitselect((uint)(z), (uint)(y), (uint)(x))
#define Maj(x,y,z) bitselect((uint)(x), (uint)(y), (uint)((x) ^ (z)))
#define BSIG0(x) (ROTR32((x), 2u) ^ ROTR32((x), 13u) ^ ROTR32((x), 22u))
#define BSIG1(x) (ROTR32((x), 6u) ^ ROTR32((x), 11u) ^ ROTR32((x), 25u))
#define SSIG0(x) (ROTR32((x), 7u) ^ ROTR32((x), 18u) ^ (((uint)(x)) >> 3))
#define SSIG1(x) (ROTR32((x), 17u) ^ ROTR32((x), 19u) ^ (((uint)(x)) >> 10))

#ifndef ONE_MINER_FOUND_POLL
#define ONE_MINER_FOUND_POLL 256u
#endif
#if ONE_MINER_FOUND_POLL > 0
#define SHOULD_STOP(FOUND_POLL, RESULT_PTR) \
  ((((FOUND_POLL)++ & (ONE_MINER_FOUND_POLL - 1u)) == 0u) && (RESULT_PTR)->found)
#else
#define SHOULD_STOP(FOUND_POLL, RESULT_PTR) (0)
#endif

#define RSTEP(WI, KI) do { \
  uint t1 = h + BSIG1(e) + Ch(e,f,g) + (uint)(KI) + (uint)(WI); \
  uint t2 = BSIG0(a) + Maj(a,b,c); \
  h = g; g = f; f = e; e = d + t1; \
  d = c; c = b; b = a; a = t1 + t2; \
} while (0)

// Message word is zero — drop the WI add (rounds 8..14 of our padded block).
#define RSTEP0(KI) do { \
  uint t1 = h + BSIG1(e) + Ch(e,f,g) + (uint)(KI); \
  uint t2 = BSIG0(a) + Maj(a,b,c); \
  h = g; g = f; f = e; e = d + t1; \
  d = c; c = b; b = a; a = t1 + t2; \
} while (0)

// Hot-path SHA finish from round 5 with:
//  - K[i] baked as immediates (no constant-buffer indexed loads)
//  - w8..w14 start as 0, w15 = bitlen 0x4f0 → RSTEP0 + simplified early schedule
// Reference path (full RSTEP from K5); kept for parity checks.
#define SHA256_FROM_R5(W0,W1,W2,W3,W4,W5,W6,W7) do { \
  uint w0=(W0),w1=(W1),w2=(W2),w3=(W3),w4=(W4),w5=(W5),w6=(W6),w7=(W7); \
  uint w8,w9,w10,w11,w12,w13,w14,w15; \
  RSTEP(w5,  0x59f111f1u); /* K5 */ \
  RSTEP(w6,  0x923f82a4u); /* K6 */ \
  RSTEP(w7,  0xab1c5ed5u); /* K7 */ \
  RSTEP0(    0xd807aa98u); /* K8  w8=0 */ \
  RSTEP0(    0x12835b01u); /* K9 */ \
  RSTEP0(    0x243185beu); /* K10 */ \
  RSTEP0(    0x550c7dc3u); /* K11 */ \
  RSTEP0(    0x72be5d74u); /* K12 */ \
  RSTEP0(    0x80deb1feu); /* K13 */ \
  RSTEP0(    0x9bdc06a7u); /* K14 */ \
  RSTEP(0x000004f0u, 0xc19bf174u); /* K15 w15=bitlen */ \
  w0  = SSIG0(w1) + w0;                      RSTEP(w0,  0xe49b69c1u); /* K16 */ \
  w1  = SSIG1(0x000004f0u)+SSIG0(w2)+w1;     RSTEP(w1,  0xefbe4786u); /* K17 */ \
  w2  = SSIG1(w0)+SSIG0(w3)+w2;              RSTEP(w2,  0x0fc19dc6u); /* K18 */ \
  w3  = SSIG1(w1)+SSIG0(w4)+w3;              RSTEP(w3,  0x240ca1ccu); /* K19 */ \
  w4  = SSIG1(w2)+SSIG0(w5)+w4;              RSTEP(w4,  0x2de92c6fu); /* K20 */ \
  w5  = SSIG1(w3)+SSIG0(w6)+w5;              RSTEP(w5,  0x4a7484aau); /* K21 */ \
  w6  = SSIG1(w4)+0x000004f0u+SSIG0(w7)+w6;  RSTEP(w6,  0x5cb0a9dcu); /* K22 w15 still bitlen */ \
  w7  = SSIG1(w5)+w0 + w7;                   RSTEP(w7,  0x76f988dau); /* K23 */ \
  w8  = SSIG1(w6)+w1;                        RSTEP(w8,  0x983e5152u); /* K24 */ \
  w9  = SSIG1(w7)+w2;                        RSTEP(w9,  0xa831c66du); /* K25 */ \
  w10 = SSIG1(w8)+w3;                        RSTEP(w10, 0xb00327c8u); /* K26 */ \
  w11 = SSIG1(w9)+w4;                        RSTEP(w11, 0xbf597fc7u); /* K27 */ \
  w12 = SSIG1(w10)+w5;                       RSTEP(w12, 0xc6e00bf3u); /* K28 */ \
  w13 = SSIG1(w11)+w6;                       RSTEP(w13, 0xd5a79147u); /* K29 */ \
  w14 = SSIG1(w12)+w7+SSIG0(0x000004f0u);    RSTEP(w14, 0x06ca6351u); /* K30 */ \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+0x000004f0u; RSTEP(w15, 0x14292967u); /* K31 */ \
  w0  = SSIG1(w14)+w9 +SSIG0(w1)+w0;  RSTEP(w0,  0x27b70a85u); \
  w1  = SSIG1(w15)+w10+SSIG0(w2)+w1;  RSTEP(w1,  0x2e1b2138u); \
  w2  = SSIG1(w0)+w11+SSIG0(w3)+w2;   RSTEP(w2,  0x4d2c6dfcu); \
  w3  = SSIG1(w1)+w12+SSIG0(w4)+w3;   RSTEP(w3,  0x53380d13u); \
  w4  = SSIG1(w2)+w13+SSIG0(w5)+w4;   RSTEP(w4,  0x650a7354u); \
  w5  = SSIG1(w3)+w14+SSIG0(w6)+w5;   RSTEP(w5,  0x766a0abbu); \
  w6  = SSIG1(w4)+w15+SSIG0(w7)+w6;   RSTEP(w6,  0x81c2c92eu); \
  w7  = SSIG1(w5)+w0 +SSIG0(w8)+w7;   RSTEP(w7,  0x92722c85u); \
  w8  = SSIG1(w6)+w1 +SSIG0(w9)+w8;   RSTEP(w8,  0xa2bfe8a1u); \
  w9  = SSIG1(w7)+w2 +SSIG0(w10)+w9;  RSTEP(w9,  0xa81a664bu); \
  w10 = SSIG1(w8)+w3 +SSIG0(w11)+w10; RSTEP(w10, 0xc24b8b70u); \
  w11 = SSIG1(w9)+w4 +SSIG0(w12)+w11; RSTEP(w11, 0xc76c51a3u); \
  w12 = SSIG1(w10)+w5+SSIG0(w13)+w12; RSTEP(w12, 0xd192e819u); \
  w13 = SSIG1(w11)+w6+SSIG0(w14)+w13; RSTEP(w13, 0xd6990624u); \
  w14 = SSIG1(w12)+w7+SSIG0(w15)+w14; RSTEP(w14, 0xf40e3585u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+w15;  RSTEP(w15, 0x106aa070u); \
  w0  = SSIG1(w14)+w9 +SSIG0(w1)+w0;  RSTEP(w0,  0x19a4c116u); \
  w1  = SSIG1(w15)+w10+SSIG0(w2)+w1;  RSTEP(w1,  0x1e376c08u); \
  w2  = SSIG1(w0)+w11+SSIG0(w3)+w2;   RSTEP(w2,  0x2748774cu); \
  w3  = SSIG1(w1)+w12+SSIG0(w4)+w3;   RSTEP(w3,  0x34b0bcb5u); \
  w4  = SSIG1(w2)+w13+SSIG0(w5)+w4;   RSTEP(w4,  0x391c0cb3u); \
  w5  = SSIG1(w3)+w14+SSIG0(w6)+w5;   RSTEP(w5,  0x4ed8aa4au); \
  w6  = SSIG1(w4)+w15+SSIG0(w7)+w6;   RSTEP(w6,  0x5b9cca4fu); \
  w7  = SSIG1(w5)+w0 +SSIG0(w8)+w7;   RSTEP(w7,  0x682e6ff3u); \
  w8  = SSIG1(w6)+w1 +SSIG0(w9)+w8;   RSTEP(w8,  0x748f82eeu); \
  w9  = SSIG1(w7)+w2 +SSIG0(w10)+w9;  RSTEP(w9,  0x78a5636fu); \
  w10 = SSIG1(w8)+w3 +SSIG0(w11)+w10; RSTEP(w10, 0x84c87814u); \
  w11 = SSIG1(w9)+w4 +SSIG0(w12)+w11; RSTEP(w11, 0x8cc70208u); \
  w12 = SSIG1(w10)+w5+SSIG0(w13)+w12; RSTEP(w12, 0x90befffau); \
  w13 = SSIG1(w11)+w6+SSIG0(w14)+w13; RSTEP(w13, 0xa4506cebu); \
  w14 = SSIG1(w12)+w7+SSIG0(w15)+w14; RSTEP(w14, 0xbef9a3f7u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+w15;  RSTEP(w15, 0xc67178f2u); /* K63 */ \
} while (0)

// SSIG0(0x000004f0u) — bitlen constant (compile-time).
#define SSIG0_BITLEN 0xe13c0097u

// Fast path: host A5c/E5c (skip r5), pre_w0..3 (W16-19), pre_c20/pre_s21 (W20/W21 crumbs).
// wr0..wr7 = work_after_r4. After r5: b=a0,c=b0,d=c0,f=e0,g=f0,h=g0; a=A5c+w5; e=E5c+w5.
// W0..W3 unused (pre_w*); W4=fw4 only needed until K20 rewrite.
#define SHA256_FROM_R5_FAST(W0,W1,W2,W3,W4,W5,W6,W7) do { \
  uint w4=(W4),w5=(W5),w6=(W6),w7=(W7); \
  uint w0,w1,w2,w3,w8,w9,w10,w11,w12,w13,w14,w15; \
  (void)(W0); (void)(W1); (void)(W2); (void)(W3); \
  a = A5c + w5; \
  e = E5c + w5; \
  b = wr0; c = wr1; d = wr2; \
  f = wr4; g = wr5; h = wr6; \
  RSTEP(w6,  0x923f82a4u); /* K6 */ \
  RSTEP(w7,  0xab1c5ed5u); /* K7 */ \
  RSTEP0(    0xd807aa98u); /* K8  w8=0 */ \
  RSTEP0(    0x12835b01u); /* K9 */ \
  RSTEP0(    0x243185beu); /* K10 */ \
  RSTEP0(    0x550c7dc3u); /* K11 */ \
  RSTEP0(    0x72be5d74u); /* K12 */ \
  RSTEP0(    0x80deb1feu); /* K13 */ \
  RSTEP0(    0x9bdc06a7u); /* K14 */ \
  RSTEP(0x000004f0u, 0xc19bf174u); /* K15 w15=bitlen */ \
  w0  = pre_w0;                              RSTEP(w0,  0xe49b69c1u); /* K16 */ \
  w1  = pre_w1;                              RSTEP(w1,  0xefbe4786u); /* K17 */ \
  w2  = pre_w2;                              RSTEP(w2,  0x0fc19dc6u); /* K18 */ \
  w3  = pre_w3;                              RSTEP(w3,  0x240ca1ccu); /* K19 */ \
  w4  = pre_c20 + SSIG0(w5);                 RSTEP(w4,  0x2de92c6fu); /* K20 */ \
  w5  = pre_s21 + SSIG0(w6) + w5;            RSTEP(w5,  0x4a7484aau); /* K21 */ \
  w6  = SSIG1(w4)+0x000004f0u+SSIG0(w7)+w6;  RSTEP(w6,  0x5cb0a9dcu); /* K22 */ \
  w7  = SSIG1(w5)+w0 + w7;                   RSTEP(w7,  0x76f988dau); /* K23 */ \
  w8  = SSIG1(w6)+w1;                        RSTEP(w8,  0x983e5152u); /* K24 */ \
  w9  = SSIG1(w7)+w2;                        RSTEP(w9,  0xa831c66du); /* K25 */ \
  w10 = SSIG1(w8)+w3;                        RSTEP(w10, 0xb00327c8u); /* K26 */ \
  w11 = SSIG1(w9)+w4;                        RSTEP(w11, 0xbf597fc7u); /* K27 */ \
  w12 = SSIG1(w10)+w5;                       RSTEP(w12, 0xc6e00bf3u); /* K28 */ \
  w13 = SSIG1(w11)+w6;                       RSTEP(w13, 0xd5a79147u); /* K29 */ \
  w14 = SSIG1(w12)+w7+SSIG0_BITLEN;          RSTEP(w14, 0x06ca6351u); /* K30 */ \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+0x000004f0u; RSTEP(w15, 0x14292967u); /* K31 */ \
  w0  = SSIG1(w14)+w9 +SSIG0(w1)+w0;  RSTEP(w0,  0x27b70a85u); \
  w1  = SSIG1(w15)+w10+SSIG0(w2)+w1;  RSTEP(w1,  0x2e1b2138u); \
  w2  = SSIG1(w0)+w11+SSIG0(w3)+w2;   RSTEP(w2,  0x4d2c6dfcu); \
  w3  = SSIG1(w1)+w12+SSIG0(w4)+w3;   RSTEP(w3,  0x53380d13u); \
  w4  = SSIG1(w2)+w13+SSIG0(w5)+w4;   RSTEP(w4,  0x650a7354u); \
  w5  = SSIG1(w3)+w14+SSIG0(w6)+w5;   RSTEP(w5,  0x766a0abbu); \
  w6  = SSIG1(w4)+w15+SSIG0(w7)+w6;   RSTEP(w6,  0x81c2c92eu); \
  w7  = SSIG1(w5)+w0 +SSIG0(w8)+w7;   RSTEP(w7,  0x92722c85u); \
  w8  = SSIG1(w6)+w1 +SSIG0(w9)+w8;   RSTEP(w8,  0xa2bfe8a1u); \
  w9  = SSIG1(w7)+w2 +SSIG0(w10)+w9;  RSTEP(w9,  0xa81a664bu); \
  w10 = SSIG1(w8)+w3 +SSIG0(w11)+w10; RSTEP(w10, 0xc24b8b70u); \
  w11 = SSIG1(w9)+w4 +SSIG0(w12)+w11; RSTEP(w11, 0xc76c51a3u); \
  w12 = SSIG1(w10)+w5+SSIG0(w13)+w12; RSTEP(w12, 0xd192e819u); \
  w13 = SSIG1(w11)+w6+SSIG0(w14)+w13; RSTEP(w13, 0xd6990624u); \
  w14 = SSIG1(w12)+w7+SSIG0(w15)+w14; RSTEP(w14, 0xf40e3585u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+w15;  RSTEP(w15, 0x106aa070u); \
  w0  = SSIG1(w14)+w9 +SSIG0(w1)+w0;  RSTEP(w0,  0x19a4c116u); \
  w1  = SSIG1(w15)+w10+SSIG0(w2)+w1;  RSTEP(w1,  0x1e376c08u); \
  w2  = SSIG1(w0)+w11+SSIG0(w3)+w2;   RSTEP(w2,  0x2748774cu); \
  w3  = SSIG1(w1)+w12+SSIG0(w4)+w3;   RSTEP(w3,  0x34b0bcb5u); \
  w4  = SSIG1(w2)+w13+SSIG0(w5)+w4;   RSTEP(w4,  0x391c0cb3u); \
  w5  = SSIG1(w3)+w14+SSIG0(w6)+w5;   RSTEP(w5,  0x4ed8aa4au); \
  w6  = SSIG1(w4)+w15+SSIG0(w7)+w6;   RSTEP(w6,  0x5b9cca4fu); \
  w7  = SSIG1(w5)+w0 +SSIG0(w8)+w7;   RSTEP(w7,  0x682e6ff3u); \
  w8  = SSIG1(w6)+w1 +SSIG0(w9)+w8;   RSTEP(w8,  0x748f82eeu); \
  w9  = SSIG1(w7)+w2 +SSIG0(w10)+w9;  RSTEP(w9,  0x78a5636fu); \
  w10 = SSIG1(w8)+w3 +SSIG0(w11)+w10; RSTEP(w10, 0x84c87814u); \
  w11 = SSIG1(w9)+w4 +SSIG0(w12)+w11; RSTEP(w11, 0x8cc70208u); \
  w12 = SSIG1(w10)+w5+SSIG0(w13)+w12; RSTEP(w12, 0x90befffau); \
  w13 = SSIG1(w11)+w6+SSIG0(w14)+w13; RSTEP(w13, 0xa4506cebu); \
  w14 = SSIG1(w12)+w7+SSIG0(w15)+w14; RSTEP(w14, 0xbef9a3f7u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+w15;  RSTEP(w15, 0xc67178f2u); /* K63 */ \
} while (0)

// Fallback when batch crosses a uint32 window (rounds from r3).
#define SHA256_FROM_R3(W0,W1,W2,W3,W4,W5,W6,W7) do { \
  uint w0=(W0),w1=(W1),w2=(W2),w3=(W3),w4=(W4),w5=(W5),w6=(W6),w7=(W7); \
  uint w8,w9,w10,w11,w12,w13,w14,w15; \
  RSTEP(w3,  0xe9b5dba5u); /* K3 */ \
  RSTEP(w4,  0x3956c25bu); /* K4 */ \
  RSTEP(w5,  0x59f111f1u); /* K5 */ \
  RSTEP(w6,  0x923f82a4u); /* K6 */ \
  RSTEP(w7,  0xab1c5ed5u); /* K7 */ \
  RSTEP0(    0xd807aa98u); /* K8 */ \
  RSTEP0(    0x12835b01u); /* K9 */ \
  RSTEP0(    0x243185beu); /* K10 */ \
  RSTEP0(    0x550c7dc3u); /* K11 */ \
  RSTEP0(    0x72be5d74u); /* K12 */ \
  RSTEP0(    0x80deb1feu); /* K13 */ \
  RSTEP0(    0x9bdc06a7u); /* K14 */ \
  RSTEP(0x000004f0u, 0xc19bf174u); /* K15 */ \
  w0  = SSIG0(w1) + w0;                      RSTEP(w0,  0xe49b69c1u); \
  w1  = SSIG1(0x000004f0u)+SSIG0(w2)+w1;     RSTEP(w1,  0xefbe4786u); \
  w2  = SSIG1(w0)+SSIG0(w3)+w2;              RSTEP(w2,  0x0fc19dc6u); \
  w3  = SSIG1(w1)+SSIG0(w4)+w3;              RSTEP(w3,  0x240ca1ccu); \
  w4  = SSIG1(w2)+SSIG0(w5)+w4;              RSTEP(w4,  0x2de92c6fu); \
  w5  = SSIG1(w3)+SSIG0(w6)+w5;              RSTEP(w5,  0x4a7484aau); \
  w6  = SSIG1(w4)+0x000004f0u+SSIG0(w7)+w6;  RSTEP(w6,  0x5cb0a9dcu); \
  w7  = SSIG1(w5)+w0 + w7;                   RSTEP(w7,  0x76f988dau); \
  w8  = SSIG1(w6)+w1;                        RSTEP(w8,  0x983e5152u); \
  w9  = SSIG1(w7)+w2;                        RSTEP(w9,  0xa831c66du); \
  w10 = SSIG1(w8)+w3;                        RSTEP(w10, 0xb00327c8u); \
  w11 = SSIG1(w9)+w4;                        RSTEP(w11, 0xbf597fc7u); \
  w12 = SSIG1(w10)+w5;                       RSTEP(w12, 0xc6e00bf3u); \
  w13 = SSIG1(w11)+w6;                       RSTEP(w13, 0xd5a79147u); \
  w14 = SSIG1(w12)+w7+SSIG0(0x000004f0u);    RSTEP(w14, 0x06ca6351u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+0x000004f0u; RSTEP(w15, 0x14292967u); \
  w0  = SSIG1(w14)+w9 +SSIG0(w1)+w0;  RSTEP(w0,  0x27b70a85u); \
  w1  = SSIG1(w15)+w10+SSIG0(w2)+w1;  RSTEP(w1,  0x2e1b2138u); \
  w2  = SSIG1(w0)+w11+SSIG0(w3)+w2;   RSTEP(w2,  0x4d2c6dfcu); \
  w3  = SSIG1(w1)+w12+SSIG0(w4)+w3;   RSTEP(w3,  0x53380d13u); \
  w4  = SSIG1(w2)+w13+SSIG0(w5)+w4;   RSTEP(w4,  0x650a7354u); \
  w5  = SSIG1(w3)+w14+SSIG0(w6)+w5;   RSTEP(w5,  0x766a0abbu); \
  w6  = SSIG1(w4)+w15+SSIG0(w7)+w6;   RSTEP(w6,  0x81c2c92eu); \
  w7  = SSIG1(w5)+w0 +SSIG0(w8)+w7;   RSTEP(w7,  0x92722c85u); \
  w8  = SSIG1(w6)+w1 +SSIG0(w9)+w8;   RSTEP(w8,  0xa2bfe8a1u); \
  w9  = SSIG1(w7)+w2 +SSIG0(w10)+w9;  RSTEP(w9,  0xa81a664bu); \
  w10 = SSIG1(w8)+w3 +SSIG0(w11)+w10; RSTEP(w10, 0xc24b8b70u); \
  w11 = SSIG1(w9)+w4 +SSIG0(w12)+w11; RSTEP(w11, 0xc76c51a3u); \
  w12 = SSIG1(w10)+w5+SSIG0(w13)+w12; RSTEP(w12, 0xd192e819u); \
  w13 = SSIG1(w11)+w6+SSIG0(w14)+w13; RSTEP(w13, 0xd6990624u); \
  w14 = SSIG1(w12)+w7+SSIG0(w15)+w14; RSTEP(w14, 0xf40e3585u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+w15;  RSTEP(w15, 0x106aa070u); \
  w0  = SSIG1(w14)+w9 +SSIG0(w1)+w0;  RSTEP(w0,  0x19a4c116u); \
  w1  = SSIG1(w15)+w10+SSIG0(w2)+w1;  RSTEP(w1,  0x1e376c08u); \
  w2  = SSIG1(w0)+w11+SSIG0(w3)+w2;   RSTEP(w2,  0x2748774cu); \
  w3  = SSIG1(w1)+w12+SSIG0(w4)+w3;   RSTEP(w3,  0x34b0bcb5u); \
  w4  = SSIG1(w2)+w13+SSIG0(w5)+w4;   RSTEP(w4,  0x391c0cb3u); \
  w5  = SSIG1(w3)+w14+SSIG0(w6)+w5;   RSTEP(w5,  0x4ed8aa4au); \
  w6  = SSIG1(w4)+w15+SSIG0(w7)+w6;   RSTEP(w6,  0x5b9cca4fu); \
  w7  = SSIG1(w5)+w0 +SSIG0(w8)+w7;   RSTEP(w7,  0x682e6ff3u); \
  w8  = SSIG1(w6)+w1 +SSIG0(w9)+w8;   RSTEP(w8,  0x748f82eeu); \
  w9  = SSIG1(w7)+w2 +SSIG0(w10)+w9;  RSTEP(w9,  0x78a5636fu); \
  w10 = SSIG1(w8)+w3 +SSIG0(w11)+w10; RSTEP(w10, 0x84c87814u); \
  w11 = SSIG1(w9)+w4 +SSIG0(w12)+w11; RSTEP(w11, 0x8cc70208u); \
  w12 = SSIG1(w10)+w5+SSIG0(w13)+w12; RSTEP(w12, 0x90befffau); \
  w13 = SSIG1(w11)+w6+SSIG0(w14)+w13; RSTEP(w13, 0xa4506cebu); \
  w14 = SSIG1(w12)+w7+SSIG0(w15)+w14; RSTEP(w14, 0xbef9a3f7u); \
  w15 = SSIG1(w13)+w8+SSIG0(w0)+w15;  RSTEP(w15, 0xc67178f2u); \
} while (0)

typedef struct {
  uint midstate[8];
  uint block0[16];
  uint target[8];
  uint work_after_r2[8];
  uint work_after_r4[8];
  uint flags;
  uint _pad;
} JobBlob;

typedef struct {
  volatile uint found;
  uint _pad;
  ulong counter;
  uint hash[8];
} ResultBlob;

static inline uint hex_nibble(uint n) {
  n &= 15u;
  // branchless: '0'+n, or 'a'+(n-10) when n>=10  (delta +39)
  return n + 0x30u + (uint)(n >= 10u) * 39u;
}

static inline uint hex_pair_arith(uint byte) {
  byte &= 255u;
  return (hex_nibble(byte >> 4) << 8) | hex_nibble(byte & 15u);
}

static inline uint hex_pair_lut(uint byte) {
  return (uint)HEX_PAIRS[byte & 255u];
}

static inline void encode_counter_words(ulong x, uint* out_h0, uint* out_h1, uint* out_h2, uint* out_h3) {
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
  *out_h0 = (p0 << 16) | p1;
  *out_h1 = (p2 << 16) | p3;
  *out_h2 = (p4 << 16) | p5;
  *out_h3 = (p6 << 16) | p7;
}

static inline void encode_lo32_words(uint lo, uint* out_h2, uint* out_h3) {
  uint p4 = hex_pair_lut((lo >> 24) & 255u);
  uint p5 = hex_pair_lut((lo >> 16) & 255u);
  uint p6 = hex_pair_lut((lo >> 8) & 255u);
  uint p7 = hex_pair_lut(lo & 255u);
  *out_h2 = (p4 << 16) | p5;
  *out_h3 = (p6 << 16) | p7;
}

static inline void encode_lo32_words_arith(uint lo, uint* out_h2, uint* out_h3) {
  uint p4 = hex_pair_arith((lo >> 24) & 255u);
  uint p5 = hex_pair_arith((lo >> 16) & 255u);
  uint p6 = hex_pair_arith((lo >> 8) & 255u);
  uint p7 = hex_pair_arith(lo & 255u);
  *out_h2 = (p4 << 16) | p5;
  *out_h3 = (p6 << 16) | p7;
}

// Patch low ASCII hex byte when only lo's low 8 bits differ from base (no byte carry).
static inline void encode_lo32_from_base(uint lo0, uint lo, uint hx2b, uint hx3b,
                                         uint* out_h2, uint* out_h3) {
  if (((lo ^ lo0) & ~0xffu) == 0u) {
    *out_h2 = hx2b;
    *out_h3 = (hx3b & 0xFFFF0000u) | hex_pair_lut(lo & 255u);
  } else {
    encode_lo32_words(lo, out_h2, out_h3);
  }
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

static inline int digest_le_target_claim(__global ResultBlob* result, ulong ctr,
                                         uint t0, uint t1, uint t2, uint t3,
                                         uint t4, uint t5, uint t6, uint t7,
                                         uint d0, uint d1, uint d2, uint d3,
                                         uint d4, uint d5, uint d6, uint d7) {
  if (d0 > t0) return 0;
  if (d0 < t0) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d1 > t1) return 0;
  if (d1 < t1) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d2 > t2) return 0;
  if (d2 < t2) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d3 > t3) return 0;
  if (d3 < t3) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d4 > t4) return 0;
  if (d4 < t4) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d5 > t5) return 0;
  if (d5 < t5) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d6 > t6) return 0;
  if (d6 < t6) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  if (d7 <= t7) { claim_hash(result, ctr, d0,d1,d2,d3,d4,d5,d6,d7); return 1; }
  return 0;
}

// Early-out on first limb (almost all rejects); only then materialize full digest.
#define TRY_HASH_R5(CTR, W5, W6, W7) do { \
  uint a, b, c, d, e, f, g, h; \
  SHA256_FROM_R5_FAST(fw0, fw1, fw2, fw3, fw4, (W5), (W6), (W7)); \
  const uint d0 = mid0 + a; \
  if (d0 <= t0) { \
    const uint d1 = mid1 + b, d2 = mid2 + c, d3 = mid3 + d; \
    const uint d4 = mid4 + e, d5 = mid5 + f, d6 = mid6 + g, d7 = mid7 + h; \
    if (digest_le_target_claim(result, (CTR), t0,t1,t2,t3,t4,t5,t6,t7, d0,d1,d2,d3,d4,d5,d6,d7)) return; \
  } \
} while (0)

// Like TRY_HASH_R5 but never returns (for dual-nonce ILP — less WI divergence).
#define TRY_HASH_R5_CLAIM(CTR, W5, W6, W7) do { \
  uint a, b, c, d, e, f, g, h; \
  SHA256_FROM_R5_FAST(fw0, fw1, fw2, fw3, fw4, (W5), (W6), (W7)); \
  const uint d0 = mid0 + a; \
  if (d0 <= t0) { \
    const uint d1 = mid1 + b, d2 = mid2 + c, d3 = mid3 + d; \
    const uint d4 = mid4 + e, d5 = mid5 + f, d6 = mid6 + g, d7 = mid7 + h; \
    (void)digest_le_target_claim(result, (CTR), t0,t1,t2,t3,t4,t5,t6,t7, d0,d1,d2,d3,d4,d5,d6,d7); \
  } \
} while (0)

// Hot path: all job state as scalar args (SGPR/private — no JobBlob global loads).
#define HI32_SCALAR_ARGS \
    uint mid0, uint mid1, uint mid2, uint mid3, uint mid4, uint mid5, uint mid6, uint mid7, \
    uint wr0, uint wr1, uint wr2, uint wr3, uint wr4, uint wr5, uint wr6, uint wr7, \
    uint t0, uint t1, uint t2, uint t3, uint t4, uint t5, uint t6, uint t7, \
    uint fw0, uint fw1, uint fw2, uint fw3, uint fw4, uint h1_lo, \
    uint A5c, uint E5c, \
    uint pre_w0, uint pre_w1, uint pre_w2, uint pre_w3, \
    uint pre_c20, uint pre_s21, \
    ulong start_counter, ulong count, \
    __global ResultBlob* result

__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_hi32_u1(HI32_SCALAR_ARGS) {
  (void)wr3; (void)wr7;
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);
  uint found_poll = 0u;
  for (ulong idx = gid; idx < count; idx += stride) {
    if (SHOULD_STOP(found_poll, result)) return;
    const ulong ctr = start_counter + idx;
    uint hx2, hx3;
    encode_lo32_words((uint)ctr, &hx2, &hx3);
    TRY_HASH_R5(ctr,
                (h1_lo << 16) | ((hx2 >> 16) & 0xFFFFu),
                ((hx2 & 0xFFFFu) << 16) | ((hx3 >> 16) & 0xFFFFu),
                ((hx3 & 0xFFFFu) << 16) | 0x00008000u);
  }
}

// Host guarantees count % 4 == 0.
__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_hi32(HI32_SCALAR_ARGS) {
  (void)wr3; (void)wr7;
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);
  for (ulong idx = gid * 4ul; idx < count; idx += stride * 4ul) {
    const uint lo0 = (uint)(start_counter + idx);
    uint hx2b, hx3b;
    encode_lo32_words(lo0, &hx2b, &hx3b);
#pragma unroll
    for (uint lane = 0u; lane < 4u; ++lane) {
      const ulong ctr = start_counter + idx + (ulong)lane;
      const uint lo = lo0 + lane;
      uint hx2, hx3;
      encode_lo32_from_base(lo0, lo, hx2b, hx3b, &hx2, &hx3);
      TRY_HASH_R5(ctr,
                  (h1_lo << 16) | ((hx2 >> 16) & 0xFFFFu),
                  ((hx2 & 0xFFFFu) << 16) | ((hx3 >> 16) & 0xFFFFu),
                  ((hx3 & 0xFFFFu) << 16) | 0x00008000u);
    }
  }
}

// Host guarantees count % 8 == 0.
__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_hi32_u8(HI32_SCALAR_ARGS) {
  (void)wr3; (void)wr7;
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);
  for (ulong idx = gid * 8ul; idx < count; idx += stride * 8ul) {
    const uint lo0 = (uint)(start_counter + idx);
    uint hx2b, hx3b;
    encode_lo32_words(lo0, &hx2b, &hx3b);
#pragma unroll
    for (uint lane = 0u; lane < 8u; ++lane) {
      const ulong ctr = start_counter + idx + (ulong)lane;
      const uint lo = lo0 + lane;
      uint hx2, hx3;
      encode_lo32_from_base(lo0, lo, hx2b, hx3b, &hx2, &hx3);
      TRY_HASH_R5(ctr,
                  (h1_lo << 16) | ((hx2 >> 16) & 0xFFFFu),
                  ((hx2 & 0xFFFFu) << 16) | ((hx3 >> 16) & 0xFFFFu),
                  ((hx3 & 0xFFFFu) << 16) | 0x00008000u);
    }
  }
}

// Dual-nonce ILP: each WI hashes gid*2 and gid*2+1 without early return between them
// (claim-only) so both digests always run — less divergence, better latency hiding.
// Host guarantees count % 2 == 0. Autotune unroll==2 selects this kernel.
__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_hi32_ilp2(HI32_SCALAR_ARGS) {
  (void)wr3; (void)wr7;
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);
  for (ulong idx = gid * 2ul; idx < count; idx += stride * 2ul) {
    const ulong ctr0 = start_counter + idx;
    const ulong ctr1 = ctr0 + 1ul;
    const uint lo0 = (uint)ctr0;
    uint hx2b, hx3b, hx2a, hx3a, hx2c, hx3c;
    encode_lo32_words(lo0, &hx2b, &hx3b);
    hx2a = hx2b;
    hx3a = hx3b;
    encode_lo32_from_base(lo0, lo0 + 1u, hx2b, hx3b, &hx2c, &hx3c);
    const uint w5a = (h1_lo << 16) | ((hx2a >> 16) & 0xFFFFu);
    const uint w6a = ((hx2a & 0xFFFFu) << 16) | ((hx3a >> 16) & 0xFFFFu);
    const uint w7a = ((hx3a & 0xFFFFu) << 16) | 0x00008000u;
    const uint w5b = (h1_lo << 16) | ((hx2c >> 16) & 0xFFFFu);
    const uint w6b = ((hx2c & 0xFFFFu) << 16) | ((hx3c >> 16) & 0xFFFFu);
    const uint w7b = ((hx3c & 0xFFFFu) << 16) | 0x00008000u;
    TRY_HASH_R5_CLAIM(ctr0, w5a, w6a, w7a);
    TRY_HASH_R5_CLAIM(ctr1, w5b, w6b, w7b);
  }
}

// Quad-nonce ILP (claim-only, no early return). Host guarantees count % 4 == 0.
// Autotune unroll==4 selects this; unroll==14 keeps sequential mine_classic_hi32.
__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_hi32_ilp4(HI32_SCALAR_ARGS) {
  (void)wr3; (void)wr7;
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);
  for (ulong idx = gid * 4ul; idx < count; idx += stride * 4ul) {
    const ulong ctr0 = start_counter + idx;
    const uint lo0 = (uint)ctr0;
    uint hx2b, hx3b;
    encode_lo32_words(lo0, &hx2b, &hx3b);
#pragma unroll
    for (uint lane = 0u; lane < 4u; ++lane) {
      const ulong ctr = ctr0 + (ulong)lane;
      const uint lo = lo0 + lane;
      uint hx2, hx3;
      encode_lo32_from_base(lo0, lo, hx2b, hx3b, &hx2, &hx3);
      TRY_HASH_R5_CLAIM(ctr,
                        (h1_lo << 16) | ((hx2 >> 16) & 0xFFFFu),
                        ((hx2 & 0xFFFFu) << 16) | ((hx3 >> 16) & 0xFFFFu),
                        ((hx3 & 0xFFFFu) << 16) | 0x00008000u);
    }
  }
}

// Closest to hasher saseul_ocl_tail14_hi32_r5_lut_u32 loop shape, but with scalar args
// (not __constant buffer — AMD constant-cache stale reads caused false shares).
// Autotune unroll==3 selects this.
__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_hi32_c_u32(HI32_SCALAR_ARGS) {
  (void)wr3; (void)wr7;
  const uint gid = (uint)get_global_id(0);
  const uint stride = (uint)get_global_size(0);
  const uint limit = (uint)count;
  const uint start_lo = (uint)start_counter;
  uint found_poll = 0u;
  for (uint idx = gid; idx < limit; idx += stride) {
    if (SHOULD_STOP(found_poll, result)) return;
    const ulong ctr = start_counter + (ulong)idx;
    uint hx2, hx3;
    encode_lo32_words(start_lo + idx, &hx2, &hx3);
    TRY_HASH_R5(ctr,
                (h1_lo << 16) | ((hx2 >> 16) & 0xFFFFu),
                ((hx2 & 0xFFFFu) << 16) | ((hx3 >> 16) & 0xFFFFu),
                ((hx3 & 0xFFFFu) << 16) | 0x00008000u);
  }
}

// hasher-style packed constants (kept for optional experiments; not used by c_u32 anymore).
typedef struct {
  uint mid[8];
  uint wr[8];
  uint tgt[8];
  uint fw0, fw1, fw2, fw3, fw4, h1_lo;
  uint A5c, E5c;
  uint pre_w0, pre_w1, pre_w2, pre_w3;
  uint pre_c20, pre_s21;
  uint _pad0, _pad1;
} Hi32Blob;

__attribute__((work_group_size_hint(256, 1, 1)))
__kernel void mine_classic_fast(__global const JobBlob* job,
                                ulong start_counter,
                                ulong count,
                                __global ResultBlob* result) {
  const ulong gid = (ulong)get_global_id(0);
  const ulong stride = (ulong)get_global_size(0);

  const uint mid0 = job->midstate[0], mid1 = job->midstate[1], mid2 = job->midstate[2], mid3 = job->midstate[3];
  const uint mid4 = job->midstate[4], mid5 = job->midstate[5], mid6 = job->midstate[6], mid7 = job->midstate[7];
  const uint t0 = job->target[0], t1 = job->target[1], t2 = job->target[2], t3 = job->target[3];
  const uint t4 = job->target[4], t5 = job->target[5], t6 = job->target[6], t7 = job->target[7];
  const uint wr0 = job->work_after_r2[0], wr1 = job->work_after_r2[1], wr2 = job->work_after_r2[2], wr3 = job->work_after_r2[3];
  const uint wr4 = job->work_after_r2[4], wr5 = job->work_after_r2[5], wr6 = job->work_after_r2[6], wr7 = job->work_after_r2[7];
  const uint b0 = job->block0[0], b1 = job->block0[1], b2 = job->block0[2], b3 = job->block0[3];

  for (ulong idx = gid; idx < count; idx += stride) {
    const ulong ctr = start_counter + idx;
    uint nh0, nh1, nh2, nh3;
    encode_counter_words(ctr, &nh0, &nh1, &nh2, &nh3);
    const uint w3 = (b3 & 0xFFFF0000u) | ((nh0 >> 16) & 0xFFFFu);
    const uint w4 = ((nh0 & 0xFFFFu) << 16) | ((nh1 >> 16) & 0xFFFFu);
    const uint w5 = ((nh1 & 0xFFFFu) << 16) | ((nh2 >> 16) & 0xFFFFu);
    const uint w6 = ((nh2 & 0xFFFFu) << 16) | ((nh3 >> 16) & 0xFFFFu);
    const uint w7 = ((nh3 & 0xFFFFu) << 16) | 0x00008000u;

    uint a = wr0, b = wr1, c = wr2, d = wr3, e = wr4, f = wr5, g = wr6, h = wr7;
    SHA256_FROM_R3(b0, b1, b2, w3, w4, w5, w6, w7);
    const uint d0 = mid0 + a;
    if (d0 <= t0) {
      const uint d1 = mid1 + b, d2 = mid2 + c, d3 = mid3 + d;
      const uint d4 = mid4 + e, d5 = mid5 + f, d6 = mid6 + g, d7 = mid7 + h;
      if (digest_le_target_claim(result, ctr, t0,t1,t2,t3,t4,t5,t6,t7, d0,d1,d2,d3,d4,d5,d6,d7)) return;
    }
  }
}
