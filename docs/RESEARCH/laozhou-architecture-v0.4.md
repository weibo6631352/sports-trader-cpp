# 系统架构 v0.4 (Sprint-2 W1 整合, 上链 deferred + Paper mock + WebSocket 中间方案)

- Owner: 老周 (cpp-chief-architect)
- Date: 2026-06-15 起草 (Sprint-2 W1)
- 验收人: 老郭 (架构评审, ADR-003 deadline W1 末 6/19)
- 关联 (Sprint-1 Retro 后):
  - `docs/RESEARCH/laozhou-architecture-v0.3.md` (v0.3 主体保留, **本文增量**, 仅修订 §15/§17 + 新增 §18/§19/§20)
  - `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (ADR-001 升 Accepted final)
  - `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md` (18 决议, **D-05 / D-06 / D-07 / D-12 / D-18 直接落本文**)
  - `docs/ADR/2026-05-28-gm-decision-defer-onchain-until-profitable.md` (**上链 deferred → §18 必做**)
  - `docs/ADR/2026-05-28-gm-signoff-paper-trade.md` (ExecutionMode 三态 + R-11 红线)
  - `docs/ADR/2026-05-28-gm-redline-websocket-non-blocking.md` (R-12)
  - `docs/ADR/2026-05-28-gm-policy-cross-domain-listening.md` (听取义务 + 双向收口 → §20)
  - `docs/ADR/2026-05-28-gm-policy-api-monitoring-longterm.md` (API 长期监控)
  - `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md` (地域合规暂不纠缠)
  - `docs/MEETINGS/sprint1-retro/laoli-speech.md` (4-5 conn 中间方案)
  - `docs/MEETINGS/sprint1-retro/laoye-speech.md` (Polymarket 2 conn + Polygon 1 conn × 3 sub)
  - `docs/MEETINGS/sprint1-retro/xiaoyuan-speech.md` (5 档 STALE 含 INPLAY_HOT_CRIT)
  - `docs/RESEARCH/xiaodeng-ml-roadmap-v2.md` (ML shadow signal Wave 14 → §19)
  - `docs/RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md` (PaperSigner / VirtualMatcher 参考)
- 状态: v0.4 在 v0.3 基础上增量, v0.3 主体仍然有效. **本文 = v0.3 + §15/§17 patch + 新章 §18/§19/§20**

---

## 0. v0.3 → v0.4 变更摘要

### 0.1 一句话版

GM 老雷 2026-05-28 同日下 6 条新决议改写架构边界: **(1) 上链 deferred, paper trading 证明盈利后再考虑** → §18 新章 mock 接口; **(2) WebSocket 拓扑收口为 4-5 conn 中间方案** (老李 + 老叶 + 老郭 三方协调结果) → §17.1.1 改写; **(3) vCPU3 内部 nice 优先级** (fsync > 周期轮询) → §15.6 修订; **(4) ML shadow signal 集成点** (小邓 Wave 14) → §19 新章; **(5) STALE 5 档含 INPLAY_HOT_CRIT** (小袁 D-06) → §17.6 修订; **(6) 听取义务 + 双向收口** 整合 → §20 新章 "跨域 review 清单".

### 0.2 与 v0.3 关键 diff 一览

| 模块 | v0.3 | v0.4 | 触发源 |
|---|---|---|---|
| §15.6 vCPU 映射 | vCPU0 跑 T0/T1 单 reactor 各 1 conn | vCPU0 跑 T0 (Polymarket 4 conn 单 reactor) + T1 (Polygon 1 conn × 3 sub) | D-05 + D-07 + 老李/老叶发言 |
| §15.6 vCPU3 优先级 | SCHED_OTHER nice 0 一刀切 | T8/T9 fsync nice=-5 (高); T5/T6/T7 周期 nice=0 (默认); T10/T11 nice=+5 (低) | 老郭裁定 + 老叶 §5.1 |
| §17.1.1 线程清单 | T0 1 conn, T1 1 conn (合 vCPU0) | T0a market_hot / T0b market_cold / T0c user + T1 polygon = 4 conn 单 reactor (vCPU0) | D-05 + D-07 老李精细化 |
| §17.6 STALE 阈值 | 单档 30s | 5 档: INPLAY_HOT_CRIT 200ms/800ms / INPLAY_HOT 500ms/2000ms / PREGAME_NEAR / PREGAME_FAR / OUTRIGHT | D-06 + 小袁实测 n=23 |
| §17.6 watchdog | 单一 30s 无 frame 阈值 | Polymarket 30s + Polygon 6s (3 块无 newHead) + per-token 5×T_half DEGRADED | 老叶 §1.5 + 小袁 §4.1 |
| §18 (新) | — | ExecutionMode.Paper mock 接口 (PaperSigner / VirtualNonce / VirtualGas / VirtualConfirm) | 上链 deferred ADR |
| §19 (新) | — | IMLSignalEngine / IShadowSignalSink / IModelRegistry + ML-R1~R8 红线落地点 | xiaodeng-ml-roadmap-v2 |
| §20 (新) | — | 跨域 review 清单 (本架构修改触发哪些人必须 confirm 才能 merge) | gm-policy-cross-domain-listening |

### 0.3 与 v0.2 / v0.3 的兼容性

- **v0.2 §2 5 层分层 / §3 数据流 / §4 依赖图 不变**
- **v0.3 §17 主体保留** (R-12 红线 + ring 拓扑 + single-flight + bulk endpoint 路由 + 红线 enforcement) — 本文仅在 §17.1.1 改 4 conn 拓扑、§17.6 改 STALE 5 档, 其他不动
- **v0.2 §14 SAFE_MODE / §13 部署假设 不变**
- **v0.2 §16 ExecutionMode 三态**: 本文 §18 把 Paper 三个 stub 接口 (virtual_nonce / virtual_gas / virtual_confirm) 落到代码层 spec, Live 接口在阶段 3 (M5+) 落地

---

## 1 ~ 14. 沿用 v0.3 + v0.2 (本文不重复)

本文只列**修订**与**新增**的小节, 完整主体见 `laozhou-architecture-v0.3.md` 与 `laozhou-architecture-v0.2.md`.

---

## 15.6 vCPU 映射 — v0.4 修订 (替代 v0.3 §15.6)

### 15.6.1 核分配 (替代 v0.3 §15.6 表)

```
vCPU 0: WebSocket event loop (Polymarket 4 conn 单 reactor + Polygon 1 conn × 3 sub)
        - T0a market_hot_reactor (1 conn, ~500 hot token, 临场 ±10min)
        - T0b market_cold_reactor (1-2 conn, ~3500 cold token, pregame_far + outright)
        - T0c user_reactor (1 conn, user channel) — 故障域隔离硬性
        - T1  polygon_reactor (1 conn × 3 sub: newHeads + USDC.e Transfer + 三合约 logs OR-filter)
        - 总计 4-5 conn × asio coroutine, 单 reactor 复用, simdjson on-demand 各栈独立 parser
        - 不发 REST, 不打 mutex > 100us, 不 fsync (R-12 红线)

