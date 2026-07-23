# Game logic library - room, tick, battle, match, session
add_library(miniarena_game STATIC
    session_manager.cpp
    room.cpp
    room_manager.cpp
    match_manager.cpp
    message_router.cpp
    game_server.cpp
)

target_include_directories(miniarena_game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(miniarena_game PUBLIC
    miniarena_common
    miniarena_proto
    miniarena_network
    miniarena_storage
    spdlog::spdlog
    Threads::Threads
)
