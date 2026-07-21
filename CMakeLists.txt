cmake_minimum_required(VERSION 3.20)
project(MiniArena VERSION 0.1.0 LANGUAGES CXX)

# ============================================================
# C++ Standard
# ============================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Build Types
# ============================================================
set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type: Debug, Release, RelWithDebInfo, ASan, TSan")

if(CMAKE_BUILD_TYPE STREQUAL "ASan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=address")
elseif(CMAKE_BUILD_TYPE STREQUAL "TSan")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
    set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fsanitize=thread")
endif()

# ============================================================
# Options
# ============================================================
option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_ROBOT  "Build robot client" OFF)

# ============================================================
# Dependencies
# ============================================================
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)

# ============================================================
# Protobuf code generation
# ============================================================
set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_BINARY_DIR}")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_DIR}/messages.proto)

set_source_files_properties(${PROTO_SRCS} ${PROTO_HDRS} PROPERTIES
    GENERATED TRUE
    COMPILE_FLAGS "-Wno-deprecated-declarations"
)

add_library(miniarena_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(miniarena_proto PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(miniarena_proto PUBLIC protobuf::libprotobuf)

# ============================================================
# Source Subdirectories
# ============================================================
add_subdirectory(src)

# ============================================================
# Tests
# ============================================================
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================
# Robot Client (Go)
# ============================================================
if(BUILD_ROBOT)
    add_subdirectory(robot)
endif()
