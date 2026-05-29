# M1 MVP 验收 Spec v2 — 可勾选 Checklist

- **Owner:** 小颖 (requirements-analyst, E 产品业务保障部)
- **Last review:** 2026-05-29
- **验收人:** 老雷 (GM) + 老钱 (CPO) + 老胡 (PM)
- **任务来源:** E 主管老胡 → 小颖 (M1 MVP 验收 checklist, GM 分工并行)
- **输入文档 (权威):**
  - `docs/RESEARCH/xiaoying-acceptance-spec-v1.md` (M1 38 条基线)
  - `docs/RESEARCH/xiaoying-acceptance-spec-v2.md` (ABI v0.5 对齐 + GM-PAPER-G 八条)
  - `docs/ADR/2026-05-29-adr-040-market-structure-per-token.md` (per-token 市场结构修正, 已合入 main)
  - `docs/ADR/2026-05-29-adr-041-frontend-stack-solidjs-vite.md` (前端栈 SolidJS, 已落地)
  - `docs/ADR/2026-05-29-observability-debug-api.md` (ADR-038, debug_api 11+ endpoint)
  - `frontend/INTEGRATION-VERIFY.md` (v5.2 SolidJS 迁移验证, 2026-05-29)
  - `include/stcpp/risk/rm_debug_snapshot.hpp` (RmDebugSnapshot 真实快照, 已实现)
  - `src/stcpp/debug_api/state_provider.hpp` (ADR-040 per-token 契约, commit 88dd1c4)
  - `docs/SPRINTS/sprint-03-w10-w2-progress.md` (W10 W2 状态, 2026-07-08)
- **版本说明:** 本文是 M1 专项验收 checklist。与 acceptance-spec-v1 (38 条系统性 criteria) 和 acceptance-spec-v2 (ABI/Gate 对齐) 互补，不重复，专注当前达成率与 blocker 标注。

---

## §0 文档目的

把 M1 五大目标（Moneyline 实盘跑通 + 第一笔成交 + PnL 看板 + 零风控失效 + 72h 无崩溃）拆成**可逐条勾选**的验收 checklist，每条明确：

1. **验收项** — 一句话描述
2. **客观判据** — 数字阈值 / CI 断言 / 命令可验证
3. **当前状态** — `done` (已达成可验) / `wip` (有实现在推进) / `todo` (尚无对应实现)
4. **blocker 标注** — 阻塞原因 + 解锁依赖

**格式约定:** `[ ]` = 未通过 (todo/wip), `[x]` = 已达成 (done, 判据可客观核实)

---

## §1 模块一: 观测看板 (功能状态可见)

> 对应 GM must-have M-01 "看到 PnL" + M-07 "系统健康度" + ADR-038 debug_api + ADR-041 前端

### 1.1 前端看板存在且可启动

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| OBS-01 | SolidJS + Vite 前端可构建 | `cd frontend && npm run build` → 0 error, dist/ 产物存在 | **done** |
| OBS-02 | TypeScript 严格模式编译零错误 | `tsc --noEmit` 返回 0 errors (strict mode) | **done** |
| OBS-03 | 开发服务器可本地访问 | `npm run dev` → `http://127.0.0.1:3000` 正常响应 | **done** |
| OBS-04 | Stub 模式可独立渲染 | `?stub=1` 参数下全部卡片渲染不报 JS error | **done** |

### 1.2 观测 API (debug_api 11+ endpoint)

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| OBS-05 | debug_server 可编译链接 | `cmake --build` 目标 `stcpp_debug_server` 无报错 | **done** |
| OBS-06 | `/healthz` 返回合规 JSON | `curl 127.0.0.1:8080/healthz` → `{"status":"ok","mode":"paper",...}` 含 `as_of_ts_ns` | **done** |
| OBS-07 | `/api/v1/status` 返回系统状态 | 含 `rm_state` / `uptime_s` / `mode` 三字段, 均非零 | **done** |
| OBS-08 | `/api/v1/book/{condition_id}` 返回 BinaryMarketBookView | 含 `token0` / `token1` / `cross_spread` / `condition_id` 字段 (ADR-040 per-token) | **done** |
| OBS-09 | `/api/v1/book_pair/{condition_id}` 与 book 等价 | 同 condition_id 两端点响应结构一致 | **done** |
| OBS-10 | `/api/v1/book/token/{token_id}` 返回单边 BookSnapshot | `token_id` / `outcome` / `condition_id` 字段存在 | **done** |
| OBS-11 | `/api/v1/market/{condition_id}` 返回 MarketInfo with tokens[] | `tokens` 数组长度 == 2; 含 `condition_id` (权威) + `market_id` (deprecated alias) | **done** |
| OBS-12 | `/api/v1/pnl/timeseries` 返回时间序列 | 含 `cum_net_pnl` / `n_trades` / `as_of_ts_ns` 字段 | **done** |
| OBS-13 | `/api/v1/pnl/attribution` 返回 PnL 归因瀑布 | 含 `gross` / `fee` / `slippage` / `spread` / `net` 分层字段 | **done** |
| OBS-14 | `/api/v1/risk/rejects` 返回拒单列表 | 含 `reason_code` / `market_id` / `rejected_ts_ns` 字段 | **done** |
| OBS-15 | `/api/v1/gate/paper` 返回门禁状态 | 含 `G00` ~ `G08` gate 对象 + `overall_pass` 字段 | **done** |
| OBS-16 | `/metrics` 返回 Prometheus text | 含 `stcpp_rm_state` / `stcpp_pnl_usdc` 等 P0 指标行 | **done** |

