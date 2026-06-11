#!/usr/bin/env python3
# BTC 预测套利 — 前向高频记录器 (老雷 2026-06-09, 隔离不碰生产)。
# 目的: 测唯一可能的 edge —— Kalshi 是否短暂滞后 BTC 快速移动(瞬时错价, 可抓后均值回归)。
# 每 N 秒记: BTC 现货(Deribit index) + Kalshi 近结算盘各 strike bid/ask/mid + 用即时实现vol算的公允 + 偏离。
# 跑完离线分析: ① Δspot 与 Δmid 的滞后(互相关) ② 偏离分布 vs (费+价差) ③ settle 时真实结果对账。
import json, urllib.request, math, time, datetime, statistics, sys, os
def get(u, to=8):
    try: return json.load(urllib.request.urlopen(urllib.request.Request(u,headers={'User-Agent':'Mozilla/5.0'}),timeout=to))
    except Exception: return None
def N(x): return 0.5*(1+math.erf(x/math.sqrt(2)))
YEAR=365.25*24*3600
DUR=float(sys.argv[1]) if len(sys.argv)>1 else 1200   # 默认 20min
STEP=float(sys.argv[2]) if len(sys.argv)>2 else 3.0
OUT=os.path.join(os.path.dirname(os.path.abspath(__file__)),"btc_log.csv")

def rvol():
    end=int(time.time()*1000); start=end-40*60*1000
    d=get(f'https://www.deribit.com/api/v2/public/get_tradingview_chart_data?instrument_name=BTC-PERPETUAL&resolution=1&start_timestamp={start}&end_timestamp={end}')
    c=(d or {}).get('result',{}).get('close',[])
    if len(c)<10: return None
    rets=[math.log(c[i]/c[i-1]) for i in range(1,len(c)) if c[i-1]>0]
    return statistics.pstdev(rets)*math.sqrt(365.25*24*60)

f=open(OUT,"w")
f.write("iso,epoch,ticker,settle,mins_left,strike,kbid,kask,kmid,spot,rvol,fair,dev\n")
t0=time.time(); nloop=0; rv=rvol() or 0.40; last_rv=t0
print(f"记录开始 → {OUT}  时长{DUR/60:.0f}min 步长{STEP}s  起始rvol={rv*100:.1f}%", file=sys.stderr)
while time.time()-t0 < DUR:
    loop_start=time.time()
    if loop_start-last_rv>120:   # 每 2min 刷新一次 realized vol
        nv=rvol()
        if nv: rv=nv
        last_rv=loop_start
    idx=get("https://www.deribit.com/api/v2/public/get_index_price?index_name=btc_usd")
    S=None
    try: S=float(idx['result']['index_price'])
    except: pass
    mk=[]; cursor=""
    for _ in range(4):
        u="https://api.elections.kalshi.com/trade-api/v2/markets?series_ticker=KXBTCD&status=open&limit=200"
        if cursor:u+="&cursor="+cursor
        d=get(u)
        if not d: break
        mk+=d.get('markets',[]); cursor=d.get('cursor','')
        if not cursor: break
    nowdt=datetime.datetime.now(datetime.timezone.utc); iso=nowdt.isoformat()[:19]; ep=time.time()
    if S:
        for m in mk:
            t=m.get('ticker','')
            if '-T' not in t: continue
            try: K=float(t.split('-T')[1])
            except: continue
            ya=m.get('yes_ask_dollars'); yb=m.get('yes_bid_dollars')
            if ya is None or yb is None: continue
            ya,yb=float(ya),float(yb); mid=(ya+yb)/2
            ct=m.get('close_time','')
            try: Tk=datetime.datetime.fromisoformat(ct.replace('Z','+00:00'))
            except: continue
            Th=(Tk-nowdt).total_seconds()
            if Th<=0: continue
            Ty=Th/YEAR
            if abs(K-S)>3000: continue   # 只记近钱档(±3000)省体积
            d2=(math.log(S/K)-0.5*rv*rv*Ty)/(rv*math.sqrt(Ty)) if Ty>0 else 0
            fair=N(d2)
            f.write(f"{iso},{ep:.1f},{t},{ct[:16]},{Th/60:.1f},{K:.0f},{yb:.3f},{ya:.3f},{mid:.3f},{S:.1f},{rv:.4f},{fair:.4f},{mid-fair:+.4f}\n")
    f.flush(); nloop+=1
    dt=STEP-(time.time()-loop_start)
    if dt>0: time.sleep(dt)
print(f"记录结束: {nloop} 轮, 文件 {OUT}", file=sys.stderr)
f.close()
