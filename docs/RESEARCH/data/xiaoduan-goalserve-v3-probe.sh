#!/usr/bin/env bash
# 小段 — Goalserve 官方文档实证 v3 (基于 docs/GOALSERVER/ 12 份官方文档)
# 用法: bash xiaoduan-goalserve-v3-probe.sh
#
# 验证 5 个 game-changing 修正:
#  1. inplay.goalserve.com 8 个 sport endpoint (gzip JSON, 含 odds value)
#  2. /getodds/soccer?cat=<cat>_10 16 个 cat 真格式
#  3. ts 增量协议 (ts 字段 + ts= 参数)
#  4. dictionaries 端点 (odds-markets + states)
#  5. settlement endpoint (oddsfeed.goalserve.com)
#  + Match Result by ID 终极结果 + inplay-mapping 5 sport
#
# 安全约定:
#  - 凭证 redact, 走代理, 全 sample 落 /tmp/xiaoduan_v3_<STAMP>/

set -u
PROJECT_ROOT="/Users/wangweibo/code/sports-trader-cpp"
ENV_FILE="${PROJECT_ROOT}/.env"
OUT_DIR="${PROJECT_ROOT}/docs/RESEARCH/data"
STAMP=$(date +%Y%m%d-%H%M%S)
RAW_DIR="/tmp/xiaoduan_v3_${STAMP}"
mkdir -p "$RAW_DIR"
REPORT="${OUT_DIR}/xiaoduan-goalserve-v3-probe-${STAMP}.txt"

[ -f "$ENV_FILE" ] || { echo "[err] .env missing"; exit 1; }
set -a; source "$ENV_FILE"; set +a

redact() { sed -e "s|/${GOALSERVE_API_KEY}/|/<REDACTED>/|g" -e "s|${GOALSERVE_API_KEY}|<REDACTED>|g" -e "s|k=${GOALSERVE_API_KEY}|k=<REDACTED>|g" -e "s|apiKey=${GOALSERVE_API_KEY}|apiKey=<REDACTED>|g"; }
log() { echo "$@" | redact | tee -a "$REPORT"; }

UA="curl/8.4.0 sports-trader-cpp-xiaoduan-v3"
PROXY="${GOALSERVE_PROXY:-http://127.0.0.1:7890}"

echo "# xiaoduan v3 goalserve official-doc probe ${STAMP}" > "$REPORT"
echo "# proxy: ${PROXY}" >> "$REPORT"

# probe_raw: 完整 URL 直 GET, 记录 http/bytes/ttfb/total + sniff content
# args: $1=label $2=full-URL $3=stem $4=accept-encoding(opt, default gzip)
probe_raw() {
  local label="$1" url="$2" stem="$3" enc="${4:-gzip,deflate}"
  local f="${RAW_DIR}/${stem}"
  local hf="${RAW_DIR}/${stem}.hdr"
  read -r http bytes ttime ttfb < <(curl -sS --max-time 20 \
      --compressed \
      -x "$PROXY" -A "$UA" -H "Accept-Encoding: ${enc}" -D "$hf" -o "$f" \
      -w '%{http_code} %{size_download} %{time_total} %{time_starttransfer}\n' \
      "$url" 2>/dev/null || echo "000 0 20.0 20.0")
  local ct
  ct=$(grep -i '^content-type' "$hf" 2>/dev/null | head -1 | tr -d '\r')
  log "$(printf '  [%s] http=%s bytes=%s ttfb=%s total=%s %s' "$label" "$http" "$bytes" "$ttfb" "$ttime" "$ct")"
}

# ============================================================
# §1. inplay.goalserve.com 8 sport gzip JSON (官方文档关键修正)
# ============================================================
log ""
log "===== §1. inplay.goalserve.com 8 sport (官方说每秒推送 gzip JSON, 含真 odds) ====="
for sp in soccer basket tennis volleyball amfootball esports hockey baseball; do
  # 试 1: 无 key 直连 (官方文档没写要 key)
  probe_raw "inplay-${sp}-nokey"  "http://inplay.goalserve.com/inplay-${sp}.gz"     "inplay-${sp}-nokey.gz"
  # 试 2: 带 key 在 query (兜底)
  probe_raw "inplay-${sp}-qkey"   "http://inplay.goalserve.com/inplay-${sp}.gz?k=${GOALSERVE_API_KEY}" "inplay-${sp}-qkey.gz"
  sleep 0.2
done

# ============================================================
# §2. /getodds/soccer?cat=<cat>_10 16 个 cat
# ============================================================
log ""
log "===== §2. pregame odds 真格式 /getodds/soccer?cat=<cat>_10 ====="
GS_FEED="http://www.goalserve.com/getfeed/${GOALSERVE_API_KEY}"
for cat in soccer basket tennis hockey handball volleyball football baseball cricket rugby rugbyleague boxing esports futsal mma darts table_tennis; do
  probe_raw "getodds-${cat}" "${GS_FEED}/getodds/soccer?cat=${cat}_10&json=1" "getodds-${cat}.json"
  sleep 0.2
done

