// tests/unit/test_backtest_walk_forward.cpp — Walk-forward + DataAccessGuard 单测
//
// Owner: 小蒋 (quant-backtest)
// Wave: backtest framework v0.2
// Last review: 2026-05-29
//
// 覆盖:
//   T01: WalkForwardSplitter — 基本分割 2 fold
//   T02: WalkForwardSplitter — 数据不足 → 0 fold
//   T03: WalkForwardSplitter — max_folds 限制
//   T04: WalkForwardSplitter — embargo 窗口不重叠
//   T05: DataAccessGuard IS — is_trade_accessible 窗口内
//   T06: DataAccessGuard IS — 窗口外 trade 拒绝
//   T07: DataAccessGuard IS — freeze_params 可调 (IS phase)
//   T08: DataAccessGuard OOS — freeze_params 不可调 (OOS phase — 编译期, 注释说明)
//   T09: DataAccessGuard IS — filter_trades 只返回窗口内合法 trade
//   T10: PurgedKFold — 按 game_id 分组

#include <gtest/gtest.h>

#include "stcpp/backtest/walk_forward.hpp"

namespace stcpp::backtest {
namespace {

// 辅助: 构造合法 TradeRecord
TradeRecord make_trade(std::string game_id, std::int64_t as_of_ns) {
    TradeRecord t;
    t.game_id           = std::move(game_id);
    t.event_ts_ns       = as_of_ns - 3;
    t.data_source_ts_ns = as_of_ns - 2;
    t.ingestion_ts_ns   = as_of_ns - 1;
    t.as_of_ts_ns       = as_of_ns;
    t.outcome           = SettleOutcome::YesWins;
    t.side              = TradeSide::BuyYes;
    t.bucket            = TradeBucket::Pregame;
    t.size_usdc         = 100.0;
    t.realized_pnl_usdc = 5.0;
    t.fill_price        = 0.45;
    return t;
}

static constexpr std::int64_t kDayNs = 86'400'000'000'000LL;

// T01: 基本分割 — 300d 数据, IS=150d, OOS=120d, embargo=7d → 1 fold
// (300 - 150 - 7 - 120 = 23d 不足第二轮)
TEST(WalkForward, BasicSplit) {
    WalkForwardConfig cfg = WalkForwardConfig::default_p0_02();
    // 缩小窗口以得到 2 fold
    cfg.train_duration_ns = 100LL * kDayNs;
    cfg.oos_duration_ns   = 80LL  * kDayNs;
    cfg.embargo_duration_ns = 7LL * kDayNs;
    cfg.step_duration_ns  = 80LL  * kDayNs;
    cfg.max_folds         = 0;

    WalkForwardSplitter splitter(cfg);
    // 数据 span: 370 天
    std::int64_t const start = 0LL;
    std::int64_t const end   = 370LL * kDayNs;
    auto const windows = splitter.generate(start, end);

    // fold 0: IS=[0, 100d], emb=[100d,107d], OOS=[107d,187d] — OK
    // fold 1: IS=[80d,180d], emb=[180d,187d], OOS=[187d,267d] — OK
    // fold 2: IS=[160d,260d], emb=[260d,267d], OOS=[267d,347d] — OK
    // fold 3: IS=[240d,340d], emb=[340d,347d], OOS=[347d,427d] — 427>370 → 停
    EXPECT_GE(windows.size(), 3u);
    EXPECT_LE(windows.size(), 4u);

    if (!windows.empty()) {
        EXPECT_EQ(windows[0].fold_index, 0u);
        // IS 长度应等于 train_duration
        EXPECT_EQ(windows[0].is_end_ns - windows[0].is_start_ns, cfg.train_duration_ns);
        // OOS 长度应等于 oos_duration
        EXPECT_EQ(windows[0].oos_end_ns - windows[0].oos_start_ns, cfg.oos_duration_ns);
        // embargo 窗口
        EXPECT_EQ(windows[0].embargo_end_ns - windows[0].is_end_ns, cfg.embargo_duration_ns);
    }
}

// T02: 数据不足 → 0 fold
TEST(WalkForward, InsufficientData) {
    WalkForwardConfig cfg = WalkForwardConfig::default_p0_02();
    WalkForwardSplitter splitter(cfg);
    // 仅 10 天数据
    auto const windows = splitter.generate(0, 10LL * kDayNs);
    EXPECT_TRUE(windows.empty());
}

// T03: max_folds 限制
TEST(WalkForward, MaxFolds) {
    WalkForwardConfig cfg;
    cfg.train_duration_ns   = 30LL * kDayNs;
    cfg.oos_duration_ns     = 20LL * kDayNs;
    cfg.embargo_duration_ns = 3LL  * kDayNs;
    cfg.step_duration_ns    = 20LL * kDayNs;
    cfg.max_folds           = 2;

    WalkForwardSplitter splitter(cfg);
    auto const windows = splitter.generate(0, 500LL * kDayNs);
    EXPECT_EQ(windows.size(), 2u);
}

// T04: OOS 与下一个 IS 无重叠 (embargo 正确切分)
TEST(WalkForward, NoOosIsOverlap) {
    WalkForwardConfig cfg;
    cfg.train_duration_ns   = 50LL * kDayNs;
    cfg.oos_duration_ns     = 30LL * kDayNs;
    cfg.embargo_duration_ns = 7LL  * kDayNs;
    cfg.step_duration_ns    = 30LL * kDayNs;
    cfg.max_folds           = 3;

    WalkForwardSplitter splitter(cfg);
    auto const windows = splitter.generate(0, 500LL * kDayNs);

    for (std::size_t i = 0; i + 1 < windows.size(); ++i) {
        // 下一 fold 的 IS start = i+1 fold 的 is_start
        // 当前 fold 的 OOS end ≤ 下一 fold IS start (允许重叠 IS, 只要 OOS 不重叠 IS)
        // 这里只检查: oos_start >= embargo_end (正确 embargo 间隔)
        EXPECT_EQ(windows[i].oos_start_ns, windows[i].embargo_end_ns);
        EXPECT_EQ(windows[i].embargo_end_ns, windows[i].is_end_ns + cfg.embargo_duration_ns);
    }
}

// T05: DataAccessGuard IS — 窗口内 trade 可访问
TEST(DataAccessGuard, TradeInsideWindow) {
    DataAccessGuard<Phase::InSample> guard(100LL, 200LL);
    auto t = make_trade("game1", 150LL);
    EXPECT_TRUE(guard.is_trade_accessible(t));
}

// T06: DataAccessGuard IS — 窗口外 trade 拒绝
TEST(DataAccessGuard, TradeOutsideWindow) {
    DataAccessGuard<Phase::InSample> guard(100LL, 200LL);

    // as_of 在窗口前
    auto t1 = make_trade("game1", 50LL);
    EXPECT_FALSE(guard.is_trade_accessible(t1));

    // as_of 在窗口后
    auto t2 = make_trade("game2", 250LL);
    EXPECT_FALSE(guard.is_trade_accessible(t2));
}

// T07: DataAccessGuard IS — freeze_params 可调 (IS phase)
TEST(DataAccessGuard, FreezeParsmsInSample) {
    DataAccessGuard<Phase::InSample> guard(0LL, 1000LL);
    EXPECT_FALSE(guard.has_frozen_params());

    ParamSet p;
    p.gross_edge_threshold = 0.06;
    p.kelly_fraction       = 0.20;
    guard.freeze_params(p);

    EXPECT_TRUE(guard.has_frozen_params());
    EXPECT_NEAR(guard.get_frozen_params().gross_edge_threshold, 0.06, 1e-10);
}

// T08: DataAccessGuard OOS — 无 freeze_params 方法 (编译期确保 — 运行测试确认 OOS guard 行为)
// 注: 编译期 enable_if 禁止在 OOS phase 调 freeze_params.
//     此测试验证 OOS guard 的 has_frozen_params() 初始为 false 且 is_trade_accessible 正常工作.
TEST(DataAccessGuard, OoSPhaseNotFrozen) {
    DataAccessGuard<Phase::OutOfSample> guard_oos(500LL, 1000LL);
    EXPECT_FALSE(guard_oos.has_frozen_params());

    // OOS guard 仍能过滤 trade
    auto t = make_trade("game1", 750LL);
    EXPECT_TRUE(guard_oos.is_trade_accessible(t));

    auto t_outside = make_trade("game2", 200LL);
    EXPECT_FALSE(guard_oos.is_trade_accessible(t_outside));
}

// T09: filter_trades — 只返回窗口内合法 trade
TEST(DataAccessGuard, FilterTrades) {
    DataAccessGuard<Phase::InSample> guard(100LL, 200LL);

    std::vector<TradeRecord> trades = {
        make_trade("g1", 50LL),   // 窗口前 → 排除
        make_trade("g2", 150LL),  // 窗口内 → 保留
        make_trade("g3", 180LL),  // 窗口内 → 保留
        make_trade("g4", 250LL),  // 窗口后 → 排除
    };

    auto const filtered = guard.filter_trades(trades);
    EXPECT_EQ(filtered.size(), 2u);
    EXPECT_EQ(filtered[0].game_id, "g2");
    EXPECT_EQ(filtered[1].game_id, "g3");
}

// T10: PurgedKFold — 同 game_id 进同 fold, 不同 game_id 分散
TEST(PurgedKFold, GameIdGrouping) {
    // 3 个 game, 每个 game 3 个 tick
    std::vector<TradeRecord> trades;
    for (int g = 0; g < 3; ++g) {
        for (int tick = 0; tick < 3; ++tick) {
            std::int64_t const ts = static_cast<std::int64_t>(g * 1000 + tick * 10 + 100);
            trades.push_back(make_trade("game" + std::to_string(g), ts));
        }
    }

    PurgedKFold kfold(3 /*n_folds*/, 5LL /*embargo_ns=5 (很小, 不影响分组)*/);
    auto const splits = kfold.split(trades);
    EXPECT_EQ(splits.size(), 3u);

    // 每个 val fold 只包含一个 game_id 的 trade
    for (auto const& s : splits) {
        if (s.val.empty()) continue;
        std::string const first_gid = s.val[0].game_id;
        for (auto const& t : s.val) {
            EXPECT_EQ(t.game_id, first_gid)
                << "Fold " << s.fold_index << " val contains mixed game_ids";
        }
    }
}

}  // namespace
}  // namespace stcpp::backtest
