// src/stcpp/debug_api/real_state_provider.hpp — RealStateProvider (真实快照接入)
//
// Owner: 小卢 (senior-ic-pool)
// last_review: 2026-05-29
//
// 设计概述 (集成 ③ 更新):
//   真实接入路径:
//     1. OrderBookSnapshotHub (小冯) — per-token double-buffer, book()/book_pair()
//     2. RmDebugSnapshot (老沈)       — lock-free ring, risk_rejects()
//     3. SizingCalculator (小袁)      — Kelly/notional 真实计算, quote_params()
//     4. ScoreSnapshotStore (小段)    — live feed 比分快照, score()
//
//   其余方法 (positions/pnl/gate/metrics/market) 委托 DemoStateProvider,
//   data_source() 返回 "real" (score/quote 已接真实计算路径)。
//
// quote_params() 真实计算 (SizingCalculator, ADR-042):
//   输入: fair_value/edge_ci_lower/price/bankroll/RiskConfig (可配置 demo 输入, 计算路径真实)
//   计算: SizingCalculator::compute(cfg, in) → kelly_fraction/suggested_notional/edge_bps
//   cap 链: 守 5 cap (PER_ORDER/PER_OUTCOME/CONDITION/BANKROLL_FRAC/FILL_RATE_FLOOR)
//   CI gating: edge_ci_lower > floor 才产出有效报价
//
// score() 真实路径 (ScoreSnapshotStore):
//   store_.Get(event_id) → EventScore (live feed 数据)
//   无数据 (store 为空 / event_id 不存在) → 回落 DemoStateProvider.score()
//
// 优雅降级 (R-12 / R-20):
//   - hub_.Read(token_id) 返回 nullopt / valid=false → 回落 Demo.book()
//   - snap_ 为 nullptr → risk_rejects() 回落 Demo.risk_rejects()
//   - SizingCalculator.compute().valid=false → 回落 Demo.quote_params()
//   - ScoreSnapshotStore.Get() nullopt → 回落 Demo.score()
//   - 任何路径均不崩溃
//
// R-12 合规:
//   - book/book_pair/risk_rejects/quote_params/score 全部只读快照
//   - 无持锁 > 100us; 无热路径反向依赖
//   - SizingCalculator::compute() 纯函数 noexcept, 无堆分配 (R-11)
//
// R-20 合规:
//   - OrderBookFeatures 4 时间戳来自上游 WssEvent
//   - ScoreSnapshotStore EventScore.ts.data_source_ts_ns 来自 Goalserve payload
//   - as_of_ts_ns 在读取时填写 (R-20 allowed)
//
// 注意: 本头文件【不得】#include 任何热路径写端模块头 (RM evaluate / signer / exec)。
//       SizingCalculator 是纯函数 noexcept (R-11 无写 ledger/audit), 可安全 include。

#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stcpp/data/score_snapshot_store.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/risk/risk_gateway.hpp"  // RiskConfig (SizingCalculator cap 注入源)
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/sizing_calculator.hpp"  // SizingCalculator (Kelly 真实计算, ADR-042)

#include "src/stcpp/debug_api/demo_state_provider.hpp"
#include "src/stcpp/debug_api/state_provider.hpp"

