// src/stcpp/debug_api/real_state_provider.hpp — RealStateProvider (真实快照接入)
//
// Owner: 小卢 (senior-ic-pool)
// last_review: 2026-05-30
// 小段 2026-05-30: 接入 ScoreEventMapper (Goalserve inplay ↔ Polymarket event 映射层)
//   score(event_id) 先走 mapper_.Resolve(event_id) → inplay_match_id
//   再查 ScoreSnapshotStore.Get(inplay_match_id) 取真实比分
//   解决 B3: score 端点 100% miss 问题
//
// 设计概述 (去 demo 重构 — 老板: 只有 live):
//   真实接入路径:
//     1. OrderBookSnapshotHub (小冯) — per-token double-buffer, book()/book_pair()
//     2. RmDebugSnapshot (老沈)       — lock-free ring, risk_rejects()
//     3. ScoreSnapshotStore (小段)    — live feed 比分快照, score()
//     4. LedgerSnapshotHub (小石)     — positions()/pnl_attribution() 读 hub 快照
//        (由 PaperLoop 周期 Publish; 无数据返回空)
//     5. QuoteSnapshotHub (小石)      — quote_params() 读 hub 快照
//        (由 PaperLoop 周期 Publish; 无数据返回 found=false)
//     6. MarketInfoMap (P1-1 修复)   — gamma 发现时填入真实 MarketInfo catalog
//        market() 查 catalog 返回 found=true 真实数据 (非硬编码 false)
//     7. LiveMetricsHooks (P1-2 修复) — 接真实 uptime/rm_reject/fill/staleness/wss
//        metrics() 填充真实值 (非全 0)
//     8. ScoreEventMapper (小段 score-mapping) — Goalserve inplay ↔ Polymarket event 映射
//        score() 先 mapper.Resolve(pm_event_id) → inplay_match_id → store.Get()
//
//   无数据语义 (彻底去 demo, 老板: 只有 live):
//     - hub/store 无数据 → 返回结构合法空值 (不是 demo 假数据)
//     - positions()         → 空 vector
//     - pnl_attribution()   → PnlAttribution{} (全 0, per_market 空)
//     - quote_params(cid)   → found=false 空 QuoteParams
//     - book(token)/book_pair(cid) → found=false (不回落 demo)
//     - score(eid)          → found=false (store 有数据时正常返回)
//     - risk_rejects()      → 空 vector
//     - data_source()       → "live" 恒定
//
//   P1-1: market() 查 MarketInfoMap (set_market_catalog 注入)
//     gamma 发现时填充 condition_id → MarketInfo; found=true 返回真实元信息
//   P1-2: metrics() 接真实值 (LiveMetricsHooks)
//     uptime_sec     = steady_clock::now() - start_tp_ (启动时刻注入)
//     rm_reject_total = snap_->count() (RmDebugSnapshot 写入总次数)
//     fill_total      = *fill_counter_ (PaperLoop stats.fills_completed atomic)
//     max_staleness_ms = 遍历 token_map_ → hub_.Read → now - event_ts_ns
//     wss_clob_connected = wss_transport_->IsConnected() (IWssTransport 接口)
//   P1-3: book()/book_pair() wss_state 从 wss_transport_->IsConnected() 动态注入
//     覆盖快照中过时的 wss_state 字符串, 与 /status 报告的 wss_connected 保持一致
//
//   R-12 合规: 全路径只读原子快照, 无持锁 > 100us
//   R-11 合规: LedgerFeatures.mode 由写端填入; response 顶层 mode 字段来此
//   R-20 合规: 4-ts 严格透传 hub 快照 (data_source_ts 来自合成链路, 非 now())
//
// score() 真实路径 (ScoreSnapshotStore):
//   store_.Get(event_id) → EventScore (live feed 数据)
//   无数据 (store 为空 / event_id 不存在) → found=false (不回落 demo)
//
// 注意: 本头文件【不得】#include 任何热路径写端模块头 (RM evaluate / signer / exec)。
//   IWssTransport 为纯接口头 (pm_wss_subscriber.hpp), 不含热路径逻辑, 合规引入。

