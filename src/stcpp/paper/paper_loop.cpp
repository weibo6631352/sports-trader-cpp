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
// TickAll — 遍历所有 condition_id, 对 token0 (YES side) 做一次 paper 尝试
// ---------------------------------------------------------------------------

void PaperLoop::TickAll() {
    for (const auto& [cond_id, tok_pair] : token_map_) {
        const std::string& token0 = tok_pair.first;   // YES token
        const std::string& token1 = tok_pair.second;  // NO token (P1-8 de-vig 用)

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

        // P1-8: 读 NO token book 取对边 mid 供 de-vig. NO book 不可得 → 单边退化
        // (devig_binary 内部以 yes_mid 退化, 不阻塞). 不污染 R-12 (仍是原子只读).
        double no_token_mid = std::numeric_limits<double>::quiet_NaN();
        if (!token1.empty()) {
            const auto no_opt = hub_.Read(token1);
            if (no_opt.has_value() && no_opt->valid) {
                const double no_mp = no_opt->microprice;
                const double no_mid = no_opt->mid;
                if (std::isfinite(no_mp) && no_mp > 0.0 && no_mp < 1.0) {
                    no_token_mid = no_mp;
                } else if (std::isfinite(no_mid) && no_mid > 0.0 && no_mid < 1.0) {
                    no_token_mid = no_mid;
                }
            }
        }

        TickOne(cond_id, token0, feat, no_token_mid);
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

void PaperLoop::TickOne(const std::string& condition_id, const std::string& token_id,
                        const polymarket::clob_wss::OrderBookFeatures& feat, double no_token_mid) {
    using namespace stcpp::data::feature_store;

    // orders_attempted: 进入 TickOne 即计数 (book 有效, 开始尝试流程)
    stats_.orders_attempted.fetch_add(1, std::memory_order_relaxed);

    const double best_ask = feat.best_ask();
    const double best_bid = feat.best_bid();
    const double microprice = std::isfinite(feat.microprice) ? feat.microprice : (best_bid + best_ask) * 0.5;

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

    // ---- A1: 解析真实 Goalserve 比分 (condition→event 映射 + score_store.Get) ----
    // fail-closed: 无 score_store / 无映射 / 未匹配 / 陈旧 / 非 in-play → 保持 stub.
    if (score_store_ != nullptr) {
        const std::shared_ptr<const ConditionEventMap> map = LoadEventMap();
        if (map) {
            const auto it = map->find(condition_id);
            if (it != map->end() && !it->second.inplay_match_id.empty()) {
                const auto es_opt = score_store_->Get(it->second.inplay_match_id);
                if (es_opt.has_value() && es_opt->found) {
                    const auto& es = *es_opt;
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

    const double mark_price = microprice;  // 真实 mark (来自 hub)

    // ---- Step 3: CI 下界 + SizingCalculator --------------------------------
    // edge_ci_lower = (p_fair - p_market_devig) - z * sqrt(p*(1-p)/n) (§10.3)
    // P1-8: 锚在 de-vig 市场概率, 不再用带 vig 的 best_ask, 杜绝 overround 假 edge.
    const double edge_ci_lower = ComputeEdgeCiLower(p_fair, p_market_devig, cfg_.n_effective, cfg_.z_90);

    // best_ask_size 做 book depth 近似 (L1 USDC depth)
    const double book_depth_l1 = std::isfinite(feat.best_ask_size()) && feat.best_ask_size() > 0.0
                                     ? feat.best_ask_size()
                                     : 1000.0;  // fallback 1000 pUSD

    sizing::SizingInput sz_in;
    sz_in.fair_value = p_fair;
    sz_in.price = best_ask;  // buy YES at ask (执行价仍是真实 ask)
    sz_in.edge_ci_lower = edge_ci_lower;
    // P1-8: edge 锚在 fair vs de-vig 市场, 非裸 ask (后者含 vig → 系统性高估 edge).
    sz_in.edge_bps = std::abs(p_fair - p_market_devig) * 10'000.0;
    sz_in.bankroll_usdc = cfg_.bankroll_usdc;
    sz_in.fill_rate = 0.65;    // 保守固定 (M1)
    sz_in.slippage_bps = 8.0;  // 保守固定 (M1)
    sz_in.buy_yes = (p_fair > best_ask);
    sz_in.current_token_exposure_usdc = 0.0;
    sz_in.current_condition_exposure_usdc = 0.0;

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
    PublishQuoteSnapshot(condition_id, fv_result, sizing_out, mark_price, edge_ci_lower, feat, has_real_fair);
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
    //   后者须同为 micro pUSD. book_depth_l1 来自 feat.best_ask_size() (pUSD), × 1e6 转 micro.
    intent.book_depth_l1_usdc = book_depth_l1 * 1'000'000.0;
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

    if (fill.reject != execution::MatchReject::Ok || fill.fill_size_usdc <= 0) {  // A1: micro int64
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
    const double pnl_fee = (static_cast<double>(fill.fill_size_usdc) / 1'000'000.0) *
                           sizing::kSportsTakerFeeRate * fill.fill_price * (1.0 - fill.fill_price);
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
// daily_pnl (DD) / consec: M1 暂不喂 (M1 只买不平 → realized=0, consec 无源; daily_pnl 的
//   unrealized 路径撞 PublishLedgerSnapshot 预存 PnL 单位 bug, 待 ledger PnL 单位修复后接)。
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

void PaperLoop::PublishQuoteSnapshot(const std::string& condition_id,
                                     const pricing::FairValueResult& fv_result,
                                     const sizing::SizingOutput& sizing_out, double mark_price,
                                     double edge_ci_lower,
                                     const polymarket::clob_wss::OrderBookFeatures& feat,
                                     bool has_real_fair) noexcept {
    sizing::QuoteFeatures qf{};
    // R-20: 4 ts 透传 (来自 hub 快照)
    qf.event_ts_ns = feat.event_ts_ns;
    qf.data_source_ts_ns = feat.data_source_ts_ns;
    qf.ingestion_ts_ns = feat.ingestion_ts_ns;
    qf.as_of_ts_ns = NowNs();  // 快照发布时刻 (R-20 allowed)

    // fair_value: 始终输出 (fv_result.p_yes()), 但 predict_ok=false 时消费方不可据此决策.
    qf.fair_value = fv_result.p_yes();
    qf.market_mid = mark_price;

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
