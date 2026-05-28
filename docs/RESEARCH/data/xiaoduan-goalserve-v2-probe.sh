#!/usr/bin/env bash
# 小段 — Goalserve 全 sport × 全 endpoint type 复用率矩阵 v2 实证脚本
# 用法: bash xiaoduan-goalserve-v2-probe.sh
#
# 安全约定:
#  - 凭证从 .env 读, 全程 sed redact GOALSERVE_API_KEY
#  - 走代理 ${GOALSERVE_PROXY}, 不直连
#  - 每 sport × endpoint 至多 5 次, 不打配额
#  - 单 endpoint --max-time 18s (避免代理挂死后僵)
#
# 输出: 同目录 xiaoduan-goalserve-v2-probe-<STAMP>.txt + 全 sample 落 /tmp/xiaoduan_v2_<STAMP>/

set -u
PROJECT_ROOT="/Users/wangweibo/code/sports-trader-cpp"
ENV_FILE="${PROJECT_ROOT}/.env"
OUT_DIR="${PROJECT_ROOT}/docs/RESEARCH/data"
STAMP=$(date +%Y%m%d-%H%M%S)
RAW_DIR="/tmp/xiaoduan_v2_${STAMP}"
mkdir -p "$RAW_DIR"
REPORT="${OUT_DIR}/xiaoduan-goalserve-v2-probe-${STAMP}.txt"

[ -f "$ENV_FILE" ] || { echo "[err] $ENV_FILE 不存在"; exit 1; }

set -a
# shellcheck disable=SC1090
source "$ENV_FILE"
set +a

redact() { sed -e "s|/${GOALSERVE_API_KEY}/|/<REDACTED>/|g" -e "s|${GOALSERVE_API_KEY}|<REDACTED>|g"; }
log() { echo "$@" | redact | tee -a "$REPORT"; }

UA="curl/8.4.0 sports-trader-cpp-xiaoduan-v2"
PROXY="${GOALSERVE_PROXY:-http://127.0.0.1:7890}"
GS="https://www.goalserve.com/getfeed/${GOALSERVE_API_KEY}"

echo "# xiaoduan v2 goalserve full matrix probe ${STAMP}" > "$REPORT"
echo "# proxy: ${PROXY}" >> "$REPORT"
echo "" >> "$REPORT"

# probe 函数: 单 URL 单次, 记录 http/bytes/time_total
# args: $1=label  $2=relative-url-path (不含 base + key)  $3=output-file-stem
probe_once() {
  local label="$1" path="$2" stem="$3"
  local url="${GS}/${path}"
  local f="${RAW_DIR}/${stem}"
  local hf="${RAW_DIR}/${stem}.hdr"
  local rc bytes http ttime ttfb
  # 关键: --max-time 18s, 防止代理 hang
  read -r http bytes ttime ttfb < <(curl -sS --max-time 18 --compressed \
      -x "$PROXY" -A "$UA" -D "$hf" -o "$f" \
      -w '%{http_code} %{size_download} %{time_total} %{time_starttransfer}\n' \
      "$url" 2>/dev/null || echo "000 0 18.000 18.000")
  log "$(printf '  [%s] http=%s bytes=%s ttfb=%s total=%s  path=%s' \
        "$label" "$http" "$bytes" "$ttfb" "$ttime" "$path")"
}

# probe sport+endpoint, 默认 3 次跑取均值/方差感
# args: $1=label  $2=path  $3=stem-prefix  $4=iters(default 3)
probe_n() {
  local label="$1" path="$2" stem="$3" n="${4:-3}"
  for i in $(seq 1 "$n"); do
    probe_once "${label}#${i}" "$path" "${stem}-${i}"
    sleep 0.3
  done
}

