#include <spdlog/spdlog.h>
#include <csignal>
#include <iostream>
#include "game/game_server.h"

std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

int main() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("MiniArena Server starting...");
    spdlog::info("Build: {} {}", __DATE__, __TIME__);

    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    try {
        miniarena::GameConfig config;
        config.listen_port = 9000;
        config.io_threads = 4;

        miniarena::GameServer server(config);
        server.start();

        spdlog::info("MiniArena Server v0.1.0 running. Press Ctrl+C to stop.");

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        spdlog::info("Shutting down...");
        server.stop();
    } catch (const std::exception& e) {
        spdlog::error("Fatal: {}", e.what());
        return 1;
    }

    spdlog::info("MiniArena Server stopped.");
    return 0;
}
