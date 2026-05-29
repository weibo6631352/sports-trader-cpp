// stcpp/stats/gate_evaluator.cpp — M4.5 7 hard gate v1 实现
//
// Owner: 小董 (stats-inference-advisor)
// 落: include/stcpp/stats/gate_evaluator.hpp + docs/RESEARCH/xiaodong-m45-gate-framework-v1.md
//
// 实现说明:
//   - Welch one-sample t-test (vs 0): t = mean / (s / sqrt(n)), df = n-1
//   - Welch two-sample t-test (paper vs random): t = (m1-m2) / sqrt(s1^2/n1 + s2^2/n2),
//                                                  df = Satterthwaite
//   - p-value 用 Student-t 分布 survival, regularized incomplete beta:
//       p_two_sided = I_x(df/2, 1/2),    x = df / (df + t^2)
//       p_one_sided = p_two_sided / 2 (若 t < 0, paper 不如 random, 1 - p/2)
//   - bootstrap percentile CI: resample n 次 (with replacement), 算 Sharpe, 取 2.5/97.5 pct.
//   - max DD: peak-to-trough on equity curve (含每点 max prior peak)
//   - PRNG: std::mt19937_64 seedable (kBootstrapSeed override 走 GateMetrics::bootstrap_seed)

#include "stcpp/stats/gate_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace stcpp::stats {

