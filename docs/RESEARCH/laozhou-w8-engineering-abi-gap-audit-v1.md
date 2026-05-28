# 工程层 ABI Gap Audit v1 — W8 P0

- owner: 老周 (系统工程部主管, A 单元)
- last_review: 2026-05-29
- sprint: Sprint-1 W8 Wave 40
- trigger: 老板 5/29 P0 verbatim + GM 错教训 (工程层 OrderIntent/Side enum 与 Polymarket SignedOrder ABI 不对齐)
- adr_ref: ADR-024 (worktree 标准), ADR-021 (worktree 隔离), 拟立 ADR-027 (核心数据结构 SSOT enforce)
- 关联:
  - `include/stcpp/risk/risk_gateway.hpp`         — OrderIntent 定义 (老韩 W4)
  - `include/stcpp/strategy/signal_iface.hpp`      — Side enum / SignalOutput 定义
  - `include/stcpp/polymarket/pm_client.hpp`       — SignedOrder / MarketInfo / Position ABI (老李)
  - `include/stcpp/signer/v52/signer_v52.hpp`      — SignV52Request (老孙)
  - `docs/RESEARCH/laoli-laoSun-handshake-v1.md`   — 跨域 ABI 契约 SSOT

---

## §1 老板 verbatim + GM 错教训

**老板 5/29 P0 verbatim:**
> "数据结构很重要 ... 大家看到后可以对市场结构和数据源结构有个清楚的认知"

**GM 审计发现:**
工程层 `OrderIntent` (老韩 W4 Wave 19 起草, 老孙 W5 Wave 24 4ts patch) 完全不含 `token_id` / `outcome` 字段, 与老李 `SignedOrder` ABI 不对齐. `laoli-laoSun-handshake-v1.md` §84 (handshake §3 表格) 明确写明 `SignedOrder` 含 `token_id` + `side` (BUY/SELL × outcome 4 组合), 但上游 `OrderIntent` 完全缺失这两个维度.

**FOM 跨域 review 失效的根因:**
老李 W6 handshake v1 锁定的是 IPolymarketClient 接口 ABI, 没有向上审 RM 入口 `OrderIntent` 是否对齐. 老韩 W4 设计 `OrderIntent` 时参考的是 RM 内部逻辑需求, 未 cross-check Polymarket 下游 ABI 要求. 两个 spec 并行存在, 中间层 (Orchestrator / Signer) 做了字段转换, 但未形成显式 ABI 契约, 导致 gap 长期隐藏.

**GM 错教训 (本次直接对应):**
- GM 错 #7: 需求 vs 工程契约争议未走协商会 — OrderIntent 字段定义时老韩独立设计, 未协商老李 Polymarket ABI
- GM 错 #8: 派单层级越过主管 — 本次由老周 (主管) 主导 audit, 严格按 GM → 主管 → IC 派单层级修复

---

## §2 当前工程层核心数据结构 audit

### 2.1 `OrderIntent` (`include/stcpp/risk/risk_gateway.hpp`)

**起草历史:** W4 Wave 19 (老韩), W5 Wave 24 4ts patch (老孙 v5.1)

**当前字段 (完整):**
```
4 ts:     event_ts_ns, data_source_ts_ns, ingestion_ts_ns, as_of_ts_ns
业务:     market_id (condition_id), strategy_id, signal_id, feature_snapshot_id
方向:     is_buy: bool (true=买, false=???  — Sell 未定义)
定价:     price: double, size_usdc: int64
book:     book_depth_l1_usdc, book_snapshot_ts_ns, tick_size
flag:     is_close: bool
```

**ABI gap 对 Polymarket 下游:**

