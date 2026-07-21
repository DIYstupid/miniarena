#include <spdlog/spdlog.h>
#include <iostream>

int main() {
    spdlog::set_level(spdlog::level::debug);
    spdlog::info("MiniArena Server starting...");
    spdlog::info("Build: {} {}", __DATE__, __TIME__);

    // TODO: Initialize gateway, game server, storage connections

    spdlog::info("MiniArena Server initialized. Press Ctrl+C to exit.");
    std::cout << "MiniArena Server v0.1.0" << std::endl;

    return 0;
}
