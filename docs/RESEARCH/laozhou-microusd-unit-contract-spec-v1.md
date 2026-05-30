# 方案 B 单位契约落地 spec — MicroPUSD typedef + 三边界 (B1)

- **owner:** 老周 (系统工程部主管 + 架构主权)
- **last_review:** 2026-05-30
- **status:** 落地 spec (IC 可直接实施); 老韩 review 风控语义后开工
- **上游决议:** `docs/MEETINGS/2026-05-30-arch-review-m1-forward-plan.md` §3 轨 B1
- **关联红线:** 铁律#3 (回测/实盘同逻辑) / R-20 (4 ts) / ADR-027 (OrderIntent ABI lock v1.8)
- **关联 ADR:** 本 spec 落 **ADR-040** (见 §5)

---

## 0. TL;DR (IC 看这段就够起步)

1. 新建单头 `include/stcpp/domain/micro_pusd.hpp` —— `struct MicroPUSD { std::int64_t v; }`, explicit 构造 + constexpr 运算符 + `_pusd` UDL + `to_pusd()/from_pusd()`。standard-layout, `sizeof==8`, ABI 零变。
2. 只锁 **三边界**: ① RiskConfig + RM setter + risk_gateway 4 比较点; ② OrderIntent.size_pUSD_micro + SignRequest.size_pUSD_micro; ③ PositionView.size_usdc + PositionLedger delta/exposure 返回值。
3. 内部 (SizingCalculator Kelly / VirtualOrder / VirtualFill / backtest) 保留 **double pUSD**, 不碰。只在出口 `from_pusd()` 转一次。
4. paper_loop 的 6 处手抖 `×1'000'000.0` 全删, 改 `MicroPUSD::from_pusd(...)`。
5. 改造顺序: **typedef + 隐式转换桥全绿 → 逐边界拆桥 → 删桥抓漏**。不破 1043 测试 (分 commit, 每步全绿)。
6. 实施: IC (小卢)，分 5 commit。老韩 review 风控语义 (§4.3)。

