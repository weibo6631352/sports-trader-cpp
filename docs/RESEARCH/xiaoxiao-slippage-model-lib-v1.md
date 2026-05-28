# SlippageModel C++ Header-only Lib Spec v1

- Owner: 小肖 (quant-numerical-algo) | Date: 2026-05-28 | Sprint-2 W1
- 关联: `xiaoxiao-kelly-slippage-model-v1.md` §3.1/§5, sprint1-retro `xiaoxiao-speech.md` §5
- 上游: 老韩 RM v0.2 §3.10 `INVALID_INTENT` | 下游: RM 同步路径 + 小蒋 VirtualMatcher
- 状态: 待 RM + paper engine PR review 联签

---

## 1. Header 接口 Spec

**路径:** `include/numerical/slippage_model.hpp` (header-only / 零依赖 / constexpr 友好 / noexcept / 零 heap)

```cpp
#pragma once
#include <cstdint>
#include <cmath>
namespace stcpp::numerical {

enum class SlippageMode : uint8_t { Linear = 0, Sqrt = 1, Clob = 2 };  // v1 仅 Linear
enum class Confidence  : uint8_t { HIGH = 0, MEDIUM = 1, LOW = 2 };
enum class RejectCode  : uint8_t {
    OK                    = 0,
    INVALID_INTENT        = 1,   // 与老韩 v0.2 §3.10 同名: nan/inf/neg/ts==0/stale/illegal-tick
    EXCEED_BOOK_DEPTH     = 2,   // rho > RHO_MAX
    FILL_RATE_BELOW_FLOOR = 3,   // expected_fill_rate < FILL_RATE_FLOOR
};

struct SlippageInput {
    double  order_size_usdc;       // > 0
    double  quote_price;           // (eps, 1-eps)
    double  book_depth_l1_usdc;    // > 0
    int64_t book_snapshot_ts_ns;   // > 0 且 wall_now - ts <= STALE_MAX_NS
    int64_t wall_now_ns;           // 调用方注入, 单测 deterministic
    double  tick_size;             // {0.001, 0.01}
};

struct SlippageOutput {
    double      expected_fill_price = 0.0;   // VWAP
    double      expected_fill_rate  = 0.0;   // [0,1]
    int32_t     slippage_bps        = 0;     // (pf - pq)/pq * 10000
    Confidence  conf                = Confidence::LOW;
    RejectCode  reject              = RejectCode::OK;
};

class SlippageModel {
 public:
    static constexpr double  FILL_RATE_FLOOR         = 0.50;
    static constexpr double  RHO_MAX                 = 3.0;
    static constexpr int     MAX_SLIPPAGE_TICKS      = 3;
    static constexpr double  KAPPA_DEPTH_GAMEDAY     = 1.0;   // 小袁 microstructure v1 校准
    static constexpr int64_t T_HALFLIFE_QUOTE_MS     = 30000; // gameday 30s
    static constexpr int64_t T_HALFLIFE_QUOTE_HOT_MS = 500;   // 关键事件 0.5s
    static constexpr double  BETA_WITHDRAW           = 0.3;
    static constexpr int64_t STALE_MAX_NS            = 60'000'000'000LL;
    static constexpr double  EPS                     = 1e-6;

    [[nodiscard]] static SlippageOutput
    compute(SlippageInput const& in,
            SlippageMode mode = SlippageMode::Linear) noexcept;

 private:
    static inline bool validate(SlippageInput const&) noexcept;
    static inline SlippageOutput compute_linear(SlippageInput const&) noexcept;
};

}  // namespace
```

**对齐老韩 v0.2 §3.10:** `INVALID_INTENT` 覆盖 5 子条件 (ts==0 / stale>60s / nan / neg / illegal-tick). RM 看到 `reject != OK` 直接映射 `RiskDecision.reject_reason`, 无需二次判断.

---

## 2. 7 单测 Case (Catch2, 文件 `tests/numerical/test_slippage_model.cpp`)

baseline: `wall_now_ns = 1'700'000'000'000'000'000LL`, `tick_size = 0.01` 除非另注.

