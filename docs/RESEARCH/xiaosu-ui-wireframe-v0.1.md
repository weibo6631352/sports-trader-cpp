# 操作员 UI Wireframe v0.1

- Owner: 小苏 (frontend-engineer)
- Date: 2026-05-28
- 验收人: 老胡 (pm) + 小尤 (ux) + 小郑 (observability)
- 关联:
  - `docs/RESEARCH/xiaozheng-observability-v0.1.md` (78 metric + 6 dashboard 蓝图)
  - `docs/RESEARCH/xiaoyou-ux-framework-v1.md` (7 维 + 4 色法 + P0~P4 告警)
  - `docs/RESEARCH/xiaogong-dogfood-playbook-v1.md` (11 边缘场景 + `stcpp-ctl` 命令清单)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` (RM 4 态 + SAFE_MODE + STALE 阈值 2s/5s/10s)
- 受 ticket: S1-024 (假, 待老胡分配)
- 依赖 ticket: S1-018 (小郑 metric), S1-022 (小尤 UX 框架), S1-023 (小宫 dogfood)

---

## 0. TL;DR (老雷 + 老胡看这一段)

- 公司没有 retail UI, 内部"用户" = 操作员 / 量化 / 风控 / SRE, 一天盯 8~12h.
- **UI = Grafana 5 个 dashboard + 1 个 web Operator Console (操盘控制台) + 1 个 CLI `stcpp-ctl`**, 三件套. 不做 native app, 不做 mobile.
- **第一个该上线的 dashboard = D2 数据健康大盘 (小余视角)**, 与小郑钦定一致, 也是 S1-021 跨洋实测唯一验收手段.
- **设计纪律**: 一屏 ≤ 12 panel, 4 色法 (绿/黄/红/灰), 单 fixed-width 字体, 单位强制后缀 (`_us` / `_ms` / `_pct` / `$`), 不闪烁不动画.
- **告警与 P0~P4 严重度对齐小尤 §4**, P0 强制 modal + 倒计时取消, P1 顶 toast, P2 侧栏 list, P3/P4 不进 UI.
- **chrome-devtools MCP 用法 = "盯 console + network + Grafana panel 渲染"**, 小宫 dogfood / 小尤 UX 评分都可调用, §7 给了 4 个具体示例.
- **多人协作**: handover 协议靠 Operator Console 的 "签到/交接" 按钮 + `stcpp-ctl handoff`, 不靠口头.
- **时区**: 操作员 UI 默认 ET (与 Polymarket 服务器同区), 右上角带 "ET / 国内 +12/+13" 双显, 不让国内 SRE 心算.

---

## 1. 总体设计哲学

### 1.1 三条铁律 (取自小尤 §6 + 老雷宪法)

1. **凌晨 3 点不能按错**: 高危按钮 (HALT / DRAIN / RESUME-ACK / CANCEL-ALL) 全部二次确认 + 3s 倒计时取消, 不允许"一击致命".
2. **一屏装得下**: 主屏 1080p 不滚动 (Grafana D1 + Operator Console), 1440p 留余量给侧栏.
3. **沉默是金**: 没事的时候屏幕安静. 颜色只有"绿/灰"是正常态, 红黄是有事. 不允许炫技动画 / 闪烁广告牌.

### 1.2 技术栈选型 (与小郑 obs 栈对齐)

| 用途 | 选型 | 理由 |
|---|---|---|
| Dashboard | **Grafana OSS** (小郑 §1.1 已定) | 不重复造轮子, 与 Prom/Loki/Tempo 同家联动 |
| Operator Console (web) | **React 18 + TanStack Query + Tailwind** | 团队 SaaS 经验, 不上 SSR (内网用) |
| Operator Console 数据源 | **Prom HTTP API + 自家 stcpp REST `/ops`** (待定与老周 v0.2) | 不直连 trader, 经 obs 节点中转 |
| CLI | **`stcpp-ctl`** (小宫附录 A 命令清单) | C++ binary, 与 trader 同 repo, share infra layer |
| Auth | **OIDC (内网 SSO) + Yubikey 2FA** (待老沈 review) | 高危按钮强制二次 (yubikey touch) |
| 实时推送 | **Grafana Live (WSS) + 自家 SSE 兜底** | Grafana panel 自动推, console 用 SSE 拉 RM 状态 |

### 1.3 不做的事 (老雷宪法第 5 条: 简洁优于花哨)

- ❌ 不做 mobile (放弃, 操作员就该坐显示器前)
- ❌ 不做 native app (web + CLI 够了)
- ❌ 不做 retail UI (公司没有 retail 用户)
- ❌ 不做"市场撮合可视化" (策略层不关心, 老梁看 D5 即可)
- ❌ 不做 in-house APM (Tempo + Grafana 联动够了)

---

## 2. Grafana D1~D5 Wireframe

### 2.1 共同约定

- **栅格**: 24 列 × 8 行 = 192 cell, 一屏 panel 数 ≤ 12 (小尤 §6.1)
- **时间范围**: 默认 `Last 1h`, 顶部 picker 可改, last refresh 5s
- **变量**: `$strategy_tag` / `$market_type` / `$source` (用 Grafana variable, 不写死 metric label, 取自小郑 §2 label whitelist)
- **panel 右上**: query inspector + 跳 Tempo (exemplar) 按钮
- **统一颜色变量** (Grafana var, 小尤 OBS-03):
  - `$green = #34a853` (正常)
  - `$yellow = #fbbc04` (WARN)
  - `$red = #ea4335` (异常 / 触发)
  - `$gray = #9aa0a6` (idle / no data)

### 2.2 D1 — 交易大盘 (操作员主屏)

**主用户**: on-call 操作员 (老胡 + 轮班), Persona = 小宫 dogfood §2 正常交易日.

**上线优先级**: P1 (M+2), 第二批上线 (D2 之后).

**panel 布局 (12 个 panel, 4 行 × 3 列):**

```
+-----------------------------+-----------------------------+-----------------------------+
| [1] RM STATE                | [2] BANKROLL & DAILY PnL    | [3] ALERT COUNT (P0~P2 actv)|
| ┌───────────────┐           | bankroll  $ 1,234,567       | P0  0    P1  0    P2  3     |
| │   RUNNING     │  GREEN    | today    +$  +12,345  +1.0% | (click → §3 console list)   |
| └───────────────┘           | dd_max   -$   -4,500  -0.4% |                             |
| since 08:30 ET (3h12m)      |                             | last fire: 11:42 [P2]       |
+-----------------------------+-----------------------------+-----------------------------+
| [4] OPEN ORDERS (by mkt)    | [5] HOTPATH p99 (us)        | [6] DATA SOURCE HEARTBEAT   |
| ML        18                | ┌───────────┐               | poly_wss      [GREEN]  1.2s |
| TOTALS    24  ▓▓▓▓▓▓        | │   312 us  │ <500 us SLO   | goalserve     [GREEN]  2.4s |
| SPREADS    7                | └───────────┘               | chain (RPC)   [GREEN]  0.8s |
| PROP       3                | exemplar → Tempo            | (click → D2 数据健康)         |
+-----------------------------+-----------------------------+-----------------------------+
| [7] TODAY ORDER FLOW        | [8] REJECT RATE (last 5m)   | [9] EXPOSURE (USDC, by mkt) |
|     submit / fill / reject  | reject  3.2%  WARN <5%      | ML       $120k  16%         |
| ML      120 / 98 / 22       | top reject:                 | TOTALS   $ 80k  11%         |
| TOTALS  87  / 71 / 16       |   STALE_DATA       12       | SPREAD   $ 25k   3%         |
| SPREAD  34  / 30 /  4       |   EDGE_CI_NEGATIVE  6       | TOTAL    $225k  30% / 50%   |
+-----------------------------+-----------------------------+-----------------------------+
| [10] RECENT AUDIT (last 10) | [11] WALLET BALANCE         | [12] NETWORK / CROSS-OCEAN  |
| 14:23  APPROVED  intent#A91 | usdc        $345,678        | poly api  rtt p99   42 ms   |
| 14:23  REJECTED  STALE_DATA | matic       $ 1,234         | rpc primary p99    180 ms   |
| 14:22  APPROVED  intent#A90 | (低于 $500 → 红, 老彭 limit) | rpc backup  p99    220 ms   |
| ... (click → console audit) |                             |                             |
+-----------------------------+-----------------------------+-----------------------------+
```

