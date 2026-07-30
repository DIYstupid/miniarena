#include "battle_manager.h"
#include "logic_worker.h"
#include "session_manager.h"
#include "room_manager.h"
#include "storage/mysql_client.h"
#include "messages.pb.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <thread>

namespace miniarena {

BattleManager::BattleManager(int logic_threads) {
    for (int i = 0; i < logic_threads; ++i) {
        auto w = std::make_unique<LogicWorker>(i);
        workers_.push_back(std::move(w));
    }
}

LogicWorker* BattleManager::assignWorker(RoomId room_id) {
    size_t idx = room_id % workers_.size();
    return workers_[idx].get();
}

void BattleManager::setSendCallback(SendCallback cb) {
    send_cb_ = std::move(cb);
    for (auto& w : workers_) {
        w->setSendCallback(send_cb_);
    }
}

void BattleManager::startBattle(Room* room, SessionManager* sessions) {
    RoomId rid = room->id();

    // Build BattlePlayers from room's players
    std::unordered_map<PlayerId, BattlePlayer> bp_map;
    for (auto& [pid, info] : room->players()) {
        BattlePlayer bp;
        bp.id       = pid;
        bp.username = info.username;
        // Spread players in a small area
        bp.pos_x = static_cast<float>(bp_map.size() * 50.0f);
        bp.pos_y = 0.0f;
        bp_map[pid] = bp;
    }

    // Assign to LogicWorker
    auto* worker = assignWorker(rid);
    worker->addRoom(room, std::move(bp_map));
    room_to_worker_[rid] = worker;

    // Start worker thread if not running (re-check after drain)
    for (auto& w : workers_) {
        if (w->roomCount() == 0) w->drainPendingOps();  // apply pending adds
        if (w->roomCount() >= 1) {
            static std::once_flag flags[16];
            std::call_once(flags[w->id()], [&w = w]() {
                std::thread([&w = w]() { w->run(); }).detach();
            });
        }
    }
    // Update session states
    for (auto& [pid, info] : room->players()) {
        auto* s = sessions->getByPlayer(pid);
        if (s) {
            sessions->setState(s->session_id, SessionState::IN_BATTLE);
        }
    }

    spdlog::info("Battle started: room {} on LogicWorker {}, {} players",
                 rid, worker->id(), bp_map.size());
}

void BattleManager::endBattle(RoomId room_id, RoomManager* rooms,
                               SessionManager* sessions, MysqlClient* mysql) {
    auto it = room_to_worker_.find(room_id);
    if (it == room_to_worker_.end()) return;

    auto* room = rooms->getRoom(room_id);
    if (!room) return;

    room->endBattle();

    auto* worker = it->second;
    // The battle state is inside the worker; we need access to it.
    // For now, mark battle as over and notify players.
    // Full settlement with stats requires exposing battle state from LogicWorker.

    // Send BattleEndNotify
    miniarena::BattleEndNotify end_notify;
    end_notify.set_room_id(room_id);
    end_notify.set_duration_sec(60);  // placeholder
    std::string end_data;
    end_notify.SerializeToString(&end_data);

    for (auto& [pid, info] : room->players()) {
        if (send_cb_) {
            send_cb_(pid, 6001, end_data);
        }

        // Settlement notify (simplified)
        miniarena::BattleSettlementNotify settle;
        settle.set_player_id(pid);
        settle.set_kills(0);
        settle.set_deaths(0);
        settle.set_damage_dealt(0);
        settle.set_damage_taken(0);
        settle.set_rank(1);
        std::string settle_data;
        settle.SerializeToString(&settle_data);

        if (send_cb_) {
            send_cb_(pid, 6002, settle_data);
        }

        // Update session
        auto* s = sessions->getByPlayer(pid);
        if (s) {
            sessions->setState(s->session_id, SessionState::IN_LOBBY);
            sessions->setCurrentRoom(s->session_id, 0);
        }

        // Async write to MySQL
        if (mysql) {
            mysql->saveBattleResult(pid, room_id, 0, 0, 0, 0, 1);
        }
    }

    worker->removeRoom(room_id);
    room_to_worker_.erase(it);
    spdlog::info("Battle ended: room {}", room_id);
}

void BattleManager::dispatchCommand(RoomId room_id, Command cmd) {
    auto it = room_to_worker_.find(room_id);
    if (it == room_to_worker_.end()) return;
    it->second->pushCommand(std::move(cmd));
}

void BattleManager::onPlayerDisconnect(PlayerId player_id, RoomId room_id) {
    auto it = room_to_worker_.find(room_id);
    if (it == room_to_worker_.end()) return;
    it->second->markDisconnected(player_id, room_id);
    spdlog::info("Battle: player {} disconnected from room {}", player_id, room_id);
}

void BattleManager::sendSnapshot(PlayerId player_id, RoomId room_id) {
    auto it = room_to_worker_.find(room_id);
    if (it == room_to_worker_.end()) return;

    std::string snapshot = it->second->getSnapshot(room_id);
    if (snapshot.empty()) return;

    if (send_cb_) {
        send_cb_(player_id, 5003, snapshot);
    }
    spdlog::info("Battle: sent snapshot to player {} for room {}", player_id, room_id);
}

std::vector<SettlementEntry> BattleManager::computeSettlement(
    const std::unordered_map<PlayerId, BattlePlayer>& players) {
    std::vector<SettlementEntry> entries;
    for (auto& [pid, bp] : players) {
        entries.push_back({pid, bp.kills, bp.deaths,
                          bp.damage_dealt, bp.damage_taken, 0});
    }
    // Rank by kills descending
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.kills > b.kills; });
    for (size_t i = 0; i < entries.size(); ++i) {
        entries[i].rank = static_cast<int32_t>(i + 1);
    }
    return entries;
}

}  // namespace miniarena