# =====================================================================
# 0. 探测矩阵设计:
#    对每 sport, 探所有 endpoint type 候选:
#      inplay / home / livescore / schedule (含 shedule typo)
#      <league>-scores / <league>-shedule
#      standings / getodds / players / teams / lineup / h2h
#    路径基于 v1 已知 + Goalserve 公开文档常见命名 + 拼写陷阱穷举
# =====================================================================

# ---------- 1. Basketball (NBA / WNBA / NCAA / EuroLeague) ----------
log ""; log "===== 1. Basketball ====="
probe_n "bsktbl-inplay"          "bsktbl/inplay"           "bsktbl-inplay" 3
probe_n "bsktbl-home"            "bsktbl/home"             "bsktbl-home" 2
probe_n "bsktbl-nba-scores"      "bsktbl/nba-scores"       "bsktbl-nba-scores" 2
probe_n "bsktbl-nba-shedule"     "bsktbl/nba-shedule"      "bsktbl-nba-shedule" 1
probe_n "bsktbl-nba-schedule"    "bsktbl/nba-schedule"     "bsktbl-nba-schedule-typo" 1
probe_n "bsktbl-wnba-scores"     "bsktbl/wnba-scores"      "bsktbl-wnba-scores" 1
probe_n "bsktbl-wnba-shedule"    "bsktbl/wnba-shedule"     "bsktbl-wnba-shedule" 1
probe_n "bsktbl-ncaa-scores"     "bsktbl/ncaa-scores"      "bsktbl-ncaa-scores" 1
probe_n "bsktbl-ncaa-shedule"    "bsktbl/ncaa-shedule"     "bsktbl-ncaa-shedule" 1
probe_n "bsktbl-euroleague"      "bsktbl/euroleague"       "bsktbl-euroleague" 1
probe_n "bsktbl-nba-standing"    "bsktbl/nba-standings"    "bsktbl-nba-standings" 1
probe_n "bsktbl-nba-players"     "bsktbl/nba-players"      "bsktbl-nba-players" 1
probe_n "bsktbl-nba-teams"       "bsktbl/nba-teams"        "bsktbl-nba-teams" 1
probe_n "bsktbl-nba-roster"      "bsktbl/nba-roster"       "bsktbl-nba-roster" 1
probe_n "bsktbl-nba-injuries"    "bsktbl/nba-injuries"     "bsktbl-nba-injuries" 1
probe_n "bsktbl-nba-statistic"   "bsktbl/nba-stats"        "bsktbl-nba-stats" 1
probe_n "bsktbl-nba-leaders"     "bsktbl/nba-leaders"      "bsktbl-nba-leaders" 1
probe_n "bsktbl-nba-h2h"         "bsktbl/nba-h2h"          "bsktbl-nba-h2h" 1
probe_n "bsktbl-livescore"       "bsktbl/livescore"        "bsktbl-livescore" 1
probe_n "bsktbl-results"         "bsktbl/results"          "bsktbl-results" 1
probe_n "bsktbl-d-1"             "bsktbl/d-1"              "bsktbl-d-1" 1
probe_n "bsktbl-nba-odds"        "bsktbl/nba-odds"         "bsktbl-nba-odds" 1
probe_n "bsktbl-nba-getodds"     "bsktbl/nba-getodds"      "bsktbl-nba-getodds" 1

