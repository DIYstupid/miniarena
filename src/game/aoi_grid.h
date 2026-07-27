#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "player.h"

namespace miniarena {

// Nine-grid AOI (Area of Interest).
// Divides the world into 200px cells, tracks which player is in each cell.
// Querying a player returns all players in their cell + 8 neighbors.
class AoiGrid {
public:
    static constexpr float kCellSize = 200.0f;

    void update(PlayerId id, float x, float y);
    void remove(PlayerId id);

    // Get all players in the same or adjacent cells as `id`.
    [[nodiscard]] std::vector<PlayerId> getNearby(PlayerId id) const;

    [[nodiscard]] size_t playerCount() const noexcept { return player_cells_.size(); }

private:
    struct CellKey {
        int32_t cx, cy;
        bool operator==(const CellKey& o) const {
            return cx == o.cx && cy == o.cy;
        }
    };
    struct CellKeyHash {
        size_t operator()(CellKey k) const {
            return std::hash<int64_t>{}(
                (static_cast<int64_t>(k.cx) << 32) ^ static_cast<int64_t>(k.cy));
        }
    };

    CellKey cellOf(float x, float y) const;

    std::unordered_map<CellKey, std::unordered_set<PlayerId>, CellKeyHash> grid_;
    std::unordered_map<PlayerId, CellKey> player_cells_;
};

}  // namespace miniarena