vCPU 1: book builder + matching engine + feature pipeline (热路径)
        - T2 book_builder 消费 wss_in_ring_market_hot / cold / user 三 ring
        - 注: feature pipeline 即 BR-1, ML 阶段 0 复用 (§19.4)

vCPU 2: strategy + risk + exec (热路径)
        - T3 strategy_engine + RiskGateway::evaluate (inline) + signer IPC send
        - PaperSigner 与 RealSigner 共用 vCPU2 (二者通过 IPC fork, 不进 trader 主进程)

vCPU 3: background pool (内部 nice 分级, v0.4 新)
        - T4 bg_rest_worker_pool (3-4 thread, REST/HTTP 异步)
        - T5/T6/T7 周期轮询线程 (gamma 60s / Goalserve inplay 1-3s / Goalserve pregame 30-60s)
        - T8/T9 audit_fsync / exec_fsync (老韩 RM 群提交)
        - T10/T11 metrics_exporter / config_watcher
```

### 15.6.2 vCPU3 内部 nice 优先级 (v0.4 新, 老郭裁定 + 老叶 §5.1 补充)

| 线程 | vCPU | nice | 理由 | Owner |
|---|---|---|---|---|
| T8 audit_fsync | 3 | **-5 (高)** | RM 同步路径背压, 群提交不能等周期任务 | 老韩 + 老周 |
| T9 exec_fsync | 3 | **-5 (高)** | settlement / position WAL 路径背压 | 老韩 + 老周 |
| T4 bg_rest_worker_pool | 3 | 0 (默认) | REST 异步, 可容忍调度抖动 | 老周 |
| T5 periodic_gamma_poller | 3 | 0 | 60s 周期, 抖动 1-2s 可接受 | 小段 |
| T6 periodic_goalserve_inplay | 3 | 0 | 1-3s 周期, 已是次秒级 | 小段 |
| T7 periodic_goalserve_pregame | 3 | 0 | 30-60s, 容忍抖动 | 小段 |
| T11 config_watcher | 3 | **+5 (低)** | inotify, 完全可异步 | 老周 |
| T10 metrics_exporter | 3 | **+5 (低)** | Prometheus pull, 数秒抖动可接受 | 小郑 |

**底层逻辑**: vCPU3 内部 fsync 决定 RM evaluate → audit 闭合的关键路径; 周期轮询晚 1s 不影响 hot path. 早 v0.3 一刀切 nice=0 → fsync 与周期轮询竞争 → 老韩 audit 群提交 p99 抖动. **v0.4 明示分三档.**

### 15.6.3 pinning + sched (替代 v0.3 §15.6 末表)

| vCPU | 内容 | pin | sched | RT prio | nice | 网卡 IRQ |
|---|---|---|---|---|---|---|
| 0 | WebSocket event loop (4-5 conn 单 reactor, T0a/T0b/T0c + T1) | pthread_setaffinity_np | SCHED_FIFO | 60 | — | 网卡 RX IRQ pin vCPU0 |
| 1 | book + match + feature | 同上 | SCHED_FIFO | 55 | — | — |
| 2 | strategy + risk + exec | 同上 | SCHED_FIFO | 55 | — | — |
| 3 | bg (REST + 周期 + WAL fsync + metrics) | 同上 | SCHED_OTHER | — | -5/0/+5 (见 §15.6.2) | — |

**c6i.xlarge 4 物理核映射不变** (v0.3 §15.6 已定, 与 ADR-001 F-3 老郭裁定一致).

### 15.6.4 不变量 (v0.4 加强)

1. vCPU0 总线程数 = 4-5 个 reactor coroutine (asio io_context 共享), 不开新 std::thread
2. 各 connection 独立 simdjson::ondemand::parser (栈对象, 不抢全局)
3. 各 connection 独立 SPSC ring (wss_in_ring_market_hot / cold / user / polygon, 各 16K frames)
4. T0c user_reactor 故障域隔离: market 任何 ring drop / parse error 不影响 user, 反之亦然 (D-05)
5. 任一 reactor coroutine 单 message 处理 p99 < 50us (R-12, 老姜 S1-011 v0.4 加压测)

---

## 17.1.1 线程清单 — v0.4 修订 (替代 v0.3 §17.1.1)

### 17.1.1 v0.4 线程表 (4 vCPU × N 线程, 收口 4-5 conn 中间方案)

| # | 线程 | pin vCPU | 类型 | 职责 | 进/出 ring |
|---|---|---|---|---|---|
| **T0a** | `wss_poly_market_hot_reactor` | 0 | SCHED_FIFO 60 | Polymarket market channel, hot 分片 (~500 临场 ±10min token); epoll → recv → unmask → simdjson on-demand → normalize → try_push | out: wss_in_ring_market_hot |
| **T0b** | `wss_poly_market_cold_reactor` | 0 | SCHED_FIFO 60 | Polymarket market channel, cold 分片 (1-2 conn, ~3500 pregame_far + outright token); 同上 | out: wss_in_ring_market_cold |
| **T0c** | `wss_poly_user_reactor` | 0 | SCHED_FIFO 60 | Polymarket user channel (订单/fill 通知); **故障域硬性独立**, market 挂不影响 user, 反之亦然 (D-05) | out: wss_in_ring_user |
| **T1** | `wss_polygon_reactor` | 0 | SCHED_FIFO 60 | Polygon WSS 1 conn × 3 sub (newHeads + USDC.e Transfer + 三合约 logs OR-filter); 仅 ExecutionMode=Live 启用, Paper mode 不订阅 (§18.4) | out: wss_in_ring_polygon |
| T2 | `book_builder` | 1 | SCHED_FIFO 55 | 消费 4 个 wss_in_ring, 增量更新 orderbook / match state / feature pipeline / 触发 STALE 判定 (§17.6), 发布 RCU snapshot | in: 4× wss_in_ring + bg_to_book_ring; out: book_to_strat_ring |
| T3 | `strategy_engine` | 2 | SCHED_FIFO 55 | 消费 book_to_strat_ring + RCU snapshot, 跑信号 → RiskGateway::evaluate → signer IPC; ML shadow path 异步分支 (§19) | in: book_to_strat_ring; out: bg_work_ring + signer_ipc_ring + shadow_audit_ring |
| T4 | `bg_rest_worker_pool` (3-4 thread) | 3 | SCHED_OTHER, nice 0 | HTTP/2 worker, single-flight 折叠, retry+backoff | in: bg_work_ring; out: bg_to_book_ring |
| T5 | `periodic_gamma_poller` | 3 | nice 0 | 60s gamma `/sports/events?limit=500` | out: bg_work_ring |
| T6 | `periodic_goalserve_inplay_poller` | 3 | nice 0 | 1-3s Goalserve inplay 全联盟 | out: bg_to_book_ring |
| T7 | `periodic_goalserve_pregame_poller` | 3 | nice 0 | 30-60s Goalserve pregame odds | out: bg_to_book_ring |
| T8 | `audit_fsync` | 3 | **nice -5** | RM 群提交 fsync (老韩) | — |
| T9 | `exec_fsync` | 3 | **nice -5** | exec / nonce WAL fsync; Paper mode 写 `paper_exec.wal` (§18.3) | — |
| T10 | `metrics_exporter` | 3 | **nice +5** | Prometheus pull | — |
| T11 | `config_watcher` | 3 | **nice +5** | TOML hot reload via RCU | — |

**总计**: 14-15 线程跑在 4 vCPU 上. vCPU0 = 4-5 coroutine (单 asio reactor); vCPU1 = 1; vCPU2 = 1; vCPU3 = 8 (IO bound, OS 调度 + nice 分档).

### 17.1.2 ring buffer 拓扑 (v0.4 修订)

```
vCPU 0 (单 reactor 4-5 coroutine)         vCPU 1                vCPU 2
┌─────────────────────────────────┐    ┌──────────────┐    ┌──────────────────┐
│ T0a market_hot   (1 conn)       │    │ T2 book      │    │ T3 strategy      │
│ T0b market_cold  (1-2 conn)     │    │ builder      │    │ + RM evaluate    │
│ T0c user         (1 conn)       │    │              │    │                  │
│ T1  polygon      (1 conn×3 sub) │    └──────┬───────┘    └────────┬─────────┘
└─┬─────┬─────┬─────┬─────────────┘           │                     │
  │SPSC │SPSC │SPSC │SPSC                     │                     │
  │16K  │16K  │16K  │8K                       │                     │
  ▼     ▼     ▼     ▼                         │                     │
 [market_hot][market_cold][user][polygon]─────┘                     │
  (wss_in_ring × 4)                                                 │
                                                                    ▼
                                              ┌────────────────────────────────┐
                                              │  bg_work_ring (MPSC, 4096)     │
                                              │  writers: T2, T3 (audit/REST)  │
                                              └──────────────┬─────────────────┘
                                                             │
                                              ┌──────────────▼─────────────────┐
                                              │ vCPU 3 (8 thread, nice 分档)   │
                                              │ T4 / T5-T7 / T8-T9 / T10-T11   │
                                              └──────────────┬─────────────────┘
                                                             │ SPSC 1024
                                                             ▼
                                                       T2 book_builder

