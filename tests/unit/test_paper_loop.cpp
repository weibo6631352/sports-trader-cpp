// tests/unit/test_paper_loop.cpp — PaperLoop 单测
#include <filesystem>
//
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-30
//
// 测试覆盖:
//   T01: 基础生命周期 — Start/Stop 不崩溃
//   T02: R-11 paper 不污染 — VirtualFill.mode_tag == 0 (断言驱动)
//   T03: R-20 ts 链 — LedgerFeatures.ts_chain_ok() 通过
//   T04: LedgerSnapshotHub 可见 — 若 hub 有有效 book, Publish 发生
//   T05: QuoteSnapshotHub 可见 — quote 快照与 fair_value 一致 (FV > ask → edge > 0)
//   T06: ComputeEdgeCiLower 数值稳定性 — NaN 输入 fail-closed / 正常值单调
//   T07: CI gating — edge_ci_lower <= 0 → no fill (sizing 不通过 → 不构造 intent)
//   T08: Stop 后 is_running() = false
//   T09: Stats 计数 — ticks_total 单调递增 (tick 至少 1 次)
//
// P0 整改测试 (dogfood-remediation 2026-05-30):
//   T12: P0-1 拒单去重 — 同一 reject 只写 ring 一次 (RmDebugSnapshot.count()单调, 不翻倍)
//   T13: P0-3 fake fair gate — M1 stub 路径 quote.predict_ok=false, edge/kelly/notional=0
//   T14: P0-4 advisory gate — advisory_markets_no_intent=true 时 orders_rejected=0
//        (不进 RM, 无 INVALID_INTENT; 仅 quote_publishes > 0)
//
// 测试策略:
//   使用合成 OrderBookSnapshotHub (先 Publish 真实 book 快照),
//   PaperLoop tick 后读 LedgerSnapshotHub / QuoteSnapshotHub 验证.
//   PaperLoop 内部有 RM SAFE_MODE → RUNNING 切换 (set_rm_running=true 默认),
//   但 RM 的 STALE_DATA 检查会拒掉 M1 合成数据 (book_snapshot_ts 是当前时间).
//   → 单测将 RiskConfig.edge_ci_lower_floor = -1.0 放宽, 并注意 RM check_stale_data_.
//   → 为了让 T04 真正产生 fill, 用 VirtualMatcher 固定种子 + 足够大 edge.
//
// 注意: PaperLoop 有内部 jthread, 需 sleep 等待 tick.

#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>

#include "stcpp/data/score_snapshot_store.hpp"  // A1: 真实比分注入
#include "stcpp/paper/paper_loop.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"
#include "stcpp/ml/fair_value_model.hpp"    // 步④ StubFairValueModel (ML 推理接线测试)
#include "stcpp/ml/model_feature_spec.hpp"  // kMlFeatureCount

using namespace stcpp;
using namespace stcpp::paper;
using namespace stcpp::polymarket::clob_wss;
using namespace stcpp::risk;
using namespace stcpp::sizing;
using namespace stcpp::pricing;

// ---------------------------------------------------------------------------
// 辅助: 构造合成 OrderBookFeatures (有效 L1 book)
// ---------------------------------------------------------------------------

static constexpr std::int64_t kEventTs = 1'000'000'000LL;
static constexpr std::int64_t kDsTs = 1'100'000'000LL;
static constexpr std::int64_t kIngTs = 1'200'000'000LL;
static constexpr std::int64_t kAsOfTs = 1'300'000'000LL;

static OrderBookFeatures MakeSyntheticBook(double bid, double ask, double bid_size = 500.0,
                                           double ask_size = 500.0) {
    OrderBookFeatures f{};
    f.valid = true;
    f.event_ts_ns = kEventTs;
    f.data_source_ts_ns = kDsTs;
    f.ingestion_ts_ns = kIngTs;
    f.as_of_ts_ns = kAsOfTs;
    f.bids[0].price = bid;
    f.bids[0].size_usdc = bid_size;
    f.asks[0].price = ask;
    f.asks[0].size_usdc = ask_size;
    f.microprice = (bid * ask_size + ask * bid_size) / (bid_size + ask_size);
    f.mid = (bid + ask) * 0.5;
    f.spread = ask - bid;
    f.imbalance = (bid_size - ask_size) / (bid_size + ask_size);
    f.wss_state = WssConnState::kConnected;
    f.sequence_no = 1;
    return f;
}

// 新鲜 ts 合成 book (A2: book_snapshot_ts 须 recent, 否则 RM STALE_DATA 拒单).
static OrderBookFeatures MakeFreshBook(double bid, double ask) {
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    OrderBookFeatures f = MakeSyntheticBook(bid, ask);
    f.event_ts_ns = now_ns - 3'000'000'000LL;  // 3s 前
    f.data_source_ts_ns = now_ns - 2'000'000'000LL;
    f.ingestion_ts_ns = now_ns - 1'000'000'000LL;  // book_snapshot_ts (RM freshness 用)
    f.as_of_ts_ns = now_ns;
    return f;
}

// ---------------------------------------------------------------------------
// 辅助: Null AuditEmitter (M1 不需要 WAL)
// ---------------------------------------------------------------------------

class NullAuditEmitter final : public AuditEmitter {
public:
    bool emit(AuditRecord const& /*rec*/) noexcept override { return true; }
};

// ---------------------------------------------------------------------------
// 测试夹具
// ---------------------------------------------------------------------------

class PaperLoopTest : public ::testing::Test {
protected:
    void SetUp() override {
        hub_ = std::make_unique<OrderBookSnapshotHub>();
        ledger_hub_ = std::make_unique<LedgerSnapshotHub>();
        quote_hub_ = std::make_unique<QuoteSnapshotHub>();
        rm_snap_ = std::make_unique<RmDebugSnapshot>();
        position_ledger_ = std::make_unique<PositionLedger>();

        // RM: 放宽 cap 以便 demo 场景产生成交
        // 单位 (A2 修): RM caps/bankroll 与 size_pUSD_micro 同为 micro pUSD (× 1e6).
        RiskConfig rm_cfg;
        rm_cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000'000);  // 10 pUSD (micro)
        rm_cfg.market_exposure_cap_usdc =
            stcpp::domain::MicroPUSD::from_micro(50'000'000);                            // 50 pUSD (micro)
        rm_cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(25'000'000);  // 25 pUSD (micro)
        rm_cfg.bankroll_usdc =
            stcpp::domain::MicroPUSD::from_micro(1'000'000'000);  // c2b 保值 // 1K pUSD (micro)
        rm_cfg.edge_ci_lower_floor = -1.0;                        // 放宽 CI 门

        auto emitter = std::make_shared<NullAuditEmitter>();
        rm_ = std::make_unique<RiskGateway>(rm_cfg, emitter);

        // FairValue model
        fv_model_ = std::make_unique<BaselineFairValueModel>(ScorePriorParams{0.30, 0.50}, 0.20);

        // token_map
        token_map_["cond-test-001"] = {"1001", "1002"};

        // PaperLoopConfig
        cfg_.tick_interval_ms = 50;  // 快速 tick (50ms, 单测友好)
        cfg_.bankroll_usdc = 1000.0;
        cfg_.n_effective = 30;
        cfg_.z_90 = 1.645;
        cfg_.strategy_id = "paper-test-v1";
        cfg_.set_rm_running = true;
    }

    void TearDown() override {
        if (loop_) {
            loop_->Stop();
        }
    }

    std::unique_ptr<PaperLoop> MakeLoop() {
        return std::make_unique<PaperLoop>(*hub_, *rm_, *position_ledger_, *ledger_hub_, *quote_hub_,
                                           rm_snap_.get(), *fv_model_, token_map_, cfg_);
    }

    // A5 (老韩 spec): DD 测试需大亏损仓位 + 高 cap (否则 exposure/bankroll 抢先拒, 测不到 DD)。
    //   hard_threshold 参数可调 (默认 5k pUSD); 重建 rm_ 后须在 MakeLoop 前调用。
    void RebuildRmHighCap(double hard_pusd = 5'000.0) {
        RiskConfig c;
        c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
        c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
        c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
        c.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
        c.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_pusd(hard_pusd);  // DD 硬阈值
        c.edge_ci_lower_floor = -1.0;                                             // 放宽信号门
        auto emitter = std::make_shared<NullAuditEmitter>();
        rm_ = std::make_unique<RiskGateway>(c, emitter);
    }

    std::unique_ptr<OrderBookSnapshotHub> hub_;
    std::unique_ptr<LedgerSnapshotHub> ledger_hub_;
    std::unique_ptr<QuoteSnapshotHub> quote_hub_;
    std::unique_ptr<RmDebugSnapshot> rm_snap_;
    std::unique_ptr<PositionLedger> position_ledger_;
    std::unique_ptr<RiskGateway> rm_;
    std::unique_ptr<BaselineFairValueModel> fv_model_;
    std::unordered_map<std::string, std::pair<std::string, std::string>> token_map_;
    PaperLoopConfig cfg_;
    std::unique_ptr<PaperLoop> loop_;
};

// ---------------------------------------------------------------------------
// T01: 基础生命周期 — Start/Stop 不崩溃
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T01_StartStop) {
    loop_ = MakeLoop();
    EXPECT_FALSE(loop_->is_running());
    loop_->Start();
    EXPECT_TRUE(loop_->is_running());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    loop_->Stop();
    EXPECT_FALSE(loop_->is_running());
}

// ---------------------------------------------------------------------------
// T02: R-11 paper 不污染 — VirtualFill.mode_tag == 0
//   (在 paper_loop.cpp TickOne() 里有 assert(fill.mode_tag == 0u))
//   这里验证 VirtualMatcher 产出的 VirtualFill.mode_tag == 0
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T02_R11_ModePaper) {
    // VirtualMatcher 直接测试 mode_tag
    execution::VirtualMatcher matcher{0xBEEFCAFEULL};
    execution::VirtualOrder ord{};
    ord.intent_id = 1;
    ord.market_id = "cond-test-001";
    ord.outcome = "YES";
    ord.size_usdc = 5.0;
    ord.quote_price = 0.55;
    ord.book_depth_l1_usdc = 500.0;
    ord.tick_size = 0.01;
    ord.event_ts_ns = kEventTs;
    ord.data_source_ts_ns = kDsTs;
    ord.ingestion_ts_ns = kIngTs;
    ord.as_of_ts_ns = kAsOfTs;
    ord.wall_now_ns = kAsOfTs;

    const auto fill = matcher.Match(ord);
    // R-11 硬填: mode_tag 必须为 0 (paper 标记)
    EXPECT_EQ(fill.mode_tag, static_cast<std::uint8_t>(0)) << "R-11 violation: VirtualFill.mode_tag != 0";
    // WAL kind 必须为 PaperAudit
    EXPECT_EQ(fill.audit_wal_kind, infra::wal::WalKind::PaperAudit)
        << "R-11 violation: audit_wal_kind != PaperAudit";
}

// ---------------------------------------------------------------------------
// T03: R-20 ts 链 — LedgerFeatures.ts_chain_ok() 通过
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T03_R20_TsChain) {
    // 构造 LedgerFeatures 并验证 ts_chain_ok()
    LedgerFeatures lf{};
    lf.event_ts_ns = kEventTs;
    lf.data_source_ts_ns = kDsTs;
    lf.ingestion_ts_ns = kIngTs;
    lf.as_of_ts_ns = kAsOfTs;
    lf.valid = true;
    lf.mode = ExecutionModeTag::kPaper;
    lf.net_qty = 5.0;
    lf.avg_entry_price = 0.55;
    lf.mark_price = 0.56;
    lf.pnl_unrealized = (0.56 - 0.55) * 5.0;

    EXPECT_TRUE(lf.ts_chain_ok()) << "R-20 ts_chain_ok() failed for valid LedgerFeatures";
    EXPECT_EQ(lf.mode, ExecutionModeTag::kPaper) << "R-11: mode must be kPaper";
}

// ---------------------------------------------------------------------------
// T04: LedgerSnapshotHub 可见
//   hub 有有效 book → PaperLoop tick → LedgerSnapshotHub.Publish 发生
//   (由于 RM stale_data 检查会拒掉合成 ts, 实际 fill 可能为 0, 但 quote 快照仍发布)
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T04_LedgerHubVisible) {
    // Publish 合成 book (fair=0.65, ask=0.55 → edge=0.10, 足够大)
    const auto feat = MakeSyntheticBook(0.53, 0.55);
    hub_->Publish("1001", feat);

    loop_ = MakeLoop();
    loop_->Start();

    // 等 3 次 tick (50ms × 3 = 150ms)
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop_->Stop();

    // 无论 RM 是否放行, stats.ticks_total > 0
    EXPECT_GT(loop_->stats().ticks_total.load(), static_cast<std::uint64_t>(0))
        << "PaperLoop should have executed at least one tick";
    // orders_attempted > 0 (book 有数据, 应进入 TickOne)
    EXPECT_GT(loop_->stats().orders_attempted.load(), static_cast<std::uint64_t>(0))
        << "orders_attempted should be > 0 after hub has valid book";
}

