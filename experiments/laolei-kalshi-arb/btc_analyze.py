#!/usr/bin/env python3
# 分析 btc_log.csv (老雷 2026-06-09)。回答三问决定盈利能力:
#  ① 滞后: Kalshi mid 是否滞后 BTC 现货? (互相关求 lag) —— 有滞后=瞬时错价可抓=速度 edge 真实。
#  ② 偏离分布: |Kalshi-公允| 超过 (Kalshi费+价差) 的频率/幅度 —— 这才是扣成本后能抓的。
#  ③ 结算对账: 近结算盘最后现货落点 vs 各 strike → "买便宜侧" 真实盈亏。
import csv, math, statistics, collections, os, sys
F=os.path.join(os.path.dirname(os.path.abspath(__file__)),"btc_log.csv")
rows=list(csv.DictReader(open(F)))
print(f"样本 {len(rows)} 行")
if len(rows)<20: print("数据太少, 等记录器多跑会"); sys.exit()
for r in rows:
    for k in ('epoch','mins_left','strike','kbid','kask','kmid','spot','rvol','fair','dev'):
        r[k]=float(r[k])

# 选最近结算的那张盘(mins_left 最小的 ticker), 取它的 ATM strike 时间序列
near_ticker=min(rows,key=lambda r:r['mins_left'])['ticker']
sub=[r for r in rows if r['ticker']==near_ticker]
# ATM strike = 出现最多且最接近现货的
strikes=collections.Counter(r['strike'] for r in sub)
spot0=statistics.median(r['spot'] for r in sub)
atmK=min(strikes, key=lambda K:abs(K-spot0))
ser=sorted([r for r in sub if r['strike']==atmK], key=lambda r:r['epoch'])
print(f"\n近结算盘 {near_ticker}  ATM strike={atmK:.0f}  序列点={len(ser)}  现货≈{spot0:.0f}")

# ① 滞后: Δspot vs Δmid 互相关 (lag 单位=采样步)
if len(ser)>15:
    ds=[ser[i]['spot']-ser[i-1]['spot'] for i in range(1,len(ser))]
    dm=[ser[i]['kmid']-ser[i-1]['kmid'] for i in range(1,len(ser))]
    def corr(a,b):
        if len(a)<3: return 0
        ma,mb=statistics.mean(a),statistics.mean(b)
        na=math.sqrt(sum((x-ma)**2 for x in a)); nb=math.sqrt(sum((x-mb)**2 for x in b))
        if na*nb==0: return 0
        return sum((a[i]-ma)*(b[i]-mb) for i in range(len(a)))/(na*nb)
    print("  Δspot↔Δmid 互相关 (lag=Kalshi 落后现货的采样步):")
    for lag in range(0,5):
        if lag==0: c=corr(ds,dm)
        else: c=corr(ds[:-lag], dm[lag:])
        print(f"    lag={lag}步 ({lag*3:.0f}s): corr={c:+.2f}")
    print("    → 若 lag≥1 的 corr 明显>lag0, 说明 Kalshi 滞后现货 → 现货先动可预测 Kalshi → 可抓; 若 lag0 最高=同步无 edge。")

# ② 偏离分布 vs 成本 (按 mins_left 分桶)
print("\n② |偏离| vs 成本 (Kalshi费0.07p(1-p)+半价差):")
for lo,hi,name in [(0,20,'<20min'),(20,180,'20-180min'),(180,9999,'>3h')]:
    seg=[r for r in rows if lo<=r['mins_left']<hi and 0.03<r['kmid']<0.97]
    if not seg: continue
    net=[abs(r['dev'])-0.07*r['kmid']*(1-r['kmid'])-(r['kask']-r['kbid'])/2 for r in seg]
    posfrac=sum(1 for x in net if x>0.01)/len(net)
    print(f"  {name:>10}: n={len(seg):>5}  |dev|中位={statistics.median(abs(r['dev']) for r in seg):.3f}  "
          f"扣费净>1c占比={posfrac*100:.1f}%  净edge中位={statistics.median(net):+.3f}")

# ③ 结算对账: 近结算盘最后一帧 spot vs 各 strike → 买便宜侧盈亏(假设按 fair 判便宜)
last_ep=max(r['epoch'] for r in sub)
final=[r for r in sub if r['epoch']==last_ep]
fs=final[0]['spot']
print(f"\n③ 结算对账 (近结算盘最后一帧 spot={fs:.0f}, settle={final[0]['settle']}):")
wins=0; tot=0; pnl=0.0
for r in sorted(final,key=lambda r:r['strike']):
    if not (0.05<r['kmid']<0.95): continue
    outcome=1.0 if fs>r['strike'] else 0.0          # YES 是否结算为真
    # 买便宜侧: fair>kmid → YES 被低估 → 买YES@kask; fair<kmid → 买NO@(1-kbid)
    if r['fair']>r['kmid']:
        cost=r['kask']; ret=outcome-cost; side='Y'
    else:
        cost=1-r['kbid']; ret=(1-outcome)-cost; side='N'
    fee=0.07*r['kmid']*(1-r['kmid'])
    ret-=fee
    pnl+=ret; tot+=1; wins+= (ret>0)
    if abs(r['dev'])>0.04:
        print(f"  K={r['strike']:.0f} kmid={r['kmid']:.2f} fair={r['fair']:.2f} dev={r['dev']:+.2f} 买{side} 结果={'win' if outcome>0.5 else 'lose'}YES pnl={ret:+.3f}")
if tot: print(f"  → 买便宜侧 {tot} 档: 胜{wins} 净PnL/档={pnl/tot:+.3f} (仅1次结算样本, 须多周期)")
print("\n注: 单次结算不算数, 须跑多个 settle 周期累计; 这是方法学验证 + 首样本。")
