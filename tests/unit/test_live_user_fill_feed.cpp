// tests/unit/test_live_user_fill_feed.cpp
//
// LiveUserFillFeed — CLOB user 频道自有成交异步接收器 (Phase 2)。
// 验证: ① 只在 CONFIRMED 入账 (MATCHED/MINED/FAILED/非trade 跳过) ② 按 trade id 去重
//        ③ 字段解析正确 (BUY/SELL, YES/NO, price/size/fee/ts) ④ 健全性 (size>0, price∈(0,1))
//        ⑤ subscribe frame 含 auth + condition_ids ⑥ 动态追加订阅。
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "stcpp/polymarket/live/live_user_fill_feed.hpp"

using stcpp::polymarket::LiveUserFillFeed;
using stcpp::polymarket::UserFill;

namespace {

// Mock IWssTransport: 捕获回调 + 记录发出的帧 + 让测试主动触发收帧/连接事件。
class MockTransport final : public stcpp::polymarket::wss::IWssTransport {
public:
    bool AsyncConnect(std::string_view url) override {
        last_url_ = std::string(url);
        connected_ = true;
        if (on_connected_)
            on_connected_();
        return true;
    }
    bool AsyncSendText(std::string_view payload) override {
        sent_.emplace_back(payload);
        return true;
    }
    void Close() override { connected_ = false; }
    void SetOnTextFrame(OnTextFrame cb) override { on_text_ = std::move(cb); }
    void SetOnConnected(OnConnected cb) override { on_connected_ = std::move(cb); }
    void SetOnDisconnected(OnDisconnected cb) override { on_disc_ = std::move(cb); }
    [[nodiscard]] bool IsConnected() const noexcept override { return connected_; }

    // 测试驱动: 模拟收到一帧
    void FireFrame(std::string_view payload, std::int64_t ts = 1'000'000'000LL) {
        if (on_text_)
            on_text_(payload, ts);
    }

    std::string last_url_;
    std::vector<std::string> sent_;

private:
    OnTextFrame on_text_;
    OnConnected on_connected_;
    OnDisconnected on_disc_;
    bool connected_{false};
};

// 构造一笔 trade 帧 (官方 schema 字段)。
std::string TradeFrame(const std::string& id, const std::string& status, const std::string& side,
                       const std::string& outcome, const std::string& price, const std::string& size,
                       const std::string& asset = "tok-123", const std::string& market = "0xcond") {
    return std::string(R"({"event_type":"trade","type":"TRADE","id":")") + id + R"(","status":")" + status +
           R"(","market":")" + market + R"(","asset_id":")" + asset + R"(","outcome":")" + outcome +
           R"(","side":")" + side + R"(","price":")" + price + R"(","size":")" + size +
           R"(","fee":"0.03","taker_order_id":"ord-1","timestamp":"1700000000000"})";
}

}  // namespace

// ---- 纯解析 (静态) -----------------------------------------------------------