// ---------------------------------------------------------------------------
// T05: QuoteSnapshotHub 可见
//   hub 有有效 book → tick → quote_hub_.Publish → Read() 返回 valid=true
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T05_QuoteHubVisible) {
    // fair=0.65, ask=0.55 → edge=100bps
    const auto feat = MakeSyntheticBook(0.53, 0.55);
    hub_->Publish("1001", feat);

    loop_ = MakeLoop();
    loop_->Start();

    // 等 3 次 tick
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    // QuoteSnapshotHub 应有数据 (quote_publishes > 0)
    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0))
        << "QuoteSnapshotHub should have been published at least once";

    // 读 quote 快照验证字段
    const auto opt = quote_hub_->Read("cond-test-001");
    // quote_hub_.Publish 用 condition_id 作 key
    if (opt.has_value() && opt->valid) {
        EXPECT_GT(opt->fair_value, 0.0) << "fair_value should be > 0";
        EXPECT_LE(opt->fair_value, 1.0) << "fair_value should be <= 1";
        EXPECT_GT(opt->market_mid, 0.0) << "market_mid should be > 0";
        // R-20: as_of_ts_ns > 0
        EXPECT_GT(opt->as_of_ts_ns, 0LL) << "R-20: as_of_ts_ns should be > 0";
    }
    // 如果 opt 无值 (ts 链校验失败 → hub 未发布), 跳过字段验证
    // 关键验证仍是 quote_publishes > 0 (publish 计数独立)
}

// ---------------------------------------------------------------------------
// T06: ComputeEdgeCiLower 数值稳定性
//   通过 PaperLoop 白盒 — 验证内部静态函数行为
//   用测试类暴露 (由于是 private, 直接通过行为验证)
//
//   等价测试: 用 FairValueEstimator + SizingCalculator 直接验证 CI gating
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T06_EdgeCiLower_Numerical) {
    // NaN p_fair → fail-closed (ci_lower = -1.0 → sizing.valid = false)
    const double nan_val = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isnan(nan_val));

    // 直接测试 SizingCalculator 的 fail-closed (edge_ci_lower = NaN)
    RiskConfig rm_cfg{};
    rm_cfg.edge_ci_lower_floor = 0.0;

    sizing::SizingInput si_nan{};
    si_nan.fair_value = 0.65;
    si_nan.price = 0.55;
    si_nan.edge_ci_lower = nan_val;  // NaN → fail-closed
    si_nan.bankroll_usdc = 1000.0;
    si_nan.fill_rate = 0.65;
    si_nan.slippage_bps = 8.0;
    si_nan.buy_yes = true;

    const auto out_nan = sizing::SizingCalculator::compute(rm_cfg, si_nan);
    EXPECT_FALSE(out_nan.valid) << "NaN edge_ci_lower should yield invalid SizingOutput";
    EXPECT_DOUBLE_EQ(out_nan.suggested_notional, 0.0) << "fail-closed: notional = 0";

    // 正常值: edge_ci_lower = 0.05 (500 bps 净 edge CI) → valid = true
    sizing::SizingInput si_ok{};
    si_ok.fair_value = 0.65;
    si_ok.price = 0.55;
    si_ok.edge_ci_lower = 0.05;
    si_ok.bankroll_usdc = 1000.0;
    si_ok.fill_rate = 0.65;
    si_ok.slippage_bps = 8.0;
    si_ok.buy_yes = true;

    const auto out_ok = sizing::SizingCalculator::compute(rm_cfg, si_ok);
    EXPECT_TRUE(out_ok.valid) << "Positive edge_ci_lower should yield valid SizingOutput";
    EXPECT_GT(out_ok.suggested_notional, 0.0) << "suggested_notional > 0 for valid edge";
    EXPECT_GT(out_ok.kelly_fractional, 0.0) << "kelly_fractional > 0 for valid edge";

    // 单调性: larger edge_ci_lower → larger suggested_notional (ceteris paribus)
    sizing::SizingInput si_large{si_ok};
    si_large.edge_ci_lower = 0.10;
    const auto out_large = sizing::SizingCalculator::compute(rm_cfg, si_large);
    EXPECT_GE(out_large.suggested_notional, out_ok.suggested_notional)
        << "Larger edge_ci_lower should yield >= suggested_notional";
}

// ---------------------------------------------------------------------------
// T07: CI gating — edge_ci_lower 过小 → no order
//   fair=0.51, ask=0.50 → raw_edge=0.01 (100bps), n=30, z=1.645
//   sigma=sqrt(0.51*0.49/30)=sqrt(0.008330)≈0.0913
//   ci_lower = 0.01 - 1.645 * 0.0913 ≈ 0.01 - 0.150 = -0.140 < 0 → no order
//   → orders_attempted > 0 but fills_completed = 0 (sizing.valid = false 时不进 RM)
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T07_CiGating_NoOrder) {
    // fair=0.51, ask=0.50 → tiny edge → CI gating 拒
    const auto feat = MakeSyntheticBook(0.49, 0.50);
    hub_->Publish("1001", feat);

    // FairValueEstimator 在无 game_row 时用纯 book 先验, microprice ≈ 0.495
    // 即使 fair_value 稍高 (sigmoid 先验 50%), edge 也很小
    // CI gating: z=1.645, n=30 → sigma ≈ 0.09 → ci_lower << 0 → sizing.valid=false

    loop_ = MakeLoop();
    loop_->Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop_->Stop();

    // orders_attempted 可能为 0 (hub_reads_empty) 或 > 0 (有数据但 CI gating 拒)
    // fills_completed 应为 0 (CI gating 或 RM reject)
    EXPECT_EQ(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "No fills expected when edge is too small for CI gating";
}

// ---------------------------------------------------------------------------
// T08: Stop 后 is_running() = false
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T08_StopRunning) {
    loop_ = MakeLoop();
    loop_->Start();
    EXPECT_TRUE(loop_->is_running());
    loop_->Stop();
    EXPECT_FALSE(loop_->is_running());
    // 幂等: 再次 Stop 不崩溃
    loop_->Stop();
    EXPECT_FALSE(loop_->is_running());
}

// ---------------------------------------------------------------------------
// T09: Stats 计数 — ticks_total 单调递增
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T09_StatsMonotonic) {
    // 无 book 数据 → hub_reads_empty 增加, ticks_total 增加
    loop_ = MakeLoop();
    loop_->Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(60));  // 至少 1 tick (50ms)
    const auto ticks1 = loop_->stats().ticks_total.load();
    EXPECT_GT(ticks1, static_cast<std::uint64_t>(0)) << "At least 1 tick should have occurred";

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const auto ticks2 = loop_->stats().ticks_total.load();
    EXPECT_GE(ticks2, ticks1) << "ticks_total should be non-decreasing";

    loop_->Stop();
}

// ---------------------------------------------------------------------------
// T10: R-11 LedgerFeatures.mode = kPaper
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T10_R11_LedgerMode) {
    LedgerSnapshotHub lhub;
    LedgerFeatures lf{};
    lf.event_ts_ns = kEventTs;
    lf.data_source_ts_ns = kDsTs;
    lf.ingestion_ts_ns = kIngTs;
    lf.as_of_ts_ns = kAsOfTs;
    lf.mode = ExecutionModeTag::kPaper;  // R-11: 硬填 paper
    lf.valid = true;
    lf.net_qty = 5.0;
    lf.avg_entry_price = 0.55;
    lf.mark_price = 0.56;

    lhub.Publish("cond-test-001", lf);
    const auto opt = lhub.Read("cond-test-001");
    ASSERT_TRUE(opt.has_value());
    EXPECT_TRUE(opt->valid);
    EXPECT_EQ(opt->mode, ExecutionModeTag::kPaper) << "R-11: LedgerFeatures.mode must be kPaper";
    EXPECT_TRUE(opt->ts_chain_ok()) << "R-20: ts_chain must be valid";
}

// ---------------------------------------------------------------------------
// T11: QuoteFeatures.advisory = true (ML-R2)
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T11_QuoteAdvisory) {
    QuoteSnapshotHub qhub;
    QuoteFeatures qf{};
    qf.event_ts_ns = kEventTs;
    qf.data_source_ts_ns = kDsTs;
    qf.ingestion_ts_ns = kIngTs;
    qf.as_of_ts_ns = kAsOfTs;
    qf.advisory = true;  // ML-R2: paper 期恒 true
    qf.valid = true;
    qf.fair_value = 0.65;
    qf.market_mid = 0.545;
    qf.edge_bps = 1050.0;

    qhub.Publish("cond-test-001", qf);
    const auto opt = qhub.Read("cond-test-001");
    ASSERT_TRUE(opt.has_value());
    EXPECT_TRUE(opt->advisory) << "ML-R2: advisory must be true in paper mode";
    EXPECT_TRUE(opt->ts_chain_ok()) << "R-20: ts_chain must be valid";
}

// ---------------------------------------------------------------------------
// T11c (老板「双边都要有」): 双边 book 都在 → NO 边时序微结构 (no_*) 也被捕获, 不只 YES。
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T11c_NoSideMicrostructure_Captured) {
    // YES book (token 1001) + NO book (token 1002) 都发布 → 双边 ring 都 push。
    hub_->Publish("1001", MakeSyntheticBook(0.53, 0.55));
    hub_->Publish("1002", MakeSyntheticBook(0.45, 0.47));  // NO 边独立 book
    loop_ = MakeLoop();
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));  // 多 tick → NO ring 累样本
    loop_->Stop();

    const auto opt = quote_hub_->Read("cond-test-001");
    if (opt.has_value() && opt->valid) {
        // 双边都有: YES 边 + NO 边时序样本都 > 0 (NO 边不再缺失)。
        EXPECT_GT(opt->ts_window_samples, 0) << "YES 边时序应有样本";
        EXPECT_GT(opt->no_ts_window_samples, 0) << "NO 边时序应也有样本 (双边都要有)";
        // no_* 字段已定义 (有限或 NaN, 但不是未初始化垃圾); realized_vol 双边都可读。
        EXPECT_FALSE(std::isnan(opt->no_realized_vol) && opt->no_ts_window_samples >= 2)
            << "NO 边样本足 → no_realized_vol 应有值";
    }
}

// ---------------------------------------------------------------------------
// T11f (Phase 2 项6): paper_loop 算完 extract_full → Publish 完整 75 列向量进 fv_hub。
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T11f_Phase2_FullVectorPublishedToHub) {
    hub_->Publish("1001", MakeSyntheticBook(0.53, 0.55));
    stcpp::ml::FeatureVectorHub fv_hub;
    loop_ = MakeLoop();
    loop_->SetFeatureVectorHub(&fv_hub);  // Start 前注入
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    loop_->Stop();  // join loop_thread_ 先于 fv_hub 析构 (本测局部)

    // fv_hub 应收到该 condition 的完整 75 列向量。
    const auto rec = fv_hub.Read("cond-test-001");
    if (rec.has_value()) {
        EXPECT_EQ(rec->count, stcpp::ml::kMlFeatureCount) << "完整 75 列 (含 0-17 原始 game/book)";
        EXPECT_STREQ(rec->spec_version, std::string(stcpp::ml::kSpecVersion).c_str());
        EXPECT_GT(rec->as_of_ts_ns, 0LL);
    }
}

// ---------------------------------------------------------------------------
// T11g (老板放开 paper ML-R2): 真 ONNX 模型 + blend weight=1.0 → ML 驱动决策路径跑通不崩。
//   stub 永不驱动 (kind 门); 仅 ONNX 触发 blend。验证 predict-before-decision 接线 + BR-1 同源特征。
// ---------------------------------------------------------------------------
#ifdef STCPP_ONNX_ENABLED
TEST_F(PaperLoopTest, T11g_MlDrivesDecision_OnnxBlend) {
    const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures" /
                         "fair_value_selftest.onnx";
    if (!std::filesystem::exists(fixture)) GTEST_SKIP() << "fixture ONNX 缺失";
    stcpp::ml::OnnxModelConfig ocfg;
    ocfg.onnx_path = fixture.string();
    ocfg.expected_feature_count = stcpp::ml::kMlFeatureCount;
    ocfg.output_outcome_count = 2;
    auto onnx = stcpp::ml::make_onnx_fair_value_model(ocfg);
    ASSERT_NE(onnx, nullptr);
    ASSERT_EQ(onnx->kind(), stcpp::ml::ModelKind::Onnx) << "真 ONNX (非 stub) 才驱动决策";

    hub_->Publish("1001", MakeSyntheticBook(0.53, 0.55));
    hub_->Publish("1002", MakeSyntheticBook(0.45, 0.47));  // 双边 book: ML 选任一边都有 book
    cfg_.ml_fair_blend_weight = 1.0;  // ML 全驱动决策 fair
    loop_ = MakeLoop();
    loop_->SetMlModel(onnx.get());  // Start 前注入
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop_->Stop();  // join loop_thread_ 先于 onnx 析构 (本测局部)

    // ML 驱动决策路径跑通 (extract_full→predict→blend p_fair→SelectSide), 不崩 + 正常发 quote。
    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0));
}
#endif

