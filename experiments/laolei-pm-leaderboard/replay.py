#!/usr/bin/env python3
"""In-play edge replay: for a trader's fills in a resolved historical window, pull PM CLOB
price trajectory per market and measure (A) realized edge to settlement, (B) causal forward
reversion after each buy (capturable without hindsight), (C) dip-buying position in range.
Usage: python3 replay.py <wallet> <T0_unix> [hours] [top_markets]"""
import json, time, sys, urllib.request, bisect
from collections import defaultdict
UA={"User-Agent":"Mozilla/5.0 research"}
def get(u,t=4,to=25):
    for i in range(t):
        try:
            with urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=to) as r:
                return json.loads(r.read().decode())
        except Exception as e: last=e; time.sleep(.5*(i+1))
    return None
A=sys.argv[1]; T0=int(sys.argv[2]); H=int(sys.argv[3]) if len(sys.argv)>3 else 30
TOPN=int(sys.argv[4]) if len(sys.argv)>4 else 60
T1=T0+H*3600; SUB=1800
ACT="https://data-api.polymarket.com/activity?user={a}&limit=500&start={s}&end={e}"
PH="https://clob.polymarket.com/prices-history?market={m}&startTs={s}&endTs={e}&fidelity=1"

# 1. pull fills
evs=[]; t=T0
while t<T1:
    p=get(ACT.format(a=A,s=t,e=min(t+SUB,T1)))
    if p: evs+=p
    t+=SUB; time.sleep(0.05)
seen=set(); fills=[]
for e in evs:
    if e["type"]!="TRADE" or e["side"]!="BUY": continue
    k=(e.get("transactionHash"),e.get("asset"),e.get("timestamp"),e.get("size"))
    if k in seen: continue
    seen.add(k)
    fills.append(e)
# group by token(asset)
bytok=defaultdict(lambda:dict(fills=[],buy=0.0,title="",cid=""))
for e in fills:
    tk=e["asset"]; m=bytok[tk]
    m["fills"].append((e["timestamp"],e.get("price",0),e.get("usdcSize",0)))
    m["buy"]+=e.get("usdcSize",0); m["title"]=e.get("title",""); m["cid"]=e["conditionId"]
toks=sorted(bytok.items(),key=lambda x:-x[1]["buy"])[:TOPN]
print(f"{A[:10]}: window {H}h  fills={len(fills)}  tokens touched={len(bytok)}  analyzing top {len(toks)} by $",file=sys.stderr)

def price_at(series_t, series_p, ts):
    i=bisect.bisect_right(series_t,ts)-1
    return series_p[i] if i>=0 else None

# 2+3. per token: fetch trajectory, compute metrics
W=defaultdict(float)  # weighted sums (by usd)
tot_usd=0.0; n_tok=0; settled=0; won=0; dip_usd=0.0
fwd_have={"5":0.0,"30":0.0}; fwd_sum={"5":0.0,"30":0.0}
edge_settle_usd=0.0
base_fwd30_usd=0.0   # baseline drift +30min over ALL times, weighted by their $ in token
below_vwap_usd=0.0   # their fill vs market mean over their active span
for tk,m in toks:
    fl=sorted(m["fills"]); t_first=fl[0][0]
    hist=get(PH.format(m=tk,s=t_first-1200,e=t_first+8*3600))
    time.sleep(0.05)
    if not hist or not hist.get("history"): continue
    H_=hist["history"]; st=[x["t"] for x in H_]; sp=[x["p"] for x in H_]
    settle=sp[-1]; lo=min(sp); hi=max(sp)
    if settle>0.85: outcome=1.0; settled+=1; won+=1
    elif settle<0.15: outcome=0.0; settled+=1
    else: continue  # unresolved within window
    n_tok+=1
    # baseline +30min drift over ALL bars in this token (unweighted mean), then weight by their $
    bdiffs=[price_at(st,sp,st[i]+1800)-sp[i] for i in range(len(st)) if price_at(st,sp,st[i]+1800) is not None]
    base30 = sum(bdiffs)/len(bdiffs) if bdiffs else 0.0
    tk_usd = sum(u for _,_,u in fl if u>0)
    base_fwd30_usd += base30 * tk_usd
    # market mean price over their active span [t_first,t_last] (consensus while they traded)
    t_lo=fl[0][0]; t_hi=fl[-1][0]
    span_p=[sp[i] for i in range(len(st)) if t_lo<=st[i]<=t_hi] or [sp[bisect.bisect_right(st,t_lo)-1 if st else 0]]
    mkt_mean=sum(span_p)/len(span_p)
    for (ts,p,u) in fl:
        if u<=0: continue
        tot_usd+=u
        edge_settle_usd += (outcome-p)*u            # realized edge to settlement, $ per share*usd
        below_vwap_usd += (mkt_mean-p)*u            # >0 = filled below market consensus (timing)
        # causal forward reversion
        for k,sec in (("5",300),("30",1800)):
            fp=price_at(st,sp,ts+sec)
            if fp is not None:
                fwd_sum[k]+= (fp-p)*u; fwd_have[k]+=u
        # dip position in game range
        if hi>lo and (p-lo)/(hi-lo) < 0.5: dip_usd+=u

if tot_usd<=0: print("no usable fills"); sys.exit()
print(f"\n{'='*78}\n{A}\n{'-'*78}")
print(f"  tokens resolved/analyzed={n_tok}  token win-rate={won/max(n_tok,1)*100:.0f}%  $analyzed=${tot_usd:,.0f}")
print(f"  (A) REALIZED EDGE to settlement (size-wtd) = {edge_settle_usd/tot_usd*100:+.2f} c/share "
      f"  -> per $1 staked nets ${edge_settle_usd/tot_usd:+.3f}")
for k in ("5","30"):
    if fwd_have[k]>0:
        print(f"  (B) CAUSAL forward reversion +{k}min (size-wtd) = {fwd_sum[k]/fwd_have[k]*100:+.2f} c/share")
tr30=fwd_sum["30"]/fwd_have["30"] if fwd_have["30"] else 0
bl30=base_fwd30_usd/tot_usd
print(f"  (B') BASELINE +30min drift (random entry, same tokens, $-wtd) = {bl30*100:+.2f} c/share")
print(f"       -> TIMING ALPHA = their+30min − baseline = {(tr30-bl30)*100:+.2f} c/share  "
      f"({'REAL dip-timing (capturable w/ fast exec)' if (tr30-bl30)>0.005 else 'mostly outcome-selection, NOT dip-timing'})")
print(f"  (D) fill vs market consensus (mean px over their active span, $-wtd) = "
      f"{below_vwap_usd/tot_usd*100:+.2f} c cheaper  (>0 = bought below consensus = timing edge)")
print(f"  (C) dip-buying: {dip_usd/tot_usd*100:.0f}% of $ in LOWER half of game price range")