#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stcpp/data/score_event_mapper.hpp"
#include "stcpp/data/score_snapshot_store.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"  // IWssTransport (P1-2/P1-3)
#include "stcpp/risk/ledger_snapshot_hub.hpp"          // LedgerSnapshotHub
#include "stcpp/risk/risk_gateway.hpp"                 // RiskConfig (为构造参数兼容保留)
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"  // QuoteSnapshotHub

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
// MarketInfoMap — condition_id → MarketInfo 目录 (P1-1 修复)
//
// gamma /events 发现时由 main 填充后通过 set_market_catalog() 注入。
// market() 查此目录，found=true 返回真实元信息。
// ============================================================================
using MarketInfoMap = std::unordered_map<std::string, MarketInfo>;

// ============================================================================
// LiveMetricsHooks — 轻量 metrics 数据源钩子 (P1-2 修复)
//
// 由 main 填充，通过 set_live_metrics_hooks() 注入 RealStateProvider。
// 所有指针可为 nullptr（对应指标降级为 0）。
//
// 设计约束 (R-12):
//   - wss_transport: 只读 IsConnected() (原子 load, < 1us)
//   - fill_counter:  原子 load (< 1us)
//   - start_tp:      值拷贝 (steady_clock::time_point, 8 bytes, 栈上)
//   - 无持锁, 无反向依赖热路径写端
// ============================================================================
struct LiveMetricsHooks {
    // 进程启动时刻 (steady_clock; 构造 HttpServer / RealStateProvider 时记录)
    // uptime_sec = steady_clock::now() - start_tp
    std::chrono::steady_clock::time_point start_tp{std::chrono::steady_clock::now()};

    // CLOB WSS transport (P1-2 wss_clob_connected + P1-3 wss_state 动态注入)
    // nullptr → wss_clob_connected=false, wss_state="UNKNOWN"
    const polymarket::wss::IWssTransport* wss_transport{nullptr};

    // PaperLoop fills_completed (原子计数器, 只读)
    // nullptr → fill_total=0 (标注无数据源而非虚报)
    const std::atomic<std::uint64_t>* fill_counter{nullptr};
};

// ============================================================================
// RealStateProvider — 真实快照接入 (彻底去 demo, 老板: 只有 live)
//
// 无数据时返回结构合法空值, 不使用任何 demo 假数据。
// ============================================================================
class RealStateProvider final : public StateProvider {
public:
    // -------------------------------------------------------------------------
    // 构造
    //   hub:         已初始化的 OrderBookSnapshotHub (caller 保证生命周期长于本对象)
    //   snap:        已 attach 的 RmDebugSnapshot* (可为 nullptr; nullptr → 空 vector)
    //   score_store: ScoreSnapshotStore* (可为 nullptr; nullptr → found=false)
    //   tokens:      condition_id → (token0_id, token1_id) (可为空; 空 → found=false)
    //   risk_cfg:    RiskConfig (保留参数兼容; 当前实现未使用)
    //   m:           运行模式 (build-time 锁定)
    //   ledger_hub:  LedgerSnapshotHub* (可为 nullptr; nullptr → 空 positions/pnl)
    //                由 PaperLoop Publish; 无数据返回空
    //   quote_hub:   QuoteSnapshotHub*  (可为 nullptr; nullptr → found=false)
    //                由 PaperLoop Publish
    // -------------------------------------------------------------------------
    explicit RealStateProvider(const polymarket::clob_wss::OrderBookSnapshotHub& hub,
                               const risk::RmDebugSnapshot* snap, const data::ScoreSnapshotStore* score_store,
                               MarketTokenMap tokens, risk::RiskConfig risk_cfg = {},
                               ExecMode m = ExecMode::Paper,
                               const risk::LedgerSnapshotHub* ledger_hub = nullptr,
                               const sizing::QuoteSnapshotHub* quote_hub = nullptr)
        : hub_(hub),
          snap_(snap),
          score_store_(score_store),
          token_map_(std::move(tokens)),
          risk_cfg_(risk_cfg),
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

