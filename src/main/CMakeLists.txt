# ============================================================
# MiniArena Server Entry Points
# ============================================================

add_executable(miniarena_server
    main.cpp
)

target_link_libraries(miniarena_server PRIVATE
    miniarena_gateway
    miniarena_game
    miniarena_storage
    miniarena_network
    miniarena_common
)
