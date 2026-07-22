#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "network_types.h"

namespace miniarena {

// A simple timing wheel for connection heartbeats and timeouts.
// Precision: 10ms per tick. Coverage: 60 seconds (6000 slots).
//
// Thread-safety: NOT thread-safe. Must be used from a single thread
// (the owning EventLoop).
class TimerWheel {
public:
    using Callback = std::function<void(ConnectionId id)>;

    static constexpr uint64_t kTickMs = net::kTimerTickMs;
    static constexpr size_t   kSlots  = net::kTimerSlots;

    TimerWheel();

    // Add or replace a timer for conn_id. Fires after timeout_ms.
    void add(ConnectionId id, uint64_t timeout_ms, Callback cb);

    // Remove a timer. Safe to call with unknown id.
    void remove(ConnectionId id);

    // Reset the timer for an existing id without changing the callback.
    void refresh(ConnectionId id, uint64_t timeout_ms);

    // Advance by one tick. Returns list of expired connection ids.
    std::vector<ConnectionId> tick();

    // Number of active timers.
    [[nodiscard]] size_t size() const noexcept { return entries_; }

private:
    struct Entry {
        ConnectionId id;
        Callback     callback;
    };

    size_t slotOf(uint64_t delay_ms) const;

    std::vector<std::vector<Entry>> slots_;
    size_t                          current_slot_ = 0;
    std::unordered_map<ConnectionId, size_t> id_to_slot_;
    size_t                          entries_ = 0;
};

}  // namespace miniarena
