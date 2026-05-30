# A1 迁移 spec: PositionLedger + VirtualFill 金额统一 micro (根治单位债真源)

owner: 老周 (架构主权)
last_review: 2026-05-30
status: 落地 spec — 派 GM 执行 (只读分析产出, 本文不改代码)
走治理: R-4 (schema/单位变更) + R-8.1#3 (ABI/字段单位变更必触发下游审计)
关联: docs/MEETINGS/2026-05-30-retro-architecture-laozhou.md, retro-synthesis.md §1 A1
前置: docs/RESEARCH/laozhou-microusd-unit-contract-spec-v1.md (MicroPUSD c1 已落)

---

## 0. TL;DR 执行摘要 (派 GM 落地)

| # | 决议 | 结论 |
|---|---|---|
| ① micro 化决策 | `risk::PositionLedger` 内部存储 + `VirtualFill.fill_size_usdc` | **裸 `std::int64_t` micro**, 不上 MicroPUSD。理由见 §1 |
| ② <1pUSD 截断 | `(int64)fill.fill_size_usdc` 把 < 1pUSD 截成 0 = 静默丢仓 | **double pUSD → micro 用 `llround(×1e6)` 单点转换**, 0.5pUSD → 500000 micro 不丢。根治点 = `risk/position_ledger.cpp:54` |
| ③ 全消费点数 | grep 全库 | **要改 5 处生产 + 4 处测试 = 9 处**; 另 ~30 处 `size_usdc` 同名不同义 (orderbook/backtest/fill_rate/serializer) **不动** (§3 标 KEEP) |
| ④ 迁移分步 | 老郭 H4: VirtualFill 冻结契约走 §10.1 单一 owner 串行 | **3 步串行 commit**, 不并行。Step1 VirtualFill 字段 → Step2 ledger 内部 → Step3 删双向 ×1e6/÷1e6。每步独立 ctest 绿 (§4) |
| ⑤ 验收 | 人工 ×1e6/÷1e6 计数 | **从 3 处 (FeedRiskGateway ×1e6 / PublishLedgerSnapshot ÷1e6 / vord /1e6) 降到 1 处** (vord→matcher 仍是 double 接口); + <1pUSD 不丢测试 + PnL 单位正确测试 + 旧 exposure gate 仍绿 (§5) |
| ⑥ ABI/R-4 等级 | VirtualFill 是否进 WAL 序列化 | **不进**: VirtualFill→PositionRecord 字段级转换 (非 memcpy), WAL ABI 零影响。VirtualFill 仅 paper-binary 内部 struct (static_assert sizeof)。**走 R-4 + ADR「内部 struct 单位变更」轻量级 (非跨 binary ABI), 老郭 review 不需全 ADR 流程** (§6) |

**一句话:** 把「whole pUSD 整数」这个孤岛抹掉 — ledger 内部和 VirtualFill 都用 micro int64, 与 RM (micro) / WAL-PositionRecord (micro) / MicroPUSD (micro) 对齐。双向补偿乘除全删, <1pUSD 仓位不再被 `(int64)` 截成 0。

---

## 1. 决策: 裸 int64 micro vs MicroPUSD 强类型

### 1.1 现状双轨 (复盘真源)

| 载体 | 当前单位 | 证据 |
|---|---|---|
| `risk::PositionView.size_usdc` | **whole pUSD 整数** (孤岛) | position_ledger.cpp:54 `(int64)fill.fill_size_usdc` |
| `risk::condition_exposure_` | whole pUSD | position_ledger.cpp:102 `+= delta_usdc` (delta = whole) |
| `VirtualFill.fill_size_usdc` | **whole pUSD double** | virtual_matcher.cpp:182 `size_usdc * p_clamped` (size_usdc 是 whole) |
| RM `condition_exposure_usdc` / `token_exposure_usdc` | **micro int64** | risk_gateway.cpp:452/465 `from_micro(cur + size_pUSD_micro)` |
| `infra::wal::PositionRecord.position_total` | **micro int64** | infra/wal/position_ledger.cpp:171 `fill_size_usdc * 1e6 + 0.5` |
| `OrderIntent.size_pUSD_micro` | **micro int64** | risk_gateway.hpp:136 |
| `cfg_.per_outcome_cap_usdc` | **MicroPUSD** | risk_gateway.hpp:307 |

