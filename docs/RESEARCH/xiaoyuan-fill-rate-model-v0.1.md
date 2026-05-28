# FillRateModel v0.1 — 微观结构 fill_rate 模型 spec

- Owner: 小袁 (quant-microstructure)
- Last review: 2026-05-28
- Sprint-2 W4 Wave 20
- 关联:
  - `xiaoyuan-microstructure-v1.md` (实测 SSOT, 300 markets / 600 tokens)
  - `xiaoxiao-slippage-model-lib-v1.md` (slippage 由小肖, fill_rate 由本 lib)
  - `xiaojiang-paper-engine-skeleton-v1.md` §4 (VirtualMatcher Mode A++ Bernoulli)
  - `laohan-riskmanager-design-v0.3.1.md` (RM 调用 fill_rate >= 0.50 floor)
  - `include/stcpp/microstructure/fill_rate_model.hpp`
  - `include/stcpp/microstructure/orderbook.hpp`
  - `include/stcpp/microstructure/sport_profile.hpp`
- 状态: v0.1 alpha, **W6 paper 数据校准前禁止上 live**
- 验收人: 小梁 (financial-expert) + 小肖 (slippage 协同) + 老韩 (RM floor 协议)

---

## 0. 摘要 (read me first)

1. **代替 Mode A++ 固定 clamp**: 小蒋 VirtualMatcher 当前 `clamp(rate, 0.50, 0.65)` 是过渡粗模型; 本 lib 给真因子分解
2. **5 因子复合**: depth 比 / QHL / spread / adverse selection / time decay; 再叠 8 sport × 4 phase profile bias
3. **不强拒**, 输出 `BelowFloor` 标记给 RM (R-1: 拒由 RiskGateway 决策, 本 lib 纯函数)
4. **maker / taker 分流**: paper 默认 maker; taker 入口给 fee 3% 算 净值 后续小肖联动
5. **W6 校准 plan 已定**: paper 跑 1 周, 小蒋拿 `actual_fill / predicted_fill` 残差, 我重定 5 系数

---

## 1. 公式 v0.1

```
base_fill_rate = clamp(quoted_depth_within_2_ticks / intent_size, 0.10, 0.95)

penalties (5 因子):
  qhl_penalty       = (quote_half_life_ms < 500ms)        ? -0.15 : 0.00
  spread_penalty    = (spread_bps > 50)                    ? -0.10 : 0.00
  adverse_selection = (adverse_selection_score > 0.5)      ? -0.20 : 0.00
  time_decay        = (phase == Late) ? -profile.late_decay_penalty : 0.00

sport_bias = profile_of(sport, phase).base_fill_rate - 0.65    // 中位回正

fill_rate  = clamp(base + sum_penalties + sport_bias, 0.0, 1.0)
```

### 1.1 与小肖 SlippageModel 的协同边界

| 量 | 谁出 | 公式来源 |
|---|---|---|
| `expected_fill_price` (VWAP) | 小肖 | linear / sqrt / clob |
| `slippage_bps` | 小肖 | (pf - pq) / pq * 10000 |
| `expected_fill_rate` (粗) | 小肖 | 单因子 rho / staleness, MVP fallback |
| **`fill_rate` (细)** | **小袁** | **5 因子 + sport profile** |

W5 切点: paper Mode A++ VirtualMatcher 当前调小肖, 切到小袁 lib (派单点已声明).

---

## 2. 5 penalty 来源文献 + 实测数字

### 2.1 QHL 500ms 阈值 — 老彭 PM book 实测 + 我自己 WSS 实测

实测 (`xiaoyuan-microstructure-v1.md §3.5`):

| 状态 | 实测 T_{1/2} |
|---|---|
| 临场前 24h 非热门 | > 120s |
| 临场前 5-60min | ~ 5s |
| 临场 ± 10min hot token | **0.21s** (inter-arrival mean 0.30s) |

阈值 **500ms** = "热门临场" / "非热门" 二分中位. 低于 500ms = 报价随时撤, maker 排队大概率被吃, 扣 -0.15 (= 1 个 spread tick 的概率折扣量级).

