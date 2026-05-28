#!/usr/bin/env bash
# 老李 + 小段 — 复用率 / 缓存矩阵 v1 实证脚本
# 用法: bash laoli-xiaoduan-reuse-probe.sh
#
# 输出: 同目录下 reuse-probe-YYYYMMDD.txt + 中间 JSON 落 /tmp/reuse_probe_*
#
# 安全:
#  - 凭证从 .env 读 (set -a; source .env)
#  - 任何 URL / log 都不打 GOALSERVE_API_KEY (用 <REDACTED> 替换)
#  - 不写 .env 内容到任何持久化文件

set -u
PROJECT_ROOT="/Users/wangweibo/code/sports-trader-cpp"
ENV_FILE="${PROJECT_ROOT}/.env"
OUT_DIR="${PROJECT_ROOT}/docs/RESEARCH/data"
STAMP=$(date +%Y%m%d-%H%M%S)
RAW_DIR="/tmp/reuse_probe_${STAMP}"
mkdir -p "$RAW_DIR"
REPORT="${OUT_DIR}/laoli-xiaoduan-reuse-probe-${STAMP}.txt"

if [ ! -f "$ENV_FILE" ]; then
  echo "[err] $ENV_FILE 不存在"; exit 1
fi

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

# redact 输出 — Goalserve key 全屏蔽
redact() { sed -e "s|/${GOALSERVE_API_KEY}/|/<REDACTED>/|g" -e "s|${GOALSERVE_API_KEY}|<REDACTED>|g"; }

UA="curl/8.4.0 sports-trader-cpp-reuse-probe"
PROXY="${GOALSERVE_PROXY:-http://127.0.0.1:7890}"

GAMMA="https://gamma-api.polymarket.com"
CLOB="https://clob.polymarket.com"
DATA="https://data-api.polymarket.com"
GS="https://www.goalserve.com/getfeed/${GOALSERVE_API_KEY}"

echo "# Reuse probe ${STAMP}" > "$REPORT"
echo "# project: sports-trader-cpp" >> "$REPORT"
echo "# proxy: ${PROXY}" >> "$REPORT"
echo "" >> "$REPORT"

log() { echo "$@" | redact | tee -a "$REPORT"; }

# ============================================================
# A. Polymarket gamma /events?closed=false&limit=N — 嵌套 markets 复用率
# ============================================================
log "===== A. Polymarket gamma /events nested markets 复用率 ====="
for limit in 5 10 20 50; do
  f="${RAW_DIR}/gamma-events-l${limit}.json"
  size=$(curl -sS -A "$UA" -o "$f" -w '%{size_download}' \
    "${GAMMA}/events?tag_id=1&closed=false&limit=${limit}&order=startDate&ascending=true")
  if [ ! -s "$f" ]; then log "  limit=${limit} EMPTY"; continue; fi
  events=$(jq 'length' "$f" 2>/dev/null || echo 0)
  total_markets=$(jq '[.[] | (.markets // []) | length] | add // 0' "$f" 2>/dev/null)
  median_markets=$(jq '[.[] | (.markets // []) | length] | sort | .[length/2|floor]' "$f" 2>/dev/null)
  max_markets=$(jq '[.[] | (.markets // []) | length] | max // 0' "$f" 2>/dev/null)
  total_tokens=$(jq '[.[] | (.markets // [])[] | (.clobTokenIds // "[]" | fromjson | length)] | add // 0' "$f" 2>/dev/null)
  log "  limit=${limit}  events=${events}  total_markets=${total_markets}  median_markets/event=${median_markets}  max_markets/event=${max_markets}  total_tokens=${total_tokens}  bytes=${size}"
done

