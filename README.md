# MiniArena — 多人实时战斗服务器

> C++20 · epoll 多 Reactor · Protobuf · 20Hz Tick · Go 压测机器人

一个面向游戏服务器岗位的个人项目。模拟多人实时竞技游戏的核心链路：长连接管理 → 登录/匹配 → Tick 驱动战斗 → 状态广播 → 断线重连 → 自动化压测。

---

## 性能总览

| 指标 | 数值 |
|---|---|
| 稳定并发连接 | **5,000** (500/s 登录, 100% 成功率, 23MB 内存) |
| 并发战斗房间 | **100** (1,000 名玩家同时战斗) |
| Tick 延迟 (P50) | **50μs** |
| Tick 延迟 (P99) | **400μs** |
| 消息失败率 | **0%** |
| 异常退出率 | **0** (10 分钟连续运行, 100 bots) |
| 测试套件 | **17/17** 通过 (GoogleTest + ASan + TSan) |

---

## 架构

```
                    ┌─────────────────┐
                    │   Go Bot Client  │  (压测机器人集群)
                    └────────┬────────┘
                             │ TCP (Protobuf 帧协议)
                             ▼
┌──────────────────────────────────────────────────────────┐
│                     Game Server (C++20)                   │
│                                                          │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌───────┐ │
│  │ Acceptor │   │ EventLoop│   │ EventLoop│   │  ...  │ │  epoll 多 Reactor
│  │ (accept) │──▶│   (IO)   │   │   (IO)   │   │       │ │  4 个 IO 线程
│  └──────────┘   └────┬─────┘   └────┬─────┘   └───────┘ │
│                      │              │                     │
│            ┌─────────┴──────────────┴──────────┐          │
│            │           MessageRouter            │          │
│            └─────────┬──────────────┬──────────┘          │
│                      │              │                     │
│         ┌────────────▼──┐  ┌───────▼──────────┐          │
│         │ SessionManager │  │   MatchManager    │          │
│         │ (登录/会话)     │  │ (匹配队列/房间)   │          │
│         └───────┬────────┘  └───────┬──────────┘          │
│                 │                   │                     │
│         ┌───────▼───────────────────▼──────────┐          │
│         │            BattleManager              │          │
│         │  ┌────────────┐  ┌────────────┐      │          │
│         │  │LogicWorker 0│  │LogicWorker 1│     │  20Hz Tick
│         │  │ rooms_[1..N]│  │rooms_[N+1..]│     │  房间串行
│         │  └────────────┘  └────────────┘      │          │
│         └──────────────────────────────────────┘          │
└──────────────────────────────────────────────────────────┘
         │                                        │
         ▼                                        ▼
   ┌─────────┐                            ┌──────────┐
   │  MySQL  │  (玩家数据持久化)            │  Redis   │  (会话/匹配队列缓存)
   └─────────┘                            └──────────┘
```

## 数据流

```
Bot 发送 MoveRequest ──TCP──▶ EventLoop::handleRead()
                                    │
                              FrameCodec::decode()  ← 18B 定长帧头 + Protobuf 变长载荷
                                    │
                              MessageRouter::dispatch(msg_id=4001)
                                    │
                              pushCommand(room_id, cmd)
                                    │
                    ┌───────────────┘
                    ▼
              LogicWorker::tick()  ← 20Hz
                    │
              TickEngine::processTick()  ← 移动/攻击/技能/伤害/冷却
                    │
              Broadcaster::broadcastToAoi()  ← AOI 九宫格过滤
                    │
              sendResponse(player_id, 4004, state)
                    │
              EventLoop::sendToConnection()  ← 异步写入队列 (P8)
                    │
              TCP ──▶ Bot 接收 BattleStateNotify
```

---

## 核心设计决策

### 1. 为什么多 Reactor 而不是 Proactor？

**选择：** epoll + 4 个 EventLoop 线程 + 1 个 Acceptor 线程

**思考：** epoll 是 Linux 上最高效的事件通知机制。Proactor (IOCP) 的优势在于 Windows 平台，Linux 的 `io_uring` 生态还不够成熟。多 Reactor 将连接均匀绑定到不同线程，避免单线程成为瓶颈。Acceptor 线程只做 `accept()` + 分发，不参与 I/O，防止惊群。

