#!/usr/bin/env bash
# scripts/run_perf_baseline.sh — 老姜 W6 Wave 28 perf baseline runner
# 跑全 bench (W4 Wave 21 原 6 + W6 Wave 28 新 2) + 生成 W6 baseline.json
# 用法: bash scripts/run_perf_baseline.sh [--build-dir <path>] [--compare]
# 前置: cmake -S . -B build -DSTCPP_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release -DSTCPP_EXEC_MODE=paper
#        cmake --build build --target bench_slippage bench_risk_gateway bench_audit_emitter \
#              bench_paper_signer bench_virtual_matcher bench_goalserve_parse \
#              bench_e2e_latency bench_spsc_latency
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
COMPARE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --compare)   COMPARE=1; shift ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done
RESULTS="${BUILD_DIR}/perf-results"; mkdir -p "${RESULTS}"
BENCHES=(slippage risk_gateway audit_emitter paper_signer virtual_matcher goalserve_parse e2e_latency spsc_latency)
FAILED=()
for B in "${BENCHES[@]}"; do
    BIN="${BUILD_DIR}/tests/perf/bench_${B}"
    [[ -x "${BIN}" ]] || { echo "[SKIP] bench_${B} not built"; continue; }
    echo "[RUN] bench_${B}"
    "${BIN}" --benchmark_format=json --benchmark_out="${RESULTS}/${B}.json" \
             --benchmark_min_time=1.0s --benchmark_repetitions=5 \
             --benchmark_report_aggregates_only=true \
             --benchmark_enable_random_interleaving=true --benchmark_color=false \
    || FAILED+=("${B}")
done
# 合并 W6 baseline JSON (Python 离线脚本 — CLAUDE.md §10 合规)
python3 - "${RESULTS}" <<'PY'
import json, sys, datetime
from pathlib import Path
d = Path(sys.argv[1]); merged = {"context": {"wave": "W6", "date": str(datetime.date.today())}, "benchmarks": []}
for f in sorted(d.glob("*.json")):
    if f.name == "w6-baseline.json": continue
    try:
        for b in json.loads(f.read_text()).get("benchmarks", []): merged["benchmarks"].append(b)
    except Exception as e: print(f"warn {f}: {e}", file=sys.stderr)
out = d / "w6-baseline.json"; out.write_text(json.dumps(merged, indent=2))
print(f"[baseline] {out} ({len(merged['benchmarks'])} benchmarks)")
PY
[[ "${COMPARE}" -eq 1 ]] && python3 "${REPO_ROOT}/scripts/perf_compare.py" "${RESULTS}" \
    "${REPO_ROOT}/tests/perf/baselines/main.json" --threshold 0.10 --warn-threshold 0.05
[[ ${#FAILED[@]} -gt 0 ]] && { echo "[ERROR] failed: ${FAILED[*]}"; exit 1; }
echo "[OK] W6 baseline: ${RESULTS}/w6-baseline.json"
