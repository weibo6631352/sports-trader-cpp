# RM v0.3 (Sprint-2 W1, STALE 5 档 + G8 观察项 + STRATEGY_DECAYED)

- Owner: 老韩 (risk-engineer)
- Date: 2026-06-15 起草
- 验收人: 老郭 (24h sign-off) + 小梁 (参数会签)
- 关联:
  - 旧版: `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` (保留, 不替换)
  - Sprint-1 Retro 决议: `docs/ADR/2026-05-28-gm-signoff-sprint1-retro.md` (D-01/D-06/D-11/D-12)
  - 多方发言:
    - `docs/MEETINGS/sprint1-retro/xiaoyuan-speech.md` (D-06 INPLAY_HOT_CRIT)
    - `docs/MEETINGS/sprint1-retro/xiaodong-speech.md` (G8 观察项 5% / OQ-D13 BLACK kill switch)
    - `docs/MEETINGS/sprint1-retro/xiaoxiao-speech.md` (INVALID_INTENT NaN 检测)
  - ADR-001: `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (Accepted final)

---

## 0. v0.2 → v0.3 变更摘要

### 0.1 关键变更 (一行话版)

| 域 | v0.2 立场 | v0.3 新立场 | 触发原因 |
|---|---|---|---|
| STALE 分档 | 3 档 (WSS / Goalserve / 对账, source-level) | **5 档 MarketState-level** (INPLAY_HOT_CRIT / INPLAY_HOT / INPLAY_COLD / PREGAME / SETTLED) | D-06 + 小袁 micro-state 实测 `T_{1/2}=0.12-0.15s` |
| MarketState 判定 | 隐含 (RUNNING 默认) | **`MarketStateClassifier` 接口** + sport/period/clock/score 决策树 | 小袁 Sprint-2 中给 code-level 细则 |
| RM 误拒率 KPI | misalignment #5 (v0.2 未落) | **G8 观察项, 月度阈值 5% (非 2%)**, 不进 M4.5 hard gate | D-04 + 小董 §2 (30 样本下 2% 分辨率不够) |
| reject enum | v0.2 14 项 + 4 新 (小肖) + AET_SIGN_FAILED | **新增 `INVALID_INTENT` (book_snapshot_ts_ns 检测) + `STRATEGY_DECAYED`** | 小肖 §2 NaN 检测 + D-13 (OQ-D13) BLACK kill switch |
| STRATEGY_DECAYED kill switch | (无) | **Bayesian decay: `P(μ<0\|data) > 0.3 持续 2 周` → RM evaluate 一律 REJECT** | 小董 §5 + OQ-D13 GM Agreed |
| paper mode 走 RM | R-11 (v0.2 已确立) | **R-11 不变, 显式重申** | D-12 paper/live ULID 命名空间方案 A |

### 0.2 沿用 v0.2 章节 (不重复)

§1 (设计目标含 G1-G8) / §2 (RiskGateway 单符号) / §3 通用变量与公式 / §4 状态机 + §4.3 副作用表 / §5 audit schema / §6 上下游交互 / §7 参数初值表 (新增 STALE 5 档与 G8 / STRATEGY_DECAYED) / §8 实现注意事项 (事件循环) / §9 测试策略 / §10 开放问题 / §12 audit WAL group commit / §13 SAFE_MODE 联动 — 均沿用 v0.2.

§11 STALE 表 + §14 残留风险点 → **重写**, 见下文.

### 0.3 与 D-XX 的对应矩阵

| GM 决议 | v0.3 落位 |
|---|---|
| D-01 KELLY 0.25 / FILL_RATE 0.50 / PER_ORDER $5K/$2K | §7 (沿用 v0.2) |
| D-04 G1-G7 hard + G8 观察 | §15 G8 章节 (新) |
| D-06 STALE 5 档含 INPLAY_HOT_CRIT | §14 5 档矩阵 (新) |
| D-11 AET_SIGN_FAILED 单列 | §3.10 enum (v0.2 已落, v0.3 保留) |
| D-12 paper/live ULID 方案 A | §16.5 paper mode 走完整 RM |
| OQ-D13 BLACK → RM kill switch | §16 STRATEGY_DECAYED 章节 (新) |

---

## 1~13. 沿用 v0.2 (修订点)

### 1.1 §1.1 设计目标 G8 修订

v0.2 G8 = "崩溃后默认 SAFE_MODE". 该目标重命名为 **G8a (Safe-Mode default)** 保留语义不变.

新增 **G8b (Reject-Rate Observability)**:

| # | 目标 | 度量 |
|---|---|---|
| G8b (v0.3 新) | RM 误拒率长期可观察, 月度 ≤ 5% | replay 抽样 + 月度审计报表 |

G8b 是 long-run monitoring, **非 M4.5 first-pass gate** (D-04 小董 §2 明确).

### 1.2 §3.10 reject enum 增量 (含 4 新 + INVALID_INTENT + STRATEGY_DECAYED)

> **W5 Wave 24 patch (2026-05-28, 老沈)**: 21 enum 在 `RiskGateway::evaluate()` 内部的
> **短路顺序 SSOT = ADR-004** (`docs/ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md`).
> 本节 spec **不复刻顺序**, 仅列 enum 语义. position_caps (R-6/7/8/9/10) 先于
> liquidity (R-15/16/17) — 红线优于客观状态. 实现见 `src/stcpp/risk/risk_gateway.cpp::evaluate()`.


v0.2 已含 14 项. v0.3 增量:

| code (v0.3 新) | 含义 | 触发规则 |
|---|---|---|
| `LOW_FILL_RATE` | expected_fill_rate < FILL_RATE_FLOOR (0.50) | 小肖 §1 / R10 |
| `EXCESSIVE_SLIPPAGE` | slippage_ticks > MAX_SLIPPAGE_TICKS (3) | 小肖 §1 / R10 |
| `EDGE_NEGATED_BY_SLIPPAGE` | net edge after slippage ≤ 0 | 小肖 §1 / R10 (语义同 EDGE_CI_NEGATIVE 但 fee/slippage 维度) |
| `EXCEED_BOOK_DEPTH` | rho = size / book_depth_l1 > RHO_MAX (3.0) | 小肖 §1 / R10 |
| `AET_SIGN_FAILED` | signer B5 REJECT, audit 单列 (D-11) | 不走 evaluate, 走 audit emit |
| **`INVALID_INTENT`** (v0.3 强化) | `book_snapshot_ts_ns == 0 OR < (now_ns - 60s)` 二者任一 → 视为未填 | 小肖 §2 NaN 检测 + R3 子项 |
| **`STRATEGY_DECAYED`** (v0.3 新) | Bayesian decay BLACK (P(μ<0\|data) > 0.3 持续 2 周) | §16 / R12 |

合计: 14 (v0.2) + 5 (小肖 4 + AET) + 2 (v0.3 强化 INVALID_INTENT + 新增 STRATEGY_DECAYED) = **21 条 reject enum**.

**封闭性不变**: 不允许 `OTHER`, 新增必 bump 版本.

---

## 14. STALE 5 档矩阵 + MarketState 判定接口 (替换 v0.2 §11)

### 14.1 5 档阈值表 (法律效力, GM D-06 Agreed)

| MarketState | 适用场景 | WARNING (DEFERRED) | HALT (REJECT) | `T_{1/2}` 实测 (小袁 n) | D-06 上限 |
|---|---|---|---|---|---|
| **INPLAY_HOT_CRIT** | NBA Q4 < 2min / NFL 2-min warning / MLB ≥8 局一分差 / NHL P3 < 5min 一分差 / OT | **200 ms** | **800 ms** | 0.12-0.15s (n=23) | ≤ 30s ✓ |
| **INPLAY_HOT** | 其余 inplay (game_in_progress && WSS rate > 1/s) | **500 ms** | **2,000 ms** | 0.21-0.45s | ≤ 30s ✓ |
| **INPLAY_COLD** | pregame inplay (开赛 ≤ 30min 内但未开始 / 暂停 / replay review) | **2,000 ms** | **10,000 ms** | 8-30s | ≤ 30s ✓ |
| **PREGAME** | 比赛前, 距开赛 > 30min | **5,000 ms** | **15,000 ms** | 60-120s | ≤ 30s ✓ |
| **SETTLED** | 比赛结束 / outright / 季后系列赛非比赛日 | **10,000 ms** | **30,000 ms** | 120s+ | = 30s ✓ (硬上限) |

**D-06 红线**: 任一 HALT ≤ 30,000ms. 任何 hot reload / ADR 不得上调到 30s 以上.

### 14.2 数据源 freshness 测量 (v0.3 改写)

v0.2 按 source 分 (WSS / Goalserve / 对账), v0.3 升级:

```
freshness_ms(market_id) =
  max(
    now - last_book_update_ts (Polymarket WSS for this market),
    now - last_score_update_ts (Goalserve for this game)
  )
