// tests/unit/test_paper_pm_client.cpp — PaperPolymarketClient v0.1 单测
//
// Owner: 老李 (#07) — Sprint-2 W5 Wave 24 (W5-A-01)
// 落:
//   docs/RESEARCH/laoli-polymarket-backend-requirements-v1.md §3.1 (14 接口) / §3.2 (9 PMError)
//                                                        §6.1 (7 OrderStatus) / §5 (HMAC 4 bug)
// 红线: R-7 mode tag / R-11 PaperAudit / R-20 PIT chain / HMAC sigType=1
//
// 覆盖:
//   - 14 接口 (F-01 ~ F-14) 各 1 基础 case (paper mode)
//   - 4 ts R-20 UPSTREAM_PAYLOAD 透传
//   - PMError 9 kind 各 1 case (可触发的 paper 路径)
//   - OrderStatus 7 状态机转移 (合法 + 非法各覆)
//   - HMAC bug #3 反模式拦截 (sigType != 1 → Rejected)

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "stcpp/execution/execution_mode.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/infra/wal/wal_kind.hpp"
#include "stcpp/polymarket/paper/paper_pm_client.hpp"
#include "stcpp/polymarket/pm_client.hpp"

namespace {

using namespace stcpp;
using PaperPolymarketClient = polymarket::paper::PaperPolymarketClient;

constexpr const char* kCidA = "0xaaa";
constexpr const char* kCidB = "0xbbb";
constexpr const char* kTokA = "1111";

polymarket::TimestampQuad MakeValidTs(std::int64_t now) {
    polymarket::TimestampQuad ts;
    ts.event_ts_ns        = now - 10'000'000;
    ts.data_source_ts_ns  = now -  8'000'000;
    ts.ingestion_ts_ns    = now -  4'000'000;
    ts.as_of_ts_ns        = now;
    ts.ds_ts_source       = polymarket::DataSourceTsSource::UpstreamPayload;
    return ts;
}

polymarket::SignedOrder MakeValidOrder(std::int64_t now, std::string client_id = "c1") {
    polymarket::SignedOrder o;
    o.condition_id      = kCidA;
    o.token_id          = kTokA;
    o.side              = 0;            // BUY YES
    o.limit_price_bps   = 5500;         // 0.55
    o.size_usdc_micro   = 100'000'000;  // $100
    o.expiration_unix_s = 0;            // GTC
    o.signature_type    = 1;            // HMAC bug #3: 必 sigType=1
    o.signature         = "";           // paper 不签真 (live reserve)
    o.maker_address     = "0xfunder";
    o.client_order_id   = std::move(client_id);
    o.ts                = MakeValidTs(now);
    return o;
}

polymarket::OrderBookSnapshot MakeOrderbook(std::string_view cid, std::int64_t now) {
    polymarket::OrderBookSnapshot s;
    s.condition_id = std::string{cid};
    s.token_id     = kTokA;
    s.ts           = MakeValidTs(now);
    s.tick_size_bps = 10;
    s.neg_risk      = false;
    // L5 yes_bids: 0.54 / 0.53 / 0.52 / 0.51 / 0.50  (降序)
    for (std::size_t i = 0; i < polymarket::kBookDepth; ++i) {
        s.yes_bids[i].price_bps        = static_cast<std::uint32_t>(5400 - 100 * i);
        s.yes_bids[i].size_usdc_micro  = (i + 1) * 100'000'000ULL;
        s.yes_bids[i].level_ts_ns      = now - 1'000'000;
    }
    // L5 yes_asks: 0.56 / 0.57 / 0.58 / 0.59 / 0.60  (升序)
    for (std::size_t i = 0; i < polymarket::kBookDepth; ++i) {
        s.yes_asks[i].price_bps        = static_cast<std::uint32_t>(5600 + 100 * i);
        s.yes_asks[i].size_usdc_micro  = (i + 1) * 100'000'000ULL;
        s.yes_asks[i].level_ts_ns      = now - 1'000'000;
    }
    return s;
}

}  // namespace

// ============================================================
// 基本: Mode tag (R-7)
// ============================================================

TEST(PaperPmClient, ModeIsPaper) {
    PaperPolymarketClient c{};
    EXPECT_EQ(c.Mode(), execution::ExecutionMode::Paper);
}