**结论:** 系统主轴已经是 micro (RM / WAL / OrderIntent / cap)。只有 `risk::PositionLedger` + `VirtualFill` 是 whole-pUSD 孤岛。A1 = 把孤岛拉回主轴。

### 1.2 裸 int64 vs MicroPUSD — 推荐裸 int64

**推荐: `risk::PositionView.size_usdc` 与 `VirtualFill.fill_size_usdc` 改 `std::int64_t` micro (裸), 不上 MicroPUSD。** 理由:

1. **signed exposure 语义.** PositionView.size_usdc 是 **signed** (正多仓/负空仓/0 平仓, position_view.hpp:27)。MicroPUSD 当前为「金额」语义偏 unsigned-ish (cap 比较都正向), 把它塞进 signed 净持仓会模糊语义。exposure 累加用裸 int64 的 `+=`/`-=` 直观。
2. **VirtualFill 是 paper 热路径 + static_assert sizeof==120 锁定.** `MicroPUSD` 是 standard-layout sizeof==8 (=int64), **字节级零变**, 理论上能塞。但 VirtualFill 已有 `sizeof==120 的 static_assert`, 上 MicroPUSD 不破 sizeof — 然而**收益低**: VirtualFill 字段众多 (price/rate 仍 double), 单独一个字段上强类型不形成完整护栏, 反增序列化/测试构造摩擦。
3. **MicroPUSD 的护栏价值在「跨 cap 比较边界」, 已由 RM 侧覆盖.** 单位债咬人的地方是 RM cap 比较 (c1 spec 的初衷), 那里已上 MicroPUSD。ledger 内部是「累加 + 快照」, 不做 cap 比较, 强类型边际收益小。
4. **ABI/序列化简单.** 裸 int64 在 PositionRecord 转换、LedgerFeatures double 输出、测试构造处都是直读, 无 `.to_pusd()/.v` 噪声。

**唯一让步:** 转换点 (double pUSD ↔ micro) **统一用 `MicroPUSD::from_pusd()` 的 llround 语义** (即 `llround(x * 1e6)`), 复用 c1 已审过的 round-to-nearest 逻辑, 不要各处手写 `(int64)(x*1e6+0.5)` (正负 round 不一致, signed 减仓会偏)。可直接调 `domain::MicroPUSD::from_pusd(x).v` 取裸值, 或抽一个 `to_micro_pusd(double)` inline helper。**推荐后者** (一个 helper, 不引 MicroPUSD 进 ledger 类型签名)。

> 备选 (不推荐): 全量 MicroPUSD。留作未来若 c2-c5 把 PositionView 也纳入强类型契约时一并做, 不在 A1 范围 (A1 只根治单位债, 不扩 scope)。

---

## 2. <1pUSD 截断根治

### 2.1 现 bug (老郭挖出, 比单位漂移更脏)

```
position_ledger.cpp:54:  auto const delta_raw = static_cast<std::int64_t>(fill.fill_size_usdc);
```
`fill.fill_size_usdc` 是 whole pUSD double。`(int64)0.7 == 0` → **0.7 pUSD 的成交被静默丢成 0 仓位** (apply_fill 不报错, RM 也收不到敞口)。小额 paper 成交 (低 size × p_fill 折扣后 < 1) 直接蒸发。这不是低估 1e6, 是**整笔丢失**。

### 2.2 micro 化后是否还截断 — 不会, 但转换点要对

改后 `VirtualFill.fill_size_usdc` 本身就是 micro int64。但**产出点** virtual_matcher.cpp:182 `out.fill_size_usdc = order.size_usdc * p_clamped` 中 `order.size_usdc` 仍是 double (VirtualOrder.size_usdc 不在本次迁移范围 — 它来自 sizing/sign_req 的 double 接口)。所以转换发生在 **matcher 产出 VirtualFill 时**:

- 旧: `out.fill_size_usdc(double) = order.size_usdc(double) * p_clamped(double)`
- 新: `out.fill_size_usdc(int64 micro) = to_micro_pusd(order.size_usdc * p_clamped)`
  其中 `to_micro_pusd(x) = llround(x * 1e6)`。0.7 pUSD → `llround(700000.0) == 700000` micro。**不丢。**

