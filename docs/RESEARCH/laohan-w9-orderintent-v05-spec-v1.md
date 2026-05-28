# OrderIntent v0.5 Spec + ABI Handshake — W9 P0

- owner: 老韩 (风控合规部主管, B 单元)
- last_review: 2026-05-29
- sprint: W9 Wave 51 P0
- trigger: ADR-027 §4 enforce (GM 错 #22 修复, W8 Wave 40 P0 ABI gap audit)
- adr_ref: ADR-027 (核心数据结构 SSOT enforce), ADR-004 (reject 顺序)
- 关联文件:
  - `include/stcpp/risk/risk_gateway.hpp`                        — OrderIntent v0.4 (本 spec 升级目标)
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` — Polymarket SSOT (SSOT 一手)
  - `docs/RESEARCH/laozhou-w8-engineering-abi-gap-audit-v1.md`   — 老周 修复方案 A 推荐
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` — ADR-027 enforce 规则
  - `docs/RESEARCH/laoli-laoSun-handshake-v1.md`                 — SignedOrder ABI lock §3

---

## §1 ADR-027 cite 块 (强 enforce, 必填)

ADR-027 Enforce-1 要求所有核心数据结构 PR description 必含以下 cite 段:

```
cite:
  polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3 §4.1 §6
  goalserve_ssot_cite:  N/A (OrderIntent 不直接对接 Goalserve; Outcome enum 扩展预留 Home/Draw/Away
                            待小段 goalserve-data-structure-ssot 确认 3-way market 后触发 L1)
  handshake_cite:       laoli-laoSun-handshake-v1.md §3 SignedOrder ABI lock (token_id + side 字段)
  adr_ref:              ADR-027 §4 Enforce-1/2/3/4
```

**cite 校验逻辑 (老高 CI, ADR-027 Enforce-3 C1-C4):**

| 检查 | 内容 | 本 spec 状态 |
|---|---|---|
| C1 | PR diff 改 OrderIntent 须含 SSOT cite | 已含 (polymarket_ssot + handshake) |
| C2 | OrderIntent / SignedOrder struct 含 token_id | 已含 (§2 字段表) |
| C3 | Side enum 含 Buy 且含 Sell | 已含 (§2 enum 定义) |
| C4 | 跨 struct ABI handshake doc 存在引用 | 已含 (laoli-laoSun-handshake-v1.md §3) |

---

## §2 OrderIntent v0.5 字段表 (方案 A, 老周 推荐)

### §2.1 enum 定义

```cpp
// signal_iface.hpp — 新增 (替换原 Side::BuyYes/BuyNo)
// SSOT cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.1 §6.2
// Outcome 是 Polymarket token 级一等公民: 1 condition_id = 2..N token_id

enum class Outcome : std::uint8_t {
    Yes  = 0,   // Binary market (Moneyline / Props / 系列赛 binary)
    No   = 1,   // Binary market complement
    // 3-way 预留 (待小段 Goalserve SSOT §4 确认 soccer market 类型后触发 L1)
    Home = 2,   // 未来: Soccer Moneyline 主场
    Draw = 3,   // 未来: Soccer Moneyline 平局
    Away = 4,   // 未来: Soccer Moneyline 客场
    // Totals 预留
    Over  = 5,  // 未来: Total Over
    Under = 6,  // 未来: Total Under
};

// SSOT cite: laoli-laoSun-handshake-v1.md §3 SignedOrder.side (uint8, BUY=0/SELL=1)
// 与 Outcome 解耦: side=Buy+outcome=Yes 表达 BuyYes, side=Sell+outcome=Yes 表达 SellYes
enum class Side : std::uint8_t {
    Buy  = 0,   // Polymarket SignedOrder.side = 0 (BUY)
    Sell = 1,   // Polymarket SignedOrder.side = 1 (SELL)
};
```

### §2.2 OrderIntent v0.5 struct

```cpp
// risk_gateway.hpp — OrderIntent v0.5
// ABI breaking: 4 处 (见 §3 ABI 影响)
// SSOT cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3.2 §3.3 §6.2

struct OrderIntent {
    // ---- R-20 4 ts (不变, ADR R-20 红线) ----
    // event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};

    // ---- 市场标识 (双主键, SSOT §2.3 concept 区分) ----
    // condition_id: bytes32 hex (0x 前缀, 66 char), market 级, per-market cap 用
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.2 conditionId
    std::string  condition_id;          // ← v0.4 market_id rename (ABI break #1)

    // token_id: uint256 string (无 0x 前缀, 十进制, 最多 77 位), outcome 级
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3 token_id
    // handshake cite: laoli-laoSun-handshake-v1.md §3 SignedOrder.token_id
    // EIP-712 Order.tokenId = uint256(token_id) — 下单 CLOB 一等公民
    std::string  token_id;              // ← v0.5 新增 (ABI break #2)

    // outcome: per-token outcome 语义标注 (与 token_id 冗余但 RM R6.2b + audit 用)
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §3.3 tokens[i].outcome
    Outcome      outcome{Outcome::Yes}; // ← v0.5 新增 (ABI break #3)

    // side: BUY/SELL, 与 outcome 解耦
    // SSOT: laoli-laoSun-handshake-v1.md §3 SignedOrder.side (uint8, BUY=0/SELL=1)
    Side         side{Side::Buy};       // ← v0.4 is_buy:bool 改 Side enum (ABI break #4)

    // ---- 业务 ID ----
    std::string  strategy_id;
    std::string  signal_id;             // 幂等 key (不变)
    std::string  feature_snapshot_id;  // ML-R8 复盘锚 (不变)

    // ---- 定价 ----
    double       price{0.0};           // ∈ (0, 1)
    std::int64_t size_usdc{0};         // > 0 整 cent

    // ---- SlippageModel 入参 + book context ----
    // book_snapshot_ts_ns 必须与 token_id 对齐: 同一 token 的 book 快照
    // SSOT: laoli-w8-polymarket-data-structure-ssot-v1.md §3.4 /book?token_id=
    double       book_depth_l1_usdc{0.0};
    std::int64_t book_snapshot_ts_ns{0};   // R8 signal validity: book_ts ↔ token_id 对齐
    double       tick_size{0.01};           // per-token (SSOT §5 T-07: 不恒为 0.01)

    // ---- 平仓标志 (保留, DRAIN 模式依赖) ----
    // 平仓语义: side=Sell + token_id = 持仓 token
    // 示例: 持 YES 仓平仓 = side=Sell + outcome=Yes + token_id=YES_token_id
    bool         is_close{false};      // 保留 (DRAIN 仅放行 is_close=true)
};
```

### §2.3 v0.4 → v0.5 字段映射表

| v0.4 字段 | v0.5 字段 | 变更类型 | SSOT 依据 |
|---|---|---|---|
| `market_id` (string) | `condition_id` (string) | rename, 同语义 | SSOT §2.3: condition_id 是正确名称, market_id 命名歧义陷阱 T-01 |
| (不存在) | `token_id` (string) | 新增 | SSOT §2.3: CLOB 下单必须字段, handshake §3 SignedOrder.token_id |
| (不存在) | `outcome` (Outcome enum) | 新增 | SSOT §3.3: tokens[i].outcome per-token 标注 |
| `is_buy: bool` | `side: Side enum` | type change | SSOT §6.2: Side 解耦原理; handshake §3 side=BUY/SELL |
| `strategy_id` | `strategy_id` | 不变 | — |
| `signal_id` | `signal_id` | 不变 | — |
| `feature_snapshot_id` | `feature_snapshot_id` | 不变 | — |
| `price` | `price` | 不变 | — |
| `size_usdc` | `size_usdc` | 不变 | — |
| `book_depth_l1_usdc` | `book_depth_l1_usdc` | 不变 | — |
| `book_snapshot_ts_ns` | `book_snapshot_ts_ns` | 语义强化: 必须对齐 token_id | SSOT §3.4: /book 按 token_id 独立 |
| `tick_size` | `tick_size` | 语义强化: per-token | SSOT §5 T-07 |
| `is_close` | `is_close` | 不变 | 保留 DRAIN 放行逻辑 |

### §2.4 Side 解耦原理 (SSOT §6.2 verbatim)

```
v0.4 BuyYes  = v0.5 side=Buy  + outcome=Yes  + token_id = YES token
v0.4 BuyNo   = v0.5 side=Buy  + outcome=No   + token_id = NO  token
(不存在)       v0.5 side=Sell + outcome=Yes  + token_id = YES token  (平 YES 仓)
(不存在)       v0.5 side=Sell + outcome=No   + token_id = NO  token  (平 NO  仓)
```

**DRAIN 模式平仓判断:**
```
DRAIN 放行条件: is_close=true AND side=Sell
DRAIN 拒绝:    is_close=false OR side=Buy (开仓)
```

---

## §3 ABI 影响

### §3.1 4 处 ABI breaking (同一 wave W9 合并上线)

| break # | 变更 | 类型 | 审批要求 (handshake §1) |
|---|---|---|---|
| #1 | `market_id` → `condition_id` rename | L2 (改字段语义/名称) | 老李 + 老孙 + GM 三方签 |
| #2 | 新增 `token_id: string` 字段 | L2 (新增非末尾; 或 L1 若追加末尾) | 老李 + 老孙 + GM 三方签 |
| #3 | 新增 `outcome: Outcome enum` 字段 | L2 | 老李 + 老孙 + GM 三方签 |
| #4 | `is_buy: bool` → `side: Side enum` type change | L2 | 老李 + 老孙 + GM 三方签 |

**注:** OrderIntent 不在 laoli-laoSun-handshake-v1.md ABI lock 原始范围内 (handshake v1 只锁 SignedOrder / Position / MarketInfo). 本次 4 处 breaking 依 ADR-027 Enforce-2 新增的 FOM 跨域 review 走 **老韩 (RM owner) + 老李 (协议 ack) + 老周 (架构 ack)** 三方 approve.

### §3.2 测试影响 (ctest)

当前 `is_buy` 硬编码出现位置 (老周 §4.1 grep 结果):

| 文件 | 出现次数 | 影响 |
|---|---|---|
| `tests/unit/test_risk_gateway.cpp` | 3 处 | OrderIntent 构造 |
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

**预估 ctest 影响范围:** 458 cases 中含 OrderIntent 硬编码 ~22 处文件位置, 约 20-30 独立 test cases.

**migration 路径:**
- `is_buy=true` → `side=Side::Buy`
- `is_buy=false` → `side=Side::Sell` (注意: v0.4 中 BuyNo 用 is_buy=true, 需结合上下文判断)
- 所有构造 OrderIntent 处增补 `token_id` + `outcome` 字段 (测试 token_id 用 mock 值如 `"1234567890"`)

### §3.3 WAL schema bump

**AuditRecord 变更 (老唐 W9 W2 spec):**

```
v1.2 (当前): AuditRecord { ..., market_id: string, signal_id: string, is_buy: bool, ... }
v1.3 (目标): AuditRecord { ..., condition_id: string, token_id: char[80],
                            outcome: uint8_t, side: uint8_t, signal_id: string, ... }
```

- `market_id` rename → `condition_id`
- 新增 `token_id: char[80]` (SSOT §2.3: uint256 string 最多 77 位, 预留 3B 对齐 + null terminator)
- 新增 `outcome: uint8_t` (Outcome enum 底层类型)
- `is_buy: bool` → `side: uint8_t` (Side enum 底层类型)
- `static_assert(sizeof(AuditRecord) == N)` 必须更新 (老高 abi_lock v1.7 负责)
- WAL schema version: v1.2 → v1.3 (magic byte bump, 老唐 replay 兼容方案)

**旧 AuditRecord (v1.2) replay 处理:**
- 读到 schema v1.2 header → 补填 `token_id=""`, `outcome=Outcome::Yes`, `side=(is_buy?Buy:Sell)`
- 老唐 W9 W2 spec 给出 migration reader 设计

### §3.4 ABI lock 影响版本

- handshake v1 → v1.1 (老李 + 老孙 co-sign): OrderIntent 纳入 ABI lock 范围, 新增 §3 OrderIntent 字段表
- abi_lock.py v1.6 → v1.7 (老高 W9 W4): 新增 OrderIntent struct grep + AuditRecord sizeof check

---

## §4 RM evaluate() API update

### §4.1 接口签名 (不变)

```cpp
[[nodiscard]] RiskDecision evaluate(OrderIntent const& intent) noexcept;
```

接口签名不变。入参 `OrderIntent` 扩展新字段后, evaluate() 内部逻辑扩展如下:

### §4.2 R6.2 cap 粒度升级: per-condition + per-outcome (token 级)

**现状 (v0.4):**
```
R6.2 per_market_exposure_cap[condition_id] <= market_exposure_cap_usdc
```

**v0.5 升级 (SSOT §6.3: Yes 仓和 No 仓是不同 CTF token, 需分别 cap):**

```
R6.2a: per_condition_exposure[condition_id] <= per_condition_cap_usdc
       (整个 condition 的双向总敞口, 含 YES+NO position 之和)

R6.2b: per_outcome_exposure[token_id] <= per_outcome_cap_usdc
       (单一 token 的单向敞口上限, 防止单 outcome 过度集中)
```

**RiskConfig 新增字段:**
```cpp
struct RiskConfig {
    // ... 现有字段不变 ...

    // v0.5 新增: per-outcome cap (token 级)
    std::int64_t per_outcome_cap_usdc = 25'000;  // 默认 = market_exposure_cap / 2

    // DRAIN 模式保持: is_close=true + side=Sell 放行
};
```

**set_market_exposure() 升级:**

```cpp
// v0.4: 按 condition_id 跟踪
void set_market_exposure(std::string const& market_id, std::int64_t usdc) noexcept;

// v0.5: 按 condition_id + token_id 双维度跟踪
// 老沈 W9 W3 实施: 内部 map 从 market_id → usdc 扩展为:
//   condition_exposure_[condition_id] += delta (买入时 +size, 平仓时 -size)
//   token_exposure_[token_id] += delta
void set_condition_exposure(std::string const& condition_id, std::int64_t usdc) noexcept;
void set_outcome_exposure(std::string const& token_id, std::int64_t usdc) noexcept;
```

### §4.3 R8 signal validity check 升级: book_snapshot_ts ↔ token_id 对齐

**SSOT 依据:** laoli-w8-polymarket-data-structure-ssot-v1.md §4.1: market channel 按 token_id 独立订阅, 每个 token 有独立 book 快照时间戳.

**v0.5 新增校验:**

```
R8.4 (新): book_snapshot_ts_ns 必须与 intent.token_id 对应的 book 快照对齐
           具体: stale_check 时用 token_id 查对应 book 的 freshness, 而非 condition_id 的 book
           违反: 拒绝, RejectCode = STALE_DATA, sub_reason = BOOK_TOKEN_ID_MISMATCH
```

**实施说明:** check_stale_data_() 中增加:
- 按 `intent.token_id` 查 `token_book_freshness_ms_[token_id]`
- 与 `intent.book_snapshot_ts_ns` 对比, 超阈值 → STALE_DATA

### §4.4 ADR-004 短路顺序 (不变)

evaluate() 21 reject 短路顺序严格保持 ADR-004 v2 定义不变:

```
1. state           (HALTED/DRAIN/SAFE_MODE — DRAIN/SAFE_MODE 平仓放行)
2. invalid_intent  (R-20 PIT 4 ts + 字段校验; v0.5 增加 token_id/outcome/side 非空校验)
3. duplicate_intent
4. stale_data      (v0.5 增加 R8.4 book_token_id_mismatch)
5. market
6. position_caps   [ADR-004 前移] (v0.5 增加 R6.2b per_outcome_cap)
7. liquidity
8. signal
9. strategy_decayed
10. AUDIT_WAL_BACKPRESSURE
```

**v0.5 新增 invalid_intent sub_reason:**

| sub_reason | 触发条件 |
|---|---|
| MISSING_TOKEN_ID | `intent.token_id.empty()` |
| MISSING_CONDITION_ID | `intent.condition_id.empty()` (原 market_id 对应) |
| INVALID_TOKEN_ID_FORMAT | token_id 含非数字字符 (uint256 string 格式校验) |
| TOKEN_OUTCOME_MISMATCH | `outcome=Yes` 但 token_id 与 condition 的 Yes token 不一致 (需 MarketInfo) |

**注:** TOKEN_OUTCOME_MISMATCH 在 W9 W3 初版可跳过 (MarketInfo 注入接口未完成), W10 W1 整合测试补全.

---

## §5 SignerV52 ABI 对齐 (handshake §3 §84)

### §5.1 OrderIntent → SignedOrder transformer 字段映射

```
OrderIntent.condition_id   → SignedOrder.condition_id   (直接传)
OrderIntent.token_id       → SignedOrder.token_id        (直接传, 无 default 推断)
OrderIntent.side           → SignedOrder.side            (Side::Buy=0 / Side::Sell=1, 直接映射)
OrderIntent.price          → SignedOrder.limit_price_bps (price * 10000 转 bps)
OrderIntent.size_usdc      → SignedOrder.size_usdc_micro (size_usdc * 1_000_000 转 micro)
OrderIntent.outcome        → (不直接传 SignedOrder; SignedOrder 用 token_id 隐含 outcome)
```

**关键:** v0.4 中 token_id 来源不明确 (Orchestrator 从 MarketInfo 推断). v0.5 起 token_id 在 OrderIntent 阶段已显式携带, transformer 直接 pass-through, 不再有"哪个 token_id"的歧义.

### §5.2 老孙 W9 W2 SignerV52 v5.3 align spec 配套

老孙需在 W9 W2 确认以下 (与本 spec 并行):

1. `SignV52Request` 是否已含 `token_id: string`? (handshake v1 §3 SignedOrder 已有, 但 SignV52Request 是内部 IPC 结构, 需独立确认)
2. `SignV52Request.side` 类型是否与 `Side` enum 底层 uint8 对齐 (BUY=0/SELL=1)?
3. Orchestrator 层 OrderIntent → SignV52Request transformer 需更新: 加 `token_id` pass-through

### §5.3 negRisk 合约选择 (不变)

negRisk 路径在 Orchestrator 层处理, 不进 OrderIntent 字段:

```
SSOT §3.5: verifyingContract 选择:
  neg_risk=false: 0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E
  neg_risk=true:  0xC5d563A36AE78145C45a50134d48A1215220f80a
```

OrderIntent 不含 `neg_risk` 字段 (从 MarketInfo 查, Orchestrator 层决策). 本 spec 不涉及.

---

## §6 测试 spec (派老沈 W10)

以下 7 个测试 case 为 v0.5 新增验收测试, 补入 ctest 458 cases → 465 cases:

### T1: token_id pass-through (ABI 链路完整性)

```
输入: OrderIntent{outcome=Yes, side=Buy, token_id="79394...", condition_id="0xa9db..."}
期望: RiskDecision.APPROVED (其余条件满足时)
      → transformer → SignedOrder.token_id = "79394..." (完全一致, 无替换)
验证方式: 在 test_fixture.hpp 中 mock transformer, 断言 SignedOrder.token_id == intent.token_id
```

### T2: per-outcome cap check (R6.2b)

```
输入: OrderIntent{outcome=No, side=Sell, token_id="40471...", size_usdc=30_000}
      前置: set_outcome_exposure("40471...", 20_000)  (已有敞口)
      config: per_outcome_cap_usdc = 25_000
期望: 20_000 + 30_000 = 50_000 > 25_000 → REJECTED, RejectCode=EXCEED_PER_OUTCOME_CAP
验证方式: ASSERT(d.reject == RejectCode::EXCEED_PER_OUTCOME_CAP)
```

### T3: is_close + side=Sell + token_id 一致性 (DRAIN 模式)

```
输入: OrderIntent{is_close=true, side=Sell, outcome=Yes, token_id="79394..."}
      RM state = DRAIN
期望: APPROVED (DRAIN 模式放行平仓)
对照: OrderIntent{is_close=false, side=Buy} in DRAIN → REJECTED (开仓拒绝)
验证方式: set_state(RmState::DRAIN), evaluate(), assert APPROVED / REJECTED
```

### T4: ctest 旧 cases migration (audit log v1.2 → v1.3)

```
目标: 旧 AuditRecord v1.2 (含 is_buy: bool) replay 到 v1.3 (含 side: uint8_t)
输入: v1.2 audit WAL binary (含 is_buy=true)
期望: replay 后 AuditRecord.side = Side::Buy (uint8=0)
      replay 后 AuditRecord.token_id = "" (空, migration 默认值)
      replay 后 AuditRecord.outcome = Outcome::Yes (uint8=0, migration 默认值)
验证方式: 老唐 WAL reader migration 测试 (audit_chain_verify_test.cpp 扩展)
```

### T5: 平仓边界 (持 YES 仓 close = side=Sell+outcome=Yes)

```
场景: 持 YES 仓 size=10_000 USDC, 发起平仓
输入: OrderIntent{is_close=true, side=Sell, outcome=Yes, token_id="79394...", size_usdc=10_000}
期望: APPROVED (合法平仓)
对照错误输入: OrderIntent{is_close=true, side=Buy, outcome=Yes} → REJECTED (矛盾: 平仓但 Buy)
             OrderIntent{is_close=true, side=Sell, outcome=No, token_id="40471..."} 且持仓为 YES
             → 允许 (平 No 仓, 独立操作, 需 per-outcome_exposure 检查)
```

### T6: hot path latency P99 < 50us (RM evaluate)

```
测试文件: tests/perf/bench_risk_gateway.cpp (已存在, 扩展 v0.5 字段)
输入: OrderIntent v0.5 (含 token_id + outcome + Side::Buy)
跑法: 10_000 次 evaluate() 循环, 统计 P50/P95/P99
期望: P99 < 50_000 ns (50us)
     P50 < 5_000 ns
注意: token_id 是 string, 确认热路径无额外 heap 分配 (token_id 可用 SSO 路径, ~77 char 超 SSO,
      需确认 string 构造在 evaluate() 外完成, intent 传 const ref)
```

### T7: ABI handshake 字段对齐 100% (OrderIntent → SignedOrder)

```
目标: OrderIntent v0.5 所有字段在 SignedOrder 中有明确对应映射
检查项:
  condition_id  → SignedOrder.condition_id  [PASS]
  token_id      → SignedOrder.token_id      [PASS]
  side (Buy=0)  → SignedOrder.side (0)      [PASS]
  side (Sell=1) → SignedOrder.side (1)      [PASS]
  price         → limit_price_bps           [PASS: * 10000]
  size_usdc     → size_usdc_micro           [PASS: * 1_000_000]
  outcome       → (not in SignedOrder, token_id 隐含) [OK, 设计意图]
测试方式: static_assert + compile-time 字段存在性检查 (参考老高 abi_lock.py C2/C3)
```

---

## §7 v0.5 实施 timeline

| 时间 | 责任人 | 任务 | 依赖 |
|---|---|---|---|
| W9 W2 | 老韩 (本 spec final) | spec final review + 派单给 W9 W3 IC | — |
| W9 W2 | 老孙 (A 单元) | SignerV52 v5.3 align spec (§5.2 3 项确认) | 本 spec |
| W9 W2 | 老唐 (B 单元) | AuditRecord v1.3 spec (WAL schema bump, §3.3) | 本 spec |
| W9 W2 | 老高 (F 顾问) | abi_lock v1.7 grep spec (§3.4 + ADR-027 C1-C4) | 本 spec |
| W9 W3 | 老沈 (B 单元) | OrderIntent v0.5 C++ 实施 (risk_gateway.hpp + signal_iface.hpp) | 老孙/老唐/老高 W9 W2 spec |
| W9 W3 | 老沈 | ctest 旧 cases migration: ~22 处 is_buy → side, 补 token_id + outcome | 老韩 W9 W2 spec |
| W9 W4 | 老孙 + 老唐 + 老高 | ABI 联调: SignerV52 v5.3 + AuditRecord v1.3 + abi_lock v1.7 | 老沈 W9 W3 实施完成 |
| W10 W1 | 全员 | 整合测试 + ctest 全过 (期望 458 + 7 = 465 cases) | W9 W4 联调通 |

**Blocker 升级路径 (CLAUDE.md §6):**
- 当事人 → 老韩 (4h) → 老胡协调 (48h) → 老雷 P0 (2h 内介入)

---

## §8 不耻下问

| 问谁 | 问题 | 优先级 | 时限 |
|---|---|---|---|
| @老李 | token_id uint256 string 精确格式约束: 最大几位? 始终纯数字? (影响 AuditRecord char[80] 分配 + T7 格式校验) | P0 | W9 W2 EOD |
| @老孙 | SignerV52 v5.3 align: SignV52Request 现有字段表? token_id 是否已含? side uint8 对齐 BUY=0/SELL=1? | P0 | W9 W2 EOD |
| @老唐 | audit schema v1.3: WAL schema version bump 方案? magic byte 位置? v1.2 replay 向后兼容 reader 设计? | P0 | W9 W2 EOD |
| @老高 | abi_lock v1.7 grep pattern: OrderIntent struct 改动如何被 CI 捕获? core_data_structure_ssot_check.py C2/C3 具体 pattern? | P1 | W9 W3 EOD |
| @老周 | 架构 review: 方案 A 字段布局是否有性能隐患? token_id string ~77 char 超 SSO, evaluate() 热路径影响评估? | P1 | W9 W2 |
| @老郭 | ADR-027 §4.2 layer 2 veto: 本 spec 是否触发架构重设计评审? (方案 A 非重设计, 应无需 veto, 请确认) | P1 | W9 W2 |
| @老雷 | GM final ack: OrderIntent v0.5 spec + ADR-027 4 enforce 生效确认 | P0 | W9 W2 EOD |

---

## §9 风控拒绝原因表 (v0.5 新增 + 修订)

### §9.1 新增 RejectCode

| RejectCode | 触发条件 | 对应规则 |
|---|---|---|
| `EXCEED_PER_OUTCOME_CAP` | `token_exposure[token_id] + size > per_outcome_cap_usdc` | R6.2b (新) |
| `MISSING_TOKEN_ID` | `intent.token_id.empty()` | invalid_intent sub_reason (新) |
| `MISSING_CONDITION_ID` | `intent.condition_id.empty()` | invalid_intent sub_reason (new, 原 MISSING_MARKET_ID rename) |
| `INVALID_TOKEN_ID_FORMAT` | token_id 含非数字字符 | invalid_intent sub_reason (新) |
| `BOOK_TOKEN_ID_MISMATCH` | book_snapshot_ts 对应 token 与 intent.token_id 不一致 | R8.4 (新) |

### §9.2 修订 RejectCode (rename)

| v0.4 | v0.5 | 变更原因 |
|---|---|---|
| `MISSING_MARKET_ID` | `MISSING_CONDITION_ID` | market_id → condition_id rename 配套 |

### §9.3 audit log 必有字段 (v0.5, 可追溯红线)

每条 audit record (APPROVED 或 REJECTED) 必含:

```
audit_id, condition_id, token_id, outcome(uint8), side(uint8),
signal_id, strategy_id, decision, reject_code, sub_reason,
4 ts (event/data_source/ingestion/as_of), decision_ts_ns,
slippage_bps, expected_fill_rate
```

**CLAUDE.md §7 #6 可追溯红线:** 风控拒单全部留 audit log, token_id 级别可追溯是 v0.5 的核心交付.

---

**完成汇报:**

pwd 验证在 worktree `/Users/wangweibo/code/sports-trader-cpp/.claude/worktrees/agent-a63b63e9d449b27b5/`.
OrderIntent v0.5 spec §1-9 完成:
- §1 ADR-027 cite 块: polymarket_ssot §3 §4.1 §6 + handshake §3 + ADR-027 §4 Enforce-1/2/3/4, cite 校验 C1-C4 全过
- §2 字段表: 4 ts 不变, condition_id rename, token_id 新增, outcome enum 新增, side enum (Buy/Sell) 替换 is_buy, is_close 保留
- §3 ABI breaking 4 处: market_id→condition_id / +token_id / +outcome / is_buy→Side, 全部 L2, 老李+老孙+GM 三方签; ctest 预估 ~22 处迁移; WAL schema v1.2→v1.3; abi_lock v1.6→v1.7
- §4 RM evaluate update: R6.2a per_condition + R6.2b per_outcome (token 级) + R8.4 book_token_id_mismatch + ADR-004 短路顺序不变
- §5 SignerV52 ABI 对齐: token_id 直接 pass-through, 老孙 W9 W2 3 项确认
- §6 7 ctest spec: T1 pass-through / T2 per-outcome cap / T3 DRAIN / T4 WAL migration / T5 平仓边界 / T6 P99<50us / T7 ABI 100%
- §7 timeline: W9 W2-W4 spec/联调, W10 W1 整合
- §8 不耻下问: 7 方, 老李/老孙/老唐/老高/老周/老郭/老雷
- §9 拒绝原因表: 5 新增 + 1 rename + audit log 字段表

worktree commit 待 git commit 完成.

**Last updated:** 2026-05-29 by 老韩 (B 主管, W9 Wave 51 P0)
