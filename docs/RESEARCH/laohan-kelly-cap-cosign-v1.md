# Kelly Sizing Cap 联签决定 v1 — RM 主权裁定

> **owner:** 老韩 (风控合规部主管, RiskManager 主权)
> **last_review:** 2026-05-29
> **status:** RM 联签 APPROVED (有 4 处修正条款, 见 §1 / §2 / §3)
> **联签对象:** `docs/RESEARCH/xiaoliang-kelly-sizing-spec-v1.md` (小梁, Kelly sizing 草案 v1)
> **解锁:** C 单元 小袁 实现 `stcpp::sizing::SizingCalculator` 的前置门 (看板 quote kelly/edge 真值)
> **SSOT 校验源:** `include/stcpp/risk/risk_gateway.hpp` RiskConfig v0.5 / `src/stcpp/risk/risk_gateway.cpp` check_signal_ / `include/stcpp/numerical/slippage_model.hpp`

---

## 0. 联签结论 (一句话)

小梁 §1.5 cap 表 **数值全部 RM 联签通过**, 但附 **4 处强制修正条款**:
1. cap 不得复制常量, 必须注入 `RiskConfig const&` (§3, 防漂移红线);
2. sizing **必须读 `edge_ci_lower` 做 gating**, 与 RM 同步保守 — 否决"点估计 sizing + CI 告警"方案 (§2);
3. RM `check_signal_` 净 edge 公式与小梁 §1.2 有出入, 以 RM `.cpp` 为唯一 SSOT, 小梁 §1.2 需校正 (§2.3);
4. `MAX_BANKROLL_FRACTION` 是 sizing 侧新护栏, **本期不进 RiskConfig**, 仅作 sizing 更严的护栏 (不违反铁律, §1.4)。

未签前 `/api/v1/quote` 的 kelly/notional 接入 = advisory only。本联签生效后 sizing 可作 quote 真值, 但 **永不接策略层 OrderIntent 绕 RM** (§4)。

---

## 1. Cap 数值拍板 (5 级 + λ)

数值 **与 RM `RiskConfig` 影子同源**, 逐条对账 SSOT 文件行号。

| Cap | RM RiskConfig 字段 (SSOT) | RM 默认值 (.hpp 行) | 联签拍板 | RM 裁定 |
|---|---|---|---|---|
| **PER_ORDER_CAP** | `per_order_cap_usdc` | 10,000 (L298) | **10,000 USDC** | 通过, 同源 |
| **PER_OUTCOME_CAP** | `per_outcome_cap_usdc` | 25,000 (L300) | **25,000 USDC** | 通过, 同源 (R6.2b per-token) |
| **CONDITION_CAP** | `market_exposure_cap_usdc` | 50,000 (L299) | **50,000 USDC** | 通过, 同源 (R6.2a per-condition) |
| **MAX_BANKROLL_FRACTION** | (RM 无对应字段) | — | **0.10 (单注 ≤ 10% bankroll)** | 通过, 但 **本期不进 RiskConfig** (§1.4) |
| **FILL_RATE_FLOOR** | `numerical::FILL_RATE_FLOOR` | 0.50 (slippage_model.hpp L72) | **0.50** | 通过, 同源 (注: 来自 slippage_model, 非 RiskConfig) |
| **bankroll** (cap4 基数) | `bankroll_usdc` | 100,000 (L302) | **100,000 USDC** | 通过, 注意运行期由 `set_bankroll()` 动态覆盖 (§1.3) |
| λ_base (fractional) | (量化主权, 非 RM cap) | — | **0.25 (quarter Kelly)** | 知会, 不属 RM 主权; RM 接受作 sizing 默认 (§1.5) |

### 1.1 PER_ORDER_CAP = 10,000 USDC
RM `check_signal_`/`check_position_caps_` 在 `it.size_pUSD_micro > cfg_.per_order_cap_usdc` 时返 `EXCEED_PER_ORDER_CAP` (risk_gateway.cpp L428)。sizing 的 cap1 必须 ≤ 此值。**通过。**

