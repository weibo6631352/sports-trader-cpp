// tests/integration/debug_api/test_p1_market_metrics_wss.cpp
// Owner: 小卢 (senior-ic-pool)
// last_review: 2026-05-30
//
// dogfood P1 整改验证测试 (feat/xiaolu-market-metrics-fix)
//   P1-1: market() 查真实 catalog → found=true, 真实元信息
//   P1-2: metrics() uptime/rm_reject/fill_total/staleness/wss_connected 非 0
//   P1-3: book/book_pair wss_state 与 /status wss_connected 一致 (动态读取)
//
// 方法:
//   - MockWssTransport: 实现 IWssTransport 接口, IsConnected() 由 atomic bool 控制
//   - RealStateProvider 直接构造 (不起 HttpServer), 验 market/metrics/book 方法
//   - HttpServer 集成路径: GET /api/v1/market/{cid} + /metrics + /api/v1/book/{cid}
//
// 约束: 纯 in-process 测试, 不依赖外部网络, 端口 18095

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/data/score_snapshot_store.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"

#include "src/stcpp/debug_api/real_state_provider.hpp"
#include "src/stcpp/debug_api/server.hpp"
#include "src/stcpp/debug_api/state_provider.hpp"
#include <httplib.h>

// 各 HTTP 集成测试用独立端口 (gtest_discover_tests 下 ctest -jN 并行执行)
static constexpr std::uint16_t k_p1_port_market = 18095;
static constexpr std::uint16_t k_p1_port_metrics = 18096;
static constexpr std::uint16_t k_p1_port_wss = 18097;

// ============================================================
// MockWssTransport — 实现 IWssTransport, IsConnected() 可控
// ============================================================
class MockWssTransport final : public stcpp::polymarket::wss::IWssTransport {
public:
    explicit MockWssTransport(bool initially_connected = false) {
        connected_.store(initially_connected, std::memory_order_relaxed);
    }

    void set_connected(bool v) { connected_.store(v, std::memory_order_release); }

    bool AsyncConnect(std::string_view) override { return false; }
    bool AsyncSendText(std::string_view) override { return false; }
    void Close() override {}
    void SetOnTextFrame(OnTextFrame) override {}
    void SetOnConnected(OnConnected) override {}
    void SetOnDisconnected(OnDisconnected) override {}
    [[nodiscard]] bool IsConnected() const noexcept override {
        return connected_.load(std::memory_order_acquire);
    }

private:
    std::atomic<bool> connected_{false};
};

// ============================================================
// Fixture: 构造 RealStateProvider + 注入 catalog + hooks
// ============================================================
class P1MarketMetricsFixture : public ::testing::Test {
protected:
    static constexpr const char* k_cid = "0xdeadbeefdeadbeefdeadbeef0000001100000000000000000000000000000001";
    static constexpr const char* k_tok0 =
        "0xaaaa1111000000000000000000000000000000000000000000000000000000aa";
    static constexpr const char* k_tok1 =
        "0xbbbb2222000000000000000000000000000000000000000000000000000000bb";

