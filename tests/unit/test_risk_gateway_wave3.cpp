// tests/unit/test_risk_gateway_wave3.cpp — RiskGateway Wave 3 缺口单测 (老沈 Wave 3 P0)
//
// 覆盖 5 个 delta (按 laoshen-rm-v0.5-field-freeze-spec-v1.md §6):
//   D1: DD -3% 软熔断 / -5% 硬 kill 分层 (GM §9 裁决 #1)
//   D2+D3: timestamp_ms V2 TS_V2_MISSING/STALE/FUTURE + bytes32 INVALID_BYTES32_FORMAT
//   D4: check_signal_ fee estimate net_edge 校验 (R-fee-2)
//   D5: emit_audit_ 透传 timestamp_ms/metadata/builder → AuditRecord v1.4
//
// enum 编号注: TS_V2_MISSING=13 / TS_V2_STALE=14 / TS_V2_FUTURE=15 / INVALID_BYTES32_FORMAT=16
//   这些编号在 reject_enum.hpp 中已与 laohan spec §7.2 对齐 (Wave 3 标注待老郭架构评审 ack)
//   测试此处仅校验语义行为, 不硬编码数值 (避免 enum 值 ABI 变更后 double-failure)
//
// cite:
//   laoshen-rm-v0.5-field-freeze-spec-v1.md §2.3.2/§2.3.3/§5.3
//   laohan-rm-v0.5-integration-spec-v1.md §3/§4/§7
//   laolei-2026-drive-directive-paper-profit-v1.md §9 裁决 #1
//
// 红线:
//   R-1: 每 reject 必 audit_id 非空
//   R-20: timestamp_ms 来自 OrderIntent (RM 不 now() 替代)
//   §5.3: fee estimate 是局部变量, 不写 OrderIntent / SignedOrder / EIP-712

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/risk/risk_gateway.hpp"

namespace stcpp::risk::test {

// ---------- in-memory emitter ------------------------------------------------

class Wave3Emitter : public AuditEmitter {
public:
    [[nodiscard]] bool emit(AuditRecord const& r) noexcept override {
        records_.push_back(r);
        return true;
    }
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] AuditRecord const& back() const { return records_.back(); }
    [[nodiscard]] AuditRecord const& at(std::size_t i) const { return records_.at(i); }

private:
    std::vector<AuditRecord> records_;
};

// ---------- fixture ----------------------------------------------------------

constexpr std::int64_t kNsPerMs = 1'000'000LL;

// 合法 mock 值
constexpr const char* kCid = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";
constexpr const char* kTid = "1234567890";
constexpr const char* kBytes32Zero = "0x0000000000000000000000000000000000000000000000000000000000000000";

