// tests/integration/debug_api/test_observability_endpoints.cpp
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   docs/ADR/2026-05-29-observability-debug-api.md §4 / §3 / §5
//   每个 endpoint 验: 返回结构 + 顶层 mode 字段 (R-11) + 黑名单字段不泄 (小白 §1)
//
// 覆盖 endpoint:
//   GET /api/v1/positions
//   GET /api/v1/pnl/timeseries  + /api/v1/pnl/attribution
//   GET /api/v1/risk/rejects
//   GET /api/v1/gate/paper
//   GET /metrics
//   GET /api/v1/market/{condition_id}  + /api/v1/book/{condition_id}
//
// 用 FakeProvider 注入富数据 (验字段渲染 + 透传); stub 路径用默认 ctor 验空结构。
// HTTP client: httplib::Client (ADR-018). 黑名单扫描: body 不得含任何敏感子串。

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "src/stcpp/debug_api/server.hpp"
#include "src/stcpp/debug_api/state_provider.hpp"

namespace {

// 每个 TEST 用独立端口 — gtest_discover_tests 下各 case 由 ctest -jN 并行执行,
// 共享端口会 bind 冲突 (port already in use → 连接失败/crash)。
constexpr std::uint16_t k_port_fake = 18091;
constexpr std::uint16_t k_port_stub = 18092;
constexpr std::uint16_t k_port_bind = 18093;

// --- 黑名单子串: 任何 endpoint body 都不得出现 (小白 §1) ---
const std::vector<std::string>& blacklist_substrings() {
    static const std::vector<std::string> kBl = {
        "private_key",
        "privateKey",
        "mnemonic",
        "shamir",
        "passphrase",
        "api_secret",
        "apiSecret",
        "secret",
        "hmac",
        "signature",
        "sig_r",
        "sig_s",
        "sig_v",
        "digest",
        "db_password",
        "goalserve_key",
        // vendor 原始字段名前缀 (ADR-038 §3 vendor-agnostic)
        "goalserve_",
        "pm_",
        "clob_",
    };
    return kBl;
}

void expect_no_blacklist(const std::string& body, const char* where) {
    for (const auto& bad : blacklist_substrings()) {
        EXPECT_EQ(body.find(bad), std::string::npos)
            << where << " 泄露黑名单子串 '" << bad << "'. body=" << body;
    }
}

bool contains(const std::string& body, const std::string& sub) {
    return body.find(sub) != std::string::npos;
}

// 验顶层 mode 字段 (R-11): JSON 必含 "mode":"paper" (测试 build 为 paper)
void expect_mode_field(const std::string& body, const char* where) {
    EXPECT_TRUE(contains(body, "\"mode\":\"paper\"")) << where << " 缺顶层 mode:paper (R-11). body=" << body;
}

// FakeProvider: 注入富数据, 验字段渲染 + 透传 (mode=paper, R-11)
class FakeProvider final : public stcpp::debug_api::StateProvider {
public:
    stcpp::debug_api::ExecMode mode() const override { return stcpp::debug_api::ExecMode::Paper; }

    std::vector<stcpp::debug_api::HoldingView> positions() const override {
        stcpp::debug_api::HoldingView r;
        r.market_id = "mkt_nba_lal_bos";
        r.outcome = "home_win";
        r.net_qty = 120.0;
        r.avg_entry_price = 0.55;
        r.mark_price = 0.58;
        r.pnl_realized = 3.6;
        r.pnl_unrealized = 1.2;
        r.as_of_ts_ns = 1234567890123456789LL;
        return {r};
    }

    std::vector<stcpp::debug_api::PnlBucket> pnl_timeseries(std::int64_t, std::int64_t) const override {
        stcpp::debug_api::PnlBucket b;
        b.bucket_start_ts_ns = 1700000000000000000LL;
        b.cum_net_pnl = 42.5;
        b.n_trades = 7;
        return {b};
    }

    stcpp::debug_api::PnlAttribution pnl_attribution() const override {
        stcpp::debug_api::PnlAttribution a;
        a.gross = 100.0;
        a.fee = -3.0;
        a.gas = -0.5;
        a.slippage = -2.0;
        a.spread = -1.0;
        a.net = 93.5;
        return a;
    }

    std::vector<stcpp::debug_api::RiskRejectRow> risk_rejects() const override {
        stcpp::debug_api::RiskRejectRow r;
        r.reason_code = "MAX_POSITION_EXCEEDED";
        r.market_id = "mkt_nba_lal_bos";
        r.intent_ref = "intent_abc123";
        r.rejected_ts_ns = 1700000000000000000LL;
        return {r};
    }