# ============================================================
# §3. ts 增量协议: 先全量取 ts, 再带 ts= 取增量
# ============================================================
log ""
log "===== §3. ts 增量协议实测 ====="
TS_URL="${GS_FEED}/getodds/soccer?cat=soccer_10&json=1"
probe_raw "ts-full" "$TS_URL" "ts-full.json"
# 从全量 sample 抠 ts 字段 (官方说响应里有 ts)
TS_VAL=$(grep -oE '"ts"[: ]*"?[0-9]+"?' "${RAW_DIR}/ts-full.json" 2>/dev/null | head -1 | grep -oE '[0-9]+' || true)
log "  抠到的 ts 值: ${TS_VAL:-<none>}"
if [ -n "${TS_VAL:-}" ]; then
  sleep 2
  probe_raw "ts-incr" "${TS_URL}&ts=${TS_VAL}" "ts-incr.json"
fi

# ============================================================
# §4. dictionaries (官方文档关键 PoC)
# ============================================================
log ""
log "===== §4. dictionaries (odds-markets + states) ====="
for sp in soccer basketball tennis baseball hockey amfootball volleyball esports; do
  probe_raw "dict-mk-${sp}"   "http://inplay.goalserve.com/dictionaries/odds-markets/${sp}" "dict-mk-${sp}.json"
  probe_raw "dict-st-${sp}"   "http://inplay.goalserve.com/dictionaries/states/${sp}"        "dict-st-${sp}.json"
  sleep 0.2
done

# ============================================================
# §5. settlement endpoint (oddsfeed.goalserve.com)
# ============================================================
log ""
log "===== §5. settlement endpoint (oddsfeed.goalserve.com) ====="
# 试单结算 (sportId=4 soccer; 真 gsId/marketId 未知, 试看错误格式)
probe_raw "settle-single" \
  "http://oddsfeed.goalserve.com/api/v1/odds/pre-game/settlement?sportId=4&gsId=85471622&marketId=16&oddname=Under:8&k=${GOALSERVE_API_KEY}&json=1" \
  "settle-single.json"
# 时间戳批量 (最近 30 分钟)
DT=$(date +%s)
probe_raw "settle-batch-time" \
  "http://oddsfeed.goalserve.com/api/v1/odds/pre-game/settlements?sportId=4&dateTime=${DT}&k=${GOALSERVE_API_KEY}&json=1" \
  "settle-batch.json"
# 比赛 ID 批量 (随便给 ID)
probe_raw "settle-batch-mid" \
  "http://oddsfeed.goalserve.com/api/v1/odds/pre-game/settlements/matches?sportId=4&matchesIds=4734063,4734063&k=${GOALSERVE_API_KEY}&json=1" \
  "settle-batch-mid.json"

# ============================================================
# §6. Match Result by ID 终极结果 (官方文档)
# ============================================================
log ""
log "===== §6. Match Result by ID (inplay.goalserve.com/results/yyyyMM/MID.json) ====="
# 官方示例
probe_raw "result-example" "http://inplay.goalserve.com/results/202104/59077136.json" "result-example.json"
# 试近期月份
probe_raw "result-202604"  "http://inplay.goalserve.com/results/202604/59077136.json" "result-202604.json"

# ============================================================
# §7. inplay-mapping 5 sport
# ============================================================
log ""
log "===== §7. inplay-pregame mapping endpoint 5 sport ====="
for path in "soccernew/inplay-mapping" "esports/inplay-mapping" "tennis_scores/inplay-mapping" "basketball/inplay-mapping" "baseball/inplay-mapping"; do
  stem=$(echo "$path" | tr '/' '-')
  probe_raw "mapping-${stem}" "${GS_FEED}/${path}?json=1" "mapping-${stem}.json"
  sleep 0.2
done

# ============================================================
# §8. livescore.goalserve.com 实时比分 (第二个域名)
# ============================================================
log ""
log "===== §8. livescore.goalserve.com (第二域名) ====="
for sp in soccer; do
  probe_raw "livescore-${sp}-home" "http://livescore.goalserve.com/api/v1/${sp}/home?apiKey=${GOALSERVE_API_KEY}" "livescore-${sp}-home.json"
  probe_raw "livescore-${sp}-live" "http://livescore.goalserve.com/api/v1/${sp}/live?apiKey=${GOALSERVE_API_KEY}" "livescore-${sp}-live.json"
done

# ============================================================
# 落地 raw_dir + magic byte sniff (确认 gzip / json / xml)
# ============================================================
log ""
log "===== §9. raw file inventory + magic byte sniff ====="
for f in "$RAW_DIR"/*; do
  [ -f "$f" ] || continue
  case "$f" in
    *.hdr) continue ;;
  esac
  sz=$(wc -c < "$f")
  # 跳 0 字节
  [ "$sz" -eq 0 ] && { log "  $(basename "$f"): EMPTY"; continue; }
  head_hex=$(xxd -l 4 "$f" 2>/dev/null | head -1 | awk '{print $2$3}')
  case "$head_hex" in
    1f8b*) kind="gzip" ;;
    7b*|5b*) kind="json/text" ;;
    3c*) kind="xml/html" ;;
    *) kind="other($head_hex)" ;;
  esac
  log "  $(printf '%-44s %8d %s' "$(basename "$f")" "$sz" "$kind")"
done

log ""
log "===== DONE ${STAMP} =====  RAW_DIR=${RAW_DIR}"