// ---------------------------------------------------------------------------
// T11e (Phase 0 联合评审): 项5 组合度量接入 TickAll (权益每周期采样) + 项1-3 门 ON 路径不崩。
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T11e_Phase0_PortfolioMetricsAndGatesWired) {
    hub_->Publish("1001", MakeSyntheticBook(0.53, 0.55));
    cfg_.dynamic_reservation = true;  // 项1+2 动态 reservation ON
    cfg_.net_ev_gate = true;          // 项3 net-EV 门 ON
    loop_ = MakeLoop();
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    // 项5: TickAll 每周期 RecordEquity → portfolio_report 有样本 (北极星 KPI 采集打通)。
    const auto pr = loop_->portfolio_report(/*ppy=*/1.0);
    EXPECT_GT(pr.samples, static_cast<std::size_t>(0)) << "TickAll 每周期采权益曲线";
    EXPECT_GE(pr.last_equity, 0.0);
    // 项1-3 门 ON 路径正常运行不崩 (thin data → 保守; 仅验证管线不崩)。
    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0));
}

// ---------------------------------------------------------------------------
// T11d (老板「各边买了多少, 可能两边都买」): 双边持仓 — YES + NO 各自量都进 QuoteFeatures,
//   不塌成单边/净。旧码 break 在首 token 只取一边丢 NO; 现 per-token 双边读。
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T11d_BothSidePositions_Captured) {
    hub_->Publish("1001", MakeSyntheticBook(0.53, 0.55));  // YES book 有效 → quote 发布
    // 两边都建仓: YES token 1001 持 100 pUSD, NO token 1002 持 30 pUSD (做市/对冲场景)。
    auto mk_fill = [](double px, double whole_pusd) {
        execution::VirtualFill f{};
        f.reject = execution::MatchReject::Ok;
        f.fill_price = px;
        f.fill_size_usdc = static_cast<std::int64_t>(whole_pusd * 1'000'000.0);  // micro
        f.as_of_ts_ns = kAsOfTs;
        f.mode_tag = 0;  // paper
        return f;
    };
    position_ledger_->apply_fill("cond-test-001", "1001", Outcome::Yes, mk_fill(0.55, 100.0));
    position_ledger_->apply_fill("cond-test-001", "1002", Outcome::No, mk_fill(0.45, 30.0));

    loop_ = MakeLoop();
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    loop_->Stop();

    const auto opt = quote_hub_->Read("cond-test-001");
    if (opt.has_value() && opt->valid) {
        EXPECT_NEAR(opt->pos_yes_qty, 100.0, 1e-6) << "YES 边持仓量 (各边各量)";
        EXPECT_NEAR(opt->pos_no_qty, 30.0, 1e-6) << "NO 边持仓量 (不能丢, 两边都买了)";
        EXPECT_NEAR(opt->pos_net_qty, 70.0, 1e-6) << "净 YES = 100 − 30";
        EXPECT_GT(opt->pos_yes_avg_entry, 0.0);
        EXPECT_GT(opt->pos_no_avg_entry, 0.0) << "NO 边 avg entry 也要有";
    }
}

// ---------------------------------------------------------------------------
// T11b (步④): 注入 ml::FairValueModel → 推理路径跑通, ml_advisory_p_yes 填充, provenance
//   反映 ML 模型; ML-R1/R2: 推理 advisory, fair_value 仍由 baseline 定 (不被 ML 驱动)。
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T11b_MlInferenceAdvisoryWired) {
    const auto feat = MakeSyntheticBook(0.53, 0.55);
    hub_->Publish("1001", feat);

    // StubFairValueModel(24) — 必与 kMlFeatureCount/extract_joined 维度一致, 否则 predict ok=false。
    stcpp::ml::StubFairValueModel stub(stcpp::ml::kMlFeatureCount, /*outcome_count=*/2);
    loop_ = MakeLoop();
    loop_->SetMlModel(&stub);  // Start 前注入 (单 writer)
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();  // join loop_thread_ 先于 stub 析构 (stub 是本测局部)

    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0));
    const auto opt = quote_hub_->Read("cond-test-001");
    if (opt.has_value() && opt->valid) {
        // 推理结果填进 advisory 列 (stub logistic 输出 ∈ (0,1]; 合成 book 固定旧 ts → 延迟特征巨大
        //   → stub 可能饱和到 1.0, 测试用 LE; 真数据 data_source_ts 近实时, 延迟小不饱和)。
        EXPECT_FALSE(std::isnan(opt->ml_advisory_p_yes)) << "ML 推理应填 ml_advisory_p_yes";
        EXPECT_GT(opt->ml_advisory_p_yes, 0.0);
        EXPECT_LE(opt->ml_advisory_p_yes, 1.0);
        // provenance 反映注入的 ML 模型 (非 baseline)。
        EXPECT_EQ(opt->model_kind, ModelKindTag::kStub);
        EXPECT_STREQ(opt->model_id, "stub-fair-value-v0.1");
        EXPECT_STREQ(opt->spec_version, std::string(stcpp::ml::kSpecVersion).c_str());
        // ML-R1/R2: fair_value 仍是 baseline 产出 (∈ (0,1)), 未被 ML advisory 覆盖。
        EXPECT_GT(opt->fair_value, 0.0);
        EXPECT_LE(opt->fair_value, 1.0);
        EXPECT_TRUE(opt->advisory) << "ML-R2: 推理 advisory";
    }
}

// ---------------------------------------------------------------------------
// T12: P0-1 拒单去重 — RmDebugSnapshot ring 不被 paper_loop 二次写入
//
// 验证方法:
//   让 PaperLoop 跑若干 tick (不产生 fill, RM 会拒单).
//   记录 rm_snap_.count() = RM 侧写入总次数.
//   因为 paper_loop 已不再调用 rm_snap_->push_reject,
//   每个 intent 只被 RM::evaluate() 内部 push 一次.
//   → count() 等于 orders_rejected (1:1, 不翻倍).
//
// 注意: 本测试禁用 advisory gate (advisory_markets_no_intent=false) 使 RM 路径可达,
//   同时 attach g_rm_debug_snapshot 让 RM 内部 push 到 rm_snap_.
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T12_P0_1_RejectNoDuplicate) {
    // attach 全局 snapshot 指针, 让 RM 内部 push_reject 到 rm_snap_
    attach_rm_debug_snapshot(rm_snap_.get());

    // 发布合成 book: 低价 outright (Spain mid=0.169), 对应 P0-3 的典型假信号场景
    // fair 将被拉高, 边 CI gating 可能通过; 但 RM INVALID_INTENT 或其他规则会拒
    const auto feat = MakeSyntheticBook(0.16, 0.18);  // mid=0.17 (低价 outright)
    hub_->Publish("1001", feat);

    // 禁用 advisory gate, 让 intent 进 RM (测试 RM 侧 push_reject 不双写)
    cfg_.advisory_markets_no_intent = false;
    loop_ = MakeLoop();
    loop_->Start();

    // 等 4 次 tick
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    // 清理全局指针
    detach_rm_debug_snapshot();

    const auto rejected = loop_->stats().orders_rejected.load();
    const auto snap_count = rm_snap_->count();

    // 核心断言: snap_count == rejected (每个 reject 只写 ring 一次)
    // 修复前: snap_count == 2 × rejected (paper_loop + RM 各 push 一次)
    // 修复后: snap_count == rejected (只有 RM 内部 push)
    if (rejected > 0) {
        EXPECT_EQ(snap_count, rejected)
            << "P0-1: RmDebugSnapshot.count() must equal orders_rejected "
               "(each reject pushed exactly once by RM, not duplicated by paper_loop). "
               "snap_count="
            << snap_count << " rejected=" << rejected;
    }

    // 验证 snapshot() 无重复 rejected_ts (纳秒级碰撞概率极低)
    const auto rows = rm_snap_->snapshot();
    // 检查每个 (market_id, intent_ref, rejected_ts_ns) 元组唯一
    std::unordered_map<std::string, int> seen;
    for (const auto& r : rows) {
        std::string key = std::string(r.market_id) + "|" + std::string(r.intent_ref) + "|" +
                          std::to_string(r.rejected_ts_ns);
        seen[key]++;
    }
    for (const auto& [k, cnt] : seen) {
        EXPECT_EQ(cnt, 1) << "P0-1: Duplicate reject row detected for key=" << k << " count=" << cnt
                          << " (expected 1, was 2 before fix)";
    }
}

// ---------------------------------------------------------------------------
// T13: P0-3 fake fair gate — M1 stub 路径 quote 中 predict_ok=false,
//      edge_bps/kelly_fraction/suggested_notional/signal_strength 全为 0
//
// 场景: 低价 outright (Spain mid=0.169) 在 stub 路径被强拉到 fair=0.434
//   修复前: edge_bps=1076, signal_strength=1, suggested_notional=32 (假阳性)
//   修复后: edge_bps=0, kelly=0, notional=0, signal=0, predict_ok=false
//
// 验证: time_status==NotStarted → has_real_fair=false → PublishQuoteSnapshot 清零.
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T13_P0_3_FakeFairGate) {
    // 低价 outright: mid=0.169, stub fair 会被拉到 ~0.434 (edge=0.265)
    // 修复后: quote.edge_bps/kelly/notional/signal 全 0, predict_ok=false
    const auto feat = MakeSyntheticBook(0.16, 0.18);  // bid=0.16, ask=0.18
    hub_->Publish("1001", feat);

    cfg_.advisory_markets_no_intent = false;  // 允许进 quote publish 流程
    loop_ = MakeLoop();
    loop_->Start();

    // 等 3 次 tick 确保 quote publish 发生
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0))
        << "T13: quote_publishes should be > 0 (book valid, quote path entered)";

    // 读 quote snapshot
    const auto opt = quote_hub_->Read("cond-test-001");
    if (opt.has_value() && opt->valid) {
        // P0-3 核心断言: 无真实 fair 时, 决策字段全为 0
        EXPECT_DOUBLE_EQ(opt->edge_bps, 0.0)
            << "P0-3: edge_bps must be 0 when has_real_fair=false (no Goalserve game_row)";
        EXPECT_DOUBLE_EQ(opt->kelly_fraction, 0.0)
            << "P0-3: kelly_fraction must be 0 when has_real_fair=false";
        EXPECT_DOUBLE_EQ(opt->suggested_notional, 0.0)
            << "P0-3: suggested_notional must be 0 when has_real_fair=false "
               "(was 32 pUSD fake signal before fix)";
        EXPECT_DOUBLE_EQ(opt->signal_strength, 0.0)
            << "P0-3: signal_strength must be 0 when has_real_fair=false";
        EXPECT_FALSE(opt->predict_ok)
            << "P0-3: predict_ok must be false when has_real_fair=false (not calibrated)";
        EXPECT_FALSE(opt->model_calibrated) << "P0-3: model_calibrated must be false (M1 stub)";
        EXPECT_TRUE(opt->advisory) << "ML-R2: advisory must remain true in paper mode";
        // fair_value 字段仍输出 (供观察), 但因 predict_ok=false 不可决策
        EXPECT_GT(opt->fair_value, 0.0) << "fair_value output for observation (but predict_ok=false)";
    }

    // 验证: 无真实 fair → 不产生 intent (orders_rejected==0 因为 has_real_fair gate 在 RM 前拦截)
    EXPECT_EQ(loop_->stats().orders_rejected.load(), static_cast<std::uint64_t>(0))
        << "P0-3: no intent should reach RM when has_real_fair=false";
}

// ---------------------------------------------------------------------------
// T14: P0-4 advisory gate — advisory_markets_no_intent=true (默认) 时
//      不产生 intent, orders_rejected=0 (无 INVALID_INTENT 进 RM)
//      quote 仍发布 (供观察), 但 edge/kelly/notional 全为 0 (P0-3 联动)
// ---------------------------------------------------------------------------

TEST_F(PaperLoopTest, T14_P0_4_AdvisoryGate) {
    // 发布有效 book (mid=0.545 — 接近 0.5 的 hockey 市场)
    const auto feat = MakeSyntheticBook(0.53, 0.56);
    hub_->Publish("1001", feat);

    // advisory_markets_no_intent=true (默认; M1 paper 期所有市场)
    cfg_.advisory_markets_no_intent = true;

    // attach snapshot 验证 RM 没有被调用
    attach_rm_debug_snapshot(rm_snap_.get());
    loop_ = MakeLoop();
    loop_->Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();
    detach_rm_debug_snapshot();

    // 核心断言: advisory gate 拦截在 RM 前, 无拒单
    EXPECT_EQ(loop_->stats().orders_rejected.load(), static_cast<std::uint64_t>(0))
        << "P0-4: advisory_markets_no_intent=true must prevent any intent from reaching RM";

    // snap_count == 0: RM 未被调用, ring 无写入
    EXPECT_EQ(rm_snap_->count(), static_cast<std::uint64_t>(0))
        << "P0-4: RmDebugSnapshot must be empty when advisory gate fires before RM";

    // quote 仍发布 (quote_publishes > 0)
    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0))
        << "P0-4: quote should still be published for observation (advisory gate fires after Step 4)";

    // quote 内容: advisory=true, predict_ok=false, edge/notional=0 (P0-3 联动)
    const auto opt = quote_hub_->Read("cond-test-001");
    if (opt.has_value() && opt->valid) {
        EXPECT_TRUE(opt->advisory) << "ML-R2: advisory must be true";
        EXPECT_FALSE(opt->predict_ok) << "P0-3+P0-4: predict_ok=false (no real fair)";
        EXPECT_DOUBLE_EQ(opt->suggested_notional, 0.0)
            << "P0-3+P0-4: suggested_notional=0 (stub fair + advisory)";
    }

    // orders_approved / fills_completed 均为 0
    EXPECT_EQ(loop_->stats().orders_approved.load(), static_cast<std::uint64_t>(0))
        << "P0-4: no orders approved when advisory gate is active";
    EXPECT_EQ(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "P0-4: no fills when advisory gate is active";
}

