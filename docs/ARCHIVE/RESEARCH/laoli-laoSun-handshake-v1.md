# IPolymarketClient ABI Lock + 老李-老孙 Handshake v1

- owner: 老李 (polymarket-protocol-expert, #07)
- co-signer: 老孙 (crypto-signing-expert, #06)
- lock_date: 2026-06-W6
- last_review: 2026-06-W6
- sprint: Sprint-2 W6 Wave 29
- adr_ref: ADR-009 v2 (Sonnet), ADR-011 (paper/live binary 共享)
- 关联:
  - `include/stcpp/polymarket/pm_client.hpp`         — IPolymarketClient 14 接口契约
  - `include/stcpp/polymarket/live/live_pm_client.hpp` — Live stub (M5+ deferred)
  - `docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md` §3.1 (14 接口)
  - `docs/RESEARCH/laosun-key-management-v5.1.md`   — signer v5.1 SignRequest/SignResponse IPC
  - `docs/MEETINGS/2026-06-01-vote-laozhou-arch-challenge.md` Smell #C (ABI handshake 缺失)

---

## 0. 背景 + 目的

老周 Smell #C: `live_pm_client.hpp` 14 接口全返回 Unknown, 但无文档承诺这 14 个接口 ABI locked.
老李 + 老孙 之间没有明确 handshake. M5+ 接入时如果接口要改, 代价极高.

**本文目的:** 正式锁定 IPolymarketClient 14 接口 ABI, 建立双方改动协议, 为 M5+ live 接入提供合同边界.

**W6 现状:** 14 接口全 stub (LivePolymarketClient 返回 Unknown), ABI lock 仅针对接口签名 + 参数 struct 布局.
活接入 (live 真实现) 在 M5+ 按 W6→W7→W8→M5 接入计划推进, 本文不约束实现细节.

---

## 1. 4 类 ABI 改动等级

| 等级 | 名称 | 定义 | 审批要求 | 典型例子 |
|------|------|------|----------|---------|
| **L0** | Safe | 新增独立接口 (不改现有接口签名) | 自动通过, 无需会签 | IPolymarketClient 加 F-15 新方法 |
| **L1** | Patch | POD struct 末尾追加新字段 (现有字段不动) | 老李 ack (单签) | SignedOrder 末尾加 `std::string routing_hint` |
| **L2** | Major | 改字段类型 / 顺序 / 大小; 改接口参数类型 | **老李 + 老孙 + GM 三方签** | `limit_price_bps` 从 uint32 改 uint64 |
| **L3** | Breaking | 删除接口; 改接口名; 改返回类型 | **老郭 (架构评审) + 老韩 (RM) + GM 三方签** | 删 F-04 CancelAll; 把 Result<OrderAck> 改 std::optional |

### 1.1 等级适用说明

- `std::string` / `std::vector` 成员: 内部堆布局不算 ABI (标准库 ABI 由编译器保证), 但字段存在性 + 语义不可删改 (L3).
- `TimestampQuad` 是 R-20 核心合约, 4 字段任意改动均视为 **L2**.
- `OrderStatus` enum 值: 加新 variant = L1; 改已有值 = L3.
- `PMErrorKind` enum 值: 同上.
- `WalKind` 字段语义 (audit_wal_kind): R-11 红线, 改动视为 L2.

---

## 2. 14 接口 ABI Lock 表

hash 算法: `sha256("<接口名>(<参数类型>..)|return:<返回类型>")` 取前 16 hex char.
参数约定: `const T&` 写 `const T&`; `std::string_view` 写原文; namespace 省略 (以 `stcpp::polymarket` 为根).

| ID | 接口名 | 参数签名 | 返回类型 | ABI Hash (sha256[:16]) | 调用约定 |
|----|--------|----------|----------|------------------------|---------|
| F-01 | `GetOrderbook` | `std::string_view condition_id` | `Result<OrderBookSnapshot>` | `90bc8366d5c77794` | 同步, noexcept, [[nodiscard]] |
| F-02 | `SubmitOrder` | `const SignedOrder& order` | `Result<OrderAck>` | `be7efac3e273b6f7` | 同步, noexcept, [[nodiscard]] |
| F-03 | `CancelOrder` | `std::string_view order_id` | `Result<OrderAck>` | `6ba899582954a1b7` | 同步, noexcept, [[nodiscard]] |
| F-04 | `CancelAll` | (void) | `Result<std::uint32_t>` | `5225e5f0b9d3c970` | 同步, noexcept, [[nodiscard]] |
| F-05 | `GetMarketInfo` | `std::string_view condition_id` | `Result<MarketInfo>` | `7d62658970295466` | 同步, noexcept, [[nodiscard]] |
| F-06 | `GetUserPositions` | `std::string_view funder_addr` | `Result<std::vector<Position>>` | `019a2afae2f219e5` | 同步, noexcept, [[nodiscard]] |
| F-07 | `GetBalance` | (void) | `Result<Balance>` | `b148cc13d624fc3a` | 同步, noexcept, [[nodiscard]] |
| F-08 | `GetOrderStatus` | `std::string_view order_id` | `Result<OrderAck>` | `80abd07c32100f11` | 同步, noexcept, [[nodiscard]] |
| F-09 | `GetMyOpenOrders` | (void) | `Result<std::vector<OrderAck>>` | `50d76efe4532943b` | 同步, noexcept, [[nodiscard]] |
| F-10 | `GetMyTrades` | `std::uint32_t limit` | `Result<std::vector<Trade>>` | `a91c3a06a651c91d` | 同步, noexcept, [[nodiscard]] |
| F-11 | `DeriveApiKey` | (void) | `Result<std::string>` | `877b723d248e250d` | 同步, noexcept, [[nodiscard]] |
| F-12 | `ListApiKeys` | (void) | `Result<std::vector<std::string>>` | `c3ecce8611a9bf8b` | 同步, noexcept, [[nodiscard]] |
| F-13 | `GetPricesHistory` | `std::string_view token_id, std::int64_t start_unix_s, std::int64_t end_unix_s` | `Result<std::vector<PriceHistoryPoint>>` | `6645d009f1c599e5` | 同步, noexcept, [[nodiscard]] |
| F-14 | `SubscribeSportsWss` | `const std::vector<std::string>& condition_ids, OrderBookCallback cb, void* user_data` | `Result<std::uint32_t>` | `cb14b44cb37ac44f` | 同步 (注册 cb), noexcept, [[nodiscard]] |

**调用约定约束:**
- 全部接口 `noexcept` — 实现层禁抛异常, 错误走 `Result<T>.error`.
- `[[nodiscard]]` — caller 必须消费返回值, CI clang 警告级.
- 全部同步 (R-12): caller 在 worker pool 调; live 实现内部异步, 但接口边界同步.
- F-14 callback `OrderBookCallback = void (*)(const OrderBookSnapshot&, void*)` — C-style 函数指针, 禁 std::function (R-12 热路径堆分配).

---

## 3. 关键 Struct ABI Hash

| Struct | 字段顺序 (声明顺序) | ABI Hash (sha256[:16]) | 备注 |
|--------|---------------------|------------------------|------|
| `TimestampQuad` | event_ts_ns, data_source_ts_ns, ingestion_ts_ns, as_of_ts_ns, ds_ts_source | `5c7125f93554f9dd` | R-20 核心, 任何字段改动 = L2 |
| `SignedOrder` | condition_id, token_id, side, limit_price_bps, size_usdc_micro, expiration_unix_s, signature_type, signature, maker_address, client_order_id, ts | `9c156025c5d86914` | F-02 输入; HMAC 4 bug 字段 signature_type 必须存在且 = 1 |
| `OrderAck` | ts, order_id, client_order_id, status, error, reject_reason, nonce, audit_wal_kind | `2806661ac6578504` | F-02/03/04/08 输出; R-11 audit_wal_kind 字段语义锁 |
| `MarketInfo` | ts, condition_id, outcomes, tick_size_bps, neg_risk, fee_rate_bps, accepting_orders, game_start_time_unix_s, clob_token_ids, liquidity_usdc | `0a270894c681f57c` | F-05 输出 |
| `Position` | condition_id, token_id, size_micro, avg_price_bps, cur_price_bps, redeemable, mergeable, ts | `845f023e1819698f` | F-06 输出; R-20 ts 在末尾 |

**POD 布局已知 (x86-64 LP64, `#pragma pack` 默认):**

`TimestampQuad`:
- offset 0: `event_ts_ns` (int64, 8B)
- offset 8: `data_source_ts_ns` (int64, 8B)
- offset 16: `ingestion_ts_ns` (int64, 8B)
- offset 24: `as_of_ts_ns` (int64, 8B)
- offset 32: `ds_ts_source` (uint8, 1B) + 7B pad
- sizeof = 40B

`SignedOrder` 含 `std::string` (non-POD), sizeof 平台相关; 字段顺序锁定, 不通过 sizeof 断言 (改用 offsetof std::string 无意义).

`OrderAck` 含 `std::string` / `PMError` (non-POD); 字段顺序锁定, 通过测试 T2 静态初始化验证字段存在性.

---

## 4. Enum 值 ABI Lock

### 4.1 OrderStatus (7 enum, §6.1)

| 枚举名 | 值 | 终态 |
|--------|----|------|
| Booked | 0 | 否 |
| PartiallyFilled | 1 | 否 |
| Filled | 2 | 是 |
| Canceled | 3 | 是 |
| Expired | 4 | 是 |
| Rejected | 5 | 是 |
| Settled | 6 | 是 |

**锁定:** 值 0-6 不可重新赋值; 新 variant 必须从 7 开始追加 (L1).

### 4.2 PMErrorKind (9 kind + Ok = 10, §3.2)

| 枚举名 | 值 |
|--------|----|
| Ok | 0 |
| NotAuthenticated | 1 |
| RateLimited | 2 |
| ServerError | 3 |
| BadRequest | 4 |
| NotFound | 5 |
| NetworkError | 6 |
| Stale | 7 |
| InvariantViolation | 8 |
| Unknown | 9 |

**锁定:** 值 0-9 不可重新赋值; 新 kind 从 10 追加 (L1).

### 4.3 DataSourceTsSource (4 enum, R-20)

| 枚举名 | 值 |
|--------|----|
| UpstreamPayload | 0 |
| UpstreamHeader | 1 |
| InferredFromDsTs | 2 |
| InferredFromIngestion | 3 |

**锁定:** R-20 核心, 值 0-3 不可改; 老孙 signer v5.1 `data_source_ts_source` 字段 (uint8) 与本 enum 直接映射, 任何值改动 = signer IPC 协议破坏 (L3).

---

## 5. 老李-老孙 接口边界 (signer 视角)

老孙 signer v5.1 (`laosun-key-management-v5.1.md`) 接受 `SignRequest` (msgpack/UDS) 并返回 `SignResponse`. IPolymarketClient::SubmitOrder (F-02) 的 `SignedOrder.signature` 字段由 signer 填写.

**接口边界协议:**

| 边界 | 老李负责 | 老孙负责 |
|------|---------|---------|
| SignedOrder 字段集合 | 定义 + 锁定 (本文) | 消费 SignedOrder 构建 SignRequest.typed_data |
| signature_type 值 | 声明语义 (必须 = 1, HMAC bug #3) | 验证 + 拒绝 sigType != 1 (B5 typed-data 二次校验) |
| TimestampQuad 4 ts | 定义字段 + R-20 不等式 | PIT assert (signer v5.1 §5.4.2 4 不等式) |
| DataSourceTsSource enum 值 | 定义 0-3 mapping | signer v5.1 `data_source_ts_source` uint8 映射 |
| F-02 调用约定 (同步) | 接口契约声明 | signer 内部异步 OK, 但 F-02 boundary 同步阻塞 |

**M5+ 接入前必须 ack 的事项 (双方联合 checklist):**

- [ ] LivePolymarketClient::SubmitOrder 调用 signer IPC (老孙 v5.1 §2.3 UDS) byte-equal 14 wire vector
- [ ] SignedOrder.signature 由 signer SignResponse.sig (65B r||s||v) 编码为 base64 (保留 padding, HMAC bug #4)
- [ ] HMAC L2 sigType=1 byte-equal 测试 (老孙 v3 §D 14 vector, 老陈 W7 集成测试)
- [ ] signer PIT assert 4 不等式与 pm_client TimestampQuad 4 ts 对齐

---

## 6. W6 现状 + M5+ 解锁计划

**W6 状态:** 14 接口全 L0 占位 (live stub 不动), ABI lock 文档已签.

| 里程碑 | 接入内容 | ABI 等级 |
|--------|---------|---------|
| W6 | gamma /events listing + clob /books (公开) | L0 (新实现, 不改接口) |
| W7 | HMAC L2 14 wire vector byte-equal + sigType=1 | L0 |
| W8 | clob /ws/market + /ws/user (HMAC) | L0 |
| M5 | POST /clob/orders (真 EIP-712 + 老孙 signer v5.1 IPC) | L0 (接口签名不变, 实现切真) |

**原则:** live 接入不改接口签名, 仅填充 stub 实现. 若实测发现接口确需变更 → 走 L2/L3 流程, 提前 2 sprint 提案.

---

## 7. PR 引用要求

任何修改以下文件的 PR **必须在 PR 描述中引用本文档** (`laoli-laoSun-handshake-v1.md`):

- `include/stcpp/polymarket/pm_client.hpp`
- `include/stcpp/polymarket/live/live_pm_client.hpp`

引用格式: `ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>`

ABI hash 变更的 PR 必须同时更新 `tests/unit/test_pm_client_abi.cpp` 的期望 hash 值.
CI `tests/ci_grep/abi_lock.py` (老高 W6 W2 落地) 将在 PR 未引用时 fail.

---

## 8. 双方签字

本 handshake 锁定 2026-06-W6, M5+ 之前 14 接口不得做 L2/L3 改动.
L2 改动需**老李 + 老孙 + GM 三方**重新签 v2; L3 改动需**老郭 + 老韩 + GM 三方**签.

**老李 (polymarket-protocol-expert, #07):** 已确认 14 接口 ABI hash + struct 字段顺序 + enum 值正确.

**老孙 (crypto-signing-expert, #06):** 已确认 signer IPC SignRequest/SignResponse 与本 handshake TimestampQuad + DataSourceTsSource + signature_type 语义对齐.

---

**lock_version:** v1
**next_review:** M5 接入前 (老李 + 老孙 联合 review)
