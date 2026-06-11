#!/usr/bin/env python3
"""Clean realized-PnL over a bounded historical window via time-paged activity.
Reconstruct per-market cashflow for markets FULLY CONTAINED in the window
(buys captured well inside left edge, resolved before right edge)."""
import json, time, sys, urllib.request, statistics as st
from collections import defaultdict
UA={"User-Agent":"Mozilla/5.0 research"}
def get(u,t=4,to=25):
    for i in range(t):
        try:
            with urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=to) as r:
                return json.loads(r.read().decode())
        except Exception as e: last=e; time.sleep(.5*(i+1))
    return None
A=sys.argv[1]
T0=int(sys.argv[2]); HOURS=int(sys.argv[3]) if len(sys.argv)>3 else 30
T1=T0+HOURS*3600
SUB=1800  # 30-min sub-windows
ACT="https://data-api.polymarket.com/activity?user={a}&limit=500&start={s}&end={e}"
evs=[]; t=T0; capped=0
while t<T1:
    p=get(ACT.format(a=A,s=t,e=min(t+SUB,T1)))
    if p:
        evs+=p
        if len(p)>=500: capped+=1
    t+=SUB; time.sleep(0.06)
# dedup by txhash+asset+type
seen=set(); uniq=[]
for e in evs:
    k=(e.get("transactionHash"),e.get("asset"),e.get("type"),e.get("timestamp"),e.get("size"))
    if k in seen: continue
    seen.add(k); uniq.append(e)
evs=uniq
M=defaultdict(lambda:dict(buy=0.0,sell=0.0,red=0.0,bw=0.0,n=0,first=10**11,last=0,title="",cat=""))
for e in evs:
    m=M[e["conditionId"]]; u=e.get("usdcSize",0)
    if e["type"]=="TRADE" and e["side"]=="BUY": m["buy"]+=u; m["bw"]+=e.get("price",0)*u; m["n"]+=1
    elif e["type"]=="TRADE" and e["side"]=="SELL": m["sell"]+=u
    elif e["type"]=="REDEEM": m["red"]+=u
    m["first"]=min(m["first"],e["timestamp"]); m["last"]=max(m["last"],e["timestamp"])
    if e.get("title"): m["title"]=e["title"]
# contained: first buy >1h after window start, resolved (redeem) before 1h before end
res=[]
for cid,m in M.items():
    if m["buy"]<=0: continue
    if m["first"] < T0+3600: continue          # left-truncated
    if m["red"]<=0 and m["sell"]<=0: continue   # not resolved/closed in window
    if m["last"] > T1-1800: continue            # right-truncated
    m["pnl"]=m["red"]+m["sell"]-m["buy"]; m["avgpx"]=m["bw"]/m["buy"]
    res.append(m)
import datetime as dt
def ds(x): return dt.datetime.fromtimestamp(x,dt.timezone.utc).strftime("%m-%d %H:%M")
n=len(res)
print(f"window {ds(T0)}..{ds(T1)} UTC  raw_events={len(evs)}  capped_subwins={capped}  contained_markets={n}")
if n:
    wins=[m for m in res if m["pnl"]>0]; tot=sum(m["pnl"] for m in res); tbuy=sum(m["buy"] for m in res)
    gl=-sum(m["pnl"] for m in res if m["pnl"]<=0); gw=sum(m["pnl"] for m in wins)
    print(f"  winners={len(wins)}/{n} ({len(wins)/n*100:.0f}%)  realized=${tot:,.0f}  buy=${tbuy:,.0f}  "
          f"ROI/turnover={tot/tbuy*100:.2f}%  profit_factor={gw/gl if gl else float('inf'):.2f}")
    print(f"  avg entry px: winners={st.mean([m['avgpx'] for m in wins]) if wins else 0:.3f}  "
          f"losers={st.mean([m['avgpx'] for m in res if m['pnl']<=0]) if n-len(wins) else 0:.3f}")
    pos=sorted(res,key=lambda m:m['pnl'],reverse=True)
    print(f"  top5 share of PnL={sum(m['pnl'] for m in pos[:5])/tot*100 if tot>0 else 0:.0f}%  "
          f"(spread→edge / concentrated→variance)")
    print("  sample best/worst:")
    for m in pos[:3]+pos[-3:]:
        print(f"     ${m['pnl']:>8,.0f}  buy=${m['buy']:>7,.0f} @{m['avgpx']:.2f} n={m['n']:>3}  {m['title'][:48]}")