| 字段 | OrderIntent 当前 | Polymarket SignedOrder 需要 | gap 严重度 |
|------|-----------------|------------------------------|-----------|
| `token_id` | 无 | `std::string token_id` (ERC1155 uint256, per-outcome) | **P0** — 无 token_id 无法构造合法订单 |
| `outcome` | 无 | `side: uint8_t` = 0(BUY/YES), 1(SELL) × outcome_token | **P0** — Sell 方向完全缺失 |
| 市场 | `market_id` = condition_id | `condition_id` = bytes32 hex | OK (字段名不同但语义对齐) |
| BUY/SELL | `is_buy: bool` | `side: uint8_t` BUY=0/SELL=1 | 高风险 — bool 掩盖了 Sell 的合法性 |
| 3-way 市场 | 无 outcome enum | YES/NO binary only | 中期缺口 — soccer Home/Draw/Away 未支持 |

**结论:** `OrderIntent` 只能表达"买哪个 condition_id", 无法表达"买/卖哪个 outcome token". Orchestrator 做字段拼接时依赖外部 context (token_id 需另找来源), 这是一个隐式依赖, 不是显式 ABI 契约.

### 2.2 `Side enum` (`include/stcpp/strategy/signal_iface.hpp`)

**当前定义:**
```cpp
enum class Side : std::uint8_t {
    BuyYes = 0,
    BuyNo  = 1,
};
```

**ABI gap:**

| 维度 | 当前 | 需要 | gap |
|------|------|------|-----|
| 买卖 | 只有 Buy (隐式) | Buy + Sell | PositionManager Reduce 操作无法表达 Sell |
| Outcome | Yes/No binary | Yes/No + Home/Draw/Away + Over/Under | 3-way 市场 (soccer) + Totals 市场漏 |
| Polymarket ABI | BuyYes/BuyNo | side(BUY=0/SELL=1) × token_id (per-outcome) | 对齐缺失 |

**影响:** `SignalOutput.side` 只有 BuyYes/BuyNo, 策略层无法发出 SellYes/SellNo 信号. PositionManager Sprint-3 W9-W10 计划的仓位平减操作 (Reduce) 在当前 Side enum 下无法表达.

**具体影响链路:**
```
ISignalEngine::tick() → SignalOutput { side: BuyYes/BuyNo }
  → Orchestrator 构造 OrderIntent { is_buy: bool }
    → RiskGateway::evaluate()
      → Signer::Sign(SignRequest { outcome: "YES"/"NO" })  ← 此处靠注释约定, 非 ABI 保证
        → PmClient::SubmitOrder(SignedOrder { token_id, side })  ← token_id 哪里来?
```

`token_id` 的来源在当前设计中**完全没有 ABI 保证** — 极有可能在 Orchestrator 里用 `GetMarketInfo` 查到 outcomes[0].token_id 硬取, 而非通过 OrderIntent 传递.

### 2.3 `SignalId enum` (`include/stcpp/strategy/signal_iface.hpp`)

**当前定义:**
```cpp
enum class SignalId : std::uint8_t {
    P0_01_PinnacleNoVig       = 0,
    P0_02_ScorePriceMismatch  = 1,
};
```

**状态:** OK. SignalId 是业务 ID, 与 outcome 解耦, 设计合理. 不参与 token_id / ABI 传递链路. 无需修改.

### 2.4 `MarketInfo / Position` 工程 struct

**handshake v1 §3 表格 (老李 ABI Lock) 定义:**
- `MarketInfo`: ts, condition_id, outcomes(vector<MarketOutcome> 含 token_id), tick_size_bps, neg_risk, fee_rate_bps, accepting_orders, game_start_time_unix_s, clob_token_ids, liquidity_usdc
- `Position`: condition_id, token_id, size_micro, avg_price_bps, cur_price_bps, redeemable, mergeable, ts

**工程层 (`include/stcpp/polymarket/pm_client.hpp`) 实际存在:**
- `MarketInfo` struct: 存在, 字段与 handshake §3 表格完全对齐 (含 `std::vector<MarketOutcome> outcomes`, 每个 `MarketOutcome.token_id`)
- `Position` struct: 存在, 字段与 handshake §3 表格完全对齐 (含 `token_id`)
- `SignedOrder` struct: 存在, 含 `token_id` + `side(uint8_t)` + `condition_id`

