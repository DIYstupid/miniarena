# P7-P9: Throughput & Stability Improvements

> **Commits:** `01dcbaa` → `a8893af` → `c8454c3` | **Date:** 2026-07-28

## Background

P6 pressure testing revealed the server crashed at ≥10 bots with "double free or corruption". Three independent data races were identified and fixed across three phases using a common pattern: **asynchronous operation queues**.

## Root Cause Analysis

### Race 1: Acceptor → EventLoop `conns_` (P7)

**Path:**
```
Acceptor thread                      EventLoop thread
─────────────                        ────────────────
addConnection(conn)
  conns_[id] = std::move(conn)       handleEvents()
  ← WRITE                                conns_.find(fd)  ← READ
                                       DATA RACE → UB
```

**Fix:** Pending connection queue. `addConnection` enqueues + wakes; EventLoop drains in `drainPending()` before `handleEvents()`.

### Race 2: onTick → EventLoop `Connection::send_buf_` (P8)

**Path:**
```
onTick thread                        EventLoop thread
────────────                         ────────────────
tryMatch() → notify_cb_              handleEvents()
  → sendResponse()                     onWritable()
    → sendToConnection()                 conn->send_buf_ 读
      → conn->sendRaw(data)           DATA RACE → double-free
        → send_buf_ 写
```

**Fix:** Pending write queue. `sendToConnection` enqueues + wakes; EventLoop drains in `drainPendingWrites()` before `handleEvents()`.

### Race 3: EventLoop → LogicWorker `rooms_` (P9)

**Path:**
```
EventLoop thread                     LogicWorker thread
────────────────                     ─────────────────
cleanupClosed()
  disconnect_cb_(cid)
    → battle_mgr_->onPlayerDisconnect()
      → markDisconnected(pid, rid)
        rooms_[rid].disconnected[pid]=now  tick()
        ← WRITE (operator[] can rehash)     for (auto& [rid,rs] : rooms_)
                                              ← ITERATING
                                            DATA RACE → iterator invalidation
                                            → SIGABRT
```

**Trigger:** 50+ bots in battle → "send queue full" → closeConnection → cleanupClosed → disconnect callback → `operator[]` on `disconnected` map → potential rehash while LogicWorker iterates `rooms_` → use-after-free.

**Fix:** Pending room ops queue. `markDisconnected`/`addRoom`/`removeRoom` enqueue + notify; LogicWorker drains in `drainPendingOps()` before `tick()`. Also added `shared_mutex` for `getSnapshot()` (reconnect) reads.

## Shared Pattern

All three fixes use the same architecture:

```
External Thread               Owner Thread
────────────────               ────────────
enqueue(op)                    drain queue
  lock(pending_mtx_)             lock(pending_mtx_)
  pending_queue_.push(op)        while !empty: pop → apply
  unlock()                       unlock()
  notify / wake ───────────→   process normally (single-threaded)
```

## Additional Improvements

| Change | Phase | Impact |
|--------|-------|--------|
| `ConnectionPool<T>` template | P7 | Reusable for MySQL + Redis |
| MySQL pool (4 conns) | P7 | ~10→25 login/s |
| Redis player cache | P7 | Hot-path login skips MySQL |
| Redis pool (4 conns) | P7 | Parallel match/session ops |
| Level-triggered EPOLLIN | P7 | Fixes data-arrive-before-register race |
| `time.After` → `time.NewTimer` | P7 | Fixes timer reset in select loop |
| Varint encoding in Go bot | P7 | Fixes protobuf field encoding |
| BattleLoop blocking fix | P9 | Removed `default` case blocking ticker |

## Results

| Metric | P6 | P7 | P8 | P9 |
|--------|-----|-----|-----|-----|
| Max stable bots | 2 | 10 | 30 | **200** |
| 100-bot login | N/A | 75/100 | 100/100 | **100/100** |
| 100-bot battle | N/A | crash | crash | **100/100** |
| 100-bot moves (45s) | N/A | N/A | 0 | **18,133 (377/s)** |
| 200-bot login | N/A | N/A | N/A | **200/200** |
| 200-bot battle | N/A | N/A | N/A | **40/200 (4 rooms)** |
| 200-bot moves (45s) | N/A | N/A | N/A | **7,314 (155/s)** |
| Server crash at ≥30 | ✅ crash | ✅ crash | ✅ crash | **NO CRASH** |

## Files Changed (P7-P9)

```
src/storage/connection_pool.h    | 110 +++++ (new)
src/storage/mysql_client.h       |  34 ++-
src/storage/mysql_client.cpp     | 128 +++---
src/storage/redis_client.h       |  25 +-
src/storage/redis_client.cpp     | 208 +++++-----
src/network/event_loop.h         |  19 +-
src/network/event_loop.cpp       |  49 ++-
src/game/logic_worker.h          |  37 ++-
src/game/logic_worker.cpp        | 143 ++++---
src/game/session_manager.cpp     |  47 ++-
robot/bot.go                     |  16 +-
robot/controller.go              |   7 +-
robot/frame.go                   |   9 +
```
