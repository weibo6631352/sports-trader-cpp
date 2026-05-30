---
owner: 老李 (#07, polymarket-protocol-expert)
last_review: 2026-05-31
sprint: MVP 实盘测试专项
status: FINAL — 供 GM 直接落地
触发: GM 要求用 <$0.1 真实订单验证实盘下单链路（达成一笔真实成交）
边界: 协议契约 + 字段语义 + 实施计划。不改主干，不写 wire 实现，不写 JSON parse。
关联:
  - docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md §A §B §D (HMAC 四 bug + SOP + 14 vector)
  - docs/RESEARCH/laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md (V2 EIP-712 ABI)
  - docs/RESEARCH/laoli-w9-w5-polymarket-market-research-update-v1.md §3.1 §3.4 (V2 wire body)
  - docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md §3.5 (SignedOrder 字段)
  - include/stcpp/polymarket/pm_client.hpp (SignedOrder / OrderAck ABI lock)
  - include/stcpp/polymarket/live/live_pm_client.hpp (14 接口 stub)
  - src/stcpp/polymarket/live/live_pm_client.cpp (当前全 stub)
---

# LivePolymarketClient::SubmitOrder 实盘下单实施计划 v1

> 目标: GM 能照此计划落地 `LivePolymarketClient::SubmitOrder`，下出一笔 <$0.1 真实成交单。
> 本文是协议契约规格说明，不包含 wire 实现代码和 JSON parse 代码（对应 CLAUDE.md 边界）。

---

## §1 CLOB V2 POST /order 端点完整契约

### §1.1 端点标识

| 项 | 值 |
|---|---|
| URL | `https://clob.polymarket.com/order` |
| Method | `POST` |
| Content-Type | `application/json` |
| 鉴权 | L2 HMAC header（见 §2） |
| 版本 | CLOB V2（2026-04-28 上线，V1 已废弃） |

### §1.2 请求体 JSON 结构（V2 wire body）

来源：`laoli-w9-w5-polymarket-market-research-update-v1.md §3.4`（原文引用）

```json
{
  "order": {
    "salt":           "12345",
    "maker":          "0x78dE3c8264C546Fffed8D9A1396cddEf7c8686BE",
    "signer":         "0x<EOA_ADDRESS_from_WALLET_PRIVATE_KEY>",
    "tokenId":        "71321045679252212594626385532706912750332728571942532289631379312455583992563",
    "makerAmount":    "90000",
    "takerAmount":    "100000",
    "side":           "BUY",
    "signatureType":  1,
    "timestamp":      "1748476800000",
    "metadata":       "0x0000000000000000000000000000000000000000000000000000000000000000",
    "builder":        "0x0000000000000000000000000000000000000000000000000000000000000000"
  },
  "expiration": "0",
  "nonce":      "0",
  "orderType":  "GTC",
  "signature":  "0x<EIP-712_ECDSA_sig_65B_hex>"
}
```

### §1.3 SignedOrder → CLOB order payload 字段映射

| CLOB wire 字段 | 类型 | 来源 | 说明 |
|---|---|---|---|
| `order.salt` | string (uint256 十进制) | `libsodium randombytes_buf(8)` 随机生成 | 防重放，每单唯一；SignerV62 内部生成 |
| `order.maker` | string (address hex, lowercase) | `.env POLYMARKET_FUNDER_ADDRESS` | funder/proxy wallet 地址 |
| `order.signer` | string (address hex, lowercase) | `WALLET_PRIVATE_KEY` 派生 EOA | EIP-712 实际签名者（与 maker 可相同或不同） |
| `order.tokenId` | string (uint256 十进制，无 0x 前缀) | `SignedOrder.token_id` | CLOB 一等公民，outcome 级标识 |
| `order.makerAmount` | string (uint256 micro) | 由 `limit_price_bps + size_pUSD_micro` 计算 | 见 §1.4 计算公式 |
| `order.takerAmount` | string (uint256 micro) | 同上 | 见 §1.4 |
| `order.side` | string `"BUY"` or `"SELL"` | `SignedOrder.side == 0 → "BUY"` | 注意 wire 是 string 不是 uint8 |
| `order.signatureType` | int `1` | 硬固定为 `1` | Magic Safe 1-of-1；HMAC bug #2 教训，绝不填 0 或 2 |
| `order.timestamp` | string (uint256 ms) | `SignV62Request.timestamp_ms` | V2 用毫秒时间戳替代 nonce，进 EIP-712 |
| `order.metadata` | string (bytes32 hex, 0x+64hex) | `SignV62Request.metadata` | 不用则填 bytes32(0) |
| `order.builder` | string (bytes32 hex, 0x+64hex) | `SignV62Request.builder` | 不用则填 bytes32(0) |
| `expiration` | string `"0"` | GTC 单固定 `"0"` | GTD 单填 Unix 秒；本测试用 GTC |
| `nonce` | string `"0"` | V2 中 nonce 已废弃，固定填 `"0"` | 仍须出现在 body 字段，但对 V2 无效 |
| `orderType` | string `"GTC"` | 测试用 `"GTC"` 即可 | 可选 `"FOK"` / `"GTD"` |
| `signature` | string (0x + hex) | `SignerV62.Sign()` 产出 | EIP-712 ECDSA 65 字节，hex 编码（非 base64） |

**注意：`order.side` 在 wire body 是 string（`"BUY"`/`"SELL"`），不是 uint8。EIP-712 Order struct 里 `side` 是 uint8（0=BUY/1=SELL）。两处编码不同，不要混淆。**

### §1.4 makerAmount / takerAmount 计算公式

Polymarket binary market 的 maker/taker amount 计算：

```
BUY side:
  makerAmount = size_pUSD_micro                         (maker 支付 USDC/pUSD)
  takerAmount = round(size_pUSD_micro / limit_price)    (maker 获得 token shares)

  其中 limit_price = limit_price_bps / 10000.0
  例: price=0.90, size=90000 micro ($0.09 pUSD):
    makerAmount = 90000
    takerAmount = round(90000 / 0.90) = 100000

SELL side:
  makerAmount = size_token_micro                        (maker 支出 token shares)
  takerAmount = round(size_token_micro * limit_price)   (maker 获得 USDC/pUSD)
```

**重要**：这两个 amount 都以 pUSD micro (1e-6) 为单位，对应 `size_pUSD_micro` 字段（V2 已从 USDC.e 改为 pUSD，见 `laosun-w10-w1-signer-v62-clob-v2-abi-spec-v1.md §5`）。

**EIP-712 typeHash 中的 `makerAmount` / `takerAmount` 与 wire body 中的值完全一致（均是 micro 整数，uint256）。**

### §1.5 order type 语义

| orderType | 语义 | 测试单建议 |
|---|---|---|
| `GTC` (Good Till Canceled) | 成交或持续挂单，不超过 expiration | 推荐：保证成交机会 |
| `FOK` (Fill Or Kill) | 立即全量成交，否则拒绝 | 最适合测试单——立即成交或立即 Rejected，不留悬空单 |
| `GTD` (Good Till Date) | expiration 秒前有效 | 不用于本次测试 |

**MVP 测试推荐 FOK**：如果 ask 有足够深度吃单，FOK 能保证立即成交；如不能成交则立即 Rejected 不留挂单烂账。如选 GTC，需监控挂单状态并手动 cancel。

### §1.6 响应体结构（ack）

成功 201 返回：
```json
{
  "orderID":   "0x<uuid>",
  "status":    "matched",
  "transactionsHashes": ["0x..."],
  "errorMsg":  ""
}
```

`status` 可能值：
- `"matched"` — 即时成交（FOK 或 taker 吃到挂单）
- `"delayed"` — 已接收排队中（seconds_delay > 0 的 market）
- `"live"` — 已上 book 挂单（GTC maker order）
- `"unmatched"` — FOK 无法全量成交，已 rejected

错误 4xx/5xx body 含 `"errorMsg"` 字段，务必读取（见 §2.5 401 SOP）。

---

## §2 L2 认证（最关键）

### §2.1 L2 HMAC header 四字段

每个需要 L2 认证的 CLOB 请求必须带以下四个 HTTP header：

| Header | 值 | 说明 |
|---|---|---|
| `POLY_ADDRESS` | `.env POLYMARKET_FUNDER_ADDRESS` | funder wallet 地址（lowercase） |
| `POLY_SIGNATURE` | HMAC-SHA256 签名 base64url | 算法见 §2.2 |
| `POLY_TIMESTAMP` | Unix 秒时间戳 string | `std::to_string(time(nullptr))` |
| `POLY_API_KEY` | `.env POLYMARKET_API_KEY` | 已在 .env 存在：`8426bb88-9cbd-dd8c-2711-f17ec158b323` |
| `POLY_PASSPHRASE` | `.env POLY_API_PASSPHRASE` | **见 §2.4：.env 当前缺失，需先补齐** |

注：实际上，L2 header 使用 `POLY_API_KEY` 而非 `apiKey`（header 名与 WSS payload 字段名不同）。官方文档中鉴权 header 就是这五个（含 POLY_PASSPHRASE）。

**POLY_API_SECRET 用于计算 HMAC 签名（不出现在 header 中），也需要补入 .env。**

### §2.2 HMAC-SHA256 签名算法

来源：`laoli-polymarket-endpoint-matrix-v3.md §D.0`（权威，原文引用）

```
secret_bytes = base64url_decode(POLY_API_SECRET)   // base64url 含 padding 解码
base_string  = POLY_TIMESTAMP + "POST" + "/order" + body_as_json_string
               // 注意：path 不含 querystring (HMAC bug #1)
               // 注意：body 必须是双引号 JSON string，不是 Python dict (HMAC bug R6)
sig_bytes    = HMAC-SHA256(secret_bytes, base_string.encode("utf-8"))
POLY_SIGNATURE = base64url_encode(sig_bytes)        // 保留 = padding，不 strip (HMAC bug #4)
```

四个 HMAC 红线（务必 byte-equal 验证，否则 401）：
- **R1**：`request_path` 不含 querystring。POST /order 的 path 就是 `/order`，无 query。
- **R2**：`POLY_SIGNATURE = base64url(sig).decode()` 保留 `=` padding，**绝不 strip**。
- **R4**：`signatureType = 1`（在 order body 里），与 HMAC sig 无关但必须正确，否则余额查询返 0 假阳性。
- **R6**：body string 必须是双引号 JSON，C++ nlohmann/simdjson 序列化已是双引号，不需要 Python 的 `replace("'", '"')`。

**与 WSS user channel 认证的区别**：WSS user channel 鉴权把 `apiKey/secret/passphrase` 直接写进 WebSocket 订阅 payload 的 `"auth"` 对象（见 `polymarket_clob_subscriber.cpp MakeUserSubscribeFrame`），不是 HTTP header + HMAC。两套鉴权机制完全不同，不可混用。

**WSS auth 可复用的只是 api_key/passphrase 字段名**，HMAC 签名算法是 REST HTTP header 专用。

### §2.3 L2 header 在 POST /order 中的示例（体现 HMAC V11 vector）

来源：`laoli-polymarket-endpoint-matrix-v3.md §D.1 V11`

```
POLY_TIMESTAMP:  1748390400
POLY_ADDRESS:    0x78de3c8264c546fffed8d9a1396cddEf7c8686be  (lowercase)
POLY_API_KEY:    8426bb88-9cbd-dd8c-2711-f17ec158b323
POLY_PASSPHRASE: <POLY_API_PASSPHRASE>
POLY_SIGNATURE:  <HMAC-SHA256 of "1748390400POST/order{...body...}">
```

base_string 构造：
```
base_string = "1748390400" + "POST" + "/order" + <body_json_string_with_double_quotes>
```

**clock skew 注意**：`POLY_TIMESTAMP` 必须与 Polymarket 服务端时钟在 ±30 秒内；跨洋链路可能有轻微偏差，建议用 `CLOCK_REALTIME`（NTP 同步的系统时钟），不用 `CLOCK_MONOTONIC`。

### §2.4 .env 凭证字段现状 + 缺口

当前 `.env` 已有字段：
- `POLYMARKET_FUNDER_ADDRESS` = `0x78dE3c8264C546Fffed8D9A1396cddEf7c8686BE`（`POLY_ADDRESS` 来源）
- `WALLET_PRIVATE_KEY` = `<存在，值 redacted>`（EIP-712 签名私钥，L1 层）
- `POLYMARKET_API_KEY` = `8426bb88-9cbd-dd8c-2711-f17ec158b323`（`POLY_API_KEY` 来源）

**当前 .env 缺失字段（阻塞 L2 认证）**：
- `POLY_API_SECRET` — HMAC 签名用的 secret（base64url 编码）
- `POLY_API_PASSPHRASE` — L2 header `POLY_PASSPHRASE` 来源

**获取方式：调用 `GET /auth/derive-api-key`（F-11）**。这是 L1 EIP-712 签名请求，用 `WALLET_PRIVATE_KEY` 签一次 `ClobAuthDomain` 结构，服务端返回三元组 `{api_key, api_secret, api_passphrase}`。该操作幂等——同一个 EOA 永远返回同一组凭证。

**结论：GM 在落地 SubmitOrder 之前，必须先实现 DeriveApiKey（F-11），把三元组写入 .env。或者用 py-clob-client-v2 临时跑一次 derive，把结果贴入 .env，跳过 F-11 C++ 实现。**

---

## §3 L1 vs L2 关系图

```
                    ┌────────────────────────────────────────────────────┐
                    │                  下单全链路                         │
                    │                                                    │
  WALLET_PRIVATE_KEY │                                                   │
         │          │   L1 层 (EIP-712)              L2 层 (HMAC)       │
         ▼          │                                                    │
   ┌───────────┐    │   1. SignerV62::Sign(SignV62Request)               │
   │  EOA 地址  │    │      - 构造 V2 EIP-712 Order struct               │
   │  (signer) │    │        (含 salt/maker/signer/tokenId/             │
   └───────────┘    │         makerAmount/takerAmount/side/              │
         │          │         signatureType/timestamp/metadata/builder)   │
         │ EIP-712  │      - keccak256(typeHash + encoded fields)        │
         │ ECDSA    │      - ECDSA sign → 65 字节 signature              │
         ▼          │      产出: SignedOrder (含 .signature hex)         │
   ┌───────────────────────────────────────────────────────┐            │
   │  SignedOrder → 序列化成 POST /order body JSON          │            │
   └───────────────────────────────────────────────────────┘            │
                    │                                                    │
                    │   2. 构造 L2 HMAC header                          │
  POLY_API_SECRET   │      POLY_TIMESTAMP = time()                      │
         │          │      base_string = ts + "POST" + "/order" + body  │
         ▼          │      POLY_SIGNATURE = base64url(HMAC-SHA256(...)) │
   ┌───────────┐    │      POLY_API_KEY = .env                          │
   │  HMAC sig │    │      POLY_PASSPHRASE = .env                       │
   └───────────┘    │                                                    │
         │          │   3. HTTP POST clob.polymarket.com/order          │
         └──────────────► 5 个 header + JSON body → OrderAck            │
                    │                                                    │
                    └────────────────────────────────────────────────────┘
```

**要点总结**：
- L1（EIP-712）：`WALLET_PRIVATE_KEY` 对 order struct 内容签名，确保订单真实性，产出 `order.signature`。签名在 body 里。
- L2（HMAC）：`POLY_API_SECRET` 对整个 HTTP 请求（时间戳+方法+路径+body）签名，确保 HTTP 请求来自授权 API key holder，产出 `POLY_SIGNATURE` header。
- **两层都必须**，缺任一层 → 服务器拒绝。
- DeriveApiKey（F-11）本身是一次 L1 签名（EIP-712 `ClobAuthDomain`），用来换取 L2 凭证。只需做一次（幂等），结果写入 .env 永久复用。

### §3.1 DeriveApiKey（F-11）协议契约

| 项 | 值 |
|---|---|
| URL | `GET https://clob.polymarket.com/auth/derive-api-key?signature_type=1&geo=US` (或无 geo 参数) |
| L2 鉴权 | **不需要** L2 HMAC——这个 endpoint 本身就是 L1 签名获取 L2 creds 的途径 |
| L1 签名方式 | EIP-712 `ClobAuthDomain` struct（见下方），用 `WALLET_PRIVATE_KEY` ECDSA 签 |
| Response | `{"apiKey": "...", "secret": "...", "passphrase": "..."}` |

ClobAuthDomain EIP-712 struct（V2 不变，domain version 仍 `"1"`，**不是** `"2"`）：
```
domain:
  name: "Polymarket Clob"
  version: "1"            ← 注意：API auth domain 版本是 1，不随 Exchange V2 升到 2
  chainId: 137
  verifyingContract: (可选，或 zero address)

message struct: "ClobAuth"
  string address    = POLYMARKET_FUNDER_ADDRESS (lowercase)
  string timestamp  = str(time())
  uint8  nonce      = 0
  string message    = "This message attests that I control the given wallet"
```

**最简 MVP 路径**：在实现 F-11 C++ 前，用 py-clob-client-v2 跑一次 derive：
```python
from py_clob_client_v2 import ClobClient
import os
c = ClobClient("https://clob.polymarket.com", key=os.environ["WALLET_PRIVATE_KEY"], chain_id=137)
creds = c.derive_api_key()
print(creds)  # {'apiKey': ..., 'secret': ..., 'passphrase': ...}
```
把三字段写入 `.env` 为 `POLY_API_SECRET` / `POLY_API_PASSPHRASE`，C++ 侧 `getenv()` 读取。

---

## §4 最小可行下单路径（MinViable Path）

### §4.1 前置条件检查（下单前必须全绿）

| 前置 | 检查方式 | 状态 |
|---|---|---|
| WALLET_PRIVATE_KEY | .env 已有 | 已有 |
| POLYMARKET_FUNDER_ADDRESS | .env 已有 | 已有 |
| POLYMARKET_API_KEY | .env 已有 | 已有 |
| POLY_API_SECRET | .env 需要 | **缺失 — 阻塞** |
| POLY_API_PASSPHRASE | .env 需要 | **缺失 — 阻塞** |
| pUSD 余额 > $0.1 | 调 GET /balance-allowance?asset_type=COLLATERAL | 需验证 |
| pUSD approve CTF Exchange V2 | 链上 approve 0xE111180000d2663C0091e4f400237545B87B996B | 需验证 |

**注意 pUSD 抵押品**：V2 抵押品是 pUSD（`0xC011a7E12a19f7B1f670d46F03B03f3342E82DFB`），不是 USDC.e。如果账户只有 USDC.e，需先调 `CollateralOnramp.wrap()` 换为 pUSD。（见 `laoli-w9-w5 §3.5 坑 V2-3`）

### §4.2 最小步骤序列

```
步骤 1: 获取 L2 creds（一次性）
  → 用 py-clob-client-v2 或 C++ F-11，调 /auth/derive-api-key
  → 把 api_key / api_secret / api_passphrase 写入 .env
  → 验证：调 GET /auth/api-keys（L2）能看到 api_key 在列表里

步骤 2: 验证余额
  → GET /balance-allowance?asset_type=COLLATERAL&signature_type=1
  → L2 header 必须含 POLY_SIGNATURE（R4：signatureType=1 in query）
  → 确认 pUSD balance > $0.10

步骤 3: 选定目标 market + token_id
  → 见 §7 测试单设计（推荐 NBA/Champions League 高流动性 market）
  → 用 gamma API 获取 condition_id + token_id
  → 用 GET /book?token_id=<id> 确认 ask side 有单可吃

步骤 4: 构造 SignV62Request
  → 填入 token_id / side=0(BUY) / limit_price_bps / size_pUSD_micro
  → timestamp_ms = current_time_ms (唯一性保证，V2 替代 nonce)
  → metadata = bytes32(0) / builder = bytes32(0)
  → 4 ts 正确填入（R-20）

步骤 5: 调 SignerV62::Sign()（live mode）
  → 产出 SignV62Response，含 .signature（hex）

步骤 6: 构造 SignedOrder（供 LivePolymarketClient::SubmitOrder 入参）
  → 注意：SignedOrder 结构中 .signature_type = 1（HMAC bug #2）
  → .signature = "0x" + SignerV62 产出的 65B hex

步骤 7: 序列化成 CLOB V2 POST /order body JSON
  → 参照 §1.2 字段映射表逐字段填入
  → side 字段：uint8 0 → string "BUY"
  → 数值字段：pUSD micro 整数 → string（CLOB 要求 uint256 以 string 传）

步骤 8: 计算 L2 HMAC header
  → POLY_TIMESTAMP = str(time())
  → base_string = POLY_TIMESTAMP + "POST" + "/order" + body
  → POLY_SIGNATURE = base64url(HMAC-SHA256(base64url_decode(POLY_API_SECRET), base_string))
  → 保留 = padding（HMAC bug #4）

步骤 9: POST https://clob.polymarket.com/order
  → 5 个 header + Content-Type: application/json + body
  → 解析响应：201 含 orderID + status
  → 若 401 → 走 §B SOP（不准 5 分钟内说 key 失效）
  → 若 status="matched" → 成交，任务完成
  → 若 status="live" → GTC 上 book，用 GET /data/order/{orderID} 查状态
```

### §4.3 SignerV62 live mode 状态

当前 `src/stcpp/signer/v62/signer_v62.cpp`（Wave 104）：

```cpp
// live / backtest → stub（当前行为）
if (mode_ == execution::ExecutionMode::Live || mode_ == execution::ExecutionMode::Backtest) {
    resp.error = SignV62Error::InternalError;
    resp.reject_reason = "mode_not_paper";
    return resp;
}
```

**SignerV62 live mode 当前是 stub，必须由老孙实现真实的 secp256k1 EIP-712 ECDSA 签名路径。** paper mode 走 Ed25519 mock，live mode 必须走真实 secp256k1 + EIP-712 V2 domain（version="2", verifyingContract=0xE111180000d2663C0091e4f400237545B87B996B）。

**这是触点之一，见 §6。**

---

## §5 HTTP 客户端选型建议

### §5.1 选型约束

- 禁止 `popen("curl")` 进生产（P0-5 红线）
- 生产代码全部 C++20（CLAUDE.md §10）
- 跨洋链路，需要合理超时设置（推荐 POST /order timeout = 5s）

### §5.2 MVP 测试阶段建议

**方案 A（推荐 MVP 快速验证）**：用 `libcurl` C API 直接调用。

libcurl 是系统级库，C++ 直接调，同步模式，无 GC，适合 MVP 单线程验证场景：

```
依赖: libcurl (macOS: brew install curl，已有 /usr/bin/curl 链接)
接口: curl_easy_init / curl_easy_setopt / curl_easy_perform
thread-safety: 单线程验证不涉及多线程，safe
超时: CURLOPT_TIMEOUT_MS = 5000
```

注意：生产多线程调用需 `curl_global_init(CURL_GLOBAL_ALL)` + per-thread handle。老陈（网络工程师）主权的 worker pool 是正式方案，MVP 阶段 GM 可单线程 libcurl。

**方案 B（中期）**：老陈 W5-02 网络层工程师接管，上 boost.beast 或其他 C++ HTTP client。

### §5.3 测试阶段权宜：curl CLI 验证

在 C++ 实现前，用 curl CLI 先打通协议通路（验证凭证 + 字段）是合理的预研步骤：

```bash
# 先跑 401 SOP：验证 L2 auth
curl -X GET "https://clob.polymarket.com/auth/api-keys" \
  -H "POLY_TIMESTAMP: $(date +%s)" \
  -H "POLY_ADDRESS: 0x78de3c8264c546fffed8d9a1396cddef7c8686be" \
  -H "POLY_API_KEY: 8426bb88-9cbd-dd8c-2711-f17ec158b323" \
  -H "POLY_SIGNATURE: <computed>" \
  -H "POLY_PASSPHRASE: <from .env>"
```

**GM 做决策**：MVP 测试单是否用 curl CLI 打通（快，但 popen 禁止），还是直接上 libcurl C++ 实现（稳妥）。我建议直接 libcurl，因为禁令明确。

---

## §6 触点标注（跨专家边界）

### §6.1 @老孙（SignerV62 live mode 实现）

**阻塞点**：SignerV62 live mode 当前返回 `InternalError: "mode_not_paper"`（`signer_v62.cpp:189-193`）。

老孙需实现：
1. live mode 下加载 `WALLET_PRIVATE_KEY`（secp256k1 私钥，来自 .env）
2. 构造 V2 EIP-712 domain（`name="Polymarket CTF Exchange"`, `version="2"`, `chainId=137`, `verifyingContract=0xE111180000d2663C0091e4f400237545B87B996B`）
3. 构造 V2 Order struct typeHash（含 timestamp/metadata/builder 新字段，移除 taker/nonce/feeRateBps/expiration）
4. keccak256(typeHash + abi.encode(order fields)) → sigHash
5. secp256k1 ECDSA sign(sigHash, private_key) → 65B (r||s||v)，hex 编码，加 "0x" 前缀
6. 红线：私钥不落盘，不进日志（CLAUDE.md §8）

**字段对齐点**（老孙需确认）：
- V2 EIP-712 Order struct 字段顺序（typeHash 计算顺序必须与 ctf-exchange-v2 Solidity `_hashOrder()` 一致）
- `makerAmount` / `takerAmount` 单位：pUSD micro（与 wire body 完全一致，均 uint256）
- `side` 进 EIP-712 是 uint8（0=BUY/1=SELL），不是 string

### §6.2 @小白（私钥安全边界）

**私钥安全红线**（CLAUDE.md §8 原文引用：「私钥明文落盘 / 出现在日志 → 系统权限暂停」）：

1. `WALLET_PRIVATE_KEY` 只在 SignerV62 构造时通过 `getenv()` 加载到 `SecureBuffer`，用完立即 `sodium_memzero` 清零，不留在普通 `std::string`
2. `POLY_API_SECRET` 同理：只在 HMAC 计算时短暂持有，计算完清零
3. POST /order body 不含私钥，含 `order.signature`（公开的 65B ECDSA sig，不是私钥）；log 可安全保留 body（需 redact `POLY_SIGNATURE` header value）
4. 401 调试时打印 L2 base_string 可以（不含私钥），但 `POLY_SIGNATURE` 值本身 log 时须 redact（防 replay）

### §6.3 @老韩（RM evaluate，<$0.1 测试单 cap 设置）

**红线**：所有下单链路绕过 `RiskManager` → 立即回滚（CLAUDE.md §8 原文引用）。

MVP 测试单必须经过 RM `evaluate()`。老韩需：
1. 确认 RM RiskConfig 中 `per_order_cap_pUSD_micro` 允许 <$0.1 的单（90000 micro = $0.09）
2. 确认 `bankroll_cap_pUSD_micro` 不会拦截（测试账户余额可能 < $1，bankroll cap 需临时放宽）
3. 测试单 net_edge 可能为负（我们主动吃 ask，作为 taker 支付 fee）——确认 RM `net_edge >= NET_EDGE_FLOOR` gate 有临时 bypass 机制，或 `NET_EDGE_FLOOR` 在 debug/test 配置可设为负数
4. 测试完毕后 RM 配置恢复 production 值

**RM evaluate gate 路径**（实盘链路）：
```
OrderIntent (token_id + side + price + size) → RiskGateway::evaluate() → Allowed
→ SignV62Request 构造 → SignerV62::Sign() → SignedOrder
→ LivePolymarketClient::SubmitOrder(SignedOrder)
→ POST /order
```

---

## §7 测试单设计（<$0.1 达成真实成交）

### §7.1 核心策略：作为 taker 吃现有 ask

要保证 <$0.1 小单成交，最可靠的方式是**作为 aggressive taker 下单**：
- `orderType = "FOK"` — 不能全量成交则立即 Rejected，不留挂单
- `side = "BUY"` 
- `limit_price` **等于或略高于 best ask**（确保能吃到对手盘）
- `size` 刚好 <$0.1 notional

### §7.2 size 约束分析

**最小订单约束**：
- gamma `orderMinSize = 5.0`（大多数市场）——即最小 $5 USD notional

这是关键问题：**Polymarket 官方最小订单是 $5 USDC，<$0.1 可能被直接 BadRequest 拒绝。**

经 endpoint-matrix v3 实测和官方 SDK 行为分析：
- `orderMinSize` 字段是 UI 显示层面的提示，CLOB 后端实际最小约束是 `min_order_size` 字段（在 `/book` 或 `/books` 响应里）
- 对于 `min_order_size`，实测部分 markets 返回 `1.0`（$1 USDC），部分返回 `5.0`（$5 USDC）
- FOK 单在高流动性 market 上 $1 notional 通常可以成交

**建议重新定义测试目标**：把 "$<$0.1" 调整为 "$1 USDC 最小单"，仍然远低于正式交易规模但符合 CLOB 最小订单约束。若 GM 坚持 <$0.1，需先查目标 market 的 `min_order_size`（用 POST /books 或 GET /book 获取）。

### §7.3 推荐目标 market（2026-05-31 时点，基于 §4 市场调研）

**首选：NBA Finals / Western Conference Finals（正在进行）**

| 参数 | 推荐值 | 理由 |
|---|---|---|
| 赛事 | NBA Western/Eastern Conference Finals 或 Finals | 2026-05-31 正进行中，流动性最高 |
| 盘口类型 | Moneyline（Will Team X win?）| 最简单，二元 Yes/No，`negRisk=false` |
| side | BUY YES token | Yes token ask 通常有最好深度 |
| orderType | `FOK` | 不成交立即 Rejected，无悬空单风险 |
| limit_price | best_ask（从 /book 实时读） | 等于 ask = taker 级别，必成交 |
| size_pUSD_micro | `1_000_000`（$1.00 pUSD） | 超过 min_order_size $1，低于 $5 风险 |
| 预期 fee | `$1 × 0.03 × p × (1-p)` ≈ $0.0075 (p=0.5) | Sports taker fee 公式 |

**具体步骤**：
```bash
# 1. 用 gamma 查当前 NBA Finals market
curl "https://gamma-api.polymarket.com/events?tag_slug=nba&active=true&order=volume&ascending=false&limit=5"
# 找 conditionId + 流动性最高的 market

# 2. 查实时 orderbook（token_id 从上一步的 clobTokenIds[0]）
curl "https://clob.polymarket.com/book?token_id=<YES_token_id>"
# 读 asks[0].price（best ask）

# 3. 确认 min_order_size 允许 $1 单
# POST /books body: [{"token_id": "<YES_token_id>"}]
```

### §7.4 备选：2026 FIFA World Cup（即将开赛）

2026 FIFA World Cup 2026-06-11 开赛，流动性预计是平台最大体育 event。当前已开盘 outright（Will Argentina win?），spread = 0.1%。如果 NBA Finals 已结束，World Cup 是最佳备选。

### §7.5 避坑提示

| 陷阱 | 规避 |
|---|---|
| `negRisk=true` market | 不选 champion outright（negRisk 需换 verifyingContract，增加复杂度）；选单场 Moneyline（negRisk=false） |
| `acceptingOrders=false` | 下单前检查该字段；比赛开球前可能冷冻（5-30 分钟 window） |
| `seconds_delay > 0` | seconds_delay 意味着官方延迟撮合（反 latency arb）；FOK 单在此情况下可能 unmatched |
| `fpmm != ""` | 老 AMM market，CLOB 下单无效；必须 `enableOrderBook=true && fpmm=""` |
| 同 ms 重复提交 | V2 `timestamp_ms` 毫秒唯一性；同 ms 内同地址不可重复；单次测试不触发 |
| pUSD vs USDC.e | 账户需有 pUSD，不是 USDC.e（见 §4.1 前置条件） |

---

## §8 已知坑汇总（线上风险）

| 坑 | 风险等级 | 说明 | 规避 |
|---|---|---|---|
| .env 缺 POLY_API_SECRET + POLY_API_PASSPHRASE | **P0 阻塞** | 无法计算 L2 HMAC → 所有私有 endpoint 401 | 先跑 DeriveApiKey（§3.1）补 .env |
| SignerV62 live mode 是 stub | **P0 阻塞** | EIP-712 签名未实现，Sign 返回 InternalError | @老孙 实现 live mode secp256k1 路径 |
| pUSD 余额 / approve | P0 | V2 抵押品是 pUSD，账户需 wrap + approve | 检查链上状态，必要时 wrap |
| min_order_size = $5 可能拒 $1 单 | P1 | 部分 market 最小单 $5，<$1 会 400 | 用 POST /books 确认 min_order_size；改用 $5 测试单 |
| HMAC padding strip（bug #4） | P1 | base64url 结果 strip `=` → 401 | C++ base64url encode 保留 padding，不调任何 rstrip |
| V2 EIP-712 typeHash 字段顺序 | P1 | 字段顺序影响 typeHash；顺序必须与 ctf-exchange-v2 Solidity 完全一致 | @老孙 对照 ctf-exchange-v2 `_hashOrder()` 确认 |
| negRisk market 错误合约地址 | P1 | negRisk=true 需用 V2 NegRisk Exchange（0xe2222...），选错 → 签名无效 | 只测 negRisk=false 的 Moneyline market |
| 401 误判为 key 失效 | 流程 | 90% 的 401 是签名算错 | 走 §B SOP（endpoint-matrix-v3 §B），先看 body，三件检查，5 分钟内禁说 key 失效 |
| `makerBaseFee / takerBaseFee` = 1000 misleading | 低 | 不等于 10% fee；实际 Sports fee = 0.03×p×(1-p) | 用 `feeSchedule.rate` 公式计算，不用 makerBaseFee 字段 |
| clock skew on `POLY_TIMESTAMP` | 中 | 跨洋节点本地时钟可能与 Polymarket 服务端偏差 >30s → 401 | NTP 同步，用 CLOCK_REALTIME；部署后优先测试时钟同步 |

---

## §9 输出字段：OrderAck 填写规范

`LivePolymarketClient::SubmitOrder` 成功返回 `OrderAck` 的字段映射：

| OrderAck 字段 | 来源 | 值 |
|---|---|---|
| `ts` | TimestampQuad | event_ts_ns = CLOB response 中服务端时间戳×1e6（R-20 UPSTREAM_PAYLOAD） |
| `order_id` | CLOB 响应 `orderID` | UUID string |
| `client_order_id` | SignedOrder.client_order_id | echo back |
| `status` | CLOB 响应 `status` | `"matched"` → Filled；`"live"` → Booked；`"unmatched"` → Rejected |
| `nonce` | V2 已废弃 nonce 概念，填 0 | `0` |
| `audit_wal_kind` | **硬固定 `WalKind::RiskAudit`**（live 走真账本，R-11） | `RiskAudit` |
| `reject_reason` | CLOB 响应 `errorMsg` | status=unmatched 时填 |

**R-11 红线**（CLAUDE.md §8 原文）：Paper mode 污染真账本（写入 position / pnl_ledger / nonce_ledger）→ P0。Live mode 必须用 `WalKind::RiskAudit`，不得用 `PaperAudit`。

---

## §10 速查：GM 落地 SubmitOrder 的关键清单

```
[ ] 1. 补 .env：POLY_API_SECRET + POLY_API_PASSPHRASE（运行 DeriveApiKey 一次）
[ ] 2. @老孙：SignerV62 live mode 实现 secp256k1 EIP-712 V2 签名
[ ] 3. 验链上 pUSD 余额 + approve CTF Exchange V2 地址
[ ] 4. 选 market：POST /books 查 min_order_size，选 negRisk=false + acceptingOrders=true
[ ] 5. 读 best ask：GET /book?token_id=<YES_token_id>
[ ] 6. 构造 SignV62Request（timestamp_ms, metadata=bytes32(0), builder=bytes32(0)）
[ ] 7. 构造 L2 HMAC header（5 fields，base_string 不含 querystring，保留 = padding）
[ ] 8. POST /order（libcurl + Content-Type: application/json + 5 header + body）
[ ] 9. 解析响应 orderID + status
[ ] 10. 若 401 → 走 endpoint-matrix-v3 §B SOP（三件检查，不草率说 key 失效）
[ ] 11. @老韩：RM cap 临时放宽允许 $1 测试单通过 evaluate（测试后恢复）
```

---

**最后更新：** 2026-05-31 by 老李 (#07, polymarket-protocol-expert)
**next_review：** 老孙 live signer 实现后 + 首笔成交后 retro
