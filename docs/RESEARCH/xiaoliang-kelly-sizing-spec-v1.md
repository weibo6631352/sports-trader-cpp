# Kelly Sizing 设计草案 v1 — edge → "该下多大"

> **owner:** 小梁 (量化研究部主管, Sharpe/Kelly 主权)
> **last_review:** 2026-05-29
> **status:** Draft — **建议升 ADR, 待老郭 (架构) + 老韩 (RM 主权) + GM 签**
> **联签阻塞项:** 所有 §4 风控边界数值 (λ 默认 / per-market cap / 总敞口 cap / fill_rate floor) **待老韩 (RM 主权) 联签**, 未签前看板 sizing 仅作 advisory 展示, 不作下单真值。

---

## 0. 定位 (这块缺什么)

设计评审会认定: 看板量化真值最缺的一块是把 **edge 翻译成 "该下多大"**。
现状: `/api/v1/quote/{condition_id}` 的 `kelly_fraction` / `suggested_notional` 由 `DemoStateProvider` 写死假数据 (0.042 / 850.0), 没有任何数学口径支撑。

本规范定 **唯一的 sizing 真值口径**: 给定 fair_value + price + bankroll + fill_rate, 算出 `SizingOutput`, 同时是:
1. `/api/v1/quote` 的 `kelly_fraction` / `suggested_notional` 真值来源 (替换 Demo);
2. 策略层 sizing 建议 (注意: **不是放行口径**, 放行权永远在 `RiskGateway::evaluate`)。

**边界声明 (铁律, §4):** 看板/sizing 展示的建议仓位 **永远 ≤ RiskManager 实际放行口径**。本规范的 cap 是 RM `RiskConfig` cap 的 **影子 (shadow)**, 数值同源, RM 是唯一真值。sizing 先于 RM 收敛, 绝不允许 sizing 建议被 RM 拒 (除非市场状态/stale 等非 sizing 维度)。

---

## 1. 数学口径 (小梁主权拍板)

### 1.1 二元 outcome Kelly 公式

Polymarket 体育是二元 share 市场: 买 1 share token, 成本 = `price` (∈ (0,1)), 结算时 win 得 1, lose 得 0。

设:
- `p` = 我方 fair 胜率 = `fair_value` (de-vig + microprice blend, 来自 `FairValueEstimator`, 已 clamp ∈ (kProbEps, kProbMax))。
- `c` = 入场价 = `price` (买方视角的 ask 侧成交价, 或卖方视角)。

**单 share 的盈亏:**
- win 概率 `p`: 收益 `+(1 - c)` per share notional `c` → 净赔率 `b = (1 - c) / c`
- lose 概率 `1 - p`: 损失 `-c` per share → 损失 1 单位 (相对下注额)

代入经典 Kelly `f* = (b·p - (1-p)) / b`, 化简 (binary share 形式):

```
f*_full = (p - c) / (1 - c)          # 买 YES (看涨), p > c 才下
```

**对称卖方 (做空 / 买 NO):** 若 `p < c`, 则买对侧 token, 用 `p' = 1 - p`, `c' = 1 - c`:

```
f*_full = ((1-p) - (1-c)) / (1 - (1-c)) = (c - p) / c
```

**统一写法 (方向由 edge 符号决定):**

```
raw_edge   = p - c                          # signed, 看涨为正
if raw_edge > 0:  f*_full = raw_edge / (1 - c)     # buy YES
elif raw_edge < 0: f*_full = (-raw_edge) / c        # buy NO (sell YES)
else: f*_full = 0
```

> 推导自洽性: `(p-c)/(1-c)` 在 `p→1` 时 →1 (全押), `p=c` 时 →0 (无 edge 不下), 与 net_edge 同号。分母 `1-c` 永不为 0 (c clamp < kProbMax)。

### 1.2 净 edge 口径 (与 RM 对齐, **关键自洽点**)

`f*` 必须用 **净 edge** (扣 fee + slippage) 而非毛 edge, 否则 sizing 会比 RM 放行口径激进, 违反 §4。

RM `check_signal_` 的净 edge 口径 (risk_gateway.cpp §D4, **SSOT**):

```
net_edge_after_fee = edge_ci_lower − kSportsTakerFeeRate × p × (1−p)      # fee_per_unit
                                   (kSportsTakerFeeRate = 0.03)
```

sizing 同源采用 (用 fair_value 的点估计而非 CI 下界做 sizing, CI 下界仅 RM gating; 见 §1.6):