**验证：** 4 个 IO 线程可稳定承载 5,000 并发连接（~1,250/线程），单线程阻塞不影响其他线程。

### 2. 为什么 Gap Buffer 而不是 Ring Buffer？

**选择：** `std::vector`-backed gap buffer，`compact()` 碎片整理

**思考：** 游戏服务器的消息以变长帧为主（Protobuf 序列化后大小不固定）。Ring buffer 处理变长消息时需要频繁判断环形边界（`wrap-around`），代码容易出错。Gap buffer 的线性地址空间使帧解析更直观。`compact()` 仅在 buffer 碎片化时调用一次，摊销 O(1)。

### 3. 为什么房间串行执行（无锁）？

**选择：** 每个 Room 绑定到一个 LogicWorker 线程，所有房间逻辑在该线程内执行

**思考：** 游戏服务器最大的并发挑战不是"多线程跑得快"，而是"状态一致性的维护成本"。房间内玩家频繁交互（移动、攻击、技能），如果加锁，每次状态读写都有竞争开销，且容易死锁。单线程串行执行天然消除所有房间内竞争，代码简洁可靠。

但单线程如何利用多核？通过多个 LogicWorker 线程实现房间级并行——不同房间由不同 Worker 处理，Worker 之间零共享状态。Round-robin 分配保证负载均衡。

**验证：** 1,000 bots × 100 房间 × 2 Worker 线程，零数据竞争，P50 Tick 延迟 50μs。

### 4. 为什么 Protobuf 而不是 JSON 或自定义二进制？

**选择：** 18 字节定长帧头（`total_length + msg_id + flags + seq`）+ Protobuf 变长载荷

**思考：** JSON 可读性好但序列化体积大（~3-5×），不适合高频状态同步。完全自定义二进制协议虽然最紧凑，但缺乏跨语言支持和可扩展性。Protobuf 提供紧凑的 varint 编码、向后兼容的 Schema 演化，以及多语言代码生成。

额外加分项：Go 机器人客户端**零依赖**手写了 Protobuf varint 解析器——证明协议是可移植的，不依赖任何特定 SDK。

### 5. 为什么连接池而不是单连接 + 互斥锁？

**选择：** 泛型 `ConnectionPool<T>` 模板，MySQL 和 Redis 各 4 连接

**思考：** 单连接 + mutex 是最简单的实现，但所有 DB 操作串行化。压测发现登录吞吐仅 ~10/s，瓶颈正是 MySQL 的 `getPlayer()` 调用。改为 4 连接池后提升至 ~25/s。

RAII Guard 模式 (`pool.borrow()` 返回一个自动归还的句柄) 消除了连接泄漏风险。泛型模板使 MySQL 和 Redis 共享同一套池逻辑，避免重复代码。

### 6. 为什么异步操作队列而不是直接跨线程修改？

**选择：** 所有跨线程数据修改通过 pending queue 异步完成

**思考：** 这是本项目最有价值的工程发现。最初的设计允许不同线程直接修改共享容器（`conns_`、`send_buf_`、`rooms_`），压测时频繁崩溃（double-free）。问题不是"偶尔的竞争"，而是**只要有一定并发度就必然触发**——`unordered_map::operator[]` 可能触发 rehash，而另一个线程正在迭代同一容器。

修复方案统一：所有外部修改改为异步入队，目标线程在自己的循环中排空队列。这个模式实现简单（每次 ~30 行）、效果立竿见影、且语义清晰——每个线程对自己的数据拥有独占访问权。

**数据对比：**

| | 修复前 | 修复后 |
|---|---|---|
| 2 bot | ✅ | ✅ |
| 30 bot | ❌ 崩溃 (double-free) | ✅ |
| 100 bot | ❌ 崩溃 | ✅ |
| 200 bot | N/A | ✅ |
| 1,000 bot | N/A | ✅ |

---

## 压力测试方法论

压测不是"启动 1000 个连接看看挂不挂"，而是一个闭环的定位→修复→验证过程：

```
1. 小规模验证 (2 bots)    → 确认功能正确性
2. 中等规模 (30 bots)     → 暴露数据竞争
3. ASan/TSan 复现         → 定位具体竞争点
4. 修复 (异步队列)         → 改一行, 测一轮
5. 大规模验证 (100-1000)   → 确认修复有效
6. 长时间运行 (10 min)     → 确认无内存泄漏/性能衰减
```