`risk/position_ledger.cpp:54` 改后:
- 旧: `static_cast<int64>(fill.fill_size_usdc)` (whole double → int64, 截断)
- 新: `fill.fill_size_usdc` (已是 micro int64, **直接用, 无 cast**)。0.7pUSD=700000 micro 完整进账。

**根治判定:** 截断点从「whole double → int64」消失。唯一 double→micro 转换收敛到 **matcher 产出一处**, 用 llround (非截断)。<1pUSD 仓位以 micro 粒度 (0.000001 pUSD) 精确入账。

---

## 3. 全消费点审计 (R-8.1#3 强制, 逐个列)

> 单位标记: `[whole]`=whole pUSD, `[micro]`=micro int64, `[pUSD-dbl]`=double pUSD, `[notional]`=订单簿名义额 (与本迁移无关同名字段)

### 3.1 必改 — 生产 (5 处)

| # | 文件:行 | 当前 | 改后 | 动作 |
|---|---|---|---|---|
| P1 | `src/stcpp/risk/position_ledger.cpp:54` | `delta_raw = (int64)fill.fill_size_usdc` `[whole→截断]` | `delta_raw = fill.fill_size_usdc` (已 micro) `[micro]` | **删 cast** — 截断根治点 |
| P2 | `src/stcpp/execution/virtual_matcher.cpp:95` | `out.fill_size_usdc = order.size_usdc * rate01` `[whole-dbl]` | `out.fill_size_usdc = to_micro_pusd(order.size_usdc * rate01)` `[micro]` | 加 llround 转换 (Match 路径) |
| P3 | `src/stcpp/execution/virtual_matcher.cpp:182` | `out.fill_size_usdc = order.size_usdc * p_clamped` `[whole-dbl]` | `out.fill_size_usdc = to_micro_pusd(order.size_usdc * p_clamped)` `[micro]` | 加 llround 转换 (MatchWithBook 路径); :64/:88/:132/:137/:153 的 `=0.0` → `=0` (类型对齐) |
| P4 | `src/stcpp/paper/paper_loop.cpp:715-721` FeedRiskGateway | `whole_pusd * kMicroPerPusd` (×1e6) `[whole→micro]` | `set_condition_exposure(cid, micro_pusd)` **删 ×1e6** (ledger 已 micro) | **删补偿乘** |
| P5 | `src/stcpp/paper/paper_loop.cpp:659` PublishLedgerSnapshot | `net_qty = (double)pv.size_usdc / 1e6` 注释写"signed micro"但实存 whole → **÷1e6 把 whole 当 micro = PnL 低估 1e6 (已躺 main)** | `net_qty = (double)pv.size_usdc / 1e6` **保留**, 但现在 pv.size_usdc **真是 micro** → 除对了, **bug 自然消** | **改注释不改式子** (÷1e6 现在语义正确) |

> **P5 关键 (回答你的 ÷1e6 bug 自然消?):** 是。代码 `÷1e6` 本来就是对 micro 的正确换算, bug 在于「数据实际是 whole」。micro 化后数据对了, 这行无须改 (式子保留), 只把误导性注释 "size_usdc (signed micro)" 从「谎言」变「真话」。**这是为什么 PnL 低估 1e6 会自愈** — 不是删除, 是数据修正后式子归位。

### 3.2 必改 — 类型声明 (2 处, 含在 Step1)

| # | 文件:行 | 当前 | 改后 |
|---|---|---|---|
| T-decl1 | `include/stcpp/execution/virtual_matcher.hpp:110` | `double fill_size_usdc{0.0}` | `std::int64_t fill_size_usdc{0}` (micro) + 更新注释 + **重算 static_assert sizeof** (§6.1) |
| T-decl2 | `include/stcpp/risk/position_view.hpp:34` | `std::int64_t size_usdc{0} // signed cent` | 保持 int64, 注释改 `// signed micro pUSD` (字段类型不变, 仅语义/注释; **下游 W76 测试期望值要 ×1e6**) |

