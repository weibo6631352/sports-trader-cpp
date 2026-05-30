# 二元市场双边快照 + per-condition 决策 架构设计 v1

> owner: 老周 (系统工程部主管 + 架构主权, A)
> last_review: 2026-05-31
> 状态: 设计草案 (待架构评审 / 老郭 + 小袁 + 小梁 会签)
> 边界: 老周定结构 (快照 / 时序 / 签名 / 入参契约 / R-12·R-20 合规); 小袁定微观结构选边; 小梁定策略选边。本 doc 不含决策逻辑。
> 不改主干 — 只读分析 + 设计。

---

## 0. 老板设计原则 (逐字, 不转述 — §8.1 红线治理纪律 #1)

> 「一个二元市场应该既持有 yes 的快照，也持有 no 的快照，触发 WSS 订阅时，只刷新单边的，这个没问题。但是进入决策时，我们一定要带入这个盘口的信息，而不仅仅是单单一边的信息。」

拆成三条工程约束:
- **C1 (存储)**: 一个 binary condition 既持 YES 快照又持 NO 快照。
- **C2 (刷新)**: WSS 触发只刷单边 — 这是既定事实,不改 (老板明确"没问题")。
- **C3 (决策)**: 决策入参必须带**整个盘口** (YES + NO 两边),不是单边。

---

## 1. 现状确认 (读码结论)

| 维度 | 现状 | 是否满足老板原则 |
|---|---|---|
| 存储 | `OrderBookSnapshotHub` 按 `token_id` keyed, YES token 与 NO token **各有独立 TokenSlot**, WSS 各自 `Publish`。两边快照**已经都在 hub 里** | C1 ✅ 已满足 (无需新存储后端) |
| 刷新 | WSS event loop 每事件只 `Publish(单个 token)` | C2 ✅ 已是单边刷新 |
| 决策 | `TickAll()` 只对 token0(YES) 调 `TickOne`; token1(NO) 仅 `hub_.Read(token1)` 取一个标量 `no_token_mid` 做 de-vig (P1-8)。**NO 的完整 book 从不进决策** | C3 ❌ **违反** — 决策只带单边 + NO 的一个 mid 标量 |
| 选边 | 硬编码 YES 多头: `token_side="YES"`(:360)、`outcome=Yes`(:511,621)、`side=Buy`(:512) | C3 ❌ 不能选边 |

**核心结论: 存储层 (hub) 已满足老板要求,缺口全在「决策读取层」。** 不需要重写 hub,需要在 `paper_loop` 决策入口把"读单边"改成"读双边、组 market snapshot、传给决策"。

**关键发现 (降低改造面):** `SizingInput` 已有 `buy_yes` 方向字段,`SizingCalculator` 注释明确"仅用 `edge_ci_lower` 绝对值算幅度,方向在调用方决定"。→ **sizing 层本就方向中立,选边逻辑天然在调用方 (paper_loop / 策略层)。** 双边改造不动 sizing。

---

## 2. 双边快照结构设计 (回答提问 1)

### 2.1 结论: 不在 hub 加 condition 级聚合存储,在决策侧组「值聚合 view」

理由 (R-12 主权裁定):
- hub 的 per-token double-buffer 是**正确的并发原语** — WSS 单边刷新天然映射到 per-token 单 slot 的 atomic flip。若改成 condition 级聚合 slot (YES+NO 同 slot),单边刷新就要 read-modify-write 整个 condition slot → 破坏"单边刷新不碰对边"的无锁性,反而引入 writer 内部读对边的耦合。**这是退步,否决。**
- 两边快照的"绑定关系"是**静态映射** (`condition_id → {yes_token, no_token}`),不是动态数据。映射已存在 `token_map_` (`condition_id → pair<yes,no>`)。决策侧按映射各读一次即可,无需新存储抽象。

### 2.2 决策侧新结构: `BinaryMarketSnapshot` (值类型, 决策线程栈上组装)

新增一个**纯值聚合 view** (非存储, 非 hub 成员),决策线程每 tick 在栈上组装:

```cpp
// include/stcpp/paper/binary_market_snapshot.hpp  (新文件, owner 老周)
namespace stcpp::paper {

// 一侧的快照 + 可用性 (决策入参用; 不是存储)
struct SideView {
    bool present{false};                         // hub.Read 命中 + valid
    polymarket::clob_wss::OrderBookFeatures book{};  // 该 token 完整 5 档 + 微观 + 4ts
    // 便捷访问 (转发 book, 决策侧少写 .book.)
    [[nodiscard]] double best_bid() const noexcept { return book.best_bid(); }
    [[nodiscard]] double best_ask() const noexcept { return book.best_ask(); }
    [[nodiscard]] double microprice() const noexcept { return book.microprice; }
    [[nodiscard]] std::int64_t data_source_ts_ns() const noexcept { return book.data_source_ts_ns; }
};

// 一个 binary condition 的整盘口 (YES book + NO book), 决策线程栈上组装
struct BinaryMarketSnapshot {
    std::string condition_id;
    std::string yes_token_id;
    std::string no_token_id;
    SideView yes;   // present=false 表示该侧 hub 无快照 / invalid
    SideView no;    // 单边可用即可决策 (退化), 双边可用走完整双边逻辑

    // 至少有一侧可决策 (fail-closed: 两边都缺 → 跳过)
    [[nodiscard]] bool any_side_present() const noexcept { return yes.present || no.present; }
    [[nodiscard]] bool both_sides_present() const noexcept { return yes.present && no.present; }
};

}  // namespace stcpp::paper
```

设计要点:
- **零存储、零锁** — 纯值类型,决策线程 `TickAll` 内部由两次 `hub_.Read()` 填充。hub 的 SWMR 原子读保持不变 (R-12)。
- **YES/NO 各持完整 `OrderBookFeatures`** — 不再只取 NO 的 mid 标量。NO 的 5 档深度 / microprice / imbalance / 4ts 全部进决策。满足 C3。
- **`present` 显式标记单边缺失** — 一边刚刷一边陈旧/缺失时,决策侧能判断哪边可信 (见 §3 时序)。

---

## 3. WSS 单边刷新 × 决策读双边 的时序 (回答提问 2)

### 3.1 时序 (不变 + 新增读)

```
WSS io_thread (vCPU0, 单边刷新, 不变):
  OnEvent(yes_token) → hub_.Publish(yes_token, feat_yes)   // 只刷 YES slot
  OnEvent(no_token)  → hub_.Publish(no_token,  feat_no)    // 只刷 NO slot
  (两边各自独立 atomic flip; 互不阻塞; C2 保持)

loop_thread (决策, 改造点):
  for each condition in token_map_:
    yv = hub_.Read(yes_token)    // atomic acquire, 拷 YES 最新 front
    nv = hub_.Read(no_token)     // atomic acquire, 拷 NO  最新 front
    snap = BinaryMarketSnapshot{... yv, nv}   // 栈上组装, 零锁
    TickOne(snap)                // 决策带整盘口
```

### 3.2 两边 ts 不同步如何处理 — **决策入参带两边各自 4ts,下游判 freshness**

老板原则核心:"带入盘口信息"。两边 ts 必然不同步 (单边刷新 → 一边新一边旧)。**架构侧的职责: 把两边各自的 4ts 原样透传进决策,让决策层 (小袁/小梁) 自己判哪边可信,不在架构层替决策层做"取哪边 ts"的归一化。**

理由 (R-20 + 边界):
- R-20 4ts 契约是 **per-data-source** 的。YES book 和 NO book 是**两个独立 WSS 流的两条 4ts 链**,各自满足 `event ≤ data_source ≤ ingestion ≤ as_of`。**强行合并成一条 4ts 会丢失"哪边陈旧"的信息,违背 R-20 透传精神。**
- 因此 `BinaryMarketSnapshot` 让 `yes.book` 与 `no.book` **各自保留独立完整 4ts**。决策层读到两条链,自己算 `staleness_yes = now - yes.data_source_ts`、`staleness_no = now - no.data_source_ts`。
- **架构层只给一个 fail-closed 兜底**: 若两边都缺 (`any_side_present()==false`) → 跳过该 condition (现状已有同类 fail-closed)。单边可用 → 进决策,由决策层按 freshness 决定退化策略 (小袁负责)。

### 3.3 R-20 覆盖两边 (合规结论)