class Wave3Test : public ::testing::Test {
protected:
    void SetUp() override {
        emitter_ = std::make_shared<Wave3Emitter>();
        cfg_ = RiskConfig{};
        cfg_.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000);
        cfg_.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(50'000);
        cfg_.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(25'000);
        cfg_.bankroll_usdc = 100'000;
        cfg_.daily_loss_halt_usdc = 0;    // 禁用旧绝对值字段, 强制走 pct 路径
        cfg_.daily_loss_soft_pct = 0.03;  // D1: -3% 软熔断
        cfg_.daily_loss_hard_pct = 0.05;  // D1: -5% 硬 kill
        cfg_.consec_loss_halt_count = 5;
        cfg_.excessive_slippage_bps = 200;
        cfg_.edge_ci_lower_floor = 0.0;
        rm_ = std::make_unique<RiskGateway>(cfg_, emitter_);
        rm_->set_state(RmState::RUNNING);
    }

    // 构造合法 intent (Wave 3 版: timestamp_ms 在窗口内)
    OrderIntent make_ok_intent(std::string sig = "sig_w3_default") {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        OrderIntent it;
        it.event_ts_ns = now - 500 * kNsPerMs;
        it.data_source_ts_ns = now - 400 * kNsPerMs;
        it.ingestion_ts_ns = now - 100 * kNsPerMs;
        it.as_of_ts_ns = now - 10 * kNsPerMs;
        it.condition_id = kCid;
        it.token_id = kTid;
        it.outcome = Outcome::Yes;
        it.side = Side::Buy;
        it.strategy_id = "strat_w3";
        it.signal_id = std::move(sig);
        it.feature_snapshot_id = "fs_w3";
        it.price = 0.50;
        it.size_pUSD_micro = 1'000;
        it.book_depth_l1_usdc = 5'000;
        it.book_snapshot_ts_ns = now - 200 * kNsPerMs;
        it.tick_size = 0.01;
        it.is_close = false;
        // V2 必填: timestamp_ms 在 [now_ms-60s, now_ms+5s] 窗口内
        it.timestamp_ms = now / kNsPerMs;  // 当前 ms, 窗口内
        // metadata/builder 使用 OrderIntent 默认值 (bytes32(0), 合法格式)
        it.metadata = kBytes32Zero;
        it.builder = kBytes32Zero;
        return it;
    }

    void expect_rejected(RiskDecision const& d, RejectCode code) {
        EXPECT_EQ(d.decision, Decision::REJECTED);
        EXPECT_EQ(d.reject, code);
        // R-1: audit_id 非空
        bool any_nonzero = false;
        for (auto b : d.audit_id)
            any_nonzero = any_nonzero || (b != 0);
        EXPECT_TRUE(any_nonzero) << "R-1 violation: audit_id 全 0";
    }

    RiskConfig cfg_;
    std::shared_ptr<Wave3Emitter> emitter_;
    std::unique_ptr<RiskGateway> rm_;
};

// =============================================================================
// D1: DD 分层软熔断 (-3% 软 / -5% 硬)
// cite: laoshen spec §2.3.3 + laolei directive §9 裁决 #1
// =============================================================================

// D1-T1: daily_pnl = 0 → APPROVED (无熔断)
TEST_F(Wave3Test, D1_DD_NoPnlLoss_Approved) {
    rm_->set_daily_pnl(0);
    auto d = rm_->evaluate(make_ok_intent("sig_d1_zero"));
    EXPECT_EQ(d.decision, Decision::APPROVED) << "D1: daily_pnl=0, 无亏损, 不应触发熔断";
}

// D1-T2: daily_pnl 在 (-3%, 0) 之间 → APPROVED (未触发软熔断)
TEST_F(Wave3Test, D1_DD_BelowSoftThreshold_Approved) {
    // bankroll=100000, soft_pct=0.03 → soft_threshold=3000
    // pnl=-2999 (loss=2999 < 3000) → 未触发
    rm_->set_daily_pnl(-2'999);
    auto d = rm_->evaluate(make_ok_intent("sig_d1_below_soft"));
    EXPECT_EQ(d.decision, Decision::APPROVED)
        << "D1: loss 2999 < 3% of bankroll 100000 (3000), 不应触发软熔断";
}

// D1-T3: daily_pnl 精确到 -3% → 软熔断触发, 新开仓拒绝 (is_close=false)
TEST_F(Wave3Test, D1_DD_AtSoftThreshold_OpenRejected) {
    // loss = 3000 = 3% × 100000 → 触发软熔断
    rm_->set_daily_pnl(-3'000);
    auto it = make_ok_intent("sig_d1_soft_open");
    it.is_close = false;  // 新开仓
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::DAILY_LOSS_HALT);
}

// D1-T4: 软熔断状态下, 平仓单放行 (is_close=true)
TEST_F(Wave3Test, D1_DD_SoftThreshold_CloseApproved) {
    // loss = 3500 → 在软熔断区间 [-5%, -3%)
    rm_->set_daily_pnl(-3'500);
    auto it = make_ok_intent("sig_d1_soft_close");
    it.is_close = true;
    it.side = Side::Sell;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED) << "D1: 软熔断区间内, is_close=true 平仓单应放行";
}

// D1-T5: -5% 硬 kill → 拒单 + 状态转 HALTED
TEST_F(Wave3Test, D1_DD_HardKill_HaltedState) {
    // loss = 5000 = 5% × 100000 → 硬 kill
    rm_->set_daily_pnl(-5'000);
    auto it = make_ok_intent("sig_d1_hard_kill");
    it.is_close = false;
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::DAILY_LOSS_HALT);
    // 状态应已迁移到 HALTED
    EXPECT_EQ(rm_->state(), RmState::HALTED) << "D1: -5% 硬 kill 后 RmState 必须转为 HALTED";
}

// D1-T6: 硬 kill 后, 平仓单也被拒 (HALTED 全拒含平仓)
TEST_F(Wave3Test, D1_DD_HardKill_CloseAlsoRejected) {
    rm_->set_daily_pnl(-5'001);  // > 5%
    // 首次评估触发 HALTED 迁移
    auto it_open = make_ok_intent("sig_d1_hard_trigger");
    [[maybe_unused]] auto d_trigger = rm_->evaluate(it_open);  // 触发 HALTED
    EXPECT_EQ(rm_->state(), RmState::HALTED);

    // 平仓单在 HALTED 下也应被拒
    auto it_close = make_ok_intent("sig_d1_hard_close");
    it_close.is_close = true;
    it_close.side = Side::Sell;
    auto d = rm_->evaluate(it_close);
    // 被 STATE_HALTED 拒 (check_state_ 排在首位)
    expect_rejected(d, RejectCode::STATE_HALTED);
}

// D1-T7: 超过 5% (更大亏损) 仍触发硬 kill
TEST_F(Wave3Test, D1_DD_LargeHardKill) {
    // loss = 80000 >> 5%
    rm_->set_daily_pnl(-80'000);
    auto d = rm_->evaluate(make_ok_intent("sig_d1_large"));
    expect_rejected(d, RejectCode::DAILY_LOSS_HALT);
    EXPECT_EQ(rm_->state(), RmState::HALTED);
}

// =============================================================================
// D2+D3: TS_V2 窗口校验 + bytes32 格式校验
// cite: laoshen spec §2.3.4 / laohan spec R3.6/R3.7/R3.8/R3.9/R3.10
// =============================================================================

// D2-T1: timestamp_ms = 0 → TS_V2_MISSING
TEST_F(Wave3Test, D2_TS_V2_MISSING_zero) {
    auto it = make_ok_intent("sig_d2_ts_missing");
    it.timestamp_ms = 0;  // 违反 R3.6
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_V2_MISSING) << "D2: timestamp_ms=0 应拒 TS_V2_MISSING";
}

// D2-T2: timestamp_ms 超过 60s 旧 → TS_V2_STALE
TEST_F(Wave3Test, D2_TS_V2_STALE_old) {
    auto it = make_ok_intent("sig_d2_ts_stale");
    auto const now_ms = ::stcpp::infra::wal::pit::NowRealtimeNs() / kNsPerMs;
    // 61s 前的 timestamp (超过 60s 窗口)
    it.timestamp_ms = now_ms - 61'000LL;  // R3.7: < now_ms - 60_000
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_V2_STALE)
        << "D2: timestamp_ms 比 now 老 61s, 应拒 TS_V2_STALE";
}

// D2-T3: timestamp_ms 在 60s 边界内 → APPROVED (不触发 STALE)
TEST_F(Wave3Test, D2_TS_V2_boundary_59s_approved) {
    auto it = make_ok_intent("sig_d2_ts_59s");
    auto const now_ms = ::stcpp::infra::wal::pit::NowRealtimeNs() / kNsPerMs;
    // 59s 前 (在 60s 窗口内)
    it.timestamp_ms = now_ms - 59'000LL;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED) << "D2: timestamp_ms 在 60s 窗口内 (59s 前), 应 APPROVED";
}

// D2-T4: timestamp_ms 比 now 早超 5s → TS_V2_FUTURE
TEST_F(Wave3Test, D2_TS_V2_FUTURE_6s_ahead) {
    auto it = make_ok_intent("sig_d2_ts_future");
    auto const now_ms = ::stcpp::infra::wal::pit::NowRealtimeNs() / kNsPerMs;
    // 6s 后 (超过 5s 漂移容忍)
    it.timestamp_ms = now_ms + 6'000LL;  // R3.8: > now_ms + 5_000
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_V2_FUTURE)
        << "D2: timestamp_ms 比 now 早 6s, 应拒 TS_V2_FUTURE";
}

// D2-T5: timestamp_ms 比 now 早 4s → APPROVED (在 5s 容忍内)
TEST_F(Wave3Test, D2_TS_V2_FUTURE_4s_approved) {
    auto it = make_ok_intent("sig_d2_ts_4s_future");
    auto const now_ms = ::stcpp::infra::wal::pit::NowRealtimeNs() / kNsPerMs;
    // 4s 后 (在 5s 容忍内)
    it.timestamp_ms = now_ms + 4'000LL;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED) << "D2: timestamp_ms 比 now 早 4s (在 5s 容忍内), 应 APPROVED";
}

// D3-T1: metadata 格式非法 (长度不对) → INVALID_BYTES32_FORMAT
TEST_F(Wave3Test, D3_Bytes32_metadata_wrong_length) {
    auto it = make_ok_intent("sig_d3_meta_len");
    // 只有 64 个字符 (少了 '0x' 前缀)
    it.metadata = "0000000000000000000000000000000000000000000000000000000000000000";
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::INVALID_BYTES32_FORMAT)
        << "D3: metadata 缺 0x 前缀 (64 chars 非 66), 应拒 INVALID_BYTES32_FORMAT";
}