    stcpp::debug_api::PaperGate paper_gate() const override {
        stcpp::debug_api::PaperGate g;
        g.has_data = true;
        g.n_trades = 150;
        g.positive_day_ratio = 0.6;
        g.sharpe = 1.8;
        g.prelim_pass = true;
        return g;
    }

    stcpp::debug_api::MetricsSnapshot metrics() const override {
        stcpp::debug_api::MetricsSnapshot m;
        m.uptime_sec = 3600;
        m.wss_sports_api_connected = true;
        m.rm_decision_total = 500;
        m.cum_net_pnl = 93.5;
        return m;
    }

    stcpp::debug_api::MarketInfo market(const std::string& cid) const override {
        stcpp::debug_api::MarketInfo mi;
        mi.found = true;
        mi.market_id = cid;
        mi.outcome = "home_win";
        mi.tick_size = 0.01;
        mi.fee_rate = 0.02;
        mi.active = true;
        mi.source = "polymarket";
        return mi;
    }

    stcpp::debug_api::BookSnapshot book(const std::string& cid) const override {
        stcpp::debug_api::BookSnapshot b;
        b.found = true;
        b.market_id = cid;
        b.best_bid = 0.54;
        b.best_ask = 0.56;
        b.microprice = 0.55;
        b.spread = 0.02;
        b.imbalance = 0.1;
        b.sequence_no = 9981;
        b.wss_state = "connected";
        b.ts.event_ts_ns = 1700000000000000000LL;
        b.ts.as_of_ts_ns = 1700000000000000001LL;
        return b;
    }

    // 前端 v3 盯盘新增 (ADR-038 增量)
    stcpp::debug_api::EventScore score(const std::string& eid) const override {
        stcpp::debug_api::EventScore s;
        s.found = true;
        s.event_id = eid;
        s.sport = "basketball";
        s.status = "inplay";
        s.period = "Q2";
        s.clock_sec = 300;
        s.home = "LAL";
        s.away = "BOS";
        s.home_score = 52;
        s.away_score = 49;
        s.ts.event_ts_ns = 1700000000000000000LL;
        s.ts.as_of_ts_ns = 1700000000000000001LL;
        return s;
    }

    stcpp::debug_api::QuoteParams quote_params(const std::string& cid) const override {
        stcpp::debug_api::QuoteParams q;
        q.found = true;
        q.market_id = cid;
        q.fair_value = 0.662;
        q.market_mid = 0.648;
        q.edge_bps = 21.6;
        q.kelly_fraction = 0.042;
        q.suggested_notional = 850.0;
        q.signal_strength = 0.71;
        q.model_conf = 0.62;
        q.as_of_ts_ns = 1700000000000000000LL;
        return q;
    }

    const char* data_source() const override { return "fake"; }
};

httplib::Client make_client(std::uint16_t port) {
    httplib::Client cli("localhost", port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);
    return cli;
}

}  // namespace

