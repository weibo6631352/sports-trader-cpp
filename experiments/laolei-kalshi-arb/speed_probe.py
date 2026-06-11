#!/usr/bin/env python3
# 网速 edge 高频探针 (老雷 2026-06-09) — 测 Kalshi 近结算ATM报价是否滞后实时现货。
# 高gamma(8min到结算ATM): 现货动$20→公允P动~10c; 报价滞后1s→可抓10c。
# 尽可能快地交替读 Deribit现货 + Kalshi ATM book, ms时间戳, 跑到结算前。
import json,urllib.request,time,sys
TICKER=sys.argv[1] if len(sys.argv)>1 else "KXBTCD-26JUN0908-T62599.99"
DUR=float(sys.argv[2]) if len(sys.argv)>2 else 420
OUT="speed_log.csv"
def get(u,to=4):
    try: return json.load(urllib.request.urlopen(urllib.request.Request(u,headers={'User-Agent':'Mozilla/5.0'}),timeout=to))
    except: return None
f=open(OUT,"w"); f.write("t_spot_ms,spot,t_kal_ms,kbid,kask\n")
t0=time.time(); n=0
while time.time()-t0<DUR:
    ts=time.time()*1000
    idx=get("https://www.deribit.com/api/v2/public/get_index_price?index_name=btc_usd")
    S=None
    try: S=float(idx['result']['index_price'])
    except: pass
    tk=time.time()*1000
    km=get(f"https://api.elections.kalshi.com/trade-api/v2/markets/{TICKER}")
    kb=ka=None
    try:
        m=km['market']; kb=float(m['yes_bid_dollars']); ka=float(m['yes_ask_dollars'])
    except: pass
    if S is not None:
        f.write(f"{ts:.0f},{S:.1f},{tk:.0f},{kb},{ka}\n"); f.flush(); n+=1
    time.sleep(0.12)
print(f"探针结束 {n} 帧 → {OUT}",file=sys.stderr)
f.close()