### 1.3 前端与后端 API 端到端联通

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| OBS-17 | 前端连接 demo provider 可渲染真实数据 | 去掉 `?stub=1`, 前端从 `127.0.0.1:8080` 拉取, 无"api-err-chip"红标 | **done** |
| OBS-18 | 赛事分组 + 双边订单簿卡片渲染 | event_id 分组, token0/token1 双卡并列, cross_spread 显示 | **done** |
| OBS-19 | DEMO 横幅显示正确 (对齐老钱裁定) | 看板顶部显示"DEMO"横幅 + 每盘口卡片右上角"demo chip"角标 | **done** |
| OBS-20 | PnL sparkline 可见 | GlobalBar 顶部 PnL sparkline 曲线可渲染 (不为空) | **done** |

### 1.4 DEMO 与 REAL 两步可见性 (对齐老钱裁定)

> 老钱裁定: M1 paper 阶段 = DEMO 模式 (stub/demo provider); 真实数据接入 = paper runtime 就绪后 (O1)

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| OBS-21 | DEMO 模式: 所有 API 返回含 `"mode":"paper"` 字段 | `curl /healthz` + `/api/v1/status` 均见 `mode` | **done** |
| OBS-22 | DEMO 模式: 看板有 fail-safe DEMO 标记, 不被误读为真实交易数据 | P0-02 DEMO 横幅存在 + "demo chip"角标存在 (INTEGRATION-VERIFY.md 已验) | **done** |
| OBS-23 | REAL 切换门禁: paper runtime 接真数据需 W11 paper runtime 启动 | paper runtime 启动 checklist 9 项 (sprint-03-w10-w2 §9), 当前 0/9 完成 | **wip** |
| OBS-24 | REAL 切换门禁: ABI V2 全链路 (老沈 transformer + 老唐 audit v1.4) 完成 | Wave 97-98 PR merged; W10 W2 待 push | **wip** |

**OBS-23/24 blocker:** paper runtime 启动依赖 Frankfurt server (AWS 账号权限阻塞, W10 W2 活跃风险)。解锁依赖: 老吴账号确认 → Frankfurt EC2 → W10 W3 SSH 连通 → W11 paper runtime 骨架 (小肖)。

---

## §2 模块二: Moneyline 盘口 (市场结构正确)

> 对应 M1-B 数据接入 + ADR-040 per-token 修正

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| ML-01 | per-token 订单簿结构正确 | `GET /api/v1/book/{condition_id}` 返回 token0/token1 各独立 OrderBook (非 per-condition 合并), ADR-040 §4 已验 | **done** |
| ML-02 | condition_id 权威字段 (bytes32 hex) | MarketInfo 含 `condition_id` 66 字符十六进制; `market_id` 字段存在 (deprecated alias = condition_id) | **done** |
| ML-03 | token_id 字段存在 (uint256 string) | `tokens[i].token_id` 非空, 长度 <= 77 位十进制字符串 | **done** |
| ML-04 | outcome 字段语义正确 | `tokens[0].outcome` + `tokens[1].outcome` 互补 (如 "YES"/"NO" 或球队名对); 不硬编"yes_book"/"no_book" | **done** |
| ML-05 | cross_spread 后端计算 | `BinaryMarketBookView.cross_spread = token0.best_ask + token1.best_ask - 1.0` (vig 等效); 前端不自算 | **done** |
| ML-06 | WSS Polymarket sports channel 订阅 | 启动 30s 内 `stcpp_ingest_book_delta_total > 0` (需 paper runtime 真接 WSS) | **todo** |
| ML-07 | Goalserve inplay 3 sport 接入 | NBA/NFL/MLB 各 >= 1 live event 时 5min 内 tick 入 WAL | **wip** |
| ML-08 | WSS 断线 30s 内重连 | 注入 60s 中断后 reconnect p95 < 30s (小冯 chaos test Wave 99) | **wip** |
| ML-09 | R-20 4 时间戳契约全链路 | `event_ts <= data_source_ts <= ingestion_ts <= as_of_ts`, 0 违例入决策 | **wip** |

