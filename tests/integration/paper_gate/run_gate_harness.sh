#!/usr/bin/env bash
# GM-PAPER-G 门禁验收 Harness
#
# Owner: 小颖 (requirements-analyst, E 产品业务保障部)
# Date:  2026-05-29
# SSOT:  docs/RESEARCH/xiaoying-paper-gate-harness-v1.md §2
#
# 用法:
#   ./run_gate_harness.sh --mode file --report <paper_report_30d.json>
#   ./run_gate_harness.sh --mode api  --endpoint http://localhost:8080/api/v1/gate/paper
#   ./run_gate_harness.sh --mode file --report <file> --daily-pnl <daily.json> [--soft-validation <soft.json>]
#
# 输出: 每条 gate PASS/FAIL + 数值, 最终 G-ALL 判定
# 退出码: 0=全部 PASS, 1=任一 FAIL, 2=输入错误

set -euo pipefail

# ── 颜色 ──────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

pass_count=0
fail_count=0
skip_count=0

# ── 工具 ──────────────────────────────────────────────────────────────────────
require_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: required command '$1' not found. Install: $2" >&2
        exit 2
    fi
}

require_cmd jq "brew install jq"
require_cmd bc "brew install bc"

# ── 参数解析 ──────────────────────────────────────────────────────────────────
MODE="file"
REPORT_FILE=""
DAILY_PNL_FILE=""
SOFT_VALIDATION_FILE=""
API_ENDPOINT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mode)           MODE="$2"; shift 2 ;;
        --report)         REPORT_FILE="$2"; shift 2 ;;
        --daily-pnl)      DAILY_PNL_FILE="$2"; shift 2 ;;
        --soft-validation) SOFT_VALIDATION_FILE="$2"; shift 2 ;;
        --endpoint)       API_ENDPOINT="$2"; shift 2 ;;
        --help|-h)
            grep "^#" "$0" | head -15 | sed 's/^# \?//'
            exit 0 ;;
        *) echo "Unknown arg: $1"; exit 2 ;;
    esac
done

# ── 数据加载 ──────────────────────────────────────────────────────────────────
load_report() {
    if [[ "$MODE" == "api" ]]; then
        require_cmd curl "brew install curl"
        REPORT_JSON=$(curl -sf "$API_ENDPOINT" 2>/dev/null) || {
            echo "ERROR: cannot reach $API_ENDPOINT" >&2
            exit 2
        }
        # API 模式: 字段路径前缀不同, 用 jq 适配
        API_MODE=1
    else
        [[ -n "$REPORT_FILE" ]] || { echo "ERROR: --report required in file mode" >&2; exit 2; }
        [[ -f "$REPORT_FILE" ]] || { echo "ERROR: file not found: $REPORT_FILE" >&2; exit 2; }
        REPORT_JSON=$(cat "$REPORT_FILE")
        API_MODE=0
    fi
}

jq_file() {
    # $1=jq_expr, reads REPORT_JSON
    echo "$REPORT_JSON" | jq -r "$1" 2>/dev/null || echo "null"
}

jq_api() {
    # API 模式字段路径适配
    local expr="$1"
    # 简单路径替换: pregame_moneyline.X -> gates.GXX.X (已在 harness-v1 §5.2 映射)
    echo "$REPORT_JSON" | jq -r "$expr" 2>/dev/null || echo "null"
}

# ── 断言辅助 ──────────────────────────────────────────────────────────────────
assert_pass() {
    local gate="$1" label="$2" value="$3" threshold="$4" op="$5" veto="$6"
    local result
    case "$op" in
        gt)  result=$(echo "$value > $threshold" | bc -l) ;;
        gte) result=$(echo "$value >= $threshold" | bc -l) ;;
        lt)  result=$(echo "$value < $threshold" | bc -l) ;;
        lte) result=$(echo "$value <= $threshold" | bc -l) ;;
        eq)  result=$([ "$value" = "$threshold" ] && echo 1 || echo 0) ;;
        ne)  result=$([ "$value" != "$threshold" ] && echo 1 || echo 0) ;;
        range) # op=range threshold="low:high"
            local lo hi
            lo=$(echo "$threshold" | cut -d: -f1)
            hi=$(echo "$threshold" | cut -d: -f2)
            result=$(echo "$value >= $lo && $value <= $hi" | bc -l) ;;
        *) echo "Unknown op: $op"; return 1 ;;
    esac

    if [[ "$result" == "1" ]]; then
        echo -e "  ${GREEN}PASS${NC} $gate [$label] value=$value threshold($op)=$threshold"
        ((pass_count++)) || true
        return 0
    else
        echo -e "  ${RED}FAIL${NC} $gate [$label] value=$value threshold($op)=$threshold  <<< VETO: $veto"
        ((fail_count++)) || true
        return 1
    fi
}