    void SetUp() override {
        // 构造 hub + snap + store + ledger + quote
        hub_ = std::make_unique<stcpp::polymarket::clob_wss::OrderBookSnapshotHub>();
        snap_ = std::make_unique<stcpp::risk::RmDebugSnapshot>();
        score_store_ = std::make_unique<stcpp::data::ScoreSnapshotStore>();
        ledger_hub_ = std::make_unique<stcpp::risk::LedgerSnapshotHub>();
        quote_hub_ = std::make_unique<stcpp::sizing::QuoteSnapshotHub>();
        mock_wss_ = std::make_unique<MockWssTransport>(/*initially_connected=*/true);
        fill_counter_.store(0, std::memory_order_relaxed);

        // token_map
        stcpp::debug_api::MarketTokenMap token_map;
        token_map[k_cid] = {k_tok0, k_tok1};

        provider_ = std::make_unique<stcpp::debug_api::RealStateProvider>(
            *hub_, snap_.get(), score_store_.get(), std::move(token_map), stcpp::risk::RiskConfig{},
            stcpp::debug_api::ExecMode::Paper, ledger_hub_.get(), quote_hub_.get());

        // P1-1: 注入真实 MarketInfo catalog
        stcpp::debug_api::MarketInfoMap catalog;
        stcpp::debug_api::MarketInfo mi;
        mi.found = true;
        mi.condition_id = k_cid;
        mi.market_id = k_cid;
        mi.tick_size = 0.01;
        mi.fee_rate = 0.0;
        mi.accepting_orders = true;
        mi.active = true;
        mi.closed = false;
        mi.resolved = false;
        mi.source = "polymarket";
        mi.event_id = "evt-001";
        mi.slug = "test-nba-2026";
        mi.polymarket_url = "https://polymarket.com/event/test-nba-2026";
        mi.sports_market_type = "outright";
        mi.group_item_title = "OKC";
        stcpp::debug_api::TokenInfo tk0, tk1;
        tk0.token_id = k_tok0;
        tk0.outcome = "Yes";
        tk0.price = 0.0;
        tk1.token_id = k_tok1;
        tk1.outcome = "No";
        tk1.price = 0.0;
        mi.tokens.push_back(std::move(tk0));
        mi.tokens.push_back(std::move(tk1));
        catalog[k_cid] = std::move(mi);
        provider_->set_market_catalog(std::move(catalog));

        // P1-2/P1-3: 注入 LiveMetricsHooks
        stcpp::debug_api::LiveMetricsHooks hooks;
        hooks.start_tp = std::chrono::steady_clock::now() - std::chrono::seconds(42);  // 模拟 42s 已运行
        hooks.wss_transport = mock_wss_.get();
        hooks.fill_counter = &fill_counter_;
        provider_->set_live_metrics_hooks(hooks);

        // 注入 events (不影响 P1 测试, 但让 provider 完整)
        provider_->set_events({});
    }

    std::unique_ptr<stcpp::polymarket::clob_wss::OrderBookSnapshotHub> hub_;
    std::unique_ptr<stcpp::risk::RmDebugSnapshot> snap_;
    std::unique_ptr<stcpp::data::ScoreSnapshotStore> score_store_;
    std::unique_ptr<stcpp::risk::LedgerSnapshotHub> ledger_hub_;
    std::unique_ptr<stcpp::sizing::QuoteSnapshotHub> quote_hub_;
    std::unique_ptr<MockWssTransport> mock_wss_;
    std::atomic<std::uint64_t> fill_counter_{0};
    std::unique_ptr<stcpp::debug_api::RealStateProvider> provider_;
};

// ============================================================
// P1-1: market() 查 catalog → found=true + 真实字段
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_1_MarketCatalog_FoundTrue) {
    const auto mi = provider_->market(k_cid);
    EXPECT_TRUE(mi.found) << "P1-1: market() 应返回 found=true (catalog 已注入)";
    EXPECT_EQ(mi.condition_id, k_cid);
    EXPECT_EQ(mi.sports_market_type, "outright");
    EXPECT_EQ(mi.slug, "test-nba-2026");
    EXPECT_EQ(mi.polymarket_url, "https://polymarket.com/event/test-nba-2026");
    EXPECT_TRUE(mi.accepting_orders);
    EXPECT_EQ(mi.tokens.size(), 2u) << "tokens[] 应有 2 条 (YES/NO)";
    EXPECT_EQ(mi.tokens[0].outcome, "Yes");
    EXPECT_EQ(mi.tokens[1].outcome, "No");
}

TEST_F(P1MarketMetricsFixture, P1_1_MarketCatalog_UnknownCidFoundFalse) {
    const auto mi = provider_->market("0xunknown_cid_not_in_catalog");
    EXPECT_FALSE(mi.found) << "P1-1: 未知 cid 应返回 found=false";
}

// ============================================================
// P1-2: metrics() uptime_sec 非零
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_2_Metrics_UptimeNonZero) {
    const auto m = provider_->metrics();
    EXPECT_GT(m.uptime_sec, 0LL) << "P1-2: uptime_sec 应 > 0 (hooks.start_tp 设为 42s 前)";
    // 允许误差: 应在 [40, 120] 秒内
    EXPECT_GE(m.uptime_sec, 40LL);
    EXPECT_LT(m.uptime_sec, 120LL);
}

