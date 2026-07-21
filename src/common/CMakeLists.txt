# Common utility library
add_library(miniarena_common STATIC
    common.cpp
)
target_include_directories(miniarena_common PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(miniarena_common PUBLIC
    miniarena_proto
    spdlog::spdlog
    Threads::Threads
)