namespace {

// ---------------------------------------------------------------------------
// 数值清洁: 序列是否全 finite (非 NaN / 非 Inf)
// ---------------------------------------------------------------------------
[[nodiscard]] bool AllFinite(const std::vector<double>& v) noexcept {
    for (double x : v) {
        if (!std::isfinite(x)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 样本均值 + 样本标准差 (n-1, Bessel 校正)
// ---------------------------------------------------------------------------
struct SampleStats {
    double mean = 0.0;
    double std  = 0.0;
    std::size_t n = 0;
};

[[nodiscard]] SampleStats ComputeSampleStats(const std::vector<double>& x) noexcept {
    SampleStats out{};
    const std::size_t n = x.size();
    if (n == 0) return out;
    double sum = 0.0;
    for (double v : x) sum += v;
    out.mean = sum / static_cast<double>(n);
    if (n < 2) {
        out.std = 0.0;
        out.n   = n;
        return out;
    }
    double sq = 0.0;
    for (double v : x) {
        const double d = v - out.mean;
        sq += d * d;
    }
    out.std = std::sqrt(sq / static_cast<double>(n - 1));
    out.n   = n;
    return out;
}

// ---------------------------------------------------------------------------
// Regularized incomplete beta I_x(a, b) — Lentz continued fraction
// Numerical Recipes 3e §6.4. 用于 Student-t p-value.
// ---------------------------------------------------------------------------
[[nodiscard]] double BetaContinuedFraction(double a, double b, double x) noexcept {
    constexpr int    kMaxIter = 200;
    constexpr double kEps     = 3.0e-12;
    constexpr double kFpMin   = 1.0e-300;

    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= kMaxIter; ++m) {
        const int    m2 = 2 * m;
        double       aa = static_cast<double>(m) * (b - m) * x /
                          ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x /
             ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < kEps) break;
    }
    return h;
}

[[nodiscard]] double RegularizedIncompleteBeta(double a, double b, double x) noexcept {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    // ln B(a,b) = lgamma(a) + lgamma(b) - lgamma(a+b)
    const double lnBeta = std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
    const double front  = std::exp(std::log(x) * a + std::log(1.0 - x) * b - lnBeta);
    if (x < (a + 1.0) / (a + b + 2.0)) {
        return front * BetaContinuedFraction(a, b, x) / a;
    }
    return 1.0 - front * BetaContinuedFraction(b, a, 1.0 - x) / b;
}

// Student-t survival: P(T > |t|) two-sided, df > 0
[[nodiscard]] double StudentTPValueTwoSided(double t, double df) noexcept {
    if (!std::isfinite(t) || df <= 0.0) return 1.0;
    const double x = df / (df + t * t);
    // p_two_sided = I_x(df/2, 1/2)
    return RegularizedIncompleteBeta(df * 0.5, 0.5, x);
}

// One-sided "greater": H1: mean1 > mean2  →  p = p_two / 2 if t > 0 else 1 - p_two / 2
[[nodiscard]] double StudentTPValueOneSidedGreater(double t, double df) noexcept {
    const double p2 = StudentTPValueTwoSided(t, df);
    if (t >= 0.0) return p2 * 0.5;
    return 1.0 - p2 * 0.5;
}

// ---------------------------------------------------------------------------
// Sharpe = mean / std * annualizer
// ---------------------------------------------------------------------------
[[nodiscard]] double ComputeSharpe(const std::vector<double>& r, double annualizer) noexcept {
    const auto s = ComputeSampleStats(r);
    if (s.n < 2 || s.std < 1.0e-12) return 0.0;
    return (s.mean / s.std) * annualizer;
}

// ---------------------------------------------------------------------------
// 选 quantile (linear interpolation, type-7 / numpy default)
// ---------------------------------------------------------------------------
[[nodiscard]] double Quantile(std::vector<double>& sorted_in, double q) noexcept {
    // 调用方负责 sort (sorted_in 已升序)
    const std::size_t n = sorted_in.size();
    if (n == 0) return 0.0;
    if (n == 1) return sorted_in[0];
    const double h = q * static_cast<double>(n - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(h));
    const std::size_t hi = std::min(lo + 1, n - 1);
    const double frac = h - static_cast<double>(lo);
    return sorted_in[lo] * (1.0 - frac) + sorted_in[hi] * frac;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// R-20 PIT 前置
// ---------------------------------------------------------------------------
bool GateEvaluator::CheckPit(const GateMetrics& m) noexcept {
    return (m.window_start_ts_ns > 0)
        && (m.window_end_ts_ns          >= m.window_start_ts_ns)
        && (m.ingestion_completed_ts_ns >= m.window_end_ts_ns)
        && (m.as_of_ts_ns               >= m.ingestion_completed_ts_ns);
}

// ---------------------------------------------------------------------------
// G1: one-sample Welch t-test, H0: mean(pnl) = 0, two-sided p < 0.05
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG1_PnLTTest(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id        = GateId::G1_PnLTTest;
    r.threshold = kG1_PValueMax;
    r.sample_size = m.per_trade_pnl_usdc.size();

    if (!AllFinite(m.per_trade_pnl_usdc)) {
        r.error = EvalError::InvalidInput;
        r.note  = "per_trade_pnl_usdc contains NaN/Inf";
        return r;
    }
    if (r.sample_size < 2) {
        r.error = EvalError::InsufficientSample;
        r.note  = "n<2 cannot run t-test";
        return r;
    }
    const auto s = ComputeSampleStats(m.per_trade_pnl_usdc);
    if (s.std < 1.0e-12) {
        // 全 0 或全等 → 无波动. mean=0 → 不显著; mean!=0 → 显著 (退化为 deterministic).
        r.actual  = (std::fabs(s.mean) < 1.0e-12) ? 1.0 : 0.0;
        r.p_value = r.actual;
        r.pass    = (s.mean > 0.0);   // 必须 mean > 0 (paper 盈利) + 无波动 = 解锁
        r.note    = "zero variance edge case";
        return r;
    }
    const double se = s.std / std::sqrt(static_cast<double>(s.n));
    const double t  = s.mean / se;
    const double df = static_cast<double>(s.n - 1);
    const double p2 = StudentTPValueTwoSided(t, df);

    r.actual  = p2;
    r.p_value = p2;
    // 解锁标准: 显著 + 方向正 (mean > 0). 显著但 mean < 0 = 显著亏 → fail.
    r.pass    = (p2 < kG1_PValueMax) && (s.mean > 0.0);
    return r;
}

// ---------------------------------------------------------------------------
// G2: bootstrap percentile CI on Sharpe, 95% CI lower > 0.5
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG2_SharpeBootstrap(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id        = GateId::G2_SharpeBoostrapCI;
    r.threshold = kG2_SharpeCILow;
    r.sample_size = m.per_trade_return.size();

    if (!AllFinite(m.per_trade_return)) {
        r.error = EvalError::InvalidInput;
        r.note  = "per_trade_return contains NaN/Inf";
        return r;
    }
    if (r.sample_size < 2) {
        r.error = EvalError::InsufficientSample;
        r.note  = "n<2 cannot bootstrap";
        return r;
    }
    if (!std::isfinite(m.sharpe_annualizer) || m.sharpe_annualizer <= 0.0) {
        r.error = EvalError::InvalidInput;
        r.note  = "sharpe_annualizer must be finite > 0";
        return r;
    }

    const double point_sharpe = ComputeSharpe(m.per_trade_return, m.sharpe_annualizer);
    r.actual = point_sharpe;

    // bootstrap
    const std::uint64_t seed = (m.bootstrap_seed == 0) ? 42ULL : m.bootstrap_seed;
    std::mt19937_64 rng(seed);
    const std::size_t n = r.sample_size;
    std::uniform_int_distribution<std::size_t> dist(0, n - 1);

    std::vector<double> sharpe_resamples;
    sharpe_resamples.reserve(kG2_BootstrapN);
    std::vector<double> resample(n, 0.0);

    for (std::size_t b = 0; b < kG2_BootstrapN; ++b) {
        for (std::size_t i = 0; i < n; ++i) {
            resample[i] = m.per_trade_return[dist(rng)];
        }
        sharpe_resamples.push_back(ComputeSharpe(resample, m.sharpe_annualizer));
    }
    std::sort(sharpe_resamples.begin(), sharpe_resamples.end());

    r.ci_lower = Quantile(sharpe_resamples, 0.025);
    r.ci_upper = Quantile(sharpe_resamples, 0.975);
    r.pass     = (r.ci_lower > kG2_SharpeCILow);
    return r;
}

// ---------------------------------------------------------------------------
// G3: RM 失效次数 = 0 (硬指标, 一次失效重置 2 周窗口)
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG3_RMZeroFail(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id        = GateId::G3_RMZeroFail;
    r.threshold = static_cast<double>(kG3_RMFailMax);
    if (m.rm_failure_count < 0) {
        r.error = EvalError::InvalidInput;
        r.note  = "rm_failure_count negative";
        return r;
    }
    r.actual = static_cast<double>(m.rm_failure_count);
    r.pass   = (m.rm_failure_count == kG3_RMFailMax);
    return r;
}

// ---------------------------------------------------------------------------
// G4: uptime / total >= 0.995
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG4_Uptime(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id        = GateId::G4_Uptime;
    r.threshold = kG4_UptimeMin;
    if (!std::isfinite(m.uptime_seconds) || !std::isfinite(m.total_window_seconds) ||
        m.uptime_seconds < 0.0 || m.total_window_seconds <= 0.0 ||
        m.uptime_seconds > m.total_window_seconds + 1.0e-9) {
        r.error = EvalError::InvalidInput;
        r.note  = "uptime / total invalid";
        return r;
    }
    r.actual = m.uptime_seconds / m.total_window_seconds;
    r.pass   = (r.actual >= kG4_UptimeMin);
    return r;
}

// ---------------------------------------------------------------------------
// G5: max DD <= 8% (peak-to-trough on equity curve)
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG5_MaxDD(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id        = GateId::G5_MaxDD;
    r.threshold = kG5_MaxDDMax;
    r.sample_size = m.equity_curve_usdc.size();

    if (!AllFinite(m.equity_curve_usdc)) {
        r.error = EvalError::InvalidInput;
        r.note  = "equity_curve_usdc contains NaN/Inf";
        return r;
    }
    if (r.sample_size == 0) {
        r.error = EvalError::InsufficientSample;
        r.note  = "empty equity curve";
        return r;
    }
    double peak = m.equity_curve_usdc[0];
    double max_dd = 0.0;
    for (double eq : m.equity_curve_usdc) {
        if (eq > peak) peak = eq;
        if (peak > 0.0) {
            const double dd = (peak - eq) / peak;
            if (dd > max_dd) max_dd = dd;
        } else {
            // peak <= 0 → equity 一直不为正, 无法定义 % DD. 视为失败.
            r.error = EvalError::InvalidInput;
            r.note  = "non-positive peak in equity curve";
            return r;
        }
    }
    r.actual = max_dd;
    r.pass   = (max_dd <= kG5_MaxDDMax);
    return r;
}

// ---------------------------------------------------------------------------
// G6: trades >= 50
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG6_MinTrades(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id          = GateId::G6_MinTrades;
    r.threshold   = static_cast<double>(kG6_TradesMin);
    r.sample_size = m.per_trade_pnl_usdc.size();
    r.actual      = static_cast<double>(r.sample_size);
    r.pass        = (r.sample_size >= kG6_TradesMin);
    return r;
}

// ---------------------------------------------------------------------------
// G7: Welch two-sample one-sided t-test, H1: paper > random, p < 0.05
// ---------------------------------------------------------------------------
GateResult GateEvaluator::EvalG7_AboveRandom(const GateMetrics& m) noexcept {
    GateResult r{};
    r.id        = GateId::G7_AboveRandom;
    r.threshold = kG7_PValueMax;
    r.sample_size = m.per_trade_pnl_usdc.size();

    if (!AllFinite(m.per_trade_pnl_usdc) || !AllFinite(m.random_baseline_pnl_usdc)) {
        r.error = EvalError::InvalidInput;
        r.note  = "paper / random series contain NaN/Inf";
        return r;
    }
    if (m.per_trade_pnl_usdc.size() < 2 || m.random_baseline_pnl_usdc.size() < 2) {
        r.error = EvalError::InsufficientSample;
        r.note  = "n<2 on either arm";
        return r;
    }
    const auto p = ComputeSampleStats(m.per_trade_pnl_usdc);
    const auto q = ComputeSampleStats(m.random_baseline_pnl_usdc);
    const double s1sq_over_n1 = (p.std * p.std) / static_cast<double>(p.n);
    const double s2sq_over_n2 = (q.std * q.std) / static_cast<double>(q.n);
    const double denom = std::sqrt(s1sq_over_n1 + s2sq_over_n2);
    if (denom < 1.0e-12) {
        // 双方无波动 → 取 mean 差直判
        r.actual  = (p.mean > q.mean) ? 0.0 : 1.0;   // 把 p-value 这一位表达 "显著 / 不显著"
        r.p_value = r.actual;
        r.pass    = (p.mean > q.mean);
        r.note    = "zero variance edge case";
        return r;
    }
    const double t = (p.mean - q.mean) / denom;
    // Satterthwaite df
    const double df_num = (s1sq_over_n1 + s2sq_over_n2) * (s1sq_over_n1 + s2sq_over_n2);
    const double df_den = (s1sq_over_n1 * s1sq_over_n1) / static_cast<double>(p.n - 1) +
                          (s2sq_over_n2 * s2sq_over_n2) / static_cast<double>(q.n - 1);
    const double df = (df_den > 0.0) ? (df_num / df_den) : 1.0;
    const double p1 = StudentTPValueOneSidedGreater(t, df);

    r.actual  = p1;
    r.p_value = p1;
    r.pass    = (p1 < kG7_PValueMax) && (p.mean > q.mean);
    return r;
}

// ---------------------------------------------------------------------------
// EvaluateAll
// ---------------------------------------------------------------------------
GateOutcome GateEvaluator::EvaluateAll(const GateMetrics& m) noexcept {
    GateOutcome out{};

    const bool pit_ok = CheckPit(m);
    if (!pit_ok) {
        // 全 7 gate 标 PitViolation, pass=false
        for (std::size_t i = 0; i < kNumGates; ++i) {
            GateResult r{};
            r.id    = static_cast<GateId>(static_cast<std::uint8_t>(i) + 1);
            r.pass  = false;
            r.error = EvalError::PitViolation;
            r.note  = "R-20 4 ts inequality violated";
            out.per_gate[i] = r;
        }
        out.pass_count  = 0;
        out.all_pass    = false;
        out.first_error = EvalError::PitViolation;
        return out;
    }

    out.per_gate[0] = EvalG1_PnLTTest(m);
    out.per_gate[1] = EvalG2_SharpeBootstrap(m);
    out.per_gate[2] = EvalG3_RMZeroFail(m);
    out.per_gate[3] = EvalG4_Uptime(m);
    out.per_gate[4] = EvalG5_MaxDD(m);
    out.per_gate[5] = EvalG6_MinTrades(m);
    out.per_gate[6] = EvalG7_AboveRandom(m);

    for (const auto& r : out.per_gate) {
        if (r.pass) ++out.pass_count;
        if (out.first_error == EvalError::Ok && r.error != EvalError::Ok) {
            out.first_error = r.error;
        }
    }
    out.all_pass = (out.pass_count == kNumGates);
    return out;
}

}  // namespace stcpp::stats
