// src/stcpp/debug_api/state_provider.hpp — 观测只读状态契约 (ADR-038 MVP)
// Owner: 小卢 (senior-ic-pool)  ADR-038 MVP
// 关联:
//   docs/ADR/2026-05-29-observability-debug-api.md §2 / §4 / §5
//   docs/RESEARCH/laozhou-observability-api-arch-v1.md §3 (零耦合状态暴露) / §5
//   docs/RESEARCH/xiaobai-observability-api-security-v1.md §1 (黑名单) / §3 (mode)
//   R-11 (paper 不污染真账本; response 带 mode), R-12 (观测侧零反向依赖),
//   R-20 (4 时间戳 epoch_ns int64)
//
// 设计 (老周 §3): 观测侧只持 const 句柄, 热路径模块零反向依赖。
//   阶段 A: debug_api 持 `const StateProvider*`, 由 main 注入。
//   各模块 owner (老韩 RM / 小冯 orderbook / 小石 ledger) 后续提供 double-buffer
//   snapshot 实现, 接到此接口。MVP 默认 StubStateProvider, 返回结构合法的空/0 值。
//
// schema 铁律 (ADR-038 §3):
//   - 4 时间戳字段名 + epoch_ns int64 (禁 ISO 字符串)
//   - vendor-agnostic 字段语义 (禁 goalserve_/pm_ 原始字段; vendor 降为 source 标签)
//   - 黑名单字段 (私钥/签名字节/API secret) 这些 POD 里【物理上不存在】, 从源头杜绝泄露
//
// 注意: 本头文件【不得】#include 任何热路径模块头 (risk/signer/exec), 保证零反向依赖。
//       只用标准库 POD。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stcpp::debug_api {

// ---- 运行模式 (R-11: response 顶层必带 mode) ----
enum class ExecMode : std::uint8_t { Paper, Live, Backtest };

inline const char* exec_mode_str(ExecMode m) noexcept {
    switch (m) {
        case ExecMode::Paper:
            return "paper";
        case ExecMode::Live:
            return "live";
        case ExecMode::Backtest:
            return "backtest";
    }
    return "paper";
}

