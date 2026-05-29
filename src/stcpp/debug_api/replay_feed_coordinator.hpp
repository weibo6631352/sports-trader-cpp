// src/stcpp/debug_api/replay_feed_coordinator.hpp
//
// ReplayFeedCoordinator v0.1
// Owner: 小肖 (numerical-algorithms, A 系统工程部)
// last_review: 2026-05-29
//
// 职责:
//   --replay 路径下, 从 OrderBookSnapshotHub 派生 LedgerFeatures + QuoteFeatures,
//   分别 Publish 到 LedgerSnapshotHub + QuoteSnapshotHub, 使
//   positions / pnl / quote 在 --replay 下实时流动 (跟随 book 的 microprice 变动).
//
// 设计要点:
//   1. 独立后台线程 (RAII — 析构时 Stop()+join, 无泄漏)
//   2. 只读 hub (R-12): 从 OrderBookSnapshotHub::Read() 原子读, 不持锁
//   3. R-11 paper mode: LedgerFeatures.mode = kPaper; 不写任何真实账本
//   4. R-20 4-ts: LedgerFeatures / QuoteFeatures 的 4 ts 全来自 OrderBookFeatures
//      (上游 ReplayDriver kSynthetic 合成链路); 禁本地 now() 替代 data_source_ts
//      — as_of_ts_ns 允许用本地 now() (R-20 "观测消费方读取时刻" 例外)
//   5. 数值稳定性:
//      - fair_value: 从 book microprice 出发, 加小偏移模拟 devig
//        (microprice ∈ (0,1) — 用 clamp 保证不溢出到 0 或 1)
//      - pnl_unrealized = (mark_price - avg_entry_price) × net_qty
//        (乘法顺序: 先差再乘, 避免大数相减灾难性抵消)
//      - kelly_fraction: 通过 SizingCalculator::compute() 真算, 不 hardcode
//   6. 合成仓位: 为每个 market_key 派生一个演示性多头仓位
//      (avg_entry_price ≈ mid_center, net_qty 固定, mark 随 microprice 变动)
//      — pnl_unrealized 实时跟随 mark 变化, 验证 positions/pnl 流动
//   7. R-12: 全链路无锁; 唯一锁是 stop_ atomic (release/acquire), 不在读写路径上
//
// 线程模型:
//   主线程: 构造 → Start() → ... → Stop()
//   后台线程: DriveLoop() 循环:
//     for each market_key:
//       1. hub_.Read(token0_id)  → OrderBookFeatures (可 nullopt)
//       2. 若有效: 派生 LedgerFeatures → ledger_hub_.Publish(market_key, ...)
//       3. 派生 QuoteFeatures (调用 SizingCalculator) → quote_hub_.Publish(market_key, ...)
//     sleep(tick_sleep_ns)
//
// 依赖 (只读快照; 零反向依赖 R-12):
//   stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp
//   stcpp/risk/ledger_snapshot_hub.hpp
//   stcpp/sizing/quote_snapshot_hub.hpp
//   stcpp/sizing/sizing_calculator.hpp
//   stcpp/risk/risk_gateway.hpp  (RiskConfig cap 注入)

#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "stcpp/polymarket/clob_wss/orderbook_snapshot_hub.hpp"
#include "stcpp/risk/ledger_snapshot_hub.hpp"
#include "stcpp/risk/risk_gateway.hpp"
#include "stcpp/sizing/quote_snapshot_hub.hpp"
#include "stcpp/sizing/sizing_calculator.hpp"