### 1.2 PER_OUTCOME_CAP=25,000 / CONDITION_CAP=50,000
RM 校验为 **累加敞口**: `cur + it.size_pUSD_micro > cap`（L448 condition / L461 outcome）。小梁 §1.5 的 headroom 公式 `max(0, CAP − current_exposure)` 与 RM 累加判定 **逻辑一致**。**通过。** 验收口径见 §5 C2/C3。

### 1.3 bankroll 不是常量 — 必须读运行期值
RM `bankroll_usdc_` 是 `atomic`, 由 `set_bankroll()` 动态注入 (risk_gateway.cpp L152 ctor 初值 + L468/L690 运行读)。
**裁定:** sizing 的 cap4 基数 **必须用 `SizingInput.bankroll_usdc` (调用方从 PositionLedger / RM 同源快照传入)**, 不得硬编码 100,000。`RiskConfig.bankroll_usdc` 仅是初始默认, 实盘以 RM 当前 bankroll 为准。否则 drawdown 后 bankroll 缩水, sizing 仍按 100k 算 → 超 RM 放行口径, 违铁律。

### 1.4 MAX_BANKROLL_FRACTION = 0.10 — 本期不进 RiskConfig
RM 当前 **无 bankroll 比例 cap** (只有绝对额 cap + `INSUFFICIENT_BANKROLL` 充足性检查)。
**裁定:** 0.10 作为 **sizing 侧更严护栏** 采纳。这使 sizing 比 RM 更保守 (sizing ≤ RM), **不违反 §4 铁律**, 无需 RM 同步即可放行。
**为何本期不进 RiskConfig:** 加 RM 字段属 ABI/config 变更, 需走老沈 field-freeze + 老郭架构 ack; MVP 阶段绝对额 cap 已足够, 比例 cap 进 RM 排到 W6 校准后随 `λ_eff × model_conf` 一并评审。**记 follow-up (§6)。** 在此之前 sizing 单边持有此护栏, RM 不依赖它。

### 1.5 λ_base = 0.25 — RM 知会, 不主权
λ 是 sizing 增长率/方差权衡, 属小梁 (Sharpe/Kelly 主权), **不是 RM cap**。RM 立场: λ 越小越保守, 0.25 与北极星 MDD≤15% 自洽, RM 无异议接受作默认。RM 唯一硬约束 — 无论 λ 取值, sizing 最终 `suggested_notional` 必须过 §1 全部 5 级 cap (§4 铁律)。

---

## 2. CI 裁定 (小梁 §4.5 矛盾的最终裁决)

### 2.1 裁定: sizing **必须读 `edge_ci_lower` 做 gating**, 与 RM 同步保守

**否决** 小梁 §1.2/§4.5 的"sizing 用 `fair_value` 点估计算 f* + CI 告警"方案。

**RM 事实 (SSOT, risk_gateway.cpp L558-592):** RM `check_signal_` gating 输入是 **`edge_ci_lower` (CI 下界)**, 不是点估计:
- `edge_ci_lower <= edge_ci_lower_floor (=0.0)` → `EDGE_CI_NEGATIVE` reject (L564);
- 净 edge 校验也用 `edge_ci_lower`: `net_edge_after_fee = edge_ci_lower − 0.03·p·(1−p) <= floor` → reject (L585)。

**矛盾根因 (小梁问的):** 点估计 edge>0 但 CI 下界 ≤ 0 时, 若 sizing 用点估计 → 显示正仓位, 但 RM 必拒 `EDGE_CI_NEGATIVE`。这 **直接违反 §4 铁律** (sizing 建议被 RM 拒, 体验割裂且 sizing > RM 放行口径)。

**裁定细则 (小袁实现照此):**

