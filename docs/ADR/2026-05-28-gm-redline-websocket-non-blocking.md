# GM 红线 — Polymarket WebSocket Event Loop 不阻塞

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Hard Redline（一票否决）
- **关联:** 用户 2026-05-28 指令、`docs/RESEARCH/laozhou-architecture-v0.2.md` §6/§8、`docs/RESEARCH/laoli-polymarket-api-spec-v1.md`

---

## 1. 用户原话

> "绝对不可以阻塞 polymarket 的 ws 事件."

## 2. 红线 R-12

**Polymarket WebSocket event loop 线程禁止任何同步 REST 调用 / 阻塞 IO / 锁等待 > 100us。** 违者 = P0。

具体：
- WebSocket event loop **独立线程**（CPU pinned，c6i.xlarge vCPU0 net-io 核）
- 收到 message → 解码 → 入 SPSC ring buffer → 立即返回继续读
- 任何 REST 调用、DB 写入、文件 IO、跨进程 IPC **必须走异步 worker pool**，不能 inline
- 锁等待禁止 > 100us（lock-free 优先，必要用 try_lock + degrade）

## 3. 与 API 调用复用的关联（用户原话第二条）

> "某些直播源一次周期调用是会覆盖某市场下所有的盘口的，并不需要每个盘口都去拉取外部 api"

落地原则：
- **bulk endpoint 优先**：Goalserve `/<sport>/inplay` 一次拉全联盟 → 一次调用覆盖 N 个市场 × N 个盘口
- **WebSocket 优先于 REST**：能 WebSocket subscribe 就不 polling
- **REST 仅做兜底补偿**：WebSocket 断或冷启动初始化
- **同时多个 strategy 要同一个 market data → single-flight**，只发一次实际调用

## 4. 新鲜度保证策略

| 数据类型 | 主路径 | 补偿路径 | TTL/Freshness 目标 |
|---|---|---|---|
| Polymarket orderbook | WebSocket market channel | REST `/book` 异步 worker 兜底（断线时） | < 100ms WebSocket staleness |
| Polymarket 成交 | WebSocket market channel | REST `/trades` 兜底 | < 200ms |
| Polymarket market list | gamma `/sports/events` 周期 60s | WebSocket 不覆盖 | 60s 内新鲜 |
| Goalserve inplay (scores / 球员) | REST polling 1-3s（无 WebSocket）| - | 3s 内新鲜 |
| Goalserve pregame odds | REST polling 30-60s | - | 60s 内新鲜 |
| Polygon RPC（gas / nonce）| WebSocket subscription 优先 | REST 兜底 | 实时 / 5s |

**周期轮询线程独立于 WebSocket event loop**（核 vCPU3 background），不互相阻塞。

## 5. 派单

- **老周 v0.3** → 架构加 §17 "异步调用 + WebSocket 不阻塞" 专章（含线程模型 + ring buffer + worker pool 拓扑图）
- **老李** → `api-call-optimization-v1` v1.1 加 "WebSocket 不阻塞 + single-flight" 设计要点
- **老陈** → WebSocket event loop 延迟基准（每条 message 处理 < 50us p99）
- **小宋** → 测试用例：模拟 REST 慢响应不影响 WebSocket event loop tick
- **老高** → PR 红线 R-12 加入代码规范 v1.1

## 6. 红线违例 = P0 事故

任何 PR 在 WebSocket event loop 线程发起同步 REST / blocking IO → 直接 reject + post-mortem。

---

**Decided by 老雷, 2026-05-28**