- 现状: `TickOne` 把单边 `feat` 的 4ts 透传进 `game_row` / `book_row` / `intent` / `quote`。
- 改造后: 决策**选哪边下单,intent 的 4ts 就透传那边 book 的 4ts** (data_source_ts 仍来自被交易 token 的 hub 快照,禁 now() 替代 — R-20 不变)。de-vig 用对边 book 时,对边 4ts 仅用于 freshness 判断 (不写进 intent 的 4ts 链,因为 intent 的成交标的是被选中那一边)。
- **新增一条 R-20 校验**: 若决策用了双边数据 (e.g. de-vig 用 NO book),且对边 book 陈旧超阈值 → de-vig 退化 (现状 `devig_binary` 已支持单边退化),freshness 由决策层 gate。这条**不改 R-20 红线条文**,是 R-20 在双边场景的应用 SOP。

---

## 4. TickOne 结构改造 (回答提问 3)

### 4.1 新签名

```cpp
// 旧: 单边 + NO mid 标量
void TickOne(const std::string& condition_id, const std::string& token_id,
             const OrderBookFeatures& feat, double no_token_mid);

// 新: per-condition, 带整盘口
void TickOne(const BinaryMarketSnapshot& mkt);
```

### 4.2 数据流

```
TickAll():
  for [cond_id, {yes_tok, no_tok}] in token_map_:
    yv = ReadSide(yes_tok)   // hub_.Read + 填 SideView (present / book)
    nv = ReadSide(no_tok)
    if (!yv.present && !nv.present) { hub_reads_empty++; continue; }  // fail-closed
    BinaryMarketSnapshot mkt{cond_id, yes_tok, no_tok, yv, nv};
    TickOne(mkt);

TickOne(mkt):
  // Step 0 [新]: 选边 (DecisionSide) — 调决策层 (§5 入参契约)
  //   M1: 退化为"恒选 YES 多头" (与现状等价, 但入参已是双边, 结构就位)
  //   M2: 真正双边选边 (小袁微观结构 + 小梁策略), 含 buy NO / sell-to-open
  DecisionSide side = SelectSide(mkt);   // 见 §5; M1 桩: 恒 {YES, Buy}
  if (side == None) return;              // 两边都不值得 → 跳过

  // Step 1: 按选中边取该边 book 作 traded_book; 对边 book 作 ref (de-vig / 微观)
  const SideView& traded = (side.outcome==YES) ? mkt.yes : mkt.no;
  const SideView& opposite = (side.outcome==YES) ? mkt.no : mkt.yes;

  // Step 2..8: 现有流程不变, 但:
  //   - fair / de-vig 用 traded + opposite 两边 (de-vig 已支持)
  //   - intent.outcome / side / token_id / price 来自 side + traded.book (不再硬编码 YES/Buy)
  //   - intent 4ts 来自 traded.book (R-20)
```

### 4.3 M1 关键约束: **结构改造 ≠ 开放空头**

- M1 的 `SelectSide` 是**桩**: 恒返回 `{outcome=YES, side=Buy}` (与现状行为**完全等价**)。
- 但 `TickOne` 入参已经是 `BinaryMarketSnapshot` (双边),Step 1 已按"选中边/对边"取 book,intent 字段已**参数化** (不再字面量 `"YES"` / `Outcome::Yes` / `Side::Buy`)。
- **意义**: M1 把"读双边 + 整盘口入决策 + 字段参数化"的**结构地基**铺好 (满足老板 C3),把"真正选边/空头"的**决策逻辑**留给 M2。结构与策略解耦,M2 接策略时不再动 paper_loop 骨架。

---

## 5. 决策入参契约 (回答提问 4 — 与小袁/小梁接口边界)

### 5.1 边界划分 (老周定结构, 小袁/小梁定逻辑)

| 谁 | 定什么 |
|---|---|
| **老周 (本 doc)** | `BinaryMarketSnapshot` 结构 / `DecisionSide` 枚举 / `SelectSide` 接口签名 / 两边 book 各自 4ts 透传 / R-12·R-20 合规 |
| **小袁 (微观结构选边)** | `SelectSide` 内部: 用两边 best_bid/ask/depth/microprice/imbalance 判哪边有微观结构 edge (e.g. 哪边 microprice 偏离 de-vig fair 更多、哪边 book 更厚可成交) |
| **小梁 (策略选边)** | fair value 方向 → 选 buy YES vs buy NO; Kelly sizing 方向 (`buy_yes` 已是 SizingInput 字段); sell-to-open 准入 (M2) |

### 5.2 传给决策层的结构 (`SelectSide` 入参)

`SelectSide(const BinaryMarketSnapshot& mkt)` 已含决策层需要的**两边全部信息**:

| 字段 (每边各一份) | 来源 | 给谁 |
|---|---|---|
| `best_bid / best_ask` | `book.best_bid()/best_ask()` | 小袁 (微观) + 小梁 (执行价) |
| `best_bid_size / best_ask_size` (L1 depth) | `book.best_bid_size()/best_ask_size()` | 小袁 (可成交深度) |
| 5 档 `bids[]/asks[]` | `book.bids/asks` | 小袁 (深度选边, M2) |
| `microprice` | `book.microprice` | 小袁 (微观锚) + 小梁 (mark) |
| `imbalance` | `book.imbalance` | 小袁 (订单流不平衡) |
| `mid / spread` | `book.mid/spread` | 小袁 |
| **4ts (每边独立)** | `book.{event/data_source/ingestion/as_of}_ts_ns` | 小袁 (freshness 选边) + R-20 |
| `present` | `SideView.present` | 单边退化判断 |

### 5.3 `SelectSide` 输出契约

```cpp
enum class SideOutcome : std::uint8_t { None, Yes, No };
struct DecisionSide {
    SideOutcome outcome{SideOutcome::None};   // None = 不交易
    strategy::Side side{strategy::Side::Buy}; // M1 恒 Buy; M2 开放 Sell(空头)
    // 决策层填: 选这边的理由 (audit / quote 展示)
    double conviction{0.0};                   // 选边置信 (小袁/小梁 填)
};
```

- **M1**: `SelectSide` 返回 `{Yes, Buy}` 恒定桩 (paper_loop 内联,不调外部决策层,零行为变化)。
- **M2**: `SelectSide` 委托给小袁的微观结构选边 + 小梁的策略选边 (signal_iface 扩展),真正双边。

**sizing 不变**: `SizingInput.buy_yes` 已是现成字段,`SelectSide` 输出的方向喂给它即可。`SizingCalculator` 方向中立 (用 `edge_ci_lower` 绝对值),无需改。

**RM 不变**: `OrderIntent.outcome/side/token_id/price` 由 `DecisionSide` + `traded.book` 填,RM 按 intent 字段评估 (per-outcome cap 已按 token_id keyed,buy NO 自然走 NO token 的 cap,无需改 RM)。

---

## 6. R-12 / 线程 / 性能 (回答提问 5)

| 维度 | 结论 |
|---|---|
| **加锁?** | **不加。** 双边读 = 两次 `hub_.Read()` (atomic acquire + POD value copy)。hub SWMR 原语不变。`BinaryMarketSnapshot` 栈上值组装,无锁无 malloc。 |
| **每 tick 多读一个 book?** | **可接受。** 现状每 tick 已读 2 次 (`Read(token0)` + de-vig 的 `Read(token1)`)。改造后仍是 2 次 (YES + NO),只是 NO 从"读完取 mid 标量"变成"读完留完整 POD"。`OrderBookFeatures` 是 trivially_copyable POD (~200B),value copy < 500ns (hub 注释实测)。**净增延迟 ~0** (本就读了两次,只是不再丢弃 NO 的其余字段)。 |
| **R-12 (WSS loop)** | **不触碰。** 全部改动在 `loop_thread_` (决策线程),WSS io_thread 的 Publish 路径**一行不动**。决策侧读 hub 仍是只读 atomic,不阻塞 WSS。 |
| **R-12 (100us)** | 决策线程非 WSS event loop,不受 100us 约束;但即便算上,双读 < 1us,远低于。 |
| **token_map 遍历** | N condition × 2 读/condition = 2N atomic read/tick。N 当前 ≤ 200,400 次 atomic read/tick < 200us,500ms tick 间隔下占比 < 0.04%。无压力。 |

**性能裁定: 双边读零额外成本** (本就读两次),纯结构收益。

---

## 7. 改动面评估 + M1/M2 边界 (回答提问 6, 诚实标)

### 7.1 M1 必须 (结构地基 — 满足老板 C3 的最小集)