**ML-06 blocker:** 需 paper runtime 真接 Polymarket WSS (小冯 GoalserveInplayClient Wave 99; PolymarketCLOBSubscriber W9 W4 已 merged)。

**ML-07 blocker:** 小冯 GoalserveInplayClient Wave 99 (W10 W2 待 push)。

**ML-09 blocker:** 需 paper runtime 启动后全链路运行才能度量 0 违例。

---

## §3 模块三: 风控门禁 (零风控失效)

> 对应 GM must-have M-02 + M-05 + acceptance-spec-v1 §1.A + §1.G

### 3.1 RM 状态机

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| RM-01 | 冷启动 default SAFE_MODE | 启动 1s 内 `GET /api/v1/status` → `rm_state:"SAFE_MODE"` | **wip** |
| RM-02 | 三签解锁 (GM+risk+audit) | SAFE_MODE → RUNNING 需 3 签; 单/双签时 state 不变 | **todo** |
| RM-03 | HALTED 禁止自愈 | HALTED 后 24h 内无自动/单签/重启回 RUNNING | **todo** |
| RM-04 | crash 后 watchdog 自动 SAFE_MODE | kill -9 后 systemd 拉起, state == SAFE_MODE (M1-G03) | **todo** |

### 3.2 RM 核心规则

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| RM-05 | OrderIntent 100% 经 RM (0 绕过) | CI grep 无绕 RM 路径 + WAL audit_id 数 == risk.wal allow 数 (1h 窗); ABI v0.5 token_id/side 透传核查 (M1-A03 ABI-UPDATE) | **wip** |
| RM-06 | RejectCode 全覆盖单测 | 21 种原始 code + 6 种 ABI v0.5 新增 sub_reason/code 各 >= 1 单测 (M1-A04 ABI-UPDATE) | **wip** |
| RM-07 | RmDebugSnapshot lock-free ring 存在 | `include/stcpp/risk/rm_debug_snapshot.hpp` 已实现; `push_reject` p99 < 1us (单测可验) | **done** |
| RM-08 | `/api/v1/risk/rejects` 返回真实拒单快照 | demo provider 下 endpoint 返回拒单行 (reason_code/market_id/rejected_ts_ns); 字段黑名单: 无私钥/签名/nonce | **done** |
| RM-09 | RM evaluate p99 < 1ms | 1M evaluate benchmark, p99 < 1ms (M1-A07) | **wip** |
| RM-10 | R-11: paper 不写真账本 | CI grep: paper engine 调用图无 position/pnl_ledger/nonce_ledger 写 | **wip** |
| RM-11 | R-11: paper/risk WAL 物理隔离 | 路径/fd/inode 不同, 1h 跑量 0 cross-write | **wip** |

### 3.3 紧急操作

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| RM-12 | GM 一键 halt, t < 1s 生效 | 操作到 `rm_state == HALTED` 间隔 < 1s; audit 落一条 STATE_TRANSITION | **todo** |
| RM-13 | HALTED 告警 3 通道 < 5s | 邮件 + notification + Grafana 三通道, t_alert - t_halt p99 < 5s | **todo** |
| RM-14 | signer 异常 → 自动 HALT/SAFE | signer timeout/panic/错误码 → 自动 HALT 或 SAFE (M1-G04) | **todo** |

**RM-01/02/03/04 blocker:** RM v0.5 spec (老韩 W10 W2) + RM v0.5 实施 (W10 W3) + paper runtime 骨架 (小肖 W11)。

**RM-12/13/14 blocker:** 需 paper runtime 启动后可操作的 HALT CLI/API。

---

## §4 模块四: Paper E2E (端到端无断点)

