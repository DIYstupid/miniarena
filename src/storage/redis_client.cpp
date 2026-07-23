#include "redis_client.h"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace miniarena {

RedisClient::~RedisClient() {
    close();
}

bool RedisClient::connect(const std::string& host, int port) {
    ctx_ = redisConnect(host.c_str(), port);
    if (!ctx_ || ctx_->err) {
        spdlog::error("Redis connect failed: {}",
                      ctx_ ? ctx_->errstr : "allocation failed");
        close();
        return false;
    }
    spdlog::info("Redis connected to {}:{}", host, port);
    return true;
}

void RedisClient::close() {
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisClient::isConnected() const noexcept {
    return ctx_ != nullptr;
}

// ---- helpers ----

bool RedisClient::checkReply(redisReply* reply, int expected_type) {
    if (!reply) return false;
    bool ok = (reply->type == expected_type);
    if (!ok && reply->type == REDIS_REPLY_ERROR) {
        spdlog::warn("Redis error: {}", reply->str);
    }
    freeReplyObject(reply);
    return ok;
}

std::string RedisClient::makeKey(const std::string& prefix, uint64_t id) {
    std::ostringstream oss;
    oss << prefix << ":" << id;
    return oss.str();
}

// ---- Generic ops ----

bool RedisClient::set(const std::string& key, const std::string& value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SET %s %b", key.c_str(), value.data(), value.size()));
    return checkReply(reply);
}

bool RedisClient::setEx(const std::string& key, int ttl_sec, const std::string& value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SETEX %s %d %b", key.c_str(), ttl_sec,
                     value.data(), value.size()));
    return checkReply(reply);
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "GET %s", key.c_str()));
    if (!reply) return std::nullopt;
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return std::nullopt;
    }
    std::string val(reply->str, reply->len);
    freeReplyObject(reply);
    return val;
}

bool RedisClient::del(const std::string& key) {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "DEL %s", key.c_str()));
    return checkReply(reply, REDIS_REPLY_INTEGER);
}

bool RedisClient::expire(const std::string& key, int ttl_sec) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "EXPIRE %s %d", key.c_str(), ttl_sec));
    return checkReply(reply, REDIS_REPLY_INTEGER);
}

bool RedisClient::exists(const std::string& key) {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "EXISTS %s", key.c_str()));
    if (!reply) return false;
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return ok;
}

// ---- List ops ----

int64_t RedisClient::lpush(const std::string& key, const std::string& value) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LPUSH %s %b", key.c_str(), value.data(), value.size()));
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (reply) freeReplyObject(reply);
        return -1;
    }
    int64_t len = reply->integer;
    freeReplyObject(reply);
    return len;
}

std::optional<std::string> RedisClient::brpop(const std::string& key, int timeout_sec) {
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "BRPOP %s %d", key.c_str(), timeout_sec));
    if (!reply) return std::nullopt;
    if (reply->type == REDIS_REPLY_NIL || reply->type != REDIS_REPLY_ARRAY || reply->elements < 2) {
        freeReplyObject(reply);
        return std::nullopt;
    }
    std::string val(reply->element[1]->str, reply->element[1]->len);
    freeReplyObject(reply);
    return val;
}

int64_t RedisClient::llen(const std::string& key) {
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "LLEN %s", key.c_str()));
    if (!reply || reply->type != REDIS_REPLY_INTEGER) {
        if (reply) freeReplyObject(reply);
        return 0;
    }
    int64_t len = reply->integer;
    freeReplyObject(reply);
    return len;
}

// ---- Domain helpers ----

void RedisClient::setSession(uint64_t session_id, const std::string& data, int ttl_sec) {
    setEx(makeKey("session", session_id), ttl_sec, data);
}

std::optional<std::string> RedisClient::getSession(uint64_t session_id) {
    return get(makeKey("session", session_id));
}

void RedisClient::delSession(uint64_t session_id) {
    del(makeKey("session", session_id));
}

void RedisClient::setOnline(uint64_t player_id, uint64_t session_id) {
    setEx(makeKey("online", player_id), 3600, std::to_string(session_id));
}

std::optional<uint64_t> RedisClient::getOnline(uint64_t player_id) {
    auto val = get(makeKey("online", player_id));
    if (!val) return std::nullopt;
    return std::stoull(*val);
}

void RedisClient::delOnline(uint64_t player_id) {
    del(makeKey("online", player_id));
}

void RedisClient::pushMatchQueue(int mode, uint64_t player_id) {
    lpush(makeKey("match_queue", mode), std::to_string(player_id));
}

std::optional<uint64_t> RedisClient::popMatchQueue(int mode, int timeout_sec) {
    auto val = brpop(makeKey("match_queue", mode), timeout_sec);
    if (!val) return std::nullopt;
    return std::stoull(*val);
}

int RedisClient::matchQueueSize(int mode) {
    return static_cast<int>(llen(makeKey("match_queue", mode)));
}

void RedisClient::setRoomRoute(uint64_t room_id, const std::string& addr) {
    setEx(makeKey("room", room_id), 3600, addr);
}

std::optional<std::string> RedisClient::getRoomRoute(uint64_t room_id) {
    return get(makeKey("room", room_id));
}

void RedisClient::delRoomRoute(uint64_t room_id) {
    del(makeKey("room", room_id));
}

}  // namespace miniarena
