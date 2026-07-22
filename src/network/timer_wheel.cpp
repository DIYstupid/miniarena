#include "timer_wheel.h"

#include <stdexcept>

namespace miniarena {

TimerWheel::TimerWheel()
    : slots_(kSlots) {}

size_t TimerWheel::slotOf(uint64_t delay_ms) const {
    uint64_t ticks = (delay_ms + kTickMs - 1) / kTickMs;  // ceil division
    if (ticks == 0) ticks = 1;
    return (current_slot_ + ticks) % kSlots;
}

void TimerWheel::add(ConnectionId id, uint64_t timeout_ms, Callback cb) {
    remove(id);  // replace if exists

    size_t slot = slotOf(timeout_ms);
    slots_[slot].push_back({id, std::move(cb)});
    id_to_slot_[id] = slot;
    ++entries_;
}

void TimerWheel::remove(ConnectionId id) {
    auto it = id_to_slot_.find(id);
    if (it == id_to_slot_.end()) return;

    size_t slot = it->second;
    auto& bucket = slots_[slot];

    for (auto entry = bucket.begin(); entry != bucket.end(); ++entry) {
        if (entry->id == id) {
            bucket.erase(entry);
            break;
        }
    }

    id_to_slot_.erase(it);
    --entries_;
}

void TimerWheel::refresh(ConnectionId id, uint64_t timeout_ms) {
    auto it = id_to_slot_.find(id);
    if (it == id_to_slot_.end()) return;

    // Remove from old slot
    size_t old_slot = it->second;
    auto& bucket = slots_[old_slot];
    Callback cb;

    for (auto entry = bucket.begin(); entry != bucket.end(); ++entry) {
        if (entry->id == id) {
            cb = std::move(entry->callback);
            bucket.erase(entry);
            break;
        }
    }

    id_to_slot_.erase(it);
    --entries_;

    // Re-add at new slot
    size_t new_slot = slotOf(timeout_ms);
    slots_[new_slot].push_back({id, std::move(cb)});
    id_to_slot_[id] = new_slot;
    ++entries_;
}

std::vector<ConnectionId> TimerWheel::tick() {
    // Advance the clock
    current_slot_ = (current_slot_ + 1) % kSlots;

    // Collect all expired entries in current slot
    std::vector<ConnectionId> expired;
    auto& bucket = slots_[current_slot_];
    for (auto& entry : bucket) {
        expired.push_back(entry.id);
        if (entry.callback) {
            entry.callback(entry.id);
        }
        id_to_slot_.erase(entry.id);
    }

    entries_ -= bucket.size();
    bucket.clear();

    return expired;
}

}  // namespace miniarena
