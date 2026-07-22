# ============================================================
# GoogleTest Setup
# ============================================================

# Build gtest from system source (Ubuntu libgtest-dev pattern)
add_library(gtest STATIC
    /usr/src/googletest/googletest/src/gtest-all.cc
)
target_include_directories(gtest PUBLIC
    /usr/src/googletest/googletest/include
    /usr/src/googletest/googletest
)
find_package(Threads REQUIRED)
target_link_libraries(gtest PUBLIC Threads::Threads)

add_library(gtest_main STATIC
    /usr/src/googletest/googletest/src/gtest_main.cc
)
target_link_libraries(gtest_main PUBLIC gtest)

# ============================================================
# Test Executables
# ============================================================

# Proto test
add_executable(test_proto
    test_proto.cpp
)
target_link_libraries(test_proto PRIVATE
    miniarena_proto
    gtest_main
)
add_test(NAME Proto COMMAND test_proto)

# Socket test
add_executable(test_socket
    test_socket.cpp
)
target_link_libraries(test_socket PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME Socket COMMAND test_socket)

# Buffer test
add_executable(test_buffer
    test_buffer.cpp
)
target_link_libraries(test_buffer PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME Buffer COMMAND test_buffer)

# FrameCodec test
add_executable(test_frame_codec
    test_frame_codec.cpp
)
target_link_libraries(test_frame_codec PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME FrameCodec COMMAND test_frame_codec)

# TimerWheel test
add_executable(test_timer_wheel
    test_timer_wheel.cpp
)
target_link_libraries(test_timer_wheel PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME TimerWheel COMMAND test_timer_wheel)

# Connection test
add_executable(test_connection
    test_connection.cpp
)
target_link_libraries(test_connection PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME Connection COMMAND test_connection)

# EventLoop test
add_executable(test_event_loop
    test_event_loop.cpp
)
target_link_libraries(test_event_loop PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME EventLoop COMMAND test_event_loop)

# Integration test
add_executable(test_integration
    test_integration.cpp
)
target_link_libraries(test_integration PRIVATE
    miniarena_network
    gtest_main
)
add_test(NAME Integration COMMAND test_integration)

# Smoke test (1000 connections)
add_executable(smoke_1000
    smoke_1000.cpp
)
target_link_libraries(smoke_1000 PRIVATE
    miniarena_network
)