```
fee_per_unit   = kSportsTakerFeeRate × p × (1 − p)        # 与 RM 同公式, 0.03
slippage_unit  = slippage_bps / 10000                     # 来自 fill_rate model / slippage model
net_raw_edge   = |p − c| − fee_per_unit − slippage_unit   # 净 edge, 已扣成本
if net_raw_edge <= 0: SizingOutput 全 0, capped_by = NO_EDGE
```

净 Kelly 用净 edge 替分子:

```
f*_full = net_raw_edge / (1 - c)    # buy YES 方向 (NO 方向用 /c)
```

> Maker 路径无 taker fee (有 0.75% rebate) — 但 sizing **保守一律按 taker fee 0.03 估**, 与 RM `check_signal_` 同源, 不利用 rebate 放大仓位 (纪律 > 收益)。Maker rebate 的收益体现在实际 PnL, 不进 sizing 分子。

### 1.3 Fractional Kelly λ (默认值 + 理由)

全 Kelly 长期增长最优, 但方差极大 (回撤可达 50%+), 且对 `p` 估计误差极敏感 (over-estimate edge → over-bet → 破产风险)。

**默认 λ = 0.25 (quarter Kelly)。**

理由 (三条, 数字说话):
1. **参数误差吸收:** fair_value 来自 baseline sigmoid 先验 + 20% microprice blend (`kDefaultBookBlend`), `model_conf` 仍偏低 (Demo 0.62)。Kelly 对 edge 的过估呈 **二次惩罚** (over-bet 增长率损失 ∝ Δf²)。MacLean-Thorp-Ziemba: 半 Kelly 保留 ~75% 增长率但砍 ~50% 方差; quarter Kelly 在模型未校准期 (MVP, W6 前无实盘校准) 是合规起点。
2. **回撤约束自洽:** 北极星 MDD ≤ 15% (CLAUDE.md §2)。full Kelly 在 Sharpe~1 策略下理论 MDD 中位 ~40%+; λ=0.25 把期望 MDD 拉到 ~10-12% 量级, 与 RM `daily_loss_hard_pct=0.05` / 北极星 15% 一致。
3. **model_conf 联动 (v1 留接口, 默认关):** 进一步可令 `λ_eff = λ_base × model_conf` (置信度低自动缩 sizing)。v1 **不默认开**, 仅记录公式; 开关 + 系数待 W6 校准数据 + 老韩联签。

```
λ_base = 0.25                              # 拍板默认 (待联签确认)
λ_eff  = λ_base                            # v1: 不乘 model_conf (留接口)
# v2 候选: λ_eff = λ_base × clamp(model_conf, 0.3, 1.0)
f_fractional = λ_eff × f*_full
```

### 1.4 fill_rate 折扣后的有效仓位口径

挂单未必全成。`f_fractional` 是 **意图仓位比例**, 乘以期望成交率得 **有效仓位**, 用于 notional 估算 (展示 "实际预期能建多少仓"):

```
fill_rate  = FillRateModel::compute(...).fill_rate     # ∈ [0,1], 小袁 lib
f_effective = f_fractional × fill_rate
```

> fill_rate 仅折扣 **suggested_notional 的有效部分展示**, 不改变意图下单量 (意图量 = `f_fractional × bankroll`, 挂出去等成交)。看板两栏都给:
> - `kelly_fractional` = `f_fractional` (意图比例)
> - `suggested_notional` = `f_fractional × bankroll`, 再过 §1.5 cap (意图名义额, RM 校验的是这个)
> - (可选展示) `effective_notional` = `suggested_notional × fill_rate` (预期实际成交)

fill_rate **不计入 cap 判定** (cap 看意图量, 与 RM 一致, RM 校验的是 intent.size_pUSD_micro 即意图量)。

### 1.5 per-market 单注上限 + 总资金敞口上限

cap 链 (短路, 取最小约束, 记录 `capped_by`), **数值全部 = RM RiskConfig 影子, 待老韩联签**:

