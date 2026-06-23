# SUPERMINER ROCm build — multi-arch AMD (RDNA2/3/4 + CDNA3)
HIPCC   ?= hipcc
ROCM    ?= /opt/rocm
ARCH    ?= fat
SRC     := $(abspath .)
ROOT    := $(abspath ../../..)/csrc
OUT_DIR ?= $(SRC)

# Consumer GPUs: portable gfx10 backend. MI300: rocWMMA/MFMA backend.
ifeq ($(ARCH),gfx942)
  GEMM_SRC := pearl_gemm_capi_rocm.cpp
  OFFLOAD_ARCHS := gfx942
  GFX10_FLAGS :=
else ifeq ($(ARCH),fat)
  GEMM_SRC := pearl_gemm_capi_gfx10.cpp
  OFFLOAD_ARCHS := gfx1030 gfx1100 gfx1101
  GFX10_FLAGS := -DSUPERMINER_GFX10_BUILD
else
  GEMM_SRC := pearl_gemm_capi_gfx10.cpp
  OFFLOAD_ARCHS := $(ARCH)
  GFX10_FLAGS := -DSUPERMINER_GFX10_BUILD
endif

OFFLOAD_FLAGS := $(foreach a,$(OFFLOAD_ARCHS),--offload-arch=$(a))

HIPCC_BASE := --gcc-toolchain=/usr -std=c++17 \
  -isystem /usr/include/c++/13 -isystem /usr/include/x86_64-linux-gnu/c++/13

HIPCC_FLAGS := -O3 -fPIC -shared $(OFFLOAD_FLAGS) $(HIPCC_BASE) \
  -I $(ROOT) -I .. -DSUPERMINER_BUILD=1 $(GFX10_FLAGS)

all: libpearl_gemm_capi.so libcuda.so.1

libpearl_gemm_capi.so: $(GEMM_SRC) ../pearl_kernels.cuh ../pearl_kernels_gfx10.cuh ../blake3_device.cuh
	$(HIPCC) $(HIPCC_FLAGS) $(GEMM_SRC) -o $(OUT_DIR)/$@

libcuda.so.1: cuda_hip_shim.cpp
	$(HIPCC) -O2 -fPIC -shared $(HIPCC_BASE) $< -o $(OUT_DIR)/$@

clean:
	rm -f $(OUT_DIR)/libpearl_gemm_capi.so $(OUT_DIR)/libcuda.so.1

.PHONY: all clean
