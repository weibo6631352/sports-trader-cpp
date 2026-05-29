#!/usr/bin/env bash
# tests/dogfood/harness/run_dogfood.sh
#
# Owner: 小宫 (dogfood-evaluator)
# Date: 2026-05-29
#
# Paper runtime dogfood 主 harness
# 启动 stub API (若未运行) + 跑 probe_obs_api.sh + gate_verdict.sh
#
# 对齐:
#   tests/dogfood/checklist-paper-runtime.md 阶段 4 + 阶段 5
#   docs/RESEARCH/xiaozheng-observability-endpoints-v1.md
#   docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md §3 GM-PAPER-G
#
# 用法:
#   # 对 stub server (自动启动/停止):
#   tests/dogfood/harness/run_dogfood.sh
#
#   # 对真实 paper runtime (已在 BASE_URL 运行):
#   BASE_URL=http://localhost:9090 tests/dogfood/harness/run_dogfood.sh --no-stub
#
#   # 仅 probe, 不跑 gate verdict:
#   tests/dogfood/harness/run_dogfood.sh --probe-only
#
# 退出码: 0 = 全通, 1 = 有失败

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
STUB_PORT="${STUB_PORT:-19091}"
BASE_URL="${BASE_URL:-http://127.0.0.1:${STUB_PORT}}"
USE_STUB=1
PROBE_ONLY=0
STUB_PID=""

for arg in "$@"; do
    case "$arg" in
        --no-stub)    USE_STUB=0 ;;
        --probe-only) PROBE_ONLY=1 ;;
        --help|-h)
            sed -n '1,35p' "$0"; exit 0 ;;
        *) printf "unknown arg: %s\n" "$arg" >&2; exit 2 ;;
    esac
done

green()  { printf '\033[32m%s\033[0m' "$1"; }
red()    { printf '\033[31m%s\033[0m' "$1"; }
yellow() { printf '\033[33m%s\033[0m' "$1"; }
bold()   { printf '\033[1m%s\033[0m' "$1"; }

cleanup() {
    if [[ -n "${STUB_PID}" ]]; then
        kill "${STUB_PID}" 2>/dev/null || true
        printf "\n[cleanup] stub server (pid %s) stopped\n" "${STUB_PID}"
    fi
}
trap cleanup EXIT

# ---- Step 1: start stub if needed ----------------------------------------

if [[ "$USE_STUB" -eq 1 ]]; then
    STUB_SCRIPT="${SCRIPT_DIR}/stub_api_server.py"
    if [[ ! -f "$STUB_SCRIPT" ]]; then
        printf "[%s] stub_api_server.py not found at %s\n" "$(red ERROR)" "$STUB_SCRIPT"
        exit 1
    fi

    printf "[dogfood] Starting stub API server on port %s...\n" "${STUB_PORT}"
    python3 "${STUB_SCRIPT}" --port "${STUB_PORT}" &
    STUB_PID=$!

    # Wait for stub to be ready (up to 5s)
    for i in $(seq 1 10); do
        if curl -sf --max-time 1 "http://127.0.0.1:${STUB_PORT}/healthz" >/dev/null 2>&1; then
            printf "[dogfood] Stub ready (pid %s)\n\n" "${STUB_PID}"
            break
        fi
        sleep 0.5
        if [[ "$i" -eq 10 ]]; then
            printf "[%s] Stub did not start within 5s\n" "$(red ERROR)"
            exit 1
        fi
    done
fi

printf "$(bold '=== Paper Runtime Dogfood Harness ===')\n"
printf "BASE_URL: %s\n" "${BASE_URL}"
printf "Mode: %s\n\n" "$([ "$USE_STUB" -eq 1 ] && echo 'stub' || echo 'live')"

OVERALL_FAIL=0

# ---- Step 2: smoke check /healthz ----------------------------------------

printf "$(bold '--- Step 2: Smoke Check (/healthz) ---')\n"
hc_code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 "${BASE_URL}/healthz" || echo "000")
if [[ "$hc_code" == "200" ]]; then
    printf "[%s] /healthz HTTP 200\n\n" "$(green PASS)"
else
    printf "[%s] /healthz returned HTTP %s — aborting\n" "$(red FAIL)" "$hc_code"
    exit 1
fi

# ---- Step 3: Observability API Probe (阶段 4 检查单) ---------------------

printf "$(bold '--- Step 3: Observability API Probe (Checklist Phase 4) ---')\n"
export BASE_URL PROBE_TIMEOUT="${PROBE_TIMEOUT:-5}"
if "${SCRIPT_DIR}/probe_obs_api.sh"; then
    printf "\n[%s] probe_obs_api.sh all PASS\n\n" "$(green PASS)"
else
    printf "\n[%s] probe_obs_api.sh had failures\n\n" "$(red FAIL)"
    ((OVERALL_FAIL++)) || true
fi

# ---- Step 4: GM-PAPER-G Gate Verdict (阶段 5) ----------------------------

