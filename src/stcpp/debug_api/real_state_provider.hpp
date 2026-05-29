// src/stcpp/debug_api/real_state_provider.hpp — RealStateProvider (真实快照接入)
//
// Owner: 小卢 (senior-ic-pool)
// last_review: 2026-05-29
//
// 设计概述 (集成 ④ 更新 — 小肖 replay-full-flow):
//   真实接入路径:
//     1. OrderBookSnapshotHub (小冯) — per-token double-buffer, book()/book_pair()
//     2. RmDebugSnapshot (老沈)       — lock-free ring, risk_rejects()
//     3. SizingCalculator (小袁)      — Kelly/notional 真实计算, quote_params()
//     4. ScoreSnapshotStore (小段)    — live feed 比分快照, score()
//     5. LedgerSnapshotHub (小石)     — positions()/pnl_attribution() 读 hub 快照
//        (--replay 时由 ReplayFeedCoordinator 派生并 Publish; 无数据回落 Demo)
//     6. QuoteSnapshotHub (小石)      — quote_params() 读 hub 快照
//        (--replay 时由 ReplayFeedCoordinator 派生 SizingCalculator 真算; 无数据回落 Demo)
//
//   优先级: hub 有数据 → hub 快照; hub 无数据 → SizingCalculator demo 计算 → Demo fallback
//
//   R-12 合规: 全路径只读原子快照, 无持锁 > 100us
//   R-11 合规: LedgerFeatures.mode 由写端填入; response 顶层 mode 字段来此
//   R-20 合规: 4-ts 严格透传 hub 快照 (data_source_ts 来自合成链路, 非 now())
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
#include "stcpp/risk/ledger_snapshot_hub.hpp"  // LedgerSnapshotHub (集成 ④)
#include "stcpp/risk/risk_gateway.hpp"         // RiskConfig (SizingCalculator cap 注入源)
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"  // QuoteSnapshotHub (集成 ④)
#include "stcpp/sizing/sizing_calculator.hpp"   // SizingCalculator (Kelly 真实计算, ADR-042)

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
    // bankroll (USDC; demo: 100,000; 真实接入时由账本快照提供)
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
    //   hub:         已初始化的 OrderBookSnapshotHub (caller 保证生命周期长于本对象)
    //   snap:        已 attach 的 RmDebugSnapshot* (可为 nullptr; nullptr → risk_rejects 回落 Demo)
    //   score_store: ScoreSnapshotStore* (可为 nullptr; nullptr → score 回落 Demo)
    //   tokens:      condition_id → (token0_id, token1_id) (可为空; 空 → book_pair 回落 Demo)
    //   sizing_cfg:  SizingCalculator 输入参数 (demo 值, 计算路径真实)
    //   risk_cfg:    RiskConfig (cap 注入源, 禁复制字面量; 老韩 §3 强制)
    //   m:           运行模式 (build-time 锁定)
    //   ledger_hub:  LedgerSnapshotHub* (可为 nullptr; nullptr → positions/pnl 回落 Demo)
    //                --replay 时由 ReplayFeedCoordinator Publish; 无数据回落 Demo (集成 ④)
    //   quote_hub:   QuoteSnapshotHub*  (可为 nullptr; nullptr → quote_params 回落 Demo/SizingCalc)
    //                --replay 时由 ReplayFeedCoordinator Publish (集成 ④)
    // -------------------------------------------------------------------------
    explicit RealStateProvider(const polymarket::clob_wss::OrderBookSnapshotHub& hub,
                               const risk::RmDebugSnapshot* snap, const data::ScoreSnapshotStore* score_store,
                               MarketTokenMap tokens, SizingConfig sizing_cfg = {},
                               risk::RiskConfig risk_cfg = {}, ExecMode m = ExecMode::Paper,
                               const risk::LedgerSnapshotHub* ledger_hub = nullptr,
                               const sizing::QuoteSnapshotHub* quote_hub = nullptr)
        : hub_(hub),
          snap_(snap),
          score_store_(score_store),
          token_map_(std::move(tokens)),
          sizing_cfg_(sizing_cfg),
          risk_cfg_(risk_cfg),
          demo_(m),
          mode_(m),
          ledger_hub_(ledger_hub),
          quote_hub_(quote_hub) {}

    RealStateProvider(const RealStateProvider&) = delete;
    RealStateProvider& operator=(const RealStateProvider&) = delete;
    RealStateProvider(RealStateProvider&&) = delete;
    RealStateProvider& operator=(RealStateProvider&&) = delete;
    ~RealStateProvider() override = default;

    // ---- 运行模式 ----
    ExecMode mode() const override { return mode_; }

    // ---- positions — 读 LedgerSnapshotHub (集成 ④); 无数据回落 Demo ----
    // LedgerFeatures → HoldingView 映射依据 ledger_snapshot_hub.hpp 末尾注释
    // R-12: hub_.Read() 原子 acquire, 无持锁
    // R-20: as_of_ts_ns 来自 LedgerFeatures.as_of_ts_ns (上游链路)
    std::vector<HoldingView> positions() const override {
        if (ledger_hub_ == nullptr) {
            return demo_.positions();
        }
        // 遍历 token_map_ 的所有 condition_id (market_key), 从 ledger_hub_ 读快照
        std::vector<HoldingView> out;
        out.reserve(token_map_.size());
        bool any_valid = false;
        for (const auto& [cond_id, _] : token_map_) {
            const auto opt = ledger_hub_->Read(cond_id);
            if (!opt.has_value() || !opt->valid) {
                continue;
            }
            const auto& lf = *opt;
            HoldingView hv;
            hv.market_id = cond_id;
            // outcome: 用 DemoStateProvider 的 derive_outcomes 语义; 此处简化为空 (debug 可接受)
            hv.outcome = derive_outcome_label(cond_id);
            hv.net_qty = lf.net_qty;
            hv.avg_entry_price = lf.avg_entry_price;
            hv.mark_price = lf.mark_price;
            hv.pnl_realized = lf.pnl_realized;
            hv.pnl_unrealized = lf.pnl_unrealized;
            hv.as_of_ts_ns = lf.as_of_ts_ns;
            out.push_back(std::move(hv));
            any_valid = true;
        }
        if (!any_valid) {
            // hub 空 (ReplayDriver 尚未喂第一帧) → 回落 Demo
            return demo_.positions();
        }
        return out;
    }

    std::vector<PnlBucket> pnl_timeseries(std::int64_t w, std::int64_t b) const override {
        return demo_.pnl_timeseries(w, b);
    }

    // ---- pnl_attribution — 读 LedgerSnapshotHub (集成 ④); 无数据回落 Demo ----
    // 聚合所有 market_key 的 LedgerFeatures → PnlAttribution
    // R-12: 全程只读原子快照; R-20: as_of_ts_ns 取最大 (最新帧)
    PnlAttribution pnl_attribution() const override {
        if (ledger_hub_ == nullptr) {
            return demo_.pnl_attribution();
        }
        PnlAttribution attr;
        attr.as_of_ts_ns = 0;
        double gross = 0.0;
        double fee = 0.0;
        bool any_valid = false;
        for (const auto& [cond_id, _] : token_map_) {
            const auto opt = ledger_hub_->Read(cond_id);
            if (!opt.has_value() || !opt->valid) {
                continue;
            }
            const auto& lf = *opt;
            gross += lf.pnl_gross;
            fee -= lf.pnl_fee;  // fee 字段约定为正数 (已付); PnlAttribution.fee 为负 (扣减)
            attr.as_of_ts_ns = std::max(attr.as_of_ts_ns, lf.as_of_ts_ns);
            PnlPerMarket pm;
            pm.market_id = cond_id;
            pm.net_pnl = lf.pnl_net();  // pnl_gross - pnl_fee
            attr.per_market.push_back(std::move(pm));
            any_valid = true;
        }
        if (!any_valid) {
            return demo_.pnl_attribution();
        }
        attr.gross = gross;
        attr.fee = fee;
        attr.gas = 0.0;          // 无 gas 数据源
        attr.slippage = 0.0;     // 无 slippage 数据源
        attr.spread = 0.0;       // 无 spread 数据源
        attr.net = gross + fee;  // net = gross - |fee| (fee 已为负)
        return attr;
    }

    PaperGate paper_gate() const override { return demo_.paper_gate(); }

    // metrics — 委托 Demo 基础, 覆写订阅计数字段为真实值 (GAP-01/02/03)
    //   subscribed_tokens_total  = hub_.token_count()  (atomic read, R-12 合规)
    //   subscribed_markets_total = hub_.token_count() / 2  (老李 spec §2.1 双 token 规则)
    //   subscribed_user_conditions: 依赖 PolymarketCLOBSubscriber.user_condition_count()
    //     RealStateProvider 当前不持 subscriber 引用 (subscriber 是热路径写端, R-12 零反向依赖)
    //     此版本先以 hub_.token_count() / 2 作为 condition 数近似 (等同 markets 数);
    //     待 subscriber 注入接口 (v0.2) 后改读 user_condition_count() getter。
    //   wss_last_disconnect_ts_ns: 同上, 待 v0.2 subscriber 注入后填真实值; 现填 0。
    MetricsSnapshot metrics() const override {
        MetricsSnapshot snap = demo_.metrics();
        const auto tok_cnt = static_cast<std::int64_t>(hub_.token_count());
        snap.subscribed_tokens_total = tok_cnt;
        snap.subscribed_markets_total = tok_cnt / 2;  // 双 token 规则 (老李 spec §2.1)
        // subscribed_user_conditions: hub_ 无此信息; v0.2 subscriber 注入后补真实值
        snap.subscribed_user_conditions = 0;
        snap.wss_last_disconnect_ts_ns = 0;  // v0.2 subscriber 注入后填真实断连 ts
        return snap;
    }

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

    // ---- quote_params — 优先读 QuoteSnapshotHub (集成 ④); 无数据 → SizingCalculator → Demo ----
    // 优先级: QuoteSnapshotHub (--replay 时 ReplayFeedCoordinator Publish 的真算结果)
    //         → SizingCalculator demo 输入真算
    //         → Demo fallback
    // R-12: hub_.Read() 原子 acquire, 无持锁
    // R-20: as_of_ts_ns 来自 QuoteFeatures.as_of_ts_ns (上游链路)
    QuoteParams quote_params(const std::string& condition_id) const override {
        // 1. 优先从 QuoteSnapshotHub 读 (集成 ④)
        if (quote_hub_ != nullptr) {
            const auto opt = quote_hub_->Read(condition_id);
            if (opt.has_value() && opt->valid) {
                return to_quote_params(condition_id, *opt);
            }
            // hub 有注册但 valid=false (尚未 Publish) → 继续尝试 SizingCalculator
        }
        // 2. SizingCalculator demo 输入真算 (原有逻辑)
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
        // 当前敞口 (standalone demo: 0; 真实接入时由账本快照注入)
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

    // ---- data_source — "real": book/rejects/score/quote/positions/pnl 已接真实路径 ----
    // score: ScoreSnapshotStore live feed (无 feed → 回落 demo)
    // quote: QuoteSnapshotHub (--replay) → SizingCalculator (demo 输入) → Demo
    // positions/pnl: LedgerSnapshotHub (--replay) → Demo
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
    const risk::LedgerSnapshotHub* ledger_hub_{nullptr};  // nullable; nullptr → 回落 Demo (集成 ④)
    const sizing::QuoteSnapshotHub* quote_hub_{nullptr};  // nullable; nullptr → 回落 Demo (集成 ④)

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

    // -----------------------------------------------------------------------
    // to_quote_params — QuoteFeatures → QuoteParams (集成 ④)
    //
    // 映射依据 quote_snapshot_hub.hpp 末尾"映射说明"注释。
    // R-20: as_of_ts_ns 来自 QuoteFeatures.as_of_ts_ns (上游链路, 非 now())。
    // -----------------------------------------------------------------------
    static QuoteParams to_quote_params(const std::string& condition_id,
                                       const sizing::QuoteFeatures& qf) noexcept {
        QuoteParams q;
        q.found = qf.valid;
        q.market_id = condition_id;
        q.as_of_ts_ns = qf.as_of_ts_ns;

        q.fair_value = qf.fair_value;
        q.market_mid = qf.market_mid;
        q.edge_bps = qf.edge_bps;
        q.kelly_fraction = qf.kelly_fraction;
        q.suggested_notional = qf.suggested_notional;
        q.signal_strength = qf.signal_strength;

        // model_confidence → model_conf (deprecated alias, G-FREEZE-W)
        q.model_confidence = qf.model_confidence;
        q.model_conf = qf.model_confidence;

        // char[] → std::string (QuoteFeatures POD; 保证 nul 终止)
        q.model_id = std::string(qf.model_id);
        q.spec_version = std::string(qf.spec_version);

        // ModelKindTag → string
        switch (qf.model_kind) {
            case sizing::ModelKindTag::kOnnx:
                q.model_kind = "onnx";
                break;
            case sizing::ModelKindTag::kTreelite:
                q.model_kind = "treelite";
                break;
            default:
                q.model_kind = "stub";
                break;
        }
        q.model_calibrated = qf.model_calibrated;
        q.fair_ci_lower = qf.fair_ci_lower;
        q.fair_ci_upper = qf.fair_ci_upper;
        q.predict_ok = qf.predict_ok;
        q.model_as_of_ts_ns = qf.model_as_of_ts_ns;
        q.advisory = qf.advisory;
        return q;
    }

    // -----------------------------------------------------------------------
    // derive_outcome_label — condition_id → outcome label (简化版; debug 用途)
    //
    // 与 DemoStateProvider::derive_outcomes() 对齐: 提取 primary token outcome。
    // 生产接入后改读 LedgerFeatures.market_key 附带的 outcome 字段。
    // -----------------------------------------------------------------------
    static std::string derive_outcome_label(const std::string& cond_id) {
        if (cond_id.find("nba-lal-bos-ml") != std::string::npos) {
            return "LAL";
        }
        if (cond_id.find("nba-lal-bos-total") != std::string::npos) {
            return "OVER_220.5";
        }
        if (cond_id.find("nba-lal-bos-spread") != std::string::npos) {
            return "LAL_-5.5";
        }
        if (cond_id.find("epl-ars-che") != std::string::npos) {
            return "OVER_2.5";
        }
        if (cond_id.find("nfl-kc-buf") != std::string::npos) {
            return "KC_-3.5";
        }
        if (cond_id.find("mlb-nyy-bos") != std::string::npos) {
            return "NYY";
        }
        return "YES";  // 默认
    }
};

}  // namespace stcpp::debug_api