```
# sizing gating 入口 (在算 f* 之前):
edge_ci_lower = SizingInput.edge_ci_lower      # 新增字段, 调用方从 FairValueEstimator/CI 传入
if edge_ci_lower <= EDGE_CI_LOWER_FLOOR:        # = RiskConfig.edge_ci_lower_floor, 默认 0.0
    SizingOutput 全 0, capped_by = NO_EDGE       # 与 RM EDGE_CI_NEGATIVE 同步置 0
    return

# 净 edge 也用 CI 下界 (与 RM L585 同源), 不用点估计:
fee_per_unit  = kSportsTakerFeeRate * p * (1 - p)     # kSportsTakerFeeRate = 0.03 (硬编码)
net_ci_edge   = edge_ci_lower - fee_per_unit
if net_ci_edge <= EDGE_CI_LOWER_FLOOR:
    SizingOutput 全 0, capped_by = NO_EDGE
    return
```

**即: sizing 的 gating 门 = RM 的 gating 门 (同读 `edge_ci_lower`, 同 floor, 同 fee 公式)。** 保证"sizing 显正仓位 ⟹ RM 必不因 signal 维度拒"。

### 2.2 f* 分子用什么 — 点估计 OR CI 下界?

裁定: **gating 用 `edge_ci_lower` (上 §2.1 硬门); 一旦过门, f* 的分子幅度可用点估计 edge 算 "该下多大"**, 但 **必须再保守裁剪**: f* 分子 `net_raw_edge` 不得超过 `net_ci_edge` 对应的 Kelly。

最简洁且无歧义的实现 (RM 推荐, 小袁照此): **f* 分子直接用 `net_ci_edge` (CI 下界净 edge), 不用点估计。**
理由: (a) 与 RM 放行口径完全同源, 闭合性单测 §5.2 零反例; (b) CI 下界 sizing 是保守做市的业界标准 (估计误差吸收已在 CI 体现, 再叠点估计等于双标); (c) 避免"点估计 f* > CI gating 允许"的灰区。

```
net_raw_edge = net_ci_edge                       # = edge_ci_lower - fee_per_unit (CI 下界净 edge)
f*_full      = net_raw_edge / (1 - c)            # buy YES; NO 方向 / c
```

> 注: 这比小梁 §1.2 原方案 (点估计 sizing) **更保守**。RM 立场: 纪律 > 收益, 采纳更保守者。小梁若坚持点估计幅度可在策略评审提, 但 gating 门 (§2.1) 不可让步。

### 2.3 净 edge 公式校正 — 以 RM .cpp 为 SSOT

小梁 §1.2 写 `net_edge = edge − 0.03×p×(1−p) − slippage_unit` (含 slippage 项)。
**RM 实际 (SSOT)**: slippage 与 fee 是 **两道独立门**, 不在同一式相减:
- 门 A (L571): `edge_bps < slippage_bps` → `EDGE_NEGATED_BY_SLIPPAGE`;
- 门 B (L585): `edge_ci_lower − 0.03·p·(1−p) <= floor` → reject (fee 单独, **不减 slippage**)。

**裁定:** sizing 同源采用 RM 的 **两道独立门**, 不把 slippage 揉进 fee 式:
```
# 门 A (slippage): edge_ci_lower(bps) < slippage_bps → NO_EDGE
# 门 B (fee):      edge_ci_lower - 0.03·p·(1-p) <= floor → NO_EDGE
```
小梁 §1.2 的合并式需校正 (记小梁 spec 修订项)。slippage_bps 仍来自小袁 FillRateModel, 用于门 A 与 fill_rate 折扣展示, 不进 fee 分子。

---

## 3. 注入方式 — `RiskConfig const&`, 禁复制常量 (红线)