TEST(LiveUserFillFeed, ParseConfirmedTradeAllFields) {
    UserFill f;
    const auto frame = TradeFrame("uuid-1", "CONFIRMED", "BUY", "YES", "0.8", "6.4");
    ASSERT_TRUE(LiveUserFillFeed::ParseConfirmedTrade(frame, 9'000'000'000LL, f));
    EXPECT_EQ(f.trade_id, "uuid-1");
    EXPECT_EQ(f.condition_id, "0xcond");
    EXPECT_EQ(f.token_id, "tok-123");
    EXPECT_EQ(f.order_id, "ord-1");
    EXPECT_TRUE(f.is_buy);
    EXPECT_TRUE(f.is_yes);
    EXPECT_DOUBLE_EQ(f.price, 0.8);
    EXPECT_DOUBLE_EQ(f.size, 6.4);
    EXPECT_DOUBLE_EQ(f.fee, 0.03);
    EXPECT_EQ(f.data_source_ts_ns, 1'700'000'000'000LL * 1'000'000LL);  // ms × 1e6
}

TEST(LiveUserFillFeed, SellNoParsing) {
    UserFill f;
    ASSERT_TRUE(LiveUserFillFeed::ParseConfirmedTrade(
        TradeFrame("u2", "CONFIRMED", "SELL", "No", "0.45", "10"), 1, f));
    EXPECT_FALSE(f.is_buy);
    EXPECT_FALSE(f.is_yes);
    EXPECT_DOUBLE_EQ(f.size, 10.0);
}

TEST(LiveUserFillFeed, NonConfirmedStatusRejected) {
    UserFill f;
    for (const char* st : {"MATCHED", "MINED", "RETRYING", "FAILED"}) {
        EXPECT_FALSE(LiveUserFillFeed::ParseConfirmedTrade(
            TradeFrame("u", st, "BUY", "YES", "0.8", "5"), 1, f))
            << "status=" << st << " 不应入账 (只认 CONFIRMED)";
    }
}

TEST(LiveUserFillFeed, NonTradeEventRejected) {
    UserFill f;
    const std::string order_evt =
        R"({"event_type":"order","type":"order","id":"o1","status":"CONFIRMED","market":"0xc","asset_id":"t","side":"BUY","price":"0.8","size":"5","outcome":"YES"})";
    EXPECT_FALSE(LiveUserFillFeed::ParseConfirmedTrade(order_evt, 1, f)) << "order 事件不是成交, 不入账";
}

TEST(LiveUserFillFeed, SanityGuards) {
    UserFill f;
    // size=0 → 拒
    EXPECT_FALSE(LiveUserFillFeed::ParseConfirmedTrade(
        TradeFrame("u", "CONFIRMED", "BUY", "YES", "0.8", "0"), 1, f));
    // price 越界 (≥1) → 拒
    EXPECT_FALSE(LiveUserFillFeed::ParseConfirmedTrade(
        TradeFrame("u", "CONFIRMED", "BUY", "YES", "1.0", "5"), 1, f));
    // price=0 → 拒
    EXPECT_FALSE(LiveUserFillFeed::ParseConfirmedTrade(
        TradeFrame("u", "CONFIRMED", "BUY", "YES", "0", "5"), 1, f));
}

// S2: maker_orders[] 嵌套同名字段不得污染顶层抽取 (depth-aware)。maker 故意排在顶层字段【之前】
//   + 带不同的 price/outcome/asset_id/size → 旧的平铺 find 会抓 maker 的; depth-aware 必取顶层。
TEST(LiveUserFillFeed, MakerOrdersFieldsDoNotContaminate) {
    const std::string frame =
        R"({"event_type":"trade","type":"TRADE","id":"TX","status":"CONFIRMED",)"
        R"("maker_orders":[{"order_id":"mk1","asset_id":"tok-MK","price":"0.20","outcome":"NO","size":"99","fee":"9"}],)"
        R"("market":"0xTOP","asset_id":"tok-TOP","outcome":"YES","side":"BUY","price":"0.80",)"
        R"("size":"6.4","fee":"0.03","taker_order_id":"ord-TOP","timestamp":"1700000000000"})";
    UserFill f;
    ASSERT_TRUE(LiveUserFillFeed::ParseConfirmedTrade(frame, 1, f));
    EXPECT_EQ(f.token_id, "tok-TOP") << "不得抓 maker 的 asset_id";
    EXPECT_EQ(f.condition_id, "0xTOP");
    EXPECT_TRUE(f.is_yes) << "不得抓 maker 的 outcome=NO";
    EXPECT_TRUE(f.is_buy);
    EXPECT_DOUBLE_EQ(f.price, 0.80) << "不得抓 maker 的 price=0.20";
    EXPECT_DOUBLE_EQ(f.size, 6.4) << "不得抓 maker 的 size=99";
    EXPECT_DOUBLE_EQ(f.fee, 0.03) << "不得抓 maker 的 fee=9";
    EXPECT_EQ(f.order_id, "ord-TOP");
}

// ---- 全链路 (mock transport → 去重 → 队列) -----------------------------------

TEST(LiveUserFillFeed, EndToEndConfirmOnceAndDedup) {
    auto mock = std::make_unique<MockTransport>();
    MockTransport* m = mock.get();
    LiveUserFillFeed feed(std::move(mock), "KEY", "SEC", "PASS");
    ASSERT_TRUE(feed.Start("wss://ws-subscriptions-clob.polymarket.com/ws/user", {"0xcond"}));
    EXPECT_TRUE(feed.connected());

    UserFill out;
    // MATCHED → 不入账
    m->FireFrame(TradeFrame("T1", "MATCHED", "BUY", "YES", "0.8", "6.4"));
    EXPECT_FALSE(feed.Pop(out));
    // MINED → 不入账
    m->FireFrame(TradeFrame("T1", "MINED", "BUY", "YES", "0.8", "6.4"));
    EXPECT_FALSE(feed.Pop(out));
    // CONFIRMED → 入账一次
    m->FireFrame(TradeFrame("T1", "CONFIRMED", "BUY", "YES", "0.8", "6.4"));
    ASSERT_TRUE(feed.Pop(out));
    EXPECT_EQ(out.trade_id, "T1");
    EXPECT_DOUBLE_EQ(out.size, 6.4);
    EXPECT_FALSE(feed.Pop(out)) << "队列应已空";
    // CONFIRMED 再来 (重发) → 去重, 不再入账
    m->FireFrame(TradeFrame("T1", "CONFIRMED", "BUY", "YES", "0.8", "6.4"));
    EXPECT_FALSE(feed.Pop(out)) << "同一 trade id 去重";
    EXPECT_EQ(feed.booked_count(), 1u);
    EXPECT_EQ(feed.dup_count(), 1u);
}

TEST(LiveUserFillFeed, SubscribeFrameHasAuthAndMarkets) {
    auto mock = std::make_unique<MockTransport>();
    MockTransport* m = mock.get();
    LiveUserFillFeed feed(std::move(mock), "MYKEY", "MYSEC", "MYPASS");
    feed.Start("wss://ws-subscriptions-clob.polymarket.com/ws/user", {"0xAAA", "0xBBB"});
    // OnConnected 自动重发 subscribe (含 auth + 全部 condition)
    ASSERT_FALSE(m->sent_.empty());
    const std::string& sub = m->sent_.back();
    EXPECT_NE(sub.find(R"("type":"User")"), std::string::npos);
    EXPECT_NE(sub.find("MYKEY"), std::string::npos);
    EXPECT_NE(sub.find("MYSEC"), std::string::npos);
    EXPECT_NE(sub.find("MYPASS"), std::string::npos);
    EXPECT_NE(sub.find("0xAAA"), std::string::npos);
    EXPECT_NE(sub.find("0xBBB"), std::string::npos);
}

TEST(LiveUserFillFeed, DynamicSubscribeSendsFreshOnly) {
    auto mock = std::make_unique<MockTransport>();
    MockTransport* m = mock.get();
    LiveUserFillFeed feed(std::move(mock), "K", "S", "P");
    feed.Start("wss://ws-subscriptions-clob.polymarket.com/ws/user", {"0xAAA"});
    const std::size_t after_start = m->sent_.size();
    std::vector<std::string> add = {"0xAAA", "0xCCC"};  // AAA 已订, CCC 新
    feed.SubscribeMarkets(add);
    ASSERT_EQ(m->sent_.size(), after_start + 1) << "只发一帧 (只含新 condition)";
    const std::string& sub = m->sent_.back();
    EXPECT_NE(sub.find("0xCCC"), std::string::npos);
    EXPECT_EQ(sub.find("0xAAA"), std::string::npos) << "已订的 AAA 不重发";
}
