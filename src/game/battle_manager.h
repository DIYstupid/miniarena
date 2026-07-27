#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include "player.h"
#include "battle_player.h"
#include "room.h"
#include "command.h"
namespace miniarena {

class LogicWorker;
class SessionManager;
class RoomManager;
class MysqlClient;

struct SettlementEntry {
    PlayerId player_id;
    int32_t kills;
    int32_t deaths;
    int32_t damage_dealt;
    int32_t damage_taken;
    int32_t rank;
};

// Manages battle lifecycle: start, tick dispatch, end, settlement.
class BattleManager {
public:
    BattleManager(int logic_threads);

    // Room → battle: create BattlePlayers from room, assign to LogicWorker.
    void startBattle(Room* room, SessionManager* sessions);

    // Battle → settlement: rank players, write MySQL, notify.
    void endBattle(RoomId room_id, RoomManager* rooms,
                   SessionManager* sessions, MysqlClient* mysql);

    // IO → Logic: dispatch a command to the room's owning LogicWorker.
    void dispatchCommand(RoomId room_id, Command cmd);

    // P5: Player disconnect from battle
    void onPlayerDisconnect(PlayerId player_id, RoomId room_id);
    void sendSnapshot(PlayerId player_id, RoomId room_id);

    // Send callback wiring
    using SendCallback = std::function<void(PlayerId, uint32_t, const std::string&)>;
    void setSendCallback(SendCallback cb);
    [[nodiscard]] size_t activeBattles() const noexcept { return room_to_worker_.size(); }

private:
    LogicWorker* assignWorker(RoomId room_id);
    std::vector<SettlementEntry> computeSettlement(
        const std::unordered_map<PlayerId, BattlePlayer>& players);

    std::vector<std::unique_ptr<LogicWorker>> workers_;
    std::unordered_map<RoomId, LogicWorker*> room_to_worker_;
    SendCallback send_cb_;
};

}  // namespace miniarena
