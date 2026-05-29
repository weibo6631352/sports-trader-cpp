// src/stcpp/debug_api/demo_state_provider.hpp — 演示用 StateProvider (前端集成 / dogfood)
// Owner: 老雷 (GM) 驱动前端集成 — 2026-05-29
// 关联:
//   state_provider.hpp (StateProvider 契约 + StubStateProvider)
//   ADR-038 §2/§3/§4 (观测 API schema 铁律: epoch_ns int64, vendor-agnostic, 黑名单字段物理不在)
//   目标: "能看到任何已经开发的功能状态" — 给观测看板填充结构合法 + 视觉可读的代表性数据。
//
// 定位 (R-11 / R-12):
//   - 这是【观测侧】只读 provider, 与热路径零耦合, 绝不调 RM/signer/exec。
//   - 数据为【确定性演示值】(非真实成交), 用于前端集成、UX 评估、dogfood 全流程跑通。
//   - 真实接入 (老韩 RM reject snapshot / 小石 持仓账本 snapshot / 小冯 orderbook features /
//     小蒋 paper 滚动统计) 落地后, 由 main 注入真 provider 替换本 Demo。
//   - 时间戳: as_of 用 now() (R-20 允许 — 观测读取时刻); 上游 event/data_source/ingestion
//     用 now() 减固定偏移【模拟】上游链路 (Demo 语义明确, 非生产数据)。
//
// 注意: 本头文件【不得】#include 任何热路径模块头, 与 StubStateProvider 同等约束。

#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "src/stcpp/debug_api/state_provider.hpp"

namespace stcpp::debug_api {

class DemoStateProvider final : public StateProvider {
public:
    explicit DemoStateProvider(ExecMode m = ExecMode::Paper) : mode_(m) {}

    ExecMode mode() const override { return mode_; }