// ============================================================
// P1-2: metrics() rm_reject_total 接 snap count
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_2_Metrics_RmRejectTotal) {
    // 初始: 0
    {
        const auto m = provider_->metrics();
        EXPECT_EQ(m.rm_reject_total, 0LL);
    }

    // push 3 条 reject
    stcpp::risk::RejectRow row{};
    std::strncpy(row.reason_code, "INVALID_INTENT", sizeof(row.reason_code) - 1);
    std::strncpy(row.market_id, k_cid, sizeof(row.market_id) - 1);
    std::strncpy(row.side, "BUY", sizeof(row.side) - 1);
    row.size_usdc = 5.0;
    row.price = 0.5;
    row.rejected_ts_ns = 1000000000LL;
    snap_->push_reject(row);
    snap_->push_reject(row);
    snap_->push_reject(row);

    {
        const auto m = provider_->metrics();
        EXPECT_EQ(m.rm_reject_total, 3LL) << "P1-2: rm_reject_total 应 = snap count (3 pushes)";
    }
}

// ============================================================
// P1-2: metrics() fill_total 接 fill_counter
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_2_Metrics_FillTotal) {
    fill_counter_.store(7, std::memory_order_relaxed);
    const auto m = provider_->metrics();
    EXPECT_EQ(m.fill_total, 7LL) << "P1-2: fill_total 应 = fill_counter (7)";
}

// ============================================================
// P1-2: metrics() wss_clob_connected 反映 MockWssTransport 状态
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_2_Metrics_WssConnectedTrue) {
    mock_wss_->set_connected(true);
    const auto m = provider_->metrics();
    EXPECT_TRUE(m.wss_clob_connected) << "P1-2: wss_clob_connected 应跟随 transport.IsConnected()=true";
}

TEST_F(P1MarketMetricsFixture, P1_2_Metrics_WssConnectedFalse) {
    mock_wss_->set_connected(false);
    const auto m = provider_->metrics();
    EXPECT_FALSE(m.wss_clob_connected) << "P1-2: wss_clob_connected 应跟随 transport.IsConnected()=false";
}

// ============================================================
// P1-3: book() wss_state 动态反映当前连接状态
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_3_BookWssState_ConnectedMatchesTransport) {
    // WSS connected=true → wss_state="CONNECTED"
    mock_wss_->set_connected(true);
    const auto b = provider_->book(k_tok0);
    EXPECT_EQ(b.wss_state, "CONNECTED") << "P1-3: wss_state 应 = CONNECTED 当 transport connected";

    // WSS connected=false → wss_state="DISCONNECTED"
    mock_wss_->set_connected(false);
    const auto b2 = provider_->book(k_tok0);
    EXPECT_EQ(b2.wss_state, "DISCONNECTED") << "P1-3: wss_state 应 = DISCONNECTED 当 transport disconnected";
}

// ============================================================
// P1-3: book_pair() token0/token1 wss_state 一致
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_3_BookPairWssState_BothTokensConsistent) {
    mock_wss_->set_connected(true);
    const auto bv = provider_->book_pair(k_cid);
    // book_pair 返回 found=false (hub 无真实数据), 但 wss_state 应填充
    EXPECT_EQ(bv.token0.wss_state, "CONNECTED");
    EXPECT_EQ(bv.token1.wss_state, "CONNECTED");

    mock_wss_->set_connected(false);
    const auto bv2 = provider_->book_pair(k_cid);
    EXPECT_EQ(bv2.token0.wss_state, "DISCONNECTED");
    EXPECT_EQ(bv2.token1.wss_state, "DISCONNECTED");
}

// ============================================================
// P1-3 + P1-2 一致性: metrics().wss_clob_connected 与 book().wss_state 一致
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_3_WssStateConsistency_MetricsAndBook) {
    // Connected scenario
    mock_wss_->set_connected(true);
    {
        const auto m = provider_->metrics();
        const auto b = provider_->book(k_tok0);
        EXPECT_TRUE(m.wss_clob_connected);
        EXPECT_EQ(b.wss_state, "CONNECTED")
            << "P1-3: book.wss_state 与 metrics.wss_clob_connected 应一致 (CONNECTED)";
    }

    // Disconnected scenario
    mock_wss_->set_connected(false);
    {
        const auto m = provider_->metrics();
        const auto b = provider_->book(k_tok0);
        EXPECT_FALSE(m.wss_clob_connected);
        EXPECT_EQ(b.wss_state, "DISCONNECTED")
            << "P1-3: book.wss_state 与 metrics.wss_clob_connected 应一致 (DISCONNECTED)";
    }
}

