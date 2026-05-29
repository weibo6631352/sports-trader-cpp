# ADR-042 — Kelly Sizing 跨单元数学契约 + 风控 cap

> **owner:** 老郭 (架构评审 + 顾问团协调) — 架构 co-sign
> **last_review:** 2026-05-29
> **status:** Accepted
> **三方签:** 小梁 (量化研究部主管, Sharpe/Kelly 主权) + 老韩 (风控合规部主管, RM 主权) + 老郭 (架构 co-sign) / GM 老雷 合并
> **依据:**
> - 小梁 sizing 规范: `docs/RESEARCH/xiaoliang-kelly-sizing-spec-v1.md` (SizingOutput + cap 链 + λ=0.25)
> - 老韩 RM 联签: `docs/RESEARCH/laohan-kelly-cap-cosign-v1.md` (APPROVED + 4 修正条款)
> **SSOT 校验源 (行号已核对, 2026-05-29):**
> - `include/stcpp/risk/risk_gateway.hpp` RiskConfig v0.5 (L298-301 cap, L311 edge_ci_lower_floor, L346/L399 bankroll atomic)
> - `src/stcpp/risk/risk_gateway.cpp` check_signal_ (L126 kSportsTakerFeeRate, L560-587 CI/fee/slippage 三门)
> - `include/stcpp/numerical/slippage_model.hpp` (L72 FILL_RATE_FLOOR=0.50)
> **关联:** ADR-040 (market-structure per-token cap), `docs/ADR/2026-05-29-observability-debug-api.md` (ADR-038 观测 API quote 真值消费方)

---

## 0. 背景与定位

设计评审认定: 看板量化真值最缺的一块是把 **edge 翻译成 "该下多大"**。现状 `/api/v1/quote/{condition_id}` 的 `kelly_fraction` / `suggested_notional` 由 `DemoStateProvider` 写死假值 (0.042 / 850.0), 无任何数学口径支撑。

本 ADR 确立 **唯一的 sizing 真值口径**, 同时是:
1. `/api/v1/quote` 的 `kelly_fraction` / `suggested_notional` 真值来源 (替换 Demo, ADR-038 消费);
2. 策略层 `OrderIntent.size_pUSD_micro` 的建议值 (注意: **建议 ≠ 放行**, 放行权永远在 `RiskGateway::evaluate`)。

**升 ADR 理由 (CLAUDE.md §6):** 本契约跨 量化研究部 (口径) ↔ 风控合规部 (cap 主权 + 红线) ↔ 系统工程部 (观测/策略接线), 且涉及 RM cap 数值与下单链路风控红线 — 符合"跨单元 + 风控红线"升 ADR 标准。小梁规范与老韩 RM 联签均建议升 ADR。

**关键裁定基调:** 小梁草案与老韩 RM 联签在 3 处口径有出入, 本 ADR **一律以老韩 RM 主权裁定为准 (更保守者胜)** — 见 §3。

---

## 1. 决策 D1 — SizingCalculator 数学口径

### 1.1 二元 Kelly 公式

Polymarket 体育是二元 share 市场 (买 1 share, win 得 1, lose 得 0)。设 `p = fair_value` (de-vig + microprice blend, clamp ∈ (kProbEps, kProbMax)), `c = price` (入场价 ∈ (0,1))。

经典 Kelly 化简为 binary share 形式 (方向由 edge 符号决定):

```
if net_raw_edge > 0:  f*_full = net_raw_edge / (1 - c)     # buy YES (看涨)
elif < 0 (镜像):       f*_full = (net_raw_edge) / c          # buy NO  (做空, 用对侧)
else:                  f*_full = 0
```

分母 `1-c` / `c` 永不为 0 (c clamp ∈ (kProbEps, kProbMax))。

### 1.2 净 edge 口径 — **用 `edge_ci_lower` CI 下界, 非点估计** (老韩 §2 裁定)

**这是本 ADR 对小梁草案的核心修正。** 小梁草案 §1.2 原方案用 `fair_value` 点估计算 f* + CI 告警 — **已被老韩 RM 主权否决**。

裁定: sizing 的 **gating 门 = RM gating 门**, 同读 `edge_ci_lower`, 同 floor, 同 fee 公式。f* 分子直接用 CI 下界净 edge:

