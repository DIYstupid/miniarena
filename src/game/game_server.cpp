#include "game_server.h"
#include "message_router.h"
#include "session_manager.h"
#include "room_manager.h"
#include "match_manager.h"
#include "storage/mysql_client.h"
#include "storage/redis_client.h"
#include "network/event_loop.h"
#include "network/acceptor.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>

namespace miniarena {

GameServer::GameServer(const GameConfig& config)
    : config_(config) {}

GameServer::~GameServer() {
    stop();
}

void GameServer::start() {
    initStorage();
    initBusiness();
    registerHandlers();
    initNetwork();

    // Start IO loops
    for (auto& loop : io_loops_) {
        std::thread([&loop = loop]() { loop->run(); }).detach();
    }

    // Start acceptor
    acceptor_->start();

    // Start timer thread
    running_ = true;
    timer_thread_ = std::thread(&GameServer::timerLoop, this);

    spdlog::info("GameServer started on port {}", acceptor_->port());
}

void GameServer::stop() {
    running_ = false;
    if (timer_thread_.joinable()) timer_thread_.join();
    if (acceptor_) acceptor_->stop();
    for (auto& loop : io_loops_) loop->stop();
}

void GameServer::initStorage() {
    mysql_ = std::make_unique<MysqlClient>();
    if (!mysql_->connect(config_.mysql_host, config_.mysql_port,
                         config_.mysql_user, config_.mysql_pass,
                         config_.mysql_db)) {
        throw std::runtime_error("Failed to connect to MySQL");
    }
    mysql_->ensureSchema();

    redis_ = std::make_unique<RedisClient>();
    if (!redis_->connect(config_.redis_host, config_.redis_port)) {
        throw std::runtime_error("Failed to connect to Redis");
    }
}

void GameServer::initBusiness() {
    sessions_ = std::make_unique<SessionManager>(mysql_.get(), redis_.get());
    rooms_    = std::make_unique<RoomManager>();
    router_   = std::make_unique<MessageRouter>();

    matcher_  = std::make_unique<MatchManager>(
        rooms_.get(), sessions_.get(), redis_.get(), config_.match_room_size);

    // Wire send callback: session → network layer
    sessions_->setSendCallback([this](ConnectionId conn_id, uint32_t msg_id,
                                       const std::string& payload) {
        sendResponse(conn_id, msg_id, payload);
    });

    // Wire match notify callback
    matcher_->setNotifyCallback([this](PlayerId player_id, uint32_t msg_id,
                                        const std::string& payload) {
        auto* s = sessions_->getByPlayer(player_id);
        if (s) {
            sendResponse(s->conn_id, msg_id, payload);
        }
    });
}

void GameServer::initNetwork() {
    io_loops_.clear();
    for (int i = 0; i < config_.io_threads; ++i) {
        io_loops_.push_back(std::make_unique<EventLoop>());
    }

    // Wire frame callback: network → message router
    for (auto& loop : io_loops_) {
        loop->setFrameCallback([this](ConnectionId conn_id, std::vector<Frame> frames) {
            for (auto& f : frames) {
                router_->dispatch(conn_id, f);
            }
        });
    }

    // Build pointer list for Acceptor
    std::vector<EventLoop*> loop_ptrs;
    for (auto& loop : io_loops_) {
        loop_ptrs.push_back(loop.get());
    }
    acceptor_ = std::make_unique<Acceptor>(config_.listen_port, loop_ptrs);
}

void GameServer::registerHandlers() {
    // Login (1001)
    router_->registerHandler(1001, [this](ConnectionId conn_id, const Frame& frame) {
        miniarena::LoginRequest req;
        if (!req.ParseFromString(frame.payload)) return;

        auto result = sessions_->login(conn_id, req.username(), req.password());

        miniarena::LoginResponse resp;
        resp.set_error_code(result.error_code);
        resp.set_session_id(result.session_id);
        resp.set_player_id(result.player_id);
        resp.set_error_msg(result.error_msg);

        std::string data;
        resp.SerializeToString(&data);
        sendResponse(conn_id, 1002, data);

        if (result.error_code == 0) {
            sessions_->setState(result.session_id, SessionState::IN_LOBBY);
        }
    });

    // Heartbeat (1101)
    router_->registerHandler(1101, [this](ConnectionId conn_id, const Frame&) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s) {
            // Not logged in — ignore heartbeat or close
            return;
        }
        sessions_->heartbeat(s->session_id);

        miniarena::HeartbeatResponse resp;
        resp.set_server_time_ms(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now().time_since_epoch()).count());

