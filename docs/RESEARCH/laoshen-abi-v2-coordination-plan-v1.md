---
owner: 老沈 (B 风控合规部 IC, security-engineer #B-002)
last_review: 2026-05-29
sprint: Sprint-3 W10
status: DRAFT — 待老韩 (B 主管) + 老郭 (F 协调) 联合 ack 后转 APPROVED
trigger: 老韩派单 — ABI V2 全链路协调计划 (小颖 M1 audit 标注 PE-01~04 blocker, Wave 97-99)
adr_ref:
  - ADR-027: ABI enforce (Enforce-1/2/3/4)
  - ADR-011: paper/live binary 物理隔离
  - abi_lock: tests/ci_grep/abi_lock.py v1.7
  - handshake: docs/RESEARCH/laoli-laoSun-handshake-v1.md §1 (L0/L1/L2/L3 等级)
cite:
  rm_spec_cite:     laoshen-rm-v0.5-field-freeze-spec-v1.md §1 (OrderIntent v0.6 字段冻结)
  laosun_v62_cite:  laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2.3
  laotang_v14_cite: include/stcpp/observability/audit_record.hpp v1.4
  laohan_cite:      laohan-rm-v0.5-integration-spec-v1.md (老韩 W10 Wave 99)
  m1_audit_cite:    docs/RESEARCH/xiaoying-m1-acceptance-spec-v2.md §4 PE-01~04
security_role: 本计划由老沈出具; ABI 治理规则约束实施安全排程; 不含私钥实施细节 (见独立 KMS 文档)
---

# ABI V2 全链路协调计划 v1

> 本文是协调计划, 不是实施文档。ABI V2 的范围、依赖排序、三方签清单、回归防护、观测层解耦在此
> 一次性明确, 供 Wave 97-99 三 Wave 的老沈/老唐/小冯各自开工前对齐。
>
> 实施代码由老韩主管分配各 IC 完成; 本文由老沈执笔、老韩审计视角把关。
> 不 git commit / 不 git push (GM 统一合并)。

---

## §1 ABI V2 范围 — 哪些锁定 struct/文件要改, 改什么, 为什么

### §1.1 触发背景

小颖 M1 audit (xiaoying-m1-acceptance-spec-v2.md §4) 标注:
- **PE-01** ABI V2 全链路 merge → wip (blocker: Wave 97-99 待 push)
- **PE-02** 老沈 V2 transformer ctest pass → wip
- **PE-03** 老唐 audit schema v1.4 → wip
- **PE-04** WSS 接真 + reconnect chaos → wip

根因是 paper runtime "REST 接真 state" 的核心链路:
`Signal → OrderIntent v0.6 → RiskGateway.evaluate() → PaperSigner.Sign() → VirtualFill → PositionLedger`
中, **SignRequest (signer_iface.hpp)** 仍保留 V1 语义字段 (`market_id`, `outcome: string`, `size_usdc`),
与已完成 ABI lock v1.8 的 `OrderIntent v0.6` 脱节, 导致 transformer 无法正确映射, REST state 无法接真。

### §1.2 锁定文件 × 改动内容 × 原因

#### F1 — `include/stcpp/risk/risk_gateway.hpp`

**ABI lock 等级 (abi_lock.py):** 已锁定, 任何 struct/enum 改动触发 Rule 4

**当前状态:** OrderIntent v0.6 已完成 (ABI lock v1.8), 字段集冻结。
见 laoshen-rm-v0.5-field-freeze-spec-v1.md §1.2: FROZEN 状态, 无进一步字段改动。

**ABI V2 改动:** 无需改动 struct 字段; 需改动的是:
- `AuditRecord` (同文件) 的 `metadata` / `builder` 字段确认写入路径连通
  (当前头文件已声明 v0.6 字段, emit_audit_() 实现层须补全, 在 .cpp 不触发 ABI 锁)
- `check_invalid_intent_()` 内新增 timestamp_ms V2 校验 (D2/D3 delta, 实现层)

**ABI 等级:** L0 (接口签名不变, 仅 .cpp 实现补全) — 无需三方签

**结论:** F1 头文件 **不改动**; 对应 .cpp 改动需引用 ABI ref 行标记 N/A 或 L0。

---

#### F2 — `include/stcpp/signer/signer_iface.hpp`

**ABI lock 等级 (abi_lock.py):** 已锁定 (v1.7 W9)

**当前状态 (代码实测):** `SignRequest` 保留 V1 字段:
```
std::string_view  market_id{};       // V1: condition 级 (已废弃)
std::string_view  outcome{};         // V1: "YES"/"NO" 字符串 (已废弃)
double            size_usdc{0.0};    // V1: 不含 micro 精度
```
与 OrderIntent v0.6 的 `condition_id`/`token_id`/`side:Side`/`size_pUSD_micro`/`timestamp_ms`/`metadata`/`builder` 完全不对齐。

**ABI V2 改动 (须走 L2 三方签):**

| 字段 | 改动类型 | 原值 | 新值 | 原因 |
|---|---|---|---|---|
| `market_id` → `condition_id` | rename (L2 ABI break) | `string_view market_id` | `string_view condition_id` | 对齐 OrderIntent v0.6 + SSOT §2.3 |
| `outcome: string_view` → `token_id: string_view` | 语义替换 (L2 ABI break) | `string_view outcome` ("YES"/"NO") | `string_view token_id` (uint256 十进制) | token 级主键; signer V62 EIP-712 Order.tokenId |
| `side: uint8_t` 新增 | 追加字段 (L1) | 无 | `uint8_t side{0}` (Buy=0/Sell=1) | EIP-712 Order.side; transformer 透传 |
| `size_usdc: double` → `size_pUSD_micro: int64_t` | 类型+语义变更 (L2 ABI break) | `double size_usdc` | `int64_t size_pUSD_micro` | micro 精度; pUSD rename (V2 CLOB) |
| `timestamp_ms: int64_t` 新增 | 追加字段 (L1) | 无 | `int64_t timestamp_ms{0}` | V2 EIP-712 Order.timestamp; 替代 nonce |
| `metadata: string_view` 新增 | 追加字段 (L1) | 无 | `string_view metadata{}` | V2 EIP-712 Order.metadata bytes32 |
| `builder: string_view` 新增 | 追加字段 (L1) | 无 | `string_view builder{}` | V2 EIP-712 Order.builder optional |

**综合等级: L2** (含字段类型/语义改动) — 须老李 + 老孙 + GM 三方签

`SignResponse` 本次不改 (仅有必要时补 `outcome: uint8_t` 供 audit 回填; 视老孙 V62 spec 结论)。

---

#### F3 — `include/stcpp/infra/wal/position_record.hpp`

**ABI lock 等级 (abi_lock.py):** 已锁定 (v1.7 W9), POD struct sizeof 校验 152B

**当前状态:** `market_id: char[32]` + `outcome: uint8_t` (0=YES/1=NO), V1 语义。

**ABI V2 需求:** paper runtime REST 接真 state 要求 PositionRecord 能追溯到 `token_id` (不仅是 market_id+outcome),
以支持 per-token 仓位查询 (ADR-040 per-token 修正)。

**改动分析:**

PositionRecord 是落盘 POD, sizeof=152B, 每个字段有 offsetof 静态断言。直接增加 `token_id` (变长 string)
会打破 trivially copyable 约束和 sizeof=152。**方案选择:**

- **方案 A (推荐):** 不改 PositionRecord; token_id 由上层 `risk::PositionLedger` (F5) 持有 map 维护,
  不落 WAL。WAL replay 后通过 condition_id+outcome 重建 token 映射。
  ABI 等级: **L0** (PositionRecord 不变), 无需三方签。

- **方案 B (备选):** market_id[32] 扩展为 token_id[80] (uint256 max = 77 digit + NUL),
  outcome 语义不变。sizeof 从 152 → 200B (新 pad 布局)。
  ABI 等级: **L3** (字段类型/大小改变 + sizeof 变化 + offsetof 全变) — 须老郭+老韩+GM 三方签 + ADR。

**安全建议 (老沈立场):** 选方案 A。WAL 格式向后兼容, replay 不破坏。
如需方案 B, 必须同时发布 WAL schema version bump + 迁移工具, 且与老王 WAL framework 联动,
代价高、风险大, 当前 M1 阶段不必要。

**结论:** F3 (`position_record.hpp`) **本次不改** (方案 A); 相应 F5 改动。

---

#### F4 — `include/stcpp/infra/wal/position_ledger.hpp`

**ABI lock 等级 (abi_lock.py):** 已锁定 (v1.7 W9, PositionLedger / PositionKey 关键词守护)

**当前状态:** `query_position(market_id)` 以 `market_id: string` 为 key; 内部 map key = market_id string。

**ABI V2 改动 (须走 L2 三方签):**

`query_position` 接口从 `market_id` 语义升级为 per-token 查询, 需支持 `token_id` 查询路径:

| 接口 | 改动类型 | 原签名 | 新签名 | 原因 |
|---|---|---|---|---|
| `query_position` | 新增重载 (L1) | `PositionState query_position(string_view market_id)` | 保留原有 + 新增 `query_position_by_token(string_view token_id)` | ADR-040 per-token 仓位; REST /positions endpoint |
| 内部 `states_` map key | 改动 (L2) | `unordered_map<string, PositionState>` key = market_id | 双 map: 保留 market_id map + 新增 token_id → PositionState map | token_id 是 per-outcome 主键 |

**注意:** `infra::wal::PositionLedger` (F4) 与 `risk::PositionLedger` (F5) 是两个不同类,
F4 是 WAL 持久层, F5 是 risk 侧读 API。本次 F4 改 L2 (双 map 内部变更 + 新重载),
F5 参见下条。

**综合等级: L2** — 须老李 + 老孙 + GM 三方签

---

#### F5 — `include/stcpp/risk/position_ledger.hpp` (risk::PositionLedger)

**ABI lock 等级 (abi_lock.py):** 已锁定 (PositionLedger / PositionKey 关键词守护)

**当前状态:** `apply_fill(condition_id, token_id, outcome, VirtualFill)` 已含正确参数,
内部 `token_positions_` 以 token_id 为 key (正确)。`get_all_positions()` 返回 `vector<PositionView>` 含
condition_id/token_id/outcome (已对齐 ADR-040)。

**ABI V2 改动:** 接口基本已对齐, 细节需确认:
- `apply_fill` 的 `VirtualFill` 是否携带完整 V2 字段 (timestamp_ms 等) — 不影响本接口签名 (L0)
- 确保 `get_per_outcome_exposure()` 返回格式与 REST endpoint 期望一致 (实现层, L0)

**综合等级: L0** (接口签名已正确, 仅确认实现对齐) — 无需三方签

---

#### F6 — `include/stcpp/strategy/signal_iface.hpp`

**ABI lock 等级 (abi_lock.py):** 已锁定 (Side enum / Outcome enum 关键词守护)

**当前状态:** `Outcome` (Yes/No/Home/Draw/Away/Over/Under) + `Side` (Buy=0/Sell=1) 已完成 ABI lock v1.7。
枚举值锁定, 不可改动。`SignalOutput.suggested_size_usdc` 语义为 signed int64。

**ABI V2 改动:** 需确认 `SignalOutput.suggested_size_usdc` 是否需 rename 为 `suggested_size_pUSD_micro`
以对齐 OrderIntent v0.6 的 `size_pUSD_micro`。

评估: rename = L2。考虑到 V2 语义已在 OrderIntent 层澄清, `signal_iface.hpp` 内保持
`suggested_size_usdc` (文档注释补充 "语义为 pUSD micro") 更安全, 避免 rename 引入 ABI break。
**结论: 不改名**; 在注释中注明 pUSD 等价语义。等级 L0, 无需三方签。

---

### §1.3 ABI V2 范围汇总表

| 文件 | 是否改动 | 改动内容摘要 | ABI 等级 | 三方签要求 |
|---|---|---|---|---|
| `risk/risk_gateway.hpp` | 不改 (仅 .cpp 实现) | emit_audit_ 补全 V2 字段透传 | L0 | 无 |
| `signer/signer_iface.hpp` | **改动 (P0)** | SignRequest: 5 字段 rename+type+新增 | **L2** | 老李+老孙+GM |
| `infra/wal/position_record.hpp` | **不改 (方案 A)** | 维持 152B POD; token 映射上移 | L0 | 无 |
| `infra/wal/position_ledger.hpp` | **改动** | 新增 query_position_by_token + 双 map | **L2** | 老李+老孙+GM |
| `risk/position_ledger.hpp` | 基本已对齐 | 实现层确认 | L0 | 无 |
| `strategy/signal_iface.hpp` | 不改 | 注释补充 pUSD 语义 | L0 | 无 |

---

## §2 Wave 97-99 依赖与顺序

### §2.1 三 Wave 任务定义

| Wave | Owner | 核心任务 | 产出 |
|---|---|---|---|
| **Wave 97** | **老沈** (B IC) | (1) `signer_iface.hpp` SignRequest V2 字段升级 (L2 三方签后实施); (2) `transformer.hpp` OrderIntent v0.6 → SignedOrder V2 映射补全; (3) PositionLedger integration test +6 | transformer 实现对齐; ctest +6 |
| **Wave 98** | **老唐** (B IC) | (1) AuditRecord v1.4 字段 timestamp_ms/metadata/builder 写入路径补全 (.cpp); (2) BLAKE3 audit chain replay verify tool; (3) R-20 4 ts chain verify ctest +5 | audit schema v1.4 全通; replay tool; ctest +5 |
| **Wave 99** | **小冯** (D IC) | (1) GoalserveInplayClient cpp HTTP 1s poll + gzip + SPSC ring; (2) reconnect chaos test 5 次断线注入 < 5s; (3) PolymarketCLOBSubscriber reconnect chaos; ctest +4 | WSS 接真 state 就绪; chaos test |

### §2.2 先后依赖关系

```
[L2 三方签完成]
      |
      v
Wave 97: 老沈 — signer_iface.hpp SignRequest V2 (头文件)
      |
      +---> Wave 98: 老唐 — audit schema v1.4 (.cpp emit_audit_ 补全)
      |               (依赖 OrderIntent v0.6 字段定义, 不依赖 Wave 97 头文件完成)
      |               可与 Wave 97 并行启动, 但须 Wave 97 merge 后联调
      |
Wave 99: 小冯 — GoalserveInplayClient + chaos
               (与 Wave 97/98 完全独立, 可并行)
```

**串行约束:**
1. L2 三方签 → Wave 97 头文件改动 (必须先签才能改锁定文件)
2. Wave 97 merge → Wave 98 emit_audit_ 联调 (audit 需消费 OrderIntent v0.6 完整字段)
3. Wave 97 + Wave 98 merge → REST 接真 state 联调 (PE-05 blocker 解锁)

**可并行部分:**
- Wave 98 的 BLAKE3 replay tool 和 ctest 可在 Wave 97 merge 前独立推进 (不依赖 SignRequest)
- Wave 99 全程与 Wave 97/98 独立 (Goalserve 不依赖 signer/audit 链)
- Wave 97 的 PositionLedger integration test +6 可在 SignRequest 头文件改动后立即开始 (不等 Wave 98)

### §2.3 阻塞升级路径

```
Wave 97 被阻 (三方签未到位 or 技术争议) → 4h 内升 老韩 → 48h 不解 → 老胡协调 → 老雷 P0 介入
Wave 98 被阻 (老唐 audit schema 联调) → 老韩 + 老沈 当日 standup
Wave 99 被阻 (AWS 账号权限 or SPSC ring 依赖) → 小余 主管 → 老周统筹
```

---

## §3 ABI 治理 — 三方签 + handshake ref 清单

### §3.1 handshake-v1.md 等级回顾

| 等级 | 名称 | 审批要求 |
|---|---|---|
| L0 | Safe | 无需会签 |
| L1 | Patch | 老李单签 |
| **L2** | **Major** | **老李 + 老孙 + GM 三方签** |
| **L3** | **Breaking** | **老郭 (架构) + 老韩 (RM) + GM 三方签** |

### §3.2 本次 ABI V2 三方签清单

#### 签单 ABI-V2-S1: SignRequest V2 升级 (F2, L2)

| 项目 | 内容 |
|---|---|
| **文件** | `include/stcpp/signer/signer_iface.hpp` |
| **等级** | L2 — Major (字段类型改变 + rename) |
| **PR body 必含** | `ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-02 L2` + `三方签` |
| **签字方** | 老李 (polymarket-protocol-expert) + 老孙 (crypto-signing-expert) + GM (老雷) |
| **前置条件** | 老孙 SignerV62 spec (laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md) 已 ack; 老李 IPolymarketClient F-02 调用约定兼容性确认 |
| **abi_lock.py 触发** | Rule 1 (锁定文件) + Rule 2 (L2 → 三方签) + Rule 4 (struct 关键词 SignV52Request/SignRequest) |
| **static_assert 更新** | `signer_iface.hpp` 无 sizeof/offsetof 断言 (含 string_view, 无 POD); 单测 `test_pm_client_abi.cpp` 需更新 ABI hash 期望值 |
| **安全审查点** | (1) `token_id` 为 string_view, caller 保证生命周期 (不得悬垂); (2) `metadata`/`builder` 格式校验在 transformer 层, signer 不重复校验; (3) `timestamp_ms` 填入者为 Orchestrator, signer 只做 > 0 前置校验 |

#### 签单 ABI-V2-S2: infra::wal::PositionLedger 双 map 升级 (F4, L2)

| 项目 | 内容 |
|---|---|
| **文件** | `include/stcpp/infra/wal/position_ledger.hpp` |
| **等级** | L2 — Major (新增接口 + 内部存储结构变更) |
| **PR body 必含** | `ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-06 L2` + `三方签` |
| **签字方** | 老李 (polymarket-protocol-expert) + 老孙 (crypto-signing-expert) + GM (老雷) |
| **前置条件** | 方案 A 确认 (PositionRecord 不改); 老王 WAL framework 架构兼容性确认 |
| **abi_lock.py 触发** | Rule 4 (PositionLedger / PositionKey 关键词) |
| **安全审查点** | 双 map 内存一致性: apply_fill 写双 map 须原子更新 (单 writer 路径已保证, 但需代码审查); WAL replay 时 token_id map 重建路径须正确 |

#### 无需三方签的改动 (L0, 仅记录)

| 改动 | 文件 | 原因 |
|---|---|---|
| emit_audit_() 补全 V2 字段 | `src/stcpp/risk/risk_gateway.cpp` | .cpp 实现层, 接口签名不变 |
| BLAKE3 replay verify tool | `tools/audit_replay_verify.cpp` | 新增文件 (L0) |
| transformer.hpp V2 mapping | `include/stcpp/signer/transformer.hpp` | 仅依赖 OrderIntent/SignRequest 接口, 无新 struct |
| GoalserveInplayClient | `include/stcpp/data/goalserve_client.hpp` (可能新增) | 新文件/新接口 (L0) |

---

## §4 回归防护

### §4.1 static_assert 更新

**F2 signer_iface.hpp:** `SignRequest` 含 `std::string_view` (非 POD), 无 sizeof static_assert。
但须更新:
- `tests/unit/test_pm_client_abi.cpp`: ABI hash 期望值 (SignedOrder ABI hash 不变; SignRequest 如有 hash test 需更新)
- `tests/unit/test_signer_v52.cpp` + `test_signer_v62.cpp`: 字段存在性测试 (field presence static assert)

**F4 infra::wal::position_ledger.hpp:** `PositionState` 无 sizeof 断言 (非 POD map 内部结构);
需补充:
```cpp
static_assert(std::is_default_constructible_v<PositionState>);
static_assert(std::is_copy_constructible_v<PositionState>);
```

**PositionRecord (不改):** 维持现有 `static_assert(sizeof(PositionRecord) == 152)` 不变。
Wave 97-99 任何 PR 不得触碰此断言。

### §4.2 ABI 一致性测试矩阵

| 测试文件 | 验证内容 | Wave 关联 |
|---|---|---|
| `tests/unit/test_signer_v62.cpp` | SignRequest V2 字段存在性 + V62 签名路径 | Wave 97 |
| `tests/unit/test_pm_client_abi.cpp` | ABI hash 稳定性 (F-02 SignedOrder) | Wave 97 |
| `tests/unit/test_audit_schema_v14.cpp` | AuditRecord v1.4 timestamp_ms/metadata/builder 写入 | Wave 98 |
| `tests/integration/paper_e2e_smoke_test.cpp` | OrderIntent v0.6 → SignRequest V2 → AuditRecord v1.4 E2E | Wave 97+98 联调 |
| `tests/replay/r20/r_r20_07_paper_30d_attestation.cpp` | R-20 4 ts 链全程不违例 | Wave 97+98 |
| `tests/integration/r11_paper_pollution_test.cpp` | paper 不写真账本 (R-11) | 持续守护 |

### §4.3 paper/live 物理隔离不破 (R-7/R-11)

Wave 97 改 `signer_iface.hpp` 时须确认:
1. `IPaperSigner` / `ILiveSigner` / `IBacktestSigner` 三标签类继承链保持不变
2. `SignResponse.audit_wal_kind` 字段不改 (paper signer 强制 `WalKind::PaperAudit`)
3. CMake target 隔离: `stcpp_signer_paper` 不链 `stcpp_signer_live` (ADR-011 强制)

Wave 98 改 audit emit 时须确认:
1. paper binary 中 AuditEmitter 只写 `WalKind::PaperAudit` 路径
2. 新增 V2 字段 (`timestamp_ms`/`metadata`/`builder`) 的 audit emit 在 paper/live 两路径均正确

**CI 守护:** `tests/ci_grep/r33_5host_paper.py` + `tests/integration/r11_paper_pollution_test.cpp`
不得因 Wave 97-99 改动而 fail。

---

## §5 与已落快照解耦 — 观测层不受 ABI V2 反向耦合

### §5.1 三个观测侧快照 (只读)

| 组件 | 文件 | 所有者 | 架构约束 |
|---|---|---|---|
| `OrderBookSnapshotHub` | `include/stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp` | 小冯 (小石在做) | vCPU0 double-buffer; 读端 read-only atomic; 无 RM/signer 依赖 |
| `RmDebugSnapshot` | `include/stcpp/risk/rm_debug_snapshot.hpp` | 老沈 | lock-free ring, 单 writer RM; 读端 debug_api 只读; RejectRow 黑名单字段不含私钥/nonce |
| `LedgerSnapshotHub` | (尚未建档, 小石负责) | 小石 | 观测侧只读快照 (类 OrderBookSnapshotHub 模式) |

### §5.2 ABI V2 不破坏观测层的约束

**OrderBookSnapshotHub (Wave 99 关联):**
- 小冯 Wave 99 改 GoalserveInplayClient 和 CLOBSubscriber reconnect, 不改 `OrderBookFeatures` struct
- `OrderBookFeatures` 的 4-ts 字段已对齐 R-20; Wave 99 不改此 struct
- **结论: OrderBookSnapshotHub 不受 ABI V2 影响**

**RmDebugSnapshot (Wave 97 关联):**
- Wave 97 改 `signer_iface.hpp` SignRequest, 不改 `RejectRow` struct
- `build_reject_row<OI, RD>()` 模板通过 `intent.condition_id`/`intent.side`/`intent.size_pUSD_micro`/
  `intent.price`/`decision.decision_ts_ns` 取值; OrderIntent v0.6 字段名称和类型均已稳定
- `size_usdc` 字段在 RejectRow 内从 `intent.size_pUSD_micro / 1e6` 换算, 不依赖 SignRequest
- **结论: RmDebugSnapshot 不受 ABI V2 影响; Wave 97 不改 rm_debug_snapshot.hpp**

**LedgerSnapshotHub (与 Wave 97-99 独立):**
- 小石正在设计, 应参照 OrderBookSnapshotHub 模式: 只读快照, 无 ABI 锁定文件依赖
- Wave 97 F4 改 `infra::wal::position_ledger.hpp` 时, 须确认 LedgerSnapshotHub 接口
  (如有) 不需要 PositionRecord POD 的 sizeof 信息 (方案 A 不改 sizeof, 此约束自然满足)
- **结论: LedgerSnapshotHub 须遵守"观测侧只读、无反向耦合"设计原则; Wave 97 F4 不破坏其约束**

### §5.3 解耦原则 (写入本计划供小石参考)

观测层快照的 ABI V2 解耦原则:
1. 观测层 struct (RejectRow/OrderBookFeatures/未来 LedgerRow) 不直接 include ABI 锁定头文件
2. 通过模板参数或接口注入取值 (如 `build_reject_row<OI, RD>` 模板)
3. 观测层字段集固定, 不随 OrderIntent/SignRequest ABI break 同步变化
4. 如需 ABI break 传导至观测层, 须走独立 PR + 老韩 review (观测层安全守护者)

---

## §6 关键路径 + 并行分析 + 风险

### §6.1 关键路径 (串行必须)

```
Step 1: L2 三方签完成 (老李 + 老孙 + GM)
         ↓
Step 2: Wave 97 老沈 — signer_iface.hpp SignRequest V2 PR merge
         ↓
Step 3: Wave 97+98 联调 — OrderIntent v0.6 → SignRequest V2 → emit_audit_ V2 字段 E2E
         ↓
Step 4: ctest 全过 (Wave 97 +6, Wave 98 +5) + CI abi_lock v1.7 全绿
         ↓
Step 5: REST 接真 state 解锁 (PE-05 就绪条件)
         ↓ (W11 依赖)
Step 6: paper runtime W11 启动 checklist 9 项全绿
```

**关键路径总耗时估算:** Step 1 (三方签) = 阻塞因子; Step 2-4 = 2-3 工作日; Step 5-6 = W11 阶段。

### §6.2 可与关键路径并行的工作

| 工作 | 并行条件 | Owner |
|---|---|---|
| Wave 98 BLAKE3 replay tool 骨架 | 无依赖, 立即开始 | 老唐 |
| Wave 99 GoalserveInplayClient HTTP poll | 无依赖, 立即开始 | 小冯 |
| Wave 99 reconnect chaos test 框架 | 无依赖 | 小冯 |
| F4 PositionLedger 双 map 实现 (L2 三方签后) | 三方签 S2 完成后 | 老沈/老王协调 |
| risk::PositionLedger L0 实现确认 | 无 ABI 锁定, 立即确认 | 老沈 |
| signal_iface.hpp 注释补充 (L0) | 无三方签要求 | 老沈 |

### §6.3 风险登记

| 风险 ID | 描述 | 级别 | Mitigation | Owner |
|---|---|---|---|---|
| R-ABI-01 | L2 三方签延迟 (老李/老孙/GM 三方未在 24h 内 ack) → Wave 97 阻塞 | P0 | 老韩提前 24h 拉三方对齐会; 老胡 4h 升级通道 | 老韩 (B 主管) |
| R-ABI-02 | signer_iface.hpp 改动破坏已有 test_signer_v52/v62 单测 | P1 | Wave 97 PR 前先在本地全跑 ctest; 老孙联调验证 | 老沈 + 老孙 |
| R-ABI-03 | Wave 98 emit_audit_() 新字段填充与 audit_record.hpp v1.4 字段偏移不对齐 | P1 | 老唐 audit schema 测试先行 (replay verify); 联调时双方对 sizeof 断言 | 老唐 + 老沈 |
| R-ABI-04 | F4 PositionLedger 双 map 在 WAL replay 时 token_id map 重建逻辑错误 | P1 | 需补 restore_from_wal 对应单测; 与老王 WAL framework 联调 | 老沈 + 老王 |
| R-ABI-05 | Wave 99 GoalserveInplayClient SPSC ring 依赖 ADR-017 小石 interface 未就绪 | P1 | Wave 99 先用临时 simple queue; ADR-017 就绪后热替换 | 小冯 + 老周 |
| R-ABI-06 | PositionRecord (F3) 方案 A → 运行时 token_id map 与 WAL replay 数据不一致 | P2 | 写明 replay 后须通过 REST /positions 重建 token map; 加 integration test 覆盖 | 老沈 + 老唐 |
| R-R11-01 | Wave 97 SignRequest V2 改动无意间引入 paper/live signer 共用路径 | P0 | CMake target 审查 (stcpp_signer_paper 链接图); CI r11 test 守护 | 老沈 |
| R-R20-01 | Wave 97 transformer 映射时 timestamp_ms 被错误默认为 0 (RM 会拒绝) | P0 | transformer 测试覆盖 timestamp_ms > 0 路径; Orchestrator 层责任明确 | 老沈 |

---

## §7 回报摘要 (给老韩/老胡)

### ABI V2 范围

**需改动的 ABI 锁定文件:**
- `include/stcpp/signer/signer_iface.hpp` (F2): SignRequest V2 升级, L2 级别
- `include/stcpp/infra/wal/position_ledger.hpp` (F4): 双 map 升级, L2 级别

**不改动的 ABI 锁定文件:**
- `include/stcpp/risk/risk_gateway.hpp` (F1): OrderIntent 已冻结, 仅 .cpp 实现补全
- `include/stcpp/infra/wal/position_record.hpp` (F3): 维持 152B POD (方案 A)
- `include/stcpp/risk/position_ledger.hpp` (F5): 接口已对齐
- `include/stcpp/strategy/signal_iface.hpp` (F6): enum 不改

### Wave 顺序

**关键路径:** L2 三方签 → Wave 97 (老沈) → Wave 97+98 联调 → ctest 全过 → REST 接真 state

**可并行:** Wave 99 (小冯) 全程独立; Wave 98 骨架部分独立

### 三方签清单

| 签单 | 文件 | 等级 | 签字方 |
|---|---|---|---|
| ABI-V2-S1 | `signer/signer_iface.hpp` | L2 | 老李 + 老孙 + GM |
| ABI-V2-S2 | `infra/wal/position_ledger.hpp` | L2 | 老李 + 老孙 + GM |

### 关键路径

L2 三方签 (Step 1) 是当前唯一 P0 阻塞因子。三方签一旦到位, Wave 97-99 可在 2-3 工作日内
完成 merge 并解锁 REST 接真 state (PE-05)。

### 文件路径

- 本计划: `/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/laoshen-abi-v2-coordination-plan-v1.md`
- ABI lock 守护: `/Users/wangweibo/code/sports-trader-cpp/tests/ci_grep/abi_lock.py`
- Handshake 等级: `/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/laoli-laoSun-handshake-v1.md §1`
- OrderIntent 冻结契约: `/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/laoshen-rm-v0.5-field-freeze-spec-v1.md §1`
- SignRequest 现状: `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/signer/signer_iface.hpp` (L60-76)
- M1 audit PE 项: `/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/xiaoying-m1-acceptance-spec-v2.md §4`

---

**END plan v1.**

**待 ack:** 老韩 (B 主管, RM 主权) + 老郭 (F 协调, 架构否决权) — ack 后状态转 APPROVED; 老李+老孙+GM 三方签启动。
**Last updated:** 2026-05-29 by 老沈 (B 风控合规部 IC, security-engineer #B-002)
