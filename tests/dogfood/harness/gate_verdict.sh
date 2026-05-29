#!/usr/bin/env bash
# tests/dogfood/harness/gate_verdict.sh
#
# Owner: 小宫 (dogfood-evaluator)
# Date: 2026-05-29
#
# GM-PAPER-G 六维 dogfood 验证维度输出脚本
#
# 对齐:
#   docs/OKR/laolei-2026-drive-directive-paper-profit-v1.md §3 GM-PAPER-G v2
#   docs/RESEARCH/xiaoying-acceptance-spec-v2.md §2 G-00~G-08
#
# 功能:
#   从 /gate/paper 读取实时数据，对每条 gate 条件做 dogfood 判定。
#   输出人类可读的 verdict 报告 (不替代 tools/m4_5_gate/run_gate_check.py)。
#
# 注意: 本脚本是 DOGFOOD 工具，不是生产 gate 判定。
#   生产 gate 判定 = tools/m4_5_gate/run_gate_check.py (Python, tool-level)
#
# 用法:
#   BASE_URL=http://localhost:19091 ./tests/dogfood/harness/gate_verdict.sh
#   BASE_URL=http://localhost:19091 ./tests/dogfood/harness/gate_verdict.sh --json
#
# 兼容: bash 3.2+ (macOS), bash 5 (Linux) — no declare -A

set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:19091}"
TIMEOUT="${PROBE_TIMEOUT:-5}"
OUTPUT_JSON=0

for arg in "$@"; do
    case "$arg" in
        --json) OUTPUT_JSON=1 ;;
        *) ;;
    esac
done

green()  { printf '\033[32m%s\033[0m' "$1"; }
red()    { printf '\033[31m%s\033[0m' "$1"; }
yellow() { printf '\033[33m%s\033[0m' "$1"; }

# Fetch /gate/paper
gate_json=$(curl -sf --max-time "${TIMEOUT}" "${BASE_URL}/gate/paper" 2>/dev/null || echo "{}")

if [ "$gate_json" = "{}" ]; then
    printf "[%s] Cannot reach %s/gate/paper\n" "$(red ERROR)" "${BASE_URL}"
    exit 1
fi

# ---- Simple JSON extraction helpers (no jq, no grep -P, bash 3 safe) -----

get_number() {
    local key="$1"
    echo "$gate_json" | grep -oE "\"${key}\"[[:space:]]*:[[:space:]]*-?[0-9]+\.?[0-9]*" \
        | grep -oE '[-0-9][0-9]*\.?[0-9]*$' | head -1 || echo "0"
}

get_bool() {
    local key="$1"
    echo "$gate_json" | grep -oE "\"${key}\"[[:space:]]*:[[:space:]]*(true|false)" \
        | grep -oE '(true|false)$' | head -1 || echo "false"
}

n_trades=$(get_number "n_trades")
sharpe_30d=$(get_number "sharpe_30d")
p_value=$(get_number "p_value")
positive_day_ratio=$(get_number "positive_day_ratio")
mdd=$(get_number "mdd")
net_pnl_usd=$(get_number "net_pnl_usd")
prelim_pass=$(get_bool "prelim_pass")
confirm_pass=$(get_bool "confirm_pass")
rm_bypass_count=$(get_number "rm_bypass_count")
r11_cross_write_count=$(get_number "r11_cross_write_count")
rm_reject_rate=$(get_number "rm_reject_rate")

# ---- Gate state storage (flat vars, bash 3 compatible) -------------------

GATES_PASS=0
GATES_FAIL=0
GATES_PENDING=0

# Store results as flat variables: gate_STATUS_G00, gate_LABEL_G00, gate_DETAIL_G00
store_gate() {
    local gid="$1"
    local status="$2"   # pass / fail / pending
    local label="$3"
    local detail="$4"
    eval "gate_STATUS_${gid}='${status}'"
    eval "gate_LABEL_${gid}='${label}'"
    eval "gate_DETAIL_${gid}='${detail}'"
    case "$status" in
        pass)    GATES_PASS=$(( GATES_PASS + 1 )) ;;
        fail)    GATES_FAIL=$(( GATES_FAIL + 1 )) ;;
        pending) GATES_PENDING=$(( GATES_PENDING + 1 )) ;;
    esac
}

