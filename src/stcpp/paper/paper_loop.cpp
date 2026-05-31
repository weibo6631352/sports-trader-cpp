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
//
// P0 整改 (2026-05-30, 小肖, dogfood-remediation):
//   P0-1 拒单去重: risk_gateway.cpp::evaluate() 内 reject_here() lambda 已经通过
//         g_rm_debug_snapshot 全局指针调用 push_reject 一次。paper_loop 原来在
//         RM 返回 REJECTED 后又手动调用 rm_snap_->push_reject 一次, 造成每个
//         reject 被写入两次 (128 唯一 reject 各出现 2 次, 纳秒级 rejected_ts 完全
//         相同可排除随机重复). 修法: 删除 paper_loop 侧的冗余 push_reject; RM 侧
//         已唯一地负责写 ring, paper_loop 仅计数 orders_rejected.
//         验证: /api/v1/risk/rejects 应返回 128 条 (不再 256 条); count() == 128.
//
//   P0-3 fake fair gate: 无真实 Goalserve game_row 时 (time_status==NotStarted,
//         即 M1 stub 路径), FairValueEstimator 退化为 prior=0.5 + 固定 kappa=0.20
//         混合 microprice, 对低价 outright (Spain mid=0.169) 强拉到 fair=0.434,
//         凭空产生 edge_bps=1076 + suggested_notional=32 — 假阳性诱导下注.
//         修法: PublishQuoteSnapshot 新增 bool has_real_fair 参数; M1 stub 路径
//         (time_status==NotStarted) 时, quote 中 edge_bps/kelly_fraction/
//         suggested_notional 强制为 0, predict_ok=false, model_calibrated=false,
//         signal_strength=0. fair_value 字段仍输出 (供观察), 但加 predict_ok=false
//         语义标记"不可决策". TickOne 在 sizing 完成后, 若 has_real_fair=false 则
//         强制 suggested_notional=0 → 不进入 Step 5 构造 intent. 宁可空不可假.
//
//   P0-4 advisory gate: advisory=true 市场 (ML-R2: paper 期所有市场恒 advisory)
//         过去仍走 Step 5-6 构造 intent 进 RiskGateway::evaluate, 依赖 RM 以
//         INVALID_INTENT 兜底拒. RM 松动 / 配置变化即真下单 = 红线. 修法: Step 4
//         (quote publish) 之后立即检查 advisory 标志, advisory=true → 直接 return,
//         不构造 intent, 不进 RM. RM 不再作 advisory 防线; paper_loop 主动 gate.
//         验证: /api/v1/risk/rejects 中 advisory 市场不应再出现 INVALID_INTENT.

#include "stcpp/paper/paper_loop.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>