**结论:** Polymarket 层 (pm_client.hpp) 的 MarketInfo / Position / SignedOrder struct 已经是完整的, 与 handshake v1 SSOT 对齐. **gap 在于上游 OrderIntent 没有把 token_id/outcome 传下来**, 而不是 pm_client 层缺字段.

### 2.5 `FairValue / SignalOutput` 字段

**当前 `SignalOutput` (`signal_iface.hpp`):**
```
signal_id: SignalId
side: Side (BuyYes/BuyNo — binary only)
edge_bps: int32  (单值 edge, 隐含 binary market)
suggested_size_usdc: int64
confidence: double
```

**ABI gap:**

| 维度 | 当前 | 需要 | gap |
|------|------|------|-----|
| fair_value 维度 | 单值 edge_bps (隐含 YES outcome) | per-outcome (YES/NO 分别有 fair value) | 小段 W8 W2 ack: 当前 Goalserve 单 bet365 single source → fair_value 应是 per-outcome |
| 3-way 市场 | 不支持 | Home/Draw/Away 三个 outcome 各自 fair_value | soccer 全盘口漏 |
| Totals | 不支持 | Over/Under fair_value | Totals 盘口漏 |

**P0-01 Goalserve devig (`p0_01_goalserve_devig.hpp`)** 当前计算 `p_yes_fair_avg` 作为单值, 通过 `BuyYes` or `BuyNo` 表达方向. 这对 binary market 是够的, 但与 `SignalOutput.side` 的 enum 设计不一致 (side 只编码"买哪个 outcome", 不编码 fair_value 本身的 per-outcome 分布).

### 2.6 `OrderBookSnapshot` 工程 struct

**`include/stcpp/microstructure/orderbook.hpp` (小袁 W4):**
```
OrderBookSnapshot {
  ts: OrderBookTs (4 ts)
  market_id: string_view   ← 注意: 这里是 condition_id, 没有 token_id!
  bid[5], ask[5]: OrderBookLevel
  tick_size: double
  top3_depth_usdc: double
  spread_bps: int32
  last_trade_ts_ns: int64
}
```

**`include/stcpp/polymarket/pm_client.hpp` (老李):**
```
OrderBookSnapshot {
  ts: TimestampQuad (4 ts)
  condition_id: string     ← condition (整个市场)
  token_id: string         ← per-outcome ERC1155 token
  yes_bids[5], yes_asks[5]: PriceLevel
  tick_size_bps: uint32
  neg_risk: bool
}
```

**ABI gap (两个同名 struct, 不同命名空间):**

| 字段 | `stcpp::microstructure::OrderBookSnapshot` | `stcpp::polymarket::OrderBookSnapshot` | gap |
|------|------------------------------------------|----------------------------------------|-----|
| token_id | 无 (只有 market_id=condition_id) | 有 `token_id` (per-outcome) | **P0** — 微结构 book 不知道自己是哪个 outcome 的 book |
| ts 类型 | `OrderBookTs` (自定义) | `TimestampQuad` (pm_client 定义) | 两套 4ts struct, 字段语义相同但类型不同 |
| book 数据 | `bid[5]/ask[5]` (双向 per market) | `yes_bids/yes_asks` (YES outcome only) | PM book endpoint 返回 per-token book, 微结构 struct 假设是 per-market book |
| Polymarket /book | `asset_id`=token_id, `bids`/`asks` | `token_id` + `yes_bids/yes_asks` | 老李 pm_client 已经对齐 /book endpoint; 微结构层用的是 condition_id 而非 token_id |

**结论:** `stcpp::microstructure::OrderBookSnapshot` 缺 `token_id` 字段, 与 Polymarket `/book` endpoint (返回 per-asset book) 不对齐. 这是第二个独立的 P0 gap.

