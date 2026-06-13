// src/stcpp/engine/trading_loop.cpp — TradingLoop 实现
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
//         g_rm_debug_snapshot 全局指针调用 push_reject 一次。trading_loop 原来在
//         RM 返回 REJECTED 后又手动调用 rm_snap_->push_reject 一次, 造成每个
//         reject 被写入两次 (128 唯一 reject 各出现 2 次, 纳秒级 rejected_ts 完全
//         相同可排除随机重复). 修法: 删除 trading_loop 侧的冗余 push_reject; RM 侧
//         已唯一地负责写 ring, trading_loop 仅计数 orders_rejected.
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
//         不构造 intent, 不进 RM. RM 不再作 advisory 防线; trading_loop 主动 gate.
//         验证: /api/v1/risk/rejects 中 advisory 市场不应再出现 INVALID_INTENT.

#include "stcpp/engine/trading_loop.hpp"
// (risk/arb_signal.hpp 已砍 2026-06-05: seq_arb 短时套利 advisory 是大模型旁路, 一并删)
#include "stcpp/polymarket/live/live_user_fill_feed.hpp"  // Phase 2: user 频道成交 (DrainUserFills)

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <thread>

#include "stcpp/control/position_controller.hpp"  // 目标仓位控制器 (Decide + ComputeReservation, BR-1)
#include "stcpp/data/feature_store_contract.hpp"
#include "stcpp/data/inplay_odds_parser.hpp"    // ToYesCanonical (inplay 赔率 orientation 翻转)
#include "stcpp/data/live_stats_parser.hpp"     // FillLiveStats (live_stats 采集 hop join)
#include "stcpp/data/score_snapshot_store.hpp"  // A1: ScoreSnapshotStore::Get(inplay_match_id)
#include "stcpp/execution/execution_mode.hpp"   // A2 红线1: execution::ExecutionContext::Mode() 运行期 mode 断言
#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/stats/gate_evaluator.hpp"  // 2026-06-12 治理: G1-G7 记分牌接 daily-close
#include "stcpp/pricing/derivative_fair_value.hpp"
#include "stcpp/pricing/tennis_fair_value.hpp"  // 网球 totals/spreads (games/sets 制; 老板「全盘口接入」)
#include "stcpp/pricing/esports_fair_value.hpp"  // 电竞 maps totals/spreads (best-of-N 枚举)
#include "stcpp/pricing/fair_resolve.hpp"  // R-2: ResolveFair 纯函数 (fair 优先级集中)
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/strategy/edge_ci.hpp"  // 单一 ComputeEdgeCiLower (回测-实盘共用)
#include "stcpp/strategy/signal_iface.hpp"

namespace stcpp::engine {

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

// token_id 格式校验 (uint256 string: 非空, ≤78 位, 纯数字). 镜像 RM risk_gateway 的 is_valid_token_id
//   (SSOT: laoli-w8-polymarket-data-structure-ssot §2.3 §5 T-05). 2026-06-10 老板「修啊」: catalog 预热期
//   token_id 可能畸形 → trading_loop 提前 fail-closed 不构造 intent, 不让 RM 兜底拒 INVALID_TOKEN_ID_FORMAT.
//   2026-06-12 off-by-one 修复: uint256 最大 2^256-1 是【78 位】十进制 (旧 77 误拒合法高 uint256 token → 漏盘)。
[[nodiscard]] bool IsValidTokenId(const std::string& tid) noexcept {
    if (tid.empty() || tid.size() > 78u) {
        return false;
    }
    for (char c : tid) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// ctor
// ---------------------------------------------------------------------------

TradingLoop::TradingLoop(const polymarket::clob_wss::OrderBookSnapshotHub& hub, risk::RiskGateway& rm,
                     risk::PositionLedger& position_ledger, risk::LedgerSnapshotHub& ledger_hub,
                     sizing::QuoteSnapshotHub& quote_hub, risk::RmDebugSnapshot* rm_snap,
                     const pricing::IFairValueModel& fv_model,
                     std::unordered_map<std::string, std::pair<std::string, std::string>> token_map,
                     TradingLoopConfig cfg) noexcept
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

TradingLoop::~TradingLoop() {
    Stop();
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

void TradingLoop::Start() {
    if (running_.load(std::memory_order_acquire)) {
        return;  // 已启动, 幂等
    }

    // R-11/R-7 (老韩 A2 红线1): 运行期 mode 交叉断言. advisory gate 解封 (产生 paper intent)
    //   仅许 build-time paper mode. 防 advisory_markets_no_intent=false 误带进 live/backtest binary.
    //   build-time 锁 + 此运行期交叉校验双保险; 不一致 → abort (R-7 立场: 绝不放行).
    if (!cfg_.advisory_markets_no_intent &&
        stcpp::execution::ExecutionContext::Mode() != stcpp::execution::ExecutionMode::Paper) {
        // 2026-06-12 实盘准备: live build 发真单是【有意行为】, 须显式 STCPP_LIVE_INTENT_OK=1
        //   (start_live.sh 设置) 才放行 — 防 live binary 被误当 paper 跑; 无此 env 仍 abort (R-7)。
        const char* live_ok = std::getenv("STCPP_LIVE_INTENT_OK");
        if (stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live && live_ok != nullptr &&
            live_ok[0] == '1') {
            std::fprintf(stderr, "[trading_loop] LIVE 模式意图确认 (STCPP_LIVE_INTENT_OK=1): 决策环将发真实 intent "
                                 "(成交仍受 LiveOrderGate arm 闸控制)\n");
        } else {
            std::fprintf(stderr,
                         "[trading_loop] FATAL (R-11/R-7): advisory_markets_no_intent=false (解封成交) 仅许 paper "
                         "mode 或 live+STCPP_LIVE_INTENT_OK=1; execution::ExecutionContext::Mode()=%s. abort.\n",
                         std::string(stcpp::execution::ToString(stcpp::execution::ExecutionContext::Mode())).c_str());
            std::abort();
        }
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
                 "[trading_loop] 启动 paper 交易循环 (tick=%lldms, tokens=%zu, "
                 "bankroll=%.0f pUSD)\n",
                 static_cast<long long>(cfg_.tick_interval_ms),
                 (LoadPaperCatalog() ? LoadPaperCatalog()->size() : 0), cfg_.bankroll_usdc);
}

void TradingLoop::Stop() noexcept {
    if (!running_.load(std::memory_order_acquire) && !loop_thread_.joinable()) {
        return;  // 未启动或已停止
    }

    stop_requested_.store(true, std::memory_order_release);
    if (loop_thread_.joinable()) {
        loop_thread_.request_stop();
        loop_thread_.join();
    }
    std::fprintf(stderr,
                 "[trading_loop] 已停止. ticks=%llu approved=%llu fills=%llu "
                 "ledger_publishes=%llu\n",
                 static_cast<unsigned long long>(stats_.ticks_total.load()),
                 static_cast<unsigned long long>(stats_.orders_approved.load()),
                 static_cast<unsigned long long>(stats_.fills_completed.load()),
                 static_cast<unsigned long long>(stats_.ledger_publishes.load()));
}

// ---------------------------------------------------------------------------
// RunLoop — 主循环 (loop_thread_ 内执行)
// ---------------------------------------------------------------------------

void TradingLoop::RunLoop(std::stop_token st) {
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
                                 "[trading_loop] RM feed-liveness: 红线 '%.*s' NEVER FED — "
                                 "paper daemon 未接通该红线喂数管道 (纸面化, 默认值静默放行风险)\n",
                                 static_cast<int>(r.key.size()), r.key.data());
                    ++never_fed;
                }
            }
            std::fprintf(stderr, "[trading_loop] RM feed-liveness 自检: %d/%zu 红线从未被喂\n", never_fed,
                         rows.size());
        }

        // 事件驱动等待 (2026-06-04 老板「别轮询, 直接触发更快」): 不再定时 sleep, 改等 cv ——
        //   数据源 (WSS book / 149hz poll / 赔率) 到达即 RequestTick() notify → 立即醒来跑下一轮决策。
        //   tick_interval_ms 退化为【fallback 心跳】上限 (无数据时也定期跑一轮: staleness/结算/feed-liveness)。
        //   R-12: 仅 loop_thread_ 阻塞在 cv (非 WSS io_thread); 数据线程 notify 不阻塞。
        {
            const auto fallback = std::chrono::milliseconds(cfg_.tick_interval_ms);
            std::unique_lock<std::mutex> lk(tick_mu_);
            tick_cv_.wait_for(lk, fallback, [this, &st] {
                return tick_pending_ || st.stop_requested() ||
                       stop_requested_.load(std::memory_order_acquire);
            });
            tick_pending_ = false;  // 消费触发标志 (本轮 TickAll 已覆盖到达的更新)
        }
    }
}


// 成交流水持久化 (2026-06-13 C5): append-only JSONL, loop_thread_ 单写, 每笔 fopen/fclose (非热路径)。
void TradingLoop::JournalFill(const FillRow& fr) {
    // 异步缓冲落盘 (2026-06-13 老板「独立线程异步写 + 缓冲不高频读写磁盘」): 本函数只【拼字符串 + 入队】(纳秒),
    //   真正 fopen/fwrite/fflush 在 journal_writer_ 的独立线程批量缓冲写 → 决策环 (loop_thread_) 零磁盘阻塞。
    const bool live = stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live;
    const char* path = live ? "data/ml_capture/live_fills_journal.jsonl"
                            : "data/ml_capture/fills_journal.jsonl";
    // 分析维度直出 (老板 2026-06-13「落盘直接划分析: 类型/赛事/盘口」): 后续按
    //   sport×league×mkt_type 切片研究开箱即用, 不再依赖事后 join。
    const MarketCat cat = MarketCatFor(fr.condition_id);
    const char* fam = "unknown";
    switch (cat.sport_family_id) {
        case 0: fam = "soccer"; break;
        case 1: fam = "basketball"; break;
        case 2: fam = "tennis"; break;
        case 3: fam = "baseball"; break;
        case 4: fam = "hockey"; break;
        case 5: fam = "amfootball"; break;
        case 6: fam = "esports"; break;
        case 7: fam = "mma"; break;
        case 8: fam = "cricket"; break;
        default: break;
    }
    const char* mt = "unknown";
    switch (cat.market_type_id) {
        case 0: mt = "moneyline"; break;
        case 1: mt = "spread"; break;
        case 2: mt = "totals"; break;
        case 3: mt = "outright"; break;
        case 4: mt = "prop"; break;
        case 5: mt = "series"; break;
        default: break;
    }
    std::string out;
    out.reserve(768);
    {
        char buf[768];
        const int n = std::snprintf(
            buf, sizeof(buf),
            "{\"ts\":%lld,\"cond\":\"%s\",\"yes\":%d,\"buy\":%d,\"close\":%d,\"px\":%.6f,"
            "\"qty\":%.4f,\"realized\":%.4f,\"fair\":%.4f,\"mark\":%.4f,\"fee\":%.5f,"
            "\"exit\":\"%s\",\"engine\":\"%s\","
            "\"sport\":\"%s\",\"league\":%d,\"mkt\":\"%s\",\"line\":%.2f",
            static_cast<long long>(fr.as_of_ts_ns), fr.condition_id.c_str(), fr.is_yes ? 1 : 0,
            fr.is_buy ? 1 : 0, fr.is_close ? 1 : 0, fr.price, fr.size_usdc, fr.realized, fr.fair, fr.mark,
            fr.fee, fr.exit_reason.c_str(), fr.engine.c_str(), fam, static_cast<int>(cat.league_id), mt,
            std::isfinite(cat.line) ? cat.line : -1.0);
        if (n > 0)
            out.append(buf, static_cast<std::size_t>(n < static_cast<int>(sizeof(buf)) ? n
                                                                                       : static_cast<int>(sizeof(buf)) - 1));
    }
    // 研究级上下文 (2026-06-13 老板「订单簿指标/流动性都落, 以后优化都参照」): 有值才写 (NaN 省略)。
    auto emit_d = [&out](const char* k, double v) {
        if (std::isfinite(v)) {
            char b[80];
            const int m = std::snprintf(b, sizeof(b), ",\"%s\":%.6g", k, v);
            if (m > 0)
                out.append(b, static_cast<std::size_t>(m < static_cast<int>(sizeof(b)) ? m
                                                                                       : static_cast<int>(sizeof(b)) - 1));
        }
    };
    emit_d("bk_spread", fr.bk_spread);
    emit_d("bk_bid_sz", fr.bk_bid_sz);
    emit_d("bk_ask_sz", fr.bk_ask_sz);
    emit_d("bk_imb", fr.bk_imb);
    emit_d("bk_micro_mid", fr.bk_micro_mid);
    emit_d("bk_age_ms", fr.bk_age_ms);
    emit_d("ofi", fr.q_ofi);
    emit_d("rvol", fr.q_rvol);
    emit_d("mom5", fr.q_mom5);
    emit_d("sh_fair", fr.sh_fair);
    emit_d("sh_vel", fr.sh_vel);
    emit_d("deploy", fr.deploy_pct);
    emit_d("vol24h", cat.volume_24h);
    emit_d("liq", cat.liquidity);
    emit_d("hold_sec", fr.hold_sec);
    emit_d("mae", fr.mae);
    emit_d("mfe", fr.mfe);
    // 复盘观测补全 (2026-06-13 老板「都改」): 入场全息 + 结算终局 (NaN 自动省略, 加性)
    emit_d("devig", fr.p_devig);
    emit_d("edge_ci", fr.edge_ci);
    emit_d("kelly_sugg", fr.kelly_sugg);
    emit_d("m_life", fr.m_life);
    emit_d("m_clv", fr.m_clv);
    emit_d("m_corr", fr.m_corr);
    emit_d("m_dd", fr.m_dd);
    emit_d("d5_bid", fr.d5_bid);
    emit_d("d5_ask", fr.d5_ask);
    emit_d("t_vol5m", fr.t_vol5m);
    emit_d("t_ratio5m", fr.t_ratio5m);
    emit_d("odds_age", fr.odds_age_ms);
    emit_d("g_remain", fr.g_remain);
    emit_d("g_sdiff", fr.g_sdiff);
    emit_d("equity", fr.equity);
    emit_d("close_mid", fr.close_mid);
    emit_d("final_bid", fr.final_bid);
    emit_d("final_ask", fr.final_ask);
    out.append("}\n");
    journal_writer_.AppendLine(path, std::move(out));  // 异步缓冲落盘 (纳秒入队, 不卡 loop_thread_)
}

// ---------------------------------------------------------------------------
// TickAll — 遍历所有 condition, 双边读 (YES book + NO book) 组 BinaryMarketSnapshot 进决策
//   (老板原则 C3: 决策带整盘口; 老周架构: 决策线程栈上组装, 零锁; R-12 不触碰)。
// ---------------------------------------------------------------------------

// DrainUserFills — 排空 CLOB user 频道成交队列 (loop_thread tick 入口, 单 writer)。
//   【当前 shadow】: 仅 log 每笔已 CONFIRMED 的真实成交 + 与 sync 路径账本对账 (是否已记此 token / size),
//   不入账 (sync 路径仍是唯一真相源 → 零真钱风险)。目的: 用真实流量验证 user 频道连得上(auth)、真 trade
//   消息解析对、按 id 去重对。验证通过后 flip (单独提交): 改为从 WSS 登持仓 (唯一真相源) + 移除同步 apply_fill。
void TradingLoop::DrainUserFills() {
    if (user_fill_feed_ == nullptr)
        return;
    polymarket::UserFill uf;
    while (user_fill_feed_->Pop(uf)) {
        const auto pv = position_ledger_.get_position(uf.token_id);
        const double ledger_sz = pv ? static_cast<double>(pv->size_usdc) / 1'000'000.0 : 0.0;
        // 对账: WSS 收到 CONFIRMED 成交 vs 同步路径已记账本仓。两者应吻合 (sync 已记则 ledger_sz≠0)。
        std::fprintf(stderr,
                     "[user-fill/shadow] cond=%.20s... tok=%.16s... %s %s sz=%.4f px=%.4f fee=%.4f "
                     "trade=%.8s | ledger_now=%.4f\n",
                     uf.condition_id.c_str(), uf.token_id.c_str(), uf.is_buy ? "BUY" : "SELL",
                     uf.is_yes ? "YES" : "NO", uf.size, uf.price, uf.fee, uf.trade_id.c_str(), ledger_sz);
    }
}

