// tests/unit/test_pm_wss_subscriber.cpp — PM WSS subscriber v0.1 单测
//
// Owner: 小冯 (#34)  spec: 小余 W5-D-02 + 老周 W5-A-02. W5 Wave 24.
// 覆盖:
//   T1   8 sub topic × 各 1 case (订阅 + recv text frame + parse 4 ts)
//   T2   reconnect (transport 断 → AsyncConnect 再调 + reconnect_attempts counter)
//   T3   heartbeat 超时 (now - last_msg_ts > heartbeat_timeout → Close → reconnect)
//   T4   back-pressure (SPSC sink 满 → drop + metrics_.frames_dropped_total)
//   T5   R-20 4 ts 不等式 enforce (timestamp 未来 → reject)
//   T6   subscribe frame 构造 (公开免鉴权, action subscribe + topic + id)
//   T7   8 topic counter 各 +1
//   T8   parse error counter (unknown topic / PONG 不入队)
//   R-33 校验: 默认 URL 第 5 host

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"

using namespace stcpp::polymarket::wss;

namespace {

// MockWssTransport — 单测用 transport (不打真网络)
class MockWssTransport : public IWssTransport {
public:
    bool AsyncConnect(std::string_view url) override {
        last_connect_url_ = std::string(url);
        ++connect_calls_;
        connected_ = true;
        if (on_connected_) on_connected_();
        return true;
    }
    bool AsyncSendText(std::string_view payload) override {
        sent_frames_.emplace_back(payload);
        return true;
    }
    void Close() override {
        if (connected_) {
            connected_ = false;
            if (on_disconnected_) on_disconnected_("close called");
        }
    }
    void SetOnTextFrame(OnTextFrame cb)       override { on_text_frame_  = std::move(cb); }
    void SetOnConnected(OnConnected cb)       override { on_connected_   = std::move(cb); }
    void SetOnDisconnected(OnDisconnected cb) override { on_disconnected_= std::move(cb); }
    [[nodiscard]] bool IsConnected() const noexcept override { return connected_; }

    void Inject(std::string_view p, std::int64_t recv_ts_ns) {
        if (on_text_frame_) on_text_frame_(p, recv_ts_ns);
    }

    std::string              last_connect_url_;
    std::vector<std::string> sent_frames_;
    int                      connect_calls_ = 0;
    bool                     connected_     = false;
    OnTextFrame              on_text_frame_;
    OnConnected              on_connected_;
    OnDisconnected           on_disconnected_;
};

// CapturingSink — SPSC sink mock (可配 cap 模拟满)
class CapturingSink : public ISpscEventSink {
public:
    explicit CapturingSink(std::size_t cap) : cap_(cap) {}
    bool TryPush(const WssEvent& ev) noexcept override {
        if (events_.size() >= cap_) return false;
        events_.push_back(ev);
        return true;
    }
    std::size_t Capacity() const noexcept override { return cap_; }

    std::vector<WssEvent> events_;
    std::size_t           cap_;
};

PMWssSubscriberConfig MakeCfg() {
    PMWssSubscriberConfig c;
    c.initial_condition_ids.push_back("0xabcdef0123456789");
    return c;
}

}  // namespace

