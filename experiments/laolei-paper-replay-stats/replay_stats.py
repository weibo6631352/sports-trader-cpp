#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/replay_stats.py
# owner: 老雷 | 2026-06-14 | 非生产离线分析 (CLAUDE.md 允许: 数据探索一次性脚本)
#
# 复盘统计: 用账本算 胜率 / CLV / 校准 + 置信区间, 回答"策略有没有 edge"。盯盘期持续跑。
# 用法: python3 replay_stats.py <fills_journal.jsonl> <settlements.jsonl> [--ver <git>]
#   --ver <git>: 只算该 git 版本的样本 (默认: 全部 + 警告跨版本); 用 version 标签行切段。
#
# 分析纪律 (三方调研共识):
#   - 胜率分母只用【真结算】(join settlements, drop settlement_value=-1; 提前卖出不算胜率)
#   - 多笔买入同盘 → 按 cond+yes 聚合(加权均价/总量), 一仓一样本
#   - 小样本用 Wilson 95% CI; 不同 git/config 版本不混算 (version 标签切段)
#   - CLV 用 close_mid(过滤陈旧簿 close_bk_age) — 低方差 edge 前瞻指标
import sys, json, math
from collections import defaultdict

def wilson(k, n, z=1.96):
    if n == 0: return (0.0, 0.0, 0.0)
    p = k/n; d = 1 + z*z/n
    c = (p + z*z/(2*n))/d
    h = z*math.sqrt(p*(1-p)/n + z*z/(4*n*n))/d
    return (p, max(0.0,c-h), min(1.0,c+h))

def load(path):
    rows=[]
    try:
        for ln in open(path):
            ln=ln.strip()
            if ln:
                try: rows.append(json.loads(ln))
                except: pass
    except FileNotFoundError: print(f"  ⚠ 文件不存在: {path}")
    return rows

def fmt_rate(k, n, label):
    if n==0: return f"  {label}: 无样本"
    p,lo,hi = wilson(k,n)
    return f"  {label}: {k}/{n}={100*p:.0f}% (CI[{100*lo:.0f},{100*hi:.0f}])"

