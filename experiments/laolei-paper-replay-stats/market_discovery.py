#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/market_discovery.py
# owner: 老雷 | 2026-06-14 | 非生产·全市场发现型分析 (CLAUDE.md 允许离线脚本)
#
# 老板目标 (2026-06-14):「我们是【收集信息进化】, 非只评估当前策略。」
#   param_research/exit_research 是【决策中心】(只看进场+被挡=引擎碰过的盘) → 评估口径。
#   进化需【市场中心】: market_tape = 对【所有 in-play 有赔率(sharp源)的体育盘】每60s 落一次全因子快照
#   (不管我们下不下单), join 结算 → 全市场因子轨迹 + 结局。本工具在此宇宙上找
#   【现有带门策略够不着的 edge】 = 进化闭环的发现端。
#
# 与 param_research 的关键差别:
#   - param: 样本=我们的【决策】(进场/被挡), 问"我们选的盘表现如何/门该不该调"。
#   - 本工具: 样本=【整个 in-play 宇宙的快照】, 问"市场哪里系统性错价(我们没去碰的也算)"。
#   FLB(只买被低估favorite)就是这类发现: 不是我们策略选出来的, 是市场结构本身的 edge。
#
# YES-canonical: market_tape 全字段按 YES 边 (sharp=YES fair, mid=YES PM中价, ...);
#   settlement_value: 1=YES赢 / 0=NO赢 → 每条快照标签 won_yes = settlement_value。
# PIT: 每条快照因子=快照时刻(无前视); 标签=最终结算 → 点-in-time 因子→未来结局, 干净无 look-ahead。
# ⚠ 伪重复(关键): 同一盘 in-play 期间有 22-59 条快照, 但只有【1个】独立结局 → 快照级 n 严重高估有效样本。
#   故: 所有显著性/IC 的"有效 n"=【独立已结算盘数】, 不是快照数。校准曲线虽按快照池, 也同时报独立盘数。
#
# 用法: python3 market_discovery.py <market_tape.jsonl> <settlements.jsonl> [--min-markets N] [--json]
import sys, json, math, collections

def load(path):
    rows = []
    try:
        for l in open(path):
            l = l.strip()
            if l:
                try: rows.append(json.loads(l))
                except: pass
    except FileNotFoundError: pass
    return rows

def fin(v): return isinstance(v, (int, float)) and math.isfinite(v)

def wilson(k, n, z=1.96):
    if n == 0: return (0.0, 0.0)
    p = k / n; d = 1 + z*z/n
    c = p + z*z/(2*n); m = z*math.sqrt(p*(1-p)/n + z*z/(4*n*n))
    return (max(0.0,(c-m)/d), min(1.0,(c+m)/d))

def quart(vals):
    if not vals: return (None, None, None)
    s = sorted(vals); n = len(s)
    q = lambda f: s[min(n-1, int(f*(n-1)+0.5))]
    return (q(0.25), q(0.50), q(0.75))

def auc_p(au, nw, nl):
    if nw < 1 or nl < 1: return 1.0
    se = math.sqrt((nw+nl+1)/(12.0*nw*nl))
    if se <= 0: return 1.0
    z = abs(au-0.5)/se
    return 2.0*(1.0 - 0.5*(1.0+math.erf(z/math.sqrt(2.0))))

def auc(wins, loss):
    if not wins or not loss: return None
    allv = sorted([(v,0) for v in loss]+[(v,1) for v in wins])
    ranks = [0.0]*len(allv); i=0
    while i < len(allv):
        j=i
        while j+1<len(allv) and allv[j+1][0]==allv[i][0]: j+=1
        r=(i+j)/2.0+1
        for k in range(i,j+1): ranks[k]=r
        i=j+1
    sum_w = sum(ranks[k] for k in range(len(allv)) if allv[k][1]==1)
    nw=len(wins); nl=len(loss)
    return (sum_w - nw*(nw+1)/2.0)/(nw*nl)

# 因子可读名 + 信号族 (与 param_research 一致语义)
LABEL = {"sharp":"sharp fair","sh_vel":"sharp速度","sh_conv":"收敛率","sh_vol":"sharp抖动","sh_age_ms":"sharp龄ms",
    "mid":"PM中价","micro":"micro压","spread":"spread","imb":"簿失衡","b1sz":"买1量","a1sz":"卖1量",
    "bd5":"5档买深","ad5":"5档卖深","bk_age_ms":"簿龄ms","ofi":"OFI流","rvol":"实波动","mom5":"5m动量",
    "g_remain":"剩余s","g_sdiff":"比分差","g_period":"赛段","vol24h":"24h量","liq":"流动性",
    "gap":"sharp−mid缺口"}
