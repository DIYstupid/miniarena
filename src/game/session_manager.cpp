#include "session_manager.h"
#include "storage/mysql_client.h"
#include "storage/redis_client.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>
#include <random>
#include <sstream>

namespace miniarena {

SessionManager::SessionManager(MysqlClient* mysql, RedisClient* redis)
    : mysql_(mysql), redis_(redis) {}

uint64_t SessionManager::generateSessionId() {
    static thread_local std::mt19937_64 rng(std::random_device{}());
    return rng();
}

void SessionManager::kickDuplicate(PlayerId player_id) {
    auto it = player_map_.find(player_id);
    if (it == player_map_.end()) return;

    uint64_t old_sid = it->second;
    auto sit = sessions_.find(old_sid);
    if (sit != sessions_.end()) {
        spdlog::info("Kicking duplicate session {} for player {}", old_sid, player_id);
        // Notify old connection
        miniarena::LoginResponse resp;
        resp.set_error_code(20003);
        resp.set_error_msg("Duplicate login - kicked");
        std::string data;
        resp.SerializeToString(&data);
        sendTo(sit->second.conn_id, 1002, data);
        // Remove old session
        conn_map_.erase(sit->second.conn_id);
        removeFromRedis(old_sid);
        sessions_.erase(sit);
    }
    player_map_.erase(it);
}

LoginResult SessionManager::login(ConnectionId conn_id,
                                   const std::string& username,
                                   const std::string& password) {
    LoginResult result;

    // 1. Look up or create player
    auto player = mysql_->getPlayer(username);
    if (!player) {
        // Auto-register (simplified; real impl would hash password)
        uint64_t pid = mysql_->createPlayer(username, password);
        if (pid == 0) {
            result.error_code = 20001;
            result.error_msg = "Failed to create player";
            return result;
        }
        player = mysql_->getPlayer(username);
        if (!player) {
            result.error_code = 20001;
            result.error_msg = "Player creation failed";
            return result;
        }
    }

    // 2. Verify password (plaintext for P3; hash comparison in production)
    if (player->password_hash != password) {
        result.error_code = 20001;
        result.error_msg = "Invalid password";
        return result;
    }

    // 3. Kick duplicate
    kickDuplicate(player->id);

    // 4. Create session
    uint64_t sid = generateSessionId();
    Session s;
    s.session_id = sid;
    s.player_id  = player->id;
    s.conn_id    = conn_id;
    s.username   = player->username;
    s.state      = SessionState::AUTHENTICATED;
    s.login_time = Clock::now();
    s.last_active = s.login_time;

    // 5. Store in maps
    sessions_[sid] = s;
    conn_map_[conn_id] = sid;
    player_map_[player->id] = sid;

    // 6. Save to Redis
    saveToRedis(s);

    // 7. Record login
    mysql_->recordLogin(player->id);

    spdlog::info("Player {} ({}) logged in, session={}", player->username, player->id, sid);

    result.error_code = 0;
    result.session_id = sid;
    result.player_id  = player->id;
    return result;
}

void SessionManager::logout(uint64_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;

    spdlog::info("Player {} logout, session={}", it->second.username, session_id);
    conn_map_.erase(it->second.conn_id);
    player_map_.erase(it->second.player_id);
    removeFromRedis(session_id);
    sessions_.erase(it);
}

void SessionManager::heartbeat(uint64_t session_id) {
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return;
    it->second.last_active = Clock::now();
}

Session* SessionManager::getByConn(ConnectionId conn_id) {
    auto it = conn_map_.find(conn_id);
    if (it == conn_map_.end()) return nullptr;
    auto sit = sessions_.find(it->second);
    return sit != sessions_.end() ? &sit->second : nullptr;
}

Session* SessionManager::getBySession(uint64_t session_id) {
    auto it = sessions_.find(session_id);
    return it != sessions_.end() ? &it->second : nullptr;
}

Session* SessionManager::getByPlayer(PlayerId player_id) {
    auto it = player_map_.find(player_id);
    if (it == player_map_.end()) return nullptr;
    auto sit = sessions_.find(it->second);
    return sit != sessions_.end() ? &sit->second : nullptr;
}

bool SessionManager::setState(uint64_t session_id, SessionState new_state) {
    auto* s = getBySession(session_id);
    if (!s) return false;
    s->state = new_state;
    saveToRedis(*s);
    return true;
}

bool SessionManager::setMatchMode(uint64_t session_id, int32_t mode) {
    auto* s = getBySession(session_id);
    if (!s) return false;
    s->match_mode = mode;
    return true;
}

bool SessionManager::setCurrentRoom(uint64_t session_id, uint64_t room_id) {
    auto* s = getBySession(session_id);
    if (!s) return false;
    s->current_room = room_id;
    saveToRedis(*s);
    return true;
}

void SessionManager::cleanupExpired() {
    auto now = Clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.last_active).count();
        if (elapsed > 3600) {  // 1 hour TTL
            spdlog::info("Session {} expired for player {}",
                         it->second.session_id, it->second.username);
            conn_map_.erase(it->second.conn_id);
            player_map_.erase(it->second.player_id);
            removeFromRedis(it->second.session_id);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void SessionManager::markDisconnected(ConnectionId conn_id) {
    auto* s = getByConn(conn_id);
    if (!s) return;
    s->state = SessionState::DISCONNECTED;
    s->last_active = Clock::now();
    saveToRedis(*s);
}

LoginResult SessionManager::tryReconnect(uint64_t session_id, ConnectionId new_conn_id) {
    LoginResult result;
    auto* s = getBySession(session_id);
    if (!s) {
        result.error_code = 20002;
        result.error_msg = "Session not found";
        return result;
    }
    if (s->state != SessionState::DISCONNECTED) {
        result.error_code = 20003;
        result.error_msg = "Session not in disconnected state";
        return result;
    }

    // Check within 30s window
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now() - s->last_active).count();
    if (elapsed > 30) {
        result.error_code = 20002;
        result.error_msg = "Reconnect window expired";
        logout(session_id);
        return result;
    }

    // Restore: rebind connection, restore state
    conn_map_.erase(s->conn_id);
    s->conn_id = new_conn_id;
    conn_map_[new_conn_id] = session_id;
    s->state = SessionState::IN_LOBBY;  // back to lobby after reconnect
    s->last_active = Clock::now();
    saveToRedis(*s);

    result.error_code = 0;
    result.session_id = session_id;
    result.player_id  = s->player_id;
    return result;
}

void SessionManager::saveToRedis(const Session& s) {
    std::ostringstream oss;
    oss << static_cast<int>(s.state) << "|"
        << s.player_id << "|"
        << s.conn_id << "|"
        << s.username << "|"
        << s.current_room << "|"
        << s.match_mode;
    redis_->setSession(s.session_id, oss.str());
    redis_->setOnline(s.player_id, s.session_id);
}

void SessionManager::removeFromRedis(uint64_t session_id) {
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        redis_->delOnline(it->second.player_id);
    }
    redis_->delSession(session_id);
}

void SessionManager::sendTo(ConnectionId conn_id, uint32_t msg_id,
                             const std::string& payload) {
    if (send_cb_) {
        send_cb_(conn_id, msg_id, payload);
    }
}

}  // namespace miniarena
