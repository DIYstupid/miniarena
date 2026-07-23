#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include <functional>
#include "session.h"

namespace miniarena {

class MysqlClient;
class RedisClient;

// Result returned by SessionManager::login().
struct LoginResult {
    int32_t     error_code = 0;
    uint64_t    session_id = 0;
    uint64_t    player_id  = 0;
    std::string error_msg;
};

// Manages all player sessions.
// Binds Connection ↔ Player, handles login/logout, duplicate kick, expiry.
class SessionManager {
public:
    SessionManager(MysqlClient* mysql, RedisClient* redis);

    // --- Login / Logout ---
    LoginResult login(ConnectionId conn_id, const std::string& username,
                      const std::string& password);

    void logout(uint64_t session_id);

    // --- Heartbeat ---
    void heartbeat(uint64_t session_id);

    // --- Queries ---
    Session* getByConn(ConnectionId conn_id);
    Session* getBySession(uint64_t session_id);
    Session* getByPlayer(PlayerId player_id);

    // --- State transitions ---
    bool setState(uint64_t session_id, SessionState new_state);
    bool setMatchMode(uint64_t session_id, int32_t mode);
    bool setCurrentRoom(uint64_t session_id, uint64_t room_id);

    // --- Cleanup ---
    void cleanupExpired();

    // --- P5: Disconnect / Reconnect ---
    void markDisconnected(ConnectionId conn_id);
    LoginResult tryReconnect(uint64_t session_id, ConnectionId new_conn_id);

    // --- Send response helper ---
    void setSendCallback(std::function<void(ConnectionId, uint32_t msg_id,
                           const std::string& payload)> cb) {
        send_cb_ = std::move(cb);
    }
    void sendTo(ConnectionId conn_id, uint32_t msg_id, const std::string& payload);

private:
    uint64_t generateSessionId();
    void kickDuplicate(PlayerId player_id);
    void saveToRedis(const Session& s);
    void removeFromRedis(uint64_t session_id);

    MysqlClient* mysql_;
    RedisClient* redis_;

    std::unordered_map<uint64_t, Session> sessions_;        // session_id → Session
    std::unordered_map<ConnectionId, uint64_t> conn_map_;   // conn_id → session_id
    std::unordered_map<PlayerId, uint64_t> player_map_;     // player_id → session_id

    std::function<void(ConnectionId, uint32_t, const std::string&)> send_cb_;
};

}  // namespace miniarena