assert_eq_int() {
    local gate="$1" label="$2" value="$3" expected="$4" veto="$5"
    if [[ "$value" == "$expected" ]]; then
        echo -e "  ${GREEN}PASS${NC} $gate [$label] value=$value == $expected"
        ((pass_count++)) || true
    else
        echo -e "  ${RED}FAIL${NC} $gate [$label] value=$value != $expected  <<< VETO: $veto"
        ((fail_count++)) || true
    fi
}

assert_bool_true() {
    local gate="$1" label="$2" value="$3" veto="$4"
    if [[ "$value" == "true" ]]; then
        echo -e "  ${GREEN}PASS${NC} $gate [$label] = true"
        ((pass_count++)) || true
    else
        echo -e "  ${RED}FAIL${NC} $gate [$label] = $value (expected true)  <<< VETO: $veto"
        ((fail_count++)) || true
    fi
}

# ── 主逻辑 ────────────────────────────────────────────────────────────────────
main() {
    load_report

    echo ""
    echo "================================================================"
    echo "  GM-PAPER-G 门禁验收 Harness"
    echo "  SSOT: docs/RESEARCH/xiaoying-paper-gate-harness-v1.md"
    echo "  Mode: $MODE"
    echo "  Date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "================================================================"
    echo ""

    # ── R-20 4ts 前置校验 ─────────────────────────────────────────────────────
    echo "[R-20] 4 时间戳单调性校验 (前置)"
    WS=$(jq_file '.window_start_ts_ns')
    WE=$(jq_file '.window_end_ts_ns')
    AOF=$(jq_file '.as_of_ts_ns')
    if [[ "$WS" != "null" && "$WE" != "null" && "$AOF" != "null" ]]; then
        PIT_OK=$(echo "$WS <= $WE && $WE <= $AOF" | bc -l)
        if [[ "$PIT_OK" == "1" ]]; then
            echo -e "  ${GREEN}PASS${NC} R-20 PIT: window_start_ts <= window_end_ts <= as_of_ts"
        else
            echo -e "  ${RED}FAIL${NC} R-20 PIT VIOLATION: ts chain broken (ws=$WS, we=$WE, aof=$AOF)"
            echo "       *** R-20 违例: 8 条 gate 全部强制 FAIL ***"
            ((fail_count+=8)) || true
            summarize
            exit 1
        fi
    else
        echo -e "  ${YELLOW}SKIP${NC} R-20 PIT: timestamp fields missing from report"
        ((skip_count++)) || true
    fi
    echo ""

    # ── G-00 14 日软验证 ──────────────────────────────────────────────────────
    echo "[G-00] 14 日软验证前置 Gate"
    if [[ -n "$SOFT_VALIDATION_FILE" && -f "$SOFT_VALIDATION_FILE" ]]; then
        SV_PNL=$(jq -r '.net_pnl_usdc' "$SOFT_VALIDATION_FILE" 2>/dev/null || echo "null")
        SV_MAX_LOSS=$(jq -r '.max_daily_loss_pct' "$SOFT_VALIDATION_FILE" 2>/dev/null || echo "null")
        assert_pass "G-00" "14d_net_pnl>0" "$SV_PNL" "0" "gt" "小梁"
        assert_pass "G-00" "14d_max_loss<3%" "$SV_MAX_LOSS" "0.03" "lt" "老韩"
    else
        echo -e "  ${YELLOW}SKIP${NC} G-00: soft_validation_summary.json not provided — manual confirmation required"
        echo "       Tip: 提供 --soft-validation <path> 可自动验证"
        ((skip_count++)) || true
    fi
    echo ""

    # ── G-01 30 日连续窗口 ────────────────────────────────────────────────────
    echo "[G-01] 30 日连续窗口"
    WIN_DAYS=$(jq_file '.window_days')
    UPTIME_PCT=$(jq_file '.uptime.uptime_pct')
    assert_eq_int "G-01" "window_days==30" "$WIN_DAYS" "30" "—"
    assert_pass "G-01" "uptime_pct>=99.0%" "$UPTIME_PCT" "0.990" "gte" "老吴"
    echo ""

    # ── G-08 数据 attestation (前置依赖 G-02) ────────────────────────────────
    echo "[G-08] 数据 Attestation (G-02 前置依赖)"
    ATT_SIGNED=$(jq_file '.data_attestation.attestation_signed')
    R20_VIOLS=$(jq_file '.data_attestation.r20_violations')
    LA_VIOLS=$(jq_file '.data_attestation.lookahead_violations')
    JOIN_ORPHANS=$(jq_file '.data_attestation.feature_join_orphans')
    SILENT_DROPS=$(jq_file '.data_attestation.data_silent_drops')
    assert_bool_true "G-08" "attestation_signed" "$ATT_SIGNED" "小余(否决权)"
    assert_eq_int "G-08" "r20_violations==0" "$R20_VIOLS" "0" "小余"
    assert_eq_int "G-08" "lookahead_violations==0" "$LA_VIOLS" "0" "小余"
    assert_eq_int "G-08" "feature_join_orphans==0" "$JOIN_ORPHANS" "0" "小余"
    assert_eq_int "G-08" "data_silent_drops==0" "$SILENT_DROPS" "0" "小余"
    G08_OK=$([[ "$ATT_SIGNED" == "true" && "$R20_VIOLS" == "0" && "$LA_VIOLS" == "0" \
               && "$JOIN_ORPHANS" == "0" && "$SILENT_DROPS" == "0" ]] && echo 1 || echo 0)
    echo ""

    # ── G-02 净 PnL > 0 ──────────────────────────────────────────────────────
    echo "[G-02] 30 日净 PnL > 0 (依赖 G-08 attestation)"
    NET_PNL=$(jq_file '.pregame_moneyline.net_pnl_usdc')
    if [[ "$G08_OK" == "1" ]]; then
        assert_pass "G-02" "net_pnl_usdc>0" "$NET_PNL" "0" "gt" "小梁+老唐"
    else
        echo -e "  ${RED}FAIL${NC} G-02: 强制 FAIL — G-08 attestation 未通过 (小余否决权未满足)"
        ((fail_count++)) || true
    fi
    echo ""

    # ── G-03 样本量 + bootstrap CI ───────────────────────────────────────────
    echo "[G-03] 样本量 + Bootstrap Sharpe CI"
    N_TRADES=$(jq_file '.pregame_moneyline.n_trades')
    CI_LOWER=$(jq_file '.pregame_moneyline.sharpe_bootstrap_ci_lower')
    PVALUE=$(jq_file '.pregame_moneyline.ttest_pvalue_onesided')
    BS_N=$(jq_file '.pregame_moneyline.sharpe_bootstrap_n')
    assert_pass "G-03" "n_trades>=100" "$N_TRADES" "100" "gte" "小梁"
    assert_pass "G-03" "bootstrap_ci_lower>0" "$CI_LOWER" "0" "gt" "小梁"
    assert_pass "G-03" "ttest_pvalue<0.10" "$PVALUE" "0.10" "lt" "小梁"
    assert_eq_int "G-03" "bootstrap_n==5000" "$BS_N" "5000" "小董"
    echo ""

    # ── G-04 日级稳定性 ───────────────────────────────────────────────────────
    echo "[G-04] 日级稳定性 (正收益日 + 单日亏损)"
    WIN_DAYS_N=$(jq_file '.pregame_moneyline.win_days')
    TOT_DAYS=$(jq_file '.pregame_moneyline.total_days')
    MAX_LOSS=$(jq_file '.pregame_moneyline.max_daily_loss_pct')
    if [[ "$WIN_DAYS_N" != "null" && "$TOT_DAYS" != "null" && "$TOT_DAYS" != "0" ]]; then
        WIN_RATE=$(echo "scale=4; $WIN_DAYS_N / $TOT_DAYS" | bc -l)
        assert_pass "G-04" "win_rate>=52%" "$WIN_RATE" "0.52" "gte" "小梁"
    else
        echo -e "  ${RED}FAIL${NC} G-04 [win_rate]: missing win_days or total_days  <<< VETO: 小梁"
        ((fail_count++)) || true
    fi
    assert_pass "G-04" "max_daily_loss<3%" "$MAX_LOSS" "0.03" "lt" "老韩(RM否决权)"
    echo ""

    # ── G-05 OOS Sharpe ───────────────────────────────────────────────────────
    echo "[G-05] OOS Sharpe >= 0.5 (+ G-03 共用 p 值)"
    SHARPE=$(jq_file '.pregame_moneyline.daily_sharpe_annualized')
    # G-05 的 pvalue 与 G-03 同字段
    assert_pass "G-05" "sharpe_annualized>=0.5" "$SHARPE" "0.5" "gte" "小梁(唯一签字)"
    assert_pass "G-05" "ttest_pvalue<0.10(同G03)" "$PVALUE" "0.10" "lt" "小梁"
    echo ""

    # ── G-06 盘口分桶 ─────────────────────────────────────────────────────────
    echo "[G-06] Pregame Moneyline 单独分桶"
    PREGAME_PNL=$(jq_file '.pregame_moneyline.net_pnl_usdc')
    LABELED_PCT=$(jq_file '.pregame_moneyline.market_type_labeled_pct')
    INPLAY_N=$(jq_file '.inplay.n_trades')
    assert_pass "G-06" "pregame_net_pnl>0" "$PREGAME_PNL" "0" "gt" "老钱(CPO否决权)"
    assert_pass "G-06" "market_type_labeled_pct==1.0" "$LABELED_PCT" "1.0" "gte" "老彭"
    echo -e "  INFO  G-06 [inplay_n_trades_display_only]: $INPLAY_N (透明度展示, 不计入 gate)"
    echo ""

    # ── G-07 风控零失效 ───────────────────────────────────────────────────────
    echo "[G-07] 风控零失效 (RM + R-11)"
    RM_BYPASS=$(jq_file '.risk_control.rm_bypass_count')
    CROSSWRITE=$(jq_file '.risk_control.paper_to_live_crosswrite_count')
    RM_REJECT=$(jq_file '.risk_control.rm_reject_count')
    RM_EVAL=$(jq_file '.risk_control.rm_evaluate_count')
    assert_eq_int "G-07" "rm_bypass_count==0" "$RM_BYPASS" "0" "老韩(否决权)"
    assert_eq_int "G-07" "paper_crosswrite_count==0" "$CROSSWRITE" "0" "老韩(R-11)"
    if [[ "$RM_REJECT" != "null" && "$RM_EVAL" != "null" && "$RM_EVAL" != "0" ]]; then
        REJECT_RATE=$(echo "scale=4; $RM_REJECT / $RM_EVAL" | bc -l)
        assert_pass "G-07" "reject_rate_in_[8%,20%]" "$REJECT_RATE" "0.08:0.20" "range" "老韩"
    else
        echo -e "  ${RED}FAIL${NC} G-07 [reject_rate]: missing rm_reject_count or rm_evaluate_count  <<< VETO: 老韩"
        ((fail_count++)) || true
    fi
    echo ""

    # ── G-ALL 汇总 ────────────────────────────────────────────────────────────
    summarize
}