namespace stcpp::debug_api {

// ---------------------------------------------------------------------------
// MarketFeedEntry — 每个盘口的联动配置
//
// market_key: condition_id (与 DemoStateProvider / token_map 对齐)
// token0_id:  PRIMARY token (从 OrderBookSnapshotHub 读 microprice 的 token)
// avg_entry_price: 合成仓位入场价 (固定; pnl_unrealized 随 mark 变化)
// net_qty: 合成仓位名义额 (USDC, 正=多头)
// ---------------------------------------------------------------------------
struct MarketFeedEntry {
    std::string market_key;
    std::string token0_id;
    double avg_entry_price{0.0};  // 合成入场价 (接近 mid_center)
    double net_qty{0.0};          // 合成仓位 (USDC)
    std::string outcome;          // "LAL" / "OVER" / etc. (供 HoldingView.outcome)
};

// ---------------------------------------------------------------------------
// ReplayFeedCoordinator — 主类
// ---------------------------------------------------------------------------
class ReplayFeedCoordinator {
public:
    // -----------------------------------------------------------------------
    // 构造
    //   book_hub:    OrderBookSnapshotHub (只读; 来自 ReplayDriver 写端)
    //   ledger_hub:  LedgerSnapshotHub (写端; coordinator 独占写)
    //   quote_hub:   QuoteSnapshotHub  (写端; coordinator 独占写)
    //   entries:     market_key → token0_id + 合成仓位配置
    //   risk_cfg:    cap 注入 (SizingCalculator 真算 kelly)
    //   tick_sleep_ns: 循环 tick 间隔 (建议 200ms, 约 5Hz; 与 ReplayDriver 同频)
    // -----------------------------------------------------------------------
    explicit ReplayFeedCoordinator(const polymarket::clob_wss::OrderBookSnapshotHub& book_hub,
                                   risk::LedgerSnapshotHub& ledger_hub, sizing::QuoteSnapshotHub& quote_hub,
                                   std::vector<MarketFeedEntry> entries, risk::RiskConfig risk_cfg = {},
                                   std::int64_t tick_sleep_ns = 200'000'000LL)
        : book_hub_(book_hub),
          ledger_hub_(ledger_hub),
          quote_hub_(quote_hub),
          entries_(std::move(entries)),
          risk_cfg_(risk_cfg),
          tick_sleep_ns_(tick_sleep_ns) {}

    // 禁止拷贝/移动 (含 atomic stop_)
    ReplayFeedCoordinator(const ReplayFeedCoordinator&) = delete;
    ReplayFeedCoordinator& operator=(const ReplayFeedCoordinator&) = delete;
    ReplayFeedCoordinator(ReplayFeedCoordinator&&) = delete;
    ReplayFeedCoordinator& operator=(ReplayFeedCoordinator&&) = delete;

    ~ReplayFeedCoordinator() { Stop(); }

    // -----------------------------------------------------------------------
    // Start — 启动后台驱动线程
    // -----------------------------------------------------------------------
    void Start() {
        stop_.store(false, std::memory_order_release);
        worker_ = std::thread([this] { DriveLoop(); });
    }

