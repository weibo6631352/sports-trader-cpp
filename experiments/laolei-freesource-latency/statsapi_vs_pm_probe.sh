#!/usr/bin/env bash
# MLB statsapi(官方,场内,早于转播) vs PM 价格 延迟探针 (老雷 2026-06-09, 隔离不碰生产)。
# 假设: 官方数据早于转播 → 早于看转播的 PM 看客(0.3s) → 我们快服务器可在 PM 重定价前下单 = 速度 edge 复活。
# 用法: 有 live MLB 赛时跑。在伦敦服务器跑(近 PM): ~/stcpp-ops/run.sh < statsapi_vs_pm_probe.sh
# 输出 CSV: 同时记 statsapi 的(总得分,play endTime,本地拉取 ms) + PM 该盘 YES mid(本地 ms)。
#   分析: 当 statsapi 总得分变化(得分事件)的【拉取时刻】 vs PM YES mid 明显跳变的时刻, 比谁先。
#   statsapi 先 = 官方数据领先 PM 看客 = 速度 edge 真实存在, 值得用 statsapi 替 Goalserve 复活 in-play。

# 1) 找一场 live MLB + 它的 PM 盘 (按队名匹配, 需手填或用系统 EventMatcher)
SCHED="https://statsapi.mlb.com/api/v1/schedule?sportId=1"
GID=$(curl -s --max-time 8 "$SCHED" 2>/dev/null | python3 -c "
import sys,json
d=json.load(sys.stdin)
for dt in d.get('dates',[]):
  for g in dt.get('games',[]):
    if g.get('status',{}).get('abstractGameState')=='Live':
      print(g['gamePk']); raise SystemExit
")
[ -z "$GID" ] && { echo 'no live MLB game now — 等有 live 赛再跑'; exit 0; }
echo "live gamePk=$GID"
# 需手填该场的 PM condition_id (用 gamma 按队名找; 或接系统 token_map)
PM_COND="${PM_COND:-FILL_PM_CONDITION_ID}"
echo "ts_ms,source,detail"
for i in $(seq 1 1800); do   # ~30min @1s
  NOW=$(python3 -c 'import time;print(int(time.time()*1000))')
  # statsapi: 总得分 + 最后 play 时间
  curl -s --max-time 2 "https://statsapi.mlb.com/api/v1.1/game/$GID/feed/live" 2>/dev/null | python3 -c "
import sys,json,os
now=os.environ['NOW']
try:
  d=json.load(sys.stdin); ld=d.get('liveData',{})
  ls=ld.get('linescore',{}); r=ls.get('teams',{})
  tot='%s-%s'%(r.get('away',{}).get('runs',''),r.get('home',{}).get('runs',''))
  cp=ld.get('plays',{}).get('currentPlay',{}).get('about',{}).get('endTime','')
  print('%s,STATSAPI,runs=%s play=%s ts=%s'%(now,tot,cp,d.get('metaData',{}).get('timeStamp','')))
except: pass
" NOW="$NOW" 2>/dev/null
  # PM: 该盘 YES mid (need running paper_server OR direct clob); 这里走 paper_server /book_pair (若在跑)
  curl -s --max-time 2 "http://localhost:8080/api/v1/book_pair/$PM_COND" 2>/dev/null | python3 -c "
import sys,json,os
now=os.environ['NOW']
try:
  d=json.load(sys.stdin); t0=d.get('token0',{})
  mid=(float(t0.get('best_bid',0))+float(t0.get('best_ask',0)))/2
  print('%s,PM,yes_mid=%.4f'%(now,mid))
except: pass
" NOW="$NOW" 2>/dev/null
  sleep 1
done