// D3-T2: metadata 含大写 hex → INVALID_BYTES32_FORMAT
TEST_F(Wave3Test, D3_Bytes32_metadata_uppercase_hex) {
    auto it = make_ok_intent("sig_d3_meta_upper");
    // 含大写 F (spec 要求全小写 hex)
    it.metadata = "0x000000000000000000000000000000000000000000000000000000000000000F";
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::INVALID_BYTES32_FORMAT)
        << "D3: metadata 含大写 hex (F), 应拒 INVALID_BYTES32_FORMAT";
}

// D3-T3: builder 格式非法 → INVALID_BYTES32_FORMAT
TEST_F(Wave3Test, D3_Bytes32_builder_invalid) {
    auto it = make_ok_intent("sig_d3_builder_bad");
    it.metadata = kBytes32Zero;  // metadata 合法
    it.builder = "0xGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG";
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::INVALID_BYTES32_FORMAT)
        << "D3: builder 含非法字符 (G), 应拒 INVALID_BYTES32_FORMAT";
}

// D3-T4: metadata = bytes32(0) 合法 → APPROVED
TEST_F(Wave3Test, D3_Bytes32_metadata_zero_approved) {
    auto it = make_ok_intent("sig_d3_zero_ok");
    it.metadata = kBytes32Zero;
    it.builder = kBytes32Zero;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED)
        << "D3: metadata/builder = bytes32(0) 是合法默认值, 应 APPROVED";
}

