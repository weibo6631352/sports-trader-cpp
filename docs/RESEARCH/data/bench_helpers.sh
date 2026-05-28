#!/usr/bin/env bash
# 老陈 — 网络基准采样工具函数 (不入 git 静态结果, 但脚本入 git 可复跑)
# 用法: source bench_helpers.sh ; bench_rest <name> <url> <n> <interval> [curl-extra]

set -u

bench_rest() {
  local name="$1"  url="$2"  n="${3:-50}"  iv="${4:-1.0}"
  shift 4 || true
  local out="/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/laochen-network-bench-${name}.csv"
  echo "idx,ts_unix,http_code,dns_ms,connect_ms,appconnect_ms,starttransfer_ms,total_ms,size_down,speed_down_bps" > "$out"
  for i in $(seq 1 "$n"); do
    local line
    line=$(curl -sS -o /dev/null \
      -w "%{http_code},%{time_namelookup},%{time_connect},%{time_appconnect},%{time_starttransfer},%{time_total},%{size_download},%{speed_download}" \
      --max-time 15 "$@" "$url" 2>/dev/null || echo "000,0,0,0,0,0,0,0")
    # 把秒转毫秒
    local code dns conn app start total size speed
    { IFS=',' read -r code dns conn app start total size speed <<< "$line"; } 2>/dev/null
    printf "%d,%s,%s,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%s\n" \
      "$i" "$(date +%s)" "$code" \
      "$(echo "$dns*1000" | bc -l)" \
      "$(echo "$conn*1000" | bc -l)" \
      "$(echo "$app*1000" | bc -l)" \
      "$(echo "$start*1000" | bc -l)" \
      "$(echo "$total*1000" | bc -l)" \
      "$size" "$speed" >> "$out"
    sleep "$iv"
  done
  echo "$out"
}

# 百分位 (输入 stdin 浮点列)
percentile() {
  python3 -c "
import sys, statistics
xs = sorted(float(x) for x in sys.stdin if x.strip())
if not xs:
    print('NA'); sys.exit()
def pct(p):
    k = (len(xs)-1)*p/100.0
    f = int(k); c = min(f+1, len(xs)-1)
    return xs[f] + (xs[c]-xs[f])*(k-f)
print(f'n={len(xs)} min={xs[0]:.1f} p50={pct(50):.1f} p95={pct(95):.1f} p99={pct(99):.1f} max={xs[-1]:.1f} mean={statistics.mean(xs):.1f}')
"
}

summarize_csv() {
  local f="$1" col="${2:-total_ms}"
  local idx
  idx=$(head -1 "$f" | tr ',' '\n' | grep -n "^${col}$" | cut -d: -f1)
  [ -z "$idx" ] && { echo "col $col missing"; return 1; }
  tail -n +2 "$f" | awk -F',' -v c="$idx" '{print $c}' | percentile
}
