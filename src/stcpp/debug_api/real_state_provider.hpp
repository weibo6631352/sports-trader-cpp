// src/stcpp/debug_api/real_state_provider.hpp — RealStateProvider (真实快照接入)
//
// Owner: 小卢 (senior-ic-pool)
// last_review: 2026-05-30
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
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stcpp/data/score_snapshot_store.hpp"
// (ml/feature_vector_hub + model_feature_spec includes 已砍 2026-06-05: 大模型特征健康端点删)
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/polymarket/wss/pm_wss_subscriber.hpp"  // IWssTransport (P1-2/P1-3)
#include "stcpp/risk/ledger_snapshot_hub.hpp"          // LedgerSnapshotHub
#include "stcpp/risk/risk_gateway.hpp"                 // RiskConfig (为构造参数兼容保留)
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"  // QuoteSnapshotHub

#include "src/stcpp/debug_api/state_provider.hpp"

namespace stcpp::debug_api {

// ----------------------------------------------------------------------------
// InPlayCapSecForSport — 按运动的「真·在打」时间窗上界 (覆盖率分母, 2026-06-03 老板「更真实」)
//   背景: 旧版统一 5h 窗对网球/篮球/足球太松 (这些 1.5-3h 就打完, 5h 内已结束的盘仍算 live →
//     分母虚高 → 覆盖率被低估); 对 cricket ODI (~8h) 又太紧 (live 的 ODI 被 5h 切掉)。
//   故按运动给典型最长时长: 短局制收窄 (踢出已打完), 长局制放宽 (cricket)。
//   sport 入参 = PM 联赛码 (小写, e.g. atp/wta/itf/bkbsl/crint/dota2/...); 前缀/子串归类。
[[nodiscard]] inline std::int64_t InPlayCapSecForSport(const std::string& sport) noexcept {
    auto has = [&](const char* sub) { return sport.find(sub) != std::string::npos; };
    auto pre = [&](const char* p) { return sport.rfind(p, 0) == 0; };  // 以 p 开头
    // tennis (atp/wta/itf/chal/tennis): 5 盘可达 ~3.5h
    if (pre("atp") || pre("wta") || pre("itf") || has("tennis") || has("chal"))
        return 3 * 3600 + 1800;  // 3.5h
    // cricket (crint/crict20blast/crict.../cricket): ODI ~8h (放宽, 防 live 长局被切)
    if (pre("cr") || has("cricket"))
        return 8 * 3600;
    // basketball (bk*/nba/...): ~2.5h + OT
    if (pre("bk") || has("nba") || has("basket"))
        return 3 * 3600;
    // esports (dota/lol/mlbb/cs/valorant/空串): BO3/BO5 可达 ~4h
    if (sport.empty() || has("dota") || has("lol") || has("mlbb") || has("cs") || has("valor") ||
        has("esport") || has("rl") || has("ow"))
        return 4 * 3600;
    // baseball: 加时局可达 ~4h
    if (has("mlb") || has("baseball") || has("kbo") || has("npb"))
        return 4 * 3600;
    // hockey: OT/点球 ~3.5h
    if (has("nhl") || has("hockey") || has("khl"))
        return 3 * 3600 + 1800;
    // amfootball: ~3.5h
    if (has("nfl") || has("ncaaf") || has("football"))
        return 3 * 3600 + 1800;
    // soccer (epl/liga/serie/bundes/soccer/...): 90min + 加时 + 伤停 ~3h
    if (has("soccer") || has("epl") || has("liga") || has("serie") || has("bundes") ||
        has("ligue") || has("ucl") || has("mls"))
        return 3 * 3600;
    return 4 * 3600;  // 默认 4h (未知运动, 中庸)
}

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

    // 韧性 watchdog (老郭): PaperLoop last_tick_ts_ns 心跳。观测端比对 now-last_tick 判 loop 存活。
    //   nullptr → 0 (无数据源)。loop 卡死 → 心跳停 → tick_staleness 飙升 → healthz/metrics 告警。
    const std::atomic<std::int64_t>* last_tick_ts{nullptr};