// ---- 4 时间戳契约 (R-20) ----
// epoch_ns int64; 缺失上游 ts 时为 0 (调用方判 0 = 无该维度)。
struct FourTs {
    std::int64_t event_ts_ns{0};
    std::int64_t data_source_ts_ns{0};
    std::int64_t ingestion_ts_ns{0};
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /api/v1/positions
// ============================================================
struct HoldingView {
    std::string market_id;  // vendor-agnostic 内部 id
    std::string outcome;    // 内部 outcome 标签 (非 vendor token 字符串)
    double net_qty{0.0};    // 净持仓 (signed)
    double avg_entry_price{0.0};
    double mark_price{0.0};
    double pnl_realized{0.0};
    double pnl_unrealized{0.0};
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /api/v1/pnl/timeseries
// ============================================================
struct PnlBucket {
    std::int64_t bucket_start_ts_ns{0};
    double cum_net_pnl{0.0};
    double realized{0.0};
    double unrealized{0.0};
    double fee{0.0};
    double gas{0.0};
    std::int64_t n_trades{0};
};

// ============================================================
// /api/v1/pnl/attribution (gross→fee→gas→slippage→spread→net 瀑布)
// ============================================================
struct PnlAttribution {
    double gross{0.0};
    double fee{0.0};
    double gas{0.0};
    double slippage{0.0};
    double spread{0.0};
    double net{0.0};
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /api/v1/risk/rejects (RM 拒单列表 + reason_code)
// ============================================================
struct RiskRejectRow {
    std::string reason_code;  // RM 枚举字符串 (e.g. "MAX_POSITION_EXCEEDED")
    std::string market_id;
    std::string intent_ref;  // 内部引用 (非签名/私钥; allowlist 安全字段)
    std::int64_t rejected_ts_ns{0};
};

// ============================================================
// /api/v1/gate/paper (GM-PAPER-G 30 日门禁仪表; MVP 字段可空)
// ============================================================
struct PaperGate {
    std::int64_t n_trades{0};
    double positive_day_ratio{0.0};
    double sharpe{0.0};
    double sharpe_se{0.0};
    double p_value{0.0};
    double hit_rate{0.0};
    double max_drawdown{0.0};
    bool prelim_pass{false};
    bool confirm_pass{false};
    std::int64_t window_days{30};
    bool has_data{false};  // MVP stub: false 表示门禁尚未积累数据
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /metrics (Prometheus 业务/健康/数据质量; 低基数 label)
// ============================================================
struct MetricsSnapshot {
    // 健康
    std::int64_t uptime_sec{0};
    bool wss_sports_api_connected{false};
    bool wss_clob_connected{false};
    bool wss_user_channel_connected{false};
    std::int64_t wss_reconnect_total{0};
    double loop_p99_us{0.0};
    // 业务
    std::int64_t rm_decision_total{0};
    std::int64_t rm_reject_total{0};
    std::int64_t fill_total{0};
    double net_edge_bps{0.0};
    double cum_net_pnl{0.0};
    // 数据质量
    double max_staleness_ms{0.0};
    std::int64_t feed_gap_total{0};
    double price_drift_bps{0.0};
};

// ============================================================
// /api/v1/market/{condition_id} (active/closed/resolved 三态分开)
// ============================================================
struct MarketInfo {
    bool found{false};
    std::string market_id;  // = 请求的 condition_id 内部映射
    std::string outcome;
    double tick_size{0.0};
    double fee_rate{0.0};
    bool neg_risk{false};
    bool accepting_orders{false};
    bool active{false};
    bool closed{false};
    bool resolved{false};
    std::string source{"polymarket"};  // vendor 降为 source 标签 (非字段名前缀)
    std::int64_t as_of_ts_ns{0};
};

// ============================================================
// /api/v1/book/{condition_id} (microprice/spread/imbalance 后端算好)
// ============================================================
struct BookSnapshot {
    bool found{false};
    std::string market_id;
    double best_bid{0.0};
    double best_ask{0.0};
    double microprice{0.0};
    double spread{0.0};
    double imbalance{0.0};  // ∈ [-1, 1]
    std::int64_t sequence_no{0};
    std::int64_t gap_count{0};
    std::string wss_state{"unknown"};
    FourTs ts{};
    std::string source{"polymarket"};
};

// ============================================================
// StateProvider — 观测只读契约 (const 方法, 线程安全读)
// ============================================================
// 实现侧约束 (R-12): 所有方法在 debug_api 线程调用, 只读 atomic / double-buffer
// front snapshot, 绝不调用 RM::evaluate / signer / emit, 绝不持热路径锁 > 100us。
class StateProvider {
public:
    virtual ~StateProvider() = default;

    // R-11: 全局运行模式 (build-time 锁定, 运行时不可切)
    virtual ExecMode mode() const = 0;

    virtual std::vector<HoldingView> positions() const = 0;
    virtual std::vector<PnlBucket> pnl_timeseries(std::int64_t window_sec, std::int64_t bucket_sec) const = 0;
    virtual PnlAttribution pnl_attribution() const = 0;
    virtual std::vector<RiskRejectRow> risk_rejects() const = 0;
    virtual PaperGate paper_gate() const = 0;
    virtual MetricsSnapshot metrics() const = 0;
    virtual MarketInfo market(const std::string& condition_id) const = 0;
    virtual BookSnapshot book(const std::string& condition_id) const = 0;
};

// StubStateProvider — MVP 默认实现, 返回结构合法的空/0 值。
// 各模块 owner 提供真实 double-buffer snapshot 后, 由 main 注入替换。
class StubStateProvider final : public StateProvider {
public:
    explicit StubStateProvider(ExecMode m) : mode_(m) {}

    ExecMode mode() const override { return mode_; }

    std::vector<HoldingView> positions() const override { return {}; }
    std::vector<PnlBucket> pnl_timeseries(std::int64_t, std::int64_t) const override { return {}; }
    PnlAttribution pnl_attribution() const override { return {}; }
    std::vector<RiskRejectRow> risk_rejects() const override { return {}; }
    PaperGate paper_gate() const override { return {}; }
    MetricsSnapshot metrics() const override { return {}; }

    MarketInfo market(const std::string& condition_id) const override {
        MarketInfo m;
        m.found = false;  // stub: 无市场目录, 一律 not found
        m.market_id = condition_id;
        return m;
    }

    BookSnapshot book(const std::string& condition_id) const override {
        BookSnapshot b;
        b.found = false;  // stub: 无 orderbook feed 接入
        b.market_id = condition_id;
        return b;
    }

private:
    ExecMode mode_;
};

}  // namespace stcpp::debug_api
