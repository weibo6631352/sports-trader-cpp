// tests/unit/test_position_ledger_w76.cpp — Wave 76 4 ctest (老沈 W9 W4)
//
// 落: laohan-w9-w3-position-ledger-rest-api-spec-v1.md §5
//
// 测试矩阵 (老韩 spec §5):
//   TC-01: 快照一致性 — get_all_positions() + get_position() 返回正确值 (read 不锁 hot path)
//   TC-02: CAS 幂等 — try_transition RUNNING→DRAIN 2 次: 1 成 1 fail
//   TC-03: DRAIN 平仓放行 — is_close=true + side=Sell 通过 check_state_
//   TC-04: reject ring tail_copy — 已 HALTED, 新 intent 拒, audit_id 非零
//
// cite:
//   polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §2.3 §3.3
//   goalserve_ssot_cite:  N/A
//   handshake_cite:       laoli-laoSun-handshake-v1.md §3
//   adr_cite:             ADR-027 Enforce-1

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

// Headers under test
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/position_view.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/risk/system_state.hpp"

// Include cpp for single-TU build (no CMake target for position_ledger yet)
#include "../../src/stcpp/risk/position_ledger.cpp"

namespace stcpp::risk {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// InMemory AuditEmitter for RiskGateway tests
class InMemAudit final : public AuditEmitter {
public:
    bool emit(AuditRecord const& /*rec*/) noexcept override { return true; }
};

// Build a minimal valid OrderIntent (R-20 4 ts, token_id, condition_id, side)
static OrderIntent make_valid_intent(std::string const& signal_id, bool is_close = false,
                                     Side side = Side::Buy) {
    OrderIntent it{};
    // R-20: event_ts <= data_source_ts <= ingestion_ts <= as_of_ts
    it.event_ts_ns = 1'000'000'000LL;
    it.data_source_ts_ns = 1'000'000'001LL;
    it.ingestion_ts_ns = 1'000'000'002LL;
    it.as_of_ts_ns = 1'000'000'003LL;
    it.condition_id = "0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890ab";
    it.token_id = "123456789012345678901234567890";
    it.outcome = Outcome::Yes;
    it.side = side;
    it.strategy_id = "S1";
    it.signal_id = signal_id;
    it.feature_snapshot_id = "fs-001";
    it.price = 0.55;
    it.size_pUSD_micro = 100;
    it.book_depth_l1_usdc = 500.0;
    it.book_snapshot_ts_ns = 999'999'999LL;  // recent (< 60s before as_of)
    it.tick_size = 0.01;
    it.is_close = is_close;
    return it;
}

// Build a VirtualFill (MatchReject::Ok, fill_shares_micro > 0)
static execution::VirtualFill make_fill(std::int64_t as_of_ts_ns, double fill_price = 0.55,
                                        double fill_shares_micro = 100.0) {
    execution::VirtualFill f{};
    f.reject = execution::MatchReject::Ok;
    f.fill_price = fill_price;
    f.fill_shares_micro = stcpp::domain::to_micro_pusd(fill_shares_micro);  // A1 whole→micro
    f.expected_fill_rate = 0.60;
    f.bernoulli_draw = true;
    // R-20: as_of_ts_ns 严格透传
    f.event_ts_ns = as_of_ts_ns - 3;
    f.data_source_ts_ns = as_of_ts_ns - 2;
    f.ingestion_ts_ns = as_of_ts_ns - 1;
    f.as_of_ts_ns = as_of_ts_ns;
    f.fill_ts_ns = as_of_ts_ns + 1;
    return f;
}

// ---------------------------------------------------------------------------
// TC-01: 快照一致性 (read 不锁 hot path)
// ---------------------------------------------------------------------------
TEST(PositionLedgerW76, TC01_SnapshotConsistency) {
    PositionLedger ledger;

    // 初始状态: 无仓位
    EXPECT_TRUE(ledger.get_all_positions().empty());
    EXPECT_FALSE(ledger.get_position("tok-001").has_value());

    std::string const cid = "0xabc123";
    std::string const tid = "101010";

    // 注入一笔成交
    auto fill = make_fill(2'000'000'000LL, 0.6, 200.0);
    ledger.apply_fill(cid, tid, Outcome::Yes, fill);

    // get_position 验证
    auto pv = ledger.get_position(tid);
    ASSERT_TRUE(pv.has_value());
    EXPECT_EQ(pv->condition_id, cid);
    EXPECT_EQ(pv->token_id, tid);
    EXPECT_EQ(pv->outcome, Outcome::Yes);
    EXPECT_EQ(pv->net_shares_micro, 200'000'000LL);
    EXPECT_NEAR(pv->avg_entry_price, 0.6, 1e-9);

    // R-20: last_update_ts == as_of_ts_ns (禁 now())
    EXPECT_EQ(pv->last_update_ts, 2'000'000'000LL);

    // get_all_positions 验证
    auto all = ledger.get_all_positions();
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].token_id, tid);
    EXPECT_EQ(all[0].net_shares_micro, 200'000'000LL);

    // per_outcome_exposure
    auto outcome_exp = ledger.get_per_outcome_exposure();
    ASSERT_EQ(outcome_exp.count(tid), 1u);
    EXPECT_EQ(outcome_exp.at(tid), 200'000'000LL);

    // per_condition_exposure
    auto cond_exp = ledger.get_per_condition_exposure();
    ASSERT_EQ(cond_exp.count(cid), 1u);
    EXPECT_EQ(cond_exp.at(cid), 200'000'000LL);

    // 再注入一笔 (VWAP 测试)
    auto fill2 = make_fill(2'000'000'001LL, 0.4, 100.0);
    ledger.apply_fill(cid, tid, Outcome::Yes, fill2);

    auto pv2 = ledger.get_position(tid);
    ASSERT_TRUE(pv2.has_value());
    EXPECT_EQ(pv2->net_shares_micro, 300'000'000LL);
    // VWAP: (200 * 0.6 + 100 * 0.4) / 300 = (120 + 40) / 300 = 0.5333...
    EXPECT_NEAR(pv2->avg_entry_price, 160.0 / 300.0, 1e-9);
    EXPECT_EQ(pv2->last_update_ts, 2'000'000'001LL);
}

// ---------------------------------------------------------------------------
// TC-02: CAS 幂等 — try_transition RUNNING→DRAIN 2 次: 1 成 1 fail
// ---------------------------------------------------------------------------
TEST(SystemStateW76, TC02_CASIdempotent) {
    StateMachine sm(SystemState::RUNNING);

    // 初始态
    EXPECT_EQ(sm.get(), SystemState::RUNNING);

    // 第一次: RUNNING → DRAIN (成功)
    bool first = sm.try_transition(SystemState::RUNNING, SystemState::DRAIN);
    EXPECT_TRUE(first);
    EXPECT_EQ(sm.get(), SystemState::DRAIN);

    // 第二次: RUNNING → DRAIN (失败: current 已是 DRAIN, 不是 RUNNING)
    bool second = sm.try_transition(SystemState::RUNNING, SystemState::DRAIN);
    EXPECT_FALSE(second);
    EXPECT_EQ(sm.get(), SystemState::DRAIN);  // 不变

    // DECAYED 吸收态: 任何 try_transition(DECAYED, *) 失败
    StateMachine sm2(SystemState::DECAYED);
    EXPECT_FALSE(sm2.try_transition(SystemState::DECAYED, SystemState::RUNNING));
    EXPECT_EQ(sm2.get(), SystemState::DECAYED);

    // 合法迁移: DRAIN → RUNNING (resume)
    bool resume = sm.try_transition(SystemState::DRAIN, SystemState::RUNNING);
    EXPECT_TRUE(resume);
    EXPECT_EQ(sm.get(), SystemState::RUNNING);
}

// ---------------------------------------------------------------------------
// TC-03: DRAIN 平仓放行 (is_close=true + side=Sell) — check_state_ 逻辑验证
// ---------------------------------------------------------------------------
TEST(SystemStateW76, TC03_DrainCloseAllowed) {
    // 使用 RiskGateway 的 DRAIN state 验证放行条件
    // (DRAIN: is_close=true + side=Sell 放行; 其余 reject STATE_DRAIN)
    RiskConfig cfg;
    cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(100'000);
    cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(500'000);
    cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(250'000);
    cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000'000.0);      // c2b
    cfg.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_pusd(50'000.0);  // c2b
    cfg.consec_loss_halt_count = 100;
    cfg.edge_ci_lower_floor = 0.0;

    auto emitter = std::make_shared<InMemAudit>();
    RiskGateway gw(cfg, emitter);
    gw.set_state(RmState::RUNNING);
    gw.set_bankroll(1'000'000LL * 1'000'000LL);  // c2b 对齐 micro

    // (a) DRAIN + is_close=true + Sell → state check 放行 (后续可能因 4ts 被拒, 但 state 关通过)
    gw.set_state(RmState::DRAIN);
    {
        auto intent = make_valid_intent("sig-drain-close", true, Side::Sell);
        // state check DRAIN+is_close=Sell → 通过; 后续 invalid_intent 4ts 可能拒
        // 我们只验证 reject != STATE_DRAIN
        auto d = gw.evaluate(intent);
        EXPECT_NE(d.reject, RejectCode::STATE_DRAIN)
            << "DRAIN + is_close=true + Sell should pass state check";
    }

    // (b) DRAIN + is_close=false → STATE_DRAIN reject
    {
        auto intent = make_valid_intent("sig-drain-open", false, Side::Buy);
        auto d = gw.evaluate(intent);
        EXPECT_TRUE(d.is_rejected());
        EXPECT_EQ(d.reject, RejectCode::STATE_DRAIN);
    }

    // (c) DRAIN + is_close=true + Buy → STATE_DRAIN reject (side 不对)
    {
        auto intent = make_valid_intent("sig-drain-buy-close", true, Side::Buy);
        auto d = gw.evaluate(intent);
        EXPECT_TRUE(d.is_rejected());
        EXPECT_EQ(d.reject, RejectCode::STATE_DRAIN);
    }

    // (d) HALTED → 全 reject STATE_HALTED
    gw.set_state(RmState::HALTED);
    {
        auto intent = make_valid_intent("sig-halted", true, Side::Sell);
        auto d = gw.evaluate(intent);
        EXPECT_TRUE(d.is_rejected());
        EXPECT_EQ(d.reject, RejectCode::STATE_HALTED);
    }
}

// ---------------------------------------------------------------------------
// TC-04: reject ring tail_copy — HALTED 状态, audit_id 非零
// ---------------------------------------------------------------------------
TEST(SystemStateW76, TC04_RejectRingTailCopy) {
    RiskConfig cfg;
    cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000'000.0);  // c2b
    cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(100'000);
    cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(500'000);
    cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(250'000);
    cfg.daily_loss_halt_usdc = stcpp::domain::MicroPUSD::from_pusd(50'000.0);  // c2b
    cfg.consec_loss_halt_count = 100;
    cfg.edge_ci_lower_floor = 0.0;

    auto emitter = std::make_shared<InMemAudit>();
    RiskGateway gw(cfg, emitter);
    gw.set_state(RmState::HALTED);

    // 发送 3 笔 intent, 全部应被 STATE_HALTED 拒
    for (int i = 0; i < 3; ++i) {
        auto intent = make_valid_intent("sig-halted-" + std::to_string(i));
        auto d = gw.evaluate(intent);

        // 每笔 reject
        EXPECT_TRUE(d.is_rejected()) << "intent " << i << " should be rejected";
        EXPECT_EQ(d.reject, RejectCode::STATE_HALTED) << "intent " << i;

        // audit_id 非零 (R-1: 非空, 即使 APPROVED; REJECTED 同样要求)
        bool any_nonzero = false;
        for (auto b : d.audit_id) {
            if (b != 0) {
                any_nonzero = true;
                break;
            }
        }
        EXPECT_TRUE(any_nonzero) << "audit_id must be non-zero for intent " << i;

        // decision_ts_ns 非零
        EXPECT_GT(d.decision_ts_ns, 0LL) << "decision_ts_ns must be set for intent " << i;
    }

    // StateMachine: HALTED → DECAYED CAS
    StateMachine sm(SystemState::HALTED);
    EXPECT_TRUE(sm.try_transition(SystemState::HALTED, SystemState::DECAYED));
    EXPECT_EQ(sm.get(), SystemState::DECAYED);
    // DECAYED 不可逆
    EXPECT_FALSE(sm.try_transition(SystemState::DECAYED, SystemState::RUNNING));
}

// ===========================================================================
// per-engine 分仓加性追踪 (2026-06-12 Option A): 各引擎独立份 + 聚合不变 + 全平清份 + 快照往返
// ===========================================================================
static FillEvent ev_micro(std::int64_t delta_micro, double price, std::int64_t ts) {
    FillEvent e;
    e.filled_size_micro = delta_micro;  // signed (负=卖)
    e.fill_price = price;
    e.mode_tag = 0;  // R-11 paper
    e.event_ts_ns = ts - 3;
    e.data_source_ts_ns = ts - 2;
    e.ingestion_ts_ns = ts - 1;
    e.as_of_ts_ns = ts;
    return e;
}

TEST(PositionLedgerEngine, TwoEnginesSameTokenSplit) {
    PositionLedger L;
    const std::string cid = "0xc", tok = "900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(100'000'000, 0.5, 1000), "sharp");
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(50'000'000, 0.6, 1001), "engB");
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 150'000'000);  // 聚合 = 和
    EXPECT_EQ(L.get_engine_position_size(tok, "sharp"), 100'000'000);
    EXPECT_EQ(L.get_engine_position_size(tok, "engB"), 50'000'000);
    EXPECT_EQ(L.get_engine_position_size(tok, "nope"), 0);
    auto ce = L.get_per_condition_engine_exposure();
    EXPECT_EQ(ce.at(cid + '\x1f' + "sharp"), 100'000'000);
    EXPECT_EQ(ce.at(cid + '\x1f' + "engB"), 50'000'000);
    // 直查单值访问器 (热路径用) 与整表一致
    EXPECT_EQ(L.get_engine_condition_exposure(cid, "sharp"), 100'000'000);
    EXPECT_EQ(L.get_engine_condition_exposure(cid, "engB"), 50'000'000);
    EXPECT_EQ(L.get_engine_condition_exposure(cid, "nope"), 0);
    EXPECT_EQ(L.get_engine_condition_exposure("0xother", "sharp"), 0);
}

TEST(PositionLedgerEngine, SharpSellOnlyReducesSharpShare) {
    PositionLedger L;
    const std::string cid = "0xc", tok = "900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(100'000'000, 0.5, 1000), "sharp");
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(50'000'000, 0.6, 1001), "engB");
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(-40'000'000, 0.55, 1002), "sharp");  // sharp 卖 40
    EXPECT_EQ(L.get_engine_position_size(tok, "sharp"), 60'000'000);  // 只减 sharp
    EXPECT_EQ(L.get_engine_position_size(tok, "engB"), 50'000'000);    // engB 不动
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 110'000'000);           // 聚合 110
}

TEST(PositionLedgerEngine, FullCloseClearsAllEngineSplits) {
    PositionLedger L;
    const std::string cid = "0xc", tok = "900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(100'000'000, 0.5, 1000), "sharp");
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(50'000'000, 0.6, 1001), "engB");
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(-150'000'000, 1.0, 1002), "sharp");  // 结算全平
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 0);  // 聚合归零
    EXPECT_EQ(L.get_engine_position_size(tok, "sharp"), 0);  // 全部引擎份清空
    EXPECT_EQ(L.get_engine_position_size(tok, "engB"), 0);
    EXPECT_TRUE(L.get_per_condition_engine_exposure().empty());
}

TEST(PositionLedgerEngine, EmptyEngineNoSplitTracking) {
    PositionLedger L;
    const std::string cid = "0xc", tok = "900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(100'000'000, 0.5, 1000));  // engine 空 → 不追踪
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 100'000'000);  // 聚合正常
    EXPECT_EQ(L.get_engine_position_size(tok, "sharp"), 0);  // 无 split
    EXPECT_TRUE(L.get_engine_pos_snapshot().empty());
}

// ---------------------------------------------------------------------------
// R-11 双向隔离 (2026-06-13 修正): 账本按运行模式收成交 — paper 实例收 mode_tag 0 / live 实例收 1。
//   旧实现写死「只收 0」→ live 成交全丢, 持仓永不入账 (Astros 单真钱事故)。
// ---------------------------------------------------------------------------
static FillEvent ev_tag(std::int64_t delta_micro, double price, std::int64_t ts, std::uint8_t mode_tag) {
    FillEvent e = ev_micro(delta_micro, price, ts);
    e.mode_tag = mode_tag;
    return e;
}

TEST(PositionLedgerModeTag, DefaultLedgerAcceptsPaperRejectsLive) {
    PositionLedger L;  // 默认 accepted_mode_tag=0 (paper)
    EXPECT_EQ(L.accepted_mode_tag(), 0);
    const std::string cid = "0xc", tok = "900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_tag(100'000'000, 0.5, 1000, /*paper*/ 0), "sharp");
    ASSERT_TRUE(L.get_position(tok).has_value());
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 100'000'000);
    // live 成交 (mode_tag=1) 投到 paper 账本 → 拒, 仓位不变 (R-11 方向)
    L.apply_fill(cid, tok, Outcome::Yes, ev_tag(50'000'000, 0.6, 1001, /*live*/ 1), "sharp");
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 100'000'000) << "paper 账本必须拒 live 成交";
}