# ---------- 2. American football (NFL / NCAA) ----------
log ""; log "===== 2. Football (NFL/NCAA) ====="
probe_n "football-inplay"        "football/inplay"         "football-inplay" 2
probe_n "football-home"          "football/home"           "football-home" 1
probe_n "football-nfl-scores"    "football/nfl-scores"     "football-nfl-scores" 2
probe_n "football-nfl-shedule"   "football/nfl-shedule"    "football-nfl-shedule" 1
probe_n "football-nfl-schedule"  "football/nfl-schedule"   "football-nfl-schedule-typo" 1
probe_n "football-nfl-standings" "football/nfl-standings"  "football-nfl-standings" 1
probe_n "football-nfl-players"   "football/nfl-players"    "football-nfl-players" 1
probe_n "football-nfl-injuries"  "football/nfl-injuries"   "football-nfl-injuries" 1
probe_n "football-nfl-stats"     "football/nfl-stats"      "football-nfl-stats" 1
probe_n "football-nfl-leaders"   "football/nfl-leaders"    "football-nfl-leaders" 1
probe_n "football-nfl-rankings"  "football/nfl-rankings"   "football-nfl-rankings" 1
probe_n "football-ncaa-scores"   "football/ncaa-scores"    "football-ncaa-scores" 1
probe_n "football-ncaa-shedule"  "football/ncaa-shedule"   "football-ncaa-shedule" 1
probe_n "football-ncaa"          "football/ncaa"           "football-ncaa" 1
probe_n "football-results"       "football/results"        "football-results" 1
probe_n "football-livescore"     "football/livescore"      "football-livescore" 1

# ---------- 3. Baseball (MLB / MiLB) ----------
log ""; log "===== 3. Baseball ====="
probe_n "baseball-inplay"        "baseball/inplay"         "baseball-inplay" 2
probe_n "baseball-home"          "baseball/home"           "baseball-home" 1
probe_n "baseball-usa"           "baseball/usa"            "baseball-usa" 2
probe_n "baseball-mlb-scores"    "baseball/mlb-scores"     "baseball-mlb-scores" 1
probe_n "baseball-mlb-shedule"   "baseball/mlb-shedule"    "baseball-mlb-shedule" 1
probe_n "baseball-mlb-standings" "baseball/mlb-standings"  "baseball-mlb-standings" 1
probe_n "baseball-mlb-players"   "baseball/mlb-players"    "baseball-mlb-players" 1
probe_n "baseball-mlb-injuries"  "baseball/mlb-injuries"   "baseball-mlb-injuries" 1
probe_n "baseball-mlb-stats"     "baseball/mlb-stats"      "baseball-mlb-stats" 1
probe_n "baseball-mlb-leaders"   "baseball/mlb-leaders"    "baseball-mlb-leaders" 1
probe_n "baseball-mlb-pitchers"  "baseball/mlb-pitchers"   "baseball-mlb-pitchers" 1
probe_n "baseball-mlb-batters"   "baseball/mlb-batters"    "baseball-mlb-batters" 1
probe_n "baseball-milb"          "baseball/milb"           "baseball-milb" 1
probe_n "baseball-japan"         "baseball/japan"          "baseball-japan" 1
probe_n "baseball-korea"         "baseball/korea"          "baseball-korea" 1
probe_n "baseball-mlb-odds"      "baseball/mlb-odds"       "baseball-mlb-odds" 1
probe_n "baseball-getodds"       "baseball/getodds"        "baseball-getodds" 1

# ---------- 4. Soccer (跨联赛, 重头) ----------
log ""; log "===== 4. Soccer ====="
probe_n "soccer-inplay-xml"      "soccer/inplay"           "soccer-inplay-xml" 2
probe_n "soccernew-inplay"       "soccernew/inplay"        "soccernew-inplay" 2
probe_n "soccer-home"            "soccer/home"             "soccer-home" 1
probe_n "soccernew-home"         "soccernew/home"          "soccernew-home" 1
probe_n "soccer-livescore"       "soccer/livescore"        "soccer-livescore" 1
probe_n "soccer-standings"       "soccer/standings"        "soccer-standings" 1
probe_n "soccer-shedule"         "soccer/shedule"          "soccer-shedule" 1
probe_n "soccer-schedule"        "soccer/schedule"         "soccer-schedule-typo" 1
probe_n "soccer-d-1"             "soccer/d-1"              "soccer-d-1" 1
probe_n "soccer-getodds"         "soccer/getodds"          "soccer-getodds" 1
probe_n "soccer-comments"        "soccer/comments"         "soccer-comments" 1
# 联赛细分 (Goalserve 用数字 league id)
probe_n "soccer-epl"             "soccer/epl"              "soccer-epl" 1
probe_n "soccer-bundesliga"      "soccer/bundesliga"       "soccer-bundesliga" 1
probe_n "soccer-laliga"          "soccer/laliga"           "soccer-laliga" 1
probe_n "soccer-mls"             "soccer/mls"              "soccer-mls" 1
probe_n "soccer-champions"       "soccer/champions-league" "soccer-champions" 1
probe_n "soccer-europa"          "soccer/europa-league"    "soccer-europa" 1
probe_n "soccer-worldcup"        "soccer/worldcup"         "soccer-worldcup" 1
probe_n "soccernew-1204"         "soccernew/1204"          "soccernew-leagueid-1204" 1
probe_n "soccernew-leagues"      "soccernew/leagues"       "soccernew-leagues" 1

