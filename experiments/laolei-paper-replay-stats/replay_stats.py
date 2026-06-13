#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/replay_stats.py
# owner: 老雷 | 2026-06-14 | 非生产离线分析 (CLAUDE.md 允许: Jupyter/一次性数据探索)
#
# 复盘"第一刀": 用现有账本算 胜率 / CLV / 校准 + 置信区间, 回答"策略到底有没有 edge"。
# 输入:
#   fills_journal.jsonl       — 入场行(buy=1)带 fair/edge/px; 结算行(close=1, exit=settlement)带 realized
#   <prefix>.settlements.jsonl — 结算 outcome (condition_id → settlement_value: 1=YES赢/0=NO赢/-1=未解析)
# 用法: python3 replay_stats.py <fills_journal.jsonl> <settlements.jsonl>
#
# 分析纪律 (三方调研共识, 写进脚本):
#   - 胜率分母只用【真结算】(join settlements, drop settlement_value=-1); 提前卖出(exit!=settlement)不算胜率
#   - 多笔买入同盘 → 按 cond+yes 聚合(加权均价/总量), 一个仓一个样本
#   - 小样本用 Wilson 95% CI; 报 NaN 覆盖率体检
#   - 按 version 标签行切段(若有多版本, 警告样本可能跨版本污染)
import sys, json, math
from collections import defaultdict

def wilson(k, n, z=1.96):
    if n == 0: return (0.0, 0.0, 0.0)
    p = k / n
    d = 1 + z*z/n
    c = (p + z*z/(2*n)) / d
    h = z*math.sqrt(p*(1-p)/n + z*z/(4*n*n)) / d
    return (p, max(0.0, c-h), min(1.0, c+h))

def load(path):
    rows = []
    try:
        for ln in open(path):
            ln = ln.strip()
            if ln:
                try: rows.append(json.loads(ln))
                except: pass
    except FileNotFoundError:
        print(f"  ⚠ 文件不存在: {path}");
    return rows

def main():
    if len(sys.argv) < 3:
        print("用法: replay_stats.py <fills_journal.jsonl> <settlements.jsonl>"); return
    fills = load(sys.argv[1])
    setts = load(sys.argv[2])

    # settlements: cond → settlement_value (drop -1 = 解析失败, PM 二元无平局)
    outcome = {}
    n_unresolved = 0
    for s in setts:
        cv = s.get("settlement_value", -1)
        if cv == -1: n_unresolved += 1; continue
        outcome[s.get("condition_id","")] = cv

    # version 标签行 → 切段警告
    versions = [r for r in fills if r.get("type") == "version"]
    # 入场行(buy=1, 非 version) 按 cond+yes 聚合
    buys = defaultdict(lambda: {"qty":0.0,"cost":0.0,"fair_w":0.0,"n":0})
    nan_edge = total_buy = 0
    for r in fills:
        if r.get("type")=="version" or r.get("buy")!=1 or r.get("close")==1: continue
        total_buy += 1
        q = r.get("qty",0.0); px = r.get("px",0.0)
        key = (r.get("cond",""), r.get("yes",0))
        b = buys[key]; b["qty"]+=q; b["cost"]+=q*px; b["fair_w"]+=q*r.get("fair",0.0); b["n"]+=1
        # edge_ci 是 emit_d 字段 (NaN 省略) → 缺失 = 轻量行 (旧 live WSS 兜底未富化)。fair 是 base 字段恒写, 不查覆盖。
        if r.get("edge_ci") is None: nan_edge += 1

    # join 聚合仓 → 结算 outcome
    won=lost=0; settled=[]; clv_list=[]
    by_bucket = defaultdict(lambda:[0,0])  # edge_or_fair bucket → [won,total]
    for (cond,yes),b in buys.items():
        if cond not in outcome: continue          # 该盘未结算/未解析 → 不进胜率分母
        if b["qty"]<=0: continue
        avg = b["cost"]/b["qty"]
        fair = b["fair_w"]/b["qty"]  # qty>0 已上面保证
        sv = outcome[cond]
        w = 1 if (yes==sv) else 0                  # 我方边==赢家边 → 赢
        won += w; lost += (1-w)
        settled.append({"cond":cond,"yes":yes,"qty":b["qty"],"avg":avg,"fair":fair,"won":w,"sv":sv})
        # 校准: 按入场均价分桶 (隐含胜率) → 实际命中
        bk = min(int(avg*10),9)
        by_bucket[bk][0]+=w; by_bucket[bk][1]+=1

    n = won+lost
    print("="*64)
    print(f"账本: {sys.argv[1].split('/')[-1]}")
    print(f"version 标签行: {len(versions)}", "(多版本→样本可能跨策略污染, 慎读)" if len(versions)>1 else "")
    if versions:
        v=versions[-1]; print(f"  最新版本: min_edge={v.get('sharp_only_min_edge')} min_open_fair={v.get('min_open_fair')} gate={v.get('sharp_only_gate')}")
    print(f"settlements: 已解析 {len(outcome)} / 未解析(-1) {n_unresolved}")
    print(f"入场行: {total_buy} 笔 → 聚合 {len(buys)} 仓; 其中已结算可判输赢 {n} 仓")
    print(f"edge_ci 覆盖: 缺 {nan_edge}/{total_buy} ({100*nan_edge/max(1,total_buy):.0f}%) (缺=轻量行/warm-up)")
    if total_buy and nan_edge/total_buy > 0.2:
        print("  ⚠ edge_ci 缺失>20% (旧 live WSS 兜底轻量行/warm-up) → 决策上下文样本受限")
    print("-"*64)
    if n==0:
        print("⚠ 无已结算且可判输赢的仓 → 出不了胜率 (需 join 上 settlements 的盘)"); return
    p,lo,hi = wilson(won,n)
    avg_entry = sum(s["avg"]*s["qty"] for s in settled)/sum(s["qty"] for s in settled)
    print(f"实际胜率: {won}/{n} = {100*p:.1f}%  (Wilson 95%CI [{100*lo:.1f}%, {100*hi:.1f}%])")
    print(f"加权均入价: {avg_entry:.3f} → 盈亏平衡胜率 ≈ {100*avg_entry:.1f}%")
    edge_pp = (p-avg_entry)*100
    print(f"实测 edge: {edge_pp:+.1f}pp (实际胜率 − 平衡线); CI 下界 edge: {(lo-avg_entry)*100:+.1f}pp")
    if lo > avg_entry: print("  ✓ CI 下界 > 平衡线 → 统计上有正 edge (该样本)")
    elif hi < avg_entry: print("  ✗ CI 上界 < 平衡线 → 统计上负 edge")
    else: print("  ? CI 跨平衡线 → 样本不足以判定 edge vs 噪声 (需更多结算)")
    print("-"*64)
    print("校准 (按入场均价桶 → 实际胜率):")
    for bk in sorted(by_bucket):
        w,t = by_bucket[bk]
        if t==0: continue
        pp,llo,lhi=wilson(w,t)
        print(f"  价 [{bk/10:.1f},{(bk+1)/10:.1f}): {w}/{t}={100*pp:.0f}% (CI[{100*llo:.0f},{100*lhi:.0f}]) 隐含≈{100*(bk+0.5)/10:.0f}%")
    # 最小样本提示
    print("-"*64)
    print(f"样本量判定: n={n}.", "n<50, 仅 Wilson CI 区间可信, 点估计勿当结论" if n<50 else "n≥50, 可做显著性")
    print("要把 94%(n=18) 和实盘分开需 ~150-200 独立结算 (4-6周不重置不改参)")

if __name__=="__main__": main()
