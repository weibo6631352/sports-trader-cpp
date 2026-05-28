// stcpp/stats/gate_evaluator.hpp — M4.5 7 hard gate 统计 framework v1
//
// Owner: 小董 (stats-inference-advisor, W4 Wave 20)
// Reviewers: 小蒋 (paper engine 数据契约) / 老韩 (RM fail 计数) / 老吴 (uptime) / 小梁 (阈值会签)
//
// 落:
//   docs/RESEARCH/xiaodong-m45-gate-framework-v1.md
//   docs/MEETINGS/sprint1-retro/ (7 gate 决议)
//
// 红线:
//   R-7   paper / live 共享 evaluator (build-time 切换数据源, 不分叉统计逻辑)
//   R-11  paper gate 与 live gate 同一 binary 同 evaluator; 输入数据源不同
//   R-20  GateMetrics 携 4 ts (sample_window_start_ts_ns / end / ingestion_completed_ts_ns / as_of_ts_ns)
//
// 7 hard gate (Sprint-1 retro 已定):
//   G1 PnL t-test          — paper PnL 与 0 显著, Welch 双侧 p < 0.05
//   G2 Sharpe bootstrap CI — 1000 次 percentile, 95% CI 下限 > 0.5
//   G3 RM 失效次数 = 0      — 任何 bypass → 重置 2 周窗口
//   G4 系统在线率 ≥ 99.5%   — uptime_s / window_s ≥ 0.995
//   G5 最大 DD ≤ 8%         — peak-to-trough, paper 期任意时点
//   G6 笔数 ≥ 50           — 防小样本偏倚 (Kelly 0.25 一笔 $1.25K)
//   G7 paper > random      — Welch t (paper vs random_baseline series), one-sided p < 0.05
//
// 设计原则:
//   - p-value 全用 Welch (不假设等方差), 自由度 n-1 / Satterthwaite
//   - bootstrap 用 percentile method (非 BCa, BCa 留 Sprint-3 升级)
//   - NaN / Inf 输入 → InvalidInput error (fail-closed, paper 解锁前所有数据必须清洁)
//   - bootstrap seed 可注入, 单测可复现
//   - 不接 paper engine data flow (W5 接小蒋 paper_audit.wal 读)

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace stcpp::stats {

// ---------------------------------------------------------------------------
// Gate ID (Sprint-1 retro 顺序锁死, 入 audit log 字符串名靠 GateIdName)
// ---------------------------------------------------------------------------

enum class GateId : std::uint8_t {
    G1_PnLTTest         = 1,
    G2_SharpeBoostrapCI = 2,
    G3_RMZeroFail       = 3,
    G4_Uptime           = 4,
    G5_MaxDD            = 5,
    G6_MinTrades        = 6,
    G7_AboveRandom      = 7,
};

inline constexpr std::size_t kNumGates = 7;

[[nodiscard]] constexpr const char* GateIdName(GateId id) noexcept {
    switch (id) {
        case GateId::G1_PnLTTest:         return "G1_PnLTTest";
        case GateId::G2_SharpeBoostrapCI: return "G2_SharpeBoostrapCI";
        case GateId::G3_RMZeroFail:       return "G3_RMZeroFail";
        case GateId::G4_Uptime:           return "G4_Uptime";
        case GateId::G5_MaxDD:            return "G5_MaxDD";
        case GateId::G6_MinTrades:        return "G6_MinTrades";
        case GateId::G7_AboveRandom:      return "G7_AboveRandom";
    }
    return "G?_Unknown";
}

// ---------------------------------------------------------------------------
// 阈值常量 (与 Sprint-1 retro + 小董 v1 research doc 一致, 任何调整须 GM 决议)
// ---------------------------------------------------------------------------

inline constexpr double kG1_PValueMax        = 0.05;   // Welch 双侧
inline constexpr double kG2_SharpeCILow      = 0.5;    // CI 下限, 解锁低门槛 (vs 北极星 1.5)
inline constexpr std::size_t kG2_BootstrapN  = 1000;   // 平衡 SE_pct ~0.014 与 paper 跑批耗时
inline constexpr int    kG3_RMFailMax        = 0;
inline constexpr double kG4_UptimeMin        = 0.995;
inline constexpr double kG5_MaxDDMax         = 0.08;   // 8% peak-to-trough
inline constexpr std::size_t kG6_TradesMin   = 50;
inline constexpr double kG7_PValueMax        = 0.05;   // Welch one-sided (paper vs random)

// ---------------------------------------------------------------------------
// 错误类型 (fail-closed 统一返回)
// ---------------------------------------------------------------------------

enum class EvalError : std::uint8_t {
    Ok                  = 0,
    InvalidInput        = 1,   // NaN / Inf / size mismatch
    InsufficientSample  = 2,   // 不足以做 t-test / bootstrap (返 pass=false + 此 code)
    PitViolation        = 3,   // R-20 4 ts 不等式破坏
};

[[nodiscard]] constexpr const char* EvalErrorName(EvalError e) noexcept {
    switch (e) {
        case EvalError::Ok:                 return "Ok";
        case EvalError::InvalidInput:       return "InvalidInput";
        case EvalError::InsufficientSample: return "InsufficientSample";
        case EvalError::PitViolation:       return "PitViolation";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// GateMetrics 输入 (R-20 4 ts 必带, 时序数据 + scalar 计数)
// ---------------------------------------------------------------------------

struct GateMetrics {
    // R-20 4 ts (sample window 起止 + 接入完成 + as_of). 单调链: start ≤ end ≤ ingestion ≤ as_of.
    std::int64_t window_start_ts_ns        = 0;   // event_ts (paper 第一笔 fill)
    std::int64_t window_end_ts_ns          = 0;   // data_source_ts (paper 最后一笔 fill)
    std::int64_t ingestion_completed_ts_ns = 0;   // ingestion_ts (audit.wal 读完时刻)
    std::int64_t as_of_ts_ns               = 0;   // as_of_ts (evaluator 调起时刻)

    // 时序: per-trade PnL (USDC, 含手续费). G1 用此, G7 paper 端用此.
    // 长度 == 实际成交笔数; G6 用此 size.
    std::vector<double> per_trade_pnl_usdc;

    // 时序: per-trade equity 累计 (peak-to-trough 算 DD; G5 用).
    // 与 per_trade_pnl_usdc 同长度; equity[i] = initial_equity + sum(pnl[0..i]).
    // 调用方算好传入, evaluator 不重算 (避 trade 时序 vs equity 时序歧义).
    std::vector<double> equity_curve_usdc;

    // 时序: per-trade return (用于 Sharpe). 调用方按 paper 跑批口径算好 (per-trade or per-day).
    // 与 per_trade_pnl_usdc 同长度; Sharpe = mean / std * sqrt(annualizer).
    std::vector<double> per_trade_return;
    double sharpe_annualizer = 1.0;   // sqrt(N_periods_per_year). 默认 1 = 不年化 (per-trade Sharpe).

    // 时序: random baseline per-trade PnL (G7 用). 长度可不等 paper.
    // 生成规则 (v1): paper 同窗内每个 signal trigger 时点, 同 size, 50/50 BUY_YES/NO 随机.
    // 见 research doc §G7.
    std::vector<double> random_baseline_pnl_usdc;

    // Scalar 计数
    int      rm_failure_count   = 0;          // G3
    double   uptime_seconds     = 0.0;        // G4 分子
    double   total_window_seconds = 0.0;      // G4 分母 (≥ uptime_seconds)

    // bootstrap 可复现 seed (G2). 默认 0 → seed=42 (单测确定性).
    std::uint64_t bootstrap_seed = 42;
};

// ---------------------------------------------------------------------------
// 单 gate 结果
// ---------------------------------------------------------------------------

struct GateResult {
    GateId      id           = GateId::G1_PnLTTest;
    bool        pass         = false;
    double      actual       = 0.0;   // 实际值 (Sharpe / DD / uptime / count / p-value)
    double      threshold    = 0.0;   // 阈值
    double      p_value      = -1.0;  // t-test gate 才有; 其他 = -1
    double      ci_lower     = 0.0;   // G2 bootstrap CI lower; 其他 = 0
    double      ci_upper     = 0.0;   // G2 bootstrap CI upper; 其他 = 0
    std::size_t sample_size  = 0;
    EvalError   error        = EvalError::Ok;
    std::string note;                 // 简短人读 (失败原因 / 调用方诊断)
};

// ---------------------------------------------------------------------------
// 全 7 gate 综合结果
// ---------------------------------------------------------------------------

struct GateOutcome {
    std::array<GateResult, kNumGates> per_gate{};
    std::size_t pass_count    = 0;
    bool        all_pass      = false;   // pass_count == 7
    EvalError   first_error   = EvalError::Ok;  // 任一 gate InvalidInput / PitViolation 高优透出
};

// ---------------------------------------------------------------------------
// Evaluator 主类
// ---------------------------------------------------------------------------

class GateEvaluator {
   public:
    GateEvaluator() = default;

    // R-20 PIT pre-check (4 ts 不等式). 失败 → all gates pass=false + first_error=PitViolation.
    [[nodiscard]] static bool CheckPit(const GateMetrics& m) noexcept;

    // 7 gate 单跑 (单测可单点验证, 不必跑全套)
    [[nodiscard]] static GateResult EvalG1_PnLTTest(const GateMetrics& m) noexcept;
    [[nodiscard]] static GateResult EvalG2_SharpeBootstrap(const GateMetrics& m) noexcept;
    [[nodiscard]] static GateResult EvalG3_RMZeroFail(const GateMetrics& m) noexcept;
    [[nodiscard]] static GateResult EvalG4_Uptime(const GateMetrics& m) noexcept;
    [[nodiscard]] static GateResult EvalG5_MaxDD(const GateMetrics& m) noexcept;
    [[nodiscard]] static GateResult EvalG6_MinTrades(const GateMetrics& m) noexcept;
    [[nodiscard]] static GateResult EvalG7_AboveRandom(const GateMetrics& m) noexcept;

    // 全跑: 含 PIT 前置 + 7 gate 顺序求值
    [[nodiscard]] static GateOutcome EvaluateAll(const GateMetrics& m) noexcept;
};

}  // namespace stcpp::stats
