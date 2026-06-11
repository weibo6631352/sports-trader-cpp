#!/usr/bin/env python3
"""Multi-window persistence test of in-play TIMING ALPHA.
For each (trader, day-window): timing_alpha = their +30min fwd return − baseline +30min drift
(baseline strips the 'winner naturally drifts to 1' confound). Then test persistence across windows.
Usage: python3 multiwindow.py <wallet> <T0,T0,...> [hours] [topn]"""
import json, time, sys, urllib.request, bisect, math
from collections import defaultdict
UA={"User-Agent":"Mozilla/5.0 research"}
def get(u,t=4,to=25):
    for i in range(t):
        try:
            with urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=to) as r:
                return json.loads(r.read().decode())
        except Exception as e: last=e; time.sleep(.5*(i+1))
    return None
ACT="https://data-api.polymarket.com/activity?user={a}&limit=500&start={s}&end={e}"
PH="https://clob.polymarket.com/prices-history?market={m}&startTs={s}&endTs={e}&fidelity=1"
def price_at(st,sp,ts):
    i=bisect.bisect_right(st,ts)-1
    return sp[i] if i>=0 else None

def analyze_window(A,T0,H,TOPN):
    T1=T0+H*3600; SUB=1800; evs=[]; t=T0
    while t<T1:
        p=get(ACT.format(a=A,s=t,e=min(t+SUB,T1)))
        if p: evs+=p
        t+=SUB; time.sleep(0.04)
    seen=set(); bytok=defaultdict(lambda:dict(fills=[],buy=0.0))
    for e in evs:
        if e["type"]!="TRADE" or e["side"]!="BUY": continue
        k=(e.get("transactionHash"),e.get("asset"),e.get("timestamp"),e.get("size"))
        if k in seen: continue
        seen.add(k); m=bytok[e["asset"]]
        m["fills"].append((e["timestamp"],e.get("price",0),e.get("usdcSize",0))); m["buy"]+=e.get("usdcSize",0)
    toks=sorted(bytok.items(),key=lambda x:-x[1]["buy"])[:TOPN]
    tot=0.0; n_tok=0; won=0; edge_s=0.0; fwd30=0.0; fwd30_h=0.0; base30=0.0; dip=0.0
    for tk,m in toks:
        fl=sorted(m["fills"]); t_first=fl[0][0]
        hist=get(PH.format(m=tk,s=t_first-1200,e=t_first+8*3600)); time.sleep(0.04)
        if not hist or not hist.get("history"): continue
        st=[x["t"] for x in hist["history"]]; sp=[x["p"] for x in hist["history"]]
        settle=sp[-1]; lo=min(sp); hi=max(sp)
        if settle>0.85: outcome=1.0; won+=1
        elif settle<0.15: outcome=0.0
        else: continue
        n_tok+=1
        bd=[price_at(st,sp,st[i]+1800)-sp[i] for i in range(len(st)) if price_at(st,sp,st[i]+1800) is not None]
        b30=sum(bd)/len(bd) if bd else 0.0
        tk_usd=sum(u for _,_,u in fl if u>0); base30+=b30*tk_usd
        for (ts,p,u) in fl:
            if u<=0: continue
            tot+=u; edge_s+=(outcome-p)*u
            fp=price_at(st,sp,ts+1800)
            if fp is not None: fwd30+=(fp-p)*u; fwd30_h+=u
            if hi>lo and (p-lo)/(hi-lo)<0.5: dip+=u
    if tot<=0: return None
    tr30=fwd30/fwd30_h if fwd30_h else 0.0; bl30=base30/tot
    return dict(n=n_tok,usd=tot,winr=won/max(n_tok,1),A=edge_s/tot,
                tr30=tr30,bl30=bl30,talpha=tr30-bl30,dip=dip/tot)

def main():
    A=sys.argv[1]; T0s=[int(x) for x in sys.argv[2].split(",")]
    H=int(sys.argv[3]) if len(sys.argv)>3 else 24; TOPN=int(sys.argv[4]) if len(sys.argv)>4 else 40
    import datetime as dt
    def d(x): return dt.datetime.fromtimestamp(x,dt.timezone.utc).strftime("%m-%d")
    print(f"\n{'='*84}\n{A}\n{'-'*84}")
    print(f"{'window':<8}{'mkts':>5}{'$anal':>10}{'winr':>6}{'A(¢)':>8}{'fwd30':>8}{'base30':>8}{'TIMING_A':>10}{'dip%':>6}")
    rows=[]
    for T0 in T0s:
        r=analyze_window(A,T0,H,TOPN)
        if not r: print(f"{d(T0):<8}  (no data)"); continue
        rows.append(r)
        print(f"{d(T0):<8}{r['n']:>5}{r['usd']:>10,.0f}{r['winr']*100:>5.0f}%{r['A']*100:>7.2f}"
              f"{r['tr30']*100:>8.2f}{r['bl30']*100:>8.2f}{r['talpha']*100:>9.2f}¢{r['dip']*100:>5.0f}%")
    if len(rows)>=2:
        ta=[r['talpha'] for r in rows]; mean=sum(ta)/len(ta)
        sd=math.sqrt(sum((x-mean)**2 for x in ta)/(len(ta)-1)); se=sd/math.sqrt(len(ta))
        tstat=mean/se if se>0 else float('inf')
        npos=sum(1 for x in ta if x>0)
        wmean_ta=sum(r['talpha']*r['usd'] for r in rows)/sum(r['usd'] for r in rows)
        wmean_A=sum(r['A']*r['usd'] for r in rows)/sum(r['usd'] for r in rows)
        print(f"{'-'*84}")
        print(f"  TIMING ALPHA across {len(rows)} windows: mean={mean*100:+.2f}¢  sd={sd*100:.2f}¢  "
              f"t={tstat:.2f}  positive={npos}/{len(rows)}  $wtd={wmean_ta*100:+.2f}¢")
        print(f"  REALIZED EDGE (A) $wtd across windows = {wmean_A*100:+.2f}¢/share")
        verdict = "PERSISTENT timing alpha" if (tstat>2 and npos>=len(rows)*0.7) else \
                  "weak/inconsistent (likely selection-driven, not dip-timing)" if mean*100<0.5 else "suggestive, needs more windows"
        print(f"  VERDICT: {verdict}")

if __name__=="__main__": main()
