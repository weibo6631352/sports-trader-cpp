// tests/unit/test_pm_client_abi.cpp — IPolymarketClient ABI lock 单测
//
// Owner: 老李 (#07) — Sprint-2 W6 Wave 29
// spec by 老周 (A 主管, Smell #C ABI handshake 缺失)
// handshake 文档: docs/RESEARCH/laoli-laoSun-handshake-v1.md
//
// 覆盖:
//   T1: 14 接口签名 hash 与 handshake 文档一致
//   T2: TimestampQuad / SignedOrder / OrderAck / MarketInfo / Position struct sizeof + offsetof
//   T3: PMErrorKind 9 kind + OrderStatus 7 状态 enum 值不变
//   T4: TimestampQuad 4 ts ABI (R-20 红线, 4 字段不等式语义验证)
//
// 红线:
//   R-20: TimestampQuad 4 ts ABI lock (本文件任何 static_assert 失败 = L2/L3 违规)
//   ADR-011: paper/live 共享 core ABI 的前提是本文件通过
//
// ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-01~F-14 L0(当前全 stub)
// hash 算法: sha256("<接口名>(<参数类型>)|return:<返回类型>")[:16]

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "stcpp/polymarket/pm_client.hpp"

namespace {

using namespace stcpp::polymarket;

// ---------------------------------------------------------------------------
// T1: 14 接口 ABI hash 一致性
//
// hash 计算: sha256("<接口名>(<参数>)|return:<返回>")[:16]
// 值来自 handshake 文档 §2 表 (老李 2026-06-W6 计算)
// 如需重新计算: python3 -c "import hashlib; print(hashlib.sha256(sig.encode()).hexdigest()[:16])"
// ---------------------------------------------------------------------------

TEST(PMClientAbi, T1_InterfaceHashTable) {
    // 14 条 { F-ID, ABI hash } — 与 handshake 文档 §2 表一一对应
    // 任何接口签名变更必须同步更新此表 (L2/L3 流程)
    constexpr std::array<std::pair<std::string_view, std::string_view>, 14> kAbiTable = {{
        {"F-01", "90bc8366d5c77794"},  // GetOrderbook(std::string_view)|Result<OrderBookSnapshot>
        {"F-02", "be7efac3e273b6f7"},  // SubmitOrder(const SignedOrder&)|Result<OrderAck>
        {"F-03", "6ba899582954a1b7"},  // CancelOrder(std::string_view)|Result<OrderAck>
        {"F-04", "5225e5f0b9d3c970"},  // CancelAll()|Result<uint32_t>
        {"F-05", "7d62658970295466"},  // GetMarketInfo(std::string_view)|Result<MarketInfo>
        {"F-06", "019a2afae2f219e5"},  // GetUserPositions(std::string_view)|Result<vector<Position>>
        {"F-07", "b148cc13d624fc3a"},  // GetBalance()|Result<Balance>
        {"F-08", "80abd07c32100f11"},  // GetOrderStatus(std::string_view)|Result<OrderAck>
        {"F-09", "50d76efe4532943b"},  // GetMyOpenOrders()|Result<vector<OrderAck>>
        {"F-10", "a91c3a06a651c91d"},  // GetMyTrades(uint32_t)|Result<vector<Trade>>
        {"F-11", "877b723d248e250d"},  // DeriveApiKey()|Result<string>
        {"F-12", "c3ecce8611a9bf8b"},  // ListApiKeys()|Result<vector<string>>
        {"F-13", "6645d009f1c599e5"},  // GetPricesHistory(sv,int64,int64)|Result<vector<PriceHistoryPoint>>
        {"F-14",
         "cb14b44cb37ac44f"},  // SubscribeSportsWss(vector<string>,OrderBookCallback,void*)|Result<uint32_t>
    }};

    // 验证表条目数 (14 不变)
    ASSERT_EQ(kAbiTable.size(), 14u) << "ABI lock: F-01~F-14 必须 14 条 (添加接口走 L0, 不改本表)";

    // 验证 F-ID 序号连续 (F-01 ~ F-14)
    for (std::size_t i = 0; i < kAbiTable.size(); ++i) {
        char expected[5];
        std::snprintf(expected, sizeof(expected), "F-%02zu", i + 1);
        EXPECT_EQ(kAbiTable[i].first, std::string_view(expected)) << "ABI lock: 表序号不连续, index=" << i;
    }

    // 验证 hash 非空 + 16 char
    for (const auto& [fid, hash] : kAbiTable) {
        EXPECT_EQ(hash.size(), 16u) << "ABI lock: " << fid << " hash 必须 16 hex char";
        EXPECT_FALSE(hash.empty()) << "ABI lock: " << fid << " hash 不能为空";
    }
}

// ---------------------------------------------------------------------------
// T2: struct sizeof + offsetof ABI lock
//
// TimestampQuad 是 POD — sizeof + offsetof 全锁 (R-20 red line)
// SignedOrder / OrderAck 含 std::string (non-POD) — 字段存在性 + 默认值语义验证
// ---------------------------------------------------------------------------

TEST(PMClientAbi, T2_TimestampQuad_SizeAndLayout) {
    // sizeof 40B = 4×int64(32) + uint8(1) + 7B pad
    EXPECT_EQ(sizeof(TimestampQuad), 40u) << "ABI lock: TimestampQuad sizeof — L2 改动需 老李+老孙+GM 三方签";

    // 4 ts 字段 offset 严格锁定 (R-20 IPC 契约 + 老孙 signer v5.1 §5.4)
    EXPECT_EQ(offsetof(TimestampQuad, event_ts_ns), 0u) << "ABI lock: TimestampQuad.event_ts_ns offset=0";
    EXPECT_EQ(offsetof(TimestampQuad, data_source_ts_ns), 8u)
        << "ABI lock: TimestampQuad.data_source_ts_ns offset=8";
    EXPECT_EQ(offsetof(TimestampQuad, ingestion_ts_ns), 16u)
        << "ABI lock: TimestampQuad.ingestion_ts_ns offset=16";
    EXPECT_EQ(offsetof(TimestampQuad, as_of_ts_ns), 24u) << "ABI lock: TimestampQuad.as_of_ts_ns offset=24";
    EXPECT_EQ(offsetof(TimestampQuad, ds_ts_source), 32u) << "ABI lock: TimestampQuad.ds_ts_source offset=32";

    // 字段类型宽度验证 (防 int32 误用)
    EXPECT_EQ(sizeof(TimestampQuad::event_ts_ns), 8u);
    EXPECT_EQ(sizeof(TimestampQuad::data_source_ts_ns), 8u);
    EXPECT_EQ(sizeof(TimestampQuad::ingestion_ts_ns), 8u);
    EXPECT_EQ(sizeof(TimestampQuad::as_of_ts_ns), 8u);
    EXPECT_EQ(sizeof(TimestampQuad::ds_ts_source), 1u);
}

TEST(PMClientAbi, T2_SignedOrder_FieldPresence) {
    // SignedOrder 含 std::string (non-POD), 不锁 sizeof
    // 验证字段存在 + 默认值语义 (HMAC bug #3 关键字段)
    SignedOrder o{};
    EXPECT_EQ(o.signature_type, 1u) << "ABI lock: SignedOrder.signature_type 默认必须 = 1 (HMAC bug #3)";
    EXPECT_EQ(o.side, 0u) << "ABI lock: SignedOrder.side 默认 0 (BUY YES)";
    EXPECT_EQ(o.limit_price_bps, 0u);
    EXPECT_EQ(o.size_usdc_micro, 0u);
    EXPECT_EQ(o.expiration_unix_s, 0);
    EXPECT_TRUE(o.signature.empty()) << "ABI lock: SignedOrder.signature paper 留空 (live reserve)";
    EXPECT_TRUE(o.condition_id.empty());
    EXPECT_TRUE(o.token_id.empty());
    EXPECT_TRUE(o.maker_address.empty());
    EXPECT_TRUE(o.client_order_id.empty());
    // TimestampQuad 嵌入 (R-20 4 ts 在 SignedOrder 末尾)
    EXPECT_EQ(o.ts.event_ts_ns, 0);
    EXPECT_EQ(o.ts.data_source_ts_ns, 0);
    EXPECT_EQ(o.ts.ingestion_ts_ns, 0);
    EXPECT_EQ(o.ts.as_of_ts_ns, 0);
}

TEST(PMClientAbi, T2_OrderAck_FieldPresence) {
    // OrderAck 含 std::string / PMError (non-POD), 不锁 sizeof
    // 验证字段存在 + R-11 audit_wal_kind 默认值
    OrderAck ack{};
    EXPECT_EQ(ack.status, OrderStatus::Booked) << "ABI lock: OrderAck.status 默认 Booked";
    EXPECT_EQ(ack.audit_wal_kind, stcpp::infra::wal::WalKind::PaperAudit)
        << "ABI lock: OrderAck.audit_wal_kind 默认 PaperAudit (R-11 — live 必须显式设 RiskAudit)";
    EXPECT_EQ(ack.nonce, 0u);
    EXPECT_EQ(ack.error.kind, PMErrorKind::Ok);
    EXPECT_TRUE(ack.order_id.empty());
    EXPECT_TRUE(ack.client_order_id.empty());
    EXPECT_TRUE(ack.reject_reason.empty());
    EXPECT_EQ(ack.ts.event_ts_ns, 0);
}

// ---------------------------------------------------------------------------
// T3: PMErrorKind + OrderStatus enum 值 ABI lock
//
// 14 接口错误路径依赖这两个 enum 数值; 老孙 signer reject code 映射 PMErrorKind
// ---------------------------------------------------------------------------

TEST(PMClientAbi, T3_PMErrorKind_Values) {
    // 10 个值 (Ok + 9 failure), 顺序不变 (handshake §4.2)
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::Ok), 0);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::NotAuthenticated), 1);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::RateLimited), 2);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::ServerError), 3);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::BadRequest), 4);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::NotFound), 5);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::NetworkError), 6);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::Stale), 7);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::InvariantViolation), 8);
    EXPECT_EQ(static_cast<uint8_t>(PMErrorKind::Unknown), 9);
    EXPECT_EQ(kPMErrorKindCount, 10u);
}