---

## §3 修复方案 (3 候选)

### 方案 A: 字段补齐 (最小破坏) — 推荐

在现有 `OrderIntent` / `Side` enum 基础上最小化增加字段, 不破坏现有 RM 逻辑:

```cpp
// signal_iface.hpp 新增
enum class Outcome : std::uint8_t {
    Yes   = 0,  No    = 1,   // binary market (Moneyline / Props)
    Home  = 2,  Draw  = 3,  Away = 4,   // 3-way market (soccer)
    Over  = 5,  Under = 6,              // Totals / Spreads
};

enum class Side : std::uint8_t {
    Buy = 0, Sell = 1,   // 与 outcome 解耦 (不再是 BuyYes/BuyNo)
};

struct SignalOutput {
    SignalId     signal_id{...};
    Outcome      outcome{Outcome::Yes};  // ← 新增
    Side         side{Side::Buy};        // ← 改: Buy/Sell (原 BuyYes/BuyNo 弃用)
    std::int32_t edge_bps{0};
    std::int64_t suggested_size_usdc{0};
    double       confidence{0.0};
    // ← 可选扩展: std::string token_id (由 Orchestrator 查 MarketInfo 填; 或策略层携带)
};

// risk_gateway.hpp OrderIntent 新增
struct OrderIntent {
    // ... 现有 4 ts 字段不动 ...
    std::string  market_id;            // condition_id (不变)
    std::string  token_id;             // ← 新增 P0: per-outcome ERC1155 token
    Outcome      outcome{};            // ← 新增 P0: outcome enum
    Side         side{Side::Buy};      // ← 改: Buy/Sell (原 is_buy: bool 弃用)
    // ... 其余字段不动 ...
    bool         is_close{false};      // 保留 (DRAIN 模式依赖)
};
```

**abi_lock 影响:**
- `OrderIntent` 字段变更 → L2 (老韩 + 老高 + GM 三方签, handshake v1 §1 规则适用于 RM 内部 ABI)
- `Side` enum 语义变更 → L2 (由 BuyYes/BuyNo 改为 Buy/Sell)
- `microstructure::OrderBookSnapshot` 加 `token_id` → L1 (末尾追加)

### 方案 B: 重设计 (clean break)

引入 `SignedOrder` (对齐 Polymarket ABI) 作为 RM 出口类型, 替代 `OrderIntent`:
- RM 入口保留 `OrderIntent` (RM 内部业务语义)
- RM 出口改为直接产出 `polymarket::SignedOrder` (含 token_id / side)
- 老韩 OrderIntent W4 设计作为 RM 内部对象保留, 不对外
- Orchestrator 不再做 OrderIntent → SignedOrder 转换, RM evaluate() 直接输出可下单对象

**优点:** 下游 (Signer / PmClient) 接口清晰, ABI gap 彻底消除.
**缺点:** RM 变成"知道 Polymarket ABI"的组件, 违反关注点分离; RM evaluate() noexcept 约束下直接构造 SignedOrder (含 string 分配) 性能风险; 老韩 W4 全部逻辑重写.

### 方案 C: 引入 OrderRequest 中间层

- `OrderIntent` 保持为 RM 内部对象 (含 outcome + token_id 等业务字段, 扩展 A)
- 新增 `OrderRequest` 为 Orchestrator 内部中间层
- `OrderIntent → OrderRequest` transformer 在 Exec 层 (不在 RM 层)
- `OrderRequest → SignedOrder` 由 Signer 完成
- 三层显式分离: RM 业务意图 / Exec 编排 / Signer 签名

**优点:** 关注点分离最干净, Sprint-3 PositionManager Reduce 操作自然融入 OrderRequest.
**缺点:** 多一层 struct, 短期工作量最大; W8-W10 排期最紧张.

### 推荐: 方案 A