// ============================================================
// F-01 GetOrderbook + PMError NotFound
// ============================================================

TEST(PaperPmClient, F01_GetOrderbook_HitInjectedMirror) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    c.TestInjectOrderbook(MakeOrderbook(kCidA, now));
    auto r = c.GetOrderbook(kCidA);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->condition_id, std::string{kCidA});
    EXPECT_EQ(r.value->yes_bids[0].price_bps, 5400u);
    EXPECT_EQ(r.value->yes_asks[0].price_bps, 5600u);
    // R-20: 4 ts UPSTREAM_PAYLOAD 透传
    EXPECT_EQ(r.value->ts.ds_ts_source, polymarket::DataSourceTsSource::UpstreamPayload);
    EXPECT_GT(r.value->ts.event_ts_ns, 0);
    EXPECT_LE(r.value->ts.event_ts_ns, r.value->ts.data_source_ts_ns);
    EXPECT_LE(r.value->ts.data_source_ts_ns, r.value->ts.ingestion_ts_ns);
    EXPECT_LE(r.value->ts.ingestion_ts_ns, r.value->ts.as_of_ts_ns);
}

TEST(PaperPmClient, F01_GetOrderbook_NotFound) {
    PaperPolymarketClient c{};
    auto r = c.GetOrderbook("0xmissing");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::NotFound);
    EXPECT_EQ(r.error.http_status, 404);
}

// ============================================================
// F-02 SubmitOrder + HMAC bug #3 + PIT InvariantViolation + BadRequest
// ============================================================

TEST(PaperPmClient, F02_SubmitOrder_BookedWithPaperAudit) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto r = c.SubmitOrder(MakeValidOrder(now));
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->status, polymarket::OrderStatus::Booked);
    // R-11: paper OrderAck 必走 PaperAudit
    EXPECT_EQ(r.value->audit_wal_kind, infra::wal::WalKind::PaperAudit);
    // order_id paper-NNN 前缀
    EXPECT_NE(r.value->order_id.find("paper-"), std::string::npos);
    EXPECT_EQ(r.value->client_order_id, std::string{"c1"});
}

TEST(PaperPmClient, F02_HmacBug3_SigTypeMustBeOne) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto bad = MakeValidOrder(now);
    bad.signature_type = 2;  // HMAC bug #3 反模式 (Polymarket proxy, 我们是 Magic 1-of-1 Safe)
    auto r = c.SubmitOrder(bad);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::BadRequest);
    ASSERT_TRUE(r.value.has_value());
    EXPECT_EQ(r.value->status, polymarket::OrderStatus::Rejected);
    EXPECT_NE(r.value->reject_reason.find("signature_type must be 1"), std::string::npos);
    // R-11: 哪怕 Rejected, paper OrderAck 仍走 PaperAudit
    EXPECT_EQ(r.value->audit_wal_kind, infra::wal::WalKind::PaperAudit);
}

TEST(PaperPmClient, F02_PitViolation_ReturnsInvariantViolation) {
    PaperPolymarketClient c{};
    auto bad = MakeValidOrder(infra::wal::pit::NowRealtimeNs());
    bad.ts.event_ts_ns = 0;  // R-20: event_ts == 0 → PIT chain 违反
    auto r = c.SubmitOrder(bad);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::InvariantViolation);
}

TEST(PaperPmClient, F02_BadRequest_PriceOutOfRange) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto bad = MakeValidOrder(now);
    bad.limit_price_bps = 12000;  // > 10000 bps (Polymarket 概率上限)
    auto r = c.SubmitOrder(bad);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::BadRequest);
    ASSERT_TRUE(r.value.has_value());
    EXPECT_EQ(r.value->status, polymarket::OrderStatus::Rejected);
}

// ============================================================
// F-03 CancelOrder + 7 状态机
// ============================================================

TEST(PaperPmClient, F03_CancelOrder_FromBookedToCanceled) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto sub = c.SubmitOrder(MakeValidOrder(now));
    ASSERT_TRUE(sub.ok());
    auto r = c.CancelOrder(sub.value->order_id);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->status, polymarket::OrderStatus::Canceled);
    EXPECT_TRUE(polymarket::IsTerminal(r.value->status));
}