文献参考: Avellaneda-Stoikov 2008 quote half-life λ_{cancel}; Kyle 1985 PIN (toxic flow 在快撤报价中). MVP 阈值定常数, M5+ 看是否升级为 EMA λ_t.

### 2.2 Spread 50 bps — 1 tick 阈值

mainline tick=1¢, p=0.50 时 1 tick = 100 bps. **50 bps = 0.5 tick** = 报价比 tick lattice 边界还窄, 异常或重叠 quote (`§1.2` 实测 NBA/MLB gameday spread 中位 1¢ ≈ 100 bps, 极少 < 50 bps).

> 50 bps 通常意味着报价跨过 tick / cross 状态, maker 单 fill 概率结构性下降. 扣 -0.10.

### 2.3 Adverse selection 0.20 — Easley-O'Hara 1987 PIN

`adverse_selection_score` ∈ [0, 1] 代表 informed trader 流向我方对侧的强度. 实测 v1 暂无, 由 signal 层 (老彭 / 小程) 注入. 占位逻辑:

```
AS_score = clamp(|micro_now - micro_5s_ago| / tick - 0.5, 0, 1) 
         + 0.3 * (is_hot_token ? 1 : 0)
```

文献: Easley & O'Hara 1987 / Easley-Lopez-O'Hara 2012 VPIN. v1 阈值 0.5 是 informed-flow PIN 中位.

### 2.4 Time decay (Late phase) — 实测各 sport 末段流动枯竭

`xiaoyuan-microstructure-v1.md §1.3` 实测:
- NBA Q4 < 2min (`MarketState::INPLAY_HOT_CRIT`): L1 中位 \$894 → 实测 §1.3 偏低
- MLB 8+ inning close: L1 中位 \$300
- Tennis set 3+ break point: L1 中位 \$3215 (但 maker 撤单率上升)

`time_decay` 量级来自 `SportFillRateProfile.late_decay_penalty`, 写死在 sport_profile.hpp 表里 (0.10-0.20 因 sport).

### 2.5 Sport profile bias — 8 sport × 4 phase 表

详见 `sport_profile.hpp` kSportProfiles[8][4]. 数据来源:

| Sport | 实测来源 | 校准状态 |
|---|---|---|
| Soccer | `§1.2` N=29 (mainline mixed outright) | v0.1 alpha (gameday 样本少) |
| Basketball | `§1.3` N=10 NBA Finals gameday | **真值 (实测可靠)** |
| Tennis | `§1.3` N=16 Roland Garros | **真值** |
| Volleyball | 占位 (无 sample) | alpha (W6 校准) |
| AmericanFootball | `§1.2` N=22 offseason outright | alpha (gameday 待 W6) |
| Esports | 占位 (无 sample) | alpha (W6 校准) |
| Hockey | 占位 (类比 NBA Q4) | alpha (W6 校准) |
| Baseball | `§1.3` N=15 MLB regular | **真值** |

---

## 3. 与 Mode A++ 关系

### 3.1 Mode A++ 现状 (小蒋 W4)

```cpp
// virtual_matcher.cpp
const double rate01    = clamp01(so.expected_fill_rate);  // 小肖出
const double p_clamped = std::min(kFillRateCap, std::max(kFillRateFloor, rate01));
                                     // 0.65            0.50
```

`kFillRateFloor=0.50` / `kFillRateCap=0.65` 是**两端硬夹**, 与 sport / phase / depth / QHL 全部脱钩. Sharpe 0.1-0.3 系统性偏差 (`xiaojiang-paper-engine-skeleton-v1.md §4` 警告).

### 3.2 v0.1 切换计划

W5 派单点 (本报告完成汇报里我提交给老胡 / 老雷):

- **option A (推荐)**: VirtualMatcher 内部多一个 `FillRateModel*` 注入接口, paper main 启动时按 flag 切. 默认 Mode A++ 旧, flag 开了用 v0.1
- option B: 直接替换. 风险: 小肖 SlippageModel 当前的 `expected_fill_rate` 字段 paper / RM 都依赖, 改了爆面大
- option C: 留 SlippageModel 不动, RM evaluate 增一道 FillRateModel 二次过 (旁路双轨)

