// tests/unit/test_live_order_gate.cpp — LiveOrderGate (ARM 总闸 + 翻译 + sink)
//
// Owner: GM (老雷) 2026-05-31 — Phase 4 D-A; 2026-06-13 随 gate 简化重写。
// 契约: ① 默认 disarmed → sink 零调用 (fail-closed); ② Disarm = kill 立即停发;
//       ③ armed → 翻译正确 + sink 恰好一次。
// §8 (RM 拒单 → sink 零调用) 的 enforcement 点在决策环 (trading_loop 两路径 Execute 前
//   rm_.evaluate, 拒单到不了 executor 缝), 由 RM/trading_loop 测试族覆盖, 不在本文件。
#include "stcpp/polymarket/live_order_gate.hpp"

#include <gtest/gtest.h>

#include <string>

#include "stcpp/infra/wal/pit.hpp"
#include "stcpp/polymarket/live_executor.hpp"

namespace stcpp::polymarket::test {

using stcpp::risk::Outcome;
using stcpp::risk::Side;

constexpr std::int64_t NS_PER_MS = 1'000'000LL;
constexpr const char* kTokenId = "1234567890";
constexpr const char* kConditionId = "0xa9db600590209698097db2fb8382989ea1cf6a9b91f0428b2e1d4f35d724c3ff";

class LiveOrderGateTest : public ::testing::Test {
protected:
    stcpp::risk::OrderIntent make_ok_intent(std::string sig = "sig_gate") {
        auto const now = ::stcpp::infra::wal::pit::NowRealtimeNs();
        stcpp::risk::OrderIntent it;
        it.event_ts_ns = now - 500 * NS_PER_MS;
        it.data_source_ts_ns = now - 400 * NS_PER_MS;
        it.ingestion_ts_ns = now - 100 * NS_PER_MS;
        it.as_of_ts_ns = now - 10 * NS_PER_MS;
        it.condition_id = kConditionId;
        it.token_id = kTokenId;
        it.outcome = Outcome::Yes;
        it.side = Side::Buy;
        it.strategy_id = "strat_a";
        it.signal_id = std::move(sig);
        it.feature_snapshot_id = "fs_01H";
        it.price = 0.50;
        it.size_pUSD_micro = 1'000;
        it.book_depth_l1_usdc = 5'000;
        it.book_snapshot_ts_ns = now - 200 * NS_PER_MS;
        it.tick_size = 0.01;
        it.is_close = false;
        it.timestamp_ms = now / NS_PER_MS;
        return it;
    }

    // 计数 stub sink。
    OrderSinkFn make_counting_sink() {
        return [this](const LiveOrderRequest& req) -> LiveOrderResult {
            ++sink_calls_;
            last_req_ = req;
            LiveOrderResult r;
            r.success = true;
            r.http_status = 200;
            r.order_id = "0xstub";
            r.status = "matched";
            return r;
        };
    }

