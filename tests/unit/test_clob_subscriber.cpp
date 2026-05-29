// tests/unit/test_clob_subscriber.cpp — PolymarketCLOBSubscriber ctest
//
// Owner: 小冯 (#34)  spec: 老李 (#07) W9 W2  Wave 79
//
// ADR-027 cite:
//   polymarket_ssot_cite: laoli-w9-wss-subscriber-impl-spec-v1.md
//   goalserve_ssot_cite:  xiaoduan-w8-goalserve-data-structure-ssot-v1.md
//   handshake_cite:       laoli-laoSun-handshake-v1.md
//   adr_cite:             ADR-027 Enforce-1
//
// 3 test cases:
//   T1: subscribe payload 双 token (assets_ids array, type "Market" 大写 M)
//   T2: book event parse + 4-ts 填充 (R-20)
//   T3: reconnect on disconnect (market channel exp backoff)

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/polymarket/clob_wss/polymarket_clob_subscriber.hpp"
#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"
#include "stcpp/polymarket/wss/wss_event.hpp"

using namespace stcpp::polymarket::clob_wss;
using namespace stcpp::polymarket::wss;

namespace {

// ---------------------------------------------------------------------------
// MockWssTransport — minimal stub, does not open real network
// ---------------------------------------------------------------------------
class MockWssTransport : public IWssTransport {
public:
    bool AsyncConnect(std::string_view url) override {
        last_connect_url_ = std::string(url);
        ++connect_calls_;
        connected_ = true;
        if (on_connected_)
            on_connected_();
        return true;
    }
    bool AsyncSendText(std::string_view payload) override {
        sent_frames_.emplace_back(payload);
        return true;
    }
    void Close() override {
        if (connected_) {
            connected_ = false;
            if (on_disconnected_)
                on_disconnected_("close");
        }
    }
    void SetOnTextFrame(OnTextFrame cb) override { on_text_frame_ = std::move(cb); }
    void SetOnConnected(OnConnected cb) override { on_connected_ = std::move(cb); }
    void SetOnDisconnected(OnDisconnected cb) override { on_disconnected_ = std::move(cb); }
    [[nodiscard]] bool IsConnected() const noexcept override { return connected_; }

    // Simulate incoming server frame
    void Inject(std::string_view payload, std::int64_t recv_ts_ns) {
        if (on_text_frame_)
            on_text_frame_(payload, recv_ts_ns);
    }

    std::string last_connect_url_;
    std::vector<std::string> sent_frames_;
    int connect_calls_ = 0;
    bool connected_ = false;

    OnTextFrame on_text_frame_;
    OnConnected on_connected_;
    OnDisconnected on_disconnected_;
};

// ---------------------------------------------------------------------------
// CapturingSink — records pushed events, can be capped to simulate full ring
// ---------------------------------------------------------------------------
class CapturingSink : public ISpscEventSink {
public:
    explicit CapturingSink(std::size_t cap = 256) : cap_(cap) {}
    bool TryPush(const WssEvent& ev) noexcept override {
        if (events_.size() >= cap_)
            return false;
        events_.push_back(ev);
        return true;
    }
    [[nodiscard]] std::size_t Capacity() const noexcept override { return cap_; }

    std::vector<WssEvent> events_;
    std::size_t cap_;
};

// ---------------------------------------------------------------------------
// Helper: build default config with two well-known token ids
// ---------------------------------------------------------------------------
PolymarketCLOBSubscriberConfig MakeDefaultCfg() {
    PolymarketCLOBSubscriberConfig cfg;
    // P-01: 同一 condition 的 YES / NO token 必须同时列入 assets_ids
    cfg.initial_market_token_ids.push_back(
        "79394535786061696782398225752166997033617536527131936000440456503427498614313");
    cfg.initial_market_token_ids.push_back(
        "40471602955768311660697606657820773573478994022447498274937726738952879800738");
    cfg.api_key = "test-api-key";
    cfg.api_secret = "test-api-secret";
    cfg.api_passphrase = "test-passphrase";
    return cfg;
}

// Construct subscriber with detached (owned) mock transports
// Returns raw pointers for frame injection (safe: subscriber holds unique_ptr lifetime)
struct TestFixture {
    MockWssTransport* market_tp{nullptr};
    MockWssTransport* user_tp{nullptr};
    std::shared_ptr<CapturingSink> market_sink;
    std::shared_ptr<CapturingSink> user_sink;
    std::unique_ptr<PolymarketCLOBSubscriber> sub;