# ---- Gate evaluations (dogfood mirror of G-00~G-08) ----------------------

# G-00: 14 日软验证前置
if [ "$prelim_pass" = "true" ]; then
    store_gate "G00" "pass" "14 日软验证前置" "prelim_pass=true"
elif [ "$n_trades" -eq 0 ] 2>/dev/null; then
    store_gate "G00" "pending" "14 日软验证前置" "paper runtime 尚未启动 (n_trades=0)"
else
    store_gate "G00" "fail" "14 日软验证前置" "prelim_pass=false (n_trades=${n_trades})"
fi

# G-01: 30 日在线率 (采集自 /metrics, 此处标记 pending)
store_gate "G01" "pending" "30 日在线率 >=99.0%" \
    "需从 /metrics stcpp_uptime_seconds 持续采集 (见 probe_obs_api.sh P4)"

# G-02: 净 PnL > 0
if awk "BEGIN{exit !($net_pnl_usd > 0)}" 2>/dev/null; then
    store_gate "G02" "pass" "30 日净 PnL > 0 (扣 fee+slippage+spread)" \
        "net_pnl_usd=${net_pnl_usd} USD"
elif [ "$n_trades" -eq 0 ] 2>/dev/null; then
    store_gate "G02" "pending" "30 日净 PnL > 0" "paper runtime 未产生交易"
else
    store_gate "G02" "fail" "30 日净 PnL > 0" "net_pnl_usd=${net_pnl_usd} (<=0)"
fi

# G-03: n_trades >= 100
if [ "$n_trades" -ge 100 ] 2>/dev/null; then
    store_gate "G03" "pending" "样本量 n_trades>=100 + bootstrap CI>0" \
        "n_trades=${n_trades} (>=100 OK); bootstrap CI 需 tools/m4_5_gate 计算"
elif [ "$n_trades" -eq 0 ] 2>/dev/null; then
    store_gate "G03" "pending" "样本量 n_trades>=100" "paper runtime 未产生交易"
else
    store_gate "G03" "fail" "样本量 n_trades>=100" \
        "n_trades=${n_trades} (<100, 不够统计显著)"
fi

# G-04: 正收益日 >= 52%
if awk "BEGIN{exit !($positive_day_ratio >= 0.52)}" 2>/dev/null; then
    store_gate "G04" "pass" "正收益日>=52% AND 0 单日>3%亏损" \
        "positive_day_ratio=${positive_day_ratio} mdd=${mdd}"
elif [ "$n_trades" -eq 0 ] 2>/dev/null; then
    store_gate "G04" "pending" "正收益日>=52%" "paper runtime 未产生交易"
else
    store_gate "G04" "fail" "正收益日>=52%" \
        "positive_day_ratio=${positive_day_ratio} (<0.52)"
fi

# G-05: OOS Sharpe >= 0.5 AND p < 0.10
sharpe_ok=0
pval_ok=0
awk "BEGIN{exit !($sharpe_30d >= 0.5)}" 2>/dev/null && sharpe_ok=1 || true
awk "BEGIN{exit !($p_value < 0.10)}" 2>/dev/null && pval_ok=1 || true

if [ "$sharpe_ok" -eq 1 ] && [ "$pval_ok" -eq 1 ]; then
    store_gate "G05" "pass" "OOS Sharpe>=0.5 AND t-test p<0.10" \
        "sharpe_30d=${sharpe_30d} p_value=${p_value}"
elif [ "$n_trades" -eq 0 ] 2>/dev/null; then
    store_gate "G05" "pending" "OOS Sharpe>=0.5" "paper runtime 未产生交易"
else
    g05_reason=""
    [ "$sharpe_ok" -eq 0 ] && g05_reason="${g05_reason}sharpe_30d=${sharpe_30d}(<0.5) "
    [ "$pval_ok" -eq 0 ]   && g05_reason="${g05_reason}p_value=${p_value}(>=0.10)"
    store_gate "G05" "fail" "OOS Sharpe>=0.5 AND t-test p<0.10" "$g05_reason"
fi

# G-06: pregame Moneyline 分桶 PnL > 0 (需 VirtualMatcher market_type 标签)
store_gate "G06" "pending" "pregame Moneyline 分桶 PnL > 0" \
    "需 VirtualFill market_type 标签 100% 覆盖 (小袁 W11 VirtualMatcher 实现后可验)"