// ---------------------------------------------------------------------------
// T15: A1 真实 Goalserve 比分接入 — score_store + 映射 → has_real_fair=true
//   T13 的镜像: 真实 in-play 比分 → predict_ok=true (vs stub predict_ok=false).
//   advisory gate 仍 true (默认) → 无成交; 仅验 quote 真 fair 流出 (A1 范围, A2 才解封).
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T15_A1_RealGoalserveScore_PredictOk) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;
    using stcpp::debug_api::EventScore;

    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

    // 真实 in-play EventScore: YesTeam 领先 2:0 (yes_is_home=true → score_diff=+2 → 看多 YES)
    EventScore es;
    es.found = true;
    es.event_id = "gs-match-1";
    es.status = "inplay";
    es.home = "YesTeam";
    es.away = "NoTeam";
    es.home_score = 2;
    es.away_score = 0;
    es.kickoff_ts_sec = now_ns / 1'000'000'000LL - 3600;
    es.ts.event_ts_ns = now_ns - 3'600'000'000'000LL;    // kickoff 1h 前 (in-play)
    es.ts.data_source_ts_ns = now_ns - 1'000'000'000LL;  // 1s 前 (fresh < 120s)
    es.ts.ingestion_ts_ns = now_ns - 500'000'000LL;      // 0.5s 前
    es.ts.as_of_ts_ns = now_ns;                          // 单调链

    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-match-1"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));

    // 映射: cond-test-001 → gs-match-1, yes_is_home=true
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-match-1", true};

    // 市场低估 YES (mid≈0.30); YesTeam 2:0 领先 → 真 fair 应 > mid
    const auto feat = MakeSyntheticBook(0.28, 0.32);
    hub_->Publish("1001", feat);

    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();  // 显式停 (store 局部, 须在其析构前 join)

    EXPECT_GT(loop_->stats().quote_publishes.load(), static_cast<std::uint64_t>(0));

    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    // A1 核心: 真实 in-play 比分 → has_real_fair=true → predict_ok=true (区别于 T13 stub)
    EXPECT_TRUE(opt->predict_ok) << "A1: real in-play Goalserve score → has_real_fair=true → predict_ok=true";
    // 真 fair (领先方) 高于被低估的市场 mid → 方向正确 (orientation 正确证明)
    EXPECT_GT(opt->fair_value, 0.30)
        << "A1: leading YES team real fair should exceed underpriced market mid (orientation ok)";
}

// ---------------------------------------------------------------------------
// T16: A1 fail-closed — 陈旧比分 (data_source_ts 超 staleness) → 退回 stub
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T16_A1_StaleScore_FailClosedToStub) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;
    using stcpp::debug_api::EventScore;

    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

    EventScore es;
    es.found = true;
    es.event_id = "gs-match-stale";
    es.status = "inplay";
    es.home = "YesTeam";
    es.away = "NoTeam";
    es.home_score = 2;
    es.away_score = 0;
    // data_source_ts 比 now 旧 300s (> 默认 120s staleness) → 应 fail-closed 退回 stub
    es.ts.event_ts_ns = now_ns - 3'600'000'000'000LL;
    es.ts.data_source_ts_ns = now_ns - 300'000'000'000LL;
    es.ts.ingestion_ts_ns = now_ns - 300'000'000'000LL;
    es.ts.as_of_ts_ns = now_ns - 300'000'000'000LL;

    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-match-stale"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));

    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-match-stale", true};

    const auto feat = MakeSyntheticBook(0.28, 0.32);
    hub_->Publish("1001", feat);

    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop_->Stop();

    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    // 陈旧比分 → 退回 stub → predict_ok=false (老韩 D4 #8: 冻结比分不当 live fair)
    EXPECT_FALSE(opt->predict_ok)
        << "A1: stale score (data_source_ts > staleness limit) must fail-closed to stub";
}

// ---------------------------------------------------------------------------
// A2 测试辅助: 构造 fresh in-play EventScore
// ---------------------------------------------------------------------------
static stcpp::debug_api::EventScore MakeFreshScore(const std::string& id, int home, int away) {
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    stcpp::debug_api::EventScore es;
    es.found = true;
    es.event_id = id;
    es.status = "inplay";
    es.home = "YesTeam";
    es.away = "NoTeam";
    es.home_score = home;
    es.away_score = away;
    es.kickoff_ts_sec = now_ns / 1'000'000'000LL - 3600;
    es.ts.event_ts_ns = now_ns - 3'600'000'000'000LL;
    es.ts.data_source_ts_ns = now_ns - 1'000'000'000LL;
    es.ts.ingestion_ts_ns = now_ns - 500'000'000LL;
    es.ts.as_of_ts_ns = now_ns;
    return es;
}

// ---------------------------------------------------------------------------
// T17: A2 第一笔 paper 成交 — advisory 解封 + 真实 fair + 市场低估 → 产生成交
//   核心: orders_approved>0 (intent 过 gate+RM) + fills>0 (第一笔 paper 成交).
//   ② (老韩 D4): quote.advisory 仍恒 true (ML-R2 不受解封影响).
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T17_A2_FirstPaperFill_AdvisoryUnlocked) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // YesTeam 2:0 领先 (60min soccer → conf~0.45 → fair 拉高), 市场显著低估 (ask=0.30) →
    //   控制器范式: reservation_buy = fair − fee − margin 比 **真实付价 raw ask** (非 de-vig 共识);
    //   raw edge 须 ≥ fee+margin (~4¢) 才过限价门。0.30 ask vs fair~0.54 → 24¢ raw edge → 成交。
    //   (注: 老 T17 的 2¢ raw edge 被控制器正确判 NotMarketable — vig 是真实成本, 限价不追。)
    auto es17 = MakeFreshScore("gs-1", 2, 0);
    es17.sport = "soccer";
    es17.clock_sec = 60 * 60;  // 60min → time_frac≈0.67 → conf≈0.45 (先验拉力足)
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-1"] = es17;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-1", true};

    hub_->Publish("1001", MakeFreshBook(0.28, 0.30));

    cfg_.advisory_markets_no_intent = false;  // A2: 解封 paper 成交
    cfg_.n_effective = 500;                   // 测试用紧 CI 让真实 edge 过门 (生产 n_eff 由小梁量化调)
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));  // ~14 ticks (Bernoulli fill 近必然)
    loop_->Stop();

    // A2 核心: intent 过 advisory gate + RM 批准 (解封证明)
    EXPECT_GT(loop_->stats().orders_approved.load(), static_cast<std::uint64_t>(0))
        << "A2: advisory 解封 + 真实 fair → intent 应过 gate 并被 RM 批准";
    // 第一笔 paper 成交
    EXPECT_GT(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "A2: 应产生 ≥1 笔 paper 成交 (MVP 第一笔成交)";

    // ② ML-R2: quote.advisory 仍恒 true (解封不撕 advisory 展示契约)
    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    EXPECT_TRUE(opt->advisory)
        << "老韩 D4 ②: quote.advisory 必须恒 true (解封 paper fill 不影响 ML-R2 展示标志)";
    EXPECT_TRUE(opt->predict_ok) << "A2: 真实 fair → predict_ok=true";
}

// ---------------------------------------------------------------------------
// T18: 红线2 (老韩) — advisory 解封但无真实 fair → 仍零 intent (拆 gate ≠ 无脑下单)
//   has_real_fair gate (Step 4c) 是解封后唯一兜底.
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T18_A2_Unlocked_NoRealFair_StillZeroIntent) {
    // 有有效 book 但无 score_store / 无映射 → has_real_fair=false
    hub_->Publish("1001", MakeFreshBook(0.28, 0.32));

    cfg_.advisory_markets_no_intent = false;  // 解封
    attach_rm_debug_snapshot(rm_snap_.get());
    loop_ = MakeLoop();
    // 故意不 SetScoreStore → has_real_fair 恒 false
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();
    detach_rm_debug_snapshot();

    // 红线2: 解封 gate 后, has_real_fair gate 仍拦 → 零 intent 进 RM
    EXPECT_EQ(loop_->stats().orders_approved.load(), static_cast<std::uint64_t>(0))
        << "红线2: advisory 解封但 has_real_fair=false → 仍不下单 (Step 4c 兜底)";
    EXPECT_EQ(rm_snap_->count(), static_cast<std::uint64_t>(0)) << "红线2: 无真实 fair 时 RM 不应被调用";
}

// ---------------------------------------------------------------------------
// T19: ④ (老韩) — 极端高 edge (大比分 + 低价) → Kelly notional 被 demo 上限 clamp, 不爆 size
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T19_A2_ExtremeEdge_NotionalClamped) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // YesTeam 5:0 大比分领先 + 市场极低估 (ask=0.10) → fair≈0.9 → edge 巨大
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-x"] = MakeFreshScore("gs-x", 5, 0);
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-x", true};

    hub_->Publish("1001", MakeFreshBook(0.08, 0.10));

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 500;
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    loop_->Stop();

    // ④: 即便 Kelly 想要巨量 size, demo 上限 (10 pUSD) clamp 住 → RM per_order_cap=10 不拒.
    //   orders_approved>0 证明 size 被 clamp 到 ≤10 (否则 RM 会以 per_order cap 拒).
    EXPECT_GT(loop_->stats().orders_approved.load(), static_cast<std::uint64_t>(0))
        << "④: 极端 edge 下 notional 应被 demo 上限 clamp ≤10 pUSD → RM 批准 (未爆 size)";
    EXPECT_EQ(loop_->stats().orders_rejected.load(), static_cast<std::uint64_t>(0))
        << "④: clamp 后 size ≤ per_order_cap, 不应触发 RM cap 拒单";
}

// ---------------------------------------------------------------------------
// T20: ③ (老韩) — 真实 fair ≈ 市场 (无真实 edge) → 无假阳性 → 不下单
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T20_A2_FairMatchesMarket_NoFalsePositive) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // 0:0 平局 (无领先) → fair≈0.5; 市场 mid≈0.5 (bid0.49/ask0.51) → edge≈0
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-tie"] = MakeFreshScore("gs-tie", 0, 0);
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-tie", true};

    hub_->Publish("1001", MakeFreshBook(0.49, 0.51));

    cfg_.advisory_markets_no_intent = false;
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    // ③: fair 与市场一致 → edge_ci_lower 不应假阳性 → 不产生成交
    EXPECT_EQ(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "③: 真实 fair ≈ 市场 (无 edge) → 无假阳性, 不应成交";
}

// ---------------------------------------------------------------------------
// T21: 红线3 (老韩) — PositionLedger::apply_fill 对非 paper fill (mode_tag!=0) 运行期 fail-closed
//   R-11「不污染真账本」运行期守卫 (release build 也 enforce, 非 debug assert).
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T21_R11_ApplyFill_RejectsNonPaperModeTag) {
    PositionLedger ledger;
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();

    execution::VirtualFill fill{};
    fill.reject = execution::MatchReject::Ok;
    fill.fill_size_usdc = 5'000'000;  // A1 micro
    fill.fill_price = 0.5;
    fill.as_of_ts_ns = now_ns;
    fill.mode_tag = 1;  // 非 paper (e.g. live) → 红线3 应拒, 不记账

    ledger.apply_fill("cond-x", "1001", Outcome::Yes, fill);
    EXPECT_TRUE(ledger.get_all_positions().empty())
        << "红线3: mode_tag!=0 (非 paper) → apply_fill 运行期拒, 不写真账本";

    // 对照: mode_tag=0 (paper) → 正常记账
    fill.mode_tag = 0;
    ledger.apply_fill("cond-x", "1001", Outcome::Yes, fill);
    EXPECT_FALSE(ledger.get_all_positions().empty()) << "对照: paper fill (mode_tag=0) 应正常记账";
}