ML shadow 旁路 (§19):                          shadow_audit_ring (MPSC, 2048)
T3 (strategy) → ML signal 异步分支 ───────────────────────┐
                                                          ▼
                                              shadow_audit.wal (独立, R-11 隔离)
```

**关键不变量** (v0.4 新加):
1. **wss_in_ring 拆 4 路独立** (market_hot / market_cold / user / polygon), 互不串流, T2 book_builder 内部按 ring 类型分发到不同 builder pipeline
2. T0c user_reactor 写 wss_in_ring_user, **任何一条 market ring 满 drop 不影响 user ring** (D-05 故障域硬性)
3. T1 polygon_reactor 在 ExecutionMode=Paper 时**不启动** (§18.4), 节省 vCPU0 一个 coroutine 空转
4. ML shadow 走独立 shadow_audit_ring + shadow_audit.wal, **不接 bg_work_ring** (§19.3, ML-R2 红线)
5. ring 写策略不变 (v0.3 §6 表): try_push, 满 drop + counter + log warn, 绝不 block

---

## 17.6 STALE 5 档 — v0.4 修订 (替代 v0.3 §17.6.1)

### 17.6.1 5 档 STALE 表 (D-06 决议)

| micro-state | T_{1/2} 实测 | WARNING (DEFERRED) | HALT (REJECT) | 触发条件 |
|---|---|---|---|---|
| **INPLAY_HOT_CRIT** | 0.12-0.15s (n=23) | **200ms** | **800ms** | NBA Q4<2min / NFL 2-min warning / MLB ≥8 局 ≤1 分差 / NHL P3<5min ≤1 分差 / OT |
| **INPLAY_HOT** | 0.21-0.45s | 500ms | 2000ms | 其余 inplay (game_in_progress && wss_rate_60s > 1/s) |
| **PREGAME_NEAR** | 8-30s | 2000ms | 10000ms | event_start - now <= 30min |
| **PREGAME_FAR** | 60-120s+ | 10000ms | 30000ms | event_start - now > 30min |
| **OUTRIGHT** | 120s+ | 30000ms | 60000ms | series / season-long |

**实现位置**: classifier 函数在 T2 book_builder (vCPU1), 输入 `(market_meta, now_ts, wss_rate_60s, Goalserve game_state)`, 输出 `MarketStateEnum`. 小袁 Sprint-2 中 code-level 实现交付 RM v0.3 (D-06 Owner).

### 17.6.2 watchdog (v0.4 修订, 老叶 §1.5 + 小袁 §4.1)

| 数据源 | watchdog 阈值 | 触发动作 |
|---|---|---|
| Polymarket WSS (T0a/b/c) | **30s 无任何 frame** (含 ping/pong) | 重连 (asio coroutine, 不阻塞别的 reactor); 同时通知 T2 标记该 ring 关联市场进入 STALE |
| Polygon WSS (T1) | **6s 内无 newHead** (3 块) | 主动切 QuickNode 备 WSS; 不影响 Polymarket reactor |
| per-token freshness | **token_silence_ms > 5 × T_half(micro_state)** | T2 标记 `signal_state=DEGRADED`, microprice 不发, 等 RM STALE warning |
| Goalserve inplay | T6 上次成功 < 10s | T2 退一档 STALE (Goalserve 来源数据降级, 见 §17.6.3) |

### 17.6.3 stale-from-Goalserve 降级 (小袁 §1 注)

INPLAY_HOT_CRIT 判定依赖 Goalserve 推送 (Q4 时钟 / inning / score_diff). Goalserve push p95 7s 延迟 (小余 C-03). T2 在 Goalserve 数据 stale 时:
- micro_state 退一档 (INPLAY_HOT_CRIT → INPLAY_HOT)
- 不上报 R-11 paper-mode 污染 (Goalserve 数据本身是真实数据, 与 paper / live 无关)

**这条与 R-12 红线不冲突**: T2 在 vCPU1, 不在 vCPU0 reactor, 退档判断 < 1us atomic compare-and-swap.

---

## 18. ExecutionMode.Paper mock 接口 (上链 deferred 后必做) — v0.4 新章

### 18.0 章节定位

GM 2026-05-28 `gm-decision-defer-onchain-until-profitable.md` 决议: **MVP 起始阶段不上链, paper trading 7 hard gate (M4.5) 全过后才解锁.** ExecutionMode 三态 (Live / Paper / Shadow) 设计**不动** (paper trade ADR), Paper mode 必须把"虚拟上链"做成 plug-in mock 接口 (派单老周 v0.4 §18 + 小蒋 paper engine v0.2 落代码).

**与 Live mode 共享 90% 路径**: data ingest / book builder / strategy / RiskGateway / audit / metrics / fill ingestion / position ledger 全部同份代码. 仅 L5 signer 出口 (TB-B 边界) 分叉为 PaperSigner / RealSigner, 通过 CMake 编译期隔离 (小蒋 v0.2 §3.2 方案 A).

### 18.1 三个 mock 接口 (v0.4 spec)

**Owner**: 老周 主笔接口 spec; 小蒋 paper-trading-engine-v0.3 落代码; 老孙 v5 提供 SecureBuffer 共享 (PR-9); 老沈 PR-7 nm 校验.

#### 18.1.1 `IPaperSigner` (替代 RealSigner, paper mode only)

```cpp
// src/exec/signer/paper_signer/paper_signer.hpp
// CMake target: stcpp_paper_signer (link 到 stcpp_signer_paper binary only)
//
// 红线:
//   - 绝不发任何链上 RPC (no chain_rpc_submit / no eip712_real)
//   - nm stcpp_signer_paper | grep -E 'chain_rpc|eip712_real' = CI reject
//   - audit_id 与 RealSigner 同 schema, 仅 payload.mode = Paper

