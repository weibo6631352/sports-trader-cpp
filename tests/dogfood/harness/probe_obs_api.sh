#!/usr/bin/env bash
# tests/dogfood/harness/probe_obs_api.sh
#
# Owner: 小宫 (dogfood-evaluator)
# Date: 2026-05-29
#
# 逐 endpoint 探针 — 对 stub 或真实 paper runtime 均可跑
#
# 对齐: docs/RESEARCH/xiaozheng-observability-endpoints-v1.md (6 节)
#        tests/dogfood/checklist-paper-runtime.md 阶段 4
#
# 用法:
#   BASE_URL=http://localhost:19091 ./tests/dogfood/harness/probe_obs_api.sh
#   BASE_URL=http://trader-paper:9090 ./tests/dogfood/harness/probe_obs_api.sh
#
# 退出码: 0 = 全通, 1 = 有失败项

set -euo pipefail

BASE_URL="${BASE_URL:-http://localhost:19091}"
TIMEOUT="${PROBE_TIMEOUT:-5}"  # curl connect+read timeout in seconds

PASS=0
FAIL=0

# ---- helpers ----------------------------------------------------------------

green() { printf '\033[32m%s\033[0m' "$1"; }
red()   { printf '\033[31m%s\033[0m' "$1"; }

check() {
    local label="$1"
    local result="$2"  # "ok" or failure message
    if [[ "$result" == "ok" ]]; then
        printf "[%s] %s\n" "$(green PASS)" "$label"
        ((PASS++)) || true
    else
        printf "[%s] %s — %s\n" "$(red FAIL)" "$label" "$result"
        ((FAIL++)) || true
    fi
}

curl_get() {
    # Returns HTTP body on stdout; exits non-zero on curl error
    curl -sf --max-time "${TIMEOUT}" "$1"
}

http_code() {
    curl -s -o /dev/null -w "%{http_code}" --max-time "${TIMEOUT}" "$1"
}

json_has_key() {
    local body="$1" key="$2"
    echo "$body" | grep -q "\"${key}\""
}

json_bool_true() {
    local body="$1" key="$2"
    echo "$body" | grep -qE "\"${key}\"[[:space:]]*:[[:space:]]*true"
}

json_number_positive() {
    # Checks that "key": <number> where number > 0
    local body="$1" key="$2"
    local val
    val=$(echo "$body" | grep -oE "\"${key}\"[[:space:]]*:[[:space:]]*[0-9]+" | grep -oE '[0-9]+$' || true)
    [[ -n "$val" && "$val" -gt 0 ]]
}

# ---- Probe 1: /healthz (小郑 obs v1 §1) --------------------------------

probe_healthz() {
    local body code
    code=$(http_code "${BASE_URL}/healthz")
    if [[ "$code" != "200" ]]; then
        check "P1-1 GET /healthz HTTP 200" "HTTP ${code} (expected 200)"
        return
    fi
    check "P1-1 GET /healthz HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/healthz" 2>/dev/null || echo "{}")
    if json_bool_true "$body" "ok"; then
        check "P1-2 /healthz ok==true" "ok"
    else
        check "P1-2 /healthz ok==true" "field ok not true in: ${body:0:200}"
    fi

    if json_number_positive "$body" "as_of_ts"; then
        check "P1-3 /healthz as_of_ts > 0" "ok"
    else
        check "P1-3 /healthz as_of_ts > 0" "as_of_ts missing or zero: ${body:0:200}"
    fi
}

# ---- Probe 2: /version --------------------------------------------------

probe_version() {
    local body code
    code=$(http_code "${BASE_URL}/version")
    if [[ "$code" != "200" ]]; then
        check "P2-1 GET /version HTTP 200" "HTTP ${code}"
        return
    fi
    check "P2-1 GET /version HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/version" 2>/dev/null || echo "{}")
    if echo "$body" | grep -q '"build_mode"[[:space:]]*:[[:space:]]*"paper"'; then
        check "P2-2 /version build_mode=paper" "ok"
    else
        check "P2-2 /version build_mode=paper" "build_mode != paper: ${body:0:200}"
    fi
}

# ---- Probe 3: /status ---------------------------------------------------

probe_status() {
    local body code
    code=$(http_code "${BASE_URL}/status")
    if [[ "$code" != "200" ]]; then
        check "P3-1 GET /status HTTP 200" "HTTP ${code}"
        return
    fi
    check "P3-1 GET /status HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/status" 2>/dev/null || echo "{}")

    if echo "$body" | grep -qE '"state"[[:space:]]*:[[:space:]]*"(RUNNING|DRAIN|HALTED)"'; then
        check "P3-2 /status state in {RUNNING,DRAIN,HALTED}" "ok"
    else
        check "P3-2 /status state in {RUNNING,DRAIN,HALTED}" "unexpected state: ${body:0:200}"
    fi

    if echo "$body" | grep -q '"mode"[[:space:]]*:[[:space:]]*"paper"'; then
        check "P3-3 /status mode=paper" "ok"
    else
        check "P3-3 /status mode=paper" "mode != paper: ${body:0:200}"
    fi
}

