cmake_minimum_required(VERSION 3.10)
project(phase2_dsm_occ)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -pthread")

# Find required packages
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Find libibverbs and librdmacm
pkg_check_modules(LIBIBVERBS REQUIRED libibverbs)
pkg_check_modules(LIBRDMACM REQUIRED librdmacm)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)
include_directories(${CMAKE_SOURCE_DIR}/experiments)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${LIBIBVERBS_INCLUDE_DIRS})
include_directories(${LIBRDMACM_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/rdma_conn.cpp
    src/dsm_object.cpp
    src/occ_engine.cpp
    src/backoff.cpp
    src/hot_detection.cpp
    src/server_arbitration.cpp
    experiments/phase2_dsm_benchmark.cpp
)

# Main executable
add_executable(phase2_dsm_benchmark ${SOURCES})

target_link_libraries(phase2_dsm_benchmark
    Threads::Threads
    ${LIBIBVERBS_LIBRARIES}
    ${LIBRDMACM_LIBRARIES}
)

target_include_directories(phase2_dsm_benchmark PRIVATE
    ${LIBIBVERBS_INCLUDE_DIRS}
    ${LIBRDMACM_INCLUDE_DIRS}
)

# Installation
install(TARGETS phase2_dsm_benchmark DESTINATION bin)
