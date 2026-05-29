#!/usr/bin/env bash
# measure.sh — SSH into 3 instances, verify chains running, pull CSV on completion
# owner: 老吴 (SRE, A-unit, #10)
# date: 2026-05-29
# usage:
#   bash measure.sh --status          # check chain health on all 3 instances
#   bash measure.sh --pull            # scp pull all 12 CSVs (run after 24h)
#   bash measure.sh --analyze         # run P50/P95/P99 stats on pulled CSVs
#   bash measure.sh --status --pull --analyze  # full sequence

set -euo pipefail

INSTANCE_IDS_FILE="/tmp/laowu-rtt-test-instance-ids.txt.ips"
SSH_KEY="${AWS_SSH_KEY:-~/.ssh/laowu-sre-key.pem}"
SSH_USER="ubuntu"
OUTPUT_DIR="/tmp/laowu-rtt-results"
CSV_FILES=(pm_clob_rtt gs_inplay_tcp_rtt pm_wss_first_event gs_pregame_rtt)

MODE_STATUS=0
MODE_PULL=0
MODE_ANALYZE=0

for arg in "$@"; do
  case "${arg}" in
    --status)  MODE_STATUS=1  ;;
    --pull)    MODE_PULL=1    ;;
    --analyze) MODE_ANALYZE=1 ;;
  esac
done

[[ $((MODE_STATUS + MODE_PULL + MODE_ANALYZE)) -eq 0 ]] && { echo "Usage: $0 [--status] [--pull] [--analyze]"; exit 1; }

log() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*"; }

# ---------------------------------------------------------------------------
# --status: check chain PIDs + sample count on each instance
# ---------------------------------------------------------------------------
check_status() {
  local region="$1"
  local instance_id="$2"
  local public_ip="$3"

  log "[${region}] Checking instance ${instance_id} (${public_ip})..."
  ssh -i "${SSH_KEY}" \
    -o StrictHostKeyChecking=no \
    -o ConnectTimeout=10 \
    "${SSH_USER}@${public_ip}" \
    'bash -s' << 'REMOTE'
echo "=== PID status ==="
if [[ -f /data/pids ]]; then
  while read pid; do
    if kill -0 "${pid}" 2>/dev/null; then
      echo "  PID ${pid}: running"
    else
      echo "  PID ${pid}: DEAD (chain failure!)"
    fi
  done < /data/pids
else
  echo "  /data/pids not found — user-data may not have completed yet"
fi

echo ""
echo "=== Sample counts ==="
for f in pm_clob_rtt gs_inplay_tcp_rtt pm_wss_first_event gs_pregame_rtt; do
  csv="/data/${f}.csv"
  if [[ -f "${csv}" ]]; then
    count=$(wc -l < "${csv}")
    echo "  ${f}: ${count} rows"
  else
    echo "  ${f}: NOT FOUND"
  fi
done

echo ""
echo "=== Last 3 lines of each CSV ==="
for f in pm_clob_rtt gs_inplay_tcp_rtt pm_wss_first_event gs_pregame_rtt; do
  csv="/data/${f}.csv"
  echo "--- ${f} ---"
  [[ -f "${csv}" ]] && tail -3 "${csv}" || echo "  (missing)"
done
REMOTE
}

# ---------------------------------------------------------------------------
# --pull: scp all CSVs from 3 instances to local OUTPUT_DIR/<region>/
# ---------------------------------------------------------------------------
pull_csvs() {
  local region="$1"
  local instance_id="$2"
  local public_ip="$3"

  local dest="${OUTPUT_DIR}/${region}"
  mkdir -p "${dest}"
  log "[${region}] Pulling CSVs from ${public_ip} -> ${dest}/"

  for f in "${CSV_FILES[@]}"; do
    scp -i "${SSH_KEY}" \
      -o StrictHostKeyChecking=no \
      -o ConnectTimeout=15 \
      "${SSH_USER}@${public_ip}:/data/${f}.csv" \
      "${dest}/${f}.csv" 2>/dev/null \
      && log "[${region}] Pulled: ${f}.csv" \
      || log "[${region}] WARN: ${f}.csv not found or scp failed"
  done
}

# ---------------------------------------------------------------------------
# --analyze: P50/P95/P99 stats over pulled CSVs using awk
# Output format compatible with docs/RESEARCH/laowu-w9-rtt-3region-results-v1.md
# ---------------------------------------------------------------------------
analyze_csv() {
  local csv="$1"
  local col="$2"     # column index (1-based) for RTT value

  if [[ ! -f "${csv}" ]]; then
    echo "N/A (file missing)"
    return
  fi

  awk -F',' -v col="${col}" '
  NF >= col && $col ~ /^[0-9]+(\.[0-9]+)?$/ {
    vals[NR] = $col * 1000   # convert to ms if needed (curl returns seconds)
    n++
  }
  END {
    if (n == 0) { print "no valid data"; exit }
    # sort vals array
    for (i = 1; i <= n; i++) arr[i] = vals[i]
    # bubble sort (sufficient for <=1500 samples)
    for (i = 1; i <= n; i++)
      for (j = i+1; j <= n; j++)
        if (arr[j] < arr[i]) { t = arr[i]; arr[i] = arr[j]; arr[j] = t }
    p50 = arr[int(n * 0.50) + 1]
    p95 = arr[int(n * 0.95) + 1]
    p99 = arr[int(n * 0.99) + 1]
    # mean
    sum = 0
    for (i = 1; i <= n; i++) sum += arr[i]
    mean = sum / n
    printf "n=%d P50=%.1f P95=%.1f P99=%.1f mean=%.1f (ms)\n", n, p50, p95, p99, mean
  }' "${csv}"
}