// ============================================================================
// T1 — 8 sub topic × 各 1 case
// ============================================================================
TEST(PMWssSubscriber, ParseAllEightSubTopics) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(64);
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    ASSERT_TRUE(sub.Start());

    // 1. market
    tp->Inject(R"({"topic":"market","condition_id":"0x123","timestamp":1700000000000,"mid":0.55,"spread":0.02,"last_trade_price":0.54,"tick_size":0.01,"neg_risk":0,"yes_bids":[],"yes_asks":[]})",
               1'700'000'000'050'000'000LL);
    // 2. game
    tp->Inject(R"({"topic":"game","event_id":"0xdead","timestamp":1700000001000,"last_update":1700000000500,"sport":4,"period":2,"time_remaining_s":420,"score_home":14,"score_away":7,"game_state":"live"})",
               1'700'000'001'100'000'000LL);
    // 3. outcomes
    tp->Inject(R"({"topic":"outcomes","condition_id":"0xabc","timestamp":1700000002000,"resolution_status":"resolved"})",
               1'700'000'002'100'000'000LL);
    // 4. book
    tp->Inject(R"({"topic":"book","asset_id":"0xtok","market":"0xmkt","timestamp":1700000003000,"hash":"abc","bids":[],"asks":[]})",
               1'700'000'003'100'000'000LL);
    // 5. price_change
    tp->Inject(R"({"topic":"price_change","asset_id":"0xt","market":"0xm","timestamp":1700000004000,"changes":[{"price":0.6,"side":"BUY","size":100}]})",
               1'700'000'004'100'000'000LL);
    // 6. last_trade_price
    tp->Inject(R"({"topic":"last_trade_price","asset_id":"0xtok","market":"0xmkt","timestamp":1700000005000,"price":0.63,"side":"BUY","size":250})",
               1'700'000'005'100'000'000LL);
    // 7. tick_size_change
    tp->Inject(R"({"topic":"tick_size_change","asset_id":"0xt","market":"0xm","timestamp":1700000006000,"old_tick":0.01,"new_tick":0.001})",
               1'700'000'006'100'000'000LL);
    // 8. system
    tp->Inject(R"({"topic":"system","status":"degraded","timestamp":1700000007000,"message":"slow"})",
               1'700'000'007'100'000'000LL);

    ASSERT_EQ(sink->events_.size(), 8u);

    // 字段抽取校验 (典型样本)
    EXPECT_EQ(sink->events_[0].topic, SubTopic::kMarket);
    EXPECT_EQ(sink->events_[0].payload.book.mid_bps, 5500u);
    EXPECT_EQ(sink->events_[0].payload.book.spread_bps, 200u);
    EXPECT_EQ(sink->events_[0].payload.book.tick_size_bps, 100u);
    EXPECT_EQ(sink->events_[1].topic, SubTopic::kGame);
    EXPECT_EQ(sink->events_[1].payload.game.game_state, GameState::kLive);
    EXPECT_EQ(sink->events_[1].payload.game.period, 2u);
    EXPECT_EQ(sink->events_[1].payload.game.score_home, 14);
    EXPECT_EQ(sink->events_[1].payload.game.ts.ds_origin, DataSourceTsOrigin::kUpstreamPayload);
    EXPECT_EQ(sink->events_[2].topic, SubTopic::kOutcomes);
    EXPECT_EQ(sink->events_[2].payload.outcomes.resolution_status, ResolutionStatus::kResolved);
    EXPECT_EQ(sink->events_[3].topic, SubTopic::kBook);
    EXPECT_EQ(sink->events_[3].payload.book.is_snapshot, 1u);
    EXPECT_EQ(sink->events_[4].topic, SubTopic::kPriceChange);
    EXPECT_EQ(sink->events_[4].payload.book.is_snapshot, 0u);
    EXPECT_EQ(sink->events_[5].topic, SubTopic::kLastTradePrice);
    EXPECT_EQ(sink->events_[5].payload.trade.price_bps, 6300u);
    EXPECT_EQ(sink->events_[6].topic, SubTopic::kTickSizeChange);
    EXPECT_EQ(sink->events_[6].payload.tick.old_tick_bps, 100u);
    EXPECT_EQ(sink->events_[6].payload.tick.new_tick_bps, 10u);
    EXPECT_EQ(sink->events_[7].topic, SubTopic::kSystemStatus);
    EXPECT_EQ(sink->events_[7].payload.system.health, SystemHealth::kDegraded);

    // 4 ts monotonic 全部成立
    for (const auto& e : sink->events_) EXPECT_TRUE(e.ts().IsMonotonic());
}

// ============================================================================
// T2 — reconnect (transport disconnect → AsyncConnect 再调)
// ============================================================================
TEST(PMWssSubscriber, ReconnectOnDisconnect) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(8);
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    sub.Start();
    EXPECT_EQ(tp->connect_calls_, 1);
    EXPECT_EQ(sub.state(), WssTransportState::kConnected);
    tp->Close();   // server 断
    EXPECT_GE(tp->connect_calls_, 2);
    EXPECT_GE(sub.metrics().reconnect_attempts_total.load(), 1u);
}

// ============================================================================
// T3 — heartbeat 超时 + ping send
// ============================================================================
TEST(PMWssSubscriber, HeartbeatTimeoutTriggersReconnect) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(8);
    auto cfg = MakeCfg();
    cfg.heartbeat_timeout = std::chrono::milliseconds(30000);
    PMWssSubscriber sub(std::move(t), sink, cfg);

    std::atomic<std::int64_t> now_ns{1'700'000'000'000'000'000LL};
    sub.SetNowFnForTest([&now_ns]() { return now_ns.load(); });
    sub.Start();
    const int connect_before = tp->connect_calls_;

    tp->Inject(R"({"topic":"market","condition_id":"0xa","timestamp":1700000000000,"mid":0.5})",
               now_ns.load());

    now_ns.fetch_add(35'000'000'000LL);   // +35s
    sub.CheckHeartbeatTimeoutNow();

    EXPECT_GE(sub.metrics().heartbeat_timeouts_total.load(), 1u);
    EXPECT_GT(tp->connect_calls_, connect_before);
}

TEST(PMWssSubscriber, HeartbeatPingSent) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(8);
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    sub.Start();
    const std::size_t before = tp->sent_frames_.size();
    sub.TickHeartbeatNow();
    ASSERT_GT(tp->sent_frames_.size(), before);
    EXPECT_EQ(tp->sent_frames_.back(), "PING");
    EXPECT_EQ(sub.metrics().heartbeat_pings_sent_total.load(), 1u);
}