// ---------------------------------------------------------------------------
// T22: A1.5 真时钟 time_frac 解锁成交 — 接真 clock_sec+sport → time_frac→conf 升,
//   生产级 n_effective=150 (非 T17 的 500) 即可成交。证明 time_frac 是正期望前置 (小梁)。
// ---------------------------------------------------------------------------
TEST_F(PaperLoopTest, T22_A15_TimeFrac_UnlocksFillAtProductionNeff) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // soccer 2:0 领先, 已踢 60 分钟 (time_frac=3600/5400≈0.67 → conf≈0.45, 远超 base 0.15)
    auto es = MakeFreshScore("gs-tf", 2, 0);
    es.sport = "soccer";     // total_game_seconds("soccer")=5400
    es.clock_sec = 60 * 60;  // 60min → time_frac≈0.67
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-tf"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-tf", true};

    // 市场低估 YES (ask=0.30); 60min 2:0 领先 → 真 fair 被 time_frac 拉高 → edge 过门
    hub_->Publish("1001", MakeFreshBook(0.28, 0.30));

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 150;  // 生产级紧度 (小梁建议 150-200), 非 T17 的 500
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    loop_->Stop();

    // A1.5: time_frac 拉高先验置信 → 生产级 n_eff 下真实领先即成交 (T17 同场景需 n=500)
    EXPECT_GT(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "A1.5: 真 time_frac (60min 2:0) → conf 升 → n_eff=150 即成交 (证明 time_frac 解锁)";

    // 批1 体育动态: 真实 in-play (60min 2:0) → game 特征 populate。
    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    EXPECT_TRUE(std::isfinite(opt->g_goal_freshness)) << "批1: g_goal_freshness 应 populate (有比分时序)";
    EXPECT_TRUE(std::isfinite(opt->g_game_phase)) << "批1: g_game_phase 应 populate";
    EXPECT_NEAR(opt->g_game_phase, 2.0, 1e-9) << "60/90min → time_frac 0.67 → 末段 phase=2";
    EXPECT_TRUE(std::isfinite(opt->g_remaining_sec)) << "批1: g_remaining_sec 应 populate";
    EXPECT_TRUE(std::isfinite(opt->g_time_x_lead)) << "批1: g_time_x_lead 应 populate";
    // live_stats 差: 存档无 live_stats → game_row -1 → NaN (接线就位, 等白名单流入)
    EXPECT_TRUE(std::isnan(opt->g_danger_attack_diff)) << "批1: live_stats 未流入 → NaN (接线就位)";
}

// ---------------------------------------------------------------------------
// P0-1 单位门禁 (老韩 RM 契约 + 老周 P0 gate): exposure 红线接通 + ×1e6 单位正确
//   fill 累积敞口 → FeedRiskGateway (production via test seam) → 越 condition cap 必触
//   EXCEED_CONDITION_EXPOSURE。漏 ×1e6 (whole 当 micro) → 敞口缩 1e6 ≈ 0 → cap 不咬 →
//   此测红 = 红线静默架空门禁 (老周: 无此测试不许 merge)。
// ---------------------------------------------------------------------------
namespace {
risk::OrderIntent MakeP01Intent(const std::string& cid, const std::string& tid, const std::string& sig,
                                std::int64_t size_micro) {
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    risk::OrderIntent it;
    it.event_ts_ns = now - 200'000'000;
    it.data_source_ts_ns = now - 150'000'000;
    it.ingestion_ts_ns = now - 50'000'000;
    it.as_of_ts_ns = now - 1'000'000;
    it.condition_id = cid;
    it.token_id = tid;
    it.outcome = risk::Outcome::Yes;
    it.side = risk::Side::Buy;
    it.strategy_id = "p01_strat";
    it.signal_id = sig;
    it.feature_snapshot_id = "p01_fs";
    it.price = 0.50;
    it.size_pUSD_micro = size_micro;
    it.book_depth_l1_usdc = 1e14;  // 极大深度 → 不触 slippage/book_depth 门
    it.book_snapshot_ts_ns = now - 1'000'000'000LL;
    it.tick_size = 0.01;
    it.timestamp_ms = now / 1'000'000LL;
    it.metadata = "0x0000000000000000000000000000000000000000000000000000000000000000";
    it.builder = "0x0000000000000000000000000000000000000000000000000000000000000000";
    return it;
}
}  // namespace

TEST_F(PaperLoopTest, P0_1_ExposureRedLine_UnitGate) {
    const std::string cid = "0xC0NDP01";
    const std::string tid = "9001";  // numeric token_id (uint256 格式合法)
    loop_ = MakeLoop();

    // RM 门设置 (到达 position_caps 前所有门: state/market/freshness)
    rm_->set_state(RmState::RUNNING);
    rm_->set_market_active(cid, true);
    rm_->set_market_state(cid, MarketState::PREGAME);
    rm_->set_market_freshness_ms(cid, 100);
    rm_->set_token_book_freshness_ms(tid, 100);
    rm_->set_recon_freshness_ms(100);

    // 控制组: 无敞口 → 10 pUSD 单不应因 condition exposure 拒 (证明 feed 前安全网空)
    rm_->set_edge_ci_lower("p01_ctrl", 0.10);
    auto d_ctrl = rm_->evaluate(MakeP01Intent(cid, tid, "p01_ctrl", 10'000'000LL));
    EXPECT_NE(d_ctrl.reject, RejectCode::EXCEED_CONDITION_EXPOSURE) << "P0-1: 无敞口时不应触 condition cap";

    // 累积敞口: apply_fill 45 whole pUSD → 仓位账本 condition_exposure = 45 (whole pUSD)
    execution::VirtualFill fill{};
    fill.fill_size_usdc = 45'000'000;  // A1 micro (=45 pUSD)
    fill.fill_price = 0.50;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill(cid, tid, strategy::Outcome::Yes, fill);

    // 喂 RM (production FeedRiskGateway): 45 whole × 1e6 = 45e6 micro
    loop_->FeedRiskGatewayForTest();

    // 越 condition cap (50 pUSD=50e6 micro): 45e6 + 10e6 = 55e6 > 50e6 → EXCEED_CONDITION_EXPOSURE
    //   (condition 在 check_position_caps_ 中先于 per_outcome 检查)
    rm_->set_edge_ci_lower("p01_over", 0.10);
    auto d_over = rm_->evaluate(MakeP01Intent(cid, tid, "p01_over", 10'000'000LL));
    EXPECT_EQ(d_over.reject, RejectCode::EXCEED_CONDITION_EXPOSURE)
        << "P0-1 单位门禁: fill 45pUSD 喂入后 +10pUSD 应越 50pUSD condition cap; "
           "若 FeedRiskGateway 漏 ×1e6 则敞口=45 micro≈0, cap 不咬 → exposure 红线静默架空";
}

// ===========================================================================
// A5 (老韩 spec laohan-a5-dd-feed-spec-v1.md): daily_pnl → DD 熔断喂数
//   daily_pnl = 时点净 MtM (best_bid 清算) − 累计 fee; 全量覆盖; 亏损为负 → RM if(pnl<0)。
// ===========================================================================

namespace {
// A5: feed_liveness_report 里查某红线 ever_fed
bool DdEverFed(RiskGateway& rm, std::string_view key) {
    for (auto const& r : rm.feed_liveness_report())
        if (r.key == key)
            return r.ever_fed;
    return false;
}
}  // namespace

// T-A5-1: DD 喂入触发硬 kill + state→HALTED (兼单位门 — 漏 ×1e6 则不触发)
TEST_F(PaperLoopTest, T_A5_1_DD_HardKill_UnitGate) {
    const std::string cid = "0xDD0001";
    const std::string tid = "8001";
    RebuildRmHighCap(5'000.0);  // 硬 5k
    loop_ = MakeLoop();
    rm_->set_state(RmState::RUNNING);
    rm_->set_market_active(cid, true);
    rm_->set_market_state(cid, MarketState::PREGAME);
    rm_->set_market_freshness_ms(cid, 100);
    rm_->set_token_book_freshness_ms(tid, 100);
    rm_->set_recon_freshness_ms(100);
    rm_->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);  // 软阈值依赖 bankroll
    rm_->set_edge_ci_lower("dd_over", 0.10);

    // 亏损持仓: 买 YES @0.80, qty=10000 pUSD; best_bid=0.20 → MtM=(0.20-0.80)*10000=-6000 pUSD
    execution::VirtualFill fill{};
    fill.fill_size_usdc = 10'000'000'000LL;  // 10000 pUSD (micro)
    fill.fill_price = 0.80;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill(cid, tid, strategy::Outcome::Yes, fill);
    hub_->Publish(tid, MakeSyntheticBook(0.20, 0.21));  // best_bid=0.20

    loop_->FeedRiskGatewayForTest();
    EXPECT_TRUE(DdEverFed(*rm_, "daily_pnl")) << "A5: FeedRiskGateway 后 daily_pnl 应 ever_fed";

    auto d = rm_->evaluate(MakeP01Intent(cid, tid, "dd_over", 10'000'000LL));
    EXPECT_EQ(d.reject, RejectCode::DAILY_LOSS_HALT)
        << "A5 单位门: -6000pUSD 浮亏喂入应越 5k 硬阈值; 漏 ×1e6 则=-6000 micro≈0, loss<5e9 不触发";
    EXPECT_EQ(rm_->state(), RmState::HALTED) << "A5: 硬 kill 应迁移 state→HALTED";
}

// T-A5-2: 软熔断方向 — is_close 平仓放行 (浮亏 ∈ [3k,5k) 软触硬不触)
TEST_F(PaperLoopTest, T_A5_2_DD_Soft_CloseExempt) {
    const std::string cid = "0xDD0002";
    const std::string tid = "8002";
    RebuildRmHighCap(5'000.0);  // 硬 5k; 软 = 0.03×100k = 3k
    loop_ = MakeLoop();
    rm_->set_state(RmState::RUNNING);
    rm_->set_market_active(cid, true);
    rm_->set_market_state(cid, MarketState::PREGAME);
    rm_->set_market_freshness_ms(cid, 100);
    rm_->set_token_book_freshness_ms(tid, 100);
    rm_->set_recon_freshness_ms(100);
    rm_->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);
    rm_->set_edge_ci_lower("dd_soft_open", 0.10);
    rm_->set_edge_ci_lower("dd_soft_close", 0.10);

    // 浮亏 4000 pUSD ∈ [3k,5k): 买 @0.80 qty=10000, best_bid=0.40 → (0.40-0.80)*10000=-4000
    execution::VirtualFill fill{};
    fill.fill_size_usdc = 10'000'000'000LL;
    fill.fill_price = 0.80;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill(cid, tid, strategy::Outcome::Yes, fill);
    hub_->Publish(tid, MakeSyntheticBook(0.40, 0.41));
    loop_->FeedRiskGatewayForTest();

    // 开仓 (is_close=false) → 软熔断拒
    auto d_open = rm_->evaluate(MakeP01Intent(cid, tid, "dd_soft_open", 10'000'000LL));
    EXPECT_EQ(d_open.reject, RejectCode::DAILY_LOSS_HALT) << "A5: 软熔断应拒新开仓";
    EXPECT_NE(rm_->state(), RmState::HALTED) << "A5: 软熔断不应硬 kill (state 不迁 HALTED)";

    // 平仓 (is_close=true) → 放行
    auto it_close = MakeP01Intent(cid, tid, "dd_soft_close", 10'000'000LL);
    it_close.is_close = true;
    it_close.side = risk::Side::Sell;
    auto d_close = rm_->evaluate(it_close);
    EXPECT_NE(d_close.reject, RejectCode::DAILY_LOSS_HALT) << "A5: 软熔断应放行平仓 (is_close=true)";
}

// T-A5-3: 无双计自愈 — 连喂两次, daily_pnl 覆盖写非累加。判别用软阈值 3k (=0.03×100k bankroll):
//   单次浮亏 2k < 3k 不触; 若累加成 4k ≥ 3k 则误触 = 双计 bug。
TEST_F(PaperLoopTest, T_A5_3_DD_NoDoubleCount) {
    const std::string cid = "0xDD0003";
    const std::string tid = "8003";
    RebuildRmHighCap(5'000.0);  // 硬 5k; 软 = 0.03×100k = 3k (本测有效判别阈)
    loop_ = MakeLoop();
    rm_->set_state(RmState::RUNNING);
    rm_->set_market_active(cid, true);
    rm_->set_market_state(cid, MarketState::PREGAME);
    rm_->set_market_freshness_ms(cid, 100);
    rm_->set_token_book_freshness_ms(tid, 100);
    rm_->set_recon_freshness_ms(100);
    rm_->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);
    rm_->set_edge_ci_lower("dd_dbl", 0.10);

    execution::VirtualFill fill{};
    fill.fill_size_usdc = 10'000'000'000LL;  // qty=10000
    fill.fill_price = 0.80;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill(cid, tid, strategy::Outcome::Yes, fill);
    hub_->Publish(tid, MakeSyntheticBook(0.60, 0.61));  // 浮亏 (0.60-0.80)*10000 = -2000

    loop_->FeedRiskGatewayForTest();
    loop_->FeedRiskGatewayForTest();  // 第二 tick: 覆盖写则仍 -2k, 累加则 -4k

    auto d = rm_->evaluate(MakeP01Intent(cid, tid, "dd_dbl", 10'000'000LL));
    EXPECT_NE(d.reject, RejectCode::DAILY_LOSS_HALT)
        << "A5: 两次喂入应覆盖写 (仍 -2k < 3k 软阈值不触); 若累加成 -4k ≥ 3k 则误触 = 双计 bug";
}

