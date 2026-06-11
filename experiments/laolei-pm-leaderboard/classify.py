#!/usr/bin/env python3
"""Broad strategy classifier across profit+volume leaderboards. Surfaces NON-odds-source
edge archetypes: negRisk structural arb / market-making rebates / resolution carry /
politics-crypto-news directional. Reads recent activity (type enum) + positions per wallet."""
import json, time, sys, urllib.request
from collections import defaultdict
UA={"User-Agent":"Mozilla/5.0 research"}
def get(u,t=4,to=25):
    for i in range(t):
        try:
            with urllib.request.urlopen(urllib.request.Request(u,headers=UA),timeout=to) as r:
                return json.loads(r.read().decode())
        except Exception as e: last=e; time.sleep(.5*(i+1))
    return None
LBP="https://lb-api.polymarket.com/profit?period=month&limit=50"
LBV="https://lb-api.polymarket.com/volume?period=month&limit=50"
ACT="https://data-api.polymarket.com/activity?user={a}&limit=500&offset={o}"
POS="https://data-api.polymarket.com/positions?user={a}&limit=500&sortBy=CURRENT&sortDirection=DESC"

SPORT_KW=["nba","nfl","mlb","nhl","ufc","mma","itf-","atp-","wta-","tennis","soccer","fifwc",
 "fifa","-fc","laliga","uefa","ucl","bundesliga","ligue","cricket","-cs2","-lol-","dota",
 "valorant"," vs. ","win on 2026","roland","libema","open:","cbb","cfb","euroleague","-odi-","t20","boxing","hockey","baseball","wnba"]
POL_KW=["election","president","trump","biden","senate","governor","democrat","republican",
 "mayor","parliament","prime-minister","congress","supreme-court","resign","impeach","poll","cabinet","nominee","speaker"]
CRY_KW=["bitcoin","btc","ethereum","-eth-","solana","-sol-","crypto","price-on","above-","dogecoin","xrp","strategic-reserve","etf"]
ECON_KW=["fed-","interest-rate","cpi","recession","gdp","jobs-report","rate-cut","inflation","tariff","shutdown"]
def cat(s):
    s=s.lower()
    if any(k in s for k in SPORT_KW): return "Sports"
    if any(k in s for k in CRY_KW): return "Crypto"
    if any(k in s for k in ECON_KW): return "Econ"
    if any(k in s for k in POL_KW): return "Politics"
    return "Other"

def pull_act(a,pages=3):
    e=[]
    for pg in range(pages):
        p=get(ACT.format(a=a,o=pg*500))
        if not p: break
        e+=p
        if len(p)<500: break
        time.sleep(0.05)
    return e

def profile(a,name):
    evs=pull_act(a); pos=get(POS.format(a=a)) or []; time.sleep(0.05)
    T=defaultdict(int)
    for e in evs: T[e["type"]]+=1
    trades=[e for e in evs if e["type"]=="TRADE"]
    buys=[e for e in trades if e["side"]=="BUY"]; sells=[e for e in trades if e["side"]=="SELL"]
    catu=defaultdict(float)
    chalk=mid=longshot=0.0
    for e in buys:
        u=e.get("usdcSize",0); p=e.get("price",0)
        catu[cat(e.get("title","")+" "+e.get("slug",""))]+=u
        if p>0.95: chalk+=u
        elif p<0.05: longshot+=u
        else: mid+=u
    tot=sum(catu.values()) or 1
    buy_usd=sum(e.get("usdcSize",0) for e in buys)
    negrisk=sum(1 for p in pos if p.get("negativeRisk"))
    struct=T["SPLIT"]+T["MERGE"]+T.get("CONVERSION",0)
    mm=T.get("MAKER_REBATE",0)+T.get("REWARD",0)+T.get("YIELD",0)
    topcat=max(catu.items(),key=lambda x:x[1])[0] if catu else "?"
    # archetype heuristic
    arche=[]
    if struct>=3 or negrisk>=40: arche.append("negRisk-arb")
    if mm>=2: arche.append("mkt-maker")
    if buy_usd>0 and chalk/buy_usd>0.5: arche.append("resolution-carry")
    if buy_usd>0 and longshot/buy_usd>0.4: arche.append("longshot-punt")
    if topcat=="Sports" and catu.get("Sports",0)/tot>0.6: arche.append("sports-grind")
    if topcat in("Politics","Crypto","Econ") and catu.get(topcat,0)/tot>0.5: arche.append(f"{topcat}-directional")
    if len(sells)>0.3*max(len(buys),1): arche.append("active-2sided")
    return dict(name=name,wallet=a,nev=len(evs),topcat=topcat,
                catmix={k:round(v/tot*100) for k,v in sorted(catu.items(),key=lambda x:-x[1])},
                struct=struct,negrisk=negrisk,mm=mm,redeem=T["REDEEM"],
                buy=len(buys),sell=len(sells),
                chalk_pct=round(chalk/buy_usd*100) if buy_usd else 0,
                longshot_pct=round(longshot/buy_usd*100) if buy_usd else 0,
                pos=len(pos),arche=arche or ["?"])

def main():
    pr=get(LBP) or []; vo=get(LBV) or []
    seen=set(); wl=[]
    for t in pr[:35]+vo[:35]:
        w=t["proxyWallet"]
        if w in seen: continue
        seen.add(w); wl.append((w,(t.get("name") or w[:10])[:16]))
    print(f"classifying {len(wl)} wallets (profit∪volume)...",file=sys.stderr)
    res=[]
    for i,(w,n) in enumerate(wl):
        res.append(profile(w,n))
        if (i+1)%10==0: print(f"  {i+1}/{len(wl)}",file=sys.stderr)
    json.dump(res,open("classify.json","w"),indent=1)
    # print grouped by archetype
    print(f"\n{'name':<17}{'topcat':<9}{'arche':<46}{'struct':>6}{'negR':>5}{'mm':>4}{'chalk%':>7}{'long%':>6}{'b/s':>9}")
    print("-"*120)
    order={"negRisk-arb":0,"mkt-maker":1,"resolution-carry":2,"Crypto-directional":3,"Politics-directional":3,"Econ-directional":3,"sports-grind":5}
    res.sort(key=lambda r:min([order.get(a,4) for a in r["arche"]]+[9]))
    for r in res:
        print(f"{r['name']:<17}{r['topcat']:<9}{','.join(r['arche'])[:45]:<46}{r['struct']:>6}{r['negrisk']:>5}{r['mm']:>4}{r['chalk_pct']:>6}%{r['longshot_pct']:>5}%{r['buy']:>4}/{r['sell']:<4}")

if __name__=="__main__": main()
