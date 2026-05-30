# Sprint-1 Retro — 小宋发言 (测试 + 回放 + chaos)

- Speaker: 小宋 (test-replay-chaos)
- Date: 2026-05-28
- 必读已扫: 老韩 v0.2 / 老胡 R-21 / 老郭 ADR-001 升 Accepted / 老钱 §3 节点窗 + M4.5 / 老周 6 misalignment / 老黄 / 小余 §8 PIT CI / 小梁 PER_ORDER_CAP

---

## 0. 一句话立场

老韩 13 拒单 enum 我全覆盖 + 4 新 enum 增量 + AET_SIGN_FAILED 我接;
R-12 4 场景压测 fixture 已起骨架, Sprint-2 W1 末跑通;
R-21 paper 联调时间不够我用 chaos + replay 顶 30% 风险, 不顶 100%;
PIT CI 主笔我不抢, 我接 framework adapter; 小宫 dogfood 11 边缘场景我接 chaos 自动化 8/8.

---

## 1. 老韩 13 拒单 enum + 4 新 + AET_SIGN_FAILED — 我的覆盖方案

**结论: 17 + 1 全覆盖, Sprint-2 W2 末交付测试套.**

| 套件 | 内容 | 状态 |
|---|---|---|
| `risk_reject_enum_test` | 13 enum (v0.2 §3.10) + 4 新 (`INVALID_INTENT` / `EDGE_NEGATED_BY_SLIPPAGE` / `BOOK_DEPTH_MISSING` / `FILL_RATE_BELOW_FLOOR`) | fixture 已起 (S1-024 framework), 每条 enum 最少 3 case (boundary / interior / chaos) |
| `risk_reject_property_test` | rapidcheck 生成器, 50K 随机 OrderIntent → enum 必须 ∈ 17 closed set + NEVER `APPROVED` if any R0-R9 fail | property gen 已写, R-1 (slippage) 等小肖代码 |
| `aet_sign_failed_audit_replay` | 模拟 RM APPROVED → signer REJECT, audit 链 (ORDER_DECISION → AET_SIGN_FAILED) 完整且 parent_audit_id 指向正确 | 拉老唐 schema v1 §2.4 + 老沈 B5 mock signer, 单测 6 case |
| `stale_segment_test` (v0.3 4 段 STALE) | INPLAY_HOT 500/2000ms × PREGAME_NEAR 2/10s × PREGAME_FAR 10/30s × OUTRIGHT 30/60s, 注入 quote freshness 抖动验 transition | 等老韩 v0.3 + 小袁 MarketState 判定函数, Sprint-2 W3 |

**强制覆盖率红线 (我承诺):**
- enum 覆盖率 = 100% (每个 enum 至少 1 hit, 0 dead path)
- branch 覆盖率 ≥ 95% (evaluate 路径)
- fail-closed 反压注入 3 场景 (fsync hang / disk full / ring full) 100% 触发 `REJECT(INTERNAL_ERROR)`
- 等老韩 S2-RM-1 evaluate p99 ≤ 200us 实现 → 我配 google benchmark regression test, 漂移 > 10% CI fail

---

## 2. R-12 WebSocket 不阻塞 — 4 场景 fixture 准备度

**结论: 4 场景骨架就位, Sprint-2 W1 末跑通. 我自己关心的第 5 个 misalignment 加进去.**

| 场景 | fixture 状态 | 验收 |
|---|---|---|
| S-1 REST 慢响应 (Polymarket /books 模拟 5s 延迟) | toxiproxy 代理 + golden frame replay 就位 | vCPU0 p99 < 50us 不被拖, bg_work_ring 不满 |
| S-2 1000 strategy single-flight (同 market_id 千并发触发 /books) | strategy mock 已写 (boost::asio coroutine spawn 1000), 计数器验只发 1 次 REST | leader 唯一 + 999 followers 等同 promise + 0 重复 REST |
| S-3 WSS 断 + bulk REST (resync) | toxiproxy 切 TCP RST, 触 bulk endpoint `/markets` (老李 v2 E2) | reconnect ≤ 3s (老陈 budget) + bulk fetch 走 vCPU3 不污染 vCPU0 |
| S-4 R-12 静态扫描 + 运行时 trip-wire | clang-tidy custom check + runtime assert (vCPU0 task > 50us → abort) | CI hard block + grep `co_await` in vCPU0 hot path = fail |
| **S-5 (我加) vCPU0 11 connection burst** (老周 Escalate-1) | 等老周 + 老姜 v0.4 拍板单/多 reactor | 11 conn 同时 burst, p99 < 50us 真实压测 |