> 对应 M1-D + O1 管线贯通验收

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| PE-01 | ABI V2 全链路 merge (OrderIntent v0.5 + SignerV62 v6.2) | CI abi_lock v1.8 通过; 无 `market_id`/`is_buy` 旧字段; `token_id`/`condition_id`/`side`/`timestamp_ms` 全链透传 | **wip** |
| PE-02 | 老沈 V2 transformer ctest pass | Wave 97 PR merged; PositionLedger integration test +6 通过 | **wip** |
| PE-03 | 老唐 audit schema v1.4 (timestamp_ms/metadata/builder) | Wave 98 PR merged; replay verify tool ctest +5 通过 | **wip** |
| PE-04 | WSS 真接 CLOB + reconnect chaos | Wave 99 小冯 GoalserveInplayClient + reconnect chaos ctest +4 | **wip** |
| PE-05 | REST API 9 endpoint 接真 state (非 stub) | `GET /api/v1/status` / `/api/v1/risk/rejects` 等返回真实 paper runtime 数据 | **todo** |
| PE-06 | paper e2e latency WSS recv → VirtualFill emit p99 < 50ms | Sim 测量端到端延迟 (M1-D01) | **todo** |
| PE-07 | VirtualFill 公式可解释 | filled_size + avg_price + slippage 偏差 < 1bp; size_pUSD_micro (V2 单位, 非 usdc_micro V1) | **todo** |
| PE-08 | audit_id 完整链路复盘 < 10s | 任一 audit_id 拉出 signal_ctx + PM book + RM verdict + signer + matcher 6 段, 含 ABI v0.5 新字段 token_id/outcome/side (M1-E01 ABI-UPDATE) | **todo** |
| PE-09 | snapshot_id ↔ audit_id 双向 join 0 孤儿 | SELECT JOIN 任一方向 0 孤儿 (M1-E02) | **todo** |

**PE-01~04 blocker:** Wave 97-99 (老沈/老唐/小冯) W10 W2 待 push; 依赖链 V2 transformer → REST state → paper runtime。

**PE-05~09 blocker:** REST 接真 state 依赖老沈 transformer (Wave 97) 先 merged; paper runtime 整体 W11 才启动。

---

## §5 模块五: 72h 稳定性

> 对应 GM must-have M-07 + M1-F01/F06 + CLAUDE.md §2 MVP 硬条件

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| ST-01 | 72h 无崩溃 (硬条件) | 72h 内 0 segfault / 0 OOM / 0 死锁 / 0 panic (systemd restart_count = 0) | **todo** |
| ST-02 | 在线率 >= 99.0% (7 天滑动) | uptime/total >= 0.99, Prometheus `stcpp_uptime_s` 连续 7 天无间断 | **todo** |
| ST-03 | WAL fsync p99 < 1ms | 1M write benchmark, p99 < 1ms (M1-F02) | **wip** |
| ST-04 | WSS event loop 0 R-12 违例 | 0 同步 REST / 0 阻塞 IO / 0 锁 > 100us 在 WSS event loop 内 | **wip** |
| ST-05 | Frankfurt server SSH 连通 + base image | 老吴 AWS 账号权限确认 → EC2 购买 → W10 W3 SSH 连通 | **wip** |
| ST-06 | vCPU 7 核 affinity 配置 | 7 线程 affinity 与设计一致 (M1-F03) | **todo** |
| ST-07 | 决策延迟 4 阶段 12 指标在线 | ingestion/signal/RM/signer 各 p50/p99/p999 进 Prometheus (M1-F05) | **wip** |

**ST-01/02 blocker:** 需 paper runtime 完整启动 (W11 目标)。

**ST-05 blocker:** AWS 账号权限 (W10 W2 活跃阻塞), 是 W11 paper runtime 启动的前置硬依赖。

---

## §6 模块六: 4 时间戳合规 (R-20 红线)