#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/score_snapshot_store.hpp"  // A1: ScoreSnapshotStore::Get(inplay_match_id)
#include "stcpp/execution/execution_mode.hpp"   // A2 红线1: kCompiledMode 运行期 mode 断言
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/strategy/edge_ci.hpp"  // 单一 ComputeEdgeCiLower (回测-实盘共用)
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::paper {

namespace {

// A1: EventScore.status 字符串 → TimeStatus (MapStatus 的逆; 见 inplay_score_parser.cpp:645).
//   "inplay"/"halftime" → InPlay (live, has_real_fair=true); "final" → Ended (terminal);
//   "pregame"/未知 → NotStarted (stub, fail-closed: 不交易非 live 局).
[[nodiscard]] stcpp::data::goalserve::TimeStatus MapEventScoreStatus(const std::string& s) noexcept {
    using stcpp::data::goalserve::TimeStatus;
    if (s == "inplay" || s == "halftime") {
        return TimeStatus::InPlay;
    }
    if (s == "final") {
        return TimeStatus::Ended;
    }
    return TimeStatus::NotStarted;  // pregame / 未知 → fail-closed
}

}  // namespace

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
      cfg_(std::move(cfg)) {
    (void)rm_snap_;  // 只写不读字段: 抑制 clang -Wunused-private-field (跨 gcc/clang 可移植)
    // G-1: 默认注入 VirtualExecutor (包 matcher_, 行为逐位不变)。live 注入留待开闸后 (老韩签字)。
    executor_ = std::make_unique<execution::VirtualExecutor>(matcher_);
}

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

    // R-11/R-7 (老韩 A2 红线1): 运行期 mode 交叉断言. advisory gate 解封 (产生 paper intent)
    //   仅许 build-time paper mode. 防 advisory_markets_no_intent=false 误带进 live/backtest binary.
    //   build-time 锁 + 此运行期交叉校验双保险; 不一致 → abort (R-7 立场: 绝不放行).
    if (!cfg_.advisory_markets_no_intent &&
        stcpp::execution::kCompiledMode != stcpp::execution::ExecutionMode::Paper) {
        std::fprintf(stderr,
                     "[paper_loop] FATAL (R-11/R-7): advisory_markets_no_intent=false (解封 paper 成交) "
                     "仅许 paper mode; kCompiledMode=%s. abort.\n",
                     std::string(stcpp::execution::ToString(stcpp::execution::kCompiledMode)).c_str());
        std::abort();
    }

    // 若配置要求, 把 RM 从 SAFE_MODE 切到 RUNNING
    if (cfg_.set_rm_running) {
        rm_.set_state(risk::RmState::RUNNING);
        // 单位对齐 (A2 修): RM bankroll_usdc_ 与 size_pUSD_micro 同为 micro pUSD;
        //   cfg_.bankroll_usdc 是 pUSD (Kelly sizing 用), 喂 RM 须 × 1e6 转 micro.
        rm_.set_bankroll(static_cast<std::int64_t>(cfg_.bankroll_usdc * 1'000'000.0));
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

    bool feed_liveness_checked = false;  // A4: 首个 tick 后一次性自检

    while (!st.stop_requested() && !stop_requested_.load(std::memory_order_acquire)) {
        TickAll();
        stats_.ticks_total.fetch_add(1, std::memory_order_relaxed);

        // A4 (老韩 spec §2.6.3): 首个完整 tick 后跑一次 RM feed-liveness 自检。此时 daemon 已喂完
        //   一轮 (bankroll@Start + exposure/freshness@tick); 仍 ever_fed=false 的红线 = paper daemon
        //   根本没接通的喂数管道 = 风控纸面化, 必须喊出来 (防「假已活」)。非热路径 (一次性, 循环外语义)。
        if (!feed_liveness_checked) {
            feed_liveness_checked = true;
            auto const rows = rm_.feed_liveness_report();
            int never_fed = 0;
            for (auto const& r : rows) {
                if (!r.ever_fed) {
                    std::fprintf(stderr,
                                 "[paper_loop] RM feed-liveness: 红线 '%.*s' NEVER FED — "
                                 "paper daemon 未接通该红线喂数管道 (纸面化, 默认值静默放行风险)\n",
                                 static_cast<int>(r.key.size()), r.key.data());
                    ++never_fed;
                }
            }
            std::fprintf(stderr, "[paper_loop] RM feed-liveness 自检: %d/%zu 红线从未被喂\n", never_fed,
                         rows.size());
        }

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
// TickAll — 遍历所有 condition, 双边读 (YES book + NO book) 组 BinaryMarketSnapshot 进决策
//   (老板原则 C3: 决策带整盘口; 老周架构: 决策线程栈上组装, 零锁; R-12 不触碰)。
// ---------------------------------------------------------------------------

void PaperLoop::TickAll() {
    // A4 (老板「他们相对都是最近刷新的就行」): tick 入口冻结一次比分快照 + 映射, 整轮全子盘口共享同版本。
    //   消除 read-skew: 否则同 event 的 moneyline/spread 各自 Get(), 采集线程中途 swap → 看不同比分版本。
    //   GetSnapshot()/LoadEventMap() 都是只读 RCU 单次 load (不碰 R-12); shared_ptr 持有保活整 tick。
    tick_event_map_ = LoadEventMap();
    tick_score_snap_ = (score_store_ != nullptr) ? score_store_->GetSnapshot() : nullptr;

    for (const auto& [cond_id, tok_pair] : token_map_) {
        const std::string& yes_tok = tok_pair.first;  // YES token
        const std::string& no_tok = tok_pair.second;  // NO token

        // 双边读 (R-12: 各 token 独立 atomic 只读, 无锁; WSS 单边刷新天然映射 per-token slot)。
        //   SideView.present = hub 命中 + valid。完整 OrderBookFeatures 进决策 (不再只取 NO mid 标量)。
        BinaryMarketSnapshot mkt;
        mkt.condition_id = cond_id;
        mkt.yes_token_id = yes_tok;
        mkt.no_token_id = no_tok;
        // 统一数据树: 带上父级引用 (event_id / neg_risk; 决策+模型用, 兄弟经 event_id 导航)。
        if (const ParentRef* pr = ParentRefFor(cond_id); pr != nullptr) {
            mkt.event_id = pr->event_id;
            mkt.neg_risk_market_id = pr->neg_risk_market_id;
        }
        if (const auto yo = hub_.Read(yes_tok); yo.has_value() && yo->valid) {
            mkt.yes.present = true;
            mkt.yes.book = *yo;
        }
        if (!no_tok.empty()) {
            if (const auto no = hub_.Read(no_tok); no.has_value() && no->valid) {
                mkt.no.present = true;
                mkt.no.book = *no;
            }
        }
        // 价格有效性门 + 选边后退化 fail-closed 全在 TickOne (按被交易边判, 支持 C4 反向桩测试)。
        TickOne(mkt);
    }
}

// ---------------------------------------------------------------------------
// TickOne — 对单个 token 执行一次完整 paper 交易流程
//
// P0-3: has_real_fair 标志贯穿 Step 2→4→5:
//   true  = 有真实 Goalserve game_row (比分/时钟驱动的先验), fair 可信, 允许产生 intent.
//   false = 无真实 Goalserve (M1 stub: time_status==NotStarted), fair 是 stub 伪值,
//           quote 发布时清零 edge/kelly/notional/signal, 不构造 intent.
//
// P0-4: advisory gate 在 Step 4 之后立即检查:
//   advisory=true → return, 不进 RM, 不产生 intent.
// ---------------------------------------------------------------------------

// SelectSide — 选边 (买 YES / 买 NO / 不交易)。
//   M1 桩: 恒返 {Yes, Buy} → 与改造前行为逐位等价 (老郭 C4 回归门禁验)。
//   Phase B (老韩 RM checklist B-1..B-8 绿后): 真双边选边 — 各边算 fair/edge_ci/Kelly f*,
//     选 f* 大者 (小袁微观选边 + 小梁 Kelly); 含买 NO (token_id=NO token, outcome=No)。
//   M2: 开放 sell-to-open 空头 (side=Sell, 老韩 condition cap signed-sum 语义重裁 — C1)。
DecisionSide PaperLoop::SelectSide(double p_fair_yes, double p_market_devig) const noexcept {
    // de-vig 锚定 (小梁 spec §1-2): raw_edge_yes 与 raw_edge_no 精确互为相反数 → 不可能两边同正。
    //   选被低估边: raw_edge_yes >= 0 → YES 模型价 > 市场共识 = YES 低估 → 买 YES;
    //               raw_edge_yes < 0  → NO 低估 → 买 NO。下游 sizing/CI gate 定是否真够 edge 下单。
    const bool is_yes = (p_fair_yes - p_market_devig) >= 0.0;
    return DecisionSide{is_yes ? TradedSide::Yes : TradedSide::No, strategy::Side::Buy, 0.0};
}

void PaperLoop::TickOne(const BinaryMarketSnapshot& mkt) {
    using namespace stcpp::data::feature_store;

    // ---- Phase B (小梁 spec §3): fair 始终 YES-canonical → 先算 fair, 再选边 ----
    // FairValueEstimator 只支持 YES token 输入 + 先验由 game_row(YES 队比分) 提供 → fair 必用 YES book。
    // 选边 (SelectSide) 是 fair 后的纯代数判断; 执行 (price/depth/4ts/token) 才按被选边切 (Step F+)。
    // YES book 缺 → 无 YES-canonical fair → fail-closed (即便 NO book 在也不交易)。
    if (!mkt.yes.present) {
        stats_.hub_reads_empty.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto& feat = mkt.yes.book;  // YES-canonical (fair / de-vig / book_row 用)
    const std::string& condition_id = mkt.condition_id;

    const double best_ask = feat.best_ask();  // YES ask (fair 段; 执行 ask 选边后定 exec_ask)
    const double best_bid = feat.best_bid();
    // L1 价格有效性门 (YES book)。
    if (!std::isfinite(best_ask) || best_ask <= 0.0 || best_ask >= 1.0) {
        return;
    }
    if (!std::isfinite(best_bid) || best_bid <= 0.0) {
        return;
    }

    // orders_attempted: 进入有效决策即计数 (YES gate 后, 时机同旧)。
    stats_.orders_attempted.fetch_add(1, std::memory_order_relaxed);

    const double microprice = std::isfinite(feat.microprice) ? feat.microprice : (best_bid + best_ask) * 0.5;

    // no_token_mid: NO book microprice 优先 / mid 回退 (de-vig 对边; 单边退化 devig_binary 处理)。
    double no_token_mid = std::numeric_limits<double>::quiet_NaN();
    if (mkt.no.present) {
        const double op_mp = mkt.no.book.microprice;
        const double op_mid = mkt.no.book.mid;
        if (std::isfinite(op_mp) && op_mp > 0.0 && op_mp < 1.0) {
            no_token_mid = op_mp;
        } else if (std::isfinite(op_mid) && op_mid > 0.0 && op_mid < 1.0) {
            no_token_mid = op_mid;
        }
    }

    // ---- A1: cross_spread (= vig, 双边 ask 和 − 1) — 流动性/定价健康度指示, 复用于 CI 调宽 + quote 发布。
    //   仅双边 book 都在时可算; 单边缺 → NaN(不调 CI, 不发布 vig)。
    double cross_spread = std::numeric_limits<double>::quiet_NaN();
    if (mkt.no.present) {
        const double no_ask = mkt.no.book.best_ask();
        if (std::isfinite(best_ask) && best_ask > 0.0 && std::isfinite(no_ask) && no_ask > 0.0) {
            cross_spread = best_ask + no_ask - 1.0;
        }
    }

    // ---- P1-8 de-vig: edge 锚 = 去 overround 的 fair 概率, 非裸 mid/ask --------
    // yes_mid 用 YES token microprice (无效则回退 L1 mid); no_token_mid 来自对边 book.
    // 双边无效 → devig_binary 返 nullopt → 无可用市场锚 → fail-closed (不产 intent).
    const double yes_mid_for_devig =
        (std::isfinite(microprice) && microprice > 0.0 && microprice < 1.0) ? microprice : feat.mid;
    const std::optional<double> p_market_devig_opt = pricing::devig_binary(yes_mid_for_devig, no_token_mid);
    const bool devig_ok = p_market_devig_opt.has_value();
    // de-vig 失败时用裸 microprice 兜底显示, 但 devig_ok=false 会在下方 gate 拦截 intent.
    const double p_market_devig = devig_ok ? *p_market_devig_opt : pricing::clamp_prob(microprice);

    // ---- Step 2: FairValueEstimator ----------------------------------------
    // M1: 无 Goalserve game_row → 退化纯订单簿先验 (score_diff=0, time_frac=0)
    // FairValueEstimator 处理 nullptr book_row 时 kappa=0 (纯先验), 不会 NaN.
    FeatureStoreGameRow game_row{};
    // 默认 stub: 无真实 Goalserve 比分 → 纯订单簿先验 (score_diff=0). 4ts 用 book feat 近似.
    game_row.time_status = stcpp::data::goalserve::TimeStatus::NotStarted;
    game_row.event_ts_ns = feat.event_ts_ns;
    game_row.data_source_ts_ns = feat.data_source_ts_ns;
    game_row.ingestion_ts_ns = feat.ingestion_ts_ns;
    game_row.as_of_ts_ns = feat.as_of_ts_ns;

    // ---- A1: 解析真实 Goalserve 比分 (condition→event 映射 + tick-local 共享快照) ----
    // fail-closed: 无 score_store / 无映射 / 未匹配 / 陈旧 / 非 in-play → 保持 stub.
    // A4: 用 TickAll 入口冻结的 tick_score_snap_/tick_event_map_ (整 tick 同版本, 消 read-skew),
    //     不再 per-condition 各自 Get()/LoadEventMap()。
    if (tick_score_snap_ != nullptr && tick_event_map_ != nullptr) {
        const ConditionEventMap& map = *tick_event_map_;
        {
            const auto it = map.find(condition_id);
            if (it != map.end() && !it->second.inplay_match_id.empty()) {
                const auto sit = tick_score_snap_->find(it->second.inplay_match_id);
                if (sit != tick_score_snap_->end() && sit->second.found) {
                    const auto& es = sit->second;
                    const auto ev_ts = MapEventScoreStatus(es.status);
                    // 新鲜度: data_source_ts 不能太旧 (老韩 D4 #8 + 老周 R-20: 冻结比分不当 live fair).
                    const std::int64_t now_ns = NowNs();
                    const bool fresh = es.ts.data_source_ts_ns > 0 &&
                                       (now_ns - es.ts.data_source_ts_ns) <= cfg_.score_staleness_limit_ns;
                    if (ev_ts != stcpp::data::goalserve::TimeStatus::NotStarted && fresh) {
                        game_row.time_status = ev_ts;
                        // orientation (老周张冠李戴防护): 把 YES 队比分填进 score_home_total,
                        //   令 FairValue score_diff = YES队 - 对手 (prior_yes 方向正确).
                        const int yes_score = it->second.yes_is_home ? es.home_score : es.away_score;
                        const int opp_score = it->second.yes_is_home ? es.away_score : es.home_score;
                        game_row.score_home_total = static_cast<std::int32_t>(yes_score);
                        game_row.score_away_total = static_cast<std::int32_t>(opp_score);
                        // A1.5 (小梁): 接真时钟 → time_frac. FairValue::time_fraction_ 用
                        //   game_row.elapsed_sec / total_game_seconds(sport). 不填则 time_frac=0,
                        //   先验置信永远压在 base 0.15, 真实领先 edge 被 CI 吃掉 → 几乎不成交.
                        game_row.elapsed_sec = static_cast<std::int32_t>(es.clock_sec);
                        game_row.sport = es.sport;  // SportInplaySlug (total_game_seconds 匹配)
                        // R-20: 4ts 切真 Goalserve ts (禁 book ts / 本地 now() 替代上游).
                        game_row.event_ts_ns = es.ts.event_ts_ns;
                        game_row.data_source_ts_ns = es.ts.data_source_ts_ns;
                        game_row.ingestion_ts_ns = es.ts.ingestion_ts_ns;
                        game_row.as_of_ts_ns = es.ts.as_of_ts_ns;
                    }
                }
            }
        }
    }

    // has_real_fair = true 当 time_status != NotStarted (真实 in-play Goalserve 比分已填).
    // 注: A1 仅打通 fair 计算 + quote 真 edge; advisory gate (Step 4b) 仍拦 intent (A2 解封).
    const bool has_real_fair = (game_row.time_status != stcpp::data::goalserve::TimeStatus::NotStarted);

    FeatureStoreBookRow book_row{};
    // token_side 始终 YES-canonical (小梁 §3 Step C: FairValueEstimator 只支持 YES 输入;
    // fair_NO=1-fair_YES)。
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

    // ---- P0-3 / P1-8 fair 锚定 --------------------------------------------
    // 默认 (无真实 Goalserve 先验, M1 stub 路径): p_fair = p_market_devig.
    //   → edge ≈ 0 (锚在去 vig 的市场上自己跟自己比), 配合 has_real_fair gate 不产 intent.
    //   这根除了"低价 outright 被 stub 强拉 → 假 edge"(Spain 0.169 → fake 1076bps).
    // 有真实 in-play game_row 时: 用 score-prior 置信加权混合到 de-vig 市场锚上,
    //   置信随时钟从 kBasePriorConfidence 升到 kMaxPriorConfidence; 终态 conf=1.0.
    double p_fair = p_market_devig;
    if (has_real_fair) {
        const double score_diff =
            static_cast<double>(game_row.score_home_total) - static_cast<double>(game_row.score_away_total);
        const bool terminal = stcpp::data::goalserve::IsTerminal(game_row.time_status);
        // A1.5 (小梁): blend 置信用真 time_frac (elapsed/total), 非硬编码 0.
        //   time_frac=0 时 conf 压在 base 0.15 (先验只 15% 拉力 → 领先 edge 被 CI 吃掉);
        //   接真时钟后 conf 随比赛进程从 0.15 升到 0.60, 先验对 fair 的拉力非线性增强.
        const int total_sec = pricing::total_game_seconds(game_row.sport);
        const double time_frac =
            (game_row.elapsed_sec >= 0 && total_sec > 0)
                ? static_cast<double>(game_row.elapsed_sec) / static_cast<double>(total_sec)
                : 0.0;
        const double p_prior = fv_result.prior_yes;
        const double conf = terminal ? 1.0 : pricing::prior_confidence(time_frac);
        (void)score_diff;  // 方向已含于 prior_yes; 保留以备未来 explicit prior 切换
        p_fair = pricing::blend_prob(p_prior, p_market_devig, conf);
    }

    // ---- Phase B Step E (小梁 spec): 选边 (de-vig 锚定; p_fair 即 p_fair_yes, YES-canonical) ----
    const DecisionSide decision = SelectSide(p_fair, p_market_devig);
    const bool is_yes = (decision.outcome == TradedSide::Yes);
    // Step F: 被选边执行 book / token / ask / depth / 4ts 全切被选边 (老郭 C2 / 老韩 B-6)。
    const SideView& traded = is_yes ? mkt.yes : mkt.no;
    if (!traded.present) {
        // 选 NO 但 NO book 无快照 (YES 高估但 NO 边缺) → fail-closed, 不交易。
        return;
    }
    const auto& exec_feat = traded.book;
    const std::string& token_id = is_yes ? mkt.yes_token_id : mkt.no_token_id;
    const double exec_ask = exec_feat.best_ask();
    if (!std::isfinite(exec_ask) || exec_ask <= 0.0 || exec_ask >= 1.0) {
        return;  // 被选边 ask 无效 (NO 边) → fail-closed
    }
    const double exec_mark = std::isfinite(exec_feat.microprice) ? exec_feat.microprice : exec_feat.mid;

    // ---- Step G: 被选边 fair / edge_ci (小梁 §2 de-vig 对称代数) ----
    //   被选边 fair/共识互余 (fair_NO=1-fair_YES, devig_NO=1-devig_YES) → edge_ci 用 canonical
    //   ComputeEdgeCiLower(被选边 fair, 被选边共识) 即对; de-vig 互余 → sigma 两边相等。
    //   (老郭 impl review nit#2: 用单一 ComputeEdgeCiLower, 消同公式两处实现的漂移风险;
    //    YES 路径 == 旧 ComputeEdgeCiLower(p_fair, p_market_devig) 逐位不变。)
    const double p_fair_selected = is_yes ? p_fair : (1.0 - p_fair);
    const double p_devig_selected = is_yes ? p_market_devig : (1.0 - p_market_devig);
    // 注 (老板「别草率守门」2026-05-31): cross_spread(vig) 不接 CI 硬收紧 — 只作模型输入(进 QuoteFeatures),
    //   让模型/策略学 vig 影响, 是否用它调门留给「守门审计 + 小梁/老韩」定。这里维持原 n_eff (不加守门)。
    const double edge_ci_lower =
        ComputeEdgeCiLower(p_fair_selected, p_devig_selected, cfg_.n_effective, cfg_.z_90);

    const double mark_price = exec_mark;  // 真实 mark (被选边 hub)

    // ---- Step H: SizingCalculator 入参 (按被选边) --------------------------
    // best_ask_size 做 book depth 近似 (L1 USDC depth; 被选边)。
    const double book_depth_l1 = std::isfinite(exec_feat.best_ask_size()) && exec_feat.best_ask_size() > 0.0
                                     ? exec_feat.best_ask_size()
                                     : 1000.0;  // fallback 1000 pUSD

    sizing::SizingInput sz_in;
    sz_in.fair_value = p_fair_selected;
    sz_in.price = exec_ask;  // 被选边 ask (买被低估边)
    sz_in.edge_ci_lower = edge_ci_lower;
    // edge_bps: 被选边 fair vs 市场共识幅度 (de-vig 对称 → 两边同幅; nit#3 语义澄清)。
    sz_in.edge_bps = std::abs(p_fair_selected - p_devig_selected) * 10'000.0;
    sz_in.bankroll_usdc = cfg_.bankroll_usdc;
    sz_in.fill_rate = 0.65;    // 保守固定 (M1)
    sz_in.slippage_bps = 8.0;  // 保守固定 (M1)
    sz_in.buy_yes = is_yes;
    sz_in.fee_rate_coef = FeeCoefFor(condition_id);  // R-fee-2: per-market 真值 (gamma feeSchedule.rate)

    // c4 (P0-2 隐患#1 闭合, 老韩 review): sizing 必须看 RM 同源的真实累计 exposure。否则第 2 笔起
    //   sizing 以为满 headroom (硬编码 0) 而 RM 按真实 exposure 拒 → surprise-reject + sizing 无感分叉
    //   (A5/P0-1 已让 paper 喂真实 exposure 给 RM, 此前 sizing 侧未跟进 = 活跃分叉)。
    //   单源同值: 与 FeedRiskGateway 喂 RM 同走 position_ledger get_per_*_exposure (micro), 无双轨。
    {
        const auto cond_exp = position_ledger_.get_per_condition_exposure();
        const auto tok_exp = position_ledger_.get_per_outcome_exposure();
        const auto cit = cond_exp.find(condition_id);
        const auto tit = tok_exp.find(token_id);
        // unit-contract-ok: ledger micro → sizing current_*_exposure_usdc 的 whole pUSD 域 (÷1e6)
        sz_in.current_condition_exposure_usdc =
            (cit != cond_exp.end()) ? static_cast<double>(cit->second) / 1'000'000.0 : 0.0;
        sz_in.current_token_exposure_usdc =
            (tit != tok_exp.end()) ? static_cast<double>(tit->second) / 1'000'000.0 : 0.0;
    }

    // c3 (P0-2 根治): caps 单一真值源 = cfg_ (whole pUSD), from_pusd 转正确 micro。sizing/RM 同源
    //   同值 (RM 侧 paper_daemon 亦 from_pusd 同源)。sizing 内部 .to_pusd() 回 whole 比 notional。
    //   终结 c2 过渡态的「whole 灌 micro 字段」语义错位 + 双错对消。
    risk::RiskConfig sizing_cfg{};
    sizing_cfg.per_order_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.per_order_cap_usdc);
    sizing_cfg.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.market_exposure_cap_usdc);
    sizing_cfg.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.per_outcome_cap_usdc);
    const sizing::SizingOutput sizing_out = sizing::SizingCalculator::compute(sizing_cfg, sz_in);

    // ---- Step 4: QuoteSnapshotHub::Publish ---------------------------------
    // 无论下单与否, 发布 quote 快照 (供 /api/v1/quote 端点显示真实估值)
    // P0-3: has_real_fair=false 时, 传递 suppress_edge=true → 清零伪 edge 字段.
    // A2: 双边微观结构 + 联合新鲜度透传 (全部模型输入+观测, 不接 gate)。
    //   joint_as_of = min(score.as_of, book.as_of) — 联合新鲜度 (老板「相对最近刷新」; 绝不 gate)。
    const double no_imbalance =
        mkt.no.present ? mkt.no.book.imbalance : std::numeric_limits<double>::quiet_NaN();
    const std::int64_t joint_as_of_ts_ns = std::min(game_row.as_of_ts_ns, feat.as_of_ts_ns);
    PublishQuoteSnapshot(condition_id, fv_result, sizing_out, mark_price, edge_ci_lower, feat, has_real_fair,
                         cross_spread, no_token_mid, no_imbalance, devig_ok, joint_as_of_ts_ns, mkt.event_id,
                         mkt.neg_risk_market_id);
    stats_.quote_publishes.fetch_add(1, std::memory_order_relaxed);

    // ---- P0-4: advisory gate -----------------------------------------------
    // advisory=true (ML-R2: paper 期所有市场) → 不产生 intent, 不进 RM.
    // RM 不再作 advisory 防线; paper_loop 在此处主动 gate.
    // 注意: advisory 检查在 quote publish 之后, quote 本身仍发布 (供观察); 但
    //        quote.edge/kelly/notional=0 (has_real_fair=false) 已保证无假信号.
    if (cfg_.advisory_markets_no_intent) {
        // 所有 paper 期市场均为 advisory; 不构造 intent.
        return;
    }

    // ---- P0-3 / P1-8: fake fair gate (second gate) -------------------------
    // 即使 advisory gate 被 override (未来真实 M2+ 场景), 仍需:
    //   1) 有真实 fair 依据 (has_real_fair) — 否则 stub 路径不产 intent;
    //   2) de-vig 成功 (devig_ok) — 否则无可用市场锚, 无信号绝不伪造 (fail-closed).
    if (!has_real_fair || !devig_ok) {
        // stub fair 路径 / 无市场锚: 直接拦截, 不产生 intent. 宁可空不可假.
        return;
    }

    // CI gating: sizing_out.valid=false 或 net_ci_edge <= 0 → 不下单 (fail-closed)
    if (!sizing_out.valid || sizing_out.suggested_notional <= 0.0) {
        return;
    }

    // ---- Step 5: 构造 OrderIntent v0.6 ------------------------------------
    // R-20: as_of_ts_ns = 信号评估时刻 (>= ingestion_ts_ns)
    const std::int64_t as_of_now = NowNs();

    // 校验 4 ts 链 (调试保护; release build 仍执行但不 abort)。Phase B: intent 成交标的是被选边,
    //   4ts 来自被选边 book exec_feat (R-20: 数据源 = 被交易 token 的 hub 快照, 禁 now() 替代)。
    if (exec_feat.event_ts_ns <= 0 || exec_feat.data_source_ts_ns < exec_feat.event_ts_ns ||
        exec_feat.ingestion_ts_ns < exec_feat.data_source_ts_ns || as_of_now < exec_feat.ingestion_ts_ns) {
        // ts 链违规: 跳过 (不构造 intent, 不送 RM)
        return;
    }

    const std::uint64_t intent_id = ++intent_seq_;

    risk::OrderIntent intent;
    // R-20 4 ts (被选边 book)
    intent.event_ts_ns = exec_feat.event_ts_ns;
    intent.data_source_ts_ns = exec_feat.data_source_ts_ns;
    intent.ingestion_ts_ns = exec_feat.ingestion_ts_ns;
    intent.as_of_ts_ns = as_of_now;

    // 市场标识
    intent.condition_id = condition_id;
    intent.token_id = token_id;
    intent.outcome = is_yes ? strategy::Outcome::Yes : strategy::Outcome::No;  // 参数化 (M1 桩 YES)
    intent.side = decision.side;                                               // M1 桩 Buy; M2 开放 Sell

    // 业务 ID (去重: intent_seq_ 单调)
    intent.strategy_id = cfg_.strategy_id;
    intent.signal_id = cfg_.strategy_id + "-" + std::to_string(intent_id);
    intent.feature_snapshot_id = "paper-m1-no-snapshot";

    // 定价: 买被选边 at ask (YES→ask_YES, NO→ask_NO; exec_ask 已切被选边)
    intent.price = exec_ask;

    // 仓位大小: SizingCalculator 建议值, 转 micro pUSD
    // P0-2 (拆 clamp 遮羞布): sizing 已受 sizing_cfg.per_order_cap_usdc (= RM cap ÷ 1e6) 约束,
    //   suggested_notional ≤ per_order_cap (pUSD) → × 1e6 后必 ≤ RM per_order_cap (micro), RM
    //   不会因 size 拒。原 `min(notional, 10.0)` 是失配年代的硬钳 (sizing/RM 单位脱节), 已删。
    const double notional_usdc = sizing_out.suggested_notional;
    intent.size_pUSD_micro = static_cast<std::int64_t>(notional_usdc * 1'000'000.0);
    if (intent.size_pUSD_micro <= 0) {
        // c5 (老韩 cap 真值 SSOT): 兜底 = min(1pUSD, per_order_cap)。原硬编码 1 pUSD 在 sub-1-pUSD
        //   cap 下会越 cap (1pUSD > cap) → 兜底自造 RM EXCEED_PER_ORDER_CAP 拒。clamp 到 cap 上限内。
        const std::int64_t cap_micro = static_cast<std::int64_t>(cfg_.per_order_cap_usdc * 1'000'000.0);
        intent.size_pUSD_micro = (cap_micro < 1'000'000LL) ? cap_micro : 1'000'000LL;
    }

    // book context (R8.4 freshness)
    // 单位对齐 (A2 修): RM check_liquidity_ 比 size_pUSD_micro vs book_depth_l1_usdc,
    //   后者须同为 micro pUSD. book_depth_l1 来自 exec_feat.best_ask_size() (被选边, pUSD), × 1e6 转 micro.
    intent.book_depth_l1_usdc = book_depth_l1 * 1'000'000.0;  // book_depth_l1 已切被选边 (Step H)
    intent.book_snapshot_ts_ns = exec_feat.ingestion_ts_ns;   // 被选边 book ts (R8.4 + R-20)
    intent.tick_size = 0.01;
    intent.fee_rate_coef = sz_in.fee_rate_coef;  // R-fee-2: 与 sizing 同源 (RM check_signal_ 用)

    // V2 EIP-712 字段 (timestamp_ms = 当前毫秒, spec-10 必须 != 0)
    intent.timestamp_ms = as_of_now / 1'000'000LL;
    intent.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
    intent.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";
    intent.is_close = false;

    // ---- Step 6: RiskGateway::evaluate ------------------------------------
    const risk::RiskDecision rd = rm_.evaluate(intent);

    if (rd.is_rejected()) {
        stats_.orders_rejected.fetch_add(1, std::memory_order_relaxed);
        // P0-1 去重修复: risk_gateway.cpp::evaluate() 内部的 reject_here() lambda 已经
        // 通过 g_rm_debug_snapshot 全局指针调用 push_reject 一次.
        // paper_loop 不再重复调用 rm_snap_->push_reject, 防止双写.
        // rm_snap_ 字段保留 (供 snapshot() 读取), 但写操作交由 RM 单独负责.
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
    vord.outcome = is_yes ? "YES" : "NO";  // 参数化 (M1 桩 YES)
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

    // G-1: 经执行器 (默认 VirtualExecutor → matcher_.Match, 行为逐位不变)。
    const execution::VirtualFill fill = executor_->Execute(vord);

    // R-11: VirtualFill.mode_tag 必须为 0 (paper 标记; VirtualMatcher 内部硬填)
    assert(fill.mode_tag == 0u);  // 防御性校验 (debug build)

    if (fill.reject != execution::MatchReject::Ok || fill.fill_size_usdc <= 0) {  // A1: micro int64
        stats_.fills_missed.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    stats_.fills_completed.fetch_add(1, std::memory_order_relaxed);

    // ---- Step 8: PositionLedger::apply_fill --------------------------------
    // R-11: position_ledger_ 是 paper 专用账本 (调用方构建时物理隔离)
    position_ledger_.apply_fill(condition_id, token_id, intent.outcome, fill);  // 参数化 (M1 桩 YES)

    // ---- Step 8b: LedgerSnapshotHub::Publish (positions/pnl 可见) ----------
    PublishLedgerSnapshot(condition_id, fill, mark_price, exec_feat);  // 被选边 mark + 4ts
    stats_.ledger_publishes.fetch_add(1, std::memory_order_relaxed);

    // ---- Step 8c: 喂 RM (P0-1) — apply_fill 后回喂敞口, 激活 exposure 红线 ----
    //   全量覆盖 (PL 已增量维护 → RM 全量 = PL 真值, 自愈)。loop_thread_ 串行, R-12 满足。
    FeedRiskGateway();

    std::fprintf(stderr,
                 "[paper_loop] FILL cond=%.24s... tok=%.16s... "
                 "fill_sz=%.4f fill_px=%.4f p_fair=%.4f edge=%.1fbps\n",
                 condition_id.c_str(), token_id.c_str(),
                 static_cast<double>(fill.fill_size_usdc) / 1'000'000.0,  // A1: micro→pUSD 显示
                 fill.fill_price, p_fair, sz_in.edge_bps);
}

// ---------------------------------------------------------------------------
// ComputeEdgeCiLower — CI 下界 (§10.3)
//
// edge_ci_lower = (p_fair - p_ask) - z * sqrt(p_fair * (1 - p_fair) / n_eff)
// 数值稳定: fail-closed 返 -1.0 (禁止下单)
// ---------------------------------------------------------------------------

/*static*/
double PaperLoop::ComputeEdgeCiLower(double p_fair, double p_ask, int n_eff, double z) noexcept {
    // 单一实现: stcpp/strategy/edge_ci.hpp (回测-实盘共用同一公式, 消两处漂移; Phase4 阻塞3 修复)。
    return stcpp::strategy::ComputeEdgeCiLower(p_fair, p_ask, n_eff, z);
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
            // A1: size_usdc 现真为 signed micro (账本 micro 化), /1e6 = qty pUSD 正确
            //   (原 size_usdc 存 whole 时此 /1e6 致 PnL 低估 1e6, A1 后数据对了, 式子本就对)。
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
    // A1: fill_size_usdc micro → /1e6 转 pUSD 算 fee (unit-contract-ok: micro→pUSD)
    // R-fee-2: fee 系数 per-market (gamma feeSchedule.rate), 与 RM/sizing 同源 FeeCoefFor(condition).
    //   未填 → kDefaultFeeCoef(0.03), 与旧 sizing::kSportsTakerFeeRate 逐位不变。
    const double pnl_fee = (static_cast<double>(fill.fill_size_usdc) / 1'000'000.0) *
                           FeeCoefFor(condition_id) * fill.fill_price * (1.0 - fill.fill_price);
    const double pnl_gross = pnl_realized + pnl_unrealized;

    // A5 (老韩 spec §4): 累计已付 fee (单调加, whole pUSD)。FeedRiskGateway 的 DD 喂数读它
    //   (daily_pnl = 时点净 MtM − cum_fee)。loop_thread_ 单 writer, 无需 atomic。
    cum_fee_pusd_ += pnl_fee;

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
// FeedRiskGateway — P0-1: paper 持仓敞口 → RM (激活 exposure 红线)
//
// 背景: RM 的 per_condition / per_outcome exposure cap 逻辑齐全, 但生产此前零喂数
//   (paper_loop 只 set_bankroll, 从不喂 exposure) → cap 永不咬 = 风控纸面化。
// 老韩 RM 契约 + 老周架构: loop_thread_ 内 apply_fill 后全量覆盖喂 RM。
//
// 🔴 单位门禁 (老周 P0 gate): 仓位账本 size_usdc 存 whole pUSD (apply_fill 把 whole
//   double cast int64); RM exposure 比 micro (check_position_caps_ from_micro(cur+size_micro))。
//   故喂前必 × 1e6 (whole → micro)。漏乘 → exposure 红线静默架空 (同 P0-2 单位 bug 同型)。
//   守护: test_paper_loop P0-1 单位门测试 (fill 越 cap → 必触 EXCEED_CONDITION_EXPOSURE)。
//
// daily_pnl (DD): A5 (老韩 spec) 已接通 (净 MtM − cum_fee, best_bid 清算; 见下)。A1 ledger PnL
//   单位根治后, 老郭事前否决前置已清除。
// consec_loss: 延 M2 (M1 只买不平 → 无平仓 trade = 无连亏源; feed-liveness NEVER FED 为预期正确态)。
// ---------------------------------------------------------------------------
void PaperLoop::FeedRiskGateway() noexcept {
    // A1 (老郭钳-6): 账本 micro 化后 get_*_exposure 已是 micro, 与 RM exposure 同单位 → 删原 ×1e6
    //   补偿乘 (P0-1 的"whole→micro"对冲乘已无意义)。直喂, 全量覆盖 (PL 真值, 自愈)。
    for (auto const& [cid, micro] : position_ledger_.get_per_condition_exposure()) {
        rm_.set_condition_exposure(cid, micro);
    }
    for (auto const& [tid, micro] : position_ledger_.get_per_outcome_exposure()) {
        rm_.set_outcome_exposure(tid, micro);
    }

    // A5 (老韩 spec §1-§5): daily_pnl → DD 熔断。M1 买入阶段语义 = 净未实现 MtM − 累计 fee。
    //   全量覆盖 (set_daily_pnl 是 atomic store 非累加) → 天然无双计, 与 exposure 喂法同构, 自愈。
    //   保守 (铁律#2): 多头清算 mark 用 best_bid (砸 bid 侧成交真值); 无效 bid 仓位按 0 浮盈
    //   (不臆造正盈余掩盖亏损)。亏损时 pnl 为负 → RM `if(pnl<0)` 分支 (符号天然对齐, 无需取反)。
    //   M2: 接平仓 (realized≠0) 后须改为 realized(日界累加) + unrealized(时点), 见 spec §3。
    double pnl_pusd = 0.0;
    for (auto const& pv : position_ledger_.get_all_positions()) {
        // unit-contract-ok: signed micro → whole share (qty)
        const double qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
        const auto bk = hub_.Read(pv.token_id);
        if (bk.has_value()) {
            const double bid = bk->best_bid();
            if (std::isfinite(bid) && bid > 0.0 && bid < 1.0) {
                pnl_pusd += (bid - pv.avg_entry_price) * qty;  // 时点浮动 MtM (清算价 best_bid)
            }
            // 无效 bid: 跳过浮盈贡献 (保守, 不臆造正值; 该仓位仍承担下方 cum_fee)
        }
    }
    pnl_pusd -= cum_fee_pusd_;  // 减累计已付 fee (spec §1.3/§4)
    // unit-contract-ok: pUSD → micro (×1e6 唯一通道; signed PnL 不走 MicroPUSD 非负 cap helper)。
    //   防 P0-2/P1-9 同型单位 bug: 漏 ×1e6 → 喂入小 1e6 → 阈值不咬 (T-A5-1 单位门守护)。
    rm_.set_daily_pnl(static_cast<std::int64_t>(pnl_pusd * 1'000'000.0));
}

// ---------------------------------------------------------------------------
// PublishQuoteSnapshot — SizingCalculator 完成后更新 QuoteSnapshotHub
//
// R-20: 4 ts 来自 feat (上游链路)
//
// P0-3 修复: has_real_fair=false 时 (M1 stub, 无 Goalserve game_row):
//   - edge_bps / kelly_fraction / suggested_notional / signal_strength 全部清零
//   - predict_ok = false (明确标记"不可决策")
//   - model_calibrated = false (已有; 与 predict_ok 双保险)
//   - fair_value 字段仍输出 stub 值 (供观察, 但消费方须检查 predict_ok)
//   宁可空不可假: 消费方 (前端/API) 见 predict_ok=false 应灰显数值, 不渲染 edge/notional.
// ---------------------------------------------------------------------------

void PaperLoop::PublishQuoteSnapshot(
    const std::string& condition_id, const pricing::FairValueResult& fv_result,
    const sizing::SizingOutput& sizing_out, double mark_price, double edge_ci_lower,
    const polymarket::clob_wss::OrderBookFeatures& feat, bool has_real_fair, double cross_spread,
    double no_microprice, double no_imbalance, bool devig_ok, std::int64_t joint_as_of_ts_ns,
    const std::string& event_id, const std::string& neg_risk_market_id) noexcept {
    sizing::QuoteFeatures qf{};
    // R-20: 4 ts 透传 (来自 hub 快照)
    qf.event_ts_ns = feat.event_ts_ns;
    qf.data_source_ts_ns = feat.data_source_ts_ns;
    qf.ingestion_ts_ns = feat.ingestion_ts_ns;
    qf.as_of_ts_ns = NowNs();  // 快照发布时刻 (R-20 allowed)

    // fair_value: 始终输出 (fv_result.p_yes()), 但 predict_ok=false 时消费方不可据此决策.
    qf.fair_value = fv_result.p_yes();
    qf.market_mid = mark_price;
    // R-fee-2: per-market 手续费系数 (始终输出, fee 是 market 元数据非决策派生, 不受 has_real_fair gate)。
    //   进 ML 训练数据 (FeatureRecorder) + 前端 /quote。与 RM/sizing 同源 FeeCoefFor(condition)。
    qf.fee_rate_coef = FeeCoefFor(condition_id);

    // A2: 盘口上下文 / 双边微观结构 (模型输入 + 观测, 绝不 gate — 老板 2026-05-31)。
    std::strncpy(qf.condition_id, condition_id.c_str(), sizeof(qf.condition_id) - 1);
    qf.condition_id[sizeof(qf.condition_id) - 1] = '\0';
    // 统一数据树: 父级引用进 quote (ML 按 event join 兄弟盘口; neg_risk 一致性锚)。
    std::strncpy(qf.event_id, event_id.c_str(), sizeof(qf.event_id) - 1);
    qf.event_id[sizeof(qf.event_id) - 1] = '\0';
    std::strncpy(qf.neg_risk_market_id, neg_risk_market_id.c_str(), sizeof(qf.neg_risk_market_id) - 1);
    qf.neg_risk_market_id[sizeof(qf.neg_risk_market_id) - 1] = '\0';
    qf.no_microprice = no_microprice;
    qf.cross_spread = cross_spread;
    qf.yes_imbalance = feat.imbalance;  // YES L1 失衡 (本边 book)
    qf.no_imbalance = no_imbalance;     // NO  L1 失衡 (对边 book; 单边缺=NaN)
    qf.devig_ok = devig_ok;
    qf.joint_as_of_ts_ns = joint_as_of_ts_ns;  // 联合新鲜度 (min(score,book) as_of); 输入不 gate

    // 当前持仓 (老板: 持仓入模型; 库存感知)。目标仓位范式: 模型需知现仓 → 控制器算 order=目标−现仓。
    {
        const auto positions = position_ledger_.get_all_positions();
        for (const auto& pv : positions) {
            if (pv.condition_id == condition_id) {
                qf.pos_net_qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
                qf.pos_avg_entry = pv.avg_entry_price;
                break;
            }
        }
        const auto cond_exp = position_ledger_.get_per_condition_exposure();
        const auto cit = cond_exp.find(condition_id);
        qf.pos_condition_exposure_usdc =
            (cit != cond_exp.end()) ? static_cast<double>(cit->second) / 1'000'000.0 : 0.0;
    }

    if (has_real_fair) {
        // 真实 fair 路径 (M2+ Goalserve 接入后): 输出真实 edge/kelly/notional.
        qf.edge_bps = sizing_out.valid ? sizing_out.net_ci_edge * 10'000.0 : 0.0;
        qf.kelly_fraction = sizing_out.valid ? sizing_out.kelly_fractional : 0.0;
        qf.suggested_notional = sizing_out.valid ? sizing_out.suggested_notional : 0.0;
        qf.signal_strength = edge_ci_lower > 0.0 ? std::min(edge_ci_lower * 10.0, 1.0) : 0.0;
        qf.predict_ok = fv_result.valid;
    } else {
        // P0-3: stub fair 路径 — 无真实 Goalserve 先验, fair 是 microprice 收缩伪值.
        // 清零所有决策字段: 消费方见到这些零值 + predict_ok=false + model_calibrated=false
        // 应拒绝据此下单. 宁可空不可假.
        qf.edge_bps = 0.0;
        qf.kelly_fraction = 0.0;
        qf.suggested_notional = 0.0;
        qf.signal_strength = 0.0;
        qf.predict_ok = false;  // 显式标记不可决策 (P0-3 关键字段)
    }

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
    qf.advisory = true;           // ML-R2: paper 期恒 true
    qf.model_calibrated = false;  // M1 stub: 未校准
    qf.model_as_of_ts_ns = feat.ingestion_ts_ns;

    qf.valid = fv_result.valid;

    quote_hub_.Publish(condition_id, qf);
}

}  // namespace stcpp::paper