    // ---- positions — 读 LedgerSnapshotHub; 无数据返回空 vector ----
    // LedgerFeatures → HoldingView 映射依据 ledger_snapshot_hub.hpp 末尾注释
    // R-12: hub_.Read() 原子 acquire, 无持锁
    // R-20: as_of_ts_ns 来自 LedgerFeatures.as_of_ts_ns (上游链路)
    std::vector<HoldingView> positions() const override {
        if (ledger_hub_ == nullptr) {
            return {};  // 无数据源 → 空
        }
        std::vector<HoldingView> out;
        out.reserve(token_map_.size());
        for (const auto& [cond_id, _] : token_map_) {
            const auto opt = ledger_hub_->Read(cond_id);
            if (!opt.has_value() || !opt->valid) {
                continue;
            }
            const auto& lf = *opt;
            HoldingView hv;
            hv.market_id = cond_id;
            hv.outcome = "YES";  // paper loop 真实成交; 后续由 LedgerFeatures 扩展 outcome 字段
            hv.net_qty = lf.net_qty;
            hv.avg_entry_price = lf.avg_entry_price;
            hv.mark_price = lf.mark_price;
            hv.pnl_realized = lf.pnl_realized;
            hv.pnl_unrealized = lf.pnl_unrealized;
            hv.as_of_ts_ns = lf.as_of_ts_ns;
            out.push_back(std::move(hv));
        }
        // hub 空 (PaperLoop 尚未 Publish 第一帧) → 空 vector (非 demo)
        return out;
    }

    // pnl_timeseries: 无真实时序数据源 → 空 vector (前端灰显)
    std::vector<PnlBucket> pnl_timeseries(std::int64_t /*w*/, std::int64_t /*b*/) const override {
        return {};
    }

    // ---- pnl_attribution — 读 LedgerSnapshotHub; 无数据返回全 0 空结构 ----
    // 聚合所有 market_key 的 LedgerFeatures → PnlAttribution
    // R-12: 全程只读原子快照; R-20: as_of_ts_ns 取最大 (最新帧)
    PnlAttribution pnl_attribution() const override {
        if (ledger_hub_ == nullptr) {
            return {};  // 无数据源 → 全 0 空结构
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
            return {};  // hub 空 → 全 0 空结构 (非 demo)
        }
        attr.gross = gross;
        attr.fee = fee;
        attr.gas = 0.0;          // 无 gas 数据源
        attr.slippage = 0.0;     // 无 slippage 数据源
        attr.spread = 0.0;       // 无 spread 数据源
        attr.net = gross + fee;  // net = gross - |fee| (fee 已为负)
        return attr;
    }

    // paper_gate: 无真实门禁数据源 → 空结构 (has_data=false, 前端灰显)
    PaperGate paper_gate() const override { return {}; }

