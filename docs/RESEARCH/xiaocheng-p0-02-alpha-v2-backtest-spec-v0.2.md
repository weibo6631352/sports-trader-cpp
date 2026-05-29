# P0-02 Alpha v2 数学定义 + Backtest Spec v0.2

- **Owner**: 小程 (quant-signal-research, C 单元 IC #19)
- **Last review**: 2026-05-29
- **Status**: v0.2 正式稿 — 待小梁 review → 老雷 ack → 老彭 8-31 回测
- **截止**: C-A1 数学定义 W10W2; C-A2 backtest spec W10W3; net edge 数字 8-31
- **验收人**: 小梁 (C 主管)
- **派单来源**: 小梁 §8.1 C 单元 W10+ backlog (GM 推进指令 v2)
- **关联文档**:
  - `xiaocheng-w10-w1-p0-02-spec-v02.md` (信号 spec v0.2 — 触发条件 + alpha 参数 SSOT)
  - `xiaojiang-backtest-framework-v0.2-cpp.md` (小蒋 backtest framework C++ SSOT)
  - `laolei-2026-drive-directive-paper-profit-v1.md` (GM-PAPER-G §3 门禁)
  - `laoqian-w8-w5-profitability-kr-v1.md` (老钱 §4 alpha 数字)
  - `laopeng-w9-inplay-edge-gross-net-confirm.md` (gross/net 歧义澄清)
  - `/include/stcpp/strategy/p0_01_goalserve_devig.hpp` (已落 de-vig 代码现状)
- **拒接声明**: 本文件是 alpha 数学定义 + backtest spec, 不含 C++ 代码, 不做回测实现 (实现归小蒋)。

---

## §0 现有 De-Vig 代码现状 (Grep 确认)

在写本 spec 前, 已 grep 确认现有代码库状态:

| 文件 | 内容 | 状态 |
|---|---|---|
| `include/stcpp/strategy/p0_01_goalserve_devig.hpp` | ADR-008 multiplicative de-vig, 8-9 家等权均值, MIN_BOOKMAKERS=3 | 已落, ABI 锁定 |
| `src/stcpp/strategy/p0_01_goalserve_devig.cpp` | `compute_multiplicative_devig()` 实现, span<const BookmakerOdds> 入参 | 已落 |
| `include/stcpp/strategy/p0_01_pinnacle_no_vig.hpp` | 单源 no-vig + PmSnapshot / IGameStateSource 接口 | 已落 |
| `tests/unit/test_p0_01_goalserve_devig.cpp` | 单测覆盖 | 已落 |

**Pregame 路径**: 多家 bookmaker (8-9 家, bookmaker_id 14/15/16/17/18/65/105/144) → `compute_multiplicative_devig()` → 等权均值 fair_p。MIN_BOOKMAKERS=3 防单源退化。

**Inplay 路径**: OQ-P02-3 实证确认 inplay feed 仅 bet365 单源 (ADR-008 §5 例外), 已在 `xiaocheng-w10-w1-p0-02-spec-v02.md` 正式写入。inplay de-vig 是本 spec 不涉及的 stretch 路径 (见 §1.3)。

**本 spec 重点**: pregame 8 家聚合的数学定义精确化 + net-edge 口径统一 + backtest 防泄漏规范 + bootstrap CI 方法。

---

## §1 Alpha v2 数学定义 (C-A1)

### §1.1 Pregame 8 家 De-Vig 聚合公式

**适用场景**: pregame Moneyline (T_kickoff - now ≤ 6h, LiveSection = Soon), 2026 MVP 唯一计入 GM-PAPER-G 的分桶。

#### 步骤 1: 单家 Bookmaker multiplicative de-vig

对于第 i 家 bookmaker (decimal odds yes_i / no_i 来自 Goalserve pregame getodds endpoint):

```
p_yes_raw_i  = 1 / odds_yes_i
p_no_raw_i   = 1 / odds_no_i
overround_i  = p_yes_raw_i + p_no_raw_i        -- 通常 1.02-1.06
p_yes_fair_i = p_yes_raw_i / overround_i       -- multiplicative de-vig (ADR-008)
```

合法性检查 (任一不满足则跳过该 bookmaker):
- `odds_yes_i > 1.0 + ε` (ε = 1e-9)
- `odds_no_i > 1.0 + ε`
- `overround_i > 0` 且 finite

#### 步骤 2: 跨 N 家等权均值聚合

```
N_valid = count(bookmakers passed validity check)

若 N_valid < MIN_BOOKMAKERS (= 3) → fair_value = nullopt (不出信号)

p_yes_fair_avg = (1/N_valid) * Σ_i p_yes_fair_i    -- 等权均值
overround_avg  = (1/N_valid) * Σ_i overround_i     -- 诊断用, 记入 feature
```

**8 家 bookmaker 清单** (小段 v3 ETL-12 确认):

| bookmaker_id | 名称 |
|---|---|
| 14 | 10Bet |
| 15 | WilliamHill |
| 16 | bet365 |
| 17 | Marathon |
| 18 | Unibet |
| 65 | BetVictor |
| 105 | 1xBet |
| 144 | Betano |

注: 老彭 W6 EOW 第 9 家 (TBD) 若实证接入, MIN_BOOKMAKERS 维持 3, 均值计算自动纳入。接口不变。

**等权均值的理由**: 当前无充分历史数据支撑精度加权 (accuracy-weighted 聚合需要 per-bookmaker 历史误差)。等权是保守基线, 后续 M+6 以后可升级为精度加权 (alpha v3 研究项)。

#### 步骤 3: 最终 fair_value

```
fair_value = p_yes_fair_avg     -- ∈ (0, 1), 不做归一化 (multiplicative 天然归一 Σp=1)
```

**与现有代码的对应关系**: 上述公式与 `compute_multiplicative_devig()` 实现完全一致 (已验证), 本文档是数学 SSOT。

### §1.2 C2 ≥ 6¢ Net-Edge 门 (Pregame 版)

Pregame C2 门槛定义 (与 P0-01 `EDGE_THRESHOLD = 0.05` 的关系说明):

```
gross_edge = |PM_mid - fair_value|

-- Pregame net-edge 门 (本 spec 新增, 与 P0-01 EDGE_THRESHOLD 区分):
-- P0-01 代码用 EDGE_THRESHOLD = 5¢ (gross edge, 触发条件 1)
-- Alpha v2 net-edge 门额外要求:
--   gross_edge > 6¢  →  net_edge ≈ 6% - 3% fee - 0.3% slip = +2.7%  (充分正期望)
--   gross_edge = 5¢  →  net_edge ≈ 5% - 3% - 0.3% = +1.7%  (边际, 不满足 alpha v2 门)
--   gross_edge = 4¢  →  net_edge ≈ 4% - 3% - 0.3% = +0.7%  (barely positive)
--   gross_edge = 3¢  →  net_edge ≈ 3% - 3% - 0.3% = -0.3%  (负期望)

C2_pregame_alpha_v2 = (gross_edge >= 0.06)   -- 6¢ = alpha v2 net-edge 正期望充分条件
```

**注意**: P0-01 代码现行触发条件是 `|edge| > 0.05` (5¢ strict)。Alpha v2 的 6¢ 门是 **backtest 筛选口径**, 不是对现有 P0-01 代码的修改请求。Backtest 时对历史 trade 用 6¢ 过滤, 出 net-edge 数字。如果 6¢ 子集 OOS 显著 → 可对小卢实现层提 C2 从 5¢ 升 6¢ 的修改请求 (走 ADR 流程)。

### §1.3 25% 死区逻辑 (Pregame 版)

```
-- score dead-zone (pregame 低频使用, 主要针对已开赛后的 near-pregame 状态):
score_dead_zone = (fair_value > 0.75) || (fair_value < 0.25)

-- size dead-zone:
size_dead_zone = (kelly_raw_size < $500)    -- bankroll 0.5% 以下不值得下

-- 合并:
trade_eligible = !score_dead_zone && !size_dead_zone
```

**Pregame 语境下 25% 死区的意义**: fair_value > 0.75 意味着 8 家 book 共同认为某方大热 (赔率约 1.33 以内)。此时 overround 压缩效应可能使 de-vig 精度偏差占 gross_edge 的比例偏高, 信噪比下降。死区是保守防御, 非严格 alpha 信号衰减。

### §1.4 Soccer/Tennis 扩展入口 (CPO 已拍 2026 仅 Moneyline, 留 spec 入口)

**CPO 2026-05-29 拍板 (老钱 §8.3)**: 2026 = Moneyline only paper 持续盈利, Soccer 3-way 不纳入 2026, Tennis 列 stretch。本 spec 留扩展入口但不激活:

```
-- Sport 路由 (Alpha v2 当前仅激活 Basketball + Soccer 2-way Moneyline):
sport_router(sport, market_type):
  case (Basketball, Moneyline):  → de_vig_2way(bookmakers)   -- 已激活, 本 spec 主路径
  case (Soccer, Moneyline_2way): → de_vig_2way(bookmakers)   -- 预留, 2-way 化处理 (draw 边吸收)
  case (Soccer, 3way):           → de_vig_3way(bookmakers)   -- 预留, GM-PAPER-G 通过后评估
  case (Tennis, Moneyline):      → de_vig_2way(bookmakers)   -- stretch, year-end 若有余量
  default:                       → nullopt                   -- 2026 不处理

-- Soccer 2-way 化说明:
-- Polymarket Soccer Moneyline 是 Home/Away 二元市场 (draw 不单独结算 / 或 draw = Away 赔付规则)
-- De-vig 公式: overround_2way = 1/odds_home + 1/odds_away (不含 draw)
-- 精度损失约 0.3-0.8pp vs 3-way (老彭 W9 W5 §3.2)
-- 2026 Soccer 扩展前需老彭实证 Soccer 2-way de-vig 精度 → 单独 ADR
```

**Tennis 说明**: Tennis 赔率结构与 Basketball 2-way 相同, 公式完全复用。主要差异是 league/tournament 覆盖率 (小段 ETL 需确认 Goalserve Tennis pregame bookmaker 覆盖数量是否 ≥ 3)。

---

## §2 Net Edge 计算口径 (Pregame 单独分桶)

### §2.1 Gross Edge → Net Edge 扣除链

```
gross_edge(t)   = |PM_mid(t) - fair_value(t)|     -- 触发时刻 t 的价差

-- 扣除 1: Polymarket taker fee (老李 W85 confirm, V2 不变)
fee_rate        = 0.03                             -- 3% taker fee (C2 fee 短路)

-- 扣除 2: 预期 slippage
slippage_rate   = FillRateModel.expected_slippage(market, size, depth)
                -- 小肖 slippage model v1 C++ 实现 (BR-5)
                -- Pregame 参考: mid-spread ~0.2-0.5%, queue position ~0.1-0.3%
                -- 保守估算 slippage_rate ≈ 0.003 (0.3%) for pregame near-even

-- 净 edge (per trade):
net_edge(t)     = gross_edge(t) - fee_rate - slippage_rate

-- 净 edge 正期望充分条件 (alpha v2 C2 门):
net_edge > 0 ← gross_edge > fee_rate + slippage_rate = 3% + 0.3% = 3.3%
             ← gross_edge > ~3.3% ≈ 3.3¢ (near-even Moneyline)
C2 ≥ 6¢ → gross_edge ≥ 6% → net_edge ≥ +2.7%  (有充足正期望缓冲)
```

### §2.2 Pregame 分桶定义 (GM-PAPER-G 要求)

**Pregame 分桶** (唯一进 2026 GM-PAPER-G gate):
- LiveSection = `Soon` (T_kickoff - now ≤ 6h, game not started)
- 数据来源: Goalserve pregame getodds endpoint (非 inplay feed)
- 触发时刻: T_kickoff - as_of_ts_ns ∈ (0, 6h)

**Inplay 分桶** (单独记账, 不进 12 月 gate):
- LiveSection = `Live` (game.live == true)
- 数据来源: Goalserve inplay.goalserve.com/inplay-soccer.gz (单 bet365, ADR-008 §5)
- Alpha v2 净 edge 中位为负 (老彭 W9 W2 确认: -1.8% ~ -0.8%), 不进 2026 gate
- 老彭 inplay 路径交给 stretch 研究, 不计入本 spec 8-31 交付承诺

**混算禁止**: Pregame 和 inplay trade 必须在 trades.parquet 的 `bucket` 列中标记 (`PREGAME` / `INPLAY`), 统计指标 (hit rate / Sharpe / net-edge) 分桶独立计算, 不允许混合 average。

### §2.3 Net Edge 口径完整定义

```
-- 单笔 trade 的 net PnL (以 USDC 计, 已成交):
-- Polymarket 二元结算: 赢 → (1 - fill_price) * size; 输 → -fill_price * size

realized_pnl_if_win  = (1 - fill_price) * size_usdc - fee_usdc - slippage_usdc
realized_pnl_if_loss = -fill_price * size_usdc - fee_usdc - slippage_usdc

-- fee_usdc = fill_price * size_usdc * fee_rate  (买 Yes 时, Yes token 价格 = fill_price)
-- slippage_usdc = size_usdc * slippage_rate

-- 期望 PnL (以 fair_value 为真实概率):
expected_pnl = fair_value * realized_pnl_if_win + (1 - fair_value) * realized_pnl_if_loss

-- 化简:
expected_pnl = fair_value * (1 - fill_price) * size
             - (1 - fair_value) * fill_price * size
             - (fee_rate + slippage_rate) * fill_price * size
             = (fair_value - fill_price) * size - (fee_rate + slippage_rate) * fill_price * size

-- net_edge_per_dollar = expected_pnl / size:
net_edge = (fair_value - fill_price) - (fee_rate + slippage_rate) * fill_price
         ≈ gross_edge - fee_rate - slippage_rate    (fill_price ≈ 0.5 near-even 近似)
```

**R-20 时间戳约束**: fill_price 必须使用 as_of_ts_ns 时刻的 PM mid (非前一个 tick, 非 kickoff 后价格)。4 时间戳单调约束: `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`。

---

## §3 Backtest Spec (C-A2)

### §3.1 Point-in-Time 防 Look-Ahead (红线)

**红线**: 回测 = 实盘同处理逻辑。任何 look-ahead 污染 = 假盈利, 小余 attestation 不签字。

具体要求:

1. **数据对齐**: 每个回测 tick 使用的 fair_value 只能来自 `as_of_ts` 时刻已知的 bookmaker 报价 (snapshot_ts_ns ≤ as_of_ts_ns)。禁止使用 kick-off 后的 bookmaker 报价回算 pregame fair_value。

2. **特征计算 PIT**: 小段 ETL 提供的 Parquet 数据中, 每行的 `snapshot_ts_ns` 是 bookmaker 报价的数据源时间戳, 不允许用更晚时间点的报价替代。

3. **结算价格 PIT**: Polymarket 结算价 (outcome 0/1) 只能用于 realized PnL 计算, 不得反向用于 fair_value 校准 (即回测时不知道结果, 和实盘一致)。

4. **DataAccessGuard**: 小蒋 backtest framework v0.2 `DataAccessGuard<Phase>` C++ template 在编译期强制 IS/OOS 隔离, 本 spec 指定 backtest 结果须经过此 guard 验证 (BR-3, 老郭架构不可妥协)。

5. **feature_snapshot_id**: 每笔 signal tick 的 `feature_snapshot_id` 必须非空, 与 R-20 4 时间戳一起构成 audit trail。

### §3.2 Walk-Forward + Purged K-Fold 切分

**总体方案**: 滚动 walk-forward + purged embargo, 与小蒋 backtest framework v0.2 §3 设计对齐。

#### Pregame Moneyline 回测数据窗 (建议)

```
-- 当前可用历史数据 (待小余/老彭确认):
-- Goalserve pregame getodds: 2024-10 起 (老彭 CSV backfill)
-- Polymarket book snapshots: 2024-10 起 (小余 Parquet)

IS 窗 (参数调优 + 模型训练):
  2024-10-01 ~ 2025-02-28  (~5 个月, 覆盖 NBA/NFL 赛季中段)

Embargo:
  2025-03-01 ~ 2025-03-07  (7 天 embargo, 防邻近 game 泄漏)

OOS 窗 1 (主要验证):
  2025-03-08 ~ 2025-06-30  (~4 个月, NBA 季后赛 + MLB 开季)

OOS 窗 2 (季节外推验证):
  2025-07-01 ~ 2025-10-31  (~4 个月, MLB 赛季 + NFL 开季)

二段 OOS (regime shift 检验):
  2025-11-01 ~ 2026-02-28  (~4 个月, NBA/NFL 新赛季)
```

注: 具体窗口以小余数据完整性 attestation 为准。若 2024-10 数据不完整, IS 起点可调整。

#### Purged K-Fold 防泄漏

```
-- purge 逻辑:
-- 1. 按 game_id 分组: 同一场比赛的所有 tick 只能进同一个 fold
--    (防止同场比赛的 pregame tick 进 IS, 而同场比赛的 inplay tick 泄漏 outcome 到 IS)
-- 2. Embargo: 同 game_id 所在 fold 的邻近 7 天 tick 也排除
--    (实现: arrow::compute::Hash 按 game_id 分组, 再按时间排序 embargo)
-- 3. Pregame 特有: 同一 market_id 的 tick 全部进同 fold (market = 场比赛)
```

#### 参数扫描网格 (IS 阶段)

| 参数 | 扫描值 | 组合数 |
|---|---|---|
| C2 gross_edge 门 | 5¢, 6¢, 7¢ | 3 |
| MIN_BOOKMAKERS | 3, 4, 5 | 3 |
| Kelly fraction | 0.15, 0.20, 0.25 | 3 |
| 死区阈值 | 20%, 25%, 30% | 3 |

总组合: 81。Bonferroni 校正 p-value = p × 81 (小蒋 `stats::bonferroni_adjust`)。校正后 p < 0.05 才算 IS 通过。

### §3.3 度量指标口径 (与 GM-PAPER-G 对齐)

所有指标口径以 **net edge** (扣 fee + slippage) 为基础, 分 pregame / inplay 两桶独立统计。

| 指标 | 定义 | GM-PAPER-G 门槛 (pregame bucket) |
|---|---|---|
| Hit Rate | n_wins / n_trades (方向正确定义: side=Buy 且 outcome=Yes=1, 或 side=Sell 且 outcome=No=1) | ≥ 52% (正收益日, GM 裁决 §9.2) |
| Net Edge / trade | E[net_pnl] / size (% per trade) | > 0 (net PnL > 0 for 30d window) |
| Sharpe (OOS, 30d) | (mean_daily_net_pnl / std_daily_net_pnl) * sqrt(252) | ≥ 0.5 (小梁背书) |
| MDD | max drawdown of cumulative net PnL curve | ≤ 10% (soft) / ≤ 15% (RM hard cap) |
| n_trades | 总成交笔数 | ≥ 100 (GM-PAPER-G §3) |
| Bootstrap Sharpe CI | 见 §3.4 | 下界 > 0 |
| t-test p | 单尾 H0: E[net_pnl] ≤ 0 | p < 0.10 |

**Sharpe 计算细节**:
- 日收益率序列: 每日 sum(net_pnl_per_trade) / bankroll
- 若某日无交易: 收益率 = 0 (不排除, 保持样本连续性)
- 年化: × sqrt(252) (日历天年化, 不用交易天年化, 避免低频信号虚高 Sharpe)

**MDD 计算**:
- 以累计 net PnL 曲线 (USDC) 计算, 非收益率曲线
- MDD = max(peak - trough) / peak_equity (其中 peak_equity = initial_bankroll + cumulative_pnl_at_peak)

### §3.4 Bootstrap CI 方法 (5000 次, 对齐 GM-PAPER-G)

**背景**: GM-PAPER-G 要求 n_trades ≥ 100 AND bootstrap Sharpe CI 下界 > 0 (GM 裁决 §9.3)。

**方法**: Stationary Bootstrap (Politis & Romano 1994), 适配时序相关性:

```
-- 参数:
n_bootstrap   = 5000      -- 重采样次数 (GM-PAPER-G 明确 5000)
block_size    = 10         -- 平均 block 长度 (天), 捕捉 game-level 相关性
                           -- 体育赛季相关性以周为单位, block=10d 保守
confidence    = 0.90       -- 90% CI (双侧 5%/5%, GM-PAPER-G: CI 下界 > 0)

-- 重采样过程:
for b in 1..n_bootstrap:
    resample = stationary_block_resample(daily_pnl_series, block_size)
    sharpe_b = compute_sharpe(resample)    -- 同口径: sqrt(252) * mean/std

CI_lower = percentile(sharpe_samples, 5%)
CI_upper = percentile(sharpe_samples, 95%)

-- 通过条件:
pass = (CI_lower > 0) AND (n_trades >= 100) AND (mean(daily_pnl) > 0)
```

**实现归属**: 小蒋 backtest framework v0.2 实现 (C++ Boost.Math + std::mt19937_64)。本 spec 只定义方法和参数, 不写代码。

**为什么用 Stationary Bootstrap (非简单 iid Bootstrap)**:
- 体育比赛有 regime 相关性 (同赛季内 bookmaker 行为相似, 赛季切换有 regime shift)
- iid bootstrap 低估方差, 导致 CI 虚紧
- Stationary bootstrap block_size = 10d 在保守性和计算效率间平衡

**为什么 n_trades ≥ 100 是硬门**:
- 30d 窗口若日均 < 4 笔 → 不足 100 笔 → bootstrap CI 无意义 (小梁背书 GM 裁决 §9.3)
- Pregame Moneyline 五大联赛 Soccer + NBA: 预估日均触发 3-8 笔 (满足 C2 ≥ 6¢ 过滤后)
- 若 30d 内不足 100 笔, 延长窗口到 45d, 但需记录偏差

### §3.5 DSR (Deflated Sharpe Ratio) 防过拟合

与小蒋 backtest framework v0.2 §4.2 一致:

```
DSR = Z(SR_hat) - correction(σ_SR, γ_SR, T, N_strategies)
-- N_strategies = 参数扫描组合数 = 81 (§3.2)
-- 门槛: DSR > 1.0 (小蒋 §4.2 MVP 门槛)
```

DSR < 1.0 表明观测 Sharpe 在 81 次扫描后统计上不显著, 信号不上 paper。

---

## §4 8-31 可交付承诺 (Pregame Moneyline Net-Edge)

### §4.1 小梁本轮 HARD commit (pregame de-vig 8 家聚合)

| 指标 | commit 值 | 置信度 | 来源 |
|---|---|---|---|
| Hit Rate (OOS) | **56-60%** | 中-高 | 老钱 §4 + 小梁背书 |
| Gross Edge / trade | **2-3%** | 中 | 老钱 §4 pregame alpha |
| Net Edge / trade (扣 fee 3% + slip 0.3%) | **约 +1.4-2.4%** | 中 | 本 spec §2.1 推算 |
| Net Edge 正 (gate 门) | **是, C2 ≥ 6¢ 子集** | 高 | §1.2 推算: 6% - 3.3% = +2.7% |
| n_trades (30d OOS) | **≥ 100** (目标 150-300) | 中-高 | 五大联赛 + NBA/NFL 估算 |
| Bootstrap CI 下界 | **> 0** (90% CI) | 中 | 取决于实际 hit rate + n_trades |

**inplay NO commit**: 净 edge 中位 -1.8% ~ -0.8% (老彭 W9 W2), 50-65% 概率为负 (小梁估)。不进 2026 gate, 不在本 8-31 交付承诺内。

### §4.2 8-31 具体交付物

到 2026-08-31, backtest 能产出的 pregame Moneyline net-edge 数字:

| 交付物 | 格式 | Owner | 依赖 |
|---|---|---|---|
| IS net_edge 数字 | OOS 第一轮: mean net_edge / trade (%) + std + n_trades | 老彭 (数字) + 小蒋 (框架) | 历史数据 G1-G3 |
| OOS hit rate | % ± bootstrap 90% CI | 同上 | 同上 |
| OOS Sharpe (30d) | 数值 + bootstrap CI [lower, upper] | 同上 | 同上 |
| 分 C2 阈值 (5¢/6¢/7¢) | 三档对比表 | 老彭回测 | IS 参数扫描 |
| Bonferroni 校正 p | N=81 校正后 p-value | 小蒋框架 | 参数扫描 |
| DSR | > 1.0? | 小蒋框架 | 同上 |
| 数据 attestation | 小余签字 (R-20 4ts 单调, 无 look-ahead) | 小余 | 数据 pipeline |

### §4.3 预期数字区间 (8-31 交付前的先验估算)

以下是基于现有 prior (老彭 + 小梁 commit) 的先验区间, 用于 8-31 实际数字的参照基准:

**Pregame Moneyline (C2 ≥ 6¢, N ≥ 3 books, 死区 25%)**:

| 场景 | Hit Rate | Net Edge / trade | OOS Sharpe (30d) | bootstrap CI |
|---|---|---|---|---|
| 乐观 | 60% | +2.5% | 1.2 | [0.8, 1.6] |
| 基准 | 57% | +1.8% | 0.7 | [0.3, 1.1] |
| 保守 | 55% | +1.2% | 0.4 | [0.1, 0.7] |
| 失败 | <54% | <+0.5% | <0.2 | CI 下界 ≤ 0 |

**失败场景处置 (preregistered)**:
- OOS hit rate < 54%: 信号回炉, 重查数据质量 + 门槛校准, 不上 paper
- CI 下界 ≤ 0: 不满足 GM-PAPER-G n_trades≥100 + CI>0 双门, 不上 paper
- OOS Sharpe < 0.5: 重新评估 C2 门槛 (升至 7¢?) + MIN_BOOKMAKERS (升至 4?)

### §4.4 关键路径依赖 (8-31 能否交付)

| 依赖 | Owner | 截止 | 风险 |
|---|---|---|---|
| Goalserve pregame 历史数据 Parquet (2024-10 起) | 老彭/老李/小段 | **6-12** | 高 (三路径: API / odds API / CSV) |
| Polymarket book snapshot Parquet (2024-10 起) | 小余 | **6-19** | 中 |
| 小蒋 backtest framework C++ skeleton | 小蒋 | **7-09** | 中 (工作量 +4 周 vs v0.1) |
| 数据 attestation (R-20, 小余签字) | 小余 | **8-15** | 低 (有流程) |
| 老彭跑实际回测 + 输出数字 | 老彭 | **8-31** | 中 (依赖框架 7-09 ready) |

**9-30 checkpoint**: 若 8-31 净 edge 数字为负 → 触发全体争议会重排 (GM §9.8), 不硬撞 12 月 gate。

---

## §5 接口契约 (本 spec 新增, 配套 W10 多人讨论会)

### §5.1 Feature 清单 (D↔C 契约, 防 look-ahead)

以下 feature 在 backtest 和 paper 两侧必须使用同一 `libstcpp_features.a` 计算 (BR-1 红线):

| Feature 名 | 类型 | 计算方式 | PIT 约束 |
|---|---|---|---|
| `goalserve_devig_p_yes_fair` | double | `compute_multiplicative_devig().p_yes_fair_avg` | snapshot_ts_ns ≤ as_of_ts_ns |
| `goalserve_overround_avg` | double | `compute_multiplicative_devig().overround_avg` | 同上 |
| `goalserve_books_used` | int | `compute_multiplicative_devig().books_used` | 同上 |
| `pm_mid` | double | `PmSnapshot.mid` | as_of_ts_ns 时刻快照 |
| `pm_top3_liquidity_usdc` | double | `PmSnapshot.top3_liquidity_usdc` | 同上 |
| `pm_fill_rate` | double | `FillRateModel.expected_fill_rate` | 同上 |
| `gross_edge_abs` | double | `|pm_mid - goalserve_devig_p_yes_fair|` | 派生 |
| `net_edge_approx` | double | `gross_edge_abs - 0.03 - 0.003` | 近似 (精确以 slippage model 为准) |
| `live_section` | enum | `LiveSection::Soon / Live` | 同上 |
| `kickoff_delta_s` | int64 | `(kickoff_ts_ns - as_of_ts_ns) / 1e9` | 同上 |
| `bucket` | string | `PREGAME` / `INPLAY` | 分桶必须在 trade time 打标 |

**D↔C 接口签字**: 小余 (D 主管) + 小程 → W10 多人讨论会, 对齐 §8.2 "feature 清单 point-in-time" 契约。

### §5.2 Backtest Signer 接口 (A↔C 契约)

与小蒋 backtest framework v0.2 §6.4 `BacktestSigner` 接口对齐:

- Pregame 信号走 `BacktestSigner.sign_and_submit(audit_id, intent, decision)`
- Slippage 由小肖 `SlippageModel::estimate()` 注入 (BR-5, 同实盘)
- Paper mode 模拟本金: 初始 $100K (GM §6 paper 模拟本金)

---

## §6 不耻下问 (待 8-31 前闭环)

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| Q1 | Goalserve pregame 历史数据 2024-10 ~ 2026-05 完整性确认 (哪些 bookmaker_id 有稳定报价) | 老彭 + 小段 | 6-12 |
| Q2 | Polymarket Soccer Moneyline 历史 book snapshots 是否有 2024-10 以前数据 (Soccer 先于 Basketball?) | 小余 | 6-12 |
| Q3 | 小蒋 backtest framework stationary bootstrap block_size 参数: 10d 是否合理? (体育赛季 autocorrelation 结构) | 小蒋 + 小程 | 7-09 |
| Q4 | IS 参数扫描 81 组合 → Bonferroni N=81 是否过于保守 (可用 Holm-Bonferroni 更宽松但仍有效)? | 小梁 | 6-30 |
| Q5 | Soccer 2-way de-vig 精度损失实证 (老彭 W9 W5 §3.2 提到 0.3-0.8pp, 需 backfill 数据验证) | 老彭 | stretch |

---

## §7 红线自检

| 红线 | 状态 | 说明 |
|---|---|---|
| 回测 = 实盘同处理逻辑 (point-in-time) | SPEC | §3.1 明确 PIT 要求 + DataAccessGuard |
| R-20 4 时间戳契约 | SPEC | §2.3 net edge 口径明确 as_of_ts 约束 |
| inplay 不混 pregame 算 | SPEC | §2.2 分桶禁止混算, `bucket` 列强制打标 |
| bootstrap CI 5000 次 | SPEC | §3.4 方法 + 参数定义 |
| n_trades ≥ 100 硬门 | SPEC | §3.3 指标表 + §4.1 预期 |
| fee 扣 3% (老李 confirm) | SPEC | §2.1 扣除链 |
| slippage 扣 (小肖 model) | SPEC | §2.1 扣除链 + §5.2 接口 |
| inplay NO commit | SPEC | §4.1 明确 |
| 不写 C++ 代码 (拒接) | OK | 本文件纯 spec |
| 不做回测实现 (拒接) | OK | 实现归小蒋 |

---

*— 小程 (quant-signal-research, C 单元 IC #19), 2026-05-29*
*待小梁 first review → 老雷 ack → 配合老彭 8-31 回测交付*
