---
owner: 老孙 (crypto-signing-expert, #06)
last_review: 2026-05-31
sprint: 实盘测试 — 小额真实成交验证
status: FINAL — 供 GM 落地 L1 签名半边; 待老李 L2 CLOB payload + 小白安全 co-review
co_reviewer: 小白 (security-engineer, #27) — §4 私钥安全必须 co-review 后才上生产
cite:
  polymarket_ssot: docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md §3.1 §3.2 §3.4
  signer_spec:     docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §2 §3 §8
  hmac_bug:        docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md §A §D
  handshake:       docs/RESEARCH/laoli-laoSun-handshake-v1.md (V1 ABI, V2 待升)
---

# L1 签名半边实施计划 v1 — 实盘 <$0.1 测试单

## §0 任务边界

老孙负责: 私钥安全读取 → SignerV62 构造 → SignV62Request 构造 → Sign() 调用 → 产出
signature 字符串 + maker_address → 交给老李的 SubmitOrder。

老孙不负责: L2 HMAC 认证 headers / POST /order wire body 组装 / 网络发送 (老李边界)。
老孙不负责: 私钥存储方案设计 (小白/老沈边界)，仅负责应用层签名实施。

---

## §1 SignV62Request 构造 — 全字段来源

### §1.1 已知条件 (测试单示例)

```
token_id = "<从 gamma /events 查到的体育市场 YES outcome token_id, uint256 十进制>"
side     = BUY  (0)
price    = 0.55 (55% probability, 即 5500 bps)
size     = $0.05 USDC  (< $0.1 测试单; micro 单位: 50_000)
market   = 普通 sports 市场, negRisk = false
```

### §1.2 完整字段来源表

| SignV62Request 字段 | 来源 | 具体值/来源路径 | 备注 |
|---|---|---|---|
| `event_ts_ns` | OrderIntent 透传 | 上游 WSS/REST payload timestamp × 1e6 | R-20: 禁 now() 替代 |
| `data_source_ts_ns` | OrderIntent 透传 | 同上游 payload | R-20 |
| `ingestion_ts_ns` | OrderIntent 透传 | 本地 CLOCK_REALTIME 摄入时刻 | R-20 |
| `as_of_ts_ns` | OrderIntent 透传 | Orchestrator 决策快照时刻 | R-20 |
| `data_source_ts_source` | transformer_v62 硬填 | `0` (UpstreamPayload) | spec-7 |
| `condition_id` | OrderIntent.condition_id | `"0x<bytes32>"` (66 chars) | spec-4: 不能误填 token_id |
| `token_id` | OrderIntent.token_id | uint256 十进制 string, 无 0x 前缀 | spec-1: 零变换 pass-through |
| `side` | `static_cast<uint8_t>(intent.side)` | 0=BUY, 1=SELL | spec-2: 禁条件重映射 |
| `outcome` | `static_cast<uint8_t>(intent.outcome)` | 0=Yes | audit only, 不进 EIP-712 |
| `limit_price_bps` | `(int64_t)(intent.price × 10000 + 0.5)` | 5500 (= 0.55) | transformer 已做 |
| `size_pUSD_micro` | OrderIntent.size_pUSD_micro | 50_000 (= $0.05 × 1e6) | pUSD micro |
| `timestamp_ms` | Orchestrator 填: `chrono::system_clock::now().count() / 1e6` | 毫秒时间戳, e.g. 1748476800000 | spec-10: 必须非零; V2 替代 nonce |
| `metadata` | OrderIntent.metadata (默认) | `"0x0000...0000"` (66 chars) | spec-9: ^0x[0-9a-f]{64}$ |
| `builder` | OrderIntent.builder (默认) | `"0x0000...0000"` (66 chars) | spec-9: 不用 builder 填零 |
| `signature_type` | **不触碰** — 依赖 SignV62Request 默认值 | `1` (Magic Safe EOA 1-of-1) | HMAC bug #2 修正; transformer spec-8 |
| `maker_address` | **外层填**: Orchestrator 从 .env `POLYMARKET_FUNDER_ADDRESS` | `"0x<lowercase 40 chars>"` | transformer 不生成; 见 §3.2 |
| `client_order_id` | **外层填**: Orchestrator 生成 UUID v4 | e.g. `"550e8400-e29b-41d4-a716-446655440000"` | dedup key |
| `audit_id` | 来自 `RiskDecision.audit_id` (非零 ULID, 16B) | 由 RiskGateway.evaluate() 返回 | BUG-W5-001 防御 |

### §1.3 transformer_v62 负责的字段 vs Orchestrator 负责的字段

transformer_v62 (`to_sign_v62_request`) 自动填: 4 ts / data_source_ts_source / condition_id /
token_id / side / outcome / limit_price_bps / size_pUSD_micro / timestamp_ms / metadata /
builder / audit_id。

**Orchestrator 在 transformer 调用后必须手动填**:
- `req.maker_address` = `.env` 的 `POLYMARKET_FUNDER_ADDRESS`
- `req.client_order_id` = 新生成 UUID v4

transformer_v62.hpp 注释原文 (line 177-178):
> "透传 maker_address (由外层 Orchestrator 填入, transformer 不生成)"
> "调用方须在拿到 SignV62Request 后填入 maker_address + client_order_id"

---

## §2 EIP-712 Domain V2

### §2.1 Domain 参数

原文 cite (laosun-w10-w1 §3.1 + signer_v62.hpp kCtfExchangeV2Addr):

```
name:              "Polymarket CTF Exchange"
version:           "2"          <- 字符串 "2"; 不是整数 2; V1 是 "1"
chainId:           137          (Polygon PoS, 不变)
verifyingContract: 由 negRisk 标志决定 (动态查 MarketInfo.neg_risk)
```

### §2.2 verifyingContract 选择

原文 cite (laoli-w9-w5 §3.2, signer_v62.hpp 常量):

| negRisk | verifyingContract |
|---|---|
| false (普通 sports 市场) | `0xE111180000d2663C0091e4f400237545B87B996B` |
| true (negRisk 市场) | `0xe2222d279d744050d28e00520010520000310F59` |

**关键**: 测试单必须先查 `GetMarketInfo(condition_id).neg_risk`，动态决定 verifyingContract。
不可硬编码 negRisk=false，万一选错合约地址 = 签名无效 = 订单被拒。

### §2.3 V2 Order struct typeHash

原文 cite (laoli-w9-w5 §3.1, laosun-w10-w1 §3.3):

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

注意: 字段顺序影响 typeHash (abi.encode 顺序)。V2 **无** taker / nonce / feeRateBps / expiration
字段。这四个字段仅存在于 V1，V2 全部移除出 EIP-712 struct。

### §2.4 makerAmount / takerAmount 计算

```
# BUY 方向 (side=0):
makerAmount = size_pUSD_micro                     # 买方支付金额 (pUSD micro)
takerAmount = size_pUSD_micro * (1 - price)       # 买方收到的对价 (shares micro, = size / price × (1-price))

# 实际精确公式 (避免浮点误差, 用整数):
# 对于 BUY: 买入 size_pUSD_micro / price_bps * 10000 = shares
# makerAmount = size_pUSD_micro  (支付的 pUSD)
# takerAmount = size_pUSD_micro * (10000 - price_bps) / price_bps  (收到的 shares pUSD)

# 示例: size=50_000 micro, price_bps=5500 (55%):
# makerAmount = 50_000
# takerAmount = 50_000 * (10000 - 5500) / 5500 = 50_000 * 4500 / 5500 ≈ 40_909

# SELL 方向 (side=1): makerAmount 和 takerAmount 互换
# makerAmount = shares (要卖的)
# takerAmount = shares * price_bps / (10000 - price_bps)  (收到的 pUSD)
```

**注意**: GM 实施 SubmitOrder 时需要把 limit_price_bps + size_pUSD_micro 转成
makerAmount + takerAmount 填入 wire body (老李边界，但老孙提供公式)。

---

## §3 SignV62Response → SignedOrder.signature 格式转换

### §3.1 当前 paper 模式实际产出格式 (CRITICAL gap)

**现状已知**:
- `signer_v62.cpp` 当前 live mode 分支: 直接返回 `SignV62Error::InternalError` ("mode_not_paper")
- paper mode: 使用 Ed25519 detached 签名 (64 bytes), 存于 `SignV62Response.signature` (vector<uint8_t>)
- Ed25519 是 paper 的 mock 算法; **live mode 需要 secp256k1 ECDSA (EIP-712)**

**CLOB V2 /order endpoint 要求的 signature 格式** (cite: laoli-w9-w5 §3.4, endpoint-matrix-v3 §A):

```
"signature": "0x<r(32B hex) + s(32B hex) + v(1B hex)>"
```

即: 65 bytes = 32B r || 32B s || 1B v, 编码为 `0x` 前缀的十六进制字符串 (130 hex chars + "0x" = 132 chars)。

**pm_client.hpp:279 注释**:
> "L1 EIP-712 sig (live 填; paper 留空 reserve)"
> "HMAC bug #3: 注明必 sigType=1"

注意: pm_client.hpp line 267-268 注释:
> "HMAC 4 bug enforce: live 实现 signature 字段必须 base64 保留 padding"

这条注释 **描述的是 L2 HMAC sig**，不是 L1 EIP-712 sig。L1 EIP-712 signature 格式是 `0x` + 65B hex，
不是 base64。不要混淆。澄清见下表:

| 签名类型 | 用途 | 格式 |
|---|---|---|
| L1 EIP-712 secp256k1 | SignedOrder.signature (订单签名) | `"0x" + hex(r[32] + s[32] + v[1])` |
| L2 HMAC-SHA256 | HTTP Authorization header | base64url 保留 padding (HMAC bug #3 修正) |

### §3.2 maker / signer / funder 三者关系 (sigType=1 的含义)

原文 cite (endpoint-matrix-v3 §A R4, laosun-w10-w1 §2.1 signature_type):

```
HMAC bug #2 修正 (原文 v3 §A):
  sigType=1 = Magic Safe 1-of-1 (EOA 直签, 没有多签合约介入)
  sigType=2 = Polymarket proxy (Magic/Email 钱包形态, 需 proxy 合约)
  我们用 EOA 私钥 → sigType=1
```

**我们的具体形态**:

| 角色 | 值 | 来源 |
|---|---|---|
| `maker` (EIP-712 Order.maker) | `POLYMARKET_FUNDER_ADDRESS` (Safe/funder wallet 地址) | .env |
| `signer` (EIP-712 Order.signer) | EOA 私钥对应地址 (`private_key → public_key → keccak256 → address`) | 由私钥推导 |
| `signature_type` | `1` | sigType=1 Magic Safe 1-of-1 |

**重要**: sigType=1 表示 `maker == signer` 或 maker 是由 signer EOA 直接控制的 1-of-1 Safe。
若 `POLYMARKET_FUNDER_ADDRESS` == EOA 地址 (即没有 Gnosis Safe 中间层)，则 maker == signer。
若 `POLYMARKET_FUNDER_ADDRESS` 是一个 Gnosis Safe 合约地址 (多签或 1-of-1 Safe)，则
maker = Safe 地址, signer = EOA, sigType=1 表示 1-of-1 Safe 认证。

**GM 实施前需确认**: `POLYMARKET_FUNDER_ADDRESS` 是 EOA 还是 Safe 合约?
- 如果是 EOA: maker = signer = 该地址, sigType=1 (直签)
- 如果是 Safe: maker = Safe, signer = EOA (私钥持有者), sigType=1 (1-of-1 Magic)

两种情况 sigType 都是 1，差别在 EIP-712 Order 的 maker 字段填什么:
- EOA: maker = EOA 地址 = WALLET_PRIVATE_KEY 对应地址
- Safe: maker = POLYMARKET_FUNDER_ADDRESS (Safe 合约地址)

### §3.3 live mode SignerV62 实现缺口 (当前状态)

`signer_v62.cpp` line 189-192:
```cpp
if (mode_ == ExecutionMode::Live || mode_ == ExecutionMode::Backtest) {
    resp.error = SignV62Error::InternalError;
    resp.reject_reason = "mode_not_paper";
    return resp;
}
```

live mode 目前是 stub，返回 InternalError。

**GM 落地 L1 签名需要实现**:
1. 构造时读入私钥 (`WALLET_PRIVATE_KEY` 从 .env)
2. 实施真正的 secp256k1 ECDSA EIP-712 签名
3. 产出 65B r||s||v hex 字符串

这是 Wave 104 P0 中标注 "live mode: stub → M5+ secp256k1 真切" 的部分。
GM 实施测试单需要绕过这个 stub 或先直接在 SubmitOrder 实现里内联签名逻辑。

**推荐**: GM 在 `live_pm_client.cpp` 的 `SubmitOrder` 里直接调用外部 secp256k1 库完成签名，
不必修改 SignerV62 (后者的 live 分支是有计划的架构重构，不应为测试单仓促实现)。

---

## §4 私钥安全规范 (红线 — 小白 co-review 必须)

**红线引用原文 (CLAUDE.md §8)**:
> "私钥明文落盘 / 出现在日志 → 系统权限暂停"

### §4.1 私钥读取规范

```cpp
// 合规读法 (C++, .env 已 export 到环境变量)
const char* raw = std::getenv("WALLET_PRIVATE_KEY");
if (raw == nullptr) {
    // 启动失败, 不继续
    throw std::runtime_error("WALLET_PRIVATE_KEY not set");
}

// 放入 SecureBuffer (析构时 sodium_memzero)
crypto::SecureBuffer<32> privkey_buf;
// hex decode: "0x" 前缀可选; 64 hex chars = 32 bytes
hex_decode_to(raw, privkey_buf.data(), 32);

// 读完立刻显式清零临时 C string 引用 (raw 是 env 指针, 不需要 free; 
// 但若做过 strdup/string copy 需要清零那个 copy)
// sodium_memzero((void*)raw, strlen(raw));  // 只有做过 strdup 才需要
```

**规则**:

| 规则 | 说明 |
|---|---|
| S-1: 不 log 私钥任何部分 | `WALLET_PRIVATE_KEY` 的前 6 chars / 后 4 chars / 长度 / hex 前缀都不 log |
| S-2: 不落盘 | 私钥不写入任何文件、audit log、WAL record |
| S-3: 使用 SecureBuffer | hex decode 进 `SecureBuffer<32>` 或 `SecureBuffer<64>` (libsodium 格式); 析构时自动 `sodium_memzero` |
| S-4: 签完即不保留 | 签名函数返回后, 私钥 bytes 仅存活于 SignerV62 对象生命周期 (构造到析构) |
| S-5: 禁止 std::string 持有私钥 | `std::string` 无法保证内存清零 (析构不调 sodium_memzero); 必须用 `SecureBuffer<N>` |
| S-6: 禁止日志包含 signature hex | signature 是 65B，可从中进行 key extraction 攻击; 不 log 完整 signature |
| S-7: 禁止 core dump | 生产进程必须 `setrlimit(RLIMIT_CORE, 0)` 禁止 core dump (core 含内存镜像) |

### §4.2 secp256k1 私钥 vs Ed25519 私钥 (格式区分)

```
secp256k1 (Polygon/EIP-712 真私钥):
  - 原始: 32 bytes 大端
  - 环境变量格式: "0x" + 64 hex chars (= 66 chars)
  - 用途: EIP-712 ECDSA 签名 → r||s||v 65B

Ed25519 (libsodium paper mock):
  - 64 bytes (libsodium seed || pubkey 拼接)
  - 仅 paper mode, 析构即清零
  - 不接触真私钥
```

**结论**: live 实施时, 从 .env 读 `WALLET_PRIVATE_KEY` (secp256k1 32B) 进 `SecureBuffer<32>`，
调 secp256k1 库签名，签完析构。与 paper mode 的 Ed25519 SecureBuffer<64> 完全分离。

### §4.3 小白 co-review checklist

以下项目需小白审核通过才能上 live:

- [ ] `SecureBuffer<32>` 包裹私钥, 无裸 `uint8_t[32]` / `std::string` 持有私钥
- [ ] 签名函数内部无 `printf` / `std::cout` / `spdlog::info` 含私钥或签名内容
- [ ] `setrlimit(RLIMIT_CORE, 0)` 在 main() 入口调用
- [ ] .env 文件权限 `chmod 600 .env`
- [ ] `WALLET_PRIVATE_KEY` 不出现在任何 git commit / PR diff
- [ ] spdlog 日志级别: private key 相关操作不 log (包括"开始签名"不带地址)
- [ ] secp256k1 库选型: 推荐 `libsecp256k1` (Bitcoin Core 维护, 无 GC, C 库, C++ 可直接链接)

---

## §5 salt / timestamp_ms / expiration

### §5.1 salt

原文 cite (laosun-w10-w1 §8.4):
> "若需 >1 order/ms, 需 salt 字段区分 (salt 由 libsodium randombytes_buf(8) 生成, 天然唯一)"

```cpp
// salt 生成 (8 bytes 随机 → uint64_t)
uint64_t salt;
randombytes_buf(&salt, sizeof(salt));
// 填入 wire body 的 "salt" 字段 (decimal string)
std::string salt_str = std::to_string(salt);
```

salt 不进 SignV62Request (transformer 不处理 salt)。salt 由老李的 SubmitOrder POST body 组装填入。
老孙边界: 说明 salt 用法；生成由老李实施或 GM 在 SubmitOrder 里实施。

### §5.2 timestamp_ms

```cpp
// 毫秒时间戳 (替代 V1 的 nonce)
int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
// 填入 OrderIntent.timestamp_ms
// 注意: Orchestrator 填入时机 = SubmitOrder 调用前; 不是 RiskGateway.evaluate() 时
// 同一 ms 内只能提交 1 个订单 (同地址); 高频场景靠 salt 区分
```

### §5.3 expiration

原文 cite (laoli-w9-w5 §3.4 wire body + laosun-w10-w1 §2.2):
> "expiration: V2 不进 EIP-712; wire body 仍有 expiration 用于 GTD"

```json
// V2 POST /order wire body 中:
{
  "expiration": "0",       // 0 = GTC (Good Till Cancelled)
  "orderType": "GTC"
}
```

**测试单推荐 GTC**:
- `expiration = "0"` + `orderType = "GTC"`
- 不要用 FOK (Fill or Kill)，因为 FOK 要求立即全量成交，测试单 $0.05 流动性不确定
- GTC 挂单会进 order book，不会立即 expire，可以观察 Booked → PartiallyFilled → Filled 状态机
- 如测试后需清理: 调 `CancelOrder(order_id)` 撤单

---

## §6 最小签名路径 (GM 照步骤实施)

```
Step 1: 启动时读私钥
  privkey_hex = getenv("WALLET_PRIVATE_KEY")  // "0x" + 64 hex
  privkey_buf = SecureBuffer<32>
  hex_decode(privkey_hex + 2, privkey_buf.data(), 32)  // 跳过 "0x"
  // 从私钥推导 EOA 地址 (secp256k1 pubkey → keccak256)
  eoa_address = derive_address(privkey_buf)  // "0x" lowercase

Step 2: 确认 maker 地址
  funder_addr = getenv("POLYMARKET_FUNDER_ADDRESS")
  // 如果 funder_addr == eoa_address: EOA 直签 (maker = signer)
  // 如果 funder_addr != eoa_address: Safe 1-of-1 (maker = Safe, signer = EOA)
  // 两者 signature_type 都是 1

Step 3: 查市场 negRisk
  market_info = GetMarketInfo(condition_id)  // REST /gamma/markets?conditionId=
  neg_risk = market_info.neg_risk
  verifying_contract = neg_risk ? kCtfExchangeV2NegRisk : kCtfExchangeV2Addr

Step 4: 构造 OrderIntent
  intent.token_id = "<target token_id>"
  intent.condition_id = "<condition_id>"
  intent.side = Side::Buy
  intent.price = 0.55
  intent.size_pUSD_micro = 50_000  // $0.05
  intent.timestamp_ms = now_ms()   // chrono::system_clock milliseconds
  intent.metadata = "0x0000...0000"
  intent.builder  = "0x0000...0000"
  // 4 ts: 从上游 orderbook snapshot 透传 (R-20)

Step 5: 通过 RiskGateway (R-1)
  risk_decision = risk_gateway.evaluate(intent)
  if (!risk_decision.is_approved()) { return; }

Step 6: 调 transformer
  auto req_opt = to_sign_v62_request(intent, risk_decision.audit_id)
  if (!req_opt.has_value()) { // spec-9/10 校验失败
    return;
  }
  auto& req = *req_opt;
  // 外层 Orchestrator 填入:
  req.maker_address = funder_addr;      // POLYMARKET_FUNDER_ADDRESS
  req.client_order_id = generate_uuid4();

Step 7: 执行 EIP-712 V2 签名 (secp256k1)
  // 计算 EIP-712 typed data hash:
  domain_separator = keccak256(abi.encode(
      keccak256("EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)"),
      keccak256("Polymarket CTF Exchange"),
      keccak256("2"),
      137,
      verifying_contract
  ))
  
  order_type_hash = keccak256(
      "Order(uint256 salt,address maker,address signer,uint256 tokenId,"
      "uint256 makerAmount,uint256 takerAmount,uint8 side,uint8 signatureType,"
      "uint256 timestamp,bytes32 metadata,bytes32 builder)"
  )
  
  // makerAmount / takerAmount 计算 (BUY 方向):
  makerAmount = req.size_pUSD_micro
  takerAmount = req.size_pUSD_micro * (10000 - req.limit_price_bps) / req.limit_price_bps
  
  order_hash = keccak256(abi.encode(
      order_type_hash,
      salt_uint256,
      maker_address_bytes20,
      eoa_address_bytes20,   // signer = EOA
      uint256(token_id_decimal_string),
      makerAmount,
      takerAmount,
      uint8(req.side),
      uint8(1),              // signatureType = 1
      uint256(req.timestamp_ms),
      bytes32(req.metadata_hex),
      bytes32(req.builder_hex)
  ))
  
  digest = keccak256("\x19\x01" + domain_separator + order_hash)
  
  // secp256k1 ECDSA sign:
  (r, s, v) = secp256k1_sign(privkey_buf, digest)
  sig_hex = "0x" + hex(r[32]) + hex(s[32]) + hex(v[1])

Step 8: 填 SignedOrder 交给 SubmitOrder
  SignedOrder order;
  order.condition_id = req.condition_id;
  order.token_id = req.token_id;
  order.side = req.side;
  order.limit_price_bps = (uint32_t)req.limit_price_bps;
  order.size_usdc_micro = (uint64_t)req.size_pUSD_micro;
  order.expiration_unix_s = 0;        // GTC
  order.signature_type = 1;           // HMAC bug #2: 必须 = 1
  order.signature = sig_hex;          // "0x" + r(32B) + s(32B) + v(1B) hex
  order.maker_address = funder_addr;
  order.client_order_id = req.client_order_id;
  order.ts = /* 4 ts quad 透传 */;
  
  // 交给老李:
  auto result = live_client.SubmitOrder(order);
```

---

## §7 与老李 (L2 CLOB payload) 的接口对齐

### §7.1 老孙产出

老孙 Sign 完成后，`SignedOrder` 结构里老孙负责的字段:

| 字段 | 格式 | 来源 |
|---|---|---|
| `signature` | `"0x" + hex(r[32]) + hex(s[32]) + hex(v[1])` = 132 chars | secp256k1 EIP-712 |
| `maker_address` | `"0x" + lowercase 40 hex chars` | POLYMARKET_FUNDER_ADDRESS |
| `signature_type` | `1` (uint32, 永不改变) | HMAC bug #2 |
| `client_order_id` | UUID v4 string | Orchestrator 生成 |

### §7.2 老李需要组装的 V2 POST /order wire body

原文 cite (laoli-w9-w5 §3.4):

```json
{
  "order": {
    "salt": "<randombytes uint256 string>",
    "maker": "<SignedOrder.maker_address>",
    "signer": "<EOA 地址 (由 WALLET_PRIVATE_KEY 推导)>",
    "tokenId": "<SignedOrder.token_id>",
    "makerAmount": "<uint256 string>",
    "takerAmount": "<uint256 string>",
    "side": "BUY",
    "signatureType": 1,
    "timestamp": "<timestamp_ms string>",
    "metadata": "<bytes32 hex>",
    "builder": "<bytes32 hex>"
  },
  "expiration": "0",
  "nonce": "0",
  "orderType": "GTC",
  "signature": "<SignedOrder.signature, 即 0x + 65B hex>"
}
```

**注意**: wire body 的 `"nonce": "0"` 是 V2 残留字段 (V1 nonce 废弃, 但 wire body 仍存在
为向后兼容，固定填 "0")。不要与 EIP-712 Order struct 的 nonce (V2 已移除) 混淆。

**老李侧注意**: POST /order 需 L2 HMAC Authorization header (cite: endpoint-matrix-v3 §A §D)。
base string = `ts + "POST" + "/order" + body_json` (body 必须双引号 JSON，不是单引号)。

### §7.3 关键差异说明 (V2 新字段在 wire body 中)

原文 cite (laoli-w9-w5 §3.4):

V2 wire body `order` 对象里的 `timestamp`/`metadata`/`builder` 是新增字段，
不在 V1 wire body 里。老李实施 SubmitOrder 时需要确认 V2 格式，不能用 V1 template。

---

## §8 makerAmount / takerAmount 精确整数计算

为避免浮点误差引起 EIP-712 hash 不一致，使用整数运算:

```cpp
// BUY: 花 pUSD 买 shares
// limit_price_bps = p × 10000 (e.g. 5500 = 55%)
// makerAmount = 花出去的 pUSD (micro)
// takerAmount = 拿到的 shares (pUSD 计价, micro)
// 数学: takerAmount = makerAmount × (1 - p) / p
//     = makerAmount × (10000 - limit_price_bps) / limit_price_bps

int64_t maker_amount = req.size_pUSD_micro;
int64_t taker_amount = (maker_amount * (10000LL - req.limit_price_bps)) / req.limit_price_bps;
// 整数除法截断, 可接受 (误差 < 1 micro pUSD = $0.000001)

// SELL: 花 shares 卖 pUSD
// makerAmount = 卖出的 shares (micro)
// takerAmount = 换到的 pUSD (micro) = shares × p / (1-p)
// = makerAmount × limit_price_bps / (10000 - limit_price_bps)
int64_t sell_taker = (maker_amount * req.limit_price_bps) / (10000LL - req.limit_price_bps);
```

---

## §9 测试单建议与风险

### §9.1 推荐参数

- token_id: 选 NBA Finals / 欧冠决赛等高流动性 sports 市场 (spread < 0.5%)
- side: BUY (taker 方向, 更容易成交)
- price: 接近当前 best ask (例如 best ask = 0.56, 出 0.56 或更高)
- size: $0.05 (50_000 micro pUSD); Sports taker fee = 3% × 0.55 × 0.45 ≈ 0.74% → 实际 fee 约 $0.0004
- orderType: GTC (可以取消)
- 不要用 FOK: $0.05 太小，FOK 如果 book 吃不到会立即 Rejected

### §9.2 负面场景预防

| 风险 | 预防 |
|---|---|
| 签名无效 (BadRequest 400) | 检查 verifyingContract (negRisk 是否选对); 检查 sigType=1; 检查 domain version="2" |
| HMAC 401 | 老李侧检查 L2 HMAC base string (endpoint-matrix-v3 §B SOP) |
| 余额不足 | 先 `GetBalance()` 确认 pUSD balance ≥ $0.10 (留双倍余量) |
| timestamp 重复 (同 ms 第二单) | 生成下一单前 sleep 1ms 或换 salt |
| negRisk 误判 | `GetMarketInfo` 返回后检查 `neg_risk` 字段 |

---

## §10 约束汇总

1. **语言**: 所有 live 实施 C++20，禁 Python 进生产 (CLAUDE.md §10)
2. **私钥红线**: 明文不落盘不进日志；违者系统权限暂停 (CLAUDE.md §8)
3. **sigType=1**: HMAC bug #2 永久 enforce，禁 sigType=2 (endpoint-matrix-v3 §A R4)
4. **R-20**: 4 ts 不等式必须满足；timestamp_ms ≠ 0 (spec-10)
5. **secp256k1**: live mode 必须 secp256k1 ECDSA, 不是 Ed25519 (Ed25519 仅 paper mock)
6. **verifyingContract 动态查**: 不可硬编码 negRisk，从 MarketInfo 动态读
7. **小白 co-review**: §4 私钥安全 checklist 必须 co-review 通过才能上 live
8. **老李接口**: 老孙产出 signature (0x hex) + maker_address，老李组装 POST body + HMAC header

---

**last_updated:** 2026-05-31 by 老孙 (crypto-signing-expert, #06)
**pending:** 小白 (#27) §4 安全 co-review + 老李 (#07) L2 接口对齐确认