class IPaperSigner {
 public:
  // 输入: 与 RealSigner 完全同构 SignRequest (audit_id + intent + fencing_token)
  // 输出: PaperFill (虚拟 fill, 不上链)
  virtual Result<PaperFill, SignerError> sign_and_fill(
      const SignRequest& req,
      const BookSnapshot& current_book,  // 用于 VirtualMatcher 模拟
      const SlippageEstimate& slip
  ) noexcept = 0;

  virtual ~IPaperSigner() = default;
};

// 默认实现:
class PaperSignerImpl : public IPaperSigner {
  IVirtualNonce& virtual_nonce_;
  IVirtualGas& virtual_gas_;
  IVirtualConfirm& virtual_confirm_;
  VirtualMatcher& matcher_;  // 小袁 microstructure + 小肖 slippage
  // ... SecureBuffer (PR-9) 共享老孙 v5
};
```

#### 18.1.2 `IVirtualNonce` (代替 nonce_mgr Redis/SQLite/RPC observe)

```cpp
// src/exec/signer/paper_signer/virtual_nonce.hpp
//
// Live 时 IRealNonce 走 老叶 nonce_mgr v1 (Redis + SQLite + RPC observe)
// Paper 时 IVirtualNonce 走 in-memory monotonic counter

class IVirtualNonce {
 public:
  // 与 IRealNonce 同接口 (调用方不知道是哪个)
  virtual uint64_t allocate(WalletAddr wallet) noexcept = 0;
  virtual void commit(WalletAddr wallet, uint64_t nonce, TxHash tx_hash) noexcept = 0;
  virtual void rollback(WalletAddr wallet, uint64_t nonce) noexcept = 0;
  virtual uint64_t observe_onchain(WalletAddr wallet) noexcept = 0;  // paper: 返回 last_committed
};

class VirtualNonceImpl : public IVirtualNonce {
  // in-process std::atomic<uint64_t> per wallet
  // 写 paper_exec.wal (与 RealSigner 同 schema, payload.mode = Paper)
  // 不调任何 RPC (无 eth_getTransactionCount)
};
```

**关键约束**:
- allocate p99 < 100us (in-memory atomic, 比 Live nonce_mgr 1ms 快一个量级)
- commit 写 `paper_exec.wal` (T9 fsync, 群提交), payload.mode = Paper
- observe_onchain() 在 paper mode 返回 last_committed (无链可观测), 但**不能 return 假装的 onchain_state** — 调用方收到的是 `PaperObservedNonce {value, source=PaperLedger}`
- R-11 红线: `paper_exec.wal` 与 `exec.wal` 物理隔离 (不同目录, 不同 fsync 线程实例)

#### 18.1.3 `IVirtualGas` (代替 gas station 调用)

```cpp
// src/exec/signer/paper_signer/virtual_gas.hpp

class IVirtualGas {
 public:
  // 与 IRealGas 同接口
  virtual GasPriceEstimate estimate(GasUrgency u) noexcept = 0;
  // paper mode 不真的付 gas, estimate 仍按当前 Polygon gas station 镜像值
  // (T5 周期轮询拿 gasstation API, paper / live 都用, 只是 paper 不真扣)
};

class VirtualGasImpl : public IVirtualGas {
  // 从 RCU snapshot 读 T5 gasstation poller 落的最近一次 baseFee / priorityFee
  // 不调 RPC, 不上链
  // GasUrgency.{Standard, Fast, Rapid} 三档映射
};
```

**关键约束**:
- 估值仍真实 (paper 跑虚拟 PnL 要扣 gas, 否则 PnL 失真, M4.5 gate G1 误判)
- 不阻塞 ws event loop (estimate 走 RCU snapshot, < 1us)
- 红线: gas 估值 > 500 gwei 仍然走 RM SAFE_MODE 路径 (老叶 Y-5, paper 也要测这个分支)

#### 18.1.4 `IVirtualConfirm` (代替链上回执监听)

```cpp
// src/exec/signer/paper_signer/virtual_confirm.hpp

class IVirtualConfirm {
 public:
  // 与 IRealConfirm 同接口 (订阅 Polygon log + matching tx_hash)
  // Paper mode: 立即返回 confirmed (跳过 12 块 confirmation)
  // 但 fill 时序仍模拟 E2E latency (老姜 sampler) + maker 撤单概率 (小袁 fill_rate sampler)
  virtual Result<ConfirmedFill, ConfirmError> wait_confirm(
      TxHash virtual_tx_hash,
      std::chrono::milliseconds timeout
  ) noexcept = 0;
};

