#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/eval_by_market_type.py
# owner: 老雷 | 2026-06-15 | 非生产·按盘口类型评估 +EV (CLAUDE.md 允许离线脚本)
#
# 老板 2026-06-15 +$300 自主授权: 放开了 totals/spreads 覆盖。核心问题 = 这些新盘口到底 +EV 还是 -EV?
#   param_research 不按盘口类型分。本脚本专评: moneyline vs totals vs spreads 各自的
#     ① 真实成交盘 (fills, 真金真结局) realized PnL / 胜率 / 单股EV / 费
#     ② 反事实 (被挡盘+观察盘 join 结算) would-PnL —— 样本补充 (有逆选偏差, 当线索)
#   迭代闭环用: 每次评估前先 backfill_settlements, 再跑本脚本看新盘口是否在赚。
#
# 用法: python3 eval_by_market_type.py <fills.jsonl> <settlements_merged.jsonl> [gate_blocks.jsonl]
import sys, json, math, collections

def jl(p):
    out=[]
    try:
        for l in open(p):
            l=l.strip()
            if l:
                try: out.append(json.loads(l))
                except: pass
    except FileNotFoundError: pass
    return out

def wilson(k,n,z=1.96):
    if n==0: return (0.0,0.0)
    p=k/n; d=1+z*z/n; c=p+z*z/(2*n); m=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))
    return (max(0.0,(c-m)/d), min(1.0,(c+m)/d))

def main():
    a=[x for x in sys.argv[1:] if not x.startswith("--")]
    if len(a)<2:
        print("用法: eval_by_market_type.py <fills.jsonl> <settlements_merged.jsonl> [gate_blocks.jsonl]"); return
    fills=jl(a[0])
    st={}
    for d in jl(a[1]):
        if d.get("parse_ok")!=0 and d.get("settlement_value",-1)!=-1:
            st[d["condition_id"]]=int(d["settlement_value"])
    gb=jl(a[2]) if len(a)>2 else []
    print(f"fills={len(fills)} settlements(usable)={len(st)} gate_blocks={len(gb)}")

    # ---- ① 真实成交: 按 mkt 分组 (买入开仓 + 对应平仓 realized) ----
    buys=[r for r in fills if r.get("buy")==1 and r.get("close")!=1 and r.get("type")!="version"]
    closes=[r for r in fills if r.get("close")==1]
    print("\n=== ① 真实成交表现 (按盘口类型; realized=平仓+结算真实) ===")
    print(f"{'盘口':<12}{'开仓':>5}{'平仓':>5}{'realized':>10}{'fee':>8}{'净':>9}")
    by_mkt_buys=collections.defaultdict(list); by_mkt_closes=collections.defaultdict(list)
    for r in buys: by_mkt_buys[r.get("mkt","?")].append(r)
    for r in closes: by_mkt_closes[r.get("mkt","?")].append(r)
    allmkts=set(by_mkt_buys)|set(by_mkt_closes)
    for m in sorted(allmkts, key=lambda x:-len(by_mkt_buys.get(x,[]))):
        nb=len(by_mkt_buys.get(m,[])); nc=len(by_mkt_closes.get(m,[]))
        real=sum(r.get("realized",0) for r in by_mkt_closes.get(m,[]))
        fee=sum(r.get("fee",0) for r in by_mkt_buys.get(m,[])+by_mkt_closes.get(m,[]) if isinstance(r.get("fee"),(int,float)))
        print(f"{m:<12}{nb:>5}{nc:>5}{real:>+10.3f}{fee:>8.3f}{real-fee:>+9.3f}")

    # ---- ② 反事实 would-PnL (持有到结算口径): 真实成交盘也算, 补样本 ----
    # 每盘 (cond,mkt,side) 用 px + 结算: won = (yes==sv); pnl/share = won - px
    print("\n=== ② 反事实/持有到结算 per-share EV (真实成交盘, 已结算; 按盘口) ===")
    print(f"{'盘口':<12}{'n':>5}{'胜率':>8}{'Wilson':>16}{'EV/股':>9}{'总':>9}")
    rows=collections.defaultdict(list)
    for r in buys:
        c=r.get("cond")
        if c not in st: continue
        yes=r.get("yes"); px=r.get("px")
        if yes not in (0,1) or not isinstance(px,(int,float)): continue
        won=1 if int(yes)==st[c] else 0
        rows[r.get("mkt","?")].append((won, won-px))
    for m in sorted(rows, key=lambda x:-len(rows[x])):
        rs=rows[m]; n=len(rs); k=sum(w for w,_ in rs)
        ev=sum(p for _,p in rs)/n; lo,hi=wilson(k,n)
        print(f"{m:<12}{n:>5}{k/n:>7.0%}{f'[{lo:.0%},{hi:.0%}]':>16}{ev:>+9.3f}{sum(p for _,p in rs):>+9.2f}")
    if not rows:
        print("  (尚无已结算的真实成交盘可评 — totals/spreads 需等比赛结算)")

if __name__=="__main__":
    main()