```
notional_kelly   = f_fractional × bankroll

# Cap 1: per-order 单注上限 (= RM per_order_cap_usdc, 默认 10,000)
notional_c1 = min(notional_kelly, PER_ORDER_CAP)         capped_by = PER_ORDER_CAP

# Cap 2: per-outcome (per-token) 敞口上限 (= RM per_outcome_cap_usdc, 默认 25,000)
#   剩余可用 = per_outcome_cap − 当前该 token 敞口
headroom_outcome = max(0, PER_OUTCOME_CAP − current_token_exposure)
notional_c2 = min(notional_c1, headroom_outcome)         capped_by = PER_OUTCOME_CAP

# Cap 3: per-market (per-condition) 敞口上限 (= RM market_exposure_cap_usdc, 默认 50,000)
headroom_condition = max(0, CONDITION_CAP − current_condition_exposure)
notional_c3 = min(notional_c2, headroom_condition)       capped_by = CONDITION_EXPOSURE

# Cap 4: 总资金敞口 / bankroll 充足性 (= RM bankroll_usdc, 默认 100,000)
#   单注不超过 bankroll × MAX_BANKROLL_FRACTION
notional_c4 = min(notional_c3, bankroll × MAX_BANKROLL_FRACTION)   capped_by = BANKROLL_FRACTION

suggested_notional = notional_c4
```

| Cap | 影子来源 (RM RiskConfig) | v1 建议默认值 | 待联签 |
|---|---|---|---|
| PER_ORDER_CAP | `per_order_cap_usdc` | 10,000 USDC | 老韩 |
| PER_OUTCOME_CAP | `per_outcome_cap_usdc` | 25,000 USDC | 老韩 |
| CONDITION_CAP | `market_exposure_cap_usdc` | 50,000 USDC | 老韩 |
| MAX_BANKROLL_FRACTION | (新, RM 无直接对应) | 0.10 (单注 ≤ 10% bankroll) | 老韩 **新增需联签** |
| λ_base (fractional) | (量化主权, 非 RM cap) | 0.25 | 小梁定, 知会老韩 |
| FILL_RATE_FLOOR | `FILL_RATE_FLOOR` (=0.50) | 0.50 (低于此 sizing 置 0 advisory) | 与小袁 lib 一致 |

> **MAX_BANKROLL_FRACTION = 0.10** 是 sizing 侧新增的总敞口护栏 (RM 当前无单注/bankroll 比例 cap, 只有绝对额 cap)。理由: 即便 per-order 10k < bankroll 100k 的 10%, 防止小 bankroll 场景下 10k 绝对额吃掉过大比例。**此条 RM 尚无对应, 需老韩决定是否同步进 RiskConfig** (否则 sizing 比 RM 更严, 不违反 §4 但需知会)。

---

## 2. SizingOutput 字段规范

建议落 `include/stcpp/sizing/sizing_output.hpp` (新 lib `stcpp::sizing`), 纯函数风格, 对齐既有数学库 (R-7 mode-agnostic / noexcept / 无堆分配):

```cpp
namespace stcpp::sizing {

enum class CappedBy : std::uint8_t {
    NONE = 0,               // Kelly 原值未触任何 cap
    NO_EDGE = 1,            // net_raw_edge <= 0, 全 0
    PER_ORDER_CAP = 2,      // 触 per-order 单注上限
    PER_OUTCOME_CAP = 3,    // 触 per-token 敞口上限
    CONDITION_EXPOSURE = 4, // 触 per-condition 敞口上限
    BANKROLL_FRACTION = 5,  // 触 bankroll 比例上限
    FILL_RATE_FLOOR = 6,    // fill_rate < FILL_RATE_FLOOR, advisory 置 0
};

// 输入快照 (audit / 看板可还原, R-20 时间戳由调用方携带)
struct SizingInput {
    double fair_value{0.0};            // p, ∈ (kProbEps, kProbMax)
    double price{0.0};                 // c, 入场价 ∈ (0,1)
    double edge_bps{0.0};              // 毛 edge (展示用, = (p-c)*10000)
    double bankroll_usdc{0.0};         // > 0
    double fill_rate{1.0};             // ∈ [0,1], 来自 FillRateModel
    double slippage_bps{0.0};          // 来自 fill_rate / slippage model
    double model_conf{1.0};            // ∈ [0,1], v1 不乘 (留接口)
    double current_token_exposure_usdc{0.0};
    double current_condition_exposure_usdc{0.0};
};

struct SizingOutput {
    double kelly_full{0.0};         // f*_full, 净 edge 全 Kelly
    double kelly_fractional{0.0};   // λ × f*_full (= /api/v1/quote kelly_fraction 真值)
    double suggested_notional{0.0}; // 过完 4 cap 的意图名义额 USDC (= quote suggested_notional 真值)
    double effective_notional{0.0}; // suggested_notional × fill_rate (预期实际成交, 展示用)
    CappedBy capped_by{CappedBy::NONE};
    double net_raw_edge{0.0};       // 扣 fee+slippage 后净 edge (debug/audit)
    bool valid{false};              // 输入含 NaN/Inf 或 fair_value/price 越界 → false, 全 0
    SizingInput snapshot{};         // 输入快照回填 (audit)
};

}  // namespace stcpp::sizing
```