### 3.3 必改 — 下游 double 消费 (检查, 多数自愈)

| # | 文件:行 | 当前 | micro 化后 |
|---|---|---|---|
| C1 | `src/stcpp/paper/paper_loop.cpp:672` pnl_fee | `fill.fill_size_usdc * fee * price *(1-price)` `[whole-dbl]` | fill_size 现 micro int64 → **必须 `/1e6` 转回 pUSD** 再算 fee (否则 fee ×1e6)。改 `(double)fill.fill_size_usdc/1e6 * ...` |
| C2 | `src/stcpp/paper/paper_loop.cpp:576/598` | `fill.fill_size_usdc <= 0.0` / `%.4f` 打印 | `<= 0` (int 比较 ok); 打印 `%.4f` → 需 `/1e6` 或改 `%lld` (显示问题, 非红线) |
| C3 | `src/stcpp/ml/hook.cpp:139` | `partial.filled_size_usdc = fill.fill_size_usdc` → training_label.hpp:82 `double filled_size_usdc` | training_label 是 **double pUSD** (ML 特征, 不应 micro)。改 `= (double)fill.fill_size_usdc / 1e6` 转回 pUSD-dbl。**否则 ML 训练标签 ×1e6 漂移** (隐患, 必改) |
| C4 | `src/stcpp/infra/wal/position_ledger.cpp:171` | `(int64)(fill.fill_size_usdc * 1e6 + 0.5)` `[whole-dbl→micro]` | fill_size 现 micro → **删 ×1e6**: `fill_size_micro = fill.fill_size_usdc` (已 micro) |

> C3/C4 是**新发现的下游隐患** — 不在你列的 6 点里, 但 R-8.1#3 审计逼出来。C4 是第二个 PositionLedger (WAL 版, `infra::wal` namespace) 也消费 fill_size_usdc, 同样 ×1e6, 必须同步删。**这是 A1 不能只改 risk:: 一个 ledger 的原因 — 两个同名 PositionLedger 都吃 VirtualFill。**

### 3.4 必改 — 测试 (4 处期望值 + 构造)

| # | 文件:行 | 改动 |
|---|---|---|
| TST1 | `tests/unit/test_position_ledger_w76.cpp:80,106,115,125,130,135,143` | `make_fill(...,200.0)` 现 200.0 是 micro=200 → 期望 `size_usdc==200` 改为构造 `200'000'000` (200 pUSD) 或 helper 内 `to_micro`; VWAP 数值同步 |
| TST2 | `tests/unit/test_position_ledger.cpp:188-190,222` | `position_total == 10'000'000` (=10pUSD micro) — WAL 版本来对; 但 fill 构造 `fill_size_usdc=10.0` 现是 10 micro → 构造改 `10'000'000`, C4 删 ×1e6 后 total 仍 10e6 ✓ |
| TST3 | `tests/unit/test_paper_loop.cpp:1024` | `fill.fill_size_usdc = 45.0 // whole pUSD` → `45'000'000` (45 pUSD micro); FeedRiskGateway 删 ×1e6 后仍喂 45e6 micro → gate 期望不变 (§5 验收) |
| TST4 | `tests/unit/test_ml_hook.cpp:225,319,339` + `test_virtual_matcher.cpp` + `test_fill_rate_model_v2.cpp` | matcher 测试 `EXPECT_NEAR(fill.fill_size_usdc, order.size_usdc * rate)` → 改 `order.size_usdc*rate*1e6`; ml_hook 期望 filled_size_usdc 经 /1e6 转回 (C3) |

### 3.5 不动 — KEEP (同名不同义, ~30 处)

以下 `size_usdc` / `*_size_usdc` 是**订单簿名义额 / backtest notional / fill_rate 深度**, 与仓位金额无关, **绝不改** (改了反造新债):

