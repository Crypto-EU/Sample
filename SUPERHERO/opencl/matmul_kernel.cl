// SUPERHERO OpenCL kernel for AMD GPUs (M31 field matmul block products)
// Loaded at runtime from share/superhero/opencl/matmul_kernel.cl

__constant uint MOD = 0x7FFFFFFFu;

inline uint m31_add(uint a, uint b) {
    uint s = a + b;
    return (s >= MOD) ? (s - MOD) : s;
}

inline uint m31_mul(uint a, uint b) {
    ulong p = (ulong)a * (ulong)b;
    ulong fold = (p & MOD) + (p >> 31);
    uint lo = (uint)(fold & MOD);
    uint hi = (uint)(fold >> 31);
    uint r = lo + hi;
    return (r >= MOD) ? (r - MOD) : r;
}

// Placeholder kernel: future versions will batch nonce evaluation on AMD RDNA.
__kernel void superhero_probe(__global uint* out) {
    if (get_global_id(0) == 0) out[0] = MOD;
}
