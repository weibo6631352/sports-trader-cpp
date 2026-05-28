# tests/perf/ — 性能回归 framework (老姜 W4 Wave 21)

Sprint-2 W4 Wave 21. 配合:
- `docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md` — 9 模块 budget + 红线
- `.github/workflows/perf-regression.yml` — PR CI gate (p99 退化 > 10% 拦)
- `scripts/perf_compare.py` — JSON 比对 + markdown diff

---

## 跑法 (本地)

```bash
# 1. 配置 (Release + STCPP_BUILD_BENCH=ON)
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DSTCPP_EXEC_MODE=paper \
    -DSTCPP_BUILD_BENCH=ON \
    -DBUILD_TESTING=ON

# 2. 编译 6 bench target
cmake --build build --parallel 4 --target \
    bench_slippage \
    bench_risk_gateway \
    bench_audit_emitter \
    bench_paper_signer \
    bench_virtual_matcher \
    bench_goalserve_parse

# 3. 跑全 (ctest label perf)
cd build && ctest -L perf --output-on-failure

# 4. 跑单, 输出 JSON 给 perf_compare
mkdir -p build/perf-results
for B in slippage risk_gateway audit_emitter paper_signer virtual_matcher goalserve_parse; do
    ./build/tests/perf/bench_$B \
        --benchmark_format=json \
        --benchmark_out=build/perf-results/${B}.json \
        --benchmark_min_time=0.5s \
        --benchmark_repetitions=5 \
        --benchmark_report_aggregates_only=true
done

# 5. 比对 baseline
python3 scripts/perf_compare.py build/perf-results tests/perf/baselines/main.json
```

---

## 6 bench 目录

| # | bench | 模块 | 预算 p99 | Owner |
|---|---|---|---|---|
| M1 | `bench_slippage.cpp` | numerical::SlippageModel | 50ns | 小肖 |
| M2 | `bench_risk_gateway.cpp` | risk::RiskGateway::evaluate | 100us | 老韩 |
| M3 | `bench_audit_emitter.cpp` | observability::AuditEmitter::emit_decision | 50us | 老唐 |
| M4 | `bench_paper_signer.cpp` | signer::paper::PaperSigner::Sign | 50us (sign-only) | 小蒋 |
| M5 | `bench_virtual_matcher.cpp` | execution::VirtualMatcher::Match | 30us | 小蒋 |
| M6 | `bench_goalserve_parse.cpp` | data::goalserve::GoalserveClient helpers | 5us / 2us / 50ns | 小段 |

W5 Wave 22 补:
- M7/M8 (WAL Append + group commit fsync) — 老王
- M9 (Signal P0-01 tick) — 小卢/小程

---

## Baselines

`tests/perf/baselines/main.json` 是 main branch tip 跑出的 JSON, 由 CI 在 main push 时
更新, **不要手动改**.

如果 PR 故意慢 (新增规则等), 在 PR description 写:

```
perf-waiver: <module> +<X>%  reason: <一句话>
```

老姜 review 批.

---

## 红线 (CI gate)

| 指标 | warning | PR block | P0 |
|---|---|---|---|
| 模块 p99 (cpu_time) | +5% | **+10%** | +20% |
| 任一 bench 编译/跑失败 | — | **是** | — |

参考 `docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md` §4.