**裁定 (强制, 老高 CI grep 守护):**
- `SizingCalculator::compute()` 签名必须接 **`stcpp::risk::RiskConfig const&`** (或其只读视图), 从中读 `per_order_cap_usdc / per_outcome_cap_usdc / market_exposure_cap_usdc / edge_ci_lower_floor`。
- **禁止** 在 `stcpp::sizing` 内 `constexpr` / `#define` / 字面量复制 10000/25000/50000/0.0 等 cap 值。
- `FILL_RATE_FLOOR` 引用 `stcpp::numerical::FILL_RATE_FLOOR` (slippage_model.hpp L72), 不复制。
- `kSportsTakerFeeRate (0.03)` 引用 RM 同一常量 (risk_gateway.cpp L126; 若需跨 TU 共享, 老周/老沈 提到公共 header, RM ack)。**sizing 不得自定义 fee 率。**
- `MAX_BANKROLL_FRACTION (0.10)` 是 sizing 独有 (RM 无字段), 可在 `stcpp::sizing` 内定 `constexpr`, 但需注释 "sizing-only 护栏, RM 无对应, follow-up §6 评审进 RiskConfig"。

**理由:** cap 值复制 = 漂移源。RM cap 一改 (老沈 field-freeze), 复制方静默失配 → sizing 可能 > RM 放行口径 → 违 §4 铁律。注入引用 = 编译期同源, 零漂移。

---

## 4. 铁律 (RM 主权, 一票否决)

1. **sizing 建议 ≤ RM 实际放行口径, 永远。** sizing 全 5 级 cap 数值 = RM RiskConfig 影子 (注入非复制); CI gating 门 = RM gating 门。sizing 算出的 `suggested_notional` 喂 `RiskGateway::evaluate` 必不因 sizing/signal 维度被拒。
2. **sizing 绝不绕 RM。** sizing 输出只是 `OrderIntent.size_pUSD_micro` 的 **建议值**, 必经 `RiskGateway::evaluate` 放行 (CLAUDE.md §8 红线 R-1)。看板 quote 展示 ≠ 放行。
3. **fail-closed。** `valid=false` (NaN/Inf/越界) → 全 0, 不下单 (与 FairValueEstimator 同风格)。
4. **更严允许, 更松禁止。** sizing 可比 RM 更保守 (如 MAX_BANKROLL_FRACTION / CI 下界 f*); 绝不可比 RM 更激进。
5. **任何 sizing→quote→OrderIntent 链路变更必须知会老韩 + 留 audit** (CLAUDE.md §7-6 可追溯)。

> 未联签前: `/api/v1/quote` kelly/notional = advisory only, 不接策略层 OrderIntent。**本联签生效后**: 可作 quote 真值, 仍永不绕 RM。

---

## 5. 小袁实现验收口径 (RM 联签门)

小袁 `stcpp::sizing::SizingCalculator` 必过下列单测, 全绿方可解锁 quote 真值接入。

### 5.1 闭合性单测 (`tests/unit/test_sizing_calculator.cpp`)
沿用小梁 §5.1, RM 补强:
- **C1** 任意输入 `suggested_notional ≤ RiskConfig.per_order_cap_usdc` (引用值, 非 10000 字面量)。
- **C2** `suggested_notional + current_token_exposure ≤ RiskConfig.per_outcome_cap_usdc`。
- **C3** `suggested_notional + current_condition_exposure ≤ RiskConfig.market_exposure_cap_usdc`。
- **C4** `suggested_notional ≤ SizingInput.bankroll_usdc × MAX_BANKROLL_FRACTION` (注意基数是 input bankroll 非 config 常量, §1.3)。
- **C5 (CI gating, 新增)** `edge_ci_lower ≤ edge_ci_lower_floor` → `capped_by=NO_EDGE`, 全 0 (§2.1)。
- **C6 (fee 门, 新增)** `edge_ci_lower − 0.03·p·(1−p) ≤ floor` → `capped_by=NO_EDGE`, 全 0 (§2.3 门 B)。
- **C7 (slippage 门, 新增)** `edge_ci_lower(bps) < slippage_bps` → `capped_by=NO_EDGE` (§2.3 门 A)。
- **capped_by 命中:** 每条 cap 构造恰好绑定输入, 断言枚举正确。
- **λ 折扣:** `kelly_fractional = 0.25 × kelly_full` (±1e-12)。
- **fill_rate:** `effective_notional = suggested_notional × fill_rate`; `fill_rate < FILL_RATE_FLOOR (0.50)` → `capped_by=FILL_RATE_FLOOR`, advisory 置 0。
- **数值健壮:** NaN/Inf, p∈{0,1}, c∈{0,1} → `valid=false`, 全 0。
- **方向对称:** p>c (buy YES) 与 镜像 p<c (buy NO) → 对称 f* 幅度。