// ============================================================================
// T4 — back-pressure
// ============================================================================
TEST(PMWssSubscriber, BackPressureDropsFrameWhenSinkFull) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(2);   // 故意小
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    sub.Start();
    const auto frame = R"({"topic":"market","condition_id":"0xa","timestamp":1700000000000,"mid":0.5})";
    tp->Inject(frame, 1'700'000'000'100'000'000LL);
    tp->Inject(frame, 1'700'000'000'200'000'000LL);
    tp->Inject(frame, 1'700'000'000'300'000'000LL);   // 第 3 帧 drop
    EXPECT_EQ(sink->events_.size(), 2u);
    EXPECT_EQ(sub.metrics().frames_dropped_total.load(), 1u);
    EXPECT_EQ(sub.metrics().frames_received_total.load(), 3u);
}

// ============================================================================
// T5 — R-20 4 ts 不等式 enforce
// ============================================================================
TEST(PMWssSubscriber, FourTsMonotonicEnforced) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(8);
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    sub.Start();

    // 正常: timestamp 过去, recv 当下 → monotonic 成立
    tp->Inject(R"({"topic":"market","condition_id":"0xa","timestamp":1700000000000,"mid":0.5})",
               1'700'000'000'100'000'000LL);
    ASSERT_EQ(sink->events_.size(), 1u);
    const auto& ts1 = sink->events_[0].ts();
    EXPECT_LE(ts1.event_ts_ns, ts1.data_source_ts_ns);
    EXPECT_LE(ts1.data_source_ts_ns, ts1.ingestion_ts_ns);
    EXPECT_LE(ts1.ingestion_ts_ns, ts1.as_of_ts_ns);
    EXPECT_TRUE(ts1.IsMonotonic());

    // 异常: timestamp 未来 → IsMonotonic = false → reject + parse_error counter
    sink->events_.clear();
    tp->Inject(R"({"topic":"market","condition_id":"0xa","timestamp":1800000000000,"mid":0.5})",
               1'700'000'000'100'000'000LL);
    EXPECT_EQ(sink->events_.size(), 0u);
    EXPECT_GE(sub.metrics().frames_parse_error_total.load(), 1u);
}

// ============================================================================
// T6 — subscribe frame 构造 (公开免鉴权)
// ============================================================================
TEST(PMWssSubscriber, SubscribeFrameFormat) {
    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(8);
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    sub.Start();
    // Start() 内部自动订阅 initial_condition_ids → market + outcomes 各 1 frame
    ASSERT_GE(tp->sent_frames_.size(), 2u);
    EXPECT_NE(tp->sent_frames_[0].find("\"action\":\"subscribe\""), std::string::npos);
    EXPECT_NE(tp->sent_frames_[0].find("\"topic\":\"market\""), std::string::npos);
    EXPECT_NE(tp->sent_frames_[1].find("\"topic\":\"outcomes\""), std::string::npos);

    sub.Subscribe(SubTopic::kGame, "0xdead");
    EXPECT_NE(tp->sent_frames_.back().find("\"topic\":\"game\""), std::string::npos);
    EXPECT_NE(tp->sent_frames_.back().find("0xdead"), std::string::npos);
}

// ============================================================================
// T7 — by_topic counter / T8 — parse error / PONG / R-33 默认 URL
// ============================================================================
TEST(PMWssSubscriber, CountersAndPongAndDefaultUrl) {
    PMWssSubscriberConfig c0;
    EXPECT_EQ(c0.url, "wss://sports-api.polymarket.com/ws");   // R-33

    auto t = std::make_unique<MockWssTransport>();
    auto* tp = t.get();
    auto sink = std::make_shared<CapturingSink>(32);
    PMWssSubscriber sub(std::move(t), sink, MakeCfg());
    sub.Start();
    tp->Inject(R"({"topic":"market","condition_id":"0xa","timestamp":1700000000000,"mid":0.5})",
               1'700'000'000'100'000'000LL);
    tp->Inject(R"({"topic":"game","event_id":"0xb","timestamp":1700000000000,"game_state":"live"})",
               1'700'000'000'200'000'000LL);
    EXPECT_EQ(sub.metrics().by_topic_frames[static_cast<std::size_t>(SubTopic::kMarket)].load(), 1u);
    EXPECT_EQ(sub.metrics().by_topic_frames[static_cast<std::size_t>(SubTopic::kGame)].load(), 1u);

    tp->Inject(R"({"topic":"unknown_garbage","x":1})", 1'700'000'000'300'000'000LL);
    EXPECT_EQ(sub.metrics().frames_parse_error_total.load(), 1u);

    tp->Inject("PONG", 1'700'000'000'400'000'000LL);            // PONG 不入队, 不算 parse error
    EXPECT_EQ(sub.metrics().frames_parse_error_total.load(), 1u);
}
