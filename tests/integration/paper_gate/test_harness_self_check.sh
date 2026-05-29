#!/usr/bin/env bash
# GM-PAPER-G Harness 自检脚本
#
# Owner: 小颖 (requirements-analyst, E 产品业务保障部)
# Date:  2026-05-29
#
# 验证 run_gate_harness.sh 对各 fixture 的行为符合预期:
#   PASS fixture  → 退出码 0
#   FAIL fixture  → 退出码 1
#
# 用法: ./test_harness_self_check.sh
# 输出: 每个测试 case PASS/FAIL + 最终通过率

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HARNESS="$SCRIPT_DIR/run_gate_harness.sh"
FIXTURE_DIR="$SCRIPT_DIR/fixtures"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

pass_count=0
fail_count=0

check_exit() {
    local case_name="$1"
    local expected_exit="$2"
    shift 2
    local actual_exit=0

    # 运行 harness, 捕获退出码 (禁止 set -e 影响)
    if "$HARNESS" "$@" >/dev/null 2>&1; then
        actual_exit=0
    else
        actual_exit=$?
    fi

    if [[ "$actual_exit" == "$expected_exit" ]]; then
        echo -e "  ${GREEN}PASS${NC} [$case_name] exit=$actual_exit (expected $expected_exit)"
        ((pass_count++)) || true
    else
        echo -e "  ${RED}FAIL${NC} [$case_name] exit=$actual_exit (expected $expected_exit)"
        ((fail_count++)) || true
    fi
}

echo ""
echo "================================================================"
echo "  GM-PAPER-G Harness 自检"
echo "  Date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "================================================================"
echo ""

# ── Case 1: PASS fixture 全部通过 ──────────────────────────────────────────
echo "Case 1: PASS fixture (all gates pass)"
check_exit "pass_fixture_exit_0" 0 \
    --mode file \
    --report "$FIXTURE_DIR/paper_report_30d_pass.json" \
    --soft-validation "$FIXTURE_DIR/soft_validation_pass.json"

# ── Case 2: G-04 FAIL (max_daily_loss > 3%) ───────────────────────────────
echo "Case 2: G-04 FAIL (max_daily_loss_pct=0.035)"
check_exit "g04_fail_exit_1" 1 \
    --mode file \
    --report "$FIXTURE_DIR/paper_report_30d_fail_g04.json" \
    --soft-validation "$FIXTURE_DIR/soft_validation_pass.json"

# ── Case 3: G-08 attestation FAIL → G-02 强制 FAIL ────────────────────────
echo "Case 3: G-08 attestation FAIL (小余否决 → G-02 强制 FAIL)"
check_exit "g08_attestation_fail_exit_1" 1 \
    --mode file \
    --report "$FIXTURE_DIR/paper_report_30d_fail_g08.json" \
    --soft-validation "$FIXTURE_DIR/soft_validation_pass.json"

# ── Case 4: G-07 reject_rate < 0.08 FAIL ─────────────────────────────────
echo "Case 4: G-07 reject_rate=0.05 < 0.08 (低于区间下限)"
check_exit "g07_reject_rate_low_exit_1" 1 \
    --mode file \
    --report "$FIXTURE_DIR/paper_report_30d_fail_g07_reject_rate.json" \
    --soft-validation "$FIXTURE_DIR/soft_validation_pass.json"

# ── Case 5: PASS fixture 不带 soft_validation → SKIP (exit 1) ─────────────
echo "Case 5: PASS fixture without --soft-validation (G-00 SKIP → exit 1)"
check_exit "pass_without_g00_exit_1" 1 \
    --mode file \
    --report "$FIXTURE_DIR/paper_report_30d_pass.json"

echo ""
echo "================================================================"
echo "  自检结果: PASS=$pass_count  FAIL=$fail_count"
echo "================================================================"

if [[ $fail_count -eq 0 ]]; then
    echo -e "  ${GREEN}ALL SELF-CHECKS PASSED${NC}"
    exit 0
else
    echo -e "  ${RED}SELF-CHECK FAILED ($fail_count failures)${NC}"
    exit 1
fi
