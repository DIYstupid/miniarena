#pragma once

#include <mysql/mysql.h>
#include <memory>
#include <optional>
#include <string>
#include <cstdint>

#include "connection_pool.h"
#include "game/player.h"

namespace miniarena {

// RAII wrapper around a MySQL connection pool.
class MysqlClient {
public:
    MysqlClient() = default;
    ~MysqlClient();

    MysqlClient(const MysqlClient&) = delete;
    MysqlClient& operator=(const MysqlClient&) = delete;

    bool connect(const std::string& host, int port,
                 const std::string& user, const std::string& pass,
                 const std::string& db, int pool_size = 4);

    void close();
    [[nodiscard]] bool isConnected() const noexcept;

    bool execute(const std::string& sql);
    std::string escape(const std::string& str);

    std::optional<PlayerRecord> getPlayer(const std::string& username);
    uint64_t createPlayer(const std::string& username, const std::string& password_hash);
    void recordLogin(uint64_t player_id);
    void saveBattleResult(uint64_t player_id, uint64_t room_id,
                          int kills, int deaths, int damage_dealt,
                          int damage_taken, int rank);
    void ensureSchema();

private:
    static MYSQL* createConnection(const std::string& host, int port,
                                   const std::string& user, const std::string& pass,
                                   const std::string& db);
    static void destroyConnection(MYSQL* conn);

    std::unique_ptr<ConnectionPool<MYSQL*>> pool_;
    bool connected_ = false;

    // Connection params saved for pool creation
    std::string host_, user_, pass_, db_;
    int port_ = 3306;
    int pool_size_ = 4;
};

}  // namespace miniarena
