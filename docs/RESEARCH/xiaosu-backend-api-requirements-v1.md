# Operator UI 后端 API 需求 v1

- Owner: 小苏 (frontend-engineer)
- Date: 2026-05-28
- Wave: W4 → W5 衔接, Sprint-3 落地前的 hard ask
- 受 ticket: W4 Wave 21 (老胡 / 老雷派单)
- 关联:
  - `docs/RESEARCH/xiaosu-ui-wireframe-v0.1.md` (我 v0.1 wireframe)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md` (RM 状态机 + 21 reject + sub_reason)
  - `docs/RESEARCH/laotang-audit-schema-v1.1.md` (audit envelope + 14 AET + hash chain)
  - `docs/RESEARCH/xiaojiang-paper-engine-skeleton-v1.md` (paper signer + VirtualMatcher + shadow_audit)
  - `docs/RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md` (8 sport + 11 TimeStatus)
  - `docs/RESEARCH/xiaoxiao-kelly-slippage-model-v1.md` (SlippageEstimate + 5 enum)
  - `docs/RESEARCH/xiaodong-stats-validation-framework-v1.md` (M4.5 7 hard gate)
- 上呈: 老韩 (RM) / 老唐 (audit) / 小蒋 (paper) / 老李 (PM client) / 小卢 (signal) / 小董 (gate) / 老周 (gateway) / 老沈 (security)

---

## 0. TL;DR

W4 后端代码已落 (193/193 测试过), 但**没人问过前端要什么 endpoint**. 本文给 4 个 owner 列 hard ask, 让 W5 之前 API 定义先冻结, 否则 Sprint-3 前端 stub 没法写.

**4 个核心 ask:**
1. 不要让 UI 直读 WAL 文件 (R-11 paper 不污染 prod, 但 UI 也不该读 binary), 后端必须暴露 HTTP/WSS gateway
2. RM 21 reject enum + 5 sub_reason 必须带 string label 暴露给 UI (不要让前端硬编码)
3. audit event 必须支持 WSS push + REST replay (since cursor 翻页), hash chain 校验 endpoint 独立
4. paper / live mode 必须 build-time 锁, UI 显示 read-only badge, 切换需重 deploy (R-7 红线)

**UI 不要 us 级**: us 是热路径决策预算 (老韩 evaluate p99 < 200us, 老姜 latency budget), UI 100ms 完全够. UI 拉慢一点没事, **绝对不能让 UI 拖慢热路径**.

---

## 1. 整体架构 (UI 数据流)

```
   [hot path: trader process]         [UI gateway, vCPU6+, 独立进程]
   ┌──────────────────────┐            ┌───────────────────────────┐
   │ RiskGateway / Audit  │──IPC/WAL──>│ stcpp-ops-gateway          │
   │ PaperSigner / Match  │  4 类 WAL  │  REST /api/* (read)        │
   │ GoalserveClient      │            │  WSS  /feed/* (push)       │──HTTPS+WSS(mTLS)──> Web / CLI / Grafana
   │ PinnacleNoVig / M45  │            │  operator-only + rate 10/s │
   └──────────────────────┘            └───────────────────────────┘
```

**隔离原则:** UI 进程物理隔离 trader; gateway 仅 read-only WAL + IPC; 写操作 (halt / mode 试切换) 走独立 control plane + 独立 audit (`AET_OPERATOR_ACTION`); gateway 挂 != trader 挂. **派 @老周 ack** 这个 gateway 进程归你架构.

---

## 2. Part 1: REST endpoint 需求清单 (24 个)

| ID | Endpoint | Method | Owner | 用途 | 期望 latency | refresh | 核心字段 |
|----|----------|--------|-------|------|--------------|---------|----------|
| R-01 | `/api/health` | GET | 老周 | gateway 自身存活 + 上游 trader IPC 状态 | < 20ms | 5s | `status` / `trader_alive` / `gateway_uptime_s` |
| R-02 | `/api/risk/state` | GET | 老韩 | 当前 RM 5 态 + 触发原因 | < 50ms | 1s | `state` / `since_ts` / `last_transition_reason` / `bankroll_usdc` |
| R-03 | `/api/risk/state/history?from=&to=` | GET | 老韩 | 状态机迁移历史 (24h 默认) | < 200ms | 30s | `[{from, to, ts, trigger, audit_id}]` |
| R-04 | `/api/risk/rejects/codes` | GET | 老韩 | 21 reject enum + 5 sub_reason **string label 字典** | < 50ms | 启动一次 | `{code_id: 17, code_name: "INVALID_INTENT", sub_reasons: [...]}` |
| R-05 | `/api/risk/events?limit=100&since_audit_id=...&reject_only=false` | GET | 老唐 | audit event log 翻页 (cursor based) | < 200ms | on demand | 见 §4 RmEvent |
| R-06 | `/api/risk/audit/verify?from=&to=` | GET | 老唐 | hash chain 校验 (BLAKE3 重算) | < 2s | on demand | `verified` / `chain_breaks: []` / `last_verified_audit_id` |
| R-07 | `/api/risk/audit/chain/head` | GET | 老唐 | 当前 hash chain head (live cursor) | < 50ms | 5s | `audit_id` / `current_hash` / `seq` |
| R-08 | `/api/paper/positions` | GET | 小蒋 | 当前 paper position 列表 | < 100ms | 1s | 见 §4 PositionRecord |
| R-09 | `/api/paper/positions/:market_id` | GET | 小蒋 | 单 market 仓位明细 (含历史 fill) | < 100ms | 5s | PositionRecord + `fills: [FillRecord]` |
| R-10 | `/api/paper/fills?limit=200&since=&market_id=&signal_id=` | GET | 小蒋 | paper fill history (多过滤维度) | < 200ms | on demand | 见 §4 FillRecord |
| R-11 | `/api/paper/pnl/series?from=&to=&bucket=1m` | GET | 小蒋 | 时序 PnL (用于曲线绘制) | < 500ms | 30s | `[{ts, realized, unrealized, total}]` |
| R-12 | `/api/paper/pnl/summary?window=today\|7d\|30d` | GET | 小蒋 | PnL 汇总 (Sharpe / DD / win rate) | < 200ms | 60s | `realized` / `unrealized` / `sharpe` / `max_dd` / `n_trades` / `win_rate` |
| R-13 | `/api/signals/feed?limit=50&strategy_tag=` | GET | 小卢 + 小程 | 最近 signal trigger / suppress | < 100ms | 1s | 见 §4 SignalTrigger |
| R-14 | `/api/signals/stats?window=1h` | GET | 小卢 | 各 strategy_tag 触发/抑制/edge 分布 | < 200ms | 30s | `[{tag, fired, suppressed, edge_bps_p50, edge_bps_p99}]` |
| R-15 | `/api/markets/active?sport=&type=` | GET | 老李 + 小冯 | 活跃 PM market 列表 + LiveSection 标签 | < 200ms | 5s | `market_id` / `sport` / `time_status` / `live_section` / `tick_size` / `book_l1` |
| R-16 | `/api/markets/:market_id` | GET | 老李 | 单 market 完整盘口 (book + 行情 + ts) | < 200ms | 1s | book / line / 4 ts / vendor source |
| R-17 | `/api/gates/m45` | GET | 小董 | 7 hard gate 当前值 + pass/fail | < 500ms | 5min | 见 §4 M45GateState |
| R-18 | `/api/gates/m45/history?from=&to=` | GET | 小董 | gate 历史 (按 evaluate 时刻) | < 500ms | 1h | `[{evaluate_ts, g1..g7, decision}]` |
| R-19 | `/api/strategies/decay` | GET | 老韩 + 小董 | Bayesian decay 监控 (`P(μ<0)` per strategy) | < 200ms | 1h | `[{tag, p_mu_neg, sustained_days, state}]` |
| R-20 | `/api/data/sources/heartbeat` | GET | 小余 | 5 data source 心跳 age (poly_wss / clob / gamma / goalserve / rpc) | < 100ms | 1s | `[{source, age_seconds, last_ok_ts, state}]` |
| R-21 | `/api/data/rate_limits` | GET | 小余 + 老陈 | 各 vendor rate-limit 剩余配额 | < 100ms | 5s | `[{vendor, remaining, total, reset_at}]` |
| R-22 | `/api/operator/halt` | POST | 老韩 | **紧急 halt** → SAFE_MODE (双确认 + audit) | < 50ms ack | on demand | req: `{reason, ack_token}`; resp: `{audit_id, new_state}` |
| R-23 | `/api/operator/mode` | GET | 老周 | 当前 ExecutionMode (read-only, build-time 锁) | < 20ms | 5s | `{mode: "Paper"\|"Shadow"\|"Live"\|"Backtest", binary_sha256, locked: true}` |
| R-24 | `/api/operator/actions?limit=50` | GET | 老唐 | operator action audit (halt / resume / mode 切换尝试) | < 200ms | on demand | `[{action, actor, ts, audit_id, result}]` |

**REST 通用约定:**
- JSON only (不 proto), 字段命名 snake_case
- 时间字段一律 `*_ts_ns` (int64 epoch_ns UTC, R-20 4 ts 契约)
- 分页: cursor based (`since_audit_id` / `since_seq`), 不用 offset (避免 audit 流大时性能塌)
- error 格式: `{error: {code: "STR_CODE", message: "...", retry_after_s?: N}}`
- rate limit: 10 req/s per UI client, gateway 单实例 200 rps 上限 (老周 review)

---

## 3. Part 2: WSS subscribe 需求清单 (10 个)

实时数据走 push, 不轮询. UI 端 reconnect 走指数退避 + cursor 续传.

| ID | URL | Owner | 用途 | 期望 throughput | 期望端到端 latency | payload |
|----|-----|-------|------|----------------|--------------------|---------|
| W-01 | `wss://internal/feed/rm_state` | 老韩 | RM 状态机迁移 (5 态变化) push | < 1 msg/s | < 100ms | `RmStateSnapshot` (§4) |
| W-02 | `wss://internal/feed/risk_events` | 老唐 | audit event 实时 (12+2 AET 全 push) | 5~50 msg/s peak | < 100ms | RmEvent / AuditEnvelope |
| W-03 | `wss://internal/feed/risk_rejects` | 老韩 | 仅 reject 子集 (高优先级 UI 闪烁告警) | 1~10 msg/s peak | < 50ms | RmEvent (filtered) |
| W-04 | `wss://internal/feed/paper_fills` | 小蒋 | 每笔 VirtualFill push | 1~5 msg/s peak | < 100ms | FillRecord (§4) |
| W-05 | `wss://internal/feed/positions` | 小蒋 | position 变化 (新仓 / 平仓 / unrealized 更新, throttled 1Hz) | 1 msg/s | < 200ms | `[PositionDelta]` |
| W-06 | `wss://internal/feed/signal_triggers` | 小卢 + 小程 | signal output stream (fire + suppress) | 10~100 msg/s peak | < 100ms | SignalTrigger (§4) |
| W-07 | `wss://internal/feed/market_data?market_ids=...` | 老李 | PM book 价格变化 (throttled 10Hz per market) | 视订阅数, 单 market < 10/s | < 200ms | `{market_id, mid, l1_bid, l1_ask, ts}` |
| W-08 | `wss://internal/feed/data_health` | 小余 | 5 data source heartbeat 状态变化 | < 1 msg/s | < 200ms | `{source, state, age_seconds}` |
| W-09 | `wss://internal/feed/operator_actions` | 老唐 | operator 动作 audit push (跨 UI session 同步) | < 1 msg/s | < 100ms | `OperatorAction` |
| W-10 | `wss://internal/feed/alerts` | 小郑 | 告警事件 (P0/P1, 与 PagerDuty 同源) | 视事件, < 1/min 正常态 | < 100ms | `{severity, source, msg, ts}` |

**WSS 协议约定:**
- subscribe 模式: `{op: "subscribe", topic: "...", since_seq?: N, filters?: {...}}`
- 服务端 ack: `{op: "subscribed", topic, current_seq}`
- payload frame: `{seq, topic, payload, server_ts_ns}` (seq 单调, 用于 client gap 检测)
- heartbeat: server 每 10s 发 `{op: "ping", server_ts_ns}`, client 30s 不收到 ping → 主动 reconnect
- reconnect: 带 `since_seq` 续传, gateway buffer 最近 5min event (老韩 / 老唐 review buffer 大小)
- backpressure: client 跟不上 → gateway 主动 close + send `{op: "lag_disconnect"}`, UI 显示降级

**绑定 R-11 红线**: WSS feed 必须**只读**, 不接受 client 写 (operator halt 走 REST, 不走 WSS)

---

## 4. Part 3: Schema 字段需求 (7 核心 struct)

### 4.1 `PositionRecord` (小蒋 owner)

```jsonc
{
  "market_id": "0xabc...123", "sport": "NBA",
  "side": "BUY_YES",                // BUY_YES / BUY_NO / SELL_YES / SELL_NO
  "size_usdc": 1234.56, "avg_entry_price": 0.5234, "current_mid": 0.5301,
  "unrealized_pnl": 7.85, "realized_pnl": 0.0, "fee_paid": 1.23, "n_fills": 3,
  "opened_at_ts_ns": 1717000000000000000, "last_update_ts_ns": 1717000300000000000,
  // R-20 4 ts (最近一次更新源)
  "event_ts_ns": ..., "data_source_ts_ns": ..., "ingestion_ts_ns": ..., "as_of_ts_ns": ...,
  "strategy_tag": "nba_pace",
  "live_section": "INPLAY_HOT"      // 小卢 5 enum: PREGAME_FAR/NEAR/INPLAY_HOT/COLD/SETTLED
}
```

### 4.2 `FillRecord` (小蒋 owner)

```jsonc
{
  "audit_id": "01H...", "signal_id": "01H...", "intent_id": "01H...",
  "market_id": "0xabc...", "side": "BUY_YES",
  "ordered_price": 0.5230, "filled_price": 0.5234,  // 含 slippage
  "ordered_size_usdc": 1234.56, "filled_size_usdc": 1234.56,  // Mode A++ floor=0.5 全成或全不成
  "slippage_bps": 7.6,              // 小肖 SlippageModel
  "fee_usdc": 1.23, "fill_rate_sampled": 0.62,      // Bernoulli p
  "fill_outcome": "FILLED",         // FILLED / UNFILLED_FLOOR / UNFILLED_BERNOULLI
  // 4 ts (R-20) + signer 透传 (老孙 v5.1 §5.4)
  "event_ts_ns": ..., "data_source_ts_ns": ..., "ingestion_ts_ns": ..., "as_of_ts_ns": ...,
  "sign_request_ts_ns": ..., "sign_complete_ts_ns": ...,
  "virtual_tx_hash": "paper_01H...",  // paper 前缀, live 真 tx
  "execution_mode": "Paper"
}
```

### 4.3 `RmEvent` (老唐 owner, audit 投影)

```jsonc
{
  "audit_id": "01H...", "seq": 192834,         // hash chain seq, 单调
  "current_hash": "blake3:abc...", "prev_hash": "blake3:def...",
  "event_type": "AET_ORDER_REJECTED",          // 14 AET string label
  "event_type_id": 4,                          // 数值, UI 不依赖字符串
  "decision": "REJECTED",                      // APPROVED / REJECTED / DRAINED
  // 仅 REJECTED 填
  "reject_code": 17, "reject_code_label": "INVALID_INTENT",  // 字典见 R-04
  "sub_reason": 3, "sub_reason_label": "SUB_REASON_NAN_OR_INF",  // 仅 code=17 时 != 0
  "intent_summary": {"market_id": "0x...", "side": "BUY_YES", "size_usdc": 500.0, "price": 0.55, "strategy_tag": "nba_pace"},
  "bankroll_snapshot_usdc": 1234567.0,
  "rule_trace": ["R3.1", "R3.4"],              // 老韩 v0.3.1 §3.10.x
  "event_ts_ns": ..., "data_source_ts_ns": ..., "ingestion_ts_ns": ..., "as_of_ts_ns": ...,
  "data_source_ts_source": "UPSTREAM_PAYLOAD"  // R-20 §2.2 enum
}
```

### 4.4 `SignalTrigger` (小卢 + 小程 owner)

```jsonc
{
  "signal_id": "01H...", "strategy_tag": "nba_pace", "market_id": "0x...",
  "side": "BUY_YES",
  "edge_bps": 4.2, "edge_ci_low_bps": 1.1, "edge_ci_high_bps": 7.3,  // 老韩 R8 CI 下界
  "confidence": 0.72, "kelly_size_usdc": 1234.56,
  "live_section": "INPLAY_HOT",         // 5 enum
  "outcome": "FIRED",                   // FIRED / SUPPRESSED_THROTTLE / _COOLDOWN / _NEG_CI / _LIVE_SECTION
  "alpha_decay_s_est": 12,
  "event_ts_ns": ..., "data_source_ts_ns": ..., "ingestion_ts_ns": ..., "as_of_ts_ns": ...
}
```

### 4.5 `M45GateState` (小董 owner)

```jsonc
{
  "evaluated_at_ts_ns": ..., "window_from_ts_ns": ..., "window_to_ts_ns": ...,
  "n_trades": 87, "overall_decision": "HOLD",   // PASS / HOLD / FAIL
  "gates": {
    "g1_pnl":       {"pass": true,  "actual": 4523.0, "threshold": 0.0,  "p_value": 0.04},
    "g2_sharpe":    {"pass": true,  "actual": 1.23,   "ci_lower": 0.42,  "threshold": 1.0},
    "g3_risk_fail": {"pass": true,  "actual": 0,      "threshold": 0},
    "g4_uptime":    {"pass": true,  "actual": 0.997,  "threshold": 0.995},
    "g5_max_dd":    {"pass": true,  "actual": 0.062,  "threshold": 0.08},
    "g6_n_trades":  {"pass": true,  "actual": 87,     "threshold": 50, "duration_days": 18},
    "g7_vs_random": {"pass": false, "actual_pnl": 4523.0, "random_pnl_mean": 4012.0, "p_value": 0.18, "threshold_p": 0.10}
  },
  "advisory": {"dsr_score": 0.42, "pbo_score": 0.31}
}
```

### 4.6 `RmStateSnapshot` (老韩 owner)

```jsonc
{
  "state": "RUNNING",                   // RUNNING / WARNING / HALTED / SAFE_MODE / DRAIN
  "since_ts_ns": ...,
  "last_transition": {"from": "WARNING", "to": "RUNNING", "trigger": "goalserve_recover", "audit_id": "01H...", "ts_ns": ...},
  "bankroll_usdc": 1234567.0, "daily_pnl_usdc": 12345.0,
  "exposure_total_usdc": 225000.0, "exposure_pct": 0.30,
  "consec_loss_count": 2, "consec_loss_limit": 5,
  "strategies": [
    {"tag": "nba_pace", "state": "RUNNING", "decayed": false, "kelly_mult": 1.0},
    {"tag": "mlb_sp",   "state": "BLACK",   "decayed": true,  "kelly_mult": 0.0}
  ],
  "stale_age_by_source": {"poly_wss": 1.2, "goalserve": 2.4, "rpc": 0.8}
}
```

### 4.7 `OperatorAction` (老唐 owner)

```jsonc
{
  "audit_id": "01H...",
  "action": "EMERGENCY_HALT",           // EMERGENCY_HALT / DRAIN_ACK / MODE_VIEW
  "actor": "operator@stcpp.io",         // OIDC subject
  "actor_kind": "HUMAN",                // HUMAN / AGENT (R-11: AGENT 不允许 halt)
  "ack_token": "yk_...",                // Yubikey 2FA token hash
  "reason": "manual stale data spike",
  "result": "SUCCESS",                  // SUCCESS / DENIED_AUTH / DENIED_RATE_LIMIT
  "ts_ns": ..., "ip_address": "10.0.x.x", "user_agent": "stcpp-ops/0.1 web"
}
```

---

## 5. Part 4: 鉴权 + 安全

**倾向 (待 @老沈 拍板):**

| 层 | 方案 | 理由 |
|----|------|------|
| 内网传输 | **mTLS** (gateway 单边 cert + UI client cert) | 内网, 不暴公网, mTLS 比 token 强 |
| 用户认证 | **OIDC SSO + Yubikey 2FA** | wireframe v0.1 §1.2 已对齐 |
| 高危操作 | **Yubikey touch 二次确认** + 3s 倒计时 | 凌晨不能按错 (wireframe §1.1) |
| API rate | 10 req/s per token, gateway 全局 200 rps | 防 UI 死循环打挂 gateway |
| audit | **所有写操作 (R-22 halt / R-23 mode-view-attempt)** 必须 emit `AET_OPERATOR_ACTION` | R-11 + 老唐 v1.1 联签 |

**敏感数据脱敏 (UI 永远看不到的):**
- 私钥 / mnemonic — 任何 API 不返回, 任何日志不含
- wallet address — 显示前 4 + 后 4 (`0xabc1...d234`), full address 仅 admin 角色 (单独 endpoint)
- Yubikey raw token — UI 仅传 hash, gateway 校验后即丢
- audit BLAKE3 内部 state — 仅 hash digest 暴露, 不暴露 intermediate

**ExecutionMode build-time 锁 (R-7 红线):**
- `R-23 /api/operator/mode` 仅 GET (read-only)
- UI 显示 `ExecutionMode` badge: `[PAPER]` 黄色 / `[SHADOW]` 灰色 / `[LIVE]` 红色 / `[BACKTEST]` 蓝色
- UI **不提供** mode 切换按钮, 提示 "切换需重新部署 binary" (老雷宪法第 7 条 minimum surprise)
- gateway 启动期读取 trader binary SHA256, 在 R-23 返回 `binary_sha256` 让 UI 校验

**派给 @老沈 review**: mTLS 还是 token-only? 内网假设是否够强? 紧急 halt 是否需要双人 ack (而非单人)?

---

## 6. Part 5: latency 期望矩阵

| 数据类 | 期望端到端 | refresh / push | 后端 owner | 备注 |
|-------|-----------|----------------|-----------|------|
| RM state badge | < 50ms (拉) / < 100ms (推) | 1Hz 拉 + WSS push | 老韩 | R-02 + W-01 |
| paper fill push | < 100ms (从 WAL append 到 UI 渲染) | WSS push | 小蒋 | W-04 |
| position list | < 100ms | 1Hz refresh | 小蒋 | R-08 |
| paper PnL series | < 500ms (查询 100k 点) | 30s refresh | 小蒋 | R-11, 用 DuckDB on WAL |
| audit chain verify | < 2s (单次, 万级 event) | on demand | 老唐 | R-06, 后端可异步, UI 显 loading |
| audit event 翻页 | < 200ms | on demand | 老唐 | R-05 cursor based |
| signal feed | < 100ms (推) | WSS + 1Hz pull | 小卢 | W-06 + R-13 |
| market data | < 200ms (10Hz throttled) | WSS | 老李 | W-07, throttle 是为了带宽 |
| M4.5 gate | < 500ms | 5min refresh | 小董 | R-17 |
| data heartbeat | < 100ms | 1Hz + WSS push 状态变化 | 小余 | R-20 + W-08 |
| operator halt ack | < 50ms ack (audit 异步可后落) | on demand | 老韩 | R-22, 关键路径 |

**重申**: UI **不要求** us 级延迟. us 是热路径决策预算 (老韩 evaluate p99 < 200us, 老姜 latency budget), UI 100ms 完全够看. **绝不能让 UI 查询拖慢热路径**, 因此 gateway 必须独立进程 + read-only IPC 或独立 WAL reader.

---

## 7. Part 6: 错误处理 + 降级

**后端任一服务挂掉, UI 必须展示降级状态而非空白:**

| 场景 | UI 行为 | 后端约束 |
|------|--------|---------|
| gateway 进程挂 | UI 显示 "gateway offline" 全屏灰 banner, 但保留最后一次成功数据 (stale 标灰) | UI 端 reconnect 退避 |
| trader 进程挂 (gateway 还活) | R-01 health 返回 `trader_alive: false`, UI 全屏红 banner + Operator Console 禁所有写 | gateway 上报 health |
| 单个 WSS feed 断 | 该 panel 显示 "stale (last update 5s ago)", 其他 panel 不影响 | WSS reconnect + cursor 续传 |
| RM = SAFE_MODE | **全屏红色 banner + 蜂鸣 (可关) + Operator Console halt 按钮变灰**, audit log 自动展开最新 5 条 reject | gateway WSS push state 变化 |
| RM = DRAIN | 顶部黄色 banner + open orders panel 闪烁, 显示 "draining, no new orders" | 同上 |
| audit chain 校验失败 (R-06) | 危险红色 modal "AUDIT CHAIN BROKEN at seq=NNN", 必须 ack 才能继续 | gateway 不应自动恢复 |
| paper/live 模式不匹配 (binary sha 改了) | UI 强制 reload + 顶部红色 "mode changed, reload" | R-23 暴露 binary_sha256 |
| WSS reconnect 期间 (一般 < 3s) | panel 角标灰色 "pending", 数据不更新但不清空 | client 端处理 |

**告警声音 (老雷宪法第 5 条简洁优于花哨, 但 SAFE_MODE 例外):**
- 默认全静音
- 仅 RM 进入 SAFE_MODE / HALTED + audit chain break → 蜂鸣一次 (操作员可永久静音, 但记 audit)

---

## 8. Part 7: 4 hard ask (给 4 个后端 owner)

### 8.1 @老韩 (RM) — deadline W5 周二 (2026-06-09)

1. **RM state 必须可 GET 拉到** (R-02): 5 态 enum + since_ts + last_transition reason + bankroll snapshot
2. **21 reject enum + 5 sub_reason 必须出 string label 字典** (R-04): 现 cpp 是 enum id, UI 不要硬编码 label, 走 endpoint 拉
3. **状态机迁移历史可查** (R-03): 24h window cursor 翻页, 用于 D3 风控大盘 panel 2
4. **strategy decay 状态可查** (R-19): 每 strategy_tag 的 `P(μ<0)` + sustained_days + state (RUNNING / WARNING / BLACK / MONITORING)
5. **WSS push state 变化** (W-01): 不要轮询, 状态变化立刻 push, 期望端到端 < 100ms

### 8.2 @老唐 (audit) — deadline W5 周二

1. **audit event 必须支持 WSS push + REST replay** (W-02 + R-05): 不只是 binary WAL, 必须有 JSON 投影 endpoint
2. **hash chain 校验独立 endpoint** (R-06): UI 可触发 from/to 区间校验, BLAKE3 重算, 返回 chain_breaks 列表
3. **cursor 翻页** (R-05): since_audit_id 续传, 不用 offset (audit 流大时性能塌)
4. **reject_only filter** (R-05): UI 端常用过滤, 在 server 侧做减带宽
5. **AET enum + label 字典** (合并到 R-04 或独立): 14 AET 数值 + 字符串 label

### 8.3 @小蒋 (paper) — deadline W5 周二

1. **paper_audit.wal 必须可 query (不只是 binary 流)** (R-09 / R-10): 按 time range / market_id / signal_id 过滤, 用 DuckDB on WAL 或独立 index sqlite (老王 v0.2 §2.1 header 已有 audit_id)
2. **position list endpoint** (R-08): 当前所有持仓 + unrealized pnl + 4 ts
3. **PnL 时序 endpoint** (R-11): 用于 D1 / Dashboard 曲线, 支持 bucket 粒度 (1m / 5m / 1h)
4. **每笔 fill WSS push** (W-04): 从 VirtualMatcher 出 fill 那一刻 push, 期望端到端 < 100ms
5. **paper / shadow / live 数据物理隔离 (R-11)**, gateway 必须区分**只读** paper_audit.wal vs shadow_audit.wal, 不能混

### 8.4 @老周 (架构) — deadline W5 周一 (2026-06-08)

1. **独立 ops-gateway 进程, vCPU6+**: 我画的拓扑 §1, 物理隔离, 不进热路径
2. **read-only WAL reader 或 IPC query**: UI 绝不能直读 WAL 文件 (R-11 + R-12 风险), 必须经 gateway 转发
3. **HTTPS/WSS over mTLS** (待 @老沈 一起拍): 内网, 不暴公网, mTLS 单独 cert per UI client
4. **rate limit + backpressure**: gateway 单实例 200 rps 上限, 超了主动 503 + UI 显示降级
5. **gateway 自身 audit**: UI 所有写操作 (R-22 halt) 走 `AET_OPERATOR_ACTION`, 这条不走 trader 的 RiskGateway (避免循环依赖)
6. **ExecutionMode build-time 锁的暴露 (R-23)**: trader binary SHA256 启动时刻冻结, gateway 读取后透传给 UI, UI 校验

---

## 9. 不耻下问 (W5 前必答)

| # | 问 | 找谁 | 截止 | 状态 |
|---|---|---|------|------|
| Q-UI1 | mTLS 还是 token? 内网假设是否够强? 紧急 halt 是否单人 ack 够 | @老沈 (security) | W5 周一 | OPEN |
| Q-UI2 | RM 21 reject + 5 sub_reason string label 字典格式 (i18n? 中英文?) | @老韩 + @小尤 (UX) | W5 周一 | OPEN |
| Q-UI3 | audit hash chain verify 是 BLAKE3 全量重算 还是有 checkpoint? 万级 event < 2s 够吗 | @老唐 | W5 周一 | OPEN |
| Q-UI4 | paper_audit.wal query 走 DuckDB on WAL 还是独立 index sqlite (老王 v0.2 已有 header) | @小蒋 + @老王 | W5 周一 | OPEN |
| Q-UI5 | gateway 进程归谁 owner? 老周 架构 / 老吴 部署 / 小郑 obs? | @老周 + @老胡 (派单) | W5 周一 | OPEN |
| Q-UI6 | WSS buffer 大小 (reconnect 续传 5min 够吗)? 内存预算 | @老韩 + @老唐 | W5 周二 | OPEN |
| Q-UI7 | obs 栈的 Prom HTTP API 是否复用作为部分 UI 数据源 (避免重写) | @小郑 (obs) | W5 周二 | OPEN |
| Q-UI8 | M4.5 gate 当前值在 trader 进程内还是离线 Python 计算? endpoint 期望直接读 metrics.json? | @小董 | W5 周二 | OPEN |
| Q-UI9 | market data WSS (W-07) 10Hz throttle 是 gateway 端还是 trader 端做? | @老李 + @老周 | W5 周二 | OPEN |
| Q-UI10 | operator audit (AET_OPERATOR_ACTION) 是否单独 WAL 类还是进 risk_audit.wal | @老唐 | W5 周二 | OPEN |

---

## 10. 验收清单 (本文 v1 完成)

- [x] REST endpoint 清单 ≥ 20 (本文 24 个, R-01 ~ R-24)
- [x] WSS subscribe 清单 ≥ 8 (本文 10 个, W-01 ~ W-10)
- [x] 核心 schema ≥ 6 (本文 7 个, PositionRecord / FillRecord / RmEvent / SignalTrigger / M45GateState / RmStateSnapshot / OperatorAction)
- [x] 4 hard ask 后端 owner 列表 (§8: 老韩 / 老唐 / 小蒋 / 老周)
- [x] 鉴权 + 安全段 (§5)
- [x] latency 矩阵 (§6)
- [x] 错误处理 + 降级 (§7)
- [x] 不耻下问 list 10 问 (§9)
- [ ] @老韩 / @老唐 / @小蒋 / @老周 W5 周一回复
- [ ] @老沈 / @小郑 / @老李 / @小董 W5 周二回复
- [ ] W5 中拉 30min review 会 (@老胡 排), 关 10 Q-UI 问

---

## 11. 后续 (Sprint-3 落地)

W5 拿到回执 → 我 Sprint-3 (W7+) 跟 @老吴 (装机) 落 React stub:
- React 18 + TanStack Query + Tailwind (wireframe v0.1 §1.2)
- WSS client 用 native WebSocket + 自家 reconnect manager (不用 SockJS, 内网纯净环境)
- 数据层走 R-01 ~ R-24 + W-01 ~ W-10, schema 直接复用 §4 (TS interface 从 JSON schema 生成)
- 第一个落地页 = Dashboard (RM state + PnL + 7 hard gate + heartbeat) — 与 wireframe v0.1 §2.2 D1 对齐
- 第二个落地页 = Trades (paper fill table + position list) — wireframe §3 Operator Console 上半

**不在本文范围:**
- 前端代码 (W5+ 与老吴一起)
- Grafana dashboard JSON (与小郑 §8 对齐, 那边走 Prom HTTP API 不走我这边 gateway)
- CLI `stcpp-ctl` (小宫 dogfood §A 已有命令清单, 走 trader 本机 UDS 不走 gateway)

---

**END v1.** 等 W5 周一 4 个 hard ask 回执 + 周二 review 会闭环 10 Q-UI 问.

— 小苏, 2026-05-28