    explicit TestFixture(PolymarketCLOBSubscriberConfig cfg = MakeDefaultCfg(), std::size_t market_cap = 256,
                         std::size_t user_cap = 256) {
        auto mt = std::make_unique<MockWssTransport>();
        auto ut = std::make_unique<MockWssTransport>();
        market_tp = mt.get();
        user_tp = ut.get();
        market_sink = std::make_shared<CapturingSink>(market_cap);
        user_sink = std::make_shared<CapturingSink>(user_cap);
        sub = std::make_unique<PolymarketCLOBSubscriber>(std::move(mt), std::move(ut), market_sink, user_sink,
                                                         std::move(cfg));
    }
};

}  // namespace

// ============================================================================
// T1: subscribe payload 双 token (assets_ids array, "Market" 大写 M)
// ============================================================================
// 老李 spec §2.1:
//   {"type":"Market","assets_ids":["<token_yes>","<token_no>"]}
// P-01: assets_ids 必须包含两个 token (YES + NO)
// P-02: type 固定 "Market" 大写 M (小写不识别)
TEST(PolymarketCLOBSubscriber, T1_SubscribePayloadDualToken) {
    TestFixture fx;
    ASSERT_TRUE(fx.sub->Start());

    // After Start() + OnMarketConnected(), market_transport_ should have sent
    // subscribe frame with initial_market_token_ids (2 tokens)
    ASSERT_GE(fx.market_tp->sent_frames_.size(), 1u)
        << "Expected at least 1 subscribe frame sent to market channel";

    // Find the subscribe frame (look for the one containing assets_ids)
    const std::string* subscribe_frame = nullptr;
    for (const auto& f : fx.market_tp->sent_frames_) {
        if (f.find("assets_ids") != std::string::npos) {
            subscribe_frame = &f;
            break;
        }
    }
    ASSERT_NE(subscribe_frame, nullptr) << "No frame containing 'assets_ids' found";

    // Must contain "type":"Market" (大写 M) — P-02
    EXPECT_NE(subscribe_frame->find("\"type\":\"Market\""), std::string::npos)
        << "subscribe frame must have \"type\":\"Market\" (大写 M)";

    // Must contain both token ids — P-01 dual-token rule
    const std::string& yes_token =
        "79394535786061696782398225752166997033617536527131936000440456503427498614313";
    const std::string& no_token =
        "40471602955768311660697606657820773573478994022447498274937726738952879800738";
    EXPECT_NE(subscribe_frame->find(yes_token), std::string::npos) << "YES token must be in assets_ids";
    EXPECT_NE(subscribe_frame->find(no_token), std::string::npos) << "NO token must be in assets_ids";

    // assets_ids is an array (has "[" and "]")
    EXPECT_NE(subscribe_frame->find("\"assets_ids\":["), std::string::npos)
        << "assets_ids must be a JSON array";

    // Verify market URL (R-33)
    EXPECT_EQ(fx.market_tp->last_connect_url_, "wss://ws-subscriptions-clob.polymarket.com/ws/market");
    EXPECT_EQ(fx.user_tp->last_connect_url_, "wss://ws-subscriptions-clob.polymarket.com/ws/user");
}