summarize() {
    echo "================================================================"
    echo "  G-ALL 判定结果"
    echo "  PASS: $pass_count  FAIL: $fail_count  SKIP: $skip_count"
    echo "================================================================"

    if [[ $fail_count -eq 0 && $skip_count -eq 0 ]]; then
        echo -e "  ${GREEN}G-ALL: PASS${NC} — 八条门禁全部通过, 请进行四方签字"
        echo ""
        echo "  下一步: 执行四方签字 checklist"
        echo "    docs/RESEARCH/xiaoying-paper-gate-harness-v1.md §4"
        exit 0
    elif [[ $fail_count -eq 0 && $skip_count -gt 0 ]]; then
        echo -e "  ${YELLOW}G-ALL: PENDING${NC} — $skip_count 条 SKIP (需补充数据或人工确认)"
        echo "    SKIP 条目不视为 PASS, 需逐一处理后重跑"
        exit 1
    else
        echo -e "  ${RED}G-ALL: FAIL${NC} — $fail_count 条断言失败"
        echo ""
        echo "  失败后处理:"
        echo "    任一 gate FAIL → 30 日窗口重置 (从失败日起重新计 14 日软验证)"
        echo "    SSOT: xiaoying-acceptance-spec-v2.md §2 G-ALL"
        exit 1
    fi
}

main "$@"