**理由:**
1. **不破坏现有 RM 逻辑** — 老韩 W4 的 21 reject 规则、position_caps、audit 链路全部保留, 只是 `is_buy` 替换为 `Side(Buy/Sell)` + 新增 `token_id` / `outcome`
2. **工作量最小** — 估算 ~300-400 行变更 (vs 方案 B ~800+ 行, 方案 C ~600 行)
3. **不影响热路径性能** — `token_id` 是 std::string, 但已存在于 `market_id` 同层, 无额外 heap 分配路径改变
4. **最快到达可用状态** — W8 W5 spec, W9-W10 实施, W11 paper 前完成
5. **方案 C 可作为 Sprint-3 PositionManager 设计时的演进路径**, 届时 OrderRequest 层可平滑引入

---

## §4 ABI breaking 评估

### 4.1 测试影响

当前 `is_buy` 硬编码出现位置:

| 文件 | 出现次数 | 影响类型 |
|------|---------|---------|
| `tests/unit/test_risk_gateway.cpp` | 3 处 | 构造 OrderIntent |
| `tests/unit/test_audit_emitter.cpp` | 1 处 | RiskDecisionInput.is_buy |
| `tests/unit/test_blake3_audit.cpp` | 1 处 | AuditRecord.is_buy |
| `tests/unit/test_strategy_unlock.cpp` | 1 处 | OrderIntent.is_buy |
| `tests/integration/paper_e2e_smoke_test.cpp` | 4 处 | OrderIntent + PmBookUpdate |
| `tests/integration/r11_paper_pollution_test.cpp` | 3 处 | is_buy |
| `tests/integration/audit_chain_verify_test.cpp` | 1 处 | AuditRecord.is_buy |
| `tests/integration/test_fixture.hpp` | 2 处 | OrderIntent + PmBookUpdate |
| `tests/perf/bench_risk_gateway.cpp` | 2 处 | OrderIntent |
| `tests/perf/bench_audit_emitter.cpp` | 1 处 | RiskDecisionInput.is_buy |
| `tests/perf/bench_e2e_latency.cpp` | 3 处 | OrderIntent |

**预估需修改 test case 数量:** ~22 处 `is_buy` 引用 + 新增 `token_id` / `outcome` 字段填值 (约 30-40 test locations, 不一定是独立 test case).

估算 ctest 458 cases 中含硬编码字段的 ~30-40 cases (从 grep 结果看约 22 个文件位置, 每个位置对应 1-2 个 test case).

### 4.2 AuditRecord / AuditEmitter 字段影响

`AuditRecord` (`include/stcpp/observability/audit_record.hpp`) 含 `is_buy: bool` 字段.
`RiskDecisionInput` (`include/stcpp/observability/audit_emitter.hpp`) 含 `is_buy: bool`.

**方案 A 修复要求:**
- `AuditRecord.is_buy` 替换为 `side: Side` + `outcome: Outcome` (或保留 is_buy + 新增 outcome)
- `AuditRecord` 是 POD 落盘结构, **字段变更 = ABI breaking change**, 需 schema version bump
- `AuditRecord` 当前 `static_assert(sizeof(AuditRecord) <= 65535)` — 新增字段后 sizeof 变大但不触发上界
- 老高 abi_lock v1.6 必须新增 `AuditRecord` + `OrderIntent` 的 field-level grep

**老唐 audit schema v1.3 需求:**
- `event_type` 已有 OrderApproved/OrderRejected 等, 不需要改
- payload 字段加 `token_id: char[66]` (ERC1155 uint256 string ~66B) + `outcome: uint8_t`
- `is_buy: bool` 替换为 `side: uint8_t` (与 Polymarket 下游对齐)
- `static_assert(sizeof(AuditRecord) == N)` 必须更新 (老高 abi_lock 负责)

### 4.3 WAL breaking 评估

