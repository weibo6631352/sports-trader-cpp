#!/usr/bin/env python3
# BTC 预测套利对比器 v2 (老雷 2026-06-09, 隔离不碰生产) —— 修 v1 的到期错配。
# 核心修正: Kalshi 盘的公允概率必须算在【Kalshi 自己的结算时刻 T_k】上, 不是某个 Deribit 到期上。
#   fair P(BTC>K @ T_k) = N(d2), d2=(ln(F/K)-½σ²T)/(σ√T), T=(T_k-now);
#   σ 取 Deribit 该 strike 的 mark_iv (smile-aware, 用最近到期的微笑曲线), F≈spot (r≈0 短周期)。
# 这样 22 分钟后结算的盘就用 22 分钟的分布算公允 → 和 Kalshi 的极端价对齐才说明"有效", 偏离才是潜在 edge。
# 输出: 每档 Kalshi价 / 期权公允 / 偏离 / 扣费净edge → 这一刻是否有可套空间。
import json, urllib.request, math, datetime, sys, bisect
def get(u, to=20):
    try: return json.load(urllib.request.urlopen(urllib.request.Request(u,headers={'User-Agent':'Mozilla/5.0'}),timeout=to))
    except Exception as e: return {'__err':str(e)}
def N(x): return 0.5*(1+math.erf(x/math.sqrt(2)))
YEAR = 365.25*24*3600
now = datetime.datetime.now(datetime.timezone.utc)

# ---- Deribit: spot + 每个近到期的 mark_iv 微笑 ----
S = float(get("https://www.deribit.com/api/v2/public/get_index_price?index_name=btc_usd")['result']['index_price'])
bs = get("https://www.deribit.com/api/v2/public/get_book_summary_by_currency?currency=BTC&kind=option").get('result',[])
# {expiry_dt: {strike: mark_iv(小数)}}  只用有 iv 的
smile = {}
for o in bs:
    p = o.get('instrument_name','').split('-')
    if len(p)!=4: continue
    iv = o.get('mark_iv')
    if iv is None: continue
    try:
        e = datetime.datetime.strptime(p[1],"%d%b%y").replace(hour=8,tzinfo=datetime.timezone.utc)
        K = float(p[2])
    except: continue
    if e <= now: continue
    smile.setdefault(e,{}).setdefault(K,[]).append(float(iv)/100.0)
# 每 expiry/strike 取 call+put iv 均值
for e in smile:
    for K in smile[e]: smile[e][K] = sum(smile[e][K])/len(smile[e][K])
exps = sorted(smile)
exp_yr = {e:(e-now).total_seconds()/YEAR for e in exps}   # 每到期距今(年)
print(f"Deribit spot={S:.0f}  近到期: " + ", ".join(f"{e.strftime('%d%b %Hh')}({(e-now).total_seconds()/3600:.0f}h)" for e in exps[:5]), file=sys.stderr)

def iv_strike(e, K):
    """到期 e 的微笑曲线在 strike K 的 iv (线性插值)。"""
    sm = smile[e]; ks = sorted(sm)
    if not ks: return None
    if K<=ks[0]: return sm[ks[0]]
    if K>=ks[-1]: return sm[ks[-1]]
    i = bisect.bisect_left(ks,K); k0,k1=ks[i-1],ks[i]
    return sm[k0]+(sm[k1]-sm[k0])*(K-k0)/(k1-k0)