namespace stcpp::debug_api {

// ============================================================================
// MarketTokenMap — condition_id → (token0_id, token1_id) 映射
//
// 生产阶段由 main 构建后传入 RealStateProvider。
// 供 book_pair(condition_id) 拆为两次 hub.Read(token_id)。
// ============================================================================
using MarketTokenMap = std::unordered_map<std::string, std::pair<std::string, std::string>>;

// ============================================================================
// SizingConfig — RealStateProvider 内部 quote 计算的输入参数配置
//
// 任务 ③ 说明: 暂用可配置 demo 输入 (fair_value/edge_ci_lower/price/bankroll),
//   但计算路径走真实 SizingCalculator，非 hardcode。
//   真实 ML 信号接入后, 由调用方注入真实 fair_value/edge_ci_lower。
// ============================================================================
struct SizingConfig {
    // fair value 输入 (demo: 0.65 = LAL 胜率估计; 真实接入时由 FairValueModel 提供)
    double demo_fair_value{0.65};
    // CI 下界 (demo: 0.018 = 180 bps; 真实接入时由信号层提供)
    double demo_edge_ci_lower{0.018};
    // 入场价 (demo: 0.636 ≈ fair - edge_bps/10000; 真实接入时由 book microprice 提供)
    double demo_price{0.636};
    // bankroll (USDC; demo: 100,000; 真实接入时由 PositionLedger 提供)
    double demo_bankroll_usdc{100'000.0};
    // fill_rate (demo: 0.85; 真实接入时由 FillRateModel 提供)
    double demo_fill_rate{0.85};
    // slippage_bps (demo: 8.0 bps; 真实接入时由 SlippageModel 提供)
    double demo_slippage_bps{8.0};
};

// ============================================================================
// RealStateProvider — 真实快照接入 (book/rejects/score/quote 已接真, 其余委托 Demo)
// ============================================================================
class RealStateProvider final : public StateProvider {
public:
    // -------------------------------------------------------------------------
    // 构造
    //   hub:        已初始化的 OrderBookSnapshotHub (caller 保证生命周期长于本对象)
    //   snap:       已 attach 的 RmDebugSnapshot* (可为 nullptr; nullptr → risk_rejects 回落 Demo)
    //   score_store: ScoreSnapshotStore* (可为 nullptr; nullptr → score 回落 Demo)
    //   tokens:     condition_id → (token0_id, token1_id) (可为空; 空 → book_pair 回落 Demo)
    //   sizing_cfg: SizingCalculator 输入参数 (demo 值, 计算路径真实)
    //   risk_cfg:   RiskConfig (cap 注入源, 禁复制字面量; 老韩 §3 强制)
    //   m:          运行模式 (build-time 锁定)
    // -------------------------------------------------------------------------
    explicit RealStateProvider(const polymarket::clob_wss::OrderBookSnapshotHub& hub,
                               const risk::RmDebugSnapshot* snap, const data::ScoreSnapshotStore* score_store,
                               MarketTokenMap tokens, SizingConfig sizing_cfg = {},
                               risk::RiskConfig risk_cfg = {}, ExecMode m = ExecMode::Paper)
        : hub_(hub),
          snap_(snap),
          score_store_(score_store),
          token_map_(std::move(tokens)),
          sizing_cfg_(sizing_cfg),
          risk_cfg_(risk_cfg),
          demo_(m),
          mode_(m) {}

    RealStateProvider(const RealStateProvider&) = delete;
    RealStateProvider& operator=(const RealStateProvider&) = delete;
    RealStateProvider(RealStateProvider&&) = delete;
    RealStateProvider& operator=(RealStateProvider&&) = delete;
    ~RealStateProvider() override = default;

    // ---- 运行模式 ----
    ExecMode mode() const override { return mode_; }

    // ---- 委托 Demo (尚无真实源) ----
    std::vector<HoldingView> positions() const override { return demo_.positions(); }

    std::vector<PnlBucket> pnl_timeseries(std::int64_t w, std::int64_t b) const override {
        return demo_.pnl_timeseries(w, b);
    }

    PnlAttribution pnl_attribution() const override { return demo_.pnl_attribution(); }

    PaperGate paper_gate() const override { return demo_.paper_gate(); }

    MetricsSnapshot metrics() const override { return demo_.metrics(); }

    MarketInfo market(const std::string& condition_id) const override { return demo_.market(condition_id); }

    // ---- score — 已接真 (ScoreSnapshotStore), 无数据回落 Demo ----
    // ScoreSnapshotStore.Get(event_id) O(1) 无锁读 (R-12 合规)
    // 无 live feed / store 空 → 回落 DemoStateProvider.score() (优雅降级)
    EventScore score(const std::string& event_id) const override {
        if (score_store_ != nullptr) {
            const auto opt = score_store_->Get(event_id);
            if (opt.has_value()) {
                return *opt;
            }
            // store 有数据但 event_id 不存在 → found=false, 不回落 Demo
            // (让前端感知 live feed 无此 event, 而非返回 Demo 假数据混淆语义)
            if (score_store_->Size() > 0) {
                EventScore miss;
                miss.found = false;
                miss.event_id = event_id;
                miss.source = "live";
                return miss;
            }
        }
        // score_store_ 为 nullptr 或 store 为空 → 回落 Demo
        return demo_.score(event_id);
    }

