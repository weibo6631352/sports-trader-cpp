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
#include "stcpp/risk/arb_signal.hpp"  // 模块5: 短时套利 advisory 信号

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>

#include "stcpp/control/position_controller.hpp"  // 目标仓位控制器 (Decide + ComputeReservation, BR-1)
#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/inplay_odds_parser.hpp"    // ToYesCanonical (inplay 赔率 orientation 翻转)
#include "stcpp/data/live_stats_parser.hpp"     // FillLiveStats (live_stats 采集 hop join)
#include "stcpp/data/score_snapshot_store.hpp"  // A1: ScoreSnapshotStore::Get(inplay_match_id)
#include "stcpp/execution/execution_mode.hpp"   // A2 红线1: kCompiledMode 运行期 mode 断言
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/pricing/derivative_fair_value.hpp"
#include "stcpp/pricing/tennis_fair_value.hpp"  // 网球 totals/spreads (games/sets 制; 老板「全盘口接入」)
#include "stcpp/pricing/esports_fair_value.hpp"  // 电竞 maps totals/spreads (best-of-N 枚举)
#include "stcpp/pricing/fair_resolve.hpp"  // R-2: ResolveFair 纯函数 (fair 优先级集中)
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
      cfg_(std::move(cfg)) {
    // R-3: ctor 从 token_map 建初始 catalog (仅 tokens + 默认 fee/cat/parent)。daemon 随后
    //   SetPaperCatalog 注入含 fee/cat/parent 的完整版; 测试只用 ctor (默认 fee=0.03/空 cat/无 parent)。
    {
        auto init = std::make_shared<PaperCatalog>();
        init->reserve(token_map.size());
        for (auto& [cond, toks] : token_map) {
            PaperMarketEntry e;
            e.tokens = std::move(toks);
            (*init)[cond] = std::move(e);
        }
        catalog_ = std::move(init);
    }
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
                 static_cast<long long>(cfg_.tick_interval_ms),
                 (LoadPaperCatalog() ? LoadPaperCatalog()->size() : 0), cfg_.bankroll_usdc);
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
        stats_.last_tick_ts_ns.store(NowNs(), std::memory_order_relaxed);  // 韧性: tick 心跳 (watchdog 用)

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
    // [P1 backtest-equivalence] 6 决策输入里 5 个(非 book)在此聚合冻结成 tick_inputs_; book 第 6 输入
    //   走 hub_ (已可经 ReplayDriver 注入, 这正是当前唯一可回放的 1/6)。replay_inputs_ 非空 → 用注入历史帧
    //   替代 store 读 (小蒋 P2 回测 harness 闭合红线#3); live 路径 replay_inputs_ 恒 null → 行为逐位不变。
    if (replay_inputs_ != nullptr) {
        tick_inputs_ = *replay_inputs_;
    } else {
        tick_inputs_.event_map = LoadEventMap();
        tick_inputs_.score = (score_store_ != nullptr) ? score_store_->GetSnapshot() : nullptr;
        tick_inputs_.catalog = LoadPaperCatalog();  // RCU 快照: 周期重发现中途 swap, 整 tick 持有同版本
        tick_inputs_.resolution = LoadResolution();   // [R-1] 刷新线程 30s swap, 整 tick 冻结同版本 (消 UB)
        tick_inputs_.live_stats = LoadLiveStats();    // [R-1] 同上
        tick_inputs_.odds = LoadOdds();               // bm_slots: 跨庄家赔率 (inplay_match_id 键), [R-1] 同上
    }
    if (tick_inputs_.catalog == nullptr) {
        return;  // 未注入 (理论不达; ctor 必置)
    }

    // [2026-06-01 凯利评审] tick 入口冻结账户权益快照 → 本轮所有子盘口 sizing 用同版本 bankroll。
    //   修「风控纸面化」(bankroll 此前硬用 cfg_ 静态初值, 回撤不缩盈利不涨)。整 tick 冻结 → 同 tick 内
    //   多笔成交不驱动 bankroll 抖动 (老韩/小梁); 下 tick 自然吸收本 tick 已实现/未实现变化。
    tick_equity_ = account_equity();

    for (const auto& [cond_id, entry] : *tick_inputs_.catalog) {
        const std::string& yes_tok = entry.tokens.first;   // YES token
        const std::string& no_tok = entry.tokens.second;   // NO token

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

    // Phase 0 项5 (联合评审, 小梁): 每 tick 周期采一次组合权益 → Sharpe/maxDD/VaR。
    //   2026-06-01 凯利评审: equity 改用 account_equity().equity_bid (含未实现 MtM, best_bid 保守口径)。
    //   原口径 bankroll+realized 不含未实现 → maxDD 严重低估 / Sharpe 虚高 (小肖/老李 R-4)。保守 best_bid
    //   防低估回撤 (风险指标宜保守, 不用 microprice)。R-20: ts 用 NowNs (权益曲线是策略侧时序, 不涉数据源契约)。
    //   用 tick 入口冻结快照 (与本轮 sizing bankroll 同源同版本)。
    portfolio_metrics_.RecordEquity(NowNs(), tick_equity_.equity_bid);

    // [2026-06-01 凯利评审] 发布账户权益副本 (debug_api /api/v1/account 经 daemon 回调读)。
    //   loop_thread_ 算 sharpe/maxDD (portfolio_metrics_ 单 writer 此处读安全) → mutex 发布给 HTTP 线程。
    //   tick 末重算一次 equity (含本轮成交后的最新持仓), 比 tick 入口冻结的 tick_equity_ 新。
    {
        AccountEquitySnapshot pub = account_equity();
        const double ppy = cfg_.tick_interval_ms > 0
                               ? 365.25 * 24.0 * 3600.0 * 1000.0 / static_cast<double>(cfg_.tick_interval_ms)
                               : 0.0;
        const auto rep = portfolio_metrics_.report(ppy);
        pub.sharpe = rep.sharpe;
        pub.max_drawdown = rep.max_drawdown;
        std::lock_guard<std::mutex> lk(acct_pub_mu_);
        published_equity_ = pub;
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

    // ---- 时序特征 observe-always (老板 2026-05-31 slice-1+2): 在交易门之前记录 ----
    //   「卖不出」(bid 没了) 正是要观测的事件 —— 旧码在 best_bid 无效时 early-return, 会把它审查掉。
    //   故在价有效性门之前 push: ts=上游 data_source_ts (禁 now()); 价无效→存 NaN (价 derive 跳过);
    //   bid 字段记录退出流动性 (含无 bid)。loop_thread_ 单 writer 无锁; 派生在 PublishQuoteSnapshot 读。
    {
        const bool px_ok = std::isfinite(best_ask) && best_ask > 0.0 && best_ask < 1.0 &&
                           std::isfinite(best_bid) && best_bid > 0.0;
        const double mp_obs =
            px_ok ? (std::isfinite(feat.microprice) ? feat.microprice : (best_bid + best_ask) * 0.5)
                  : std::numeric_limits<double>::quiet_NaN();
        ts_history_[condition_id].Push(feat.data_source_ts_ns, mp_obs, best_bid, feat.best_bid_size(),
                                       best_ask, feat.best_ask_size());
    }
    // NO 边时序 push (双边对称, 老板「双边都要有」): NO book 独立微结构。同 observe-always 语义
    //   (价无效→NaN; bid 无→退出流动性观测)。NO 缺快照则本 tick 不 push (样本不足派生→NaN)。
    if (mkt.no.present) {
        const auto& nbook = mkt.no.book;
        const double n_bid = nbook.best_bid(), n_ask = nbook.best_ask();
        const bool n_px_ok = std::isfinite(n_ask) && n_ask > 0.0 && n_ask < 1.0 &&
                             std::isfinite(n_bid) && n_bid > 0.0;
        const double n_mp =
            n_px_ok ? (std::isfinite(nbook.microprice) ? nbook.microprice : (n_bid + n_ask) * 0.5)
                    : std::numeric_limits<double>::quiet_NaN();
        ts_history_no_[condition_id].Push(nbook.data_source_ts_ns, n_mp, n_bid, nbook.best_bid_size(),
                                          n_ask, nbook.best_ask_size());
    }

    // L1 价格有效性门 (YES book) — 仅 gate 交易决策 (观测已在上方记录, 不受此 return 审查)。
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

    // 批1 g_fld_signal: power de-vig 与 multiplicative 的差 = favorite-longshot 偏差强度 (小肖)。
    //   双边有效才有意义; 否则 0 (无偏差信号)。喂模型, 不替换主 fair。
    double g_fld_signal = 0.0;
    if (devig_ok && mkt.no.present) {
        const auto p_power = pricing::devig_binary_power(yes_mid_for_devig, no_token_mid);
        if (p_power.has_value()) g_fld_signal = p_market_devig - *p_power;
    }

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
    // 慢源新鲜度 (老板「每个源标时间, 模型学权重」): resolution(60s) / catalog(300s) 各自最后刷新时刻。
    //   (mapping/live_stats 在下方比分 join 块填; 查不到→保持 0→age NaN, 模型不读)。
    if (const ResolutionEntry* res = ResolutionFor(condition_id)) {
        game_row.resolution_fetched_at_ns = res->fetched_at_ns;
    }
    game_row.catalog_discovered_at_ns = CatalogDiscoveredAtFor(condition_id);

    // ---- A1: 解析真实 Goalserve 比分 (condition→event 映射 + tick-local 共享快照) ----
    // fail-closed: 无 score_store / 无映射 / 未匹配 / 陈旧 / 非 in-play → 保持 stub.
    // A4: 用 TickAll 入口冻结的 tick_score_snap_/tick_event_map_ (整 tick 同版本, 消 read-skew),
    //     不再 per-condition 各自 Get()/LoadEventMap()。
    bool map_is_draw = false;  // 盈利修复: 3-way 平局盘 → 下游 sharp fair 取 draw 概率
    // [score-flow diag] 地基可观测 (2026-06-03 老板「先打地基才知有什么事件」): 逐环计数 score→game_row
    //   链掉点 (定位 in-play 事件为何不流入决策); 每 3000 次 emit 一行 (cov-diag 同风格, 临时诊断)。
    static std::atomic<long long> sf_calls{0}, sf_mapped{0}, sf_scorefound{0}, sf_inplay{0},
        sf_stale{0}, sf_realfair{0}, sf_sharp{0};
    const long long sf_n = sf_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (tick_inputs_.score != nullptr && tick_inputs_.event_map != nullptr) {
        const ConditionEventMap& map = *tick_inputs_.event_map;
        {
            const auto it = map.find(condition_id);
            if (it != map.end() && !it->second.inplay_match_id.empty()) {
                sf_mapped.fetch_add(1, std::memory_order_relaxed);
                map_is_draw = it->second.is_draw;
                game_row.mapping_as_of_ns = it->second.match_as_of_ns;  // 映射新鲜度 (老板「每个源标时间」)
                const auto sit = tick_inputs_.score->find(it->second.inplay_match_id);
                if (sit != tick_inputs_.score->end() && sit->second.found) {
                    sf_scorefound.fetch_add(1, std::memory_order_relaxed);
                    const auto& es = sit->second;
                    const auto ev_ts = MapEventScoreStatus(es.status);
                    // 新鲜度: data_source_ts 不能太旧 (老韩 D4 #8 + 老周 R-20: 冻结比分不当 live fair).
                    const std::int64_t now_ns = NowNs();
                    const bool fresh = es.ts.data_source_ts_ns > 0 &&
                                       (now_ns - es.ts.data_source_ts_ns) <= cfg_.score_staleness_limit_ns;
                    const bool is_inplay = (ev_ts != stcpp::data::goalserve::TimeStatus::NotStarted);
                    if (is_inplay) sf_inplay.fetch_add(1, std::memory_order_relaxed);
                    if (is_inplay && !fresh) sf_stale.fetch_add(1, std::memory_order_relaxed);
                    if (ev_ts != stcpp::data::goalserve::TimeStatus::NotStarted && fresh) {
                        sf_realfair.fetch_add(1, std::memory_order_relaxed);
                        game_row.time_status = ev_ts;
                        // orientation (老周张冠李戴防护): 把 YES 队比分填进 score_home_total,
                        //   令 FairValue score_diff = YES队 - 对手 (prior_yes 方向正确).
                        const int yes_score = it->second.yes_is_home ? es.home_score : es.away_score;
                        const int opp_score = it->second.yes_is_home ? es.away_score : es.home_score;
                        game_row.score_home_total = static_cast<std::int32_t>(yes_score);
                        game_row.score_away_total = static_cast<std::int32_t>(opp_score);
                        // 网球已打局数 (totals/spreads 用); 同 orientation 翻成 YES-canonical。非网球=0。
                        const int yes_games = it->second.yes_is_home ? es.games_home : es.games_away;
                        const int opp_games = it->second.yes_is_home ? es.games_away : es.games_home;
                        game_row.score_home_games = static_cast<std::int32_t>(yes_games);
                        game_row.score_away_games = static_cast<std::int32_t>(opp_games);
                        // A1.5 (小梁): 接真时钟 → time_frac. FairValue::time_fraction_ 用
                        //   game_row.elapsed_sec / total_game_seconds(sport). 不填则 time_frac=0,
                        //   先验置信永远压在 base 0.15, 真实领先 edge 被 CI 吃掉 → 几乎不成交.
                        game_row.elapsed_sec = static_cast<std::int32_t>(es.clock_sec);
                        game_row.sport = es.sport;  // SportInplaySlug (total_game_seconds 匹配)
                        // P1.1 (特征审计): Goalserve period 字符串 → 1-based 节序数, 喂 g_period (#2)。
                        //   此前 game_row.period 从不赋值 → 恒 0 → g_period 死。无时钟运动 (网球/棒球)
                        //   也由此拿到 set/inning 进度 (P3.2 phase 锚基础)。
                        game_row.period = stcpp::pricing::parse_period_ordinal(es.period, es.sport);
                        // R-20: 4ts 切真 Goalserve ts (禁 book ts / 本地 now() 替代上游).
                        game_row.event_ts_ns = es.ts.event_ts_ns;
                        game_row.data_source_ts_ns = es.ts.data_source_ts_ns;
                        game_row.ingestion_ts_ns = es.ts.ingestion_ts_ns;
                        game_row.as_of_ts_ns = es.ts.as_of_ts_ns;
                        // inplay bet365 de-vig fair → game_row, 按 yes_is_home 翻成 YES-canonical
                        //   (与上面比分同源翻转, 消 home/YES 混淆)。ToYesCanonical 纯函数 BR-1 共用。
                        const auto inplay_yc = stcpp::data::ToYesCanonical(
                            it->second.yes_is_home, es.inplay_bet365_home_fair,
                            es.inplay_bet365_away_fair);
                        game_row.inplay_bet365_home_fair = inplay_yc.yes_fair;  // YES 边胜率
                        game_row.inplay_bet365_away_fair = inplay_yc.opp_fair;  // 对手边胜率
                        // [score-flow diag] 匹配上的 in-play 场是否有 sharp (bet365 de-vig 真值)?
                        //   定位脱节: has_real_fair 场里多少真带 sharp (vs 只 score_prior)。
                        if (es.inplay_bet365_home_fair >= 0.0)
                            sf_sharp.fetch_add(1, std::memory_order_relaxed);
                        game_row.inplay_bet365_draw_fair = es.inplay_bet365_draw_fair;  // 平局 (与边无关)
                        // bm_slots: 跨庄家赔率注入 (getodds 经 inplay-mapping join 到 inplay_match_id,
                        //   RefreshOdds 注入)。按 yes_is_home + map_is_draw 定向 de-vig 折二元 →
                        //   g_bm_devig_p_yes(#5)/overround(#6)/valid_bm_count(#7)/x_devig_minus_mid(#16)。
                        //   查不到 → bm_slots 保持默认 (NaN/valid=false), 特征 NaN, 不造假。
                        if (const auto* mo = OddsFor(it->second.inplay_match_id)) {
                            stcpp::data::goalserve::FillBmSlotsYesCanonical(
                                *mo, it->second.yes_is_home, map_is_draw, game_row);
                        }
                        // live_stats hop: 按 inplay_match_id join (2026-06-02 实测教训: soccernew/live 与
                        //   inplay 的 league_id 与队名两者都不同空间 → 原 (league|home|away) join 永不匹配;
                        //   改 inplay_match_id, RefreshLiveStats 经 inplay-mapping 桥 soccernew→inplay 键)。
                        //   → game_row.soccer_* (g_danger_attack/shot_on_target/possession/red_card/corner_diff)。
                        //   查不到 → soccer_* 保持 -1 (fail-safe, 特征 NaN, 绝不造假)。
                        if (const auto* ls = LiveStatsFor(it->second.inplay_match_id)) {
                            stcpp::data::livescore::FillLiveStats(game_row, *ls);
                            game_row.live_stats_as_of_ns = ls->as_of_ts_ns;  // live_stats 新鲜度
                        }
                    }
                }
            }
        }
    }
    // [score-flow diag] emit: 每 3000 次决策 → 一行链路掉点 (地基可观测; 老板「先打地基才知有什么事件」)。
    if (sf_n % 3000 == 0) {
        std::fprintf(stderr,
                     "[score-flow] calls=%lld mapped=%lld score_found=%lld in-play=%lld stale=%lld "
                     "→ has_real_fair=%lld 其中有sharp=%lld (链路掉点 + sharp 脱节定位)\n",
                     sf_n, sf_mapped.load(), sf_scorefound.load(), sf_inplay.load(), sf_stale.load(),
                     sf_realfair.load(), sf_sharp.load());
    }

    // has_real_fair = true 当 time_status != NotStarted (真实 in-play Goalserve 比分已填).
    // 注: A1 仅打通 fair 计算 + quote 真 edge; advisory gate (Step 4b) 仍拦 intent (A2 解封).
    const bool has_real_fair = (game_row.time_status != stcpp::data::goalserve::TimeStatus::NotStarted);

    // ---- slice-3b/3c 结算 (老板 2026-05-31): 市场已定 → realize 持仓 + 平仓, 之后不交易 ----
    //   触发 + winner 两源 (按权威性):
    //     ① 注入的 REST resolution (3c, 权威): status=Resolved(2) + winner 已知 → 按 winner 结算
    //        (gamma `closed` / clob `tokens[].winner`; 全 market type 通用, 不靠 Goalserve 比分推断)。
    //     ② Goalserve 终态 (3b 兜底): Ended → 按终态比分 winner (moneyline; 无 REST 注入时用)。
    //   持仓最终值 = winner 1 / loser 0。realize → cum_realized; 平仓 → 账本归零。
    //   幂等 (settled_conditions_): 首次结算一次, 之后该盘口跳过决策 (已定不再交易)。
    {
        bool do_settle = false;
        double settle_yes = 0.5, settle_no = 0.5;  // 平局/未知 → push
        const ResolutionEntry* res = ResolutionFor(condition_id);
        if (res != nullptr && res->status == 2 /*Resolved*/ && res->winner >= 0) {
            do_settle = true;  // ① REST 权威
            settle_yes = (res->winner == 1) ? 1.0 : 0.0;
            settle_no = 1.0 - settle_yes;
        } else if (game_row.time_status == stcpp::data::goalserve::TimeStatus::Ended) {
            do_settle = true;  // ② Goalserve 比分兜底
            const int yes_sc = game_row.score_home_total;  // YES 边比分 (orientation 已应用)
            const int opp_sc = game_row.score_away_total;
            if (yes_sc > opp_sc) {
                settle_yes = 1.0;
                settle_no = 0.0;
            } else if (yes_sc < opp_sc) {
                settle_yes = 0.0;
                settle_no = 1.0;
            }
        }
        if (do_settle) {
            if (settled_conditions_.find(condition_id) == settled_conditions_.end()) {
                SettleCondition(condition_id, mkt.yes_token_id, mkt.no_token_id, settle_yes, settle_no,
                                game_row);
                settled_conditions_.emplace(condition_id, char{1});
            }
            return;  // 已定盘口: 不产 quote/intent (持仓已 realize)
        }
    }

    FeatureStoreBookRow book_row{};
    // token_side 始终 YES-canonical (小梁 §3 Step C: FairValueEstimator 只支持 YES 输入;
    // fair_NO=1-fair_YES)。
    book_row.token_side = "YES";
    // L1..L5 bid/ask (P3.1 特征审计: 此前只拷 L1 → b_book_levels_valid 恒 2,
    //   且 spread_bps_f/top3_depth_usdc 从不计算 → b_spread_bps/b_top3_depth 死。
    //   feat.bids/asks 已是 LiveBookPublisher 解析的 5 档真值, 直接透传)。
    for (std::size_t lv = 0; lv < stcpp::data::feature_store::kOrderBookLevels; ++lv) {
        book_row.bid_price[lv] = feat.bids[lv].price;
        book_row.bid_size_usdc[lv] = feat.bids[lv].size_usdc;
        book_row.ask_price[lv] = feat.asks[lv].price;
        book_row.ask_size_usdc[lv] = feat.asks[lv].size_usdc;
    }
    // 微观结构
    book_row.microprice = microprice;
    // #10 审计: 缺失 imbalance 填 NaN 而非 0.0 (0.0 会被读成"双边均衡"假值; NaN=诚实缺失)。
    //   仅喂特征 b_imbalance, FairValueEstimator 只读 microprice 不读 imbalance, 不影响定价。
    book_row.imbalance =
        std::isfinite(feat.imbalance) ? feat.imbalance : std::numeric_limits<double>::quiet_NaN();
    book_row.mid = std::isfinite(feat.mid) ? feat.mid : (best_bid + best_ask) * 0.5;
    book_row.tick_size = 0.01;
    // P3.1: spread_bps_f + top3_depth_usdc (此前漏算)。spread 用已验 L1 价 + mid;
    //   top3 = 前 3 档双边有效 size 之和 (无效/缺档 size 非有限 → 跳过)。
    if (std::isfinite(book_row.mid) && book_row.mid > 0.0) {
        book_row.spread_bps_f = (best_ask - best_bid) / book_row.mid * 10000.0;
    }
    {
        double depth = 0.0;
        for (std::size_t lv = 0; lv < 3 && lv < stcpp::data::feature_store::kOrderBookLevels; ++lv) {
            const double bs = book_row.bid_size_usdc[lv];
            const double as_ = book_row.ask_size_usdc[lv];
            if (std::isfinite(bs) && bs > 0.0)
                depth += bs;
            if (std::isfinite(as_) && as_ > 0.0)
                depth += as_;
        }
        book_row.top3_depth_usdc = depth;
    }
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

    // 盘口定价分派 (数据驱动真 gate, 替代 RM 已删的空壳 enable_xxx 布尔):
    //   moneyline 走 FairValueEstimator (score_diff→p_yes); totals/spreads 走专属派生定价模型
    //   (derivative_fair_value: 终场分布建模 + line 比较 — 老板 2026-05-31「缺盘口模型就加」)。
    //   outright/prop/series 暂无模型 → fail-closed。派生定价 invalid (赛前/太早/无时钟) 亦 fail-closed。
    const MarketCat mc = MarketCatFor(condition_id);
    const std::int32_t mkt_type = mc.market_type_id;
    std::optional<double> derivative_p_yes;  // totals/spreads 专属定价 (有值 → 覆盖 moneyline p_fair)
    // 市场兜底标志: outright/prop/series 无专属 score 模型 → 不"未接入", 改发市场 de-vig 公允
    //   (edge≈0, 诚实标 market_devig, 不交易)。关键: 强制挡 score-prior/sharp/ML (否则匹配到某场
    //   比赛会用单场比分当冠军/系列概率 = 垃圾, 同 cricket 教训)。
    bool market_implied = false;
    // sport-aware 分派: 网球(set/game)/电竞(maps) 走专属离散模型; 连续时钟运动走 derivative(Poisson/Normal)。
    const bool is_tennis = (game_row.sport == "tennis");
    const bool is_esports = (game_row.sport == "esports");
    if (mkt_type == 2) {  // totals (大小分 / 网球总局 / 电竞总图)
        const pricing::DerivativeFairResult dr =
            is_tennis    ? pricing::TennisTotalsFairYes(game_row, mc.line, mc.yes_is_over)
            : is_esports ? pricing::EsportsTotalsFairYes(game_row, mc.line, mc.yes_is_over)
                         : pricing::TotalsFairYes(game_row, mc.line, mc.yes_is_over);
        if (dr.valid)
            derivative_p_yes = dr.p_yes;
        else
            market_implied = true;  // 模型不可用 (赛前/太早/不支持运动/无 line/终态) → 市场兜底 (不"未接入")
    } else if (mkt_type == 1) {  // spreads (让分 / 网球让局 / 电竞图让分)
        const pricing::DerivativeFairResult dr =
            is_tennis    ? pricing::TennisSpreadsFairYes(game_row, mc.line)
            : is_esports ? pricing::EsportsSpreadsFairYes(game_row, mc.line)
                         : pricing::SpreadsFairYes(game_row, mc.line);
        if (dr.valid)
            derivative_p_yes = dr.p_yes;
        else
            market_implied = true;  // 市场兜底
    } else if (mkt_type > 2) {
        // outright/prop/series: 无专属 score 模型 → 市场兜底 (发 quote, fair=市场 de-vig, 不交易)。
        market_implied = true;
    }

    // derivative 有效性兜底 (2026-06-03 修 fair=0.999/0.001 垃圾单, catch-all): totals/spreads 模型对
    //   子盘口格式 (网球 sets/games/分盘 · 电竞 maps/kills/BO3-BO5 · 让分 handicap) 有多重假设, 假设被
    //   违反时输出【钉在 clamp】的极端 fair (0.001/0.999), 而市场价仍正常 → 假大 edge 垃圾单。单位守卫
    //   (tennis line 比例 / esports line>9) 已挡明显的, 此处通用兜底: derivative 钉极端而市场不极端 →
    //   判定格式错配 → 弃用 derivative, 走市场兜底 (不产垃圾 fair, 不交易)。真实 in-play 派生大 edge 极少
    //   恰好钉在 clamp; 误挡的 paper 期可观测再放宽 (老板「虚拟盘别太谨慎」— 但垃圾单更污染调模型反馈)。
    if (derivative_p_yes.has_value()) {
        const double dp = *derivative_p_yes;
        const bool deriv_pinned = (dp <= 0.02 || dp >= 0.98);
        const bool mkt_normal = (p_market_devig >= 0.05 && p_market_devig <= 0.95);
        if (deriv_pinned && mkt_normal) {
            static std::atomic<int> deriv_dbg{0};
            if (deriv_dbg.fetch_add(1, std::memory_order_relaxed) < 60)
                std::fprintf(stderr,
                             "[deriv-sanity] 弃用钉极端派生 fair (格式错配?) cond=%.16s mkt_type=%d "
                             "deriv=%.4f mkt_devig=%.4f → 市场兜底\n",
                             condition_id.c_str(), mkt_type, dp, p_market_devig);
            derivative_p_yes = std::nullopt;
            market_implied = true;
        }
    }

    // ---- P0-3 / P1-8 fair 锚定 --------------------------------------------
    // 默认 (无真实 Goalserve 先验, M1 stub 路径): p_fair = p_market_devig.
    //   → edge ≈ 0 (锚在去 vig 的市场上自己跟自己比), 配合 has_real_fair gate 不产 intent.
    //   这根除了"低价 outright 被 stub 强拉 → 假 edge"(Spain 0.169 → fake 1076bps).
    // 有真实 in-play game_row 时: 用 score-prior 置信加权混合到 de-vig 市场锚上,
    //   置信随时钟从 kBasePriorConfidence 升到 kMaxPriorConfidence; 终态 conf=1.0.
    double p_fair = p_market_devig;  // 最终由 ResolveFair 一处解析 (优先级集中在 fair_resolve.hpp; R-2 老周/老郭)
    pricing::FairSrc fair_src_dbg = pricing::FairSrc::kMarketDevig;  // [diag] 捕获 ResolveFair 真实选源
    // fair-input 标量: has_real_fair 块内填; sharp<0=无效 → ResolveFair 回落 score-prior。derivative 在下面 optional。
    double fair_sharp_yes = -1.0;
    double fair_score_prior = 0.5;
    double fair_prior_conf = 0.0;
    // 3a 时序: 结算临近度 (老板 2026-05-31, feature-first; 体育免新数据源 — 从 Goalserve 时钟派生)。
    //   = clamp(1 − time_frac, 0, 1); terminal → 0 (结算已定); 无真 fair/无时钟 → NaN。喂模型 +
    //   与 bid_absence_frac 组合 = 「临近结算 ∧ 卖不出」归零陷阱信号 (模型学, 不硬门)。
    double time_to_resolution_frac = std::numeric_limits<double>::quiet_NaN();
    double g_time_x_lead = std::numeric_limits<double>::quiet_NaN();  // 批1: 时间感知领先 (体育最大非线性)
    double g_remaining_sec = std::numeric_limits<double>::quiet_NaN();  // 批1 补漏: 剩余秒
    SportsFeatures sports;  // 批1 体育动态 (game_row.score/live_stats 派生; 无真比分→NaN)
    // 批1 补漏 g_periods_won: 已完成节中各队领先节数 (score_*_periods[]; 有真实比分才有意义)。
    std::int32_t g_periods_won_home = 0, g_periods_won_away = 0;
    if (has_real_fair) {
        const std::uint8_t np = game_row.last_completed_period;
        for (std::uint8_t i = 0; i < np && i < game_row.score_home_periods.size(); ++i) {
            if (game_row.score_home_periods[i] > game_row.score_away_periods[i])
                ++g_periods_won_home;
            else if (game_row.score_away_periods[i] > game_row.score_home_periods[i])
                ++g_periods_won_away;
        }
    }
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
        // fair 优先级集中到 ResolveFair (R-2 老周/老郭): 此处仅捕获 fair-input 标量, 不直接定 p_fair。
        //   sharp = bet365 in-play de-vig 共识 (盈利修复; 3-way 平局盘取 draw 概率, 胜负盘取 YES-canonical
        //   home_fair 已按 yes_is_home 翻转)。<0/越界 → ResolveFair 回落 score-prior blend (fail-safe)。
        fair_score_prior = fv_result.prior_yes;
        fair_prior_conf = terminal ? 1.0 : pricing::prior_confidence(time_frac);
        // 护栏 (2026-06-03): cricket 等比分不适配 goals-like 先验的运动 → prior_conf=0
        //   (score-prior 零拉力 → ResolveFair 回落市场 de-vig → 覆盖但不在垃圾 fair 上交易)。
        //   cricket innings 制 runs 差饱和 sigmoid 详见 pricing::score_prior_applicable。
        if (!pricing::score_prior_applicable(game_row.sport))
            fair_prior_conf = 0.0;
        fair_sharp_yes = map_is_draw ? game_row.inplay_bet365_draw_fair : game_row.inplay_bet365_home_fair;
        time_to_resolution_frac = terminal ? 0.0 : std::clamp(1.0 - time_frac, 0.0, 1.0);
        // 批1 g_time_x_lead: 领先 × 剩余时间占比 (领先 1 球在 80min vs 20min 价值天差地别)。
        g_time_x_lead = score_diff * std::clamp(1.0 - time_frac, 0.0, 1.0);
        // 批1 补漏 g_remaining_sec: 剩余秒 = total × 剩余占比。
        g_remaining_sec = (total_sec > 0) ? static_cast<double>(total_sec) * time_to_resolution_frac : 0.0;

        // 批1 体育动态: 比赛阶段/垃圾时间/关键时段 (点) + 进球新鲜度/动量 (game ring) + live_stats 差。
        // P3.2 (特征审计): 无时钟运动 (网球/棒球/排球 total_sec=0) 用 period 进度当 phase 锚,
        //   否则 phase/garbage/clutch 恒 0。phase_frac 仅供 phase 类特征, 不动 time_frac (后者喂定价
        //   prior_confidence, 改它=改无时钟运动定价, 属量化 owner 范围, 此处不越界)。
        double phase_frac = time_frac;  // 有时钟运动: 直接用时钟占比 (行为不变)
        if (total_sec == 0) {
            const int reg = pricing::regulation_periods(game_row.sport);
            if (reg > 0 && game_row.period > 0) {
                phase_frac = std::clamp(
                    (static_cast<double>(game_row.period) - 0.5) / static_cast<double>(reg), 0.0, 0.999);
            }
        }
        sports.game_phase = std::floor(std::clamp(phase_frac, 0.0, 0.999) * 3.0);  // 0早/1中/2末
        const double abs_diff = std::abs(score_diff);
        sports.garbage_time = (phase_frac > 0.85 && abs_diff >= 3.0) ? 1.0 : 0.0;
        sports.clutch = (phase_frac > 0.85 && abs_diff <= 1.0) ? 1.0 : 0.0;
        // 比分时序 ring: Observe (as_of 上游观测刻, 禁 now()) → 进球新鲜度 + 5min 动量。
        auto& gh = game_history_[condition_id];
        gh.Observe(feat.as_of_ts_ns, game_row.score_home_total, game_row.score_away_total);
        sports.goal_freshness = gh.GoalFreshness(feat.as_of_ts_ns);
        sports.net_momentum_5m = gh.NetMomentum(300'000'000'000LL);
        // live_stats 差 (小段字段; -1=无数据→NaN; 白名单+livescore client 后流入)。
        auto sdiff = [](std::int32_t h, std::int32_t a) -> double {
            return (h >= 0 && a >= 0) ? static_cast<double>(h - a) : std::numeric_limits<double>::quiet_NaN();
        };
        sports.danger_attack_diff =
            sdiff(game_row.soccer_dangerous_attacks_home, game_row.soccer_dangerous_attacks_away);
        sports.shot_on_target_diff =
            sdiff(game_row.soccer_shots_on_target_home, game_row.soccer_shots_on_target_away);
        sports.possession_home = (game_row.soccer_possession_home_pct >= 0)
                                     ? static_cast<double>(game_row.soccer_possession_home_pct)
                                     : std::numeric_limits<double>::quiet_NaN();
        sports.red_card_diff = sdiff(game_row.soccer_red_cards_home, game_row.soccer_red_cards_away);
        sports.corner_diff = sdiff(game_row.soccer_corners_home, game_row.soccer_corners_away);
        // inplay bet365 de-vig fair (ParseInplayOddsDevig 填 game_row; -1=无 odds → NaN)。
        sports.bm_inplay_fair = (game_row.inplay_bet365_home_fair >= 0.0)
                                    ? game_row.inplay_bet365_home_fair
                                    : std::numeric_limits<double>::quiet_NaN();
    }

    // ---- ML 驱动决策 blend (老板 2026-05-31 放开 paper 期 ML-R2) ----
    //   真 ONNX 模型加载时, blend ML fair 进决策 p_fair (YES-canonical)。安全护栏:
    //   ① 仅 kind==Onnx (stub 永不驱动决策, 无真模型→纯 baseline) ② ready+维度匹配+predict ok
    //   ③ PaperLoop 天然 paper (VirtualFill 不花真钱; live 路径另接, 绝不复用此 blend 驱动真单)。
    //   特征经 PopulateFeatureColumns 与 PublishQuoteSnapshot 同源 (BR-1: 训练捕获=决策推理一致)。
    //   ⚠ derivative 盘口 (totals/spreads) 不 blend: 当前 ONNX 是 moneyline 语义, blend 进派生 p_fair 会污染
    //     (派生解析模型即该盘口的 fair)。未来 market-type-aware ONNX 上线再放开 (cat_market_type 特征已就位)。
    //   R-2: ML 推理 (非纯) 在此算 ml_p_opt, blend 算术交 ResolveFair (一处定优先级)。
    std::optional<double> ml_p_opt;
    // 热加载: Load() 拿当前模型 shared_ptr (本次推理引用期内不被 daemon watcher 换走/回收)。
    const auto ml_model = ml_holder_.Load();
    // (2026-06-03 老板「优化模型不缩减场景」: 去掉 has_real_fair 门 — 模型 Platt 校准后全场景用,
    //   过度自信被校准治住, 不需门挡。无比分盘模型从微结构特征预测, 校准后≈市场不产极端。)
    if (cfg_.ml_fair_blend_weight > 0.0 && !derivative_p_yes && ml_model != nullptr &&
        ml_model->ready() && ml_model->kind() == ml::ModelKind::Onnx) {
        sizing::QuoteFeatures fqf{};
        const double blend_no_imb =
            mkt.no.present ? mkt.no.book.imbalance : std::numeric_limits<double>::quiet_NaN();
        const std::int64_t blend_joint = std::min(game_row.as_of_ts_ns, feat.as_of_ts_ns);
        const std::int64_t blend_no_ds = mkt.no.present ? mkt.no.book.data_source_ts_ns : 0;
        const std::int64_t blend_no_ing = mkt.no.present ? mkt.no.book.ingestion_ts_ns : 0;
        PopulateFeatureColumns(fqf, condition_id, fv_result, microprice, feat, cross_spread, no_token_mid,
                               blend_no_imb, devig_ok, blend_joint, mkt.event_id, mkt.neg_risk_market_id,
                               time_to_resolution_frac, g_time_x_lead, g_fld_signal, g_remaining_sec,
                               g_periods_won_home, g_periods_won_away, sports, blend_no_ds, blend_no_ing,
                               mkt.no.present ? &mkt.no.book : nullptr);
        const ml::FeatureVector fv = ml::extract_full(game_row, book_row, fqf);
        if (fv.size() == ml_model->expected_feature_count()) {
            const auto mp = ml_model->predict(fv);
            const double ml_p = mp.prob(0);
            // 校准门 (2026-06-03 紧急修): 只有【已校准】(calibrated && conf>0) 的模型才驱动 fair。
            //   未训练/退化模型 (无 sidecar → calibrated=false / conf=0) 会输出 ~恒定垃圾 (实测
            //   ~0.9995) → weight=1.0 下 fair=垃圾 → 全 moneyline 假 edge → 垃圾成交。此门挡掉
            //   (与前端 modelReady 同口径); 等 auto-train 训出带 sidecar 的真模型才放行驱动。
            // fail-safe: ML 输出非有限 (NaN 特征/数值) → 不 blend, 保 baseline (宁可不动不可乱动)。
            if (mp.ok && mp.calibrated && mp.confidence > 0.0 && std::isfinite(ml_p) && ml_p > 0.0 &&
                ml_p < 1.0) {
                ml_p_opt = ml_p;
            }
        }
    }

    // ---- R-2 (老周/老郭 评审): 一处解析决策 fair (显式优先级 derivative > sharp > score-prior; ----
    //   ML 仅非 derivative 叠加)。逐位等价原 inline 四层逻辑; 纯函数可单测 (fair_resolve.hpp)。
    {
        pricing::FairInputs fin;
        fin.p_market_devig = p_market_devig;
        fin.derivative_p_yes = derivative_p_yes;
        fin.sharp_yes = fair_sharp_yes;
        fin.score_prior_yes = fair_score_prior;
        fin.prior_conf = fair_prior_conf;
        fin.has_real_fair = has_real_fair;
        // ML 驱动总开关 (默认关): 关 → ml_p 仅 advisory (qf.ml_advisory_p_yes 仍记录/前端显示),
        //   但【不进 fair】→ 不驱动交易。验证通过 + 策略评审后才 ml_drive_enabled=true 放行。
        fin.ml_p_yes = cfg_.ml_drive_enabled ? ml_p_opt : std::nullopt;
        fin.ml_blend_weight = cfg_.ml_fair_blend_weight;
        if (market_implied) {
            // outright/prop/series 市场兜底: 挡 score-prior/sharp/derivative/ML → fair = 纯市场 de-vig。
            //   单场比分/匹配的 sharp 对"冠军/系列"语义错误, 必须挡 (防垃圾 fair); edge≈0 不交易。
            fin.derivative_p_yes = std::nullopt;
            fin.sharp_yes = -1.0;
            fin.prior_conf = 0.0;
            fin.has_real_fair = false;
            fin.ml_p_yes = std::nullopt;
        }
        const auto fr = pricing::ResolveFair(fin);
        p_fair = fr.p_fair;
        fair_src_dbg = fr.src;  // [diag] 真实选源 (sharp_inplay / score_prior_blend / ...)
        // [fair-sanity] 防垃圾门 (2026-06-03): fair 与市场极端背离 (>kMaxPlausibleEdge) = 大概率
        //   orientation 翻转 / EventMatcher 误配 / 模型饱和 (实测 inplay sharp de-vig clamp 0.9995 被
        //   贴到便宜 underdog YES → 假 86% edge → 垃圾成交)。真实体育 edge 极少 >0.45 → fail-closed
        //   不交易, 并 log src + 成分定位根因 (老韩式 edge 合理性上界)。
        // 改 log-only (2026-06-03, 老板「别那么谨慎, 还是虚拟盘」): 原阻断门是 ML 垃圾事故的应急止血,
        //   但 ML 已由 ml_drive_enabled(默认关)+校准门挡住, 此门反而拦合法 derivative/sharp 大 edge
        //   (破 T17 totals 测试)。改只记录极端背离 (诊断), 不阻断 — 垃圾防护由 ML 闸 + 校准门承担。
        // [fair-sanity] 纯诊断 log (不拦; 老板「优化模型不加门」)。模型 Platt 校准后过度自信被治,
        //   极端背离应大幅减少; 仍记录供观测 (若校准后还频繁极端 = 校准不足, 继续优化模型而非加门)。
        constexpr double kMaxPlausibleEdge = 0.45;
        if (std::abs(p_fair - p_market_devig) > kMaxPlausibleEdge) {
            // 2026-06-03 老板「调通模型让其盈利」: 实测 PM 体育盘高效 (事件延迟/信息边验证), 模型源
            //   (ml_blend/score_prior_blend) 对市场极端背离 (>0.45) = 大概率模型错配/过度自信 (实见
            //   tennis/esports ml_blend fair 0.35~0.43 vs 市场 0.85~0.95) → 回退市场价 (edge 归零, 不
            //   产单)。sharp_inplay/derivative 的大 edge 是合法信号 (sharp 钱 / totals 错价), 不拦, 仅记录。
            const bool model_src = (fr.src == pricing::FairSrc::kMlBlend ||
                                    fr.src == pricing::FairSrc::kScorePriorBlend);
            static std::atomic<int> sanity_dbg{0};
            if (sanity_dbg.fetch_add(1, std::memory_order_relaxed) < 80)
                std::fprintf(stderr,
                             "[fair-sanity] 极端背离 cond=%.24s sport=%s mkt_type=%d src=%s "
                             "p_fair=%.4f mkt_devig=%.4f%s\n",
                             condition_id.c_str(), game_row.sport.c_str(), mkt_type,
                             pricing::to_string(fr.src), p_fair, p_market_devig,
                             model_src ? " → 模型源回退市场(不交易)" : " (sharp/deriv, 合法, 仅记录)");
            if (model_src) p_fair = p_market_devig;  // 模型误配 → edge 归零, SelectSide 不产单
        }
    }

    // ---- sharp +EV 门 (2026-06-03 老板「改成 sharp 驱动」, 历史回测确认) ----
    //   42 万已结算行回测: src=sharp_inplay 且 |sharp − 市场| ≥ 5% 时, 结算结果站 sharp 77%,
    //   净 +0.20/单 (≥10% 偏离 98%/+0.36); <5% 偏离是噪声(亏); ML/score-prior/derivative 源无
    //   回测确认 edge。→ 只交易高置信 sharp 信号, 其余回退市场(edge 归零, 不产单)。
    //   (ML 驱动已关 ml_drive_enabled=false → fair 落 sharp/score-prior/market; 此门再收到只剩 sharp。)
    //   2026-06-04: 收进 cfg_.sharp_only_gate (默认关) —— 这是【策略过滤器】非管线不变量, 无条件施加会
    //   把通用 fill 管线/契约单测的非 sharp fair 全归零 (T17/T_Profit/TS4… 9 测试)。生产 daemon 置 true。
    if (cfg_.sharp_only_gate) {
        const bool sharp_signal = (fair_src_dbg == pricing::FairSrc::kSharpInplay) &&
                                  (std::abs(p_fair - p_market_devig) >= cfg_.sharp_only_min_edge);
        if (!sharp_signal) p_fair = p_market_devig;  // 非高置信 sharp → 不产单
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

    // ---- Phase 0 联合评审 (2026-05-31): 动态 n_eff (项1) + margin_floor (项2) + OFI (项4) ----
    //   从 condition 时序环派生, 替静态 cfg_。BR-1 仅决定 reservation/CI 宽度, 不碰 fair。
    const std::int64_t ts_w = cfg_.ts_feature_window_ns;
    int n_eff_dyn = cfg_.n_effective;
    double margin_floor_dyn = cfg_.edge_ci_lower_floor;
    double b_ofi_dyn = std::numeric_limits<double>::quiet_NaN();
    {
        const auto yh = ts_history_.find(condition_id);
        if (yh != ts_history_.end()) {
            b_ofi_dyn = yh->second.OFI(ts_w);  // 项4 force_cross 用 (always; 仅读不改门)
            if (cfg_.dynamic_reservation) {     // 项1+2 生产开关 (lib 默认 false 向后兼容)
                const int ys = static_cast<int>(yh->second.WindowSampleCount(ts_w));
                const auto nh = ts_history_no_.find(condition_id);
                const int ns = (nh != ts_history_no_.end())
                                   ? static_cast<int>(nh->second.WindowSampleCount(ts_w))
                                   : ys;
                // 项1: n_eff = clamp(min(YES,NO 样本), n_eff_min, n_eff_max)。样本足→收窄 CI, 稀疏→展宽;
                //   clamp 下限防 samples=1 时 sigma=0.5 reservation 永不成交 (数值小肖静默失效护栏)。
                n_eff_dyn = std::clamp(std::min(ys, ns), cfg_.n_eff_min, cfg_.n_eff_max);
                // 项2: margin_floor = max(static_floor, amihud_coef×amihud, 0.5×cross_spread)。
                //   半 vig (0.5×cross_spread) 经济地基: 至少赚回付出的半边 vig 才有净 edge。amihud 待校准。
                const double amh = yh->second.AmihudApprox(ts_w);
                const double amh_term = (std::isfinite(amh) && cfg_.amihud_margin_coef > 0.0)
                                            ? cfg_.amihud_margin_coef * amh
                                            : 0.0;
                const double vig_term =
                    std::isfinite(cross_spread) ? std::max(0.0, 0.5 * cross_spread) : 0.0;
                margin_floor_dyn = std::max({cfg_.edge_ci_lower_floor, amh_term, vig_term});
            }
        }
    }

    // ---- Step G: 被选边 fair / edge_ci (小梁 §2 de-vig 对称代数) ----
    //   被选边 fair/共识互余 (fair_NO=1-fair_YES, devig_NO=1-devig_YES) → edge_ci 用 canonical
    //   ComputeEdgeCiLower(被选边 fair, 被选边共识) 即对; de-vig 互余 → sigma 两边相等。
    //   (老郭 impl review nit#2: 用单一 ComputeEdgeCiLower, 消同公式两处实现的漂移风险;
    //    YES 路径 == 旧 ComputeEdgeCiLower(p_fair, p_market_devig) 逐位不变。)
    const double p_fair_selected = is_yes ? p_fair : (1.0 - p_fair);
    const double p_devig_selected = is_yes ? p_market_devig : (1.0 - p_market_devig);
    // 注 (老板「别草率守门」2026-05-31): cross_spread(vig) 不接 CI 硬收紧 — 只作模型输入(进 QuoteFeatures),
    //   让模型/策略学 vig 影响, 是否用它调门留给「守门审计 + 小梁/老韩」定。这里维持原 n_eff (不加守门)。
    // 源感知 edge 下界 (老板 2026-06-03「sharp 路径纯 net-EV 门」, 见 edge_ci.hpp ResolveEdgeCiLower):
    //   sharp_inplay = bet365 de-vig 共识【点估计】, 二项抽样惩罚 (n_eff≈6 → ~0.22) 对它是错误模型,
    //   砍杀全部 in-play 套利 → 永不成交。sharp 源改纯 net-EV (raw_edge, margin 交下游 slippage/fee/net_ev 门);
    //   score-prior/ML 源仍走二项 CI (确为噪声估计)。fair_src_dbg 即 ResolveFair 真实选源。
    // paper_no_edge_gates (老板「把门都去了, 调模型」): 全源走 raw_edge (不扣二项噪声), margin=0。
    //   否则只 sharp 源走 raw, 其余 (score-prior/ML) 仍二项 CI。
    const bool fair_is_sharp = (fair_src_dbg == pricing::FairSrc::kSharpInplay);
    const double edge_ci_lower = stcpp::strategy::ResolveEdgeCiLower(
        fair_is_sharp || cfg_.paper_no_edge_gates, p_fair_selected, p_devig_selected, n_eff_dyn, cfg_.z_90,
        cfg_.paper_no_edge_gates ? 0.0 : cfg_.sharp_edge_margin);

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
    // [2026-06-01 凯利评审 D1/D2, docs/MEETINGS/2026-06-01-kelly-equity-review.md] bankroll 动态化:
    //   原硬用 cfg_.bankroll_usdc 静态初值 = 老韩裁定「风控纸面化」(回撤不缩盈利不涨, 复利输入断)。
    //   老板拍板: 凯利分母 = 裸 equity 含浮盈 (几何增长最优), 浮盈用 best_bid 保守价估 (化解浮盈幻觉)。
    //   → bankroll_for_kelly = tick_equity_.equity_bid (= realized_equity + best_bid 未实现, tick 冻结同版本)。
    //   fail-closed (老韩硬约束): equity 非有限 → 回落 min(initial, realized), 绝不回落大值偷偷放大;
    //   ≤0 → 喂 0 → sizing validate_input 走 NO_EDGE 全 0 (停手)。
    double bankroll_for_kelly = tick_equity_.equity_bid;
    if (!std::isfinite(bankroll_for_kelly)) {
        bankroll_for_kelly = std::min(cfg_.bankroll_usdc, tick_equity_.realized_equity);
    }
    bankroll_for_kelly = std::max(0.0, bankroll_for_kelly);
    sz_in.bankroll_usdc = bankroll_for_kelly;
    sz_in.fill_rate = 0.65;    // 保守固定 (M1)
    sz_in.slippage_bps = 8.0;  // 保守固定 (M1)
    sz_in.buy_yes = is_yes;
    sz_in.fee_rate_coef = FeeCoefFor(condition_id);  // R-fee-2: per-market 真值 (gamma feeSchedule.rate)
    sz_in.no_edge_gate = cfg_.paper_no_edge_gates;   // 老板「把门都去了」: 跳过 sizing edge 门 (Step1/2/3)

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
    // [2026-06-01 凯利评审 Step2, 老韩] cap 链净值缩放: 回撤时绝对 cap (C1-3) 按净值比例同步砍, 不让 drawdown
    //   中单笔绝对暴露相对放大。scale = clamp(bankroll_for_kelly / initial, 0.5, 1.0)。sizing-only 更严 (不松
    //   RM 口径, 合规 R-1); floor=0.5 (老韩拍, 跌破再深绝对 cap 最多砍半); 盈利时 clamp 1.0 (cap 是绝对上限不放大)。
    const double cap_scale =
        cfg_.bankroll_usdc > 0.0
            ? std::clamp(bankroll_for_kelly / cfg_.bankroll_usdc, 0.5, 1.0)
            : 1.0;
    risk::RiskConfig sizing_cfg{};
    sizing_cfg.per_order_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.per_order_cap_usdc * cap_scale);
    sizing_cfg.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.market_exposure_cap_usdc * cap_scale);
    sizing_cfg.per_outcome_cap_usdc = domain::MicroPUSD::from_pusd(cfg_.per_outcome_cap_usdc * cap_scale);
    const sizing::SizingOutput sizing_out = sizing::SizingCalculator::compute(sizing_cfg, sz_in);

    // ---- Step 3 (老雷 controller spec v1, 小梁 Q-梁-1): reservation 限价界 ----
    //   required_margin = max(edge_ci_lower_floor, z×sqrt(p(1−p)/n)); reservation_buy/sell 对称。
    //   被选边视角 (p_fair_selected); fee 锚在各自触价 (买 exec_ask / 卖 exec_bid)。BR-1 共用纯函数。
    const double exec_bid = exec_feat.best_bid();
    const control::ReservationPrices reservation = control::ComputeReservation(control::ReservationInput{
        /*fair=*/p_fair_selected,
        /*exec_ask=*/exec_ask,
        /*exec_bid=*/exec_bid,
        /*fee_coef=*/sz_in.fee_rate_coef,
        /*margin_floor=*/margin_floor_dyn,
        /*z=*/cfg_.z_90,
        /*n_eff=*/n_eff_dyn,
    });
    // 目标仓位 (老板 2026-05-31): |target| = Kelly suggested_notional (受 cap/bankroll 约束)。
    //   H-3 非对称: 无真实 fair / sizing 无效 / de-vig 失败 → target=0 (不开新仓; 减仓由 gap<0 涌现)。
    //   target_signed: +多 YES / −多 NO(=空 YES) (观测/训练; 控制器按被选边 long magnitude 运行)。
    // Phase 0 项3 (net-EV 预筛): edge < 2×fee_per_unit + slippage → 不开新仓 (防 fee 流血;
    //   微观/小梁评审)。fee_per_unit 锚在 exec_ask; slippage 用 sizing 同值。仅 gate 开仓, 减仓由 gap<0 涌现。
    const double fee_pu = sz_in.fee_rate_coef * exec_ask * (1.0 - exec_ask);
    const double slippage_frac = sz_in.slippage_bps / 10'000.0;
    const double net_ev_edge = std::abs(p_fair_selected - p_devig_selected);
    // 老板「把门都去了」: 调模型模式跳过 net-EV 门。
    const bool net_ev_ok =
        cfg_.paper_no_edge_gates || !cfg_.net_ev_gate || (net_ev_edge >= (2.0 * fee_pu + slippage_frac));
    // target 放行条件: 常规要 has_real_fair (Goalserve 比分); 调模型模式放行模型驱动 fair (pre-game ml_blend)
    //   + sharp, 让模型在其训练域 (pre-game) 也能自主交易。sizing_out.valid 已保证 net edge>0 (真有 edge 才动),
    //   src=market_devig (无 fair) 时 edge=0 → sizing 无效 → 不交易, 故放行安全 (不会在无 fair 盘乱开)。
    const bool tradeable_fair = has_real_fair || cfg_.paper_no_edge_gates;
    const double target_mag =
        (tradeable_fair && sizing_out.valid && devig_ok && net_ev_ok) ? sizing_out.suggested_notional : 0.0;
    const double target_signed = is_yes ? target_mag : -target_mag;

    // [decision-diag] 定位 sharp→可下单侧 脱节 (老板「为什么有 sharp 的源进不了可下单侧」)。
    //   仅 has_real_fair + 有 sharp 的盘, 节流打印: 真实选源/p_fair/devig/sharp_in/edge_ci/sizing 是否有效/
    //   net_ev 是否过/target。一眼看出 sharp 是否被选为 fair, 以及 valid/edge 在哪一步被砍成 0。
    if (has_real_fair && fair_sharp_yes >= 0.0 && fair_sharp_yes <= 1.0) {
        static std::atomic<int> dd_n{0};
        const int k = dd_n.fetch_add(1, std::memory_order_relaxed);
        if (k < 80)
            std::fprintf(stderr,
                         "[decision-diag] %s src=%s pfair=%.3f devig=%.3f sharp_in=%.3f side=%s "
                         "edge_ci=%.4f szvalid=%d net_ci=%.4f net_ev_ok=%d target=%.1f map_draw=%d\n",
                         condition_id.substr(0, 12).c_str(), pricing::to_string(fair_src_dbg), p_fair,
                         p_market_devig, fair_sharp_yes, is_yes ? "YES" : "NO", edge_ci_lower,
                         sizing_out.valid ? 1 : 0, sizing_out.net_ci_edge, net_ev_ok ? 1 : 0, target_mag,
                         map_is_draw ? 1 : 0);
    }

    // ---- Step 4: QuoteSnapshotHub::Publish ---------------------------------
    // 无论下单与否, 发布 quote 快照 (供 /api/v1/quote 端点显示真实估值)
    // P0-3: has_real_fair=false 时, 传递 suppress_edge=true → 清零伪 edge 字段.
    // A2: 双边微观结构 + 联合新鲜度透传 (全部模型输入+观测, 不接 gate)。
    //   joint_as_of = min(score.as_of, book.as_of) — 联合新鲜度 (老板「相对最近刷新」; 绝不 gate)。
    const double no_imbalance =
        mkt.no.present ? mkt.no.book.imbalance : std::numeric_limits<double>::quiet_NaN();
    const std::int64_t joint_as_of_ts_ns = std::min(game_row.as_of_ts_ns, feat.as_of_ts_ns);
    // 步④/v0.3: ML 推理移到 PublishQuoteSnapshot 末尾 (qf 全特征就位后, extract_full 含双边时序/持仓
    //   24-53 列)。此处传 game_row + book_row 供 extract_full 填 0-23 原始 game/book 列。
    // 派生盘口 (totals/spreads): 用派生 fair 替换 fv_result, 使捕获的 qf.fair_value / fair_ci / baseline_fair
    //   反映派生模型 (非 moneyline 估值) — 否则训练数据 fair 列对 totals/spreads 是错的 (BR-1 一致性)。
    pricing::FairValueResult publish_fv = fv_result;
    if (derivative_p_yes) {
        const double pd = std::clamp(*derivative_p_yes, 1e-6, 1.0 - 1e-6);
        publish_fv.probs = {pd, 1.0 - pd};
        publish_fv.prior_yes = pd;
        publish_fv.valid = true;
    }
    PublishQuoteSnapshot(condition_id, publish_fv, sizing_out, mark_price, edge_ci_lower, feat, has_real_fair,
                         cross_spread, no_token_mid, no_imbalance, devig_ok, joint_as_of_ts_ns, mkt.event_id,
                         mkt.neg_risk_market_id, target_signed, reservation.buy_px, reservation.sell_px,
                         reservation.required_margin, time_to_resolution_frac, g_time_x_lead, g_fld_signal,
                         g_remaining_sec, g_periods_won_home, g_periods_won_away, sports, game_row, book_row,
                         mkt.no.present ? mkt.no.book.data_source_ts_ns : 0,
                         mkt.no.present ? mkt.no.book.ingestion_ts_ns : 0,
                         mkt.no.present ? &mkt.no.book : nullptr);  // v0.8 NO book 5档深度
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

    // ---- 红线2 (老韩): 无市场锚 fail-closed -------------------------------
    // de-vig 失败 → 无可用市场锚 → reservation 不可信, 无信号绝不伪造 (即便有持仓也不动)。
    // 注 (H-3 非对称, 老韩 Q-韩-2): has_real_fair=false **不再**早退。stub fair → target=0 →
    //   控制器 gap=target−current≤0 → 只可减仓 (撤减仓侧 gate), 绝不开新仓 (留开仓侧 stub→0)。
    //   无持仓时 gap=0 → ZeroGap → 不动 (解封但无真 fair + 无仓 → 零 intent, 红线2 兜底)。
    if (!devig_ok) {
        return;
    }

    // ---- Step 4 (老雷 controller spec v1) + M2-a (选边翻转平旧边): 双边目标仓位控制 ----
    // 每 tick 对盘口两边各驱动到目标 (目标仓位范式的完整形态):
    //   被选(低估)边: target = Kelly magnitude → 买增至目标。
    //   非选(高估)边: target = 0 → 平旧边 (若有持仓)。选边翻转后旧边自动变非选边, 由此收敛回 flat。
    // 安全 (老韩 C1 不触发 / 老周 Q-周-2 范围内): 平旧边是**减仓** (long→0, 不穿零跨 0, 不开空) →
    //   H-1 signed cap magnitude 放行减仓 + H-2 反向穿零不触发 + avg_entry 归零 (现成账本, 无需 reverse 修)。
    //   churn 自抑: 刚买的边 bid<fair+margin → reservation_sell 平不 marketable → 不会买完立刻平
    //   (限价不追内生防抖; 仅当旧边真 overpriced=bid≥fair+margin 才平 = 取利平仓)。
    // M2-b 注: sell-to-open 真开空对二元市场结构性 N/A (CTF 不能持负余额; 空 YES≡持 NO; signed
    //   target 已由两腿 long-only 张成)。老郭 C1: 负 delta 跨零才令 condition cap 失真, M2-a 只平不开空
    //   不触发。故 M2-b 仅留「强制穿越防抖」(下方; 真开空机器不建)。详见 spec §11.6。

    // 强制穿越 (小梁 Q-梁-2): YES-canonical p_fair 较上 tick 大跳 (|Δ|>阈, e.g. 进球) → 绕死区不堵。
    //   loop_thread_ 单 writer, last_p_fair_ 无锁读写。两腿共用本 condition 的 force 标志。
    bool force_cross = false;
    {
        const auto fit = last_p_fair_.find(condition_id);
        if (fit != last_p_fair_.end()) {
            force_cross = std::abs(p_fair - fit->second) > cfg_.force_cross_fair_delta;
        }
        last_p_fair_[condition_id] = p_fair;  // 更新 (YES-canonical, 选边前的稳定信号)
    }
    // Phase 0 项4: 进球新鲜 (goal_freshness) + OFI 确认 → force_cross 绕死区 (打通进球后 30-120s
    //   延迟 edge 窗口; 微观评审)。force_cross 仅绕死区, 仍受 reservation 限价门约束 → 宽松触发安全。
    if (sports.goal_freshness > cfg_.goal_freshness_force_thr && std::isfinite(b_ofi_dyn) &&
        std::abs(b_ofi_dyn) >= cfg_.ofi_force_thr) {
        force_cross = true;
    }

    // noise_free: sharp 点估计 / 调模型模式 → reservation 跳二项 z×σ (与 edge_ci 同源判据), 让 sharp
    //   信号能跨价进可下单侧 (2026-06-04 老板「跑通赔率 edge 线」修「sizing 说买/reservation 说噪声」双标)。
    const bool reservation_noise_free = fair_is_sharp || cfg_.paper_no_edge_gates;

    // 被选边: 买增至 Kelly 目标 (target_mag; H-3: 无真 fair/无效 sizing → 0 → 只减不开)。
    ExecuteControllerSide(condition_id, token_id, is_yes ? strategy::Outcome::Yes : strategy::Outcome::No,
                          exec_feat, book_depth_l1, p_fair_selected, target_mag, sz_in.fee_rate_coef,
                          force_cross, n_eff_dyn, margin_floor_dyn, reservation_noise_free);

    // M2-a 平旧边: 非选边若有持仓 → target=0 平仓 (旧边 overpriced → bid 高 → reservation_sell 可成交)。
    const SideView& other = is_yes ? mkt.no : mkt.yes;
    if (other.present) {
        const std::string& other_token = is_yes ? mkt.no_token_id : mkt.yes_token_id;
        bool other_held = false;
        {
            const auto tok_exp = position_ledger_.get_per_outcome_exposure();
            const auto tit = tok_exp.find(other_token);
            other_held = (tit != tok_exp.end() && tit->second != 0);
        }
        if (other_held) {
            const auto& other_feat = other.book;
            // 平旧边走卖出 → 深度看 bid 侧 (best_bid_size); 无效则 fallback 1000 pUSD。
            const double other_depth =
                (std::isfinite(other_feat.best_bid_size()) && other_feat.best_bid_size() > 0.0)
                    ? other_feat.best_bid_size()
                    : 1000.0;
            // 非选边 fair = 1 − 被选边 fair (de-vig 互余, YES/NO 对称)。target=0 → 仅平仓。
            ExecuteControllerSide(condition_id, other_token,
                                  is_yes ? strategy::Outcome::No : strategy::Outcome::Yes, other_feat,
                                  other_depth, 1.0 - p_fair_selected, /*target_mag=*/0.0,
                                  FeeCoefFor(condition_id), force_cross, n_eff_dyn, margin_floor_dyn,
                                  reservation_noise_free);
        }
    }
}

