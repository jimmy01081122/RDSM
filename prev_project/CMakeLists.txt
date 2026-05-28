cmake_minimum_required(VERSION 3.10)
project(FaRM_DSM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -g")

# Find RDMA libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBIBVERBS libibverbs)
pkg_check_modules(LIBRDMACM librdmacm)

if(NOT LIBIBVERBS_FOUND)
    message(FATAL_ERROR "libibverbs not found")
endif()

if(NOT LIBRDMACM_FOUND)
    message(FATAL_ERROR "librdmacm not found")
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${LIBIBVERBS_INCLUDE_DIRS})
include_directories(${LIBRDMACM_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/rdma_wrapper.cpp
    src/memory_manager.cpp
    src/dsm_transaction.cpp
    src/perf_monitor.cpp
    src/herd_kv_store.cpp
)

set(HEADERS
    include/rdma_wrapper.h
    include/memory_manager.h
    include/dsm_transaction.h
    include/perf_monitor.h
    include/herd_kv_store.h
)

# Library
add_library(farm_dsm STATIC ${SOURCES} ${HEADERS})
target_link_libraries(farm_dsm
    ${LIBIBVERBS_LIBRARIES}
    ${LIBRDMACM_LIBRARIES}
    pthread
)

# Example/Test executables
add_executable(farm_benchmark experiments/farm_benchmark.cpp)
target_link_libraries(farm_benchmark farm_dsm)

add_executable(herd_benchmark experiments/herd_benchmark.cpp)
target_link_libraries(herd_benchmark farm_dsm)

add_executable(os_analysis experiments/os_analysis.cpp)
target_link_libraries(os_analysis farm_dsm)

# Add compile options
target_compile_options(farm_dsm PRIVATE -fPIC)

# Print configuration
message(STATUS "Building FaRM DSM system")
message(STATUS "LIBIBVERBS: ${LIBIBVERBS_LIBRARIES}")
message(STATUS "LIBRDMACM: ${LIBRDMACM_LIBRARIES}")