// ============================================================
// 富数据路径 (FakeProvider): 字段渲染 + mode + 黑名单
// ============================================================
TEST(ObservabilityEndpoints, AllEndpoints_FakeProvider) {
    FakeProvider fp;
    stcpp::debug_api::HttpServer srv(k_port_fake, &fp);
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto cli = make_client(k_port_fake);

    // /api/v1/positions
    {
        auto r = cli.Get("/api/v1/positions");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/positions");
        expect_no_blacklist(r->body, "/positions");
        EXPECT_TRUE(contains(r->body, "\"pnl_unrealized\":1.2")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"mark_price\":0.58")) << r->body;
        EXPECT_TRUE(contains(r->body, "mkt_nba_lal_bos")) << r->body;
    }

    // /api/v1/pnl/timeseries
    {
        auto r = cli.Get("/api/v1/pnl/timeseries?window=600&bucket=30");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/pnl/timeseries");
        expect_no_blacklist(r->body, "/pnl/timeseries");
        EXPECT_TRUE(contains(r->body, "\"window_sec\":600")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"cum_net_pnl\":42.5")) << r->body;
    }

    // /api/v1/pnl/attribution (瀑布)
    {
        auto r = cli.Get("/api/v1/pnl/attribution");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/pnl/attribution");
        expect_no_blacklist(r->body, "/pnl/attribution");
        EXPECT_TRUE(contains(r->body, "\"gross\":100")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"net\":93.5")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"slippage\":-2")) << r->body;
    }

    // /api/v1/risk/rejects
    {
        auto r = cli.Get("/api/v1/risk/rejects");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/risk/rejects");
        expect_no_blacklist(r->body, "/risk/rejects");
        EXPECT_TRUE(contains(r->body, "MAX_POSITION_EXCEEDED")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"reason_code\"")) << r->body;
    }

    // /api/v1/gate/paper
    {
        auto r = cli.Get("/api/v1/gate/paper");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/gate/paper");
        expect_no_blacklist(r->body, "/gate/paper");
        EXPECT_TRUE(contains(r->body, "\"has_data\":true")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"sharpe\":1.8")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"prelim_pass\":true")) << r->body;
    }

    // /metrics (Prometheus text)
    {
        auto r = cli.Get("/metrics");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_no_blacklist(r->body, "/metrics");
        auto ct = r->get_header_value("Content-Type");
        EXPECT_NE(ct.find("text/plain"), std::string::npos) << ct;
        EXPECT_TRUE(contains(r->body, "stcpp_uptime_seconds")) << r->body;
        EXPECT_TRUE(contains(r->body, "stcpp_rm_decision_total")) << r->body;
        // mode 作为低基数 label
        EXPECT_TRUE(contains(r->body, "mode=\"paper\"")) << r->body;
        // wss channel 枚举 label
        EXPECT_TRUE(contains(r->body, "channel=\"sports_api\"")) << r->body;
        // 低基数: 不得出现 per-market label
        EXPECT_EQ(r->body.find("market_id="), std::string::npos) << r->body;
        EXPECT_EQ(r->body.find("intent_id="), std::string::npos) << r->body;
    }

    // /api/v1/market/{condition_id}
    {
        auto r = cli.Get("/api/v1/market/0xCONDITION");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/market");
        expect_no_blacklist(r->body, "/market");
        EXPECT_TRUE(contains(r->body, "\"found\":true")) << r->body;
        EXPECT_TRUE(contains(r->body, "0xCONDITION")) << r->body;
        // 三态分开
        EXPECT_TRUE(contains(r->body, "\"active\":true")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"closed\":false")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"resolved\":false")) << r->body;
        // vendor 降为 source 标签
        EXPECT_TRUE(contains(r->body, "\"source\":\"polymarket\"")) << r->body;
    }

    // /api/v1/book/{condition_id}
    {
        auto r = cli.Get("/api/v1/book/0xCONDITION");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/book");
        expect_no_blacklist(r->body, "/book");
        EXPECT_TRUE(contains(r->body, "\"microprice\":0.55")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"spread\":0.02")) << r->body;
        EXPECT_TRUE(contains(r->body, "\"imbalance\":0.1")) << r->body;
        // 4 时间戳 (R-20): event_ts 透传, 非本地 now()
        EXPECT_TRUE(contains(r->body, "\"event_ts\":1700000000000000000")) << r->body;
    }

    srv.stop();
}

// ============================================================
// stub 路径 (默认 provider): 空结构合法 + mode + 黑名单 + 404
// ============================================================
TEST(ObservabilityEndpoints, StubProvider_EmptyButValid) {
    stcpp::debug_api::HttpServer srv(k_port_stub);  // 默认 StubStateProvider(paper)
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto cli = make_client(k_port_stub);

    {
        auto r = cli.Get("/api/v1/positions");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/positions stub");
        expect_no_blacklist(r->body, "/positions stub");
        EXPECT_TRUE(contains(r->body, "\"positions\":[]")) << r->body;
    }
    {
        auto r = cli.Get("/api/v1/gate/paper");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        expect_mode_field(r->body, "/gate stub");
        EXPECT_TRUE(contains(r->body, "\"has_data\":false")) << r->body;
    }
    {
        auto r = cli.Get("/metrics");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 200);
        EXPECT_TRUE(contains(r->body, "stcpp_uptime_seconds")) << r->body;
    }
    {
        // stub: market not found → 404 + found:false
        auto r = cli.Get("/api/v1/market/0xUNKNOWN");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 404);
        EXPECT_TRUE(contains(r->body, "\"found\":false")) << r->body;
        expect_mode_field(r->body, "/market stub");
    }
    {
        auto r = cli.Get("/api/v1/book/0xUNKNOWN");
        ASSERT_TRUE(r) << r.error();
        EXPECT_EQ(r->status, 404);
        EXPECT_TRUE(contains(r->body, "\"found\":false")) << r->body;
    }

    srv.stop();
}

// ============================================================
// 默认 bind 127.0.0.1 (ADR-038 §5): localhost 可达
// ============================================================
TEST(ObservabilityEndpoints, DefaultBindLoopback) {
    stcpp::debug_api::HttpServer srv(k_port_bind);
    srv.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    httplib::Client cli("127.0.0.1", k_port_bind);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(2, 0);
    auto r = cli.Get("/api/v1/positions");
    ASSERT_TRUE(r) << "127.0.0.1 loopback 不可达: " << r.error();
    EXPECT_EQ(r->status, 200);

    srv.stop();
}