`PositionRecord` (`include/stcpp/infra/wal/position_record.hpp`):
- 当前含 `outcome: uint8_t` (0=YES, 1=NO) — **已存在**, 与方案 A 的 Outcome enum 自然对齐
- 当前不含 `token_id` — 如需加入, 是 POD struct L2 breaking change (ABI locked, `static_assert(sizeof == 152)`)
- **建议:** PositionRecord 暂不加 token_id (它通过 market_id + outcome 已可唯一确定 position), 待 Sprint-3 PositionManager 设计时整体评估

`PositionState` (memory, 不落盘): 含 `outcome: uint8_t`, 无需改.

### 4.4 老高 abi_lock v1.6 必须覆盖

当前 abi_lock.py v1.5 覆盖范围: pm_client.hpp + ed25519.hpp + CMakeLists crypto.
v1.6 必须新增:
- `OrderIntent` struct 字段改动触发 handshake 引用检查
- `Side` enum 语义变更触发检查
- `AuditRecord` sizeof 变更触发版本检查
- `core_data_structure_ssot_check.py` (新建): grep `OrderIntent` struct 改动 PR 是否引用 laoli-polymarket-data-structure-ssot doc + xiaoduan-goalserve-data-structure-ssot doc

---

## §5 FOM 跨域 review 流程整改 (GM 错预防)

### 拟立 ADR-027 — 核心数据结构 SSOT enforce

**ADR 编号:** ADR-027
**提案人:** 老周 (A 主管)
**提请方:** 老郭 (F 顾问团协调 + 架构评审主持)
**触发:** W8 Wave 40 P0 — OrderIntent/Side enum 与 Polymarket SignedOrder ABI gap

**ADR-027 推荐内容:**

1. **核心业务数据结构 SSOT 强制引用规则**
   核心数据结构定义为: `OrderIntent` / `SignedOrder` / `Position` / `MarketInfo` / `FairValue` (将来) / `SignalOutput` / `OrderBookSnapshot` (microstructure 层)
   任何修改上述 struct 的 PR 必须在 PR description 中显式引用相关一手 SSOT:
   - Polymarket 端: `laoli-polymarket-data-structure-ssot-vX.md` (老李 W8 W2 产出, 并行 worktree)
   - Goalserve 端: `xiaoduan-goalserve-data-structure-ssot-vX.md` (小段 W8 W2 产出, 并行 worktree)
   - 跨域 ABI 契约: `laoli-laoSun-handshake-v1.md` 范本, 将来扩展为 `laozhou-cross-domain-abi-contract-v1.md`

2. **字段设计 PR 必须 cross-check 三方 spec**
   - OrderIntent 字段改动 → 必须 cross-check 老李 SignedOrder ABI (token_id / side 对齐)
   - SignalOutput.side/outcome 改动 → 必须 cross-check 小段 Goalserve outcome 分类 (3-way 支持)
   - AuditRecord 字段改动 → 必须 cross-check 老唐 audit schema SSOT
   - 老高 `abi_lock.py` v1.6 + `core_data_structure_ssot_check.py` 在 CI grep 层强 enforce

3. **跨域 ABI handshake 文档化要求**
   参考 `laoli-laoSun-handshake-v1.md` 范本, 任何两个模块之间存在"上游 struct → 下游 struct 字段映射"的关系, 必须有显式 handshake 文档:
   - 老韩 OrderIntent → 老孙 SignV52Request: 需补 `laohan-laosun-orderintent-signer-handshake-v1.md`
   - 小程/小蒋 SignalOutput → 老韩 OrderIntent: 需补 `signal-rm-handshake-v1.md`
   - 小袁 microstructure::OrderBookSnapshot → 老李 polymarket::OrderBookSnapshot: 需补 ABI mapping

4. **FOM (跨域 review) 流程硬化**
   - 任何 Sprint IC 在修改核心业务 struct 前, 必须主动通知下游 owner (最小惊喜原则)
   - 主管 (老周 / 老韩 / 小梁 / 小余) 在 weekly sync 中汇报核心 struct 变更计划 (提前 1 sprint)
   - 老郭架构评审 (月第1个周四) 维护"核心数据结构影响地图" (谁依赖谁)