**关键指标 + 阈值 + 颜色:**

| Panel | 主指标 | 颜色规则 |
|---|---|---|
| 1 RM STATE | `stcpp_l4_state{state}` | RUNNING=绿, WARNING=黄, DRAIN=黄, HALTED=红, SAFE_MODE=红 |
| 2 PnL | `stcpp_l4_daily_pnl_usdc` | dd_today < 1% bankroll 绿, 1~3% 黄, > 3% 红 (与老韩 DAILY_LOSS_HALT 对齐) |
| 3 ALERT | `ALERTS{severity in [critical,warning]}` | P0 > 0 红, P1 > 0 红, P2 > 0 黄, 全 0 绿 |
| 4 OPEN ORDERS | `stcpp_l5_order_open_count` | 单 market_type 超 50% per_market_cap 黄, 超 80% 红 |
| 5 HOTPATH p99 | `histogram_quantile(0.99, stcpp_l3_signal_to_intent_seconds + l4_eval + l5_submit)` | < 500us 绿 (SLO), 500us~1ms 黄, > 1ms 红 (1min 持续 → P0 自动 DRAIN) |
| 6 HEARTBEAT | `stcpp_l2_wss_heartbeat_age_seconds` | < 2s 绿, 2~10s 黄, > 10s 红 (与 RM v0.2 STALE 阈值对齐, **不是 30s, 是 2s/10s**) |
| 7 ORDER FLOW | `stcpp_l5_order_submit_total - fill - reject` | 趋势图 + 文字, 5m 滚动 |
| 8 REJECT RATE | `stcpp_l4_reject_total / stcpp_l4_evaluate_total` 5m rate | < 5% 绿, 5~20% 黄, > 20% 红 |
| 9 EXPOSURE | `stcpp_l4_market_exposure_ratio` | 总 < 30% 绿, 30~50% 黄, > 50% 红 |
| 10 AUDIT | Loki tail (老韩 audit log 投影, OBS-04 小尤要求) | reject 行黄底, halt 行红底 |
| 11 WALLET | `stcpp_l5_wallet_balance_usdc` | > $1k 绿, $500~$1k 黄, < $500 红 (老彭 gas reserve 阈值) |
| 12 NETWORK | `stcpp_l1_net_rtt_seconds{peer}` | poly < 50ms 绿, < 100ms 黄, > 100ms 红 |

**panel 数: 12 (顶到小尤上限).** 不再加. 后续若要加新 panel 必须先砍一个.

### 2.3 D2 — 数据健康大盘 (小余视角) — **第一个上线**

**主用户**: 小余 (data) + 小董 (goalserve) + on-call.

**上线优先级**: **P0 (M+1, 第一个上)** — 小郑 §5 钦定, 老姜 §3 跨洋 RTT 实测 (S1-021) 唯一兜底.

**panel 布局 (10 个 panel, 4 行不满):**

```
+--------------------------------+--------------------------------+--------------------------------+
| [1] HEARTBEAT 三色灯            | [2] WSS RECONNECT (24h)        | [3] CROSS-OCEAN RTT (peer)     |
| poly_wss     [GREEN]  age 1.2s | reconnect total:  4            | poly_api  p50  35ms p99  42ms  |
| goalserve    [GREEN]  age 2.4s | last reason: idle_timeout      | rpc_alch  p50 160ms p99 180ms  |
| chain (RPC)  [GREEN]  age 0.8s | last duration: 1.8s            | rpc_qknd  p50 200ms p99 220ms  |
| (与 RM STALE 阈值对齐)          | (老韩 R3 重连 < 3s)             | goalserve p50  18ms p99  24ms  |
+--------------------------------+--------------------------------+--------------------------------+
| [4] WSS HEARTBEAT AGE 时序图    | [5] INGEST RATE (msg/s by src)| [6] STALE COUNTER (by source)  |
|     poly_wss  ────╮             | poly_wss   ▓▓▓▓▓▓▓ 1234        | poly_wss     stale_seconds 0   |
|                ╰──╯ stale spike | goalserve  ▓▓▓▓    687         | goalserve    stale_seconds 0   |
|     goalserve ────────          | chain      ▓        12         | chain        stale_seconds 0   |
| 阈值线 2s/10s 标 红黄虚线        |                                |                                |
+--------------------------------+--------------------------------+--------------------------------+
| [7] PARSE ERROR (5m, by src)   | [8] FIELD MISSING (5m)         | [9] ETL E2E p99 (ms, by src)   |
| poly_wss   parse_error  0      | poly_wss  inactive_list   0    | poly_wss   p99   4.2 ms        |
| goalserve  parse_error  2      | goalserve weather         0    | goalserve  p99   6.8 ms        |
| chain      parse_error  0      | goalserve sp_changed      0    | chain      p99   2.1 ms        |
| (click row → Loki query)       | (click → 跳到字段 schema diff) | SLO < 20ms                     |
+--------------------------------+--------------------------------+--------------------------------+
| [10] REST RATE-LIMIT REMAINING                                                                   |
|      poly_gamma     2980 / 3000   GREEN                                                          |
|      poly_clob       450 /  500   YELLOW (90% used)                                              |
|      goalserve      4200 / 5000   GREEN                                                          |
+--------------------------------------------------------------------------------------------------+
```

**关键阈值** (与 RM v0.2 §3.7 对齐, 不是 v0.1 的 30s/60s):

| source | WARN | HALT |
|---|---|---|
| poly_wss | age > 2s | age > 10s |
| goalserve | age > 5s | age > 15s |
| chain (RPC) | age > 10s | age > 30s |

**为什么 D2 第一个上 (重申 + 我的角度):**
1. 老姜 §3 跨洋 RTT 实测 (S1-021) 必须靠 panel 3+4 兜底
2. 小宫 §6.4 Goalserve 断 30s chaos drill 必须靠 panel 4 看
3. 我 (小苏) 做 Operator Console panel 6/12 直接 reuse D2 panel 1/3 (省工)
4. 不依赖策略 / 风控 / 执行任何上层, ROI 最高

### 2.4 D3 — 风控大盘 (老韩视角)

**主用户**: 老韩 (risk) + 老唐 (audit) + on-call 排查拒单.

**上线优先级**: P1 (M+2), 与 D1 同批.

**panel 布局 (12 panel):**