def iv_at(K, T_k):
    """该 strike 在 horizon T_k(年) 的隐含波动率 —— 期限结构感知: 在总方差(σ²·t)上按时间插值。"""
    if T_k<=0: return None
    # T_k 短于最短到期 → 用最短到期 iv (平推, 18min<21h 时已验证 realized≈IV 成立)
    if T_k <= exp_yr[exps[0]]: return iv_strike(exps[0], K)
    if T_k >= exp_yr[exps[-1]]: return iv_strike(exps[-1], K)
    # 找包夹的两个到期, 总方差线性插值
    for lo,hi in zip(exps, exps[1:]):
        t_lo,t_hi = exp_yr[lo],exp_yr[hi]
        if t_lo <= T_k <= t_hi:
            iv_lo,iv_hi = iv_strike(lo,K),iv_strike(hi,K)
            if iv_lo is None or iv_hi is None: return iv_lo or iv_hi
            var_lo,var_hi = iv_lo*iv_lo*t_lo, iv_hi*iv_hi*t_hi
            w=(T_k-t_lo)/(t_hi-t_lo)
            var=var_lo+(var_hi-var_lo)*w
            return math.sqrt(var/T_k)
    return iv_strike(exps[0], K)

def fair_P_gt(K, T_k_years):
    sig = iv_at(K, T_k_years)
    if not sig or T_k_years<=0: return None
    d2 = (math.log(S/K) - 0.5*sig*sig*T_k_years)/(sig*math.sqrt(T_k_years))
    return N(d2), sig

# ---- Kalshi BTC 日盘 ----
mk=[]; cursor=""
for _ in range(6):
    u="https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXBTCD&status=open&limit=200"
    if cursor:u+="&cursor="+cursor
    d=get(u); mk+=d.get('markets',[]); cursor=d.get('cursor','')
    if not cursor:break
KFEE=lambda p: 0.07*p*(1-p)   # Kalshi 交易费/合约
rows=[]
for m in mk:
    t=m.get('ticker','')
    if '-T' not in t: continue
    try: K=float(t.split('-T')[1])
    except: continue
    ya=m.get('yes_ask_dollars'); yb=m.get('yes_bid_dollars')
    if ya is None or yb is None: continue
    ya,yb=float(ya),float(yb)
    ct=m.get('close_time','')
    try: T_k=datetime.datetime.fromisoformat(ct.replace('Z','+00:00'))
    except: continue
    Th=(T_k-now).total_seconds()/3600
    if Th<=0: continue
    fp=fair_P_gt(K,(T_k-now).total_seconds()/YEAR)
    if fp is None: continue
    fair,sig=fp
    mid=(ya+yb)/2
    rows.append((K,yb,ya,mid,fair,sig,Th,ct[:16]))
rows.sort()
print(f"\n=== Kalshi BTC vs 期权公允(同 horizon) | now={now.strftime('%H:%M')}UTC | spot={S:.0f} ===")
print(f"{'strike':>8} {'bid/ask':>11} {'Kmid':>5} {'公允':>5} {'σ%':>5} {'偏离':>6} {'净edge':>7} {'剩min':>6}")
cand=0
for K,yb,ya,mid,fair,sig,Th,ct in rows:
    if not (0.02<mid<0.98 or 0.02<fair<0.98): continue
    dev=mid-fair
    # 可成交方向: Kalshi 贵→卖(无券则跳)/便宜→买; 净= |dev| - Kalshi费 - 假设1c期权对冲滑点
    net=abs(dev)-KFEE(mid)-0.01
    flag=''
    if net>0.01 and 0.0<ya<1.0 and yb>0.0: flag=' <<净>1c'; cand+=1
    print(f"{K:>8.0f} {yb:>5.2f}/{ya:>4.2f} {mid:>6.2f} {fair:>5.2f} {sig*100:>5.1f} {dev:>+6.2f} {net:>+7.3f} {Th*60:>6.1f}{flag}")
print(f"\n扣费扣对冲后净>1c 的档: {cand}  (用 Kalshi 自身 horizon 算公允; 偏离≈0=有效无套利; 偏离大且双边有量=潜在)")
print("⚠ 仍非干净套利: Kalshi 11:00 结算 vs Deribit 期权 08:00 到期 = 不同结算时刻 → 对冲有基差; σ 借最近到期 21h 的 iv (无更短到期)。")