    // A4 (2026-06-02): CLOB WSS 看门狗重连计数。nullptr → 0。/metrics stcpp_wss_reconnect_total。
    //   (G-FREEZE-W 末尾加性字段, 纯加不改名)
    const std::atomic<std::uint64_t>* wss_reconnect_counter{nullptr};
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

    // (set_feature_vector_hub 已砍 2026-06-05: 大模型特征向量健康端点删)

    // 账户级现金/估值回调 (老雷 2026-06-01 凯利评审): daemon 注入 lambda (捕获 paper_loop_, 经
    //   published_account_equity() 线程安全拷贝读)。on-demand 求值 → HTTP 线程零陈旧。本头不 include
    //   paper_loop (热路径写端边界, line 49); 翻译逻辑落 daemon.cpp (该层同时依赖 paper + debug_api)。
    void set_account_snapshot_fn(std::function<AccountSnapshot()> fn) { account_fn_ = std::move(fn); }
    [[nodiscard]] AccountSnapshot account_snapshot() const override {
        if (account_fn_) return account_fn_();
        return {};  // 未注入 → has_data=false (前端灰显)
    }

    // 成交流水回调 (2026-06-04 老板「看懂买卖价」): daemon 注入 lambda (捕获 paper_loop_, 调 RecentFills(n,market))。
    void set_fills_fn(std::function<std::vector<FillView>(const std::string&)> fn) { fills_fn_ = std::move(fn); }
    [[nodiscard]] std::vector<FillView> fills(const std::string& market = "") const override {
        if (fills_fn_) return fills_fn_(market);
        return {};
    }

    // 映射状态可观测 (老雷 2026-06-01): daemon RefreshEventMapping 线程周期 push 快照。
    //   互斥保护 (写: 映射刷新线程; 读: HTTP 线程; 频率低, 短锁安全)。
    void set_mapping_status(MappingStatusReport rep) {
        std::lock_guard<std::mutex> lk(mapping_mtx_);
        mapping_snapshot_ = std::move(rep);
    }
    [[nodiscard]] MappingStatusReport mapping_status() const override {
        std::lock_guard<std::mutex> lk(mapping_mtx_);
        return mapping_snapshot_;
    }

    // feature_health — 大模型特征向量健康 (fv_hub 聚合) 已砍 2026-06-05「砍掉大模型训练功能」。
    //   接口方法保留 (StateProvider 契约), 恒返空报告 (无大模型 → 无特征向量健康可言)。
    [[nodiscard]] FeatureHealthReport feature_health() const override { return {}; }

    // ---- 运行模式 ----
    ExecMode mode() const override { return mode_; }

