#pragma once

#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include "player.h"
#include "session.h"
#include "room.h"
#include "network/network_types.h"

namespace miniarena {

struct GameConfig {
    uint16_t listen_port    = 9000;
    int      io_threads     = 4;
    int      match_room_size = 10;
    int      match_interval_ms = 500;

    std::string mysql_host = "127.0.0.1";
    int         mysql_port = 3306;
    std::string mysql_user = "miniarena";
    std::string mysql_pass = "miniarena";
    std::string mysql_db   = "miniarena";

    std::string redis_host = "127.0.0.1";
    int         redis_port = 6379;
};

class MysqlClient;
class RedisClient;
class SessionManager;
class RoomManager;
class MatchManager;
class BattleManager;
class MessageRouter;
class EventLoop;
class Acceptor;

class GameServer {
public:
    explicit GameServer(const GameConfig& config);
    ~GameServer();

    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    void start();
    void stop();

private:
    void initStorage();
    void initBusiness();
    void initNetwork();
    void registerHandlers();
    void timerLoop();
    void sendResponse(ConnectionId conn_id, uint32_t msg_id,
                      const std::string& payload);
    void onConnectionAccepted(ConnectionId conn_id, EventLoop* loop);

    GameConfig config_;

    // Storage
    std::unique_ptr<MysqlClient> mysql_;
    std::unique_ptr<RedisClient> redis_;

    // Business
    std::unique_ptr<SessionManager> sessions_;
    std::unique_ptr<RoomManager> rooms_;
    std::unique_ptr<MatchManager> matcher_;
    std::unique_ptr<BattleManager> battle_mgr_;
    std::unique_ptr<MessageRouter> router_;

    // Network
    std::vector<std::unique_ptr<EventLoop>> io_loops_;
    std::unique_ptr<Acceptor> acceptor_;

    // Connection → EventLoop mapping for sendResponse
    std::mutex conn_mutex_;
    std::unordered_map<ConnectionId, EventLoop*> conn_to_loop_;

    // Timer
    std::thread timer_thread_;
    std::atomic<bool> running_{false};
};

}  // namespace miniarena