// D3-T5: 自定义合法 bytes32 hex → APPROVED
TEST_F(Wave3Test, D3_Bytes32_custom_valid_hex) {
    auto it = make_ok_intent("sig_d3_custom");
    it.metadata = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    it.builder = "0xcafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe";
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED) << "D3: 合法小写 bytes32 hex, 应 APPROVED";
}

// =============================================================================
// D4: fee estimate net_edge 校验 (R-fee-2)
// cite: laoshen spec §2.3.2 + §5.3
// 安全约束: fee estimate 是局部变量; kSportsTakerFeeRate=0.03 硬编码不入 RiskConfig
// =============================================================================

// D4-T1: edge_ci_lower 足够大, fee 不侵蚀 net_edge → APPROVED
TEST_F(Wave3Test, D4_Fee_LargeEdge_Approved) {
    // edge_ci_lower=0.10 >> fee_per_unit = 0.03 × 0.5 × 0.5 = 0.0075
    // net_edge = 0.10 - 0.0075 = 0.0925 > 0 → APPROVED
    auto it = make_ok_intent("sig_d4_large_edge");
    rm_->set_edge_ci_lower("sig_d4_large_edge", 0.10);
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED)
        << "D4: edge_ci_lower=0.10 远大于 fee, net_edge > 0, 应 APPROVED";
}