# ============================================================
# B. 单个 event 详情 vs nested  — 复用率验证
# ============================================================
log ""
log "===== B. /events/{id} 单调用 vs nested (复用证伪) ====="
EVENT_IDS_FILE="${RAW_DIR}/gamma-events-l10.json"
# 抽出前 5 个 event id 做 per-event 调用
ids=$(jq -r '.[0:5][] | .id' "$EVENT_IDS_FILE" 2>/dev/null)
i=0
for eid in $ids; do
  f="${RAW_DIR}/event-${eid}.json"
  t_ms=$(curl -sS -A "$UA" -o "$f" -w '%{time_total}' "${GAMMA}/events/${eid}")
  size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
  mk=$(jq '(.markets // []) | length' "$f" 2>/dev/null)
  log "  event ${eid}  markets=${mk}  bytes=${size}  total_s=${t_ms}"
  i=$((i+1))
done

# ============================================================
# C. CLOB /markets 全量分页 — 每页能多少 market
# ============================================================
log ""
log "===== C. CLOB /markets 分页规模 (next_cursor) ====="
f="${RAW_DIR}/clob-markets-p1.json"
curl -sS -A "$UA" -o "$f" "${CLOB}/markets?next_cursor=MA=="
n=$(jq '.data | length' "$f" 2>/dev/null)
nxt=$(jq -r '.next_cursor' "$f" 2>/dev/null)
limit=$(jq '.limit // null' "$f" 2>/dev/null)
total=$(jq '.count // null' "$f" 2>/dev/null)
size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
log "  page1: data=${n}  limit=${limit}  count_total=${total}  next_cursor=${nxt}  bytes=${size}"

# ============================================================
# D. CLOB /sampling-simplified-markets vs /sampling-markets — 全活跃复用度
# ============================================================
log ""
log "===== D. CLOB /sampling-simplified-markets 全量 ====="
f="${RAW_DIR}/clob-sampling.json"
t_ms=$(curl -sS -A "$UA" -o "$f" -w '%{time_total}' "${CLOB}/sampling-simplified-markets?next_cursor=MA==")
n=$(jq '.data | length' "$f" 2>/dev/null)
size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
log "  data=${n}  bytes=${size}  total_s=${t_ms}"

# ============================================================
# E. CLOB /books 批量 — 复用率天花板
# ============================================================
log ""
log "===== E. CLOB /books 批量上限测试 ====="
# 从上面拿出 token_ids
TOKEN_FILE="${RAW_DIR}/tokens.txt"
jq -r '.[].markets // [] | .[] | (.clobTokenIds // "[]" | fromjson | .[])' \
  "${RAW_DIR}/gamma-events-l50.json" 2>/dev/null | head -800 > "$TOKEN_FILE"
n_tokens_avail=$(wc -l < "$TOKEN_FILE" | tr -d ' ')
log "  可用 token_id 池: ${n_tokens_avail}"

for batch in 50 100 200 400; do
  if [ "$n_tokens_avail" -lt "$batch" ]; then log "  batch=${batch}: token 不足 (only ${n_tokens_avail}), skip"; continue; fi
  body=$(head -${batch} "$TOKEN_FILE" | awk '{printf "{\"token_id\":\"%s\"},", $1}' | sed 's/,$//')
  body="[${body}]"
  f="${RAW_DIR}/books-b${batch}.json"
  t_ms=$(curl -sS -A "$UA" -X POST -H "Content-Type: application/json" \
    -o "$f" -w '%{time_total}' --max-time 30 \
    -d "$body" "${CLOB}/books" 2>/dev/null)
  size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
  http_code=$(curl -sS -A "$UA" -X POST -H "Content-Type: application/json" \
    -o /dev/null -w '%{http_code}' --max-time 30 \
    -d "$body" "${CLOB}/books" 2>/dev/null)
  ret_n=$(jq 'length' "$f" 2>/dev/null || echo "?")
  log "  batch=${batch}  http=${http_code}  returned=${ret_n}  total_s=${t_ms}  bytes=${size}"
done