- `microstructure/orderbook.hpp` + `fill_rate_model.*` 的 `Level.size_usdc` `[notional 深度 double]` — 订单簿挂单量
- `numerical/slippage_model.hpp` `order_size_usdc` / `book_depth_l1_usdc` `[notional]`
- `backtest/*` (`types.hpp` / `event_replayer.hpp` / `stats.hpp` / `metrics.hpp`) `Trade.size_usdc` `[pUSD-dbl]` — 回测内部, 自洽双轨
- `data/feature_store_contract.hpp` `bid/ask_size_usdc[]` `[notional]`
- `polymarket/pm_client.hpp` `size_usdc_micro` `[已 micro]` — 已对
- `net/outbound_serializer.*` `size_usdc_micro` `[已 micro]`
- `strategy/signal_iface.hpp` `suggested_size_usdc` `[cent, 另一轨]` + `p0_01_*.cpp`
- `execution/virtual_matcher.hpp` `VirtualOrder.size_usdc` / `VirtualOrderWithBook.size_usdc` `[pUSD-dbl 输入]` — **保持 double** (sizing 接口), 仅在产出 VirtualFill 时转 micro
- `risk/rm_debug_snapshot.hpp` `size_usdc` — 已是 `micro/1e6` 的 debug 视图, 对

> **审计结论:** 真正要碰的金额仓位轴 = `VirtualFill.fill_size_usdc` + `PositionView.size_usdc` + 两个 `PositionLedger::apply_fill` + paper_loop 三处补偿 + ml_hook/training_label。**共 9 生产改动点 (P1-5 + C1-4) + ~12 测试期望调整**。其余同名字段是噪声, R-8.1#3 审计的价值正在于把它们识别为 KEEP, 不误伤。

---

## 4. 迁移分步顺序 (老郭 H4: VirtualFill 单一 owner 串行, 不并行)

> 红线 §10.1#2: VirtualFill 是被多模块 include 的冻结契约文件 (virtual_matcher.hpp), **禁止多 agent 并行加/改字段**。本迁移全程 GM 单 owner 在 main 串行 (§10.2 新模式), 3 个独立 commit, 每步 ctest 全绿才进下一步。

### Step 0 (前置, 同 commit 或独立小 commit): 落 helper
- 加 `to_micro_pusd(double) -> int64` inline (复用 `MicroPUSD::from_pusd(x).v` 的 llround) 到 execution 或 domain 公共头。
- 回归门: 仅编译, 无行为变化。

### Step 1: VirtualFill 字段 double→int64 micro + 所有产出/消费点同步 (1 commit)
**改:** T-decl1 (字段类型 + sizeof static_assert) → P2/P3 (matcher 产出 to_micro) → C1/C2/C3/C4 (paper_loop pnl_fee/打印 + ml_hook + WAL ledger 删×1e6) → TST2/TST4 (matcher/ml_hook/WAL 测试期望)。
**为什么打包:** VirtualFill 字段类型一变, **所有读它的点同 commit 必须一起改** (否则编译失败 / 单位错)。这是「冻结契约改动必须原子」的体现 — 不能留半套。
**回归门:** `cmake --build` 干净 + `ctest` 全绿 (虚拟撮合 / ml_hook / WAL ledger / paper_loop 编译通过)。**此步后 `risk::PositionLedger` 仍存 whole** (P1 还没改) — 但因 apply_fill:54 现在收到的是 micro int64, `(int64)micro` 不再截断 (micro 本就是 int64), 只是 size_usdc 值变成了 micro 量级。**注意: 此步 risk ledger 会短暂「存 micro 值但 FeedRiskGateway 仍 ×1e6」→ exposure ×1e6 过大**。故 Step1 与 Step2 之间**不可发布**, 但可独立 commit (编译/单测绿即可, P0-1 gate 测试在 Step2 修期望)。

> 若担心 Step1 中间态 P0-1 gate 测试红: 可把 P4 (删 FeedRiskGateway ×1e6) + TST3 期望并入 Step1 末尾, 让 Step1 自洽。**推荐合并** — 见 Step2 调整。

### Step 2: risk::PositionLedger 内部 micro + FeedRiskGateway 删 ×1e6 + W76 测试 (1 commit)
**改:** P1 (apply_fill 删 cast) → P4 (FeedRiskGateway 删 ×1e6) → position_view 注释 (T-decl2) → TST1/TST3 (W76 + paper_loop gate 期望 ×1e6)。
**回归门:** ctest 全绿, **重点 P0-1 ExposureRedLine_UnitGate 绿** (§5)。此步后 risk ledger 全 micro, RM 喂数对齐, 无补偿乘。