每一轮修复都有"修复前 vs 修复后"的数据对比，证明改动有效。

### 压测演进

| Phase | 瓶颈 | 方案 | 最大稳定 bots |
|---|---|---|---|
| P6 | — | 基线 | 2 |
| P7 | MySQL/Redis 单连接, Acceptor 竞争 | 连接池 + 待处理队列 | 30 |
| P8 | sendToConnection 跨线程写 | 异步写队列 | 30 (稳定) |
| P9 | LogicWorker rooms_ 竞争 | 房间操作异步队列 | 200 |
| P10 | 匹配器串行出房 | 批量匹配 `while` | 1,000 |
| P11 | 线程启动 + 埋点 | 修复 + Tick 统计 | 1,000 |

---

## 构建与运行

```bash
# 构建 (Release)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 运行服务器
./build/src/main/miniarena_server

# 运行测试
cd build && ctest -j4

# 运行压测 (Go 机器人)
cd robot
go run . -addr 127.0.0.1:9000 -bots 100 -rate 50 -duration 30
```

---

## 项目结构

```
miniarena/
├── CMakeLists.txt
├── proto/messages.proto          # 协议定义 (登录/匹配/战斗/重连)
├── src/
│   ├── main/main.cpp             # 入口, 配置, spdlog
│   ├── network/                  # 网络层
│   │   ├── event_loop.{h,cpp}    # epoll 事件循环 (P7/P8 异步队列)
│   │   ├── connection.{h,cpp}    # Gap Buffer + 帧编解码
│   │   ├── acceptor.{h,cpp}      # TCP accept + fd 分发
│   │   ├── socket.{h,cpp}        # RAII socket + TCP_NODELAY
│   │   ├── buffer.{h,cpp}        # Gap Buffer 实现
│   │   ├── frame_codec.{h,cpp}   # 18B 帧头编解码
│   │   └── timer_wheel.{h,cpp}   # 分层时间轮 (心跳/超时)
│   ├── game/                     # 业务层
│   │   ├── game_server.{h,cpp}   # 服务器主类, 回调注册
│   │   ├── session_manager.{h,cpp} # 登录/会话 (P7 Redis 缓存)
│   │   ├── match_manager.{h,cpp} # 匹配队列 (P10 批量出房)
│   │   ├── room.{h,cpp}          # 房间状态机
│   │   ├── logic_worker.{h,cpp}  # 20Hz Tick 循环 (P9 异步操作)
│   │   ├── battle_manager.{h,cpp}# 战斗管理器
│   │   ├── tick_engine.{h,cpp}   # Tick 计算引擎
│   │   ├── aoi_grid.{h,cpp}      # AOI 九宫格
│   │   ├── broadcaster.{h,cpp}   # 广播器
│   │   └── command.{h,cpp}       # 命令队列
│   └── storage/                  # 存储层
│       ├── connection_pool.h     # 泛型连接池 (P7)
│       ├── mysql_client.{h,cpp}  # MySQL 封装 (连接池)
│       └── redis_client.{h,cpp}  # Redis 封装 (连接池 + 玩家缓存)
├── robot/                        # Go 压测机器人
│   ├── bot.go                    # Bot 生命周期 (登录→匹配→战斗)
│   ├── frame.go                  # 手写 Protobuf 帧解析器
│   ├── controller.go             # 压测控制器 (速率/统计/报告)
│   └── ptest.go                  # 压测入口 (连接/战斗/吞吐/稳定性)
├── tests/                        # 17 个 GoogleTest 测试套件
└── docs/                         # 架构决策 + 压测报告
```

---

## 技术栈

| 层 | 技术 | 说明 |
|---|---|---|
| 语言 | C++20, Go 1.22 | 服务器 C++, 机器人 Go |
| 构建 | CMake | Debug/Release/ASan/TSan |
| 网络 | epoll, TCP | 多 Reactor, 水平触发 |
| 协议 | Protobuf | 18B 帧头 + varint 载荷 |
| 日志 | spdlog | 异步, 分级 |
| 测试 | GoogleTest | 17 套, 含 ASan/TSan |
| 存储 | MySQL 8.0, Redis 7.0 | 连接池 (4 连接) |
| 并发 | std::thread, std::mutex, std::condition_variable | 异步操作队列模式 |