class VirtualConfirmImpl : public IVirtualConfirm {
  // VirtualMatcher 已经决定 fill / unfilled (小袁 §4.2 simulate_taker_fill)
  // 这里只是把 ConfirmedFill payload 包装回给 fill ingestion (与 Live 同 schema)
  // mode = Paper, tx_hash = "0xPAPER_" + ULID (R-11 区分)
};
```

### 18.2 与 Live 共享 90% 路径 — 分叉边界

```
trader 主进程 (单 binary, --mode={Live,Paper,Shadow})
  L1 infra ─────┐
  L2 data ──────┤  100% 共享
  L3 strategy ──┤  100% 共享 (含 RM evaluate)
  L4 risk ──────┤  100% 共享
  L5 exec SM ───┤  100% 共享 (audit_id 决策路径同份)
                │
                ▼
        signer IPC (TB-B 边界, 同 framed binary 协议)
                │
                ▼
        ┌───────┴───────┐
        │ fork signer 子进程 (CMake 编译期 3 个 binary)
        ▼               ▼               ▼
  stcpp_signer_live  stcpp_signer_paper  (backtest in-process)
  └ IRealSigner      └ IPaperSigner      └ IBacktestSigner
    ├ chain_rpc       ├ VirtualNonce       ├ deterministic
    ├ nonce_mgr v1    ├ VirtualGas         └ ...
    ├ gasstation      ├ VirtualConfirm
    └ Polygon WSS     └ VirtualMatcher
                        (小袁 §4.2 复用)
```

**红线 R-11 enforce 路径** (小蒋 v0.2 §2.4 + 老沈 PR-7):
- `nm stcpp_signer_live | grep -E 'Paper|Virtual' && exit 1`
- `nm stcpp_signer_paper | grep -E 'chain_rpc|eip712_real|nonce_mgr_v1' && exit 1`
- `nm stcpp_trader | grep -E 'RealSigner|PaperSigner_full' && exit 1` (trader 主进程只见接口, 不见实现)

### 18.3 paper_exec.wal / paper_audit.wal 隔离 (R-11 落地)

| WAL 文件 | 用途 | fsync 线程 | mode payload |
|---|---|---|---|
| `risk_audit.wal` | RM 决策 audit (Live) | T8 audit_fsync | mode=Live |
| `paper_audit.wal` | RM 决策 audit (Paper, 独立文件) | T8 audit_fsync (同线程, 不同文件) | mode=Paper |
| `shadow_audit.wal` | ML shadow signal audit (§19) | T8 audit_fsync | mode=Shadow, model_id 必填 |
| `exec.wal` | nonce / position WAL (Live) | T9 exec_fsync | mode=Live |
| `paper_exec.wal` | virtual_nonce / virtual_fill WAL (Paper) | T9 exec_fsync | mode=Paper |

**R-11 静态扫描** (老练 CI):
- 任何 `paper_*.wal` 写入路径出现 `mode != Paper` → CI reject
- 任何 `*_audit.wal` 写入路径出现 `tx_hash` 不带 `0xPAPER_` 前缀且 mode=Paper → CI reject

### 18.4 vCPU0 影响 (T1 在 Paper mode 不启动)

ExecutionMode.Paper 启动时:
- T1 polygon_reactor **不启动** (节省 vCPU0 一个 asio coroutine)
- T1 替换为 `VirtualPolygonStub` (vCPU3 in-process timer, 每 2s 推一个 mock newHead 给 T9 paper_exec.wal 时序对齐)
- VirtualGas 仍从 T5 gas_station 周期 poller 取真值 (paper PnL 计算需要真 gas)
- VirtualConfirm 不订阅 Polygon log, 直接由 VirtualMatcher 决定 fill 时序

**这意味着 Paper mode 的 vCPU0 上只有 T0a/T0b/T0c 三个 Polymarket reactor**, 单 reactor 复用 + 故障域分片仍成立, p99 < 50us 预算 (老姜 v0.4) 更宽松.

### 18.5 与未来 Live 切换 — 无缝路径

GM ADR §4 触发 (M4.5 7 gate + 律师 entity 选择确认) 后:
1. CMake target 切 `stcpp_signer_live` (`--mode=live`)
2. `IPaperSigner` 实例换成 `IRealSigner` (signer 主进程 fork 不同 binary)
3. `IVirtualNonce` → 老叶 nonce_mgr v1 (Redis + SQLite + RPC observe)
4. `IVirtualGas` → 真 gasstation API (T5 周期不变, 估值变真值)
5. `IVirtualConfirm` → 真 Polygon WSS 订阅 (T1 启用)
6. T1 polygon_reactor 启动 (1 conn × 3 sub)
7. WAL 切 `exec.wal` / `risk_audit.wal` (与 paper 同步并存, paper 实例保留作训练数据来源 — 小邓 §7)

**重做工作量**: 接口已经 spec, 只是换实现. Live 切 paper 之后 `stcpp_signer_paper` binary 不删除 — paper 是 ML 训练 + shadow signal 的持续数据源 (§19), 与 Live 并存.

### 18.6 测试场景 (派 @小宋, paper-mode 专项)

| 场景 | 验证 | Pass criteria |
|---|---|---|
| 18.6.1 | Paper mode 启动, 不调任何 Polygon RPC, 不发任何 chain_rpc_submit | tcpdump 监听 Polygon edge IP 0 包; nm 静态扫 0 chain symbol |
| 18.6.2 | Paper fill 走 RM 全路径, audit_id 与 Live 同 schema | paper_audit.wal payload.mode == Paper, schema parse 与 risk_audit.wal 同 binary 解析 |
| 18.6.3 | VirtualNonce allocate p99 < 100us | 1k allocate burst, histogram p99 |
| 18.6.4 | gas estimate > 500 gwei 触发 SAFE_MODE (paper 也要测) | 注入 mock gasstation 800 gwei, RM 进 SAFE_MODE |
| 18.6.5 | paper_exec.wal 与 exec.wal 物理隔离 | 同时 Live + Paper instance 并跑, 两个 WAL 文件路径不交叉 |

---

## 19. ML shadow signal 集成点 (小邓 Wave 14) — v0.4 新章

### 19.0 章节定位

`xiaodeng-ml-roadmap-v2.md` 立 ML-R1~R8 红线 + 4 阶段路线图. **paper 期 ML 仅 shadow** (ML-R2), **0 行进 OrderIntent 路径**. 本章定 C++ 接口 spec + 集成点 + 与 production 决策路径完全隔离的 ring 拓扑 + WAL 隔离.

**Owner**: 接口 spec 老周 主笔; 实现小邓 (D0-4 ONNX inference); paper engine 留位小蒋 (v0.3 链入 `libstcpp_ml_inference.a`); 集成 review 老姜 (latency) + 老韩 (RM 隔离) + 老沈 (无私钥/无外发).

### 19.1 三个接口 spec

```cpp
// src/ml/ml_signal_engine.hpp
// CMake target: stcpp_ml_inference (link 到 stcpp_trader 仅 paper/backtest mode)
// CMake flag: -DENABLE_ML_INFERENCE=ON (Live mode = OFF, ML-R2)