# ---- Probe 4: /metrics (Prometheus) ------------------------------------

probe_metrics() {
    local body code
    code=$(http_code "${BASE_URL}/metrics")
    if [[ "$code" != "200" ]]; then
        check "P4-1 GET /metrics HTTP 200" "HTTP ${code}"
        return
    fi
    check "P4-1 GET /metrics HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/metrics" 2>/dev/null || echo "")

    local required_metrics=(
        "stcpp_uptime_seconds"
        "stcpp_wss_connected"
        "stcpp_data_staleness_seconds"
        "stcpp_rm_decision_total"
        "stcpp_pnl_usd"
        "stcpp_bankroll_usd"
        "stcpp_event_loop_latency_seconds"
        "stcpp_audit_wal_ring_fill_ratio"
        "stcpp_ts_lag_seconds"
        "stcpp_gate_sharpe_30d"
    )

    for m in "${required_metrics[@]}"; do
        if echo "$body" | grep -q "^${m}"; then
            check "P4-2 /metrics has ${m}" "ok"
        else
            check "P4-2 /metrics has ${m}" "metric not found"
        fi
    done

    # R-12 gate: event loop p99 < 100us check via metric presence
    if echo "$body" | grep -q 'stcpp_event_loop_latency_seconds_bucket'; then
        check "P4-3 /metrics event_loop latency histogram present" "ok"
    else
        check "P4-3 /metrics event_loop latency histogram present" "missing histogram"
    fi

    # R-20 gate: ts_lag histogram present
    if echo "$body" | grep -q 'stcpp_ts_lag_seconds_bucket'; then
        check "P4-4 /metrics R-20 ts_lag histogram present" "ok"
    else
        check "P4-4 /metrics R-20 ts_lag histogram present" "missing histogram"
    fi
}

# ---- Probe 5: /risk/rejects (小郑 obs v1 §4) ---------------------------

probe_risk_rejects() {
    local body code
    code=$(http_code "${BASE_URL}/risk/rejects?n=10")
    if [[ "$code" != "200" ]]; then
        check "P5-1 GET /risk/rejects HTTP 200" "HTTP ${code}"
        return
    fi
    check "P5-1 GET /risk/rejects HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/risk/rejects?n=10" 2>/dev/null || echo "{}")

    if json_has_key "$body" "rejects"; then
        check "P5-2 /risk/rejects has rejects array" "ok"
    else
        check "P5-2 /risk/rejects has rejects array" "key missing: ${body:0:200}"
    fi

    if json_has_key "$body" "as_of_ts"; then
        check "P5-3 /risk/rejects has as_of_ts (R-20)" "ok"
    else
        check "P5-3 /risk/rejects has as_of_ts (R-20)" "missing as_of_ts: ${body:0:200}"
    fi

    # Check that rejects have required fields
    if echo "$body" | grep -qE '"reason_code"[[:space:]]*:'; then
        check "P5-4 /risk/rejects entries have reason_code" "ok"
    else
        check "P5-4 /risk/rejects entries have reason_code" "reason_code missing"
    fi
}

# ---- Probe 6: /trace/recent (小郑 obs v1 §2) ---------------------------

probe_trace_recent() {
    local body code
    code=$(http_code "${BASE_URL}/trace/recent?n=10")
    if [[ "$code" != "200" ]]; then
        check "P6-1 GET /trace/recent HTTP 200" "HTTP ${code}"
        return
    fi
    check "P6-1 GET /trace/recent HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/trace/recent?n=10" 2>/dev/null || echo "{}")

    if json_has_key "$body" "traces"; then
        check "P6-2 /trace/recent has traces array" "ok"
    else
        check "P6-2 /trace/recent has traces array" "missing: ${body:0:200}"
    fi
}

# ---- Probe 7: /trace/{intent_id} (小郑 obs v1 §2) ----------------------

probe_trace_detail() {
    local intent_id="01HZ00000000000A"
    local body code
    code=$(http_code "${BASE_URL}/trace/${intent_id}")
    if [[ "$code" != "200" ]]; then
        check "P7-1 GET /trace/{id} HTTP 200" "HTTP ${code}"
        return
    fi
    check "P7-1 GET /trace/{id} HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/trace/${intent_id}" 2>/dev/null || echo "{}")

    # Check full span chain: signal.emit, rm.evaluate, signer.sign, exec.match
    local required_spans=("signal.emit" "rm.evaluate" "signer.sign" "exec.match")
    for span in "${required_spans[@]}"; do
        if echo "$body" | grep -q "\"${span}\""; then
            check "P7-2 /trace/{id} has span ${span}" "ok"
        else
            check "P7-2 /trace/{id} has span ${span}" "span missing"
        fi
    done
}

