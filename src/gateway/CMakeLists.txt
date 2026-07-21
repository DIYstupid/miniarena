# Gateway server
add_library(miniarena_gateway STATIC
    gateway.cpp
)

target_include_directories(miniarena_gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(miniarena_gateway PUBLIC
    miniarena_network
    miniarena_common
)