class IMLSignalEngine {
 public:
  // 每决策点 tick (与 rule signal 共享 FeatureAssembler, BR-1)
  // 返回 optional<MLSignalCandidate>; nullopt = 模型不发 (drift/stale/no_signal)
  //
  // 红线:
  //   - ExecutionMode != Live 才被调用 (Live binary 不链入 libstcpp_ml_inference.a)
  //   - 必带 model_id + feature_snapshot_id (ML-R8)
  //   - p99 < 50ms (ML-R7, < hot path 500us 不冲突, 因为在 paper 决策路径 200-500ms 大头里)
  virtual std::optional<MLSignalCandidate> tick(
      const FeatureSnapshot& fs,
      MarketId mid,
      AsOfTs as_of_ts
  ) noexcept = 0;
};

// src/ml/shadow_signal_sink.hpp
class IShadowSignalSink {
 public:
  // emit() 写 shadow_audit.wal (R-11 隔离, 独立 fsync, 不进 risk_audit.wal)
  // 异步 fire-and-forget (不阻塞 strategy engine)
  virtual void emit(const MLSignalCandidate& c) noexcept = 0;
};

// src/ml/model_registry.hpp
class IModelRegistry {
 public:
  // 当前生效模型 (SIGHUP 热 reload, atomic shared_ptr swap)
  // model_id 内嵌 sha256(model.onnx) 前 16 hex, 用于 ML-R8 audit
  virtual std::shared_ptr<OnnxModel> current_model(SignalId sid) noexcept = 0;
};
```

### 19.2 路径全图 (与 Production 决策路径完全隔离)

```
T3 strategy_engine (vCPU2)
   │
   ├──► [同步] Rule Signal Engine ──► RiskGateway::evaluate ──► signer IPC (Live/Paper)
   │                                                              │
   │                                                              ▼ audit_id (mode=Live/Paper)
   │                                                       risk_audit.wal / paper_audit.wal
   │
   └──► [异步] IMLSignalEngine::tick(fs, mid, as_of_ts)  // 仅 paper/backtest mode
              │
              ▼ optional<MLSignalCandidate>
        IShadowSignalSink::emit(c)
              │
              ▼ (MPSC shadow_audit_ring, 2048)
        T8 audit_fsync (vCPU3, 同线程不同文件)
              │
              ▼
        shadow_audit.wal (mode=Shadow, model_id 必填)
              │
              ▼ (异步 dashboard / cron 报表)
        ML vs rule 4 类 case 报告 (小邓 §5)