**根因 (评审已确认):** `risk_gateway.cpp:432/452/465/473` 拿 `size_pUSD_micro` (micro=1e-6 scale) 直接比 `cfg_.per_order_cap_usdc`(=10000，被设计/测试当 whole pUSD)。两边数量级差 1e6。GM A2 已 paper 侧 `×1e6` hotfix (paper_loop.cpp:140/470/478)，但 `_micro` 散布 29 文件、靠人肉 `×1e6`，任一漏乘 = 静默资金量级错 (踩铁律#3)。本契约用类型把单位钉死在三边界，让漏乘变编译错。

---

## 1. MicroPUSD 类型设计

### 1.1 设计约束 (老周拍板)

| 约束 | 决定 | 理由 |
|---|---|---|
| 底层表示 | `std::int64_t v` 单字段 | ABI 零变 (见 §6.1); WAL/VirtualFill 布局不动 |
| 构造 | `explicit constexpr` (禁隐式 int64→MicroPUSD) | 防裸 int64 误当 micro 灌入 |
| `MicroPUSD * MicroPUSD` | **禁** (不提供) | pUSD×pUSD 量纲是 pUSD²，业务无意义，编译期拦死 |
| `MicroPUSD * 整数` | 提供 (返 MicroPUSD) | n 倍下单合法 |
| `MicroPUSD / MicroPUSD` | 提供，返 `double` 比例 | DD%/利用率算比例合法，返无量纲 double |
| `MicroPUSD / 整数` | 提供 (返 MicroPUSD) | 均摊合法 |
| 加减 | 同型 `+ -`，返 MicroPUSD | exposure 累加 |
| 比较 | 全套 `<=> ` (C++20 三路) | 4 个 cap 比较点 |
| `to_pusd()` | `→ double` (v / 1e6) | 出口给 double 算法 (Kelly/展示) |
| `from_pusd(double)` | `→ MicroPUSD` (round) | 入口从 double 算法转回 |
| `_pusd` UDL | `10_pusd == from_pusd(10.0)` | 测试 fixture 字面量可读 |
| 负值 | 允许 (signed) | PositionView signed size (空仓为负) |
| noexcept | 全部 `constexpr noexcept` | 热路径零开销 |

**关键纪律: 不提供 `operator int64_t()` 隐式转出。** 取裸值只能 `m.v` (显式)，让"把 micro 当 whole 用"在 call site 可见。

### 1.2 header 草案 (完整, IC 可直接落)

```cpp
// stcpp/domain/micro_pusd.hpp — MicroPUSD strong typedef v1 (老周 B1, ADR-040)
//
// 单位契约: 1 MicroPUSD = 1e-6 pUSD (micro). 唯一货币量纲类型，钉死三边界。
//
// 设计 (laozhou-microusd-unit-contract-spec-v1.md §1):
//   - 单字段 int64; standard-layout; sizeof==8; ABI 零变 (WAL/VirtualFill 布局不动)
//   - explicit 构造 (禁裸 int64 隐式灌入)
//   - 禁 MicroPUSD*MicroPUSD (pUSD² 无意义); 除同型返 double 比例
//   - to_pusd()/from_pusd() 显式跨 double 算法边界
//   - _pusd UDL 给测试字面量
//
// 红线: 铁律#3 (单位口径回测/实盘一致) / R-20 不涉 (无 ts)
#pragma once

#include <cmath>
#include <cstdint>
#include <compare>

namespace stcpp::domain {

struct MicroPUSD {
    std::int64_t v{0};

    constexpr MicroPUSD() noexcept = default;
    explicit constexpr MicroPUSD(std::int64_t micro) noexcept : v(micro) {}

    // ---- 跨 double pUSD 算法边界 (唯一合法转换通道) ----
    [[nodiscard]] constexpr double to_pusd() const noexcept {
        return static_cast<double>(v) / 1'000'000.0;
    }
    // 从 double pUSD 转回 micro; round-to-nearest (截断会系统性低估 exposure)
    [[nodiscard]] static MicroPUSD from_pusd(double pusd) noexcept {
        return MicroPUSD{static_cast<std::int64_t>(std::llround(pusd * 1'000'000.0))};
    }
    // 直接给 micro 整数 (上游已是 micro 时, 比 from_pusd 省一次乘)
    [[nodiscard]] static constexpr MicroPUSD from_micro(std::int64_t micro) noexcept {
        return MicroPUSD{micro};
    }

    // ---- 同型加减 (exposure 累加) ----
    constexpr MicroPUSD& operator+=(MicroPUSD o) noexcept { v += o.v; return *this; }
    constexpr MicroPUSD& operator-=(MicroPUSD o) noexcept { v -= o.v; return *this; }

    // ---- 整数倍 / 整数均摊 (n 倍下单 / 均摊) ----
    constexpr MicroPUSD& operator*=(std::int64_t k) noexcept { v *= k; return *this; }
    constexpr MicroPUSD& operator/=(std::int64_t k) noexcept { v /= k; return *this; }

    // ---- 比较 (4 cap 比较点; 三路) ----
    friend constexpr auto operator<=>(MicroPUSD, MicroPUSD) noexcept = default;
    friend constexpr bool operator==(MicroPUSD, MicroPUSD) noexcept = default;
};

// 同型加减 (返新值)
[[nodiscard]] constexpr MicroPUSD operator+(MicroPUSD a, MicroPUSD b) noexcept { return MicroPUSD{a.v + b.v}; }
[[nodiscard]] constexpr MicroPUSD operator-(MicroPUSD a, MicroPUSD b) noexcept { return MicroPUSD{a.v - b.v}; }
[[nodiscard]] constexpr MicroPUSD operator-(MicroPUSD a) noexcept { return MicroPUSD{-a.v}; }

// 整数倍 (左右对称) / 整数均摊
[[nodiscard]] constexpr MicroPUSD operator*(MicroPUSD a, std::int64_t k) noexcept { return MicroPUSD{a.v * k}; }
[[nodiscard]] constexpr MicroPUSD operator*(std::int64_t k, MicroPUSD a) noexcept { return MicroPUSD{a.v * k}; }
[[nodiscard]] constexpr MicroPUSD operator/(MicroPUSD a, std::int64_t k) noexcept { return MicroPUSD{a.v / k}; }

// 同型相除 → 无量纲比例 double (DD% / 利用率)
[[nodiscard]] constexpr double operator/(MicroPUSD a, MicroPUSD b) noexcept {
    return static_cast<double>(a.v) / static_cast<double>(b.v);
}

// !! 故意不声明 operator*(MicroPUSD, MicroPUSD): pUSD² 无量纲意义, 误用→编译错 !!

// ---- _pusd UDL (测试字面量: 10_pusd == from_pusd(10.0)) ----
[[nodiscard]] constexpr MicroPUSD operator""_pusd(long double pusd) noexcept {
    return MicroPUSD{static_cast<std::int64_t>(pusd * 1'000'000.0L + (pusd >= 0 ? 0.5L : -0.5L))};
}
[[nodiscard]] constexpr MicroPUSD operator""_pusd(unsigned long long pusd) noexcept {
    return MicroPUSD{static_cast<std::int64_t>(pusd) * 1'000'000};
}
// micro 直读 UDL (cap 阈值以 micro 写时用): 1_upusd == from_micro(1)
[[nodiscard]] constexpr MicroPUSD operator""_upusd(unsigned long long micro) noexcept {
    return MicroPUSD{static_cast<std::int64_t>(micro)};
}

// ---- ABI / layout 保证 (编译期, §6.1) ----
static_assert(sizeof(MicroPUSD) == sizeof(std::int64_t), "MicroPUSD must be 8 bytes (ABI lock)");
static_assert(alignof(MicroPUSD) == alignof(std::int64_t), "MicroPUSD align == int64 (ABI lock)");
static_assert(std::is_standard_layout_v<MicroPUSD>, "MicroPUSD must be standard-layout (WAL/VirtualFill)");
static_assert(std::is_trivially_copyable_v<MicroPUSD>, "MicroPUSD must be trivially copyable (memcpy WAL)");

}  // namespace stcpp::domain
```

> **注 1 (constexpr llround):** `std::llround` 非 constexpr (C++20)，故 `from_pusd` 不是 constexpr (它是运行期入口，无所谓)。`_pusd` UDL 自己用 `+0.5` round 保持 constexpr，可在测试 `static_assert`。IC 别把 `from_pusd` 标 constexpr 否则编译错。
>
> **注 2 (UDL 重载歧义):** `10_pusd` 走 `unsigned long long` 版, `10.0_pusd` 走 `long double` 版，两版语义一致 (都 ×1e6)。IC 两版都要留。

---

## 2. 三边界契约 (精确到字段 / 签名)

> **不变的内部域 (绝不碰):** SizingCalculator (Kelly 全 double pUSD) / VirtualOrder.size_usdc / VirtualFill.fill_size_usdc / backtest stats/metrics / orderbook book_depth / fill_rate / slippage / parquet / ML training_label / audit_record。这些是 double pUSD 或独立量纲，**不在三边界内，本次不动**。29 文件里只有下面列的字段进契约。

### 边界① — RiskManager 入口 (老韩域，风控语义见 §4.3)

**文件:** `include/stcpp/risk/risk_gateway.hpp` + `src/stcpp/risk/risk_gateway.cpp`

`RiskConfig` 4 个金额阈值 `int64_t → MicroPUSD`:

| 字段 | 旧类型 | 新类型 | 语义锁定 |
|---|---|---|---|
| `per_order_cap_usdc` | `int64_t` | `MicroPUSD` | 单单上限 (micro pUSD) |
| `market_exposure_cap_usdc` | `int64_t` | `MicroPUSD` | per-condition cap |
| `per_outcome_cap_usdc` | `int64_t` | `MicroPUSD` | per-token cap |
| `bankroll_usdc` | `int64_t` | `MicroPUSD` | 本金 |
| `daily_loss_halt_usdc` | `int64_t` | `MicroPUSD` | DD 硬熔断绝对值 (旧兼容字段) |

> `daily_loss_soft_pct/hard_pct/strategy_decay_min_ev_ratio/edge_ci_lower_floor` 是无量纲 double / pct，**不动**。`consec_loss_halt_count`(int32)、`excessive_slippage_bps`(int32) 不动。

RM setter 形参 `int64_t → MicroPUSD`:

```cpp
void set_market_exposure(std::string const& market_id, MicroPUSD e) noexcept;
void set_condition_exposure(std::string const& condition_id, MicroPUSD e) noexcept;
void set_outcome_exposure(std::string const& token_id, MicroPUSD e) noexcept;
void set_bankroll(MicroPUSD e) noexcept;
// set_daily_pnl: 见 §4.3 老韩裁定 (pnl 也是 MicroPUSD)
void set_daily_pnl(MicroPUSD pnl) noexcept;
```

RM 内部 atomic 存储仍 `std::int64_t`（atomic<MicroPUSD> 非 lock-free 风险），存 `.v`，读出 `MicroPUSD::from_micro(...)`。`condition_exposure_usdc / market_exposure_usdc / token_exposure_usdc` 三个 map 的 value 保持 `int64_t`（map value 是 micro raw），**只在比较点包成 MicroPUSD**：

`risk_gateway.cpp` 4 比较点改造 (本 bug 的现场):

```cpp
// :432  EXCEED_PER_ORDER_CAP
if (MicroPUSD::from_micro(it.size_pUSD_micro.v) > cfg_.per_order_cap_usdc) ...
//   → it.size_pUSD_micro 已是 MicroPUSD (边界②), 直接 it.size_pUSD_micro > cfg_.per_order_cap_usdc

// :452  EXCEED_CONDITION_EXPOSURE
if (MicroPUSD::from_micro(cur) + it.size_pUSD_micro > cfg_.market_exposure_cap_usdc) ...

// :465  EXCEED_PER_OUTCOME_CAP
if (MicroPUSD::from_micro(cur_tok) + it.size_pUSD_micro > cfg_.per_outcome_cap_usdc) ...

// :473  INSUFFICIENT_BANKROLL
if (it.size_pUSD_micro > MicroPUSD::from_micro(br)) ...   // br = bankroll atomic load
```

DD 熔断块 (:482-499) `daily_pnl_usdc_` / `hard_threshold` / `soft_threshold`：pnl 与 bankroll 都是 micro，`hard_pct × bankroll` 算法走 `cfg_.bankroll.to_pusd() × pct → from_pusd()` 或直接 micro int 算（老韩 §4.3 定）。

### 边界② — SignRequest / OrderIntent

**文件:** `include/stcpp/risk/risk_gateway.hpp` (OrderIntent) + `include/stcpp/signer/signer_iface.hpp` (SignRequest)

| 结构.字段 | 旧 | 新 |
|---|---|---|
| `OrderIntent.size_pUSD_micro` | `std::int64_t` | `MicroPUSD` |
| `SignRequest.size_pUSD_micro` | `std::int64_t` | `MicroPUSD` |

> **ABI 注意:** OrderIntent 受 ADR-027 ABI lock v1.8 管。MicroPUSD `sizeof==8 == int64`，字段偏移零变，**ABI lock 不破**（§6.1 证明）。但 spec 须在 ADR-040 显式记"v0.6 size_pUSD_micro 底层不变，仅 typedef 包装"，并通知老孙 (SignerV62) / 老唐 (audit WAL) 下游：`AuditRecord` 没有 size 字段（已确认 risk_gateway.hpp:265 AuditRecord 无 size），**audit 链不受影响**，无需改 audit schema。

transformer_v62 / signer 透传点：取裸 micro 用 `.v`（`SignV62Request.size_pUSD_micro = req.size_pUSD_micro.v`）。

### 边界③ — PositionLedger

**文件:** `include/stcpp/risk/position_view.hpp` + `include/stcpp/risk/position_ledger.hpp` + `src/stcpp/risk/position_ledger.cpp` + `src/stcpp/infra/wal/position_ledger.cpp`

| 结构.字段 / 函数 | 旧 | 新 |
|---|---|---|
| `PositionView.size_usdc` | `std::int64_t` (signed micro) | `MicroPUSD` (signed) |
| `apply_fill` delta | (内部从 VirtualFill 算) | 出口转 `MicroPUSD::from_pusd(fill.fill_size_usdc * dir)` |
| `update_position_locked_` `delta_usdc` 形参 | `std::int64_t` | `MicroPUSD` |
| `get_per_outcome_exposure()` 返回 | `unordered_map<string,int64_t>` | `unordered_map<string, MicroPUSD>` |
| `get_per_condition_exposure()` 返回 | `unordered_map<string,int64_t>` | `unordered_map<string, MicroPUSD>` |
| 内部 `condition_exposure_` map value | `int64_t` | `MicroPUSD` |

> `last_update_ts`(R-20 ts) / `avg_entry_price`(double price) / `outcome` **不动**。
>
> **VirtualFill.fill_size_usdc 是 double pUSD（内部域），不进契约。** apply_fill 在 ledger 入口把 double pUSD → MicroPUSD（一次转换），ledger 内部全 MicroPUSD。这是边界③的"转换发生点"。

### 内部域转换出口 (唯二两处 from_pusd)

1. **SizingCalculator 出口 (paper_loop.cpp:469-470):**
   ```cpp
   const double notional_pusd = std::min(sizing_out.suggested_notional, 10.0);
   intent.size_pUSD_micro = domain::MicroPUSD::from_pusd(notional_pusd);   // 删 ×1'000'000.0
   if (intent.size_pUSD_micro.v <= 0) intent.size_pUSD_micro = 1.0_pusd;   // 最小 1 pUSD
   ```
2. **PositionLedger.apply_fill 入口:** double `fill.fill_size_usdc` → `MicroPUSD::from_pusd(...)`。

### paper_loop 6 处 ×1e6 hotfix — 全删，走 MicroPUSD

| 行 | 旧 (A2 hotfix) | 新 |
|---|---|---|
| :140 | `set_bankroll((int64)(cfg_.bankroll_usdc * 1e6))` | `set_bankroll(MicroPUSD::from_pusd(cfg_.bankroll_usdc))` |
| :469-470 | `notional × 1e6 → int64` | `MicroPUSD::from_pusd(notional_pusd)` (见上) |
| :472 | `= 1'000'000LL` | `= 1.0_pusd` |
| :478 | `book_depth_l1 * 1e6` | book_depth **不进契约**(double pUSD)，保留 — 见下注 |
| :510 | `sign_req.size_pUSD_micro = intent.size_pUSD_micro` | 同 (两边都 MicroPUSD，直接赋) |
| :536 | `vord.size_usdc = (double)sign_req.size_pUSD_micro.v / 1e6` | `= sign_req.size_pUSD_micro.to_pusd()` |
| :631 | `(double)pv.size_usdc / 1e6` | `pv.size_usdc.to_pusd()` |

> **:478 book_depth_l1_usdc 注意:** `OrderIntent.book_depth_l1_usdc` 是 **double**（不在契约），但 RM `check_liquidity_` 拿它跟 `size_pUSD_micro` 比，单位必须对齐。**老周裁定:** 本次 **不改 OrderIntent.book_depth_l1_usdc 类型**（它是 double pUSD，量纲不同于 MicroPUSD，强行 typedef 会污染 slippage/orderbook 一大片内部域）。check_liquidity_ 比较时 `it.size_pUSD_micro.to_pusd() <= it.book_depth_l1_usdc`（都转 double pUSD 比），:478 的 `×1e6` 删掉，book_depth 保持 pUSD。**这是契约边界的明确切口：MicroPUSD 管"账目/cap/仓位"，double pUSD 管"盘口流动性/sizing 计算"，两域在比较点用 to_pusd() 桥接。** 老韩 review 确认 liquidity 语义。

---

## 3. 改造顺序 (不破 1043 测试)

> 评审定调「typedef + 隐式转换桥全绿 → 逐边界拆桥 → 删桥抓漏」，展开成可执行步骤。**每步 commit 前 `cmake --build && ctest` 全绿**，绿了才下一步。

### Step 0 — 落 header + 单测 (commit 1)
- 写 `micro_pusd.hpp`（§1.2）。
- 写 `tests/unit/test_micro_pusd.cpp`: 构造 / +−/×整数/÷整数/÷同型返比例 / `_pusd`/`_upusd` UDL / to_pusd↔from_pusd round-trip / `static_assert` layout / **编译期负例**（`MicroPUSD*MicroPUSD` 用 `static_assert(!requires{...})` 或注释 negative-compile 说明）。
- 注册进 CMake。**此步零下游改动，纯加法，1043 测试不动。**

### Step 1 — 加隐式转换桥 (commit 2，临时)
- 在 header **临时**加两个隐式转换（带 `// TODO(B1): 拆桥后删`）：
  ```cpp
  /*implicit*/ constexpr MicroPUSD(std::int64_t micro) noexcept : v(micro) {}  // 桥: int64→MicroPUSD
  constexpr operator std::int64_t() const noexcept { return v; }               // 桥: MicroPUSD→int64
  ```
  > 注：与 §1.2 的 `explicit` 冲突——Step 1 临时去 explicit + 加 operator int64。这是脚手架，Step 4 拆回。
- 三边界字段全部改成 `MicroPUSD` 类型（边界①②③一次性改字段声明）。因有隐式桥，**所有现存 call site（裸 int64 字面量 / 比较）无需改，自动隐式转换，编译过**。
- 跑全量 ctest → 应全绿（行为零变，只是类型换皮 + 隐式还原 int64）。**这一步证明字段替换本身不破语义。**

### Step 2 — 边界① 拆桥 (commit 3)
- risk_gateway 4 比较点（:432/452/465/473）+ RM setter 形参 + RiskConfig 字段，改成显式 MicroPUSD 用法（`.v` / `from_micro` / 直接同型比较）。
- **测试 fixture 改字面量过 UDL：**
  - `tests/unit/test_risk_gateway.cpp`: `cfg_.per_order_cap_usdc = 10'000` → `= 10'000.0_pusd`（**注意语义校准！** 旧值 10000 在 bug 下被当 whole pUSD，新值须按"真实想要的 pUSD 上限"重设——老韩 §4.3 定 demo cap 真值，不是机械 ×1）。`it.size_pUSD_micro = 1'000` → `= MicroPUSD::from_micro(1'000)` 或按测试意图 `0.001_pusd`。
  - `set_bankroll(500)` → `set_bankroll(...)` 按真值。
  - `tests/unit/risk_manager_fixture.hpp` / `test_risk_gateway_wave3.cpp` / `bench_risk_gateway.cpp` / `test_sizing_rm_consistency.cpp` / `test_rm_debug_snapshot.cpp` 同步。
- **关键:** 此步是唯一改变数值语义的步（cap 从"错配 whole"改成"正确 micro 上限"），老韩必须 review 每个改后的 cap/bankroll 真值。其余边界纯类型透传不改数值。

### Step 3 — 边界② + 边界③ 拆桥 (commit 4)
- 边界②: OrderIntent / SignRequest `size_pUSD_micro` 显式用法；transformer/signer 透传点 `.v`；paper_loop :510/:536 改 to_pusd。
- 边界③: PositionView / PositionLedger 全显式；apply_fill 入口 `from_pusd`；paper_loop :631 改 to_pusd；exposure map 返回类型改。
- fixture: `test_position_ledger.cpp` / `test_position_ledger_w76.cpp` / `test_paper_loop.cpp` / `test_paper_signer.cpp` / `test_paper_pm_client.cpp` / `test_outbound_serializer.cpp` / `test_order_intent_to_sign_request.cpp` / `test_paper_daemon.cpp` / `paper_e2e_smoke_test.cpp` / `test_fixture.hpp` 的 size 字面量过 `_pusd`/`from_micro`。
- paper_loop.cpp 6 处 ×1e6 全删（§2 表）。

### Step 4 — 删桥抓漏 (commit 5)
- 删 Step 1 的两个隐式桥，恢复 `explicit` 构造 + 删 `operator int64_t`。
- **重新编译——所有还在裸用 int64 的 call site 此刻报编译错。** 逐个修（要么 `.v` 取裸，要么 `from_micro/_pusd` 包）。编译错清零 = 漏改边界全部抓出（编译期兜底，§6.3）。
- 全量 ctest 末次全绿。

> **为何先桥后拆:** 直接改字段成 explicit MicroPUSD 会一次性炸出几百个 call-site 编译错，无法分辨"该改的边界"vs"内部域误伤"。先桥让替换平滑落地+测试守语义不变，逐边界拆桥让每步可独立验证，最后删桥用编译器穷举抓漏。这是评审定的安全路径。

---

## 4. 谁实施 + ADR + 老韩 review 点

### 4.1 实施: IC 小卢，5 commit (§3 Step 0-4 一步一 commit)
- 全程主干 main（GM 写代码新模式 §10.2，IC 此 task 经老周派、老韩 review 后由 GM 落地或 worktree 串行——**B1 落地前老韩冻结一切金额接线**，§见评审 B2 注，故本 task 独占金额域，无并行冲突）。
- 每 commit 前 `cmake --build build && ctest --test-dir build` 全绿。commit message 标 `[B1 step N/5]`。

### 4.2 单 ADR: **ADR-040**
- 标题: `ADR-040: MicroPUSD 单位契约 — 三边界 strong typedef`
- 内容: 本 spec §0/§1.1 决策表 + §2 三边界字段表 + §6.1 ABI 零变证明 + 下游通知清单（老孙/老唐/小梁）。
- 入 `docs/ADR/2026-05-30-adr-040-microusd-unit-contract.md`，老周 owner，老郭架构评审签字，老韩风控签字。

### 4.3 老韩 review 点 (风控语义，非类型)
1. **cap 阈值真值 (Step 2 关键):** 旧 `per_order_cap_usdc=10'000`/`market=50'000`/`per_outcome=25'000`/`bankroll=100'000` 这些数在 bug 下口径混乱。老韩必须给出 **demo/MVP 阶段每个 cap 的真实 pUSD 目标值**（如 per_order 真上限 10 pUSD → `10.0_pusd`），IC 按老韩值填，不机械 ×1。**这是本次唯一改数值语义处，风控主权在老韩。**
2. **DD pnl 单位 (set_daily_pnl + :482-499):** pnl 确认 MicroPUSD（与 bankroll 同单位才能比 pct）。`hard_pct × bankroll` 算法老韩定走 double 中转还是纯 int micro。`daily_loss_halt_usdc` 绝对值阈值真值。
3. **liquidity 比较切口 (:478 + check_liquidity_):** 确认 MicroPUSD(账目/cap) vs double pUSD(盘口) 在比较点 `to_pusd()` 桥接的语义正确（§2 边界注），book_depth 保持 double pUSD 不进契约这个切分是否 OK。
4. **round 方向 (from_pusd llround):** 确认 round-to-nearest（非截断）对 exposure 累加无系统性偏差风险。

---

## 5. ADR 编号
- **ADR-040**（IC 创建文件，老周拟号；若 040 已占，顺延，由小米/老郭确认最新号）。

---

## 6. 风险 + 回滚

### 6.1 ABI 影响 = 零 (证明)
- `MicroPUSD` = 单 `int64_t` 字段，无 vtable / 无虚 / 无额外成员。
- `static_assert sizeof==8 && alignof==alignof(int64) && is_standard_layout && is_trivially_copyable`（header 内置，编译期保障）。
- 受影响结构 (OrderIntent / SignRequest / PositionView) 中 `size_*` 字段**偏移与大小不变**，结构总 sizeof 不变 → ADR-027 ABI lock v1.8 **不破**。
- WAL / VirtualFill 序列化：PositionView 若进 WAL，`memcpy` 布局字节级一致（trivially_copyable + 同 layout）。**但 IC 须确认 WAL 序列化是否 `reinterpret_cast` 整结构**——若是字段级 `write(size_usdc)` 则也无影响（写的是 `.v`）。老唐 WAL schema 通知项。

### 6.2 隐式转换桥误用风险 (Step 1-3 窗口)
- 桥存在的 3 个 commit 窗口内，裸 int64 可隐式进 MicroPUSD → 桥本身可能掩盖单位错。
- **缓解:** 桥是**短命脚手架**，Step 4 必删；删桥后编译器穷举所有裸用点（§6.3）。桥窗口内不依赖类型抓 bug，靠 Step 1 "行为零变全绿"守语义。
- **硬约束:** 桥 commit 绝不 push 到 release/long-run 分支，仅 B1 工作序列内存在。Step 4 未完成 = B1 未完成，不算交付。

### 6.3 漏改边界兜底 = 编译期 (首选) + 运行期断言 (DD 块)
- **编译期 (主兜底):** Step 4 删桥后，任何"把 micro 当 whole / whole 当 micro"的残留 = 类型不匹配编译错。`explicit` 构造 + 无 `operator int64_t` = 裸 int64 进不去、MicroPUSD 出不来，除非显式 `.v` / `from_*` / `to_pusd`。漏乘 ×1e6 的老 bug 此后**不可能编译通过**。
- **运行期 (辅，DD/exposure 数值健全):** risk_gateway DD 块加 debug 断言（仅 debug build）：`assert(cfg_.bankroll.v >= 0)`、exposure 累加后 `assert(exposure.v 在合理量级)`（老韩定上界）。release 不带，零热路径开销。
- **CI grep 守护 (老高):** 加 grep 规则禁止三边界文件内出现裸 `* 1'000'000` / `/ 1'000'000.0`（应全走 to_pusd/from_pusd），漏网 hotfix 复活即 CI 红。

### 6.4 回滚
- 单位契约是**纯类型 + 数值校准**改动，无运行时行为新增（除 Step 2 cap 真值校准）。
- 回滚 = `git revert` 5 个 commit（逆序）。因每 commit 独立全绿，可回滚到任意中间步。
- **不可回滚项:** Step 2 老韩校准的 cap 真值是"修正错配"，回滚会退回 bug 态（cap 错配恒拒/恒过），故回滚仅限"类型方案有缺陷"场景，cap 真值校准应独立保留。建议 cap 真值校准也可拆成 Step 2 内独立子 commit 便于隔离。

---

## 7. 不做什么 (scope 红线，防过度工程)
- **不做** 29 文件全量 strong-typedef（评审明确否决，过度工程）。
- **不碰** SizingCalculator / VirtualOrder / VirtualFill / backtest / orderbook / slippage / fill_rate / parquet / ML / audit_record 的 double pUSD 字段。
- **不改** OrderIntent.book_depth_l1_usdc（double pUSD，盘口域，比较点 to_pusd 桥接）。
- **不引入** atomic<MicroPUSD>（lock-free 风险，RM atomic 存 int64 raw，比较点包 MicroPUSD）。
- **不改** AuditRecord（无 size 字段，audit 链零影响）。

---

## 8. 交付 checklist (IC 自检 + 老周验收)
- [ ] commit1: micro_pusd.hpp + test_micro_pusd.cpp，全绿，1043→1043+N
- [ ] commit2: 三边界字段换型 + 隐式桥，ctest 全绿 (行为零变)
- [ ] commit3: 边界① 拆桥 + 老韩 cap 真值，fixture UDL，全绿
- [ ] commit4: 边界②③ 拆桥 + paper_loop 6×1e6 删，全绿
- [ ] commit5: 删桥 + 编译错清零 (漏改抓尽)，全绿
- [ ] ADR-040 落 docs/ADR/，老郭+老韩签字
- [ ] 下游通知: 老孙(signer .v 透传) / 老唐(WAL 布局零变) / 小梁(sizing 出口 from_pusd) / 老高(CI grep 守护)
- [ ] 老韩 §4.3 四点 review 通过

---

## c6 续章 — PositionLedger/VirtualFill micro 收口 (A1, 老郭轻量级 ADR 签字)

> last_review: 2026-05-30 | 老周 spec + 老郭裁定 (ADR-041 c-series, 非完整评审)

**背景:** 复盘四方收敛 —— 单位债真源在 `PositionLedger` (存 whole pUSD), 非 RiskConfig。
`PublishLedgerSnapshot:659 ÷1e6` 把 whole 当 micro → PnL 低估 1e6 (已躺 main); `(int64)0.7 = 0`
静默丢仓。R-4 根治: PositionLedger + VirtualFill 金额统一 micro。

**R-4 红线原文 (§8.1#1 粘原文禁转述):**
> 「数据 schema 静默变更（不通知下游） → 责任人承担事故」(CLAUDE.md §8 红线)
> 「ABI/字段单位变更必触发下游审计 (补 R-4 配套): 任何字段重命名/单位变更必须 audit 全部
>   比较点/消费点的单位一致性, 否则会「静默架空」依赖该字段的红线」(CLAUDE.md §8.1#3)

**裁定 (老郭):**
1. **`VirtualFill.fill_size_usdc` + `PositionLedger.size_usdc` 用裸 int64 micro, 不上 MicroPUSD**
   —— 老郭撤回"坚持 MicroPUSD"立场 (理由: 单字段不成 struct 级护栏 + ledger 累加不比 cap,
   MicroPUSD 护栏价值在 cap 比较边界已 c1/c2 覆盖)。**这是"helper 单点 + c5 F4 grep + 注释契约
   + bench 补审"四件套换来的豁免, 非免费**。
2. **double→micro 唯一入口 `domain::to_micro_pusd()`** (llround, 复用 from_pusd)。
3. **ABI 零影响**: VirtualFill double(8B)→int64(8B) 同宽, sizeof==120 不变 (static_assert 未改),
   非 memcpy 序列化 (WAL 走字段级转换), R-7 live 不 link。R-4 + R-8.1#3, 轻量级 ADR。
4. **两个同名 PositionLedger** (risk:: + infra::wal::) 同 commit 改 (避免单位再裂)。

**验收 (达成):** 补偿性 ×1e6 从 2 处 (FeedRiskGateway + WAL ledger) → 0; PublishLedgerSnapshot
÷1e6 PnL bug 自愈 (数据对了); <1pUSD 不丢 (`A1_SubOnePusdFill_NotTruncatedToZero`); c5 加 F4
规则守 fill_size×1e6 反模式; P0-1 ExposureRedLine gate 迁移后仍绿 (45e6 → 仍触 EXCEED)。
全量 1061/1061 绿。