```
+--------------------------------+--------------------------------+--------------------------------+
| [1] STATE MACHINE              | [2] STATE TRANSITION (24h)     | [3] BANKROLL TIMELINE          |
| RUNNING     ████████████   3h12| RUNNING→WARNING  4             |        ┌─────────────────┐     |
| WARNING     ██             18m | WARNING→RUNNING  3             |        │ ╱╲ ╱─╲╱─╮       │     |
| DRAIN                       0  | WARNING→HALTED   0             |        │╱   ╲              │     |
| HALTED                      0  | HALTED→DRAIN     0  (双人 ack) |        └─────────────────┘     |
| SAFE_MODE                   0  | last transition: 11:42 STALE  | dd_today  -$ 4,500  -0.4%      |
+--------------------------------+--------------------------------+--------------------------------+
| [4] REJECT HEATMAP (reject_code × market_type, last 1h)                                          |
|              ML  TOTALS SPREAD PROP  OUTRIGHT 系列  分节                                          |
| STALE_DATA   3    2     0     0      0       0    1     yellow (>5)                              |
| EDGE_CI_NEG  6    4     1     0      0       0    0     yellow                                   |
| INSUF_BANK   0    0     0     0      0       0    0                                              |
| EXPO_LIMIT   0    0     0     0      0       0    0                                              |
| PER_ORDER    1    0     0     0      0       0    0                                              |
| (click cell → 跳 Tempo trace + Loki audit grep)                                                  |
+--------------------------------+--------------------------------+--------------------------------+
| [5] EXPOSURE BY MARKET_TYPE    | [6] EVALUATE p99 (us)          | [7] AUDIT WRITE LATENCY        |
| (与 D1 panel 9 同源)            | ┌───────┐ < 200us SLO         | p99   180 us   GREEN           |
|                                | │ 145us │ exemplar → Tempo     | error_total  0  GREEN          |
+--------------------------------+--------------------------------+--------------------------------+
| [8] CONSEC LOSS COUNTER        | [9] IDEMPOTENCY HIT RATE       | [10] RECON DRIFT (ledger↔chain)|
| current  2 / 5 (HALT @ 5)      | hit_total       1234           | drift  $0.00   GREEN           |
| (老韩 CONSEC_LOSS_HALT)         | hit_rate        0.3%           | last verify: 14:25 (5m ago)    |
+--------------------------------+--------------------------------+--------------------------------+
| [11] AUDIT TAIL (last 20, operator-readable, OBS-04 小尤要求)                                    |
| 14:23:01  APPROVED  intent#A91  NBA-ML  size=$120  edge=4.2bps  ci_low=1.1                       |
| 14:23:00  REJECTED  intent#A90  reason=STALE_DATA  source=goalserve  age=6.2s  thr=5s            |
| 14:22:55  STATE     RUNNING→WARNING  trigger=goalserve_stale_5s                                  |
| ...                                                                                              |
+--------------------------------------------------------------------------------------------------+
| [12] STRATEGY ENABLED MATRIX  (strategy_tag × market_type, 0=灰, 1=绿)                            |
+--------------------------------------------------------------------------------------------------+
```

### 2.5 D4 — 链上状态 (老叶 + 老孙 视角)

**主用户**: 老叶 (RPC) + 老孙 (signer) + on-call 链上排查.

**上线优先级**: P1 (M+2).

**panel 布局 (9 panel):**

```
+--------------------------------+--------------------------------+--------------------------------+
| [1] RPC PROVIDER ACTIVE        | [2] RPC LATENCY (by provider)  | [3] RPC ERROR RATE (5m)        |
|   active = alchemy   [GREEN]   | alchemy   p99  180ms           | alchemy   err_rate  0.1%  GR   |
|   backup = quicknode [STANDBY] | quicknode p99  220ms           | quicknode err_rate  0.2%  GR   |
|   last failover: 23h ago       | (failover @ 5xx > 3 in 30s)    |                                |
+--------------------------------+--------------------------------+--------------------------------+
| [4] NONCE STATE                | [5] NONCE GAP COUNTER          | [6] GAS PRICE (gwei)           |
| wallet#1   current  4523       | gap_total  0    GREEN          | current   42 gwei   GREEN      |
| wallet#1   pending tx  2       | (gap > 0 → P0 自动 HALT)        | 1h p99    78 gwei   GREEN      |
|                                |                                | spike > 200 → P2 (老彭)        |
+--------------------------------+--------------------------------+--------------------------------+
| [7] WALLET BALANCE             | [8] SIGN LATENCY (us)          | [9] FILL LATENCY (submit→fill) |
| usdc       $345,678   GREEN    | p99    62us   GREEN            | p50     320ms                  |
| matic      $1,234     GREEN    | (老孙 50us 目标 + 20us slack)   | p99     1.2s                   |
| (gas reserve > $500 阈值)       | err_total  0  GREEN            |                                |
+--------------------------------+--------------------------------+--------------------------------+
```

### 2.6 D5 — 信号监控 (小梁视角)

**主用户**: 小梁 (quant) + 小程 (signal) + 小蒋 (mm) + 小袁 (portfolio).

**上线优先级**: P2 (M+3), 最后上.

**panel 布局 (11 panel):**

```
+--------------------------------+--------------------------------+--------------------------------+
| [1] SIGNAL FIRE RATE (by tag)  | [2] SIGNAL SUPPRESSED (5m)     | [3] STRATEGY ENABLED MATRIX    |
| nba_pace      ▓▓▓ 12/min       | nba_pace   throttle    2       | (strategy × market_type)       |
| nba_lineup    ▓   3/min        | nba_lineup cooldown    1       | mm   ML  ✓ TOT ✓ SPR ✓         |
| mlb_sp        ▓▓  5/min        | (老程 §2 suppressor)           | dir  ML  ✓ TOT ✗ SPR ✗         |
+--------------------------------+--------------------------------+--------------------------------+
| [4] EDGE bps 分布 (histogram)  | [5] EDGE CI_LOW (by tag)       | [6] ALPHA DECAY (s)            |
|    ┌────────┐                  | nba_pace   ci_low  1.8 bps     | nba_pace   p50   12s           |
|    │  ╱╲    │                  | nba_lineup ci_low  3.2 bps     | nba_pace   p99   60s           |
|    │ ╱  ╲   │                  | mlb_sp     ci_low -0.5 bps RED | (alpha 衰减越快越要快下单)      |
|    │╱    ╲  │                  | (< 0 → suppress, 老韩 R8)      |                                |
+--------------------------------+--------------------------------+--------------------------------+
| [7] MM QUOTE SPREAD (bps)      | [8] MM INVENTORY (USDC)        | [9] DIRECT PnL (unrealized)    |
| nba_mm    p50   12 bps         | nba_mm     +$ 12,345           | dir_nba    +$ 4,567            |
| mlb_mm    p50   18 bps         | mlb_mm     -$ 3,200            | dir_mlb    -$   850            |
+--------------------------------+--------------------------------+--------------------------------+
| [10] INTENT EMIT vs DROPPED                                                                      |
|   emit_total  234     dropped_total  3 (mpsc_full)                                               |
|   (drop > 10/s → 报警, 老梁 backpressure)                                                         |
+--------------------------------------------------------------------------------------------------+
| [11] HEDGE EMIT (by strategy, 小袁 portfolio)                                                     |
+--------------------------------------------------------------------------------------------------+
```

### 2.7 Dashboard panel 数汇总

| Dashboard | panel 数 | 上限 12 ? |
|---|---|---|
| D1 交易大盘 | **12** | 顶满 |
| D2 数据健康 | **10** | 留 2 余量 |
| D3 风控大盘 | **12** | 顶满 |
| D4 链上状态 | **9** | 留 3 余量 |
| D5 信号监控 | **11** | 留 1 余量 |

合计 54 panel × 平均 2~3 个 query / panel = ~150 个 query, 与小郑 78 metric 的 query 模板对应 (见 §8).

---

## 3. Operator Console Wireframe (Web + CLI)