```cpp
// gating 门 (算 f* 之前, 与 RM check_signal_ 同源):
//   门 A (slippage, RM cpp L570-572): edge_ci_lower*10000(bps) < slippage_bps → NO_EDGE
//   门 B (fee,      RM cpp L584-587): edge_ci_lower − 0.03·p·(1−p) ≤ edge_ci_lower_floor → NO_EDGE
//   门 0 (CI,       RM cpp L564):     edge_ci_lower ≤ edge_ci_lower_floor → NO_EDGE
// 任一门触发 → SizingOutput 全 0, capped_by = NO_EDGE, return

fee_per_unit = kSportsTakerFeeRate * p * (1 - p);   // 0.03, RM 同公式 (cpp L584)
net_ci_edge  = edge_ci_lower - fee_per_unit;        // CI 下界净 edge (= RM net_edge_after_fee)
net_raw_edge = net_ci_edge;                         // f* 分子直接用 CI 下界净 edge (老韩 §2.2)
f*_full      = net_raw_edge / (1 - c);              // buy YES; NO 方向 / c
```

**理由 (老韩 §2.1/§2.2):** (a) 与 RM 放行口径完全同源, 一致性单测零反例; (b) CI 下界 sizing 是保守做市业界标准, 估计误差吸收已在 CI 体现; (c) 避免"点估计 f* > CI gating 允许"的灰区; (d) 纪律 > 收益。

### 1.3 fee / slippage 双独立门 (老韩 §2.3 校正)

小梁草案 §1.2 合并式 `net_edge = edge − fee − slippage_unit` **已校正**: RM 实际是 **两道独立门** (slippage 门 A / fee 门 B), 不揉进同一式相减。slippage_bps 仅用于门 A 判定与 fill_rate 折扣展示, **不进 fee 分子**。sizing 同源采用两道独立门 (见 §1.2 代码)。

> 实现注: RM 的 fee 门 (cpp L587) reject code 复用 `EDGE_NEGATED_BY_SLIPPAGE` (非独立 `EDGE_CI_NEGATIVE`); 一致性单测断言时需对齐此实际 code (见 §5)。

### 1.4 Fractional Kelly λ = 0.25 (小梁主权, 老韩知会)

**默认 λ_base = 0.25 (quarter Kelly)。** 理由: 参数误差吸收 (Kelly 对 edge 过估呈二次惩罚) + 回撤约束自洽 (full Kelly 理论 MDD ~40%+, λ=0.25 拉到 ~10-12%, 与北极星 MDD≤15% / RM daily_loss_hard_pct=0.05 一致)。

```
λ_eff = λ_base = 0.25                  # v1 不乘 model_conf (留接口, follow-up §7)
f_fractional = λ_eff × f*_full
```

λ 属小梁量化主权 (非 RM cap)。RM 立场: λ 越小越保守, 0.25 无异议接受;唯一硬约束 — 无论 λ 取值, 最终 `suggested_notional` 必过 §2 全部 cap。

### 1.5 fill_rate 折扣 — 只算 effective_notional, 不进 cap

```
f_effective        = f_fractional × fill_rate          # ∈ [0,1], 来自 FillRateModel (小袁)
effective_notional = suggested_notional × fill_rate    # 仅展示 "预期实际成交"
```

fill_rate **不计入 cap 判定** — cap 看意图量 (= `f_fractional × bankroll` 过 cap), 与 RM 一致 (RM 校验的是 `intent.size_pUSD_micro` 意图量)。仅 `fill_rate < FILL_RATE_FLOOR (0.50)` 时 advisory 置 0, `capped_by = FILL_RATE_FLOOR`。

---

## 2. 决策 D2 — cap 链 5 级数值 + capped_by

cap 链短路, 取最小约束, 记录 `capped_by`。**数值全部 = RM RiskConfig 影子, 注入引用非复制 (§3)。**

```
notional_kelly = f_fractional × bankroll                          # bankroll 用 SizingInput 运行期值 (老韩 §1.3)

Cap 1 (per-order):     min(., per_order_cap_usdc)                          → PER_ORDER_CAP
Cap 2 (per-outcome):   min(., max(0, per_outcome_cap_usdc − tok_exp))      → PER_OUTCOME_CAP
Cap 3 (per-condition): min(., max(0, market_exposure_cap_usdc − cond_exp)) → CONDITION_EXPOSURE
Cap 4 (bankroll frac): min(., bankroll × MAX_BANKROLL_FRACTION)            → BANKROLL_FRACTION
suggested_notional = Cap4 结果
```

