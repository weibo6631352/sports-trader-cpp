#!/usr/bin/env python3
# BTC 预测套利可行性 (老雷 2026-06-09, 隔离不碰生产)。
# 思路: Kalshi BTC 日盘 P(BTC>K@T) 的盘口价 vs 现货+波动率算的理论公允概率 → 看是否系统性偏离(=可套)。
# 公允: 风险中性下 P(S_T>K)=N(d2), d2=(ln(S/K)-σ²T/2)/(σ√T) (r≈0 短周期)。σ 用 Deribit DVOL(BTC 隐含波动指数)。
import json, urllib.request, math, datetime, sys
def get(u, to=15):
    try: return json.load(urllib.request.urlopen(urllib.request.Request(u,headers={'User-Agent':'Mozilla/5.0'}),timeout=to))
    except Exception as e: return {'__err':str(e)}
def N(x): return 0.5*(1+math.erf(x/math.sqrt(2)))

# 1) BTC 现货
spot=None
for u in ["https://api.coinbase.com/v2/prices/BTC-USD/spot"]:
    d=get(u)
    try: spot=float(d['data']['amount'])
    except: pass
print("BTC spot:",spot)

# 2) BTC 隐含波动率 (Deribit DVOL index, 年化%)
dv=get("https://www.deribit.com/api/v2/public/get_volatility_index_data?currency=BTC&start_timestamp=0&end_timestamp=9999999999999&resolution=3600")
sigma=None
try:
    data=dv['result']['data']
    sigma=float(data[-1][4])/100.0  # 最新 close DVOL, 转小数
except Exception as e:
    sigma=0.50; print("DVOL 取失败, 用 0.50 兜底:",dv.get('__err',e))
print("BTC 隐含波动率(年化):",sigma)

now=datetime.datetime.now(datetime.timezone.utc)
# 3) Kalshi BTC 日盘 (T=above strike) — 取未来结算 + 近钱档
mk=[]; cursor=""
for _ in range(6):
    u="https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXBTCD&status=open&limit=200"
    if cursor:u+="&cursor="+cursor
    d=get(u)
    mk+=d.get('markets',[]); cursor=d.get('cursor','')
    if not cursor:break
rows=[]
for m in mk:
    t=m.get('ticker','')
    if '-T' not in t: continue
    try: K=float(t.split('-T')[1])
    except: continue
    ya=m.get('yes_ask_dollars'); yb=m.get('yes_bid_dollars')
    if ya is None or yb is None: continue
    ya=float(ya); yb=float(yb)
    ct=m.get('close_time','')
    try: T=(datetime.datetime.fromisoformat(ct.replace('Z','+00:00'))-now).total_seconds()/(365.25*24*3600)
    except: continue
    if T<=0: continue  # 已过结算
    mid=(ya+yb)/2
    # 公允 P(BTC>K)
    if spot and sigma and T>0:
        d2=(math.log(spot/K)-0.5*sigma*sigma*T)/(sigma*math.sqrt(T))
        fair=N(d2)
    else: fair=None
    rows.append((K,yb,ya,mid,fair,ct[:16],T*365.25*24))  # 末位=小时
rows.sort()
print("\n=== Kalshi BTC 日盘 盘口 vs 理论公允 (按 strike) ===")
print(f"{'strike':>9} {'盘口bid/ask':>12} {'mid':>5} {'公允':>5} {'偏离':>6} {'剩h':>5}  settle")
big=0
for K,yb,ya,mid,fair,ct,hrs in rows:
    if fair is None: continue
    if not (0.03<mid<0.97 or 0.03<fair<0.97): continue
    dev=mid-fair
    flag=' <<偏离大' if abs(dev)>=0.05 and ya<0.97 and yb>0.0 else ''
    if abs(dev)>=0.05: big+=1
    print(f"{K:>9.0f} {yb:>5.2f}/{ya:>4.2f} {mid:>6.2f} {fair:>5.2f} {dev:>+6.2f} {hrs:>5.1f}  {ct}{flag}")
print(f"\n|偏离|>=5c 的档: {big}  (偏离大且双边有量=潜在套利; 偏离≈0=盘口已对=无套利)")
print("注: 公允用 DVOL 隐含波动率 + 对数正态; Kalshi 结算源可能与 Deribit 指数略不同(基差); 这是可行性筛, 非成交信号。")