# ---------- 5. Hockey (NHL / KHL) ----------
log ""; log "===== 5. Hockey ====="
probe_n "hockey-inplay"          "hockey/inplay"           "hockey-inplay" 2
probe_n "hockey-home"            "hockey/home"             "hockey-home" 1
probe_n "hockey-nhl-scores"      "hockey/nhl-scores"       "hockey-nhl-scores" 2
probe_n "hockey-nhl-shedule"     "hockey/nhl-shedule"      "hockey-nhl-shedule" 1
probe_n "hockey-nhl-schedule"    "hockey/nhl-schedule"     "hockey-nhl-schedule-typo" 1
probe_n "hockey-nhl-standings"   "hockey/nhl-standings"    "hockey-nhl-standings" 1
probe_n "hockey-nhl-players"     "hockey/nhl-players"      "hockey-nhl-players" 1
probe_n "hockey-nhl-injuries"    "hockey/nhl-injuries"     "hockey-nhl-injuries" 1
probe_n "hockey-nhl-stats"       "hockey/nhl-stats"        "hockey-nhl-stats" 1
probe_n "hockey-nhl-leaders"     "hockey/nhl-leaders"      "hockey-nhl-leaders" 1
probe_n "hockey-khl"             "hockey/khl"              "hockey-khl" 1
probe_n "hockey-livescore"       "hockey/livescore"        "hockey-livescore" 1
probe_n "hockey-getodds"         "hockey/getodds"          "hockey-getodds" 1

# ---------- 6. Tennis (ATP / WTA / ITF) ----------
log ""; log "===== 6. Tennis ====="
probe_n "tennis-home"            "tennis/home"             "tennis-home" 2
probe_n "tennis-inplay"          "tennis/inplay"           "tennis-inplay" 1
probe_n "tennis-livescore"       "tennis/livescore"        "tennis-livescore" 1
probe_n "tennis-atp"             "tennis/atp"              "tennis-atp-ranking" 1
probe_n "tennis-wta"             "tennis/wta"              "tennis-wta-ranking" 1
probe_n "tennis-atp-shedule"     "tennis/atp-shedule"      "tennis-atp-shedule" 1
probe_n "tennis-wta-shedule"     "tennis/wta-shedule"      "tennis-wta-shedule" 1
probe_n "tennis-results"         "tennis/results"          "tennis-results" 1
probe_n "tennis-tournaments"     "tennis/tournaments"      "tennis-tournaments" 1
probe_n "tennis-getodds"         "tennis/getodds"          "tennis-getodds" 1
probe_n "tennis-d-1"             "tennis/d-1"              "tennis-d-1" 1