| # | 场景 | input 关键字段 | 期望输出 |
|---|---|---|---|
| 1 | $2K gameday 0¢ (小袁实测中位) | size=2000, q=0.50, L1=2500, Δt=200ms | reject=OK, pf≈0.504, fill_rate≥0.85, conf=HIGH |
| 2 | $10K gameday 0.4-0.9¢ | size=10000, q=0.55, L1=8000, Δt=500ms | reject=OK, pf∈[0.5604,0.5690], fill_rate≈0.78, conf=MEDIUM |
| 3 | 边界 ρ→ρ_max | size=16000, L1=5000 (ρ=3.2) | reject=EXCEED_BOOK_DEPTH; 子测试 ρ=2.999 过 / ρ=3.001 拒 (fuzz v1 §8.4 R-B) |
| 4 | 数值稳定 p→0 (catastrophic cancellation) | size=500, q=0.01, L1=5000, tick=0.001 | reject=OK, pf≈0.01005, ULP < 4·ε·q, 无 NaN/Inf |
| 5 | paper case 0.58/0.55/0.585 | size=5000, q=0.55, L1=1670 (ρ≈3) | reject=OK, slippage_bps ≥ 300 (3+ ticks); Kelly EDGE_NEGATED 由 RM 集成测试覆盖 |
| 6 | fill_rate < 0.50 floor | size=10000, q=0.55, L1=4000, Δt=3000ms | reject=FILL_RATE_BELOW_FLOOR (rho=2.5, s_stale≈0.095, pi_w≈0.528, fr≈0.171) |
| 7 | INVALID_INTENT 退化 (5 子测试) | 7a ts=0 / 7b ts=now-90s / 7c q=NaN / 7d L1=-1 / 7e tick=0.005 | 全部 reject=INVALID_INTENT |

**Case 4 数值验证细节:** Kelly 公式必须用 `(q_low - p)/(1-p)` 而非 `(b*q - (1-q))/b` 形式 (v1 §1.3, p→0 时第二种相对误差 1e-9, 第一种 1e-16); 但 lib 内只算 pf, Kelly 在 RM 内, 本 case 仅验 pf 无 NaN/Inf 且 ULP 内.

**Case 5 简化:** lib unit 不覆盖 EDGE_NEGATED (跨 Kelly 调用方), 仅断言 pf >= quote + 3·tick → `slippage_bps ≥ 300`. 真 EDGE_NEGATED 在老韩 RM 集成测覆盖.

---

## 3. Google Benchmark 配置

文件: `benchmarks/numerical/bench_slippage_model.cpp` | 编译: `-O2 -march=native -DNDEBUG`

```cpp
static void BM_Slippage_OneLevel(benchmark::State& s) {
    SlippageInput in{2000.0, 0.50, 2500.0,
                     1'700'000'000'000'000'000LL,
                     1'700'000'000'200'000'000LL, 0.01};
    for (auto _ : s) {
        auto out = SlippageModel::compute(in);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_Slippage_OneLevel);
// 另: BM_Slippage_MultiLevel (rho=2), BM_Slippage_Reject_InvalidIntent (ts=0)
```

**验收门槛 (p99):**
| Bench | median | **p99** |
|---|---|---|
| OneLevel | ≤ 50ns | **≤ 200ns** |
| MultiLevel | ≤ 80ns | ≤ 200ns |
| Reject_InvalidIntent | ≤ 20ns | ≤ 50ns |

**预算分摊:** 200ns p99 占老韩 RM 同步路径 6us 预算 **3.3%**, 留 5.8us 给 R0-R7 + audit hand-off + Kelly compute.

---

## 4. 与小蒋 paper engine 共享 (Sprint-2 W2)

```cpp
// VirtualMatcher 调用约定
auto out = stcpp::numerical::SlippageModel::compute(input);
if (out.reject != RejectCode::OK)
    return SimFill{.status=Reject, .reason=map_to_paper_reject(out.reject)};
// out.expected_fill_price → paper fill 价; out.expected_fill_rate → partial fill 决策
```

**Sprint-2 W3 小袁回灌:** Polymarket 实测后修订 `KAPPA_DEPTH_GAMEDAY` / `T_HALFLIFE_QUOTE_MS`, 三签 (小肖+小蒋+小梁) bump v1.1. config 4 常量 backtest/paper/prod 必须 hash 一致 (老周 §15.6 R-12).

---

## 5. 命名碰撞决议 (Sprint-2 W1 GM)

| Namespace | Enum | 值 |
|---|---|---|
| `stcpp::numerical` | `SlippageMode` | `Linear`, `Sqrt`, `Clob` |
| `stcpp::execution` | `PaperMode`    | `Sim`, `Hybrid`, `Real` |

禁用 "Mode A/B/C" 字母代号. PR review 检查项.

---

## 6. 不耻下问 (待 ACK)

| # | 事项 | 找谁 | 截止 |
|---|---|---|---|
| 1 | RM v0.2 §3.10 `INVALID_INTENT` 覆盖 5 子条件确认 | 老韩 | Sprint-2 W1 末 |
| 2 | `KAPPA_DEPTH_GAMEDAY = 1.0` 与 microstructure v1 §3 一致 | 小袁 | Sprint-2 W1 |
| 3 | VirtualMatcher 复用 + `map_to_paper_reject()` 映射表 | 小蒋 | Sprint-2 W2 |
| 4 | `MAX_SLIPPAGE_TICKS = 3` 软监控位逻辑 (RM 还是策略层处理) | 老韩 + 小梁 | Sprint-2 W1 |

---

**END v1.** Sprint-2 W1 交付: header lib + 7 单测 + 3 bench. Sprint-2 W3 小袁回灌后 bump v1.1.

— 小肖, 2026-05-28