# ---- Probe 8: /gate/paper (小郑 obs v1 §3, GM-PAPER-G 实时仪表) --------

probe_gate_paper() {
    local body code
    code=$(http_code "${BASE_URL}/gate/paper")
    if [[ "$code" != "200" ]]; then
        check "P8-1 GET /gate/paper HTTP 200" "HTTP ${code}"
        return
    fi
    check "P8-1 GET /gate/paper HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/gate/paper" 2>/dev/null || echo "{}")

    local required_fields=(
        "window_days"
        "n_trades"
        "positive_day_ratio"
        "sharpe_30d"
        "sharpe_se"
        "p_value"
        "hit_rate"
        "mdd"
        "net_pnl_usd"
        "prelim_pass"
        "confirm_pass"
        "as_of_ts"
    )
    for f in "${required_fields[@]}"; do
        if json_has_key "$body" "$f"; then
            check "P8-2 /gate/paper has field ${f}" "ok"
        else
            check "P8-2 /gate/paper has field ${f}" "field missing"
        fi
    done

    # G-07 specific: rm_bypass_count must be present (风控零失效)
    if json_has_key "$body" "rm_bypass_count"; then
        check "P8-3 /gate/paper has rm_bypass_count (G-07)" "ok"
    else
        check "P8-3 /gate/paper has rm_bypass_count (G-07)" "missing"
    fi

    # R-11: r11_cross_write_count must be present
    if json_has_key "$body" "r11_cross_write_count"; then
        check "P8-4 /gate/paper has r11_cross_write_count (R-11)" "ok"
    else
        check "P8-4 /gate/paper has r11_cross_write_count (R-11)" "missing"
    fi
}

# ---- Probe 9: /data/latency/{market_id} (小郑 obs v1 §6, R-20) ---------

probe_data_latency() {
    local market_id="mkt_test_dogfood"
    local body code
    code=$(http_code "${BASE_URL}/data/latency/${market_id}")
    if [[ "$code" != "200" ]]; then
        check "P9-1 GET /data/latency/{id} HTTP 200" "HTTP ${code}"
        return
    fi
    check "P9-1 GET /data/latency/{id} HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/data/latency/${market_id}" 2>/dev/null || echo "{}")

    local required_fields=("event_to_source_ms" "source_to_ingest_ms" "ingest_to_asof_ms"
                           "total_lag_ms" "ts_chain_valid")
    for f in "${required_fields[@]}"; do
        if json_has_key "$body" "$f"; then
            check "P9-2 /data/latency has field ${f} (R-20)" "ok"
        else
            check "P9-2 /data/latency has field ${f} (R-20)" "field missing"
        fi
    done

    # R-20: ts_chain_valid must be true (no inversion)
    if json_bool_true "$body" "ts_chain_valid"; then
        check "P9-3 /data/latency ts_chain_valid=true (R-20 no inversion)" "ok"
    else
        check "P9-3 /data/latency ts_chain_valid=true (R-20 no inversion)" \
              "ts_chain_valid != true: ${body:0:200}"
    fi
}

# ---- Probe 10: /logs (小郑 obs v1 §5) ----------------------------------

probe_logs() {
    local body code
    code=$(http_code "${BASE_URL}/logs?level=warn&limit=5")
    if [[ "$code" != "200" ]]; then
        check "P10-1 GET /logs HTTP 200" "HTTP ${code}"
        return
    fi
    check "P10-1 GET /logs HTTP 200" "ok"

    body=$(curl_get "${BASE_URL}/logs?level=warn&limit=5" 2>/dev/null || echo "{}")

    # Security: no private key in log output
    if echo "$body" | grep -qiE 'private_key|wallet_private|0x[0-9a-fA-F]{60,}'; then
        check "P10-2 /logs no private key leak" "SECURITY: key pattern found in logs"
    else
        check "P10-2 /logs no private key leak" "ok"
    fi

    if json_has_key "$body" "logs"; then
        check "P10-3 /logs has logs array" "ok"
    else
        check "P10-3 /logs has logs array" "missing: ${body:0:200}"
    fi
}

# ---- run all probes -------------------------------------------------------

printf "\n=== Paper Runtime Observability API Probe ===\n"
printf "BASE_URL: %s\n\n" "${BASE_URL}"

probe_healthz
echo
probe_version
echo
probe_status
echo
probe_metrics
echo
probe_risk_rejects
echo
probe_trace_recent
echo
probe_trace_detail
echo
probe_gate_paper
echo
probe_data_latency
echo
probe_logs

# ---- summary --------------------------------------------------------------

echo
printf "=== Summary: %d PASS, %d FAIL ===\n" "${PASS}" "${FAIL}"

if [[ "${FAIL}" -gt 0 ]]; then
    printf "[%s] %d probe(s) failed. Review above for details.\n" "$(red FAIL)" "${FAIL}"
    exit 1
else
    printf "[%s] All %d probes passed.\n" "$(green PASS)" "${PASS}"
    exit 0
fi