# ---------- 7. Cricket (IPL / ICC) ----------
log ""; log "===== 7. Cricket ====="
probe_n "cricket-livescore"      "cricket/livescore"       "cricket-livescore" 2
probe_n "cricket-home"           "cricket/home"            "cricket-home" 1
probe_n "cricket-inplay"         "cricket/inplay"          "cricket-inplay" 1
probe_n "cricket-shedule"        "cricket/shedule"         "cricket-shedule" 1
probe_n "cricket-fixtures"       "cricket/fixtures"        "cricket-fixtures" 1
probe_n "cricket-squads"         "cricket/squads"          "cricket-squads" 1
probe_n "cricket-ipl"            "cricket/ipl"             "cricket-ipl" 1
probe_n "cricket-icc"            "cricket/icc"             "cricket-icc" 1
probe_n "cricket-rankings"       "cricket/rankings"        "cricket-rankings" 1
probe_n "cricket-getodds"        "cricket/getodds"         "cricket-getodds" 1

# ---------- 8. Rugby (Union / League) ----------
log ""; log "===== 8. Rugby ====="
probe_n "rugby-inplay"           "rugby/inplay"            "rugby-inplay" 2
probe_n "rugby-home"             "rugby/home"              "rugby-home" 1
probe_n "rugby-livescore"        "rugby/livescore"         "rugby-livescore" 1
probe_n "rugbyleague-inplay"     "rugbyleague/inplay"      "rugbyleague-inplay" 1
probe_n "rugbyleague-home"       "rugbyleague/home"        "rugbyleague-home" 1
probe_n "rugby-shedule"          "rugby/shedule"           "rugby-shedule" 1
probe_n "rugby-getodds"          "rugby/getodds"           "rugby-getodds" 1

# ---------- 9. Handball ----------
log ""; log "===== 9. Handball ====="
probe_n "handball-inplay"        "handball/inplay"         "handball-inplay" 2
probe_n "handball-home"          "handball/home"           "handball-home" 1
probe_n "handball-livescore"     "handball/livescore"      "handball-livescore" 1
probe_n "handball-shedule"       "handball/shedule"        "handball-shedule" 1
probe_n "handball-getodds"       "handball/getodds"        "handball-getodds" 1

# ---------- 10. Volleyball ----------
log ""; log "===== 10. Volleyball ====="
probe_n "volleyball-inplay"      "volleyball/inplay"       "volleyball-inplay" 2
probe_n "volleyball-home"        "volleyball/home"         "volleyball-home" 1
probe_n "volleyball-livescore"   "volleyball/livescore"    "volleyball-livescore" 1
probe_n "volleyball-shedule"     "volleyball/shedule"      "volleyball-shedule" 1
probe_n "volleyball-getodds"     "volleyball/getodds"      "volleyball-getodds" 1

# ---------- 11. Badminton / Table tennis / Snooker / Darts ----------
log ""; log "===== 11. 小众 indoor (badminton/tabletennis/snooker/darts) ====="
probe_n "badminton-inplay"       "badminton/inplay"        "badminton-inplay" 1
probe_n "badminton-home"         "badminton/home"          "badminton-home" 1
probe_n "tabletennis-inplay"     "tabletennis/inplay"      "tabletennis-inplay" 1
probe_n "tabletennis-home"       "tabletennis/home"        "tabletennis-home" 1
probe_n "table-tennis-home"      "table-tennis/home"       "table-tennis-home-dash" 1
probe_n "snooker-inplay"         "snooker/inplay"          "snooker-inplay" 1
probe_n "snooker-home"           "snooker/home"            "snooker-home" 1
probe_n "darts-home"             "darts/home"              "darts-home" 2
probe_n "darts-inplay"           "darts/inplay"            "darts-inplay" 1