    // ---- positions — 读 LedgerSnapshotHub; 无数据返回空 vector ----
    // LedgerFeatures → HoldingView 映射依据 ledger_snapshot_hub.hpp 末尾注释
    // R-12: hub_.Read() 原子 acquire, 无持锁
    // R-20: as_of_ts_ns 来自 LedgerFeatures.as_of_ts_ns (上游链路)
    std::vector<HoldingView> positions() const override {
        // [mark-staleness fix 2026-06-05] 优先用 daemon 注入的 live MTM 回调 (per-token PositionLedger +
        //   当前簿, 与 /account 同源)。修旧路径两病: ① LedgerSnapshotHub.mark_price fill-gated 冻结陈旧;
        //   ② 该快照无 side → 硬编码 "YES" 误标 NO 仓。未注入时回落旧 ledger_hub_ 路径 (契约/单测兼容)。
        if (positions_fn_) return positions_fn_();
        std::lock_guard<std::mutex> lk(meta_mu_);  // R-4: 守护 token_map_ 遍历 (重发现热刷)
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

    // 净值时序回调 (2026-06-01 凯利评审 Step3 落地): daemon 注入 lambda (捕获 paper_loop, 调 equity_snapshot
    //   按 bucket 分桶)。on-demand, 经线程安全拷贝读。本头不 include paper (热路径写端边界, line 49)。
    void set_pnl_timeseries_fn(std::function<std::vector<PnlBucket>(std::int64_t, std::int64_t)> fn) {
        pnl_ts_fn_ = std::move(fn);
    }
    // [mark-staleness fix 2026-06-05] /api/v1/positions 回调: daemon 注入 lambda (捕获 paper_loop, 调
    //   positions_mtm() → per-token live MTM + YES/NO)。注入时 positions() 走它, 不再读冻结的 LedgerSnapshotHub。
    void set_positions_fn(std::function<std::vector<HoldingView>()> fn) { positions_fn_ = std::move(fn); }
    std::vector<PnlBucket> pnl_timeseries(std::int64_t window_sec, std::int64_t bucket_sec) const override {
        if (pnl_ts_fn_) return pnl_ts_fn_(window_sec, bucket_sec);
        return {};  // 未注入 → 空 (前端灰显)
    }

    // ---- pnl_attribution — 读 LedgerSnapshotHub; 无数据返回全 0 空结构 ----
    // 聚合所有 market_key 的 LedgerFeatures → PnlAttribution
    // R-12: 全程只读原子快照; R-20: as_of_ts_ns 取最大 (最新帧)
    PnlAttribution pnl_attribution() const override {
        PnlAttribution attr;
        // 2026-06-04 老板「顶部不准」根治: 顶层瀑布与【账户】同源。旧版 sum(ledger_hub.pnl_gross) 不含
        //   动态平仓已实现 (gross 只 $0.10 vs 真实已实现 −$686) → net 严重偏小 → 顶栏/瀑布显示离谱。
        //   改用 account 权威口径: gross=已实现+浮盈, fee=−已付费, net=account.net_pnl (与资金概览/顶栏一致)。
        if (account_fn_) {
            const auto a = account_fn_();
            if (a.has_data) {
                attr.gross = a.cum_realized_pnl + a.cum_unrealized_pnl;  // 费前毛 P&L
                attr.fee = -a.cum_fee_paid;                              // 扣减 (负)
                attr.net = a.net_pnl;                                    // = equity − bankroll (唯一真值)
                attr.as_of_ts_ns = a.as_of_ts_ns;
            }
        }
        attr.gas = 0.0;
        attr.slippage = 0.0;
        attr.spread = 0.0;
        // per_market 分盘明细仍读 ledger_hub (分盘归因; 顶层已由 account 校准, 不再 sum 它)。
        if (ledger_hub_ != nullptr) {
            for (const auto& [cond_id, _] : token_map_) {
                const auto opt = ledger_hub_->Read(cond_id);
                if (!opt.has_value() || !opt->valid) continue;
                PnlPerMarket pm;
                pm.market_id = cond_id;
                pm.net_pnl = opt->pnl_net();
                attr.per_market.push_back(std::move(pm));
            }
        }
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
        std::lock_guard<std::mutex> lk(meta_mu_);  // R-4: 守护 token_map_/catalog_ 遍历 (重发现热刷)
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

        // ---- A4 (2026-06-02): wss_reconnect_total (CLOB WSS 看门狗重连计数) ----
        if (hooks_.wss_reconnect_counter != nullptr) {
            snap.wss_reconnect_total =
                static_cast<std::int64_t>(hooks_.wss_reconnect_counter->load(std::memory_order_relaxed));
        }

        // ---- 韧性 watchdog: loop_thread_ 心跳停摆 (now − last_tick_ts) ----
        if (hooks_.last_tick_ts != nullptr) {
            const std::int64_t lt = hooks_.last_tick_ts->load(std::memory_order_relaxed);
            snap.loop_tick_staleness_ms =
                (lt > 0) ? static_cast<double>(now_ns() - lt) / 1.0e6 : -1.0;  // -1 = loop 未跑过
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
        // wss_user_channel: 当前无接入 → false (标注而非虚报)。
        //   注: sports_api 通道已删 (2026-06-02) —— Goalserve 比分走 HTTP REST inplay feed, 非 WSS。

        // ---- 覆盖率/识别率 metric (ADR-038 小卢 2026-05-30) ----
        // 全部从 catalog_ / token_map_ / score_store_ 只读算, 无新外部调用 (R-12)
        //
        // 1. 盘口类型识别率: 遍历 catalog_, 按 sports_market_type 分桶
        //    recognized = sports_market_type 非空且 != "unknown"
        //    unknown    = 空字符串 或 "unknown"
        {
            std::int64_t recognized = 0;
            std::int64_t unknown = 0;
            std::int64_t live = 0;  // 真·在打 → 比分匹配率真分母 (老板 2026-06-02)
            // 真·在打 = 有真实开赛时间(排 outright/期货 kickoff=0) ∧ 已开赛 ∧ 未到结算窗口(排已结束)。
            //   注: mi.live (=kickoff<=now) 太松 (含 kickoff=0 缺失 + 已结束 8h 窗口内) → 不用。
            //   这是 UI 指标 (非热路径/非数据), 用本地 now 算无 R-20 顾虑。
            const std::int64_t now_sec = std::chrono::duration_cast<std::chrono::seconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count();
            // event_id → sport (PM 联赛码) 映射 — 供 markets_live 按运动收窄时间窗 (更真实, 2026-06-03)。
            //   events_ 同在 meta_mu_ 下 (本快照已持锁读 catalog_), 同锁读安全 (R-12 只读)。
            std::unordered_map<std::string, std::string> sport_by_event;
            sport_by_event.reserve(events_.size());
            for (const auto& ev : events_)
                sport_by_event[ev.event_id] = ev.sport;
            for (const auto& [cid, mi] : catalog_) {
                const bool is_unknown = mi.sports_market_type.empty() || mi.sports_market_type == "unknown";
                if (is_unknown) {
                    ++unknown;
                } else {
                    ++recognized;
                }
                // 真·在打窗口: 已开赛 + 距开赛 ≤ 该运动典型最长时长 (InPlayCapSecForSport)。
                //   旧版统一 5h 对网球/篮球/足球太松 (已打完仍算 live → 分母虚高 → 覆盖率低估);
                //   对 cricket ODI 又太紧。按运动收窄/放宽 → 分母更贴真实在打 (老板「更真实一点」)。
                //   不用 gamma endDate (实测=结算日, 比赛后数天, 排不掉已结束)。
                std::string sp;
                if (!mi.event_id.empty()) {
                    if (auto it = sport_by_event.find(mi.event_id); it != sport_by_event.end())
                        sp = it->second;
                }
                const std::int64_t cap = InPlayCapSecForSport(sp);
                if (mi.game_start_ts_sec > 0 && mi.game_start_ts_sec <= now_sec &&
                    now_sec <= mi.game_start_ts_sec + cap) {
                    ++live;
                }
            }
            snap.market_type_recognized_total = recognized;
            snap.market_type_unknown_total = unknown;
            snap.markets_live_total = live;
        }

        // 2. 市场覆盖
        //    markets_discovered_total  = catalog_ 条目数 (gamma /events 发现并填入的市场)
        //    markets_subscribed_total  已被 tok_cnt / 2 覆盖 (订阅 hub 的双 token 口径)
        //    tokens_subscribed_total   已被 hub.token_count() 覆盖
        snap.markets_discovered_total = static_cast<std::int64_t>(catalog_.size());

        // 3. 直播员/比分匹配率: 取 EventMatcher 实际映射结果 (daemon push 的 mapping_snapshot_.matched),
        //    与 /api/v1/mapping/status 同源。
        //    2026-06-02 修真 bug: 原用 score_store_->Get(mi.event_id) —— 但 mi.event_id 是 Polymarket
        //    event_id, 而 score_store 按 Goalserve inplay_match_id 做 key → 用错 key 永远查不到 →
        //    score_matched 恒 0 (OPS 匹配率恒 0%), 即便 EventMatcher 实际匹配了 N 个。改读真映射数。
        {
            std::lock_guard<std::mutex> lk_map(mapping_mtx_);
            snap.score_matched_total = static_cast<std::int64_t>(mapping_snapshot_.matched);
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
        std::lock_guard<std::mutex> lk(meta_mu_);  // R-4: 守护 catalog_ 查询 (重发现热刷)
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
    // ScoreSnapshotStore.Get(event_id) O(1) 无锁读 (R-12 合规)
    // score_store_ 为 nullptr 或 event_id 不存在 → found=false (不回落 demo)
    EventScore score(const std::string& event_id) const override {
        if (score_store_ != nullptr) {
            const auto opt = score_store_->Get(event_id);
            if (opt.has_value()) {
                return *opt;
            }
        }
        // store 无数据 / event_id 不存在 → found=false
        EventScore miss;
        miss.found = false;
        miss.event_id = event_id;
        miss.source = "live";
        return miss;
    }

    // scores_all — 枚举 score_store 全部快照 (绕开 per-event key 不匹配; 盯盘看板用)。
    //   R-12: GetSnapshot 原子引用计数快照, 只读零热路径依赖。只返 found 的 (in-play feed 只填进行中)。
    //   2026-06-03 (老板「前端挂在比赛节点下面」): 反查 inplay_match_id → PM event_id (mapping markets +
    //   catalog), 把 EventScore.event_id 改成 PM event_id → 前端 lastEventScore[PM event_id] 挂到对应
    //   比赛节点 (EventAccordion) 显示比分/赛点/进度。查不到 PM event 的保留原 key (无节点, 前端忽略)。
    std::vector<EventScore> scores_all() const override {
        std::vector<EventScore> out;
        if (score_store_ == nullptr) return out;
        const auto snap = score_store_->GetSnapshot();
        if (!snap) return out;
        // 反查表: inplay_match_id → PM event_id (经 matched mapping row → condition → catalog.event_id)。
        std::unordered_map<std::string, std::string> imid_to_event;
        {
            MappingStatusReport rep;
            { std::lock_guard<std::mutex> lk(mapping_mtx_); rep = mapping_snapshot_; }
            std::lock_guard<std::mutex> lk(meta_mu_);
            for (const auto& row : rep.markets) {
                if (!row.matched || row.inplay_match_id.empty()) continue;
                const auto cit = catalog_.find(row.condition_id);
                if (cit != catalog_.end() && !cit->second.event_id.empty())
                    imid_to_event[row.inplay_match_id] = cit->second.event_id;
            }
        }
        out.reserve(snap->size());
        for (const auto& [key, es] : *snap) {
            if (!es.found) continue;
            EventScore e = es;
            const auto it = imid_to_event.find(key);
            if (it != imid_to_event.end()) e.event_id = it->second;  // → PM event_id (挂比赛节点)
            out.push_back(std::move(e));
        }
        return out;
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
    std::vector<EventInfo> events() const override {
        std::lock_guard<std::mutex> lk(meta_mu_);
        return events_;
    }

    // set_events — 启动注入 + R-6 周期重发现热刷 (meta_mu_ 守护: HTTP 线程读 / 重发现线程写)。
    void set_events(std::vector<EventInfo> ev) {
        std::lock_guard<std::mutex> lk(meta_mu_);
        events_ = std::move(ev);
    }
    // R-6: 周期重发现重订后刷新 token_map (staleness/positions 遍历用; meta_mu_ 守护)。
    void set_token_map(MarketTokenMap tokens) {
        std::lock_guard<std::mutex> lk(meta_mu_);
        token_map_ = std::move(tokens);
    }

    // ---- P1-1: set_market_catalog — gamma 发现结果注入 (非热路径, 启动时调用一次) ----
    // 注入后 market() 返回真实 found=true 数据，不再硬编码 false。
    // R-12: catalog_ 注入后不再写入, market() 只读 (const 方法, 无锁)。
    void set_market_catalog(MarketInfoMap catalog) {
        std::lock_guard<std::mutex> lk(meta_mu_);
        catalog_ = std::move(catalog);
    }

    // ---- P1-2/P1-3: set_live_metrics_hooks — 真实 metrics 数据源注入 ----
    // 注入后 metrics() 返回真实 uptime/rm_reject/fill/staleness/wss 值。
    // P1-3: book()/book_pair() wss_state 从 hooks_.wss_transport->IsConnected() 动态读取。
    void set_live_metrics_hooks(LiveMetricsHooks hooks) { hooks_ = hooks; }

private:
    const polymarket::clob_wss::OrderBookSnapshotHub& hub_;
    const risk::RmDebugSnapshot* snap_;            // nullable; nullptr → 空 vector
    const data::ScoreSnapshotStore* score_store_;  // nullable; nullptr → found=false
    MarketTokenMap token_map_;
    risk::RiskConfig risk_cfg_;  // 保留参数兼容 (当前实现未使用)
    ExecMode mode_;
    const risk::LedgerSnapshotHub* ledger_hub_{nullptr};  // nullable; nullptr → 空
    const sizing::QuoteSnapshotHub* quote_hub_{nullptr};  // nullable; nullptr → found=false
    // (fv_hub_ 已砍 2026-06-05: 大模型特征向量健康端点删, feature_health 恒返空)
    std::function<AccountSnapshot()> account_fn_{};       // 账户现金/估值回调 (daemon 注入); 空 → has_data=false
    std::function<std::vector<FillView>(const std::string&)> fills_fn_{};  // 成交流水回调(market过滤; daemon注入)
    std::function<std::vector<PnlBucket>(std::int64_t, std::int64_t)> pnl_ts_fn_{};  // 净值时序回调 (daemon 注入)
    std::function<std::vector<HoldingView>()> positions_fn_{};  // per-token live 持仓回调 (daemon 注入; 空→读 ledger_hub_)
    mutable std::mutex mapping_mtx_;                       // 保护 mapping_snapshot_ (低频写/读)
    MappingStatusReport mapping_snapshot_;                // daemon push 的映射快照
    // R-4 (老周 D-2): meta_mu_ 守护 token_map_/events_/catalog_ — R-6 周期重发现热刷, HTTP 线程读。
    //   原启动注入后只读, 重发现一上线即运行期写 → 不锁会陈旧/race。低频, 短锁安全。
    mutable std::mutex meta_mu_;
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

        // 大模型 provenance (model_id/kind/spec/confidence/calibrated/fair_ci/advisory) 已砍 2026-06-05。
        q.predict_ok = qf.predict_ok;             // baseline fair 有效
        q.model_as_of_ts_ns = qf.model_as_of_ts_ns;  // feature PIT 锚
        // 调试可观测: fair 来源分解 (sharp 共识 / de-vig / 领先 / 新鲜度)。
        //   语义明确 (探针反馈): sharp ∈ (0,1) = 已映射 Goalserve + 有 bet365 odds;
        //   -1 = 未映射 / 无 odds (g_bm_inplay_fair 默认 0 不可与真 0 混淆)。
        q.sharp_fair = (qf.g_bm_inplay_fair > 0.0 && qf.g_bm_inplay_fair < 1.0) ? qf.g_bm_inplay_fair : -1.0;
        q.devig_ok = qf.devig_ok;
        q.g_time_x_lead = qf.g_time_x_lead;
        q.joint_as_of_ts_ns = qf.joint_as_of_ts_ns;
        q.data_source_ts_ns = qf.data_source_ts_ns;  // 真实赔率新鲜度 (订单簿 WSS 版本时刻)
        q.sharp_data_source_ts_ns = qf.sharp_data_source_ts_ns;  // GS sharp 赔率新鲜度 (inplay 赔率版本时刻)
        // sharp fair 时序 (老板 2026-06-05 line movement): velocity/收敛率/抖动/样本数 透传 (观测先行)。
        q.sharp_velocity = qf.g_sharp_velocity;
        q.sharp_conv_rate = qf.g_sharp_conv_rate;
        q.sharp_vol = qf.g_sharp_vol;
        q.sharp_samples = qf.g_sharp_samples;
        // 持仓管理 Stage 2 sizing 乘子透传 (观测)。
        q.lifecycle_mult = qf.g_lifecycle_mult;
        q.clv_mult = qf.g_clv_mult;
        q.rolling_clv_mean = qf.g_rolling_clv_mean;
        q.rolling_clv_n = qf.g_rolling_clv_n;
        q.dd_mult = qf.g_dd_mult;
        q.corr_mult = qf.g_corr_mult;
        return q;
    }
};

}  // namespace stcpp::debug_api