| Cap | RM RiskConfig 字段 (SSOT 行) | 数值 | capped_by 枚举 | 裁定 |
|---|---|---|---|
| PER_ORDER_CAP | `per_order_cap_usdc` (hpp L298) | 10,000 USDC | `PER_ORDER_CAP=2` | 同源, 通过 |
| PER_OUTCOME_CAP | `per_outcome_cap_usdc` (hpp L300) | 25,000 USDC | `PER_OUTCOME_CAP=3` | 同源 (ADR-040 per-token), 通过 |
| CONDITION_CAP | `market_exposure_cap_usdc` (hpp L299) | 50,000 USDC | `CONDITION_EXPOSURE=4` | 同源 (per-condition), 通过 |
| **MAX_BANKROLL_FRACTION** | (RM **无**对应字段) | **0.10** | `BANKROLL_FRACTION=5` | sizing 独有护栏, **本期不进 RiskConfig** |
| FILL_RATE_FLOOR | `numerical::FILL_RATE_FLOOR` (slippage_model.hpp L72) | 0.50 | `FILL_RATE_FLOOR=6` | 同源 (来自 numerical, 非 RiskConfig) |
| bankroll (cap4 基数) | `bankroll_usdc` (hpp L301, atomic L399) | 运行期 `set_bankroll()` | — | **必用运行期值, 禁硬编码 100k** |

其余枚举: `NONE=0` (未触 cap), `NO_EDGE=1` (gating 门触发, §1.2)。

### 2.1 MAX_BANKROLL_FRACTION = 0.10 — sizing 护栏, RM 无字段

RM 当前无 bankroll 比例 cap (只有绝对额 cap + `INSUFFICIENT_BANKROLL` 充足性检查)。0.10 作为 **sizing 侧更严护栏** 采纳 — 使 sizing 比 RM 更保守 (sizing ≤ RM), **不违反铁律 §4**, RM 不依赖它。

**为何本期不进 RiskConfig (老韩 §1.4):** 加 RM 字段属 ABI/config 变更, 需老沈 field-freeze + 老郭 ABI ack; MVP 阶段绝对额 cap 已足够。进 RM 排到 W6 校准后随 `λ_eff × model_conf` 一并评审 (follow-up §7)。`MAX_BANKROLL_FRACTION` 可在 `stcpp::sizing` 内定 `constexpr` (sizing 独有), 须注释标明 "sizing-only 护栏, RM 无对应"。

### 2.2 bankroll 必用运行期值 (老韩 §1.3, 防 drawdown 漂移)

RM `bankroll_usdc_` 是 atomic, 由 `set_bankroll()` 动态注入 (cpp L468/L690 运行读)。cap4 基数 **必须用 `SizingInput.bankroll_usdc`** (调用方从 PositionLedger / RM 同源快照传入), 不得硬编码 100,000。否则 drawdown 后 bankroll 缩水而 sizing 仍按 100k 算 → 超 RM 放行口径, 违铁律。

---

## 3. 决策 D3 — 风控边界铁律 (RM 主权, 一票否决)

1. **sizing 建议 ≤ RM 实际放行口径, 永远。** sizing 全 5 级 cap 数值 = RM RiskConfig 影子; gating 门 = RM gating 门。sizing 算出的 `suggested_notional` 喂 `RiskGateway::evaluate` 必不因 sizing/signal 维度被拒。
2. **sizing 绝不绕 RM。** sizing 输出只是 `OrderIntent.size_pUSD_micro` 的**建议值**, 必经 `RiskGateway::evaluate` 放行 (CLAUDE.md §8 红线 R-1: 任何下单链路绕过 RiskManager → 立即回滚 + post-mortem)。看板 quote 展示 ≠ 放行。
3. **fail-closed。** `valid=false` (NaN/Inf / fair_value/price 越界) → 全 0, 不下单 (与 FairValueEstimator 同风格)。
4. **更严允许, 更松禁止。** sizing 可比 RM 更保守 (MAX_BANKROLL_FRACTION / CI 下界 f*); 绝不可比 RM 更激进。
5. **cap 注入 `RiskConfig const&`, 禁复制字面量 (老高 CI grep 守护)。**
   - `SizingCalculator::compute()` 签名必须接 `stcpp::risk::RiskConfig const&` (或只读视图), 从中读 `per_order_cap_usdc / per_outcome_cap_usdc / market_exposure_cap_usdc / edge_ci_lower_floor`。
   - **禁止** 在 `stcpp::sizing` 内 `constexpr`/`#define`/字面量复制 `10000/25000/50000/0.0` 等 cap 值。
   - `FILL_RATE_FLOOR` 引用 `stcpp::numerical::FILL_RATE_FLOOR`, 不复制。
   - `kSportsTakerFeeRate (0.03)` 引用 RM 同一常量 (cpp L126); 若需跨 TU 共享, 老周/老沈 提公共 header, RM ack。sizing **不得自定义 fee 率**。
   - **理由:** cap 值复制 = 漂移源。RM cap 一改, 复制方静默失配 → sizing 可能 > RM 放行口径 → 违铁律。注入引用 = 编译期同源, 零漂移。