    int sink_calls_{0};
    LiveOrderRequest last_req_;
};

// 翻译: BUY $1 @ 0.5 → maker=1e6(USDC), taker=2e6(shares)。
TEST(LiveOrderGateTranslate, BuyAmounts) {
    stcpp::risk::OrderIntent it;
    it.token_id = "999";
    it.side = Side::Buy;
    it.price = 0.5;
    it.size_pUSD_micro = 1'000'000;  // $1
    const auto req = TranslateIntent(it, /*neg_risk=*/false);
    EXPECT_TRUE(req.is_buy);
    EXPECT_EQ(req.token_id, "999");
    EXPECT_EQ(req.maker_amount, 1'000'000u);  // USDC
    EXPECT_EQ(req.taker_amount, 2'000'000u);  // 2 shares
    EXPECT_EQ(req.order_type, "FOK");
}

// 翻译: SELL $1 @ 0.5 → maker=2e6(shares), taker=1e6(USDC); neg_risk 透传。
TEST(LiveOrderGateTranslate, SellAmountsAndNegRisk) {
    stcpp::risk::OrderIntent it;
    it.side = Side::Sell;
    it.price = 0.5;
    it.size_pUSD_micro = 1'000'000;
    const auto req = TranslateIntent(it, /*neg_risk=*/true);
    EXPECT_FALSE(req.is_buy);
    EXPECT_EQ(req.maker_amount, 2'000'000u);  // shares
    EXPECT_EQ(req.taker_amount, 1'000'000u);  // USDC
    EXPECT_TRUE(req.neg_risk);
}

// CLOB 精度: SELL odd-lot (6 位小数持仓) 必须圆整 — shares 钉 2 位(10'000 倍数), USDC 钉 4 位(100 倍数)。
//   未圆整会被 CLOB 400 拒 ("maker max 2 decimals, taker max 4 decimals", 2026-06-12 都柏林实测)。
TEST(LiveOrderGateTranslate, SellOddLotPrecisionFloored) {
    stcpp::risk::OrderIntent it;
    it.side = Side::Sell;
    it.price = 0.19;
    it.size_pUSD_micro = 1'050'000;  // → shares≈5.526316 (6 位小数)
    const auto req = TranslateIntent(it, false);
    EXPECT_EQ(req.maker_amount % 10'000u, 0u) << "卖出 shares 须 2 位小数 (10'000 micro 倍数)";
    EXPECT_EQ(req.taker_amount % 100u, 0u) << "USDC 须 4 位小数 (100 micro 倍数)";
    EXPECT_EQ(req.maker_amount, 5'520'000u);  // 5.526.. → 向下钉 5.52 股 (不超卖)
    EXPECT_LE(req.maker_amount, 5'526'316u);  // 绝不超过原始持仓 (防超卖)
}

// BUY 精度【与 SELL 相反】(2026-06-13 链路探针实测 CLOB 400 坐实): market BUY 的 maker(USDC) ≤ 2 位小数、
//   taker(shares) ≤ 5 位; 旧实现误套 SELL 规则 (USDC 4 位) → 便宜/odd 价 BUY 几乎全被 400 拒 (Alan 真钱事故根因)。
TEST(LiveOrderGateTranslate, BuyPrecisionSatisfied) {
    stcpp::risk::OrderIntent it;
    it.side = Side::Buy;
    it.price = 0.07;
    it.size_pUSD_micro = 2'000'000;  // $2 @ 0.07 → 28.57.. 股
    const auto req = TranslateIntent(it, false);
    EXPECT_EQ(req.maker_amount % 10'000u, 0u) << "market BUY: maker(USDC) 须 ≤2 位小数 (10'000 micro 倍数)";
    EXPECT_EQ(req.taker_amount % 10u, 0u) << "market BUY: taker(shares) 须 ≤5 位小数 (10 micro 倍数)";
    EXPECT_GE(req.maker_amount, 1'000'000u) << "≥$1 名义门";
    // 隐含限价 = maker/taker ≥ intent.price → marketable (向下圆整 taker → 限价不低于 ask)。
    const double implied = static_cast<double>(req.maker_amount) / static_cast<double>(req.taker_amount);
    EXPECT_GE(implied, it.price - 1e-9) << "BUY 隐含限价须 ≥ ask (marketable)";
}

// 回归 (Alan 真钱事故): 便宜 token 凑不到 $1 的 marketable BUY → maker 钉到 $1 (而非被 400 拒)。
//   旧实现: 5 股 × 0.18 = $0.90 < $1 → CLOB "min size: 1" 400 → 不入账 → cap 瞎 → 同秒重试风暴。
TEST(LiveOrderGateTranslate, BuyCheapTokenFloorsToOneDollar) {
    stcpp::risk::OrderIntent it;
    it.side = Side::Buy;
    it.price = 0.18;
    it.size_pUSD_micro = 900'000;  // $0.90 (≈5 股) < $1
    const auto req = TranslateIntent(it, false);
    EXPECT_GE(req.maker_amount, 1'000'000u) << "便宜 BUY 必须凑够 $1 名义门 (否则 CLOB 400)";
    EXPECT_EQ(req.maker_amount % 10'000u, 0u) << "maker(USDC) ≤2 位";
    EXPECT_EQ(req.taker_amount % 10u, 0u) << "taker(shares) ≤5 位";
    EXPECT_GT(req.taker_amount, 5'000'000u) << "$1 @ 0.18 → >5 股";
}

// fail-closed: 默认未开闸 → sink 零调用。
TEST_F(LiveOrderGateTest, DisarmedBlocks) {
    LiveOrderGate gate(make_counting_sink());
    EXPECT_FALSE(gate.Armed());
    const auto gr = gate.Submit(make_ok_intent(), false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_EQ(gr.gate_block, GateBlock::DISARMED);
    EXPECT_EQ(sink_calls_, 0);
}

// Disarm = kill: 开闸成功一笔后 Disarm → 立即停发。
TEST_F(LiveOrderGateTest, DisarmKillsSubmit) {
    LiveOrderGate gate(make_counting_sink());
    gate.Arm();
    EXPECT_TRUE(gate.Submit(make_ok_intent("s1"), false).submitted);
    gate.Disarm();
    const auto gr = gate.Submit(make_ok_intent("s2"), false);
    EXPECT_FALSE(gr.submitted);
    EXPECT_EQ(gr.gate_block, GateBlock::DISARMED);
    EXPECT_EQ(sink_calls_, 1);
}

// armed → sink 恰好调用一次, 且请求已正确翻译。
TEST_F(LiveOrderGateTest, ArmedSubmitsOnce) {
    LiveOrderGate gate(make_counting_sink());
    gate.Arm();
    const auto gr = gate.Submit(make_ok_intent(), false);
    EXPECT_TRUE(gr.submitted);
    EXPECT_EQ(gr.gate_block, GateBlock::NONE);
    EXPECT_EQ(sink_calls_, 1);
    EXPECT_EQ(last_req_.token_id, kTokenId);
    EXPECT_TRUE(last_req_.is_buy);
    EXPECT_TRUE(gr.order.success);
}

// 2026-06-13 硬化: LiveExecutor 无条件回填 CLOB 回执 + 标记硬拒(4xx) — 此前丢弃 → daemon 把硬拒当可重试 miss → 风暴。
TEST_F(LiveOrderGateTest, ExecReportSurfacesHardReject) {
    LiveOrderGate gate([](const LiveOrderRequest&) -> LiveOrderResult {
        LiveOrderResult r;
        r.success = false;
        r.http_status = 400;
        r.error = "invalid amount for a marketable BUY order ($0.999), min size: 1";
        return r;
    });
    gate.Arm();
    LiveExecutor exec(gate);
    const ExecReport rep = exec.Execute(make_ok_intent(), false);
    EXPECT_TRUE(rep.submitted);
    EXPECT_FALSE(rep.filled);
    EXPECT_TRUE(rep.hard_reject) << "4xx 必须标记 hard_reject (→ adapter 出 ClobRejected → loop 冷却, 非重试)";
    EXPECT_EQ(rep.http_status, 400);
    EXPECT_FALSE(rep.clob_error.empty()) << "CLOB error 必须透传 (终结 error 被吞)";
}

// matched → 非硬拒 + filled (回执正常路径)。
TEST_F(LiveOrderGateTest, ExecReportMatchedNotHardReject) {
    LiveOrderGate gate(make_counting_sink());  // 返 matched/200
    gate.Arm();
    LiveExecutor exec(gate);
    const ExecReport rep = exec.Execute(make_ok_intent(), false);
    EXPECT_TRUE(rep.filled);
    EXPECT_FALSE(rep.hard_reject);
    EXPECT_EQ(rep.http_status, 200);
}

}  // namespace stcpp::polymarket::test
