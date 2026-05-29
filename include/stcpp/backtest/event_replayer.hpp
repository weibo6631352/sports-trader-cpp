// stcpp/backtest/event_replayer.hpp — Event-driven backtest replayer v0.2
//
// Owner: 小蒋 (quant-backtest)
// Last review: 2026-05-29
//
// 落: xiaojiang-backtest-framework-v0.2-cpp.md §6 (回测引擎技术架构)
//     xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md §3 §5 (backtest spec + 接口契约)
//
// 红线:
//   BR-1: 回测/paper/实盘三层共用 feature pipeline (C++ 直接 link)
//   BR-5: 回测 slippage 模型必须用小肖 v1 C++ 同一份
//   R-7:  ExecutionMode-agnostic
//   R-11: 不写 ledger (BacktestLedger 是独立模块)
//   R-20: 4 ts 单调链强制校验
//
// 本模块职责:
//   1. SignalEvent — 单个信号触发事件 (含 OrderBookSnapshot + fair_value)
//   2. BacktestConfig — 回测运行参数 (bankroll / fee / ParamSet)
//   3. BacktestLedger — PnL 累计 + TradeRecord 列表
//   4. EventReplayer — 事件驱动回放引擎:
//      a. 按时间顺序处理 SignalEvent
//      b. 调用 FillRateModel::compute_from_clob_book (BR-5 复用同一 fill/slippage 模型)
//      c. Bernoulli 抽样 fill (用于 paper-mode 同等模拟)
//      d. 输出 TradeRecord 序列 (供 MetricsComputer 计算指标)
//
// 依赖:
//   stcpp/microstructure/fill_rate_model.hpp (复用 compute_from_clob_book)
//   stcpp/numerical/slippage_model.hpp       (复用小肖 slippage v1)
//   stcpp/backtest/types.hpp

#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "stcpp/backtest/types.hpp"
#include "stcpp/microstructure/fill_rate_model.hpp"
#include "stcpp/microstructure/orderbook.hpp"
#include "stcpp/numerical/slippage_model.hpp"

namespace stcpp::backtest {

// ---------------------------------------------------------------------------
// 1. SignalEvent — 单个信号触发事件
// ---------------------------------------------------------------------------
//
// 对应小程 §5.1 feature 清单, 含 point-in-time 防 look-ahead 所需字段.
//

struct SignalEvent {
    // R-20 4 ts
    std::int64_t event_ts_ns{0};        // bookmaker 报价事件时间
    std::int64_t data_source_ts_ns{0};  // Goalserve snapshot_ts_ns
    std::int64_t ingestion_ts_ns{0};    // 本地入库时间
    std::int64_t as_of_ts_ns{0};        // 信号触发时刻 (决策点)

    // 市场标识
    std::string market_id;
    std::string game_id;
    TradeBucket bucket{TradeBucket::Pregame};

    // 信号值
    double fair_value{0.0};  // 小程 §5.1 goalserve_devig_p_yes_fair
    double pm_mid{0.0};      // Polymarket mid at as_of_ts
    double gross_edge{0.0};  // |pm_mid - fair_value|
    TradeSide side{TradeSide::BuyYes};
    double kelly_size_usdc{0.0};  // Kelly sizing 后的计划下单量

    // CLOB 快照 (复用 FillRateModel::compute_from_clob_book)
    stcpp::microstructure::OrderBookSnapshot book{};

    // Microprobe (可设空 Microprobe{} 退化到 0 penalty)
    stcpp::microstructure::Microprobe probe{};

    // 过滤标记 (C2 门 / 死区 / MIN_BOOKMAKERS 等)
    bool filtered_out{false};
};

// ---------------------------------------------------------------------------
// 2. BacktestConfig — 回测运行参数
// ---------------------------------------------------------------------------

struct BacktestConfig {
    double initial_bankroll{100'000.0};  // 初始本金 (USDC, 小程 §5.2: $100K)
    double fee_rate{0.03};               // Polymarket taker fee (3%)
    ParamSet params{};                   // 参数集 (C2 门 / Kelly / 死区 / MIN_BOOKS)

    // Bernoulli fill 抽样 seed (确定性复现)
    std::uint64_t fill_seed{12345};

    // 是否启用 Bernoulli 抽样 (true=抽样模拟 fill, false=确定性 fill_rate)
    bool use_bernoulli_fill{true};

    // slippage 模式 (复用小肖 SlippageModel)
    stcpp::numerical::SlippageMode slippage_mode{stcpp::numerical::SlippageMode::Linear};
};

// ---------------------------------------------------------------------------
// 3. BacktestLedger — PnL 累计 + TradeRecord 列表
// ---------------------------------------------------------------------------
//
// 轻量 PnL 账本, 与生产 position_ledger 完全独立 (R-11: 不写生产 ledger).
// 结算在 settle_trades 时注入 outcome.
//

class BacktestLedger {
public:
    explicit BacktestLedger(double initial_bankroll)
        : initial_bankroll_(initial_bankroll), current_equity_(initial_bankroll) {}