    // metrics — 接真实数据源 (P1-2 修复; 拒绝全 0 虚报)
    //
    //   subscribed_tokens_total   = hub_.token_count()  (atomic read, R-12)
    //   subscribed_markets_total  = hub_.token_count() / 2 (双 token 规则)
    //   uptime_sec                = steady_clock::now() - hooks_.start_tp (真实启动时长)
    //   rm_reject_total           = snap_->count()  (RmDebugSnapshot 写入总次数, atomic)
    //   fill_total                = *hooks_.fill_counter (PaperLoop fills_completed, atomic)
    //   max_staleness_ms          = 遍历 token_map_ → hub_.Read → (now - event_ts_ns) / 1e6
    //   wss_clob_connected        = hooks_.wss_transport->IsConnected() (原子 bool, < 1us)
    //
    //   无数据源 (指针 nullptr) → 该指标标注 0 (明确无数据, 非虚报)
    //   R-12: 全程原子读/值运算, 无持锁 > 100us
    MetricsSnapshot metrics() const override {
        MetricsSnapshot snap;

        // ---- 订阅计数 (已有逻辑) ----
        const auto tok_cnt = static_cast<std::int64_t>(hub_.token_count());
        snap.subscribed_tokens_total = tok_cnt;
        snap.subscribed_markets_total = tok_cnt / 2;
        snap.subscribed_user_conditions = 0;
        snap.wss_last_disconnect_ts_ns = 0;

        // ---- P1-2: uptime_sec (真实启动时长) ----
        {
            using namespace std::chrono;
            snap.uptime_sec = static_cast<std::int64_t>(
                duration_cast<seconds>(steady_clock::now() - hooks_.start_tp).count());
        }

        // ---- P1-2: rm_reject_total (RmDebugSnapshot 总写入次数) ----
        if (snap_ != nullptr) {
            snap.rm_reject_total = static_cast<std::int64_t>(snap_->count());
        }

        // ---- P1-2: fill_total (PaperLoop fills_completed 原子计数) ----
        if (hooks_.fill_counter != nullptr) {
            snap.fill_total = static_cast<std::int64_t>(hooks_.fill_counter->load(std::memory_order_relaxed));
        }

        // ---- P1-2: max_staleness_ms (遍历 token_map_ 取最大 staleness) ----
        {
            const std::int64_t now = now_ns();
            double max_stale_ms = 0.0;
            for (const auto& [cond_id, tok_pair] : token_map_) {
                for (const auto& tid : {tok_pair.first, tok_pair.second}) {
                    const auto opt = hub_.Read(tid);
                    if (!opt.has_value() || !opt->valid) {
                        continue;
                    }
                    const std::int64_t event_ts = opt->event_ts_ns;
                    if (event_ts > 0 && now > event_ts) {
                        const double stale_ms = static_cast<double>(now - event_ts) / 1'000'000.0;
                        if (stale_ms > max_stale_ms) {
                            max_stale_ms = stale_ms;
                        }
                    }
                }
            }
            snap.max_staleness_ms = max_stale_ms;
        }

        // ---- P1-2/P1-3: wss_clob_connected (从 IWssTransport 动态读取) ----
        if (hooks_.wss_transport != nullptr) {
            snap.wss_clob_connected = hooks_.wss_transport->IsConnected();
        }
        // wss_sports_api / wss_user_channel: 当前无接入 → false (标注而非虚报)

        // ---- 小段 score-mapping 统计 (G-FREEZE-W 只增, 2026-05-30) ----
        // ScoreEventMapper.LastStats() O(1) mutex 持锁 < 1us (R-12 合规)
        if (score_mapper_ != nullptr) {
            const data::MatchStats ms = score_mapper_->LastStats();
            snap.score_matched_total = ms.matched_count;
            snap.score_attempted_total = ms.total_attempted;
            snap.score_match_rate = ms.match_rate();
        }

        return snap;
    }

    // market — P1-1 修复: 查 catalog_ (gamma 发现注入), found=true 返回真实元信息
    //
    //   catalog_ 由 set_market_catalog() 在启动时注入 (gamma /events 发现结果)
    //   命中 → found=true + 真实 tick_size/fee_rate/accepting_orders/tokens 等字段
    //   未命中 (catalog_ 空 或 condition_id 不在 catalog) → found=false (合法空值)
    //
    //   R-12: catalog_ 注入后只读 (unordered_map::find O(1) 无锁)
    MarketInfo market(const std::string& condition_id) const override {
        const auto it = catalog_.find(condition_id);
        if (it != catalog_.end()) {
            return it->second;  // found=true, 真实 gamma 发现数据
        }
        // catalog 未命中 → found=false (catalog 未注入 或 该 condition_id 未发现)
        MarketInfo mi;
        mi.found = false;
        mi.condition_id = condition_id;
        mi.market_id = condition_id;  // deprecated alias
        mi.source = "polymarket";
        return mi;
    }