**已知坑 (提前 flag):**
- old toxiproxy macOS 上不稳, fallback `tc netem` Linux 容器跑, 我侧 Docker compose 已写
- bulk REST 在 vCPU3 跟 audit fsync 共享 (老郭 §3.1 nice 优先级问题), 我把 fsync vs bulk REST 共核场景加进 chaos #4

---

## 3. R-21 paper engine 联调时间不够 (I=4 P=4) — 我能顶多少

**结论: 顶 30%, 不顶 100%. 老胡升级 GM 不能只靠我.**

**我能顶的 30%:**
1. **paper replay framework MVP** (Sprint-2 W2 出): 拉一段 gameday raw WSS tape (小冯 90 天 cold storage 之一) → paper engine 跑 → 与历史 backtest baseline bit-identical diff. 这能在 paper engine 联调时间不够时, 提前用历史 tape 跑通 RM + slippage + Kelly 完整路径 (R-11 不污染 position+pnl+nonce 我会签)
2. **chaos 注入预演 11 场景** (小宫 dogfood 11 边缘, 见 §5): paper D1 8/29 前我先用 chaos 把 NBA Finals / NFL Sunday / MLB doubleheader 三类高密度场景压一遍, 找出 paper engine bug, 缩短联调
3. **paper / live ULID 命名空间方案 A 验证** (老韩 misalignment #3): 我跑 paper + live 同进程并发 ULID 生成, payload `mode` 字段冗余, 验取证可区分; 给老韩 / 小蒋 / GM 决策数据

**我顶不了的 70%:**
- paper engine 本身的功能正确性 (那是小蒋的活, 我只能 replay 验)
- 8/14 paper engine 联调 deadline 推 → paper D1 8/29 推 → M4.5 gate 9/12 推, 这个时间杠杆我不在 critical path 上
- M4.5 第 1 次 gate 失败的 root cause 分析 (那是小董的活, 我配合提供 replay 抽样)

**给老胡的诚实话:** R-21 红色 16 不能靠 chaos 化解. paper engine W7-W8 (8/22-9/4) 独立列 CP 是对的, 我配合, 不抢戏.

---

## 4. PIT CI 红线 (小余 §8 请求小蒋主笔) — framework 接口

**结论: 主笔我不抢 (小蒋对), 但 framework adapter 我接.**

| 角色 | Owner |
|---|---|
| PIT CI 主笔 | **小蒋** (paper engine owner, 数据流入口) |
| 数据供给 | 小余 (parquet time-versioned + 4 时间戳) |
| review | 小邓 (ML 视角) |
| **测试 framework 接口** | **我 (小宋)** |

**我侧接口规范 (Sprint-2 W3 交付):**
- `pit_replay_test::ReplayDriver` 接 小余 parquet → MessagePack adapter (C-14 已认领), 暴露 `replay(as_of_ts, market_id) -> FeatureSnapshot`
- `pit_future_leak_check` CI step: 扫 train join 任何 `feature.as_of_ts > label_window_start` = test fail (R-3 红线)
- `pit_bit_identical_diff`: replay 24h 实时 → 历史 schema 重算 → bit-identical, 1 字节差 = fail (小余 §4.2)
- chaos 注入: schema 静默变更 (R-2 红线) 必须被 CI 抓, 我加 `schema_drift_chaos` 自动 fuzz 一个字段类型变化, daily CI 扫不到 = framework bug

**我和小蒋的边界 (不踩):**
- 主笔逻辑 (谁该过, 谁该 fail) = 小蒋
- 我只做 harness + adapter + chaos 注入器, 不定 policy

**给小余 §7.3 #2 (schema bump 节奏放宽到 Slack):** 我同意 Sprint-1/2 放宽, 但**前提是 schema_drift_chaos 在 CI 里**, 静默变更必被抓; 抓到再补 ADR. 否则放宽 = 出事. 6/12 老郭 + 老周评估时我陪审.

---

## 5. 小宫 dogfood 11 边缘场景 — chaos 自动化 8/8

**结论: 8 个能自动化, 3 个需人在环 (不是我顶).**

| # | 场景 | 自动化? | 实现方式 |
|---|---|---|---|
| 1 | NBA Q1 末关键节点窗 | 自动 | gameday tape replay + 触发 P0-01 信号断言 |
| 2 | NFL Sunday 多场同时 | 自动 | 8 tape 并发 replay, RM/throttle 验 |
| 3 | MLB 双连战 (doubleheader) | 自动 | 24h tape, 持仓 30min 上限 enforce 验 (老钱 §1 红线) |
| 4 | WSS 断 + bulk REST resync | 自动 | R-12 场景 S-3 复用 |
| 5 | RM SAFE_MODE 触发 + 5min unlock | 自动 | inject fsync hang → SAFE_MODE → 5min heartbeat → unlock 验 |
| 6 | Pinnacle 数据 stale > 5min | 自动 | toxiproxy 卡 Pinnacle 路径 A, 验 P0-01 不下单 |
| 7 | Polygon nonce 漂移 | 自动 | mock nonce_manager 注入 gap, sqlite WAL fallback 验 |
| 8 | UMA 仲裁挑战期 label | 自动 | parquet label.is_confirmed=false 训练 drop 验 (小余 C-20) |
| 9 | 操盘员手动 disarm (人在环) | **半自动** | UI hook + 我注入 metric, 实际按钮按下需小尤 fixture |
| 10 | KMS unwrap 5min 重 unwrap | **半自动** | 老沈 mock KMS, 需老沈配合 fixture |
| 11 | Sygnum onboarding 切换 | **不自动** | 业务流程, 老黄 + 老胡走人工演练 |

**8/8 自动化承诺 Sprint-2 W4 末跑通 (含 nightly chaos run).**

---

## 6. 双向收口

### 6.1 我接的派单

- 老韩: 13 + 4 enum + AET_SIGN_FAILED 全覆盖 — 接, §1
- 老韩 misalignment #3 paper/live ULID 方案 A 验证 — 接, §3 第 3 条
- 老胡: R-21 paper 联调 chaos + replay 顶 30% — 接, §3
- 老郭 ADR 模板"听取确认清单"我配 test fixture (每个 reviewer 必须签字才合并) — 接, Sprint-2 W2
- 小余 §8 请求 3: PIT CI framework adapter — 接, §4

### 6.2 我派出 (等回执)

- **老韩**: STALE v0.3 4 段判定函数 (`MarketState` enum + 转移条件) 给我代码级 spec, Sprint-2 W2 我才能写 fixture
- **老周**: Escalate-1 单/多 reactor 决议出来后, 我 §2 S-5 场景按结论调
- **小蒋**: PIT CI 主笔确认 (小余 §8.3 已点你名), 我 framework adapter Sprint-2 W3 交, 你 main logic 时间表给我
- **小袁**: hot token 判定 code (老韩 派) 给我 mock, chaos 注入 hot/cold transition
- **GM 老雷**: 老韩 Q1/Q2/Q3 三项拍板影响我 §1 + §3 fixture, 拍完 Sprint-2 W1 我才能锁实现

---

## 7. 我承认 Sprint-1 没做到的

1. **没出 v1 test framework spec** — Sprint-1 我全程被动配合 (小石数据结构 / 老韩 RM / 老周架构 review), 自己没产出独立文档. Sprint-2 W1 (6/19) 出 `xiaosong-test-replay-chaos-framework-v0.1.md`, 含 §1 enum 覆盖 / §2 R-12 4 场景 / §4 PIT adapter / §5 chaos 11 场景
2. **没 anticipate paper/live ULID 命名空间** — 老韩 misalignment #3 是我应该 Sprint-1 内发现的 (取证场景我管). 听取义务漏读老唐 schema §2.1 ULID 全局唯一与老王 paper_audit 独立 ring 的张力. ack
3. **R-12 fixture Sprint-1 末没跑通** — 老郭 §3.4 v0.4 deadline "任一未过 = v0.4 不发" 把我列硬约束, 我 W4 之前应该出 demo 而不是骨架. Sprint-2 W1 末必跑通, 否则我自降 owner 等级

---

**END.**

— 小宋 (test-replay-chaos), 2026-05-28