run_analysis() {
  log "=== RTT Analysis (all regions) ==="
  echo ""

  # Chain 1: pm_clob_rtt.csv — col 2 = time_connect (seconds from curl)
  # Chain 2: gs_inplay_tcp_rtt.csv — col 2 = rtt_ms (already ms)
  # Chain 3: pm_wss_first_event.csv — col 2 = elapsed_ms
  # Chain 4: gs_pregame_rtt.csv — col 2 = time_connect (seconds from curl)

  for region in eu-central-1 eu-west-1 eu-west-2; do
    echo "--- ${region} ---"
    local d="${OUTPUT_DIR}/${region}"

    echo -n "  Chain 1 PM CLOB time_connect:  "
    if [[ -f "${d}/pm_clob_rtt.csv" ]]; then
      # time_connect is in seconds (curl -w output), multiply by 1000 in awk
      awk -F',' 'NF>=2 && $2 ~ /^[0-9]/ { printf "%.3f\n", $2 * 1000 }' \
        "${d}/pm_clob_rtt.csv" > /tmp/chain1_ms.txt
      analyze_csv /tmp/chain1_ms.txt 1
    else
      echo "missing"
    fi

    echo -n "  Chain 2 GS inplay TCP SYN:     "
    if [[ -f "${d}/gs_inplay_tcp_rtt.csv" ]]; then
      awk -F',' 'NF>=2 && $2 ~ /^[0-9]/ { print $2 }' \
        "${d}/gs_inplay_tcp_rtt.csv" > /tmp/chain2_ms.txt
      analyze_csv /tmp/chain2_ms.txt 1
    else
      echo "missing"
    fi

    echo -n "  Chain 3 PM WSS 1st event:      "
    if [[ -f "${d}/pm_wss_first_event.csv" ]]; then
      awk -F',' 'NF>=2 && $2 ~ /^[0-9]/ { print $2 }' \
        "${d}/pm_wss_first_event.csv" > /tmp/chain3_ms.txt
      analyze_csv /tmp/chain3_ms.txt 1
    else
      echo "missing"
    fi

    echo -n "  Chain 4 GS pregame time_connect: "
    if [[ -f "${d}/gs_pregame_rtt.csv" ]]; then
      awk -F',' 'NF>=2 && $2 ~ /^[0-9]/ { printf "%.3f\n", $2 * 1000 }' \
        "${d}/gs_pregame_rtt.csv" > /tmp/chain4_ms.txt
      analyze_csv /tmp/chain4_ms.txt 1
    else
      echo "missing"
    fi
    echo ""
  done

  echo "--- ADR-013 v2 Decision Triggers ---"
  echo "Condition A (Frankfurt confirmed): Frankfurt chain2 P95 <=25ms AND <=Dublin chain2 P95"
  echo "Condition B (Refute Frankfurt):    Frankfurt chain1 P95 >25ms OR (Frankfurt chain2 P95 >30ms AND Dublin chain2 <=25ms)"
  echo "Condition C (Major re-eval):       Any region chain1 P95 >50ms OR any chain2 P95 >50ms"
  echo ""
  echo "Results saved to: ${OUTPUT_DIR}/"
  echo "Next: fill laowu-w9-rtt-3region-results-v1.md table from above numbers, @ old Guo confirm."
}

# ---------------------------------------------------------------------------
# Main loop — read instance IDs file
# ---------------------------------------------------------------------------
if [[ ! -f "${INSTANCE_IDS_FILE}" ]]; then
  log "ERROR: ${INSTANCE_IDS_FILE} not found — run spin.sh first"
  exit 1
fi

mkdir -p "${OUTPUT_DIR}"

while read -r region instance_id public_ip; do
  [[ -z "${region}" ]] && continue
  [[ "${MODE_STATUS}" -eq 1 ]] && check_status "${region}" "${instance_id}" "${public_ip}"
  [[ "${MODE_PULL}" -eq 1 ]]   && pull_csvs  "${region}" "${instance_id}" "${public_ip}"
done < "${INSTANCE_IDS_FILE}"

[[ "${MODE_ANALYZE}" -eq 1 ]] && run_analysis

log "=== measure.sh DONE ==="