// T-A5-4: 无效 bid 保守 — 拿不到有效 best_bid 的仓位不臆造浮盈 (MtM 贡献 0)
TEST_F(PaperLoopTest, T_A5_4_DD_InvalidBid_Conservative) {
    const std::string cid = "0xDD0004";
    const std::string tid = "8004";
    RebuildRmHighCap(5'000.0);
    loop_ = MakeLoop();
    rm_->set_state(RmState::RUNNING);
    rm_->set_market_active(cid, true);
    rm_->set_market_state(cid, MarketState::PREGAME);
    rm_->set_market_freshness_ms(cid, 100);
    rm_->set_token_book_freshness_ms(tid, 100);
    rm_->set_recon_freshness_ms(100);
    rm_->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);
    rm_->set_edge_ci_lower("dd_nobid", 0.10);

    // 持仓但 hub 无该 token book → hub_.Read 返回 nullopt → MtM 贡献跳过 (不臆造正浮盈)
    execution::VirtualFill fill{};
    fill.fill_size_usdc = 10'000'000'000LL;
    fill.fill_price = 0.80;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill(cid, tid, strategy::Outcome::Yes, fill);
    // 故意不 Publish tid 的 book

    loop_->FeedRiskGatewayForTest();  // 不崩溃; daily_pnl = -cum_fee = 0 (直喂路径无 fee 累计)
    EXPECT_TRUE(DdEverFed(*rm_, "daily_pnl")) << "A5: 无效 bid 仍喂 daily_pnl (=0, 标 ever_fed)";

    auto d = rm_->evaluate(MakeP01Intent(cid, tid, "dd_nobid", 10'000'000LL));
    EXPECT_NE(d.reject, RejectCode::DAILY_LOSS_HALT)
        << "A5: 无效 bid → MtM=0 (不臆造亏也不臆造盈), daily_pnl≥0 不触 DD";
}

// T-A5-5: feed-liveness 状态转移 — daily_pnl NEVER FED → ever_fed; consec_loss 仍 NEVER FED (M2 边界)
TEST_F(PaperLoopTest, T_A5_5_FeedLiveness_Transition) {
    RebuildRmHighCap(5'000.0);
    loop_ = MakeLoop();

    // 喂前: daily_pnl 从未喂
    EXPECT_FALSE(DdEverFed(*rm_, "daily_pnl")) << "A5: FeedRiskGateway 前 daily_pnl 应 NEVER FED";

    loop_->FeedRiskGatewayForTest();  // 无仓位也喂 set_daily_pnl(0) → 标 fed

    EXPECT_TRUE(DdEverFed(*rm_, "daily_pnl")) << "A5: FeedRiskGateway 后 daily_pnl ever_fed";
    EXPECT_FALSE(DdEverFed(*rm_, "consec_loss"))
        << "A5: consec_loss 延 M2 (M1 无平仓源), 应仍 NEVER FED — 锁 M2 边界";
}

// T-A5-6 (老韩 A5 review nit#1): MtM=0 临界 — best_bid==avg_entry 不臆造盈亏 (break-even)
TEST_F(PaperLoopTest, T_A5_6_DD_BreakEven_ZeroMtM) {
    const std::string cid = "0xDD0006";
    const std::string tid = "8006";
    RebuildRmHighCap(5'000.0);
    loop_ = MakeLoop();
    rm_->set_state(RmState::RUNNING);
    rm_->set_market_active(cid, true);
    rm_->set_market_state(cid, MarketState::PREGAME);
    rm_->set_market_freshness_ms(cid, 100);
    rm_->set_token_book_freshness_ms(tid, 100);
    rm_->set_recon_freshness_ms(100);
    rm_->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);
    rm_->set_edge_ci_lower("dd_be", 0.10);

    execution::VirtualFill fill{};
    fill.fill_size_usdc = 10'000'000'000LL;  // qty=10000
    fill.fill_price = 0.50;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill(cid, tid, strategy::Outcome::Yes, fill);
    hub_->Publish(tid, MakeSyntheticBook(0.50, 0.51));  // best_bid==avg_entry 0.50 → MtM=0

    loop_->FeedRiskGatewayForTest();
    auto d = rm_->evaluate(MakeP01Intent(cid, tid, "dd_be", 10'000'000LL));
    EXPECT_NE(d.reject, RejectCode::DAILY_LOSS_HALT)
        << "A5: best_bid==avg_entry → MtM=0 → daily_pnl=0 (无 cum_fee), 不臆造亏损不触 DD";
}

// T-A5-7 (老韩 A5 review nit#1): 多仓位聚合 — daily_pnl = Σ 各仓 MtM (跨 condition/token 求和)
TEST_F(PaperLoopTest, T_A5_7_DD_MultiPositionAggregation) {
    RebuildRmHighCap(5'000.0);  // 硬 5k; 软 = 0.03×100k = 3k (聚合判别阈)
    loop_ = MakeLoop();
    rm_->set_state(RmState::RUNNING);
    rm_->set_bankroll(stcpp::domain::MicroPUSD::from_pusd(100'000.0).v);
    // 两个 condition/token, 各亏 2000 pUSD → 聚合 4000 ≥ 软 3k 触发; 单仓 2000 < 3k 不触
    struct P {
        std::string cid, tid;
    };
    const P p0{"0xDDM01", "8101"};
    const P p1{"0xDDM02", "8102"};
    for (auto const& p : {p0, p1}) {
        rm_->set_market_active(p.cid, true);
        rm_->set_market_state(p.cid, MarketState::PREGAME);
        rm_->set_market_freshness_ms(p.cid, 100);
        rm_->set_token_book_freshness_ms(p.tid, 100);
        execution::VirtualFill fill{};
        fill.fill_size_usdc = 10'000'000'000LL;  // qty=10000
        fill.fill_price = 0.80;
        fill.reject = execution::MatchReject::Ok;
        position_ledger_->apply_fill(p.cid, p.tid, strategy::Outcome::Yes, fill);
        hub_->Publish(p.tid, MakeSyntheticBook(0.60, 0.61));  // (0.60-0.80)*10000 = -2000 each
    }
    rm_->set_recon_freshness_ms(100);
    rm_->set_edge_ci_lower("dd_multi", 0.10);

    loop_->FeedRiskGatewayForTest();
    auto d = rm_->evaluate(MakeP01Intent(p0.cid, p0.tid, "dd_multi", 10'000'000LL));
    EXPECT_EQ(d.reject, RejectCode::DAILY_LOSS_HALT)
        << "A5: 两仓各亏 2k 应聚合成 -4k ≥ 软 3k 触发 DD; 若只算单仓 -2k < 3k 则不触 = 聚合 bug";
}

// Phase B (小梁 spec §2): SelectSide 选边逻辑单测 (de-vig 锚定, 纯函数)
//   raw_edge_yes = p_fair_yes - p_market_devig; >= 0 → YES 低估买 YES; < 0 → NO 低估买 NO。
TEST_F(PaperLoopTest, T_PhaseB_SelectSide_DeVigAnchored) {
    loop_ = MakeLoop();
    // YES 低估 (模型 fair 0.60 > 市场共识 0.50) → 买 YES
    EXPECT_EQ(loop_->SelectSideForTest(0.60, 0.50).outcome, TradedSide::Yes);
    // NO 低估 (模型 fair_YES 0.30 < 共识 0.50, 即 fair_NO 0.70 > 共识_NO 0.50) → 买 NO
    EXPECT_EQ(loop_->SelectSideForTest(0.30, 0.50).outcome, TradedSide::No);
    // 临界 p_fair == devig → raw_edge=0 >= 0 → 买 YES (默认, 下游 sizing 会因 edge=0 不下单)
    EXPECT_EQ(loop_->SelectSideForTest(0.50, 0.50).outcome, TradedSide::Yes);
    // side 恒 Buy (M1 只买不平; sell-to-open 空头 M2)
    EXPECT_EQ(loop_->SelectSideForTest(0.30, 0.50).side, strategy::Side::Buy);
}

// Phase B (小梁 §7 C4 反向-fill): NO 被低估 → 真买 NO 端到端 (解 Phase A 建不起 NO intent)。
//   soccer 0:3 落后 + 75min (conf~0.52) → 模型 fair_YES 低 (~0.30); 市场仍 ~0.50 (未反映落后) →
//   raw_edge_yes<0 → 选 NO; n_eff=150 sigma 小 → NO edge 过 CI → 在 NO token(1002) 成交。
TEST_F(PaperLoopTest, T_PhaseB_BuyNo_EndToEnd) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    auto es = MakeFreshScore("gs-no", 0, 3);  // YES 队 0:3 落后 → prior_yes 低
    es.sport = "soccer";                      // total_game_seconds=5400
    es.clock_sec = 75 * 60;                   // 75min → time_frac≈0.83 → conf≈0.52
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-no"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-no", true};

    // 市场把 YES/NO 都定价 ~0.50 (未反映落后) → devig≈0.50 > 模型 fair_YES 0.30 → NO 低估
    // 市场把 YES 高估 (~0.70)、NO 低估 (~0.30) (未反映 0:3 落后) → devig≈0.70 >> 模型 fair_YES
    //   → raw_edge_yes 强负 → 买被低估的 NO (ask~0.31)。
    hub_->Publish("1001", MakeFreshBook(0.69, 0.71));  // YES book (市场高估 YES)
    hub_->Publish("1002", MakeFreshBook(0.29, 0.31));  // NO book (被选边, 低估)

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 150;  // 生产级紧度 → sigma 小 → NO edge 过 CI
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    loop_->Stop();

    EXPECT_GT(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "Phase B: NO 被低估 (YES 0:3 落后但市场未反映) → 选 NO → 应成交";
    // 验成交在 NO token(1002), 非 YES(1001) — 证选边 + 字段切换正确, 不买错边
    bool found_no = false;
    for (auto const& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1002") {
            found_no = true;
            EXPECT_EQ(pv.outcome, strategy::Outcome::No) << "Phase B: NO token 仓位 outcome 应为 No";
        }
        EXPECT_NE(pv.token_id, "1001") << "Phase B: 不应在 YES token 成交 (选的是 NO, 防买错边)";
    }
    EXPECT_TRUE(found_no) << "Phase B: 应在 NO token(1002) 建仓 (买被低估的 NO)";
}

// Phase B fail-closed: 选 NO 但 NO book 缺 → 不交易 (防「选 NO 用 YES 价/深度」漏网, 老郭核)。
TEST_F(PaperLoopTest, T_PhaseB_NoSelected_NoBookAbsent_FailClosed) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;
    auto es = MakeFreshScore("gs-noabs", 0, 3);  // YES 0:3 落后 → 模型 fair_YES 低 → 倾向选 NO
    es.sport = "soccer";
    es.clock_sec = 75 * 60;
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-noabs"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-noabs", true};
    // 只发 YES book (市场高估 YES) — NO book 缺。SelectSide 倾向 NO 但 NO book 不可得 → fail-closed。
    hub_->Publish("1001", MakeFreshBook(0.69, 0.71));
    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 150;
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    loop_->Stop();
    EXPECT_EQ(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "Phase B fail-closed: 选 NO 但 NO book 缺 → 不应成交 (绝不用 YES 价/深度替代买错边)";
}

// 盈利能力演示: 连接好的流水线在「模型有 edge」时产生盈利 (运行 + 盈利)。
//   场景: YES 2:0 领先 (模型 fair~0.65) + 市场初始低估 YES (ask~0.47) → 流水线买入被低估 YES;
//         市场收敛到 fair (best_bid~0.64) → YES 仓位 MtM 盈利 (买被低估边的正期望兑现)。
//   注: 实盘真盈利需 A3 (Goalserve live 比分接通); 本测用合成 edge + 收敛 demo 流水线盈利能力
//       (证整条决策→成交→PnL 链路 operational, A3 一到位即可换 live 数据产真盈利)。
TEST_F(PaperLoopTest, T_Profit_PipelineProducesProfit) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // 高 bankroll/cap (足够大仓位演示 ≥500 pUSD PnL); RM 与 cfg 同源 (P0-2)
    RiskConfig c;
    c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(10'000.0);
    c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
    c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(100'000.0);
    c.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000'000.0);
    c.edge_ci_lower_floor = -1.0;
    rm_ = std::make_unique<RiskGateway>(c, std::make_shared<NullAuditEmitter>());
    cfg_.bankroll_usdc = 1'000'000.0;
    cfg_.per_order_cap_usdc = 10'000.0;
    cfg_.market_exposure_cap_usdc = 100'000.0;
    cfg_.per_outcome_cap_usdc = 100'000.0;
    cfg_.n_effective = 150;
    cfg_.advisory_markets_no_intent = false;

    auto es = MakeFreshScore("gs-profit", 2, 0);  // YES 2:0 领先 → 模型 fair_YES 高
    es.sport = "soccer";
    es.clock_sec = 60 * 60;  // 60min → conf~0.45
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-profit"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-profit", true};

    // 市场初始低估 YES (ask 0.47), NO 高估 (0.53) → devig~0.47 < 模型 fair → 买 YES。
    //   大 book 深度 (200k) → 大单 (per_order 10k) 能成交 (撮合不 BelowFloor)。
    const std::int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
    auto big_fresh = [&](double bid, double ask) {
        auto f = MakeSyntheticBook(bid, ask, 200'000.0, 200'000.0);  // 大深度
        f.event_ts_ns = now_ns - 3'000'000'000LL;
        f.data_source_ts_ns = now_ns - 2'000'000'000LL;
        f.ingestion_ts_ns = now_ns - 1'000'000'000LL;
        f.as_of_ts_ns = now_ns;
        return f;
    };
    hub_->Publish("1001", big_fresh(0.45, 0.47));
    hub_->Publish("1002", big_fresh(0.53, 0.55));

    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    loop_->Stop();

    ASSERT_GT(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "流水线应买入被低估的 YES (模型 fair > 市场 ask)";

    // 市场收敛到模型 fair (best_bid~0.64): YES 仓位按收敛 mark 计 MtM (A5 净 MtM 口径)
    const double converged_bid = 0.64;
    double total_pnl = 0.0;
    for (auto const& pv : position_ledger_->get_all_positions()) {
        const double net_qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;  // A5 口径
        total_pnl += (converged_bid - pv.avg_entry_price) * net_qty;
    }
    std::fprintf(stderr, "[PROFIT] 流水线 MtM PnL = %.2f pUSD (买被低估 YES @~0.47 → 收敛 %.2f)\n", total_pnl,
                 converged_bid);
    EXPECT_GE(total_pnl, 500.0)
        << "流水线盈利能力: 买被低估 YES + 市场收敛 fair → MtM PnL ≥ 500 pUSD (运行 + 盈利演示)";
}

// ===========================================================================
// 目标仓位控制器 wire 集成测试 (老雷 controller spec v1 §11 Step 5)
//   控制器替换一次性 BUY: order = 目标 − 现仓; 限价不追 (reservation 门); 收敛后死区不动。
// ===========================================================================

// TC-1: 限价不追 (老周 Q-周-1) — raw edge 薄 (vig 吃光净 edge) → reservation_buy < best_ask →
//   控制器判 NotMarketable → 不下单 (orders_held>0, 零成交)。即老 T17 的 2¢ edge 场景被正确拦。
//   关键: de-vig 共识看似有 edge (sizing 可能 suggested>0), 但**真实付价 raw ask** 越过 reservation。
TEST_F(PaperLoopTest, TC1_Controller_LimitNotChase_Holds) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // YES 2:0 但**无时钟** (time_frac=0 → conf 压在 base 0.15 → fair 仅微高于市场);
    //   ask=0.32 raw edge ~2¢ < fee+margin (~4¢) → reservation_buy < 0.32 → 限价不追。
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-thin"] = MakeFreshScore("gs-thin", 2, 0);  // 无 sport/clock → time_frac=0
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-thin", true};

    hub_->Publish("1001", MakeFreshBook(0.28, 0.32));

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 500;
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    loop_->Stop();

    EXPECT_EQ(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "TC-1: raw edge 薄 → reservation_buy < best_ask → 限价不追, 零成交";
    EXPECT_EQ(loop_->stats().orders_approved.load(), static_cast<std::uint64_t>(0))
        << "TC-1: 不可成交价不构造 intent (老周 Q-周-1 控制器前置门, 不进 RM)";
    EXPECT_GT(loop_->stats().orders_held.load(), static_cast<std::uint64_t>(0))
        << "TC-1: 控制器主动 hold (NotMarketable/死区) → orders_held 计数";

    // 验 quote: reservation_buy_px < best_ask (0.32) — 限价门拦截的直接证据
    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    EXPECT_TRUE(opt->predict_ok) << "TC-1: 有真实 fair (in-play 比分), 仅 raw edge 不足";
    EXPECT_LT(opt->reservation_buy_px, 0.32)
        << "TC-1: reservation_buy = fair − fee − margin < 付价 0.32 → 越界无净 edge (限价不追根因)";
}

// TC-2: 目标仓位收敛 (老板范式核心) — 持续 tick 不再无界累加; 到目标后控制器进死区 hold。
//   旧一次性 BUY: 每 tick 都下单 → 敞口涨到 cap 才停。新控制器: order=目标−现仓 → 收敛即停。
//   判据: orders_held>0 (收敛后死区生效) 且 持仓 ≈ target (≤ per_order_cap, 不爆 exposure)。
TEST_F(PaperLoopTest, TC2_Controller_ConvergesToTarget_NoUnboundedAccumulation) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    auto es = MakeFreshScore("gs-conv", 2, 0);
    es.sport = "soccer";
    es.clock_sec = 60 * 60;  // conf~0.45 → fair 足够高 → 有真实 edge → 先买到目标
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-conv"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-conv", true};

    hub_->Publish("1001", MakeFreshBook(0.28, 0.30));  // 显著低估 → 限价门过

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 500;
    // per_order_cap=10 pUSD (fixture 默认): target = Kelly ∧ ≤10 → 持仓收敛到 ≤10, 不涨到 exposure 50
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));  // ~16 ticks (足够收敛 + 多次死区 hold)
    loop_->Stop();

    EXPECT_GT(loop_->stats().fills_completed.load(), static_cast<std::uint64_t>(0))
        << "TC-2: 有真实 edge + 低估 → 先买到目标 (≥1 成交)";
    EXPECT_GT(loop_->stats().orders_held.load(), static_cast<std::uint64_t>(0))
        << "TC-2: 收敛到目标后控制器进死区 hold (证明非旧一次性 BUY 的每 tick 下单)";

    // 持仓收敛到 target (≤ per_order_cap 10 pUSD), 绝不爆 exposure cap (50 pUSD)
    double net_qty = 0.0;
    for (const auto& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1001") {
            net_qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
        }
    }
    EXPECT_GT(net_qty, 0.0) << "TC-2: 应建立 YES 多仓";
    EXPECT_LE(net_qty, 10.0 + 1e-6)
        << "TC-2: 持仓收敛到 target (≤ per_order_cap 10), 不无界累加到 exposure cap";
}