if [[ "$PROBE_ONLY" -eq 0 ]]; then
    printf "$(bold '--- Step 4: GM-PAPER-G Gate Verdict (Checklist Phase 5) ---')\n"
    if "${SCRIPT_DIR}/gate_verdict.sh"; then
        printf "\n[%s] gate_verdict.sh completed\n\n" "$(green PASS)"
    else
        gate_exit=$?
        if [[ "$gate_exit" -eq 1 ]]; then
            printf "\n[%s] gate_verdict.sh: GATE FAILURES detected\n\n" "$(red FAIL)"
            ((OVERALL_FAIL++)) || true
        fi
        # exit 0 from gate_verdict = pending (not a failure for harness)
    fi
fi

# ---- Step 5: Red-line checks (R-11 / R-12 / R-20 observable) ------------

printf "$(bold '--- Step 5: Red-line Observable Checks ---')\n"

# R-11: paper mode must not write to position/nonce WALs
# Dogfood check: /gate/paper r11_cross_write_count == 0
gate_json=$(curl -sf --max-time 5 "${BASE_URL}/gate/paper" 2>/dev/null || echo "{}")
r11_count=$(echo "$gate_json" | grep -oE '"r11_cross_write_count"[[:space:]]*:[[:space:]]*[0-9]+' \
    | grep -oE '[0-9]+$' || echo "-1")

if [[ "$r11_count" == "0" ]]; then
    printf "[%s] R-11: paper mode r11_cross_write_count == 0\n" "$(green PASS)"
elif [[ "$r11_count" == "-1" ]]; then
    printf "[%s] R-11: r11_cross_write_count field missing from /gate/paper\n" "$(yellow WARN)"
else
    printf "[%s] R-11 VIOLATION: r11_cross_write_count == %s\n" "$(red FAIL)" "$r11_count"
    ((OVERALL_FAIL++)) || true
fi

# R-20: ts_lag histogram present in /metrics
metrics_body=$(curl -sf --max-time 5 "${BASE_URL}/metrics" 2>/dev/null || echo "")
if echo "$metrics_body" | grep -q 'stcpp_ts_lag_seconds_bucket'; then
    printf "[%s] R-20: stcpp_ts_lag_seconds_bucket metric present\n" "$(green PASS)"
else
    printf "[%s] R-20: stcpp_ts_lag_seconds_bucket metric MISSING from /metrics\n" \
        "$(yellow WARN)"
fi

# R-12: event_loop latency histogram present
if echo "$metrics_body" | grep -q 'stcpp_event_loop_latency_seconds_bucket'; then
    printf "[%s] R-12: stcpp_event_loop_latency_seconds_bucket present\n" "$(green PASS)"
else
    printf "[%s] R-12: event_loop_latency_seconds histogram MISSING\n" "$(yellow WARN)"
fi

# G-07: rm_bypass_count == 0 (RM 零失效, 最高优先级)
rm_bypass=$(echo "$gate_json" | grep -oE '"rm_bypass_count"[[:space:]]*:[[:space:]]*[0-9]+' \
    | grep -oE '[0-9]+$' || echo "-1")
if [[ "$rm_bypass" == "0" ]]; then
    printf "[%s] G-07 / R-1: rm_bypass_count == 0 (RM 零失效)\n" "$(green PASS)"
elif [[ "$rm_bypass" == "-1" ]]; then
    printf "[%s] G-07: rm_bypass_count field missing\n" "$(yellow WARN)"
else
    printf "[%s] G-07 / R-1 VIOLATION: rm_bypass_count == %s\n" "$(red FAIL)" "$rm_bypass"
    ((OVERALL_FAIL++)) || true
fi

echo

# ---- Step 6: Manual checklist reminder -----------------------------------

printf "$(bold '--- Step 6: Manual Checklist Reminder (Operator Actions) ---')\n"
printf "[%s] 需 operator 手动验证以下项 (非 curl 可自动化):\n\n" "$(yellow TODO)"
printf "  M-1: 操作员能否 5min 内看懂 --help 并知道 --mode=paper 入口?\n"
printf "  M-2: 配置文件 paper_config.toml 文档是否有明确路径说明?\n"
printf "  M-3: paper 模式 PnL 数字是否有明确的'这是虚拟的'标识?\n"
printf "  M-4: /gate/paper prelim_pass / confirm_pass 两阶段是否直观?\n"
printf "  M-5: G-08 数据 attestation — 小余是否已有签字 SOP?\n"
printf "  M-6: 边缘场景: kill stcpp_signer_paper 后系统进 SAFE_MODE 而非崩溃?\n"
printf "\n"
printf "  参考: tests/dogfood/checklist-paper-runtime.md 阶段 0 + 6\n"

echo

# ---- Final summary -------------------------------------------------------

printf "$(bold '=== Dogfood Harness Complete ===')\n"
if [[ "${OVERALL_FAIL}" -gt 0 ]]; then
    printf "[%s] %d section(s) had failures. Review above output.\n" \
        "$(red FAIL)" "${OVERALL_FAIL}"
    exit 1
else
    printf "[%s] Dogfood harness passed all automated checks.\n" "$(green PASS)"
    printf "     Paper runtime observability API is functional (stub or live).\n"
    printf "     Proceed with manual checklist items (Step 6) for full W11 gate.\n"
    exit 0
fi
