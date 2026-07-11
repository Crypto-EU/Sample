cmake_minimum_required(VERSION 3.16)
project(SUPERHERO VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SUPERHERO_ENABLE_OPENCL "Build OpenCL AMD backend" ON)

find_package(Threads REQUIRED)

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
    src/stratum/client.cpp
    src/stratum/json.cpp
    src/main.cpp
)

if(SUPERHERO_ENABLE_OPENCL)
    find_package(OpenCL)
    if(OpenCL_FOUND)
        list(APPEND SUPERHERO_SOURCES src/gpu/opencl_backend.cpp)
        add_compile_definitions(SUPERHERO_HAVE_OPENCL=1)
    else()
        message(STATUS "OpenCL not found; building CPU-only SUPERHERO")
    endif()
endif()

add_executable(superhero ${SUPERHERO_SOURCES})
target_include_directories(superhero PRIVATE src)
target_link_libraries(superhero PRIVATE Threads::Threads)

if(SUPERHERO_ENABLE_OPENCL AND OpenCL_FOUND)
    target_link_libraries(superhero PRIVATE OpenCL::OpenCL)
    target_compile_definitions(superhero PRIVATE SUPERHERO_HAVE_OPENCL=1)
endif()

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
install(FILES opencl/matmul_kernel.cl DESTINATION share/superhero/opencl)