    // ---- quote_params — 接 SizingCalculator 真实计算 (ADR-042) ----
    // 输入: demo/可配置 fair_value/edge_ci_lower/price/bankroll (SizingConfig)
    // 计算: SizingCalculator::compute() → kelly_fraction/suggested_notional/net_ci_edge
    // 守 5 cap 链 + CI gating (sizing_calculator.hpp §4)
    // valid=false → 回落 DemoStateProvider.quote_params()
    // AI provenance: advisory=true (ML-R2), model_id="demo-fv-v0" (demo 输入标记)
    QuoteParams quote_params(const std::string& condition_id) const override {
        // 构造 SizingInput (demo 输入值, 计算路径走真实 SizingCalculator)
        sizing::SizingInput in;
        in.fair_value = sizing_cfg_.demo_fair_value;
        in.edge_ci_lower = sizing_cfg_.demo_edge_ci_lower;
        in.price = sizing_cfg_.demo_price;
        in.bankroll_usdc = sizing_cfg_.demo_bankroll_usdc;
        in.fill_rate = sizing_cfg_.demo_fill_rate;
        in.slippage_bps = sizing_cfg_.demo_slippage_bps;
        in.buy_yes = (in.fair_value > in.price);  // 方向: fair > price → buy YES
        // edge_bps (展示用, 不进 gating)
        in.edge_bps = std::abs(in.fair_value - in.price) * 10'000.0;
        // 当前敞口 (standalone demo: 0; 真实接入时由 PositionLedger 提供)
        in.current_token_exposure_usdc = 0.0;
        in.current_condition_exposure_usdc = 0.0;

        // 真实 Kelly 计算 (守 5 cap + CI gating)
        const sizing::SizingOutput out = sizing::SizingCalculator::compute(risk_cfg_, in);

        if (!out.valid) {
            // fail-closed: sizing 无效 (CI gating 拒 / 输入越界) → 回落 Demo
            QuoteParams fb = demo_.quote_params(condition_id);
            fb.model_id = "demo-fv-v0";
            fb.advisory = true;
            fb.predict_ok = false;
            return fb;
        }

        const std::int64_t now = now_ns();
        QuoteParams q;
        q.found = true;
        q.market_id = condition_id;
        q.as_of_ts_ns = now;

        // ---- 量化字段 (来自 SizingCalculator 真实计算) ----
        q.fair_value = in.fair_value;
        q.market_mid = in.price;  // demo: mid ≈ entry price
        q.edge_bps = in.edge_bps;
        q.kelly_fraction = out.kelly_fractional;                    // quarter Kelly (λ=0.25)
        q.suggested_notional = out.suggested_notional;              // 过完 5 cap
        q.signal_strength = sizing_cfg_.demo_edge_ci_lower * 10.0;  // demo proxy: CI lower × 10
        // model_conf 跟随 model_confidence (deprecated alias, G-FREEZE-W)
        q.model_conf = 0.62;  // demo confidence 占位值
        q.model_confidence = 0.62;

        // ---- AI provenance (小邓 spec v1 §3.2; demo 标记) ----
        q.model_id = "demo-fv-v0";  // 标记为 demo 输入 (非真实 ML 模型)
        q.model_kind = "stub";      // stub = 占位, 看板灰显 (XD-4)
        q.spec_version = "ml-feature-spec-v0.1";
        q.model_calibrated = false;                 // demo 无校准
        q.predict_ok = true;                        // SizingCalculator 计算成功
        q.advisory = true;                          // ML-R2: paper 期恒 true
        q.model_as_of_ts_ns = now - 500'000'000LL;  // 模拟 feature PIT 锚 500ms
        q.fair_ci_lower = in.fair_value - 0.031;
        q.fair_ci_upper = in.fair_value + 0.026;

        return q;
    }