// D4-T2: edge_ci_lower 足够大但 fee 侵蚀后 net_edge ≤ 0 → EDGE_NEGATED_BY_SLIPPAGE
// p=0.5: fee_per_unit = 0.03 × 0.5 × 0.5 = 0.0075
// edge_ci_lower = 0.005 < fee_per_unit 0.0075 → net_edge < 0
TEST_F(Wave3Test, D4_Fee_EdgeErodedByFee_Rejected) {
    auto it = make_ok_intent("sig_d4_fee_erode");
    it.price = 0.5;
    // edge_ci_lower=0.005 > 0 (通过 CI floor), 但 fee=0.03×0.5×0.5=0.0075 > edge → 被 fee 侵蚀
    rm_->set_edge_ci_lower("sig_d4_fee_erode", 0.005);
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::REJECTED) << "D4: edge_ci_lower=0.005 被 fee=0.0075 侵蚀, 应 REJECTED";
    EXPECT_EQ(d.reject, RejectCode::EDGE_NEGATED_BY_SLIPPAGE)
        << "D4: R-fee-2 应 EDGE_NEGATED_BY_SLIPPAGE (spec §2.3.2 复用)";
}

// D4-T3: fee 极小的验证 — 用 p=0.5 + 极大 edge 确保 fee 不是拒绝原因
// 注: p=0.01 时 slippage_bps 可能 > edge_bps (slippage check 先触发),
//     故改用 p=0.5 + edge=0.20 (远大于 fee=0.0075 且大于任何合理 slippage)
TEST_F(Wave3Test, D4_Fee_LargeEdgeOverridesFee_Approved) {
    auto it = make_ok_intent("sig_d4_large_over_fee");
    it.price = 0.5;
    it.book_depth_l1_usdc = 100'000;  // 足够深, slippage 极小
    // fee_per_unit = 0.03 × 0.5 × 0.5 = 0.0075
    // edge=0.20 >> fee, net_edge = 0.20 - 0.0075 = 0.1925 >> 0 → APPROVED
    rm_->set_edge_ci_lower("sig_d4_large_over_fee", 0.20);
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED)
        << "D4: edge=0.20 >> fee=0.0075 (p=0.5), net_edge >> 0, 应 APPROVED";
}

// D4-T4: p = 0.5 (最大 fee 点), edge_ci_lower 精确在 fee 边界 → REJECTED
// fee_per_unit at p=0.5: 0.03 × 0.25 = 0.0075
// edge_ci_lower = 0.0075 exactly → net_edge = 0.0075 - 0.0075 = 0.0 ≤ floor(0.0) → REJECTED
TEST_F(Wave3Test, D4_Fee_ExactBoundary_Rejected) {
    auto it = make_ok_intent("sig_d4_boundary");
    it.price = 0.5;
    rm_->set_edge_ci_lower("sig_d4_boundary", 0.0075);  // = fee_per_unit at p=0.5
    auto d = rm_->evaluate(it);
    // net_edge = 0.0075 - 0.0075 = 0 ≤ edge_ci_lower_floor (0.0) → REJECTED
    EXPECT_EQ(d.decision, Decision::REJECTED) << "D4: net_edge = 0 (= floor 0.0), 应 REJECTED (R-fee-2 边界)";
}

