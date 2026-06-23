# SUPERMINER ROCm build — multi-arch AMD (RDNA2/3/4 + CDNA3)
HIPCC   ?= hipcc
ROCM    ?= /opt/rocm
ARCH    ?= fat
SRC     := $(abspath .)
ROOT    := $(abspath ../../..)/csrc
OUT_DIR ?= $(SRC)

# Fat binary: RX 6000/7000/9000 + MI300
ifeq ($(ARCH),fat)
  OFFLOAD_ARCHS := gfx1030 gfx1100 gfx1101 gfx1200 gfx942
  OFFLOAD_FLAGS := $(foreach a,$(OFFLOAD_ARCHS),--offload-arch=$(a))
else
  OFFLOAD_FLAGS := --offload-arch=$(ARCH)
endif

HIPCC_FLAGS := -O3 -fPIC -shared $(OFFLOAD_FLAGS) -I $(ROOT) -I .. \
  -DSUPERMINER_BUILD=1

all: libpearl_gemm_capi.so libcuda.so.1

libpearl_gemm_capi.so: pearl_gemm_capi_rocm.cpp ../pearl_kernels.cuh ../blake3_device.cuh
	$(HIPCC) $(HIPCC_FLAGS) $< -o $(OUT_DIR)/$@

libcuda.so.1: cuda_hip_shim.cpp
	$(HIPCC) -O2 -fPIC -shared $< -o $(OUT_DIR)/$@

clean:
	rm -f $(OUT_DIR)/libpearl_gemm_capi.so $(OUT_DIR)/libcuda.so.1

.PHONY: all clean
