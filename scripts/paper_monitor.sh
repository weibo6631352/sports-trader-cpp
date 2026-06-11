#!/usr/bin/env bash
# 观察 paper 盈亏趋势 + 最差持仓 (调模型用)。在服务器跑。
LOG=/tmp/paper_server.log
acct() {
  curl -s --max-time 5 http://localhost:7080/api/v1/account 2>/dev/null | python3 -c "import json,sys
try:
 d=json.load(sys.stdin)['account']
 print('net=%.2f real=%.2f unreal=%.2f fee=%.2f pos=%d eq=%.2f maxDD=%.4f'%(d['net_pnl'],d['cum_realized_pnl'],d['cum_unrealized_pnl'],d['cum_fee_paid'],d['open_positions'],d['equity'],d['max_drawdown']))
except Exception as e: print('acct err',e)"
}
ITERS=${1:-4}; IVAL=${2:-300}
for i in $(seq 1 $ITERS); do
  sleep $IVAL
  echo "=== T+$((i*IVAL/60))min $(date -u +%H:%M:%S) ==="
  acct
  echo "fills=$(grep -c 'paper_loop. FILL' $LOG 2>/dev/null) 极端背离=$(grep -c '极端背离' $LOG 2>/dev/null)"
done
echo "=== 最差 10 持仓 (unrealized) ==="
curl -s --max-time 5 http://localhost:7080/api/v1/positions 2>/dev/null | python3 -c "import json,sys
d=json.load(sys.stdin).get('positions',[])
d.sort(key=lambda p:p.get('pnl_unrealized',0))
for p in d[:10]: print('%s qty=%.1f entry=%.3f mark=%.3f unreal=%.3f real=%.3f'%(p['market_id'][:14],p.get('net_qty',0),p.get('avg_entry_price',0),p.get('mark_price',0),p.get('pnl_unrealized',0),p.get('pnl_realized',0)))
print('--- 最好 5 ---')
for p in sorted(d,key=lambda p:-p.get('pnl_unrealized',0))[:5]: print('%s unreal=%.3f real=%.3f'%(p['market_id'][:14],p.get('pnl_unrealized',0),p.get('pnl_realized',0)))"
echo "=== done $(date -u +%H:%M:%S) ==="
