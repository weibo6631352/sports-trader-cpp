# PnL 归因计算集成规范 v1

- **owner:** 小程 (quant-signal-research, C 量化研究部 IC #19)
- **last_review:** 2026-05-29
- **status:** Draft — 待小梁 (量化主管) + 老石 (G-LEDGER-OWNER) + 老周 (架构) 三方确认后转 Ready
- **关联:**
  - `xiaocheng-pnl-attribution-calc-spec-v1.md` (口径 SSOT, 本文件是落地细化)
  - `include/stcpp/risk/ledger_snapshot_hub.hpp` (LedgerSnapshotHub + LedgerFeatures POD)
  - `src/stcpp/debug_api/real_state_provider.hpp` (RealStateProvider::pnl_attribution 当前实现)
  - `src/stcpp/debug_api/replay_feed_coordinator.hpp` (现有 demo LedgerFeatures 派生)
  - `include/stcpp/infra/wal/position_ledger.hpp` (WAL PositionLedger, infra 层)
  - `include/stcpp/risk/position_ledger.hpp` (risk PositionLedger, 读 API 层)
  - `include/stcpp/execution/virtual_matcher.hpp` (VirtualFill — fill 数据原始来源)
  - `include/stcpp/backtest/types.hpp` (TradeSide / SettleOutcome / compute_realized_pnl)
  - `include/stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp` (mark price 来源)
- **不含代码实现:** 接口草案 + 计算口径，实现归 A 单元

---

## §1 本文目标

`xiaocheng-pnl-attribution-calc-spec-v1.md` 已锁定数学口径（恒等式、各分项定义、验收 T1-T7）。
本文在此基础上解决三个落地问题：

1. **现有 hub 字段盘点**：LedgerSnapshotHub 现在能提供哪些 PnL 分项，哪些还缺失
2. **缺口弥补路径**：gross/slippage 重建需要 FillSummary 视图——接口草案 + 对接约定
3. **真实 mark price 路径**：unrealized PnL 如何从 OrderBookSnapshotHub 取真实 mark

---

## §2 现有 LedgerSnapshotHub 字段盘点

### 2.1 LedgerFeatures POD 字段表（已有）

| 字段 | 类型 | 含义 | attribution 用途 |
|---|---|---|---|
| `pnl_gross` | `double` | `pnl_realized + pnl_unrealized` | 顶层 gross 候选（见 §2.2 问题） |
| `pnl_fee` | `double` | 正数，已付 taker fee | 顶层 fee = -Σ pnl_fee |
| `pnl_realized` | `double` | 历史已实现 PnL（含义见 §2.2） | 不直接用，见下文 |
| `pnl_unrealized` | `double` | (mark - avg_entry) × net_qty | unrealized 组件 |
| `mark_price` | `double` | 写端填入的当前市价 | unrealized 重算 |
| `avg_entry_price` | `double` | 加权均价 | unrealized 重算 |
| `net_qty` | `double` | 净持仓 | unrealized 重算 |
| `as_of_ts_ns` | `int64_t` | 透传自 VirtualFill | as_of_ts 锚 |
| `mode` | `ExecutionModeTag` | paper/live/backtest | R-11 mode 标记 |

`pnl_net()` = `pnl_gross - pnl_fee`（正号 fee 转负号 net 差）。

### 2.2 字段语义问题（OQ-2 实地核实结论）

通过阅读 `src/stcpp/infra/wal/position_ledger.cpp` 实现，确认：

**WAL 层（infra::wal::PositionLedger）：**
- `PositionRecord.realized_pnl` 在 v0.1 开仓阶段保持 0（注释：`仅减仓时结算; 当前 v0.1 仅开仓, realized_pnl 不变; settle 时由小蒋回填`）
- `_update_state` 直接把 `rec.realized_pnl` 回写到 `PositionState.realized_pnl`，无 fee/slippage 逻辑
- 结论：**WAL 层 realized_pnl = 毛盈亏（未扣 fee/slippage），由小蒋 settle 时写入**

**ReplayFeedCoordinator（现有 demo LedgerFeatures 派生）：**
```cpp
lf.pnl_realized = entry.net_qty * 0.02;     // 2% realized demo — 硬编码 demo 值
lf.pnl_fee = entry.net_qty * 0.03 * 0.005;  // 0.5% × 3% taker fee demo
lf.pnl_gross = lf.pnl_realized + lf.pnl_unrealized;
```
这些是 demo 占位值，不来自真实 fill 数据。

**当前 RealStateProvider::pnl_attribution()：**
```cpp
gross += lf.pnl_gross;
fee -= lf.pnl_fee;  // fee 正数转负数
attr.slippage = 0.0;  // 无数据源
attr.spread = 0.0;    // 无数据源
attr.net = gross + fee;
```
此实现在 hub 有 demo 数据时可输出结构合法的 JSON，但数字是 demo 值，`net != gross + fee + slippage + spread + gas`（因 slippage/spread/gas 均为 0，数学上实际是满足的，但各分项意义错误）。

### 2.3 字段充分性矩阵

| attribution 分项 | 数据需求 | 现有 hub 字段 | 充分？ | 缺口 |
|---|---|---|---|---|
| **gross** | per-fill (side, outcome, fill_price, size) | 无 per-fill 数据 | 否 | 需 FillSummary 视图 |
| **fee** | per-fill (fill_price, size) | `pnl_fee` (聚合 demo) | 部分 | demo 值，真实接入需 per-fill |
| **gas** | 链上 gas receipt | 无 | 否（paper=0 不阻塞） | live 时需 audit WAL |
| **slippage** | per-fill slippage_bps | 无 | 否 | 需 FillSummary.slippage_bps |
| **spread** | per-fill spread (Mode A) | 无 | 否（Mode A++=0 不阻塞） | Mode A 时需 |
| **net** | 上面五项之和 | `pnl_net()` (demo) | 否（由上面各项推导） | 全部修正后自动正确 |
| **per_market** | market_id + 各分项 | `market_key` | 是（key 已有） | 各分项缺口同上 |
| **as_of_ts_ns** | 最新 fill 的 as_of_ts | `lf.as_of_ts_ns` | 是 | max 聚合 |
| **unrealized mark** | 实时 book mark price | `lf.mark_price`（来自 OrderBookSnapshotHub.microprice） | 是（真实路径已通） | 见 §4 |

---

## §3 闭合恒等式计算口径（与 spec-v1 一致，加落地细化）

本节将 spec-v1 §3 中的数学定义和 LedgerFeatures 字段对齐，给出 A 单元实现时的具体步骤。

### 3.1 gross — 从 FillSummary 序列重建（不能从 hub 直读）

**原因**：`LedgerFeatures.pnl_gross` = `pnl_realized + pnl_unrealized`，其中 `pnl_realized` 在 hub 里目前是 demo 值；即使真实接入后，hub 的 `pnl_realized` 也是累计净额（含多笔 apply_fill 叠加），**没有 outcome 信息**，无法判断是否 win/lose，不能直接推导 gross。

**正确计算路径**：

```
foreach FillSummary fs in get_trade_history(window_start_ns, window_end_ns):
    if fs.outcome == Pending: skip
    won = (fs.side == BuyYes && fs.outcome == YesWins) ||
          (fs.side == SellYes && fs.outcome == NoWins)
    gross_i = won ? (1.0 - fs.fill_price) * fs.size_usdc
                  : -fs.fill_price * fs.size_usdc
    gross += gross_i
```

这与 `backtest/types.hpp::compute_realized_pnl` 中的 `gross` 项完全一致（去掉 fee+slippage cost 项）。

### 3.2 fee — 从 FillSummary 重建（or 从 hub pnl_fee 聚合）

真实 fill 接入后，有两条路：

**路径 A（推荐，与 gross 同源）**：
```
fee_i = -0.03 * fs.fill_price * fs.size_usdc    // TAKER_FEE_PCT = 0.03
fee = sum(fee_i) over all fills in window
```

**路径 B（hub 聚合，精度依赖写端填写质量）**：
```
fee = -sum(lf.pnl_fee for each market key in hub where lf.valid)
```

路径 B 依赖写端（VirtualMatcher/PositionLedger 写入 LedgerFeatures 时）必须正确计算 `pnl_fee = 0.03 × fill_price × size_usdc`。当前 demo 写端（ReplayFeedCoordinator）是占位值，不可用于真实计算。

**v1 paper 建议**：先实现路径 A（FillSummary），路径 B 作为后期优化（减少 FillSummary 遍历开销）。若选路径 B，写端必须保证 `pnl_fee` 精确 = `-sum(fee_i over per-fill)`，且老石在 LedgerFeatures 字段注释中明确此语义。

### 3.3 gas

paper mode = 0.0，直接硬填。无数据源缺口，不阻塞 v1 闭合。

### 3.4 slippage — 从 FillSummary.slippage_bps 重建

```
slippage_i = -(fs.slippage_bps / 10000.0) * fs.size_usdc
slippage = sum(slippage_i) over all fills   // 恒 <= 0
```

`slippage_bps` 来自 `VirtualFill.slippage_bps`（`VirtualMatcher` 输出），需在 FillSummary 里透传保留。

### 3.5 spread

Mode A++ = 0.0（slippage 已全量捕获偏离）。不阻塞 v1 闭合。

### 3.6 net 和闭合验证

```
net = gross + fee + gas + slippage + spread

// 验收门槛（T1）
assert |net - (gross + fee + gas + slippage + spread)| < 1e-6
```

net 由上式推导，不独立读取任何 hub 字段，保证算术闭合。

---

## §4 与 LedgerSnapshotHub 对接——字段来源矩阵

### 4.1 来自 hub 的字段（直接可用）

| 归因需求 | hub 字段路径 | 说明 |
|---|---|---|
| `as_of_ts_ns` | `max(lf.as_of_ts_ns for all valid keys)` | 最新帧 ts，R-20 合规 |
| `per_market[k].market_id` | hub `market_key`（condition_id） | 遍历 token_map_ 的 key |
| `mode` (R-11) | `lf.mode` → `ExecMode` 转换 | paper=kPaper(0) |
| unrealized (辅助) | `lf.pnl_unrealized` 或 `(lf.mark_price - lf.avg_entry_price) * lf.net_qty` | 可用于 HoldingView，attribution 不直接用 |

### 4.2 来自 FillSummary 视图的字段（gross/fee/slippage 核心）

以下字段**不在 LedgerSnapshotHub**，需从 FillSummary/TradeHistory 视图读取：

| 归因分项 | 所需字段 | 来源 | 说明 |
|---|---|---|---|
| gross | `fill_price`, `size_usdc`, `side`, `outcome` | `FillSummary` | VirtualFill + settle outcome |
| fee | `fill_price`, `size_usdc` | `FillSummary` | 同上 |
| slippage | `slippage_bps`, `size_usdc` | `FillSummary.slippage_bps` | 来自 VirtualFill.slippage_bps |
| per_market | `market_id` | `FillSummary.market_id` | condition_id 分组键 |
| as_of_ts | `fill_as_of_ts_ns` | `FillSummary.fill_as_of_ts_ns` | R-20 最后 ts |

### 4.3 当前实现（RealStateProvider）中对接现状

`real_state_provider.hpp` 的 `pnl_attribution()` 当前代码（line 198-232）：
- 从 `ledger_hub_->Read(cond_id)` 读 `LedgerFeatures`
- 读 `lf.pnl_gross` 和 `lf.pnl_fee`（均为 demo 值）
- `slippage = 0.0, spread = 0.0, gas = 0.0`
- `net = gross + fee`

这在结构上是合法的（闭合，因 slippage/spread/gas = 0），但数字不来自真实 fill。**真实接入的改动点**：将 `gross/fee/slippage` 从读 hub 聚合字段改为遍历 FillSummary 计算，`net` 由五项之和得出。

---

## §5 FillSummary 接口草案（供 A 单元参考）

以下是小程提出的接口需求，A 单元（老石/老王）可按架构偏好调整签名，但**必须提供的语义**不变。

### 5.1 FillSummary 数据结构

```cpp
// 命名空间建议: stcpp::risk 或 stcpp::infra::wal (老石/老周决定)
// 文件建议: include/stcpp/risk/fill_summary.hpp

struct FillSummary {
    // 市场标识 (condition_id, vendor-agnostic)
    std::string  market_id;

    // 交易方向
    backtest::TradeSide    side;      // BuyYes / SellYes

    // 结算结果 (Pending 表示未结算, 计算 gross 时跳过)
    backtest::SettleOutcome outcome;

    // 成交价 (∈ (0,1), 来自 VirtualFill.fill_price)
    double fill_price{0.0};

    // 成交名义额 USDC (来自 VirtualFill.fill_size_usdc)
    double size_usdc{0.0};

    // 滑点 bps (有符号; 来自 VirtualFill.slippage_bps; 买方不利方向 > 0)
    int32_t slippage_bps{0};

    // 半价差 bps (Mode A 时填; Mode A++ = 0; 单位同 slippage_bps)
    int32_t spread_half_bps{0};

    // R-20: 最后一个时间戳 (来自 VirtualFill.as_of_ts_ns)
    int64_t fill_as_of_ts_ns{0};
};
```

**设计约定**：
- `TradeSide` / `SettleOutcome` 复用 `backtest/types.hpp` 已有定义，不新建枚举
- `market_id` 用 `condition_id`（与 PositionRecord.market_id 32B 同源，转 string 时 null-padded 处理）
- `fill_as_of_ts_ns` 对应 `PositionRecord.fill_as_of_ts_ns`，R-20 合规

### 5.2 TradeHistoryProvider 接口

```cpp
// 建议放在: include/stcpp/risk/trade_history_provider.hpp
// 实现注入到 LedgerStateProvider 构造函数，不改 StateProvider 接口

class TradeHistoryProvider {
public:
    virtual ~TradeHistoryProvider() = default;

    // 返回 [window_start_ns, window_end_ns] 内所有 FillSummary 快照副本
    // 按 fill_as_of_ts_ns 升序排列
    // R-12: 返回 copy，调用方持有期间无锁
    // 窗口端点 0 表示无限制（window_start_ns=0 → 全历史）
    [[nodiscard]] virtual std::vector<FillSummary>
    get_fills(int64_t window_start_ns = 0,
              int64_t window_end_ns   = INT64_MAX) const noexcept = 0;

    // 已结算笔数（快速统计，无需全量遍历）
    [[nodiscard]] virtual int64_t settled_count() const noexcept = 0;
};
```

**替代方案**（若 A 单元不想新建接口类）：在 `infra::wal::PositionLedger` 上直接扩展：

```cpp
// 在 include/stcpp/infra/wal/position_ledger.hpp 扩展
[[nodiscard]] std::vector<FillSummary>
get_trade_history(int64_t window_start_ns = 0,
                  int64_t window_end_ns   = INT64_MAX) const noexcept;
```

老石/老王决定放在哪里，小程不强制。关键是**接口语义**：只读快照副本，不持锁，R-12 合规。

### 5.3 FillSummary 数据来源链

```
VirtualMatcher::Match / MatchWithBook
    → VirtualFill {fill_price, fill_size_usdc, slippage_bps, outcome, market_id, as_of_ts_ns}
        → infra::wal::PositionLedger::apply_fill
            → PositionRecord 写 WAL (fill_price 没进 WAL! 见 §5.4)
                → FillSummary 需另行持久化或在内存中维护
```

### 5.4 关键数据缺口：fill_price 不在 PositionRecord WAL

**这是 OQ-2 的核心发现**：

查阅 `position_record.hpp` 字段布局（152B），其中**不含 fill_price 字段**。WAL 里有的是：
- `position_delta`（USDC * 1e6 增量）
- `entry_avg_price_micro`（累计均价）
- `realized_pnl`（累计净额，settle 时回填）

`entry_avg_price_micro` 是累计加权均价，不是单笔 `fill_price`；在多笔成交的情况下，无法从 WAL 反推单笔 `fill_price`。

**因此，FillSummary 必须在 apply_fill 时同步维护**，不能从 WAL replay 重建单笔数据。

推荐实现：在 `infra::wal::PositionLedger` 内维护一个 `fill_history_` ring buffer（或 `std::deque`），每次 `apply_fill` 成功后追加 `FillSummary`。读端通过 `get_trade_history()` 取 copy。容量上限由老石/老王决定（建议 10000 笔，约 800KB）。

---

## §6 unrealized PnL 与真实 mark price 对接

### 6.1 当前 mark price 路径（已通）

`ReplayFeedCoordinator::DriveLoop()` 在每个 tick 里：
```
mark_price = OrderBookFeatures.microprice   // 来自 OrderBookSnapshotHub
             or (best_bid + best_ask) / 2   // microprice 无效时 fallback
→ LedgerFeatures.mark_price = mark_price    // hub 写端
→ LedgerFeatures.pnl_unrealized = (mark_price - avg_entry_price) * net_qty
```

这条路径在 `--replay` 模式下**已经是真实 book 数据**（`OrderBookSnapshotHub` 的数据来自 WSS event loop 的 `hub_.Publish`），不是假数据。

### 6.2 真实 paper/live fill 接入后 mark price 的更新方式

当真实 VirtualMatcher 接入时，mark price 的更新不变，仍由 book hub 驱动：

```
[vCPU0 WSS loop]
    OrderBookSnapshotHub.Publish(token_id, OrderBookFeatures)

[vCPU3 VirtualMatcher loop 或独立 mark 更新 thread]
    book_feat = OrderBookSnapshotHub.Read(token_id)
    mark = book_feat.microprice (or mid fallback)
    LedgerFeatures.mark_price = mark
    LedgerFeatures.pnl_unrealized = (mark - avg_entry_price) * net_qty
    LedgerSnapshotHub.Publish(market_key, LedgerFeatures)
```

**关键**：mark price 来源永远是 `OrderBookSnapshotHub`（真实 WSS book），不是本地 `now()` 或硬编码值。`pnl_unrealized` 的 R-20 合规性通过 hub 的 4-ts 透传链保证（`LedgerFeatures.event_ts_ns / data_source_ts_ns` 来自 `OrderBookFeatures`）。

### 6.3 unrealized PnL 在 attribution 中的位置

按 spec-v1 §3.1，`gross` 只含**已结算**成交的毛盈亏（`outcome != Pending`），不含 unrealized。

`LedgerFeatures.pnl_unrealized` 在 `LedgerFeatures.pnl_gross` 里（= `pnl_realized + pnl_unrealized`），但 attribution 的 `gross` 分项**不包含 unrealized 部分**。

**对接说明**：

| 概念 | attribution 分项 | hub 字段 | 关系 |
|---|---|---|---|
| 已结算毛盈亏 | `gross` | 不在 hub（从 FillSummary 重建） | 仅 settled fills |
| 未实现 PnL | 不在 attribution 瀑布 | `lf.pnl_unrealized` | 仅在 HoldingView 展示 |
| hub.pnl_gross | 不直接用 | `lf.pnl_gross = realized + unrealized` | 混合，不等于 attribution.gross |

这意味着 **attribution 的 `gross` 和 `LedgerFeatures.pnl_gross` 语义不同**，不能互换读取。

---

## §7 PnlAttributionCalculator 纯函数接口草案

以下是小程给 A 单元实现的纯函数计算器接口草案（不含 I/O，纯数学变换）。

### 7.1 接口签名

```cpp
// include/stcpp/risk/pnl_attribution_calculator.hpp
// 纯函数，无状态，noexcept，线程安全
// 输入: FillSummary 序列 + gas (live 时从 audit WAL 注入, paper = 0)
// 输出: PnlAttribution (debug_api 类型)

namespace stcpp::risk {

struct PnlAttributionInput {
    std::vector<FillSummary> fills;       // 已结算 fill 序列 (outcome != Pending)
    double gas_usdc{0.0};                 // 链上 gas (paper = 0.0; live 从 audit WAL 注入)
    double fee_rate{0.03};               // taker fee rate (绑定 TAKER_FEE_PCT, 防漂移)
};

// PnlAttributionCalculator::compute — 纯函数
// 输入: PnlAttributionInput
// 输出: debug_api::PnlAttribution
// 恒等式保证: |net - (gross + fee + gas + slippage + spread)| < 1e-6
// R-12: 纯计算，无 I/O, noexcept
[[nodiscard]] debug_api::PnlAttribution
compute_pnl_attribution(const PnlAttributionInput& in) noexcept;

}  // namespace stcpp::risk
```

### 7.2 计算步骤（算法伪代码）

```
function compute_pnl_attribution(in):
    gross = 0.0, fee = 0.0, slippage = 0.0, spread = 0.0
    per_market = map<market_id, {gross, fee, slippage, spread}>
    max_as_of_ts = 0

    for fs in in.fills:
        if fs.outcome == Pending: continue

        won = (fs.side == BuyYes && fs.outcome == YesWins) ||
              (fs.side == SellYes && fs.outcome == NoWins)

        // gross
        gross_i = won ? (1.0 - fs.fill_price) * fs.size_usdc
                      : -fs.fill_price * fs.size_usdc

        // fee (负数)
        fee_i = -in.fee_rate * fs.fill_price * fs.size_usdc

        // slippage (负数)
        slip_i = -(double(fs.slippage_bps) / 10000.0) * fs.size_usdc

        // spread (Mode A++ = 0; Mode A = -(spread_half_bps/10000) * shares)
        // shares = size_usdc / fill_price
        spd_i = -(double(fs.spread_half_bps) / 10000.0) * (fs.size_usdc / fs.fill_price)

        gross     += gross_i
        fee       += fee_i
        slippage  += slip_i
        spread    += spd_i

        per_market[fs.market_id].gross    += gross_i
        per_market[fs.market_id].fee      += fee_i
        per_market[fs.market_id].slippage += slip_i
        per_market[fs.market_id].spread   += spd_i

        max_as_of_ts = max(max_as_of_ts, fs.fill_as_of_ts_ns)

    gas = in.gas_usdc    // paper = 0.0
    net = gross + fee + gas + slippage + spread

    // 恒等式检查 (assert 或 static_assert 在单测中)
    assert |net - (gross + fee + gas + slippage + spread)| < 1e-6

    // 构造输出
    attr.gross = gross
    attr.fee = fee
    attr.gas = gas
    attr.slippage = slippage
    attr.spread = spread
    attr.net = net
    attr.as_of_ts_ns = max_as_of_ts
    for (k, pm) in per_market:
        attr.per_market.push_back({k, pm.gross + pm.fee + pm.gas + pm.slippage + pm.spread})

    return attr
```

### 7.3 LedgerStateProvider 集成方式

```
LedgerStateProvider::pnl_attribution():
    1. TradeHistoryProvider::get_fills(0, INT64_MAX)  // 全历史，v1 无 window 参数
    2. 仅保留 outcome != Pending 的 fills（已结算）
    3. gas_usdc = 0.0  // paper 阶段
    4. return PnlAttributionCalculator::compute_pnl_attribution({fills, gas})
    5. 结果不用 hub 的 pnl_gross/pnl_fee 覆盖（这是关键隔离点）
```

---

## §8 单测验收口径（T1-T7 补充实现说明）

以下对 spec-v1 §8 的 T1-T7 补充落地口径。不重复数学定义，仅说明构造方式。

### T1 — 基础闭合

构造 `PnlAttributionInput` with N 笔 settled fills（mix of BuyYes win/lose, SellYes win/lose），调用 `compute_pnl_attribution`，检查：
```cpp
EXPECT_NEAR(attr.net, attr.gross + attr.fee + attr.gas + attr.slippage + attr.spread, 1e-6);
```

### T2 — per_market 汇总

```cpp
double sum_pm = 0.0;
for (const auto& pm : attr.per_market) sum_pm += pm.net_pnl;
EXPECT_NEAR(sum_pm, attr.net, 1e-6);
```

### T3 — 空 fill 序列

```cpp
PnlAttributionInput empty_in;
auto attr = compute_pnl_attribution(empty_in);
EXPECT_NEAR(attr.gross, 0.0, 1e-10);
EXPECT_NEAR(attr.net,   0.0, 1e-10);
```

### T4 — 单笔 BuyYes win 数值校验

```cpp
FillSummary fs;
fs.market_id       = "test-market-A";
fs.side            = TradeSide::BuyYes;
fs.outcome         = SettleOutcome::YesWins;
fs.fill_price      = 0.60;
fs.size_usdc       = 1000.0;
fs.slippage_bps    = 30;
fs.spread_half_bps = 0;   // Mode A++
fs.fill_as_of_ts_ns = 1000000LL;

auto attr = compute_pnl_attribution({{fs}, 0.0, 0.03});
EXPECT_NEAR(attr.gross,    400.0, 1e-6);
EXPECT_NEAR(attr.fee,      -18.0, 1e-6);
EXPECT_NEAR(attr.slippage,  -3.0, 1e-6);
EXPECT_NEAR(attr.spread,     0.0, 1e-10);
EXPECT_NEAR(attr.gas,        0.0, 1e-10);
EXPECT_NEAR(attr.net,      379.0, 1e-6);
```

### T5 — 单笔 BuyYes lose

```cpp
// 同 T4 但 outcome = NoWins
EXPECT_NEAR(attr.gross,   -600.0, 1e-6);
EXPECT_NEAR(attr.fee,      -18.0, 1e-6);
EXPECT_NEAR(attr.slippage,  -3.0, 1e-6);
EXPECT_NEAR(attr.net,     -621.0, 1e-6);
```

### T6 — fee_rate 系数一致性

```cpp
// 验证计算器使用的 fee_rate 与 TAKER_FEE_PCT 一致
// 在 T4 基础上检查 fee / (fill_price * size_usdc) == TAKER_FEE_PCT (±1e-9)
EXPECT_NEAR(-attr.fee / (0.60 * 1000.0), stcpp::microstructure::TAKER_FEE_PCT, 1e-9);
```

### T7 — 多 market per_market 分组

```cpp
// market_A: 2 笔 fills；market_B: 1 笔 fill
// 断言:
EXPECT_EQ(attr.per_market.size(), 2u);
double sum = 0.0;
for (auto& pm : attr.per_market) sum += pm.net_pnl;
EXPECT_NEAR(sum, attr.net, 1e-6);
```

---

## §9 open questions 更新（对接 spec-v1 §10）

| # | 问题 | 状态 | 本文新增发现 |
|---|---|---|---|
| OQ-1 | TradeHistory 接口签名放哪 | 开放，48h 确认 | 建议 `TradeHistoryProvider` 独立接口，或在 `infra::wal::PositionLedger` 扩展，见 §5.2 |
| OQ-2 | `PositionRecord.realized_pnl` 含义 | **已核实**：v0.1 = 毛盈亏（开仓时 = 0，settle 时小蒋回填），不含 fee | `fill_price` 不在 WAL，FillSummary 必须在 apply_fill 时内存维护，见 §5.4 |
| OQ-3 | live gas 从链上取 | 开放，v1 paper 不阻塞 | 无新发现 |
| OQ-4 | Mode A spread 记录 | 开放，v1 paper Mode A++ spread=0 不阻塞 | 无新发现 |
| OQ-5 | 计算窗口（全历史 vs 滚动） | 开放，小梁拍板 | v1 建议：无 window 参数，全历史 `get_fills(0, INT64_MAX)` |
| **OQ-6（新）** | `pnl_gross` 语义隔离 | 需老石确认 | `LedgerFeatures.pnl_gross` ≠ `attribution.gross`；写端注释需明确"此字段含 unrealized，不等于 settled-only gross" |
| **OQ-7（新）** | FillSummary ring buffer 容量 | 需老石/老王决定 | 建议 10000 笔（~800KB），超出后 oldest 覆盖；attribution 窗口内的 fills 必须在 buffer 内 |

---

## §10 实现归属更新（对接 spec-v1 §7）

| 组件 | 提名实现方 | 交付物 |
|---|---|---|
| `FillSummary` struct + `TradeHistoryProvider` 接口 | **老石（G-LEDGER-OWNER）** | `include/stcpp/risk/fill_summary.hpp` + `include/stcpp/risk/trade_history_provider.hpp` |
| `PositionLedger::get_trade_history()` 实现（内存 ring buffer） | **老王（WAL framework）+ 老石** | `src/stcpp/infra/wal/position_ledger.cpp` 扩展 |
| `PnlAttributionCalculator::compute_pnl_attribution()` 纯函数 | **A 单元 IC（小卢 or 老王指定）** | `include/stcpp/risk/pnl_attribution_calculator.hpp` + `.cpp` |
| `LedgerStateProvider::pnl_attribution()` 集成（替换 RealStateProvider demo 路径） | **小卢（观测 IC）** | `src/stcpp/debug_api/real_state_provider.hpp` 修改，注入 `TradeHistoryProvider` |
| T1-T7 闭合单测 | A 单元 IC（与 Calculator 同） | `tests/unit/test_pnl_attribution.cpp` |
| T8 timeseries 一致性集成测试 | 小蒋（timeseries SSOT）+ 老石 | `tests/integration/test_pnl_ledger_consistency.cpp` |

---

## §11 接口冻结声明（对接 spec-v1 §9）

以下不变合约继承自 spec-v1，在本文执行层面强化：

1. `PnlAttributionCalculator` 的输入是 `FillSummary` 序列，不是 `LedgerFeatures` 聚合值
2. `attribution.gross` 仅含 `outcome != Pending` 的 settled fills，不含 unrealized
3. `LedgerFeatures.pnl_gross` ≠ `attribution.gross`；两者不可互换
4. `fee_rate = 0.03` 与 `TAKER_FEE_PCT` 常量绑定，`PnlAttributionInput.fee_rate` 默认值 = `TAKER_FEE_PCT`，禁止在调用侧硬编码 0.03 数字
5. mark price 来源恒为 `OrderBookSnapshotHub`，不允许在 `LedgerFeatures.mark_price` 写入时用本地 `now()` 替代 book ts

---

**文档结束**

— 小程 (quant-signal-research, C 量化研究部 IC #19), 2026-05-29