字段语义:
- `kelly_full` / `kelly_fractional`: 比例 (∈ [0,1]), 与方向无关 (符号在 net_raw_edge 已定向, output 取绝对幅度)。
- `suggested_notional`: **意图名义额** (RM 校验对象), 已过 4 cap。
- `effective_notional`: 仅展示, = suggested × fill_rate。
- `capped_by`: 记录最终绑定约束 (取最小那条), 看板可显示 "被 X 限制"。
- `valid=false` 时全 0 (fail-closed, 不下单, 与 FairValueEstimator 同风格)。

---

## 3. 数据流 + 实现归属

### 3.1 接线 (fair_value → fill_rate → sizing → quote)

```
FeatureStoreGameRow/BookRow
        │
        ▼
FairValueEstimator.estimate()  ──► FairValueResult { probs[YES/NO], valid }   (小肖)
        │  p = probs[YES]
        ▼
FillRateModel.compute()        ──► FillRateOutput { fill_rate, slippage_bps }  (小袁)
        │  fill_rate, slippage_bps
        ▼
SizingCalculator.compute()     ──► SizingOutput { kelly_*, suggested_notional, capped_by }  (★新, 小袁实现)
        │
        ├──► /api/v1/quote: kelly_fraction = kelly_fractional
        │                   suggested_notional = suggested_notional
        │   (经只读 provider, R-12 观测侧零反向依赖; 替换 DemoStateProvider 假值)
        │
        └──► 策略层 OrderIntent.size_pUSD_micro = suggested_notional (意图量)
                     │
                     ▼
             RiskGateway.evaluate()  ──► 真值放行 (sizing 是建议, RM 是裁判)
```

### 3.2 谁实现

**提名小袁 (quant-microstructure) 供 C++ 实现 (新 lib `stcpp::sizing`)。**
理由: sizing 公式紧贴 `FillRateModel` (fill_rate / slippage_bps 折扣是小袁主权), 同 lib 协同最自洽; 数学口径本规范已锁死, 小袁做 C++ 实现 + 单测, 不做口径裁决 (口径在小梁)。

- **小梁 (本人):** 数学口径 + λ + cap 阈值 (本规范), 策略评审拍板。
- **小袁:** `stcpp::sizing` C++ 实现 + 闭合性单测 (§5)。
- **小肖:** `FairValueEstimator` 已有, 提供 p (无新工作)。
- **小卢 (观测):** 把真 SizingCalculator 经只读 provider 接到 `/api/v1/quote`, 替换 `DemoStateProvider::quote_params` 假值 (R-12 零反向依赖)。
- **老韩 (RM):** §4 cap 数值联签 + 确认 sizing cap = RM cap 影子。

> **本规范不含 C++ 实现代码** (小梁边界: 定数学不写代码)。

---

## 4. 风控边界 (硬约束, **待老韩 RM 主权联签**)

铁律 (CLAUDE.md §8 红线, ADR R-1 配套):

1. **sizing 展示/建议仓位 ≤ RM 实际放行口径, 永远。** sizing cap 是 `RiskConfig` cap 的 shadow, 数值同源, RM 是唯一真值。RM cap 变更时 sizing cap 必须同步 (建议: sizing 直接读同一份 `RiskConfig`, 不复制常量, 防漂移)。
2. **sizing 绝不绕过 RM。** sizing 输出只是 `OrderIntent.size_pUSD_micro` 的建议值, 必经 `RiskGateway::evaluate` 放行。看板展示 ≠ 放行。
3. **联签数值 (上表 §1.5):** PER_ORDER_CAP / PER_OUTCOME_CAP / CONDITION_CAP / MAX_BANKROLL_FRACTION / FILL_RATE_FLOOR — **全部待老韩 (RM 主权) 联签**。`MAX_BANKROLL_FRACTION` 是 sizing 新增护栏, RM 当前无对应字段, 需老韩决定是否同步进 `RiskConfig`。
4. **edge 净口径同源:** sizing 净 edge 公式 (§1.2) 与 RM `check_signal_` 的 `net_edge_after_fee = edge − 0.03×p×(1−p) − slippage` 必须一致 (同 `kSportsTakerFeeRate`), 否则 sizing 建仓 > 0 但 RM 拒 (EDGE_NEGATED_BY_SLIPPAGE), 体验割裂。
5. **CI vs 点估计分工:** RM gating 用 `edge_ci_lower` (CI 下界, 保守放行); sizing 用 `fair_value` 点估计算 f* (展示该下多大)。**潜在不一致:** 点估计 edge>0 但 CI 下界 ≤ 0 时, sizing 显示正仓位但 RM 拒。**建议联签项:** sizing 也读 edge_ci_lower, 若 CI 下界 ≤ floor 则 capped_by=NO_EDGE 置 0 (与 RM 同步保守)。**待老韩确认采用 CI 下界 gating 还是点估计 sizing + CI 告警。**