TEST(PositionLedgerModeTag, LiveLedgerAcceptsLiveRejectsPaper) {
    PositionLedger L(/*accepted_mode_tag=*/1);  // live 实例
    EXPECT_EQ(L.accepted_mode_tag(), 1);
    const std::string cid = "0xc", tok = "900";
    // live 成交 (mode_tag=1) → 入账 (这正是旧实现丢弃、真钱事故的那笔)
    L.apply_fill(cid, tok, Outcome::Yes, ev_tag(100'000'000, 0.8, 1000, /*live*/ 1), "sharp");
    ASSERT_TRUE(L.get_position(tok).has_value()) << "live 账本必须收 live 成交 (修复核心)";
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 100'000'000);
    EXPECT_DOUBLE_EQ(L.get_position(tok)->avg_entry_price, 0.8);
    // paper 成交 (mode_tag=0) 投到 live 账本 → 拒 (R-11 反向也隔离)
    L.apply_fill(cid, tok, Outcome::Yes, ev_tag(50'000'000, 0.5, 1001, /*paper*/ 0), "sharp");
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 100'000'000) << "live 账本必须拒 paper 成交";
}

// 2026-06-13 真钱风暴根因复现: apply_fill 落账后, FeedRiskGateway 喂 cap 的两个敞口源
//   (get_per_condition_exposure / get_per_outcome_exposure) 必须也反映该成交; 否则 cap 被喂 0 → 瞎 →
//   同 token 重发风暴 ($129 超 per_outcome cap $12 的 5 倍, armed 真钱事故)。
TEST(PositionLedgerModeTag, LiveFillUpdatesCapExposureGetters) {
    PositionLedger L(/*accepted_mode_tag=*/1);  // live 实例
    const std::string cid = "0xcond1", tok = "tok900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_tag(100'000'000, 0.8, 1000, /*live*/ 1), "sharp");
    ASSERT_TRUE(L.get_position(tok).has_value());
    EXPECT_EQ(L.get_position(tok)->net_shares_micro, 100'000'000);
    // FeedRiskGateway 喂 cap 的源 — 必须反映 live 成交, 否则 cap 瞎:
    auto cexp = L.get_per_condition_exposure();
    auto oexp = L.get_per_outcome_exposure();
    EXPECT_EQ(cexp[cid], 100'000'000) << "get_per_condition_exposure 必须反映 live 成交 (返股数); =0 则瞎";
    EXPECT_EQ(oexp[tok], 100'000'000) << "get_per_outcome_exposure 必须反映 live 成交 (返股数); =0 则瞎";
}