---

## §6 派单 backlog (W8 W5 + Sprint-3 W9)

以下为 W8 W5 启动, 按方案 A 推荐版:

| 任务 | 负责人 | 截止 | 说明 |
|------|--------|------|------|
| OrderIntent v0.5 spec | 老韩 (B 主管) | W8 W5 EOD | 按方案 A 添加 token_id / Outcome enum / Side(Buy/Sell) 替换 is_buy; RM 21 reject 逻辑不变 |
| SignerV52 ABI align handshake §84 | 老孙 | W8 W5 EOD | SignV52Request 接 `OrderIntent.token_id` (当前 outcome uint8 已有, token_id 缺); 更新 handshake v1 §3 mapping |
| AuditEmitter schema v1.3 | 老唐 | W8 W5 EOD | AuditRecord / RiskDecisionInput 加 `token_id: char[66]` + `outcome: uint8_t`, 替换 `is_buy: bool`; sizeof static_assert 更新 |
| abi_lock v1.6 + core_data_structure_ssot_check.py | 老高 | W8 W5 EOD | 扩展 abi_lock.py v1.6 覆盖 OrderIntent/AuditRecord; 新建 ssot_check.py; CI grep 集成 |
| ADR-027 立项 | 老郭 | W8 W5 EOD | 核心数据结构 SSOT enforce + FOM 跨域 review 流程; 由老周本 doc 作为 input |

Sprint-3 W9-W10 实施:
- 老韩 + 老周: OrderIntent v0.5 实施 (改 risk_gateway.hpp, 更新 RM evaluate() 入参校验)
- 老孙: SignerV52 v5.3 接 token_id (ABI align)
- 老唐: AuditRecord v1.3 实施 (WAL schema bump, replay compatibility)
- 老王: WAL framework 确认 AuditRecord schema 版本化方案 (新字段 replay 向后兼容)
- 小袁: microstructure::OrderBookSnapshot 加 token_id (L1 追加)
- test 更新: ~30-40 test locations (老高负责 grep, IC 负责修改)

---

## §7 与老李 / 小段 W8 W4 SSOT 联动

**老李 Polymarket SSOT (并行 worktree, 预期 W8 W4 完成):**
- 完成后老周 update §2.4 audit 表 (确认 `pm_client.hpp` MarketInfo / Position / SignedOrder 字段)
- 重点关注: Polymarket `token_id` 是 uint256 string 还是 hex string? 长度约束? (影响 AuditRecord char[66] 分配)
- 老李 doc 完成后, §3 方案 A 中 `token_id: std::string` 字段长度约束可精确化

**小段 Goalserve SSOT (并行 worktree, 预期 W8 W4 完成):**
- 完成后老周 update §2.2 Side enum gap (确认 Goalserve 支持哪些 market type: binary / 3-way / totals)
- 重点关注: Goalserve odds API 是否返回 Home/Draw/Away 三组 odds? 还是只有 binary outcome?
- 如果 Goalserve 只返回 binary outcome, soccer 3-way 支持可推后到 Sprint-4

**当前临时约定 (待小段 doc 后确认):**
- Outcome enum 按方案 A 设计含 Home/Draw/Away, 但 P0-01 信号层 MVP 仅用 Yes/No
- 3-way Outcome 值域在 enum 中预留, 不触发 Soccer-specific 逻辑 (可 null 路径)

---

## §8 风险评估

