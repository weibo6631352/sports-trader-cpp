// src/stcpp/debug_api/demo_state_provider.hpp — 演示用 StateProvider (前端集成 / dogfood)
// Owner: 老雷 (GM) 驱动前端集成 — 2026-05-29
// 关联:
//   state_provider.hpp (StateProvider 契约 + StubStateProvider)
//   ADR-038 §2/§3/§4 (观测 API schema 铁律: epoch_ns int64, vendor-agnostic, 黑名单字段物理不在)
//   docs/RESEARCH/laozhou-market-structure-contract-fix-v1.md (ADR-040 市场结构修正)
//   目标: "能看到任何已经开发的功能状态" — 给观测看板填充结构合法 + 视觉可读的代表性数据。
//
// ADR-040 变更 (2026-05-29):
//   market(): 填 condition_id + tokens[] (每盘口 2 个 token, 互补价格) + slug + polymarket_url
//   book(token_id): 按 token_id 返回单边 book (token0/token1 互补镜像, 带 outcome/condition_id)
//   book_pair(condition_id): 组装 token0/token1 双 book + cross_spread
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
    // ADR-040: 填 condition_id / tokens[] (每盘口 2 token, 互补价格) / slug / polymarket_url
    //   market_id 保持原值 (deprecated alias = condition_id)。
    MarketInfo market(const std::string& condition_id) const override {
        MarketInfo mi;
        mi.found = true;  // Demo: 任意 condition_id 视为活跃市场
        mi.condition_id = condition_id;
        mi.market_id = condition_id;  // DEPRECATED alias = condition_id
        mi.tick_size = 0.01;
        mi.fee_rate = 0.02;
        mi.neg_risk = false;
        mi.accepting_orders = true;
        mi.active = true;
        mi.closed = false;
        mi.resolved = false;
        mi.source = "polymarket";
        mi.as_of_ts_ns = now_ns();
        // 前端 v3: market → event 锚
        mi.event_id = derive_event_id(condition_id);

        // ADR-040: tokens[] — 2 个 token, 互补价格 (价格之和 ≈ 1.00)
        // 代表性 demo: nba-lal-bos-ml → LAL(tok-lal-001, 0.65) / BOS(tok-bos-001, 0.35)
        const auto tok_ids = derive_token_ids(condition_id);
        const auto outcomes = derive_outcomes(condition_id);
        TokenInfo t0;
        t0.token_id = tok_ids.first;
        t0.outcome = outcomes.first;
        t0.price = 0.65;
        t0.winner = false;
        TokenInfo t1;
        t1.token_id = tok_ids.second;
        t1.outcome = outcomes.second;
        t1.price = 0.35;
        t1.winner = false;
        mi.tokens = {t0, t1};

        // ADR-040 P1: neg_risk_market_id (可空; Demo: neg_risk=false 时留空)
        mi.neg_risk_market_id = "";

        // ADR-040 Polymarket 超链接 (老板要求 P0)
        mi.slug = derive_slug(condition_id);
        mi.polymarket_url = "https://polymarket.com/event/" + mi.slug;

        return mi;
    }

    // ---- /api/v1/score/{event_id} ----
    // Demo: 按 event_id 返回代表性比分; 任意未知 id 返回合法 inplay demo (found=true)。
    EventScore score(const std::string& event_id) const override {
        const std::int64_t now = now_ns();
        EventScore s;
        s.found = true;
        s.event_id = event_id;
        s.source = "goalserve";

        if (event_id.find("nba-lal-bos") != std::string::npos) {
            s.sport = "basketball";
            s.status = "inplay";
            s.period = "Q3";
            s.clock_sec = 522;
            s.home = "LAL";
            s.away = "BOS";
            s.home_score = 87;
            s.away_score = 91;
        } else if (event_id.find("epl-ars-che") != std::string::npos) {
            s.sport = "soccer";
            s.status = "inplay";
            s.period = "2H";
            s.clock_sec = 3720;
            s.home = "ARS";
            s.away = "CHE";
            s.home_score = 1;
            s.away_score = 1;
        } else if (event_id.find("nfl-kc-buf") != std::string::npos) {
            s.sport = "football";
            s.status = "halftime";
            s.period = "HT";
            s.clock_sec = 0;
            s.home = "KC";
            s.away = "BUF";
            s.home_score = 17;
            s.away_score = 14;
        } else if (event_id.find("mlb-nyy-bos") != std::string::npos) {
            s.sport = "baseball";
            s.status = "inplay";
            s.period = "T7";
            s.clock_sec = 0;
            s.home = "NYY";
            s.away = "BOS";
            s.home_score = 4;
            s.away_score = 3;
        } else {
            s.sport = "basketball";
            s.status = "inplay";
            s.period = "Q2";
            s.clock_sec = 300;
            s.home = "HOME";
            s.away = "AWAY";
            s.home_score = 52;
            s.away_score = 49;
        }

        // 4 时间戳 (R-20): Demo 语义 — 用 now()-偏移模拟上游链路延迟
        s.ts.event_ts_ns = now - 2'500'000'000LL;
        s.ts.data_source_ts_ns = now - 2'200'000'000LL;
        s.ts.ingestion_ts_ns = now - 1'800'000'000LL;
        s.ts.as_of_ts_ns = now;
        return s;
    }

    // ---- /api/v1/quote/{condition_id} ----
    QuoteParams quote_params(const std::string& condition_id) const override {
        QuoteParams q;
        q.found = true;
        q.market_id = condition_id;
        q.fair_value = 0.662;
        q.market_mid = 0.648;
        q.edge_bps = 21.6;
        q.kelly_fraction = 0.042;
        q.suggested_notional = 850.0;
        q.signal_strength = 0.71;
        q.model_conf = 0.62;
        q.as_of_ts_ns = now_ns();
        return q;
    }

    // ---- data_source ----
    const char* data_source() const override { return "demo"; }

    // ---- /api/v1/book/{token_id} (ADR-040 per-token) ----
    // 按 token_id 返回单边 book。
    // Demo: tok-lal-001 → token0 book (LAL, 0.65 side);
    //        tok-bos-001 → token1 book (BOS, 0.35 side);
    //        其他 → fallback token0 demo。
    // token0.best_bid + token1.best_ask = 1.00 (互补镜像)
    // token0.best_ask + token1.best_ask - 1.0 = cross_spread (vig)
    BookSnapshot book(const std::string& token_id) const override {
        const std::int64_t now = now_ns();
        // 判断是否为 token1 (BOS/Away/Under 侧)
        const bool is_token1 = is_second_token(token_id);
        BookSnapshot b = make_book_snapshot(token_id, is_token1, now);
        // 从 token_id 反查 condition_id 和 outcome (Demo 简化: 枚举已知 token)
        const auto [cond_id, outcome] = lookup_token_meta(token_id, is_token1);
        b.condition_id = cond_id;
        b.market_id = cond_id;  // deprecated alias
        b.outcome = outcome;
        return b;
    }

    // ---- /api/v1/book_pair/{condition_id} (ADR-040 新增) ----
    // 组装 token0 + token1 双 book + cross_spread。
    // 任意未知 condition_id 返回合法 demo (found=true)。
    BinaryMarketBookView book_pair(const std::string& condition_id) const override {
        const std::int64_t now = now_ns();
        const auto tok_ids = derive_token_ids(condition_id);
        const auto outcomes = derive_outcomes(condition_id);

        BinaryMarketBookView v;
        v.found = true;
        v.condition_id = condition_id;

        // token0 (LAL/Home/Yes/Over 侧): best_bid=0.644, best_ask=0.656
        v.token0 = make_book_snapshot(tok_ids.first, false, now);
        v.token0.condition_id = condition_id;
        v.token0.market_id = condition_id;  // deprecated alias
        v.token0.outcome = outcomes.first;

        // token1 (BOS/Away/No/Under 侧): best_bid=0.344, best_ask=0.356
        // 互补性: token0.best_ask + token1.best_ask - 1.0 = 0.656 + 0.356 - 1.0 = 0.012
        v.token1 = make_book_snapshot(tok_ids.second, true, now);
        v.token1.condition_id = condition_id;
        v.token1.market_id = condition_id;  // deprecated alias
        v.token1.outcome = outcomes.second;

        // cross_spread = token0.best_ask + token1.best_ask - 1.0 (等效 vig)
        v.cross_spread = round4(v.token0.best_ask + v.token1.best_ask - 1.0);

        // ts: 取两 token 较旧 as_of (保守 staleness)
        v.ts.event_ts_ns = v.token0.ts.event_ts_ns;  // 相同 offset
        v.ts.data_source_ts_ns = v.token0.ts.data_source_ts_ns;
        v.ts.ingestion_ts_ns = v.token0.ts.ingestion_ts_ns;
        v.ts.as_of_ts_ns = now;

        return v;
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

    // 轻量正弦近似 (Bhaskara I, 误差 < 2%, Demo 足够)
    static double sine(double x) noexcept {
        constexpr double pi = 3.14159265358979323846;
        constexpr double two_pi = 2.0 * pi;
        x = x - two_pi * static_cast<double>(static_cast<long long>(x / two_pi));
        if (x > pi) {
            x -= two_pi;
        }
        if (x < -pi) {
            x += two_pi;
        }
        const double num = 16.0 * x * (pi - (x < 0 ? -x : x));
        const double den = 5.0 * pi * pi - 4.0 * (x < 0 ? -x : x) * (pi - (x < 0 ? -x : x));
        return num / den;
    }

    // condition_id → event_id: 去掉已知后缀后拼演示日期
    static std::string derive_event_id(const std::string& condition_id) {
        static const char* const kSuffixes[] = {"-spread", "-total", "-ml", nullptr};
        std::string base = condition_id;
        for (int i = 0; kSuffixes[i] != nullptr; ++i) {
            const std::string suf = kSuffixes[i];
            if (base.size() > suf.size() && base.compare(base.size() - suf.size(), suf.size(), suf) == 0) {
                base = base.substr(0, base.size() - suf.size());
                break;
            }
        }
        return base + "-2026-05-29";
    }

    // condition_id → slug (for polymarket_url): 派生代表性 slug
    static std::string derive_slug(const std::string& condition_id) {
        // Demo: 去掉已知后缀后拼日期, 作为代表性 slug
        // 真实接入时 slug 来自 gamma `slug` 字段
        static const char* const kSuffixes[] = {"-spread", "-total", "-ml", nullptr};
        std::string base = condition_id;
        for (int i = 0; kSuffixes[i] != nullptr; ++i) {
            const std::string suf = kSuffixes[i];
            if (base.size() > suf.size() && base.compare(base.size() - suf.size(), suf.size(), suf) == 0) {
                base = base.substr(0, base.size() - suf.size());
                break;
            }
        }
        return base + "-2026-05-29";
    }

    // condition_id → (token0_id, token1_id)
    // Demo: 基于 condition_id 派生固定 demo token ids
    static std::pair<std::string, std::string> derive_token_ids(const std::string& condition_id) {
        if (condition_id.find("nba-lal-bos") != std::string::npos) {
            return {"tok-lal-001", "tok-bos-001"};
        }
        if (condition_id.find("epl-ars-che") != std::string::npos) {
            return {"tok-ars-001", "tok-che-001"};
        }
        if (condition_id.find("nfl-kc-buf") != std::string::npos) {
            return {"tok-kc-001", "tok-buf-001"};
        }
        if (condition_id.find("mlb-nyy-bos") != std::string::npos) {
            return {"tok-nyy-001", "tok-bos-nyy-001"};
        }
        // fallback: deterministic demo ids
        return {condition_id + "-tok0", condition_id + "-tok1"};
    }

    // condition_id → (outcome0, outcome1)
    static std::pair<std::string, std::string> derive_outcomes(const std::string& condition_id) {
        if (condition_id.find("nba-lal-bos") != std::string::npos) {
            return {"LAL", "BOS"};
        }
        if (condition_id.find("epl-ars-che") != std::string::npos) {
            return {"ARS", "CHE"};
        }
        if (condition_id.find("nfl-kc-buf") != std::string::npos) {
            return {"KC", "BUF"};
        }
        if (condition_id.find("mlb-nyy-bos") != std::string::npos) {
            return {"NYY", "BOS"};
        }
        if (condition_id.find("total") != std::string::npos) {
            return {"Over", "Under"};
        }
        return {"Yes", "No"};
    }

    // token_id → (condition_id, outcome): Demo 反查映射
    static std::pair<std::string, std::string> lookup_token_meta(const std::string& token_id,
                                                                 bool is_token1) {
        struct Entry {
            const char* tok;
            const char* cond;
            const char* outcome;
        };
        static const Entry kMap[] = {
            {"tok-lal-001", "nba-lal-bos-ml", "LAL"},
            {"tok-bos-001", "nba-lal-bos-ml", "BOS"},
            {"tok-ars-001", "epl-ars-che-total", "ARS"},
            {"tok-che-001", "epl-ars-che-total", "CHE"},
            {"tok-kc-001", "nfl-kc-buf-spread", "KC"},
            {"tok-buf-001", "nfl-kc-buf-spread", "BUF"},
            {"tok-nyy-001", "mlb-nyy-bos-ml", "NYY"},
            {"tok-bos-nyy-001", "mlb-nyy-bos-ml", "BOS"},
            {nullptr, nullptr, nullptr},
        };
        for (int i = 0; kMap[i].tok != nullptr; ++i) {
            if (token_id == kMap[i].tok) {
                return {kMap[i].cond, kMap[i].outcome};
            }
        }
        // generic: ends with -tok0/-tok1
        const std::string suf0 = "-tok0";
        const std::string suf1 = "-tok1";
        if (token_id.size() > suf0.size()) {
            if (token_id.compare(token_id.size() - suf0.size(), suf0.size(), suf0) == 0) {
                const std::string cond = token_id.substr(0, token_id.size() - suf0.size());
                return {cond, "Yes"};
            }
            if (token_id.compare(token_id.size() - suf1.size(), suf1.size(), suf1) == 0) {
                const std::string cond = token_id.substr(0, token_id.size() - suf1.size());
                return {cond, "No"};
            }
        }
        // absolute fallback
        return {"unknown-condition", is_token1 ? "No" : "Yes"};
    }

    // 判断 token_id 是否为 token1 (second token)
    static bool is_second_token(const std::string& token_id) {
        // Demo: tok-bos-001, tok-che-001, tok-buf-001, tok-bos-nyy-001, *-tok1
        if (token_id == "tok-bos-001" || token_id == "tok-che-001" || token_id == "tok-buf-001" ||
            token_id == "tok-bos-nyy-001") {
            return true;
        }
        // generic: ends with "-tok1"
        const std::string suf = "-tok1";
        if (token_id.size() > suf.size() &&
            token_id.compare(token_id.size() - suf.size(), suf.size(), suf) == 0) {
            return true;
        }
        return false;
    }

    // 构建单边 BookSnapshot demo 值
    // token0 (is_token1=false): best_bid=0.644, best_ask=0.656 (LAL/Home/Yes 侧)
    // token1 (is_token1=true):  best_bid=0.344, best_ask=0.356 (BOS/Away/No 侧)
    // 互补性验证:
    //   token0.best_bid + token1.best_ask = 0.644 + 0.356 = 1.000 ✓
    //   token0.best_ask + token1.best_bid = 0.656 + 0.344 = 1.000 ✓
    //   cross_spread = token0.best_ask + token1.best_ask - 1.0 = 0.656 + 0.356 - 1.0 = 0.012
    static BookSnapshot make_book_snapshot(const std::string& token_id, bool is_token1, std::int64_t now) {
        BookSnapshot b;
        b.found = true;
        b.token_id = token_id;
        b.source = "polymarket";
        b.wss_state = "CONNECTED";
        b.sequence_no = is_token1 ? 88422LL : 88421LL;
        b.gap_count = 0;

        if (!is_token1) {
            // token0: LAL/Home/Yes 侧
            b.best_bid = 0.644;
            b.best_ask = 0.656;
            b.microprice = 0.648;
            b.spread = round4(b.best_ask - b.best_bid);
            b.imbalance = 0.23;
            b.bids = {{0.644, 3200.0}, {0.638, 1800.0}, {0.630, 900.0}, {0.620, 400.0}};
            b.asks = {{0.656, 2700.0}, {0.662, 1500.0}, {0.670, 600.0}, {0.680, 300.0}};
        } else {
            // token1: BOS/Away/No 侧 (互补镜像)
            b.best_bid = 0.344;
            b.best_ask = 0.356;
            b.microprice = 0.350;
            b.spread = round4(b.best_ask - b.best_bid);
            b.imbalance = -0.23;  // 镜像 imbalance
            b.bids = {{0.344, 2700.0}, {0.338, 1500.0}, {0.330, 600.0}, {0.320, 300.0}};
            b.asks = {{0.356, 3200.0}, {0.362, 1800.0}, {0.370, 900.0}, {0.380, 400.0}};
        }

        // 4 时间戳 (R-20): Demo 用 now()-偏移模拟链路
        b.ts.event_ts_ns = now - 100'000'000LL;
        b.ts.data_source_ts_ns = now - 90'000'000LL;
        b.ts.ingestion_ts_ns = now - 80'000'000LL;
        b.ts.as_of_ts_ns = now;

        return b;
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
