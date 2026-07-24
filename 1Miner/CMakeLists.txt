cmake_minimum_required(VERSION 3.16)
project(1Miner VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ONE_MINER_ENABLE_OPENCL "Build OpenCL AMD/NVIDIA backend" ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

set(ONE_MINER_SOURCES
  src/main.cpp
  src/sha256.cpp
  src/pow.cpp
  src/pool_rabbit.cpp
  src/pool_nats.cpp
  src/backend_cpu.cpp
  src/util.cpp
  src/stats.cpp
)

add_executable(1miner ${ONE_MINER_SOURCES})
target_include_directories(1miner PRIVATE src)
target_link_libraries(1miner PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

if(ONE_MINER_ENABLE_OPENCL)
  find_package(OpenCL)
  if(OpenCL_FOUND)
    target_sources(1miner PRIVATE src/backend_opencl.cpp)
    target_compile_definitions(1miner PRIVATE ONE_MINER_HAS_OPENCL=1)
    target_link_libraries(1miner PRIVATE OpenCL::OpenCL)
    message(STATUS "OpenCL backend enabled")
  else()
    message(WARNING "OpenCL not found; building CPU-only")
  endif()
endif()

install(TARGETS 1miner RUNTIME DESTINATION bin)
install(FILES src/opencl_kernels.cl DESTINATION share/1miner)
install(PROGRAMS
  hiveos/h-config.sh
  hiveos/h-run.sh
  hiveos/h-stats.sh
  DESTINATION share/1miner/hiveos
)
install(FILES hiveos/h-manifest.conf DESTINATION share/1miner/hiveos)
