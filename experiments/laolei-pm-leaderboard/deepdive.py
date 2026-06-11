#!/usr/bin/env python3
"""Deep-dive selected wallets: market mix, entry-price profile, trade style.
Usage: python3 deepdive.py <wallet> [<wallet> ...]"""
import json, time, sys, urllib.request, math
from collections import defaultdict

UA = {"User-Agent": "Mozilla/5.0 research"}
def get(url, tries=4, timeout=25):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return json.loads(r.read().decode())
        except Exception as e:
            last=e; time.sleep(0.6*(i+1))
    print(f"  ! fail {url}: {last}", file=sys.stderr); return None

ACT = "https://data-api.polymarket.com/activity?user={a}&limit=500&offset={o}"
POS = "https://data-api.polymarket.com/positions?user={a}&limit=500&sortBy=CURRENT&sortDirection=DESC"
NAME= "https://lb-api.polymarket.com/profit?period=all&limit=1&address={a}"

# market category by keyword on title/slug/eventSlug
CATS = [
 ("Tennis",   ["itf-","atp-","wta-","tennis","-vs-" ]),  # -vs- is weak; refined below
 ("Soccer",   ["fifwc","fifa","-fc-","soccer","laliga","epl","uefa","ucl","bundesliga","seriea","ligue","mls","-utd","arsenal","madrid","barca"]),
 ("Basketball/NBA",["nba","basketball","lakers","celtics","-nbafinals","euroleague"]),
 ("Baseball/MLB",["mlb","baseball","yankees","dodgers","-mlb-"]),
 ("NFL/CFB",  ["nfl","-cfb","superbowl","super-bowl","football-"]),
 ("Hockey/NHL",["nhl","hockey","stanley"]),
 ("MMA/UFC/Box",["ufc","mma","-boxing","fight"]),
 ("Cricket",  ["cricket","ipl-","-odi-","-t20"]),
 ("Esports",  ["-cs2","-lol-","dota","valorant","esports","csgo"]),
 ("Crypto",   ["bitcoin","btc","ethereum","-eth-","crypto","solana","price-on"]),
 ("Politics/Election",["election","-president","trump","biden","senate","governor","poll","democrat","republican","mamdani","nyc-mayor","parliament","prime-minister"]),
 ("Econ/Fed", ["fed-","interest-rate","cpi","recession","gdp","jobs-report","rate-cut"]),
]
def categorize(title, slug, eslug):
    s = f"{title} {slug} {eslug}".lower()
    # sports leagues first by slug prefix
    for cat, kws in CATS:
        for kw in kws:
            if kw in s:
                return cat
    return "Other"

def pull_activity(a, max_events=3000):
    evs=[]; off=0
    while len(evs) < max_events:
        page = get(ACT.format(a=a, o=off))
        if not page: break
        evs += page
        if len(page) < 500: break
        off += 500; time.sleep(0.12)
    return evs