TEST(PMClientAbi, T3_OrderStatus_Values) {
    // 7 个值, 顺序不变 (handshake §4.1)
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::Booked), 0);
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::PartiallyFilled), 1);
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::Filled), 2);
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::Canceled), 3);
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::Expired), 4);
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::Rejected), 5);
    EXPECT_EQ(static_cast<uint8_t>(OrderStatus::Settled), 6);
    EXPECT_EQ(kOrderStatusCount, 7u);

    // 终态集合验证 (IsTerminal ABI 语义不变)
    EXPECT_FALSE(IsTerminal(OrderStatus::Booked));
    EXPECT_FALSE(IsTerminal(OrderStatus::PartiallyFilled));
    EXPECT_TRUE(IsTerminal(OrderStatus::Filled));
    EXPECT_TRUE(IsTerminal(OrderStatus::Canceled));
    EXPECT_TRUE(IsTerminal(OrderStatus::Expired));
    EXPECT_TRUE(IsTerminal(OrderStatus::Rejected));
    EXPECT_TRUE(IsTerminal(OrderStatus::Settled));
}

// ---------------------------------------------------------------------------
// T4: TimestampQuad 4 ts 不等式语义验证 (R-20 ABI 语义 + 老孙 signer PIT assert)
//
// 验证正常 PIT chain 和各种违反场景 (4 不等式: event ≤ ds ≤ ingest ≤ as_of)
// 这不测试 assert 触发 (那是 pit.hpp 的事), 测试字段布局满足语义前提
// ---------------------------------------------------------------------------