### 5.2 与 RM 一致性 (`tests/integration/test_sizing_rm_consistency.cpp`)
RM 主权强制项:
- **零 sizing 维度 reject (核心门):** N≥10,000 组随机输入, 用 sizing `suggested_notional` 构 `OrderIntent.size_pUSD_micro`, 喂 **同一 `RiskConfig`** 的 `RiskGateway::evaluate`, 断言 **绝不出现** `EXCEED_PER_ORDER_CAP / EXCEED_PER_OUTCOME_CAP / EXCEED_CONDITION_EXPOSURE / INSUFFICIENT_BANKROLL / EDGE_CI_NEGATIVE / EDGE_NEGATED_BY_SLIPPAGE`。(允许 state/stale/market/duplicate 等非 sizing 维度 reject。)
- **net edge 同源 (±1e-9):** sizing `net_raw_edge` 与 RM `net_edge_after_fee` 在同 `(edge_ci_lower, p)` 下数值一致 (同 `kSportsTakerFeeRate=0.03`)。
- **CI gating 同源:** sizing 置 0 (NO_EDGE) 的输入集 ⊇ RM 因 `EDGE_CI_NEGATIVE`/fee 门拒的输入集 (sizing 不更松)。
- **cap 同源 grep 守护 (老高 CI):** `stcpp::sizing` TU 内出现 `10000|25000|50000` 数字字面量 (cap 上下文) 或自定义 fee 率 → CI 失败。验证 cap 来自 `RiskConfig const&` 注入。
- **bankroll 动态:** 单测覆盖 `set_bankroll()` 缩水后 (如 80k), sizing cap4 用 80k 重算, 不超 RM (§1.3 防 drawdown 漂移)。

**验收门槛:** 5.1 全绿 + 5.2 零 sizing 维度 reject + net edge ±1e-9 + grep 守护绿。任一红 → 不解锁 quote 真值, 退回小袁。

---

## 6. Follow-up (W6 校准后随策略评审)

1. **MAX_BANKROLL_FRACTION 是否进 RiskConfig** (§1.4): 加 RM 字段需老沈 field-freeze + 老郭 ABI ack。本期 sizing 单边持有。
2. **λ_eff × model_conf 开关** (小梁 §1.3 v2): 置信度联动缩仓, 需 W6 实盘校准数据 + 老韩联签。
3. **kSportsTakerFeeRate 跨 TU 共享 header**: 若 sizing 需引用, 老周/老沈 提公共常量头, RM ack (当前在 risk_gateway.cpp L126 文件内)。
4. **小梁 §1.2 净 edge 公式校正**: 合并式 → RM 两道独立门 (§2.3), 记小梁 spec 修订。

---

## 7. 联签签字

| 角色 | 人 | 决定 | 日期 |
|---|---|---|---|
| RM 主权 (本签) | 老韩 | **APPROVED + 4 修正条款** | 2026-05-29 |
| Sharpe/Kelly 主权 | 小梁 | 草案 owner (待回 §2.2/§2.3 校正 ack) | — |
| 架构评审 (升 ADR 时) | 老郭 | 待 (建议升 ADR, CLAUDE.md §6 跨单元+风控红线) | — |
| GM | 老雷 | 待统一合并 | — |
| 实现 | 小袁 | 待 (照 §5 验收) | — |