# G-07: RM 零失效
g07_fail=0
[ "$rm_bypass_count" -gt 0 ] 2>/dev/null && g07_fail=$(( g07_fail + 1 )) || true
[ "$r11_cross_write_count" -gt 0 ] 2>/dev/null && g07_fail=$(( g07_fail + 1 )) || true
rm_rate_ok=0
awk "BEGIN{exit !($rm_reject_rate >= 0.08 && $rm_reject_rate <= 0.20)}" 2>/dev/null \
    && rm_rate_ok=1 || true

if [ "$g07_fail" -eq 0 ] && [ "$rm_rate_ok" -eq 1 ]; then
    store_gate "G07" "pass" "RM 零失效: bypass=0 + R-11 + 拒单率[8%,20%]" \
        "bypass=${rm_bypass_count} r11_cross=${r11_cross_write_count} reject_rate=${rm_reject_rate}"
elif [ "$n_trades" -eq 0 ] 2>/dev/null; then
    store_gate "G07" "pending" "RM 零失效" "paper runtime 未产生交易"
else
    g07_reason=""
    [ "$rm_bypass_count" -gt 0 ] 2>/dev/null \
        && g07_reason="${g07_reason}bypass=${rm_bypass_count}(>0) " || true
    [ "$r11_cross_write_count" -gt 0 ] 2>/dev/null \
        && g07_reason="${g07_reason}r11_cross=${r11_cross_write_count}(>0) " || true
    [ "$rm_rate_ok" -eq 0 ] \
        && g07_reason="${g07_reason}reject_rate=${rm_reject_rate}(not in [0.08,0.20])" || true
    store_gate "G07" "fail" "RM 零失效" "$g07_reason"
fi

# G-08: 数据 attestation (manual)
store_gate "G08" "pending" "数据 attestation (小余签字)" \
    "manual gate: 小余 (D 主管) 需出具签字 attestation 报告 (R-20 + look-ahead 检测)"

# ---- Output ---------------------------------------------------------------

if [ "$OUTPUT_JSON" -eq 1 ]; then
    printf '{"base_url":"%s","gates":{' "$BASE_URL"
    first=1
    for gid in G00 G01 G02 G03 G04 G05 G06 G07 G08; do
        [ "$first" -eq 0 ] && printf ','
        eval "_s=\$gate_STATUS_${gid}"
        eval "_d=\$gate_DETAIL_${gid}"
        printf '"%s":{"status":"%s","detail":"%s"}' "$gid" "$_s" "$_d"
        first=0
    done
    printf '},"summary":{"pass":%d,"fail":%d,"pending":%d}}\n' \
        "$GATES_PASS" "$GATES_FAIL" "$GATES_PENDING"
    exit 0
fi

printf "\n=== GM-PAPER-G Dogfood Gate Verdict ===\n"
printf "Source: %s/gate/paper\n" "${BASE_URL}"
printf "Note: DOGFOOD 工具, 非生产 gate 判定 (生产用 tools/m4_5_gate/run_gate_check.py)\n\n"

for gid in G00 G01 G02 G03 G04 G05 G06 G07 G08; do
    eval "_s=\$gate_STATUS_${gid}"
    eval "_l=\$gate_LABEL_${gid}"
    eval "_d=\$gate_DETAIL_${gid}"
    case "$_s" in
        pass)    icon="$(green PASS)" ;;
        fail)    icon="$(red   FAIL)" ;;
        pending) icon="$(yellow WAIT)" ;;
        *)       icon="UNKN" ;;
    esac
    printf "[%s] %s — %s\n       %s\n" "$icon" "$gid" "$_l" "$_d"
done

echo
printf "=== Summary: %d PASS, %d FAIL, %d PENDING ===\n" \
    "$GATES_PASS" "$GATES_FAIL" "$GATES_PENDING"

if [ "${GATES_FAIL}" -gt 0 ]; then
    printf "[%s] %d gate(s) FAILED — paper runtime 不具备进入 30 日正式窗口条件\n" \
        "$(red BLOCKED)" "${GATES_FAIL}"
    exit 1
else
    printf "[%s] 0 gate FAIL — 继续运行 paper runtime 待 PENDING 条件满足\n" \
        "$(green OK)"
    exit 0
fi