    // ---- risk_rejects — 已接真 (RmDebugSnapshot), 回落 Demo ----
    std::vector<RiskRejectRow> risk_rejects() const override {
        if (snap_ == nullptr) {
            return demo_.risk_rejects();
        }
        const std::vector<risk::RejectRow> raw = snap_->snapshot();
        if (raw.empty()) {
            // ring 空 (启动初期尚无拒单) → 回落 Demo 避免空屏
            return demo_.risk_rejects();
        }
        std::vector<RiskRejectRow> out;
        out.reserve(raw.size());
        for (const auto& r : raw) {
            RiskRejectRow row;
            row.reason_code = r.reason_code;
            row.market_id = r.market_id;
            row.intent_ref = r.intent_ref;
            row.side = r.side;
            row.size = r.size_usdc;
            row.price = r.price;
            row.rejected_ts_ns = r.rejected_ts_ns;
            out.push_back(std::move(row));
        }
        return out;
    }

    // ---- book — 按 token_id 查单边 (已接真), 回落 Demo ----
    BookSnapshot book(const std::string& token_id) const override {
        const auto opt = hub_.Read(token_id);
        if (!opt.has_value() || !opt->valid) {
            // hub 无数据 → 回落 Demo
            BookSnapshot fb = demo_.book(token_id);
            fb.source = "demo-fallback";
            fb.wss_state = "UNKNOWN";
            return fb;
        }
        return to_book_snapshot(token_id, *opt);
    }

    // ---- book_pair — 按 condition_id 查双 token (已接真), 回落 Demo ----
    BinaryMarketBookView book_pair(const std::string& condition_id) const override {
        const auto it = token_map_.find(condition_id);
        if (it == token_map_.end()) {
            // condition_id 不在 token_map → 回落 Demo
            BinaryMarketBookView fb = demo_.book_pair(condition_id);
            fb.token0.source = "demo-fallback";
            fb.token1.source = "demo-fallback";
            return fb;
        }

        const std::string& tok0_id = it->second.first;
        const std::string& tok1_id = it->second.second;

        const auto opt0 = hub_.Read(tok0_id);
        const auto opt1 = hub_.Read(tok1_id);

        const bool have0 = opt0.has_value() && opt0->valid;
        const bool have1 = opt1.has_value() && opt1->valid;

        if (!have0 && !have1) {
            // 双边均无数据 → 回落 Demo
            BinaryMarketBookView fb = demo_.book_pair(condition_id);
            fb.token0.source = "demo-fallback";
            fb.token1.source = "demo-fallback";
            return fb;
        }

        BinaryMarketBookView bv;
        bv.found = true;
        bv.condition_id = condition_id;

        if (have0) {
            bv.token0 = to_book_snapshot(tok0_id, *opt0);
        } else {
            bv.token0 = demo_.book(tok0_id);
            bv.token0.source = "demo-fallback";
        }
        bv.token0.condition_id = condition_id;
        bv.token0.market_id = condition_id;  // deprecated alias

        if (have1) {
            bv.token1 = to_book_snapshot(tok1_id, *opt1);
        } else {
            bv.token1 = demo_.book(tok1_id);
            bv.token1.source = "demo-fallback";
        }
        bv.token1.condition_id = condition_id;
        bv.token1.market_id = condition_id;  // deprecated alias

        // cross_spread = token0.best_ask + token1.best_ask - 1.0 (等效 vig)
        bv.cross_spread = bv.token0.best_ask + bv.token1.best_ask - 1.0;

        // ts: 取两 token 较旧 as_of (保守 staleness)
        const std::int64_t now = now_ns();
        bv.ts.event_ts_ns = std::min(bv.token0.ts.event_ts_ns, bv.token1.ts.event_ts_ns);
        bv.ts.data_source_ts_ns = std::min(bv.token0.ts.data_source_ts_ns, bv.token1.ts.data_source_ts_ns);
        bv.ts.ingestion_ts_ns = std::min(bv.token0.ts.ingestion_ts_ns, bv.token1.ts.ingestion_ts_ns);
        bv.ts.as_of_ts_ns = now;

        return bv;
    }