    // 追加成交 (fill_price / size_usdc 已确定, outcome 待结算)
    void add_trade(TradeRecord t) { trades_.push_back(std::move(t)); }

    // 结算: 按 market_id 注入结果, 计算 realized_pnl
    void settle_market(std::string const& market_id, SettleOutcome outcome) {
        for (auto& t : trades_) {
            if (t.market_id == market_id && t.outcome == SettleOutcome::Pending) {
                t.outcome = outcome;
                t.realized_pnl_usdc = compute_realized_pnl(t.side, outcome, t.fill_price, t.size_usdc,
                                                           t.fee_rate, t.slippage_rate);
                t.net_edge = (t.size_usdc > 0.0) ? t.realized_pnl_usdc / t.size_usdc : 0.0;
                current_equity_ += t.realized_pnl_usdc;
            }
        }
    }

    [[nodiscard]] std::vector<TradeRecord> const& trades() const noexcept { return trades_; }
    [[nodiscard]] double initial_bankroll() const noexcept { return initial_bankroll_; }
    [[nodiscard]] double current_equity() const noexcept { return current_equity_; }

    void clear() {
        trades_.clear();
        current_equity_ = initial_bankroll_;
    }

private:
    double initial_bankroll_;
    double current_equity_;
    std::vector<TradeRecord> trades_;
};

// ---------------------------------------------------------------------------
// 4. EventReplayer — 事件驱动回放引擎
// ---------------------------------------------------------------------------
//
// 核心流程 (对应 §2.3 模块图):
//   for each SignalEvent (按 as_of_ts 排序):
//     1. PIT 校验 (R-20 4 ts 单调链)
//     2. C2 / 死区 / ParamSet 过滤
//     3. FillRateModel::compute_from_clob_book → fill_rate + slippage_rate (BR-5)
//     4. Bernoulli 抽样 fill (or 确定性)
//     5. SlippageModel::compute → expected_fill_price
//     6. 生成 TradeRecord → ledger.add_trade
//
// 红线: 不直接调 RM (paper backtest 模式下可注入 RM mock; 此处 v0.2 简化不强制 RM)
//

class EventReplayer {
public:
    explicit EventReplayer(BacktestConfig cfg) : cfg_(std::move(cfg)), rng_(cfg_.fill_seed) {}