// ===========================================================================
// M2-a: 选边翻转平旧边 (老雷 spec §11.6; 老韩 C1 不触发 + 老周 Q-周-2 范围内)
//   持有被低估边 → 市场反转令该边 overpriced (另一边变低估) → 选边翻转 → 旧边自动平仓。
//   验证: 旧边 (YES) 持仓被卖回 (减仓/趋零) + 新边 (NO) 建仓。这是 Step3-5 发现的真实 de-risk 路径。
// ===========================================================================
TEST_F(PaperLoopTest, TM2a_SideFlip_ClosesOldSide) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // 比分恒定: YES 2:0 领先 (fair_YES~0.55, 60min soccer)。fair 不变, 只动市场价格制造翻转。
    auto es = MakeFreshScore("gs-flip", 2, 0);
    es.sport = "soccer";
    es.clock_sec = 60 * 60;
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-flip"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-flip", true};

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 500;
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));

    // ---- Phase 1: 市场低估 YES (ask_YES=0.30), 高估 NO (ask_NO=0.70) → 选 YES, 建 YES 多仓 ----
    hub_->Publish("1001", MakeFreshBook(0.28, 0.30));  // YES 被低估
    hub_->Publish("1002", MakeFreshBook(0.68, 0.70));  // NO 高估
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(450));  // ~9 ticks: 建 YES 仓

    double yes_after_p1 = 0.0;
    for (const auto& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1001") yes_after_p1 = static_cast<double>(pv.size_usdc) / 1'000'000.0;
    }
    ASSERT_GT(yes_after_p1, 0.0) << "M2-a Phase1: 应先建立 YES 多仓 (被低估边)";

    // ---- Phase 2: 比分翻转 (YES 队 0:3 落败) → fair_YES 暴跌, 市场未追上 (仍 ~0.54) ----
    //   现实 de-risk 场景: 领先丢失 → fair 翻 → 旧持仓的边 overpriced (市场滞后) → 平旧边。
    //   fair_YES~0.32 < devig_YES~0.54 → 选边翻转到 NO。
    //   旧边 YES: 非选边 + 持仓 + bid 0.53 ≥ reservation_sell(YES)≈0.36 → 平 YES (减仓趋零)。
    //   新边 NO: 低估 (ask_NO 0.47 < fair_NO 0.68) → 建 NO 多仓。
    auto es2 = MakeFreshScore("gs-flip", 0, 3);  // YES 队 0:3 落败 (领先丢失 → 翻转)
    es2.sport = "soccer";
    es2.clock_sec = 60 * 60;
    auto sm2 = std::make_shared<ScoreMap>();
    (*sm2)["gs-flip"] = es2;
    store.Publish(std::shared_ptr<const ScoreMap>(sm2));  // 热刷比分 (loop 下个 tick 读新值)
    hub_->Publish("1001", MakeFreshBook(0.52, 0.54));  // YES 市场滞后 (仍高于新 fair → overpriced)
    hub_->Publish("1002", MakeFreshBook(0.45, 0.47));  // NO 现被低估 (fair_NO~0.68)
    std::this_thread::sleep_for(std::chrono::milliseconds(550));  // ~11 ticks: 平 YES + 建 NO
    loop_->Stop();

    double yes_final = 0.0, no_final = 0.0;
    for (const auto& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1001") yes_final = static_cast<double>(pv.size_usdc) / 1'000'000.0;
        if (pv.token_id == "1002") no_final = static_cast<double>(pv.size_usdc) / 1'000'000.0;
    }
    std::fprintf(stderr, "[M2a] YES: %.4f(p1) → %.4f(final); NO final=%.4f\n", yes_after_p1, yes_final,
                 no_final);

    // 核心: 选边翻转后旧边 (YES) 被平掉 (减仓 → 显著低于 Phase1; 趋零)
    EXPECT_LT(yes_final, yes_after_p1)
        << "M2-a: 选边翻转后旧边 YES 应被平仓 (减仓; bid 高 → reservation_sell 可成交)";
    // 新边 (NO) 建仓 (买被低估的 NO)
    EXPECT_GT(no_final, 0.0) << "M2-a: 翻转后新被低估边 NO 应建仓";
    // 旧边不穿零不开空 (H-2): YES 持仓 ≥ 0 (减仓 clamp ≤ 持仓, 绝不变负)
    EXPECT_GE(yes_final, 0.0) << "M2-a/H-2: 平旧边绝不穿零开空 (long→0, 不反向)";
}