6. **任何 sizing → quote → OrderIntent 链路变更必须知会老韩 + 留 audit** (CLAUDE.md §7-6 可追溯)。

---

## 4. SizingOutput / SizingInput 字段契约

落 `include/stcpp/sizing/sizing_output.hpp` (新 lib `stcpp::sizing`), 纯函数风格 (R-7 mode-agnostic / noexcept / 无堆分配)。

```cpp
namespace stcpp::sizing {

enum class CappedBy : std::uint8_t {
    NONE = 0, NO_EDGE = 1, PER_ORDER_CAP = 2, PER_OUTCOME_CAP = 3,
    CONDITION_EXPOSURE = 4, BANKROLL_FRACTION = 5, FILL_RATE_FLOOR = 6,
};

struct SizingInput {
    double fair_value{0.0};                    // p, ∈ (kProbEps, kProbMax)
    double price{0.0};                         // c, ∈ (0,1)
    double edge_ci_lower{0.0};                 // CI 下界 (gating + f* 分子, 老韩 §2)
    double edge_bps{0.0};                      // 毛 edge (展示用)
    double bankroll_usdc{0.0};                 // 运行期值, > 0 (老韩 §1.3)
    double fill_rate{1.0};                     // ∈ [0,1], FillRateModel
    double slippage_bps{0.0};                  // 门 A + 折扣展示
    double model_conf{1.0};                    // ∈ [0,1], v1 不乘 (留接口)
    double current_token_exposure_usdc{0.0};
    double current_condition_exposure_usdc{0.0};
};

struct SizingOutput {
    double kelly_full{0.0};         // f*_full (CI 下界净 edge 全 Kelly)
    double kelly_fractional{0.0};   // λ × f*_full (= quote kelly_fraction 真值)
    double suggested_notional{0.0}; // 过 4 cap 的意图名义额 (= quote suggested_notional 真值, RM 校验对象)
    double effective_notional{0.0}; // suggested × fill_rate (展示)
    CappedBy capped_by{CappedBy::NONE};
    double net_raw_edge{0.0};       // = net_ci_edge (debug/audit)
    bool valid{false};              // fail-closed
    SizingInput snapshot{};         // audit 回填
};

}  // namespace stcpp::sizing
```

> 相对小梁草案新增 `edge_ci_lower` 字段 (老韩 §2 裁定的 gating + 分子源)。

---

## 5. 验收门 (RM 联签解锁条件, 小袁实现照此)

### 5.1 闭合性单测 `tests/unit/test_sizing_calculator.cpp`
- C1 `suggested_notional ≤ RiskConfig.per_order_cap_usdc` (引用值非 10000 字面量)
- C2 `suggested_notional + current_token_exposure ≤ per_outcome_cap_usdc`
- C3 `suggested_notional + current_condition_exposure ≤ market_exposure_cap_usdc`
- C4 `suggested_notional ≤ SizingInput.bankroll_usdc × MAX_BANKROLL_FRACTION` (基数是 input bankroll, §2.2)
- C5 (CI gating) `edge_ci_lower ≤ edge_ci_lower_floor` → `capped_by=NO_EDGE`, 全 0
- C6 (fee 门) `edge_ci_lower − 0.03·p·(1−p) ≤ floor` → `capped_by=NO_EDGE`, 全 0
- C7 (slippage 门) `edge_ci_lower(bps) < slippage_bps` → `capped_by=NO_EDGE`
- capped_by 命中: 每条 cap 构造恰好绑定输入, 断言枚举正确
- λ 折扣: `kelly_fractional = 0.25 × kelly_full` (±1e-12)
- fill_rate: `effective_notional = suggested × fill_rate`; `< FILL_RATE_FLOOR` → `capped_by=FILL_RATE_FLOOR` 置 0
- 数值健壮: NaN/Inf, p∈{0,1}, c∈{0,1} → `valid=false` 全 0
- 方向对称: p>c (buy YES) 与镜像 p<c (buy NO) → 对称 f* 幅度