// D4-T5: fee estimate 安全隔离验证 — intent 字段不被 fee 改写
// cite: spec §5.3: fee estimate 是局部变量, 不写入 OrderIntent
TEST_F(Wave3Test, D4_Fee_IsolationCheck) {
    auto it = make_ok_intent("sig_d4_isolation");
    double const original_price = it.price;
    std::int64_t const original_size = it.size_pUSD_micro;
    rm_->set_edge_ci_lower("sig_d4_isolation", 0.10);
    [[maybe_unused]] auto d_iso = rm_->evaluate(it);
    // intent 字段未被修改 (fee 计算隔离)
    EXPECT_DOUBLE_EQ(it.price, original_price) << "D4 isolation: intent.price 不应被 fee 计算改写";
    EXPECT_EQ(it.size_pUSD_micro, original_size)
        << "D4 isolation: intent.size_pUSD_micro 不应被 fee 计算改写";
}

// =============================================================================
// D5: emit_audit_ 透传 timestamp_ms/metadata/builder → AuditRecord v1.4
// cite: laoshen spec §2.3.6 可追溯红线: APPROVED 和 REJECTED 均必须记录三字段
// =============================================================================

// D5-T1: APPROVED 路径 — audit record 含正确 timestamp_ms
TEST_F(Wave3Test, D5_AuditTransparency_Approved_TimestampMs) {
    auto it = make_ok_intent("sig_d5_approved_ts");
    std::int64_t const ts_ms = it.timestamp_ms;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED);
    ASSERT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().timestamp_ms, ts_ms) << "D5: APPROVED 路径 audit record 必须透传 timestamp_ms";
}

// D5-T2: APPROVED 路径 — audit record 含正确 metadata
TEST_F(Wave3Test, D5_AuditTransparency_Approved_Metadata) {
    auto it = make_ok_intent("sig_d5_approved_meta");
    it.metadata = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED);
    ASSERT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().metadata, it.metadata) << "D5: APPROVED 路径 audit record 必须透传 metadata";
}

// D5-T3: APPROVED 路径 — audit record 含正确 builder
TEST_F(Wave3Test, D5_AuditTransparency_Approved_Builder) {
    auto it = make_ok_intent("sig_d5_approved_bldr");
    it.builder = "0xcafebabecafebabecafebabecafebabecafebabecafebabecafebabecafebabe";
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED);
    ASSERT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().builder, it.builder) << "D5: APPROVED 路径 audit record 必须透传 builder";
}

// D5-T4: REJECTED 路径 — audit record 同样透传三字段
// (REJECTED 因 EXCEED_PER_ORDER_CAP, 与 V2 字段无关)
TEST_F(Wave3Test, D5_AuditTransparency_Rejected_V2Fields) {
    auto it = make_ok_intent("sig_d5_rejected");
    it.metadata = "0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
    it.builder = "0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
    std::int64_t const ts_ms = it.timestamp_ms;
    // 触发 EXCEED_PER_ORDER_CAP
    it.size_pUSD_micro = 20'000;  // > per_order_cap 10000
    auto d = rm_->evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_PER_ORDER_CAP);
    ASSERT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().timestamp_ms, ts_ms) << "D5: REJECTED 路径 audit record 必须透传 timestamp_ms";
    EXPECT_EQ(emitter_->back().metadata, it.metadata) << "D5: REJECTED 路径 audit record 必须透传 metadata";
    EXPECT_EQ(emitter_->back().builder, it.builder) << "D5: REJECTED 路径 audit record 必须透传 builder";
}

