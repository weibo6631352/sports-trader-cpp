#!/usr/bin/env python3
"""Scan negRisk multi-outcome events for structural arb.
Buy-all-YES arb if Σ(YES ask over real outcomes) < 1.  NO-side arb if Σ(YES bid) > 1.
'real/liquid' = has a bestBid (not null) and ask<0.99 (excludes dead placeholder slots)."""
import json, time, sys, urllib.request
UA={"User-Agent":"Mozilla/5.0 research"}
def get(u,t=4,to=30):
    for i in range(t):
        try:
            with urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=to) as r:
                return json.loads(r.read().decode())
        except Exception as e: last=e; time.sleep(.5*(i+1))
    return None
EV="https://gamma-api.polymarket.com/events?closed=false&order=volume&ascending=false&limit={n}&offset={o}"
def f(x):
    try: return float(x)
    except: return None
rows=[]
for off in range(0,300,100):
    evs=get(EV.format(n=100,o=off)) or []
    for e in evs:
        if not e.get("negRisk"): continue
        mk=e.get("markets",[])
        if len(mk)<3: continue
        sa=sb=0.0; nl=0; nslot=len(mk)
        for m in mk:
            a=f(m.get("bestAsk")); b=f(m.get("bestBid"))
            if b is None or a is None or a>=0.99: continue  # dead/placeholder
            sa+=a; sb+=b; nl+=1
        if nl<3: continue
        rows.append(dict(title=e["title"][:42],n=nslot,nl=nl,sa=sa,sb=sb,
                         vol=f(e.get("volume")) or 0))
    time.sleep(0.1)
# arb flags
print(f"scanned {len(rows)} liquid negRisk events\n")
print(f"{'event':<44}{'#out':>5}{'#liq':>5}{'ΣYESask':>9}{'ΣYESbid':>9}{'buyArb':>8}{'noArb':>7}{'vol$M':>8}")
print("-"*100)
buy=[]; no=[]
for r in sorted(rows,key=lambda x:-x["vol"]):
    buy_arb=max(0.0,1.0-r["sa"]); no_arb=max(0.0,r["sb"]-1.0)
    if buy_arb>0.01: buy.append(r)
    if no_arb>0.02: no.append(r)
    flag = "  <<BUY" if buy_arb>0.01 else ("  <<NO" if no_arb>0.02 else "")
    print(f"{r['title']:<44}{r['n']:>5}{r['nl']:>5}{r['sa']:>9.3f}{r['sb']:>9.3f}"
          f"{buy_arb:>8.3f}{no_arb:>7.3f}{r['vol']/1e6:>8.0f}{flag}")
print(f"\nBUY-all-YES arb candidates (Σask<0.99): {len(buy)}")
print(f"NO-side arb candidates (Σbid>1.02): {len(no)}")
