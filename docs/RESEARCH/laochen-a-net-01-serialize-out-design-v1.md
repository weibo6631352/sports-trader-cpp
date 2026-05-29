# A-NET-01: serialize_into 出站设计 + 背压策略 v1

- **Owner:** 老陈 (A 系统工程部 IC, #03)
- **Last review:** 2026-05-29
- **Ticket:** A-NET-01 (老周 §8.1 派单, 截止 2026-08-08)
- **依赖:** serialize_into 接口 W9W3 已锁 (WalRecord concept)
- **红线:** R-12 (event loop 禁同步 IO / 锁 > 100us) / R-20 (4 ts) / R-11 (paper 零污染)

---

## §1 现状 audit 结论

### 1.1 serialize_into 接口现状

`infra::wal::WalRecord` concept (`include/stcpp/infra/wal/wal_writer.hpp:39`) 已冻结:

```cpp
{ r.serialize_into(out) } -> std::convertible_to<std::size_t>;
{ T::max_serialized_size() } -> std::convertible_to<std::size_t>;
```

现有实现: `AuditRecord`, `IngestRawRecord`, `PositionRecord`, `FeatureSnapshot`, `TrainingLabel`。
全部是 POD memcpy 风格, 无堆分配, 接口稳定。

**关键发现**: serialize_into 现在只用于 WAL 落盘 (二进制内部格式)。出站到外部 REST API / WSS 的 JSON 序列化 **完全空白** — 目前 debug_api 是手拼字符串 (`endpoint_healthz.cpp:22` 注释明确 "Sprint-4 升 glaze"), live_pm_client.cpp 全部 stub。

### 1.2 WSS 出站现状

`pm_wss_subscriber.hpp` 的 `IWssTransport::AsyncSendText(std::string_view)` 是出站入口, 已是异步非阻塞 (进 io_context queue)。订阅帧 `MakeSubscribeFrame()` 目前在 subscriber 内部手拼 JSON。

### 1.3 REST 出站现状

`live_pm_client.cpp` 全 stub (返 PMErrorKind::Unknown)。`SubmitOrder` 出站需要序列化 `SignedOrder` → JSON POST body。现无任何 HTTP/2 client 实现。

### 1.4 glaze 现状

glaze 在 `endpoint_healthz.cpp` 注释中提及为 Sprint-4 升级目标 (OQ-4), 但 **尚未引入任何 glaze header / CMake target**。项目当前无 glaze 依赖。

---

## §2 serialize_into 出站设计

### 2.1 整体原则

出站序列化分两层:

```
OrderIntent / SignedOrder (业务 struct)
        ↓  [OutboundSerializer: zero-alloc, 复用 buffer]
std::span<char>  JSON text
        ↓  [REST handler / WSS AsyncSendText]
wire
```

serialize_into (WAL binary) 与出站 JSON 序列化 **完全分开** — 前者是内部持久化格式 (concept 已锁), 后者是出站网络格式。命名区分:

- `serialize_into(std::span<std::byte>)` — 已有 WalRecord 接口, 不动
- `serialize_out(OutboundBuffer&)` — 本设计新增, 专给 REST/WSS 出站用

### 2.2 OutboundBuffer: zero-alloc 复用 buffer

```cpp
// include/stcpp/net/outbound_buffer.hpp

namespace stcpp::net {

// OutboundBuffer — 预分配, 复用, 非线程安全 (每条线程/worker 独享一个)
//
// 设计要点:
//   1. 固定容量上界 kMaxOutboundJson = 4096B (SignedOrder JSON 实测 < 1KB)
//   2. reset() O(1) — 只移 write_pos_, 无 memset
//   3. view() 返 string_view, 零拷贝交给 AsyncSendText / HTTP body
//   4. caller 负责在下一次 reset() 前不复用 view()
//   5. 跨洋链路带宽紧: 禁 pretty-print, 全 compact JSON

inline constexpr std::size_t kMaxOutboundJson = 4096;

class OutboundBuffer {
public:
    OutboundBuffer() noexcept = default;

    void reset() noexcept { write_pos_ = 0; }

    // append raw bytes (内部用)
    bool append(std::string_view sv) noexcept {
        if (write_pos_ + sv.size() > kMaxOutboundJson) return false;
        std::memcpy(buf_.data() + write_pos_, sv.data(), sv.size());
        write_pos_ += sv.size();
        return true;
    }

    bool append(char c) noexcept {
        if (write_pos_ >= kMaxOutboundJson) return false;
        buf_[write_pos_++] = static_cast<std::byte>(c);
        return true;
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return {reinterpret_cast<const char*>(buf_.data()), write_pos_};
    }

    [[nodiscard]] std::size_t size() const noexcept { return write_pos_; }

private:
    std::array<std::byte, kMaxOutboundJson> buf_{};
    std::size_t write_pos_{0};
};

} // namespace stcpp::net
```

### 2.3 OutboundSerializer: glaze 出站接口设计

glaze 的 `glz::write_json<T>(val, buf)` / `glz::serialize_into<Opts, T>()` 支持写入已有 buffer, 可做 zero-alloc。设计如下:

```cpp
// include/stcpp/net/outbound_serializer.hpp

#pragma once
#include "stcpp/net/outbound_buffer.hpp"
#include "stcpp/polymarket/pm_client.hpp"  // SignedOrder, OrderAck
#include "stcpp/risk/risk_gateway.hpp"     // OrderIntent (出站 debug/audit 用)

namespace stcpp::net {

// OutboundSerializer — 无状态, 无堆分配, 可单测
// 每个 worker thread 持一个 OutboundBuffer 实例, 传入此类方法复用
//
// 返回 false = buffer 溢出 (kMaxOutboundJson 不够) → caller emit metric + drop/retry

class OutboundSerializer {
public:
    // REST POST /clob/orders 出站 body: SignedOrder → compact JSON
    // caller: live_pm_client.cpp SubmitOrder()
    // 字段对应 Polymarket CLOB v2 POST body (老李 endpoint-matrix-v3 §A)
    [[nodiscard]] static bool SerializeSignedOrder(
        const polymarket::SignedOrder& order,
        OutboundBuffer& buf) noexcept;

    // REST DELETE /clob/orders/{id} 不需要 body, 只 path param — 不在此
    // REST POST /cancel-all 不需要 body

    // WSS 出站: subscribe frame (8 sub topic, 已有 MakeSubscribeFrame 手拼)
    // W10 可迁到此统一管理, v1 暂保留 subscriber 内部实现
    // [[nodiscard]] static bool SerializeSubscribeFrame(
    //     SubTopic topic, std::string_view target_id, OutboundBuffer& buf) noexcept;

    // Debug API JSON 出站 (Sprint-4 替换 endpoint_healthz 手拼)
    // [[nodiscard]] static bool SerializeHealthz(...) noexcept;
};

} // namespace stcpp::net
```

**glaze 集成方式** (不引入 glaze 前的过渡): v1 先用手写 JSON 序列化函数 (与现有 subscriber 一致风格), 结构与 glaze API 兼容。待 Sprint-4 引入 glaze 后, 只需将函数体替换为 `glz::write_json<Opts>` 调用, 调用点不改。

SignedOrder 字段映射 (老李 endpoint-matrix-v3 §A + 老孙 signer_v62 §2.3):

| JSON key          | C++ field                  | 注意                              |
|-------------------|----------------------------|-----------------------------------|
| `conditionId`     | `condition_id`             | bytes32 hex, 带 0x 前缀          |
| `tokenId`         | `token_id`                 | uint256 decimal string, 无 0x    |
| `side`            | `side` (0=BUY/1=SELL)      | 输出字符串 "BUY"/"SELL"           |
| `price`           | `limit_price_bps / 10000.0`| 浮点, 精度 4 位小数               |
| `size`            | `size_usdc_micro / 1e6`    | 浮点                              |
| `expiration`      | `expiration_unix_s`        | 0 → 省略或填 "0"                 |
| `signatureType`   | `signature_type` (= 1)     | HMAC bug #3: 必须 1              |
| `signature`       | `signature`                | base64 保留 padding               |
| `makerAddress`    | `maker_address`            |                                   |

### 2.4 REST handler 接入方式

```
vCPU3 PaperSigner/LiveSigner
    → (worker thread, 非 event loop)
    → live_pm_client.cpp SubmitOrder()
        → buf.reset()
        → OutboundSerializer::SerializeSignedOrder(order, buf)
        → http2_client.PostJson(endpoint, buf.view())  ← nghttp2+Asio 异步
        → await result (在 worker thread 上 await, 不在 WSS event loop)
```

R-12 保障: SubmitOrder 在 vCPU3 worker 线程调用, **不在** WSS event loop (vCPU0)。buf 是线程局部或 per-call stack 分配, 无锁竞争。

### 2.5 WSS 出站接入方式

WSS 出站只有订阅帧 (subscribe/unsubscribe JSON), 不携带 OrderIntent 数据。路径:

```
PMWssSubscriber::Subscribe()
    → MakeSubscribeFrame()  [手拼 JSON, ≤ 200B]
    → IWssTransport::AsyncSendText()  [进 io_context queue, 非阻塞]
```

此路径已符合 R-12。v1 不改动, 仅在设计文档中标注 "Sprint-4 可迁 OutboundSerializer"。

订阅帧 JSON 示意 (不超 200B, kMaxOutboundJson 绰绰有余):

```json
{"type":"subscribe","channel":"market","id":"0xabc..."}
```

---

## §3 背压设计

### 3.1 出站队列拓扑

```
vCPU2 RiskGateway (APPROVED)
        │
        ↓  RiskQueue [SPSC, capacity=4096, drop→REJECT/SYSTEM_BACKPRESSURE]
vCPU3 PaperSigner / Orchestrator
        │
        ├──→ [OutboundSubmitQueue: SPSC, capacity=256, 专给 REST 出站]
        │           ↓  HTTP/2 async worker (非 WSS loop)
        │           → live_pm_client POST /clob/orders
        │
        └──→ WALQueue [已有, capacity=65536]
```

出站专用队列 OutboundSubmitQueue (新增):

- 容量 256 (2^8) — 对应 RiskQueue 4096 × 拒单率 8-20% → 实际 APPROVED ≤ 3277/tick, 下单频率远低于行情, 256 足够 5s+ 缓冲
- 满时策略: **drop + emit metric `rest_submit_drop_total`** (不阻塞, R-12)
- 注: drop 在 vCPU3 Orchestrator 侧, 不在 vCPU0 WSS loop

### 3.2 背压分级策略

| 队列 | 满时策略 | metric | 告警级别 |
|------|----------|--------|----------|
| MarketDataBus (65536) | drop oldest (vCPU0 ingest) | `mdb_drop_total` | P1 |
| SignalQueue (8192) | drop newest | `signal_drop_total` | P1 |
| RiskQueue (4096) | REJECT(SYSTEM_BACKPRESSURE) + audit emit | `rq_backpressure_total` | P0 |
| **OutboundSubmitQueue (256)** | **drop + metric** (新增) | `rest_submit_drop_total` | **P0** |
| WALQueue (65536) | 永不丢 P0 | `walq_overflow_total` | P0 |

OutboundSubmitQueue 满时 P0 告警理由: drop 意味着 APPROVED 订单未送出 → 漏单 → paper PnL 误差。小郑 prom exporter 接 `rest_submit_drop_total > 0` 触发 P0 alert。

### 3.3 HTTP/2 连接池背压

nghttp2 + Asio 连接池:

- 连接数上界: 显式 cap = 4 (老周 v0.6 §17: vCPU0 4-5 conn 拓扑)
- 每连接 concurrent stream 上界: cap = 100 (nghttp2 默认, 可配)
- 当所有 stream slot 满时: `http2_client.Submit()` 返 `PMErrorKind::NetworkError` → Orchestrator 走 retry/backoff, 不阻塞调用线程 (Asio async 模型)

Retry/Backoff (REST 出站):

```
attempt 1: 立即
attempt 2: 200ms
attempt 3: 800ms  (× 4 multiplier)
attempt 4: 3200ms
attempt 5: GIVE_UP → emit audit + drop (最大尝试 5 次, cap 不超 5s 总时长)
```

PMErrorKind → retry 决策映射:

| PMErrorKind | 动作 |
|-------------|------|
| Ok | 成功, 无 retry |
| RateLimited (429) | 退避 × multiplier + 记录 429_ts, 5min 内自我限流 |
| ServerError (5xx) | retry (最多 3 次) |
| NetworkError | retry (最多 5 次) |
| NotAuthenticated (401) | 不 retry, emit audit INVALID_INTENT/AUTH_FAIL, 走人工 SOP |
| BadRequest (400) | 不 retry (签名 bug 或字段错, 需人工修复) |
| NotFound (404) | 不 retry |
| Stale (R-20) | 不 retry (数据过期, 决策已失效) |
| InvariantViolation | 不 retry, P0 incident |

### 3.4 WSS 出站背压

WSS 出站 (订阅帧) 经 `AsyncSendText` 进 io_context queue。Beast 的 async_write queue 本身有缓冲, 极少积压 (订阅帧 < 200B, 频率 < 1/s)。

若 io_context 积压 (极端 case: 大量 subscribe 调用):

- IWssTransport::AsyncSendText 返 false → subscriber 层 emit `wss_send_drop_total` metric → P1 alert
- 不阻塞 OnTextFrame callback (R-12)

---

## §4 与 RM v0.5 / OrderIntent v0.6 字段冻结的衔接

### 4.1 当前依赖状态

OrderIntent v0.6 字段集已在 `include/stcpp/risk/risk_gateway.hpp` ABI lock v1.8 锁定 (Wave 104 P0)。

老沈 RM v0.5 实施 (B 单元, 截止 W10W1) 冻结的是 RM evaluate() 逻辑, 不改 OrderIntent 字段。

### 4.2 并行开工点

老陈 A-NET-01 依赖 OrderIntent → SignedOrder 的字段映射, 而 `SignedOrder` 字段集 **已在 ABI lock v1 (W6) 冻结** (老李 + 老孙签字, laoli-laoSun-handshake-v1.md §3)。

因此:

| 字段集 | 状态 | 老陈可定稿? |
|--------|------|-------------|
| `SignedOrder` (出站 JSON 字段) | ABI lock v1 已锁 | **可立即定稿** |
| `OrderIntent v0.6` (进 RM 字段) | ABI lock v1.8 已锁 | **可立即定稿** |
| RM evaluate() 逻辑 | 老沈 W10W1 实施中 | 不影响出站序列化 |
| RM v0.5 字段冻结 (§8.2 A↔B 契约) | W10 多人讨论会签字 | 讨论会前可并行 |

**结论: 老陈 A-NET-01 现在即可开工, 无需等老沈 RM v0.5 完成。** 出站序列化只依赖 SignedOrder (已锁) 和 HTTP/2 wire 层, 与 RM evaluate() 逻辑完全解耦。

### 4.3 一旦老沈冻结即可定稿的唯一剩余项

RM v0.5 字段冻结 (§8.2 A↔B 接口契约签字) 后, 老陈需确认的只有一点:

- `OrderIntent.size_pUSD_micro` rename (v0.6) → 对应 SignedOrder 出站的 `size` 字段转换公式: `size = intent.size_pUSD_micro / 1_000_000.0` (pUSD micro → pUSD 浮点)
- 这一换算在 `OutboundSerializer::SerializeSignedOrder()` 内部处理, 字段名变化不影响出站 JSON key (JSON key 固定为 Polymarket CLOB API 要求的 `"size"`)

**因此 RM v0.5 冻结对老陈是零阻塞: 换算公式已知, 可先写好, 字段名只影响 C++ 侧读取路径 (已在 risk_gateway.hpp v0.6 定义)。**

---

## §5 Skeleton 接口 (.h 草案)

以下两个头文件草案。语法正确, 无 .cpp 实现。

### 5.1 `include/stcpp/net/outbound_buffer.hpp`

```cpp
// stcpp/net/outbound_buffer.hpp — 出站 JSON 复用 buffer
//
// Owner: 老陈 (A-NET-01)  Last review: 2026-05-29
// 红线: zero-alloc, 非线程安全 (per-worker 独享)
// 容量 4096B: SignedOrder JSON 实测 < 1KB, 留 4x 余量

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace stcpp::net {

inline constexpr std::size_t kMaxOutboundJson = 4096;

class OutboundBuffer {
public:
    OutboundBuffer() noexcept = default;

    // 重置游标, O(1), 无 memset
    void reset() noexcept { write_pos_ = 0; }

    // 追加字符串. 溢出返 false (caller emit metric + drop)
    [[nodiscard]] bool append(std::string_view sv) noexcept {
        if (write_pos_ + sv.size() > kMaxOutboundJson) return false;
        std::memcpy(buf_.data() + write_pos_, sv.data(), sv.size());
        write_pos_ += sv.size();
        return true;
    }

    [[nodiscard]] bool append(char c) noexcept {
        if (write_pos_ >= kMaxOutboundJson) return false;
        buf_[write_pos_++] = static_cast<std::byte>(c);
        return true;
    }

    // 返 string_view, 生命期 = buf_ (reset() 前有效)
    [[nodiscard]] std::string_view view() const noexcept {
        return {reinterpret_cast<const char*>(buf_.data()), write_pos_};
    }

    [[nodiscard]] std::size_t size() const noexcept { return write_pos_; }
    [[nodiscard]] bool        empty() const noexcept { return write_pos_ == 0; }

private:
    std::array<std::byte, kMaxOutboundJson> buf_{};
    std::size_t write_pos_{0};
};

} // namespace stcpp::net
```

### 5.2 `include/stcpp/net/outbound_serializer.hpp`

```cpp
// stcpp/net/outbound_serializer.hpp — 出站 JSON 序列化 (REST POST body + WSS frame)
//
// Owner: 老陈 (A-NET-01)  Last review: 2026-05-29
//
// 红线:
//   - noexcept, zero-alloc (使用 OutboundBuffer)
//   - 不在 WSS event loop 调用 (R-12; caller = vCPU3 worker)
//   - compact JSON only (跨洋带宽紧)
//   - HMAC bug #3: signature_type 必须序列化为整数 1
//
// 依赖: ABI lock v1 SignedOrder (已锁); Sprint-4 升 glaze 后只换函数体

#pragma once

#include <cstdint>
#include <string_view>

#include "stcpp/net/outbound_buffer.hpp"
#include "stcpp/polymarket/pm_client.hpp"  // SignedOrder

namespace stcpp::net {

class OutboundSerializer {
public:
    // 序列化 SignedOrder → compact JSON, 写入 buf
    // 用途: live_pm_client POST /clob/orders body
    // 字段映射: CLOB v2 API (老李 endpoint-matrix-v3 §A + 老孙 signer_v62 §2.3)
    // 返 false: buf 溢出 (caller drop + emit rest_submit_overflow_total)
    //
    // 生成 JSON 示意:
    // {"conditionId":"0x...","tokenId":"123...","side":"BUY","price":0.5500,
    //  "size":10.000000,"expiration":0,"signatureType":1,
    //  "signature":"base64==","makerAddress":"0x..."}
    [[nodiscard]] static bool SerializeSignedOrder(
        const polymarket::SignedOrder& order,
        OutboundBuffer& buf) noexcept;

    // 序列化 cancel-all 请求 body (POST /cancel-all, 无 body 参数时传空 JSON)
    // Polymarket cancel-all: body = "{}" 即可
    [[nodiscard]] static bool SerializeCancelAll(OutboundBuffer& buf) noexcept;

    // 序列化 WSS 订阅帧 (可选, v1 先用 subscriber 内部手拼; Sprint-4 迁至此)
    // [[nodiscard]] static bool SerializeWssSubscribe(
    //     std::string_view channel, std::string_view target_id,
    //     OutboundBuffer& buf) noexcept;
};

} // namespace stcpp::net
```

### 5.3 `include/stcpp/net/http2_client.hpp` (接口草案, 不含 nghttp2 实现)

```cpp
// stcpp/net/http2_client.hpp — HTTP/2 client 接口 (nghttp2 + Asio 实现在 .cpp)
//
// Owner: 老陈 (A-NET-01)  Last review: 2026-05-29
//
// 红线:
//   R-12: Submit 异步非阻塞. 回调在 Asio io_context 线程, 内严禁 block
//   连接池 cap: kMaxConnections = 4 (老周 v0.6 §17)
//   重试/退避: 由 Orchestrator 层控制, Http2Client 本身不重试
//
// 不耻下问:
//   - nghttp2 session 生命期 → @老周 (architecture)
//   - TLS SNI / BoringSSL ctx → 我 (老陈) 管 TLS transport

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "stcpp/polymarket/pm_client.hpp"  // PMErrorKind, PMError

namespace stcpp::net {

inline constexpr std::size_t kMaxHttp2Connections = 4;
inline constexpr std::size_t kMaxConcurrentStreams = 100;

struct Http2Response {
    int         status{0};
    std::string body;
    PMErrorKind error{polymarket::PMErrorKind::Ok};
};

// 回调在 Asio io_context 线程, 内严禁阻塞 (R-12)
using Http2Callback = std::function<void(Http2Response)>;

class Http2Client {
public:
    // cfg: host + port + TLS SNI
    explicit Http2Client(std::string host, std::uint16_t port) noexcept;
    ~Http2Client();

    Http2Client(const Http2Client&)            = delete;
    Http2Client& operator=(const Http2Client&) = delete;

    // 异步 POST. body_view 生命期必须持续到回调 (caller 持 OutboundBuffer 不 reset)
    // 返 false: 连接池满 / 未连接 (caller 走 retry 逻辑)
    [[nodiscard]] bool AsyncPost(
        std::string_view path,
        std::string_view body_view,
        std::string_view content_type,
        Http2Callback    cb) noexcept;

    // 异步 DELETE (撤单, 无 body)
    [[nodiscard]] bool AsyncDelete(
        std::string_view path,
        Http2Callback    cb) noexcept;

    // 异步 GET (行情查询等)
    [[nodiscard]] bool AsyncGet(
        std::string_view path,
        Http2Callback    cb) noexcept;

    // 连接管理 (Asio io_context 内异步)
    void Connect() noexcept;   // 触发连接, 非阻塞
    void Close() noexcept;

    [[nodiscard]] bool IsConnected() const noexcept;
    [[nodiscard]] std::size_t ActiveStreams() const noexcept;

private:
    struct Impl_;
    std::unique_ptr<Impl_> impl_;
};

} // namespace stcpp::net
```

---

## §6 R-12 合规性分析

| 路径 | 线程 | 是否在 event loop | R-12 合规 |
|------|------|-------------------|-----------|
| OnTextFrame → TryPush(WssEvent) | vCPU0 ingest (Beast io_ctx) | 是 | 合规 (TryPush O(1) 非阻塞) |
| Subscribe() → AsyncSendText() | 调用方线程 (vCPU3 或初始化) | 否 | 合规 |
| SubmitOrder() → SerializeSignedOrder() → AsyncPost() | vCPU3 worker | 否 | 合规 |
| OutboundBuffer::append() | vCPU3 worker | 否 | 合规 (纯内存操作) |
| Http2Client 回调 | Asio io_ctx (独立线程) | 是 | 回调内仅做 TryPush → 合规 |

**严格禁止的模式** (已在设计中排除):
- 在 OnTextFrame callback 内调用 SubmitOrder() — 任何 REST 调用
- 在 OnTextFrame callback 内持 mutex > 100us
- Http2Client::AsyncPost 的回调内做同步 REST

---

## §7 实施路径 (截止 2026-08-08)

| 周 | 任务 | 产出 |
|----|------|------|
| W10 (现在) | 本文档 + outbound_buffer.hpp + outbound_serializer.hpp 骨架 | 接口草案 (本文件) |
| W11 | outbound_serializer.cpp 实现 (手写 JSON, 无 glaze 依赖) + 单测 | 可测试序列化 |
| W12 | http2_client.hpp/cpp (nghttp2+Asio stub, 连接 + AsyncPost) | 可 wire 到 live_pm_client |
| W13 | live_pm_client SubmitOrder/CancelOrder 接 Http2Client + 集成测试 | REST 出站联通 |
| W14 | OutboundSubmitQueue 背压 + prom metric 接小郑 exporter | 背压监控 |
| ~8-08 | 端到端 paper runtime 出站跑通, R-12/R-20/R-11 CI 验证 | A-NET-01 交付 |

---

## §8 核心结论摘要

1. **serialize_into (WalRecord) 与出站 JSON 完全独立**: WalRecord concept 只管内部 WAL binary, 不触碰网络 JSON。新增 `OutboundBuffer + OutboundSerializer` 体系处理出站, 零交叉污染。

2. **zero-alloc 方案可行**: `OutboundBuffer` 固定 4096B 栈/成员分配, `string_view` 零拷贝传给 `AsyncSendText` / `AsyncPost`, 热路径无 heap alloc。

3. **R-12 完全合规**: SubmitOrder 在 vCPU3 worker 调用, Http2 回调在独立 Asio io_ctx, 无任何 REST/阻塞 IO 进入 vCPU0 WSS event loop。

4. **背压**: OutboundSubmitQueue (256, 2^8) drop + P0 metric; HTTP/2 stream 满时 caller 走 retry/backoff (5 次上限, 总时长 ≤ 5s); 全程非阻塞。

5. **与 RM 并行开工**: SignedOrder ABI lock v1 已锁 (老李+老孙 W6 签字), OrderIntent v0.6 已锁 (Wave 104)。老陈现在即可开工, 无需等老沈 RM v0.5 逻辑完成。RM v0.5 字段冻结后唯一需要确认的是 `size_pUSD_micro / 1e6` 换算, 已在设计中覆盖。

6. **glaze 路径**: v1 手写 JSON (与现有 subscriber 风格一致), 接口兼容 glaze API 约定, Sprint-4 升级只换函数体, 调用点不动。