// D5-T5: bytes32(0) 默认值也正确透传
TEST_F(Wave3Test, D5_AuditTransparency_DefaultZeroValues) {
    auto it = make_ok_intent("sig_d5_zero_vals");
    // 使用默认 bytes32(0) 值
    it.metadata = kBytes32Zero;
    it.builder = kBytes32Zero;
    auto d = rm_->evaluate(it);
    EXPECT_EQ(d.decision, Decision::APPROVED);
    ASSERT_EQ(emitter_->size(), 1u);
    EXPECT_EQ(emitter_->back().metadata, kBytes32Zero) << "D5: bytes32(0) 默认 metadata 应正确透传";
    EXPECT_EQ(emitter_->back().builder, kBytes32Zero) << "D5: bytes32(0) 默认 builder 应正确透传";
}

// =============================================================================
// enum 编号确认 (非强制 ABI 锁; 记录当前值供老郭评审)
// cite: laoshen spec §7.2 表 / reject_enum.hpp Wave 3 注释
// 注意: 这些测试验证"当前"编号状态, 不是强制 freeze
//   TS_V2_MISSING=13 / TS_V2_STALE=14 / TS_V2_FUTURE=15 / INVALID_BYTES32_FORMAT=16
//   待老郭架构评审 ack 后可标注为 ABI-frozen
// =============================================================================

TEST(Wave3EnumValues, TS_V2_enum_current_values) {
    // 记录当前 enum 值 (与 laohan spec §7.2 表对齐)
    // 若老郭评审后改动, 此处会编译失败提示 ABI 变更
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::TS_V2_MISSING), 13)
        << "TS_V2_MISSING 当前值=13; laohan spec §7.2 R3.6";
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::TS_V2_STALE), 14)
        << "TS_V2_STALE 当前值=14; laohan spec §7.2 R3.7";
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::TS_V2_FUTURE), 15)
        << "TS_V2_FUTURE 当前值=15; laohan spec §7.2 R3.8";
    EXPECT_EQ(static_cast<int>(InvalidIntentSubReason::INVALID_BYTES32_FORMAT), 16)
        << "INVALID_BYTES32_FORMAT 当前值=16; laohan spec §7.2 R3.9/R3.10; 从原 14 移至 16";
}

// =============================================================================
// 组合测试: TS_V2 + bytes32 + DD 短路顺序验证
// =============================================================================

// COMBO-T1: TS_V2_MISSING 优先于 DD 软熔断 (check_invalid_intent_ 在 check_position_caps_ 前)
TEST_F(Wave3Test, Combo_TS_V2_MISSING_BeforeDD) {
    rm_->set_daily_pnl(-3'500);  // 在软熔断区间
    auto it = make_ok_intent("sig_combo_ts_before_dd");
    it.timestamp_ms = 0;  // TS_V2_MISSING (step 2 触发)
    it.is_close = false;
    auto d = rm_->evaluate(it);
    // check_invalid_intent_ (step 2) 先于 check_position_caps_ (step 6)
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::TS_V2_MISSING)
        << "Combo: TS_V2_MISSING (step 2) 应优先于 DD 软熔断 (step 6)";
}

// COMBO-T2: metadata 格式错优先于 fee 校验 (均在 check_invalid_intent_ vs check_signal_)
TEST_F(Wave3Test, Combo_Bytes32_BeforeFeeCheck) {
    rm_->set_edge_ci_lower("sig_combo_meta_fee", 0.005);  // 会被 fee 侵蚀
    auto it = make_ok_intent("sig_combo_meta_fee");
    it.metadata = "invalid_not_bytes32";  // 格式错 (step 2 触发)
    auto d = rm_->evaluate(it);
    // check_invalid_intent_ (step 2) 先于 check_signal_ (step 8)
    expect_rejected(d, RejectCode::INVALID_INTENT);
    EXPECT_EQ(d.sub_reason, InvalidIntentSubReason::INVALID_BYTES32_FORMAT)
        << "Combo: INVALID_BYTES32_FORMAT (step 2) 应优先于 fee check (step 8)";
}

}  // namespace stcpp::risk::test