# ---------- 12. Combat sports (Boxing / MMA / UFC) ----------
log ""; log "===== 12. Boxing / MMA / UFC ====="
probe_n "boxing-shedule"         "boxing/shedule"          "boxing-shedule" 1
probe_n "boxing-schedule"        "boxing/schedule"         "boxing-schedule-typo" 1
probe_n "boxing-home"            "boxing/home"             "boxing-home" 1
probe_n "boxing-fighters"        "boxing/fighters"         "boxing-fighters" 1
probe_n "boxing-results"         "boxing/results"          "boxing-results" 1
probe_n "boxing-getodds"         "boxing/getodds"          "boxing-getodds" 1
probe_n "mma-schedule"           "mma/schedule"            "mma-schedule" 2
probe_n "mma-shedule"            "mma/shedule"             "mma-shedule-typo" 1
probe_n "mma-home"               "mma/home"                "mma-home" 1
probe_n "mma-fighters"           "mma/fighters"            "mma-fighters" 1
probe_n "mma-results"            "mma/results"             "mma-results" 1
probe_n "mma-rankings"           "mma/rankings"            "mma-rankings" 1
probe_n "mma-getodds"            "mma/getodds"             "mma-getodds" 1
probe_n "ufc-schedule"           "ufc/schedule"            "ufc-schedule" 1

# ---------- 13. Golf (PGA / LPGA / Euro / Masters) ----------
log ""; log "===== 13. Golf ====="
probe_n "golf-home"              "golf/home"               "golf-home" 1
probe_n "golf-inplay"            "golf/inplay"             "golf-inplay" 1
probe_n "golf-leaderboard"       "golf/leaderboard"        "golf-leaderboard" 1
probe_n "golf-pga"               "golf/pga"                "golf-pga" 1
probe_n "golf-lpga"              "golf/lpga"               "golf-lpga" 1
probe_n "golf-european"          "golf/european"           "golf-european" 1
probe_n "golf-masters"           "golf/masters"            "golf-masters" 1
probe_n "golf-tournaments"       "golf/tournaments"        "golf-tournaments" 1
probe_n "golf-shedule"           "golf/shedule"            "golf-shedule" 1
probe_n "golf-players"           "golf/players"            "golf-players" 1
probe_n "golf-rankings"          "golf/rankings"           "golf-rankings" 1
probe_n "golf-getodds"           "golf/getodds"            "golf-getodds" 1

# ---------- 14. Motorsport (F1 / NASCAR / MotoGP / IndyCar) ----------
log ""; log "===== 14. Motorsport ====="
probe_n "f1-home"                "f1/home"                 "f1-home" 1
probe_n "f1-shedule"             "f1/shedule"              "f1-shedule" 1
probe_n "f1-schedule"            "f1/schedule"             "f1-schedule-typo" 1
probe_n "f1-results"             "f1/results"              "f1-results" 1
probe_n "f1-standings"           "f1/standings"            "f1-standings" 1
probe_n "f1-drivers"             "f1/drivers"              "f1-drivers" 1
probe_n "f1-races"               "f1/races"                "f1-races" 1
probe_n "f1-livetiming"          "f1/livetiming"           "f1-livetiming" 1
probe_n "f1-getodds"             "f1/getodds"              "f1-getodds" 1
probe_n "formula1-home"          "formula1/home"           "formula1-home" 1
probe_n "nascar-home"            "nascar/home"             "nascar-home" 1
probe_n "nascar-shedule"         "nascar/shedule"          "nascar-shedule" 1
probe_n "nascar-results"         "nascar/results"          "nascar-results" 1
probe_n "motogp-home"            "motogp/home"             "motogp-home" 1
probe_n "motogp-shedule"         "motogp/shedule"          "motogp-shedule" 1
probe_n "indycar-home"           "indycar/home"            "indycar-home" 1
probe_n "racing-home"            "racing/home"             "racing-home" 1

# ---------- 15. Esports (LoL / CS / Dota2) ----------
log ""; log "===== 15. Esports ====="
probe_n "esports-inplay"         "esports/inplay"          "esports-inplay" 1
probe_n "esports-home"           "esports/home"            "esports-home" 1
probe_n "esports-shedule"        "esports/shedule"         "esports-shedule" 1
probe_n "esports-lol"            "esports/lol"             "esports-lol" 1
probe_n "esports-csgo"           "esports/csgo"            "esports-csgo" 1
probe_n "esports-cs2"            "esports/cs2"             "esports-cs2" 1
probe_n "esports-dota2"          "esports/dota2"           "esports-dota2" 1
probe_n "esports-valorant"       "esports/valorant"        "esports-valorant" 1
probe_n "esoccer-inplay"         "esoccer/inplay"          "esoccer-inplay" 1