    // ---- data_source — "real": book/rejects/score/quote 已接真实路径, 其余 demo ----
    // score: ScoreSnapshotStore live feed (无 feed → 回落 demo)
    // quote: SizingCalculator 真实 Kelly 计算 (demo 输入, 非 hardcode)
    const char* data_source() const override { return "real"; }

private:
    const polymarket::clob_wss::OrderBookSnapshotHub& hub_;
    const risk::RmDebugSnapshot* snap_;            // nullable; nullptr → 回落 Demo
    const data::ScoreSnapshotStore* score_store_;  // nullable; nullptr → 回落 Demo
    MarketTokenMap token_map_;
    SizingConfig sizing_cfg_;    // SizingCalculator 输入参数
    risk::RiskConfig risk_cfg_;  // cap 注入源 (禁复制字面量, 老韩 §3)
    DemoStateProvider demo_;
    ExecMode mode_;

    // -----------------------------------------------------------------------
    // to_book_snapshot — OrderBookFeatures → BookSnapshot
    //
    // 映射依据 orderbook_snapshot_hub.hpp 末尾"映射说明"注释。
    // condition_id / outcome 由调用方在返回后补填 (token_map 查询结果)。
    // as_of_ts_ns 在此读取时填写 (R-20 allowed: 观测消费方读取时刻)。
    // -----------------------------------------------------------------------
    static BookSnapshot to_book_snapshot(const std::string& token_id,
                                         const polymarket::clob_wss::OrderBookFeatures& f) noexcept {
        using namespace polymarket::clob_wss;

        BookSnapshot b;
        b.found = f.valid;
        b.token_id = token_id;
        // condition_id / outcome 由调用方在返回后补填
        b.source = "polymarket";
        b.wss_state = WssConnStateName(f.wss_state);

        if (!f.valid) {
            return b;
        }

        b.best_bid = f.best_bid();
        b.best_ask = f.best_ask();
        b.microprice = nan_to_zero(f.microprice);
        b.spread = nan_to_zero(f.spread);
        b.imbalance = nan_to_zero(f.imbalance);
        b.sequence_no = f.sequence_no;
        b.gap_count = f.gap_count;

        // R-20: 4 时间戳来自上游 (禁本地 now() 替代前三个)
        b.ts.event_ts_ns = f.event_ts_ns;
        b.ts.data_source_ts_ns = f.data_source_ts_ns;
        b.ts.ingestion_ts_ns = f.ingestion_ts_ns;
        b.ts.as_of_ts_ns = now_ns();  // 观测消费方读取时刻 (R-20 allowed)

        // 深度阶梯 (kBookDepthLevels = 5, NaN 价格跳过)
        b.bids.reserve(kBookDepthLevels);
        for (std::size_t i = 0; i < kBookDepthLevels; ++i) {
            const auto& lvl = f.bids[i];
            if (std::isnan(lvl.price) || lvl.price <= 0.0) {
                break;
            }
            b.bids.push_back({lvl.price, lvl.size_usdc});
        }
        b.asks.reserve(kBookDepthLevels);
        for (std::size_t i = 0; i < kBookDepthLevels; ++i) {
            const auto& lvl = f.asks[i];
            if (std::isnan(lvl.price) || lvl.price <= 0.0) {
                break;
            }
            b.asks.push_back({lvl.price, lvl.size_usdc});
        }

        return b;
    }

    static std::int64_t now_ns() noexcept {
        using namespace std::chrono;
        return static_cast<std::int64_t>(
            duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    // NaN/inf 安全转换: 无效数值置 0 避免 JSON 非法输出
    static double nan_to_zero(double v) noexcept { return (std::isfinite(v)) ? v : 0.0; }
};

}  // namespace stcpp::debug_api