### 5.2 与 RM 一致性 `tests/integration/test_sizing_rm_consistency.cpp`
- **零 sizing 维度 reject (核心门):** N≥10,000 组随机输入, 用 sizing `suggested_notional` 构 `OrderIntent.size_pUSD_micro`, 喂同一 `RiskConfig` 的 `RiskGateway::evaluate`, 断言**绝不**出现 `EXCEED_PER_ORDER_CAP / EXCEED_PER_OUTCOME_CAP / EXCEED_CONDITION_EXPOSURE / INSUFFICIENT_BANKROLL / EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE`。(允许 state/stale/market/duplicate 等非 sizing 维度 reject。)
- **net edge 同源 (±1e-9):** sizing `net_raw_edge` 与 RM `net_edge_after_fee` 在同 `(edge_ci_lower, p)` 下一致 (同 `kSportsTakerFeeRate=0.03`)。
- **CI gating 同源:** sizing 置 0 (NO_EDGE) 输入集 ⊇ RM 因 CI/fee/slippage 门拒的输入集 (sizing 不更松)。
- **cap 同源 grep 守护 (老高 CI):** `stcpp::sizing` TU 内出现 `10000|25000|50000` cap 上下文字面量或自定义 fee 率 → CI 失败。
- **bankroll 动态:** `set_bankroll()` 缩水 (如 80k) 后 cap4 用 80k 重算, 不超 RM (§2.2)。

**验收门槛:** 5.1 全绿 + 5.2 零 sizing 维度 reject + net edge ±1e-9 + grep 守护绿。任一红 → 不解锁 quote 真值, 退回小袁。

---

## 6. 实施归属

- **小梁:** 数学口径 + λ + cap 阈值 (本 ADR §1/§2), 策略评审拍板。已 ack 老韩 §2.2/§2.3 校正。
- **老韩:** §3 cap 数值联签 + RM 主权裁定 (APPROVED + 4 修正条款)。
- **老郭 (本签):** 架构 co-sign — 确认契约边界自洽 (sizing ≤ RM 单向、注入非复制零漂移、fail-closed、跨单元接口清晰)。
- **小袁 (quant-microstructure):** `stcpp::sizing::SizingCalculator` C++ 实现 + §5 单测 (feat 分支在跑)。口径已锁, 不做裁决。
- **小卢 (观测):** 把真 SizingCalculator 经只读 provider 接到 `/api/v1/quote`, 替换 `DemoStateProvider::quote_params` 假值 (R-12 零反向依赖, ADR-038)。接真值前 = advisory only。
- **小肖:** `FairValueEstimator` 提供 p (无新工作)。
- **老高:** §5.2 grep 守护 CI rule。

**解锁顺序:** 小袁 §5 全绿 → 老韩验收 → 小卢接 quote 真值 (此前 advisory only, 不接策略层 OrderIntent)。

---

## 7. Follow-up (W6 校准后随策略评审)

1. **MAX_BANKROLL_FRACTION 是否进 RiskConfig** (§2.1): 加 RM 字段需老沈 field-freeze + 老郭 ABI ack。本期 sizing 单边持有。
2. **λ_eff × model_conf 开关** (§1.4 v2): 置信度联动缩仓, 需 W6 实盘校准数据 + 老韩联签。
3. **kSportsTakerFeeRate 跨 TU 共享 header**: sizing 若引用, 老周/老沈 提公共常量头, RM ack (当前在 risk_gateway.cpp L126 文件内)。
4. **小梁 §1.2 spec 修订归档**: 合并式 → 两道独立门 + 点估计 → CI 下界, 同步回小梁 spec v2。

---

## 8. 签字

| 角色 | 人 | 决定 | 日期 |
|---|---|---|---|
| Sharpe/Kelly 主权 | 小梁 | 草案 owner + ack 老韩 §2 校正 | 2026-05-29 |
| RM 主权 | 老韩 | APPROVED + 4 修正条款 | 2026-05-29 |
| 架构 co-sign | 老郭 | **Accepted** (契约边界自洽, 单向 ≤ RM, 零漂移) | 2026-05-29 |
| GM | 老雷 | 合并 | 2026-05-29 |
| 实现 | 小袁 | 照 §5 验收 (进行中) | — |
