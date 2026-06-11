#!/usr/bin/env python3
"""In-play vs pregame signal: intra-market BUY price dispersion + trade time-span per market.
A pregame value bettor enters a market in a tight price/time window.
An in-play trader buys the same match at widely varying prices over hours (riding the swing)."""
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
def pull(a,mx=3000):
    e=[];o=0
    while len(e)<mx:
        p=get(ACT.format(a=a,o=o))
        if not p:break
        e+=p
        if len(p)<500:break
        o+=500;time.sleep(.12)
    return e
for a in sys.argv[1:]:
    evs=pull(a); name=(evs[0].get("name") if evs else a[:8]) or a[:8]
    tr=[e for e in evs if e["type"]=="TRADE" and e["side"]=="BUY"]
    bym=defaultdict(list)
    for e in tr: bym[e["conditionId"]].append(e)
    ranges=[]; spans=[]; nfill=[]
    for cid,es in bym.items():
        if len(es)<2:
            nfill.append(1); continue
        ps=[e.get("price",0) for e in es]; ts=[e["timestamp"] for e in es]
        ranges.append(max(ps)-min(ps)); spans.append((max(ts)-min(ts))/3600.0); nfill.append(len(es))
    big_range=sum(1 for r in ranges if r>0.20)
    long_span=sum(1 for s in spans if s>1.0)   # traded same match over >1h
    hrs=defaultdict(int)
    for e in tr: hrs[(e["timestamp"]//3600)%24]+=1
    tot=sum(hrs.values()) or 1
    peakh=sorted(hrs.items(),key=lambda x:-x[1])[:4]
    print(f"\n=== {name} ({a[:10]}) ===")
    print(f"  markets touched={len(bym)}  fills/market: med={int(st.median(nfill))} max={max(nfill)}")
    print(f"  intra-market BUY price range: med={st.median(ranges) if ranges else 0:.3f}  "
          f">0.20 in {big_range}/{len(ranges)} multi-fill markets ({(big_range/max(len(ranges),1))*100:.0f}%)")
    print(f"  intra-market time-span: med={st.median(spans) if spans else 0:.2f}h  "
          f">1h in {long_span}/{len(spans)} ({(long_span/max(len(spans),1))*100:.0f}%)  -> in-play accumulation signal")
    print(f"  busiest UTC hours: " + ", ".join(f"{h:02d}h={n*100//tot}%" for h,n in peakh))
