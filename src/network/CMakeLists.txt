# Network library - epoll, reactor, socket wrappers
add_library(miniarena_network STATIC
    network.cpp
)

target_include_directories(miniarena_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(miniarena_network PUBLIC
    miniarena_common
    miniarena_proto
    spdlog::spdlog
    Threads::Threads
)
