# Storage library - Redis, MySQL wrappers
add_library(miniarena_storage STATIC
    storage.cpp
)

target_include_directories(miniarena_storage PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(miniarena_storage PUBLIC
    miniarena_common
    spdlog::spdlog
)