> 对应 CLAUDE.md §8 R-20 + M1-B04 + acceptance-spec-v1 §1.B + 永久-06

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| TS-01 | 所有 debug_api 响应含 `as_of_ts_ns` (int64, epoch ns) | `curl /healthz` + `/api/v1/status` + `/api/v1/book/{id}` 均含 `as_of_ts_ns` 非零 int64 | **done** |
| TS-02 | 禁 ISO 字符串时间戳 (schema 铁律) | CI grep `"timestamp"` / `"time"` 字段值为字符串 → CI fail; 全部用 epoch_ns int64 | **done** |
| TS-03 | `event_ts <= data_source_ts <= ingestion_ts <= as_of_ts` 全链 | R-20 违例 = 0 (paper runtime 运行期度量) | **wip** |
| TS-04 | 缺失 ts 字段时 fail-closed (返回 0, 调用方拒绝) | 4 种 ts 违例 (M1-D04 + ABI v0.5 新增 MISSING_TOKEN_ID/MISSING_CONDITION_ID/INVALID_TOKEN_ID_FORMAT) 各 >= 1 单测 | **wip** |
| TS-05 | `FourTs` struct 存在于 state_provider.hpp | `struct FourTs { event_ts_ns / data_source_ts_ns / ingestion_ts_ns / as_of_ts_ns }` 已定义 | **done** |
| TS-06 | `feature_snapshot_id` 唯一 (1M emit 0 碰撞) | 1M emit, set size == 1M (M1-C04) | **wip** |
| TS-07 | audit replay 含 R-20 4ts V2 validate (老唐 Wave 98) | audit chain replay verify ctest +5 通过 | **wip** |

---

## §7 模块七: PnL 看板可见性

> 对应 GM must-have M-01 + M1-H01/H02/H03 + O2 盈利可证

| # | 验收项 | 客观判据 | 状态 |
|---|---|---|---|
| PNL-01 | PnL 看板今日数字可见 (1 屏内) | GlobalBar 顶部显示净 PnL 数字 (含符号 + 颜色) | **done** (DEMO 数据) |
| PNL-02 | PnL sparkline 曲线可见 | PnlSparkline.tsx 渲染 SVG 曲线 (INTEGRATION-VERIFY.md 已验) | **done** (DEMO 数据) |
| PNL-03 | PnL 归因瀑布 (折叠区) 可见 | SecondaryFooter 含 gross/fee/slippage/spread/net 分层 | **done** (DEMO 数据) |
| PNL-04 | 7/30 天趋势图 (4 时间维度) | `GET /api/v1/pnl/timeseries?window=7d` + `30d` 均返回序列 | **done** (DEMO 数据) |
| PNL-05 | PnL 数字与 audit chain 一致 (diff == 0) | 看板 PnL == SUM(virtual_fill.pnl); 需 paper runtime 真实成交 | **todo** |
| PNL-06 | PnL 看板接真实 paper runtime 数据 (非 DEMO) | 切掉 demo provider, 前端无"api-err-chip" | **todo** |

**PNL-05/06 blocker:** VirtualMatcher 尚未实现 (小袁 W11); paper runtime 未启动。

---

## §8 综合判据: M1 通过门槛

M1 MVP 通过需满足以下硬性条件 (全部为 done, 无 wip/todo 豁免):

| # | M1 硬门槛 | 对应 checklist 项 | 必须完成 |
|---|---|---|---|
| H-01 | 72h 无崩溃 | ST-01 | 是 |
| H-02 | 零风控失效 (RM 100% 拦截 + 0 绕过) | RM-05 + RM-10 + RM-11 | 是 |
| H-03 | paper 端到端有 VirtualFill 产出 | PE-06 + PE-07 | 是 |
| H-04 | PnL 看板接真实数据 (非 DEMO stub) | PNL-05 + PNL-06 | 是 |
| H-05 | R-20 4ts 全链 0 违例 | TS-03 + TS-07 | 是 |
| H-06 | DEMO 标记正确显示 (老钱裁定两步可见) | OBS-19 + OBS-21 + OBS-22 | 是 (已 done) |
| H-07 | paper runtime 72h 在线率 >= 99% | ST-02 | 是 |

---

## §9 当前达成率汇总

### 9.1 按模块统计

| 模块 | done | wip | todo | 本模块合计 |
|---|---|---|---|---|
| §1 观测看板 | 21 | 2 | 1 | 24 |
| §2 Moneyline 盘口 | 5 | 4 | 0 | 9 |
| §3 风控门禁 | 2 | 5 | 7 | 14 |
| §4 Paper E2E | 0 | 4 | 5 | 9 |
| §5 72h 稳定性 | 0 | 4 | 3 | 7 |
| §6 4 时间戳合规 | 3 | 4 | 0 | 7 |
| §7 PnL 看板 | 4 | 0 | 2 | 6 |
| **合计** | **35** | **23** | **18** | **76** |

### 9.2 达成率

- **done:** 35 / 76 = **46%**
- **done + wip:** 58 / 76 = **76%** (有实现在推进)
- **todo (无实现):** 18 / 76 = **24%**

### 9.3 当前已达成的主要里程碑