    // ---- score — 读 ScoreSnapshotStore (live feed); 无数据 → found=false ----
    //
    // 小段 score-mapping 接入 (2026-05-30):
    //   Step 1: score_mapper_.Resolve(pm_event_id) → inplay_match_id
    //           ScoreEventMapper 维护 Polymarket event_id → Goalserve inplay_match_id 映射
    //           精确路径 (gameId) 优先, 否则队名+时间 fuzzy match
    //   Step 2: ScoreSnapshotStore.Get(inplay_match_id) → EventScore
    //
    // 回落逻辑:
    //   mapper 为 nullptr (未注入) → 直接用 event_id 查 store (旧行为, 兼容测试)
    //   mapper 无映射 / store 无数据 → found=false
    //
    // R-12 合规: mapper.Resolve O(1) mutex < 1us; store.Get O(1) mutex < 5ns
    // R-20 合规: EventScore.ts 4ts 来自 ScoreSnapshotStore (Goalserve payload 透传)
    EventScore score(const std::string& event_id) const override {
        if (score_store_ == nullptr) {
            EventScore miss;
            miss.found = false;
            miss.event_id = event_id;
            miss.source = "live";
            return miss;
        }

        // 确定查 store 用的 key (inplay_match_id)
        std::string store_key = event_id;  // fallback: 直查 (mapper 未注入时)
        if (score_mapper_ != nullptr) {
            const std::string inplay_id = score_mapper_->Resolve(event_id);
            if (!inplay_id.empty()) {
                store_key = inplay_id;  // 映射命中 → 用 Goalserve inplay_match_id
            }
            // 映射 miss → store_key 保持 event_id (大概率仍 miss, 但保证逻辑正确)
        }

        const auto opt = score_store_->Get(store_key);
        if (opt.has_value()) {
            // 把 pm_event_id 写回 EventScore.event_id (保证上层 ID 对齐)
            EventScore es = *opt;
            es.event_id = event_id;
            return es;
        }

        // store 无数据 → found=false
        EventScore miss;
        miss.found = false;
        miss.event_id = event_id;
        miss.source = "live";
        return miss;
    }

    // ---- quote_params — 读 QuoteSnapshotHub (PaperLoop Publish); 无数据 → found=false ----
    // R-12: hub_.Read() 原子 acquire, 无持锁
    // R-20: as_of_ts_ns 来自 QuoteFeatures.as_of_ts_ns (上游链路)
    // 无数据时不使用 SizingCalculator + demo 输入计算假值 (老板: 只有 live)
    QuoteParams quote_params(const std::string& condition_id) const override {
        if (quote_hub_ != nullptr) {
            const auto opt = quote_hub_->Read(condition_id);
            if (opt.has_value() && opt->valid) {
                return to_quote_params(condition_id, *opt);
            }
        }
        // hub 无数据 → found=false 空结构 (非 demo)
        QuoteParams q;
        q.found = false;
        q.market_id = condition_id;
        q.advisory = true;      // ML-R2: paper 期恒 true
        q.model_kind = "stub";  // 无模型接入
        q.predict_ok = false;
        return q;
    }

