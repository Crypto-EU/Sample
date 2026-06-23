cmake_minimum_required(VERSION 3.16)
project(SUPERMINER VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_executable(superminer
  src/main.cpp
  src/stratum.cpp
  src/gpu_worker.cpp
  src/device.cpp
  src/challenge.cpp
  src/share.cpp
  src/stats.cpp
  src/util.cpp
)

target_include_directories(superminer PRIVATE src/include)

target_link_libraries(superminer PRIVATE Threads::Threads dl)

target_link_directories(superminer PRIVATE ${CMAKE_SOURCE_DIR}/native/target/release)
target_link_libraries(superminer PRIVATE superminer_share)
set_target_properties(superminer PROPERTIES
  BUILD_RPATH "${CMAKE_SOURCE_DIR}/native/target/release"
  INSTALL_RPATH "$ORIGIN"
)

install(TARGETS superminer RUNTIME DESTINATION .)
install(FILES ${CMAKE_SOURCE_DIR}/native/target/release/libsuperminer_share.so
        ${CMAKE_SOURCE_DIR}/native/target/release/libpearl_mining_capi.so
        DESTINATION . OPTIONAL)