void TradingLoop::TickAll() {
    // Phase 2: 先排空 user 频道成交 (shadow: log+对账; flip 后登持仓须在 account_equity 之前)。
    DrainUserFills();
    // A4 (老板「他们相对都是最近刷新的就行」): tick 入口冻结一次比分快照 + 映射, 整轮全子盘口共享同版本。
    //   消除 read-skew: 否则同 event 的 moneyline/spread 各自 Get(), 采集线程中途 swap → 看不同比分版本。
    //   GetSnapshot()/LoadEventMap() 都是只读 RCU 单次 load (不碰 R-12); shared_ptr 持有保活整 tick。
    // (回测 replay 注入缝 2026-06-12 老板裁决删: 模型假设源自同批历史数据, 回测=in-sample 假象;
    //  验证 = 脚本验数据 + paper walk-forward。tick_inputs_ 聚合冻结本身保留 — 消 read-skew 是生产需要。)
    tick_inputs_.event_map = LoadEventMap();
    tick_inputs_.score = (score_store_ != nullptr) ? score_store_->GetSnapshot() : nullptr;
    tick_inputs_.catalog = LoadPaperCatalog();  // RCU 快照: 周期重发现中途 swap, 整 tick 持有同版本
    tick_inputs_.resolution = LoadResolution();   // [R-1] 刷新线程 30s swap, 整 tick 冻结同版本 (消 UB)
    tick_inputs_.live_stats = LoadLiveStats();    // [R-1] 同上
    tick_inputs_.odds = LoadOdds();               // bm_slots: 跨庄家赔率 (inplay_match_id 键), [R-1] 同上
    if (tick_inputs_.catalog == nullptr) {
        return;  // 未注入 (理论不达; ctor 必置)
    }

    // [2026-06-01 凯利评审] tick 入口冻结账户权益快照 → 本轮所有子盘口 sizing 用同版本 bankroll。
    //   修「风控纸面化」(bankroll 此前硬用 cfg_ 静态初值, 回撤不缩盈利不涨)。整 tick 冻结 → 同 tick 内
    //   多笔成交不驱动 bankroll 抖动 (老韩/小梁); 下 tick 自然吸收本 tick 已实现/未实现变化。
    tick_equity_ = account_equity();

    // (FLB-hold 引擎 2026-06-13 老板「直接删干净」整删: n=34 实证 73.5% 胜率 < 0.80 入场盈亏线,
    //  realized −59.88, 赔付不对称把正胜率变亏钱; 同期 sharp 16/17 +107.71。git 史可考。
    //  per-engine 账本基建保留 — sharp 在用 + 存量 flb 仓结算归因仍走通用 engine_by_token_ 路径。)

    // ---- CLV 失效熔断状态刷新 (2026-06-12 治理: CLVTracker 反哺入场, 30s 节流) ----
    //   正率<70% (样本≥30) = 模型失效 (赔率源断/匹配错/延迟恶化) → 熔断新开仓; 恢复自动解除。
    {
        constexpr std::uint64_t kClvBreakerMinN = 30;
        constexpr double kClvBreakerRate = 0.70;
        const std::int64_t cb_now = NowNs();
        if (cb_now - last_clv_breaker_check_ns_ >= 30'000'000'000LL) {
            last_clv_breaker_check_ns_ = cb_now;
            const auto cr = clv_tracker_.report();
            const bool trip = cr.n_fills >= kClvBreakerMinN && cr.clv_close_positive_rate < kClvBreakerRate;
            if (trip != clv_breaker_) {
                clv_breaker_ = trip;
                std::fprintf(stderr, "[clv-breaker] %s — CLV正率 %.1f%% (n=%llu, 阈 70%%/30): %s\n",
                             trip ? "熔断ON" : "恢复OFF", cr.clv_close_positive_rate * 100.0,
                             static_cast<unsigned long long>(cr.n_fills),
                             trip ? "停新开仓 (减仓/平仓/结算照常)" : "恢复新开仓");
            }
        }
    }

    // P4 部署率 WARN (2026-06-11 晚会): >85% 软告警 (不硬停, 持有到结算下高部署=相关性暴露)
    if (tick_equity_.deploy_pct > 0.85 && NowNs() - last_deploy_warn_ns_ > 300'000'000'000LL) {
        last_deploy_warn_ns_ = NowNs();
        std::fprintf(stderr, "[deploy-warn] 部署率 %.0f%% > 85%% (open=%zu)\n", tick_equity_.deploy_pct * 100.0,
                     static_cast<std::size_t>(tick_equity_.open_positions));
    }

    // ---- P5 日级 PnL 滚账 (2026-06-11 晚会): UTC 日界打一行, grep 即得逐日成绩 ----
    {
        const std::int64_t day_now = NowNs() / 86'400'000'000'000LL;
        if (last_daily_close_day_ == 0) {
            last_daily_close_day_ = day_now;
        } else if (day_now != last_daily_close_day_) {
            last_daily_close_day_ = day_now;
            std::fprintf(stderr, "[daily-close] realized=%+.2f fee=%.2f equity_mark=%.2f open_pos=%zu clv_n=%llu\n",
                         cum_realized_pnl_pusd_, cum_fee_pusd_, tick_equity_.equity_mark,
                         static_cast<std::size_t>(tick_equity_.open_positions),
                         static_cast<unsigned long long>(clv_tracker_.report().n_fills));
            // GateEvaluator 记分牌 (2026-06-12 治理「能利用的利用起来」: stats G1-G7 写完零调用 → 接上)。
            //   paper→live 晋升的统计证据, 每日界打一版。G7 (vs random baseline) 无基线数据 → n/a;
            //   G3 RM 失效=0 (无已知失效); G4 uptime 为进程内口径 (重启不可见, 看 systemd/监控)。
            if (gate_trade_pnl_.size() >= 2) {
                stats::GateMetrics gm;
                gm.window_start_ts_ns = first_trade_ts_ns_;
                gm.window_end_ts_ns = last_trade_ts_ns_;
                gm.ingestion_completed_ts_ns = NowNs();
                gm.as_of_ts_ns = gm.ingestion_completed_ts_ns;
                gm.per_trade_pnl_usdc = gate_trade_pnl_;
                gm.equity_curve_usdc.reserve(gate_trade_pnl_.size());
                double eq = cfg_.bankroll_usdc;
                for (const double p : gate_trade_pnl_) {
                    eq += p;
                    gm.equity_curve_usdc.push_back(eq);
                }
                gm.per_trade_return.reserve(gate_trade_pnl_.size());
                for (const double p : gate_trade_pnl_) {
                    gm.per_trade_return.push_back(p / cfg_.bankroll_usdc);  // 对账本的逐笔收益 (Sharpe 基)
                }
                gm.rm_failure_count = 0;
                gm.uptime_seconds = static_cast<double>(gm.window_end_ts_ns - gm.window_start_ts_ns) / 1e9;
                gm.total_window_seconds = gm.uptime_seconds;
                const auto outcome = stats::GateEvaluator::EvaluateAll(gm);
                for (const auto& g : outcome.per_gate) {
                    std::fprintf(stderr, "[gate-eval] G%d %s actual=%.4f thr=%.4f n=%zu%s%s\n",
                                 static_cast<int>(g.id), g.pass ? "PASS" : "fail", g.actual, g.threshold,
                                 g.sample_size, g.note.empty() ? "" : " note=", g.note.c_str());
                }
            }
        }
    }

    // ---- 账本快照 60s 节流 (2026-06-11 持久化: 重启不再清零持仓/realized/CLV) ----
    if (!cfg_.ledger_snapshot_path.empty()) {
        const std::int64_t snap_now = NowNs();
        if (snap_now - last_ledger_snapshot_ns_ >= 60'000'000'000LL) {
            last_ledger_snapshot_ns_ = snap_now;
            SaveLedgerSnapshot();
        }
    }
    // ---- 持仓路径采样 60s (2026-06-13 老板「止损分析要路径」): 每开仓一行 (bid/ask/mid/sharp/qty)
    //   → position_path.jsonl (BackgroundWriter 异步缓冲)。未来任何出场/止损规则的离线回测金料。 ----
    {
        const std::int64_t pp_now = NowNs();
        if (pp_now - last_pos_path_ns_ >= 60'000'000'000LL) {
            last_pos_path_ns_ = pp_now;
            SamplePositionPaths();
        }
    }

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
            // R6.2c 相关性集中度 cap: 首次见即向 RM 注册 condition→event (eager, 覆盖首单;
            //   register-once 防每 tick 锁churn)。event_gross 由 FeedRiskGateway 喂敞口时维护。
            if (!mkt.event_id.empty() && rm_event_registered_.insert(cond_id).second) {
                rm_.set_condition_event(cond_id, mkt.event_id);
            }
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

    // 孤儿结算兜底 (2026-06-10 老板「安全重做」, CLV=0 真根因): 比赛结束掉出 catalog 的持仓不再被上面 TickOne 结算
    //   → 孤儿仓永不结算 → CLV 永远 0 (edge 金标准失效)。补 loop 线程扫描: 持仓 cid 不在 catalog(孤儿) + 有【权威
    //   resolution】(SettlementPoller 现已轮询有持仓的 condition) → 直接 SettleToken 结算。全在 loop_thread (R-12 单
    //   writer); 只读 resolution/catalog(RCU) + position_ledger(同线程)。【不碰 catalog 重建 token_map_ — 避开上次孤儿
    //   改动致 GP fault 的崩溃区】。SettleToken 平仓→size 0→下 tick 自动跳过 (无需去重; OnSettle 二次为 no-op)。
    if (tick_inputs_.catalog != nullptr) {
        bool any_orphan_settled = false;
        int orphan_pending = 0, orphan_resolved = 0;  // 诊断: 孤儿持仓(掉出 catalog) 计数 (老板「查结算是否漏」)
        for (const auto& pv : position_ledger_.get_all_positions()) {  // 返回 copy, 迭代中 SettleToken 改账本安全
            if (pv.size_usdc == 0) continue;
            if (tick_inputs_.catalog->find(pv.condition_id) != tick_inputs_.catalog->end()) continue;  // 仍在 catalog
            ++orphan_pending;  // 持仓但掉出 catalog = 孤儿 (= 消失在持仓界面的盘)
            const ResolutionEntry* res = ResolutionFor(pv.condition_id);
            if (res == nullptr || res->status != 2 || res->winner < 0) continue;  // 无权威 resolution → 等
            ++orphan_resolved;
            const double settle_yes = (res->winner == 1) ? 1.0 : 0.0;  // winner 1=YES / 0=NO
            const double settle_px =
                (pv.outcome == strategy::Outcome::Yes) ? settle_yes : (1.0 - settle_yes);
            const std::int64_t rts = (res->fetched_at_ns > 0) ? res->fetched_at_ns : NowNs();  // R-20 4ts
            data::feature_store::FeatureStoreGameRow gr{};
            gr.event_ts_ns = rts;
            gr.data_source_ts_ns = rts;
            gr.ingestion_ts_ns = rts;
            std::fprintf(stderr, "[orphan-settle] cond=%.24s outcome=%s settle=%.1f → 结算孤儿仓 (掉出 catalog 的盘)\n",
                         pv.condition_id.c_str(), (pv.outcome == strategy::Outcome::Yes) ? "YES" : "NO", settle_px);
            SettleToken(pv.condition_id, pv.token_id, pv.outcome, settle_px, gr);  // 平仓 + realize + CLV OnSettle
            any_orphan_settled = true;
        }
        if (any_orphan_settled) FeedRiskGateway();  // 同 SettleCondition: realize 进 daily_pnl + 敞口归零
        // 诊断 (老板「查消失的盘结算有没有进账户」): 有孤儿持仓未结算时, 节流(每~30s)打一行 —— 暴露
        //   「持仓掉出 catalog 但还没 resolution → 卡在 limbo」的盘 (= 用户怀疑的泄漏); 若 pending 一直>0 且不降 = 漏。
        if (orphan_pending > orphan_resolved) {
            const std::int64_t now_d = NowNs();
            if (now_d - last_orphan_diag_ns_ > 30'000'000'000LL) {  // 30s 节流
                last_orphan_diag_ns_ = now_d;
                std::fprintf(stderr,
                             "[orphan-diag] 孤儿持仓(掉出catalog)=%d 其中已resolved待结算=%d → pending未resolved=%d "
                             "(SettlementPoller 应轮询补 resolution; 长期>0 不降 = 结算漏)\n",
                             orphan_pending, orphan_resolved, orphan_pending - orphan_resolved);
            }
        }
    }

    // Phase 0 项5 (联合评审, 小梁): 每 tick 周期采一次组合权益 → Sharpe/maxDD/VaR + 净值曲线 (/api/v1/pnl/timeseries)。
    //   2026-06-10 (老板「净值曲线与 pnl 不一致」): 改用 equity_mark (microprice) —— 与账本展示 net_pnl(=equity_mark
    //   −bankroll) 同口径, 让【净值曲线 == net_pnl 数字】严格一致。原 equity_bid(best_bid 保守) 是给风险指标的口径,
    //   但曲线借同一序列 → 曲线(bid)系统性低于展示 net_pnl(mark) 差一个 bid-mark 价差 = 老板看到的不一致。统一展示
    //   口径为 mark (标准 MTM; 凯利 bankroll 另用 realized_equity 不受影响)。R-20: ts 用 NowNs (策略侧时序, 不涉数据源契约)。
    portfolio_metrics_.RecordEquity(NowNs(), tick_equity_.equity_mark);

    // DD→target 乘子更新 (持仓管理 Stage2, 老板「回撤大只停加仓 + hysteresis, 不砍现仓」):
    //   降档立即生效 (回撤加深快去险); 升档需当前回撤比降档阈值再回落 hysteresis_band (黏滞防抖)。
    {
        const double dd = portfolio_metrics_.current_drawdown();
        const control::DrawdownConfig dc{cfg_.dd_mult_enabled, cfg_.dd_t1,  cfg_.dd_t2,
                                         cfg_.dd_halt,          cfg_.dd_m_t1, cfg_.dd_m_t2,
                                         cfg_.dd_hysteresis_band};
        const double drop_m = control::DrawdownTierMultiplier(dd, dc);
        const double restore_m = control::DrawdownTierMultiplier(dd + cfg_.dd_hysteresis_band, dc);
        if (drop_m < dd_mult_) {
            dd_mult_ = drop_m;  // 回撤加深 → 立即降档
        } else if (restore_m > dd_mult_) {
            dd_mult_ = restore_m;  // 回撤回落超过 band → 升档恢复
        }
    }

    // [2026-06-01 凯利评审] 发布账户权益副本 (debug_api /api/v1/account 经 daemon 回调读)。
    //   loop_thread_ 算 sharpe/maxDD (portfolio_metrics_ 单 writer 此处读安全) → mutex 发布给 HTTP 线程。
    //   tick 末重算一次 equity (含本轮成交后的最新持仓), 比 tick 入口冻结的 tick_equity_ 新。
    {
        AccountEquitySnapshot pub = account_equity();
        const double ppy = cfg_.tick_interval_ms > 0
                               ? 365.25 * 24.0 * 3600.0 * 1000.0 / static_cast<double>(cfg_.tick_interval_ms)
                               : 0.0;
        const auto rep = portfolio_metrics_.report(ppy);
        // Sharpe 改用【逐笔已实现收益】(金融小梁 P1-C): 原 rep.sharpe 建在 per-tick 权益 MtM ×√7944 ≈ −239 垃圾
        //   (per-tick MtM 高度自相关, 非真实交易收益)。改 mean/std of trade_returns_ = per-trade Sharpe (非垃圾)。
        //   annualizer=1 (年化口径待金融 ADR — paper 交易频率不稳, 年化无意义)。样本 <2 报 0。
        double sh = 0.0;
        if (trade_returns_.size() >= 2) {
            double m = 0.0;
            for (const double r : trade_returns_) m += r;
            m /= static_cast<double>(trade_returns_.size());
            double v = 0.0;
            for (const double r : trade_returns_) v += (r - m) * (r - m);
            v /= static_cast<double>(trade_returns_.size() - 1);
            const double sd = std::sqrt(v);
            if (sd > 1.0e-9) sh = m / sd;
        }
        pub.sharpe = sh;
        pub.max_drawdown = rep.max_drawdown;
        std::lock_guard<std::mutex> lk(acct_pub_mu_);
        published_equity_ = pub;
    }
}


// ---------------------------------------------------------------------------
// [D-阶段1] ResolveGameContext — 比分/赔率源上下文解析 (2026-06-12 业务流分段)
//   A1 condition→event 映射 + tick 冻结快照 → game_row (比分/时钟/sharp fair/bm_slots/live_stats)。
//   fail-closed: 无映射/陈旧/非 in-play → game_row 保持 stub。从 TickOne 原样抽出, 行为逐位不变。
// ---------------------------------------------------------------------------
void TradingLoop::ResolveGameContext(const std::string& condition_id,
                                     stcpp::data::feature_store::FeatureStoreGameRow& game_row,
                                     bool& map_is_draw) {
    // ---- A1: 解析真实 Goalserve 比分 (condition→event 映射 + tick-local 共享快照) ----
    // fail-closed: 无 score_store / 无映射 / 未匹配 / 陈旧 / 非 in-play → 保持 stub.
    // A4: 用 TickAll 入口冻结的 tick_score_snap_/tick_event_map_ (整 tick 同版本, 消 read-skew),
    //     不再 per-condition 各自 Get()/LoadEventMap()。
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
                        // 记队名 (2026-06-04 老板「名字是已知的, 你是没记录吗, 很不严谨」): YES-canonical
                        //   落 game_row 供审计匹配对错。home_team=YES 队, away_team=对手 (按 yes_is_home 翻)。
                        game_row.home_team = it->second.yes_is_home ? es.home : es.away;
                        game_row.away_team = it->second.yes_is_home ? es.away : es.home;
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
                        // A-step-2 分局盘 (老板「第一局/第二局」): 段盘 (seg_index>0) 用【当前段 fair】, 且
                        //   PM 段号 == bet365 当前段号 (es.inplay_seg_index) 才用 (过去/未来段无 live 赔率)。
                        //   不符 → -1 (fail-closed 无 sharp, 绝不回退全场 fair = 修 A-step-1 之前「全场套错段」)。
                        double src_home = es.inplay_bet365_home_fair;
                        double src_away = es.inplay_bet365_away_fair;
                        if (it->second.seg_index > 0) {
                            if (es.inplay_seg_index == it->second.seg_index &&
                                es.inplay_seg_home_fair >= 0.0) {
                                src_home = es.inplay_seg_home_fair;
                                src_away = es.inplay_seg_away_fair;
                            } else {
                                src_home = -1.0;  // 段号不符 / 无段赔率 → 无 sharp (fail-closed)
                                src_away = -1.0;
                            }
                        }
                        const auto inplay_yc =
                            stcpp::data::ToYesCanonical(it->second.yes_is_home, src_home, src_away);
                        game_row.inplay_bet365_home_fair = inplay_yc.yes_fair;  // YES 边胜率
                        game_row.inplay_bet365_away_fair = inplay_yc.opp_fair;  // 对手边胜率
                        // [score-flow diag] 匹配上的 in-play 场是否有 sharp (bet365 de-vig 真值)?
                        //   定位脱节: has_real_fair 场里多少真带 sharp (vs 只 score_prior)。
                        if (src_home >= 0.0)
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

}


// ---------------------------------------------------------------------------
// [D-阶段2] TrySettleResolved — 已定盘结算 (slice-3b/3c; 2026-06-12 业务流分段)
//   REST resolution 权威 ① / Goalserve 终态兜底 ② → realize+平仓 (幂等 settled_conditions_)。
//   返回 true = 已定盘 (调用方停止本盘决策)。从 TickOne 原样抽出, 行为逐位不变。
// ---------------------------------------------------------------------------
bool TradingLoop::TrySettleResolved(const std::string& condition_id, const BinaryMarketSnapshot& mkt,
                                    const stcpp::data::feature_store::FeatureStoreGameRow& game_row) {

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
            return true;  // 已定盘口
        }
        return false;
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
DecisionSide TradingLoop::SelectSide(double p_fair_yes, double p_market_devig) const noexcept {
    // de-vig 锚定 (小梁 spec §1-2): raw_edge_yes 与 raw_edge_no 精确互为相反数 → 不可能两边同正。
    //   选被低估边: raw_edge_yes >= 0 → YES 模型价 > 市场共识 = YES 低估 → 买 YES;
    //               raw_edge_yes < 0  → NO 低估 → 买 NO。下游 sizing/CI gate 定是否真够 edge 下单。
    const bool is_yes = (p_fair_yes - p_market_devig) >= 0.0;
    return DecisionSide{is_yes ? TradedSide::Yes : TradedSide::No, strategy::Side::Buy, 0.0};
}

