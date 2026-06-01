// tests/unit/test_replay_decision_input_provider.cpp — 回测多输入回放 P2 (红线#3 闭合证明)
//
// Owner: 小蒋 (回测) + 老雷 (GM)
// 证明 ReplayDecisionInputProvider 经 SetReplayInputs 注入 → 解锁 book-only 回放做不到的两件事:
//   ① in-play 比分(sharp)回放 → fair 锚 sharp 共识 (book-only 无比分 → fair 走 baseline prior)
//   ② resolution 回放 → 持仓结算 (book-only → positions_settled 恒 0)
//   ③ scores.jsonl ↔ provider 解析 round-trip (ScoreFrameRecorder 落盘格式契约)
//   ④ BuildAt PIT: 取 frame_ts<=ts 最近帧, 不看未来
// 这三条正是 docs/RESEARCH/laolei-backtest-equivalence-spec-v1.md 的 P2 验收机制。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include <gtest/gtest.h>

#include "stcpp/backtest/replay_decision_input_provider.hpp"
#include "stcpp/data/score_frame_recorder.hpp"
#include "stcpp/data/score_snapshot_store.hpp"
#include "stcpp/paper/paper_loop.hpp"
#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/pricing/fair_value_estimator.hpp"
#include "stcpp/risk/fill_event.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/position_ledger.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/risk/rm_debug_snapshot.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"
#include "stcpp/strategy/signal_iface.hpp"