TEST(PaperPmClient, F03_CancelOrder_NotFound) {
    PaperPolymarketClient c{};
    auto r = c.CancelOrder("paper-9999999999");
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::NotFound);
}

TEST(PaperPmClient, F03_CancelOrder_TerminalRejected) {
    // 已 Filled 不可撤 (合法转移仅 Filled → Settled)
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto sub = c.SubmitOrder(MakeValidOrder(now));
    ASSERT_TRUE(sub.ok());
    ASSERT_TRUE(c.TestTransitionOrder(sub.value->order_id, polymarket::OrderStatus::Filled));
    auto r = c.CancelOrder(sub.value->order_id);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::InvariantViolation);
}

// ============================================================
// 7 状态机合法 / 非法转移完整覆盖
// ============================================================

TEST(PaperPmClient, StateMachine_LegalTransitions_All7) {
    using polymarket::OrderStatus;
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::Booked,          OrderStatus::PartiallyFilled));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::Booked,          OrderStatus::Filled));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::Booked,          OrderStatus::Canceled));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::Booked,          OrderStatus::Expired));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::Booked,          OrderStatus::Rejected));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::PartiallyFilled, OrderStatus::Filled));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::PartiallyFilled, OrderStatus::Canceled));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::PartiallyFilled, OrderStatus::Expired));
    EXPECT_TRUE(polymarket::IsLegalTransition(OrderStatus::Filled,          OrderStatus::Settled));

    // 非法: 终态 → 任何 (Settled 例外性自闭合)
    EXPECT_FALSE(polymarket::IsLegalTransition(OrderStatus::Canceled, OrderStatus::Filled));
    EXPECT_FALSE(polymarket::IsLegalTransition(OrderStatus::Expired,  OrderStatus::Booked));
    EXPECT_FALSE(polymarket::IsLegalTransition(OrderStatus::Rejected, OrderStatus::Booked));
    EXPECT_FALSE(polymarket::IsLegalTransition(OrderStatus::Settled,  OrderStatus::Booked));
    // Filled 只可 → Settled, 不可回 PartiallyFilled
    EXPECT_FALSE(polymarket::IsLegalTransition(OrderStatus::Filled,   OrderStatus::PartiallyFilled));
}

TEST(PaperPmClient, StateMachine_IsTerminal_All7) {
    EXPECT_FALSE(polymarket::IsTerminal(polymarket::OrderStatus::Booked));
    EXPECT_FALSE(polymarket::IsTerminal(polymarket::OrderStatus::PartiallyFilled));
    EXPECT_TRUE (polymarket::IsTerminal(polymarket::OrderStatus::Filled));
    EXPECT_TRUE (polymarket::IsTerminal(polymarket::OrderStatus::Canceled));
    EXPECT_TRUE (polymarket::IsTerminal(polymarket::OrderStatus::Expired));
    EXPECT_TRUE (polymarket::IsTerminal(polymarket::OrderStatus::Rejected));
    EXPECT_TRUE (polymarket::IsTerminal(polymarket::OrderStatus::Settled));
}

// ============================================================
// F-04 CancelAll
// ============================================================

TEST(PaperPmClient, F04_CancelAll_CountsNonTerminal) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto a = c.SubmitOrder(MakeValidOrder(now, "a"));
    auto b = c.SubmitOrder(MakeValidOrder(now, "b"));
    auto d = c.SubmitOrder(MakeValidOrder(now, "d"));
    ASSERT_TRUE(a.ok() && b.ok() && d.ok());
    // 把 d 推到 Filled 终态 (不应被 CancelAll 重复改)
    ASSERT_TRUE(c.TestTransitionOrder(d.value->order_id, polymarket::OrderStatus::Filled));

    auto r = c.CancelAll();
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value, 2u);  // a + b 被撤; d 已 Filled (终态) 不撤
}

// ============================================================
// F-05 GetMarketInfo
// ============================================================