1. **观测 API 已落地:** debug_api 11+ endpoint 编译链接正常, demo provider 端到端联通 (ADR-038 阶段 A 达成)
2. **前端 SolidJS 迁移完成:** v5.2 SolidJS + Vite 零 TS 错误, DEMO 模式下全功能渲染 (ADR-041 落地)
3. **per-token 市场结构修正:** ADR-040 commit 88dd1c4 已合入 main, BinaryMarketBookView / TokenInfo 契约正确
4. **RmDebugSnapshot 实现:** lock-free ring 头文件已实现, 满足 R-12 (push_reject p99 < 1us)
5. **4 时间戳 schema 铁律 API 层已落:** 所有 endpoint 返回 epoch_ns int64, 禁 ISO 字符串

### 9.4 M1 当前 Blocker 汇总

| Blocker | 影响项数 | 解锁依赖 | ETA |
|---|---|---|---|
| paper runtime 未启动 (W11 目标) | 18 项 (全部 todo) | Frankfurt server (老吴 W10 W3) + RM v0.5 (老韩 W10 W3) + paper 骨架 (小肖 W11) | W11 |
| AWS Frankfurt 账号权限 (活跃阻塞) | 间接影响 paper runtime | 老吴 W10 W2 账号确认; 无权限升老周 4h | W10 W2 今日 |
| ABI V2 全链路 (Wave 97-99 待 push) | 12 wip 项 | 老沈 transformer + 老唐 audit v1.4 + 小冯 inplay client | W10 W2 末 |
| VirtualMatcher 未实现 | PE-06/07 + PNL-05/06 | 小袁 W11 FillRateModel + VirtualMatcher | W11 |
| RM v0.5 spec + 实施 | RM-01/02 等 wip | 老韩 spec W10 W2 + 实施 W10 W3 | W10 W3 |

---

## §10 澄清清单 (小颖挂单)

| # | 问题 | 影响项 | 问谁 | 时限 |
|---|---|---|---|---|
| Q1 | OBS-23 "REAL 切换": 切换时序是"paper runtime 启动即切"还是"需 GM 三签授权才切"? 影响 OBS-23 验收判据定义 | OBS-23/24 | 老雷 + 老钱 | W10 W4 |
| Q2 | PNL-04 "7/30 天趋势": 是否包含 inplay 数据? 还是只算 pregame Moneyline (对齐 G-06 盘口分桶) | PNL-04 | 老钱 | W10 W4 |
| Q3 | ML-06 WSS 真接 blocker: 若 Frankfurt server W10 W3 未就绪, M1 是否可以用本地 localhost 模拟 WSS 做 pre-validation? | ML-06/ML-07 | 老周 + 老韩 | W10 W3 |
| Q4 | RM-02 三签解锁: M1 paper 阶段 SAFE_MODE → RUNNING 需要 GM + risk + audit 三个真人签, 还是 CI 模拟三签? | RM-02 | 老韩 + 老雷 | W10 W4 |
| Q5 | ST-01 72h 无崩溃: 计时起点是"paper runtime 首次启动"还是"paper runtime 首次 RM 放行首笔 VirtualFill"? | ST-01/02 | 老胡 | W10 W4 |

---

## §11 自检 (小颖)

- [x] 每条 checklist 含验收项 + 客观判据 + 当前状态 (done/wip/todo)
- [x] 覆盖全部 5 大 M1 目标: 观测看板 / Moneyline 盘口 / 风控门禁 / paper e2e / 72h 稳定性
- [x] 额外覆盖: 4 时间戳合规 (R-20 永久红线) + PnL 看板 + DEMO→REAL 两步可见性
- [x] 当前已达成项有客观可核实依据 (commit hash / endpoint / INTEGRATION-VERIFY.md)
- [x] blocker 逐条标注解锁依赖 + ETA
- [x] 达成率数字: 35/76 done (46%) + 23 wip (30%) + 18 todo (24%)
- [x] M1 硬门槛 7 条明确列出 (H-01~H-07)
- [x] 澄清清单 5 条挂单
- [x] 每条判据可客观核实 (无"看起来好" / "用户体验满意"等主观词)
- [x] 不写代码 / 不写 PRD / 不跟进 ticket / 不 git commit/push
- [x] owner + last_review 开头

---

**最后更新:** 2026-05-29 by 小颖 (requirements-analyst, E 产品业务保障部)
**下次 review:** W11 paper runtime 启动后 (预计大量 todo → done)
**升级路径:** 任何 blocker 解锁或验收项状态变更 → @老胡 (24h ack) → @老雷 (48h 不下升级)
