# Game logic library - room, tick, battle, match, session
add_library(miniarena_game STATIC
    game.cpp
)

target_include_directories(miniarena_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(miniarena_game PUBLIC
    miniarena_common
    miniarena_proto
    spdlog::spdlog
    Threads::Threads
)
