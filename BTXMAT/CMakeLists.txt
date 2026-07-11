cmake_minimum_required(VERSION 3.16)
project(BTXMAT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenCL REQUIRED)

set(BTXMAT_SOURCES
    src/crypto/sha256.cpp
    src/crypto/uint256.cpp
    src/crypto/arith_uint256.cpp
    src/util/hex.cpp
    src/util/log.cpp
    src/matmul/field.cpp
    src/matmul/matrix.cpp
    src/matmul/noise.cpp
    src/matmul/transcript.cpp
    src/matmul/solver.cpp
    src/gpu/gpu_miner.cpp
    src/stratum/client.cpp
    src/stratum/json.cpp
    src/main.cpp
)

add_executable(btxmat ${BTXMAT_SOURCES})
target_include_directories(btxmat PRIVATE src)
target_link_libraries(btxmat PRIVATE Threads::Threads OpenCL::OpenCL)
target_link_options(btxmat PRIVATE -static-libstdc++ -static-libgcc)
target_compile_definitions(btxmat PRIVATE BTXMAT_GPU_ONLY=1)
target_compile_options(btxmat PRIVATE -O3 -Wall -Wextra)

add_executable(btxmat-test tests/test_vectors.cpp
    src/crypto/sha256.cpp
    src/crypto/uint256.cpp
    src/crypto/arith_uint256.cpp
    src/util/hex.cpp
    src/matmul/field.cpp
    src/matmul/matrix.cpp
    src/matmul/noise.cpp
    src/matmul/transcript.cpp
    src/matmul/solver.cpp
)
target_include_directories(btxmat-test PRIVATE src)
target_compile_options(btxmat-test PRIVATE -O2 -Wall -Wextra)

install(TARGETS btxmat RUNTIME DESTINATION bin)
install(DIRECTORY hiveos/ DESTINATION share/btxmat/hiveos)
install(DIRECTORY opencl/ DESTINATION share/btxmat/opencl)