FAM = {"sharp":"赢家","sh_vel":"未来","sh_conv":"未来","sh_vol":"未来","sh_age_ms":"执行",
    "mid":"执行","micro":"执行","spread":"执行","imb":"未来","b1sz":"执行","a1sz":"执行","bd5":"执行","ad5":"执行",
    "bk_age_ms":"执行","ofi":"未来","rvol":"未来","mom5":"未来","g_remain":"窗口","g_sdiff":"赢家","g_period":"窗口",
    "vol24h":"执行","liq":"执行","gap":"赢家"}
def lab(k): return LABEL.get(k, k)
def fam(k): return FAM.get(k, "?")
# 因子池 (market_tape 字段; price 家族 sharp/mid 对结局天然预测=非发现, 标注但不当 alpha)
POOL = ["sharp","mid","gap","sh_vel","sh_conv","sh_vol","imb","ofi","rvol","mom5",
        "spread","micro","bd5","ad5","b1sz","a1sz","vol24h","liq","g_remain","g_sdiff"]
PRICE_FAM = {"sharp","mid","micro"}  # 价格家族: 对结局预测是"市场已知", 非我们的发现

def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = sys.argv[1:]
    min_markets = int(flags[flags.index("--min-markets")+1]) if "--min-markets" in flags else 6
    json_mode = "--json" in flags
    if len(a) < 2:
        print("用法: market_discovery.py <market_tape.jsonl> <settlements.jsonl> [--min-markets N] [--json]")
        return
    tape_p, settle_p = a[0], a[1]
    J = {"tool": "market_discovery", "data_source": "market_tape(全in-play宇宙)×settlements"}

    # settlements: cond → 1=YES赢/0=NO赢
    outcome = {}
    for s in load(settle_p):
        if s.get("parse_ok") == 0 or s.get("settlement_value", -1) == -1: continue
        outcome[s["condition_id"]] = s["settlement_value"]

    tape = load(tape_p)
    # 派生 gap = sharp − mid (仅 bvalid 簿有效时 mid 才真); 快照标签 = 结算
    snaps = []           # 已结算快照 (cond ∈ outcome)
    all_conds = set()
    for r in tape:
        c = r.get("cond"); all_conds.add(c)
        if c not in outcome: continue
        won = outcome[c]
        if won not in (0, 1): continue
        s = dict(r); s["won"] = won
        if fin(r.get("sharp")) and fin(r.get("mid")) and r.get("bvalid") == 1 and r.get("mid", 0) > 0:
            s["gap"] = r["sharp"] - r["mid"]
        snaps.append(s)

    # 按盘聚合 (独立结局口径; 因子取 in-play 期均值 — v0 简化, 注: 混早晚期, 后续可改入场刻/固定horizon)
    by_cond = collections.defaultdict(list)
    for s in snaps: by_cond[s["cond"]].append(s)
    markets = []
    for c, ss in by_cond.items():
        m = {"cond": c, "won": ss[0]["won"], "n_snap": len(ss),
             "sport_id": ss[0].get("sport_id"), "mkt_id": ss[0].get("mkt_id")}
        for k in POOL:
            vs = [s[k] for s in ss if fin(s.get(k))]
            m[k] = (sum(vs)/len(vs)) if vs else None
        markets.append(m)

    nW = sum(m["won"] for m in markets); nMk = len(markets)
    ts = [r.get("ts", 0) for r in tape if r.get("ts")]
    span_h = (max(ts)-min(ts))/3.6e12 if len(ts) >= 2 else 0.0
    sports = collections.Counter(r.get("sport_id") for r in tape)

    print("=" * 78)
    print("全市场发现型分析 (老板「收集信息进化, 挖现有策略够不着的 edge」) — market_tape × 结算")
    print(f"宇宙: {len(tape)} 快照 / {len(all_conds)} 盘 / 跨 {span_h:.1f}h | sport_id分布 {dict(sports)}")
    print(f"已结算可分析: {len(snaps)} 快照 / {nMk} 盘 (YES赢 {nW}/{nMk}) | settlements {len(outcome)}")
    print(f"⚠ 伪重复: {len(snaps)}快照仅 {nMk} 个独立结局 → 有效 n=盘数={nMk}, 显著性按盘算不按快照。")
    print(f"PIT: 快照因子=快照时刻(无前视), 标签=最终结算 → 干净。YES-canonical(sharp/mid 皆 YES 边)。")
    J["universe"] = {"snapshots_total": len(tape), "markets_total": len(all_conds), "span_h": round(span_h,2),
                     "settled_snapshots": len(snaps), "settled_markets": nMk, "yes_won": nW,
                     "settlements": len(outcome)}
    if nMk < min_markets:
        print(f"⚠ 已结算盘 {nMk} < {min_markets} → 仅【能力演示】, 结论不可信。等 market_tape 累积(多个比赛时段)。")
    print("=" * 78)
    if nMk == 0:
        print("无已结算盘 → 无法分析。等 market_tape × 结算 累积。")
        if json_mode: print("===JSON_BEGIN==="); print(json.dumps(J, ensure_ascii=False)); print("===JSON_END===")
        return
    base = nW / nMk
    print(f"基准: 宇宙 YES 赢率 {100*base:.0f}% ({nW}/{nMk}盘)")

    # ============ ① PM 价格校准曲线 (核心发现端: 市场哪里系统错价) ============
    # PM 若校准: 价带[a,b)内 YES 实际赢率 ≈ 价带中点。偏离=可交易错价(FLB=favorite带被低估)。
    print("\n" + "-" * 78)
    print("① PM 价格校准 (按 PM中价mid 分带 → YES 实际赢率; 校准则赢率≈带中点, 偏离=系统错价=edge所在)")
    print("   ⚠ 赢率按【快照池】算(伪重复), 故同时报【独立盘数】; CI 该读盘数, 别读快照数。")
    print("   列: 价带 | 快照赢率 | n快照 | n独立盘 | 错价Δ(实际−带中点)")
    pm_cal = []
    bands = [(0.0,0.1),(0.1,0.2),(0.2,0.3),(0.3,0.4),(0.4,0.5),(0.5,0.6),(0.6,0.7),(0.7,0.8),(0.8,0.9),(0.9,1.0)]
    for lo, hi in bands:
        seg = [s for s in snaps if fin(s.get("mid")) and lo <= s["mid"] < hi and s.get("bvalid")==1 and s["mid"]>0]
        if not seg: continue
        nmk = len(set(s["cond"] for s in seg)); w = sum(s["won"] for s in seg); wr = w/len(seg)
        mids = sum(s["mid"] for s in seg)/len(seg)
        print(f"  [{lo:.1f},{hi:.1f})  赢率 {100*wr:3.0f}%  n快照{len(seg):4d}  n盘{nmk:3d}  错价Δ{wr-mids:+.2f}")
        pm_cal.append({"band":[lo,hi],"win_rate":round(wr,3),"n_snap":len(seg),"n_markets":nmk,"mispricing":round(wr-mids,3)})
    J["pm_calibration"] = pm_cal

    # ============ ② sharp−mid gap → 实际赢率 (edge finder: PM 相对 sharp 错价能否赚) ============
    # gap>0 = sharp 认为 YES 比 PM 价更可能 → PM 低估 YES。若实际赢率随 gap 升 = sharp 领先可交易。
    print("\n" + "-" * 78)
    print("② sharp−mid 缺口 → YES 实际赢率 (gap>0=sharp比PM更看好YES=PM低估; 赢率随gap升=sharp领先可交易)")
    print("   这是 sharp-edge 在【全宇宙】的检验(不止我们入的盘): 缺口分桶 → 实际赢率 + 桶内均mid对照。")
    gp = [s for s in snaps if fin(s.get("gap"))]
    if gp:
        gv = sorted(s["gap"] for s in gp)
        # 五分位桶
        qs = [gv[min(len(gv)-1, int(f*(len(gv)-1)+0.5))] for f in (0.2,0.4,0.6,0.8)]
        edges = [-9] + qs + [9]
        print("   列: gap带 | 快照赢率 | 桶内均mid | 赢率−均mid(超额) | n快照 | n盘")
        gap_buckets = []
        for i in range(len(edges)-1):
            lo, hi = edges[i], edges[i+1]
            seg = [s for s in gp if lo <= s["gap"] < hi] if i < len(edges)-2 else [s for s in gp if lo <= s["gap"] <= hi]
            if not seg: continue
            nmk = len(set(s["cond"] for s in seg)); w = sum(s["won"] for s in seg); wr = w/len(seg)
            mm = sum(s["mid"] for s in seg)/len(seg)
            tag = "  ←超额>0=赚" if wr-mm > 0.03 else ""
            print(f"  [{lo:+.3f},{hi:+.3f})  赢率 {100*wr:3.0f}%  均mid {mm:.2f}  超额 {wr-mm:+.2f}  n快照{len(seg):4d}  n盘{nmk:3d}{tag}")
            gap_buckets.append({"gap_lo":round(lo,4),"gap_hi":round(hi,4),"win_rate":round(wr,3),
                                "mean_mid":round(mm,3),"excess":round(wr-mm,3),"n_snap":len(seg),"n_markets":nmk})
        J["gap_buckets"] = gap_buckets
    else:
        print("  无 gap 有效快照(需 sharp+mid+bvalid 同有效)。等簿有效快照累积。")

    # ============ ③ 因子 → 结局 IC (按【盘】聚合=诚实 n; 找非价格因子的增量预测力) ============
    print("\n" + "-" * 78)
    print(f"③ 因子→YES结局 判别 (IC=2·AUC−1; 按【盘】聚合非快照=诚实有效n={nMk}; ★过BH-FDR q<5%)")
    print("   价格家族(sharp/mid/micro)对结局预测是【市场已知】非发现; 真 alpha=非价格因子有增量判别力。")
    print("   ⚠ 当前 n=盘数 极小 → 几乎不可能过 FDR(正确: 诚实拒绝小样本断言), 仅看方向当假设。")
    W = [m for m in markets if m["won"] == 1]; L = [m for m in markets if m["won"] == 0]
    scored = []
    for k in POOL:
        wv = [m[k] for m in W if fin(m.get(k))]; lv = [m[k] for m in L if fin(m.get(k))]
        if len(wv) < 2 or len(lv) < 2: continue
        au = auc(wv, lv)
        if au is None: continue
        scored.append((abs(au-0.5)*2, k, au, len(wv), len(lv), quart(wv), quart(lv)))
    scored.sort(reverse=True)
    ps = sorted([(auc_p(au, nw, nl), k) for d,k,au,nw,nl,wq,lq in scored])
    mm = len(ps); fdr_pass = set()
    for rank, (p, k) in enumerate(ps, 1):
        if p <= (rank/mm)*0.05: fdr_pass = set(kk for _, kk in ps[:rank])
    if not scored:
        print("  样本不足(每侧需≥2盘), 无可算因子。等累积。")
    print("   列: IC | p | 赢盘[p25/中/p75] vs 输盘[p25/中/p75]")
    facts = []
    for disc, k, au, nw, nl, wq, lq in scored:
        p = auc_p(au, nw, nl); ic = 2*au-1
        is_price = k in PRICE_FAM
        star = "  ★过FDR" if (k in fdr_pass and not is_price) else ("  [价格族·非发现]" if is_price else "")
        wqs = f"[{wq[0]:+.3g}/{wq[1]:+.3g}/{wq[2]:+.3g}]" if wq[0] is not None else "—"
        lqs = f"[{lq[0]:+.3g}/{lq[1]:+.3g}/{lq[2]:+.3g}]" if lq[0] is not None else "—"
        print(f"  [{fam(k)}] {lab(k):<10} IC{ic:+.2f} ({'赢盘高' if au>0.5 else '赢盘低'}) p={p:.3f} n{nw}/{nl} | 赢{wqs} vs 输{lqs}{star}")
        facts.append({"key":k,"label":lab(k),"family":fam(k),"is_price":is_price,"ic":round(ic,4),
                      "auc":round(au,4),"p":round(p,4),"n_win":nw,"n_loss":nl,"fdr_strong":(k in fdr_pass and not is_price)})
    J["factors_market"] = facts
    cand = [lab(k) for d,k,au,nw,nl,wq,lq in scored if k in fdr_pass and k not in PRICE_FAM]
    print(f"  → 非价格强候选(过FDR, 需OOS): {', '.join(cand) if cand else '暂无(n太小, 等累积)'}")

    # ============ ④ 分运动/盘口 切片 (edge 住在哪个运动/盘口) ============
    print("\n" + "-" * 78)
    print("④ 分 sport_id / market_type 切片 (YES赢率 + 均gap; 看错价 edge 集中在哪类盘)")
    for keyname, kf in (("sport_id","sport_id"), ("mkt_id","mkt_id")):
        groups = collections.defaultdict(list)
        for m in markets: groups[m.get(kf)].append(m)
        rows = []
        for g, ms in groups.items():
            w = sum(x["won"] for x in ms); gaps = [x["gap"] for x in ms if fin(x.get("gap"))]
            mg = sum(gaps)/len(gaps) if gaps else None
            rows.append((g, len(ms), w/len(ms), mg))
        rows.sort(key=lambda x: -x[1])
        print(f"  按 {keyname}:")
        for g, n, wr, mg in rows:
            mgs = f"均gap{mg:+.3f}" if mg is not None else "均gap—"
            lo, hi = wilson(int(round(wr*n)), n)
            print(f"    {keyname}={g}  n盘{n:3d}  YES赢率 {100*wr:3.0f}% (Wilson[{100*lo:.0f},{100*hi:.0f}])  {mgs}")

    print("=" * 78)
    print(f"宇宙快照 {len(tape)} / 已结算盘 {nMk}. market_tape 越积(多比赛时段) IC/校准越准。")
    print("读法: ①看PM校准在哪带系统偏离(FLB式favorite错价); ②看gap正超额=sharp领先可交易;")
    print("      ③非价格因子方向当假设(n小不过FDR正常); ④看edge集中的运动/盘口。结论须 OOS, 现为发现端就位。")
    if json_mode:
        print("===JSON_BEGIN==="); print(json.dumps(J, ensure_ascii=False)); print("===JSON_END===")

if __name__ == "__main__":
    main()
