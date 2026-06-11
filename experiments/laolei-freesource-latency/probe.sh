#!/usr/bin/env bash
# 免费源延迟探针 (老雷 2026-06-09, 隔离实验不碰生产)。
# 用法: 有 live MLB 赛时跑 — 同时轮询 ESPN + Goalserve inplay 同一赛的比分, 比分变化时比时间戳。
# 在服务器跑 (有 .env Goalserve key): ~/stcpp-ops/run.sh < probe.sh   (或 scp 后 bash probe.sh)
set -a; [ -f /home/ec2-user/sports-trader-cpp/.env ] && source /home/ec2-user/sports-trader-cpp/.env; set +a
ESPN="https://site.api.espn.com/apis/site/v2/sports/baseball/mlb/scoreboard"
GS="http://inplay.goalserve.com/inplay-baseball.gz?key=${GOALSERVE_API_KEY}"   # 生产用同一 inplay feed
echo "ts_ms,source,game,score"
for i in $(seq 1 600); do   # ~10min @1s
  NOW=$(python3 -c 'import time;print(int(time.time()*1000))')
  # ESPN: 取 in-progress 赛比分
  curl -s --max-time 2 "$ESPN" 2>/dev/null | python3 -c "
import sys,json,os
now=os.environ.get('NOW','')
try:
  d=json.load(sys.stdin)
  for e in d.get('events',[]):
    c=e.get('competitions',[{}])[0]
    if c.get('status',{}).get('type',{}).get('state')!='in': continue
    cs=c.get('competitors',[]); g='/'.join(x.get('team',{}).get('abbreviation','') for x in cs)
    sc='-'.join(str(x.get('score','')) for x in cs)
    print('%s,ESPN,%s,%s'%(now,g,sc))
except: pass
" NOW="$NOW" 2>/dev/null
  # Goalserve: 生产 inplay feed (gz) 的比分 (格式见 inplay_score_parser); 这里只标记拉取时刻供对比
  GST=$(curl -s --max-time 2 "$GS" 2>/dev/null | gunzip 2>/dev/null | grep -oE 'updated_ts="[0-9]+"' | head -1)
  echo "$NOW,GS_feed_version,$GST"
  sleep 1
done
# 分析: 同一 game 同一比分, ESPN 行的 ts_ms 比 GS feed 版本时刻早多少 = ESPN 领先 Goalserve 的量。
# 注意: 即便 ESPN 领先 Goalserve, 仍需过 PM 效率墙(PM ~0.3s 直接看客) — 见 FINDINGS.md §3。
