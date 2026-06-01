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

# Shared source files
set(CORE_SOURCES
    src/rdma_conn.cpp
    src/dsm_object.cpp
    src/occ_engine.cpp
    src/latency_sampler.cpp
    src/backoff.cpp
    src/hot_detection.cpp
    src/server_arbitration.cpp
)

add_library(rdsm_core STATIC ${CORE_SOURCES})
target_link_libraries(rdsm_core
    Threads::Threads
    ${LIBIBVERBS_LIBRARIES}
    ${LIBRDMACM_LIBRARIES}
)

# Main executable
add_executable(phase2_dsm_benchmark experiments/phase2_dsm_benchmark.cpp)
target_link_libraries(phase2_dsm_benchmark
    rdsm_core
)

enable_testing()

add_executable(rdsm_regression_tests tests/rdsm_regression_tests.cpp)
target_link_libraries(rdsm_regression_tests rdsm_core)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME rdsm_regression_tests COMMAND rdsm_regression_tests)
add_test(
    NAME benchmark_json_compat
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/check_benchmark_json.py
            $<TARGET_FILE:phase2_dsm_benchmark>
)

# Installation
install(TARGETS phase2_dsm_benchmark DESTINATION bin)