### 3.1 Web Operator Console — 主界面 (1080p, 不滚动)

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ stcpp Operator Console            ET 14:23:01  CN 03:23:01 (+13h)   user: laohu   [signout]   │
├──────────────────────────────────────────────────────────────────────────────────────────────┤
│ ┌────────────────────────┐ ┌──────────────────────────────┐ ┌────────────────────────────┐  │
│ │ SYSTEM STATE            │ │ TODAY P&L                     │ │ ALERT BANNER (P0/P1 here) │  │
│ │ ┌────────────┐          │ │ bankroll  $1,234,567          │ │ — no active P0/P1 —        │  │
│ │ │  RUNNING   │  3h12m   │ │ today     +$12,345  (+1.0%)   │ │ (P0 → 红 modal 强制 ack)   │  │
│ │ └────────────┘          │ │ dd_max    -$ 4,500  (-0.4%)   │ │                            │  │
│ │  (绿)                    │ │                              │ │                            │  │
│ └────────────────────────┘ └──────────────────────────────┘ └────────────────────────────┘  │
│                                                                                              │
│ ┌──────────────────────────────────────────────────────────────────────────────────────────┐│
│ │ CONTROLS (高危, 全部二次确认)                                                              ││
│ │  [ HALT ]  [ DRAIN ]  [ SAFE-MODE UNLOCK ]  [ CANCEL ALL (mkt picker) ]                   ││
│ │  [ RESUME-ACK (双人) ]   [ PAUSE MARKET (id picker) ]                                     ││
│ │   ─ 当前 state RUNNING, HALT 可点; SAFE-MODE-UNLOCK 灰 (没在 SAFE_MODE)                    ││
│ └──────────────────────────────────────────────────────────────────────────────────────────┘│
│                                                                                              │
│ ┌────────────────────────┐ ┌──────────────────────────────┐ ┌────────────────────────────┐  │
│ │ DATA HEARTBEAT (D2 reuse)│ OPEN POSITIONS               │ │ COMMAND HISTORY (audit)    │  │
│ │  poly_wss  GR  1.2s     │  ML       18 orders  $120k    │ │ 14:23 RESUME-ACK by laohu  │  │
│ │  goalserve GR  2.4s     │  TOTALS   24 orders  $ 80k    │ │      witness=xiaogong      │  │
│ │  chain     GR  0.8s     │  SPREADS   7 orders  $ 25k    │ │ 11:42 HALT auto (STALE)    │  │
│ │                          │  PROP      3 orders  $  8k    │ │ 11:42 STATE WARNING→HALTED │  │
│ └────────────────────────┘ └──────────────────────────────┘ │ ... (click → full audit)   │  │
│                                                              └────────────────────────────┘  │
│                                                                                              │
│  [ Open Grafana D1 ]  [ Open D2 ]  [ Open D3 ]  [ Open D4 ]  [ Open D5 ]                     │
│                                                                                              │
│ status bar:   stcpp-trader v0.2.1   build 9a3c    obs OK   audit OK   last fsync 0.8s ago    │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 高危按钮二次确认 (小尤 FE-03 强制)

任何 HALT / DRAIN / SAFE-MODE-UNLOCK / RESUME-ACK / CANCEL-ALL 按钮:

```
┌────────────────────────────────────────────────────────────┐
│  ⚠  Confirm: HALT                                           │
│                                                            │
│  你将把系统切到 HALTED. 所有 evaluate 会返 REJECTED.       │
│  恢复需要双人 ack (RESUME-ACK).                            │
│                                                            │
│  Reason (必填, audit 入库):                                 │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ goalserve stale 60s, manual halt prep                │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  [Cancel]                            [Confirm HALT  3s]   │  ← 3s 倒计时 + Yubikey touch
│                                              ↑ 2s 1s 0s   │
└────────────────────────────────────────────────────────────┘
```

倒计时期间按 Esc / Cancel = 撤销. 0s 后 button 才真的点得动. **不是"3 秒后自动执行", 是"3 秒后才允许执行"**, 反向防误按.

### 3.3 RESUME-ACK 双人流程 (老韩 §6.6)

```
┌────────────────────────────────────────────────────────────┐
│  ⚠  Confirm: RESUME-ACK                                     │
│                                                            │
│  当前 state = HALTED, 持续 8m12s                            │
│  trigger:    auto, reject_code=STALE_DATA                  │
│              source=goalserve, age=62s                     │
│                                                            │
│  Operator:   laohu   (← 当前登录)                           │
│  Witness:    ┌──────────────────────────────┐              │
│              │ xiaogong (online)            ▾ │  ← 选当前在线 │
│              └──────────────────────────────┘              │
│  Witness yubikey touch: ◯ pending → ● confirmed            │
│                                                            │
│  Reason (必填):                                             │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ goalserve recovered 5min ago, RTT back to <50ms,     │ │
│  │ verified stale_age now 1.8s, ready for RUNNING       │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  Next state:  ( ) RUNNING       (•) DRAIN (保守)            │
│                                                            │
│  [Cancel]              [Confirm RESUME-ACK  3s]            │
└────────────────────────────────────────────────────────────┘
```

audit 双签写入 (老韩 §5 audit schema): `operator=laohu, witness=xiaogong, witness_yubikey_sig=<hex>`.

### 3.4 CLI `stcpp-ctl` 子命令清单 (与小宫附录 A 对齐)

**主命令分组** (与小宫 §11 B1 + 附录 A 同步, 我 = 实现者):

```
$ stcpp-ctl --help
stcpp-ctl <subcommand> [options]

Inspect (只读, 无副作用)
  status               系统总览 (state / bankroll / open orders / freshness)
  status --json        机器消费
  audit  --tail N      最近 N 条 audit
  audit  --grep <pat>  按 reject_code / market_id grep
  orders --market <id> 某市场挂单
  positions [--pending-resolve]
  markets --status <enabled|paused|cancelled>
  markets --filter "today,kick=13:00"
  pnl    --today
  schedule --today
  schedule --market <id> --refresh
  chain-status         RPC active provider, gas, nonce
  recon  --since <dur> 链上 vs ledger 对账
  recon  --verify      强制重对账 (启动后或 RESUME-ACK 前)
  config --diff        current vs file
  handoff --from <a> --to <b>   交班输出 "5 个数字" + 写入 audit

Control (高危, 全 audit, 二次确认)
  halt          --reason "<text>"
  drain         --reason "<text>"
  resume-ack    --operator <name> --witness <name> --reason "<text>"
  cancel-all    --market <id> --reason "<text>"
  pause-market  --market <id> --reason "<text>"
  pause-open    --market <id> --reason "<text>"     仅冻新开仓
  safe-mode-unlock --operator <name> --witness <name> --reason "<text>"

Maintenance (需 KMS 权限, 仅 老孙/老叶 可执行)
  nonce --resync      与链上重对齐 (取 max)
  nonce --show
  wal-repair          (老陈/老周 指导, 不在 dogfood 范围)

Replay & Chaos (sandbox only, prod 禁用)
  replay --fixture <name> --speed <Nx>
  replay --since <ts>  --speed <Nx>
  chaos  --inject  <goalserve-down|wss-down|kms-down|rpc-down> --duration <s>
  chaos  --revoke  <name>

Help & Explain (小尤 FE-06)
  explain <error_code>     解释错误码, 出"下一步"建议
  explain <state>          解释当前 state 含义
```

**CLI 配色 + sandbox 区分** (小宫 B6 + 小尤 FE-04):