TEST(PaperPmClient, F05_GetMarketInfo_HitCache) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    polymarket::MarketInfo m;
    m.condition_id           = kCidA;
    m.tick_size_bps          = 10;
    m.neg_risk               = false;
    m.fee_rate_bps           = 300;     // taker 3%
    m.accepting_orders       = true;
    m.liquidity_usdc         = 50000.0;
    m.ts                     = MakeValidTs(now);
    c.TestInjectMarketInfo(m);

    auto r = c.GetMarketInfo(kCidA);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->fee_rate_bps, 300u);
    EXPECT_DOUBLE_EQ(r.value->liquidity_usdc, 50000.0);
    EXPECT_TRUE(r.value->accepting_orders);
}

TEST(PaperPmClient, F05_GetMarketInfo_NotFound) {
    PaperPolymarketClient c{};
    auto r = c.GetMarketInfo(kCidB);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::NotFound);
}

// ============================================================
// F-06 GetUserPositions
// ============================================================

TEST(PaperPmClient, F06_GetUserPositions_InjectedLedger) {
    PaperPolymarketClient c{};
    polymarket::Position p;
    p.condition_id    = kCidA;
    p.token_id        = kTokA;
    p.size_micro      = 500'000'000;  // $500 long
    p.avg_price_bps   = 5400;
    p.cur_price_bps   = 5500;
    p.ts              = MakeValidTs(infra::wal::pit::NowRealtimeNs());
    c.TestInjectPosition(p);

    auto r = c.GetUserPositions("0xfunder");
    ASSERT_TRUE(r.ok());
    ASSERT_EQ(r.value->size(), 1u);
    EXPECT_EQ((*r.value)[0].size_micro, 500'000'000);
    EXPECT_EQ((*r.value)[0].avg_price_bps, 5400u);
}

// ============================================================
// F-07 GetBalance (paper infinite)
// ============================================================

TEST(PaperPmClient, F07_GetBalance_DefaultInfinite) {
    PaperPolymarketClient c{};
    auto r = c.GetBalance();
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->balance_usdc_micro, 1'000'000'000'000'000ULL);  // 1e15
    // R-20: paper mock 无 upstream payload, 显式标 INFERRED_FROM_INGESTION
    EXPECT_EQ(r.value->ts.ds_ts_source, polymarket::DataSourceTsSource::InferredFromIngestion);
}

// ============================================================
// F-08 GetOrderStatus + F-09 GetMyOpenOrders
// ============================================================

TEST(PaperPmClient, F08_GetOrderStatus_Booked) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto sub = c.SubmitOrder(MakeValidOrder(now));
    ASSERT_TRUE(sub.ok());
    auto r = c.GetOrderStatus(sub.value->order_id);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->status, polymarket::OrderStatus::Booked);
}

TEST(PaperPmClient, F09_GetMyOpenOrders_FilterTerminal) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto a = c.SubmitOrder(MakeValidOrder(now, "a"));
    auto b = c.SubmitOrder(MakeValidOrder(now, "b"));
    ASSERT_TRUE(a.ok() && b.ok());
    ASSERT_TRUE(c.TestTransitionOrder(b.value->order_id, polymarket::OrderStatus::Filled));

    auto r = c.GetMyOpenOrders();
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->size(), 1u);
    EXPECT_EQ((*r.value)[0].status, polymarket::OrderStatus::Booked);
}

// ============================================================
// F-10 GetMyTrades
// ============================================================

TEST(PaperPmClient, F10_GetMyTrades_LimitZeroReturnsAll) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    polymarket::Trade t;
    t.trade_id       = "t1";
    t.condition_id   = kCidA;
    t.price_bps      = 5500;
    t.size_usdc_micro = 100'000'000;
    t.ts             = MakeValidTs(now);
    c.TestInjectTrade(t);
    auto r = c.GetMyTrades(0);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->size(), 1u);
}

// ============================================================
// F-11 / F-12 paper not applicable (PMError BadRequest)
// ============================================================

TEST(PaperPmClient, F11_DeriveApiKey_NotApplicableInPaper) {
    PaperPolymarketClient c{};
    auto r = c.DeriveApiKey();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::BadRequest);
}

TEST(PaperPmClient, F12_ListApiKeys_NotApplicableInPaper) {
    PaperPolymarketClient c{};
    auto r = c.ListApiKeys();
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::BadRequest);
}

// ============================================================
// F-13 GetPricesHistory (paper v0.1 返空)
// ============================================================