# ============================================================
# F. /prices-history 单 token 实测 (有无 bulk)
# ============================================================
log ""
log "===== F. /prices-history 单 vs (尝试) bulk ====="
TOK=$(head -1 "$TOKEN_FILE")
f="${RAW_DIR}/prices-history.json"
t_ms=$(curl -sS -A "$UA" -o "$f" -w '%{time_total}' \
  "${CLOB}/prices-history?market=${TOK}&interval=1d&fidelity=60")
size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
n=$(jq '.history | length' "$f" 2>/dev/null)
log "  single token  points=${n}  bytes=${size}  total_s=${t_ms}"

# 尝试 ?markets=A,B,C 多 token (Polymarket 文档没列, 验证是否支持)
TOK2=$(sed -n '2p' "$TOKEN_FILE")
TOK3=$(sed -n '3p' "$TOKEN_FILE")
f="${RAW_DIR}/prices-history-multi.json"
http_code=$(curl -sS -A "$UA" -o "$f" -w '%{http_code}' \
  "${CLOB}/prices-history?market=${TOK},${TOK2},${TOK3}&interval=1d&fidelity=60")
size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
log "  multi-token ?market=A,B,C http=${http_code}  bytes=${size}"

# ============================================================
# G. Goalserve — 联赛级 endpoint 复用率 (NBA scores 一次 → 多场 game)
# ============================================================
log ""
log "===== G. Goalserve 联赛级 endpoint 复用率 ====="

probe_gs() {
  local name="$1" path="$2" parser="$3"
  local f="${RAW_DIR}/gs-${name}.${parser}"
  local t_ms code size
  t_ms=$(curl -sS -A "$UA" --proxy "$PROXY" --compressed --max-time 20 \
    -o "$f" -w '%{time_total}' \
    "${GS}/${path}" 2>/dev/null || echo "TIMEOUT")
  code=$(curl -sS -A "$UA" --proxy "$PROXY" --compressed --max-time 20 \
    -o /dev/null -w '%{http_code}' \
    "${GS}/${path}" 2>/dev/null || echo "000")
  size=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null || echo 0)
  echo "$f $t_ms $code $size"
}

