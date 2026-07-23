#pragma once

#include <cstdint>
#include <string>
#include <chrono>
#include "player.h"
#include "network/network_types.h"

namespace miniarena {

enum class SessionState {
    CONNECTED,       // P2: connection established
    AUTHENTICATED,   // Login success
    IN_LOBBY,        // In lobby
    MATCHING,        // Waiting for match
    IN_ROOM,         // Inside a room
    IN_BATTLE,       // In battle (P4)
    SETTLEMENT,      // Battle result (P4)
    DISCONNECTED,    // Disconnected, awaiting reconnect (P5)
};

struct Session {
    uint64_t        session_id = 0;
    PlayerId        player_id  = 0;
    ConnectionId    conn_id    = 0;
    SessionState    state      = SessionState::CONNECTED;
    std::string     username;
    Clock::time_point login_time;
    Clock::time_point last_active;
    uint64_t        current_room = 0;  // 0 = no room
    int32_t         match_mode   = 0;  // 0 = not matching

    bool isOnline() const {
        return state != SessionState::DISCONNECTED;
    }
};

}  // namespace miniarena