def profile(a):
    evs = pull_activity(a)
    pos = get(POS.format(a=a)) or []
    time.sleep(0.1)
    name = (evs[0].get("name") if evs else None) or a[:10]
    now = max((e["timestamp"] for e in evs), default=0)
    month_ago = now - 31*86400
    trades = [e for e in evs if e["type"]=="TRADE"]
    redeems= [e for e in evs if e["type"]=="REDEEM"]
    splits = [e for e in evs if e["type"] in ("SPLIT","MERGE","CONVERSION")]
    buys = [e for e in trades if e["side"]=="BUY"]
    sells= [e for e in trades if e["side"]=="SELL"]
    # last-month subset
    tm = [e for e in trades if e["timestamp"]>=month_ago]
    rm = [e for e in redeems if e["timestamp"]>=month_ago]

    # category mix by USD traded (last 30d)
    catusd = defaultdict(float); catcnt=defaultdict(int)
    for e in tm:
        c = categorize(e.get("title",""), e.get("slug",""), e.get("eventSlug",""))
        catusd[c]+=e.get("usdcSize",0); catcnt[c]+=1
    # entry price buckets (BUY only, last 30d)
    buckets = {"longshot<.15":0,".15-.40":0,"coinflip .40-.60":0,".60-.85":0,"fav .85-.97":0,"chalk>.97":0}
    busd = {k:0.0 for k in buckets}
    for e in tm:
        if e["side"]!="BUY": continue
        p=e.get("price",0); u=e.get("usdcSize",0)
        if   p<0.15: k="longshot<.15"
        elif p<0.40: k=".15-.40"
        elif p<0.60: k="coinflip .40-.60"
        elif p<0.85: k=".60-.85"
        elif p<0.97: k="fav .85-.97"
        else:        k="chalk>.97"
        buckets[k]+=1; busd[k]+=u
    distinct_mkts = len(set(e["conditionId"] for e in tm))
    sizes = sorted(e.get("usdcSize",0) for e in tm)
    med = sizes[len(sizes)//2] if sizes else 0
    avg = sum(sizes)/len(sizes) if sizes else 0
    vol30 = sum(e.get("usdcSize",0) for e in tm)
    # open positions snapshot
    open_val = sum(p.get("currentValue",0) for p in pos)
    open_pnl = sum(p.get("cashPnl",0) for p in pos)
    top_pos = sorted(pos, key=lambda p:p.get("currentValue",0), reverse=True)[:8]
    negrisk = sum(1 for p in pos if p.get("negativeRisk"))

    return dict(name=name, wallet=a,
        n_trades_all=len(trades), n_redeem_all=len(redeems), n_split=len(splits),
        n_trades_30d=len(tm), n_redeem_30d=len(rm), distinct_mkts_30d=distinct_mkts,
        buy30=sum(1 for e in tm if e["side"]=="BUY"), sell30=sum(1 for e in tm if e["side"]=="SELL"),
        vol30=vol30, med_trade=med, avg_trade=avg,
        catusd=dict(sorted(catusd.items(), key=lambda x:-x[1])),
        price_buckets_cnt=buckets, price_buckets_usd={k:round(v) for k,v in busd.items()},
        open_positions=len(pos), open_value=open_val, open_unreal_pnl=open_pnl, negrisk_pos=negrisk,
        top_positions=[dict(title=p.get("title"), outcome=p.get("outcome"),
                            px=p.get("avgPrice"), cur=p.get("curPrice"),
                            val=round(p.get("currentValue",0)), pnl=round(p.get("cashPnl",0)),
                            end=p.get("endDate")) for p in top_pos])

def main():
    res = [profile(a) for a in sys.argv[1:]]
    json.dump(res, open("deep.json","w"), indent=1)
    for r in res:
        print("\n"+"="*100)
        print(f"{r['name']}  ({r['wallet']})")
        print("-"*100)
        print(f"  trades(all)={r['n_trades_all']:,}  redeems(all)={r['n_redeem_all']:,}  split/merge={r['n_split']:,}")
        print(f"  LAST 30D: trades={r['n_trades_30d']:,}  buy/sell={r['buy30']}/{r['sell30']}  redeems={r['n_redeem_30d']}  distinct_markets={r['distinct_mkts_30d']:,}")
        print(f"            volume30d=${r['vol30']:,.0f}  median_trade=${r['med_trade']:,.0f}  avg_trade=${r['avg_trade']:,.0f}")
        tot=sum(r['catusd'].values()) or 1
        print(f"  CATEGORY MIX (30d $ traded):")
        for c,v in list(r['catusd'].items())[:8]:
            print(f"      {c:<22} ${v:>12,.0f}  ({v/tot*100:4.1f}%)")
        print(f"  ENTRY PRICE (BUY count / $ , 30d):")
        for k in r['price_buckets_cnt']:
            print(f"      {k:<18} n={r['price_buckets_cnt'][k]:>5}   ${r['price_buckets_usd'][k]:>12,}")
        print(f"  OPEN: {r['open_positions']} positions  value=${r['open_value']:,.0f}  unreal_pnl=${r['open_unreal_pnl']:,.0f}  negRisk_pos={r['negrisk_pos']}")
        print(f"  TOP OPEN POSITIONS:")
        for p in r['top_positions']:
            print(f"      ${p['val']:>9,}  pnl=${p['pnl']:>9,}  @{p['px']:.3f}->{(p['cur'] or 0):.3f}  {(p['outcome'] or '')[:6]:<6} {str(p['title'])[:52]}  ({p['end']})")

if __name__=="__main__":
    main()