// 2026-06-13 单位根治契约: 持仓本位=股数; cap/sizing 用的 notional getter = 股数 × 均价 (USD 名义)。
//   锁「股→USD」换算: 100 股 @ 0.80 → 名义 80 USD。若 cap 直接拿股数当 USD (旧 bug), 价≠1 时单位错→风暴。
TEST(PositionLedgerModeTag, NotionalGettersDeriveUsdFromShares) {
    PositionLedger L(/*accepted_mode_tag=*/1);
    const std::string cid = "0xcondN", tok = "tokN";
    L.apply_fill(cid, tok, Outcome::Yes, ev_tag(100'000'000, 0.80, 1000, /*live*/ 1), "sharp");
    // exposure getter = 股数 (本位); notional getter = 股数 × 均价 = USD 名义。
    EXPECT_EQ(L.get_per_outcome_exposure()[tok], 100'000'000) << "exposure=股数(100 股)";
    EXPECT_EQ(L.get_per_outcome_notional()[tok], 80'000'000) << "notional=USD(100股×0.80=80)";
    EXPECT_EQ(L.get_per_condition_notional()[cid], 80'000'000) << "condition notional=USD";
    EXPECT_EQ(L.get_engine_condition_notional(cid, "sharp"), 80'000'000) << "engine condition notional=USD";
    EXPECT_EQ(L.get_engine_position_notional(tok, "sharp"), 80'000'000) << "engine position notional=USD";
    EXPECT_EQ(L.get_engine_position_size(tok, "sharp"), 100'000'000) << "engine position size=股数";
}

TEST(PositionLedgerEngine, SnapshotRoundtrip) {
    PositionLedger L;
    const std::string cid = "0xc", tok = "900";
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(100'000'000, 0.5, 1000), "sharp");
    L.apply_fill(cid, tok, Outcome::Yes, ev_micro(50'000'000, 0.6, 1001), "engB");
    EXPECT_EQ(L.get_engine_pos_snapshot().size(), 2u);
    // 恢复: 聚合 P 行 (engine 空) + PE split (restore_engine_split)
    PositionLedger L2;
    L2.apply_fill(cid, tok, Outcome::Yes, ev_micro(150'000'000, 0.533, 1000));  // 聚合恢复
    L2.restore_engine_split(tok, "sharp", 100'000'000);
    L2.restore_engine_split(tok, "engB", 50'000'000);
    EXPECT_EQ(L2.get_engine_position_size(tok, "sharp"), 100'000'000);
    EXPECT_EQ(L2.get_engine_position_size(tok, "engB"), 50'000'000);
    EXPECT_EQ(L2.get_position(tok)->net_shares_micro, 150'000'000);
}

}  // namespace
}  // namespace stcpp::risk
