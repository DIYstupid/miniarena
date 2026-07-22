# Network library - epoll, reactor, socket wrappers
add_library(miniarena_network STATIC
    socket.cpp
    buffer.cpp
    frame_codec.cpp
    timer_wheel.cpp
    connection.cpp
    event_loop.cpp
    acceptor.cpp
)

target_include_directories(miniarena_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(miniarena_network PUBLIC
    miniarena_common
    miniarena_proto
    spdlog::spdlog
    Threads::Threads
)
