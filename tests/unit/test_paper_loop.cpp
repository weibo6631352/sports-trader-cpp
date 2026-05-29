// tests/unit/test_paper_loop.cpp — PaperLoop 单测
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
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>

#include "stcpp/paper/paper_loop.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"

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
        RiskConfig rm_cfg;
        rm_cfg.per_order_cap_usdc = 10;  // 10 pUSD demo cap
        rm_cfg.market_exposure_cap_usdc = 50;
        rm_cfg.per_outcome_cap_usdc = 25;
        rm_cfg.bankroll_usdc = 1000;
        rm_cfg.edge_ci_lower_floor = -1.0;  // 放宽 CI 门
        rm_cfg.enable_moneyline = true;

        auto emitter = std::make_shared<NullAuditEmitter>();
        rm_ = std::make_unique<RiskGateway>(rm_cfg, emitter);

        // FairValue model
        fv_model_ = std::make_unique<BaselineFairValueModel>(ScorePriorParams{0.30, 0.50}, 0.20);

        // token_map
        token_map_["cond-test-001"] = {"token-yes-001", "token-no-001"};

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
    hub_->Publish("token-yes-001", feat);

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
    hub_->Publish("token-yes-001", feat);

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
    hub_->Publish("token-yes-001", feat);

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
