// Clean-room OpenCL SHA256 classic midstate miner for 1Miner.
// Message layout (ASCII hex): previous_blockhash(78) + digest(64) + nonce(16) = 158 bytes.
// Threads search a uint64 counter space encoded as 16 lowercase hex chars.
//
// Layout note: ulong fields must be 8-byte aligned. Keep an explicit pad after target[]
// so host (C++) and device (OpenCL) JobBlob layouts match.

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

#define ROTR(x,n) (((x)>>(n))|((x)<<(32u-(n))))
#define Ch(x,y,z) (((x)&(y))^((~(x))&(z)))
#define Maj(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define BSIG0(x) (ROTR((x),2u)^ROTR((x),13u)^ROTR((x),22u))
#define BSIG1(x) (ROTR((x),6u)^ROTR((x),11u)^ROTR((x),25u))
#define SSIG0(x) (ROTR((x),7u)^ROTR((x),18u)^((x)>>3))
#define SSIG1(x) (ROTR((x),17u)^ROTR((x),19u)^((x)>>10))

typedef struct {
  uint midstate[8];
  uint prefix_tail[16]; // remaining prefix bytes packed into BE words
  uint prefix_tail_len; // 14
  uint target[8];       // big-endian words
  uint _pad_align8;     // pad so ulongs are 8-byte aligned (matches host)
  ulong start_counter;
  ulong count;
} JobBlob;

typedef struct {
  volatile uint found;
  uint _pad_align8;
  ulong counter;
  uint hash[8];
} ResultBlob;

static inline void sha256_compress(uint state[8], const uint w0in[16]) {
  uint w[64];
  #pragma unroll
  for (int i=0;i<16;++i) w[i]=w0in[i];
  for (int i=16;i<64;++i) w[i]=SSIG1(w[i-2])+w[i-7]+SSIG0(w[i-15])+w[i-16];
  uint a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
  for (int i=0;i<64;++i) {
    uint t1=h+BSIG1(e)+Ch(e,f,g)+K256[i]+w[i];
    uint t2=BSIG0(a)+Maj(a,b,c);
    h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
  }
  state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}

static inline uint hex_nibble(uint n) {
  return n + 0x30u + (((n + 6u) >> 4) * 0x27u);
}

static inline void counter_to_hex_words(ulong ctr, uint* o0, uint* o1, uint* o2, uint* o3) {
  // 16 ASCII hex chars as 4 big-endian uint words (4 chars each)
  uint bytes[8];
  #pragma unroll
  for (int i=0;i<8;++i) {
    uint shift = (uint)((7-i)*8);
    uint b = (uint)((ctr >> shift) & 255u);
    bytes[i] = (hex_nibble((b>>4)&15u)<<8) | hex_nibble(b&15u);
  }
  *o0 = (bytes[0]<<16) | bytes[1];
  *o1 = (bytes[2]<<16) | bytes[3];
  *o2 = (bytes[4]<<16) | bytes[5];
  *o3 = (bytes[6]<<16) | bytes[7];
}

// AMD OpenCL is strict about address spaces: do not pass __constant/__global pointers into
// parameters that default to __private. Callers must copy into private arrays first.
static inline int hash_le_target(const uint h[8], const uint t[8]) {
  #pragma unroll
  for (int i=0;i<8;++i) {
    if (h[i] < t[i]) return 1;
    if (h[i] > t[i]) return 0;
  }
  return 1;
}

__kernel void mine_classic(__global const JobBlob* job, __global ResultBlob* result) {
  ulong gid = (ulong)get_global_id(0);
  ulong stride = (ulong)get_global_size(0);

  // Snapshot job fields into private memory once per work-item.
  uint midstate[8];
  uint target[8];
  uint prefix_tail[16];
  ulong start_counter = job->start_counter;
  ulong count = job->count;
  #pragma unroll
  for (int k=0;k<8;++k) {
    midstate[k] = job->midstate[k];
    target[k] = job->target[k];
  }
  #pragma unroll
  for (int k=0;k<16;++k) prefix_tail[k] = job->prefix_tail[k];

  for (ulong i = gid; i < count; i += stride) {
    if (result->found) return;
    ulong ctr = start_counter + i;

    uint state[8];
    #pragma unroll
    for (int k=0;k<8;++k) state[k]=midstate[k];

    // Build final block: remaining 14 prefix bytes + 16 nonce bytes = 30, then padding.
    uint w[16];
    #pragma unroll
    for (int k=0;k<16;++k) w[k]=0;

    uchar tail[64];
    #pragma unroll
    for (int k=0;k<64;++k) tail[k]=0;
    #pragma unroll
    for (int k=0;k<14;++k) {
      uint word = prefix_tail[k>>2];
      uint sh = 24u - ((uint)(k & 3) * 8u);
      tail[k] = (uchar)((word >> sh) & 255u);
    }
    uint n0,n1,n2,n3;
    counter_to_hex_words(ctr, &n0,&n1,&n2,&n3);
    uchar nonce_ascii[16];
    #pragma unroll
    for (int b=0;b<4;++b) {
      uint word = (b==0?n0:b==1?n1:b==2?n2:n3);
      nonce_ascii[b*4+0]=(uchar)((word>>24)&255u);
      nonce_ascii[b*4+1]=(uchar)((word>>16)&255u);
      nonce_ascii[b*4+2]=(uchar)((word>>8)&255u);
      nonce_ascii[b*4+3]=(uchar)(word&255u);
    }
    #pragma unroll
    for (int k=0;k<16;++k) tail[14+k]=nonce_ascii[k];

    // SHA256 padding for total message length 158 bytes.
    // Already consumed 2*64=128 bytes as midstate; remaining payload 30 bytes.
    tail[30]=0x80;
    // length in bits = 158*8 = 1264 = 0x04f0
    tail[62]=0x04;
    tail[63]=0xf0;
    #pragma unroll
    for (int k=0;k<16;++k) {
      w[k] = ((uint)tail[k*4]<<24)|((uint)tail[k*4+1]<<16)|((uint)tail[k*4+2]<<8)|((uint)tail[k*4+3]);
    }
    sha256_compress(state, w);

    if (hash_le_target(state, target)) {
      if (atomic_cmpxchg(&result->found, 0u, 1u) == 0u) {
        result->counter = ctr;
        #pragma unroll
        for (int k=0;k<8;++k) result->hash[k]=state[k];
      }
      return;
    }
  }
}
