// src/stcpp/paper/paper_loop.cpp — PaperLoop 实现
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-30
//
// 红线:
//   R-11: paper 不污染真账本 — VirtualFill.mode_tag==0 + PaperSigner.audit_wal_kind=PaperAudit
//   R-12: 独立线程, hub_.Read() 只读原子, Publish noexcept
//   R-20: 4 ts 全链路透传 (data_source_ts_ns 来自 hub 快照, 禁 now() 替代)
//
// ToS: 仅 paper 虚拟成交, 不向 Polymarket CLOB 下单.

#include "stcpp/paper/paper_loop.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::paper {

// ---------------------------------------------------------------------------
// ctor
// ---------------------------------------------------------------------------

PaperLoop::PaperLoop(const polymarket::clob_wss::OrderBookSnapshotHub& hub, risk::RiskGateway& rm,
                     risk::PositionLedger& position_ledger, risk::LedgerSnapshotHub& ledger_hub,
                     sizing::QuoteSnapshotHub& quote_hub, risk::RmDebugSnapshot* rm_snap,
                     const pricing::IFairValueModel& fv_model,
                     std::unordered_map<std::string, std::pair<std::string, std::string>> token_map,
                     PaperLoopConfig cfg) noexcept
    : hub_(hub),
      rm_(rm),
      position_ledger_(position_ledger),
      ledger_hub_(ledger_hub),
      quote_hub_(quote_hub),
      rm_snap_(rm_snap),
      fv_estimator_(fv_model),
      nonce_provider_(0),
      gas_estimator_(),
      confirm_watcher_(0xC0FFEE'D00DULL),
      psigner_(&nonce_provider_, &gas_estimator_, &confirm_watcher_),
      matcher_(0xBEEFCAFEULL),
      token_map_(std::move(token_map)),
      cfg_(std::move(cfg)) {}

// ---------------------------------------------------------------------------
// dtor — 保证线程已 join
// ---------------------------------------------------------------------------

PaperLoop::~PaperLoop() {
    Stop();
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

void PaperLoop::Start() {
    if (running_.load(std::memory_order_acquire)) {
        return;  // 已启动, 幂等
    }

    // 若配置要求, 把 RM 从 SAFE_MODE 切到 RUNNING
    if (cfg_.set_rm_running) {
        rm_.set_state(risk::RmState::RUNNING);
        rm_.set_bankroll(static_cast<std::int64_t>(cfg_.bankroll_usdc));
    }

    stop_requested_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);

    // 启动 jthread (C++20; 析构自动 request_stop + join)
    loop_thread_ = std::jthread([this](std::stop_token st) {
        RunLoop(st);
        running_.store(false, std::memory_order_release);
    });

    std::fprintf(stderr,
                 "[paper_loop] 启动 paper 交易循环 (tick=%lldms, tokens=%zu, "
                 "bankroll=%.0f pUSD)\n",
                 static_cast<long long>(cfg_.tick_interval_ms), token_map_.size(), cfg_.bankroll_usdc);
}

void PaperLoop::Stop() noexcept {
    if (!running_.load(std::memory_order_acquire) && !loop_thread_.joinable()) {
        return;  // 未启动或已停止
    }

    stop_requested_.store(true, std::memory_order_release);
    if (loop_thread_.joinable()) {
        loop_thread_.request_stop();
        loop_thread_.join();
    }
    std::fprintf(stderr,
                 "[paper_loop] 已停止. ticks=%llu approved=%llu fills=%llu "
                 "ledger_publishes=%llu\n",
                 static_cast<unsigned long long>(stats_.ticks_total.load()),
                 static_cast<unsigned long long>(stats_.orders_approved.load()),
                 static_cast<unsigned long long>(stats_.fills_completed.load()),
                 static_cast<unsigned long long>(stats_.ledger_publishes.load()));
}

// ---------------------------------------------------------------------------
// RunLoop — 主循环 (loop_thread_ 内执行)
// ---------------------------------------------------------------------------

void PaperLoop::RunLoop(std::stop_token st) {
    using namespace std::chrono_literals;

    while (!st.stop_requested() && !stop_requested_.load(std::memory_order_acquire)) {
        TickAll();
        stats_.ticks_total.fetch_add(1, std::memory_order_relaxed);

        // 间隔 sleep (R-12: 不 spinlock; sleep 期间响应 stop_token)
        const auto interval = std::chrono::milliseconds(cfg_.tick_interval_ms);
        const auto deadline = std::chrono::steady_clock::now() + interval;
        while (std::chrono::steady_clock::now() < deadline) {
            if (st.stop_requested() || stop_requested_.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

// ---------------------------------------------------------------------------
// TickAll — 遍历所有 condition_id, 对 token0 (YES side) 做一次 paper 尝试
// ---------------------------------------------------------------------------

void PaperLoop::TickAll() {
    for (const auto& [cond_id, tok_pair] : token_map_) {
        const std::string& token0 = tok_pair.first;  // YES token

        // 1. 读真实 hub 快照 (R-12: 原子只读, 无锁)
        const auto opt = hub_.Read(token0);
        if (!opt.has_value() || !opt->valid) {
            stats_.hub_reads_empty.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        const auto& feat = *opt;

        // 2. L1 价格校验 (best_ask 必须有效)
        const double best_ask = feat.best_ask();
        const double best_bid = feat.best_bid();
        if (!std::isfinite(best_ask) || best_ask <= 0.0 || best_ask >= 1.0) {
            continue;
        }
        if (!std::isfinite(best_bid) || best_bid <= 0.0) {
            continue;
        }

        TickOne(cond_id, token0, feat);
    }
}

// ---------------------------------------------------------------------------
// TickOne — 对单个 token 执行一次完整 paper 交易流程
// ---------------------------------------------------------------------------

void PaperLoop::TickOne(const std::string& condition_id, const std::string& token_id,
                        const polymarket::clob_wss::OrderBookFeatures& feat) {
    using namespace stcpp::data::feature_store;

    // orders_attempted: 进入 TickOne 即计数 (book 有效, 开始尝试流程)
    stats_.orders_attempted.fetch_add(1, std::memory_order_relaxed);

    const double best_ask = feat.best_ask();
    const double best_bid = feat.best_bid();
    const double microprice = std::isfinite(feat.microprice) ? feat.microprice : (best_bid + best_ask) * 0.5;

    // ---- Step 2: FairValueEstimator ----------------------------------------
    // M1: 无 Goalserve game_row → 退化纯订单簿先验 (score_diff=0, time_frac=0)
    // FairValueEstimator 处理 nullptr book_row 时 kappa=0 (纯先验), 不会 NaN.
    FeatureStoreGameRow game_row{};
    game_row.time_status = stcpp::data::goalserve::TimeStatus::NotStarted;
    // 4 ts (R-20 守护: game_row 4 ts 用 hub 快照 ts 近似, 真实接入 Goalserve 后替换)
    game_row.event_ts_ns = feat.event_ts_ns;
    game_row.data_source_ts_ns = feat.data_source_ts_ns;
    game_row.ingestion_ts_ns = feat.ingestion_ts_ns;
    game_row.as_of_ts_ns = feat.as_of_ts_ns;

    FeatureStoreBookRow book_row{};
    // token_side = "YES" (FairValueEstimator extract_microprice_ 检查此字段)
    book_row.token_side = "YES";
    // L1 bid/ask
    book_row.bid_price[0] = best_bid;
    book_row.bid_size_usdc[0] = feat.best_bid_size();
    book_row.ask_price[0] = best_ask;
    book_row.ask_size_usdc[0] = feat.best_ask_size();
    // 微观结构
    book_row.microprice = microprice;
    book_row.imbalance = std::isfinite(feat.imbalance) ? feat.imbalance : 0.0;
    book_row.mid = std::isfinite(feat.mid) ? feat.mid : (best_bid + best_ask) * 0.5;
    book_row.tick_size = 0.01;
    // 4 ts 从 hub 快照透传 (R-20)
    book_row.event_ts_ns = feat.event_ts_ns;
    book_row.data_source_ts_ns = feat.data_source_ts_ns;
    book_row.ingestion_ts_ns = feat.ingestion_ts_ns;
    book_row.as_of_ts_ns = feat.as_of_ts_ns;

    const pricing::FairValueResult fv_result = fv_estimator_.estimate(game_row, &book_row);

    if (!fv_result.valid) {
        // fail-closed: FairValueEstimator 返回无效 → 跳过本 token
        return;
    }

    const double p_fair = fv_result.p_yes();
    const double mark_price = microprice;  // 真实 mark (来自 hub)

    // ---- Step 3: CI 下界 + SizingCalculator --------------------------------
    // edge_ci_lower = (p_fair - best_ask) - z * sqrt(p*(1-p)/n) (§10.3)
    const double edge_ci_lower = ComputeEdgeCiLower(p_fair, best_ask, cfg_.n_effective, cfg_.z_90);

    // best_ask_size 做 book depth 近似 (L1 USDC depth)
    const double book_depth_l1 = std::isfinite(feat.best_ask_size()) && feat.best_ask_size() > 0.0
                                     ? feat.best_ask_size()
                                     : 1000.0;  // fallback 1000 pUSD

    sizing::SizingInput sz_in;
    sz_in.fair_value = p_fair;
    sz_in.price = best_ask;  // buy YES at ask
    sz_in.edge_ci_lower = edge_ci_lower;
    sz_in.edge_bps = std::abs(p_fair - best_ask) * 10'000.0;
    sz_in.bankroll_usdc = cfg_.bankroll_usdc;
    sz_in.fill_rate = 0.65;    // 保守固定 (M1)
    sz_in.slippage_bps = 8.0;  // 保守固定 (M1)
    sz_in.buy_yes = (p_fair > best_ask);
    sz_in.current_token_exposure_usdc = 0.0;
    sz_in.current_condition_exposure_usdc = 0.0;

    risk::RiskConfig rm_cfg{};  // 默认 cap 配置 (per_order=10K, bankroll=100K)
    const sizing::SizingOutput sizing_out = sizing::SizingCalculator::compute(rm_cfg, sz_in);

    // ---- Step 4: QuoteSnapshotHub::Publish ---------------------------------
    // 无论下单与否, 发布 quote 快照 (供 /api/v1/quote 端点显示真实估值)
    PublishQuoteSnapshot(condition_id, fv_result, sizing_out, mark_price, edge_ci_lower, feat);
    stats_.quote_publishes.fetch_add(1, std::memory_order_relaxed);

    // CI gating: sizing_out.valid=false 或 net_ci_edge <= 0 → 不下单 (fail-closed)
    if (!sizing_out.valid || sizing_out.suggested_notional <= 0.0) {
        return;
    }

    // ---- Step 5: 构造 OrderIntent v0.6 ------------------------------------
    // R-20: as_of_ts_ns = 信号评估时刻 (>= ingestion_ts_ns)
    const std::int64_t as_of_now = NowNs();

    // 校验 4 ts 链 (调试保护; release build 仍执行但不 abort)
    // event_ts_ns <= data_source_ts_ns <= ingestion_ts_ns <= as_of_now
    if (feat.event_ts_ns <= 0 || feat.data_source_ts_ns < feat.event_ts_ns ||
        feat.ingestion_ts_ns < feat.data_source_ts_ns || as_of_now < feat.ingestion_ts_ns) {
        // ts 链违规: 跳过 (不构造 intent, 不送 RM)
        return;
    }

    const std::uint64_t intent_id = ++intent_seq_;

    risk::OrderIntent intent;
    // R-20 4 ts
    intent.event_ts_ns = feat.event_ts_ns;
    intent.data_source_ts_ns = feat.data_source_ts_ns;
    intent.ingestion_ts_ns = feat.ingestion_ts_ns;
    intent.as_of_ts_ns = as_of_now;

    // 市场标识
    intent.condition_id = condition_id;
    intent.token_id = token_id;
    intent.outcome = strategy::Outcome::Yes;
    intent.side = strategy::Side::Buy;

    // 业务 ID (去重: intent_seq_ 单调)
    intent.strategy_id = cfg_.strategy_id;
    intent.signal_id = cfg_.strategy_id + "-" + std::to_string(intent_id);
    intent.feature_snapshot_id = "paper-m1-no-snapshot";

    // 定价 (买 YES at ask)
    intent.price = best_ask;

    // 仓位大小: SizingCalculator 建议值, 转 micro pUSD
    // clamp 到合理范围 (demo 场景 <= 10 pUSD, 避免 RM cap 触发)
    const double notional_usdc = std::min(sizing_out.suggested_notional, 10.0);  // demo 上限 10 pUSD
    intent.size_pUSD_micro = static_cast<std::int64_t>(notional_usdc * 1'000'000.0);
    if (intent.size_pUSD_micro <= 0) {
        intent.size_pUSD_micro = 1'000'000LL;  // 最小 1 pUSD
    }

    // book context (R8.4 freshness)
    intent.book_depth_l1_usdc = book_depth_l1;
    intent.book_snapshot_ts_ns = feat.ingestion_ts_ns;
    intent.tick_size = 0.01;

    // V2 EIP-712 字段 (timestamp_ms = 当前毫秒, spec-10 必须 != 0)
    intent.timestamp_ms = as_of_now / 1'000'000LL;
    intent.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
    intent.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";
    intent.is_close = false;

    // ---- Step 6: RiskGateway::evaluate ------------------------------------
    const risk::RiskDecision rd = rm_.evaluate(intent);

    if (rd.is_rejected()) {
        stats_.orders_rejected.fetch_add(1, std::memory_order_relaxed);

        // push_reject 到 rm_snap (可选)
        if (rm_snap_ != nullptr) {
            risk::RejectRow rr = risk::build_reject_row(rd, intent);
            rm_snap_->push_reject(rr);
        }
        return;
    }

    // APPROVED
    stats_.orders_approved.fetch_add(1, std::memory_order_relaxed);

    // ---- Step 7: PaperSigner::Sign ----------------------------------------
    signer::SignRequest sign_req;
    sign_req.audit_id = rd.audit_id;
    sign_req.intent_id = intent_id;
    sign_req.condition_id = intent.condition_id;
    sign_req.token_id = intent.token_id;
    sign_req.price = intent.price;
    sign_req.size_pUSD_micro = intent.size_pUSD_micro;
    sign_req.side = static_cast<std::uint8_t>(intent.side);
    sign_req.timestamp_ms = intent.timestamp_ms;
    sign_req.metadata = intent.metadata;
    sign_req.builder = intent.builder;
    // R-20 4 ts 透传
    sign_req.event_ts_ns = intent.event_ts_ns;
    sign_req.data_source_ts_ns = intent.data_source_ts_ns;
    sign_req.ingestion_ts_ns = intent.ingestion_ts_ns;
    sign_req.as_of_ts_ns = intent.as_of_ts_ns;

    const signer::SignResponse sign_resp = psigner_.Sign(sign_req);

    if (sign_resp.error != signer::SignerError::Ok) {
        // Sign 失败 (PIT 违规等) → fail-closed, 不撮合
        return;
    }

    // ---- Step 7b: VirtualMatcher::MatchWithBook (Mode A++) -----------------
    // 使用 Mode A++ (Bernoulli) 因为 FeatureStoreBookRow 已经包含了 L1 信息
    // 构造 VirtualOrder 从 SignRequest
    execution::VirtualOrder vord;
    vord.audit_id = sign_req.audit_id;
    vord.intent_id = sign_req.intent_id;
    vord.market_id = sign_req.condition_id;
    vord.outcome = "YES";
    vord.size_usdc = static_cast<double>(sign_req.size_pUSD_micro) / 1'000'000.0;
    vord.quote_price = sign_req.price;
    vord.book_depth_l1_usdc = book_depth_l1;
    vord.tick_size = 0.01;
    // R-20 4 ts 透传
    vord.event_ts_ns = sign_req.event_ts_ns;
    vord.data_source_ts_ns = sign_req.data_source_ts_ns;
    vord.ingestion_ts_ns = sign_req.ingestion_ts_ns;
    vord.as_of_ts_ns = sign_req.as_of_ts_ns;
    vord.wall_now_ns = NowNs();

    const execution::VirtualFill fill = matcher_.Match(vord);

    // R-11: VirtualFill.mode_tag 必须为 0 (paper 标记; VirtualMatcher 内部硬填)
    assert(fill.mode_tag == 0u);  // 防御性校验 (debug build)

    if (fill.reject != execution::MatchReject::Ok || fill.fill_size_usdc <= 0.0) {
        stats_.fills_missed.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    stats_.fills_completed.fetch_add(1, std::memory_order_relaxed);

    // ---- Step 8: PositionLedger::apply_fill --------------------------------
    // R-11: position_ledger_ 是 paper 专用账本 (调用方构建时物理隔离)
    position_ledger_.apply_fill(condition_id, token_id, strategy::Outcome::Yes, fill);

    // ---- Step 8b: LedgerSnapshotHub::Publish (positions/pnl 可见) ----------
    PublishLedgerSnapshot(condition_id, fill, mark_price, feat);
    stats_.ledger_publishes.fetch_add(1, std::memory_order_relaxed);

    std::fprintf(stderr,
                 "[paper_loop] FILL cond=%.24s... tok=%.16s... "
                 "fill_sz=%.4f fill_px=%.4f p_fair=%.4f edge=%.1fbps\n",
                 condition_id.c_str(), token_id.c_str(), fill.fill_size_usdc, fill.fill_price, p_fair,
                 sz_in.edge_bps);
}

// ---------------------------------------------------------------------------
// ComputeEdgeCiLower — CI 下界 (§10.3)
//
// edge_ci_lower = (p_fair - p_ask) - z * sqrt(p_fair * (1 - p_fair) / n_eff)
// 数值稳定: fail-closed 返 -1.0 (禁止下单)
// ---------------------------------------------------------------------------

/*static*/
double PaperLoop::ComputeEdgeCiLower(double p_fair, double p_ask, int n_eff, double z) noexcept {
    if (!std::isfinite(p_fair) || !std::isfinite(p_ask) || n_eff <= 0) {
        return -1.0;  // fail-closed
    }
    const double raw_edge = p_fair - p_ask;
    // sigma_approx = sqrt(p_fair * (1 - p_fair) / n_eff)
    const double var = p_fair * (1.0 - p_fair);
    if (!std::isfinite(var) || var < 0.0) {
        return -1.0;
    }
    const double sigma = std::sqrt(var / static_cast<double>(n_eff));
    const double ci_lower = raw_edge - z * sigma;
    if (!std::isfinite(ci_lower)) {
        return -1.0;
    }
    // clamp [-1, 1] (数值边界)
    return ci_lower < -1.0 ? -1.0 : (ci_lower > 1.0 ? 1.0 : ci_lower);
}

// ---------------------------------------------------------------------------
// NowNs — CLOCK_REALTIME epoch ns (pit.hpp 内联函数)
// ---------------------------------------------------------------------------

/*static*/
std::int64_t PaperLoop::NowNs() noexcept {
    return infra::wal::pit::NowRealtimeNs();
}

// ---------------------------------------------------------------------------
// PublishLedgerSnapshot — apply_fill 成功后更新 LedgerSnapshotHub
//
// R-20: 4 ts 来自 fill + feat (上游链路透传, 禁 now() 替代 data_source_ts_ns)
// R-11: mode = kPaper (标记 paper 模式)
// ---------------------------------------------------------------------------

void PaperLoop::PublishLedgerSnapshot(const std::string& condition_id, const execution::VirtualFill& fill,
                                      double mark_price,
                                      const polymarket::clob_wss::OrderBookFeatures& feat) noexcept {
    // 从 PositionLedger 读最新仓位快照
    const auto pos_opt = position_ledger_.get_all_positions();

    // 找此 condition_id 对应仓位 (简化: 仅报告第一个匹配 YES token)
    double net_qty = 0.0;
    double avg_entry = 0.0;
    std::int64_t last_update = fill.as_of_ts_ns;

    for (const auto& pv : pos_opt) {
        if (pv.condition_id == condition_id) {
            // size_usdc (signed micro) → qty in pUSD
            net_qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
            avg_entry = pv.avg_entry_price;
            last_update = pv.last_update_ts;
            break;
        }
    }

    // 未实现 PnL = (mark - avg_entry) * net_qty
    const double pnl_unrealized = (mark_price - avg_entry) * net_qty;
    // 已实现 PnL: 简化 M1 只跟 fill.fill_size_usdc × (fill.fill_price - best_ask)
    // 真实 realized 在平仓时产生; M1 买入阶段 realized = 0
    const double pnl_realized = 0.0;
    const double pnl_fee =
        fill.fill_size_usdc * sizing::kSportsTakerFeeRate * fill.fill_price * (1.0 - fill.fill_price);
    const double pnl_gross = pnl_realized + pnl_unrealized;

    risk::LedgerFeatures lf{};
    // R-20: 4 ts 透传 (data_source_ts_ns 来自 feat, 禁本地 now() 替代)
    lf.event_ts_ns = feat.event_ts_ns;
    lf.data_source_ts_ns = feat.data_source_ts_ns;
    lf.ingestion_ts_ns = feat.ingestion_ts_ns;
    lf.as_of_ts_ns = (last_update > feat.ingestion_ts_ns) ? last_update : NowNs();
    // 仓位
    lf.net_qty = net_qty;
    lf.avg_entry_price = avg_entry;
    lf.mark_price = mark_price;
    // PnL
    lf.pnl_realized = pnl_realized;
    lf.pnl_unrealized = pnl_unrealized;
    lf.pnl_fee = pnl_fee;
    lf.pnl_gross = pnl_gross;
    // R-11: paper 模式标记
    lf.mode = risk::ExecutionModeTag::kPaper;
    lf.valid = true;

    ledger_hub_.Publish(condition_id, lf);
}

// ---------------------------------------------------------------------------
// PublishQuoteSnapshot — SizingCalculator 完成后更新 QuoteSnapshotHub
//
// R-20: 4 ts 来自 feat (上游链路)
// ---------------------------------------------------------------------------

void PaperLoop::PublishQuoteSnapshot(const std::string& condition_id,
                                     const pricing::FairValueResult& fv_result,
                                     const sizing::SizingOutput& sizing_out, double mark_price,
                                     double edge_ci_lower,
                                     const polymarket::clob_wss::OrderBookFeatures& feat) noexcept {
    sizing::QuoteFeatures qf{};
    // R-20: 4 ts 透传 (来自 hub 快照)
    qf.event_ts_ns = feat.event_ts_ns;
    qf.data_source_ts_ns = feat.data_source_ts_ns;
    qf.ingestion_ts_ns = feat.ingestion_ts_ns;
    qf.as_of_ts_ns = NowNs();  // 快照发布时刻 (R-20 allowed)

    // 核心量化字段
    qf.fair_value = fv_result.p_yes();
    qf.market_mid = mark_price;
    qf.edge_bps = sizing_out.valid ? sizing_out.net_ci_edge * 10'000.0 : 0.0;
    qf.kelly_fraction = sizing_out.valid ? sizing_out.kelly_fractional : 0.0;
    qf.suggested_notional = sizing_out.valid ? sizing_out.suggested_notional : 0.0;
    qf.signal_strength = edge_ci_lower > 0.0 ? std::min(edge_ci_lower * 10.0, 1.0) : 0.0;

    // ML provenance (M1 stub 标记; 非真实 ML 模型)
    qf.model_kind = sizing::ModelKindTag::kStub;
    // model_id: "paper-fv-baseline" (NUL terminated)
    std::strncpy(qf.model_id, "paper-fv-baseline", sizeof(qf.model_id) - 1);
    qf.model_id[sizeof(qf.model_id) - 1] = '\0';
    std::strncpy(qf.spec_version, "m1-paper-v0.1", sizeof(qf.spec_version) - 1);
    qf.spec_version[sizeof(qf.spec_version) - 1] = '\0';

    qf.model_confidence = 0.0;                    // M1 stub: 无置信度
    qf.fair_ci_lower = fv_result.p_yes() - 0.05;  // ±5% 近似 (M1)
    qf.fair_ci_upper = fv_result.p_yes() + 0.05;
    qf.predict_ok = fv_result.valid;
    qf.advisory = true;  // ML-R2: paper 期恒 true
    qf.model_calibrated = false;
    qf.model_as_of_ts_ns = feat.ingestion_ts_ns;

    qf.valid = fv_result.valid;

    quote_hub_.Publish(condition_id, qf);
}

}  // namespace stcpp::paper