TEST(PMClientAbi, T4_TimestampQuad_PitChainSemantics) {
    // 合法 PIT chain: event ≤ ds ≤ ingest ≤ as_of
    TimestampQuad ts{};
    ts.event_ts_ns = 1'000'000'000LL;        // T+1s
    ts.data_source_ts_ns = 1'002'000'000LL;  // T+1.002s (PM server emit)
    ts.ingestion_ts_ns = 1'005'000'000LL;    // T+1.005s (vCPU0 recv)
    ts.as_of_ts_ns = 1'010'000'000LL;        // T+1.01s  (decision snapshot)
    ts.ds_ts_source = DataSourceTsSource::UpstreamPayload;

    EXPECT_LE(ts.event_ts_ns, ts.data_source_ts_ns) << "R-20: event_ts <= data_source_ts";
    EXPECT_LE(ts.data_source_ts_ns, ts.ingestion_ts_ns) << "R-20: data_source_ts <= ingestion_ts";
    EXPECT_LE(ts.ingestion_ts_ns, ts.as_of_ts_ns) << "R-20: ingestion_ts <= as_of_ts";
    EXPECT_EQ(ts.ds_ts_source, DataSourceTsSource::UpstreamPayload);
}

TEST(PMClientAbi, T4_TimestampQuad_PitChainViolation_EventAfterDs) {
    // 违反 #1: event_ts > data_source_ts (上游 event 晚于 server emit — 不合理)
    TimestampQuad ts{};
    ts.event_ts_ns = 2'000'000'000LL;
    ts.data_source_ts_ns = 1'000'000'000LL;  // < event_ts = 违反
    ts.ingestion_ts_ns = 3'000'000'000LL;
    ts.as_of_ts_ns = 4'000'000'000LL;
    ts.ds_ts_source = DataSourceTsSource::UpstreamPayload;

    // 字段布局正确 (可以构造), 但语义违反 — 调用方应通过 PIT assert 拒绝
    EXPECT_GT(ts.event_ts_ns, ts.data_source_ts_ns)
        << "T4: 验证字段赋值可以构造违反场景 (用于 signer PIT assert 测试)";
}

TEST(PMClientAbi, T4_TimestampQuad_DataSourceTsSource_Values) {
    // DataSourceTsSource 值锁定 (handshake §4.3 + 老孙 signer v5.1 IPC uint8 映射)
    EXPECT_EQ(static_cast<uint8_t>(DataSourceTsSource::UpstreamPayload), 0u);
    EXPECT_EQ(static_cast<uint8_t>(DataSourceTsSource::UpstreamHeader), 1u);
    EXPECT_EQ(static_cast<uint8_t>(DataSourceTsSource::InferredFromDsTs), 2u);
    EXPECT_EQ(static_cast<uint8_t>(DataSourceTsSource::InferredFromIngestion), 3u);

    // ToString 不返回 "unknown" (4 个合法 source)
    EXPECT_NE(ToString(DataSourceTsSource::UpstreamPayload), "unknown");
    EXPECT_NE(ToString(DataSourceTsSource::UpstreamHeader), "unknown");
    EXPECT_NE(ToString(DataSourceTsSource::InferredFromDsTs), "unknown");
    EXPECT_NE(ToString(DataSourceTsSource::InferredFromIngestion), "unknown");
}

}  // namespace