// ---------------------------------------------------------------------------
// ExecuteControllerSide — 对单边 token 执行一次目标仓位控制 (Step 4-8 共用; BR-1 形态)。
//   被选边 (target=Kelly) 与非选边平旧边 (target=0) 共用同一执行路径, 消重复 + 保一致。
//   流程: ledger current → ComputeReservation → Decide → (act) intent → RM → sign → match →
//          apply_fill (卖负 delta) → ledger publish → FeedRM。!act / reject / miss → 计数 return。
//   side_book: 本边 book 快照 (4ts/depth/touch 全取自此, R-20 禁 now() 替代上游)。
//   p_fair_side: 本边 fair prob (被选边 = p_fair_selected; 非选边 = 1 − p_fair_selected)。
// ---------------------------------------------------------------------------
void PaperLoop::ExecuteControllerSide(const std::string& condition_id, const std::string& token_id,
                                      strategy::Outcome outcome,
                                      const polymarket::clob_wss::OrderBookFeatures& side_book,
                                      double book_depth_l1, double p_fair_side, double target_mag,
                                      double fee_coef, bool force_cross, int n_eff, double margin_floor,
                                      bool noise_free) noexcept {
    const double exec_ask = side_book.best_ask();
    const double exec_bid = side_book.best_bid();
    const double mark_price = std::isfinite(side_book.microprice) ? side_book.microprice : side_book.mid;

    // M3 CLV 尺子: 每 tick 更新本 token 市场 mid (收盘参考价 = 结算前最后值)。离线评估, 不回喂决策。
    clv_tracker_.UpdateMid(token_id, mark_price);

    // reservation 限价界 (小梁 Q-梁-1; BR-1 纯函数)。
    const control::ReservationPrices reservation = control::ComputeReservation(control::ReservationInput{
        /*fair=*/p_fair_side,
        /*exec_ask=*/exec_ask,
        /*exec_bid=*/exec_bid,
        /*fee_coef=*/fee_coef,
        /*margin_floor=*/margin_floor,  // Phase 0 项2: 动态 (半 vig + amihud); 调用方算好传入
        /*z=*/cfg_.z_90,
        /*n_eff=*/n_eff,  // Phase 0 项1: 动态 (min YES/NO 样本, clamp)
        /*noise_free=*/noise_free,  // sharp/调模型 → 跳二项 z×σ (只留半 vig 地基), 让 sharp 进可下单侧
    });

    // current = 本边 token 当前持仓 (ledger per-outcome, micro→whole pUSD; long ≥0)。
    double current_pusd = 0.0;
    {
        const auto tok_exp = position_ledger_.get_per_outcome_exposure();
        const auto tit = tok_exp.find(token_id);
        if (tit != tok_exp.end()) {
            current_pusd = static_cast<double>(tit->second) / 1'000'000.0;
        }
    }
    // 防抖死区 (小梁 Q-梁-2): threshold = max(floor, 0.10×|target|)。
    const double min_rebalance = std::max(cfg_.min_rebalance_floor_pusd, 0.10 * std::abs(target_mag));

    control::ControlInput cin;
    cin.target_pusd = target_mag;  // 被选边 = Kelly; 非选边平旧边 = 0
    cin.current_pusd = current_pusd;
    cin.reservation_buy_px = reservation.buy_px;
    cin.reservation_sell_px = reservation.sell_px;
    cin.best_ask = exec_ask;  // 本边 ask (买入触价 + 限价不追门)
    cin.best_bid = exec_bid;  // 本边 bid (卖出触价 + 限价不追门)
    cin.min_rebalance_pusd = min_rebalance;
    cin.per_order_cap_pusd = cfg_.per_order_cap_usdc;
    cin.allow_short = false;       // 空头 clamp 0 (sell-to-open 对二元市场 N/A; 见 spec §11.6)
    cin.force_cross = force_cross;  // 小梁 Q-梁-2: fair 大跳绕死区

    const control::ControlAction action = control::Decide(cin);
    if (!action.act) {
        // 控制器决定本 tick 不动 (死区/限价不可成交/已达目标/fail-closed)。不产 intent。
        stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // ---- Step 5: 构造 OrderIntent v0.6 (按控制器动作: side/size/is_close/限价) -----
    const std::int64_t as_of_now = NowNs();
    // 校验 4 ts 链 (R-20: 数据源 = 被交易 token 的 hub 快照, 禁 now() 替代)。
    if (side_book.event_ts_ns <= 0 || side_book.data_source_ts_ns < side_book.event_ts_ns ||
        side_book.ingestion_ts_ns < side_book.data_source_ts_ns || as_of_now < side_book.ingestion_ts_ns) {
        return;  // ts 链违规: 跳过 (不构造 intent, 不送 RM)
    }

    const std::uint64_t intent_id = ++intent_seq_;

    risk::OrderIntent intent;
    intent.event_ts_ns = side_book.event_ts_ns;
    intent.data_source_ts_ns = side_book.data_source_ts_ns;
    intent.ingestion_ts_ns = side_book.ingestion_ts_ns;
    intent.as_of_ts_ns = as_of_now;
    intent.condition_id = condition_id;
    intent.token_id = token_id;
    intent.outcome = outcome;
    intent.side = action.side;          // 买增 / 卖减
    intent.is_close = action.is_close;  // 减仓 → RM cap 放行 + DD 软熔断放行 (老韩 H-1)
    intent.strategy_id = cfg_.strategy_id;
    intent.signal_id = cfg_.strategy_id + "-" + std::to_string(intent_id);
    intent.feature_snapshot_id = "paper-m1-no-snapshot";
    // 定价 (老周 Q-周-1 限价不追前置门已过): marketable-limit 实际成交在触价 (买 ask / 卖 bid)。
    intent.price = (action.side == strategy::Side::Buy) ? exec_ask : exec_bid;
    // size: 控制器动作 (已 clamp per_order_cap + 卖不超持仓), 转 micro pUSD。
    intent.size_pUSD_micro = static_cast<std::int64_t>(action.size_pusd * 1'000'000.0);
    if (intent.size_pUSD_micro <= 0) {
        stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
        return;  // size 舍入到 0 micro → 不臆造 size (防卖侧超卖)
    }
    intent.book_depth_l1_usdc = book_depth_l1 * 1'000'000.0;  // micro 对齐 (RM check_liquidity_)
    intent.book_snapshot_ts_ns = side_book.ingestion_ts_ns;
    intent.tick_size = 0.01;
    intent.fee_rate_coef = fee_coef;  // R-fee-2: per-market 真值 (RM check_signal_ 用)
    intent.timestamp_ms = as_of_now / 1'000'000LL;
    intent.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
    intent.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";

    // ---- Step 6: RiskGateway::evaluate (减仓单不绕 RM 保命门, 老韩 Q-韩-4) ----
    const risk::RiskDecision rd = rm_.evaluate(intent);
    if (rd.is_rejected()) {
        stats_.orders_rejected.fetch_add(1, std::memory_order_relaxed);
        // P0-1: RM 内部 reject_here() 已 push_reject 一次; paper_loop 不重复写。
        return;
    }
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
    sign_req.event_ts_ns = intent.event_ts_ns;
    sign_req.data_source_ts_ns = intent.data_source_ts_ns;
    sign_req.ingestion_ts_ns = intent.ingestion_ts_ns;
    sign_req.as_of_ts_ns = intent.as_of_ts_ns;
    const signer::SignResponse sign_resp = psigner_.Sign(sign_req);
    if (sign_resp.error != signer::SignerError::Ok) {
        return;  // Sign 失败 → fail-closed, 不撮合
    }

    // ---- Step 7b: VirtualMatcher (Mode A++; ABI 零改, 1119 bit-identical) -----
    execution::VirtualOrder vord;
    vord.audit_id = sign_req.audit_id;
    vord.intent_id = sign_req.intent_id;
    vord.market_id = sign_req.condition_id;
    vord.outcome = (outcome == strategy::Outcome::Yes) ? "YES" : "NO";
    vord.size_usdc = static_cast<double>(sign_req.size_pUSD_micro) / 1'000'000.0;
    vord.quote_price = sign_req.price;
    vord.book_depth_l1_usdc = book_depth_l1;
    vord.tick_size = 0.01;
    vord.event_ts_ns = sign_req.event_ts_ns;
    vord.data_source_ts_ns = sign_req.data_source_ts_ns;
    vord.ingestion_ts_ns = sign_req.ingestion_ts_ns;
    vord.as_of_ts_ns = sign_req.as_of_ts_ns;
    vord.wall_now_ns = NowNs();
    const execution::VirtualFill fill = executor_->Execute(vord);
    assert(fill.mode_tag == 0u);  // R-11 (debug build)
    if (fill.reject != execution::MatchReject::Ok || fill.fill_size_usdc <= 0) {
        stats_.fills_missed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    stats_.fills_completed.fetch_add(1, std::memory_order_relaxed);

    // ---- Step 8: PositionLedger::apply_fill (卖负 delta, 老周 Q-周-2) ----------
    //   matcher 出 fill_size_usdc 恒正; 符号在此按 side 定。减仓量已被控制器 clamp ≤ 持仓 → new_size≥0。
    // 卖减仓 realize PnL (2026-06-04 金融团队会议「动态持仓实现盈利, 非结算」): 平仓卖出 →
    //   (卖价 − avg_entry) × 卖出 qty 累加进 cum_realized。原仅结算 realize → take-profit/收敛退出卖出
    //   realized 永 0 (账面看不到动态盈利)。此处补齐, 与 SettleToken (:1459) / unrealized (:1536) 同公式。
    //   取 apply_fill 【前】的 avg_entry (减仓不改 avg, 但前置取更稳)。
    double sell_realized = 0.0;
    if (intent.side == strategy::Side::Sell) {
        const auto pos_before = position_ledger_.get_position(token_id);
        if (pos_before && pos_before->avg_entry_price > 0.0) {
            const double sold_qty = static_cast<double>(fill.fill_size_usdc) / 1'000'000.0;
            sell_realized = (fill.fill_price - pos_before->avg_entry_price) * sold_qty;
            cum_realized_pnl_pusd_ += sell_realized;
        }
    }

    risk::FillEvent ev;
    ev.filled_size_micro =
        (intent.side == strategy::Side::Sell) ? -fill.fill_size_usdc : fill.fill_size_usdc;
    ev.fill_price = fill.fill_price;
    ev.mode_tag = fill.mode_tag;  // R-11 载体平移 (0=paper)
    ev.event_ts_ns = fill.event_ts_ns;
    ev.data_source_ts_ns = fill.data_source_ts_ns;
    ev.ingestion_ts_ns = fill.ingestion_ts_ns;
    ev.as_of_ts_ns = fill.as_of_ts_ns;  // R-20 透传
    position_ledger_.apply_fill(condition_id, token_id, intent.outcome, ev);

    // ---- Step 8b/8c: 账本快照 + 喂 RM 敞口 ----------------------------------
    PublishLedgerSnapshot(condition_id, fill, mark_price, side_book);
    stats_.ledger_publishes.fetch_add(1, std::memory_order_relaxed);
    FeedRiskGateway();

    // M3 CLV 尺子: 记买入(建仓)成交 entry (卖减仓是退出非建仓, 不计 CLV)。离线评估 only。
    if (intent.side == strategy::Side::Buy) {
        clv_tracker_.RecordFill(token_id, fill.fill_price, mark_price,
                                static_cast<double>(fill.fill_size_usdc) / 1'000'000.0, fill.as_of_ts_ns);
    }

    std::fprintf(stderr,
                 "[paper_loop] FILL cond=%.24s... tok=%.16s... side=%s is_close=%d "
                 "fill_sz=%.4f fill_px=%.4f fair=%.4f realized=%.4f\n",
                 condition_id.c_str(), token_id.c_str(),
                 (intent.side == strategy::Side::Buy) ? "BUY" : "SELL", intent.is_close ? 1 : 0,
                 static_cast<double>(fill.fill_size_usdc) / 1'000'000.0, fill.fill_price, p_fair_side,
                 sell_realized);
}

// ---------------------------------------------------------------------------
// SettleCondition / SettleToken (slice-3b 结算, 老板 2026-05-31)
//
// 比赛 Ended → outcome 确定 → 持仓最终值 = winner 1 / loser 0。winner 从终态比分 + orientation:
//   game_row.score_home_total = YES 边比分 (TickOne 已按 yes_is_home 填), score_away_total = 对手。
//   yes_score > opp → YES 胜 (settle YES=1/NO=0); < → NO 胜; = → 平局 push (各 0.5)。
// realize = (settle − avg_entry) × qty 累加进 cum_realized_pnl_pusd_; apply_fill 负 delta 平仓归零。
// R-11: paper 账本; R-20: 4ts 用终态比分 ts (禁 now() 替代 data_source)。loop_thread_ 单 writer。
// ---------------------------------------------------------------------------
void PaperLoop::SettleCondition(const std::string& condition_id, const std::string& yes_token_id,
                                const std::string& no_token_id, double settle_yes, double settle_no,
                                const data::feature_store::FeatureStoreGameRow& game_row) noexcept {
    SettleToken(condition_id, yes_token_id, strategy::Outcome::Yes, settle_yes, game_row);
    SettleToken(condition_id, no_token_id, strategy::Outcome::No, settle_no, game_row);
    // 结算后喂 RM: realized 进 daily_pnl + 敞口归零 (loop_thread_ 串行, R-12 满足)。
    FeedRiskGateway();
}

void PaperLoop::SettleToken(const std::string& condition_id, const std::string& token_id,
                            strategy::Outcome outcome, double settle_price,
                            const data::feature_store::FeatureStoreGameRow& game_row) noexcept {
    // 当前持仓 (signed micro; v1 long-only ≥0)。无仓 → no-op。
    const auto pos_opt = position_ledger_.get_position(token_id);
    if (!pos_opt.has_value() || pos_opt->size_usdc == 0) {
        return;
    }
    const std::int64_t qty_micro = pos_opt->size_usdc;
    const double qty = static_cast<double>(qty_micro) / 1'000'000.0;
    const double avg = pos_opt->avg_entry_price;
    // realize PnL = (结算值 − 加权入场价) × qty (qty signed; v1 long → 正)。
    cum_realized_pnl_pusd_ += (settle_price - avg) * qty;

    // 平仓: apply_fill 负 delta 到 0 (settle_price 作 fill_price; 平仓 avg 归零, R-11 paper)。
    risk::FillEvent ev;
    ev.filled_size_micro = -qty_micro;  // 平掉全部 (→ 0, 不穿零)
    ev.fill_price = settle_price;
    ev.mode_tag = 0;  // R-11 paper
    // R-20: 4ts 用终态比分 ts (禁 now() 替代 data_source); as_of = NowNs (结算时刻 ≥ ingestion)。
    ev.event_ts_ns = game_row.event_ts_ns;
    ev.data_source_ts_ns = game_row.data_source_ts_ns;
    ev.ingestion_ts_ns = game_row.ingestion_ts_ns;
    ev.as_of_ts_ns = NowNs();
    position_ledger_.apply_fill(condition_id, token_id, outcome, ev);

    // M3 CLV 尺子: 结算 → 算该 token 全部建仓成交的 CLV (close mid / 0-1 settle)。离线评估 only。
    clv_tracker_.OnSettle(token_id, settle_price);

    stats_.positions_settled.fetch_add(1, std::memory_order_relaxed);
    std::fprintf(stderr,
                 "[paper_loop] SETTLE cond=%.24s... tok=%.16s... settle=%.2f avg=%.4f qty=%.4f "
                 "realized=%.4f cum_realized=%.4f\n",
                 condition_id.c_str(), token_id.c_str(), settle_price, avg, qty,
                 (settle_price - avg) * qty, cum_realized_pnl_pusd_);
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
// account_equity — 单一账户权益口径 (2026-06-01 凯利评审)。收敛原双轨 (FeedRiskGateway daily_pnl
//   与 RecordEquity / sizing bankroll 此前各算一套)。双口径: best_bid 保守 (凯利/DD) + microprice 展示。
//   stale book (data_source_ts 超 score_staleness_limit_ns) / 无效 bid → 该仓位 0 浮盈 (保守)。
//   R-11: 只读 paper position_ledger_ + hub_ 实例, 不碰真账本。
PaperLoop::AccountEquitySnapshot PaperLoop::account_equity() const noexcept {
    AccountEquitySnapshot s;
    s.bankroll_init = cfg_.bankroll_usdc;
    s.cum_realized = cum_realized_pnl_pusd_;
    s.cum_fee = cum_fee_pusd_;
    s.realized_equity = cfg_.bankroll_usdc + cum_realized_pnl_pusd_ - cum_fee_pusd_;
    double locked_cost = 0.0;  // MVP 近似 cash: 多头占用资金 = Σ(avg_entry × qty)
    for (auto const& pv : position_ledger_.get_all_positions()) {
        // unit-contract-ok: signed micro → whole share (qty)
        const double qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
        if (qty == 0.0) continue;
        ++s.open_positions;
        if (qty > 0.0) locked_cost += pv.avg_entry_price * qty;
        const auto bk = hub_.Read(pv.token_id);
        if (!bk.has_value()) continue;
        if (bk->data_source_ts_ns > s.as_of_ts_ns) s.as_of_ts_ns = bk->data_source_ts_ns;
        // [follow-up 小肖] staleness gate (stale book→0 浮盈) 改 DD 红线路径行为, 需老韩签字+改 A5 测试,
        //   另案 (本 commit 保留 FeedRiskGateway 原口径: 任何 valid bid 计入, 不按 book 龄过滤)。
        const double bid = bk->best_bid();
        if (std::isfinite(bid) && bid > 0.0 && bid < 1.0) {
            s.unrealized_bid += (bid - pv.avg_entry_price) * qty;  // 保守清算价 (砸 bid)
        }
        const double mark = bk->microprice;
        if (std::isfinite(mark) && mark > 0.0 && mark < 1.0) {
            s.unrealized_mark += (mark - pv.avg_entry_price) * qty;  // 展示 (中间价)
            s.position_mtm += mark * qty;                            // 持仓市值 Σ qty×mark
        }
    }
    s.equity_bid = s.realized_equity + s.unrealized_bid;
    s.equity_mark = s.realized_equity + s.unrealized_mark;
    s.cash_available = cfg_.bankroll_usdc - locked_cost + cum_realized_pnl_pusd_ - cum_fee_pusd_;
    return s;
}

void PaperLoop::FeedRiskGateway() noexcept {
    // A1 (老郭钳-6): 账本 micro 化后 get_*_exposure 已是 micro, 与 RM exposure 同单位 → 删原 ×1e6
    //   补偿乘 (P0-1 的"whole→micro"对冲乘已无意义)。直喂, 全量覆盖 (PL 真值, 自愈)。
    for (auto const& [cid, micro] : position_ledger_.get_per_condition_exposure()) {
        rm_.set_condition_exposure(cid, micro);
    }
    for (auto const& [tid, micro] : position_ledger_.get_per_outcome_exposure()) {
        rm_.set_outcome_exposure(tid, micro);
    }

    // A5 (老韩 spec §1-§5): daily_pnl → DD 熔断。语义 = 净未实现 MtM(best_bid 保守) + 已实现 − 累计 fee。
    //   2026-06-01 凯利评审: 收敛进单一 account_equity() (best_bid 保守口径同源, 含 staleness gate)。
    //   daily_pnl = equity_bid − bankroll_init (= unrealized_bid + cum_realized − cum_fee)。
    //   全量覆盖 (set_daily_pnl atomic store 非累加) → 天然无双计。亏损 pnl<0 → RM if(pnl<0) 分支符号天然对齐。
    const AccountEquitySnapshot eq = account_equity();
    const double pnl_pusd = eq.equity_bid - eq.bankroll_init;
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

// ---------------------------------------------------------------------------
// PopulateFeatureColumns — 填 QuoteFeatures 观测特征列 (TickOne blend predict + PublishQuoteSnapshot
//   共用; BR-1: 训练捕获=决策 blend 同一份特征, 杜绝 train-serve skew)。不填决策输出/provenance。
// ---------------------------------------------------------------------------
void PaperLoop::PopulateFeatureColumns(
    sizing::QuoteFeatures& qf, const std::string& condition_id,
    const pricing::FairValueResult& fv_result, double mark_price,
    const polymarket::clob_wss::OrderBookFeatures& feat, double cross_spread, double no_microprice,
    double no_imbalance, bool devig_ok, std::int64_t joint_as_of_ts_ns, const std::string& event_id,
    const std::string& neg_risk_market_id, double time_to_resolution_frac, double g_time_x_lead,
    double g_fld_signal, double g_remaining_sec, std::int32_t g_periods_won_home,
    std::int32_t g_periods_won_away, const SportsFeatures& sports, std::int64_t no_book_ds_ts,
    std::int64_t no_book_ing_ts,
    const polymarket::clob_wss::OrderBookFeatures* no_book_full) noexcept {
    // R-20: 4 ts 透传 (来自 hub 快照)
    qf.event_ts_ns = feat.event_ts_ns;
    qf.data_source_ts_ns = feat.data_source_ts_ns;
    qf.ingestion_ts_ns = feat.ingestion_ts_ns;
    qf.as_of_ts_ns = NowNs();  // 快照发布时刻 (R-20 allowed)
    // NO book 时间戳载体 (双边 book 时间独立; extract_full.fill_latency_features 算 NO book 龄/延迟)。
    qf.no_book_data_source_ts_ns = no_book_ds_ts;
    qf.no_book_ingestion_ts_ns = no_book_ing_ts;

    // v0.8 L2-L5 深度分布 (双边独立; LiveBookPublisher 5 档真值 → compute_depth_metrics 聚合)。
    const auto yd = polymarket::clob_wss::compute_depth_metrics(feat);  // YES book
    qf.b_bid_depth_5lvl = yd.bid_depth_5lvl;
    qf.b_ask_depth_5lvl = yd.ask_depth_5lvl;
    qf.b_l1_concentration = yd.l1_concentration;
    qf.b_depth_imbalance_5lvl = yd.depth_imbalance_5lvl;
    // v0.10 trade-flow (LiveBookPublisher 已填进 feat; 双边独立)。
    qf.b_trade_signed_vol_5m = feat.trade_signed_vol_5m;
    qf.b_trade_buy_ratio_5m = feat.trade_buy_ratio_5m;
    qf.b_trade_intensity_5m = feat.trade_intensity_5m;
    if (no_book_full != nullptr) {  // NO book (单边缺则保持 NaN)
        const auto nd = polymarket::clob_wss::compute_depth_metrics(*no_book_full);
        qf.no_b_bid_depth_5lvl = nd.bid_depth_5lvl;
        qf.no_b_ask_depth_5lvl = nd.ask_depth_5lvl;
        qf.no_b_l1_concentration = nd.l1_concentration;
        qf.no_b_depth_imbalance_5lvl = nd.depth_imbalance_5lvl;
        qf.no_b_trade_signed_vol_5m = no_book_full->trade_signed_vol_5m;
        qf.no_b_trade_buy_ratio_5m = no_book_full->trade_buy_ratio_5m;
        qf.no_b_trade_intensity_5m = no_book_full->trade_intensity_5m;
        // v0.12 双边定价一致性/无风险锁定 (老板: YES+NO<1 套利/锁损 → 喂特征让模型自主判断, 非硬规则)。
        const double y_bid = feat.best_bid(), y_ask = feat.best_ask();
        const double n_bid = no_book_full->best_bid(), n_ask = no_book_full->best_ask();
        if (std::isfinite(y_bid) && std::isfinite(n_bid) && y_bid > 0.0 && n_bid > 0.0)
            qf.x_yes_no_bid_sum = y_bid + n_bid;  // >1 = 卖双边锁利 / 平仓锁损空间
        double lock = 0.0;
        if (std::isfinite(y_ask) && std::isfinite(n_ask) && y_ask > 0.0 && n_ask > 0.0)
            lock = std::max(lock, 1.0 - (y_ask + n_ask));  // 买双边锁利 (YES+NO ask < 1)
        if (std::isfinite(y_bid) && std::isfinite(n_bid) && y_bid > 0.0 && n_bid > 0.0)
            lock = std::max(lock, (y_bid + n_bid) - 1.0);  // 卖双边锁利 (YES+NO bid > 1)
        qf.x_arb_free_edge = lock;  // >0 = 当前存在无风险锁定空间 (套利或锁损)
    }

    // fair_value: 始终输出 (fv_result.p_yes()), 但 predict_ok=false 时消费方不可据此决策.
    qf.fair_value = fv_result.p_yes();
    // YES-canonical mark (feat=YES book; blend 与 capture 同源, BR-1 — 不用选边后 mark, 否则 NO 边偏)。
    const double yes_mark = std::isfinite(feat.microprice) ? feat.microprice : feat.mid;
    qf.market_mid = yes_mark;
    (void)mark_price;  // 参数保留 (调用方对称); 特征用 yes_mark 保 BR-1 一致
    // R-fee-2: per-market 手续费系数 (始终输出, fee 是 market 元数据非决策派生, 不受 has_real_fair gate)。
    //   进 ML 训练数据 (FeatureRecorder) + 前端 /quote。与 RM/sizing 同源 FeeCoefFor(condition)。
    qf.fee_rate_coef = FeeCoefFor(condition_id);

    // v0.7: 类别上下文码 (真实 Polymarket 市场结构 → ML 特征 82-85; per-condition 注入查填)。
    //   始终输出 (market 元数据非决策派生); 查不到 → 默认 sports/-1 (占位, 不影响 gate)。
    const MarketCat mc = MarketCatFor(condition_id);
    qf.cat_asset_class_id = mc.asset_class_id;
    qf.cat_sport_family_id = mc.sport_family_id;
    qf.cat_league_id = mc.league_id;
    qf.cat_market_type_id = mc.market_type_id;
    qf.line = mc.line;  // totals/spreads 线值 (元数据旁注; 解释派生盘口 quote 用)
    qf.mkt_volume_24h_usdc = mc.volume_24h;  // v0.9 市场活跃度 (gamma REST)
    qf.mkt_liquidity_usdc = mc.liquidity;    // v0.9 book 流动性 (gamma REST)

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

    // slice-3 结算特征 (老板 2026-05-31, feature-first): 临近度 (体育时钟派生) + PM WSS 结算状态。
    //   始终输出 (市场生命周期非决策派生, 不受 has_real_fair gate)。time_to_resolution 无时钟 → NaN。
    qf.time_to_resolution_frac = time_to_resolution_frac;
    // resolution_status: 注入的 REST 源优先 (3c 权威, gamma closed/clob winner); 否则 book 载体 (feat)。
    {
        const ResolutionEntry* res = ResolutionFor(condition_id);
        qf.resolution_status = (res != nullptr) ? res->status : feat.resolution_status;
    }

    // 时序特征 (老板 2026-05-31): 从 condition 环形缓冲派生 (PIT 窗口; 样本不足 → NaN)。
    //   fee 同, 始终输出 (市场动态非决策派生, 不受 has_real_fair gate); 训练数据捕获 + 观测。
    {
        const auto hit = ts_history_.find(condition_id);
        if (hit != ts_history_.end()) {
            const std::int64_t w = cfg_.ts_feature_window_ns;
            qf.mp_roc_per_sec = hit->second.RateOfChangePerSec(w);
            qf.realized_vol = hit->second.RealizedVol(w);
            qf.ts_window_samples = static_cast<std::int32_t>(hit->second.WindowSampleCount(w));
            qf.bid_absence_frac = hit->second.BidAbsenceFrac(w);  // slice-2 卖不出 (观测/训练, 非硬门)
            qf.exit_depth_mean = hit->second.ExitDepthMean(w);
            // 批1 微结构 (老郭 b_): Amihud / bid 深度波动 / OFI / vol_ratio (短窗/长窗)。
            qf.b_amihud = hit->second.AmihudApprox(w);
            qf.b_bid_depth_vol = hit->second.BidDepthVol(w);
            qf.b_ofi = hit->second.OFI(w);
            const double vol_short = hit->second.RealizedVol(w / 3);  // 短窗 (制度切换)
            const double vol_long = hit->second.RealizedVol(w);
            qf.b_vol_ratio = (std::isfinite(vol_short) && std::isfinite(vol_long) && vol_long > 0.0)
                                 ? vol_short / vol_long
                                 : std::numeric_limits<double>::quiet_NaN();
            // 批1 补漏: 多尺度动量 (30s 短期 / 5min 中期)。
            qf.b_mp_roc_30s = hit->second.RateOfChangePerSec(30'000'000'000LL);
            qf.b_mp_roc_5m = hit->second.RateOfChangePerSec(300'000'000'000LL);
            // v0.12 套利尺度短窗时序 (老板「更多时序特征」; 补 per_sec/30s/5m 之间的 5-15s 空档)。
            qf.b_mp_roc_5s = hit->second.RateOfChangePerSec(5'000'000'000LL);
            qf.b_ofi_10s = hit->second.OFI(10'000'000'000LL);
            qf.b_realized_vol_10s = hit->second.RealizedVol(10'000'000'000LL);
            const double roc15 = hit->second.RateOfChangePerSec(15'000'000'000LL);
            qf.b_mp_accel = (std::isfinite(qf.b_mp_roc_5s) && std::isfinite(roc15))
                                ? (qf.b_mp_roc_5s - roc15)
                                : std::numeric_limits<double>::quiet_NaN();
        } else {
            qf.mp_roc_per_sec = std::numeric_limits<double>::quiet_NaN();
            qf.realized_vol = std::numeric_limits<double>::quiet_NaN();
            qf.ts_window_samples = 0;
            qf.bid_absence_frac = std::numeric_limits<double>::quiet_NaN();
            qf.exit_depth_mean = std::numeric_limits<double>::quiet_NaN();
            qf.b_amihud = std::numeric_limits<double>::quiet_NaN();
            qf.b_bid_depth_vol = std::numeric_limits<double>::quiet_NaN();
            qf.b_ofi = std::numeric_limits<double>::quiet_NaN();
            qf.b_vol_ratio = std::numeric_limits<double>::quiet_NaN();
            qf.b_mp_roc_30s = std::numeric_limits<double>::quiet_NaN();
            qf.b_mp_roc_5m = std::numeric_limits<double>::quiet_NaN();
        }
    }

    // NO 边时序微结构 (老板「双边都要有」): 与上方 YES 块严格对称, 从 ts_history_no_ 派生。
    //   NO 边无环 (NO book 缺) → 全 NaN (样本不足语义一致)。
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const auto nhit = ts_history_no_.find(condition_id);
        if (nhit != ts_history_no_.end()) {
            const std::int64_t w = cfg_.ts_feature_window_ns;
            qf.no_mp_roc_per_sec = nhit->second.RateOfChangePerSec(w);
            qf.no_realized_vol = nhit->second.RealizedVol(w);
            qf.no_ts_window_samples = static_cast<std::int32_t>(nhit->second.WindowSampleCount(w));
            qf.no_bid_absence_frac = nhit->second.BidAbsenceFrac(w);
            qf.no_exit_depth_mean = nhit->second.ExitDepthMean(w);
            qf.no_b_amihud = nhit->second.AmihudApprox(w);
            qf.no_b_bid_depth_vol = nhit->second.BidDepthVol(w);
            qf.no_b_ofi = nhit->second.OFI(w);
            const double nvs = nhit->second.RealizedVol(w / 3);
            const double nvl = nhit->second.RealizedVol(w);
            qf.no_b_vol_ratio =
                (std::isfinite(nvs) && std::isfinite(nvl) && nvl > 0.0) ? nvs / nvl : nan;
            qf.no_b_mp_roc_30s = nhit->second.RateOfChangePerSec(30'000'000'000LL);
            qf.no_b_mp_roc_5m = nhit->second.RateOfChangePerSec(300'000'000'000LL);
        } else {
            qf.no_mp_roc_per_sec = nan;
            qf.no_realized_vol = nan;
            qf.no_ts_window_samples = 0;
            qf.no_bid_absence_frac = nan;
            qf.no_exit_depth_mean = nan;
            qf.no_b_amihud = nan;
            qf.no_b_bid_depth_vol = nan;
            qf.no_b_ofi = nan;
            qf.no_b_vol_ratio = nan;
            qf.no_b_mp_roc_30s = nan;
            qf.no_b_mp_roc_5m = nan;
        }
    }

    // 批1 cross/fair 派生 (老郭 x_/g_; 点特征, 从 fair/mid/microprice 现算)。
    {
        const double fair = fv_result.p_yes();
        qf.x_log_odds_fair = pricing::logit(fair);
        qf.x_log_odds_edge =
            (yes_mark > 0.0 && yes_mark < 1.0) ? pricing::logit(fair) - pricing::logit(yes_mark) : 0.0;
        qf.x_pin_risk = std::min(fair, 1.0 - fair);
        qf.x_pin_x_expiry = std::isfinite(time_to_resolution_frac) ? qf.x_pin_risk * time_to_resolution_frac
                                                                   : std::numeric_limits<double>::quiet_NaN();
        qf.b_dislocation = (std::isfinite(feat.microprice) && std::isfinite(feat.mid))
                               ? feat.microprice - feat.mid
                               : std::numeric_limits<double>::quiet_NaN();
        qf.g_time_x_lead = g_time_x_lead;    // TickOne 传入 (有真 fair 才有值, 否则 NaN)
        qf.g_fld_signal = g_fld_signal;      // favorite-longshot 偏差信号
        qf.g_remaining_sec = g_remaining_sec;        // 批1 补漏: 剩余秒
        qf.g_periods_won_home = g_periods_won_home;  // 批1 补漏: 各节胜负
        qf.g_periods_won_away = g_periods_won_away;
        // 批1 体育动态 (TickOne 算好的 sports struct 透传)。
        qf.g_game_phase = sports.game_phase;
        qf.g_garbage_time = sports.garbage_time;
        qf.g_clutch = sports.clutch;
        qf.g_goal_freshness = sports.goal_freshness;
        qf.g_net_momentum_5m = sports.net_momentum_5m;
        qf.g_danger_attack_diff = sports.danger_attack_diff;
        qf.g_shot_on_target_diff = sports.shot_on_target_diff;
        qf.g_possession_home = sports.possession_home;
        qf.g_red_card_diff = sports.red_card_diff;
        qf.g_corner_diff = sports.corner_diff;
        qf.g_bm_inplay_fair = sports.bm_inplay_fair;
    }

    // 当前持仓 (老板: 持仓入模型; 库存感知)。目标仓位范式: 模型需知现仓 → 控制器算 order=目标−现仓。
    //   老板「各边买了多少, 可能两边都买」: per-token 双边读 (旧码 break 在首个 token = 只取一边,
    //   丢 NO; 现按 token_map_ 的 YES/NO 各读各量)。pos_net_qty = YES − NO (净方向便利量)。
    if (tick_inputs_.catalog != nullptr) {
        const auto tmit = tick_inputs_.catalog->find(condition_id);
        if (tmit != tick_inputs_.catalog->end()) {
            if (const auto yp = position_ledger_.get_position(tmit->second.tokens.first)) {  // YES token
                qf.pos_yes_qty = static_cast<double>(yp->size_usdc) / 1'000'000.0;
                qf.pos_yes_avg_entry = yp->avg_entry_price;
            }
            if (const auto np = position_ledger_.get_position(tmit->second.tokens.second)) {  // NO token
                qf.pos_no_qty = static_cast<double>(np->size_usdc) / 1'000'000.0;
                qf.pos_no_avg_entry = np->avg_entry_price;
            }
        }
        qf.pos_net_qty = qf.pos_yes_qty - qf.pos_no_qty;  // 净 YES 方向 (NO 持仓 = 反向 YES 敞口)
        qf.pos_avg_entry = qf.pos_yes_avg_entry;          // 向后兼容 (YES 边; 双边见 pos_yes/no_avg_entry)
        const auto cond_exp = position_ledger_.get_per_condition_exposure();
        const auto cit = cond_exp.find(condition_id);
        qf.pos_condition_exposure_usdc =
            (cit != cond_exp.end()) ? static_cast<double>(cit->second) / 1'000'000.0 : 0.0;
    }
}

void PaperLoop::PublishQuoteSnapshot(
    const std::string& condition_id, const pricing::FairValueResult& fv_result,
    const sizing::SizingOutput& sizing_out, double mark_price, double edge_ci_lower,
    const polymarket::clob_wss::OrderBookFeatures& feat, bool has_real_fair, double cross_spread,
    double no_microprice, double no_imbalance, bool devig_ok, std::int64_t joint_as_of_ts_ns,
    const std::string& event_id, const std::string& neg_risk_market_id, double target_signed_notional,
    double reservation_buy_px, double reservation_sell_px, double required_margin,
    double time_to_resolution_frac, double g_time_x_lead, double g_fld_signal, double g_remaining_sec,
    std::int32_t g_periods_won_home, std::int32_t g_periods_won_away, const SportsFeatures& sports,
    const data::feature_store::FeatureStoreGameRow& ml_game_row,
    const data::feature_store::FeatureStoreBookRow& ml_book_row, std::int64_t no_book_ds_ts,
    std::int64_t no_book_ing_ts, const polymarket::clob_wss::OrderBookFeatures* no_book_full) noexcept {
    sizing::QuoteFeatures qf{};
    PopulateFeatureColumns(qf, condition_id, fv_result, mark_price, feat, cross_spread, no_microprice,
                           no_imbalance, devig_ok, joint_as_of_ts_ns, event_id, neg_risk_market_id,
                           time_to_resolution_frac, g_time_x_lead, g_fld_signal, g_remaining_sec,
                           g_periods_won_home, g_periods_won_away, sports, no_book_ds_ts, no_book_ing_ts,
                           no_book_full);

    if (has_real_fair) {
        // 真实 fair 路径 (M2+ Goalserve 接入后): 输出真实 edge/kelly/notional.
        qf.edge_bps = sizing_out.valid ? sizing_out.net_ci_edge * 10'000.0 : 0.0;
        qf.kelly_fraction = sizing_out.valid ? sizing_out.kelly_fractional : 0.0;
        qf.suggested_notional = sizing_out.valid ? sizing_out.suggested_notional : 0.0;
        qf.signal_strength = edge_ci_lower > 0.0 ? std::min(edge_ci_lower * 10.0, 1.0) : 0.0;
        qf.predict_ok = fv_result.valid;
        // 目标仓位范式 (老雷 controller spec v1): target_signed + reservation 限价界 (观测/训练)。
        qf.target_signed_notional = target_signed_notional;
        qf.reservation_buy_px = reservation_buy_px;
        qf.reservation_sell_px = reservation_sell_px;
        qf.required_margin = required_margin;
    } else {
        // P0-3: stub fair 路径 — 无真实 Goalserve 先验, fair 是 microprice 收缩伪值.
        // 清零所有决策字段: 消费方见到这些零值 + predict_ok=false + model_calibrated=false
        // 应拒绝据此下单. 宁可空不可假.
        qf.edge_bps = 0.0;
        qf.kelly_fraction = 0.0;
        qf.suggested_notional = 0.0;
        qf.signal_strength = 0.0;
        qf.predict_ok = false;  // 显式标记不可决策 (P0-3 关键字段)
        // P0-3: stub fair → reservation/target 无意义, 清零 (宁可空不可假)。
        qf.target_signed_notional = 0.0;
        qf.reservation_buy_px = 0.0;
        qf.reservation_sell_px = 0.0;
        qf.required_margin = 0.0;
    }

    // ML 推理 (步④) + Phase 2 项6 完整向量捕获: qf 全特征就位 → extract_full 产完整
    //   kMlFeatureCount(75) 列 (含 0-17 原始 game/book + 18-74 qf 派生)。一次算, 供 record + predict。
    //   advisory, ML-R1/R2: 旁路, 绝不改 fair_value/决策。R-12: paper loop_thread_, 非 WSS。
    const ml::FeatureVector fv = ml::extract_full(ml_game_row, ml_book_row, qf);
    // 项6: 完整向量 Publish 进 fv_hub (短锁 POD copy; 独立 recorder 线程落盘, IO 离决策线程)。
    //   训练 X 一列不缺 (含 score_diff/b_mid 等 FeatureRecorder 落不到的 0-17 原始列)。
    if (fv_hub_ != nullptr && fv.size() == ml::kMlFeatureCount) {
        ml::FeatureVectorRecord rec;
        rec.set_condition(condition_id);
        rec.as_of_ts_ns = qf.as_of_ts_ns;  // 决策时刻 (dedup + PIT; 同 FeatureRecorder 语义)
        rec.set_spec(fv.spec_version);
        rec.set_values(fv.values);
        rec.baseline_fair = qf.fair_value;  // 缺口B: 残差训练 y=label−baseline 的锚 (非 X 列)
        rec.line = qf.line;                 // totals/spreads 线值 (元数据旁注)
        rec.valid = true;
        fv_hub_->Publish(rec);
    }
    ml::ModelPrediction ml_pred_storage;
    const ml::ModelPrediction* ml_pred = nullptr;
    // 热加载: Load() 拿当前模型 (本次引用期内不被换走)。
    const auto ml_model = ml_holder_.Load();
    if (ml_model != nullptr && ml_model->ready() &&
        fv.size() == ml_model->expected_feature_count()) {
        ml_pred_storage = ml_model->predict(fv);
        ml_pred = &ml_pred_storage;
    }

    // 短时套利 advisory 分支 (模块5): seq_arb_model 同源 fv → 预测 → ComputeArbSignal → 填 qf.arb_*。
    //   旁路: 绝不驱动真单 (stub 恒 ok=false; 真模型也止于 advisory 直到开闸)。与结算链物理隔离 (主计划 §5.2)。
    const auto seq_arb_model = seq_arb_holder_.Load();  // 热加载: 拿当前模型 copy (期内不被换走删)
    if (seq_arb_model != nullptr && seq_arb_model->ready() &&
        fv.size() == seq_arb_model->expected_feature_count()) {
        const ml::ArbPrediction ap = seq_arb_model->predict(fv);
        risk::ArbMarketState ms;
        ms.mid = qf.market_mid;
        ms.best_ask = feat.best_ask();
        ms.best_bid = feat.best_bid();
        const auto dm = polymarket::clob_wss::compute_depth_metrics(feat);
        ms.exit_depth_usdc = std::isfinite(dm.bid_depth_5lvl) ? dm.bid_depth_5lvl : 0.0;  // 卖出平仓深度
        const double p = std::isfinite(qf.market_mid) ? qf.market_mid : 0.5;
        ms.fee_roundtrip = 2.0 * qf.fee_rate_coef * p * (1.0 - p);  // 往返手续费 (价格单位)
        ms.slip_est = std::isfinite(qf.cross_spread) ? 0.0 : 0.0;
        ms.slip_est = 0.5 * std::max(0.0, ms.best_ask - ms.best_bid);  // 半 spread 滑点估计
        ms.bankroll_usdc = cfg_.bankroll_usdc;
        ms.max_notional_usdc = cfg_.arb_max_notional_usdc;
        ms.lambda = cfg_.arb_lambda;
        ms.pred_gen_ts_ns = qf.as_of_ts_ns;
        ms.now_ns = NowNs();
        ms.est_rtt_ns = static_cast<std::int64_t>(cfg_.arb_est_rtt_ns);
        ms.max_open_legs = cfg_.arb_max_open_legs;
        const risk::ArbSignal sig = risk::ComputeArbSignal(ap, ms);
        qf.arb_actionable = sig.actionable ? 1 : 0;
        qf.arb_horizon_sec = sig.horizon_sec;
        qf.arb_predicted_dmid = sig.predicted_dmid;
        qf.arb_net_edge = sig.net_edge;
        qf.arb_signal_quality = sig.signal_quality;
        qf.arb_suggested_notional = sig.suggested_notional;
        qf.arb_reject_code = static_cast<std::int32_t>(sig.reject);
    }

    // ML provenance.
    //   若推理成功 → 填 ML 模型 provenance + advisory 输出 (ml_advisory_p_yes)。
    //   否则: 回落 baseline provenance (无 ML 模型时的 M1 行为, 逐位不变)。
    if (ml_pred != nullptr && ml_pred->ok) {
        qf.ml_advisory_p_yes = ml_pred->prob(0);  // ML 模型 YES fair (advisory; 不驱动决策)
        // ml::ModelKind → sizing::ModelKindTag (同值 0/1/2: Stub/Onnx/Treelite)。
        qf.model_kind = (ml_model != nullptr)
                            ? static_cast<sizing::ModelKindTag>(static_cast<std::uint8_t>(ml_model->kind()))
                            : sizing::ModelKindTag::kStub;
        std::strncpy(qf.model_id, ml_pred->model_id.data(),
                     std::min(ml_pred->model_id.size(), sizeof(qf.model_id) - 1));
        qf.model_id[std::min(ml_pred->model_id.size(), sizeof(qf.model_id) - 1)] = '\0';
        std::strncpy(qf.spec_version, ml_pred->spec_version.data(),
                     std::min(ml_pred->spec_version.size(), sizeof(qf.spec_version) - 1));
        qf.spec_version[std::min(ml_pred->spec_version.size(), sizeof(qf.spec_version) - 1)] = '\0';
        qf.model_confidence = ml_pred->confidence;
        qf.fair_ci_lower = ml_pred->ci_low;
        qf.fair_ci_upper = ml_pred->ci_high;
        qf.model_calibrated = ml_pred->calibrated;
        qf.model_as_of_ts_ns = ml_pred->as_of_ts_ns;
    } else {
        // baseline provenance (无 ML 模型; M1 行为不变)
        qf.model_kind = sizing::ModelKindTag::kStub;
        std::strncpy(qf.model_id, "paper-fv-baseline", sizeof(qf.model_id) - 1);
        qf.model_id[sizeof(qf.model_id) - 1] = '\0';
        std::strncpy(qf.spec_version, "m1-paper-v0.1", sizeof(qf.spec_version) - 1);
        qf.spec_version[sizeof(qf.spec_version) - 1] = '\0';
        qf.model_confidence = 0.0;                    // M1 stub: 无置信度
        qf.fair_ci_lower = fv_result.p_yes() - 0.05;  // ±5% 近似 (M1)
        qf.fair_ci_upper = fv_result.p_yes() + 0.05;
        qf.model_calibrated = false;  // M1 stub: 未校准
        qf.model_as_of_ts_ns = feat.ingestion_ts_ns;
    }
    qf.advisory = true;  // ML-R2: paper 期恒 true (ML 推理不进生产决策)

    qf.valid = fv_result.valid;

    quote_hub_.Publish(condition_id, qf);
}

}  // namespace stcpp::paper
