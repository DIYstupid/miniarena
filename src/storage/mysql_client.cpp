#include "mysql_client.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <sstream>

namespace miniarena {

MysqlClient::~MysqlClient() {
    close();
}

MYSQL* MysqlClient::createConnection(const std::string& host, int port,
                                      const std::string& user, const std::string& pass,
                                      const std::string& db) {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        spdlog::error("mysql_init failed");
        return nullptr;
    }
    bool reconnect = 1;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);
    if (!mysql_real_connect(conn, host.c_str(), user.c_str(), pass.c_str(),
                            db.c_str(), port, nullptr, 0)) {
        spdlog::error("mysql_real_connect failed: {}", mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }
    return conn;
}

void MysqlClient::destroyConnection(MYSQL* conn) {
    if (conn) mysql_close(conn);
}

bool MysqlClient::connect(const std::string& host, int port,
                           const std::string& user, const std::string& pass,
                           const std::string& db, int pool_size) {
    host_ = host;
    port_ = port;
    user_ = user;
    pass_ = pass;
    db_   = db;
    pool_size_ = pool_size;

    try {
        pool_ = std::make_unique<ConnectionPool<MYSQL*>>(
            pool_size_,
            [this] { return createConnection(host_, port_, user_, pass_, db_); },
            destroyConnection);
        connected_ = true;
        spdlog::info("MySQL pool: {} connections to {}:{}/{}", pool_size_, host_, port_, db_);
        return true;
    } catch (const std::exception& e) {
        spdlog::error("MySQL pool creation failed: {}", e.what());
        return false;
    }
}

void MysqlClient::close() {
    connected_ = false;
    pool_.reset();
}

bool MysqlClient::isConnected() const noexcept {
    return connected_;
}

bool MysqlClient::execute(const std::string& sql) {
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;
    if (mysql_query(conn, sql.c_str()) != 0) {
        spdlog::error("MySQL query failed: {} — SQL: {}", mysql_error(conn), sql);
        return false;
    }
    return true;
}

std::string MysqlClient::escape(const std::string& str) {
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;
    std::string out(str.size() * 2 + 1, '\0');
    size_t len = mysql_real_escape_string(conn, out.data(), str.c_str(), str.size());
    out.resize(len);
    return out;
}

std::optional<PlayerRecord> MysqlClient::getPlayer(const std::string& username) {
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;

    std::string escaped(username.size() * 2 + 1, '\0');
    size_t len = mysql_real_escape_string(conn, escaped.data(), username.c_str(), username.size());
    escaped.resize(len);

    std::string sql = "SELECT id, username, password, rating, total_games, wins "
                      "FROM players WHERE username='" + escaped + "'";
    if (mysql_query(conn, sql.c_str()) != 0) {
        spdlog::error("getPlayer query failed: {}", mysql_error(conn));
        return std::nullopt;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    if (!result) return std::nullopt;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) { mysql_free_result(result); return std::nullopt; }
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
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;

    std::string eu(username.size() * 2 + 1, '\0');
    std::string ep(password_hash.size() * 2 + 1, '\0');
    size_t lu = mysql_real_escape_string(conn, eu.data(), username.c_str(), username.size());
    size_t lp = mysql_real_escape_string(conn, ep.data(), password_hash.c_str(), password_hash.size());
    eu.resize(lu); ep.resize(lp);
    std::string sql = "INSERT INTO players (username, password) VALUES ('" + eu + "', '" + ep + "')";
    if (mysql_query(conn, sql.c_str()) != 0) {
        spdlog::error("createPlayer failed: {}", mysql_error(conn));
        return 0;
    }
    return mysql_insert_id(conn);
}

void MysqlClient::recordLogin(uint64_t player_id) {
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;
    std::string sql = "INSERT INTO login_records (player_id) VALUES (" + std::to_string(player_id) + ")";
    mysql_query(conn, sql.c_str());  // best-effort, ignore errors
}

void MysqlClient::saveBattleResult(uint64_t player_id, uint64_t room_id,
                                    int kills, int deaths, int damage_dealt,
                                    int damage_taken, int rank) {
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;
    std::ostringstream oss;
    oss << "INSERT INTO battle_records "
        << "(player_id, room_id, kills, deaths, damage_dealt, damage_taken, `rank`) "
        << "VALUES (" << player_id << ", " << room_id << ", "
        << kills << ", " << deaths << ", " << damage_dealt << ", "
        << damage_taken << ", " << rank << ")";
    mysql_query(conn, oss.str().c_str());  // best-effort
}

void MysqlClient::ensureSchema() {
    auto guard = pool_->borrow();
    MYSQL* conn = *guard;
    const char* schemas[] = {
        R"(CREATE TABLE IF NOT EXISTS players (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(64) NOT NULL UNIQUE,
            password VARCHAR(128) NOT NULL DEFAULT '',
            rating INT DEFAULT 1000,
            total_games INT DEFAULT 0,
            wins INT DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        ))",
        R"(CREATE TABLE IF NOT EXISTS login_records (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            player_id BIGINT UNSIGNED NOT NULL,
            login_time DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_id) REFERENCES players(id)
        ))",
        R"(CREATE TABLE IF NOT EXISTS battle_records (
            id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
            player_id BIGINT UNSIGNED NOT NULL,
            room_id BIGINT UNSIGNED NOT NULL,
            kills INT DEFAULT 0,
            deaths INT DEFAULT 0,
            damage_dealt INT DEFAULT 0,
            damage_taken INT DEFAULT 0,
            `rank` INT DEFAULT 0,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (player_id) REFERENCES players(id)
        ))",
    };
    for (auto* s : schemas) {
        mysql_query(conn, s);
    }
}

}  // namespace miniarena
