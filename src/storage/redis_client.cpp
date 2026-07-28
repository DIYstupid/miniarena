#include "redis_client.h"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace miniarena {

RedisClient::~RedisClient() {
    close();
}

redisContext* RedisClient::createConnection(const std::string& host, int port) {
    redisContext* ctx = redisConnect(host.c_str(), port);
    if (!ctx || ctx->err) {
        spdlog::error("redisConnect failed: {}",
                      ctx ? ctx->errstr : "out of memory");
        if (ctx) redisFree(ctx);
        return nullptr;
    }
    return ctx;
}

void RedisClient::destroyConnection(redisContext* ctx) {
    if (ctx) redisFree(ctx);
}

bool RedisClient::connect(const std::string& host, int port, int pool_size) {
    host_ = host;
    port_ = port;
    pool_size_ = pool_size;

    try {
        pool_ = std::make_unique<ConnectionPool<redisContext*>>(
            pool_size_,
            [this] { return createConnection(host_, port_); },
            destroyConnection);
        connected_ = true;
        spdlog::info("Redis pool: {} connections to {}:{}", pool_size_, host_, port_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Redis pool creation failed: {}", e.what());
        return false;
    }
}

void RedisClient::close() {
    connected_ = false;
    pool_.reset();
}

bool RedisClient::isConnected() const noexcept {
    return connected_;
}

bool RedisClient::checkReply(redisReply* reply, int expected_type, redisContext* ctx) {
    if (!reply) {
        spdlog::error("Redis reply null: {}", ctx->errstr);
        return false;
    }
    if (reply->type == REDIS_REPLY_ERROR) {
        spdlog::error("Redis error: {}", reply->str);
        freeReplyObject(reply);
        return false;
    }
    if (expected_type >= 0 && reply->type != expected_type) {
        spdlog::error("Redis unexpected reply type {} (expected {})",
                      reply->type, expected_type);
        freeReplyObject(reply);
        return false;
    }
    return true;
}

std::string RedisClient::makeKey(const std::string& prefix, uint64_t id) {
    return prefix + ":" + std::to_string(id);
}

// ── Generic commands ──────────────────────────────────────────────

bool RedisClient::set(const std::string& key, const std::string& value) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "SET %s %s", key.c_str(), value.c_str()));
    bool ok = checkReply(reply, REDIS_REPLY_STATUS, ctx);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::setEx(const std::string& key, int ttl_sec, const std::string& value) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "SETEX %s %d %s", key.c_str(), ttl_sec, value.c_str()));
    bool ok = checkReply(reply, REDIS_REPLY_STATUS, ctx);
    if (reply) freeReplyObject(reply);
    return ok;
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "GET %s", key.c_str()));
    if (!checkReply(reply, -1, ctx)) {
        if (reply) freeReplyObject(reply);
        return std::nullopt;
    }
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return std::nullopt;
    }
    std::string val(reply->str, reply->len);
    freeReplyObject(reply);
    return val;
}

bool RedisClient::del(const std::string& key) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "DEL %s", key.c_str()));
    if (reply) freeReplyObject(reply);
    return true;
}

bool RedisClient::expire(const std::string& key, int ttl_sec) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "EXPIRE %s %d", key.c_str(), ttl_sec));
    bool ok = checkReply(reply, REDIS_REPLY_INTEGER, ctx);
    if (reply) freeReplyObject(reply);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "EXISTS %s", key.c_str()));
    if (!checkReply(reply, REDIS_REPLY_INTEGER, ctx)) {
        if (reply) freeReplyObject(reply);
        return false;
    }
    bool exists = reply->integer == 1;
    freeReplyObject(reply);
    return exists;
}

// ── List operations ───────────────────────────────────────────────

int64_t RedisClient::lpush(const std::string& key, const std::string& value) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "LPUSH %s %s", key.c_str(), value.c_str()));
    if (!checkReply(reply, REDIS_REPLY_INTEGER, ctx)) {
        if (reply) freeReplyObject(reply);
        return 0;
    }
    int64_t len = reply->integer;
    freeReplyObject(reply);
    return len;
}

std::optional<std::string> RedisClient::brpop(const std::string& key, int timeout_sec) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "BRPOP %s %d", key.c_str(), timeout_sec));
    if (!checkReply(reply, REDIS_REPLY_ARRAY, ctx)) {
        if (reply) freeReplyObject(reply);
        return std::nullopt;
    }
    if (reply->elements < 2) {
        freeReplyObject(reply);
        return std::nullopt;
    }
    std::string val(reply->element[1]->str, reply->element[1]->len);
    freeReplyObject(reply);
    return val;
}

int64_t RedisClient::llen(const std::string& key) {
    auto guard = pool_->borrow();
    redisContext* ctx = *guard;
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx, "LLEN %s", key.c_str()));
    if (!checkReply(reply, REDIS_REPLY_INTEGER, ctx)) {
        if (reply) freeReplyObject(reply);
        return 0;
    }
    int64_t len = reply->integer;
    freeReplyObject(reply);
    return len;
}

// ── Session helpers ───────────────────────────────────────────────

void RedisClient::setSession(uint64_t session_id, const std::string& data, int ttl_sec) {
    setEx(makeKey("session", session_id), ttl_sec, data);
}

std::optional<std::string> RedisClient::getSession(uint64_t session_id) {
    return get(makeKey("session", session_id));
}

void RedisClient::delSession(uint64_t session_id) {
    del(makeKey("session", session_id));
}

// ── Online status ─────────────────────────────────────────────────

void RedisClient::setOnline(uint64_t player_id, uint64_t session_id) {
    set(makeKey("online", player_id), std::to_string(session_id));
}

std::optional<uint64_t> RedisClient::getOnline(uint64_t player_id) {
    auto val = get(makeKey("online", player_id));
    if (!val) return std::nullopt;
    return std::stoull(*val);
}

void RedisClient::delOnline(uint64_t player_id) {
    del(makeKey("online", player_id));
}

// ── Match queue ───────────────────────────────────────────────────

void RedisClient::pushMatchQueue(int mode, uint64_t player_id) {
    lpush("match_queue:" + std::to_string(mode), std::to_string(player_id));
}

std::optional<uint64_t> RedisClient::popMatchQueue(int mode, int timeout_sec) {
    auto val = brpop("match_queue:" + std::to_string(mode), timeout_sec);
    if (!val) return std::nullopt;
    return std::stoull(*val);
}

int RedisClient::matchQueueSize(int mode) {
    return static_cast<int>(llen("match_queue:" + std::to_string(mode)));
}

// ── Room routing ──────────────────────────────────────────────────

void RedisClient::setRoomRoute(uint64_t room_id, const std::string& addr) {
    set(makeKey("room", room_id), addr);
}

std::optional<std::string> RedisClient::getRoomRoute(uint64_t room_id) {
    return get(makeKey("room", room_id));
}

void RedisClient::delRoomRoute(uint64_t room_id) {
    del(makeKey("room", room_id));
}

// ── Player cache ──────────────────────────────────────────────────

void RedisClient::cachePlayer(const std::string& username, uint64_t player_id) {
    set("player:" + username, std::to_string(player_id));
}

std::optional<uint64_t> RedisClient::getCachedPlayer(const std::string& username) {
    auto val = get("player:" + username);
    if (!val) return std::nullopt;
    return std::stoull(*val);
}

}  // namespace miniarena