```

**关键不变量**:
1. ML 异步分支**不写 bg_work_ring** (绝不触发 REST), 不写 wss_in_ring (绝不影响 reactor)
2. ML emit() 失败 (ring 满) → drop + counter, **不影响 rule signal** (ML-R2)
3. shadow_audit.wal 与 risk_audit.wal / paper_audit.wal 是**三份独立文件**, mode payload 互不交叉
4. ML 推理 50ms 在 vCPU2 上**不阻塞 rule path** — 实际实现为 vCPU3 上 ONNX worker pool, T3 dispatch 后立即返回 (后台异步)

### 19.3 ML-R1~R8 红线落地点 (代码层 enforce)

| 红线 | 落地 | Enforcement |
|---|---|---|
| ML-R1 ML 不进 RM | RiskGateway::evaluate() 签名只接 RuleSignal, 不接 MLSignal | 编译期 type check, 老郭 D2 enforce 同款 |
| ML-R2 paper 期 ML 不进 OrderIntent | IMLSignalEngine 不发 OrderIntent, 只发 MLSignalCandidate → ShadowSink | 接口 spec, 老沈 PR review |
| ML-R3 M4.5 不看 ML PnL | run_gate_check.py 只读 risk_audit + paper_audit, 不读 shadow_audit | 小蒋 tool 实现, 老练 CI 校验 |
| ML-R4 Python 0 行进 binary | nm stcpp_trader 0 个 python symbol | CMake + nm CI |
| ML-R5 ONNX/Treelite C++ | libstcpp_ml_inference.a 只链 onnxruntime / treelite, 禁 pybind11 | CMake 依赖白名单 |
| ML-R6 4 类 case ≥ 20% | 阶段 2 报告 (小邓 §5) | 小邓 + 小董 cron 报表 |
| ML-R7 模型 < 100MB | CI 检查 model.onnx size | 老练 |
| ML-R8 推理必带 model_id + feature_snapshot_id | MLSignalCandidate struct 必填字段 | struct 定义 + 老唐 schema |

### 19.4 与 FeatureAssembler 共享 (BR-1)

ML 与 rule 走同一份 `FeatureAssembler` (vCPU1 T2 内 feature pipeline 模块, 老周 v0.3 §15.6 已定):
- T2 把 FeatureSnapshot 写入 RCU 双缓冲 (lock-free read)
- T3 rule signal 读 RCU snapshot
- T3 ML signal 读**同一份** RCU snapshot (BR-1)

**这意味着 ML 不重算 feature**, 节省 vCPU2 上的 feature 算力, 也保证 ML/rule 对比公平.

### 19.5 vCPU 资源占用 (paper mode)

| 阶段 | vCPU0 | vCPU1 | vCPU2 | vCPU3 |
|---|---|---|---|---|
| Live (M5+) | T0a/b/c + T1 polygon (4-5 conn) | T2 book | T3 strategy + RM | T4..T11 + (无 ML) |
| Paper (含 ML shadow) | T0a/b/c (3 conn, T1 不启) | T2 book + feature | T3 strategy + RM + ML dispatch (异步) | T4..T11 + **ML ONNX worker pool (新, 2-3 thread)** |
| Paper (无 ML) | 同 Paper | 同 | T3 + RM | T4..T11 |

**ML ONNX worker pool**: vCPU3 新增 2-3 thread, nice 0 (与 T4 同档), 处理 IMLSignalEngine 异步推理. 不抢 fsync (T8/T9 nice -5) 也不被 metrics 抢 (T10/T11 nice +5).

### 19.6 测试场景 (派 @小宋 + @小邓)

| 场景 | 验证 | Pass criteria |
|---|---|---|
| 19.6.1 | Live mode 不链入 libstcpp_ml_inference | nm stcpp_trader_live grep -E 'MLSignal\|onnx' 0 命中 |
| 19.6.2 | Paper mode ML 推理 p99 < 50ms | 1k tick burst histogram |
| 19.6.3 | ML emit drop 不影响 rule path | shadow ring 注入满, rule signal 仍 p99 < 500us |
| 19.6.4 | shadow_audit.wal 不污染 paper_audit.wal | 同时跑, 两 WAL 文件读取互相不见对方记录 |
| 19.6.5 | ML 推理缺 model_id → drop | MLSignalCandidate without model_id → emit() return drop + counter |

---

## 20. 跨域 review 清单 (听取义务 + 双向收口) — v0.4 新章

### 20.0 章节定位

GM `gm-policy-cross-domain-listening.md` Standing Policy: **架构修改触发跨域 review, 不签字 ≠ 通过.** 本章是 v0.4 主架构变更 (§15.6 + §17.1.1 + §17.6 + §18 + §19) 触发的**必须 confirm 才能 merge 清单**, 主持人老郭 (ADR 评审 owner).

**三态收口规则** (引用 ADR §2.5):
- ✅ **Agreed** — 明确接受
- ⚖️ **Compromised** — 让步达成
- ⬆️ **Escalated** — 升级 GM 拍板

**禁止 "先这样吧, 回头再说"** — 必须带截止日.

### 20.1 跨域 review 清单 (主架构变更 → 必到人员)

| 议题 | 触发条 | 必到人员 (Owner + 听取人) | 验收口径 | 截止 |
|---|---|---|---|---|
| §15.6 vCPU 映射 (4-5 conn 单 reactor + vCPU3 nice 分级) | D-05 + D-07 + 老郭裁定 | **老周 (owner) + 老郭 (评审) + 老姜 (latency) + 老李 (Polymarket conn 数) + 老叶 (Polygon T1) + 老陈 (网络压测)** | Sprint-2 W3 4-5 conn 压测 p99 < 50us | 6/22 |
| §17.1.1 线程清单 (T0a/T0b/T0c/T1 + T8-T11 nice 分档) | D-07 + 老叶 §5.1 | **老周 + 老郭 + 老韩 (T8 audit_fsync 路径) + 老姜 + 小石 (ring 拓扑 4 路) + 老叶 (T1 polygon)** | 老韩 RM v0.3 接口对齐, 老姜 latency 单列 ring p99 | 6/19 |
| §17.6 STALE 5 档 (含 INPLAY_HOT_CRIT) | D-06 + 小袁 §1 | **老周 + 老韩 (owner, RM v0.3 §11) + 小袁 (classifier 实现) + 小段 (Goalserve push 输入) + 小宋 (fixture)** | 小袁 classifier code-level 交付 RM v0.3, 小宋 5 段 transition fixture | 6/26 |
| §18 ExecutionMode.Paper mock (PaperSigner / VirtualNonce / VirtualGas / VirtualConfirm) | 上链 deferred ADR | **老周 (接口 spec) + 小蒋 (paper-engine v0.3 落代码) + 老孙 (SecureBuffer 共享) + 老沈 (PR-7 nm 校验) + 老叶 (未来 Live 切换路径) + 老练 (R-11 CI)** | 小蒋 v0.3 接口落代码 + 老沈 nm CI pass + 5 个 mock 测试场景 (§18.6) | Sprint-2 W2 (6/26) |
| §19 ML shadow signal 集成点 (IMLSignalEngine / IShadowSignalSink / IModelRegistry) | xiaodeng-ml-roadmap-v2 D0-4 | **老周 (接口 spec) + 小邓 (Wave 14 owner, D0-4) + 小蒋 (paper engine 留位) + 老姜 (50ms latency) + 老韩 (ML 不进 RM 验证) + 老沈 (无外发) + 老唐 (shadow_audit schema)** | 小邓 D0-4 接口 spec 完稿 + ML-R1~R8 代码层 enforcement | Sprint-2 W2 (6/26) |
| §20 跨域 review 清单本身 (元) | D-18 ADR 模板加听取确认 | **老郭 (主审) + 老周 (本架构 owner) + 小宋 (fixture)** | 本文 §20 表全部 owner 签字, 缺一不能 merge | 6/19 (Sprint-2 W1 末) |

### 20.2 听取义务 — 已在本架构 v0.4 落地的证据

| 来源 | 我 (老周) 听取了什么 | 体现在本架构 |
|---|---|---|
| 老李 §3.2 (4-5 conn 精细化) | 不是 1 不是 11, 是 hot/cold/user/polygon 分片 | §15.6.1 + §17.1.1 T0a/T0b/T0c/T1 表 |
| 老叶 §1.3 (故障域 2 conn) + §1.4 (newHeads 抢 vCPU0 metric) + §5.1 (vCPU3 nice T5) | Polymarket user 隔离 + Polygon WSS watchdog 6s + T5 gas_poller nice 0 | §15.6.2 nice 表 + §17.6.2 watchdog 表 |
| 老郭 D-05 / D-07 + ADR §3.1 (vCPU3 fsync > 周期) | 5 档 fsync 高 / 周期默认 / metrics 低 | §15.6.2 nice 分档 |
| 小袁 §1 (INPLAY_HOT_CRIT 5 档) + §4.1 (per-token DEGRADED) | 5 档 STALE + per-token 5×T_half | §17.6.1 + §17.6.2 |
| 小邓 ML-R2 + ML-R8 | shadow path 完全隔离 + 必带 model_id + feature_snapshot_id | §19.2 + §19.3 |
| 上链 deferred ADR (老雷) | Paper mock 三接口 + R-11 WAL 隔离 | §18 整章 |
| 小蒋 v0.2 PR-7 / PR-9 (CMake 隔离 + SecureBuffer 共享) | §18.1.1 PaperSignerImpl 共享 SecureBuffer + §18.2 三 binary | §18.1 + §18.2 |

### 20.3 双向收口 — 反向我等谁回执

| 派出 | 给谁 | 我等什么 | 截止 |
|---|---|---|---|
| §15.6.2 T5 nice 0 提议 | 老叶 + 小段 | 老叶 §5.1 已 ack T5 nice 0, 等小段 T6/T7 同档 ack | 6/19 |
| §17.1.1 T0a/T0b/T0c/T1 落表 | 老李 (D-07 owner) | 老李 §8 #3 承诺 Sprint-2 W3 压测数据, 不达标降级 (C) MVP NBA only | 6/22 |
| §17.6.1 5 档 STALE 接 RM v0.3 §11 | 老韩 | 老韩 RM v0.3 §11 落 5 档 + 小袁 classifier code 接入 | 6/26 |
| §18.1 三个 mock 接口 spec | 小蒋 (v0.3 落代码) | 小蒋 paper-engine v0.3 落 IPaperSigner / IVirtualNonce / IVirtualGas / IVirtualConfirm | 6/26 |
| §18.2 nm 校验三 binary | 老沈 + 老练 | nm CI rule 跑通, 静态扫 0 漏网 | 6/19 |
| §19.1 ML 三接口 spec | 小邓 (D0-4) | 小邓 D0-4 接口 spec + libstcpp_ml_inference.a mock implementation | 6/26 |
| §19.3 ML-R1 编译期 type check | 老韩 + 老郭 | RiskGateway::evaluate() 签名约束确认 (不接 MLSignal) | 6/19 |

### 20.4 升级条款 (Escalated → GM)

若任一 §20.1 议题在截止前未走到 Agreed / Compromised, **主持人老郭升级 GM 老雷**, 不允许"先这样吧". 当前 v0.4 起草时已识别**无 Escalated 项** — 所有变更均在 D-05/D-06/D-07/D-12/D-18 决议范围内, 实施细节即可.

### 20.5 PR-merge 强制门 (老高 + 老练)

本架构变更对应 PR (Sprint-2 W1-W3) 必须满足:
- [ ] §20.1 表中所有 Owner + 听取人 @-mention 在 PR 描述中
- [ ] 每个被点名人员必须 review approve 或显式 "no comment" (不允许沉默)
- [ ] 老郭 ADR-003 (本 v0.4) 签字
- [ ] 老高 R-12 + R-11 静态扫描 pass
- [ ] 老练 CI nm 校验 pass (signer 三 binary 符号隔离 + ML Live mode 不链入)

任一缺失 → CI block-merge, 不允许 waiver.

---

## 21. 残留 OPEN (v0.4 派单)

| # | 议题 | 期限 | Owner |
|---|---|---|---|
| OQ-V04-1 | 4-5 conn 单 reactor p99 < 50us 实测 (D-07 Compromised 条件) | Sprint-2 W3 (6/22) | 老姜 + 老李 + 老周 联跑 |
| OQ-V04-2 | INPLAY_HOT_CRIT classifier code-level (D-06) | Sprint-2 末 (6/26) | 小袁 |
| OQ-V04-3 | IVirtualNonce allocate p99 < 100us benchmark | Sprint-2 W2 | 小蒋 + 老姜 |
| OQ-V04-4 | IMLSignalEngine ONNX inference 50ms p99 实测 | 阶段 1 D1-6 (9/18) | 小邓 + 老姜 |
| OQ-V04-5 | shadow_audit.wal 与 paper_audit.wal 物理隔离测试 (R-11 + ML-R2) | Sprint-2 W2 | 小宋 + 老沈 |
| OQ-V04-6 | vCPU3 ML ONNX worker pool 与 T4 REST pool 资源占用 (新 2-3 thread) | Sprint-2 W3 | 老姜 + 小邓 |
| OQ-V04-7 | 老叶 raw tx buffer zeroize 检测目标 (§5.4 提议, Sprint-3 自动化 lint) | Sprint-3 | 老郭 + 老沈 + 老叶 |
| OQ-V04-8 | Polygon WSS 6s newHead watchdog 在 Paper mode 替换 VirtualPolygonStub 一致性 | Sprint-2 W2 | 老叶 + 小蒋 |

---

## 22. 与 ticket 的对应 (v0.4 增量)

- S2-W1-001: v0.4 §15.6 + §17.1.1 落代码 (老周 + 老李 + 老叶)
- S2-W1-002: §17.6 STALE 5 档 classifier (小袁 + 老韩)
- S2-W1-003: §18 IPaperSigner / IVirtualNonce / IVirtualGas / IVirtualConfirm 接口 (老周 主笔 + 小蒋 实现)
- S2-W1-004: §19 IMLSignalEngine / IShadowSignalSink / IModelRegistry mock impl (小邓 D0-4)
- S2-W1-005: §18 + §19 nm 校验 CI rules (老练 + 老沈)
- S2-W1-006: §20 跨域 review 清单 PR-merge 门 (老高 PR 模板 + 老练 CI)
- S2-W2-007: T0a/b/c/T1 reactor 实现 + 4 路 ring 拓扑 (老周 + 小石)
- S2-W2-008: vCPU3 nice 分档 systemd unit (老吴 + 老周)
- S2-W3-009: 4-5 conn 压测 p99 < 50us (老姜 + 老李 + 老陈)

---

## 23. 会签

**评审请求**:

- **老郭 (架构评审, ADR-003 主审)**: §15.6 + §17.1.1 + §17.6 + §18 + §19 + §20 整体, 4-5 conn 拓扑 + nice 分档是否覆盖 D-07 Compromised 条件
- **老李 (协议)**: §17.1.1 T0a/T0b/T0c 拓扑是否与 §3.2 精细方案一致 (4-5 conn 不是 11, 不是 1-2); Sprint-2 W3 压测确认
- **老叶 (链上)**: §17.1.1 T1 (1 conn × 3 sub) + §17.6.2 6s newHead watchdog + §18.4 T1 paper mode 不启用 + §18.5 Live 切换路径
- **老韩 (RM 接口)**: §17.6.1 5 档 STALE 接 RM v0.3 §11 + §18.3 paper_audit / shadow_audit 隔离 + §19.3 ML-R1 RiskGateway::evaluate 不接 MLSignal
- **老姜 (latency)**: §15.6.3 sched + §17.1.1 4-5 reactor 单核 p99 < 50us 是否可达; ML 推理 50ms 是否独立 budget
- **小袁 (微观)**: §17.6.1 INPLAY_HOT_CRIT 5 档表是否与 §1 实测一致
- **小蒋 (paper engine)**: §18.1 三个 mock 接口 spec 是否能落 v0.3 代码
- **小邓 (ML)**: §19.1 三接口 spec 是否能落 D0-4
- **老沈 (security)**: §18.2 nm 校验 + §19.3 ML-R5 Python 0 行 + 无外发
- **老高 (PR 规范)**: §20.5 PR-merge 强制门
- **老练 (CI)**: §18 + §19 nm 静态扫描规则
- **老雷 (GM)**: 终签

**会签栏**:
- 老周 (主笔): __签 (2026-06-15)__
- 老郭 (ADR-003 主审): ____
- 老李 (D-07 owner): ____
- 老叶 (D-08 owner): ____
- 老韩 (RM 接口): ____
- 老姜 (latency): ____
- 小袁 (D-06 owner): ____
- 小蒋 (paper engine v0.3): ____
- 小邓 (ML Wave 14): ____
- 老沈 (PR-7): ____
- 老高 (R-12 + R-11 enforcement): ____
- 老练 (CI): ____
- 老雷 (GM 终签): ____

— 老周 (cpp-chief-architect), 2026-06-15
