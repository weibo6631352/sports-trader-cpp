#!/usr/bin/env python3
"""Luck-vs-skill quantification from activity alone.
Per market realized PnL = sum(REDEEM usdc + SELL usdc) - sum(BUY usdc).
For resolved markets this is true realized profit -> hit rate, ROI, profit concentration."""
import json, time, sys, urllib.request, statistics as st
from collections import defaultdict
UA={"User-Agent":"Mozilla/5.0 research"}
def get(u,t=4,to=25):
    for i in range(t):
        try:
            with urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=to) as r:
                return json.loads(r.read().decode())
        except Exception as e:
            last=e; time.sleep(.6*(i+1))
    print("fail",u,last,file=sys.stderr); return None
ACT="https://data-api.polymarket.com/activity?user={a}&limit=500&offset={o}"
NOWREF=None
def pull(a,mx=6000):
    e=[];o=0
    while len(e)<mx:
        p=get(ACT.format(a=a,o=o))
        if not p:break
        e+=p
        if len(p)<500:break
        o+=500;time.sleep(.1)
    return e

CATS=[("Tennis",["itf-","atp-","wta-","roland","-open:","libema","ilkley","makarska","wuning"]),
 ("Soccer",["fifwc","fifa","-fc","laliga","uefa","ucl","bundesliga","ligue","mls","win on"]),
 ("NBA",["nba","lakers","celtics","knicks","euroleague"]),
 ("MLB",["mlb","yankees","dodgers","rays","red sox","cardinals","reds"," vs. "]),
 ("NFL/CFB",["nfl","cfb","superbowl"]),("NHL",["nhl","hockey","stanley"]),
 ("MMA/UFC",["ufc","mma","boxing"]),("Cricket",["cricket","ipl-","t20"]),
 ("Esports",["cs2","-lol-","dota","valorant","csgo"]),
 ("Crypto",["bitcoin","btc","ethereum","solana","price-on"]),
 ("Politics",["election","president","trump","senate","mayor","poll"]),]
def cat(s):
    s=s.lower()
    for c,k in CATS:
        if any(x in s for x in k): return c
    return "Other"

def analyze(a):
    evs=pull(a); name=(evs[0].get("name") if evs else a[:8]) or a[:8]
    now=max((e["timestamp"] for e in evs),default=0)
    M=defaultdict(lambda:dict(buy=0.0,sell=0.0,red=0.0,n=0,bw=0.0,bsh=0.0,
                              last=0,first=10**11,title="",cat=""))
    for e in evs:
        cid=e["conditionId"]; m=M[cid]
        u=e.get("usdcSize",0); ty=e["type"]
        if ty=="TRADE" and e["side"]=="BUY":
            m["buy"]+=u; m["bw"]+=e.get("price",0)*u; m["bsh"]+=e.get("size",0); m["n"]+=1
        elif ty=="TRADE" and e["side"]=="SELL": m["sell"]+=u
        elif ty=="REDEEM": m["red"]+=u
        m["last"]=max(m["last"],e["timestamp"]); m["first"]=min(m["first"],e["timestamp"])
        if e.get("title"): m["title"]=e["title"]; m["cat"]=cat(e["title"]+" "+e.get("slug",""))
    # resolved = last activity >3d ago OR has redeem (game settled)
    # CLEAN-LEG filter: only markets whose FIRST buy is well inside the pull window
    # (else the buy leg is truncated at the left edge -> realized PnL inflated).
    oldest=min((e["timestamp"] for e in evs),default=now)
    res=[]; dropped_edge=0
    for cid,m in M.items():
        resolved = (m["red"]>0) or (m["last"] < now-3*86400)
        if not resolved or m["buy"]<=0: continue
        if m["first"] < oldest + 86400:   # first buy too close to window start -> legs may be truncated
            dropped_edge+=1; continue
        pnl=m["red"]+m["sell"]-m["buy"]
        m["pnl"]=pnl; m["avgpx"]=(m["bw"]/m["buy"]) if m["buy"] else 0
        res.append(m)
    n=len(res)
    if n==0: print(f"\n{name}: no resolved markets"); return
    wins=[m for m in res if m["pnl"]>0]; losses=[m for m in res if m["pnl"]<=0]
    tot=sum(m["pnl"] for m in res); tbuy=sum(m["buy"] for m in res)
    pos=sorted(res,key=lambda m:m["pnl"],reverse=True)
    top5=sum(m["pnl"] for m in pos[:5]); top5w_share=top5/tot if tot>0 else 0
    gross_win=sum(m["pnl"] for m in wins); gross_loss=-sum(m["pnl"] for m in losses)
    # by category
    bc=defaultdict(lambda:[0,0.0,0.0])  # n, pnl, buy
    for m in res:
        bc[m["cat"]][0]+=1; bc[m["cat"]][1]+=m["pnl"]; bc[m["cat"]][2]+=m["buy"]
    span_d=(now-oldest)/86400
    print(f"\n{'='*92}\n{name}  ({a})\n{'-'*92}")
    print(f"  window={span_d:.0f}d  clean resolved markets={n:,} (dropped {dropped_edge} left-edge)  "
          f"winners={len(wins):,} ({len(wins)/n*100:.0f}%)  losers={len(losses):,}")
    print(f"  realized PnL=${tot:,.0f}   capital deployed(buy)=${tbuy:,.0f}   ROI/turnover={tot/tbuy*100:.2f}%")
    print(f"  avg win=${st.mean([m['pnl'] for m in wins]) if wins else 0:,.0f}  "
          f"avg loss=${st.mean([m['pnl'] for m in losses]) if losses else 0:,.0f}  "
          f"profit factor={gross_win/gross_loss if gross_loss>0 else float('inf'):.2f}")
    print(f"  avg entry price (buy-wtd, winners)={st.mean([m['avgpx'] for m in wins]) if wins else 0:.3f}  "
          f"(losers)={st.mean([m['avgpx'] for m in losses]) if losses else 0:.3f}")
    print(f"  PROFIT CONCENTRATION: top-5 markets = {top5w_share*100:.0f}% of total PnL  "
          f"({'CONCENTRATED→luck-ish' if top5w_share>0.6 else 'SPREAD→grinding edge'})")
    print(f"  by category (resolved):")
    for c,(cn,cp,cb) in sorted(bc.items(),key=lambda x:-x[1][1]):
        print(f"      {c:<10} n={cn:>4}  PnL=${cp:>11,.0f}  ROI={cp/cb*100 if cb else 0:>6.1f}%")

for a in sys.argv[1:]: analyze(a)
