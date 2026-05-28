# SignerV52 v5.3 ABI Align Spec v1

- owner: 老孙 (crypto-signing-expert, #06)
- last_review: 2026-05-29
- sprint: W9 Wave 52 P0
- status: DRAFT — 待老韩 (OrderIntent v0.5 W9 W2 ack) + 老唐 (audit v1.3 W9 W2 ack) + 老高 (abi_lock v1.7 W9 W4 ack)
- adr_ref: ADR-027 §4 Enforce-1/Enforce-2 (cite 强 enforce), ADR-009 v2 (Sonnet)
- 关联:
  - `docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md` §3.5 (SignedOrder fields)
  - `docs/RESEARCH/laoli-laoSun-handshake-v1.md` §84 (SignedOrder ABI)
  - `docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md` §4 Enforce-1/2/3
  - `docs/RESEARCH/laosun-key-management-v5.1.md` §5.4 (IPC msgpack + PIT assert)
  - `docs/RESEARCH/laotang-audit-schema-v1.1.md` §5 (4 ts透传)

---

## §1 ADR-027 cite 块

ADR-027 Enforce-1 要求所有 SignedOrder struct 变更 PR 含独立 cite 段:

```
cite:
  polymarket_ssot_cite: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md
                        §3.5 SignedOrder 字段表 (11 字段 + EIP-712 11 字段 + verifyingContract 选择规则)
  handshake_cite:       docs/RESEARCH/laoli-laoSun-handshake-v1.md
                        §84 SignedOrder ABI Hash 9c156025c5d86914 (11 字段顺序锁定)
  goalserve_ssot_cite:  N/A (signer 不消费 Goalserve 数据; outcome audit 字段由 OrderIntent 传入)
  adr_cite:             docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md
                        Enforce-1 (SSOT cite 强 enforce) + Enforce-2 (FOM 4 人 approve)
```

ADR-027 Enforce-2 要求本 spec 变更 PR 获得以下 4 人 approve:
- 老李 (#07) — Polymarket spec owner: 字段对齐 CLOB ABI
- 小段 (#37) — Goalserve spec owner: N/A 本 wave (signer 无 Goalserve 字段)
- 老郭 (代数据结构专家 IC) — 字段完整性 + 下游 ABI 对齐
- 老周 (#02) — struct layout + ABI 兼容性

---

## §2 v5.3 SignV52Request / Response 字段

### §2.1 v5.1 → v5.3 变更点摘要

| 维度 | v5.1 | v5.3 | 影响 |
|---|---|---|---|
| `token_id` | 缺失 | **新增** (uint256 string, CLOB 一等公民) | EIP-712 Order.tokenId 直接来源 |
| `side` | 缺失 | **新增** (uint8, 0=Buy 1=Sell, 与老韩 v0.5 enum 对齐) | 替代原 BuyYes/BuyNo 耦合设计 |
| `outcome` | 缺失 | **新增** (uint8, audit only, 不入签名) | 审计可读性; 与老韩 v0.5 enum 对齐 |
| IPC msgpack schema | v1.2 (v5.1) | **v1.3** | 与老唐 audit v1.3 同步 |

### §2.2 完整结构体定义

```cpp
// signer IPC v5.3 SignV52Request
// 改动: 新增 token_id / side / outcome 三字段 (ADR-027 Enforce-1 cite: handshake §84)
// 其余 v5.1 字段 (R-20 4ts, hmac, nonce, peer_pid_hint) 不动
struct SignV52Request {
    // ── R-20 四时间戳 (不变, v5.1 §5.4) ──────────────────────────────
    std::int64_t  event_ts_ns;          // ns, 上游事件真实发生时间
    std::int64_t  data_source_ts_ns;    // ns, 上游 payload 发布时间
    std::int64_t  ingestion_ts_ns;      // ns, L0 摄入 (CLOCK_MONOTONIC_RAW)
    std::int64_t  as_of_ts_ns;          // ns, OrderIntent 进 signer 时刻

    // ── OrderIntent → SignV52Request 字段 (老韩 v0.5 直传) ──────────
    std::string   condition_id;         // 0x... bytes32 hex, market 级
                                        // cite: SSOT §3.5 condition_id
    std::string   token_id;             // uint256 string (无 0x 前缀, 十进制)
                                        // ← 新增 v5.3 (ADR-027 gap 修复)
                                        // cite: SSOT §3.5 token_id + handshake §84 tokenId
    std::uint8_t  side;                 // 0=Buy, 1=Sell
                                        // ← 新增 v5.3, 与老韩 v0.5 Side enum 对齐
                                        // cite: SSOT §3.5 side + handshake §84 side
    std::int64_t  limit_price_bps;      // 价格 bps, e.g. 5500 = 0.55
                                        // cite: SSOT §3.5 limit_price_bps
    std::int64_t  size_usdc_micro;      // USD size micro (6 dec), e.g. 10_000_000 = 10 USDC
                                        // cite: SSOT §3.5 size_usdc_micro
    std::int64_t  expiration_unix_s;    // 过期时间 Unix 秒, 0 = GTC
                                        // cite: SSOT §3.5 expiration_unix_s
    std::uint8_t  signature_type;       // 必须 = 1 (Magic Safe 1-of-1, HMAC bug #2 反陷阱)
                                        // cite: SSOT §5 T-10 + handshake §84 signature_type
    std::string   maker_address;        // funder 地址 0x hex (lowercase)
                                        // cite: SSOT §3.5 maker_address
    std::string   client_order_id;      // UUID 形式客户端自定义 ID
    std::string   ts;                   // ISO8601, OrderIntent 产生时间

    // ── 复盘锚 (v5.1 4ts patch) ──────────────────────────────────────
    std::string   audit_id;             // ULID, 与老唐 audit v1.3 header audit_id 对齐

    // ── audit 用字段 (不入 EIP-712 签名) ────────────────────────────
    std::uint8_t  outcome;              // ← 新增 v5.3
                                        // 0=Yes, 1=No, 2=Over, 3=Under, 255=Unknown
                                        // 仅用于 audit emit, 不进 EIP-712 Order struct
                                        // 与老韩 v0.5 Outcome enum 一致
                                        // cite: SSOT §2.4 outcomeIndex 对齐规则

    // ── v5.1 安全字段 (不动) ─────────────────────────────────────────
    // 注: request_id / wallet_addr / typed_data / nonce / peer_pid_hint / hmac[32]
    // 继承自 laosun-key-management-v5.1.md §5.4.1 SignRequest, 此处省略重复声明
    // 实现时 SignV52Request 扩展 SignRequest 或作字段超集 struct
};

struct SignV52Response {
    std::string   signature;            // 65 bytes hex (r||s||v, base64 保留 padding)
                                        // cite: handshake §84 signature HMAC bug #4
    std::string   client_order_id;      // echo back
    bool          success;
    std::string   reject_reason;        // 若 success=false, reject code string
    WalKind       audit_wal_kind;       // R-11 paper/live WAL 区分
                                        // cite: handshake §84 OrderAck.audit_wal_kind
};
```

### §2.3 字段映射总览

| OrderIntent (老韩 v0.5) | SignV52Request (老孙 v5.3) | EIP-712 Order struct | 说明 |
|---|---|---|---|
| `condition_id` | `condition_id` | — (不进 EIP-712 Order body) | market 级元数据 |
| `token_id` | `token_id` | `tokenId` (uint256) | CLOB 一等公民, 直传 |
| `side` (0=Buy/1=Sell) | `side` | `side` (uint8) | enum 一致 |
| `limit_price_bps` | `limit_price_bps` | `makerAmount`/`takerAmount` 推算 | bps 转 amount |
| `size_usdc_micro` | `size_usdc_micro` | `makerAmount`/`takerAmount` | micro 转 amount |
| `expiration_unix_s` | `expiration_unix_s` | `expiration` (uint256) | 直传 |
| `signature_type` | `signature_type` (= 1) | `signatureType` | 必须 = 1 |
| `maker_address` | `maker_address` | `maker` (address) | funder 地址 |
| `outcome` | `outcome` | — (不进 EIP-712) | audit only |
| 4 ts | 4 ts | — | R-20 IPC 透传 |

---

## §3 EIP-712 签名输入

### §3.1 EIP-712 Order struct (11 字段, 来自 handshake §84 + SSOT §3.5)

下列字段完整列出, 不省略 (ADR-027 Enforce-1 要求):

| EIP-712 字段 | Solidity 类型 | 来源 / 计算方式 | 锁定状态 |
|---|---|---|---|
| `salt` | uint256 | 运行时 libsodium `randombytes_buf(8)` → uint64 → uint256, 防重放 | 已锁 |
| `maker` | address | `maker_address` (funder, lowercase hex → checksum address) | 已锁 |
| `signer` | address | 从 WALLET_PRIVATE_KEY 推出的 EOA | 已锁 |
| `taker` | address | `0x0000000000000000000000000000000000000000` (任意 taker) | 已锁 |
| `tokenId` | uint256 | `token_id` (十进制 uint256 string → bignum) | **v5.3 新关注** |
| `makerAmount` | uint256 | Buy side: size_usdc_micro; Sell side: token_amount (由 price 算) | 已锁 |
| `takerAmount` | uint256 | Buy side: token_amount; Sell side: size_usdc_micro | 已锁 |
| `expiration` | uint256 | `expiration_unix_s` (直转) | 已锁 |
| `nonce` | uint256 | 老叶 nonce_mgr 预留 | 已锁 |
| `feeRateBps` | uint256 | 体育市场 = 300 (3%), 来自 MarketInfo.fee_rate_bps | 已锁 |
| `side` | uint8 | `side` (0=BUY, 1=SELL) | 已锁 |
| `signatureType` | uint8 | `signature_type` = 1 (HMAC bug #2 反陷阱) | 已锁 |

**EIP-712 domain (verifyingContract 选择, SSOT §3.5 T-04):**

```
普通市场 (negRisk=false): 0x4bFb41d5B3570DeFd03C39a9A4D8dE6Bd8B8982E
negRisk 市场 (negRisk=true): 0xC5d563A36AE78145C45a50134d48A1215220f80a
```

negRisk 标志必须从 MarketInfo.neg_risk 传入 signer, 签错合约地址 = 下单被拒 (SSOT §5 T-04).

### §3.2 token_id 在 EIP-712 中的位置

**结论: token_id 进 EIP-712 message (Order struct), 不进 domain.**

- EIP-712 `domain` 字段: name, version, chainId, verifyingContract — 均为 market/chain 级元数据
- EIP-712 `message` (Order struct): 含 `tokenId` 字段, 即 outcome 级 token_id
- Polymarket CTF Exchange 合约 on-chain `Order.tokenId` 是 EIP-712 message 的 uint256 字段
- `condition_id` 不进 EIP-712 message body (仅用于 user channel 订阅 + 风控层)

**待老李 confirm (不耻下问 §8 @老李):**
- tokenId encoding: 十进制 uint256 string → `uint256` ABI encoding (大端 32 字节)
- 确认 negRisk 场景 tokenId 是否有特殊处理 (当前认为与普通市场相同, 仅 verifyingContract 不同)

---

## §4 ABI Breaking 变更记录

### §4.1 v5.1 → v5.3 breaking 变更

| 变更 | 类型 | 处理 |
|---|---|---|
| `token_id` 字段新增 | L1 (末尾追加 POD-compatible string) | 需 msgpack schema bump v1.3 |
| `side` 字段新增 | L1 (末尾追加, uint8) | 需 msgpack schema bump v1.3 |
| `outcome` 字段新增 | L1 (末尾追加, uint8, audit only) | 需 msgpack schema bump v1.3 |

依据 handshake v1 §1 等级定义: POD struct 末尾追加新字段 = L1 (老李单签). 本次 3 字段均属 L1 合集, 一次 PR 合并.

### §4.2 HMAC 4 bug 反陷阱维持 (W6 已修, v5.3 不回退)

| bug | 修复状态 | v5.3 要求 |
|---|---|---|
| HMAC bug #1: path+query 混入 base string | W6 修复 | base string = ts+method+path(无 query)+body, 维持 |
| HMAC bug #2: sigType 填 2 而非 1 | W6 修复 | signature_type 字段强校验 = 1, 维持 |
| HMAC bug #3: rstrip 截断 base64 padding | W6 修复 | base64 保留 padding, 维持 |
| HMAC bug #4: param_type 错误 | W6 修复 | 维持 W6 修复逻辑不变 |

T3 测试 spec 专门覆盖这 4 个反陷阱 (见 §5).

### §4.3 IPC msgpack schema bump

- 当前版本: v1.2 (v5.1, 含 4 ts + source enum)
- 新版本: **v1.3** (v5.3, 加 token_id + side + outcome)
- schema bump 与老唐 audit v1.3 同步 (同一 wave W9 W2)
- 向后兼容: v1.2 reader 收到 v1.3 message 会忽略未知字段 (msgpack 语义), 但 signer v5.3 拒绝 v1.2 message (缺 token_id = REJECT INVALID_INTENT)

---

## §5 测试 Spec

以下 7 个 ctest 覆盖 v5.3 全部新增行为. T1-T7 均为 IC 自测范围, 老沈联动验收 T4.

### T1: token_id ABI binding end-to-end

```
测试目标: SignV52Request{token_id="1677202003548168...", outcome=0, side=0} → EIP-712 Order.tokenId
          byte-equal 对比 Polymarket 官方 Python py-clob-client 的 EIP-712 hash 输出

输入:
  condition_id = "0xa9db6005902..." (SSOT §3 样本)
  token_id = "1677202003548168512111076196662317438975560192301735320827449539424843146463"
  side = 0 (Buy)
  limit_price_bps = 5500
  size_usdc_micro = 10_000_000
  expiration_unix_s = 0 (GTC)
  signature_type = 1
  negRisk = false → verifyingContract = 0x4bFb41d5...

验收标准:
  1. EIP-712 structHash(Order) byte-equal 对比 golden (py-clob-client 同参数输出)
  2. recovered_address == signer_EOA (ecrecover 验证)
  3. token_id uint256 encoding: big-endian 32 bytes, 无截断

注: golden 生成用 curl + py-clob-client 离线跑 (Python 测试辅助, 不进生产)
```

### T2: side enum 0/1 round-trip

```
测试目标: side=0 (Buy) / side=1 (Sell) 与老韩 v0.5 Side enum 一致性

验收标准:
  1. SignV52Request{side=0} → EIP-712 Order.side == 0 → recovered_sig 有效
  2. SignV52Request{side=1} → EIP-712 Order.side == 1 → recovered_sig 有效
  3. SignV52Request{side=2} → signer 拒绝 (INVALID_INTENT, reject_reason="invalid_side")
  4. 老韩 v0.5 OrderIntent.side 字段值 0/1 → SignV52Request.side 0/1 无 remapping
```

### T3: HMAC 4 bug 反陷阱

```
测试目标: 4 个历史 HMAC bug 在 v5.3 均不复现

T3a (HMAC bug #1): base string 生成
  input: GET /clob/orders?market=0xa9db... (含 querystring)
  expected: base_string = ts + "GET" + "/clob/orders" + "" (无 querystring, 无 body)
  forbidden: base_string 含 "?market=..."

T3b (HMAC bug #2): sigType assert
  input: SignV52Request{signature_type=2}
  expected: signer reject, reject_reason="invalid_signature_type"
  input2: SignV52Request{signature_type=1}
  expected2: signer accept (进 EIP-712 流程)

T3c (HMAC bug #3): base64 padding
  input: signature bytes 长度 % 3 != 0 (需 padding)
  expected: base64 output 含 "=" padding 字符
  forbidden: rstrip("=") 截断

T3d (HMAC bug #4): param_type (具体验证内容依 W6 修复细节, 由 IC 实施时对照 W6 修复记录填入)
```

### T4: SecureBuffer sodium_memzero 维持

```
测试目标: 私钥材料离开 SecureBuffer scope 后内存清零 (W8 W1 已落, v5.3 不回退)

验收标准 (与老沈联动):
  1. SecureBuffer<64> 析构后, /proc/self/mem 读取原地址 → 全零 (或 SIGSEGV 若已 mprotect)
  2. 新增字段 token_id/side/outcome 作为 string/uint8 进入 signer 后, 不单独需要 SecureBuffer
     (这三个字段是公开订单参数, 非私钥材料)
  3. WALLET_PRIVATE_KEY 在 SignV52Request 处理全程不以明文出现在任何 log/audit payload

工具: valgrind --tool=memcheck 或 AddressSanitizer (test build 加 -fsanitize=address)
```

### T5: paper / live mode 共用 stcpp_crypto_ed25519

```
测试目标: paper engine (ADR-011) 与 live 共用同一 signer binary + 同一 stcpp_crypto_ed25519 lib

验收标准:
  1. paper mode (audit_wal_kind=PAPER) 签名输出与 live mode 签名输出 byte-equal (同私钥同参数)
  2. paper mode 不写入 position / pnl_ledger / nonce_ledger (R-11 红线, 由 WAL kind check 拦)
  3. stcpp_crypto_ed25519 不区分 paper/live 模式 (crypto 层无环境判断)
  4. mock_private_key (paper test key, 非真实) 与 live key 走相同签名路径
```

### T6: ctest 旧 v5.1 cases migration

```
测试目标: v5.1 已有的 ctest 在 v5.3 全部通过 (无 regression)

迁移范围:
  - v5.1 PIT assert 4 不等式测试 (§5.4.2 pit_assert)
  - v5.1 reject code TS_ORDER_VIOLATED / TS_FUTURE / TS_UNKNOWN_SRC
  - v5.1 audit emit signer.pit.passed / rejected / inferred_src

迁移方式:
  - 测试文件 tests/unit/test_signer_v51_pit.cpp → test_signer_v53_pit.cpp (rename + add token_id param)
  - 旧 SignRequest 构造改为 SignV52Request 构造, 新增字段填 valid default
  - 期望行为不变
```

### T7: hot path latency P99 < 8us

```
测试目标: v5.3 新增 token_id (string copy) 不导致 hot path latency 超 W6 baseline 8us P99

方法:
  N = 100_000 次 SignV52Request 签名 (同一参数, 排除 I/O)
  测量: CLOCK_MONOTONIC_RAW sign_request_ts → sign_complete_ts (v5.1 §5.4.1 SignResponse 字段)
  baseline: W6 P99 < 8us (laosun-key-management-v5.1.md 已验)

验收标准:
  P50 < 3us, P99 < 8us, P999 < 15us
  token_id string copy 成本: std::string move (已有 buf), 不应影响 P99

注: token_id 最长 77 字节, Small String Optimization (SSO) 边界 (GCC libstdc++ SSO = 15B).
    超 SSO 会堆分配. 热路径需评估是否换 FixedString<80> 避免堆分配.
    若 P99 > 8us 因 SSO 退化 → 升级为 using TokenId = std::array<char,80> + 长度字段.
    本 spec 阶段标记为 TODO, IC 实施时测量后决定.
```

---

## §6 与老韩 OrderIntent v0.5 联动

### §6.1 ABI 链路

```
OrderIntent (老韩 v0.5, 风控通过后)
    ↓  直传 (零 remapping)
SignV52Request (老孙 v5.3)
    ↓  EIP-712 signed
SignedOrder (Polymarket CLOB)
    ↓  POST /clob/orders
Polymarket 撮合
```

### §6.2 三字段传递约定

| 字段 | OrderIntent 定义 | SignV52Request 约定 | EIP-712 落点 |
|---|---|---|---|
| `token_id` | `std::string token_id` (uint256 string) | 直传, 不转换 | `Order.tokenId` (uint256 bignum encoding) |
| `side` | `uint8_t side` (0=Buy, 1=Sell) | 直传, 不重新映射 | `Order.side` (uint8) |
| `outcome` | `uint8_t outcome` (0=Yes,1=No,...) | 直传, 仅 audit | 不进 EIP-712 |

**约束:**
- token_id 传递链路中不做任何 string 变换 (不加 0x 前缀, 不截断, 不 normalize)
- side enum 值 0/1 在 OrderIntent → SignV52Request → EIP-712 三层保持一致, 不转换
- outcome 字段仅用于 audit emit (AET_ORDER_PLACED payload), 不影响签名结果
- negRisk 字段由 MarketInfo 传入 signer (不经 OrderIntent), 决定 verifyingContract 选择

### §6.3 接口边界 (老韩出 OrderIntent v0.5 后核对)

老韩 W9 W2 出 OrderIntent v0.5 时, 需与老孙 confirm:
1. `token_id` 字段类型: `std::string` (uint256 十进制, 无前缀)
2. `side` 字段类型: `uint8_t` 或 enum `Side : uint8_t`, 值 0=Buy/1=Sell
3. `outcome` 字段类型: `uint8_t` 或 enum `Outcome : uint8_t`, 值 0=Yes/1=No/2=Over/3=Under
4. 4 ts 字段: TimestampQuad (handshake §3 已锁) 直接透传进 SignV52Request 4 ts 字段

---

## §7 Timeline

| 节点 | 负责人 | 截止 | 依据 |
|---|---|---|---|
| W9 W2: spec final + 老韩 sync | 老孙 | W9 W2 | 本文 draft → 与老韩 OrderIntent v0.5 字段 align |
| W9 W2: audit schema v1.3 ack | 老唐 | W9 W2 | msgpack v1.3 bump 与 audit v1.3 同步 |
| W9 W3: signer_v53.cpp 实施 + 8 ctest | 老孙 IC | W9 W3 | §5 T1~T7 全绿 (7 test, IC 本地跑) |
| W9 W4: ABI 联调 | 老韩 + 老唐 + 老高 | W9 W4 | abi_lock v1.7 CI grep + 三方 ABI hash verify |
| W10 W1: integration test | 老孙 + 老韩 + 老陈 | W10 W1 | OrderIntent v0.5 → signer v5.3 → mock CLOB 端到端 |

---

## §8 不耻下问

| 问谁 | 问题 | 优先级 | 背景 |
|---|---|---|---|
| @老韩 | OrderIntent v0.5 字段确认: token_id 类型/格式, side enum 0/1, outcome enum 值, 4 ts 透传方式 | P0 | §6.3, 本 wave W9 W2 并行 |
| @老唐 | audit schema v1.3: msgpack v1.3 schema 格式确认; AET_ORDER_PLACED payload 加 token_id + outcome 字段 | P0 | §4.3, 本 wave W9 W2 并行 |
| @老高 | abi_lock v1.7 CI grep: SignV52Request 新字段 token_id/side 加入 C2/C3 grep 检查; msgpack v1.3 schema file 路径 | P0 | §4.3, W9 W4 |
| @老李 | token_id 在 EIP-712 中的 encoding 确认: 十进制 uint256 string → uint256 bignum → 32 byte big-endian ABI encoding? negRisk 场景 tokenId 是否特殊? | P1 | §3.2 open question |
| @老郭 | layer 2 architecture review: signer v5.3 ABI 与 IPolymarketClient F-02 SubmitOrder 接口边界是否需要 L2 变更 (handshake v1 §1 等级判断) | P1 | §4.1, ADR-027 Enforce-2 |
| @老雷 | GM ack: SignerV52 v5.3 spec 纳入 W9 sprint backlog, W9 W3 IC 实施截止 | P0 | 本 wave 任务对齐 |

---

## §9 约束汇总

1. **语言:** signer_v53.cpp 全部 C++20, 禁 Rust, 禁 Python 进生产 (CLAUDE.md §10)
2. **R-20:** 4 ts 不等式 event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts 在 signer PIT assert 强校验 (v5.1 §5.4.2 维持)
3. **R-11:** paper mode 不污染真账本; audit_wal_kind=PAPER 由 signer 下游 WAL 区分 (v5.1 §5.2 维持)
4. **私钥:** 老孙边界 = application 签名层; 私钥存储由老沈设计, 老孙实施接口 (不越界)
5. **下单决策:** 老孙不接; side/token_id 由 OrderIntent 传入, 老孙仅签名不判断是否应下单
6. **ADR-027 Enforce-1:** 本 spec PR description 必含 §1 cite 块
7. **ADR-027 Enforce-2:** PR 合并需老李 + 老郭 + 老周 三方 approve (小段 N/A)
8. **红线:** 私钥明文不落盘不进日志; 违者系统权限暂停 (CLAUDE.md §8)

---

**last_updated:** 2026-05-29 by 老孙 (crypto-signing-expert, #06)
**next_review:** W9 W2 (老韩 OrderIntent v0.5 sync)
