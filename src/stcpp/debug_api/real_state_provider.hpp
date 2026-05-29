// src/stcpp/debug_api/real_state_provider.hpp — RealStateProvider (真实快照接入)
//
// Owner: 小卢 (senior-ic-pool)
// last_review: 2026-05-29
//
// 设计概述:
//   将两个已合并的真实只读快照接到观测 API:
//     1. OrderBookSnapshotHub (小冯) — per-token double-buffer, book()/book_pair()
//     2. RmDebugSnapshot (老沈)       — lock-free ring, risk_rejects()
//
//   其余方法 (positions/pnl/gate/metrics/market/score/quote/status) 委托 DemoStateProvider,
//   data_source() 返回 "mixed" 标记哪些已接真、哪些仍 demo。
//
// 优雅降级 (R-12 / R-20):
//   - hub_.Read(token_id) 返回 nullopt / valid=false → 回落 DemoStateProvider.book()
//   - snap_ 为 nullptr → risk_rejects() 回落 DemoStateProvider.risk_rejects()
//   - 任何路径均不崩溃
//
// R-12 合规:
//   - book() / book_pair() / risk_rejects() 全部只读 atomic/double-buffer 快照
//   - 无持锁 > 100us; 无热路径反向依赖
//   - #include 仅标准库 + hub + snapshot 头 (不引 RM/signer/exec)
//
// R-20 合规:
//   - OrderBookFeatures 4 时间戳来自上游 WssEvent (禁本地 now() 替代 data_source_ts)
//   - as_of_ts_ns 在 Read() 返回后由 ToBookSnapshot() 在读取时填写 (R-20 allowed)
//   - RejectRow.rejected_ts_ns 来自 decision.decision_ts_ns (R-20 compliant)
//
// condition_id → token 映射:
//   生产阶段由调用方 (main) 在 Attach() 之前注入 MarketTokenMap。
//   map 为 string→pair<string,string> (condition_id → {token0_id, token1_id})。
//   如果 map 中无对应条目, book_pair 回落 DemoStateProvider。
//
// 注意: 本头文件【不得】#include 任何热路径写端模块头 (RM evaluate / signer / exec)。
//       只读 hub/snapshot 头 + 标准库可以。

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

#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"

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
// RealStateProvider — 真实快照接入 (book/rejects 已接真, 其余委托 Demo)
// ============================================================================
class RealStateProvider final : public StateProvider {
public:
    // -------------------------------------------------------------------------
    // 构造
    //   hub:    已初始化的 OrderBookSnapshotHub (caller 保证生命周期长于本对象)
    //   snap:   已 attach 的 RmDebugSnapshot* (可为 nullptr; nullptr → risk_rejects 回落 Demo)
    //   tokens: condition_id → (token0_id, token1_id) (可为空 map; 空 → book_pair 回落 Demo)
    //   m:      运行模式 (build-time 锁定)
    // -------------------------------------------------------------------------
    explicit RealStateProvider(const polymarket::clob_wss::OrderBookSnapshotHub& hub,
                               const risk::RmDebugSnapshot* snap, MarketTokenMap tokens,
                               ExecMode m = ExecMode::Paper)
        : hub_(hub), snap_(snap), token_map_(std::move(tokens)), demo_(m), mode_(m) {}

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

    EventScore score(const std::string& event_id) const override { return demo_.score(event_id); }

    QuoteParams quote_params(const std::string& condition_id) const override {
        return demo_.quote_params(condition_id);
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

    // ---- data_source — "mixed": book/rejects 已接真, 其余 demo ----
    const char* data_source() const override { return "mixed"; }

private:
    const polymarket::clob_wss::OrderBookSnapshotHub& hub_;
    const risk::RmDebugSnapshot* snap_;  // nullable; nullptr → 回落 Demo
    MarketTokenMap token_map_;
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
