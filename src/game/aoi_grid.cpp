#include "aoi_grid.h"
#include <cmath>

namespace miniarena {

AoiGrid::CellKey AoiGrid::cellOf(float x, float y) const {
    return {static_cast<int32_t>(std::floor(x / kCellSize)),
            static_cast<int32_t>(std::floor(y / kCellSize))};
}

void AoiGrid::update(PlayerId id, float x, float y) {
    CellKey new_cell = cellOf(x, y);
    auto it = player_cells_.find(id);

    if (it != player_cells_.end()) {
        if (it->second == new_cell) return;  // same cell, no change
        // Remove from old cell
        grid_[it->second].erase(id);
        if (grid_[it->second].empty()) {
            grid_.erase(it->second);
        }
    }

    grid_[new_cell].insert(id);
    player_cells_[id] = new_cell;
}

void AoiGrid::remove(PlayerId id) {
    auto it = player_cells_.find(id);
    if (it == player_cells_.end()) return;

    grid_[it->second].erase(id);
    if (grid_[it->second].empty()) {
        grid_.erase(it->second);
    }
    player_cells_.erase(it);
}

std::vector<PlayerId> AoiGrid::getNearby(PlayerId id) const {
    std::vector<PlayerId> result;
    auto it = player_cells_.find(id);
    if (it == player_cells_.end()) return result;

    CellKey center = it->second;

    // Check 9 cells: center + 8 neighbors
    for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dy = -1; dy <= 1; ++dy) {
            CellKey neighbor{center.cx + dx, center.cy + dy};
            auto cit = grid_.find(neighbor);
            if (cit != grid_.end()) {
                for (PlayerId pid : cit->second) {
                    result.push_back(pid);
                }
            }
        }
    }
    return result;
}

}  // namespace miniarena