    // ---- risk_rejects — 读 RmDebugSnapshot ring; 无数据 → 空 vector ----
    std::vector<RiskRejectRow> risk_rejects() const override {
        if (snap_ == nullptr) {
            return {};  // 无 snapshot 接入 → 空
        }
        const std::vector<risk::RejectRow> raw = snap_->snapshot();
        if (raw.empty()) {
            return {};  // ring 空 (启动初期尚无拒单) → 空 vector (非 demo)
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

    // ---- book — 按 token_id 查单边 (真实 OrderBookSnapshotHub); 无数据 → found=false ----
    // live 真实 book 有数据时正常返回; hub 无数据 → found=false (不回落 demo)
    // P1-3: wss_state 从 hooks_.wss_transport 动态读取当前连接状态 (非快照旧值)
    BookSnapshot book(const std::string& token_id) const override {
        const bool wss_conn = (hooks_.wss_transport != nullptr) && hooks_.wss_transport->IsConnected();
        const auto opt = hub_.Read(token_id);
        if (!opt.has_value() || !opt->valid) {
            // hub 无数据 → found=false (非 demo)
            BookSnapshot fb;
            fb.found = false;
            fb.token_id = token_id;
            fb.source = "live";
            fb.wss_state = wss_conn ? "CONNECTED" : "DISCONNECTED";
            return fb;
        }
        return to_book_snapshot(token_id, *opt, wss_conn);
    }

    // ---- book_pair — 按 condition_id 查双 token (真实); 无数据 → found=false ----
    // P1-3: wss_state 从 hooks_.wss_transport 动态读取当前连接状态 (非快照旧值)
    BinaryMarketBookView book_pair(const std::string& condition_id) const override {
        const bool wss_conn = (hooks_.wss_transport != nullptr) && hooks_.wss_transport->IsConnected();
        const std::string wss_state_str = wss_conn ? "CONNECTED" : "DISCONNECTED";

        const auto it = token_map_.find(condition_id);
        if (it == token_map_.end()) {
            // condition_id 不在 token_map → found=false (不回落 demo)
            BinaryMarketBookView fb;
            fb.found = false;
            fb.condition_id = condition_id;
            fb.token0.found = false;
            fb.token0.source = "live";
            fb.token0.wss_state = wss_state_str;
            fb.token1.found = false;
            fb.token1.source = "live";
            fb.token1.wss_state = wss_state_str;
            return fb;
        }

        const std::string& tok0_id = it->second.first;
        const std::string& tok1_id = it->second.second;

        const auto opt0 = hub_.Read(tok0_id);
        const auto opt1 = hub_.Read(tok1_id);

        const bool have0 = opt0.has_value() && opt0->valid;
        const bool have1 = opt1.has_value() && opt1->valid;

        if (!have0 && !have1) {
            // 双边均无数据 → found=false (不回落 demo)
            BinaryMarketBookView fb;
            fb.found = false;
            fb.condition_id = condition_id;
            fb.token0.found = false;
            fb.token0.token_id = tok0_id;
            fb.token0.source = "live";
            fb.token0.wss_state = wss_state_str;
            fb.token1.found = false;
            fb.token1.token_id = tok1_id;
            fb.token1.source = "live";
            fb.token1.wss_state = wss_state_str;
            return fb;
        }

        BinaryMarketBookView bv;
        bv.found = true;
        bv.condition_id = condition_id;

        if (have0) {
            bv.token0 = to_book_snapshot(tok0_id, *opt0, wss_conn);
        } else {
            bv.token0.found = false;
            bv.token0.token_id = tok0_id;
            bv.token0.source = "live";
            bv.token0.wss_state = wss_state_str;
        }
        bv.token0.condition_id = condition_id;
        bv.token0.market_id = condition_id;  // deprecated alias

        if (have1) {
            bv.token1 = to_book_snapshot(tok1_id, *opt1, wss_conn);
        } else {
            bv.token1.found = false;
            bv.token1.token_id = tok1_id;
            bv.token1.source = "live";
            bv.token1.wss_state = wss_state_str;
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

    // ---- data_source — 恒 "live" (老板: 只有 live) ----
    const char* data_source() const override { return "live"; }

    // ---- events (小冯 schema append, G-FREEZE-W 只增, 2026-05-29) ----
    // 返回 live 模式从 gamma /events 发现的活跃体育 event 列表。
    // 由 main 在启动时调用 set_events() 注入; 之后只读。
    // R-12: 只读 value copy, 无锁 (events_ 在 set_events 注入后不再写入)。
    std::vector<EventInfo> events() const override { return events_; }

    // set_events — 由 main --live 路径在启动时注入 (非热路径, 启动时调用一次)
    void set_events(std::vector<EventInfo> ev) { events_ = std::move(ev); }

    // ---- P1-1: set_market_catalog — gamma 发现结果注入 (非热路径, 启动时调用一次) ----
    // 注入后 market() 返回真实 found=true 数据，不再硬编码 false。
    // R-12: catalog_ 注入后不再写入, market() 只读 (const 方法, 无锁)。
    void set_market_catalog(MarketInfoMap catalog) { catalog_ = std::move(catalog); }

    // ---- P1-2/P1-3: set_live_metrics_hooks — 真实 metrics 数据源注入 ----
    // 注入后 metrics() 返回真实 uptime/rm_reject/fill/staleness/wss 值。
    // P1-3: book()/book_pair() wss_state 从 hooks_.wss_transport->IsConnected() 动态读取。
    void set_live_metrics_hooks(LiveMetricsHooks hooks) { hooks_ = hooks; }

    // ---- 小段 score-mapping: set_score_mapper — ScoreEventMapper 注入 ----
    // 注入后 score(pm_event_id) 先走映射层查 inplay_match_id 再查 ScoreSnapshotStore.
    // 非热路径, 启动时注入一次; 采集线程周期调用 mapper.Refresh() 更新映射表.
    // nullptr → 降级为直接用 event_id 查 store (旧行为, 无 panic)
    // R-12: mapper.Resolve() O(1) mutex < 1us, 读线程安全
    void set_score_mapper(const data::ScoreEventMapper* mapper) noexcept {
        score_mapper_ = mapper;
    }

private:
    const polymarket::clob_wss::OrderBookSnapshotHub& hub_;
    const risk::RmDebugSnapshot* snap_;            // nullable; nullptr → 空 vector
    const data::ScoreSnapshotStore* score_store_;  // nullable; nullptr → found=false
    // 小段 score-mapping: ScoreEventMapper (nullable; nullptr → 旧行为直查 store)
    const data::ScoreEventMapper* score_mapper_{nullptr};
    MarketTokenMap token_map_;
    risk::RiskConfig risk_cfg_;  // 保留参数兼容 (当前实现未使用)
    ExecMode mode_;
    const risk::LedgerSnapshotHub* ledger_hub_{nullptr};  // nullable; nullptr → 空
    const sizing::QuoteSnapshotHub* quote_hub_{nullptr};  // nullable; nullptr → found=false
    // events_: live 模式从 gamma /events 发现的活跃体育 event 列表
    // 由 main set_events() 注入; 之后只读 (R-12 无锁 const 方法读安全)
    std::vector<EventInfo> events_;
    // P1-1: MarketInfo catalog (gamma 发现注入, 只读)
    MarketInfoMap catalog_;
    // P1-2/P1-3: live metrics hooks (metrics 数据源注入, 只读)
    LiveMetricsHooks hooks_;

    // -----------------------------------------------------------------------
    // to_book_snapshot — OrderBookFeatures → BookSnapshot
    //
    // 映射依据 orderbook_snapshot_hub.hpp 末尾"映射说明"注释。
    // condition_id / outcome 由调用方在返回后补填 (token_map 查询结果)。
    // as_of_ts_ns 在此读取时填写 (R-20 allowed: 观测消费方读取时刻)。
    //
    // P1-3 修复: wss_connected 参数 (当前 IWssTransport::IsConnected() 值)
    //   覆盖快照中过时的 f.wss_state (快照写入时刻的状态, 可能已过期)
    //   wss_connected=true  → "CONNECTED"
    //   wss_connected=false → "DISCONNECTED"
    //   这样 book endpoint 的 wss_state 与 /status 的 wss_connected 保持一致。
    // -----------------------------------------------------------------------
    static BookSnapshot to_book_snapshot(const std::string& token_id,
                                         const polymarket::clob_wss::OrderBookFeatures& f,
                                         bool wss_connected) noexcept {
        using namespace polymarket::clob_wss;

        BookSnapshot b;
        b.found = f.valid;
        b.token_id = token_id;
        // condition_id / outcome 由调用方在返回后补填
        b.source = "polymarket";
        // P1-3: 用当前连接状态覆盖快照中的过时 wss_state
        b.wss_state = wss_connected ? "CONNECTED" : "DISCONNECTED";

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
    // to_quote_params — QuoteFeatures → QuoteParams
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
};

}  // namespace stcpp::debug_api