def main():
    args=[a for a in sys.argv[1:] if not a.startswith("--")]
    ver_filter=None
    if "--ver" in sys.argv:
        i=sys.argv.index("--ver"); ver_filter=sys.argv[i+1] if i+1<len(sys.argv) else None
    if len(args)<2:
        print("用法: replay_stats.py <fills_journal.jsonl> <settlements.jsonl> [--ver <git>]"); return
    fills=load(args[0]); setts=load(args[1])

    # settlements: cond → settlement_value (drop -1/parse_ok=0)
    outcome={}; n_unresolved=0
    for s in setts:
        if s.get("parse_ok",1)==0 or s.get("settlement_value",-1)==-1: n_unresolved+=1; continue
        outcome[s["condition_id"]]=s["settlement_value"]

    # version 标签行 (ts 升序) → 每笔 fill 归属"其 ts 前最近一条 version"
    vers=sorted([r for r in fills if r.get("type")=="version"], key=lambda r:r.get("ts",0))
    def ver_of(ts):
        g="?"
        for v in vers:
            if v.get("ts",0)<=ts: g=v.get("git","?")
            else: break
        return g

    # 入场行(buy) 按 cond+yes 聚合 (带 version + sport + edge)
    buys=defaultdict(lambda:{"qty":0.0,"cost":0.0,"fair_w":0.0,"edge_w":0.0,"edge_n":0,"sport":"?","mkt":"?","ts":0,"git":"?"})
    nan_edge=total_buy=0
    for r in fills:
        if r.get("type")=="version" or r.get("buy")!=1 or r.get("close")==1: continue
        ts=r.get("ts",0); g=ver_of(ts)
        if ver_filter and g!=ver_filter: continue
        total_buy+=1
        q=r.get("qty",0.0); px=r.get("px",0.0)
        b=buys[(r.get("cond",""), r.get("yes",0))]
        b["qty"]+=q; b["cost"]+=q*px; b["fair_w"]+=q*r.get("fair",0.0)
        b["sport"]=r.get("sport","?"); b["mkt"]=r.get("mkt","?"); b["ts"]=ts; b["git"]=g
        if "feat" not in b: b["feat"]=r   # 首单全特征 (开仓决策上下文; 全方位信号分析用)
        e=r.get("edge_ci")
        if e is None: nan_edge+=1
        else: b["edge_w"]+=q*e; b["edge_n"]+=1

    # 结算行 (close=1 & exit=settlement): 风险/CLV 字段 (mae/mfe/hold/close_mid) by (cond,yes)
    closerow={}; clv=[]
    for r in fills:
        if r.get("close")!=1 or r.get("exit")!="settlement": continue
        if ver_filter and ver_of(r.get("ts",0))!=ver_filter: continue
        closerow[(r.get("cond",""), r.get("yes",0))]=r
        cm=r.get("close_mid"); age=r.get("close_bk_age_ms")
        b=buys.get((r.get("cond",""), r.get("yes",0)))
        if cm is None or not b or b["qty"]<=0: continue
        if age is not None and age>600000: continue  # >10min 陈旧簿 → close_mid 不可信, 跳
        clv.append(cm - b["cost"]/b["qty"])

    # join 聚合仓 → 结算 outcome (胜率分母)
    won=lost=0; settled=[]; feat_rows=[]
    seg_sport=defaultdict(lambda:[0,0]); seg_band=defaultdict(lambda:[0,0]); seg_edge=defaultdict(lambda:[0,0]); seg_ver=defaultdict(lambda:[0,0])
    for (cond,yes),b in buys.items():
        if cond not in outcome or b["qty"]<=0: continue
        avg=b["cost"]/b["qty"]; w=1 if yes==outcome[cond] else 0
        won+=w; lost+=(1-w); settled.append((avg,w,b["qty"]))
        # 全方位: 每仓 = 首单进场特征 + 结算风险 + 输赢 (供 信号→输赢 判别 + 风险分析)
        feat_rows.append({"won":w, "avg":avg, "feat":b.get("feat",{}), "close":closerow.get((cond,yes),{})})
        seg_sport[b["sport"]][0]+=w; seg_sport[b["sport"]][1]+=1
        seg_band[min(int(avg*10),9)][0]+=w; seg_band[min(int(avg*10),9)][1]+=1
        seg_ver[b["git"]][0]+=w; seg_ver[b["git"]][1]+=1
        if b["edge_n"]>0:
            eb=min(int((b["edge_w"]/b["qty"])*100//2),9)  # edge 2pp 一桶
            seg_edge[eb][0]+=w; seg_edge[eb][1]+=1

    n=won+lost
    print("="*64)
    print(f"账本: {args[0].split('/')[-1]}" + (f"  [--ver {ver_filter}]" if ver_filter else ""))
    print(f"version 标签: {len(vers)} 条" + (f" → 版本 {sorted(set(v.get('git','?') for v in vers))}" if vers else " (旧数据无标签)"))
    if len(set(b['git'] for b in buys.values()))>1 and not ver_filter:
        print("  ⚠ 样本跨多个 git 版本 → 混算有污染! 建议 --ver <git> 单版本算, 或看下方分版本胜率")
    print(f"settlements: 已解析 {len(outcome)} / 丢弃(-1/parse_ok=0) {n_unresolved}")
    print(f"入场: {total_buy} 笔 → {len(buys)} 仓; 已结算可判 {n} 仓; edge_ci 缺 {100*nan_edge/max(1,total_buy):.0f}%")
    print("-"*64)
    if n==0:
        print("⚠ 无已结算可判输赢的仓 → 出不了胜率 (仓还没结算/未 join 上 settlements)"); return
    p,lo,hi=wilson(won,n)
    avg_entry=sum(a for a,_,_ in settled)/len(settled)
    print(f"★ 胜率: {won}/{n} = {100*p:.1f}%  Wilson95%CI [{100*lo:.1f}%, {100*hi:.1f}%]")
    print(f"  均入价 {avg_entry:.3f} → 平衡线 {100*avg_entry:.1f}%; 实测 edge {(p-avg_entry)*100:+.1f}pp (CI下界 {(lo-avg_entry)*100:+.1f}pp)")
    print(f"  {'✓ CI下界>平衡线 → 统计上有正edge' if lo>avg_entry else ('✗ CI上界<平衡线 → 负edge' if hi<avg_entry else '? CI跨平衡线 → 样本不足判定')}")
    # PnL/不对称 (2026-06-14 cycle-3: favorite 赢小输大才是真杀手 — 胜率高也可能净亏, 胜率单看会骗人)。
    #   逐仓 realized = 赢?(1−avg)×qty : −avg×qty。期望/仓 > 0 才是真赚 (= 胜率 × 均赢 − 败率 × 均输)。
    pnls=[((1-a) if w else -a)*q for a,w,q in settled]
    wins=[x for x in pnls if x>0]; losses=[x for x in pnls if x<=0]
    tot=sum(pnls); aw=sum(wins)/len(wins) if wins else 0.0; al=sum(losses)/len(losses) if losses else 0.0
    print(f"★ PnL/不对称: 总 ${tot:+.2f} | 期望 ${tot/len(pnls):+.3f}/仓 {'✓正期望' if tot>0 else '✗负期望'}")
    print(f"  均赢 ${aw:+.2f}×{len(wins)} vs 均输 ${al:+.2f}×{len(losses)} | 赢输额比 {abs(aw/al) if al else 0:.2f} (favorite 常<1=输的更狠)")
    if clv:
        cm=sum(clv)/len(clv); sd=(sum((x-cm)**2 for x in clv)/len(clv))**0.5
        t=cm/(sd/len(clv)**0.5) if sd>0 else 0
        print(f"★ CLV(close): 均 {cm:+.4f} (n={len(clv)}, t≈{t:+.1f})  {'✓正(入场优于收盘线)' if cm>0 else '✗负'}")
    print("-"*64)
    print("分运动:");   [print(fmt_rate(w,t,s)) for s,(w,t) in sorted(seg_sport.items(),key=lambda x:-x[1][1])]
    print("分价带:");   [print(fmt_rate(w,t,f'[{b/10:.1f},{(b+1)/10:.1f}) 隐含{100*(b+0.5)/10:.0f}%')) for b,(w,t) in sorted(seg_band.items())]
    if any(t for _,t in seg_edge.values()):
        print("分edge_ci桶 (校准: edge 越高胜率应越高):"); [print(fmt_rate(w,t,f'edge[{e*2}pp,{e*2+2}pp)')) for e,(w,t) in sorted(seg_edge.items())]
    if len(seg_ver)>1:
        print("分版本 (跨版本勿混读):"); [print(fmt_rate(w,t,g)) for g,(w,t) in sorted(seg_ver.items(),key=lambda x:-x[1][1])]

    # ===== 全方位: 进场信号 → 输赢判别 (2026-06-14 老板「全字段挖信号」) =====
    #   每个进场特征: 赢家均值 vs 输家均值。差越大 = 该特征越能区分输赢 (单变量信号强度)。
    #   按 |判别力| 排序, 最能分输赢的特征浮上来。n 小=噪声, 预注册看 sh_vel/odds_age/edge/g_*/imb/liq。
    # 特征源: feat(首单进场全息) + close(结算风险)。(key, 取值函数, 单位提示)
    EFEAT = [("edge_ci","edge",1),("devig","devig",1),("fair","fair",1),("kelly_sugg","kelly",1),
             ("sh_fair","sh_fair",1),("sh_vel","sh_vel(领先>0)",1),("m_clv","m_clv",1),("m_dd","m_dd",1),
             ("bk_spread","spread",1),("bk_imb","簿失衡imb",1),("bk_micro_mid","micro压",1),("bk_age_ms","簿龄ms",1),
             ("ofi","OFI流",1),("rvol","实波动",1),("mom5","5m动量",1),("t_ratio5m","买占比",1),
             ("d5_bid","5档买深",1),("d5_ask","5档卖深",1),("odds_age","赔率龄ms",1),
             ("g_remain","赛剩余s",1),("g_sdiff","比分差",1),("g_period","赛段",1),
             ("vol24h","24h量",1),("liq","流动性",1),("deploy","部署率",1),("n_open","并发仓",1)]
    def m(rows, key):
        vs=[r["feat"].get(key) for r in rows if isinstance(r["feat"].get(key),(int,float))]
        return (sum(vs)/len(vs), len(vs)) if vs else (None,0)
    wr=[r for r in feat_rows if r["won"]]; lr=[r for r in feat_rows if not r["won"]]
    print("-"*64)
    if lr and wr:
        rankings=[]
        for key,lab,_ in EFEAT:
            mw,nw=m(wr,key); ml,nl=m(lr,key)
            if mw is None or ml is None: continue
            sep=abs(mw-ml)/(abs(mw)+abs(ml)+1e-9)  # 归一化判别力
            rankings.append((sep,lab,mw,ml,nw,nl))
        rankings.sort(reverse=True)
        print(f"进场信号→输赢判别 (赢{len(wr)} vs 输{len(lr)}; 按判别力排序, 差大=能分输赢):")
        for sep,lab,mw,ml,nw,nl in rankings[:12]:
            print(f"  {lab:14} 赢均 {mw:+.4g} | 输均 {ml:+.4g}  (判别力 {sep:.2f})")
    else:
        print(f"进场信号→输赢判别: 需赢和输都有样本 (当前 赢{len(wr)}/输{len(lr)}) → 等出现输仓")
    # 风险信号 (MAE/MFE/hold by 输赢): 输家是否入场后更早/更深走低?
    def cm_(rows,key):
        vs=[r["close"].get(key) for r in rows if isinstance(r["close"].get(key),(int,float))]
        return sum(vs)/len(vs) if vs else None
    print("风险信号 (持有期, by 输赢):")
    for key,lab in [("mae","MAE最大不利"),("mfe","MFE最大有利"),("hold_sec","持有秒")]:
        aw_=cm_(wr,key); al_=cm_(lr,key)
        if aw_ is not None or al_ is not None:
            print(f"  {lab:12} 赢 {aw_ if aw_ is None else round(aw_,3)} | 输 {al_ if al_ is None else round(al_,3)}")
        # MFE未兑现 (留桌上利润): 赢仓 MFE 远大于实际 realized → 出场太晚/可锁更多 (仅赢仓有意义)
    mfe_left=[r["close"].get("mfe") for r in wr if isinstance(r["close"].get("mfe"),(int,float))]
    if mfe_left: print(f"  赢仓 MFE 均 {sum(mfe_left)/len(mfe_left):+.3f} (>实际涨幅=利润留桌上, 出场偏晚)")

    # ===== C. 机会错过 (gate-efficacy): 被挡盘最终赢面 → 错过的赢 vs 避开的输 =====
    import os
    gbs=load(os.path.join(os.path.dirname(args[0]) or ".","gate_blocks.jsonl"))
    geff=defaultdict(lambda:[0,0,0.0])  # gate → [若入场赢数, 已结算被挡数, would-PnL/股 累计]
    for g in gbs:
        cond=g.get("cond")
        if cond not in outcome: continue
        px=g.get("px",0.5); ys=g.get("yes",-1)
        # side-对齐定价 (F-2 review, 同 param_research): 选中边(ys∈{0,1}) px 已是该边 ask 直接用;
        #   派生边(yes=-1, 仅 sharp_gap_low, px=YES-canon) → NO 取对侧 1-px, 否则押大热门 would-PnL 虚高。
        if ys in (0,1):
            side=ys; cost=px
        else:
            side=1 if g.get("fair",0)>=0.5 else 0
            cost=px if side==1 else 1.0-px
        won_if=1 if side==outcome[cond] else 0
        pn=(1-cost) if won_if else -cost
        e=geff[g.get("gate")]; e[0]+=won_if; e[1]+=1; e[2]+=pn
    print("-"*64)
    if any(t for _,t,_ in geff.values()):
        print("机会错过 (被挡盘若入场会怎样; 赢面高+正PnL=门挡了赢的=可能太紧; 负PnL=避开了输的=门对):")
        for gt,(w,t,pn) in sorted(geff.items(),key=lambda x:-x[1][1]):
            if t==0: continue
            pp,lo,hi=wilson(w,t)
            v="⚠可能太紧(挡了赢)" if (pn>0 and pp>0.55) else ("✓避损(门对)" if pn<0 else "中性")
            print(f"  {gt:16} 被挡已结算 {t} | 若入场赢面 {100*pp:.0f}%(CI[{100*lo:.0f},{100*hi:.0f}]) | would-PnL {pn/t:+.3f}/股 {v}")
    else:
        print("机会错过: 暂无【被挡且已结算】的盘 (gate_blocks×settlements 还没 join 上) → 等累积")

    # ===== D. 执行/费用 + H. 样本累积速率 =====
    fees=[r.get("fee",0.0) for r in fills if r.get("buy")==1 and isinstance(r.get("fee"),(int,float))]
    tot_fee=sum(fees)
    print(f"执行: 总费 ${tot_fee:.2f}" + (f" (占毛赢 {100*tot_fee/sum(wins):.0f}%)" if wins and sum(wins)>0 else ""))
    # 出场分布
    exits=defaultdict(int)
    for r in fills:
        if r.get("close")==1: exits[r.get("exit","?") or "(空)"]+=1
    if exits: print("出场分布: " + ", ".join(f"{k}:{v}" for k,v in sorted(exits.items(),key=lambda x:-x[1])))
    # 累积速率 → ETA 到 n=150 (用结算行 ts)
    sts=sorted(c.get("ts",0) for c in [x["close"] for x in feat_rows] if c.get("ts"))
    if len(sts)>=2:
        span_h=(sts[-1]-sts[0])/3.6e12; rate=len(sts)/span_h if span_h>0 else 0
        if rate>0: print(f"累积: {n} 结算/{span_h:.1f}h = {rate:.1f}/h → 到 n=150 还需 ~{(150-n)/rate:.0f}h ({(150-n)/rate/24:.1f}天)")

    print("-"*64)
    print(f"样本量: n={n}." + (" n<50 仅 CI 可信, 点估计勿当结论; 信号判别 n太小=噪声" if n<50 else " n≥50 可做显著性"))
    print("目标 ~150-200 独立结算 (4-6周不重置不改参) 才能把 94% vs 67% 分开 + 信号判别才稳")

if __name__=="__main__": main()
