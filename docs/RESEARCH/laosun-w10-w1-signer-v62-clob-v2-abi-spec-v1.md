---
owner: 老孙 (crypto-signing-expert, #06)
last_review: 2026-05-29
sprint: W10 Wave 91 P0
status: DRAFT — 待老李 W10 W2 handshake v2 ack + 老韩 OrderIntent v0.6 ack + 老唐 audit schema v1.4 ack
adr_ref: ADR-027 Enforce-1, ADR-029 flow, ADR-032 local-first
trigger: 老李 W85 PR #15 — CLOB V2 于 2026-04-28 上线, V1 废弃, 老孙 SignerV52 v5.3 = V1 ABI, 生产订单全部被拒
---

# SignerV62 v6.2 — CLOB V2 ABI Spec v1

## §1 ADR-027 cite 块 (强 enforce)

```
cite:
  polymarket_ssot_cite: docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md
                        §3.1 EIP-712 Order struct 变更 (V1→V2)
                        §3.2 V2 合约地址
                        §3.4 CLOB V2 POST /order wire body
                        §2.3 新费用体系
  goalserve_ssot_cite:  N/A (signer 不消费 Goalserve 数据; outcome/side 由 OrderIntent 传入)
  handshake_cite:       派老李 W10 W2 handshake v2 (当前 laoli-laoSun-handshake-v1.md §84 = V1 ABI,
                        W10 W2 升为 handshake v2 含 V2 字段; 本 spec 先行, handshake v2 随后 co-sign)
  adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
                        Enforce-1 (SSOT cite 强 enforce) + Enforce-2 (FOM 4 人 approve)
```

**ADR-027 Enforce-2 — 本 spec PR 须获得以下 4 人 approve:**

| reviewer | 负责人 | review 范围 |
|---|---|---|
| Polymarket spec owner | 老李 (#07) | V2 字段对齐 CLOB V2 ABI |
| Goalserve spec owner | 小段 (#37) | N/A (signer 无 Goalserve 字段) |
| 数据结构专家 (代) | 老郭 | 字段完整性 + 下游 ABI 对齐 |
| 架构 review | 老周 (#02) | struct layout + ABI 兼容性 |

---

## §2 SignV62Request — 新版 IPC struct

### §2.1 完整结构体定义

```cpp
// signer IPC v6.2 SignV62Request
// CLOB V2 ABI: 移除 nonce/feeRateBps/expiration(signed)/taker
//              新增 timestamp_ms / metadata / builder
// cite: laoli-w9-w5-polymarket-market-research-update-v1.md §3.1 §3.4

struct SignV62Request {
    // ── R-20 四时间戳 (维持, ADR R-20 红线) ─────────────────────────────
    // 不等式: event_ts_ns <= data_source_ts_ns <= ingestion_ts_ns <= as_of_ts_ns
    std::int64_t  event_ts_ns;          // ns, 上游事件真实发生时间 (数据源自带, 禁 now())
    std::int64_t  data_source_ts_ns;    // ns, 上游 payload 发布时间
    std::int64_t  ingestion_ts_ns;      // ns, L0 摄入 (CLOCK_MONOTONIC_RAW)
    std::int64_t  as_of_ts_ns;          // ns, OrderIntent 进 signer 时刻

    // ── 市场标识 (维持, V2 无变化) ────────────────────────────────────────
    std::string   condition_id;         // 0x... bytes32 hex (66 char), market 级
    std::string   token_id;             // uint256 十进制 string (无 0x, 最多 77 位)
                                        // EIP-712 Order.tokenId 直接来源

    // ── 方向 / 结果 (维持) ───────────────────────────────────────────────
    std::uint8_t  side;                 // 0=Buy, 1=Sell (Side enum 底层值)
    std::uint8_t  outcome;              // 0=Yes,1=No,2=Over,3=Under (audit only, 不入签名)

    // ── 定价 (维持, 单位变化: USDC.e → pUSD) ────────────────────────────
    std::int64_t  limit_price_bps;      // 价格 bps, e.g. 5500 = 0.55

    // V2: 抵押品由 USDC.e 换为 pUSD, 单位语义不变 (micro = 1e-6)
    // cite: laoli-w9-w5 §2.1 抵押品变更; 老沈 W10 W2 抵押品迁移 spec
    std::int64_t  size_pUSD_micro;      // pUSD 数量 micro (1e-6), e.g. 10_000_000 = 10 pUSD

    // ── V2 新增: timestamp 替代 nonce ────────────────────────────────────
    // V1: nonce (老叶 nonce_mgr) — V2 中废弃
    // V2: timestamp_ms 毫秒时间戳, 保证唯一性; 同地址同 ms 内不可重复提交
    // cite: laoli-w9-w5 §3.1 "timestamp (uint256) — 毫秒时间戳, 替代 nonce 保证唯一性"
    // cite: laoli-w9-w5 §3.4 "timestamp=1748476800000" V2 wire body
    std::int64_t  timestamp_ms;         // EIP-712 Order.timestamp (uint256, ms)

    // ── V2 新增: metadata (bytes32) ──────────────────────────────────────
    // 应用元数据, bytes32 hex string (0x 前缀 + 64 hex chars = 66 chars)
    // 不使用时填 "0x0000000000000000000000000000000000000000000000000000000000000000"
    // cite: laoli-w9-w5 §3.1 "metadata (bytes32) — 应用元数据"
    std::string   metadata;             // bytes32 hex (0x 前缀), EIP-712 Order.metadata

    // ── V2 新增: builder (bytes32, optional) ─────────────────────────────
    // Builder code (gasless relayer 专用); 不使用时填 bytes32(0)
    // cite: laoli-w9-w5 §3.1 "builder (bytes32) — builder code (optional, zero if not used)"
    // cite: laoli-w9-w5 §2.2 builder-relayer-client SDK
    std::string   builder;              // bytes32 hex (0x 前缀), 不用则填 bytes32(0)

    // ── 签名配置 (维持) ──────────────────────────────────────────────────
    std::string   maker_address;        // funder 地址 0x hex (lowercase, 20 bytes)
    std::string   client_order_id;      // UUID 形式客户端自定义 ID
    std::uint8_t  signature_type;       // 必须 = 1 (Magic Safe EOA; HMAC bug #2 反陷阱)

    // ── audit ─────────────────────────────────────────────────────────────
    std::string   audit_id;             // ULID, 与老唐 audit v1.4 header audit_id 对齐
};
```

### §2.2 V1 → V2 字段对比表

| 字段 | v5.3 (V1, 已废弃) | v6.2 (V2, 当前) | 变更说明 |
|---|---|---|---|
| `nonce` | `std::int64_t nonce` (老叶 nonce_mgr) | **移除** | V2 废弃; 唯一性由 timestamp_ms 保证 |
| `feeRateBps` | 隐含 300 (3%), 进 EIP-712 | **移除** (签名层) | V2 fee 不入签名; exchange 自动 calc |
| `expiration_unix_s` | 进 EIP-712 Order.expiration | **移除** (签名层) | V2 不进 EIP-712; wire body 仍有 expiration 用于 GTD |
| `taker` | `0x000...000` (任意 taker), 进 EIP-712 | **移除** | V2 EIP-712 Order struct 无 taker 字段 |
| `size_usdc_micro` | USDC.e micro | `size_pUSD_micro` | 抵押品 USDC.e → pUSD (rename + 语义) |
| `timestamp_ms` | 不存在 | **新增** `std::int64_t timestamp_ms` | V2 ms 时间戳替代 nonce, 进 EIP-712 |
| `metadata` | 不存在 | **新增** `std::string metadata` | bytes32 应用元数据, 进 EIP-712 |
| `builder` | 不存在 | **新增** `std::string builder` | bytes32 optional builder code, 进 EIP-712 |
| 其余字段 | condition_id / token_id / side / outcome / limit_price_bps / maker_address / client_order_id / signature_type / audit_id / 4 ts | 全部维持 | 不变 |

### §2.3 OrderIntent v0.6 → SignV62Request 字段映射

| OrderIntent v0.6 字段 | SignV62Request v6.2 字段 | EIP-712 Order 字段 | 说明 |
|---|---|---|---|
| `condition_id` | `condition_id` | — (不进 EIP-712 body) | market 级, 订阅/风控用 |
| `token_id` | `token_id` | `tokenId` (uint256) | CLOB 一等公民, 直传 |
| `side` (uint8) | `side` | `side` (uint8) | 0=BUY/1=SELL 直传 |
| `outcome` (uint8) | `outcome` | — (不进签名) | audit only |
| `limit_price_bps` | `limit_price_bps` | 推算 `makerAmount/takerAmount` | bps 转 amount |
| `size_pUSD_micro` | `size_pUSD_micro` | `makerAmount/takerAmount` | micro 转 amount |
| `timestamp_ms` (新, OrderIntent v0.6) | `timestamp_ms` | `timestamp` (uint256) | ms 直传 |
| `metadata` (新, OrderIntent v0.6) | `metadata` | `metadata` (bytes32) | bytes32 hex 直传 |
| `builder` (new, optional) | `builder` | `builder` (bytes32) | 不用则 bytes32(0) |
| `maker_address` | `maker_address` | `maker` (address) | funder 地址 |
| `signature_type` | `signature_type` (= 1) | `signatureType` | 强制 = 1 |
| 4 ts | 4 ts | — | R-20 透传 |

**OrderIntent v0.6 须新增字段 (老韩 W10 W2 配套):**
- `timestamp_ms: int64_t` — 替代 nonce; Orchestrator 层按系统时间填入 (ms)
- `metadata: std::string` — bytes32 hex; 默认填 bytes32(0) string; 策略层可自定义
- `builder: std::string` — bytes32 hex optional; 默认填 bytes32(0) string

---

## §3 EIP-712 Domain v2

### §3.1 V2 domain 定义

```
EIP-712 domain (V2, cite: laoli-w9-w5 §3.1):
  name:              "Polymarket CTF Exchange"
  version:           "2"          ← V1 是 "1", V2 关键变更
  chainId:           137          (Polygon PoS, 不变)
  verifyingContract: (见 §3.2)
```

注: API auth domain (`ClobAuthDomain`) 的 version 仍是 `"1"`, 不随 Exchange domain 升级.

### §3.2 verifyingContract 选择 (V2 新地址)

| 市场类型 | V1 地址 (已废弃) | V2 地址 (当前) |
|---|---|---|
| 普通市场 (negRisk=false) | `0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E` | `0xE111180000d2663C0091e4f400237545B87B996B` |
| negRisk 市场 (negRisk=true) | `0xC5d563A36AE78145C45a50134d48A1215220f80a` | `0xe2222d279d744050d28e00520010520000310F59` |

**negRisk 标志由 MarketInfo.neg_risk 传入 signer (不经 OrderIntent), 决定 verifyingContract 选择.**
签错合约地址 = 签名无效 = 下单被拒. 必须从 MarketInfo 动态查, 不可硬编码.

### §3.3 EIP-712 Order struct typeHash (V2)

V2 Order struct 字段 (cite: laoli-w9-w5 §3.1 + ctf-exchange-v2 Solidity):

```
Order(
  uint256 salt,
  address maker,
  address signer,
  uint256 tokenId,
  uint256 makerAmount,
  uint256 takerAmount,
  uint8   side,
  uint8   signatureType,
  uint256 timestamp,
  bytes32 metadata,
  bytes32 builder
)
```

**V2 vs V1 字段对比:**

| V1 Order struct 字段 | V2 Order struct 字段 | 变更 |
|---|---|---|
| `uint256 salt` | `uint256 salt` | 不变 |
| `address maker` | `address maker` | 不变 |
| `address signer` | `address signer` | 不变 |
| `address taker` | **移除** | V2 无 taker 字段 |
| `uint256 tokenId` | `uint256 tokenId` | 不变 |
| `uint256 makerAmount` | `uint256 makerAmount` | 不变 |
| `uint256 takerAmount` | `uint256 takerAmount` | 不变 |
| `uint256 expiration` | **移除** (签名层) | V2 expiration 不进 EIP-712 |
| `uint256 nonce` | **移除** | V2 废弃 nonce |
| `uint256 feeRateBps` | **移除** | V2 fee 不入签名 |
| `uint8 side` | `uint8 side` | 不变 |
| `uint8 signatureType` | `uint8 signatureType` | 不变 |
| — | `uint256 timestamp` | **V2 新增** (ms, 替代 nonce) |
| — | `bytes32 metadata` | **V2 新增** |
| — | `bytes32 builder` | **V2 新增** |

字段顺序须与 ctf-exchange-v2 Solidity 合约 `_hashOrder()` 中 `abi.encode()` 顺序完全一致; 字段顺序影响 typeHash 计算. **@老李 W10 W2 handshake v2 确认精确字段顺序.**

---

## §4 fee 字段移除 — fee 体系外置

### §4.1 V1 vs V2 fee 处理

| 维度 | V1 (已废弃) | V2 (当前) |
|---|---|---|
| feeRateBps 字段 | 进 EIP-712 Order struct (`uint256 feeRateBps`) | **不入签名** |
| fee 计算时机 | 嵌入签名, 下单前确定 | exchange operator 在 match time 按 market category 自动 calc |
| Sports fee 公式 | V1 geeRateBps=300 固定值 | `fee = C × 0.03 × p × (1-p)` (C=shares, p=price) |
| Maker fee | 嵌入 | 0 (maker 永远不付费) |
| Maker rebate | 无 | 25% of taker fees, 每日结算 |

### §4.2 RM 内部 fee evaluate (不入签名, 仅内部使用)

fee 字段移出签名层后, RM evaluate 仍需 fee 估算用于净期望值计算:

```
RM 内部 fee estimate (老韩 OrderIntent v0.6 配套):
  sports_taker_fee_estimate = size_pUSD_micro * 0.03 * limit_price_bps/10000 * (1 - limit_price_bps/10000)
  net_edge = gross_edge - sports_taker_fee_estimate   (taker 视角)
  maker 视角: net_edge = gross_edge + sports_maker_rebate_estimate (25% × taker_fee_pool)
```

**这些 fee 字段不进 SignV62Request, 不进 EIP-712.** RM evaluate 在 OrderIntent 通过风控后才生成 SignV62Request.

### §4.3 派单: fee 估算规则 → @老韩

老韩 OrderIntent v0.6 spec 需包含:
- RM R-fee-1: `sports_taker_fee_bps` 内部估算字段 (不进 IPC)
- RM R-fee-2: `net_edge >= NET_EDGE_FLOOR` 校验 (fee 后净期望)
- RM R-fee-3: maker rebate 25% 计入做市策略的 net PnL 估算

---

## §5 pUSD 迁移

### §5.1 抵押品变更概述 (cite: laoli-w9-w5 §2.1 坑 V2-3)

| 维度 | V1 | V2 |
|---|---|---|
| 抵押品代币 | USDC.e | pUSD (ERC-20, Polymarket 发行, backed by USDC) |
| pUSD 合约地址 | — | `0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB` (proxy) |
| 充值路径 | — | `CollateralOnramp.wrap()` USDC.e → pUSD |
| onramp 合约 | — | `0x93070a847efEf7F70739046A929D47a521F5B8ee` |

### §5.2 老孙边界 (spec 不涉抵押品实施)

pUSD 抵押品迁移涉及:
- 链上 approve 地址更新 (CTF Exchange V2 = `0xE1111...`)
- CollateralOnramp.wrap() 调用
- position ledger pUSD balance tracking

**以上超出老孙签名边界. 派老沈 W10 W2 抵押品迁移 spec.**

老孙仅需:
- SignV62Request 中 `size_pUSD_micro` 字段名正确反映 pUSD 单位
- EIP-712 `makerAmount/takerAmount` 仍以 pUSD micro 为单位 (与 USDC.e micro 数值语义相同, 仅代币变)

---

## §6 Timeline + 实施排期

| 节点 | 负责人 | 截止 | 依赖 |
|---|---|---|---|
| **W10 W1**: 本 spec final (laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md) | 老孙 | W10 W1 | 本 wave |
| **W10 W2**: 老李 handshake v2 + 确认 EIP-712 字段顺序 | 老李 | W10 W2 | 本 spec |
| **W10 W2**: 老孙 C++ 实施 SignerV62 (signer_v62.cpp) | 老孙 | W10 W2 | 老李 handshake v2 |
| **W10 W2**: 老沈 SignV62Request transformer + pUSD 迁移 spec | 老沈 | W10 W2 | 本 spec |
| **W10 W3**: 老沈 ABI 联调 (OrderIntent v0.6 → SignV62 → mock CLOB) | 老沈 + 老孙 | W10 W3 | 老孙 v6.2 实施完 |
| **W10 W4**: 老唐 audit schema v1.4 (V2 字段: timestamp_ms/metadata/builder + pUSD rename) | 老唐 | W10 W4 | 本 spec + 老沈 transformer |
| **W10 W4 末**: ctest 全过 V2 链路 (~30-50 新 cases, 见 §8) | 老沈/老唐/小卢 | W10 W4 | 老唐 v1.4 |

---

## §7 与 V1 SignerV52 共存 (paper/live 切换)

### §7.1 共存策略

| 模式 | 使用哪个 signer | 说明 |
|---|---|---|
| backtest (V2 切换日 2026-04-28 之前的历史数据) | V1 v5.3 | 历史回测复现 V1 时代订单行为 |
| backtest (V2 切换日 2026-04-28 之后) | V2 v6.2 | V2 签名逻辑 |
| paper mode (当前) | V2 v6.2 | Paper engine 模拟 V2 生产链路 |
| live mode | V2 v6.2 **仅此** | V1 在生产被拒, 禁止 live 用 V1 |

### §7.2 ADR-018 build switch

```cmake
# CMakeLists.txt — ADR-018 兼容
option(STCPP_CLOB_VERSION "CLOB ABI version: v1 or v2" "v2")

if(STCPP_CLOB_VERSION STREQUAL "v1")
    # 仅 backtest binary 允许: 链接 signer_v52.cpp (V1 EIP-712 domain version "1")
    target_compile_definitions(stcpp_backtest PRIVATE STCPP_CLOB_V1)
elseif(STCPP_CLOB_VERSION STREQUAL "v2")
    # paper + live binary: 链接 signer_v62.cpp (V2 EIP-712 domain version "2")
    target_compile_definitions(stcpp_paper PRIVATE STCPP_CLOB_V2)
    target_compile_definitions(stcpp_live  PRIVATE STCPP_CLOB_V2)
endif()
```

**约束:**
- `STCPP_CLOB_VERSION=v1` 只允许 backtest binary, 不允许 paper/live binary
- live binary 如果链接 V1 signer → CI build check FAIL (老高 abi_lock v1.8 grep 拦截)
- paper mode 必须用 V2 signer (ADR-011 paper/live binary 共用签名路径)

### §7.3 V1 signer_v52.cpp 退役计划

- W10 W4: signer_v62.cpp ctest 全过后, signer_v52.cpp 标注 `[[deprecated]]`
- W12: backtest 验证 V2 历史数据后, signer_v52.cpp 移入 `src/legacy/` 归档
- V1 signer 不得删除 (历史回测需要), 只归档不运行于 paper/live

---

## §8 ABI Breaking 评估

### §8.1 全链路 ABI breaking 影响

| 链路节点 | breaking 变更 | 影响等级 |
|---|---|---|
| OrderIntent (老韩 v0.5→v0.6) | 新增 `timestamp_ms/metadata/builder`, 移除 `nonce` (如有), rename `size_usdc_micro→size_pUSD_micro` | L2 (老韩+老孙+GM 三方签) |
| SignV62Request (本 spec) | 移除 4 字段 + 新增 3 字段 + rename 1 字段 = 8 处变更 | L2 (ADR-027 Enforce-2 FOM) |
| EIP-712 Order struct | typeHash 变化 (V2 新字段) + domain version "1"→"2" + verifyingContract 新地址 | **P0 breaking** |
| audit schema (老唐 v1.3→v1.4) | AuditRecord 新增 `timestamp_ms/metadata/builder` (uint256/bytes32) + rename `size_usdc_micro→size_pUSD_micro` | L1 末尾追加 |
| WAL (老王) | WAL schema version bump (msgpack v1.3→v1.4) | L1 |
| REST API positions | `collateral_type: "USDC.e"→"pUSD"` (老沈/老王负责) | 下游通知 |

### §8.2 ctest update 范围 (~30-50 cases)

| 测试文件 | 需更新 case 数 | 变更内容 |
|---|---|---|
| `tests/unit/test_signer_v62_eip712.cpp` (新建) | ~15 | V2 EIP-712 domain/Order struct hash golden 对比 |
| `tests/unit/test_signer_v62_request.cpp` (新建) | ~8 | SignV62Request 构造 + 字段校验 |
| `tests/unit/test_signer_v52_pit.cpp` (迁移) | ~5 | V1 cases 迁移到 v6.2 (4 ts PIT assert 维持) |
| `tests/integration/paper_e2e_smoke_test.cpp` | ~7 | OrderIntent v0.6 → SignV62Request 链路 |
| `tests/integration/audit_chain_verify_test.cpp` | ~5 | AuditRecord v1.4 字段 (timestamp_ms 等) |
| `tests/perf/bench_signer_v62.cpp` (新建) | ~3 | hot path P99 latency 基线 (≤ 8us) |
| **合计** | **~43 cases** | |

### §8.3 HMAC 4 bug 反陷阱维持 (v6.2 不回退)

V1 v5.3 修复的 4 个 HMAC bug 在 v6.2 完整继承:

| bug | 修复状态 | v6.2 要求 |
|---|---|---|
| HMAC bug #1: querystring 混入 base string | W6 修复 | base string = ts+method+path(无 query)+body, 维持 |
| HMAC bug #2: sigType=2 而非 1 | W6 修复 | signature_type 字段强校验 = 1, 维持 |
| HMAC bug #3: rstrip 截断 base64 padding | W6 修复 | base64 保留 padding, 维持 |
| HMAC bug #4: param_type 错误 | W6 修复 | 维持 W6 修复逻辑不变 |

### §8.4 nonce manager 退役

老叶 `laoye-nonce-manager-design-v1.md` 设计的 nonce manager 在 V2 中废弃.
V2 唯一性由 `timestamp_ms` (毫秒级) 保证.

**高频场景注意 (cite: laoli-w9-w5 §3.5 坑 V2-2):**
同地址同 ms 内不可重复提交. 若需 >1 order/ms, 需 `salt` 字段区分 (salt 由 `libsodium randombytes_buf(8)` 生成, 天然唯一). 老叶 nonce manager 标注 deprecated, 不再驱动唯一性.

---

## §9 不耻下问

| 对象 | 问题 | 优先级 | 时限 |
|---|---|---|---|
| **@老李** | V2 EIP-712 Order struct 精确字段顺序 (typeHash abi.encode 顺序); negRisk V2 verifyingContract = 0xe2222...? | P0 | W10 W2 EOD |
| **@老李** | handshake v2 doc (laoli-laoSun-handshake-v2.md): V2 ABI lock + 字段顺序 + byte-equal golden | P0 | W10 W2 |
| **@老沈** | OrderIntent → SignV62Request transformer: pUSD 迁移 spec + CollateralOnramp.wrap() 调用时机 | P0 | W10 W2 |
| **@老韩** | OrderIntent v0.6 spec: 新增 timestamp_ms/metadata/builder 三字段; nonce 字段是否存在 v0.5 中需确认移除 | P0 | W10 W2 |
| **@老唐** | audit schema v1.4: AuditRecord 新增 timestamp_ms (int64), metadata (char[66]), builder (char[66]) + rename size_usdc_micro→size_pUSD_micro; WAL schema v1.3→v1.4 magic bump | P0 | W10 W4 |
| **@老高** | abi_lock v1.8: CI grep 新增 V2 keyword (timestamp_ms/metadata/builder/pUSD); grep 拦截 live binary 链接 V1 signer | P1 | W10 W2 |
| **@总裁** | Wave 91 P0 ack: SignerV62 V2 ABI spec 纳入 W10 sprint backlog, 优先级 = MVP 阻塞 | P0 | W10 W1 EOD |

---

## §10 约束汇总

1. **语言:** signer_v62.cpp 全部 C++20, 禁 Rust, 禁 Python 进生产 (CLAUDE.md §10 + §12.3)
2. **R-20:** 4 ts 不等式在 signer PIT assert 强校验 (v5.3 逻辑维持, 不回退)
3. **R-11:** paper mode 不污染真账本; audit_wal_kind=PAPER 由 signer 下游 WAL 区分
4. **私钥:** 老孙边界 = application 签名层; 私钥存储由老沈设计 (不越界)
5. **下单决策:** 老孙不接; side/token_id/timestamp_ms 由 OrderIntent 传入, 老孙仅签名
6. **ADR-027 Enforce-1:** 本 spec PR description 必含 §1 cite 块
7. **ADR-027 Enforce-2:** PR 合并需老李 + 老郭 + 老周三方 approve
8. **ADR-032:** sub-agent push 后立刻汇报, 不等远端 CI
9. **red line:** 私钥明文不落盘不进日志; 违者系统权限暂停 (CLAUDE.md §8)
10. **V2 domain version "2":** EIP-712 domain version 字段必须为字符串 `"2"`, 不可用整数 2

---

**last_updated:** 2026-05-29 by 老孙 (crypto-signing-expert, #06, Wave 91 P0)
**next_review:** W10 W2 (老李 handshake v2 + 老韩 OrderIntent v0.6 sync)