    // ---- /api/v1/positions ----
    std::vector<HoldingView> positions() const override {
        const std::int64_t now = now_ns();
        std::vector<HoldingView> v;
        v.push_back(
            make_holding("nba-lal-bos-ml", "LAL", 1500.0, 0.62, 0.65, 45.0, 45.0, now - 60'000'000'000LL));
        v.push_back(
            make_holding("nba-lal-bos-ml", "BOS", -800.0, 0.38, 0.35, -12.5, 24.0, now - 30'000'000'000LL));
        v.push_back(make_holding("epl-ars-che-total", "OVER_2.5", 2200.0, 0.71, 0.74, 110.0, 66.0,
                                 now - 10'000'000'000LL));
        return v;
    }

    // ---- /api/v1/pnl/timeseries ----
    std::vector<PnlBucket> pnl_timeseries(std::int64_t window_sec, std::int64_t bucket_sec) const override {
        if (window_sec <= 0) {
            window_sec = 3600;
        }
        if (bucket_sec <= 0) {
            bucket_sec = 300;
        }
        std::int64_t n = window_sec / bucket_sec;
        if (n < 2) {
            n = 12;
        }
        if (n > 240) {
            n = 240;  // 防御: 上限
        }
        const std::int64_t now = now_ns();
        const std::int64_t bucket_ns = bucket_sec * 1'000'000'000LL;

        std::vector<PnlBucket> out;
        out.reserve(static_cast<std::size_t>(n));
        double cum = 0.0;
        for (std::int64_t i = 0; i < n; ++i) {
            // 确定性"随机游走": 正弦 + 线性漂移 (无 Math.random, 可复现)
            const double phase = static_cast<double>(i);
            const double delta = 6.0 * sine(phase * 0.7) + 1.4 * phase * 0.1 + 2.0;
            cum += delta;
            PnlBucket b;
            b.bucket_start_ts_ns = now - (n - i) * bucket_ns;
            b.cum_net_pnl = round4(cum);
            b.realized = round4(delta * 0.6);
            b.unrealized = round4(delta * 0.4);
            b.fee = round4(0.8 + 0.5 * (1.0 + sine(phase)));
            b.gas = round4(0.05 + 0.03 * (1.0 + sine(phase * 1.3)));
            b.n_trades = 2 + (i % 6);
            out.push_back(b);
        }
        return out;
    }

    // ---- /api/v1/pnl/attribution ----
    PnlAttribution pnl_attribution() const override {
        PnlAttribution a;
        a.gross = 312.5;
        a.fee = -18.3;
        a.gas = -2.1;
        a.slippage = -9.7;
        a.spread = 24.6;
        a.net = a.gross + a.fee + a.gas + a.slippage + a.spread;  // = 307.0
        a.as_of_ts_ns = now_ns();
        a.per_market = {
            {"nba-lal-bos-ml", 90.0},
            {"epl-ars-che-total", 176.0},
            {"nfl-kc-buf-spread", 77.5},
            {"mlb-nyy-bos-ml", -36.5},
        };
        return a;
    }

    // ---- /api/v1/risk/rejects ----
    std::vector<RiskRejectRow> risk_rejects() const override {
        const std::int64_t now = now_ns();
        std::vector<RiskRejectRow> v;
        v.push_back({"MAX_POSITION_EXCEEDED", "nba-lal-bos-ml", "intent-7f3a", "BUY", 500.0, 0.41,
                     now - 2'000'000'000LL});
        v.push_back({"MARKET_NOT_ACCEPTING_ORDERS", "mlb-nyy-bos-ml", "intent-9c21", "SELL", 100.0, 0.88,
                     now - 12'000'000'000LL});
        v.push_back({"KELLY_FRACTION_CAP", "epl-ars-che-total", "intent-b04e", "BUY", 1200.0, 0.73,
                     now - 41'000'000'000LL});
        return v;
    }

    // ---- /api/v1/gate/paper ----
    PaperGate paper_gate() const override {
        PaperGate g;
        g.has_data = true;  // Demo: 门禁已积累数据
        g.window_days = 30;
        g.n_trades = 142;
        g.positive_day_ratio = 0.6333;
        g.sharpe = 1.12;
        g.sharpe_se = 0.18;
        g.p_value = 0.043;
        g.hit_rate = 0.5634;
        g.max_drawdown = 0.082;
        g.prelim_pass = true;
        g.confirm_pass = false;
        g.as_of_ts_ns = now_ns();
        return g;
    }

    // ---- /metrics ----
    MetricsSnapshot metrics() const override {
        MetricsSnapshot m;
        m.uptime_sec = 3721;
        m.wss_sports_api_connected = true;
        m.wss_clob_connected = true;
        m.wss_user_channel_connected = false;
        m.wss_reconnect_total = 1;
        m.loop_p99_us = 74.0;
        m.rm_decision_total = 142;
        m.rm_reject_total = 3;
        m.fill_total = 139;
        m.net_edge_bps = 12.4;
        m.cum_net_pnl = 307.0;
        m.max_staleness_ms = 38.0;
        m.feed_gap_total = 0;
        m.price_drift_bps = 2.1;
        return m;
    }

    // ---- /api/v1/market/{condition_id} ----
    MarketInfo market(const std::string& condition_id) const override {
        MarketInfo mi;
        mi.found = true;  // Demo: 任意 condition_id 视为活跃市场
        mi.market_id = condition_id;
        mi.outcome = "YES";
        mi.tick_size = 0.01;
        mi.fee_rate = 0.02;
        mi.neg_risk = false;
        mi.accepting_orders = true;
        mi.active = true;
        mi.closed = false;
        mi.resolved = false;
        mi.source = "polymarket";
        mi.as_of_ts_ns = now_ns();
        return mi;
    }

    // ---- /api/v1/book/{condition_id} ----
    BookSnapshot book(const std::string& condition_id) const override {
        const std::int64_t now = now_ns();
        BookSnapshot b;
        b.found = true;
        b.market_id = condition_id;
        b.best_bid = 0.644;
        b.best_ask = 0.656;
        b.microprice = 0.648;
        b.spread = round4(b.best_ask - b.best_bid);
        b.imbalance = 0.23;
        b.sequence_no = 88421;
        b.gap_count = 0;
        b.wss_state = "CONNECTED";
        b.source = "polymarket";
        b.ts.event_ts_ns = now - 100'000'000LL;
        b.ts.data_source_ts_ns = now - 90'000'000LL;
        b.ts.ingestion_ts_ns = now - 80'000'000LL;
        b.ts.as_of_ts_ns = now;
        b.bids = {{0.644, 3200.0}, {0.638, 1800.0}, {0.630, 900.0}, {0.620, 400.0}};
        b.asks = {{0.656, 2700.0}, {0.662, 1500.0}, {0.670, 600.0}, {0.680, 300.0}};
        return b;
    }

private:
    ExecMode mode_;

    static std::int64_t now_ns() noexcept {
        using namespace std::chrono;
        return static_cast<std::int64_t>(
            duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    static double round4(double v) noexcept {
        return static_cast<double>(static_cast<long long>(v * 10000.0 + (v >= 0 ? 0.5 : -0.5))) / 10000.0;
    }

    // 轻量正弦近似 (避免 <cmath> 依赖差异; Bhaskara I 近似, 误差 < 2%, Demo 足够)。
    static double sine(double x) noexcept {
        constexpr double pi = 3.14159265358979323846;
        constexpr double two_pi = 2.0 * pi;
        // 归一到 [-pi, pi]
        x = x - two_pi * static_cast<double>(static_cast<long long>(x / two_pi));
        if (x > pi) {
            x -= two_pi;
        }
        if (x < -pi) {
            x += two_pi;
        }
        // Bhaskara I sine approximation
        const double num = 16.0 * x * (pi - (x < 0 ? -x : x));
        const double den = 5.0 * pi * pi - 4.0 * (x < 0 ? -x : x) * (pi - (x < 0 ? -x : x));
        return num / den;
    }

    static HoldingView make_holding(const char* mkt, const char* outcome, double qty, double avg, double mark,
                                    double r, double u, std::int64_t ts) {
        HoldingView h;
        h.market_id = mkt;
        h.outcome = outcome;
        h.net_qty = qty;
        h.avg_entry_price = avg;
        h.mark_price = mark;
        h.pnl_realized = r;
        h.pnl_unrealized = u;
        h.as_of_ts_ns = ts;
        return h;
    }
};

}  // namespace stcpp::debug_api