        std::string data;
        resp.SerializeToString(&data);
        sendResponse(conn_id, 1102, data);
    });

    // MatchStart (2001)
    router_->registerHandler(2001, [this](ConnectionId conn_id, const Frame& frame) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s) {
            miniarena::MatchStartResponse resp;
            resp.set_error_code(20002);
            resp.set_error_msg("Not logged in");
            std::string data;
            resp.SerializeToString(&data);
            sendResponse(conn_id, 2002, data);
            return;
        }

        miniarena::MatchStartRequest req;
        req.ParseFromString(frame.payload);

        int mode = req.mode();
        int err = matcher_->joinQueue(s->player_id, mode);

        miniarena::MatchStartResponse resp;
        resp.set_error_code(err);
        std::string data;
        resp.SerializeToString(&data);
        sendResponse(conn_id, 2002, data);
    });

    // MatchCancel (2003)
    router_->registerHandler(2003, [this](ConnectionId conn_id, const Frame&) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s) return;
        matcher_->leaveQueue(s->player_id);
    });

    // EnterRoom (3001)
    router_->registerHandler(3001, [this](ConnectionId conn_id, const Frame& frame) {
        auto* s = sessions_->getByConn(conn_id);
        miniarena::EnterRoomResponse resp;

        if (!s) {
            resp.set_error_code(20002);
            resp.set_error_msg("Not logged in");
        } else {
            miniarena::EnterRoomRequest req;
            req.ParseFromString(frame.payload);

            auto* room = rooms_->getRoom(req.room_id());
            if (!room) {
                resp.set_error_code(40001);
                resp.set_error_msg("Room not found");
            } else {
                int err = room->addPlayer(s->player_id, s->username);
                resp.set_error_code(err);
                if (err == 0) {
                    resp.set_room_id(req.room_id());
                    sessions_->setCurrentRoom(s->session_id, req.room_id());
                }
            }
        }

        std::string data;
        resp.SerializeToString(&data);
        sendResponse(conn_id, 3002, data);
    });

    // PlayerReady (3003)
    router_->registerHandler(3003, [this](ConnectionId conn_id, const Frame&) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s || s->current_room == 0) return;

        auto* room = rooms_->getRoom(s->current_room);
        if (!room) return;

        room->playerReady(s->player_id);

        // If all ready, start battle
        if (room->allReady()) {
            room->startBattle();

            miniarena::BattleStartNotify notify;
            notify.set_room_id(room->id());
            notify.set_countdown_ms(0);
            std::string data;
            notify.SerializeToString(&data);

            for (auto& [pid, info] : room->players()) {
                auto* ps = sessions_->getByPlayer(pid);
                if (ps) {
                    sendResponse(ps->conn_id, 3004, data);
                }
            }
        }
    });
}

void GameServer::timerLoop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.match_interval_ms));

        // Matchmaking
        matcher_->tryMatch(0);

        // Session cleanup
        sessions_->cleanupExpired();
    }
}

void GameServer::sendResponse(ConnectionId conn_id, uint32_t msg_id,
                               const std::string& payload) {
    // Encode and queue through the network layer
    // Find the EventLoop that owns this connection and send through it.
    // For now, we co-locate: connections are owned by EventLoops,
    // and sendFrame will be handled in the next EPOLLOUT cycle.
    auto data = FrameCodec::encode(msg_id, 0, 0, payload);
    // TODO: dispatch to correct EventLoop via a send queue
    // For now, we record that this needs the network layer integration
}

}  // namespace miniarena