### Step 3: PublishLedgerSnapshot 注释归位 + PnL 单位验收测试 (1 commit)
**改:** P5 (÷1e6 保留, 注释从谎言改真话) + 新增 PnL 单位正确测试 (§5) + <1pUSD 不丢测试。
**回归门:** ctest 全绿 + 新增 2 个验收测试绿。

> **Step1+2 可合并为一个大 commit** (若 GM 判断中间态不可发布风险高): VirtualFill + 两个 ledger + FeedRiskGateway 一次原子改完, 一次 ctest。代价是 diff 大、回滚粒度粗。**老周建议: Step1 末尾合入 P4+TST3 让每步自洽, 维持 3 commit** (回滚粒度细, 符合 §10.1 可独立 commit 边界)。

---

## 5. 验收口径 (怎么证明根治了)

### 5.1 人工补偿乘除计数 (核心 KPI)
| 链路点 | 迁移前 | 迁移后 |
|---|---|---|
| FeedRiskGateway `×1e6` (whole→micro RM) | 1 (P4) | **0** (删) |
| PublishLedgerSnapshot `÷1e6` | 1 (P5, 但语义错=低估) | **1, 语义正确** (micro→pUSD-dbl 合法换算, 非补偿) |
| vord.size_usdc `/1e6` (paper_loop:560) | 1 | **1, 保留** (sign_req.size_pUSD_micro→VirtualOrder double 接口, 合法跨边界, **不在本迁移消除范围**) |
| WAL ledger `×1e6` (C4) | 1 | **0** (删) |
| pnl_fee / ml_hook `/1e6` (C1/C3) | 0 | **+2** (micro→pUSD-dbl 合法换算, 新增但正确) |

**判定:** 「**补偿性**」乘除 (为掩盖单位漂移而存在的 ×1e6) **从 2 处 (FeedRiskGateway + WAL) 降到 0**。剩余乘除全是「**合法跨边界换算**」(micro 存储 ↔ double pUSD 算法/特征/外部接口), 是设计内允许的, 不算债。这是根治的量化证据。

### 5.2 必加测试 (Step3)
1. **<1pUSD 不丢:** `apply_fill` 一笔 `fill_size_usdc = 700'000` (0.7 pUSD micro) → `get_position().size_usdc == 700'000` (旧代码会得 0)。**直接验老郭挖的截断 bug 已死。**
2. **PnL 单位正确:** 构造 1 笔 fill (e.g. 100 pUSD micro @0.6, mark 0.65) → PublishLedgerSnapshot → `LedgerFeatures.net_qty == 100.0` (pUSD, 不是 1e8), `pnl_unrealized == (0.65-0.6)*100 == 5.0` (不是 5e6 或 5e-6)。**验 1e6 低估已消。**
3. **matcher 产出 micro:** `MatchWithBook` order.size_usdc=200.0, rate=0.6 → `fill.fill_size_usdc == 120'000'000` (120 pUSD micro), 非 120.0。
4. **ml 标签不漂:** ml_hook → `training_label.filled_size_usdc` 仍是 pUSD-dbl (e.g. 120.0), 非 1.2e8。

### 5.3 旧测试仍绿 (回归)
- **P0-1 ExposureRedLine_UnitGate (test_paper_loop.cpp:1004):** 这是老周 P0 gate。改后: fill 45 pUSD micro (`45'000'000`) → FeedRiskGateway **不再 ×1e6** 直接喂 45e6 micro → +10e6 = 55e6 > 50e6 cap → 仍触 `EXCEED_CONDITION_EXPOSURE`。**期望不变, 单位路径变** (TST3 改构造值 45.0→45'000'000, 删测试里对 ×1e6 的注释依赖)。**绿 = exposure 红线迁移后未被静默架空。**
- W76 TC-01 / position_ledger T1-T5 / virtual_matcher / fill_rate_model_v2 / ml_hook 全绿 (期望值已按 §3.4 调)。

---

## 6. R-4 治理 + ABI

### 6.1 VirtualFill 是否进 WAL 序列化 (ABI 影响) — 不进, 内部 struct

