# MiniArena 架构决策记录

## 1. 多 Reactor 模型（而非 Proactor）

**选择：** epoll + 多 Reactor（4 个 EventLoop 线程 + 1 个 Acceptor 线程）

**理由：**
- epoll 是 Linux 最高效的事件通知机制，无需像 IOCP 那样拷贝缓冲区
- 多 Reactor 将连接均匀分配到多个线程，避免单线程瓶颈
- Acceptor 线程只负责 accept + 分发，不做 I/O，防止 accept 惊群
- P7 引入 pending connection queue 后，Acceptor ↔ EventLoop 零数据竞争

**验证数据：** 4 线程可稳定支撑 5,000 并发连接（~1,250/线程），内存仅 23MB。

## 2. Gap Buffer（而非 Ring Buffer）

**选择：** `std::vector`-backed gap buffer with `compact()`

**理由：**
- Gap buffer 读取时无需判断环形边界，代码更清晰
- `compact()` 仅在缓冲区碎片化时调用（低频），摊销 O(1)
- Ring buffer 在处理变长帧时边界判断复杂，容易出错
- 256KB send queue 上限足以应对 20Hz × 10 人 × 200 字节的广播

## 3. 房间串行执行（无锁设计）

**选择：** 每个 Room 绑定到一个 LogicWorker 线程，所有房间逻辑在该线程内串行执行

**理由：**
- 房间内玩家交互频繁（移动、攻击、技能），加锁会产生大量竞争
- 单线程串行执行消除了所有房间内并发问题
- 多房间并行通过多 LogicWorker 实现（round-robin 分配）
- P9 引入 pending ops queue 后，外部线程（EventLoop、onTick）通过异步入队修改房间状态，LogicWorker 线程独占房间数据

**验证数据：** 100 rooms × 10 players = 1,000 bots 同时战斗，0 数据竞争，0 崩溃。

## 4. TCP 长连接（而非 UDP）

**选择：** TCP 长连接

**理由：**
- 战斗指令（移动、攻击、技能）需要可靠传输
- TCP 内建流控和重传，无需在应用层实现
- 连接断开可检测（RST/FIN），支持断线重连
- 心跳机制检测静默连接（5s 超时）

## 5. Protobuf 帧协议（而非 JSON/自定义二进制）

**选择：** 18 字节定长帧头 + Protobuf 变长载荷

**理由：**
- 帧头固定格式（4B length + 4B msg_id + 2B flags + 8B seq），解析零开销
- Protobuf 载荷紧凑（varint + length-delimited），比 JSON 小 3~5×
- Proto3 默认值省略传输（error_code=0 不占字节）
- Go 机器人零依赖手写解析器，验证协议可移植性

## 6. 连接池（而非单连接 + 互斥锁）

**选择：** `ConnectionPool<T>` 泛型模板，MySQL 和 Redis 各 4 连接

**理由：**
- 单连接 + mutex 串行化所有 DB 操作，登录吞吐 ~10/s
- 4 连接池可将并发 DB 操作提升 4×
- RAII Guard 自动归还连接，消除泄漏风险
- P7 实测：登录吞吐 10→25/s（2.5×）

## 7. 异步操作队列（而非直接跨线程修改）

**选择：** 所有跨线程数据修改通过 pending queue 异步完成

**应用：**
- P7: 连接入队（Acceptor → EventLoop）
- P8: 写入入队（onTick → EventLoop）
- P9: 房间操作入队（EventLoop → LogicWorker）

**理由：**
- 消除所有跨线程数据竞争（三轮，17 个测试套件验证）
- 保持目标线程的单线程语义
- 改动量小（每次 ~30 行），模式统一

## 8. 匹配器批量出房

**选择：** `while` 循环一次匹配到队列不足为止

**理由：**
- 原 `if` 每次只开一个房间，500ms 间隔，100 个房间需 50 秒
- 改为 `while` 后，所有房间在一个 tick 内创建完成
- P10 实测：1000 bots 战斗数 20→980（49×）