# ---------- 16. Horse racing / 灰色 sport ----------
log ""; log "===== 16. Horse racing / others ====="
probe_n "horseracing-home"       "horseracing/home"        "horseracing-home" 1
probe_n "horse-racing-home"      "horse-racing/home"       "horse-racing-home-dash" 1
probe_n "racing-uk"              "racing/uk"               "racing-uk" 1
probe_n "greyhound-home"         "greyhound/home"          "greyhound-home" 1
probe_n "cycling-home"           "cycling/home"            "cycling-home" 1
probe_n "athletics-home"         "athletics/home"          "athletics-home" 1

# ---------- 17. Generic odds endpoints (二次确认 v1 odds 阻塞) ----------
log ""; log "===== 17. odds endpoint sport-by-sport 二次确认 ====="
for s in basketball soccer football baseball hockey tennis cricket rugby handball volleyball boxing mma golf f1 esports darts snooker; do
  probe_n "getodds-${s}"         "getodds/${s}"            "getodds-${s}" 1
done
probe_n "getodds-soccer-bm"      "getodds/soccer?bookmakers=bet365"     "getodds-soccer-bm365" 1
probe_n "getodds-soccer-pinn"    "getodds/soccer?bookmakers=pinnacle"   "getodds-soccer-pinn" 1
probe_n "odds-soccer"            "odds/soccer"             "odds-soccer-root" 1
probe_n "odds-basketball"        "odds/basketball"         "odds-basketball-root" 1
probe_n "odds-feed"              "odds/feed"               "odds-feed" 1

# ---------- 18. lastupdate + If-Modified-Since 二次确认 ----------
log ""; log "===== 18. incremental 协议二次确认 ====="
ts=$(date -u +%s)
probe_n "lastupd-nbashed"   "bsktbl/nba-shedule?lastupdate=${ts}"   "lastupdate-nba-shedule" 1
probe_n "lastupd-soccer-in" "soccer/inplay?lastupdate=${ts}"        "lastupdate-soccer-inplay" 1
probe_n "lastupd-mlb"       "baseball/usa?lastupdate=${ts}"         "lastupdate-mlb" 1

# ---------- 19. Goalserve schedule refresh diff (4 round × 45s) ----------
log ""; log "===== 19. 字段刷新频率 (45s 间隔 × 4 次, 跑 inplay 拿 diff) ====="
for i in 1 2 3 4; do
  probe_once "diff-bsktbl-inplay-t${i}" "bsktbl/inplay"  "diff-bsktbl-inplay-t${i}"
  probe_once "diff-soccer-inplay-t${i}" "soccernew/inplay" "diff-soccer-inplay-t${i}"
  probe_once "diff-tennis-home-t${i}"   "tennis/home"    "diff-tennis-home-t${i}"
  probe_once "diff-baseball-usa-t${i}"  "baseball/usa"   "diff-baseball-usa-t${i}"
  if [ "$i" -lt 4 ]; then sleep 45; fi
done

# ---------- 20. 字节统计 (per file size, 给矩阵用) ----------
log ""; log "===== 20. per-file size dump (用于 v2 矩阵) ====="
for f in "${RAW_DIR}"/*; do
  [ -f "$f" ] || continue
  base=$(basename "$f")
  # 跳过 header file
  case "$base" in
    *.hdr) continue ;;
  esac
  s=$(stat -f%z "$f" 2>/dev/null || stat -c%s "$f" 2>/dev/null)
  log "  ${base}: ${s} bytes"
done

log ""
log "==== DONE. RAW_DIR=${RAW_DIR}  REPORT=${REPORT} ===="