    // 批量回放一组 SignalEvent (已按 as_of_ts 排序)
    // 返回: TradeRecord 序列 (包含 filtered_out=true 的 trade 以供 audit)
    [[nodiscard]] std::vector<TradeRecord> replay(std::vector<SignalEvent> const& events) {
        std::vector<TradeRecord> records;
        records.reserve(events.size());

        for (auto const& ev : events) {
            records.push_back(process_event(ev));
        }

        return records;
    }

private:
    [[nodiscard]] TradeRecord process_event(SignalEvent const& ev) {
        TradeRecord t;

        // --- R-20 4 ts 填充 ---
        t.event_ts_ns = ev.event_ts_ns;
        t.data_source_ts_ns = ev.data_source_ts_ns;
        t.ingestion_ts_ns = ev.ingestion_ts_ns;
        t.as_of_ts_ns = ev.as_of_ts_ns;
        t.market_id = ev.market_id;
        t.game_id = ev.game_id;
        t.side = ev.side;
        t.bucket = ev.bucket;
        t.fair_value = ev.fair_value;
        t.pm_mid = ev.pm_mid;
        t.gross_edge = ev.gross_edge;
        t.fee_rate = cfg_.fee_rate;
        t.size_usdc = ev.kelly_size_usdc;

        // --- R-20 PIT 校验 ---
        if (!trade_ts_ok(t)) {
            t.filtered_out = true;
            return t;
        }

        // --- 上游已过滤的信号 (C2 / 死区 / MIN_BOOKMAKERS) ---
        if (ev.filtered_out) {
            t.filtered_out = true;
            return t;
        }

        // --- C2 / 死区 再校验 (防上游漏传) ---
        if (ev.gross_edge < cfg_.params.gross_edge_threshold) {
            t.filtered_out = true;
            return t;
        }
        // 死区: fair_value > (1 - threshold) 或 < threshold
        if (ev.fair_value > (1.0 - cfg_.params.dead_zone_threshold) ||
            ev.fair_value < cfg_.params.dead_zone_threshold) {
            t.filtered_out = true;
            return t;
        }

        // --- Step 3: FillRateModel::compute_from_clob_book (BR-5 复用) ---
        using FRM = stcpp::microstructure::FillRateModel;
        using FI = stcpp::microstructure::FillIntent;

        FI intent;
        intent.side = (ev.side == TradeSide::BuyYes) ? stcpp::microstructure::Side::Buy
                                                     : stcpp::microstructure::Side::Sell;
        intent.price = ev.pm_mid;
        intent.size_usdc = ev.kelly_size_usdc;
        intent.sport = stcpp::microstructure::Sport::Basketball;  // default
        intent.phase = stcpp::microstructure::InplayPhase::Mid;
        intent.path = stcpp::microstructure::MatchPath::Maker;

        auto const clob_out = FRM::compute_from_clob_book(ev.book, ev.probe, intent);

        // 若 CLOB 模型失败 (无效 snapshot), 用保守默认
        double fill_rate = 0.75;   // 保守默认
        double slip_rate = 0.003;  // 保守 0.3%
        if (clob_out.reject == stcpp::microstructure::FillRateReject::Ok ||
            clob_out.reject == stcpp::microstructure::FillRateReject::BelowFloor) {
            fill_rate = clob_out.fill_rate;
            slip_rate = clob_out.slippage_rate;
        }
        t.slippage_rate = slip_rate;

        // --- Step 4: Bernoulli 抽样 fill ---
        bool const actually_filled = [&]() -> bool {
            if (!cfg_.use_bernoulli_fill) {
                return fill_rate >= 0.5;  // 确定性: 高于 0.5 就填
            }
            std::uniform_real_distribution<double> u(0.0, 1.0);
            return u(rng_) < fill_rate;
        }();

        if (!actually_filled) {
            // UNFILLED: 记录但 outcome 留 Pending, size_usdc = 0
            t.size_usdc = 0.0;
            t.outcome = SettleOutcome::Pending;
            return t;
        }

        // --- Step 5: SlippageModel → expected_fill_price ---
        // 使用小肖 SlippageModel::compute (BR-5)
        stcpp::numerical::SlippageInput si;
        si.order_size_usdc = ev.kelly_size_usdc;
        si.quote_price = ev.pm_mid;
        si.book_depth_l1_usdc =
            (ev.book.bid[0].size_usdc > 0.0) ? ev.book.bid[0].size_usdc : 1000.0;  // fallback
        si.book_snapshot_ts_ns = ev.data_source_ts_ns;
        si.wall_now_ns = ev.as_of_ts_ns;
        si.tick_size = ev.book.tick_size;

        auto const slip_out = stcpp::numerical::SlippageModel::compute(si, cfg_.slippage_mode);

        // fill_price: 若 slippage model 失败, fallback pm_mid + slip_rate
        double fill_price = ev.pm_mid;
        if (slip_out.reject == stcpp::numerical::RejectCode::Ok) {
            fill_price = slip_out.expected_fill_price;
            // 用 slippage model 的精确 slippage_rate 覆盖 CLOB 估算
            if (ev.pm_mid > 0.0) {
                t.slippage_rate = static_cast<double>(slip_out.slippage_bps) / 10'000.0;
            }
        } else {
            // fallback: mid ± slip_rate
            if (ev.side == TradeSide::BuyYes) {
                fill_price = ev.pm_mid * (1.0 + slip_rate);
            } else {
                fill_price = ev.pm_mid * (1.0 - slip_rate);
            }
        }

        // clamp fill_price ∈ (0, 1)
        if (fill_price <= 0.0)
            fill_price = 0.001;
        if (fill_price >= 1.0)
            fill_price = 0.999;

        t.fill_price = fill_price;
        // outcome 待结算, 不在 replay 阶段填
        t.outcome = SettleOutcome::Pending;

        return t;
    }

    BacktestConfig cfg_;
    std::mt19937_64 rng_;
};

}  // namespace stcpp::backtest