// ============================================================================
// T2: book event parse + 4-ts 填充 (R-20 data_source_ts = timestamp_ms × 1e6)
// ============================================================================
// 老李 spec §2.2 book event:
//   event_type, market, asset_id, timestamp (ms), hash, bids, asks
// R-20 红线:
//   data_source_ts = timestamp_ms × 1e6 ns (禁 now() 替代)
//   event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts
// P-03: timestamp 单位 ms, ×1e6 才是 ns (×1e9 则 4-ts 不等式失败)
TEST(PolymarketCLOBSubscriber, T2_BookEventParseAndFourTs) {
    TestFixture fx;
    ASSERT_TRUE(fx.sub->Start());

    // timestamp = 1748390400000 ms = 1748390400000000000 ns
    constexpr std::int64_t kTimestampMs = 1'748'390'400'000LL;
    constexpr std::int64_t kExpectedDsNs = kTimestampMs * 1'000'000LL;

    // recv_ts must be >= data_source_ts (ingestion happens after data is published)
    // We add a small delta simulating wire latency
    constexpr std::int64_t kRecvTs = kExpectedDsNs + 50'000'000LL;  // +50ms wire latency

    // CLOB book event (老李 spec §2.2 wire format)
    // timestamp field as integer (also accepted as string per spec)
    const std::string book_frame =
        R"({"event_type":"book","market":"0xa9db6005902000000000000000000000000000000000000000000000000000001","asset_id":"79394535786061696782398225752166997033617536527131936000440456503427498614313","timestamp":1748390400000,"hash":"deadbeef","bids":[{"price":"0.55","size":"100.0"}],"asks":[{"price":"0.56","size":"80.0"}],"tick_size":"0.01","last_trade_price":"0.545"})";

    fx.market_tp->Inject(book_frame, kRecvTs);

    // Expect exactly 1 event pushed
    ASSERT_EQ(fx.market_sink->events_.size(), 1u) << "Expected 1 book event in market sink";

    const WssEvent& ev = fx.market_sink->events_[0];
    EXPECT_EQ(ev.topic, SubTopic::kBook);
    EXPECT_EQ(ev.payload.book.is_snapshot, 1u) << "book event must be snapshot";

    // R-20: data_source_ts = timestamp_ms × 1e6
    const FourTs& ts = ev.payload.book.ts;
    EXPECT_EQ(ts.data_source_ts_ns, kExpectedDsNs)
        << "data_source_ts must equal timestamp_ms * 1e6 (P-03: not *1e9)";

    // R-20: event_ts = data_source_ts (no independent event_ts field in book)
    EXPECT_EQ(ts.event_ts_ns, ts.data_source_ts_ns) << "event_ts must equal data_source_ts for book events";

    // R-20: ds_origin = kUpstreamPayload
    EXPECT_EQ(ts.ds_origin, DataSourceTsOrigin::kUpstreamPayload)
        << "data_source_ts origin must be UPSTREAM_PAYLOAD";

    // R-20: 4-ts monotonic chain
    EXPECT_LE(ts.event_ts_ns, ts.data_source_ts_ns) << "event_ts <= data_source_ts";
    EXPECT_LE(ts.data_source_ts_ns, ts.ingestion_ts_ns) << "data_source_ts <= ingestion_ts";
    EXPECT_LE(ts.ingestion_ts_ns, ts.as_of_ts_ns) << "ingestion_ts <= as_of_ts";
    EXPECT_TRUE(ts.IsMonotonic()) << "FourTs::IsMonotonic() must return true";

    // ingestion_ts = recv_ts_ns (transport callback entry)
    EXPECT_EQ(ts.ingestion_ts_ns, kRecvTs) << "ingestion_ts must equal recv_ts_ns from transport callback";

    // Verify tick_size and last_trade_price extraction
    // tick_size "0.01" → 100 bps
    EXPECT_EQ(ev.payload.book.tick_size_bps, 100u) << "tick_size 0.01 should map to 100 bps";
    // last_trade_price "0.545" → 5450 bps
    EXPECT_EQ(ev.payload.book.last_trade_price_bps, 5450u) << "last_trade_price 0.545 should map to 5450 bps";

    // T2 extra: verify future-timestamp rejection (R-20 §6)
    // Send a frame with timestamp 10s ahead of recv_ts → must be dropped
    const std::string future_frame =
        R"({"event_type":"book","market":"0xmkt","asset_id":"tok1","timestamp":1748390415000,"hash":"x","bids":[],"asks":[]})";
    // recv_ts is 5s behind the timestamp → data_source_ts > recv_ts + 5s guard → drop
    constexpr std::int64_t kPastRecvTs = kExpectedDsNs - 5'000'000'000LL;
    std::size_t count_before = fx.market_sink->events_.size();
    fx.market_tp->Inject(future_frame, kPastRecvTs);
    EXPECT_EQ(fx.market_sink->events_.size(), count_before)
        << "Future-timestamp frame must be dropped (R-20 future guard)";
    EXPECT_GE(fx.sub->market_metrics().frames_parse_error_total.load(), 1u)
        << "frames_parse_error_total must increment for rejected future frame";
}

