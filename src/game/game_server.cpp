#include "game_server.h"
#include "message_router.h"
#include "session_manager.h"
#include "room_manager.h"
#include "match_manager.h"
#include "battle_manager.h"
#include "logic_worker.h"
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

    battle_mgr_ = std::make_unique<BattleManager>(2);  // 2 logic threads

    // Wire send callback: session → network
    sessions_->setSendCallback([this](ConnectionId conn_id, uint32_t msg_id,
                                       const std::string& payload) {
        sendResponse(conn_id, msg_id, payload);
    });

    // Wire match notify callback
    matcher_->setNotifyCallback([this](PlayerId player_id, uint32_t msg_id,
                                        const std::string& payload) {
        auto* s = sessions_->getByPlayer(player_id);
        if (s) sendResponse(s->conn_id, msg_id, payload);
    });

    // Wire battle send callback
    battle_mgr_->setSendCallback([this](PlayerId player_id, uint32_t msg_id,
                                         const std::string& payload) {
        auto* s = sessions_->getByPlayer(player_id);
        if (s) sendResponse(s->conn_id, msg_id, payload);
    });
}

void GameServer::initNetwork() {
    io_loops_.clear();
    for (int i = 0; i < config_.io_threads; ++i) {
        io_loops_.push_back(std::make_unique<EventLoop>());
    }

    for (auto& loop : io_loops_) {
        auto* loop_ptr = loop.get();
        loop->setFrameCallback([this, loop_ptr](ConnectionId conn_id, std::vector<Frame> frames) {
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                conn_to_loop_[conn_id] = loop_ptr;
            }
            for (auto& f : frames) {
                router_->dispatch(conn_id, f);
            }
        });
        loop->setDisconnectCallback([this](ConnectionId conn_id) {
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                conn_to_loop_.erase(conn_id);
            }
            auto* s = sessions_->getByConn(conn_id);
            if (!s) return;
            sessions_->markDisconnected(conn_id);
            if (s->state == SessionState::IN_BATTLE && s->current_room > 0) {
                battle_mgr_->onPlayerDisconnect(s->player_id, s->current_room);
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
        // login_handler - silent in production
        miniarena::LoginRequest req;
        bool ok = req.ParseFromString(frame.payload);
        // parse result - silent in production
        if (!ok) return;

        auto result = sessions_->login(conn_id, req.username(), req.password());

        miniarena::LoginResponse resp;
        resp.set_error_code(result.error_code);
        resp.set_session_id(result.session_id);
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
            battle_mgr_->startBattle(room, sessions_.get());

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

    // PlayerMove (4001)
    router_->registerHandler(4001, [this](ConnectionId conn_id, const Frame& frame) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s || s->state != SessionState::IN_BATTLE) return;

        miniarena::PlayerMoveRequest req;
        if (!req.ParseFromString(frame.payload)) return;

        Command cmd;
        cmd.type = CommandType::MOVE;
        cmd.player_id = s->player_id;
        cmd.sequence = frame.sequence;
        cmd.move_dir_x = req.direction_x();
        cmd.move_dir_y = req.direction_y();
        battle_mgr_->dispatchCommand(s->current_room, cmd);
    });

    // PlayerAttack (4002)
    router_->registerHandler(4002, [this](ConnectionId conn_id, const Frame& frame) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s || s->state != SessionState::IN_BATTLE) return;

        miniarena::PlayerAttackRequest req;
        if (!req.ParseFromString(frame.payload)) return;

        Command cmd;
        cmd.type = CommandType::ATTACK;
        cmd.player_id = s->player_id;
        cmd.sequence = frame.sequence;
        cmd.target_id = req.target_id();
        battle_mgr_->dispatchCommand(s->current_room, cmd);
    });

    // PlayerSkill (4003)
    router_->registerHandler(4003, [this](ConnectionId conn_id, const Frame& frame) {
        auto* s = sessions_->getByConn(conn_id);
        if (!s || s->state != SessionState::IN_BATTLE) return;

        miniarena::PlayerSkillRequest req;
        if (!req.ParseFromString(frame.payload)) return;

        Command cmd;
        cmd.type = CommandType::SKILL;
        cmd.player_id = s->player_id;
        cmd.sequence = frame.sequence;
        cmd.skill_id = req.skill_id();
        cmd.target_id = req.target_id();
        cmd.skill_target_x = req.target_x();
        cmd.skill_target_y = req.target_y();
        battle_mgr_->dispatchCommand(s->current_room, cmd);
    });

    // ReconnectRequest (5001)
    router_->registerHandler(5001, [this](ConnectionId conn_id, const Frame& frame) {
        miniarena::ReconnectRequest req;
        if (!req.ParseFromString(frame.payload)) return;

        auto result = sessions_->tryReconnect(req.session_id(), conn_id);

        miniarena::ReconnectResponse resp;
        resp.set_error_code(result.error_code);
        resp.set_error_msg(result.error_msg);

        std::string data;
        resp.SerializeToString(&data);
        sendResponse(conn_id, 5002, data);

        // If reconnected and in battle, send snapshot
        if (result.error_code == 0) {
            auto* s = sessions_->getBySession(result.session_id);
            if (s && s->state == SessionState::IN_BATTLE && s->current_room > 0) {
                battle_mgr_->sendSnapshot(s->player_id, s->current_room);
            }
        }
    });
}

void GameServer::timerLoop() {
    while (running_) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.match_interval_ms));
        matcher_->tryMatch(0);
        sessions_->cleanupExpired();
    }
}

void GameServer::sendResponse(ConnectionId conn_id, uint32_t msg_id,
                               const std::string& payload) {
    auto data = FrameCodec::encode(msg_id, 0, 0, payload);

    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = conn_to_loop_.find(conn_id);
    if (it != conn_to_loop_.end()) {
        it->second->sendToConnection(conn_id, data);
    }
}

void GameServer::onConnectionAccepted(ConnectionId conn_id, EventLoop* loop) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    conn_to_loop_[conn_id] = loop;
}

}  // namespace miniarena