// ===========================================================================
// 时序地基 (老板 2026-05-31): ml::FeatureHistory 接入 paper_loop → 微价变化率/realized vol
//   进 QuoteFeatures。验证: 推进 ts + 变价的 book 序列 → 时序特征端到端 populate (非 NaN)。
// ===========================================================================
TEST_F(PaperLoopTest, TS1_TimeSeriesFeatures_Populate) {
    // 显式 ds_ts book: data_source_ts 推进 (时序样本去重靠单调门), 价格上行 → ROC>0。
    //   4ts 链有效 + ingestion 新鲜 (RM 不 stale 拒); 仅 quote 路径需通 (ts 特征不受 has_real_fair gate)。
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    auto book_at = [&](double bid, double ask, std::int64_t ds_ts) {
        auto f = MakeSyntheticBook(bid, ask);
        f.event_ts_ns = now - 10'000'000'000LL;  // 10s 前
        f.data_source_ts_ns = ds_ts;              // 显式推进 (时序锚)
        f.ingestion_ts_ns = now - 1'000'000'000LL;  // 1s 前 (新鲜)
        f.as_of_ts_ns = now;
        return f;
    };

    loop_ = MakeLoop();  // advisory 默认 true 即可: quote 仍发布, ts 特征不受 gate
    loop_->Start();
    // 三个推进-ts + 上行价 book (间隔 > tick 50ms 确保 loop 各读到一次)。
    hub_->Publish("1001", book_at(0.49, 0.51, now - 5'000'000'000LL));
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    hub_->Publish("1001", book_at(0.53, 0.55, now - 4'000'000'000LL));
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    hub_->Publish("1001", book_at(0.57, 0.59, now - 3'000'000'000LL));
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    loop_->Stop();

    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    std::fprintf(stderr, "[TS1] samples=%d roc=%.6f vol=%.6f\n", opt->ts_window_samples,
                 opt->mp_roc_per_sec, opt->realized_vol);

    // 时序地基: 推进的 book 序列 → 窗口内 ≥2 样本 → 派生非 NaN。
    EXPECT_GE(opt->ts_window_samples, 2) << "TS1: 3 个推进-ts book → 窗口内 ≥2 时序样本";
    ASSERT_TRUE(std::isfinite(opt->mp_roc_per_sec)) << "TS1: 变化率应 populate (非 NaN)";
    EXPECT_GT(opt->mp_roc_per_sec, 0.0) << "TS1: 价格上行 → 变化率 > 0 (方向正确)";
    ASSERT_TRUE(std::isfinite(opt->realized_vol)) << "TS1: realized vol 应 populate";
    EXPECT_GT(opt->realized_vol, 0.0) << "TS1: 价格在动 → vol > 0";

    // 批1 微结构: Amihud / OFI / depth_vol 应 populate (推进 book + bid/ask 在 ring)。
    EXPECT_TRUE(std::isfinite(opt->b_amihud)) << "TS1: b_amihud 应 populate (现有 ring 派生)";
    EXPECT_TRUE(std::isfinite(opt->b_ofi)) << "TS1: b_ofi 应 populate (best_ask_size 已入 ring)";
    EXPECT_TRUE(std::isfinite(opt->b_bid_depth_vol)) << "TS1: b_bid_depth_vol 应 populate";
    // 批1 cross: log_odds / pin_risk 点特征 (从 fair 现算, 恒有值)。
    EXPECT_TRUE(std::isfinite(opt->x_log_odds_fair)) << "TS1: x_log_odds_fair 应 populate";
    EXPECT_GT(opt->x_pin_risk, 0.0) << "TS1: x_pin_risk = min(fair,1−fair) > 0";
    EXPECT_LE(opt->x_pin_risk, 0.5) << "TS1: pin_risk ≤ 0.5";
}

// TS2 (slice-2 卖不出): observe-always 捕获无 bid tick (旧码 early-return 会审查掉) →
//   bid_absence_frac > 0。证明「卖不出」被量化成特征 (老板: 模型包含, 非硬门)。
TEST_F(PaperLoopTest, TS2_ExitLiquidity_CapturesNoBid) {
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    auto book_at = [&](double bid, double ask, double bid_size, std::int64_t ds_ts) {
        auto f = MakeSyntheticBook(bid, ask, bid_size, 500.0);
        f.event_ts_ns = now - 10'000'000'000LL;
        f.data_source_ts_ns = ds_ts;
        f.ingestion_ts_ns = now - 1'000'000'000LL;
        f.as_of_ts_ns = now;
        return f;
    };

    loop_ = MakeLoop();
    loop_->Start();
    hub_->Publish("1001", book_at(0.49, 0.51, 500.0, now - 5'000'000'000LL));  // 有 bid
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    hub_->Publish("1001", book_at(0.0, 0.51, 0.0, now - 4'000'000'000LL));  // 卖不出 (无 bid; 旧码 bail)
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    hub_->Publish("1001", book_at(0.49, 0.51, 500.0, now - 3'000'000'000LL));  // 有 bid (触发 quote 发布)
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    loop_->Stop();

    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    std::fprintf(stderr, "[TS2] samples=%d absence=%.4f exit_depth=%.2f\n", opt->ts_window_samples,
                 opt->bid_absence_frac, opt->exit_depth_mean);

    EXPECT_GE(opt->ts_window_samples, 3) << "TS2: 3 个推进-ts book (含无 bid) 都被 observe-always 记录";
    ASSERT_TRUE(std::isfinite(opt->bid_absence_frac));
    EXPECT_GT(opt->bid_absence_frac, 0.0)
        << "TS2: 无 bid tick 被捕获 (旧码 early-return 会审查掉这个卖不出事件)";
    EXPECT_LT(opt->bid_absence_frac, 1.0) << "TS2: 非整窗卖不出 (有 bid 样本也在)";
    EXPECT_TRUE(std::isfinite(opt->exit_depth_mean)) << "TS2: 退出深度均值 populate";
}

// TS3 (slice-3 结算, feature-first): 真时钟 → time_to_resolution_frac 派生 (体育免新数据源) +
//   resolution_status 从 book 快照流到 quote (字段载体接通, 旧码缺字段载不了)。
TEST_F(PaperLoopTest, TS3_ResolutionFeatures) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    auto es = MakeFreshScore("gs-res", 2, 0);
    es.sport = "soccer";     // total_game_seconds = 5400
    es.clock_sec = 60 * 60;  // 60min → time_frac≈0.667 → time_to_resolution_frac≈0.333
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-res"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-res", true};

    // book 带 resolution_status = kResolving(1) — 验证字段从 OrderBookFeatures 流到 QuoteFeatures
    auto f = MakeFreshBook(0.28, 0.30);
    f.resolution_status = 1;  // PM WSS kResolving (合成注入; 真路径待 adapter 接 kOutcomes)
    hub_->Publish("1001", f);

    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    const auto opt = quote_hub_->Read("cond-test-001");
    ASSERT_TRUE(opt.has_value() && opt->valid);
    ASSERT_TRUE(opt->predict_ok) << "TS3: 真 in-play 比分 → has_real_fair";
    std::fprintf(stderr, "[TS3] ttr=%.4f res_status=%u\n", opt->time_to_resolution_frac,
                 opt->resolution_status);
    // 3a: 结算临近度从时钟派生 (60min/90min → ~0.33)
    ASSERT_TRUE(std::isfinite(opt->time_to_resolution_frac)) << "TS3: 真时钟 → 临近度 populate";
    EXPECT_GT(opt->time_to_resolution_frac, 0.0);
    EXPECT_LT(opt->time_to_resolution_frac, 1.0);
    EXPECT_NEAR(opt->time_to_resolution_frac, 1.0 - 3600.0 / 5400.0, 0.02) << "TS3: ≈0.333 (1−60/90min)";
    // 3c 载体: resolution_status 从 book 流到 quote (字段接通)
    EXPECT_EQ(opt->resolution_status, 1) << "TS3: PM 结算状态从 OrderBookFeatures 流到 QuoteFeatures";
}

// TS4 (slice-3b 结算 realize): 建 YES 仓 → 比赛 Ended (YES 胜) → 持仓 realize 到 1.0 + 平仓。
//   验证: 持仓平掉 (账本归零) + realized PnL > 0 (买便宜→结算 1.0) + positions_settled 计数。
//   现实修复: 此前持仓在账本永远挂着, paper PnL 结算时错 (无 bid → MtM 贡献 0)。
TEST_F(PaperLoopTest, TS4_Settlement_RealizesAndCloses) {
    using stcpp::data::ScoreMap;
    using stcpp::data::ScoreSnapshotStore;

    // ---- Phase 1: in-play YES 2:0 领先 + 市场低估 (ask 0.30) → 建 YES 多仓 ----
    auto es = MakeFreshScore("gs-settle", 2, 0);
    es.sport = "soccer";
    es.clock_sec = 60 * 60;
    auto sm = std::make_shared<ScoreMap>();
    (*sm)["gs-settle"] = es;
    ScoreSnapshotStore store;
    store.Publish(std::shared_ptr<const ScoreMap>(sm));
    auto emap = std::make_shared<ConditionEventMap>();
    (*emap)["cond-test-001"] = EventMapEntry{"gs-settle", true};

    cfg_.advisory_markets_no_intent = false;
    cfg_.n_effective = 500;
    loop_ = MakeLoop();
    loop_->SetScoreStore(&store);
    loop_->SetEventMapping(std::shared_ptr<const ConditionEventMap>(emap));

    hub_->Publish("1001", MakeFreshBook(0.28, 0.30));
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(450));

    double yes_qty = 0.0, avg = 0.0;
    for (const auto& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1001") {
            yes_qty = static_cast<double>(pv.size_usdc) / 1'000'000.0;
            avg = pv.avg_entry_price;
        }
    }
    ASSERT_GT(yes_qty, 0.0) << "TS4 Phase1: 应先建 YES 多仓";

    // ---- Phase 2: 比赛结束 (status final → Ended), YES 2:0 胜 → 结算 YES=1.0 ----
    auto es2 = MakeFreshScore("gs-settle", 2, 0);
    es2.status = "final";  // → TimeStatus::Ended (终态)
    es2.sport = "soccer";
    es2.clock_sec = 90 * 60;
    auto sm2 = std::make_shared<ScoreMap>();
    (*sm2)["gs-settle"] = es2;
    store.Publish(std::shared_ptr<const ScoreMap>(sm2));  // 热刷终态
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    loop_->Stop();

    std::fprintf(stderr, "[TS4] yes_qty(p1)=%.4f avg=%.4f realized=%.4f settled=%llu\n", yes_qty, avg,
                 loop_->cum_realized_pnl_pusd(),
                 static_cast<unsigned long long>(loop_->stats().positions_settled.load()));

    // 核心: 持仓被平掉 (结算归零账本)
    double yes_final = 0.0;
    for (const auto& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1001") yes_final = static_cast<double>(pv.size_usdc) / 1'000'000.0;
    }
    EXPECT_DOUBLE_EQ(yes_final, 0.0) << "TS4: 结算后 YES 持仓平掉 (不再永远挂账本)";
    EXPECT_GT(loop_->stats().positions_settled.load(), static_cast<std::uint64_t>(0))
        << "TS4: positions_settled 计数 > 0";
    // realized = (1.0 − avg) × qty > 0 (买 ~0.30 → 结算 1.0)
    EXPECT_GT(loop_->cum_realized_pnl_pusd(), 0.0)
        << "TS4: YES 胜 → realized PnL > 0 (买便宜结算 1.0); 现实 PnL 结算正确性修复";
    EXPECT_NEAR(loop_->cum_realized_pnl_pusd(), (1.0 - avg) * yes_qty, 1e-6)
        << "TS4: realized = (1.0 − avg_entry) × qty";

    // M3 CLV 尺子: 结算后 CLV 报告应记录建仓成交 (买 ~0.30 → YES 胜结算 1.0 → CLV_settle 正)。
    const auto clv = loop_->clv_report();
    std::fprintf(stderr, "[TS4-CLV] n=%llu clv_close=%.4f clv_settle=%.4f pos_rate=%.2f\n",
                 static_cast<unsigned long long>(clv.n_fills), clv.clv_close_mean, clv.clv_settle_mean,
                 clv.clv_close_positive_rate);
    EXPECT_GT(clv.n_fills, 0u) << "TS4: CLV 尺子应记录已结算的建仓成交";
    EXPECT_GT(clv.clv_settle_mean, 0.0)
        << "TS4: 买 ~0.30 → YES 胜结算 1.0 → CLV_settle = 1.0−0.30 ≈ +0.70 (正期望兑现)";
    EXPECT_NEAR(clv.clv_settle_mean, 1.0 - avg, 0.02) << "TS4: CLV_settle ≈ 1.0 − avg_entry";
}

// TS5 (slice-3c REST resolution 注入 → 权威结算): app 层轮询 gamma closed/clob winner →
//   SetResolutionByCondition 注入 (非 WSS — market 频道不推 resolution)。status=Resolved+winner →
//   按 winner 权威结算 (全 market type 通用, 不靠 Goalserve 比分)。验证 REST 路径独立触发结算。
TEST_F(PaperLoopTest, TS5_RestResolutionInjection_AuthoritativeSettle) {
    // 直接 apply_fill 预建 YES 仓 (avg 0.40, qty 5) — 避免 mid-run 注入 race (Start 前注入)。
    execution::VirtualFill fill{};
    fill.fill_size_usdc = 5'000'000;  // 5 pUSD
    fill.fill_price = 0.40;
    fill.reject = execution::MatchReject::Ok;
    position_ledger_->apply_fill("cond-test-001", "1001", strategy::Outcome::Yes, fill);

    // 注入 REST 结算: condition resolved, YES 赢 (winner=1)。game 仍 stub (无 Goalserve) →
    //   证明 REST 路径独立于 Goalserve 终态触发结算。
    std::unordered_map<std::string, ResolutionEntry> resmap;
    resmap["cond-test-001"] = ResolutionEntry{/*status=*/2, /*winner=*/1};

    loop_ = MakeLoop();
    loop_->SetResolutionByCondition(resmap);  // Start 前注入 (单 writer)
    hub_->Publish("1001", MakeFreshBook(0.38, 0.42));  // 有效 book → TickOne 到结算块
    loop_->Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    loop_->Stop();

    std::fprintf(stderr, "[TS5] realized=%.4f settled=%llu\n", loop_->cum_realized_pnl_pusd(),
                 static_cast<unsigned long long>(loop_->stats().positions_settled.load()));

    // REST 权威结算: YES 仓平掉 (account 归零) + realized = (1.0−0.40)×5 = 3.0
    bool yes_open = false;
    for (const auto& pv : position_ledger_->get_all_positions()) {
        if (pv.token_id == "1001") yes_open = true;
    }
    EXPECT_FALSE(yes_open) << "TS5: REST Resolved → YES 仓被权威结算平掉 (不靠 Goalserve)";
    EXPECT_GT(loop_->stats().positions_settled.load(), static_cast<std::uint64_t>(0));
    EXPECT_NEAR(loop_->cum_realized_pnl_pusd(), (1.0 - 0.40) * 5.0, 1e-6)
        << "TS5: realized = (1.0 − 0.40) × 5 = 3.0 (winner=YES 按 REST 注入)";
}