TEST(PaperPmClient, F13_GetPricesHistory_EmptyIsOk) {
    PaperPolymarketClient c{};
    auto r = c.GetPricesHistory(kTokA, 1700000000, 1700001000);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value->empty());
}

// ============================================================
// F-14 SubscribeSportsWss callback wiring
// ============================================================

namespace {
struct WssCallbackState {
    std::uint32_t fired_count{0};
    std::string   last_cid;
};
void OnBook(const polymarket::OrderBookSnapshot& snap, void* ud) {
    auto* st = static_cast<WssCallbackState*>(ud);
    ++st->fired_count;
    st->last_cid = snap.condition_id;
}
}  // namespace

TEST(PaperPmClient, F14_SubscribeSportsWss_FiresForInjectedBook) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    c.TestInjectOrderbook(MakeOrderbook(kCidA, now));
    c.TestInjectOrderbook(MakeOrderbook(kCidB, now));
    WssCallbackState state;
    std::vector<std::string> cids = {std::string{kCidA}, std::string{kCidB}, "0xmissing"};
    auto r = c.SubscribeSportsWss(cids, &OnBook, &state);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(*r.value, 2u);
    EXPECT_EQ(state.fired_count, 2u);
}

TEST(PaperPmClient, F14_SubscribeSportsWss_NullCallback) {
    PaperPolymarketClient c{};
    auto r = c.SubscribeSportsWss({}, nullptr, nullptr);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error.kind, polymarket::PMErrorKind::BadRequest);
}

// ============================================================
// PMError 9 kind 全 ToString 覆盖 (枚举闭合)
// ============================================================

TEST(PaperPmClient, PMError_AllKindsHaveToString) {
    using K = polymarket::PMErrorKind;
    EXPECT_STREQ(polymarket::ToString(K::Ok).data(),                 "Ok");
    EXPECT_STREQ(polymarket::ToString(K::NotAuthenticated).data(),   "NotAuthenticated");
    EXPECT_STREQ(polymarket::ToString(K::RateLimited).data(),        "RateLimited");
    EXPECT_STREQ(polymarket::ToString(K::ServerError).data(),        "ServerError");
    EXPECT_STREQ(polymarket::ToString(K::BadRequest).data(),         "BadRequest");
    EXPECT_STREQ(polymarket::ToString(K::NotFound).data(),           "NotFound");
    EXPECT_STREQ(polymarket::ToString(K::NetworkError).data(),       "NetworkError");
    EXPECT_STREQ(polymarket::ToString(K::Stale).data(),              "Stale");
    EXPECT_STREQ(polymarket::ToString(K::InvariantViolation).data(), "InvariantViolation");
    EXPECT_STREQ(polymarket::ToString(K::Unknown).data(),            "Unknown");
}

// ============================================================
// HMAC bug 反模式: paper signature 字段 reserve 给 live (空字符串合法)
// ============================================================

TEST(PaperPmClient, HmacReserve_PaperAllowsEmptySignature) {
    PaperPolymarketClient c{};
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto o = MakeValidOrder(now);
    o.signature = "";  // paper 不签真; live W5+ 接入 byte-equal v3 §D 14 vector
    auto r = c.SubmitOrder(o);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value->status, polymarket::OrderStatus::Booked);
}

// ============================================================
// R-20 ingestion ts 单调 (多笔 SubmitOrder 之间不倒流)
// ============================================================

TEST(PaperPmClient, R20_IngestionTsMonotonic_AcrossSubmits) {
    PaperPolymarketClient c{};
    // a 用 (now - 100ms), b 用 (now - 50ms) — 都在 wall clock 现在之前, 保证 PIT 不被 NowRealtimeNs() 拒
    const std::int64_t now = infra::wal::pit::NowRealtimeNs();
    auto a = c.SubmitOrder(MakeValidOrder(now - 100'000'000, "a"));
    auto b = c.SubmitOrder(MakeValidOrder(now -  50'000'000, "b"));
    ASSERT_TRUE(a.ok() && b.ok());
    // ack 的 ingestion_ts 直接继承自 caller 注入 — paper 不修改
    EXPECT_LE(a.value->ts.ingestion_ts_ns, b.value->ts.ingestion_ts_ns);
}