// ============================================================
// HTTP 端点集成: GET /api/v1/market/{cid} 返回 200 + found=true
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_1_HttpEndpoint_Market_Returns200_FoundTrue) {
    stcpp::debug_api::HttpServer server(k_p1_port_market, provider_.get());
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    httplib::Client cli("127.0.0.1", k_p1_port_market);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);

    const std::string path = std::string("/api/v1/market/") + k_cid;
    auto res = cli.Get(path.c_str());
    ASSERT_TRUE(res) << "GET " << path << " 连接失败";
    EXPECT_EQ(res->status, 200) << "P1-1: found=true 应返回 HTTP 200, body=" << res->body;
    EXPECT_NE(res->body.find("\"found\":true"), std::string::npos)
        << "P1-1: body 应含 found:true, body=" << res->body;
    EXPECT_NE(res->body.find("\"slug\":\"test-nba-2026\""), std::string::npos)
        << "P1-1: body 应含 slug 真实值";
    EXPECT_NE(res->body.find("\"accepting_orders\":true"), std::string::npos)
        << "P1-1: body 应含 accepting_orders:true";

    server.stop();
}

// ============================================================
// HTTP 端点集成: GET /metrics uptime > 0
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_2_HttpEndpoint_Metrics_UptimeNonZero) {
    stcpp::debug_api::HttpServer server(k_p1_port_metrics, provider_.get());
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    httplib::Client cli("127.0.0.1", k_p1_port_metrics);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);

    auto res = cli.Get("/metrics");
    ASSERT_TRUE(res) << "GET /metrics 连接失败";
    EXPECT_EQ(res->status, 200);

    // 验证 stcpp_uptime_seconds{mode="paper"} != 0
    const std::string& body = res->body;
    // 找 "stcpp_uptime_seconds{" 行
    auto pos = body.find("stcpp_uptime_seconds");
    ASSERT_NE(pos, std::string::npos) << "P1-2: /metrics 应含 stcpp_uptime_seconds";
    // 找值: 形如 "stcpp_uptime_seconds{mode=\"paper\"} 42"
    // 找最后一个空格后的数字
    auto val_pos = body.find_last_of(' ', body.find('\n', pos));
    if (val_pos != std::string::npos && val_pos < body.size()) {
        // 简单检查: 行内含非零数字
        std::string line = body.substr(pos, body.find('\n', pos) - pos);
        // 行应含 " 4" 或更大数字 (≥40s uptime, 以 "4" 开头足以验证非零)
        EXPECT_NE(line.rfind(" 0"), line.size() - 2)  // 末尾不是 " 0"
            << "P1-2: stcpp_uptime_seconds 应非零, line=" << line;
    }

    server.stop();
}

// ============================================================
// HTTP 端点集成: GET /status 与 GET /api/v1/book/{cid} wss_state 一致
// ============================================================
TEST_F(P1MarketMetricsFixture, P1_3_HttpEndpoint_StatusAndBook_WssConsistent) {
    mock_wss_->set_connected(false);  // WSS 断线场景

    stcpp::debug_api::HttpServer server(k_p1_port_wss, provider_.get());
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    httplib::Client cli("127.0.0.1", k_p1_port_wss);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);

    // /status: wss_connected.clob = false
    auto status_res = cli.Get("/status");
    ASSERT_TRUE(status_res);
    EXPECT_NE(status_res->body.find("\"clob\":false"), std::string::npos)
        << "P1-3: /status wss_connected.clob 应 = false, body=" << status_res->body;

    // /api/v1/book/{cid}: token0.wss_state = "DISCONNECTED"
    const std::string book_path = std::string("/api/v1/book/") + k_cid;
    auto book_res = cli.Get(book_path.c_str());
    ASSERT_TRUE(book_res);
    // book_pair 中 token0.wss_state 应为 DISCONNECTED
    EXPECT_NE(book_res->body.find("\"wss_state\":\"DISCONNECTED\""), std::string::npos)
        << "P1-3: book wss_state 应 = DISCONNECTED 与 /status 一致, body=" << book_res->body;

    server.stop();
}