| # | 改动 | 文件 | 工程量 | 风险 |
|---|---|---|---|---|
| M1-1 | 新增 `binary_market_snapshot.hpp` (`SideView`/`BinaryMarketSnapshot`/`DecisionSide`/`SideOutcome`) | 新文件 | 小 (纯 POD 头) | 低 |
| M1-2 | `TickAll` 改: 双边 `ReadSide` → 组 `BinaryMarketSnapshot` → `TickOne(mkt)` | paper_loop.cpp | 中 | 低 (读路径) |
| M1-3 | `TickOne` 改签名为 `TickOne(const BinaryMarketSnapshot&)`; 内部 `SelectSide` 桩 (恒 YES/Buy); 字段参数化 (删硬编码 `"YES"`/`Yes`/`Buy` 字面量,改从 `DecisionSide`+`traded.book` 取) | paper_loop.cpp/.hpp | 中 | **中** (行为必须与现状逐位等价,需回归) |
| M1-4 | de-vig 改用 `opposite.book` 完整快照 (现状已用 NO mid;改成从 `opposite.book` 取 mid,语义不变,但来源是结构化 SideView) | paper_loop.cpp | 小 | 低 |
| M1-5 | 单边缺失 fail-closed (`any_side_present()`) + 两边各自 4ts 透传校验 | paper_loop.cpp | 小 | 低 |
| M1-6 | 回归测试: 验"M1 桩恒选 YES/Buy"行为与改造前**逐位等价** (同 fill / 同 reject / 同 quote) | test_paper_loop | 中 | — (必须做) |

**M1 验收**: 改造后跑 paper daemon,fills/rejects/quote 输出与改造前**完全一致** (因为 `SelectSide` 是恒 YES/Buy 桩)。证明"结构换了、行为没变"。这是结构改造正确性的硬门禁。

### 7.2 M2 可延 (真正双边决策逻辑)

| # | 延后项 | 谁 | 为什么能延 |
|---|---|---|---|
| M2-1 | `SelectSide` 真实双边选边 (buy YES vs buy NO) | 小袁 (微观) + 小梁 (策略) | M1 桩已让结构就位,逻辑可独立接 |
| M2-2 | sell-to-open 空头 (`side=Sell`) | 小梁 + 老韩 (RM 空头 cap) | 需 RM 空头敞口语义 + 平仓逻辑,M1 只买不平 |
| M2-3 | 双边深度选边 (用 5 档而非 L1) | 小袁 | L1 选边 M1 够用 |
| M2-4 | 按两边 freshness 动态退化的 de-vig 加权 | 小袁 | M1 de-vig 单边退化已 fail-closed,够用 |

### 7.3 回归面 (诚实标风险)

- **最大风险点 = M1-3 字段参数化**。现状 `outcome=Yes`/`side=Buy`/`token_side="YES"`/`vord.outcome="YES"` 散在 4 处。参数化后若 `SelectSide` 桩没严格返回 YES/Buy,或某处漏改,会导致 NO/YES 错配 (买错边)。**缓解: M1-6 逐位等价回归 + 桩恒定 + 4 处全部走 `DecisionSide`/`traded` 单一来源。**
- **de-vig 来源切换 (M1-4)**: 现状 NO mid 取 `no_opt->microprice` 优先、回退 `mid`。改造后从 `opposite.book` 取同样字段,**逻辑必须逐字保留** (microprice 优先回退 mid),否则 de-vig 结果漂移。
- **不碰的**: hub / RM / sizing / signer / matcher / ledger 全部不动。改动**收敛在 paper_loop + 1 新头文件**。回归面可控。

---

## 8. 架构裁定 (老周拍板)

1. **存储层 hub 不动** — per-token double-buffer 已正确满足 C1/C2,condition 级聚合是退步,否决。
2. **新增决策侧值聚合 `BinaryMarketSnapshot`** — 决策线程栈上组装,零锁零存储,满足 C3。
3. **两边各持独立完整 4ts** — 不合并 4ts 链,freshness 判断交决策层 (R-20 透传精神)。
4. **TickOne 改 per-condition + 整盘口入参 + 字段参数化** — M1 必做的结构地基。
5. **选边逻辑桩化留 M2** — M1 恒 YES/Buy 桩,逐位等价回归门禁;真双边/空头交小袁+小梁+老韩。
6. **性能零额外成本** — 本就双读,纯结构收益,R-12 不触碰。
7. **边界**: 老周定结构 (本 doc); 小袁定微观结构选边 (`SelectSide` 微观部分); 小梁定策略选边 + sizing 方向; 老韩定 M2 空头 RM 语义。

**送评**: 架构评审 (老郭) + 小袁 (微观接口) + 小梁 (策略接口) 会签。结论入 ADR。