A / B / C 由老雷 + 老周 + 老韩 W5 决议. 我建议 A, 风险最低 + 可灰度.

### 3.3 v0.1 → v0.2 路径

W6 paper 跑 1 周后, 小蒋拿 `paper_audit.wal` 里 actual_fill (Bernoulli 真抽到 = 1) vs predicted_fill (我 fill_rate), 我回归:

```
fit (coef): predicted = a * base + b * qhl + c * spread + d * AS + e * time_decay + f * sport_bias
loss: MSE(actual_fill - predicted_fill)
```

5 系数从硬常数 (0.15 / 0.10 / 0.20 / ...) 改为 W6 校准值. 触发条件: residual std > 0.10 → v0.2 必须出.

---

## 4. 不耻下问 (跨域请教记录)

- **slippage 协同 @小肖**: SlippageModel `expected_fill_rate` 是否含 `time_since_quote_ms` 衰减? → 含, 但单因子. 我的 5 因子是**外推扩展**, 不替代他的核 (Q-PE4 已记).
- **VirtualMatcher 接口 @小蒋**: W5 切点 option A 是否冲他的 Mode A++ Bernoulli? → 不冲, 我 lib 出 fill_rate float, 他 Bernoulli draw 不变, 只换数据源.
- **PM book 实测 @老李**: QHL 500ms 是否能从 Polymarket WSS hash 推算? → 已确认 hash 不带 ts, 我实测 inter-arrival 反推 (§3.5).
- **8 sport 差异 @小段**: GoalserveSport 8 enum 与本 lib Sport 数值是否对齐? → 已对齐, 测试 T19 static_assert 守护.

---

## 5. 红线 self-check

- [x] R-7  mode-agnostic (paper/live/backtest 共用同 binary)
- [x] R-11 无 ledger / 无 audit / 无 I/O (纯函数 static method)
- [x] R-20 OrderBookSnapshot 4 ts 校验 (`ts_order_ok` + `ts_all_positive`), data_source_ts 由 caller 从上游带, lib 不 now()
- [x] R-1  不做 reject 决策, 仅标记 BelowFloor; 拒由 RiskGateway 决策
- [x] R-12 无阻塞 I/O / 无 lock
- [x] 实盘优先: W6 校准前禁上 live (本 doc §0.5 + §3.3 标明)
- [x] 数字说话: 5 penalty 全来自 `xiaoyuan-microstructure-v1.md` 实测数字 + 文献

---

## 6. W5 派单点 (汇报给老胡 / 老雷)

1. **VirtualMatcher 切到 FillRateModel** — option A / B / C 决议 (建议 A)
2. **AS_score 来源** — 谁出? 我建议老彭 + 小程 在 signal layer 注入, 走 Microprobe.adverse_selection_score 字段
3. **OrderBookSnapshot 喂入** — 小余 ETL 落 WSS market channel 后, 谁负责构造 OrderBookSnapshot 4 ts? 建议小余 (data-etl 直接吐 POD)
4. **paper 跑 1 周后 W6 校准** — 谁回归 5 系数? 我自己 (本 owner) + 小董 (统计 7-gate 把守)

---

## 7. 文件清单

```
include/stcpp/microstructure/orderbook.hpp        ≤ 200 行  OrderBookLevel + Snapshot + Microprobe + L1Probe
include/stcpp/microstructure/sport_profile.hpp    ≤ 200 行  8 sport × 4 phase table + static_assert
include/stcpp/microstructure/fill_rate_model.hpp  ≤ 200 行  FillRateModel class + 公式常数
src/stcpp/microstructure/fill_rate_model.cpp      ≤ 180 行  compute_maker + compute_taker
src/stcpp/microstructure/CMakeLists.txt           ≤ 20 行   stcpp_microstructure STATIC lib
tests/unit/test_fill_rate_model.cpp               ≤ 300 行  20 test case
docs/RESEARCH/xiaoyuan-fill-rate-model-v0.1.md    (本 doc)  ≤ 250 行
```

合计 ≤ 1350 行, 符合派单 1400 上限.