    // -----------------------------------------------------------------------
    // Stop — 发停止信号 + join (RAII safe; 可重复调用)
    // -----------------------------------------------------------------------
    void Stop() noexcept {
        stop_.store(true, std::memory_order_release);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    // -----------------------------------------------------------------------
    // publish_count — 累计 Publish 轮次 (供监控)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::int64_t publish_count() const noexcept {
        return publish_count_.load(std::memory_order_relaxed);
    }

private:
    // -----------------------------------------------------------------------
    // DriveLoop — 后台线程主循环
    //
    // 每轮: 遍历 entries_, 对每个 market_key:
    //   1. book_hub_.Read(token0_id) → OrderBookFeatures
    //   2. 若有效: 派生 mark_price = microprice (或 best_bid/ask 中点)
    //   3. 构造 LedgerFeatures → ledger_hub_.Publish()
    //   4. 构造 QuoteFeatures (SizingCalculator) → quote_hub_.Publish()
    //   5. sleep(tick_sleep_ns_)
    //
    // R-12: 全程只读 book_hub (atomic acquire), 无持锁
    // R-20: 4-ts 严格透传 OrderBookFeatures (data_source_ts 来自合成链路, 非 now())
    // R-11: LedgerFeatures.mode = kPaper; 不写真实账本
    // -----------------------------------------------------------------------
    void DriveLoop() {
        using namespace stcpp::polymarket::clob_wss;
        using namespace stcpp::risk;
        using namespace stcpp::sizing;

        while (!stop_.load(std::memory_order_acquire)) {
            const std::int64_t now = now_ns();

            for (const auto& entry : entries_) {
                // 1. 读 book 快照 (原子, 无锁, R-12)
                const auto opt = book_hub_.Read(entry.token0_id);
                if (!opt.has_value() || !opt->valid) {
                    // book 尚未就绪 (ReplayDriver 还未 Publish 第一帧) → 跳过
                    continue;
                }
                const OrderBookFeatures& bk = *opt;

                // 2. mark_price: microprice 若有效则用; 否则用 (best_bid + best_ask) / 2
                //    数值稳定性: microprice 由 OrderBookFeatures 保证 ∈ (0,1) 或 NaN
                double mark_price = bk.microprice;
                if (!std::isfinite(mark_price) || mark_price <= 0.0 || mark_price >= 1.0) {
                    // 回退到 mid (best_bid / best_ask 均来自合成帧, 保证有效)
                    const double bid = bk.best_bid();
                    const double ask = bk.best_ask();
                    if (std::isfinite(bid) && std::isfinite(ask) && bid > 0.0 && ask > bid) {
                        mark_price = 0.5 * (bid + ask);
                    } else {
                        continue;  // 无法派生有效 mark_price → 跳过本 tick
                    }
                }
                // clamp 到 (0.001, 0.999) — 防止极端值导致 PnL 溢出
                mark_price = std::fmax(0.001, std::fmin(0.999, mark_price));

                // 3. 派生 LedgerFeatures (R-11 paper; R-20 4-ts 透传)
                LedgerFeatures lf;
                // R-20: event/data_source/ingestion 来自 bk (上游合成链路, 不用 now())
                lf.event_ts_ns = bk.event_ts_ns;
                lf.data_source_ts_ns = bk.data_source_ts_ns;
                lf.ingestion_ts_ns = bk.ingestion_ts_ns;
                lf.as_of_ts_ns = now;  // R-20 允许: 观测消费方读取时刻
                // 仓位字段
                lf.net_qty = entry.net_qty;
                lf.avg_entry_price = entry.avg_entry_price;
                lf.mark_price = mark_price;
                // PnL 数值稳定性:
                //   pnl_unrealized = (mark - entry) × qty
                //   先做差(小数量级), 再乘 qty, 避免大数相减灾难性抵消
                //   mark 和 avg_entry ∈ (0,1); qty 单位 USDC (~1000); 差 < 0.1
                //   → 乘积量级 ∈ [-100, 100], double 精度完全足够
                lf.pnl_unrealized = (mark_price - entry.avg_entry_price) * entry.net_qty;
                // 合成 realized PnL: demo 固定值 (模拟已结清部分; 与 DemoStateProvider 对齐)
                lf.pnl_realized = entry.net_qty * 0.02;     // 2% realized demo
                lf.pnl_fee = entry.net_qty * 0.03 * 0.005;  // 0.5% × 3% taker fee demo
                lf.pnl_gross = lf.pnl_realized + lf.pnl_unrealized;
                lf.valid = true;
                lf.mode = ExecutionModeTag::kPaper;  // R-11: paper mode 标记

                ledger_hub_.Publish(entry.market_key, lf);

                // 4. 派生 QuoteFeatures (SizingCalculator 真算 kelly)
                //    fair_value: mark_price + devig_offset (小偏移模拟去佣金)
                //    devig_offset = spread / 2 (half-spread as vig proxy)
                //    数值稳定性: clamp 到 (0.001, 0.999)
                const double half_spread = std::fmax(0.0, bk.spread * 0.5);
                double fair_value = mark_price + half_spread;
                fair_value = std::fmax(0.001, std::fmin(0.999, fair_value));

                // edge_bps = |fair - mid| × 10000
                const double edge_bps = std::fabs(fair_value - mark_price) * 10'000.0;
                // edge_ci_lower (demo): 使用完整 spread 的 80% 作为 CI 下界
                // 数值说明: fair_value = mid + half_spread; edge = half_spread
                //   fee_per_unit = 0.03 × p × (1-p) ≈ 0.0072 (p ≈ 0.5)
                //   需要 edge_ci_lower > fee_per_unit 才能通过 CI gating
                //   half_spread ≈ 0.01 → edge_ci_lower = full_spread × 0.8 = 0.016 > 0.0072 ✓
                const double full_spread = std::fmax(0.0, bk.spread);
                const double edge_ci_lower = full_spread * 0.8;

                // SizingCalculator 真实计算 kelly_fraction / suggested_notional
                SizingInput sin;
                sin.fair_value = fair_value;
                sin.price = mark_price;
                sin.edge_ci_lower = edge_ci_lower;
                sin.edge_bps = edge_bps;
                sin.bankroll_usdc = 100'000.0;  // demo bankroll
                sin.fill_rate = 0.85;
                sin.slippage_bps = 8.0;
                sin.buy_yes = (fair_value > mark_price);
                sin.current_token_exposure_usdc = 0.0;
                sin.current_condition_exposure_usdc = 0.0;

                const SizingOutput sout = SizingCalculator::compute(risk_cfg_, sin);

                QuoteFeatures qf;
                // R-20: 4-ts 透传 bk
                qf.event_ts_ns = bk.event_ts_ns;
                qf.data_source_ts_ns = bk.data_source_ts_ns;
                qf.ingestion_ts_ns = bk.ingestion_ts_ns;
                qf.as_of_ts_ns = now;  // R-20 允许: 观测消费方读取时刻
                // 量化字段
                qf.fair_value = fair_value;
                qf.market_mid = mark_price;
                qf.edge_bps = edge_bps;
                if (sout.valid) {
                    qf.kelly_fraction = sout.kelly_fractional;
                    qf.suggested_notional = sout.suggested_notional;
                } else {
                    // CI gating 未通过 (edge 太小) → kelly=0 (fail-closed)
                    qf.kelly_fraction = 0.0;
                    qf.suggested_notional = 0.0;
                }
                // signal_strength: edge_ci_lower × 10 (demo proxy)
                qf.signal_strength = std::fmin(1.0, edge_ci_lower * 10.0);
                // ML provenance: stub (无真实模型)
                // model_id 用 char[] 安全赋值 (QuoteFeatures POD, 无 std::string)
                {
                    static constexpr const char kModelId[] = "replay-synth-v0";
                    static_assert(sizeof(kModelId) <= sizeof(qf.model_id), "model_id overflow");
                    std::copy(std::begin(kModelId), std::end(kModelId), qf.model_id);
                }
                {
                    static constexpr const char kSpecVer[] = "ml-feature-spec-v0.1";
                    static_assert(sizeof(kSpecVer) <= sizeof(qf.spec_version), "spec_version overflow");
                    std::copy(std::begin(kSpecVer), std::end(kSpecVer), qf.spec_version);
                }
                qf.model_kind = ModelKindTag::kStub;
                qf.model_confidence = 0.62;  // demo placeholder
                qf.fair_ci_lower = fair_value - 0.031;
                qf.fair_ci_upper = fair_value + 0.026;
                qf.model_as_of_ts_ns = bk.ingestion_ts_ns;  // feature PIT ≤ as_of (R-20)
                qf.predict_ok = sout.valid;
                qf.advisory = true;  // ML-R2: paper 期恒 true
                qf.model_calibrated = false;
                qf.valid = true;

                quote_hub_.Publish(entry.market_key, qf);
            }

            publish_count_.fetch_add(1, std::memory_order_relaxed);

            // tick sleep (interruptible: 每 10ms 检查 stop_)
            const std::int64_t sleep_total_ns = tick_sleep_ns_;
            const std::int64_t check_interval_ns = 10'000'000LL;  // 10ms
            std::int64_t slept_ns = 0;
            while (slept_ns < sleep_total_ns && !stop_.load(std::memory_order_acquire)) {
                const std::int64_t nap = std::fmin(static_cast<double>(check_interval_ns),
                                                   static_cast<double>(sleep_total_ns - slept_ns));
                std::this_thread::sleep_for(std::chrono::nanoseconds(static_cast<std::int64_t>(nap)));
                slept_ns += check_interval_ns;
            }
        }
    }

    // R-20 allowed: as_of_ts_ns (观测消费方读取时刻)
    static std::int64_t now_ns() noexcept {
        using namespace std::chrono;
        return static_cast<std::int64_t>(
            duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count());
    }

    const polymarket::clob_wss::OrderBookSnapshotHub& book_hub_;
    risk::LedgerSnapshotHub& ledger_hub_;
    sizing::QuoteSnapshotHub& quote_hub_;
    std::vector<MarketFeedEntry> entries_;
    risk::RiskConfig risk_cfg_;
    std::int64_t tick_sleep_ns_;

    std::atomic<bool> stop_{false};
    std::atomic<std::int64_t> publish_count_{0};
    std::thread worker_;
};

}  // namespace stcpp::debug_api