void TradingLoop::TickOne(const BinaryMarketSnapshot& mkt) {
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

    // [D-阶段1] 比分/赔率源上下文解析 → ResolveGameContext (2026-06-12 业务流分段, 行为逐位不变)
    bool map_is_draw = false;  // 盈利修复: 3-way 平局盘 → 下游 sharp fair 取 draw 概率
    ResolveGameContext(condition_id, game_row, map_is_draw);

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
    // [D-阶段2] 已定盘结算 → TrySettleResolved (2026-06-12 业务流分段, 行为逐位不变)
    if (TrySettleResolved(condition_id, mkt, game_row)) {
        return;  // 已定盘口: 不产 quote/intent (持仓已 realize)
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
    // 默认 (无 sharp 赔率 + 无真实 Goalserve 先验): p_fair = p_market_devig.
    //   → edge ≈ 0 (锚在去 vig 的市场上自己跟自己比); 进场门 = fair_src==sharp_inplay (2026-06-12 删比分门,
    //   见下方 tradeable_fair), 非 sharp 即不开新仓, market_devig 自然不产 intent.
    //   这根除了"低价 outright 被 stub 强拉 → 假 edge"(Spain 0.169 → fake 1076bps).
    // 有真实 in-play game_row 时: 用 score-prior 置信加权混合到 de-vig 市场锚上,
    //   置信随时钟从 kBasePriorConfidence 升到 kMaxPriorConfidence; 终态 conf=1.0.
    double p_fair = p_market_devig;  // 最终由 ResolveFair 一处解析 (优先级集中在 fair_resolve.hpp; R-2 老周/老郭)
    pricing::FairSrc fair_src_dbg = pricing::FairSrc::kMarketDevig;  // [diag] 捕获 ResolveFair 真实选源
    // fair-input 标量。derivative 在下面 optional。
    //   赔率解耦 (老板 2026-06-12「删比分门, 进场认赔率不认比分」): sharp 赔率不再被 has_real_fair(比分)
    //   门锁 —— 无条件读 bet365 in-play 赔率 (与比分同一 inplay feed; game_row 字段默认 -1, 无赔率自然
    //   invalid, ResolveFair 据 [0,1] 判)。map_is_draw / game_row 已由上方 ResolveGameContext 填。
    double fair_sharp_yes =
        map_is_draw ? game_row.inplay_bet365_draw_fair : game_row.inplay_bet365_home_fair;
    double fair_score_prior = 0.5;
    double fair_prior_conf = 0.0;
    FairCandidates fair_cands;  // 候选 fair 全集 (ResolveFair 块内从 fin 捕获; 显示所有源 + 标记决出)
    // 3a 时序: 结算临近度 (老板 2026-05-31, feature-first; 体育免新数据源 — 从 Goalserve 时钟派生)。
    //   = clamp(1 − time_frac, 0, 1); terminal → 0 (结算已定); 无真 fair/无时钟 → NaN。喂模型 +
    //   与 bid_absence_frac 组合 = 「临近结算 ∧ 卖不出」归零陷阱信号 (模型学, 不硬门)。
    double time_to_resolution_frac = std::numeric_limits<double>::quiet_NaN();
    double g_time_x_lead = std::numeric_limits<double>::quiet_NaN();  // 批1: 时间感知领先 (体育最大非线性)
    double g_remaining_sec = std::numeric_limits<double>::quiet_NaN();  // 批1 补漏: 剩余秒
    SportsFeatures sports;  // 批1 体育动态 (game_row.score/live_stats 派生; 无真比分→NaN)
    // 分运动 必输局判定 (2026-06-04 老板「分运动」): +1=已决出且 YES 领先(NO 是必输方) / −1=已决出且 NO 领先
    //   (YES 是必输方) / 0=未决出。各运动用各自比分单位 + 阶段阈值 (替代统一 |diff|≥3 的 garbage_time)。
    double game_decided_sign = 0.0;
    // 临近末尾标志 (2026-06-05 老板「临近末尾必输的那种, 还得禁止买入」): phase_frac 进末段 (>0.85)。
    //   配 ExecuteControllerSide 内 exec_ask<near_end_max_buy_price (市场把本边定为近必输) → 禁止新开仓,
    //   防买进末段 longshot 被结算归零。窄闸 (仅末段+便宜), 非广义 leaning 闸 (老板「不要入场闸了」已撤广义)。
    bool near_end = false;
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
        // (fair_sharp_yes 已在上方无条件读取 — 赔率解耦, 不再锁在 has_real_fair 块内)
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
        near_end = (phase_frac > 0.85);  // 末段 (临近末尾必输买入闸用; 配 exec_ask 便宜判定)
        const double abs_diff = std::abs(score_diff);
        sports.garbage_time = (phase_frac > 0.85 && abs_diff >= 3.0) ? 1.0 : 0.0;
        sports.clutch = (phase_frac > 0.85 && abs_diff <= 1.0) ? 1.0 : 0.0;
        // 分运动 必输局判定 (老板「分运动」): 各运动比分单位 + 阶段阈值, 判该盘是否已基本决出。
        //   tennis/volleyball=盘差, esports=图差(series), basket/rugby/amf=分差, soccer/hockey=球差, baseball=分差。
        //   阈值 = 落后方近乎确定输的程度 (中后段)。落后方 = score_diff 符号反向。
        {
            const std::string& sp = game_row.sport;
            bool decided = false;
            if (sp == "tennis" || sp == "volleyball" || sp == "esports") {
                decided = (abs_diff >= 1.0 && phase_frac > 0.50);  // 盘/图: 落后≥1 且中后段 = 需连扳, 近决出
            } else if (sp == "basket") {
                decided = (abs_diff >= 12.0 && phase_frac > 0.85);  // 篮球: 末节 + 12 分
            } else if (sp == "amfootball" || sp == "rugby") {
                decided = (abs_diff >= 16.0 && phase_frac > 0.85);  // 美式/橄榄: 末段 + 16 分 (>2 次得分)
            } else if (sp == "soccer" || sp == "hockey") {
                decided = (abs_diff >= 2.0 && phase_frac > 0.82);   // 足/冰: 末段 + 2 球
            } else if (sp == "baseball") {
                decided = (abs_diff >= 4.0 && phase_frac > 0.70);   // 棒球: 末局 + 4 分
            } else {
                decided = (abs_diff >= 3.0 && phase_frac > 0.85);   // 默认 (原 garbage_time)
            }
            if (decided && score_diff != 0.0) game_decided_sign = (score_diff > 0.0) ? 1.0 : -1.0;
        }
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

    // ---- R-2 (老周/老郭 评审): 一处解析决策 fair (显式优先级 derivative > sharp > score-prior) ----
    //   纯函数可单测 (fair_resolve.hpp)。大模型 ML blend 已砍 (2026-06-05 老板「砍掉大模型训练功能」):
    //   fair 只来自 derivative/sharp/score-prior/市场 de-vig, 不再有 ONNX 推理 blend。
    {
        pricing::FairInputs fin;
        fin.p_market_devig = p_market_devig;
        fin.derivative_p_yes = derivative_p_yes;
        fin.sharp_yes = fair_sharp_yes;
        fin.score_prior_yes = fair_score_prior;
        fin.prior_conf = fair_prior_conf;
        fin.has_real_fair = has_real_fair;
        if (market_implied) {
            // outright/prop/series 市场兜底: 挡 score-prior/sharp/derivative → fair = 纯市场 de-vig。
            //   单场比分/匹配的 sharp 对"冠军/系列"语义错误, 必须挡 (防垃圾 fair); edge≈0 不交易。
            fin.derivative_p_yes = std::nullopt;
            fin.sharp_yes = -1.0;
            fin.prior_conf = 0.0;
            fin.has_real_fair = false;
        }
        // 候选 fair 全集快照 (观测「显示所有源 + 标记正在用的」): 取 ResolveFair 实际看到的 fin
        //   (含 market_implied 屏蔽: 屏蔽后 sharp=-1 / derivative=nullopt → 候选记 -1=不适用)。
        fair_cands.market_devig = fin.p_market_devig;
        fair_cands.sharp = (fin.sharp_yes >= 0.0 && fin.sharp_yes <= 1.0) ? fin.sharp_yes : -1.0;
        fair_cands.derivative = fin.derivative_p_yes.value_or(-1.0);
        fair_cands.score_prior = fin.has_real_fair
            ? pricing::blend_prob(fin.score_prior_yes, fin.p_market_devig, fin.prior_conf) : -1.0;
        const auto fr = pricing::ResolveFair(fin);
        p_fair = fr.p_fair;
        fair_src_dbg = fr.src;  // [diag] 真实选源 (sharp_inplay / score_prior_blend / ...)
        // [fair-sanity] 防垃圾门 (2026-06-03): fair 与市场极端背离 (>kMaxPlausibleEdge) = 大概率
        //   orientation 翻转 / EventMatcher 误配 / 模型饱和 (实测 inplay sharp de-vig clamp 0.9995 被
        //   贴到便宜 underdog YES → 假 86% edge → 垃圾成交)。真实体育 edge 极少 >0.45 → fail-closed
        //   不交易, 并 log src + 成分定位根因 (老韩式 edge 合理性上界)。
        // 改 log-only (2026-06-03, 老板「别那么谨慎, 还是虚拟盘」): 原阻断门是垃圾事故的应急止血, 但
        //   此门反而拦合法 derivative/sharp 大 edge (破 T17 totals 测试)。改只记录极端背离 (诊断), 不阻断。
        // [fair-sanity] 纯诊断 log (不拦)。砍大模型后 fair 由 sharp/derivative/score-prior 驱动
        //   (2026-06-05), 极端背离多为 orientation 翻转/误配/derivative 单位错配 (后者已由 deriv-sanity 守卫拦)。
        constexpr double kMaxPlausibleEdge = 0.45;
        if (std::abs(p_fair - p_market_devig) > kMaxPlausibleEdge) {
            // 2026-06-03 老板「调通模型让其盈利」: 实测 PM 体育盘高效 (事件延迟/信息边验证), 模型源
            //   (score_prior_blend; ml_blend 已 2026-06-12 治理删) 对市场极端背离 (>0.45) = 大概率
            //   模型错配/过度自信 → 回退市场价 (edge 归零, 不产单)。
            //   sharp_inplay/derivative 的大 edge 是合法信号 (sharp 钱 / totals 错价), 不拦, 仅记录。
            const bool model_src = (fr.src == pricing::FairSrc::kScorePriorBlend);
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
        // [orientation-flip 门] (2026-06-04 老板「方向错了, 排查一下」): sharp 与市场互补 (sharp+market≈1
        //   且差>0.30) = sharp 被贴到错误一边 (张冠李戴 / orientation 翻转, 实测 ~9 盘 sharp 0.91 vs 市场
        //   0.12) → 凭空造反向 fantasy edge → 反向下单亏。朝向可疑 → fail-closed: p_fair=市场 (edge 归零,
        //   任何源都不产单)。补 fair-sanity(>0.45) 漏掉的"互补但 model 已回退" + 中价位翻转。
        {
            const double sh = fin.sharp_yes;
            if (sh >= 0.0 && sh <= 1.0 && std::abs(sh + p_market_devig - 1.0) < 0.08 &&
                std::abs(sh - p_market_devig) > 0.30) {
                static std::atomic<int> flip_dbg{0};
                if (flip_dbg.fetch_add(1, std::memory_order_relaxed) < 60)
                    std::fprintf(stderr,
                                 "[orient-flip] cond=%.24s sharp=%.4f mkt=%.4f GS[home=%s|away=%s] (互补→朝向可疑, 排查名字匹配)\n",
                                 condition_id.c_str(), sh, p_market_devig,
                                 game_row.home_team.c_str(), game_row.away_team.c_str());
                p_fair = p_market_devig;  // 朝向可疑 → 不交易
            }
        }
    }

    // ---- sharp +EV 门 (2026-06-03 老板「改成 sharp 驱动」, 历史回测确认) ----
    //   42 万已结算行回测: src=sharp_inplay 且 |sharp − 市场| ≥ 5% 时, 结算结果站 sharp 77%,
    //   净 +0.20/单 (≥10% 偏离 98%/+0.36); <5% 偏离是噪声(亏); ML/score-prior/derivative 源无
    //   回测确认 edge。→ 只交易高置信 sharp 信号, 其余回退市场(edge 归零, 不产单)。
    //   (砍大模型后 fair 落 sharp/score-prior/market [无 ONNX blend]; 此门再收到只剩 sharp。)
    //   2026-06-04: 收进 cfg_.sharp_only_gate (默认关) —— 这是【策略过滤器】非管线不变量, 无条件施加会
    //   把通用 fill 管线/契约单测的非 sharp fair 全归零 (T17/T_Profit/TS4… 9 测试)。生产 daemon 置 true。
    // ---- 赔率源新鲜度门 (2026-06-04 老板「超过3秒的赔率源不进决策」) ----------------------------
    //   sharp 来自 inplay 赔率 feed (data_source_ts = Goalserve updated_ts, R-20 上游ts, 每版~2s 重盖)。
    //   feed 版本距决策刻 > sharp_max_staleness_sec = 赔率源陈旧 (feed 停更/掉点) → 回退市场 (不拿陈旧
    //   赔率决策, 防 feed 停更期市场已动而我们用旧值逆向下单)。常态不触发 (feed 活着每~2s 重盖 ts)。
    if (cfg_.sharp_max_staleness_sec > 0.0 && fair_src_dbg == pricing::FairSrc::kSharpInplay &&
        game_row.data_source_ts_ns > 0) {
        const double odds_age_sec = static_cast<double>(NowNs() - game_row.data_source_ts_ns) / 1e9;
        if (odds_age_sec > cfg_.sharp_max_staleness_sec) {
            static std::atomic<int> stale_dbg{0};
            if (stale_dbg.fetch_add(1, std::memory_order_relaxed) < 40)
                std::fprintf(stderr, "[odds-stale] cond=%.24s 赔率源 age=%.1fs > %.1fs → 回退市场(不决策)\n",
                             condition_id.c_str(), odds_age_sec, cfg_.sharp_max_staleness_sec);
            p_fair = p_market_devig;                       // 陈旧赔率源 → edge 归零, 不产单
            fair_src_dbg = pricing::FairSrc::kMarketDevig;  // 标记已回退 (下游 sharp_only_gate 不再当 sharp)
        }
    }

    if (cfg_.sharp_only_gate) {
        const double sharp_gap = std::abs(p_fair - p_market_devig);
        // 2026-06-04 老板「这个差的太多了」: gap 既要够大(≥min_edge 才有信号), 又不能离谱大
        //   (> max_gap = 快变盘 sharp 滞后 2.3s 的假 gap / 错配, 不是真 edge — 别拿陈旧 sharp 逆市场正确移动下单)。
        const bool sharp_signal = (fair_src_dbg == pricing::FairSrc::kSharpInplay) &&
                                  (sharp_gap >= cfg_.sharp_only_min_edge) &&
                                  (sharp_gap <= cfg_.sharp_max_gap);
        if (!sharp_signal) p_fair = p_market_devig;  // 非高置信 sharp (无信号/离谱大滞后) → 不产单
    }

    // ---- Phase B Step E (小梁 spec): 选边 (de-vig 锚定; p_fair 即 p_fair_yes, YES-canonical) ----
    const DecisionSide decision = SelectSide(p_fair, p_market_devig);
    bool is_yes = (decision.outcome == TradedSide::Yes);
    // 不要切边 (2026-06-10 老板「不要切边, 除非对面是赢家, 我们只买赢面大的」): 已持有某边 → 锁定决策到【持有边】,
    //   不因对面有 edge 就切过去 (放掉赢面仓追对面 = 切边, 老板禁)。对面成赢家由 game_decided 兜底 (处理持有边=必输
    //   方→平仓)。未持仓 → 按 SelectSide(edge 方向)选, min_open_fair(0.58) 保证只开赢面大的一方 (fair≥0.58)。
    {
        // 热路径直查 (性能审计 2026-06-12: 不建整张敞口表, 只查这两个 token)。聚合口径 (任一引擎持有即锁边)。
        const auto ypos = position_ledger_.get_position(mkt.yes_token_id);
        const auto npos = position_ledger_.get_position(mkt.no_token_id);
        const bool hold_yes = (ypos && ypos->size_usdc != 0);
        const bool hold_no = (npos && npos->size_usdc != 0);
        if (hold_yes && !hold_no) {
            is_yes = true;            // 锁 YES (持有 YES, 绝不切 NO)
        } else if (hold_no && !hold_yes) {
            is_yes = false;           // 锁 NO
        }
        // 两边都持仓 (历史切边残留) → 不锁, 让 M2-a/book 规则自然收敛到单边
    }
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
        // per-engine sizing (2026-06-12 Option A): 主决策环 = sharp → 当前敞口取 sharp 自己那份 →
        //   单源同值: 与 FeedRiskGateway 同走 position_ledger (per-engine 取 sharp 自己那份)。
        // 热路径直查 (性能审计 2026-06-12: 不每 tick 建整张敞口表)。
        // unit-contract-ok: ledger micro → sizing current_*_exposure_usdc 的 whole pUSD 域 (÷1e6)
        sz_in.current_condition_exposure_usdc =
            static_cast<double>(position_ledger_.get_engine_condition_exposure(condition_id, "sharp")) / 1'000'000.0;
        sz_in.current_token_exposure_usdc =
            static_cast<double>(position_ledger_.get_engine_position_size(token_id, "sharp")) / 1'000'000.0;
    }

    // c3 (P0-2 根治): caps 单一真值源 = cfg_ (whole pUSD), from_pusd 转正确 micro。sizing/RM 同源
    //   同值 (RM 侧 trader_daemon 亦 from_pusd 同源)。sizing 内部 .to_pusd() 回 whole 比 notional。
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
    // 进场门 (老板 2026-06-12「删比分门: 进场必须有赔率源, 不要求比分」): 新开仓只在 fair=sharp
    //   (bet365 in-play 赔率) 时放行 —— 不再用 has_real_fair(比分) 当进场条件, 也不放行 score-prior /
    //   裸市场 de-vig / paper_no_edge_gates 的非赔率 fair。
    //   ⚠ 仅限【进场/新开仓】: 进场后赔率消失, 持仓的离场/备用 (score-prior 估值 / sharp 掉档冻结 /
    //      game_decided / 结算) 全在 ResolveFair (赔率没了走 score-prior) + 下方 sel_target 逻辑, 不受此门限。
    //   (FLB 引擎已 2026-06-13 整删。)
    const bool tradeable_fair = (fair_src_dbg == pricing::FairSrc::kSharpInplay);
    double target_mag =
        (tradeable_fair && sizing_out.valid && devig_ok && net_ev_ok) ? sizing_out.suggested_notional : 0.0;
    // 必输方开仓护栏 (老板 2026-06-09「调试持仓逻辑, 查明真正原因」, 数据驱动): 被选边【模型 fair】太低 = 模型
    //   自己认为该边胜率极低 (近必输 longshot) → 不开新仓 (target=0; 减仓/平仓/must_win 不受限)。
    //   根因: 实测灾难性亏损全是「买便宜必输方→结算归零」(如网球 down-a-set underdog fair=0.11 买 0.08 → 崩到
    //   0.03, 单笔 −0.87/−2.00); 而分运动 game_decided 必输保护对 tennis best-of-3 永不触发 (set 差+phase 边界
    //   bug: set1/3 set 差恒 0, set2 phase 恰=0.5 不 >0.5)。用【模型 fair】(非市场价地板, 老板「用模型」) 当护栏:
    //   下侧 (到 0) 远大于 edge 的低 fair longshot 永远 −EV, 不该开。default 0=关 (lib/契约不变); daemon 置 0.15。
    if (cfg_.min_open_fair > 0.0 && p_fair_selected < cfg_.min_open_fair) {
        if (target_mag > 0.0) LogGateBlock(condition_id, "min_open_fair", p_fair_selected, exec_ask, target_mag);
        target_mag = 0.0;  // 模型认定近必输方 → 只减不开 (longshot 崩盘护栏)
    }
    // CLV 失效熔断 (2026-06-12 治理): CLV 正率<70% (TickAll 30s 刷新) = 入场质量系统性坏掉
    //   (赔率源断/匹配错/延迟恶化) → sharp 引擎停新开仓直到恢复。减仓/平仓/结算不受限
    
    if (target_mag > 0.0 && clv_breaker_) {
        LogGateBlock(condition_id, "clv_breaker", p_fair_selected, exec_ask, target_mag);
        target_mag = 0.0;
    }
    // 三振出局 gate (老板 2026-06-11 拍板): 同盘止损满 2 次 → 本场只减不开。首次止损后的再入照常
    //   (老板「当作新机会」语义保留); 连吃两次打脸 = 拉锯 régime (sharp 自身随比分来回翻, 无信息优势),
    //   不再循环送钱 (实测 3 个循环盘吃掉 78% realized 亏损, 最狠单盘 6 开 8 平 −16.6u)。
    constexpr int kMaxStopsPerMarket = 2;
    if (target_mag > 0.0) {
        if (const auto ms_it = market_stop_count_.find(condition_id);
            ms_it != market_stop_count_.end() && ms_it->second >= kMaxStopsPerMarket) {
            LogGateBlock(condition_id, "three_strikes", p_fair_selected, exec_ask, target_mag);
            target_mag = 0.0;  // 出局: 不开新仓 (减仓/平仓不受限, 同 min_open_fair 语义)
        }
    }
    // 赢面稳定窗 (老板 2026-06-11 拍板「入场太早赢面不稳定」): 被选边 sharp 在过去 3min 内必须【全程】
    //   ≥ min_open_fair 才开新仓 —— 买「稳定的赢面」不买「正在经过 0.65 的钟摆」。实测: 低桶 (0.65-0.75)
    //   入场中位 5 分钟即被割 = 买在摆动途中。sharp_history 是 YES-canonical: YES 侧看 WindowMin ≥ 门,
    //   NO 侧看 1−WindowMax ≥ 门。历史未覆盖整窗 (新盘/刚匹配/sharp 断流) → NaN → 不开 (fail-closed:
    //   等 3 分钟稳定证据)。减仓/平仓不受限 (同 min_open_fair 语义, hold 由下方 sel_target>=cur_e 保护)。
    // 2026-06-11 老板「进场条件只要进行中+有赔率源」: 稳定窗改【证据制】—— 窗口历史不全 (重启后/新盘/
    //   sharp 稀疏) 不再 fail-closed 黑窗 (原版每次重启全员 3min 进不了场), 只有【实际观测到】窗口内
    //   sharp 跌破过门槛 (= 钟摆证据) 才拦。
    if (target_mag > 0.0 && cfg_.min_open_fair > 0.0 && cfg_.open_stable_window_ns > 0) {
        bool unstable_evidence = false;
        if (const auto sh_st = sharp_history_.find(condition_id); sh_st != sharp_history_.end()) {
            if (is_yes) {
                const double wmin = sh_st->second.WindowMinSeen(cfg_.open_stable_window_ns);
                unstable_evidence = std::isfinite(wmin) && wmin < cfg_.min_open_fair;
            } else {
                const double wmax = sh_st->second.WindowMaxSeen(cfg_.open_stable_window_ns);
                unstable_evidence = std::isfinite(wmax) && (1.0 - wmax) < cfg_.min_open_fair;
            }
        }
        if (unstable_evidence) {
            LogGateBlock(condition_id, "stable_window", p_fair_selected, exec_ask, target_mag);
            target_mag = 0.0;  // 窗口内实证跌破过门槛 (钟摆) → 只减不开
        }
    }
    // edge-生命周期乘子 (持仓管理 Stage 2, 老板 2026-06-05「sharp 速度/收敛接进决策」): 用本盘 sharp 时序
    //   状态 (Vol 稳定性 + ConvergenceRate 发散谨慎) 缩 target 【量级】∈[floor,1], 抑制噪声驱动过度交易。
    //   PIT-safe: 查 sharp_history_ 已有样本 (本 tick push 在 PublishQuoteSnapshot, 在此之后)。
    //   ∈[floor,1] 不碰方向/不放大; 样本不足/NaN → 1.0 (fail-open, 等于基线)。架构界线见 ComputeLifecycleMultiplier。
    double lifecycle_mult = 1.0;
    if (target_mag > 0.0) {
        if (const auto sh_lc = sharp_history_.find(condition_id); sh_lc != sharp_history_.end()) {
            const std::int64_t sw_lc = cfg_.sharp_fair_vel_window_ns;
            const control::LifecycleInput lc_in{
                sh_lc->second.Vol(sw_lc), sh_lc->second.ConvergenceRate(sw_lc),
                static_cast<std::int32_t>(sh_lc->second.WindowSampleCount(sw_lc))};
            const control::LifecycleConfig lc_cfg{
                cfg_.lifecycle_mult_enabled, cfg_.lifecycle_vol_ref, cfg_.lifecycle_k_vol,
                cfg_.lifecycle_div_ref,      cfg_.lifecycle_k_div,   cfg_.lifecycle_floor,
                cfg_.lifecycle_min_samples};
            lifecycle_mult = control::ComputeLifecycleMultiplier(lc_in, lc_cfg);
            target_mag *= lifecycle_mult;
        }
    }
    // CLV sizing 乘子 (持仓管理 Stage 2, 老板 2026-06-05「CLV 好就实时放大」): 滚动 CLV 均值 (系统级近期
    //   入场质量) 调 target 量级 —— 可 >1 放大 (老板授权; 封顶 clv_max_mult 护栏; RM caps 仍硬夹)。
    //   样本不足/NaN → 1.0。不碰方向 (乘 |target|)。GM 护栏: 滚动均值非瞬时, 见 ComputeClvMultiplier。
    double clv_mult = 1.0;
    if (target_mag > 0.0) {
        const control::ClvSizingConfig clv_cfg{cfg_.clv_mult_enabled, cfg_.clv_ref,   cfg_.clv_k_amp,
                                               cfg_.clv_k_cut,         cfg_.clv_max_mult, cfg_.clv_floor,
                                               cfg_.clv_min_samples};
        clv_mult = control::ComputeClvMultiplier(
            rolling_clv_.Mean(), static_cast<std::int32_t>(rolling_clv_.Count()), clv_cfg);
        target_mag *= clv_mult;
    }
    // 相关性折扣乘子 (持仓管理 Stage 2 §4.1 规模层): 同赛事【其他】盘已有敞口 (ρ 加权占用 event cap) → 缩本盘
    //   target 量级 ∈[floor,1], 撞 R6.2c 硬 cap 前提前 taper。existing_event_gross 取 RM 同源 (扣本盘自身 →
    //   ∂m/∂target_self=0 防自激)。P0: ρ=NaN→rho_default 单一保守值 (per-type ρ 表待 paper 校准)。默认关。
    double corr_mult = 1.0;
    if (target_mag > 0.0 && cfg_.corr_mult_enabled) {
        const double existing_gross =
            static_cast<double>(rm_.get_event_gross_excl_condition(condition_id)) / 1'000'000.0;
        const control::CorrelationConfig corr_cfg{cfg_.corr_mult_enabled, cfg_.corr_taper_start,
                                                  cfg_.corr_floor, cfg_.corr_rho_default};
        corr_mult = control::ComputeCorrelationMultiplier(
            control::CorrelationInput{target_mag, existing_gross, cfg_.corr_event_cap_pusd,
                                      std::numeric_limits<double>::quiet_NaN()},
            corr_cfg);
        target_mag *= corr_mult;
    }
    const double target_signed = is_yes ? target_mag : -target_mag;

    // [decision-diag] 定位 sharp→可下单侧 脱节 (老板「为什么有 sharp 的源进不了可下单侧」)。
    //   仅 has_real_fair + 有 sharp 的盘, 节流打印: 真实选源/p_fair/devig/sharp_in/edge_ci/sizing 是否有效/
    //   net_ev 是否过/target。一眼看出 sharp 是否被选为 fair, 以及 valid/edge 在哪一步被砍成 0。
    if (has_real_fair && fair_sharp_yes >= 0.0 && fair_sharp_yes <= 1.0) {
        static std::atomic<int> dd_n{0};
        const int k = dd_n.fetch_add(1, std::memory_order_relaxed);
        if (k < 80)
            std::fprintf(stderr,
                         "[decision-diag] %s sport=%s mkt_type=%d mimplied=%d src=%s pfair=%.3f devig=%.3f "
                         "sharp_in=%.3f side=%s edge_ci=%.4f szvalid=%d net_ci=%.4f net_ev_ok=%d target=%.1f map_draw=%d\n",
                         condition_id.substr(0, 12).c_str(), game_row.sport.c_str(), mkt_type,
                         market_implied ? 1 : 0, pricing::to_string(fair_src_dbg), p_fair, p_market_devig,
                         fair_sharp_yes, is_yes ? "YES" : "NO", edge_ci_lower, sizing_out.valid ? 1 : 0,
                         sizing_out.net_ci_edge, net_ev_ok ? 1 : 0, target_mag, map_is_draw ? 1 : 0);
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
                         reservation.required_margin, game_decided_sign, near_end,
                         time_to_resolution_frac, g_time_x_lead, g_fld_signal,
                         g_remaining_sec, g_periods_won_home, g_periods_won_away, sports, game_row, book_row,
                         p_fair, static_cast<std::int8_t>(fair_src_dbg), fair_cands,  // 决策 fair + 选源 + 候选全集 (显示)
                         mkt.no.present ? mkt.no.book.data_source_ts_ns : 0,
                         mkt.no.present ? mkt.no.book.ingestion_ts_ns : 0,
                         mkt.no.present ? &mkt.no.book : nullptr);  // v0.8 NO book 5档深度
    stats_.quote_publishes.fetch_add(1, std::memory_order_relaxed);

    // ---- P0-4: advisory gate -----------------------------------------------
    // advisory=true (ML-R2: paper 期所有市场) → 不产生 intent, 不进 RM.
    // RM 不再作 advisory 防线; trading_loop 在此处主动 gate.
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

    // ---- token_id 有效性门 (2026-06-10 老板「修啊」) ------------------------
    // catalog 预热期被选边 token_id 可能畸形 (空/非数字/>77位) → 提前 fail-closed, 不构造 intent
    //   (否则走完 Step4-6 被 RM 兜底拒 INVALID_TOKEN_ID_FORMAT = 拒单噪声 + 白做功)。真 token_id
    //   加载后自然恢复交易。放在 quote publish 之后 → 不影响观测 (盘仍显示), 只挡交易 (同 advisory gate 语义)。
    //   warmup 瞬态: 此刻无持仓, 挡新开/加仓即可; 真 token_id 到位前本就不该有该盘仓位。
    if (!IsValidTokenId(token_id)) {
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

    // 必输局保护 (2026-06-04 老板「用比赛阶段数学模型, 分运动」): 用分运动决出判定 game_decided_sign ——
    //   +1=YES 领先已决出(NO 必输) / −1=NO 领先已决出(YES 必输)。被选边若是【必输方】→ target=0,
    //   控制器只减不开 (不买进必输局结算归零被套)。各运动用各自比分单位+阶段阈值 (tennis/esports 盘图差,
    //   clock 运动时间+分差), 修「统一 |diff|≥3 对 tennis/esports 永不触发」。
    double sel_target = target_mag;
    const char* sel_reason = "kelly_reduce";  // 卖出原因 (老板「出现卖出就检查是否合理」): 默认 Kelly 减仓; 各 force 点覆盖
    if (game_decided_sign != 0.0) {
        const bool sel_is_loser = (is_yes && game_decided_sign < 0.0) || (!is_yes && game_decided_sign > 0.0);
        // 市场确认门 (2026-06-11 反事实实证: 0xc811 比分判死甩卖时市场仍价 0.42, 终局我方赢, 误判 −14.17):
        //   真判死的盘交易在 0.02-0.10; 比分判死还须【市场同意】(被选边市场隐含 ≤0.20) 才甩卖残值,
        //   否则持有让结算裁决 (杀比分计数误判甩赢家, 保留真垃圾时间残值回收)。
        constexpr double kGameDecidedMaxPx = 0.20;
        if (sel_is_loser) {
            if (devig_ok && p_devig_selected <= kGameDecidedMaxPx) {
                sel_target = 0.0;
                sel_reason = "game_decided";  // 该运动已决出 + 市场确认 → 平残值
            } else {
                // 市场不确认 (价 >0.20 = 比分判死可能误判/翻盘中) → 【冻结持有】等结算 (2026-06-11
                //   堵漏: 原版只挡甩卖不冻结 → 仓位漏进普通 Kelly 路径被 kelly_reduce 原价甩 (实测
                //   20.2u@0.292 −7.26), 所有持有保护都没接住。哲学同 hold-to-settle: 让结算裁决。
                // per-engine: 冻结的「现仓」= sharp 自己那份 (与 current_pusd 同源, 防 freeze 目标>current 误买)。
                const double cur_gd = std::abs(
                    static_cast<double>(position_ledger_.get_engine_position_size(token_id, "sharp")) / 1'000'000.0);
                if (cur_gd > 0.0 && sel_target < cur_gd) sel_target = cur_gd;  // 冻结: 只增不减由后续门管
            }
        }
    }

    // 必赢锁利买入 (2026-06-05 老板「必赢的, 只要除去买和卖手续费有利润就买」): 已决出且被选边是【赢方】→
    //   锁结算收敛利润。买进结算到 1, 扣买+卖手续费仍净正 → 强制买到 must_win_lock_usdc (绕 Kelly 谨慎 +
    //   force_cross 穿价)。事件延迟真 edge (市场尚未把赢方收敛到 1)。decided 阈值已含高 phase, 不另判末段。
    bool sel_force_winbuy = false;
    if (cfg_.must_win_lock_usdc > 0.0 && game_decided_sign != 0.0) {
        const bool sel_is_winner = (is_yes && game_decided_sign > 0.0) || (!is_yes && game_decided_sign < 0.0);
        if (sel_is_winner && std::isfinite(exec_ask) && exec_ask > 0.0 && exec_ask < 1.0) {
            const double fcoef = sz_in.fee_rate_coef;
            const double buy_fee = fcoef * exec_ask * (1.0 - exec_ask);
            const double sell_bid = (std::isfinite(exec_bid) && exec_bid > 0.0) ? exec_bid : exec_ask;
            const double sell_fee = fcoef * sell_bid * (1.0 - sell_bid);
            if ((1.0 - exec_ask) - buy_fee - sell_fee > 0.0) {  // 扣买卖费仍净正 = 锁利
                sel_target = std::max(sel_target, cfg_.must_win_lock_usdc);  // 买到锁仓上限 (RM market cap 兜)
                sel_force_winbuy = true;
            }
        }
    }

    // sharp 掉档冻结持仓 (老板 2026-06-09「修 sharp-dropout 卖飞」): 仓在 sharp 有效时开 (favorite, ~71% 赢),
    //   中途 sharp 掉出 → ResolveFair 塌到 score-prior/市场 de-vig → Kelly target→0 → 贱卖赢家 (实测 fair 0.64≪
    //   sharp 0.88 把 0.79 仓卖飞)。修: 【未决出 + fair 非 sharp(信号降级)】→ 冻结现仓 (sel_target=现仓, 不按降级
    //   信号开/加/减/平), 等 sharp 回来重评 / 结算。game_decided(比分驱动, 不依赖 sharp) 在上面已处理决出方, 不在此覆盖。
    bool sel_force_stop = false;   // 上移声明: 冻结期硬止损 (下方) 也要置位以绕 loss_cut HOLD
    if (game_decided_sign == 0.0 && !fair_is_sharp) {
        const auto pos_frz = position_ledger_.get_position(token_id);  // 聚合 (取 avg)
        // per-engine: 冻结现仓 = sharp 自己那份 (与 current_pusd 同源); avg 仍取聚合 (下方硬止损用)。
        const double cur_qty = std::abs(
            static_cast<double>(position_ledger_.get_engine_position_size(token_id, "sharp")) / 1'000'000.0);
        const double avg_frz = (pos_frz && pos_frz->avg_entry_price > 0.0) ? pos_frz->avg_entry_price : 0.0;
        if (cur_qty > 0.0) sel_target = cur_qty;   // 冻结: sharp 是真值源, 降级时不拿 score-prior 噪声减/平赢家
        // 冻结期硬下行保护 (2026-06-10 持仓策略会 老韩 bug#2 + 老板「下行不够细致 / 两边都要考虑」): 冻结 ≠ 裸暴露。
        //   sharp 掉档时 favorite 真崩盘 —— rel_stop / vel_exit 都 gated 在 fair_is_sharp 上 (见下) → 全失效 → 只能裸亏到
        //   结算。补一道【不依赖 sharp】的灾难止损: mark 跌破均入 ×(1−frozen_hard_stop_pct, 默认 0.40, 远松于 rel_stop 0.25)。
        //   安全性关键: 本门【仅在 fair_is_sharp==false 时触发】, 而 2026-06-09 −5.80 灾难的签名是【sharp 仍有效(0.79) +
        //   退化簿(0.0129)】—— 两者互斥, 结构上不可能回归 −5.80。再加【双边簿紧(价差 ≤ kFrozenStopMaxSpread)】门: 退化簿
        //   是单边 bid 塌 (价差极宽), 真崩盘是双边齐跌 (价差窄) → 只割双边确认的真崩盘, 不碰单边塌的退化簿。
        constexpr double kFrozenStopMaxSpread = 0.10;  // 簿紧门: 价差 ≤ 此值 = 双边确认真崩盘 (退化单边塌价差极宽)
        if (cur_qty > 0.0
            && FrozenHardStopTriggered(avg_frz, mark_price, exec_bid, exec_ask,
                                       cfg_.frozen_hard_stop_pct, kFrozenStopMaxSpread)) {
            sel_target = 0.0;
            sel_force_stop = true;  // 冻结期灾难止损: 截尾损, 绕 loss_cut HOLD
            sel_reason = "frozen_hard";
        }
    }

    // (fair/mark 基止损全家桶 2026-06-12 治理删: rel_stop / vel_exit / 赢面持有门 v2 / book_det 割肉 ——
    //  2026-06-11 hold-to-settlement 反事实判死「任何 mark/fair 基止损都在收割自己」(n=5 被割仓 60% 终赢
    //  Δ+54), 生产参数全 0 关闭数日, 实测 500 fill 0 卖出。出场只剩: 结算 + frozen_hard + game_decided
    //  市场确认。git 史可考。)

    // ★★ 持有到结算铁律 (2026-06-11 老板拍板 hold-to-settlement) ——【一票裁决, 最后】★★
    //   比赛进行中: 撤销任何止盈/缩仓卖出 (Kelly 缩仓等), 持有骑到结算。
    //   独立 backstop 不受此覆盖: ① frozen_hard (sharp 掉档+双边簿确认真崩盘, sel_force_stop=true,
    //   灾难逃生门 —— 2026-06-12 治理修复: 旧版此处把 force_stop 一并撤销, frozen_hard 结构性不可达)
    //   ② game_decided (sign≠0 本块不进) ③ settlement (SettleToken 不经此)。
    if (game_decided_sign == 0.0 && !sel_force_stop) {
        // per-engine: 持有骑到结算的「现仓」= sharp 自己那份 (历史他引擎份额不在此)。
        const double cur_e =
            std::abs(static_cast<double>(position_ledger_.get_engine_position_size(token_id, "sharp")) / 1'000'000.0);
        if (cur_e > 0.0 && sel_target < cur_e) {
            sel_target = cur_e;  // 持有骑到结算 (撤任何止盈/缩仓卖出)
        }
    }

    // 三振出局计数 (老板 2026-06-11): force_stop 连续段 = 1 个止损 episode (drain 多 tick 只计一次);
    //   清除后再触发算新 episode。frozen_hard / book_deteriorate / rel_stop 全计 (都是被打脸)。
    if (sel_force_stop) {
        if (market_stop_episode_.insert(condition_id).second) {
            const int n = ++market_stop_count_[condition_id];
            std::fprintf(stderr, "[two-strikes] cond=%.16s... 止损 episode #%d (%s)%s\n", condition_id.c_str(), n,
                         sel_reason, n >= 2 ? " → 出局, 本场不再开新仓" : "");
        }
    } else {
        market_stop_episode_.erase(condition_id);
    }

    // 被选边: 买增至 Kelly 目标 (target_mag; H-3: 无真 fair/无效 sizing → 0 → 只减不开)。
    last_sell_reason_[token_id] = sel_reason;  // 卖出原因 (老板「出现卖出就检查是否合理」): ApplyFill 对卖出回读填 FillRow
    // 入场决策上下文 (2026-06-13 复盘观测补全): devig/edge/Kelly/乘子/赔率龄/赛段 → FillRow 落盘
    EntryCtx ectx;
    ectx.devig_side = p_devig_selected;
    ectx.edge_ci = edge_ci_lower;
    ectx.kelly_sugg = sizing_out.valid ? sizing_out.suggested_notional : 0.0;
    ectx.m_life = lifecycle_mult;
    ectx.m_clv = clv_mult;
    ectx.m_corr = corr_mult;
    if (game_row.data_source_ts_ns > 0)
        ectx.odds_age_ms = static_cast<double>(NowNs() - game_row.data_source_ts_ns) / 1e6;
    if (std::isfinite(g_remaining_sec)) ectx.g_remain = g_remaining_sec;
    ectx.g_sdiff = static_cast<double>(game_row.score_home_total - game_row.score_away_total);
    ExecuteControllerSide(condition_id, token_id, is_yes ? strategy::Outcome::Yes : strategy::Outcome::No,
                          exec_feat, book_depth_l1, p_fair_selected, sel_target, sz_in.fee_rate_coef,
                          force_cross || sel_force_stop || sel_force_winbuy, n_eff_dyn, margin_floor_dyn,
                          reservation_noise_free, sel_force_stop, near_end, &ectx);

    // M2-a 平旧边: 非选边若有持仓 → target=0 平仓 (旧边 overpriced → bid 高 → reservation_sell 可成交)。
    const SideView& other = is_yes ? mkt.no : mkt.yes;
    if (other.present) {
        const std::string& other_token = is_yes ? mkt.no_token_id : mkt.yes_token_id;
        // 热路径直查 (性能审计 2026-06-12: 不建整表, 只查 other_token)。聚合口径 (任一引擎持有即视为 held)。
        const auto other_pos = position_ledger_.get_position(other_token);
        const bool other_held = (other_pos && other_pos->size_usdc != 0);
        if (other_held) {
            const auto& other_feat = other.book;
            // M2-a 扛一扛门 (2026-06-10 老板「还是割肉了」+「不然都要扛一扛」): 切换选边平旧边是裸割路径。
            //   2026-06-12 治理简化 (随 hold_if_winning_floor 旋钮删除): hold-to-settlement 语义 ——
            //   比赛进行中 + 会亏卖(旧边 bid < 旧边入场) → 一律不平, 扛到结算; 盈利平仓照常放行。
            bool skip_m2a_close = false;
            if (game_decided_sign == 0.0) {
                const auto pos_o = position_ledger_.get_position(other_token);
                const double avg_o = (pos_o && pos_o->avg_entry_price > 0.0) ? pos_o->avg_entry_price : 0.0;
                const double other_bid = other_feat.best_bid();  // 旧边真卖价
                if (avg_o > 0.0 && std::isfinite(other_bid) && other_bid < avg_o) {  // 会亏卖 → 扛着
                    skip_m2a_close = true;
                }
            }
            if (!skip_m2a_close) {
                // 平旧边走卖出 → 深度看 bid 侧 (best_bid_size); 无效则 fallback 1000 pUSD。
                const double other_depth =
                    (std::isfinite(other_feat.best_bid_size()) && other_feat.best_bid_size() > 0.0)
                        ? other_feat.best_bid_size()
                        : 1000.0;
                last_sell_reason_[other_token] = "m2a_switch";  // 卖出原因: 切换选边平旧边
                // 非选边 fair = 1 − 被选边 fair (de-vig 互余, YES/NO 对称)。target=0 → 仅平仓。
                ExecuteControllerSide(condition_id, other_token,
                                      is_yes ? strategy::Outcome::No : strategy::Outcome::Yes, other_feat,
                                      other_depth, 1.0 - p_fair_selected, /*target_mag=*/0.0,
                                      FeeCoefFor(condition_id), force_cross, n_eff_dyn, margin_floor_dyn,
                                      reservation_noise_free, /*force_stop=*/false, /*near_end=*/false);
            }
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
void TradingLoop::ExecuteControllerSide(const std::string& condition_id, const std::string& token_id,
                                      strategy::Outcome outcome,
                                      const polymarket::clob_wss::OrderBookFeatures& side_book,
                                      double book_depth_l1, double p_fair_side, double target_mag,
                                      double fee_coef, bool force_cross, int n_eff, double margin_floor,
                                      bool noise_free, bool force_stop, bool near_end,
                                      const EntryCtx* ectx) noexcept {
    const double exec_ask = side_book.best_ask();
    const double exec_bid = side_book.best_bid();
    const double mark_price = std::isfinite(side_book.microprice) ? side_book.microprice : side_book.mid;

    // M3 CLV 尺子: 每 tick 更新本 token 市场 mid (收盘参考价 = 结算前最后值)。离线评估, 不回喂决策。
    clv_tracker_.UpdateMid(token_id, mark_price);
    RepublishLedgerMark(condition_id, token_id, mark_price, side_book);  // 逐盘 PnL 实时 MTM (2026-06-11)

    // (执行层 §4.1 exec_margin/tox_gate 2026-06-12 治理删: 默认关从未验证; 逆选保护已由
    //  dynamic_reservation 的 amihud margin_floor 项 [活跃] 覆盖, hold-to-settlement 后无
    //  rebalance churn 病灶。git 史可考。)

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

    // current = 本边 token 当前持仓 (micro→whole pUSD; long ≥0)。
    //   per-engine (2026-06-12 Option A): 主决策环是 sharp 引擎 → 只取【sharp 自己那份】, 控制器据此
    //   决定加/减/平 → sharp 卖出最多卖到自己份 (历史他引擎份额不在 current 里, 永不被误卖)。
    double current_pusd =
        static_cast<double>(position_ledger_.get_engine_position_size(token_id, "sharp")) / 1'000'000.0;
    // 防抖死区 (小梁 Q-梁-2): threshold = max(floor, 0.10×|target|)。
    //   (fee_k 费率放宽扩展 2026-06-12 治理删: 恒 0 从未开, hold-to-settlement 后无 rebalance churn。)
    const double min_rebalance = control::ComputeRebalanceDeadband(
        target_mag, p_fair_side, fee_coef,
        control::DeadbandConfig{cfg_.min_rebalance_floor_pusd, 0.10});

    control::ControlInput cin;
    // DD→target (持仓管理 Stage2, 老板「只停加仓不砍现仓」): 回撤触发 (dd_mult_<1) 时只压【加仓】幅度
    //   (target>current 时把增量 ×dd_mult_), target≤current 的减仓 (sharp 驱动) 原样放行 → 绝不因回撤
    //   强制减仓 (低流动性区不被迫 taker 锤实浮亏)。dd_mult_=0 → 维持现仓 (只持不加不砍)。
    double dd_target = target_mag;
    if (dd_mult_ < 1.0 && target_mag > current_pusd) {
        dd_target = current_pusd + dd_mult_ * (target_mag - current_pusd);
    }
    cin.target_pusd = dd_target;  // 被选边 = Kelly(×乘子, DD 限加仓); 非选边平旧边 = 0
    cin.current_pusd = current_pusd;
    cin.reservation_buy_px = reservation.buy_px;
    cin.reservation_sell_px = reservation.sell_px;
    cin.fair = p_fair_side;  // taker 退出护栏锚 fair (2026-06-10 老韩复盘: 锚 reservation_buy 被 vig 偷放宽)
    cin.best_ask = exec_ask;  // 本边 ask (买入触价 + 限价不追门)
    cin.best_bid = exec_bid;  // 本边 bid (卖出触价 + 限价不追门)
    cin.min_rebalance_pusd = min_rebalance;
    // 最小买单 = Polymarket CLOB 5 股【凑整目标】(2026-06-13 老板「限制的是5股不是5美元, 别再搞错」修正,
    //   + 官方文档核实: minimum_order_size 单位是【股(outcome token)】非 USD; 真盘 API 全市场返 5;
    //   limit order 的 size 字段=股数; 低于 → INVALID_ORDER_MIN_SIZE 拒单。我们 marketable-limit 走 size=股):
    //   旧实现把「5 股」死写成 $5 USD → favorite 价带(5 股=$3.5-4.2)被 $5 过严白挡合格单。
    //   正确: USD 门 = 5 股 × 买价(exec_ask = intent.price 同值 → shares = size_pUSD/price = 5.00 股)。
    //   ⚠ 圆整缓冲 (kShareSafetyPad): live_order_gate 把 size_pUSD 转股时有两道向下损失 ——
    //     ① (int64)(size×1e6) 截断 ② shares floor 到 2 位小数 (0.01 股粒度) —— 实测全价带 96/771 (12.5%) 价位
    //     会被削成 4.99 股 → INVALID_ORDER_MIN_SIZE 真盘拒。垫 0.05 股 (成本 +1% ≈ $0.04) → 实测 0 拒。
    constexpr double kClobMinShares = 5.0;
    constexpr double kShareSafetyPad = 0.05;  // 抗 gate 圆整/截断, 保证落地 ≥5.00 股
    // share_floor_usd = 「5 股值多少 pUSD」= 把 PM「最小 5 股」(股数约束) 翻译成系统内部 pUSD 量纲的桥梁。
    //   两处复用: ① cin.min_order_pusd 控制器凑整目标 ② 下方 kMinBiteUsd 引擎切深度下限。
    const double share_floor_usd =
        (std::isfinite(exec_ask) && exec_ask > 0.0) ? (kClobMinShares + kShareSafetyPad) * exec_ask : 0.0;
    cin.min_order_pusd = share_floor_usd;  // (旧 max(cfg_.min_order_pusd,..) 删: cfg 旋钮已废, 见 hpp)
    cin.per_order_cap_pusd = cfg_.per_order_cap_usdc;
    cin.allow_short = false;       // 空头 clamp 0 (sell-to-open 对二元市场 N/A; 见 spec §11.6)
    cin.force_cross = force_cross;  // 小梁 Q-梁-2: fair 大跳绕死区 (买侧加仓; 含 stop 用于绕死区)
    cin.force_stop = force_stop;    // 老姜 2026-06-09: 仅灾难止损触发卖侧 taker 退出 (与 force_cross 解耦防 churn)
    // (cin.predictive_unwind 恒默认 false — cfg 旋钮 2026-06-12 治理删, 控制器能力+单测保留)

    const control::ControlAction action = control::Decide(cin);
    if (!action.act) {
        // 控制器决定本 tick 不动 (死区/限价不可成交/已达目标/fail-closed)。不产 intent。
        // gate 反事实 (2026-06-13 老板「限价不追也接日志, 量化纪律价值」): 仅记【真错过】——
        //   NotMarketable(ask>买保留=不追) + 想加仓(target>current)。死区/已达目标/卖侧不算错过, 不记。
        if (action.reason == control::NoActReason::NotMarketable && target_mag > current_pusd)
            LogGateBlock(condition_id, "no_chase", p_fair_side, exec_ask, target_mag - current_pusd);
        stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // ---- 入场价感知平仓闸 (2026-06-04 老板「别稍微亏本就卖, 要考虑持仓买卖价格」) ----------
    //   减仓卖单若卖价(bid) < 均入价 = 锁亏。只在【信号真反转】(本边 fair 跌破均入超 loss_cut_fair_band,
    //   = 该止损) 才放行割损; 否则 HOLD —— 不为 fair 小波动/predictive_unwind 在亏损里 churn 卖出。
    //   取利平仓 (bid ≥ 均入) 与盈利减仓不受限。settlement realize 走 SettleToken 不经此, 不受影响。
    //   force_stop (相对止损已触发) 时绕过此 HOLD —— mark 已跌破均入 ×(1−rel_stop_pct), 该割就割, 不再等 fair。
    if (!force_stop && cfg_.loss_cut_fair_band > 0.0 && action.side == strategy::Side::Sell && action.is_close) {
        const auto pos_now = position_ledger_.get_position(token_id);
        const double avg_entry = (pos_now && pos_now->avg_entry_price > 0.0) ? pos_now->avg_entry_price : 0.0;
        if (avg_entry > 0.0 && exec_bid < avg_entry &&
            p_fair_side > avg_entry - cfg_.loss_cut_fair_band) {
            // 卖价低于均入(会锁亏) 且 fair 未真跌破均入(信号没反转) → 不在亏损里卖, 持有等回归/结算。
            stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    // ---- 订单簿结构感知 买/卖 (2026-06-04 老板「买卖都要看订单簿结构; 一直涨能卖就持仓, 簿转向才止盈」) ----
    //   本边簿结构: L1 失衡 imb=(bid_sz−ask_sz)/和; microprice vs mid = 方向压力。
    //   簿转向下行 (卖压) = 失衡 < −thr 且 microprice < mid。
    //   · 买入(建仓/加仓): 簿正下行 → 不接下跌的刀, 等簿稳 (HOLD)。
    //   · 盈利减仓(bid≥均入=取利): 簿仍支撑(未转向下行) → 骑住趋势不急止盈; 仅簿转向才放行止盈。
    if (cfg_.book_exit_enabled) {
        const double bsz = side_book.best_bid_size();
        const double asz = side_book.best_ask_size();
        const double imb = (std::isfinite(bsz) && std::isfinite(asz) && bsz + asz > 0.0)
                               ? (bsz - asz) / (bsz + asz)
                               : 0.0;
        const double micro = std::isfinite(side_book.microprice) ? side_book.microprice : mark_price;
        const bool book_down = (imb < -cfg_.book_exit_imb_thr) && (micro < side_book.mid);  // 簿转向下行
        if (action.side == strategy::Side::Buy && book_down) {
            // 入场先动者判别 (2026-06-10 老板「入场看赔率源先动还是订单簿先动」): 簿在往坏方向走时, 区分真假 edge ——
            //   ① 赔率源(sharp)先动 (本边 fair velocity > 0 = 在升) = 【先手优势】, edge 真 → 即使簿短暂下行也进场;
            //   ② sharp 没领先 (velocity ≤ 0) + 簿下行 = edge 很可能是【sharp 滞后(2.3s)才产生的假象】(PM 已知价在跌、
            //      sharp 还没跟上) → 接飞刀必亏 → 挡。这是治 Goalserve 滞后逆选的入场闸 (与"赔率源先动=先手"同源)。
            bool sharp_led = false;
            if (const auto shb = sharp_history_.find(condition_id);
                shb != sharp_history_.end() &&
                shb->second.WindowSampleCount(cfg_.sharp_fair_vel_window_ns) >= 3) {
                const double vel_yes = shb->second.Velocity(cfg_.sharp_fair_vel_window_ns);
                if (std::isfinite(vel_yes)) {
                    const double side_vel = (outcome == strategy::Outcome::Yes) ? vel_yes : -vel_yes;
                    sharp_led = (side_vel > 0.0);  // 本边 sharp fair 在升 = 赔率源先动 = 先手
                }
            }
            if (!sharp_led) {  // 簿下行 + sharp 没领先 → 假 edge (sharp 滞后) → 不接下跌的刀
                LogGateBlock(condition_id, "book_down_no_lead", p_fair_side, exec_ask, action.size_pusd);
                stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // sharp 领先 → 先手优势, 放行入场 (即使簿短暂下行)
        }
        if (action.side == strategy::Side::Sell && action.is_close) {
            const auto pos_tp = position_ledger_.get_position(token_id);
            const double avg_e = (pos_tp && pos_tp->avg_entry_price > 0.0) ? pos_tp->avg_entry_price : 0.0;
            if (avg_e > 0.0 && exec_bid >= avg_e) {  // 盈利区
                // 骑住赢家 (2026-06-10 老板「一直涨能卖就持仓, 簿转向才止盈」): 簿仍支撑 → 不急止盈。
                //   (tp_reversal_vel_thr/near_settle_capture_frac 旋钮 2026-06-12 治理删 — 生产恒 0 =
                //    本 book-only 行为即生产语义; hold-to-settlement 下盈利减仓本就被铁律撤销。)
                if (!book_down) {
                    stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
                    return;  // 簿仍支撑 → 骑住
                }
            }
        }
    }

    // (再入场冷却闸 reentry_cooldown_ns + rebuy fair 改善门 rebuy_edge_premium 2026-06-12 治理删:
    //  生产恒 0 关; hold-to-settlement 后无卖出→无 rebuy churn 路径, 防的病已不存在。git 史可考。)

    // (min_buy_price 必输局保护 2026-06-12 治理删: 生产恒 0.0, 被 min_open_fair + near_end 闸取代。)

    // ---- 临近末尾必输买入闸 (2026-06-05 老板「临近末尾必输的那种, 还得禁止买入」) ----------------
    //   末段 (near_end: phase_frac>0.85) 且本边市场买入价(exec_ask) < near_end_max_buy_price (市场把该边定为
    //   近必输) → 不开新仓, 防买进末段 longshot 被结算归零。窄闸 (仅末段+便宜双条件, 用市场实时价非陈旧 sharp)。
    //   减仓/平仓/中前段/非便宜边不受限。
    if (cfg_.near_end_max_buy_price > 0.0 && near_end &&
        action.side == strategy::Side::Buy && exec_ask < cfg_.near_end_max_buy_price) {
        LogGateBlock(condition_id, "near_end", p_fair_side, exec_ask, action.size_pusd);
        stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // ---- 高价带买入闸 (2026-06-12 老板「分析数据样本盈利最大化」, 数据驱动) ----------------
    //   实测 41 结算分带: 0.70-0.84 入场带 = 利润引擎 (21 笔 ~95% 胜, +23%/笔);
    //   ≥0.84 带 = 出血点 (15 笔, sharp 79% 胜 vs BE 87%, 合计 −49 — 0.85-0.90 桶 62% 胜 −27%/笔)。
    //   favorite 不对称在高价带惩罚最狠 (输 = 全额 −0.86×qty, 赢只 +0.14×qty), 校准误差零容忍;
    //   且 deploy 94% 资金稀缺, 高价带每一元都在挤占 +23% 带的仓位。
    //   豁免: force_cross (进球事件/必赢锁利 — 实测 0.90+ 锁利 5/5 全胜, 是另一性质的事件 edge)。
    //   减仓/平仓不受限。50 笔新结算后复评 (代码常数, 老板「策略系数不进配置层」)。
    constexpr double kMaxOpenAsk = 0.84;
    if (action.side == strategy::Side::Buy && !force_cross && exec_ask > kMaxOpenAsk) {
        LogGateBlock(condition_id, "max_open_ask", p_fair_side, exec_ask, action.size_pusd);
        stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
        return;  // 高价带 −EV (数据实证) → 不买; 资金让位给 0.70-0.84 带
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
    // ---- 深度感知切单 (Phase 2, depth-aware-accumulation 设计 §3.1) -----------------------
    //   FOK 单若 size > 簿当下可成交深度 → 整单 0 成交 (fill-or-kill 语义)。把【买单】切到 L1 ask 可成交
    //   深度的 SAFETY_FRAC, 让 FOK 落在能成交的量内; 不足目标 (Kelly residual) 的部分由后续 tick 簿深度
    //   回补后继续吃 —— 跨 tick 累积 (current=sharp 份额, target=Kelly → 每 tick residual 自然递减)。
    //   仅买单 (开/加仓); 卖/平不切 (减仓要尽量出清, 别留尾仓)。
    //   ADR 2026-06-12-rm-boundary: RM 已不再用 LOW_FILL_RATE 毙单, 改由引擎在此主动切单到可成交深度。
    double order_size_pusd = action.size_pusd;  // 控制器动作 (已 clamp per_order_cap + 卖不超持仓)
    if (action.side == strategy::Side::Buy) {
        const double ask_depth = side_book.best_ask_size();  // L1 ask 可成交深度 (USD 口径)
        if (std::isfinite(ask_depth) && ask_depth > 0.0) {
            constexpr double kBiteSafetyFrac = 0.80;  // 不抢光显示量, 留并发 taker/撤单余量
            // 单笔下限 = Polymarket CLOB 5 股 × 买价 (2026-06-13 老板「5股不是5美元」; 同 cin.min_order):
            //   favorite 0.70-0.84 → $3.5-4.2 (旧死 $5 过严)。share_floor_usd 已在上方按 5×exec_ask 算好。
            const double kMinBiteUsd = share_floor_usd;
            const double fillable = ask_depth * kBiteSafetyFrac;
            if (order_size_pusd > fillable) order_size_pusd = fillable;  // 切到可成交深度; 余量下 tick 补
            if (order_size_pusd < kMinBiteUsd) {
                LogGateBlock(condition_id, "thin_depth_bite", p_fair_side, exec_ask, action.size_pusd);
                stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
                return;  // 切完不足单笔下限 → 不下, 等深度回补 (防 churn; 目标仍由后续 tick 累积)
            }
        }
        // 手续费 + 现金充足 (2026-06-13 老板「还要算上手续费, 否则钱不够如何交易」): 凑够 5 股的钱必须【含费】,
        //   否则下单时真实余额不足被拒。fee = size×fee_coef×p×(1−p) (R-fee-2 同公式)。cash_available 已扣
        //   多头锁定成本 + cum_fee (AccountEquitySnapshot, account_equity())。不够 = 物理下不了单 (非策略门),
        //   跳过等仓位结算释放现金。tick 入口冻结口径 (同 tick 多单按同一现金基准, 保守)。
        const double fee_est = order_size_pusd * fee_coef * exec_ask * (1.0 - exec_ask);
        if (order_size_pusd + fee_est > tick_equity_.cash_available) {
            LogGateBlock(condition_id, "insufficient_cash", p_fair_side, exec_ask, order_size_pusd);
            stats_.orders_held.fetch_add(1, std::memory_order_relaxed);
            return;  // 现金(含费)不够凑这单 → 等持仓结算回血; 不硬下导致链上 insufficient-funds 失败
        }
    }
    // size: 转 micro pUSD。
    intent.size_pUSD_micro = static_cast<std::int64_t>(order_size_pusd * 1'000'000.0);
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
        // P0-1: RM 内部 reject_here() 已 push_reject 一次; trading_loop 不重复写。
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
    // live 路由字段 (2026-06-12; paper matcher 忽略): token/方向/negRisk
    vord.token_id = sign_req.token_id;
    vord.is_buy = (intent.side == strategy::Side::Buy);
    if (tick_inputs_.catalog) {
        if (const auto pit2 = tick_inputs_.catalog->find(condition_id); pit2 != tick_inputs_.catalog->end()) {
            vord.neg_risk = !pit2->second.parent.neg_risk_market_id.empty();
        }
    }
    const execution::VirtualFill fill = executor_->Execute(vord);
    // R-11/R-7 模式断言 (2026-06-12 live 接线): paper build 必 0, live build 必 1 (编译期定)
    assert(fill.mode_tag ==
           (stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live ? 1u : 0u));
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
            cum_realized_by_market_[condition_id] += sell_realized;  // 逐盘累计 (对账修: 平仓不丢 realized)
            trade_returns_.push_back((fill.fill_price - pos_before->avg_entry_price) / pos_before->avg_entry_price);  // 逐笔收益 (Sharpe口径)
            if (trade_returns_.size() > 5000) trade_returns_.erase(trade_returns_.begin(), trade_returns_.begin() + 2500);
            // GateEvaluator 记分牌 (2026-06-12 治理): 逐笔已实现 PnL 序列 (G1/G5/G6 输入)
            gate_trade_pnl_.push_back(sell_realized);
            if (gate_trade_pnl_.size() > 5000) gate_trade_pnl_.erase(gate_trade_pnl_.begin(), gate_trade_pnl_.begin() + 2500);
            if (first_trade_ts_ns_ == 0) first_trade_ts_ns_ = as_of_now;
            last_trade_ts_ns_ = as_of_now;
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
    position_ledger_.apply_fill(condition_id, token_id, intent.outcome, ev, "sharp");  // per-engine 归属 (赔率引擎)

    // ---- Step 8b/8c: 账本快照 + 喂 RM 敞口 ----------------------------------
    PublishLedgerSnapshot(condition_id, fill, mark_price, side_book);
    stats_.ledger_publishes.fetch_add(1, std::memory_order_relaxed);
    FeedRiskGateway();

    // M3 CLV 尺子: 记买入(建仓)成交 entry (卖减仓是退出非建仓, 不计 CLV)。
    if (intent.side == strategy::Side::Buy) {
        clv_tracker_.RecordFill(token_id, fill.fill_price, mark_price,
                                static_cast<double>(fill.fill_size_usdc) / 1'000'000.0,
                                fill.as_of_ts_ns);  // 结算口径, 离线 only
        // 实时 CLV (PIT-safe, Stage2 sizing): 决策 fair − 成交价 (买被低估边: 正=入场优于 fair=好入场)。
        //   成交刻 fair 已观测 (无未来参考) → 可驱动 sizing。滚动均值 → control::ComputeClvMultiplier。
        rolling_clv_.Record(p_fair_side - fill.fill_price);
    }

    std::fprintf(stderr,
                 "[trading_loop] FILL cond=%.24s... tok=%.16s... side=%s is_close=%d "
                 "fill_sz=%.4f fill_px=%.4f fair=%.4f realized=%.4f\n",
                 condition_id.c_str(), token_id.c_str(),
                 (intent.side == strategy::Side::Buy) ? "BUY" : "SELL", intent.is_close ? 1 : 0,
                 static_cast<double>(fill.fill_size_usdc) / 1'000'000.0, fill.fill_price, p_fair_side,
                 sell_realized);

    // ---- 成交流水落 ring (2026-06-04 老板「看懂买卖价」) — 前端流水 + /api/v1/fills ----
    //   event_title 留空: 前端用自己的市场缓存把 condition_id 映射成人读队名 (后端不重复查)。
    {
        if (intent.side == strategy::Side::Buy) {
            engine_by_token_.emplace(token_id, "sharp");  // 引擎归因 (2026-06-12): 首次入场记, 不覆盖
        }
        FillRow fr;
        fr.as_of_ts_ns = fill.as_of_ts_ns;
        fr.condition_id = condition_id;
        fr.is_yes = (intent.outcome == strategy::Outcome::Yes);
        fr.is_buy = (intent.side == strategy::Side::Buy);
        fr.is_close = intent.is_close;
        fr.price = fill.fill_price;
        fr.size_usdc = static_cast<double>(fill.fill_size_usdc) / 1'000'000.0;
        fr.realized = sell_realized;
        fr.cum_realized = cum_realized_pnl_pusd_;
        fr.fair = p_fair_side;  // 模型对被交易边的 fair (FILL 日志 fair= 同源) — 前端算声称 edge
        fr.mark = mark_price;   // 成交刻市场 mark — 前端算模型偏差 fair−mark
        fr.fee = fr.size_usdc * FeeCoefFor(condition_id) * fill.fill_price * (1.0 - fill.fill_price);  // 逐笔费 (老板「逐笔体现」)
        // 研究级上下文 (2026-06-13 老板「订单簿指标也要落」): 成交刻簿快照 + 因子 + sharp + 部署率。
        fr.bk_spread = side_book.best_ask() - side_book.best_bid();
        fr.bk_bid_sz = side_book.best_bid_size();
        fr.bk_ask_sz = side_book.best_ask_size();
        {
            const double bs = side_book.best_bid_size(), as_ = side_book.best_ask_size();
            if (std::isfinite(bs) && std::isfinite(as_) && bs + as_ > 0.0) fr.bk_imb = (bs - as_) / (bs + as_);
        }
        if (std::isfinite(side_book.microprice) && std::isfinite(side_book.mid))
            fr.bk_micro_mid = side_book.microprice - side_book.mid;
        if (side_book.as_of_ts_ns > 0)
            fr.bk_age_ms = static_cast<double>(fill.as_of_ts_ns - side_book.as_of_ts_ns) / 1e6;
        if (const auto th = ts_history_.find(condition_id); th != ts_history_.end()) {
            const std::int64_t w = cfg_.ts_feature_window_ns;
            fr.q_ofi = th->second.OFI(w);
            fr.q_rvol = th->second.RealizedVol(w);
            fr.q_mom5 = th->second.RateOfChangePerSec(300'000'000'000LL);
        }
        if (const auto shj = sharp_history_.find(condition_id);
            shj != sharp_history_.end() && shj->second.WindowSampleCount(cfg_.sharp_fair_vel_window_ns) >= 3) {
            fr.sh_fair = shj->second.last_sharp();
            fr.sh_vel = shj->second.Velocity(cfg_.sharp_fair_vel_window_ns);
        }
        fr.deploy_pct = tick_equity_.deploy_pct;
        // 复盘观测补全 (2026-06-13): 入场决策全息 — ctx 来自 TickOne (平旧边 nullptr → NaN 省略)
        if (ectx != nullptr) {
            fr.p_devig = ectx->devig_side;
            fr.edge_ci = ectx->edge_ci;
            fr.kelly_sugg = ectx->kelly_sugg;
            fr.m_life = ectx->m_life;
            fr.m_clv = ectx->m_clv;
            fr.m_corr = ectx->m_corr;
            fr.odds_age_ms = ectx->odds_age_ms;
            fr.g_remain = ectx->g_remain;
            fr.g_sdiff = ectx->g_sdiff;
        }
        fr.m_dd = dd_mult_;
        {
            const auto dm = polymarket::clob_wss::compute_depth_metrics(side_book);
            fr.d5_bid = dm.bid_depth_5lvl;
            fr.d5_ask = dm.ask_depth_5lvl;
        }
        fr.t_vol5m = side_book.trade_signed_vol_5m;
        fr.t_ratio5m = side_book.trade_buy_ratio_5m;
        fr.equity = tick_equity_.equity_bid;
        // 持有路径: 买入开始追踪 (结算行回填 hold/MAE/MFE)
        if (fr.is_buy) {
            auto& pp = pos_path_[token_id];
            if (pp.entry_ns == 0) { pp.entry_ns = fill.as_of_ts_ns; pp.entry_px = fill.fill_price; }
            if (std::isfinite(mark_price)) {
                pp.min_mid = std::min(pp.min_mid, mark_price);
                pp.max_mid = std::max(pp.max_mid, mark_price);
            }
        }
        if (!fr.is_buy) {  // 卖出原因 (老板「出现卖出就检查是否合理」): 回读本 tick 主逻辑写的决出原因
            const auto rit = last_sell_reason_.find(token_id);
            fr.exit_reason = (rit != last_sell_reason_.end()) ? rit->second : "kelly_reduce";
        }
        std::lock_guard<std::mutex> lk(fills_mu_);
        JournalFill(fr);
    fills_ring_.push_back(std::move(fr));
        if (fills_ring_.size() > kFillsRingCap) fills_ring_.pop_front();
    }
}


// ---------------------------------------------------------------------------
// 账本持久化 (2026-06-11 老板「迭代部署 vs 攒数据互相残杀」根治)
//   行式 TSV (无 JSON 依赖): V1 头 / P 持仓 / M 逐盘累计 / C CLV pending / R CLV 聚合。
//   写: tmp+rename 原子; 读: >24h 陈旧忽略。停机期错过的结算由 SettlementPoller(catalog∪held)
//   + 孤儿 sweep 自动补账。paper-only (R-11)。
// ---------------------------------------------------------------------------
void TradingLoop::SaveLedgerSnapshot() {
    if (cfg_.ledger_snapshot_path.empty()) return;
    const std::string tmp = cfg_.ledger_snapshot_path + ".tmp";
    FILE* fp = std::fopen(tmp.c_str(), "w");
    if (fp == nullptr) return;
    std::fprintf(fp, "V1 %lld %.10g %.10g\n", static_cast<long long>(NowNs()), cum_realized_pnl_pusd_,
                 cum_fee_pusd_);
    for (const auto& pv : position_ledger_.get_all_positions()) {
        if (pv.size_usdc == 0) continue;
        std::fprintf(fp, "P %s %s %d %lld %.10g\n", pv.condition_id.c_str(), pv.token_id.c_str(),
                     pv.outcome == strategy::Outcome::Yes ? 1 : 0, static_cast<long long>(pv.size_usdc),
                     pv.avg_entry_price);
    }
    for (const auto& [cid, v] : cum_realized_by_market_) {
        const auto fit = cum_fee_by_market_.find(cid);
        std::fprintf(fp, "M %s %.10g %.10g\n", cid.c_str(), v, fit != cum_fee_by_market_.end() ? fit->second : 0.0);
    }
    clv_tracker_.ForEachPendingFill([fp](const std::string& tok, const eval::CLVTracker::FillRec& r) {
        std::fprintf(fp, "C %s %.10g %.10g %.10g %lld\n", tok.c_str(), r.entry_price, r.entry_mid, r.size_pusd,
                     static_cast<long long>(r.entry_ts_ns));
    });
    const auto agg = clv_tracker_.aggregates();
    std::fprintf(fp, "R %llu %llu %.10g %.10g %.10g %.10g\n", static_cast<unsigned long long>(agg.n_settled),
                 static_cast<unsigned long long>(agg.n_positive_close), agg.sum_clv_close, agg.sum_clv_settle,
                 agg.sum_notional, agg.sum_notional_clv_close);
    // E 行: token→engine 归因 + B 行: 引擎分账 (2026-06-12 老板「能区分开」)
    for (const auto& [tok, eng] : engine_by_token_) {
        std::fprintf(fp, "E %s %s\n", tok.c_str(), eng.c_str());
    }
    // PE 行: per-engine 分仓持久化 (2026-06-12; key=token\x1fengine → 拆回 token/engine/size)。
    //   恢复时聚合仓位走 P 行 (apply_fill), PE 行只补归属层 split → 重启后 sharp/flb 各管各份不丢。
    for (const auto& [k, sz] : position_ledger_.get_engine_pos_snapshot()) {
        const auto sep = k.find('\x1f');
        if (sep == std::string::npos || sz == 0) continue;
        std::fprintf(fp, "PE %s %s %lld\n", k.substr(0, sep).c_str(), k.substr(sep + 1).c_str(),
                     static_cast<long long>(sz));
    }
    {
        std::lock_guard<std::mutex> lke(engine_mu_);
        for (const auto& [eng, eb] : engine_book_) {
            std::fprintf(fp, "B %s %.10g %lld %lld\n", eng.c_str(), eb.realized,
                         static_cast<long long>(eb.settles), static_cast<long long>(eb.wins));
        }
    }
    // L 行: CLV 末次观测 mid (终局仓估值锚; 无此行重启后赢定仓浮盈回退 0)。
    clv_tracker_.ForEachLastMid([fp](const std::string& tok, double mid) {
        std::fprintf(fp, "L %s %.10g\n", tok.c_str(), mid);
    });
    // (D 行 [抄底锚] 2026-06-13 随抄底引擎删除。)
    std::fclose(fp);
    std::rename(tmp.c_str(), cfg_.ledger_snapshot_path.c_str());
}

void TradingLoop::RestoreLedgerSnapshot() {
    if (cfg_.ledger_snapshot_path.empty()) return;
    FILE* fp = std::fopen(cfg_.ledger_snapshot_path.c_str(), "r");
    if (fp == nullptr) return;  // 无快照 = 全新开始
    char line[1024];
    bool header_ok = false;
    int n_pos = 0, n_clv = 0;
    const std::int64_t now = NowNs();
    while (std::fgets(line, sizeof(line), fp) != nullptr) {
        if (line[0] == 'V') {
            long long saved_ns = 0;
            double cr = 0.0, cf = 0.0;
            if (std::sscanf(line, "V1 %lld %lf %lf", &saved_ns, &cr, &cf) == 3) {
                if (now - saved_ns > 24LL * 3600 * 1'000'000'000) break;  // >24h 陈旧忽略
                cum_realized_pnl_pusd_ = cr;
                cum_fee_pusd_ = cf;
                header_ok = true;
            }
        } else if (!header_ok) {
            continue;
        } else if (line[0] == 'P' && line[1] == 'E') {
            // PE 行: per-engine 分仓归属 split (聚合仓位由上面 P 行经 apply_fill 恢复; 此处只补归属层)。
            char tok[90] = {0}, eng[20] = {0};
            long long sz = 0;
            if (std::sscanf(line, "PE %89s %19s %lld", tok, eng, &sz) == 3 && sz != 0)
                position_ledger_.restore_engine_split(tok, eng, sz);
        } else if (line[0] == 'P') {
            char cid[80] = {0}, tok[90] = {0};
            int is_yes = 0;
            long long sz = 0;
            double avg = 0.0;
            if (std::sscanf(line, "P %79s %89s %d %lld %lf", cid, tok, &is_yes, &sz, &avg) == 5 && sz != 0) {
                risk::FillEvent ev;
                ev.filled_size_micro = sz;
                ev.fill_price = avg;
                ev.mode_tag = position_ledger_.accepted_mode_tag();  // 跟本账本运行模式 (paper=0/live=1); 快照文件按模式分名 → 重建标签一致
                ev.event_ts_ns = ev.data_source_ts_ns = ev.ingestion_ts_ns = now - 1;
                ev.as_of_ts_ns = now;
                // 聚合恢复 (engine 空): per-engine split 由后续 PE 行补 (重启后各引擎各管各份)。
                position_ledger_.apply_fill(cid, tok, is_yes != 0 ? strategy::Outcome::Yes : strategy::Outcome::No,
                                            ev);
                ++n_pos;
            }
        } else if (line[0] == 'M') {
            char cid[80] = {0};
            double r = 0.0, f = 0.0;
            if (std::sscanf(line, "M %79s %lf %lf", cid, &r, &f) == 3) {
                cum_realized_by_market_[cid] = r;
                cum_fee_by_market_[cid] = f;
            }
        } else if (line[0] == 'C') {
            char tok[90] = {0};
            double px = 0.0, mid = 0.0, szp = 0.0;
            long long ts2 = 0;
            if (std::sscanf(line, "C %89s %lf %lf %lf %lld", tok, &px, &mid, &szp, &ts2) == 5) {
                clv_tracker_.RecordFill(tok, px, mid, szp, ts2);
                ++n_clv;
            }
        // (D 行恢复 2026-06-13 随抄底引擎删除 — 旧快照 D 行直接忽略)
        } else if (line[0] == 'E') {
            char tok[90] = {0}, eng[20] = {0};
            if (std::sscanf(line, "E %89s %19s", tok, eng) == 2) engine_by_token_[tok] = eng;
        } else if (line[0] == 'B') {
            char eng[20] = {0};
            double r = 0.0;
            long long st2 = 0, w2 = 0;
            if (std::sscanf(line, "B %19s %lf %lld %lld", eng, &r, &st2, &w2) == 4) {
                std::lock_guard<std::mutex> lke(engine_mu_);
                auto& eb = engine_book_[eng];
                eb.realized = r;
                eb.settles = st2;
                eb.wins = w2;
            }
        } else if (line[0] == 'L') {
            char tok[90] = {0};
            double mid = 0.0;
            if (std::sscanf(line, "L %89s %lf", tok, &mid) == 2) clv_tracker_.UpdateMid(tok, mid);
        } else if (line[0] == 'F') {
            long long ts3 = 0;
            char cid[80] = {0}, exitr[40] = {0}, eng[20] = {0};
            int iy = 0, ib = 0, ic = 0;
            double px = 0, szu = 0, rl = 0, crl = 0, fa = 0, mk = 0, fe = 0;
            if (std::sscanf(line, "F %lld %79s %d %d %d %lf %lf %lf %lf %lf %lf %lf %39s %19s", &ts3, cid, &iy,
                            &ib, &ic, &px, &szu, &rl, &crl, &fa, &mk, &fe, exitr, eng) == 14) {
                FillRow fr;
                fr.as_of_ts_ns = ts3;
                fr.condition_id = cid;
                fr.is_yes = iy != 0;
                fr.is_buy = ib != 0;
                fr.is_close = ic != 0;
                fr.price = px;
                fr.size_usdc = szu;
                fr.realized = rl;
                fr.cum_realized = crl;
                fr.fair = fa;
                fr.mark = mk;
                fr.fee = fe;
                fr.exit_reason = (exitr[0] == '-' && exitr[1] == 0) ? "" : exitr;
                fr.engine = (eng[0] == '-' && eng[1] == 0) ? "" : eng;
                std::lock_guard<std::mutex> lk(fills_mu_);
                // (恢复路径不写 journal — 这些是历史行, journal 在原始成交时已写过; 防重启重复)
                fills_ring_.push_back(std::move(fr));
                if (fills_ring_.size() > kFillsRingCap) fills_ring_.pop_front();
            }
        } else if (line[0] == 'R') {
            unsigned long long ns = 0, npc = 0;
            eval::CLVTracker::Aggregates a;
            if (std::sscanf(line, "R %llu %llu %lf %lf %lf %lf", &ns, &npc, &a.sum_clv_close, &a.sum_clv_settle,
                            &a.sum_notional, &a.sum_notional_clv_close) == 6) {
                a.n_settled = ns;
                a.n_positive_close = npc;
                clv_tracker_.RestoreAggregates(a);
            }
        }
    }
    std::fclose(fp);
    if (header_ok) {
        FeedRiskGateway();  // 恢复仓位敞口喂 RM (caps 立即生效)
        std::fprintf(stderr, "[ledger-restore] 快照恢复: 持仓 %d, CLV pending %d, cum_realized=%.2f (停机期结算由孤儿 sweep 补)\n",
                     n_pos, n_clv, cum_realized_pnl_pusd_);
    }
    RestoreFillsJournalTail();  // 成交流水环回放 (2026-06-13 老板「盯盘成交纪录都没有了」)
}

// RestoreFillsJournalTail — 从 fills journal 尾部回放最近 kFillsRingCap 笔进内存环 (2026-06-13)。
//   根因: fills_ring_ 纯内存, 重启即丢 (持仓/realized 走快照恢复, 流水没有) → 盯盘成交记录空。
//   journal 文件 (JournalFill 异步落盘) 在盘上有全量 → 启动时 tail 回放。仅启动期调用 (单线程),
//   仍取 fills_mu_ 保险。解析容错: 缺键给默认/NaN, 坏行跳过。
void TradingLoop::RestoreFillsJournalTail() {
    const bool live = stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live;
    const char* path = live ? "data/ml_capture/live_fills_journal.jsonl" : "data/ml_capture/fills_journal.jsonl";
    FILE* jf = std::fopen(path, "r");
    if (jf == nullptr) return;  // 无 journal = 全新开始
    // 收尾部行 (环形保留最后 kFillsRingCap 行, 文件序=时间序)
    std::deque<std::string> tail;
    {
        char buf[2048];
        while (std::fgets(buf, sizeof(buf), jf) != nullptr) {
            tail.emplace_back(buf);
            if (tail.size() > kFillsRingCap) tail.pop_front();
        }
    }
    std::fclose(jf);
    // 极简字段提取 (自家 JournalFill 写的扁平 JSON, 无嵌套/无转义)
    auto num = [](const std::string& s, const char* key, double dflt) -> double {
        const std::string pat = std::string("\"") + key + "\":";
        const auto p = s.find(pat);
        if (p == std::string::npos) return dflt;
        return std::strtod(s.c_str() + p + pat.size(), nullptr);
    };
    auto str = [](const std::string& s, const char* key) -> std::string {
        const std::string pat = std::string("\"") + key + "\":\"";
        const auto p = s.find(pat);
        if (p == std::string::npos) return {};
        const auto st = p + pat.size();
        const auto e = s.find('"', st);
        return e == std::string::npos ? std::string{} : s.substr(st, e - st);
    };
    constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
    std::size_t restored = 0;
    std::lock_guard<std::mutex> lk(fills_mu_);
    for (const auto& ln : tail) {
        if (ln.empty() || ln[0] != '{') continue;
        FillRow fr;
        fr.as_of_ts_ns = static_cast<std::int64_t>(num(ln, "ts", 0));
        fr.condition_id = str(ln, "cond");
        if (fr.as_of_ts_ns <= 0 || fr.condition_id.empty()) continue;  // 坏行跳过
        fr.is_yes = num(ln, "yes", 1) != 0;
        fr.is_buy = num(ln, "buy", 1) != 0;
        fr.is_close = num(ln, "close", 0) != 0;
        fr.price = num(ln, "px", 0);
        fr.size_usdc = num(ln, "qty", 0);
        fr.realized = num(ln, "realized", 0);
        fr.fair = num(ln, "fair", 0);
        fr.mark = num(ln, "mark", 0);
        fr.fee = num(ln, "fee", 0);
        fr.exit_reason = str(ln, "exit");
        fr.engine = str(ln, "engine");
        fr.bk_spread = num(ln, "bk_spread", kNan);
        fr.bk_bid_sz = num(ln, "bk_bid_sz", kNan);
        fr.bk_ask_sz = num(ln, "bk_ask_sz", kNan);
        fr.bk_imb = num(ln, "bk_imb", kNan);
        fr.q_ofi = num(ln, "ofi", kNan);
        fr.q_rvol = num(ln, "rvol", kNan);
        fr.q_mom5 = num(ln, "mom5", kNan);
        fr.sh_fair = num(ln, "sh_fair", kNan);
        fr.sh_vel = num(ln, "sh_vel", kNan);
        fr.deploy_pct = num(ln, "deploy", kNan);
        fr.hold_sec = num(ln, "hold_sec", kNan);
        fr.mae = num(ln, "mae", kNan);
        fr.mfe = num(ln, "mfe", kNan);
        fills_ring_.push_back(std::move(fr));
        if (fills_ring_.size() > kFillsRingCap) fills_ring_.pop_front();
        ++restored;
    }
    if (restored > 0)
        std::fprintf(stderr, "[fills-restore] journal 回放 %zu 笔成交流水进环 (%s)\n", restored, path);
}

// SamplePositionPaths — 持仓路径采样 (2026-06-13 老板「将来想做止损分析」): 60s/仓 一行
//   (ts/cond/token/边/量/均入/bid/ask/mid/sharp/engine) → position_path.jsonl。loop_thread_ only;
//   落盘走 journal_writer_ 异步缓冲 (决策环零磁盘阻塞)。
void TradingLoop::SamplePositionPaths() {
    const bool live = stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live;
    const char* path = live ? "data/ml_capture/live_position_path.jsonl" : "data/ml_capture/position_path.jsonl";
    const std::int64_t now = NowNs();
    for (const auto& pv : position_ledger_.get_all_positions()) {
        if (pv.size_usdc == 0) continue;
        double bid = std::numeric_limits<double>::quiet_NaN(), ask = bid, mid = bid, sharp = bid;
        if (const auto bk = hub_.Read(pv.token_id); bk && bk->valid) {
            bid = bk->best_bid();
            ask = bk->best_ask();
            mid = bk->mid;
        }
        if (const auto sh = sharp_history_.find(pv.condition_id); sh != sharp_history_.end())
            sharp = sh->second.last_sharp();  // YES-canonical
        const auto eit2 = engine_by_token_.find(pv.token_id);
        char buf[512];
        const int n = std::snprintf(
            buf, sizeof(buf),
            "{\"ts\":%lld,\"cond\":\"%s\",\"tok\":\"%.24s\",\"yes\":%d,\"qty\":%.4f,\"avg\":%.4f,"
            "\"bid\":%.4f,\"ask\":%.4f,\"mid\":%.4f,\"sharp\":%.4f,\"eng\":\"%s\"}\n",
            static_cast<long long>(now), pv.condition_id.c_str(), pv.token_id.c_str(),
            pv.outcome == strategy::Outcome::Yes ? 1 : 0,
            static_cast<double>(pv.size_usdc) / 1'000'000.0, pv.avg_entry_price, bid, ask, mid, sharp,
            eit2 != engine_by_token_.end() ? eit2->second.c_str() : "sharp");
        if (n > 0) journal_writer_.AppendLine(path, std::string(buf, static_cast<std::size_t>(n)));
    }
}

// LogGateBlock — gate 拒点反事实 journal (2026-06-13 老板「评估门值不值」): 入场被门挡时落一行
//   (盘/门名/fair/参考价/本想下多少) → gate_blocks.jsonl; per cond×gate 5min 节流防爆量。
//   未来与结算 join → 每道门的真实价值 (省的钱 vs 错过的钱) 可量化。loop_thread_ only。
void TradingLoop::LogGateBlock(const std::string& cond, const char* gate, double fair, double ref_px,
                               double would_usd) {
    const std::int64_t now = NowNs();
    auto& last = gate_log_ns_[cond + "|" + gate];
    if (now - last < 300'000'000'000LL) return;  // 5min 节流
    last = now;
    const bool live = stcpp::execution::ExecutionContext::Mode() == stcpp::execution::ExecutionMode::Live;
    const char* path = live ? "data/ml_capture/live_gate_blocks.jsonl" : "data/ml_capture/gate_blocks.jsonl";
    char buf[384];
    const int n = std::snprintf(buf, sizeof(buf),
                                "{\"ts\":%lld,\"cond\":\"%s\",\"gate\":\"%s\",\"fair\":%.4f,"
                                "\"px\":%.4f,\"would\":%.2f}\n",
                                static_cast<long long>(now), cond.c_str(), gate, fair, ref_px, would_usd);
    if (n > 0) journal_writer_.AppendLine(path, std::string(buf, static_cast<std::size_t>(n)));
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
void TradingLoop::SettleCondition(const std::string& condition_id, const std::string& yes_token_id,
                                const std::string& no_token_id, double settle_yes, double settle_no,
                                const data::feature_store::FeatureStoreGameRow& game_row) noexcept {
    SettleToken(condition_id, yes_token_id, strategy::Outcome::Yes, settle_yes, game_row);
    SettleToken(condition_id, no_token_id, strategy::Outcome::No, settle_no, game_row);
    // 结算后喂 RM: realized 进 daily_pnl + 敞口归零 (loop_thread_ 串行, R-12 满足)。
    FeedRiskGateway();
}

void TradingLoop::SettleToken(const std::string& condition_id, const std::string& token_id,
                            strategy::Outcome outcome, double settle_price,
                            const data::feature_store::FeatureStoreGameRow& game_row) noexcept {
    // M3 CLV 尺子: 结算 → 算该 token 全部建仓成交的 CLV (close mid / 0-1 settle)。离线评估 only。
    //   【2026-06-10 根因修复】必须在【无持仓早退之前】调 —— 我们大多数仓提前 take-profit/止损平掉, 结算时
    //   无持仓; 原 OnSettle 在早退之后 → 提前平仓的入场 CLV 永不计 → clv_n 恒 0 (edge 金标准失效)。
    //   OnSettle 内部检查 fills_[token], 无记录则 no-op (对没交易的 token 安全)。
    clv_tracker_.OnSettle(token_id, settle_price);

    // 当前持仓 (signed micro; v1 long-only ≥0)。无仓 → 后续 realize/平仓 no-op (CLV 已上面算过)。
    const auto pos_opt = position_ledger_.get_position(token_id);
    if (!pos_opt.has_value() || pos_opt->size_usdc == 0) {
        return;
    }
    const std::int64_t qty_micro = pos_opt->size_usdc;
    const double qty = static_cast<double>(qty_micro) / 1'000'000.0;
    const double avg = pos_opt->avg_entry_price;
    // realize PnL = (结算值 − 加权入场价) × qty (qty signed; v1 long → 正)。
    cum_realized_pnl_pusd_ += (settle_price - avg) * qty;
    cum_realized_by_market_[condition_id] += (settle_price - avg) * qty;  // 逐盘累计 (对账修: 结算也入逐盘)
    // P3+引擎分账 (2026-06-12 老板「能区分开」): 真实引擎标签 (entry 时记) 分账 + 逐笔日志
    {
        const auto eit = engine_by_token_.find(token_id);
        const std::string eng3 = eit != engine_by_token_.end() ? eit->second : "sharp";
        {
            std::lock_guard<std::mutex> lke(engine_mu_);
            auto& eb = engine_book_[eng3];
            eb.realized += (settle_price - avg) * qty;
            ++eb.settles;
            if (settle_price >= 0.5) ++eb.wins;
        }
        std::fprintf(stderr, "[settle] %s engine=%s realized=%+.2f entry=%.3f qty=%.1f cond=%.16s\n",
                     settle_price >= 0.5 ? "WIN" : "LOSE", eng3.c_str(), (settle_price - avg) * qty, avg, qty,
                     condition_id.c_str());
    }
    if (avg > 0.0) {  // 逐笔收益 (Sharpe口径)
        trade_returns_.push_back((settle_price - avg) / avg);
        if (trade_returns_.size() > 5000) trade_returns_.erase(trade_returns_.begin(), trade_returns_.begin() + 2500);
        // GateEvaluator 记分牌 (2026-06-12 治理): 结算路逐笔 PnL (与卖出路同序列)
        gate_trade_pnl_.push_back((settle_price - avg) * qty);
        if (gate_trade_pnl_.size() > 5000) gate_trade_pnl_.erase(gate_trade_pnl_.begin(), gate_trade_pnl_.begin() + 2500);
        if (first_trade_ts_ns_ == 0) first_trade_ts_ns_ = NowNs();
        last_trade_ts_ns_ = NowNs();
    }

    // 平仓: apply_fill 负 delta 到 0 (settle_price 作 fill_price; 平仓 avg 归零)。
    risk::FillEvent ev;
    ev.filled_size_micro = -qty_micro;  // 平掉全部 (→ 0, 不穿零)
    ev.fill_price = settle_price;
    ev.mode_tag = position_ledger_.accepted_mode_tag();  // 跟本账本运行模式 (live 仓结算必须用 live tag, 否则结算 fill 被门拒→仓平不掉)
    // R-20: 4ts 用终态比分 ts (禁 now() 替代 data_source); as_of = NowNs (结算时刻 ≥ ingestion)。
    ev.event_ts_ns = game_row.event_ts_ns;
    ev.data_source_ts_ns = game_row.data_source_ts_ns;
    ev.ingestion_ts_ns = game_row.ingestion_ts_ns;
    ev.as_of_ts_ns = NowNs();
    // 结算全平: 聚合 token→0, 账本「归零清该 token 全部引擎份」收口 (传非空 engine 触发清理; 归属仅作标签)。
    const auto seit = engine_by_token_.find(token_id);
    position_ledger_.apply_fill(condition_id, token_id, outcome, ev,
                                seit != engine_by_token_.end() ? seit->second : std::string("sharp"));

    // 成交流水补结算一笔 (2026-06-10 老板「成交流水的累计已实现也与 pnl 对不上」): 结算 realize 此前只进
    //   cum_realized_pnl_pusd_(账本) 不进 fills_ring_ → 流水 cum_realized 漏结算实现 → 与账本 cum_realized 背离。
    //   补一笔 settlement 流水 (is_close, price=settle, fee=0, exit_reason=settlement) → 流水 cum_realized 与账本一致。
    {
        FillRow fr;
        fr.as_of_ts_ns = ev.as_of_ts_ns;
        fr.condition_id = condition_id;
        fr.is_yes = (outcome == strategy::Outcome::Yes);
        fr.is_buy = false;          // 结算 = 平仓 realize (非买)
        fr.is_close = true;
        fr.price = settle_price;     // 结算值 (1/0/0.5)
        fr.size_usdc = qty;
        fr.realized = (settle_price - avg) * qty;
        fr.cum_realized = cum_realized_pnl_pusd_;  // 已含本笔结算 (与账本同源)
        fr.fair = settle_price;      // 结算权威 = fair
        fr.mark = settle_price;
        fr.fee = 0.0;                // 结算无交易费
        fr.exit_reason = "settlement";
        // 终局簿上下文 (2026-06-13 复盘补全): 收盘线 close_mid → 逐仓 CLV 离线可 derive (close−entry)
        if (const auto fb = hub_.Read(token_id); fb && fb->valid) {
            fr.final_bid = fb->best_bid();
            fr.final_ask = fb->best_ask();
            if (std::isfinite(fb->mid)) fr.close_mid = fb->mid;
        }
        // 持有路径回填 (2026-06-13 研究级落盘: 结算行带 hold/MAE/MFE — 出场研究金料)
        if (const auto ppit = pos_path_.find(token_id); ppit != pos_path_.end()) {
            const auto& pp = ppit->second;
            if (pp.entry_ns > 0) fr.hold_sec = static_cast<double>(fr.as_of_ts_ns - pp.entry_ns) / 1e9;
            if (std::isfinite(pp.min_mid) && pp.entry_px > 0.0) fr.mae = pp.entry_px - pp.min_mid;
            if (std::isfinite(pp.max_mid) && pp.entry_px > 0.0) fr.mfe = pp.max_mid - pp.entry_px;
            pos_path_.erase(ppit);
        }
        std::lock_guard<std::mutex> lk(fills_mu_);
        JournalFill(fr);
    fills_ring_.push_back(std::move(fr));
        if (fills_ring_.size() > kFillsRingCap) fills_ring_.pop_front();
    }

    // (CLV OnSettle 已移到函数顶部, 在无持仓早退前调 — 修提前平仓入场 CLV 漏计。)

    stats_.positions_settled.fetch_add(1, std::memory_order_relaxed);
    std::fprintf(stderr,
                 "[trading_loop] SETTLE cond=%.24s... tok=%.16s... settle=%.2f avg=%.4f qty=%.4f "
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
double TradingLoop::ComputeEdgeCiLower(double p_fair, double p_ask, int n_eff, double z) noexcept {
    // 单一实现: stcpp/strategy/edge_ci.hpp (回测-实盘共用同一公式, 消两处漂移; Phase4 阻塞3 修复)。
    return stcpp::strategy::ComputeEdgeCiLower(p_fair, p_ask, n_eff, z);
}

// ---------------------------------------------------------------------------
// NowNs — CLOCK_REALTIME epoch ns (pit.hpp 内联函数)
// ---------------------------------------------------------------------------

/*static*/
std::int64_t TradingLoop::NowNs() noexcept {
    return infra::wal::pit::NowRealtimeNs();
}

// ---------------------------------------------------------------------------
// PublishLedgerSnapshot — apply_fill 成功后更新 LedgerSnapshotHub
//
// R-20: 4 ts 来自 fill + feat (上游链路透传, 禁 now() 替代 data_source_ts_ns)
// R-11: mode = kPaper (标记 paper 模式)
// ---------------------------------------------------------------------------

void TradingLoop::PublishLedgerSnapshot(const std::string& condition_id, const execution::VirtualFill& fill,
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
    // 逐盘已实现 (老板 2026-06-09 对账修): 用持久累计 cum_realized_by_market_ (sell+settle 都加), 非硬编码 0。
    //   原硬编码 0 → 逐盘 net_pnl 永丢 realized → 平仓后和成交流水 fills 对不上 (顶栏早改 account 口径修了, 逐盘漏)。
    const double pnl_realized = cum_realized_by_market_[condition_id];
    // A1: fill_size_usdc micro → /1e6 转 pUSD 算 fee (unit-contract-ok: micro→pUSD)
    // R-fee-2: fee 系数 per-market (gamma feeSchedule.rate), 与 RM/sizing 同源 FeeCoefFor(condition).
    //   未填 → kDefaultFeeCoef(0.03), 与旧 sizing::kSportsTakerFeeRate 逐位不变。
    const double pnl_fee_this = (static_cast<double>(fill.fill_size_usdc) / 1'000'000.0) *
                           FeeCoefFor(condition_id) * fill.fill_price * (1.0 - fill.fill_price);
    const double pnl_gross = pnl_realized + pnl_unrealized;

    // A5 (老韩 spec §4): 累计已付 fee (单调加, whole pUSD)。FeedRiskGateway 的 DD 喂数读它
    //   (daily_pnl = 时点净 MtM − cum_fee)。loop_thread_ 单 writer, 无需 atomic。
    cum_fee_pusd_ += pnl_fee_this;
    cum_fee_by_market_[condition_id] += pnl_fee_this;            // 逐盘累计费 (对账修)
    const double pnl_fee = cum_fee_by_market_[condition_id];     // lf 逐盘 net = 累计realized + 浮盈 − 累计fee

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

// RepublishLedgerMark — 逐盘 PnL 实时 MTM 重发 (2026-06-11 老板「这种咋还算赔钱啊」):
//   PublishLedgerSnapshot 只在【成交时】发布 → 逐盘面板冻结在入场瞬间 (NO 已 0.999 仍显 −0.06=费);
//   持有到结算架构下中间无成交 → 冻结数小时。每 tick 用实时 mark 重算重发; 不碰 fee 累计 (那只在
//   真成交时累加, 此处只读 cum 映射, 用 find 不用 operator[] 防插入)。loop_thread_ only。
void TradingLoop::RepublishLedgerMark(const std::string& condition_id, const std::string& token_id,
                                    double mark_price,
                                    const polymarket::clob_wss::OrderBookFeatures& feat) noexcept {
    const auto pos = position_ledger_.get_position(token_id);
    if (!pos.has_value() || pos->size_usdc == 0) return;
    if (!(mark_price > 0.0) || !std::isfinite(mark_price)) return;
    const double net_qty = static_cast<double>(pos->size_usdc) / 1'000'000.0;
    const double avg_entry = pos->avg_entry_price;
    risk::LedgerFeatures lf{};
    lf.event_ts_ns = feat.event_ts_ns;
    lf.data_source_ts_ns = feat.data_source_ts_ns;
    lf.ingestion_ts_ns = feat.ingestion_ts_ns;
    lf.as_of_ts_ns = NowNs();
    lf.net_qty = net_qty;
    lf.avg_entry_price = avg_entry;
    lf.mark_price = mark_price;
    const auto rit = cum_realized_by_market_.find(condition_id);
    const auto fit = cum_fee_by_market_.find(condition_id);
    lf.pnl_realized = rit != cum_realized_by_market_.end() ? rit->second : 0.0;
    lf.pnl_unrealized = (mark_price - avg_entry) * net_qty;
    lf.pnl_fee = fit != cum_fee_by_market_.end() ? fit->second : 0.0;
    lf.pnl_gross = lf.pnl_realized + lf.pnl_unrealized;
    lf.mode = risk::ExecutionModeTag::kPaper;
    lf.valid = true;
    ledger_hub_.Publish(condition_id, lf);
}

// ---------------------------------------------------------------------------
// FeedRiskGateway — P0-1: paper 持仓敞口 → RM (激活 exposure 红线)
//
// 背景: RM 的 per_condition / per_outcome exposure cap 逻辑齐全, 但生产此前零喂数
//   (trading_loop 只 set_bankroll, 从不喂 exposure) → cap 永不咬 = 风控纸面化。
// 老韩 RM 契约 + 老周架构: loop_thread_ 内 apply_fill 后全量覆盖喂 RM。
//
// 🔴 单位门禁 (老周 P0 gate): 仓位账本 size_usdc 存 whole pUSD (apply_fill 把 whole
//   double cast int64); RM exposure 比 micro (check_position_caps_ from_micro(cur+size_micro))。
//   故喂前必 × 1e6 (whole → micro)。漏乘 → exposure 红线静默架空 (同 P0-2 单位 bug 同型)。
//   守护: test_trading_loop P0-1 单位门测试 (fill 越 cap → 必触 EXCEED_CONDITION_EXPOSURE)。
//
// daily_pnl (DD): A5 (老韩 spec) 已接通 (净 MtM − cum_fee, best_bid 清算; 见下)。A1 ledger PnL
//   单位根治后, 老郭事前否决前置已清除。
// consec_loss: 延 M2 (M1 只买不平 → 无平仓 trade = 无连亏源; feed-liveness NEVER FED 为预期正确态)。
// ---------------------------------------------------------------------------
// account_equity — 单一账户权益口径 (2026-06-01 凯利评审)。收敛原双轨 (FeedRiskGateway daily_pnl
//   与 RecordEquity / sizing bankroll 此前各算一套)。双口径: best_bid 保守 (凯利/DD) + microprice 展示。
//   stale book (data_source_ts 超 score_staleness_limit_ns) / 无效 bid → 该仓位 0 浮盈 (保守)。
//   R-11: 只读 paper position_ledger_ + hub_ 实例, 不碰真账本。
TradingLoop::AccountEquitySnapshot TradingLoop::account_equity() const noexcept {
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
        // 资金恒等式修复 (2026-06-10 老板「持仓市值加现金与账户净值对不上」): 估值【对所有仓统一覆盖】——
        //   bid/mark 有效用之, 否则【回退入场价】(无价信息按入场价计 → 该仓 unrealized=0)。原 bug: locked_cost 对
        //   所有多头无条件计, 但 position_mtm/unrealized 只在 mark 有效时计 (无簿仓 continue 跳过) → 无 mark 仓
        //   成本进了 cash 扣减、市值没进 position_mtm → 破坏 cash+position_mtm≡equity 恒等式 (实测差 46.59)。
        double bid_val = pv.avg_entry_price;   // 回退: 无有效 bid → 按入场价 (unrealized_bid 该仓 = 0)
        double mark_val = pv.avg_entry_price;  // 回退: 无有效 mark → 按入场价 (unrealized_mark 该仓 = 0)
        const auto bk = hub_.Read(pv.token_id);
        bool have_mark = false;
        if (bk.has_value()) {
            if (bk->data_source_ts_ns > s.as_of_ts_ns) s.as_of_ts_ns = bk->data_source_ts_ns;
            // [follow-up 小肖] staleness gate (stale book→0 浮盈) 改 DD 红线路径行为, 需老韩签字+改 A5 测试, 另案。
            const double bid = bk->best_bid();
            if (std::isfinite(bid) && bid > 0.0 && bid < 1.0) bid_val = bid;     // 保守清算价 (砸 bid)
            const double mark = bk->microprice;
            if (std::isfinite(mark) && mark > 0.0 && mark < 1.0) {
                mark_val = mark;  // 展示 (中间价)
                have_mark = true;
            }
        }
        // 终局仓估值统一 (2026-06-11 老板「算算账亏在哪」审计): 死簿仓回退成本价会把赢定仓浮盈记 0
        //   (实测 6 个 settling_won 被低估 ~+3.0, 总账与 positions 端口径背离 2.96)。统一用 CLV 末次
        //   观测 mid (与 positions_mtm 同口径); 双向更准 (输定仓按成本记同样高估权益)。bid 口径同享
        //   (终局死簿无 bid, 末次 mid 是最佳清算估计)。
        if (!have_mark) {
            const double lm = clv_tracker_.last_mid_for(pv.token_id);
            if (std::isfinite(lm) && lm > 0.0 && lm < 1.0) {
                mark_val = lm;
                const double cur_bid = bk.has_value() ? bk->best_bid() : std::numeric_limits<double>::quiet_NaN();
                if (!(std::isfinite(cur_bid) && cur_bid > 0.0)) bid_val = lm;
            }
        }
        s.unrealized_bid += (bid_val - pv.avg_entry_price) * qty;
        s.unrealized_mark += (mark_val - pv.avg_entry_price) * qty;
        s.position_mtm += mark_val * qty;  // 持仓市值: 全仓覆盖 (mark 或回退入场价) → cash+position_mtm≡equity
    }
    s.equity_bid = s.realized_equity + s.unrealized_bid;
    s.equity_mark = s.realized_equity + s.unrealized_mark;
    s.cash_available = cfg_.bankroll_usdc - locked_cost + cum_realized_pnl_pusd_ - cum_fee_pusd_;
    s.deploy_pct = cfg_.bankroll_usdc > 0.0 ? locked_cost / cfg_.bankroll_usdc : 0.0;  // P4 部署率 (2026-06-11 晚会)
    return s;
}

// positions_mtm — per-持仓 live MTM (mark-staleness fix 2026-06-05)。与 account_equity() 同源 (hub_.Read
//   live 簿 microprice), 但 per-token 展开 + 带 YES/NO (PositionView.outcome, 真账本里 side 是 known 的)。
//   无 live 簿 → mark 回落 avg_entry (unrealized=0, 老韩铁律#2 不臆造浮盈)。已平仓 (qty=0) 不列。仅观测。
std::vector<TradingLoop::PositionMtm> TradingLoop::positions_mtm() const noexcept {
    std::vector<PositionMtm> out;
    for (auto const& pv : position_ledger_.get_all_positions()) {
        // unit-contract-ok: signed micro → whole share (qty); 同 account_equity()
        const double qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
        if (qty == 0.0) continue;  // 已平仓 → 不列 (与 account_equity open_positions 口径一致)
        PositionMtm p;
        p.condition_id = pv.condition_id;
        p.is_yes = (pv.outcome == strategy::Outcome::Yes);
        p.net_qty = qty;
        p.avg_entry = pv.avg_entry_price;
        double mark = pv.avg_entry_price;  // fallback: 无 live 簿 → 成本价 (unrealized=0)
        std::int64_t as_of = pv.last_update_ts;
        bool live_book = false;
        const auto bk = hub_.Read(pv.token_id);
        if (bk.has_value()) {
            const double m = bk->microprice;
            if (std::isfinite(m) && m > 0.0 && m < 1.0) {
                mark = m;  // 当前 live 中价 (展示口径同 account)
                live_book = true;
            }
            if (bk->data_source_ts_ns > 0) as_of = bk->data_source_ts_ns;  // R-20: 簿版本时刻, 禁 now()
        }
        // 状态标 + 终局估值 (2026-06-11 老板「状态能标一下/盯盘不知道盈亏」): 终局盘 (比赛打完掉出
        //   catalog/簿撤) 无活簿 → 用 CLV 末次观测 mid 估值 (NO 已 0.999 的赢仓显真实浮盈, 不再显 0),
        //   状态按估值分赢定/输定/未明 (前端着色)。
        if (live_book) {
            p.status = "live";
        } else {
            const double lm = clv_tracker_.last_mid_for(pv.token_id);
            if (std::isfinite(lm)) mark = lm;
            p.status = mark >= 0.90 ? "settling_won" : (mark <= 0.10 ? "settling_lost" : "settling");
        }
        p.mark = mark;
        p.pnl_unrealized = (mark - pv.avg_entry_price) * qty;
        p.as_of_ts_ns = as_of;
        out.push_back(std::move(p));
    }
    return out;
}

void TradingLoop::FeedRiskGateway() noexcept {
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
void TradingLoop::PopulateFeatureColumns(
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

    // fair_value: 此函数填【估计器特征】(fv_result.p_yes(), 训练捕获用)。【真·决策 fair (p_fair) + fair_src
    //   覆盖在 TickOne PopulateFeatureColumns 调用后】(2026-06-10 修: 显示需决策 fair 非估计器, 见调用点)。
    qf.fair_value = fv_result.p_yes();
    // YES-canonical mark (feat=YES book; blend 与 capture 同源, BR-1 — 不用选边后 mark, 否则 NO 边偏)。
    const double yes_mark = std::isfinite(feat.microprice) ? feat.microprice : feat.mid;
    qf.market_mid = yes_mark;
    (void)mark_price;  // 参数保留 (调用方对称); 特征用 yes_mark 保 BR-1 一致
    // R-fee-2: per-market 手续费系数 (始终输出, fee 是 market 元数据非决策派生, 不受 has_real_fair gate)。
    //   进前端 /quote (量化因子)。与 RM/sizing 同源 FeeCoefFor(condition)。
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

void TradingLoop::PublishQuoteSnapshot(
    const std::string& condition_id, const pricing::FairValueResult& fv_result,
    const sizing::SizingOutput& sizing_out, double mark_price, double edge_ci_lower,
    const polymarket::clob_wss::OrderBookFeatures& feat, bool has_real_fair, double cross_spread,
    double no_microprice, double no_imbalance, bool devig_ok, std::int64_t joint_as_of_ts_ns,
    const std::string& event_id, const std::string& neg_risk_market_id, double target_signed_notional,
    double reservation_buy_px, double reservation_sell_px, double required_margin,
    double game_decided_sign, bool near_end,
    double time_to_resolution_frac, double g_time_x_lead, double g_fld_signal, double g_remaining_sec,
    std::int32_t g_periods_won_home, std::int32_t g_periods_won_away, const SportsFeatures& sports,
    const data::feature_store::FeatureStoreGameRow& ml_game_row,
    [[maybe_unused]] const data::feature_store::FeatureStoreBookRow& ml_book_row, double decision_fair,
    std::int8_t fair_src_code, const FairCandidates& fair_cands, std::int64_t no_book_ds_ts,
    std::int64_t no_book_ing_ts, const polymarket::clob_wss::OrderBookFeatures* no_book_full) noexcept {
    sizing::QuoteFeatures qf{};
    PopulateFeatureColumns(qf, condition_id, fv_result, mark_price, feat, cross_spread, no_microprice,
                           no_imbalance, devig_ok, joint_as_of_ts_ns, event_id, neg_risk_market_id,
                           time_to_resolution_frac, g_time_x_lead, g_fld_signal, g_remaining_sec,
                           g_periods_won_home, g_periods_won_away, sports, no_book_ds_ts, no_book_ing_ts,
                           no_book_full);
    // 显示用【真·决策 fair】覆盖估计器值 (2026-06-10 修「fair 到底用什么」: PopulateFeatureColumns 填的
    //   qf.fair_value=fv_result.p_yes() 是 score-prior 估计器中间值, 与决策 fair 不符 → 前端"fair 0.59 vs
    //   sharp 0.97"假象 + ⚠源非sharp 误报。决策真用 p_fair (ResolveFair: sharp 优先)。覆盖成决策值 + 暴露选源。
    //   qf.fair_value/fair_src 仅供显示 (sizing/RM 走独立 p_fair_selected, 不受影响)。
    qf.fair_value = decision_fair;
    qf.fair_src = fair_src_code;
    // 候选 fair 全集 (显示所有源 + fair_src 标记正在用的, 治「乱切」观感)
    qf.fair_cand_devig = fair_cands.market_devig;
    qf.fair_cand_sharp = fair_cands.sharp;
    qf.fair_cand_score_prior = fair_cands.score_prior;
    qf.fair_cand_derivative = fair_cands.derivative;
    // GS sharp 赔率版本时刻 (2026-06-05 老板「赔率延迟放合适位置」): now−它 = 驱动 sharp fair 的 inplay 赔率多旧。
    qf.sharp_data_source_ts_ns = ml_game_row.data_source_ts_ns;

    // sharp fair 时序环 push + 派生 (老板 2026-06-05「方向真值=赔率源 sharp; 盘口趋势/line movement 一阶导」):
    //   把赔率源 sharp 的轨迹时序化 → sharp 速度(line movement 方向) + 市场价相对 sharp 收敛/发散率。
    //   PIT: 用 sharp 赔率版本时刻 sharp_data_source_ts_ns (禁 now())。sharp/ts 无效 → 不 push (无环→NaN)。
    //   观测先行 (§8.1): 仅填 QuoteFeatures 观测字段, 绝不驱动交易决策 (Stage 2 另行设计+回测+人确认)。
    {
        const double sharp_yes = qf.g_bm_inplay_fair;  // YES-canonical sharp (de-vig); ∈(0,1) 才有效
        const std::int64_t sharp_ts = qf.sharp_data_source_ts_ns;
        if (sharp_yes > 0.0 && sharp_yes < 1.0 && sharp_ts > 0) {
            sharp_history_[condition_id].Push(sharp_ts, sharp_yes, qf.market_mid);
        }
        const auto sh = sharp_history_.find(condition_id);
        if (sh != sharp_history_.end()) {
            const std::int64_t sw = cfg_.sharp_fair_vel_window_ns;  // 默认 10s (~5 样本)
            qf.g_sharp_velocity = sh->second.Velocity(sw);
            qf.g_sharp_conv_rate = sh->second.ConvergenceRate(sw);
            qf.g_sharp_vol = sh->second.Vol(sw);
            qf.g_sharp_samples = static_cast<std::int32_t>(sh->second.WindowSampleCount(sw));
        }
    }

    // 持仓管理 Stage 2 sizing 乘子观测 (老板 2026-06-05): 用与 TickOne sizing 一致的成员重算, 暴露盯盘。
    //   方向归 sharp, 乘子只调 |target| 量级。g_lifecycle 缩噪声(≤1); g_clv 可放大(>1, 全局 CLV 驱动)。
    {
        if (const auto sh2 = sharp_history_.find(condition_id); sh2 != sharp_history_.end()) {
            const std::int64_t sw2 = cfg_.sharp_fair_vel_window_ns;
            const control::LifecycleInput lc_in{sh2->second.Vol(sw2), sh2->second.ConvergenceRate(sw2),
                                                static_cast<std::int32_t>(sh2->second.WindowSampleCount(sw2))};
            const control::LifecycleConfig lc_cfg{cfg_.lifecycle_mult_enabled, cfg_.lifecycle_vol_ref,
                                                  cfg_.lifecycle_k_vol,        cfg_.lifecycle_div_ref,
                                                  cfg_.lifecycle_k_div,        cfg_.lifecycle_floor,
                                                  cfg_.lifecycle_min_samples};
            qf.g_lifecycle_mult = control::ComputeLifecycleMultiplier(lc_in, lc_cfg);
        }
        qf.g_rolling_clv_mean = rolling_clv_.Mean();
        qf.g_rolling_clv_n = static_cast<std::int32_t>(rolling_clv_.Count());
        const control::ClvSizingConfig clv_cfg{cfg_.clv_mult_enabled, cfg_.clv_ref,      cfg_.clv_k_amp,
                                               cfg_.clv_k_cut,         cfg_.clv_max_mult, cfg_.clv_floor,
                                               cfg_.clv_min_samples};
        qf.g_clv_mult = control::ComputeClvMultiplier(qf.g_rolling_clv_mean, qf.g_rolling_clv_n, clv_cfg);
        // DD→target 乘子 (账户级回撤去险; 全局 dd_mult_ 成员, 与 ExecuteControllerSide 同源)。
        qf.g_dd_mult = dd_mult_;
        // 相关性折扣乘子 (per-event; 用 RM 同源 event_gross 重算; prospective 用 1.0 占位 — CM 与本笔量级无关)。
        if (cfg_.corr_mult_enabled) {
            const double existing_gross =
                static_cast<double>(rm_.get_event_gross_excl_condition(condition_id)) / 1'000'000.0;
            const control::CorrelationConfig corr_cfg{cfg_.corr_mult_enabled, cfg_.corr_taper_start,
                                                      cfg_.corr_floor, cfg_.corr_rho_default};
            qf.g_corr_mult = control::ComputeCorrelationMultiplier(
                control::CorrelationInput{1.0, existing_gross, cfg_.corr_event_cap_pusd,
                                          std::numeric_limits<double>::quiet_NaN()},
                corr_cfg);
        }
    }

    // 决出状态观测 (老板 2026-06-09): 分运动「必输/必赢局」判定 + 末段标志 → 盯盘看 must_win_lock 触发根因。
    qf.g_game_decided_sign = game_decided_sign;
    qf.g_near_end = near_end;

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

    // 大模型 ML 推理 / 训练捕获 (extract_full→fv_hub) / seq-arb advisory + 全部 model provenance
    //   字段 (model_id/model_kind/spec_version/model_confidence/fair_ci/advisory/ml_advisory_p_yes)
    //   已砍 (2026-06-05 老板「砍掉大模型训练功能, 删干净不要留尾巴」)。量化因子由
    //   PopulateFeatureColumns 直接填 qf (上面)。fair_value/决策由 sharp/score-prior/derivative 驱动。
    qf.model_as_of_ts_ns = feat.ingestion_ts_ns;  // feature PIT 锚 (baseline)

    qf.valid = fv_result.valid;

    quote_hub_.Publish(condition_id, qf);
}

}  // namespace stcpp::engine
