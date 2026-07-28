#pragma once

#include <hiredis/hiredis.h>
#include <memory>
#include <optional>
#include <string>
#include <cstdint>
#include <vector>

#include "connection_pool.h"

namespace miniarena {

class RedisClient {
public:
    RedisClient() = default;
    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool connect(const std::string& host, int port, int pool_size = 4);
    void close();
    [[nodiscard]] bool isConnected() const noexcept;

    // --- Generic commands ---
    bool set(const std::string& key, const std::string& value);
    bool setEx(const std::string& key, int ttl_sec, const std::string& value);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool expire(const std::string& key, int ttl_sec);
    bool exists(const std::string& key);

    // --- List operations ---
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

    // --- Player cache (for login acceleration) ---
    void cachePlayer(const std::string& username, uint64_t player_id);
    std::optional<uint64_t> getCachedPlayer(const std::string& username);

private:
    static redisContext* createConnection(const std::string& host, int port);
    static void destroyConnection(redisContext* ctx);
    bool checkReply(redisReply* reply, int expected_type, redisContext* ctx);
    std::string makeKey(const std::string& prefix, uint64_t id);

    std::unique_ptr<ConnectionPool<redisContext*>> pool_;
    bool connected_ = false;

    std::string host_;
    int port_ = 6379;
    int pool_size_ = 4;
};

}  // namespace miniarena