**审计结果: VirtualFill 不被 memcpy 进任何 WAL/网络帧。** 证据:
- `infra/wal/position_ledger.cpp:112 _build_record` 是**字段级转换** `VirtualFill → PositionRecord` (逐字段读 fill.market_id/outcome/4ts/fill_size_usdc, 算出 PositionRecord 再落 WAL), **非 struct 整体序列化**。
- VirtualFill 是 **paper-binary 进程内 struct** (R-7: live binary 不 link), 不跨 binary、不跨网络。`sizeof(VirtualFill)==120 的 static_assert` 是防意外 padding 的内部锁, **非跨 ABI 契约**。

**fill_size_usdc double(8B)→int64(8B): 同宽**, sizeof 不变 (仍 120), offset 不变, padding 不变。`sizeof==120 的 static_assert` **无需改数值**, 但注释要更新 (字段类型变)。**ABI 零影响。**

### 6.2 R-4 等级 (走老郭哪个流程)

- **R-4 (schema/单位静默变更红线)** 适用: `fill_size_usdc` 单位 whole-pUSD-double → micro-int64 + `PositionView.size_usdc` 语义 whole→micro。R-8.1#3 强制「字段单位变更必 audit 全下游」→ **本文 §3 即该 audit, 满足 R-4 配套要求**。
- **ADR 等级: 轻量级 (内部 struct 单位统一), 非「重大技术栈/跨 binary ABI 变更」。** 理由: (a) 不跨 binary; (b) sizeof/offset 零变; (c) 是「消除既有单位债」而非引入新契约。**建议: 老郭 review 走「单位迁移 ADR 附录」或并入 MicroPUSD 单位契约 ADR (laozhou-microusd-unit-contract-spec) 的 c-series 续章, 不另起完整 ADR 评审流程。** 老郭有架构否决权, 终裁在他。
- **下游通知 (§7 最小惊喜 + R-4):** 通知消费 VirtualFill / PositionView / LedgerFeatures 的 owner —
  - 小蒋 (VirtualMatcher) / 老沈 (risk PositionLedger) / 小程 (ML hook→training_label, fill_size 单位) / 老韩 (RM exposure 喂数路径) / 小余 (若 parquet_writer 落 filled_size_usdc)。
  - parquet_writer.hpp:128/172 `filled_size_usdc` 来自 training_label (pUSD-dbl), C3 保证它仍是 pUSD-dbl → **数据落盘单位不变**, 但 owner 须知会 (R-4 不静默)。

### 6.3 风险登记
| 风险 | 缓解 |
|---|---|
| Step1 中间态 exposure ×1e6 过大 (ledger micro 但 FeedRiskGateway 还 ×1e6) | Step1 末尾合入 P4 让每步自洽, 不发布中间态 |
| 两个同名 PositionLedger 只改一个 | §3.3 C4 显式列 WAL 版; Step1 同 commit 改两个 |
| ml/parquet 标签 ×1e6 漂移 (隐性) | C3 显式 /1e6 转回 + TST4/5.2#4 测试钉死 |
| `(int64)` round 方向: signed 减仓 llround vs 截断 | 转换统一 llround (MicroPUSD::from_pusd 语义), 不手写 `+0.5` (负值会错) |

---

## 附: 关键文件路径
- 改: `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/execution/virtual_matcher.hpp` (VirtualFill 字段 + sizeof 注释)
- 改: `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/execution/virtual_matcher.cpp` (:95 :182 产出 to_micro)
- 改: `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/risk/position_ledger.cpp` (:54 删 cast — 截断根治)
- 改: `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/risk/position_view.hpp` (:34 注释 whole→micro)
- 改: `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/paper/paper_loop.cpp` (:659 注释 / :672 C1 / :715-721 P4 删×1e6)
- 改: `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/infra/wal/position_ledger.cpp` (:171 C4 删×1e6)
- 改: `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/ml/hook.cpp` (:139 C3 /1e6)
- 测试: test_position_ledger_w76 / test_position_ledger / test_paper_loop / test_virtual_matcher / test_fill_rate_model_v2 / test_ml_hook
- 参: `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/domain/micro_pusd.hpp` (from_pusd llround 复用)