- prod 环境 prompt: `stcpp-ctl[prod]>` 红色边框
- sandbox: `stcpp-ctl[SANDBOX]>` 黄色边框 + 每条命令前缀打印 `[SANDBOX]`
- output 严格 fixed-width, 用 `─│┌┐└┘` 画框, 不用 unicode 装饰符

**`status` 样例输出 (操作员每天看 20 次):**

```
$ stcpp-ctl status
─────────────────────────────────────────────────────────────────
  stcpp-trader v0.2.1   build 9a3c   uptime 3h12m   ET 14:23:01
─────────────────────────────────────────────────────────────────
  STATE         RUNNING                       since 11:11 ET
  BANKROLL      $ 1,234,567.89   today +$12,345.67 (+1.0%)

  DATA SOURCES                age      thr_warn  thr_halt  ok
    poly_wss                  1.2s     2s        10s       ✓
    goalserve                 2.4s     5s        15s       ✓
    chain (alchemy)           0.8s     10s       30s       ✓

  OPEN ORDERS                 count    notional_usdc
    moneyline                 18       $ 120,345
    totals                    24       $  80,210
    spreads                    7       $  25,500
    prop                       3       $   8,100
    TOTAL                     52       $ 234,155   (30% / 50% cap)

  NONCE   wallet#1 current=4523  pending=2  gap=0  ✓
  RPC     active=alchemy  rtt_p99=180ms  backup=quicknode standby
  AUDIT   last_fsync=0.8s ago  wal_seq=1234567  ✓
─────────────────────────────────────────────────────────────────
  ALERTS  P0: 0   P1: 0   P2: 3   (run: stcpp-ctl audit --tail)
─────────────────────────────────────────────────────────────────
```

### 3.5 Audit Trail 查询界面

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Audit Trail                                       [Export CSV]  [Close] │
├──────────────────────────────────────────────────────────────────────────┤
│ Filters:  [time: last 1h ▾] [type: all ▾] [reject_code: ▾] [op: ▾]      │
├──────────────────────────────────────────────────────────────────────────┤
│ ts (ET)     type      audit_id   trace_id  detail                        │
│ 14:23:01    APPROVED  01HW...A91 9f3a...   NBA-ML  size=$120  edge=4.2bps│
│ 14:23:00    REJECTED  01HW...A90 9f39...   STALE_DATA  goalserve age=6.2s│
│ 14:22:55    STATE     -          -         RUNNING→WARNING  trigger=...  │
│ 14:22:30    APPROVED  01HW...A8F 9f38...   NBA-TOT size=$80  edge=3.1bps │
│ ...                                                                      │
│ (每行 click → 弹出详细 + 一键跳 Tempo trace + Loki context grep)           │
└──────────────────────────────────────────────────────────────────────────┘
```

每行携带 `trace_id` (小尤 OBS-07 同形态 16 hex), click → Grafana Tempo. 老韩 §7 audit ↔ trace 联动接口契约.

---

## 4. 告警通知模板

### 4.1 严重度映射 (合并小尤 §4 + 小郑 §4)

| 级别 | 触发场景 | 通道 | UI 表现 | 自动动作 |
|---|---|---|---|---|
| **P0** (灾难) | nonce_gap / audit_write_fail / RM HALT / signer_down / bankroll_drift>5% | 电话 (PagerDuty) + Slack #stcpp-crit + SMS + Webhook→RM | 红色 full-screen modal + 强制 ack 才能继续 | RM auto HALT / SAFE_MODE |
| **P1** (严重) | hotpath p99>500us 5min / WSS 全断 / RPC 全错 / bankroll_drift 1~5% | Slack #stcpp-high + 电话兜底 | 顶 toast 常驻红, 不可 dismiss 直到 resolve | RM WARNING, 不停盘 |
| **P2** (警告) | 单源 STALE / gas spike / reject_rate>20% / per_mkt_cap>80% | Slack #stcpp-warn | 侧栏 list 黄, 可手 dismiss | 仅记录 |
| **P3** (信息) | 单源短抖动 / 单笔 edge_ci<0 / disk>75% | 日报 email | 不进 UI | 仅记录 |
| **P4** (info) | 今日成交 N 单 / 复盘提示 | 周报 | 不进 UI | — |

### 4.2 Alertmanager subject 模板 (小尤 §4.2 强制)

```
[<P级>][<模块>][<sport?>] <一句话症状>  (持续 <时长>)
```

例:
- `[P0][Risk][-] RM HALT — STATE WARNING→HALTED, trigger=STALE_DATA goalserve age=62s`
- `[P1][Polymarket-WSS][NBA] 主盘 WSS 断 30s — 重连 2 次未成功`
- `[P2][Goalserve][MLB] inplay push 滞后 5s — market_id 7788`

### 4.3 Alertmanager body 模板 (小尤 §4.3 + OBS-01 强约束)

```yaml
# alertmanager template (与小郑 §4.1 路由 + 小尤 §4.3 双满足)
# {{ }} 是 alertmanager 模板语法

[{{.Labels.severity}}][{{.Labels.module}}][{{.Labels.sport | default "-"}}] {{.Annotations.summary}}

现象:    {{.Annotations.symptom}}
影响:    {{.Annotations.impact}}            # e.g. "NBA 4 个市场 5s 不更新"
自动动作: {{.Annotations.auto_action}}      # e.g. "WSS 第 2 次重连进行中"
人工建议: {{.Annotations.next_action}}      # auto/wait/manual:<cmd>/escalate:<who>
相关:
  - Grafana: {{.Annotations.grafana_url}}
  - Runbook: {{.Annotations.runbook_url}}
  - Audit:   {{.Annotations.audit_query}}
  - Trace:   {{.Annotations.tempo_trace_id}}

规则 ID: {{.Labels.alertname}}-{{.Labels.alert_id}}
触发时间: {{.StartsAt | utc}}  (持续 {{since .StartsAt}})

trace_id: {{.Annotations.trace_id}}   # 16-hex, 与 UI / 日志同形态 (OBS-07)
```

### 4.4 完整告警示例 (P1, WSS 断)

```
Subject: [P1][Polymarket-WSS][NBA] 主盘 WSS 断连 30s — 触发自动重连第 2 次

[POLY-E0042] Polymarket WSS 主盘断连
  现象:    NBA 主盘 WSS endpoint 在 02:14:33 ET 断开 (TCP RST)
  影响:    4 个 NBA 在售市场 5s 内价格不更新; RM 已转 WARNING; 暂停新开仓
  自动动作: BackoffPolicy 第 2 次重连进行中, 预计 3s 完成
  人工建议: auto (等 ≤ 10s); 若 10s 未恢复 → manual: stcpp-ctl chaos --revoke wss-down
            或 manual: stcpp-ctl pause-market --market <id> --reason "wss-down"
            10s 仍未恢复 → escalate: 老李

  相关:
    - Grafana D2:       http://ops.internal/d/data-health?from=now-15m
    - Runbook:          http://wiki.internal/runbook/poly-wss-disconnect
    - Audit (grep):     stcpp-ctl audit --grep "POLY-E0042"
    - Trace:            http://ops.internal/tempo/trace/9f3a7bce4f1d2a8b

  规则 ID: POLY_WSS_DISCONNECT-7788
  触发时间: 2026-05-28 02:14:33 UTC  (持续 30s)
  trace_id: 9f3a7bce4f1d2a8b9c0d1e2f3a4b5c6d