recon_freshness_ms =
  now - last_recon_sync_ts (全局对账, 与单市场无关)
```

**判定**:
- `freshness_ms(market_id) > WARNING(state) → DEFERRED`
- `freshness_ms(market_id) > HALT(state) → REJECTED(STALE_DATA)`
- 对账 freshness 仍按 v0.2 单列 (10s WARNING / 30s HALT), **不分 MarketState**, 因对账是全局.

**WSS + Goalserve 取 max** = 最保守, 与 v0.2 §11.1 "双源取最大值" 一致.

### 14.3 MarketStateClassifier 接口 (老韩定接口, 小袁实现细则)

```cpp
namespace stcpp::risk {

enum class MarketState : uint8_t {
    INPLAY_HOT_CRIT = 0,
    INPLAY_HOT      = 1,
    INPLAY_COLD     = 2,
    PREGAME         = 3,
    SETTLED         = 4,
};

struct MarketContext {
    SportType   sport;             // NBA / NFL / MLB / NHL / Soccer / ...
    GamePhase   phase;             // pregame / live / halftime / final / postponed
    uint8_t     period;            // 1-4 / 1-7 (MLB) / 0=pregame
    int32_t     clock_seconds;     // 节内剩余, 0 = period 结束
    int32_t     score_diff;        // |home - away|
    bool        overtime_active;
    bool        two_minute_warning_active;  // NFL
    double      wss_rate_60s;      // last 60s WSS event rate
    int64_t     game_start_ts_ns;  // 开赛时间
    int64_t     now_ns;
};

// 老韩定接口, 小袁 Sprint-2 中实现细则 (D-06 派单)
MarketState classify(const MarketContext& ctx) noexcept;

// 阈值表查询 (constexpr)
struct StaleThresholds { uint32_t warn_ms; uint32_t halt_ms; };
constexpr StaleThresholds threshold_of(MarketState s) noexcept;

}
```

**判定决策树** (引用小袁 §1 §6.1 派单, 由小袁 Sprint-2 中给 C++ 实现):

```
if NBA && period==4 && clock<120s         → INPLAY_HOT_CRIT
if NFL && two_minute_warning_active       → INPLAY_HOT_CRIT
if MLB && inning>=8 && |score_diff|<=1    → INPLAY_HOT_CRIT
if NHL && period==3 && clock<300s && |score_diff|<=1 → INPLAY_HOT_CRIT
if overtime_active                        → INPLAY_HOT_CRIT
if phase==live && wss_rate_60s > 1/s      → INPLAY_HOT
if phase==live (其他)                     → INPLAY_COLD
if phase==pregame && (game_start - now) <= 30min → INPLAY_COLD (邻近开赛)
if phase==pregame                         → PREGAME
if phase==final OR phase==postponed       → SETTLED
default                                   → PREGAME (fail-safe 保守档)
```

**fail-safe**: 任何 classify 异常 (sport 未知 / clock 负数 / NaN) → 退到 **INPLAY_COLD** (中间档, 保守).

### 14.4 状态变化触发动态切换 (v0.3 新)

MarketState 变化 → STALE 阈值动态切换, **不影响 RM 主状态机 (RUNNING/WARNING/HALTED)**:

- RM 主状态机层级: `RUNNING / WARNING / HALTED / SAFE_MODE / DRAIN` (v0.2 §4.1, 全局).
- MarketState 层级: 每 market 独立, 仅决定 STALE 阈值.

**切换时机**:
- 每次 evaluate 时按 `OrderIntent.market_id` + 当前 game state 重算 MarketState (constexpr 决策树, < 100ns).
- 状态机 tick (1ms 心跳) 不必触发 MarketState 切换, 因为 evaluate 即时算.
- MarketState 变化 audit: **不**每次落 audit (会爆量), 仅在 STALE 触发 WARNING / HALT 时落 audit, 含当时 MarketState.

**性能预算**:
- classify() + threshold_of() 共 < 200ns (constexpr 表 + 决策树短路).
- 不挤 G3 evaluate 200us 预算.

### 14.5 触发动作矩阵 (替换 v0.2 §11.2)

| 触发条件 | 状态转移 (RM 主) | evaluate 返回 | KELLY 切换 |
|---|---|---|---|
| `freshness > WARNING(state)` | RUNNING → WARNING (该 market 计数) | DEFERRED | 0.5x (该 market) |
| `freshness > HALT(state)` | WARNING → HALTED (全局 if N markets stale > M / 全局 if 关键 market) | REJECTED(STALE_DATA) | 取消未成交 |
| 对账 freshness > 10s | RUNNING → WARNING (全局) | DEFERRED | 0.5x 全局 |
| 对账 freshness > 30s | WARNING → HALTED (全局) | REJECTED(STALE_DATA) + 电话告警 | 全取消 |
| MarketState 切到 INPLAY_HOT_CRIT 且 freshness > 200ms | RUNNING → WARNING | DEFERRED | 0.5x |
| MarketState 切到 SETTLED | (该 market 后续 evaluate) | REJECT(MARKET_SETTLED) v0.3 改用现有 INVALID_INTENT + sub-reason | 不开仓 |

**HALTED 升级条件** (v0.3 微调):
- 单一 market HALT 不立即拖全局; **同时 ≥ 3 个 market HALT 或 关键 market (单笔敞口 top-N) HALT** → 全局 HALTED.
- 阈值 N=3 / M= top 5 → 待小梁会签 (`STALE_GLOBAL_HALT_MARKET_N` / `STALE_GLOBAL_HALT_TOPN`).

### 14.6 D-06 红线遵守复核

- INPLAY_HOT_CRIT HALT 800ms ≤ 30s ✓
- INPLAY_HOT HALT 2s ≤ 30s ✓
- INPLAY_COLD HALT 10s ≤ 30s ✓
- PREGAME HALT 15s ≤ 30s ✓
- SETTLED HALT 30s = 30s ✓ (硬上限)
- 对账 HALT 30s = 30s ✓ (硬上限)

任一配置变更让任一档 HALT > 30s = 启动失败 (config self-check).

---

## 15. G8 RM 误拒率观察项 (D-04 小董 §2 落地)

### 15.1 定义 (引小董 §2 原文)

```
G8 (观察项, 非 hard gate):
  M4.5 窗口内 RM REJECT 总数 R
  抽样 min(R, 30) 笔走 stcpp-audit replay
  老韩 + 小梁双签判定 "本应放行" = N
  误拒率 = N / sampled, 月度报表
  阈值: ≤ 5% 月度 (D-04 GM Agreed, 不是 v0.2 misalignment#5 的 2%)
  连续 2 个月 > 5% → 触发 RM v0.3+ 阈值复议 (走 ADR, 不动 G1-G7)
```

### 15.2 RM 内部支持 (v0.3 落地)

| 项 | 实现 |
|---|---|
| reject 计数 | metric `rm_reject_count{reason=...}` Prometheus exporter (沿用 v0.2 §14.2) |
| reject audit 完整 | v0.2 §5.2 字段已含决策 + bankroll 快照 + rule_trace, replay 可用 |
| replay 工具 | `stcpp-audit replay --reject-only --window 14d --sample 30` (派单 @小宋) |
| 双签判定 SOP | @小董 v1.1 §5.6 (deadline 6/19) 给 SOP, RM owner = 老韩 + 财务 = 小梁 |
| 月度报表 | @小郑 dashboard `rm_misreject_rate_monthly` (Grafana panel) |

### 15.3 不进 hard gate 的理由 (复述小董 §2)

1. **样本量不足**: 14 天 50 笔 reject × 30% reject 率 = 15 笔, 2% 阈值 = 0.3 笔, 离散 0/1 无统计意义.
2. **人工判定主观**: 双签判定引入人因, 与 G1-G7 全机器判定原则违背.
3. **不卡 M4.5 first-pass**: G8 是 long-run monitoring, 不是 14 天 gate.

### 15.4 G8 触发后的处置 (RM owner 视角)

- 月度 > 5%, 一次性: 告警, 不动阈值. 老韩 + 小梁 next month sample 复审.
- 连续 2 个月 > 5%: 触发 RM v0.3+ ADR, 候选缓解措施:
  - STALE 阈值放宽 (WARNING 阈值, **不动 HALT 上限 30s**).
  - KELLY_FRACTION_WARNING 放宽 (但不动 KELLY_FRACTION 常态).
  - reject 子项细分定位 (是 STALE_DATA 多还是 EDGE_CI_NEGATIVE 多).
- **永不动**: G1-G7 hard gate / D-06 红线 (30s) / SAFE_MODE 默认.

---

## 16. STRATEGY_DECAYED kill switch (OQ-D13 + 小董 §5 落地)

### 16.1 触发条件 (Bayesian decay monitor, 小董 §5)

```
Bayesian decay monitor (μ_i = strategy i 真 daily PnL mean):
  prior:    N(0, σ_prior²), σ_prior = 0.3 × bankroll × historical_vol
  posterior: 滚动 14 天数据 + Bayesian update
  
状态分级:
  GREEN:  P(μ_i > 0 | data) > 0.7
  YELLOW: P(μ_i > 0 | data) ∈ [0.5, 0.7]
  RED:    P(μ_i > 0 | data) ∈ [0.3, 0.5]
  BLACK:  P(μ_i < 0 | data) > 0.3 持续 2 周

触发 BLACK → RM kill switch
```

### 16.2 RM evaluate 行为 (老韩 owner)

```
evaluate(intent):
  if strategy_decay_state[intent.strategy_id] == BLACK:
    return REJECTED(STRATEGY_DECAYED)
  # 否则继续 R0..R12 正常评估
```

**强语义**:
- 不分 RUNNING/WARNING/SAFE_MODE/DRAIN: BLACK 一律 REJECT (开仓 + 平仓都 reject, 平仓走人工撤单).
- 不允许 hot reload 自动恢复, **必须走 ADR + 老钱 + 老雷 + 小梁 三方签** (小董 §5.4).
- audit emit **`AET_STRATEGY_DECAYED`** (新 audit event type, @老唐 schema v1.2 加 enum).

### 16.3 输入来源 (RM <- 小董 monitor)

```
RM 读取通道:
  - 文件: /var/lib/stcpp/strategy_decay_state.json (atomic write, 小董 cron 每日 00:05 UTC 更新)
  - 内存 cache: RM 启动 + 每 60s reload, mtime 比较, 不变 skip
  - 字段: { "strategy_id_v1": "GREEN|YELLOW|RED|BLACK", "since_ts": <ulid>, "p_neg": 0.xx, "reason": "..." }
```

**fail-safe**:
- 文件缺失或 mtime > 48h → 视为 BLACK (保守 fail-closed) **OR** 视为 GREEN?
  - **v0.3 决议**: 视为 **`UNAVAILABLE`** (新中间态), evaluate 返回 `DEFERRED` (不 REJECT 也不 APPROVE), 触发电话告警 @老韩 + @老雷.
  - 这是为了避免小董 cron 故障导致全盘 REJECT (DoS 风险).
- 文件 schema 错误 (jq 解析失败) → 同上, UNAVAILABLE + 告警.

### 16.4 解锁路径 (小董 §5.4 + 老韩补)

```
BLACK → 解锁:
  1. 小董 monitor 重新计算 P(μ>0|data) > 0.5 (即 RED 以上, 不是 GREEN 即可) AND 持续 1 周
  2. ADR 起草, 含: 历史 P(μ<0) 曲线 + 缓解措施 + 重启后监控期 (7 天)
  3. 三签: 老钱 (CPO) + 老雷 (GM) + 小梁 (financial-expert)
  4. CLI: stcpp-admin risk strategy unlock --id <strategy_id> --adr <path> --signers ...
  5. RM 状态 BLACK → MONITORING (7 天观察, 不全开 KELLY 仅 0.5x)
  6. MONITORING 期满 OK → 切回 GREEN, KELLY 恢复常态
```

CLI 落 audit `AET_STRATEGY_UNLOCK`, 双签字段.

### 16.5 与 paper mode 的关系 (R-11 重申, D-12)

- **paper mode 走完整 RM** (v0.2 R-11): paper intent 也走 evaluate, 也受 STRATEGY_DECAYED 影响.
- D-12 paper/live ULID 方案 A: ULID generator 独立, audit payload `mode: enum {Live, Paper, Shadow}` 字段区分.
- Bayesian decay monitor 输入数据:
  - **live PnL only** (生产期), 不混 paper.
  - paper 期 (M4.5 前): monitor 用 paper PnL, BLACK 触发 kill switch 防止 paper 错误信号污染 M4.5 gate.
  - 切换点: live 启动 (`stcpp-admin risk strategy live-mode <id>`) 后, decay monitor 自动切 live PnL 源.

### 16.6 kill switch UI (派单 @小尤)

- Grafana panel "Strategy Decay State": 每 strategy 颜色 GREEN/YELLOW/RED/BLACK, history 14d.
- BLACK 触发 → PagerDuty 电话告警 (@老韩 + @老雷 + @小梁) + Slack incident channel.
- unlock CLI 提示 + 双签字段 wizard.
- **派单**: @小尤 (UX) Sprint-2 W3 出 wireframe.

### 16.7 参数表 (新增到 §7)

| 参数 | 类型 | v0.3 建议 | 待会签 | 调整规则 |
|---|---|---|---|---|
| `BAYES_DECAY_P_THRESHOLD` | prob | 0.3 (P(μ<0)) | @小董 | 只可调高 (更保守 = 更难触发) |
| `BAYES_DECAY_PERSIST_DAYS` | days | 14 | @小董 | 只可调高 |
| `BAYES_DECAY_FILE` | path | `/var/lib/stcpp/strategy_decay_state.json` | — | — |
| `BAYES_DECAY_FILE_MAX_AGE_HOURS` | h | 48 | @小董 | — |
| `BAYES_DECAY_RELOAD_INTERVAL_S` | s | 60 | @老韩 | — |
| `STRATEGY_UNLOCK_MONITORING_DAYS` | days | 7 | @老雷 | — |

### 16.8 R-5 残留风险

- **R-5 (中)**: Bayesian decay 误报 (false BLACK) 概率. 小董 §5 prior σ_prior = 0.3 × bankroll × hist_vol 偏紧, 早期 sample 不足时 posterior 可能误判. 缓解: 14 天持续门槛 + 解锁路径需 7 天观察期, 双重缓冲.

---

## 附录 A — v0.3 PR 拆分与 owner

| PR | 内容 | Owner | 依赖 |
|---|---|---|---|
| PR-9 (v0.3 新) | MarketStateClassifier 接口 + constexpr threshold table | 老韩 | — |
| PR-10 (v0.3 新) | classify() 决策树实现 | 小袁 | PR-9 (老韩定接口) |
| PR-11 (v0.3 新) | INVALID_INTENT 强化 (book_snapshot_ts_ns 检测) + 单测 | 老韩 | 小肖 §2 |
| PR-12 (v0.3 新) | STRATEGY_DECAYED enum + file watcher + evaluate hook | 老韩 | 小董 bayes_decay_monitor.py |
| PR-13 (v0.3 新) | AET_STRATEGY_DECAYED audit emit | 老韩 + 老唐 | 老唐 schema v1.2 |
| PR-14 (v0.3 新) | G8 月度报表 + replay tool | 老韩 + 小宋 + 小郑 | — |
| PR-15 (v0.3 新) | kill switch UI wireframe | 小尤 | PR-12 |

---

## 附录 B — 不耻下问 (派单回执)

| 我问谁 | 问题 | 回执 deadline |
|---|---|---|
| @小袁 | MarketState classify() C++ 实现细则 (`docs/RESEARCH/xiaoyuan-microstructure-v1.1.md` §1) | Sprint-2 中 |
| @小董 | P(μ<0) 计算细则 + `bayes_decay_monitor.py` 文件 schema 定稿 | Sprint-2 末 |
| @小尤 | kill switch UI wireframe (Grafana panel + PagerDuty + unlock wizard) | Sprint-2 W3 |
| @老唐 | audit schema v1.2 加 `AET_STRATEGY_DECAYED` + `AET_STRATEGY_UNLOCK` | Sprint-2 W2 |
| @小宋 | `stcpp-audit replay --reject-only` G8 工具 | Sprint-2 末 |
| @小郑 | Grafana panel `rm_misreject_rate_monthly` + `strategy_decay_state` | Sprint-2 末 |
| @小梁 | §14.5 `STALE_GLOBAL_HALT_MARKET_N` (=3) / `STALE_GLOBAL_HALT_TOPN` (=5) 会签 | Sprint-2 中 |

---

**END v0.3.** 等老郭 24h sign-off + 小梁 §14.5 全局升级阈值会签 → bump v1.0 进 Sprint-2 W2 实现.
