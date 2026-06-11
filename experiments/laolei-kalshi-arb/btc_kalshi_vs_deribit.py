#!/usr/bin/env python3
# BTC 预测套利对比器 (老雷 2026-06-09, 隔离不碰生产)。
# Kalshi BTC 日盘 P(BTC>K) 盘口 vs Deribit 期权隐含 P(BTC>K) (同 strike/到期)。
# 期权 P(>K) = -dC/dK (相邻 strike call 价差; Deribit 反向期权 USD payoff = max(S-K,0) 与标准 call 同)。
# 输出: 同 strike 两边概率差 - Kalshi 费 - 期权对冲费 = 净 edge → GO/NO-GO 硬数据。
import json, urllib.request, datetime, sys, bisect
def get(u, to=20):
    try: return json.load(urllib.request.urlopen(urllib.request.Request(u,headers={'User-Agent':'Mozilla/5.0'}),timeout=to))
    except Exception as e: return {'__err':str(e)}

now = datetime.datetime.now(datetime.timezone.utc)
KALSHI_FEE = lambda p: 0.07*p*(1-p)        # Kalshi 交易费/合约 (近似 0.07·p·(1-p))
DERIBIT_FEE = 0.0003                         # 期权对冲费 ≈ 0.0003 BTC/张 ≈ 0.03% underlying (USD 占比)

# ---- 1) Deribit BTC 现货 index + 期权 book summary (mark in BTC) ----
idx = get("https://www.deribit.com/api/v2/public/get_index_price?index_name=btc_usd")
S = None
try: S = float(idx['result']['index_price'])
except: pass
print("Deribit BTC index:", S, file=sys.stderr)
bs = get("https://www.deribit.com/api/v2/public/get_book_summary_by_currency?currency=BTC&kind=option")
opts = bs.get('result', [])
# 按到期分组 calls: {expiry_str: {strike: call_usd}}
calls = {}
exp_dt = {}
for o in opts:
    name = o.get('instrument_name','')   # BTC-12JUN26-64000-C
    parts = name.split('-')
    if len(parts)!=4 or parts[3]!='C': continue
    try:
        K = float(parts[2]); mark = o.get('mark_price')
        if mark is None: continue
        c_usd = float(mark)*S   # 反向期权 USD payoff = max(S-K,0) → C_usd = mark_BTC × index
    except: continue
    e = parts[1]
    calls.setdefault(e, {})[K] = c_usd
    if e not in exp_dt:
        try: exp_dt[e] = datetime.datetime.strptime(e, "%d%b%y").replace(hour=8, tzinfo=datetime.timezone.utc)
        except: pass
print("Deribit call 到期数:", len(calls), "| 样本:", list(calls.keys())[:6], file=sys.stderr)

def deribit_P_gt(expiry, K):
    """期权隐含 P(S_T>K), 用相邻 strike call 价差 -dC/dK, 线性插值到 K。"""
    cc = calls.get(expiry, {})
    ks = sorted(cc)
    if len(ks) < 2: return None
    # 构造 P(>K) 在每个相邻中点
    pts = []
    for a, b in zip(ks, ks[1:]):
        if b <= a: continue
        p = (cc[a]-cc[b])/(b-a)
        pts.append(((a+b)/2, max(0.0, min(1.0, p))))
    if not pts: return None
    xs = [x for x,_ in pts]; ys = [y for _,y in pts]
    if K <= xs[0]: return ys[0]
    if K >= xs[-1]: return ys[-1]
    i = bisect.bisect_left(xs, K)
    x0,x1,y0,y1 = xs[i-1],xs[i],ys[i-1],ys[i]
    return y0 + (y1-y0)*(K-x0)/(x1-x0)

# ---- 2) Kalshi BTC 日盘 ----
mk=[]; cursor=""
for _ in range(6):
    u="https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXBTCD&status=open&limit=200"
    if cursor:u+="&cursor="+cursor
    d=get(u); mk+=d.get('markets',[]); cursor=d.get('cursor','')
    if not cursor:break
krows=[]
for m in mk:
    t=m.get('ticker','')
    if '-T' not in t: continue
    try: K=float(t.split('-T')[1])
    except: continue
    ya=m.get('yes_ask_dollars'); yb=m.get('yes_bid_dollars')
    if ya is None or yb is None: continue
    ya,yb=float(ya),float(yb)
    ct=m.get('close_time','')
    try: kdt=datetime.datetime.fromisoformat(ct.replace('Z','+00:00'))
    except: continue
    if kdt<=now: continue
    krows.append((kdt,K,yb,ya,(ya+yb)/2))

# ---- 3) 匹配 (Kalshi settle → 最近 Deribit expiry) + 比 P + 净 edge ----
print(f"\n=== Kalshi BTC P(>K) vs Deribit 期权隐含 P(>K) ===")
print(f"{'strike':>8} {'Kalshi':>6} {'Deribit':>7} {'差':>6} {'扣费净':>7} {'Δh(基差)':>8}  K_settle")
exps = sorted(exp_dt.items(), key=lambda kv: kv[1])
results=[]
for kdt,K,yb,ya,kmid in sorted(krows, key=lambda r:(r[0],r[1])):
    # 最近 Deribit 到期
    best=min(exps, key=lambda e: abs((e[1]-kdt).total_seconds())) if exps else None
    if not best: continue
    dP=deribit_P_gt(best[0], K)
    if dP is None: continue
    if not (0.04<kmid<0.96 or 0.04<dP<0.96): continue
    diff=kmid-dP
    # 净 edge: |差| - Kalshi费 - 期权对冲费 (粗略)
    net=abs(diff)-KALSHI_FEE(kmid)-DERIBIT_FEE
    dh=(kdt-best[1]).total_seconds()/3600
    flag=' <<净>0' if net>0.02 and abs(dh)<8 else ''
    results.append((net,K,kmid,dP,diff,dh,flag))
results.sort(reverse=True)
for net,K,kmid,dP,diff,dh,flag in results[:24]:
    print(f"{K:>8.0f} {kmid:>6.2f} {dP:>7.2f} {diff:>+6.2f} {net:>+7.3f} {dh:>+8.1f}{flag}")
pos=[r for r in results if r[0]>0.02 and abs(r[5])<8]
print(f"\n扣费后净 edge>2c 且基差<8h 的档: {len(pos)}  (有=潜在可套; 0=两边一致/被费吃掉=无套利)")
print("⚠ 基差(Δh): Kalshi 结算时刻 vs Deribit 到期(08:00UTC)差; |Δh|大=不是同一时刻的BTC=假信号, 须近0才算。")