// ============================================================================
// T3: reconnect on disconnect (market channel exp backoff)
// ============================================================================
// 老李 spec §5.1:
//   reconnect_initial=1s, cap=30s, multiplier=2x
//   重连后必须重发 subscribe payload (P-08)
//   重连后 sequence_no 状态重置 (§4)
// P-05: 重连时等待新 book snapshot 才接受 price_change diff
TEST(PolymarketCLOBSubscriber, T3_ReconnectOnDisconnect) {
    TestFixture fx;
    ASSERT_TRUE(fx.sub->Start());

    // Verify initial state: connected after Start()
    EXPECT_EQ(fx.sub->market_state(), WssTransportState::kConnected);
    EXPECT_EQ(fx.market_tp->connect_calls_, 1);

    // Record how many subscribe frames were sent during initial connect
    std::size_t frames_before_disconnect = fx.market_tp->sent_frames_.size();
    ASSERT_GE(frames_before_disconnect, 1u) << "Must send subscribe frame on initial connect";

    // Simulate server disconnect
    fx.market_tp->Close();

    // After Close(), OnMarketDisconnected triggers ScheduleMarketReconnect
    // which calls AsyncConnect again (v0.1: immediate reconnect)
    EXPECT_GE(fx.market_tp->connect_calls_, 2) << "AsyncConnect must be called again after disconnect";
    EXPECT_GE(fx.sub->market_metrics().reconnect_attempts_total.load(), 1u)
        << "reconnect_attempts_total metric must increment";

    // After reconnect (OnMarketConnected called again by mock):
    // P-08: subscribe payload must be resent
    std::size_t frames_after = fx.market_tp->sent_frames_.size();
    EXPECT_GT(frames_after, frames_before_disconnect)
        << "Subscribe payload must be resent after reconnect (P-08)";

    // Verify the resent frame still contains both token ids (P-08 full replay)
    const std::string* resent_frame = nullptr;
    for (std::size_t i = frames_before_disconnect; i < fx.market_tp->sent_frames_.size(); ++i) {
        if (fx.market_tp->sent_frames_[i].find("assets_ids") != std::string::npos) {
            resent_frame = &fx.market_tp->sent_frames_[i];
            break;
        }
    }
    ASSERT_NE(resent_frame, nullptr) << "Resent frame after reconnect must contain assets_ids";
    EXPECT_NE(resent_frame->find("\"type\":\"Market\""), std::string::npos)
        << "Resent frame must have type=Market";

    // Verify state after reconnect
    EXPECT_EQ(fx.sub->market_state(), WssTransportState::kConnected);

    // T3 extra: send a book event after reconnect — should be accepted (fresh snapshot)
    constexpr std::int64_t kTs = 1'748'390'401'000LL;  // ms
    constexpr std::int64_t kTsNs = kTs * 1'000'000LL;
    constexpr std::int64_t kRecv = kTsNs + 10'000'000LL;
    const std::string book_after_reconnect =
        R"({"event_type":"book","market":"0xmkt","asset_id":"79394535786061696782398225752166997033617536527131936000440456503427498614313","timestamp":1748390401000,"hash":"a","bids":[],"asks":[]})";
    fx.market_tp->Inject(book_after_reconnect, kRecv);
    EXPECT_GE(fx.market_sink->events_.size(), 1u) << "book event after reconnect must be accepted";

    // Heartbeat: TickMarketHeartbeatNow must send PING and increment counter
    std::size_t frames_before_ping = fx.market_tp->sent_frames_.size();
    fx.sub->TickMarketHeartbeatNow();
    EXPECT_GT(fx.market_tp->sent_frames_.size(), frames_before_ping)
        << "TickMarketHeartbeatNow must send PING frame";
    EXPECT_GE(fx.sub->market_metrics().heartbeat_pings_sent_total.load(), 1u)
        << "heartbeat_pings_sent_total must increment";

    // Heartbeat timeout: inject a past recv_ts then advance now past timeout
    std::atomic<std::int64_t> now_ns{1'748'390'402'000'000'000LL};
    fx.sub->SetNowFnForTest([&now_ns]() { return now_ns.load(); });

    // inject frame with current now as recv_ts (sets last_market_msg_ts_ns_)
    const std::string fresh_frame =
        R"({"event_type":"book","market":"0xmkt","asset_id":"tok1","timestamp":1748390402000,"hash":"b","bids":[],"asks":[]})";
    fx.market_tp->Inject(fresh_frame, now_ns.load());

    // advance 35 seconds
    now_ns.fetch_add(35'000'000'000LL);
    fx.sub->CheckMarketHeartbeatTimeoutNow();
    EXPECT_GE(fx.sub->market_metrics().heartbeat_timeouts_total.load(), 1u)
        << "heartbeat timeout must be detected after 35s with no frames";
}