> **标注: 以上 §4 全部 "待老韩联签"。未联签前 `/api/v1/quote` 的 kelly/notional 真值接入 = advisory only, 不接策略层 OrderIntent。**

---

## 5. 验收标准

### 5.1 闭合性单测 (sizing 不超 cap)

`tests/unit/test_sizing_calculator.cpp` (小袁), 必覆盖:

- **C1 不超 per-order:** 任意输入, `suggested_notional ≤ PER_ORDER_CAP`。
- **C2 不超 per-outcome:** `suggested_notional + current_token_exposure ≤ PER_OUTCOME_CAP`。
- **C3 不超 per-condition:** `suggested_notional + current_condition_exposure ≤ CONDITION_CAP`。
- **C4 不超 bankroll 比例:** `suggested_notional ≤ bankroll × MAX_BANKROLL_FRACTION`。
- **capped_by 正确性:** 构造每条 cap 恰好绑定的输入, 断言 `capped_by` 命中对应枚举。
- **边界自洽:** `p = c` → kelly_full = 0, capped_by = NO_EDGE; `net_raw_edge ≤ 0` → 全 0。
- **方向对称:** `p > c` (buy YES) 与 `p < c` (buy NO) 镜像输入 → 对称 f* 幅度。
- **数值健壮:** NaN/Inf 输入 / `p ∈ {0,1}` 边界 / `c ∈ {0,1}` → valid=false, 全 0 (fail-closed)。
- **λ 折扣:** `kelly_fractional = λ × kelly_full` (±1e-12)。
- **fill_rate 折扣:** `effective_notional = suggested_notional × fill_rate`; `fill_rate < FILL_RATE_FLOOR` → capped_by=FILL_RATE_FLOOR, advisory 置 0。

### 5.2 与 RM 口径一致性 (property test)

`tests/integration/test_sizing_rm_consistency.cpp`:

- **不被 RM 拒 (sizing 维度):** 对 N 组随机输入, 用 sizing 的 `suggested_notional` 构造 `OrderIntent.size_pUSD_micro`, 喂 `RiskGateway::evaluate` (同 RiskConfig), 断言 **不出现** `EXCEED_PER_ORDER_CAP / EXCEED_PER_OUTCOME_CAP / EXCEED_CONDITION_EXPOSURE / INSUFFICIENT_BANKROLL`。(允许 stale/state/market 等非 sizing 维度 reject。)
- **net edge 同源:** sizing 的 `net_raw_edge` 与 RM `net_edge_after_fee` 在同 (p, c, slippage) 下数值一致 (±1e-9), 防 fee 系数漂移。
- **cap 同源 grep 守护 (老高 CI):** sizing cap 常量若复制而非引用 `RiskConfig` → CI 失败 (防数值漂移)。建议直接注入 `RiskConfig const&`。

验收门槛: 5.1 全绿 + 5.2 零 sizing 维度 reject + net edge 一致性 ±1e-9。

---

## 6. 待办 / 升 ADR 理由

- **建议升 ADR** (待老郭 + 老韩 + GM 签): 本规范定义跨单元数学契约 (量化口径 ↔ RM cap ↔ 观测 quote 真值 ↔ 策略层 sizing), 涉及 RM 主权 cap 数值, 符合 "跨单元 / 风控红线" 升 ADR 标准 (CLAUDE.md §6)。
- **联签阻塞:** §4 全部 cap 数值 + CI-vs-点估计分工 待老韩。
- **后续 (W6 校准后):** λ_eff × model_conf 开关 (§1.3 v2); MAX_BANKROLL_FRACTION 是否进 RiskConfig (§1.5)。
</content>
</invoke>
