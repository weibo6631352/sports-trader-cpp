# PnL Attribution 真实计算规范 v1

- **owner:** 小程 (quant-signal-research, C 量化研究部 IC #19)
- **last_review:** 2026-05-29
- **status:** Draft — 待小梁 (量化主管) 拍板 + 老石 (G-LEDGER-OWNER) ledger 接入确认 + 老韩 (RM 主权) fee 口径联签
- **关联:**
  - `xiaoliang-kelly-sizing-spec-v1.md` (net edge 口径同源, §1.2)
  - `src/stcpp/debug_api/state_provider.hpp` (`PnlAttribution` struct + `StateProvider::pnl_attribution()`)
  - `src/stcpp/debug_api/endpoint_pnl.cpp` (瀑布 JSON 渲染现状, 现读 DemoStateProvider)
  - `src/stcpp/debug_api/demo_state_provider.hpp` (现有假数据 — 本规范替换目标)
  - `include/stcpp/infra/wal/position_record.hpp` (PositionRecord WAL POD, ground-truth)
  - `include/stcpp/infra/wal/position_ledger.hpp` (PositionLedger 主类, apply\_fill 热路径)
  - `include/stcpp/risk/position_ledger.hpp` (PositionLedger read API — get\_all\_positions)
  - `include/stcpp/backtest/types.hpp` (`compute_realized_pnl` / `compute_net_edge` 参考实现)
  - `xiaotang-audit-schema-v1.1.md` (4-ts 契约 R-20)
  - `laopeng-w9-inplay-edge-gross-net-confirm.md` (gross/net 澄清 — 老彭确认)
  - `xiaojiang-paper-trading-engine-v0.2-cpp.md` (paper ledger WAL / VirtualFill 来源)
- **不含代码实现:** 本规范定口径不写代码 (小程边界); 实现派给 A 单元

---

## §1 为什么要这份规范

`/api/v1/pnl/attribution` 的 endpoint 已存在 (`endpoint_pnl.cpp`)，但 `StateProvider::pnl_attribution()` 目前由 `DemoStateProvider` 返回写死假数：`gross=312.5, fee=-18.3, gas=-2.1, slippage=-9.7, spread=24.6, net=307.0`。这些数字没有任何数学口径支撑，也不来自 ledger 真值。

本规范解决三个问题：

1. **每个分项的精确数学定义**（不产生歧义，工程可直接实现）
2. **恒等式：Σ分项 = net，误差 < 1e-6**（验收门槛）
3. **数据源**：从哪里读（ledger WAL snapshot），读哪些字段，如何聚合到 per\_market 和顶层

---

## §2 前置口径约定

### 2.1 二元 Polymarket share 盈亏结构

Polymarket 体育是二元结算市场：买入 1 share（Yes token），成本 = fill\_price（∈(0,1)）。结算时：
- Win（Yes 结算 = 1.0）：单 share 毛收益 = `1.0 - fill_price`
- Lose（No 结算 = 0.0）：单 share 毛损失 = `-fill_price`

卖方方向（买 No / SellYes）对称：成本 `1 - fill_price`，win 时毛收益 `fill_price - 0`，lose 时毛损失 `-(1 - fill_price)`。

所有金额单位：**USDC**（内部计算 micro-USDC int64 = USDC × 1e6，对外输出 USDC double，精度 1e-4）。

### 2.2 fee 口径同源（与 Kelly sizing 对齐）

Polymarket taker fee 率：`kSportsTakerFeeRate = 0.03`（3%）。

已在 `include/stcpp/microstructure/orderbook.hpp` 定义：
```
inline constexpr double TAKER_FEE_PCT = 0.03;
```

maker rebate（0.75%）在极少数挂单成交时适用；v1 **保守口径：一律按 taker 3% 计入 fee 分项**，与小梁 Kelly sizing spec §1.2 同源，不因 maker rebate 调整 fee 行。Maker rebate 产生的额外 PnL 会体现在 gross，不调整 fee 分项（保守偏向 = 与 RM 口径一致，见小梁 §4 铁律 4）。

### 2.3 数据来源层级

```
primary truth:  PositionRecord (infra/wal/position_ledger.hpp)
                  ├── realized_pnl       (累计已实现 PnL, USDC×1e6 int64)
                  ├── position_delta     (本次变动量, +买 -卖)
                  ├── fill_event_ts_ns / fill_ds_ts_ns / fill_ingestion_ts_ns / fill_as_of_ts_ns
                  └── entry_avg_price_micro (累计平均买入价 × 1e6)

secondary:      VirtualFill (execution/virtual_matcher.hpp)
                  ├── fill_price         (VWAP)
                  ├── fill_size_usdc
                  ├── slippage_bps       (来自 SlippageModel / FillRateModel)
                  └── 4 ts (R-20 全链路透传)
```

**R-11 红线**：paper mode 时读 paper WAL（`/var/lib/stcpp/paper/position*.wal`），response 带 `mode: "paper"`，不污染真账本。**G-LEDGER-OWNER = 小石**（data-structures-expert）持有 ledger snapshot 供数接口，attribution 读其提供的 `PositionLedger::get_all_positions()` + `PositionLedger::last_record()` 快照。

---

## §3 瀑布各项精确定义

瀑布顺序固定：`gross → fee → gas → slippage → spread → net`

### 3.1 gross（毛盈亏 = mark-to-fill 已实现）

**定义：** 已平仓头寸的结算盈亏，按 fill\_price 计算，**不扣任何成本**。

对每笔已结算（`SettleOutcome != Pending`）的交易 i：

```
gross_i =
  (1 - fill_price_i) × size_usdc_i          # BuyYes + YesWins (win)
  - fill_price_i × size_usdc_i              # BuyYes + NoWins  (lose)
  fill_price_i × size_usdc_i                # SellYes + NoWins (win)
  - (1 - fill_price_i) × size_usdc_i       # SellYes + YesWins (lose)

gross = Σ gross_i  (over all settled trades in window)
```

**从 ledger 读法：** `PositionRecord.realized_pnl` 是累计已实现 PnL（WAL append 结构，同一 market 跨多笔 record），但其值已经扣了 fee+slippage（因为 `position_ledger.cpp` 里 `_update_state` 目前累计净额）。因此 **gross 不能直接从 `realized_pnl` 字段读**，需从 `VirtualFill` 历史序列（或 audit WAL）重建：

```
gross = Σ_i  (VirtualFill_i.fill_price 和结算结果计算毛盈亏)
      = Σ_i  compute_realized_pnl_gross(side_i, outcome_i, fill_price_i, size_usdc_i)
```

其中 `compute_realized_pnl_gross` = `compute_realized_pnl`（`backtest/types.hpp`）去掉 fee/slippage 项：

```
gross_i = won_i ? (1.0 - fill_price_i) * size_usdc_i
                : -fill_price_i * size_usdc_i
```

**实现依赖**：A 单元需在 ledger snapshot 或 audit WAL 里保存 per-fill 的 fill\_price + size + outcome，attribution 计算方能重建 gross。**此为接口需求，需老石与 A 单元（老王/小蒋）在 ledger snapshot 接口里提供 `TradeHistory` 迭代视图。**

### 3.2 fee（Polymarket taker fee）

**定义：** 每笔成交向 Polymarket 支付的 taker fee。

```
fee_i    = -kSportsTakerFeeRate × fill_price_i × size_usdc_i
         = -0.03 × fill_price_i × size_usdc_i

fee      = Σ fee_i  (负数，成本)
```

`fee_i` 恒为负（成本侧）。

**注意**：fee 按 fill\_price × size（即按成本计，不按名义值），与 Polymarket 实际 fee schedule 一致：fee = fee\_rate × 入场价 × shares。fill\_price = 入场价，size\_usdc = fill\_price × shares，故：

```
fee_i = -fee_rate × fill_price_i × size_usdc_i / fill_price_i × fill_price_i
```

但 `size_usdc` 本身已经等于 `fill_price × shares_qty`，所以简化为上式。

**从 ledger 读法：** 需 per-fill 的 `fill_price` + `size_usdc`，与 gross 同一数据源。

### 3.3 gas（链上 gas 成本）

**定义：** 链上提交交易的 gas fee（USDC，负数）。

```
gas = Σ_i  gas_usdc_i   (负数)
```

**paper mode（当前阶段）**：`VirtualFill` 无真实链上提交，gas = 0.0。gas 分项为 0 是合法值，不影响恒等式。

**live mode（后期）**：`gas_usdc_i` 来自 Polygon 链上交易收据（老叶 RPC + 老孙 signer），以 MATIC gas × 当日 MATIC/USDC 汇率转换。gas 存入 audit WAL（老唐 schema §2.x），attribution 从 audit WAL 的 `AET_FILL_SETTLED` 事件里读取。

**v1 paper 默认值：** `gas = 0.0`，分项不隐藏（显式置 0，不从 JSON 省略），前端瀑布图可见。

### 3.4 slippage（市场冲击成本）

**定义：** 实际 fill\_price 偏离决策时 mid\_price 所产生的成本。买单时 fill > mid 为负（付出额外），卖单时 fill < mid 为负。

```
slippage_i = signed_qty_i × (mid_price_at_decision_i - fill_price_i)

where:
  signed_qty_i = +size_usdc_i  # BuyYes (买 Yes)
               = -size_usdc_i  # SellYes (卖 Yes)

slippage = Σ slippage_i
```

**等价形式（与 VirtualFill.slippage\_bps 对应）**：

```
slippage_i = -|slippage_bps_i| / 10000 × fill_price_i × size_usdc_i / fill_price_i
           = -slippage_bps_i_abs / 10000 × size_usdc_i / fill_price_i × fill_price_i

# 化简: slippage_bps 已经编码了方向，直接：
slippage_i = -(slippage_bps_i / 10000) × size_usdc_i
```

其中 `VirtualFill.slippage_bps` 是有符号值（买方 bps > 0 表示价格不利方向）。

**从 ledger 读法：** `VirtualFill.slippage_bps` 直接给出，attribution 计算时直接用：

```
slippage = -Σ_i  (|slippage_bps_i| / 10000) × size_usdc_i
```

负号：slippage 是成本，恒 ≤ 0（或 = 0 maker 无冲击）。

### 3.5 spread（taker 跨 spread 成本）

**定义：** 主动吃单时付出的买卖价差成本（taker 视角：以 ask 价买入而非 mid 价，相当于多付出半个 spread）。

```
spread_i = -(best_ask_i - mid_price_i) × (size_usdc_i / fill_price_i)   # BuyYes taker
         = -(mid_price_i - best_bid_i) × (size_usdc_i / fill_price_i)   # SellYes taker
         = -(spread_bps_i / 2) / 10000 × shares_i

where shares_i = size_usdc_i / fill_price_i
```

**简化形式（与 orderbook spread 字段对齐）**：

```
half_spread_i = (best_ask_i - best_bid_i) / 2   # BookSnapshot.spread / 2
spread_i      = -half_spread_i × (size_usdc_i / fill_price_i)

spread = Σ spread_i
```

**特殊情况**：
- Maker 成交（挂单）：不付 spread 成本，`spread_i = 0`。
- paper VirtualMatcher Mode A++（Bernoulli，无真实 book）：用 `SlippageModel` 已估算的 expected\_fill\_price，即 slippage 已包含 spread 成本，此时 `spread_i = 0`，spread 成本完全体现在 slippage 分项中，两项不重叠。

**v1 paper 默认值**：Mode A++ 路径下 `spread = 0.0`，slippage 已涵盖 spread。Mode A（CLOB book 路径）时 spread 可从 `OrderBookSnapshot.spread` 计算。

**拆分原则（防双计）**：`slippage` 和 `spread` 二者合计不超过实际 fill\_price 与 mid\_price 的差距：

```
slippage_i + spread_i = -(fill_price_i - mid_price_i) × signed_shares_i   # 数学等式
```

工程实现时，若 Mode A++ 的 slippage 已经全量捕获偏离，`spread_i` 强制 = 0，避免双计。Mode A（CLOB）时 spread 单独记录。

### 3.6 net（净 PnL）

**定义：**

```
net = gross + fee + gas + slippage + spread
```

`fee / gas / slippage / spread` 均为负数（成本），故 net ≤ gross。

**恒等式（强制验证）：**

```
|net - (gross + fee + gas + slippage + spread)| < 1e-6   # 闭合门槛
```

`net` 直接由上式计算，不独立读取，保证闭合。

---

## §4 per\_market 分市场

`PnlAttribution.per_market` 是顶层各项的分市场拆解：每个 market\_id 独立求和，市场间 net\_pnl 之和等于顶层 net。

```
per_market[k].net_pnl = gross_k + fee_k + gas_k + slippage_k + spread_k

验证：
Σ_k per_market[k].net_pnl == net   (误差 < 1e-6)
```

**market\_id 口径**：用 `PositionRecord.market_id`（32B null-padded bytes32 hex，即 Polymarket condition\_id），与 `PositionView.condition_id` + `HoldingView.market_id` 保持 vendor-agnostic 内部一致。

**per\_market 粒度**：按 condition\_id（即盘口）分组，不按 event\_id 或 token\_id。前端如需 event 层聚合（跨盘口），由前端处理，后端不做。

---

## §5 4 时间戳契约（R-20）

`PnlAttribution.as_of_ts_ns` 的含义：

```
as_of_ts_ns = max(fill_as_of_ts_ns over all fills in window)
```

即 attribution 窗口内最晚一笔成交的 `fill_as_of_ts_ns`（来自 VirtualFill，透传至 PositionRecord.fill\_as\_of\_ts\_ns）。不得用 `now()` 替代（R-20 红线），除非 ledger 尚无任何成交（首次启动时允许 = 0）。

`pnl_attribution()` 调用方（endpoint）将 `PnlAttribution.as_of_ts_ns` 直接写入 JSON `as_of_ts` 字段，不另行读 `now()`。

---

## §6 数据源接入（StateProvider 真值实现）

### 6.1 现状

`StateProvider::pnl_attribution()` 目前由 `DemoStateProvider::pnl_attribution()` 返回写死数据。`StubStateProvider::pnl_attribution()` 返回 zero struct。

### 6.2 目标：LedgerStateProvider（真值实现）

新建 `LedgerStateProvider` 实现 `StateProvider` 接口，`pnl_attribution()` 从 ledger snapshot 计算真值。

**数据流：**

```
PositionLedger (risk::PositionLedger 或 infra::wal::PositionLedger)
    ↓ get_all_positions() → vector<PositionView>     # per-token 持仓快照
    ↓ last_record() → PositionRecord                 # 最新 WAL record（含 as_of_ts）
    ↓ [需扩展] TradeHistory → 迭代 per-fill 数据      # gross/fee/slippage 重建

LedgerStateProvider::pnl_attribution()
    ↓ 聚合 TradeHistory
    ↓ 计算 gross / fee / gas / slippage / spread
    ↓ net = gross + fee + gas + slippage + spread
    ↓ 按 market_id 分组 → per_market[]
    ↓ 验证恒等式 |net - Σ分项| < 1e-6
    ↓ 填充 as_of_ts_ns
    → PnlAttribution
```

**R-12 约束**：`pnl_attribution()` 在 debug\_api 线程（vCPU6）调用，只读 atomic snapshot，不持锁 > 100us。`PositionLedger::get_all_positions()` 已经是 shared\_lock + copy，满足约束。TradeHistory 视图需同样是 copy-on-read。

### 6.3 ledger snapshot 现有字段 vs 需要扩展的字段

| 需要的信息 | 现有字段 | 是否够用 | 扩展需求 |
|---|---|---|---|
| per-fill gross | VirtualFill.fill\_price + fill\_size\_usdc + 结算 outcome | 不在 PositionRecord | 需 TradeHistory（fill 序列快照，见 §6.4） |
| per-fill fee | fill\_price + fill\_size\_usdc | 不在 PositionRecord | 同上 |
| per-fill slippage\_bps | VirtualFill.slippage\_bps | 不在 PositionRecord | 同上 |
| per-fill spread | BookSnapshot.spread at fill\_time | 不在 PositionRecord | Mode A++ 下 = 0，无需扩展 |
| gas | 链上 gas（live）/ 0（paper） | paper = 0 不需要 | live 需 audit WAL 读 |
| as\_of\_ts | PositionRecord.fill\_as\_of\_ts\_ns | 已有 | 够用 |
| market\_id | PositionRecord.market\_id（32B） | 已有 | 够用 |
| 累计 realized\_pnl | PositionRecord.realized\_pnl | 已有但含 fee | 不直接用（见 §3.1 说明） |

### 6.4 TradeHistory 接口需求（派给 A 单元）

attribution 计算需要一个**只读迭代视图**，遍历窗口内所有已结算成交，每条记录包含：

```cpp
struct FillSummary {
    std::string  market_id;       // condition_id (vendor-agnostic)
    TradeSide    side;            // BuyYes / SellYes
    SettleOutcome outcome;        // YesWins / NoWins / Pending
    double       fill_price;      // 成交价 ∈ (0,1)
    double       size_usdc;       // 成交名义额 USDC
    int32_t      slippage_bps;    // 有符号，来自 VirtualFill.slippage_bps
    int32_t      spread_half_bps; // 半 spread bps（Mode A）; Mode A++ = 0
    std::int64_t fill_as_of_ts_ns; // R-20 最后一个 ts
};
```

**接口建议（供 A 单元参考，不强制）**：

```cpp
// 在 risk::PositionLedger 或新的 TradeHistoryProvider 上扩展
std::vector<FillSummary> get_trade_history(std::int64_t window_start_ns,
                                           std::int64_t window_end_ns) const noexcept;
```

该接口属于**跨单元接口契约**，需由 A 单元（老周/老王/老石）提供实现，C 单元（小程/小蒋）消费。依照 CLAUDE.md §6 协商原则：小程出口径需求（本文档），A 单元出实现约束，由老胡协商窗口确认接口。

---

## §7 谁实现（实现归属提名）

| 组件 | 提名实现方 | 理由 |
|---|---|---|
| `LedgerStateProvider::pnl_attribution()` C++ 实现 | **A 单元 IC**（小卢 or 老王指定）| 紧贴 ledger 实现，A 单元对 WAL/PositionLedger 最熟；pnl\_attribution 计算是纯函数，口径由小程锁死，实现不需要量化判断 |
| `FillSummary` 迭代视图 / TradeHistory 扩展 | **老石（G-LEDGER-OWNER）+ 老王** | 老石持有 ledger 数据结构主权，WAL 扩展需老王 WAL framework 配合 |
| `LedgerStateProvider` main 注入（替换 DemoStateProvider） | **小卢（观测 IC）** | 小卢持有 HttpServer + StateProvider 注入点，替换 DemoStateProvider 是小卢本人工作边界（`laozhou-w8-debug-rest-api-spec-v1.md §9`） |
| 闭合单测（§8） | A 单元 IC（与 `LedgerStateProvider` 同文件）| 验收门槛由小梁/小程 review |

**提名说明**：小程提信号/口径，不写代码（小程边界 = 量化口径 + 信号研究，不含 C++ 实现），与小梁 Kelly sizing spec §3 惯例一致。

---

## §8 验收标准（闭合单测）

文件：`tests/unit/test_pnl_attribution.cpp`

### 8.1 恒等式闭合（必过，P0）

**T1 — 基础闭合：**
构造 N 笔已结算 FillSummary（覆盖 BuyYes win/lose + SellYes win/lose），计算 PnlAttribution，断言：
```
|attribution.net - (attribution.gross + attribution.fee + attribution.gas
                   + attribution.slippage + attribution.spread)| < 1e-6
```

**T2 — per\_market 汇总等于顶层 net：**
```
|Σ per_market[k].net_pnl - attribution.net| < 1e-6
```

**T3 — 零成交时全 0：**
空 FillSummary 列表 → attribution 所有分项 = 0.0，net = 0.0。

**T4 — 单笔 BuyYes win 数值校验：**
```
fill_price = 0.60, size_usdc = 1000.0, slippage_bps = 30, outcome = YesWins, gas = 0

gross     = (1.0 - 0.60) × 1000.0 = 400.0
fee       = -0.03 × 0.60 × 1000.0 = -18.0
gas       = 0.0
slippage  = -(30/10000) × 1000.0   = -3.0
spread    = 0.0  (Mode A++)
net       = 400.0 - 18.0 + 0.0 - 3.0 + 0.0 = 379.0

断言：|attribution.net - 379.0| < 1e-6
```

**T5 — 单笔 BuyYes lose 数值校验：**
```
fill_price = 0.60, size_usdc = 1000.0, slippage_bps = 30, outcome = NoWins, gas = 0

gross     = -0.60 × 1000.0          = -600.0
fee       = -0.03 × 0.60 × 1000.0  = -18.0
slippage  = -3.0
net       = -600.0 - 18.0 - 3.0    = -621.0

断言：|attribution.net - (-621.0)| < 1e-6
```

**T6 — fee 系数一致性（防 kSportsTakerFeeRate 漂移）：**
attribution.fee 计算时使用的 fee\_rate 与 `TAKER_FEE_PCT = 0.03` 一致（±1e-9）。
（对标小梁 Kelly sizing spec §5.2 "cap 同源 grep 守护"原则，防止两处常量不同步。）

**T7 — 多 market per\_market 分组：**
构造 2 个 market（market\_A 2 笔，market\_B 1 笔），断言：
- `per_market.size() == 2`
- per\_market[A].net\_pnl + per\_market[B].net\_pnl == attribution.net（±1e-6）

### 8.2 与 timeseries 同 ledger ground-truth（对标小蒋）

`pnl_attribution()` 和 `pnl_timeseries()` 读同一份 `TradeHistory`（同一 ledger 快照），两者的 `cum_net_pnl`（timeseries 末桶）= `attribution.net`（±允许桶边界误差 1e-4）。

此一致性验证为集成测试，文件：`tests/integration/test_pnl_ledger_consistency.cpp`，由小蒋（timeseries SSOT）+ 老石（ledger SSOT）联合验收。

---

## §9 接口冻结声明（恒等式是合约）

以下数学关系视为不可变合约，任何修改须走 ADR：

1. `net = gross + fee + gas + slippage + spread`（精度 < 1e-6）
2. `Σ per_market[k].net_pnl = net`（精度 < 1e-6）
3. `fee_i = -0.03 × fill_price_i × size_usdc_i`（fee\_rate 常量绑定 `TAKER_FEE_PCT`）
4. `slippage_i = -(slippage_bps_i / 10000) × size_usdc_i`（Mode A++ 下）
5. `spread_i = 0`（Mode A++ 下；Mode A CLOB 时 = `-(spread/2) × shares`，需单独 ADR）
6. `gas = 0`（paper mode；live 时需 audit WAL 扩展，需单独 ADR）

---

## §10 开放问题（派单前须确认）

| # | 问题 | 找谁 | 截止 |
|---|---|---|---|
| OQ-1 | TradeHistory 迭代视图接口签名：是在 `risk::PositionLedger` 扩展还是新建 `TradeHistoryProvider`？ | 老石（LEDGER-OWNER）+ 老王（WAL framework）+ 老周（架构审批） | 接到本 spec 后 48h |
| OQ-2 | `PositionRecord.realized_pnl` 现在存的是净 PnL 还是毛 PnL？如果是净，gross 只能从 FillSummary 重建，要确认 WAL/paper ledger 落了哪些字段（fill\_price / slippage\_bps 是否持久化） | 小蒋（paper engine）+ 老石 | 同上 |
| OQ-3 | live mode 下 gas\_usdc 从链上 gas receipt 取，单位换算（MATIC → USDC）谁维护汇率？是否进 audit WAL？ | 老叶（polygon RPC）+ 老唐（audit schema） | v1 paper 不阻塞，live 前确认 |
| OQ-4 | spread 分项：Mode A（CLOB book 路径）下 `BookSnapshot.spread` 是否在 VirtualFill 时已记录？还是需 book 快照旁路？ | 老冯（orderbook）+ 小袁（microstructure）| v1 paper Mode A++ spread=0，不阻塞 |
| OQ-5 | `pnl_attribution()` 的计算窗口（全历史 vs 当日 vs 滚动 7d）？现有 DemoStateProvider 是全量，建议 v1 默认全历史（无 window 参数），与 timeseries window 解耦。 | 小梁（量化主管拍板）| Sprint-1 Sprint Planning 前 |

---

## §11 与 Kelly sizing spec 口径对齐检查表

小梁 Kelly sizing spec v1（`xiaoliang-kelly-sizing-spec-v1.md`）中 net\_edge 口径（§1.2）：

```
net_edge_after_fee = edge_ci_lower − kSportsTakerFeeRate × p × (1−p)
                   = gross_edge − 0.03 × p × (1−p)   (近似)
```

本规范 fee 分项：`fee_i = -0.03 × fill_price × size_usdc`

两者一致性：在 near-even 盘（`fill_price ≈ 0.5`，`p ≈ 0.5`）时，`p×(1-p) ≈ 0.25`，而 `fill_price × size_usdc = 0.5 × size`，两个表达式对 per-unit edge 的一阶效应一致（`0.03 × 0.25 ≈ 0.0075 per unit` vs `0.03 × 0.5 = 0.015 per dollar`，单位不同但同源系数）。**关键点**：两者均使用 `kSportsTakerFeeRate = 0.03`，保证系数同源不漂移。

老彭 `laopeng-w9-inplay-edge-gross-net-confirm.md` 已确认：inplay edge 1.5-2.5% 是 gross（pre-fee），Polymarket 3% taker fee 后 net 可能为负。本规范 fee 分项明确将该 3% 扣出，与老彭澄清一致。

---

**文档结束**

— 小程 (quant-signal-research, C 量化研究部 IC #19), 2026-05-29