```

### 4.5 渠道路由 (alertmanager `route` 草稿)

```yaml
route:
  receiver: default-slack-warn
  group_by: [alertname, severity]
  routes:
    - matchers: [severity="P0"]
      receiver: pagerduty-crit + slack-crit + sms + webhook-rm
      group_wait: 0s
      repeat_interval: 5m
    - matchers: [severity="P1"]
      receiver: slack-high + pagerduty-high
      group_wait: 30s
      repeat_interval: 30m
    - matchers: [severity="P2"]
      receiver: slack-warn
      group_wait: 5m
      repeat_interval: 4h
    - matchers: [severity="P3"]
      receiver: email-daily-digest
      group_wait: 1h
      repeat_interval: 24h
```

### 4.6 静默规则 (小尤 OBS-05 强制)

- 任何 silence 必须填 `reason` + `ttl` (默认 1h, 最大 24h, 永久 silence 拒收)
- silence 列表在 Operator Console 顶部 banner 灰显, 让操作员知道"有东西被静音了"

---

## 5. 设计纪律 (Design Discipline)

### 5.1 一屏 panel 数

- Grafana 单 dashboard: **≤ 12 panel** (小尤 §6.1 上限)
- Operator Console: **≤ 10 区块** (主屏)
- 超过 → 必须先砍, 不允许"再多塞一个"

### 5.2 颜色 4 色法

```
GREEN  (#34a853)  正常 / on / 阈值内 / ok
YELLOW (#fbbc04)  注意 / 接近阈值 / 重试中 / WARN
RED    (#ea4335)  异常 / 触发 / 断连 / HALT
GRAY   (#9aa0a6)  idle / no data / disabled / standby
```

**禁:** 蓝 (与链接色冲) / 紫 / 橙 / 粉. 单 dashboard 颜色 ≤ 5 (含黑白灰).

**色盲三件套** (小尤 §6.3): 颜色 + 文字 label + 图标. e.g. RM STATE = `[GREEN]✓ RUNNING`, 不是单纯绿底.

### 5.3 字体 + 单位

- **字体**: 全系统单一 `JetBrains Mono` (web) / `Menlo` (CLI), fixed-width
- **字号**: dashboard 最小 14px, console 最小 14px, CLI 等宽
- **单位强制后缀**:
  - 延迟: `_us` / `_ms` / `_s` (大于 1000 自动换), e.g. `312 us`, `1.2 s`
  - 金额: `$1,234,567` 或 `USDC 1,234.56` (带千分位)
  - 百分比: 1 位小数, e.g. `12.3%`
  - 时间: ET 主显, CN 副显 (右上 toggle)
- **空值**: 显示 `—`, 不显示 `null`/`undefined`

### 5.4 闪烁 / 动画纪律 (小尤 §6.5)

- **仅 P0 P1 告警**可闪烁, 频率 ≤ 1 Hz
- **禁全屏背景闪** (操作员一晚看 8h)
- **禁 toast 滑入动画**, 直接 fade-in 200ms
- **禁数字 spinning animation** (老虎机式抖动), 直接刷新

### 5.5 高危按钮纪律

- 全部二次确认 + 3s 倒计时 (FE-03)
- Reason 必填, 入 audit
- RESUME-ACK / SAFE-MODE-UNLOCK 必须双人 + Yubikey
- prod / sandbox 命令同名, 但**视觉强区分** (B6: prompt 颜色 + 边框)

---

## 6. 响应式 + 多人协作 (Hand-over)

### 6.1 分辨率 baseline

- **1080p (1920×1080)**: 主屏标准, 所有 dashboard + console 必须装下
- **1440p (2560×1440)**: 副屏推荐, panel 字号自动放大 1.2x
- **4k**: 不专门优化, 等比放大 (浏览器原生)
- **mobile**: **放弃** (老雷宪法 + 公司没有 retail). 但留一个降级 read-only mobile page: 只显示 RM STATE + P0/P1 alert, 用于操作员临时离座

### 6.2 时区显示

- 默认 ET (与 Polymarket 同), 右上角永远显示 `ET HH:MM:SS   CN +13:00 HH:MM:SS`
- 夏令时切换日 (3 月 2 周日 + 11 月 1 周日) toggle 自动调整, 提前 24h 顶 banner 提醒
- CLI `stcpp-ctl status` 全程 ET, 用户要 CN 自己 `TZ=Asia/Shanghai stcpp-ctl status`

### 6.3 多人同看 hand-over 协议

**问题**: 国内 + 美东 + 轮班操作员可能同时看一个 dashboard, 谁 in charge ?

**协议** (与小宫 B5 同步):

1. **"on-duty" 标记**: Operator Console 顶部 `current op: laohu (since 14:23)`, 全员可见
2. **签到/交班按钮**: 当前 on-duty 操作员点 `[ Hand off to → ]`, 选下一个人, 触发:
   - `stcpp-ctl handoff --from laohu --to xiaogong`
   - 弹出 "5 个数字" 交接表 (小宫 §5.2):
     ```
     state:         RUNNING
     bankroll:      $1,234,567 (+$12,345)
     open_orders:   52 across 4 market_types
     active_alerts: P2 × 3 (goalserve_lag, gas_high, ...)
     待办:           等 14:30 NBA 18:00 lineup, watch reject_rate
     ```
   - 接班人 confirm 后, on-duty 标记切到 xiaogong
   - 写入 audit (`STATE_HANDOFF`)
3. **没明确 hand-off 时**: 默认 last login 的人是 on-duty, console 顶部 banner 黄色提示 "no explicit on-duty, last login=laohu @ 12:30, please confirm or hand-off"
4. **冲突操作**: 同一时刻 A 点 HALT B 点 RESUME, 后到者会看到 "operation race, current state changed by laohu 2s ago, please review"

### 6.4 跨时区 on-call 排班 (老胡待定)

- 美东 08:00-22:00 ET: 国内值班 (21:00-11:00 国内)
- 美东 22:00-08:00 ET: 美西/欧洲值班 (待招? 老雷决策)
- 夏令时切换日: 双人值守 24h (小宫 B8)

---

## 7. chrome-devtools MCP 用法示例

`.mcp.json` 已配 `chrome-devtools-mcp`. 我 / 小宫 / 小尤 都可直接调用. 用法 4 类:

### 7.1 用法 A: dogfood 期间盯 Grafana panel 渲染 (小宫)

**场景**: 小宫跑 §6.4 Goalserve 断 30s chaos, 想验证 D2 panel 4 时序图阈值线是否正确画出来.

**MCP 调用**:

```
chrome-devtools.navigate("http://ops.internal:3000/d/data-health")
chrome-devtools.list_console_messages()  # 检查有无 JS error
chrome-devtools.take_screenshot(panel="wss_heartbeat_age")
chrome-devtools.evaluate_script("""
  // 抓 panel 内 SVG 的红黄虚线 y 值
  document.querySelectorAll('[data-panel-id=\"4\"] line[stroke-dasharray]')
    .forEach(l => console.log(l.getAttribute('y1')));
""")
```

**收益**: 验证 panel 阈值线是否对齐 2s/10s, 不用人工放大屏幕量像素.

### 7.2 用法 B: 小尤 UX 评分卡数据采集 (D1 信息密度)

**场景**: 小尤要给 D1 交易大盘打 D1 信息密度分.

**MCP 调用**:

```
chrome-devtools.navigate("http://ops.internal:3000/d/live-trading")
chrome-devtools.evaluate_script("""
  // 统计一屏可见 panel 数 + 数字 cell 数
  const panels = document.querySelectorAll('.react-grid-item');
  const cells = document.querySelectorAll('.stat-panel--value, .table-cell');
  return {
    panel_count: panels.length,
    digit_cell_count: cells.length,
    viewport: { w: window.innerWidth, h: window.innerHeight },
    has_horizontal_scroll: document.body.scrollWidth > window.innerWidth,
  };
""")
```

**输出**: 直接喂小尤评分卡, 不用她人工数. 例如返回 `{panel: 12, digit: 47, scroll: false}` → 信息密度评分 ~ 8/10.

### 7.3 用法 C: 抓 console error / network 失败 (frontend bug)

**场景**: Operator Console 报 P1 toast 不出来, 怀疑 SSE 断了.

**MCP 调用**:

```
chrome-devtools.navigate("http://ops.internal/console")
chrome-devtools.list_console_messages()           # JS error / warn
chrome-devtools.list_network_requests(filter="eventsource")  # SSE 连接状态
chrome-devtools.get_network_request("/api/sse/alerts")  # 看 headers + status
```

**收益**: 不用我手动打开 devtools 截图, MCP 直接给 dump.

### 7.4 用法 D: 高危按钮二次确认行为验证

**场景**: FE-03 HALT 按钮 3s 倒计时, 验证 0s 前确实不响应点击.

**MCP 调用**:

```
chrome-devtools.navigate("http://ops.internal/console")
chrome-devtools.evaluate_script("document.querySelector('button[data-action=halt]').click()")
chrome-devtools.evaluate_script("document.querySelector('button[data-action=confirm-halt]').click()")
# 期望 click 被 disabled state 吃掉, 不真触发
chrome-devtools.take_screenshot(full_page=true)
# 等 3s
chrome-devtools.evaluate_script("await new Promise(r => setTimeout(r, 3500))")
chrome-devtools.evaluate_script("document.querySelector('button[data-action=confirm-halt]').click()")
# 这次应该触发, 但应弹 Yubikey prompt
```

**收益**: 自动化回归测试小尤 FE-03 规则, 不用人手点.

### 7.5 用法 E: Grafana variable / 时间范围联调

**场景**: D3 panel 4 reject heatmap 切到 `last 24h` 后是否还能渲染.

**MCP 调用**:

```
chrome-devtools.navigate("http://ops.internal:3000/d/risk-state?from=now-24h&to=now")
chrome-devtools.wait_for_selector(".panel-content[data-panel-id='4'] svg")
chrome-devtools.list_network_requests(filter="api/ds/query")
# 看 Prom 查询 24h 是不是超时
```

### 7.6 与 filesystem MCP 配合

`.mcp.json` 第二个 MCP `filesystem` 指向 repo 根. 可直接读 `docs/RESEARCH/*.md` 不出 sandbox.

**典型联动**:

```
# 1. 读小郑 metric 清单
filesystem.read("/docs/RESEARCH/xiaozheng-observability-v0.1.md")

# 2. 拼出 PromQL
# (我写 panel query 时直接 read 小郑文档 + read 老韩 reject_code enum)

# 3. 在 Grafana 创 panel
chrome-devtools.navigate("http://ops.internal:3000/dashboard/new")
chrome-devtools.evaluate_script("// inject panel JSON via API")
```

---

## 8. 与小郑 metric 接口

### 8.1 我需要小郑提供的 metric 字段 (已对照 v0.1, 都有)

D1~D5 共用 metric list, **从小郑 78 metric 全表来**, 我列每个 panel 用的:

| Dashboard | Panel | metric (小郑 §2 编号) |
|---|---|---|
| D1-1 | RM STATE | `stcpp_l4_state{state}` |
| D1-2 | PnL | `stcpp_l4_bankroll_usdc`, `stcpp_l4_daily_pnl_usdc`, `stcpp_l4_daily_loss_ratio` |
| D1-3 | ALERT COUNT | `ALERTS{}` (Prom 内建) by severity |
| D1-4 | OPEN ORDERS | `stcpp_l5_order_open_count{market_type}` |
| D1-5 | HOTPATH p99 | `stcpp_l3_signal_to_intent_seconds + l4_evaluate + l5_order_submit` (sum of histograms) |
| D1-6 | HEARTBEAT | `stcpp_l2_wss_heartbeat_age_seconds{source}` |
| D1-7 | ORDER FLOW | `stcpp_l5_order_submit_total`, `_fill_total`, `_reject_total` 5m rate |
| D1-8 | REJECT RATE | `rate(stcpp_l4_reject_total[5m]) / rate(stcpp_l4_evaluate_total[5m])` + topk reject_code |
| D1-9 | EXPOSURE | `stcpp_l4_market_exposure_usdc{market_type}`, `_ratio` |
| D1-10 | AUDIT | Loki `{job="stcpp-trader",log_type="audit"}` |
| D1-11 | WALLET | `stcpp_l5_wallet_balance_usdc{wallet}` |
| D1-12 | NETWORK | `stcpp_l1_net_rtt_seconds{peer}` |

D2~D5 略, 全部覆盖在小郑 78 metric 内, **0 个缺**.

### 8.2 我给小郑的 PromQL 草稿 (核心 panel)

**D1-5 HOTPATH p99 (老姜 SLO 500us)**:

```promql
# 组合三段 histogram 的 p99 (近似, 严格用 trace exemplar)
histogram_quantile(
  0.99,
  sum by (le) (
    rate(stcpp_l3_signal_to_intent_seconds_bucket[5m])
    + rate(stcpp_l4_evaluate_seconds_bucket[5m])
    + rate(stcpp_l5_order_submit_seconds_bucket[5m])
  )
) * 1e6   # to microseconds
```

**D2-1 三色灯 heartbeat (与 RM v0.2 STALE 阈值对齐 2s/10s)**:

```promql
# 每个 source 一个时序, threshold 用 Grafana value mapping
stcpp_l2_wss_heartbeat_age_seconds{source=~"poly_wss|goalserve|chain"}
# value mapping in panel:
#   < 2  → GREEN
#   2~10 → YELLOW
#   > 10 → RED
```

**D2-9 ETL E2E p99**:

```promql
histogram_quantile(0.99, sum by (source, le) (rate(stcpp_l2_etl_e2e_seconds_bucket[5m]))) * 1000  # ms
```

**D3-4 REJECT HEATMAP (reject_code × market_type)**:

```promql
sum by (reject_code, market_type) (
  increase(stcpp_l4_reject_total[1h])
)
# Grafana panel type: heatmap, x=market_type, y=reject_code, value=color
```

**D3-6 EVALUATE p99**:

```promql
histogram_quantile(0.99, sum by (le) (rate(stcpp_l4_evaluate_seconds_bucket[5m]))) * 1e6  # us
```

**D4-4 NONCE GAP (硬约束 == 0)**:

```promql
# 任意 > 0 → P0
sum(stcpp_l5_nonce_gap_total) > 0
```

**D4-6 GAS PRICE (老叶)**:

```promql
stcpp_l5_gas_price_gwei{provider="alchemy"}
# 阈值: > 200 gwei → P2
```

**D5-1 SIGNAL FIRE RATE (by strategy_tag)**:

```promql
sum by (strategy_tag, signal_type) (rate(stcpp_l3_signal_fire_total[1m])) * 60   # /min
```

**D5-4 EDGE bps 分布**:

```promql
# Grafana heatmap panel, 用 histogram bucket 原生支持
stcpp_l3_edge_bps_bucket{strategy_tag=~"$strategy_tag"}
```

**完整 PromQL 草稿** (54 panel × 1~3 query) 太长, 我下一步拆到 `docs/RESEARCH/xiaosu-grafana-promql-draft.md` (v0.2 出), 本文先列核心.

### 8.3 我反过来需要小郑 v0.2 补的东西

| # | 需求 | 给小郑 | 优先级 | 关联 |
|---|---|---|---|---|
| FE-Z1 | metric 加 label `is_sandbox` (区分 prod/sandbox) | 小郑 v0.2 | P0 | 小宫 B6 sandbox 视觉区分 |
| FE-Z2 | audit Loki 投影 metric (操作员可读, 不是 raw JSON) | 小郑 OBS-04 | P0 | 小尤 OBS-04 已记 |
| FE-Z3 | `stcpp_meta_handoff_total{from,to}` counter | 小郑 v0.2 加 | P1 | §6.3 handover audit |
| FE-Z4 | dashboard JSON 进 git (`grafana/dashboards/*.json`), 我提 PR 不是手点 UI | 小郑 + 老吴 | P1 | infra-as-code |
| FE-Z5 | exemplar 在所有 histogram (老姜 hot path 3 个 + RM eval + signer + RPC) | 小郑 §6.4 已列, 我用 | P1 | trace 跳转 |
| FE-Z6 | metric `stcpp_l4_state_seconds{state}` (gauge, 每 state 累计时长) | 小郑 v0.2 加 | P2 | D3-1 timeline |

---

## 9. 开放问题

| # | 议题 | 待定 | 跟进 |
|---|---|---|---|
| OQ-1 | Operator Console 数据源走 Prom HTTP API 还是自家 `/ops` REST? | 倾向自家 REST (RM state / handoff 等业务字段不在 metric) | @老周 v0.2 + @我 |
| OQ-2 | Yubikey 2FA 是否真上线 (小公司团队 < 10 人值不值) | 我倾向上, 老沈说了算 | @老沈 |
| OQ-3 | RESUME-ACK 双人 ack 凌晨 3 点找不到 witness 怎么办 | 老韩 §6.6 + 小宫 Q7 同问 | @老雷 (decisive) |
| OQ-4 | 1080p 是否真是 baseline (国内值班机器配置) | 小尤 Q2 同问 | @老吴 |
| OQ-5 | Grafana iframe 嵌 console vs console 单独建 panel | 倾向 iframe (省工 + 一致), 但 SSO + CORS 要解 | @老吴 + @老沈 |
| OQ-6 | 高危按钮 audit 同步 vs 异步 | 同步 (与 RM evaluate audit 同栈, fail-closed) | @老韩 |
| OQ-7 | command history 在哪存 (Loki vs audit WAL) | audit WAL 独立 + Loki 投影显示 (双轨) | @老韩 + @老唐 |
| OQ-8 | `stcpp-ctl` 是 C++ binary 还是 thin wrapper | C++ binary, share infra/log + audit append 接口 | @老周 + @我 |
| OQ-9 | mobile read-only page 要不要做 | 不做 (放弃, 老雷宪法), 但若老胡坚持给一个 5 panel 的极简版 | @老胡 决定 |
| OQ-10 | chrome-devtools MCP 在 prod 还是只在 sandbox 用 | 只 sandbox + staging, prod 走 read-only | @老沈 |
| OQ-11 | dashboard JSON 进 git 后, 谁有 push 权限 | 我 + 小郑, review 由老吴 | @老吴 |
| OQ-12 | sandbox 终端配色 / prompt 前缀方案具体值 | 等老吴 + 小尤 dogfood 试一轮 | @老吴 + @小尤 |

---

## 10. 验收标准 + 下一步

### 10.1 v0.1 验收 (老胡 + 小尤 + 小郑 联签)

- [ ] §2 5 个 Grafana dashboard panel 数与小郑 metric 全 cover (我已对照, 0 缺)
- [ ] §3 Operator Console 主屏 1080p 不滚动 (待原型实测, Sprint-2)
- [ ] §3.4 `stcpp-ctl` 子命令清单与小宫附录 A 对齐
- [ ] §4 告警 subject/body 模板符合小尤 §4.2/§4.3
- [ ] §5 颜色 4 色 + 字体 fixed-width + 无闪烁
- [ ] §6 handover 协议与小宫 B5 对齐
- [ ] §7 chrome-devtools MCP 5 个用法可执行 (待 Sprint-2 实跑)
- [ ] §8 PromQL 草稿与小郑 v0.2 联签

### 10.2 下一步 (Sprint-2 计划)

1. **W2-1**: 拆出 `xiaosu-grafana-promql-draft.md` (所有 54 panel 完整 query), 找小郑联签
2. **W2-2**: 用 `grafana/dashboards/*.json` 在 staging 起 D2, 跑小宫 §6.4 chaos drill
3. **W2-3**: Operator Console 起 React 骨架 (只做 D1 reuse + control buttons + audit tail), 出 v0.1 演示
4. **W2-4**: `stcpp-ctl status` + `audit --tail` + `explain` 三个最常用子命令实装, 跑 §1.1 90min onboarding demo
5. **W2-5**: 用 chrome-devtools MCP 跑 §7.4 高危按钮 3s 倒计时回归测试

### 10.3 跨人依赖 (我等他们)

- @小郑: v0.2 metric 补 `is_sandbox` label / `stcpp_meta_handoff_total` (FE-Z1/Z3)
- @老周: Operator Console 数据源接口 (`/ops` REST 还是 Prom 直读) v0.2
- @老韩: RESUME-ACK 双人在小团队怎么落 (OQ-3)
- @老沈: Yubikey 2FA 决策 (OQ-2)
- @老吴: 部署 Grafana + obs 节点 (S1-018 进度依赖)
- @小宫: dogfood 跑剧本预演反馈 (我哪些假命令她跑不通)
- @小尤: UX 评分卡过 D2 第一张 (M+1 上线后立刻)

---

## 附录 A — 与现有文档对齐点

- 小郑 obs v0.1 §2 78 metric → 本文 §2 全 panel query / §8 PromQL 草稿
- 小郑 obs v0.1 §5 6 dashboard → 本文 §2 详化 D1~D5 (D6 系统性能延 Sprint-3)
- 小尤 ux v1 §4 P0~P3 分级 → 本文 §4 + 加 P4 info 级
- 小尤 ux v1 §6 一屏 12 panel → 本文 §5.1 + §2 panel 数对照
- 小尤 ux v1 §9 FE-01~FE-07 → 本文 §3.2 二次确认 / §3.4 explain / §3.5 audit
- 小宫 dogfood v1 附录 A 命令清单 → 本文 §3.4 完全对齐
- 小宫 dogfood v1 §11 B1~B8 → 本文 §6.3 handover / §3.4 sandbox 区分 / §9 OQ
- 老韩 RM v0.2 §3.7 STALE 阈值 (2s/5s/10s) → 本文 §2.3 D2 阈值 (不是 v0.1 的 30s/60s)
- 老韩 RM v0.2 §4 状态机 4 态 + SAFE_MODE → 本文 §2.4 D3-1 / §3.1 console state

---

## 附录 B — Wireframe 截图清单 (Sprint-2 出)

按 §2 顺序, 我 Sprint-2 用 figma / excalidraw 出真草图:

1. D1 交易大盘 (1080p, 12 panel)
2. D2 数据健康 (1080p, 10 panel) — 第一个上线
3. D3 风控大盘 (1080p, 12 panel)
4. D4 链上状态 (1080p, 9 panel)
5. D5 信号监控 (1080p, 11 panel)
6. Operator Console 主屏
7. HALT 二次确认 modal
8. RESUME-ACK 双人 modal
9. Audit Trail 查询界面
10. CLI status / handoff output

---

**END v0.1.** 等老胡 + 小尤 + 小郑 review, v0.2 在 Sprint-2 中期出 (含 staging 实测截图 + 完整 PromQL + chrome-devtools MCP 跑通回归).
