#pragma once

#include <cstdint>
#include <cstddef>
#include <chrono>

namespace miniarena {

// Connection identifier
using ConnectionId = uint64_t;

// Time helpers
using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Milliseconds = std::chrono::milliseconds;

// Network constants
namespace net {
    constexpr size_t   kHeaderSize        = 18;
    constexpr size_t   kMaxPayload        = 64 * 1024;
    constexpr size_t   kMaxPacketSize     = kHeaderSize + kMaxPayload;
    constexpr size_t   kSendQueueMax      = 256 * 1024;
    constexpr uint32_t kMaxMsgRatePerSec  = 100;
    constexpr uint32_t kMaxIllegalCount   = 10;
    constexpr int      kListenBacklog     = 128;
    constexpr uint64_t kHeartbeatIntervalMs = 5000;
    constexpr uint64_t kConnectionTimeoutMs = 15000;
    constexpr uint64_t kTimerTickMs         = 10;
    constexpr size_t   kTimerSlots          = 6000;  // 60s / 10ms
    constexpr uint16_t kDefaultPort         = 9000;
    constexpr int      kDefaultIoThreads    = 4;
}  // namespace net

}  // namespace miniarena