| 风险 | 概率 | 影响 | 缓解 |
|------|------|------|------|
| 工程 ABI 修复工作量超预期 | 中 | ~800-1200 行跨 RM/Signer/Audit/WAL/microstructure | W9-W10 IC 工作量 +30-50%; Sprint-3 排期受影响 |
| test 更新遗漏 (is_buy 残留) | 高 | ctest 部分 false pass | 老高 ssot_check.py + CI grep 强 enforce |
| WAL replay 兼容性 (AuditRecord 字段增加) | 中 | 存量 WAL 文件 replay 失败 | 老唐 schema version byte; 老王 replay 兼容性方案 |
| PositionRecord token_id 缺失 (Sprint-3 impact) | 低 | Sprint-3 PositionManager Reduce 操作设计时发现需补 | 已在 §4.3 标注; Sprint-3 设计时整体补 |
| Side enum 语义变更影响 P0-01 信号逻辑 | 低 | BuyYes/BuyNo → Buy × Outcome enum 映射关系变更 | 方案 A 有明确映射: BuyYes=Buy+Yes, BuyNo=Buy+No |
| paper engine MVP W11 时间窗口 | 高 | W9-W10 实施 + 测试必须在 W11 paper 启动前完成 | 强 deadline; 老胡 PM 跟踪 |

**工作量量化:**
- W8 W5 spec 产出: ~5 文档 (各 1-2h)
- W9-W10 实施: ~800-1000 行 C++ 变更 (跨 RM / Signer / Audit / microstructure / tests)
- ctest update: ~30-40 locations, 预估 0.5 sprint IC 工时

**MVP 路径影响:**
- W11 paper 启动前必须完成 token_id / Outcome 实施
- 不完成则 paper 模式下 SubmitOrder 构造的 SignedOrder.token_id 来源不明确 (hardcode 风险)
- 最坏情况: W11 paper 推迟 1 sprint (W12 启动)

---

## §9 不耻下问

| 问题 | 请教对象 | 时限 | 背景 |
|------|---------|------|------|
| `token_id` uint256 string 精确长度约束 (影响 AuditRecord char[N] 分配) | @老李 | W8 W4 EOD | Polymarket SSOT 并行 worktree 完成后 ack |
| Goalserve 是否支持 3-way market odds (Home/Draw/Away)? soccer 实测? | @小段 | W8 W4 EOD | Goalserve SSOT 并行 worktree 完成后 ack |
| OrderIntent v0.5 spec: is_buy→Side 迁移是否影响 21 reject 规则? (特别是 DRAIN 只允许平仓逻辑) | @老韩 | W8 W5 | B 主管 ack |
| SignerV52 接 token_id: SignV52Request 现有 outcome uint8 是否足够? 还是需要 string token_id? | @老孙 | W8 W5 | A 单元 IC ack |
| AuditRecord schema 版本化: 新字段加入后 WAL replay 兼容性方案 (magic byte? version field?) | @老唐 @老王 | W8 W5 | B 单元 + A 单元联合 ack |
| abi_lock v1.6 具体 grep pattern 设计: OrderIntent struct 改动如何被 CI 捕获? | @老高 | W8 W5 | F 顾问团 ack |
| ADR-027 正式立项: 架构评审议程加入? 还是紧急 offline review? | @老郭 | W8 W5 | F 协调人 ack |
| 修复方案最终选 A/B/C: 老雷 GM final ack | @老雷 | W8 W5 | GM 拍板 (三方协商后) |

---

**完成汇报:**
pwd 验证在 worktree `.claude/worktrees/agent-aa1114999b958590c/`, ABI gap audit §1-9 完成. 6 核心 struct 字段 audit 结果: OrderIntent 缺 token_id + outcome (P0), Side enum 缺 Sell 方向 (P0), microstructure::OrderBookSnapshot 缺 token_id (P0), SignalOutput/FairValue 缺 per-outcome 维度 (中期), MarketInfo/Position/SignedOrder (pm_client 层) 与 handshake v1 SSOT 已对齐. 3 修复方案对比推荐方案 A (字段补齐, 最小破坏). ADR-027 立项准备完成 (§5). W8 W5 6 派单 backlog 明确 (§6). 全 worktree + commit.
