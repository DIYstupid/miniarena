#pragma once

#include <hiredis/hiredis.h>
#include <optional>
#include <string>
#include <cstdint>
#include <vector>

namespace miniarena {

// Thin RAII wrapper around hiredis (Redis C client).
class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool connect(const std::string& host, int port);
    void close();
    [[nodiscard]] bool isConnected() const noexcept;

    // --- Generic commands ---
    bool set(const std::string& key, const std::string& value);
    bool setEx(const std::string& key, int ttl_sec, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, int ttl_sec);
    bool exists(const std::string& key);

    // --- List operations (match queue) ---
    int64_t lpush(const std::string& key, const std::string& value);
    std::optional<std::string> brpop(const std::string& key, int timeout_sec = 0);
    int64_t llen(const std::string& key);

    // --- Session helpers ---
    void setSession(uint64_t session_id, const std::string& data, int ttl_sec = 3600);
    std::optional<std::string> getSession(uint64_t session_id);
    void delSession(uint64_t session_id);

    // --- Online status ---
    void setOnline(uint64_t player_id, uint64_t session_id);
    std::optional<uint64_t> getOnline(uint64_t player_id);
    void delOnline(uint64_t player_id);

    // --- Match queue ---
    void pushMatchQueue(int mode, uint64_t player_id);
    std::optional<uint64_t> popMatchQueue(int mode, int timeout_sec = 0);
    int matchQueueSize(int mode);

    // --- Room routing ---
    void setRoomRoute(uint64_t room_id, const std::string& addr);
    std::optional<std::string> getRoomRoute(uint64_t room_id);
    void delRoomRoute(uint64_t room_id);

private:
    bool checkReply(redisReply* reply, int expected_type = REDIS_REPLY_STATUS);
    std::string makeKey(const std::string& prefix, uint64_t id);

    redisContext* ctx_ = nullptr;
};

}  // namespace miniarena
