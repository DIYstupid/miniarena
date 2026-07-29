# P7: Throughput Improvements

> **Commit:** `01dcbaa` | **Date:** 2026-07-27

## Problem

Pressure testing revealed three bottlenecks limiting throughput:

| # | Bottleneck | Symptom |
|---|-----------|---------|
| 1 | Cross-thread data race in `addConnection` | Acceptor thread writes `conns_`/`fd_to_id_` while EventLoop reads them — UB, frames silently lost |
| 2 | MySQL single connection + `recursive_mutex` | ~10 login/s, bots stuck in `doLogin()` |
| 3 | Redis single connection + `std::mutex` | All Redis ops serialized, match queue bottleneck |

## Solution

### 1. Pending Connection Queue

**Before:**
```
Acceptor thread ──→ loop->addConnection(conn) ──→ modifies conns_, fd_to_id_, timer_wheel_
EventLoop thread ──→ handleEvents() ──→ reads conns_, fd_to_id_, timer_wheel_  ← DATA RACE
```

**After:**
```
Acceptor thread           EventLoop thread
─────────────────         ─────────────────
conn = accept()
lock(pending_mtx_)
pending_conns_.push(conn)
unlock(pending_mtx_)
wake() ───────────────→   epoll_wait returns
                            drainPending():
                              lock → pop all → unlock
                              for each: addConnectionImpl()  ← single-threaded
```

- `addConnection` now only enqueues + wakes
- `addConnectionImpl` (private) does the actual fd registration — runs in EventLoop thread only
- `drainPending()` called at top of each `run()` iteration
- Also switched `addConnectionImpl` from `EPOLLET` to `EPOLLIN` (level-triggered) — eliminates race where data arrives before epoll registration

**Files:** `src/network/event_loop.h` (+10), `src/network/event_loop.cpp` (+20)

### 2. ConnectionPool<T> Template

Generic RAII connection pool with borrow/return semantics:

```
ConnectionPool<T>
├─ std::queue<T> pool_
├─ std::mutex mtx_ + std::condition_variable cv_
├─ borrow() → Guard (RAII, auto-returns on destruction)
└─ Guard: operator* → T, get() → T
```

Used by both MySQL and Redis. Pool size configurable (default 4).

**File:** `src/storage/connection_pool.h` (new, 110 lines)

### 3. MySQL Connection Pool + Redis Login Cache

**MySQL pool:**
- Replaced single `MYSQL*` + `recursive_mutex` with `ConnectionPool<MYSQL*>` (4 connections)
- Each method borrows a connection via RAII guard
- Removed `recursive_mutex` entirely — thread safety from pool

**Redis login cache:**
- `RedisClient::cachePlayer(username, pid)` / `getCachedPlayer(username)`
- Stores `player:<username> → player_id` mapping in Redis
- `SessionManager::login()` checks Redis cache BEFORE MySQL `getPlayer()`
- On cache miss, populates cache after MySQL lookup
- On new player registration, caches immediately

**Files:** `src/storage/mysql_client.{h,cpp}`, `src/storage/redis_client.{h,cpp}`, `src/game/session_manager.cpp`

### 4. Redis Connection Pool

- Replaced single `redisContext*` + per-method `std::mutex` with `ConnectionPool<redisContext*>` (4 connections)
- Removed all per-method `std::lock_guard<std::mutex> lock(mtx_)` calls
- Each method borrows → uses → returns via RAII guard

**Files:** `src/storage/redis_client.{h,cpp}`

## Results

| Metric | Before (P6) | After (P7) |
|--------|------------|------------|
| Login throughput | ~10/s | ~25/s (2.5×) |
| 2-bot full flow | ✅ | ✅ |
| 30-bot login | 13/30 | 30/30 ✅ |
| 30-bot battle | 12 started | 20 started |
| 30-bot moves | N/A | 576 moves, 58/s |
| No crash at ≤30 bots | ❌ (double-free) | ✅ |

## Known Issue → P8

`sendToConnection()` is called from the **onTick thread** (via `sendResponse()` → `notify_cb_` → match/battle dispatch) and directly modifies `Connection::send_buf_`. The **EventLoop thread** simultaneously reads/writes `send_buf_` in `onWritable()`. This is a data race on the per-connection buffer.

**Fix (P8):** Per-connection `std::mutex` protecting `send_buf_` access, or a pending-write queue in EventLoop.

## Files Changed

```
src/storage/connection_pool.h   | 110 +++++ (new)
src/storage/mysql_client.h      |  34 ++-
src/storage/mysql_client.cpp    | 128 +++---
src/storage/redis_client.h      |  25 +-
src/storage/redis_client.cpp    | 208 +++++-----
src/network/event_loop.h        |  13 +-
src/network/event_loop.cpp      |  24 +-
src/game/session_manager.cpp    |  47 ++-
```