namespace {

using namespace stcpp;
using namespace stcpp::paper;
using namespace stcpp::polymarket::clob_wss;
using namespace stcpp::risk;
using namespace stcpp::sizing;
using namespace stcpp::pricing;
using stcpp::backtest::ReplayDecisionInputProvider;
using stcpp::data::ScoreMap;
using stcpp::debug_api::EventScore;

std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

OrderBookFeatures MakeBook(double bid, double ask, std::int64_t now_ns) {
    OrderBookFeatures f{};
    f.valid = true;
    f.event_ts_ns = now_ns - 3'000'000'000LL;
    f.data_source_ts_ns = now_ns - 2'000'000'000LL;
    f.ingestion_ts_ns = now_ns - 1'000'000'000LL;
    f.as_of_ts_ns = now_ns;
    f.bids[0].price = bid;
    f.bids[0].size_usdc = 500.0;
    f.asks[0].price = ask;
    f.asks[0].size_usdc = 500.0;
    f.microprice = (bid + ask) * 0.5;
    f.mid = (bid + ask) * 0.5;
    f.spread = ask - bid;
    f.imbalance = 0.0;
    f.wss_state = WssConnState::kConnected;
    f.sequence_no = 1;
    return f;
}

// 单市场 PaperLoop harness (= bench_tick_one 范式; deps 由 struct 保活)。
struct Harness {
    std::unique_ptr<OrderBookSnapshotHub> hub;
    std::unique_ptr<LedgerSnapshotHub> ledger_hub;
    std::unique_ptr<QuoteSnapshotHub> quote_hub;
    std::unique_ptr<RmDebugSnapshot> rm_snap;
    std::unique_ptr<PositionLedger> ledger;
    std::unique_ptr<RiskGateway> rm;
    std::unique_ptr<BaselineFairValueModel> fv_model;
    std::unique_ptr<PaperLoop> loop;
};

Harness BuildHarness() {
    Harness h;
    h.hub = std::make_unique<OrderBookSnapshotHub>();
    h.ledger_hub = std::make_unique<LedgerSnapshotHub>();
    h.quote_hub = std::make_unique<QuoteSnapshotHub>();
    h.rm_snap = std::make_unique<RmDebugSnapshot>();
    h.ledger = std::make_unique<PositionLedger>();

    RiskConfig rm_cfg;
    rm_cfg.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_micro(10'000'000);
    rm_cfg.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_micro(25'000'000);
    rm_cfg.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_micro(100'000'000);
    rm_cfg.bankroll_usdc = stcpp::domain::MicroPUSD::from_micro(1'000'000'000);
    rm_cfg.edge_ci_lower_floor = -1.0;
    h.rm = std::make_unique<RiskGateway>(rm_cfg, nullptr);

    h.fv_model = std::make_unique<BaselineFairValueModel>(ScorePriorParams{0.30, 0.50}, 0.20);

    const std::int64_t now = NowNs();
    std::unordered_map<std::string, std::pair<std::string, std::string>> token_map;
    token_map["cond-0"] = {"Y0", "N0"};
    h.hub->Publish("Y0", MakeBook(0.50, 0.55, now));  // YES bid0.50/ask0.55
    h.hub->Publish("N0", MakeBook(0.45, 0.50, now));

    PaperLoopConfig cfg;
    cfg.tick_interval_ms = 50;
    cfg.bankroll_usdc = 1000.0;
    cfg.n_effective = 200;
    cfg.z_90 = 1.645;
    cfg.strategy_id = "replay-test";
    h.loop = std::make_unique<PaperLoop>(*h.hub, *h.rm, *h.ledger, *h.ledger_hub, *h.quote_hub,
                                         h.rm_snap.get(), *h.fv_model, std::move(token_map), cfg);
    return h;
}

std::shared_ptr<const PaperCatalog> BuildCatalog() {
    auto c = std::make_shared<PaperCatalog>();
    PaperMarketEntry e;
    e.tokens = {"Y0", "N0"};
    e.fee_coef = 0.03;
    e.cat.asset_class_id = 0;     // Sports
    e.cat.sport_family_id = 0;    // soccer
    e.cat.market_type_id = 0;     // moneyline
    e.parent.event_id = "evt-1";
    (*c)["cond-0"] = e;
    return c;
}

std::shared_ptr<const ConditionEventMap> BuildEventMap() {
    auto m = std::make_shared<ConditionEventMap>();
    EventMapEntry em;
    em.inplay_match_id = "match-1";
    em.yes_is_home = true;
    em.is_draw = false;
    em.match_confidence = 1.0;
    em.match_as_of_ns = NowNs();
    (*m)["cond-0"] = em;
    return m;
}

EventScore MakeInplay(std::int64_t now, double sharp_home_fair, int hs, int as) {
    EventScore es;
    es.found = true;
    es.event_id = "evt-1";
    es.sport = "soccer";
    es.status = "inplay";
    es.period = "2H";
    es.clock_sec = 3600;
    es.home = "Home FC";
    es.away = "Away FC";
    es.home_score = hs;
    es.away_score = as;
    es.ts.event_ts_ns = now - 3'000'000'000LL;
    es.ts.data_source_ts_ns = now - 2'000'000'000LL;
    es.ts.ingestion_ts_ns = now - 1'000'000'000LL;
    es.ts.as_of_ts_ns = now;
    es.league_id = "lg-1";
    es.kickoff_ts_sec = now / 1'000'000'000LL - 3600;
    es.inplay_bet365_home_fair = sharp_home_fair;
    es.inplay_bet365_away_fair = 1.0 - sharp_home_fair - 0.05;
    es.inplay_bet365_draw_fair = 0.05;
    return es;
}

bool WaitFrames(const stcpp::data::ScoreFrameRecorder& r, std::uint64_t n) {
    for (int i = 0; i < 60; ++i) {
        if (r.frames_written() >= n) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// ① in-play sharp 回放 → fair 锚定 sharp 共识 (book-only 永远到不了 sharp-anchor 路径)
//   红线#3 机制本质 = "has_real_fair=false → fair 走 baseline prior"。回放 sharp 比分应令
//   fair_value 锚到 0.70 (sharp), 与无比分时的 baseline prior (~0.50) 显著不同。
TEST(ReplayDecisionInputProvider, InplaySharpReplayAnchorsFairValue) {
    Harness h = BuildHarness();
    const std::int64_t now = NowNs();

    // baseline: 无 replay → 无比分 → fair 走 baseline score-prior (~0.50, 非 sharp)
    h.loop->TickAllForBench();
    const auto base_q = h.quote_hub->Read("cond-0");
    ASSERT_TRUE(base_q.has_value()) << "baseline tick 应发布 quote";
    const double base_fair = base_q->fair_value;
    EXPECT_GT(base_fair, 0.35);
    EXPECT_LT(base_fair, 0.60) << "无比分时 fair 应是 baseline prior (~0.50), 非 sharp";

    // replay: in-play sharp home 0.70 (de-vig 三边) → fair 锚定 sharp 0.70
    ReplayDecisionInputProvider prov;
    prov.SetCatalog(BuildCatalog());
    prov.SetEventMap(BuildEventMap());
    ScoreMap sm;
    sm["match-1"] = MakeInplay(now, 0.70, 1, 0);
    prov.AddScoreFrame(now, std::move(sm));
    auto snap = prov.BuildAt(now);
    h.loop->SetReplayInputs(&snap);
    h.loop->TickAllForBench();
    h.loop->SetReplayInputs(nullptr);

    const auto rep_q = h.quote_hub->Read("cond-0");
    ASSERT_TRUE(rep_q.has_value());
    const double rep_fair = rep_q->fair_value;
    EXPECT_GT(rep_fair, 0.62)
        << "回放 in-play sharp(0.70) 应令 fair 锚到 sharp 共识 (book-only 到不了此路径)";
    EXPECT_GT(rep_fair - base_fair, 0.10) << "sharp 回放 fair 应显著高于 baseline prior";
}

// ② resolution 回放 → 结算持仓 (book-only → positions_settled 恒 0)
TEST(ReplayDecisionInputProvider, ReplayResolutionSettlesHeldPosition) {
    Harness h = BuildHarness();
    const std::int64_t now = NowNs();

    // 直接 seed 一笔 YES 持仓 (R-1 经 apply_fill; 隔离概率性 fill, 确定性测结算路径)
    FillEvent ev;
    ev.filled_size_micro = 5'000'000;  // +5 pUSD
    ev.fill_price = 0.55;
    ev.mode_tag = 0;  // R-11 paper
    ev.event_ts_ns = now - 3'000'000'000LL;
    ev.data_source_ts_ns = now - 2'000'000'000LL;
    ev.ingestion_ts_ns = now - 1'000'000'000LL;
    ev.as_of_ts_ns = now;
    h.ledger->apply_fill("cond-0", "Y0", strategy::Outcome::Yes, ev);
    ASSERT_TRUE(h.ledger->get_position("Y0").has_value());

    ReplayDecisionInputProvider prov;
    prov.SetCatalog(BuildCatalog());
    prov.SetEventMap(BuildEventMap());
    ReplayDecisionInputProvider::ResolutionMap res;
    res["cond-0"] = ResolutionEntry{/*status=Resolved*/ 2, /*winner=YES*/ 1};
    prov.SetResolution(std::move(res));
    auto snap = prov.BuildAt(now);
    h.loop->SetReplayInputs(&snap);
    h.loop->TickAllForBench();
    h.loop->SetReplayInputs(nullptr);

    EXPECT_GT(h.loop->stats().positions_settled.load(), 0u)
        << "回放 resolution(status=2) 应结算持仓 (book-only 永不结算 — 红线#3 窟窿)";
}

// ③ scores.jsonl ↔ provider round-trip (ScoreFrameRecorder 落盘格式契约)
TEST(ReplayDecisionInputProvider, JsonlRoundTripFromRecorder) {
    const std::string path =
        (std::filesystem::temp_directory_path() /
         ("stcpp_replay_rt." +
          std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + ".jsonl"))
            .string();
    std::remove(path.c_str());

    stcpp::data::ScoreSnapshotStore store;
    auto m = std::make_shared<ScoreMap>();
    EventScore es = MakeInplay(1'000'000'000, 0.70, 2, 1);
    es.home = R"(Team "X")";  // 含引号 → 测转义 round-trip
    (*m)["match-1"] = es;
    store.Publish(m);

    stcpp::data::ScoreFrameRecorder::Config cfg;
    cfg.output_path = path;
    cfg.poll_interval_sec = 1;
    stcpp::data::ScoreFrameRecorder rec(store, cfg);
    rec.Start();
    ASSERT_TRUE(WaitFrames(rec, 1));
    rec.Stop();

    ReplayDecisionInputProvider prov;
    ASSERT_TRUE(prov.LoadScoreFramesJsonl(path));
    ASSERT_EQ(prov.frame_count(), 1u);

    auto snap = prov.BuildAt(2'000'000'000);
    ASSERT_NE(snap.score, nullptr);
    auto it = snap.score->find("match-1");
    ASSERT_NE(it, snap.score->end());
    EXPECT_TRUE(it->second.found);
    EXPECT_EQ(it->second.home_score, 2);
    EXPECT_EQ(it->second.away_score, 1);
    EXPECT_NEAR(it->second.inplay_bet365_home_fair, 0.70, 1e-9);
    EXPECT_EQ(it->second.ts.as_of_ts_ns, 1'000'000'000);
    EXPECT_EQ(it->second.ts.event_ts_ns, 1'000'000'000 - 3'000'000'000LL);
    EXPECT_EQ(it->second.home, R"(Team "X")");  // 转义还原
    EXPECT_EQ(it->second.status, "inplay");

    std::remove(path.c_str());
}

// PIT: BuildAt 取 frame_ts <= ts 的最近帧, 不看未来帧
TEST(ReplayDecisionInputProvider, BuildAtPicksLatestFrameNotFuture) {
    ReplayDecisionInputProvider prov;
    ScoreMap a;
    a["m"] = MakeInplay(1000, 0.60, 0, 0);
    ScoreMap b;
    b["m"] = MakeInplay(2000, 0.80, 1, 0);
    prov.AddScoreFrame(1000, std::move(a));
    prov.AddScoreFrame(2000, std::move(b));

    EXPECT_EQ(prov.BuildAt(500).score, nullptr);  // 首帧前 → 无可用帧
    EXPECT_NEAR(prov.BuildAt(1500).score->at("m").inplay_bet365_home_fair, 0.60, 1e-9);  // 取帧1
    EXPECT_NEAR(prov.BuildAt(2500).score->at("m").inplay_bet365_home_fair, 0.80, 1e-9);  // 取帧2
}

}  // namespace
