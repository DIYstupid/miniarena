#include "mysql_client.h"

#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace miniarena {

MysqlClient::~MysqlClient() {
    close();
}

bool MysqlClient::connect(const std::string& host, int port,
                           const std::string& user, const std::string& pass,
                           const std::string& db) {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        spdlog::error("mysql_init failed");
        return false;
    }

    // Auto-reconnect
    bool reconnect = 1;
    mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(), pass.c_str(),
                            db.c_str(), port, nullptr, 0)) {
        spdlog::error("mysql_real_connect failed: {}", mysql_error(conn_));
        close();
        return false;
    }

    spdlog::info("MySQL connected to {}:{}/{}", host, port, db);
    return true;
}

void MysqlClient::close() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool MysqlClient::isConnected() const noexcept {
    return conn_ != nullptr;
}

bool MysqlClient::execute(const std::string& sql) {
    if (mysql_query(conn_, sql.c_str()) != 0) {
        spdlog::error("MySQL query failed: {} — SQL: {}", mysql_error(conn_), sql);
        return false;
    }
    return true;
}

std::string MysqlClient::escape(const std::string& str) {
    if (!conn_) return str;
    std::string out(str.size() * 2 + 1, '\0');
    size_t len = mysql_real_escape_string(conn_, out.data(), str.c_str(), str.size());
    out.resize(len);
    return out;
}

std::optional<PlayerRecord> MysqlClient::getPlayer(const std::string& username) {
    std::string sql = "SELECT id, username, password, rating, total_games, wins "
                      "FROM players WHERE username='" + escape(username) + "'";
    if (mysql_query(conn_, sql.c_str()) != 0) {
        spdlog::error("getPlayer query failed: {}", mysql_error(conn_));
        return std::nullopt;
    }

    MYSQL_RES* result = mysql_store_result(conn_);
    if (!result) return std::nullopt;

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return std::nullopt;
    }

    PlayerRecord p;
    p.id            = row[0] ? std::stoull(row[0]) : 0;
    p.username      = row[1] ? row[1] : "";
    p.password_hash = row[2] ? row[2] : "";
    p.rating        = row[3] ? std::stoi(row[3]) : 1000;
    p.total_games   = row[4] ? std::stoi(row[4]) : 0;
    p.wins          = row[5] ? std::stoi(row[5]) : 0;

    mysql_free_result(result);
    return p;
}

uint64_t MysqlClient::createPlayer(const std::string& username,
                                    const std::string& password_hash) {
    std::string sql = "INSERT INTO players (username, password) VALUES ('"
                    + escape(username) + "', '" + escape(password_hash) + "')";
    if (!execute(sql)) {
        return 0;
    }
    return mysql_insert_id(conn_);
}

void MysqlClient::recordLogin(uint64_t player_id) {
    std::string sql = "INSERT INTO login_records (player_id) VALUES ("
                    + std::to_string(player_id) + ")";
    execute(sql);
}

void MysqlClient::saveBattleResult(uint64_t player_id, uint64_t room_id,
                                    int kills, int deaths, int damage_dealt,
                                    int damage_taken, int rank) {
    std::ostringstream oss;
    oss << "INSERT INTO battle_records "
        << "(player_id, room_id, kills, deaths, damage_dealt, damage_taken, `rank`) "
        << "VALUES (" << player_id << ", " << room_id << ", "
        << kills << ", " << deaths << ", " << damage_dealt << ", "
        << damage_taken << ", " << rank << ")";
    execute(oss.str());
}

void MysqlClient::ensureSchema() {
    execute(R"(
        CREATE TABLE IF NOT EXISTS players (
            id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            username    VARCHAR(64) NOT NULL UNIQUE,
            password    VARCHAR(128) NOT NULL DEFAULT '',
            rating      INT DEFAULT 1000,
            total_games INT DEFAULT 0,
            wins        INT DEFAULT 0,
            created_at  DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )");
    execute(R"(
        CREATE TABLE IF NOT EXISTS login_records (
            id          BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            player_id   BIGINT UNSIGNED NOT NULL,
            login_time  DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_id) REFERENCES players(id)
        )
    )");
    execute(R"(
        CREATE TABLE IF NOT EXISTS battle_records (
            id              BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            player_id       BIGINT UNSIGNED NOT NULL,
            room_id         BIGINT UNSIGNED NOT NULL,
            kills           INT DEFAULT 0,
            deaths          INT DEFAULT 0,
            damage_dealt    INT DEFAULT 0,
            damage_taken    INT DEFAULT 0,
            `rank`          INT DEFAULT 0,
            created_at      DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_id) REFERENCES players(id)
        )
    )");
}

}  // namespace miniarena
