#pragma once

#include <mysql/mysql.h>
#include <optional>
#include <string>
#include <cstdint>
#include <memory>


#include "game/player.h"

namespace miniarena {

// Thin RAII wrapper around MySQL C API.
class MysqlClient {
public:
    MysqlClient() = default;
    ~MysqlClient();

    MysqlClient(const MysqlClient&) = delete;
    MysqlClient& operator=(const MysqlClient&) = delete;

    bool connect(const std::string& host, int port,
                 const std::string& user, const std::string& pass,
                 const std::string& db);

    void close();
    [[nodiscard]] bool isConnected() const noexcept;

    // Execute a raw query. Returns true on success.
    bool execute(const std::string& sql);

    // Escaped string for safe interpolation.
    std::string escape(const std::string& str);

    // --- Player operations ---
    std::optional<PlayerRecord> getPlayer(const std::string& username);
    uint64_t createPlayer(const std::string& username, const std::string& password_hash);

    // --- Login records ---
    void recordLogin(uint64_t player_id);

    // --- Battle records (P4) ---
    void saveBattleResult(uint64_t player_id, uint64_t room_id,
                          int kills, int deaths, int damage_dealt,
                          int damage_taken, int rank);

    // --- Schema init ---
    void ensureSchema();

    // Access underlying handle if needed
    [[nodiscard]] MYSQL* handle() noexcept { return conn_; }

private:
    MYSQL* conn_ = nullptr;
};

}  // namespace miniarena