# NBA daily scores
r=$(probe_gs "nba-scores" "bsktbl/nba-scores" "json"); read -r f t c s <<< "$r"
games=$(jq '[(.scores.category // [])] | flatten | length' "$f" 2>/dev/null || echo 0)
# 兜底 — 不同结构
[ "$games" = "0" ] && games=$(jq '[(.scores // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  bsktbl/nba-scores   http=${c} games=${games} bytes=${s} total_s=${t}"

# NBA schedule (whole season)
r=$(probe_gs "nba-shedule" "bsktbl/nba-shedule" "json"); read -r f t c s <<< "$r"
sched_games=$(jq '[(.shedules // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  bsktbl/nba-shedule  http=${c} games=${sched_games} bytes=${s} total_s=${t}"

# NBA inplay
r=$(probe_gs "nba-inplay" "bsktbl/inplay" "json"); read -r f t c s <<< "$r"
inplay_games=$(jq '[(.scores // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  bsktbl/inplay       http=${c} games=${inplay_games} bytes=${s} total_s=${t}"

# NFL
r=$(probe_gs "nfl" "football/nfl-scores" "json"); read -r f t c s <<< "$r"
nfl_games=$(jq '[(.scores // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  football/nfl-scores http=${c} games=${nfl_games} bytes=${s} total_s=${t}"

# MLB
r=$(probe_gs "mlb" "baseball/usa" "json"); read -r f t c s <<< "$r"
mlb_games=$(jq '[(.scores // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  baseball/usa        http=${c} games=${mlb_games} bytes=${s} total_s=${t}"

# NHL
r=$(probe_gs "nhl" "hockey/nhl-scores" "json"); read -r f t c s <<< "$r"
nhl_games=$(jq '[(.scores // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  hockey/nhl-scores   http=${c} games=${nhl_games} bytes=${s} total_s=${t}"

# Soccer inplay (XML — 用 grep 估算 match 数)
r=$(probe_gs "soccer-inplay" "soccer/inplay" "xml"); read -r f t c s <<< "$r"
soccer_matches=$(grep -oc "<match " "$f" 2>/dev/null || echo 0)
soccer_cats=$(grep -oc "<category " "$f" 2>/dev/null || echo 0)
log "  soccer/inplay       http=${c} matches=${soccer_matches} categories=${soccer_cats} bytes=${s} total_s=${t}"

# Soccer home (whole-day all leagues)
r=$(probe_gs "soccer-home" "soccer/home" "xml"); read -r f t c s <<< "$r"
soccer_h_matches=$(grep -oc "<match " "$f" 2>/dev/null || echo 0)
soccer_h_cats=$(grep -oc "<category " "$f" 2>/dev/null || echo 0)
log "  soccer/home         http=${c} matches=${soccer_h_matches} categories=${soccer_h_cats} bytes=${s} total_s=${t}"

# Tennis home
r=$(probe_gs "tennis-home" "tennis/home" "json"); read -r f t c s <<< "$r"
tennis_matches=$(jq '[(.scores // {}) | .. | objects | select(.match) | .match] | flatten | length' "$f" 2>/dev/null || echo 0)
log "  tennis/home         http=${c} matches=${tennis_matches} bytes=${s} total_s=${t}"

# ============================================================
# H. Goalserve incremental — 测试 ?lastupdate= If-Modified-Since
# ============================================================
log ""
log "===== H. Goalserve incremental 参数测试 ====="
NOW=$(date +%s)
# 1. ?lastupdate=<now-3600>
f="${RAW_DIR}/gs-incr1.json"
sz=$(curl -sS -A "$UA" --proxy "$PROXY" --compressed --max-time 20 \
  -o "$f" -w '%{size_download}' \
  "${GS}/bsktbl/nba-shedule?lastupdate=$((NOW-3600))" 2>/dev/null)
sz2=$(curl -sS -A "$UA" --proxy "$PROXY" --compressed --max-time 20 \
  -o "${RAW_DIR}/gs-incr-baseline.json" -w '%{size_download}' \
  "${GS}/bsktbl/nba-shedule" 2>/dev/null)
log "  ?lastupdate=<now-1h>  size=${sz} vs baseline=${sz2}  (相同=参数被忽略, 不同=有效)"

# 2. If-Modified-Since header
hdr=$(curl -sS -A "$UA" --proxy "$PROXY" --compressed --max-time 20 \
  -H "If-Modified-Since: $(date -u -r $((NOW-60)) "+%a, %d %b %Y %H:%M:%S GMT" 2>/dev/null || date -u -d "@$((NOW-60))" "+%a, %d %b %Y %H:%M:%S GMT")" \
  -o /dev/null -w '%{http_code}' \
  "${GS}/bsktbl/nba-shedule" 2>/dev/null)
log "  If-Modified-Since (-60s)  http=${hdr}  (304=支持 / 200=不支持)"

# 3. ETag
etag=$(curl -sS -A "$UA" --proxy "$PROXY" --compressed --max-time 20 \
  -D - -o /dev/null \
  "${GS}/bsktbl/nba-shedule" 2>/dev/null | grep -i "^etag\|^last-modified\|^cache-control" || echo "  (无 ETag/Last-Modified/Cache-Control)")
log "  ETag/Last-Modified/Cache-Control 响应头:"
echo "$etag" | sed 's/^/    /' >> "$REPORT"

# ============================================================
# I. Polymarket WSS 多 token 共享连接验证
# ============================================================
log ""
log "===== I. Polymarket WSS 100-token 单连接订阅验证 ====="
# 取 100 个 token_id
head -100 "$TOKEN_FILE" > "${RAW_DIR}/wss-tokens.txt"
n_t=$(wc -l < "${RAW_DIR}/wss-tokens.txt" | tr -d ' ')
log "  尝试订阅 ${n_t} tokens 在单个 WSS 连接"

PYWSS="/Users/wangweibo/code/sports-trader-cpp/.venv/bin/python"
cat > "${RAW_DIR}/wss_multi.py" <<'PY'
import asyncio, json, sys, time
import websockets

async def main():
    tokens = [l.strip() for l in open(sys.argv[1]) if l.strip()]
    url = "wss://ws-subscriptions-clob.polymarket.com/ws/market"
    t0 = time.time()
    async with websockets.connect(url, open_timeout=10) as ws:
        t_open = time.time() - t0
        await ws.send(json.dumps({"type":"Market","assets_ids":tokens}))
        # 收 15 秒
        msgs = 0; bytes_in = 0; first = None
        unique_assets = set()
        try:
            while time.time() - t0 < 18:
                m = await asyncio.wait_for(ws.recv(), timeout=3)
                if first is None: first = time.time() - t0
                msgs += 1; bytes_in += len(m)
                try:
                    d = json.loads(m)
                    if isinstance(d, list):
                        for item in d:
                            if isinstance(item, dict) and 'asset_id' in item:
                                unique_assets.add(item['asset_id'])
                    elif isinstance(d, dict) and 'asset_id' in d:
                        unique_assets.add(d['asset_id'])
                except: pass
        except asyncio.TimeoutError:
            pass
        print(json.dumps({
            "open_s": round(t_open, 3),
            "first_msg_s": round(first or -1, 3),
            "duration_s": round(time.time()-t0, 3),
            "msgs": msgs,
            "bytes": bytes_in,
            "unique_asset_ids_seen": len(unique_assets),
            "tokens_subscribed": len(tokens)
        }))

asyncio.run(main())
PY

if [ -x "$PYWSS" ]; then
  wss_out=$("$PYWSS" "${RAW_DIR}/wss_multi.py" "${RAW_DIR}/wss-tokens.txt" 2>&1 | tail -1)
  log "  WSS result: ${wss_out}"
else
  log "  (skip — .venv python 不可用)"
fi

# ============================================================
# J. 同 series 一组比赛字段对比 — 验证用户假设 "series 共享数据"
# ============================================================
log ""
log "===== J. NBA 同 series (季后赛系列赛) 多比赛字段对比 ====="
# 从 nba-shedule 抽出最近的 in-progress series — 5/28 是 NBA season 末
# 取最近 30 天内的比赛, group by (hometeam, awayteam) pair, 找重复 ≥3 次的 series
NBA_SCHED="${RAW_DIR}/gs-nba-shedule.json"
if [ -f "$NBA_SCHED" ] && [ -s "$NBA_SCHED" ]; then
  pairs=$(jq -r '
    [(.shedules // {}) | .. | objects | select(.match) | .match] | flatten |
    map(select(.status == "Final" or .status == "Not Started" or .status == "In Play")) |
    map({pair: ([.hometeam.name, .awayteam.name] | sort | join(" vs ")), date: .datetime_utc, id: .id, status: .status}) |
    group_by(.pair) | map(select(length >= 2)) |
    map({pair: .[0].pair, games: length, dates: [.[].date], ids: [.[].id], statuses: [.[].status]})
    ' "$NBA_SCHED" 2>/dev/null | jq '.[0:6]')
  echo "$pairs" >> "$REPORT"
  pair_count=$(echo "$pairs" | jq 'length')
  log "  发现 ${pair_count} 个 ≥2 场对决 (可能是 series / 季后赛)"
fi

# ============================================================
# 汇总
# ============================================================
log ""
log "===== 汇总 ====="
log "  原始数据 raw: ${RAW_DIR}"
log "  报告: ${REPORT}"
echo "DONE: $REPORT"
