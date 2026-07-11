cmake_minimum_required(VERSION 3.16)
project(SUPERHERO VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(OpenCL REQUIRED)

set(SUPERHERO_SOURCES
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

add_executable(superhero ${SUPERHERO_SOURCES})
target_include_directories(superhero PRIVATE src)
target_link_libraries(superhero PRIVATE Threads::Threads OpenCL::OpenCL)
target_link_options(superhero PRIVATE -static-libstdc++ -static-libgcc)
target_compile_definitions(superhero PRIVATE SUPERHERO_GPU_ONLY=1)
target_compile_options(superhero PRIVATE -O3 -Wall -Wextra)

add_executable(superhero-test tests/test_vectors.cpp
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
target_include_directories(superhero-test PRIVATE src)
target_compile_options(superhero-test PRIVATE -O2 -Wall -Wextra)

install(TARGETS superhero RUNTIME DESTINATION bin)
install(DIRECTORY hiveos/ DESTINATION share/superhero/hiveos)
install(DIRECTORY opencl/ DESTINATION share/superhero/opencl)
