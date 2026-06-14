#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/param_research.py
# owner: 老雷 | 2026-06-14 | 非生产参数研究 (CLAUDE.md 允许离线脚本)
#
# 老板目标 (2026-06-14): 「我们那么多门那么多参数, 最终想通过统计分析【调整参数】或【发现新增参数】,
#   从大数据中总结更有利的【获利模式】。还更想要不同参数下对【选定赢家】的表现。」
#
# 把参数当自变量、赢家捕获/盈利当因变量。在【决策全集】上跑:
#   决策全集 = 进场盘(fills buy, 全特征 + 真实结局/PnL) + 被挡盘(gate_blocks, 已补全因子向量 + 反事实结局/would-PnL)
#   被挡盘=进场盘 10-20×, 大数据全在此 → 才挖得动模式。
#
# 四段输出 (老板三口径全选 + 元目标):
#   ① 赢家 vs 输家画像 + 判别力(AUC) → 自动标【候选新参数】(判别力强但当前没设门的因子)
#   ② 参数阈值扫描 × 赢家捕获 → 每个参数扫一遍门槛, 看赢家捕获/拖进输家/PnL → 推荐最优门槛(调参)
#   ③ 2 维组合挖矿 → 找 (因子A×因子B) 高胜率+正PnL+够样本的组合(获利模式)
#   ④ 单个标志性赢家深挖 → 最大盈利赢家的进场→出场全程轨迹(position_path)
#
# 用法: python3 param_research.py <fills_journal.jsonl> <gate_blocks.jsonl> <settlements.jsonl> \
#            [position_path.jsonl] [--min-support N] [--ver <git>]
import sys, json, math, collections

def wilson(k, n, z=1.96):
    if n == 0: return (0.0, 0.0)
    p = k / n; d = 1 + z*z/n
    c = p + z*z/(2*n); m = z*math.sqrt(p*(1-p)/n + z*z/(4*n*n))
    return (max(0.0,(c-m)/d), min(1.0,(c+m)/d))

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

# 因子可读名 (与 replay_stats 一致 + 新因子)
LABEL = {
    "fair":"模型fair", "px":"进场价", "edge_ci":"edge", "devig":"devig", "kelly_sugg":"kelly建议",
    "sh_fair":"sharp fair", "sh_vel":"sharp速度", "sh_conv":"收敛率★", "sh_vol":"sharp抖动",
    "ofi":"OFI流", "rvol":"实波动", "mom5":"5m动量", "bk_imb":"簿失衡", "bk_micro_mid":"micro压",
    "bk_spread":"spread", "bk_bid_sz":"买1量", "bk_ask_sz":"卖1量", "d5_bid":"5档买深", "d5_ask":"5档卖深",
    "vol24h":"24h量", "liq":"流动性", "deploy":"部署率", "cash_avail":"可用现金", "n_open":"持仓数",
    "equity":"净值", "odds_age":"赔率龄ms", "g_remain":"剩余进度", "g_sdiff":"比分差", "g_period":"比赛阶段",
    "m_life":"赛程", "m_clv":"CLV分", "m_corr":"相关分", "t_vol5m":"5m成交量", "t_ratio5m":"5m买比",
    "bk_age_ms":"簿龄ms",
}
# 当前已设门的因子 (用来标"候选新参数" = 判别力强但没设门)
GATED = {"fair","px","sh_vel","edge_ci","g_remain","bk_imb","d5_ask","odds_age"}
# 扫描/画像的候选因子池 (数值型, 有业务含义)
POOL = ["fair","px","edge_ci","devig","kelly_sugg","sh_fair","sh_vel","sh_conv","sh_vol","ofi","rvol",
        "mom5","bk_imb","bk_micro_mid","bk_spread","d5_bid","d5_ask","vol24h","liq","deploy",
        "odds_age","g_remain","g_sdiff","g_period","m_clv","t_vol5m","t_ratio5m"]

def lab(k): return LABEL.get(k,k)
# 信号族 (老板「未来一段时间 vs 赢家 别混淆但结合用; 未来信号对预测赢家也有用; 标清意义」): 不排除, 只打标
FAM = {"fair":"赢家","devig":"赢家","edge_ci":"赢家","sh_fair":"赢家","kelly_sugg":"赢家","g_sdiff":"赢家","m_clv":"赢家",
       "sh_vel":"未来","sh_conv":"未来","sh_vol":"未来","ofi":"未来","rvol":"未来","mom5":"未来","bk_imb":"未来","bk_micro_mid":"未来",
       "g_remain":"窗口","g_period":"窗口",
       "px":"执行","bk_spread":"执行","d5_bid":"执行","d5_ask":"执行","odds_age":"执行","vol24h":"执行","liq":"执行","deploy":"执行",
       "t_vol5m":"未来","t_ratio5m":"未来"}
def fam(k): return FAM.get(k,"?")

def quart(vals):  # (p25, 中位, p75) — 给分布而非只均值 (老板「提供更多数据挖掘信息」)
    if not vals: return (None, None, None)
    s = sorted(vals); n = len(s)
    q = lambda f: s[min(n-1, int(f*(n-1)+0.5))]
    return (q(0.25), q(0.50), q(0.75))

def auc_p(au, nw, nl):
    # Mann-Whitney AUC 两侧 p (正态近似): z=(AUC-0.5)/SE, SE=sqrt((nw+nl+1)/(12·nw·nl))
    if nw < 1 or nl < 1: return 1.0
    se = math.sqrt((nw+nl+1)/(12.0*nw*nl))
    if se <= 0: return 1.0
    z = abs(au-0.5)/se
    return 2.0*(1.0 - 0.5*(1.0+math.erf(z/math.sqrt(2.0))))  # 两侧

def auc(wins, loss):
    # rank-based AUC = P(win 的 f > loss 的 f); 0.5=无区分, →1 赢家高, →0 赢家低
    if not wins or not loss: return None
    allv = sorted([(v,0) for v in loss]+[(v,1) for v in wins])
    # 平均秩 (处理并列)
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

def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = sys.argv[1:]
    min_support = 8
    if "--min-support" in flags: min_support = int(flags[flags.index("--min-support")+1])
    ver = flags[flags.index("--ver")+1] if "--ver" in flags else None
    json_mode = "--json" in flags  # 机器可读结构化输出 (老板「喂第三方agent决策」); 文本仍打, JSON 附在末尾 marker 内
    J = {"tool": "param_research", "version": ver}  # 结构化结果累积, 末尾 dump
    if len(a) < 3:
        print("用法: param_research.py <fills.jsonl> <gate_blocks.jsonl> <settlements.jsonl> [position_path.jsonl] [--min-support N] [--ver <git>]")
        return
    fills_p, gates_p, settle_p = a[0], a[1], a[2]
    pp_p = a[3] if len(a) > 3 else None

    # settlements: cond → settlement_value (drop -1/parse_ok=0)
    outcome = {}; n_unres = 0
    for s in load(settle_p):
        if s.get("parse_ok") == 0 or s.get("settlement_value", -1) == -1: n_unres += 1; continue
        outcome[s["condition_id"]] = s["settlement_value"]

    # 版本过滤 (按 fills 的 version 行切分样本; 不混版本)
    fills = load(fills_p)
    ver_cut = None
    if ver:
        for r in fills:
            if r.get("type") == "version" and r.get("git") == ver:
                ver_cut = r.get("ts", 0); break

    # ---- 决策全集 ----
    # 进场盘 (fills buy, 真实结局): won = (yes==settle); pnl/股 = won - 进场价
    universe = []
    for r in fills:
        if r.get("buy") != 1 or r.get("close") == 1 or r.get("type") == "version": continue
        if ver_cut is not None and r.get("ts", 0) < ver_cut: continue
        cond = r.get("cond")
        if cond not in outcome: continue
        yes = r.get("yes", -1)
        if yes not in (0, 1): continue
        sv = outcome[cond]; won = 1 if yes == sv else 0
        px = r.get("px")
        pnl = (won - px) if isinstance(px, (int, float)) else None
        universe.append({"src": "entered", "won": won, "pnl": pnl, "yes": yes, "f": r})
    # 被挡盘 (gate_blocks, 反事实结局): side = yes 或 fair≥0.5 推; won = (side==settle); would-pnl = won - px
    gates = load(gates_p)
    for r in gates:
        if ver_cut is not None and r.get("ts", 0) < ver_cut: continue
        cond = r.get("cond")
        if cond not in outcome: continue
        yes_orig = r.get("yes", -1)
        yes = yes_orig
        if yes not in (0, 1):
            fr = r.get("fair")
            if not isinstance(fr, (int, float)): continue
            yes = 1 if fr >= 0.5 else 0
        sv = outcome[cond]; won = 1 if yes == sv else 0
        px = r.get("px")
        # entry cost 必须 side-对齐: 选中边(yes_orig∈{0,1})px 已是该边 ask 直接用;
        #   派生边(原 yes=-1, 如 sharp_gap_low 记的是 YES 价)→ NO 边取对侧 1−px, 否则押大热门 PnL 虚高。
        if isinstance(px, (int, float)):
            cost = px if yes_orig in (0, 1) else (px if yes == 1 else 1.0 - px)
            pnl = won - cost
        else:
            pnl = None
        universe.append({"src": "blocked", "won": won, "pnl": pnl, "yes": yes, "gate": r.get("gate"), "f": r})

    ent = [u for u in universe if u["src"] == "entered"]
    blk = [u for u in universe if u["src"] == "blocked"]
    settled = [u for u in universe if u["pnl"] is not None]
    print("=" * 78)
    print(f"参数研究 (老板「调参/发现新参数/挖获利模式」)  版本={ver or '全部'}")
    print(f"决策全集: 进场 {len(ent)} (真实成交,真实结局) + 被挡 {len(blk)} (【反事实模拟】:假设当时入场) = 已结算 {len(settled)} 个决策点")
    print(f"settlements: 已解析 {len(outcome)} / 丢弃 {n_unres}")
    print(f"⚠⚠ 口径(量化评审修正): 进场{len(ent)}(过门)与被挡{len(blk)}(未过门)是【两个不同总体】, 混合统计无干净'策略表现'含义。")
    print(f"   → ①②③ 是在【决策population(进场+被挡)】上找【信号方向 + 因子覆盖】, 不是策略盈亏; 结局由市场定(对门外生)故方向有效。")
    print(f"   → 【真实策略表现见下方 ⓪(仅进场盘)】; 被挡盘反事实仅供 per-gate 机会错过 + 因子分布覆盖, 且有逆选偏差(记录价高估), 当线索别当真。")
    if len(settled) < 12:
        print(f"⚠ 已结算决策点 {len(settled)} < 12 → 结论不可信, 仅演示能力。等大数据累积 (被挡盘是主力)。")
    print("=" * 78)
    if not settled:
        print("无已结算决策点 → 无法分析。等账本累积。"); return

    wins = [u for u in settled if u["won"] == 1]
    loss = [u for u in settled if u["won"] == 0]
    base_wr = len(wins) / len(settled)
    base_pnl = sum(u['pnl'] for u in settled)/len(settled)
    print(f"决策population基准(信号挖掘口径, 非策略盈亏): 赢面 {len(wins)}/{len(settled)} = {100*base_wr:.0f}% | "
          f"均PnL/股 {base_pnl:+.4f}(含反事实, 无策略含义)")
    J["universe"] = {"entered_real": len(ent), "blocked_counterfactual": len(blk),
                     "settled": len(settled), "settlements": len(outcome)}
    J["baseline_decision_population"] = {"win_rate": base_wr, "mean_pnl_per_share": base_pnl,
        "is_mixed_population": True, "note": "进场+被挡两总体混合, 仅信号挖掘口径, 非策略表现; 策略表现见strategy_real"}
    J["segments"] = {}; J["sweep"] = {}

    # ============ ⓪ 策略真实表现 (仅进场盘真实结局, 金融专家P0) — 这才是"该不该投钱"的口径 ============
    print("\n" + "-" * 78)
    print("⓪ 策略真实表现 (仅【进场盘】真实结局; 这才是策略盈亏, 与上面决策population口径分开)")
    ent_s = [u for u in ent if u["pnl"] is not None]
    if len(ent_s) < 3:
        print(f"  进场已结算 {len(ent_s)} < 3 → 策略真实表现等累积 (这块要进场盘结算, 比被挡盘慢)")
        J["strategy_real"] = {"n": len(ent_s), "note": "进场已结算不足, 等累积"}
    else:
        ns = len(ent_s); nw = sum(u["won"] for u in ent_s)
        pnls = [u["pnl"] for u in ent_s]
        costs = [u["f"].get("px") for u in ent_s if isinstance(u["f"].get("px"), (int, float))]
        ev = sum(pnls)/ns
        var = sum((x-ev)**2 for x in pnls)/ns; sd = math.sqrt(var)
        se = sd/math.sqrt(ns); ev_lo, ev_hi = ev-1.96*se, ev+1.96*se
        tstat = ev/se if se > 0 else 0.0
        wlo, whi = wilson(nw, ns)
        avg_cost = sum(costs)/len(costs) if costs else 0.5
        per_risk = ev/avg_cost if avg_cost > 0 else 0.0  # EV/每股最大损失(≈进场价) — 跨价带可比
        # 经验 Kelly: 每仓 return on stake r=pnl/cost; g(f)=mean ln(1+f·r); 网格找 f*
        rs = [u["pnl"]/u["f"]["px"] for u in ent_s if isinstance(u["f"].get("px"),(int,float)) and u["f"]["px"]>0]
        f_star, g_star = 0.0, 0.0
        if rs:
            for fi in [i/100 for i in range(1, 100)]:
                if all(1+fi*r > 0 for r in rs):
                    g = sum(math.log(1+fi*r) for r in rs)/len(rs)
                    if g > g_star: g_star, f_star = g, fi
        fee_tot = sum(u["f"].get("fee",0) for u in ent_s if isinstance(u["f"].get("fee"),(int,float)))
        print(f"  n={ns} 真实进场已结算 | 赢面 {nw}/{ns}={100*nw/ns:.0f}% (Wilson[{100*wlo:.0f},{100*whi:.0f}])")
        print(f"  EV/股 {ev:+.4f} (95%CI[{ev_lo:+.4f},{ev_hi:+.4f}], t={tstat:+.2f}) {'✓EV>0显著' if tstat>1.96 else '✗EV未显著(CI跨0)'}")
        print(f"  单位风险EV {per_risk:+.4f} (=EV/均进场价{avg_cost:.2f}; 跨价带可比, 修'偏向longshot'坑)")
        if rs and g_star > 0:
            print(f"  Kelly: f*={f_star:.2f} 几何增长g={g_star:+.4f}>0 → 建议 1/4-Kelly={f_star/4:.2f} bankroll/仓")
        else:
            print(f"  Kelly: 无正增长f → ⚠g≤0: 算术EV{('正' if ev>0 else '负')}但几何会破产/不增长, 此策略当前不可投")
        print(f"  费: 总${fee_tot:.2f} (毛EV→净EV的拖累; churn是头号成本)")
        J["strategy_real"] = {"n": ns, "win_rate": round(nw/ns,4), "win_ci": [round(wlo,3),round(whi,3)],
            "ev_per_share": round(ev,4), "ev_ci": [round(ev_lo,4),round(ev_hi,4)], "t_stat": round(tstat,2),
            "ev_significant": tstat > 1.96, "per_unit_risk_ev": round(per_risk,4),
            "kelly_f_star": round(f_star,3), "geom_growth_g": round(g_star,4),
            "geom_positive": g_star > 0, "fee_total": round(fee_tot,2)}

    def feat_vals(rows, k):
        out = []
        for u in rows:
            v = u["f"].get(k)
            if isinstance(v, (int, float)) and math.isfinite(v): out.append(v)
        return out

    # ============ ① 赢家 vs 输家画像 + 判别力 → 候选新参数【假设清单】 ============
    print("\n" + "-" * 78)
    bonf = 0.05 / max(1, len(POOL))  # Bonferroni: 扫 len(POOL) 个因子, 校正后阈值
    print(f"① 赢家 vs 输家画像 (IC=rank-biserial=2·AUC−1∈[-1,1] |IC|>.05弱/.1中/.15强; 判别力=|IC|; p=Mann-Whitney; ★=过BH-FDR q<5%)")
    print("   ★【赢均/输均 = 该因子本身在赢家/输家上的均值, 不是 PnL!】 例: [赢家]模型fair 赢均0.46 = 赢家进场时 fair 均值 0.46")
    print("   '赢家低'(IC<0)= 赢的盘该因子反而更低(如 fair 低=被低估的便宜货才有 edge, 合理); '赢家高'(IC>0)反之。")
    print("   量化评审: 用 BH-FDR(q<5%)替 Bonferroni(27因子相关→Bonf过杀); IC 是量化标准语言(可跨因子/跨时间比+可合成)。")
    print("   仍是【假设清单】非结论, 入参前须 OOS 独立验证。")
    scored = []
    for k in POOL:
        wv = feat_vals(wins, k); lv = feat_vals(loss, k)
        if len(wv) < 4 or len(lv) < 4: continue
        au = auc(wv, lv)
        if au is None: continue
        disc = abs(au - 0.5) * 2
        wm = sum(wv)/len(wv); lm = sum(lv)/len(lv)
        scored.append((disc, k, au, wm, lm, len(wv), len(lv), quart(wv), quart(lv)))
    scored.sort(reverse=True)
    # BH-FDR (Benjamini-Hochberg, q<5%): 比 Bonferroni 少过杀 (量化大师建议)
    ps = sorted([(auc_p(au,nw,nl), k) for d,k,au,wm,lm,nw,nl,wq,lq in scored])
    m = len(ps); fdr_pass = set()
    for rank, (p, k) in enumerate(ps, 1):
        if p <= (rank/m)*0.05: fdr_pass = set(kk for _, kk in ps[:rank])  # 最大通过秩
    if not scored:
        print("  样本不足, 无可算判别的因子")
    print("   信号族(老板「标清意义,结合用」): [赢家]终局胜负 | [未来]动态方向(对预测赢家也有用) | [窗口]剩余时间 | [执行]成本/新鲜度")
    print("   列: IC(signed) | p | 赢家[p25/中位/p75] vs 输家[p25/中位/p75] (分布非只均值)")
    for disc, k, au, wm, lm, nw, nl, wq, lq in scored:
        p = auc_p(au, nw, nl); ic = 2*au - 1  # signed RankIC
        strong = (k not in GATED) and (k in fdr_pass)
        arrow = "赢家高" if au > 0.5 else "赢家低"
        tag = "  ★强候选(过FDR)" if strong else ("  ·候选·未过FDR(仅假设)" if k not in GATED and p < 0.05 else "")
        wqs = f"[{wq[0]:+.3g}/{wq[1]:+.3g}/{wq[2]:+.3g}]" if wq[0] is not None else "—"
        lqs = f"[{lq[0]:+.3g}/{lq[1]:+.3g}/{lq[2]:+.3g}]" if lq[0] is not None else "—"
        print(f"  [{fam(k)}] {lab(k):<10} IC{ic:+.2f} ({arrow}) p={p:.3f} n{nw}/{nl} | 赢{wqs} vs 输{lqs}{tag}")
    cands = [k for d,k,au,wm,lm,nw,nl,wq,lq in scored if k not in GATED and k in fdr_pass]
    print(f"  → ★强候选假设 (过BH-FDR q<5%, 仍需OOS验证): {', '.join(lab(k) for k in cands) if cands else '暂无(n不够/信号弱)'}")
    J["factors"] = [{"key": k, "label": lab(k), "family": fam(k), "gated": k in GATED,
                     "ic_rank": round(2*au-1,4), "disc": round(disc,4), "auc": round(au,4), "p": round(auc_p(au,nw,nl),4),
                     "direction": "winner_high" if au>0.5 else "winner_low",
                     "n_win": nw, "n_loss": nl, "win_p25_med_p75": [wq[0],wq[1],wq[2]],
                     "loss_p25_med_p75": [lq[0],lq[1],lq[2]],
                     "fdr_strong": (k not in GATED) and (k in fdr_pass)}
                    for disc,k,au,wm,lm,nw,nl,wq,lq in scored]
    J["candidates_strong"] = cands

    # ============ ①b 分段画像 (老板「更多数据挖掘」): 运动/盘口/价带/赛段 切片 ============
    print("\n" + "-" * 78)
    print("①b 分段画像 (决策全集切片, 含反事实; 每段 n/赢面/均PnL — 供 agent 挖'哪类盘更值得入')")
    def seg(name, keyfn, minn=5):
        groups = collections.defaultdict(list)
        for u in settled:
            kv = keyfn(u)
            if kv is not None and kv != "": groups[kv].append(u)
        rows = [(g, len(us), sum(x["won"] for x in us)/len(us), sum(x["pnl"] for x in us)/len(us))
                for g, us in groups.items() if len(us) >= minn]
        rows.sort(key=lambda r: -r[1])
        if rows:
            print(f"  按{name}:")
            for g, n, wr, pp in rows:
                lo, hi = wilson(int(round(wr*n)), n)
                print(f"    {str(g):<16} n{n:>3} 赢面{100*wr:>3.0f}%(CI[{100*lo:.0f},{100*hi:.0f}]) 均PnL{pp:+.4f}")
        J["segments"][name] = [{"value": str(g), "n": n, "win_rate": round(wr,4),
                                "ci": [round(wilson(int(round(wr*n)),n)[0],3), round(wilson(int(round(wr*n)),n)[1],3)],
                                "mean_pnl": round(pp,4)} for g,n,wr,pp in rows]
    def pxband(u):
        v = u["f"].get("px")
        if not isinstance(v, (int, float)): return None
        lo = int(v*10)/10.0
        return f"[{lo:.1f},{lo+0.1:.1f})"
    def hourkey(u):  # 时段 (UTC 小时, 从 ts ns) — 老板「能加的都加」
        ts = u["f"].get("ts")
        if not isinstance(ts, (int, float)) or ts <= 0: return None
        return f"{int((ts//1_000_000_000//3600)%24):02d}h"
    seg("运动", lambda u: u["f"].get("sport"))
    seg("盘口", lambda u: u["f"].get("mkt"))
    seg("价带", pxband)
    seg("赛段g_period", lambda u: u["f"].get("g_period"))
    seg("时段UTC", hourkey)

    # ============ ①c 因子相关性 (老板「能加的都加」): 哪些因子冗余, agent 别双重计数 ============
    print("\n" + "-" * 78)
    print("①c 因子相关性 (Pearson, 共同样本≥10; |r|大=冗余/同源, 供 agent 去重不双算)")
    def pearson(xs, ys):
        n = len(xs)
        if n < 3: return None
        mx = sum(xs)/n; my = sum(ys)/n
        sx = sum((x-mx)**2 for x in xs); sy = sum((y-my)**2 for y in ys)
        if sx <= 0 or sy <= 0: return None
        return sum((xs[i]-mx)*(ys[i]-my) for i in range(n)) / math.sqrt(sx*sy)
    corr = []
    for i in range(len(POOL)):
        for j in range(i+1, len(POOL)):
            ka, kb = POOL[i], POOL[j]
            pairs = [(u["f"][ka], u["f"][kb]) for u in settled
                     if isinstance(u["f"].get(ka),(int,float)) and math.isfinite(u["f"].get(ka))
                     and isinstance(u["f"].get(kb),(int,float)) and math.isfinite(u["f"].get(kb))]
            if len(pairs) < 10: continue
            r = pearson([p[0] for p in pairs], [p[1] for p in pairs])
            if r is not None: corr.append((abs(r), r, ka, kb, len(pairs)))
    corr.sort(reverse=True)
    if corr:
        for ar, r, ka, kb, n in corr[:12]:
            print(f"  {lab(ka)}[{fam(ka)}] ~ {lab(kb)}[{fam(kb)}]  r={r:+.2f} (n{n}){'  ⚠高度冗余' if ar>0.8 else ''}")
    else:
        print("  共同样本不足 → 等累积")
    J["correlations_top"] = [{"a": ka, "b": kb, "r": round(r,3), "n": n} for ar,r,ka,kb,n in corr[:20]]

    # ============ ①d 因子分层 (quantile, 量化大师建议): 比扫阈值更标准, 看分位单调性 ============
    print("\n" + "-" * 78)
    print("①d 因子5分位分层 (top判别因子; 每位 n/赢面/均PnL; 单调=真信号, 非单调=噪声; 比②扫阈值更直观)")
    J["quantile_layers"] = {}
    for disc, k, au, *_ in scored[:6]:
        if disc < 0.08: break
        vals = [(u["f"][k], u) for u in settled if isinstance(u["f"].get(k),(int,float)) and math.isfinite(u["f"][k])]
        if len(vals) < 15: continue
        vals.sort(key=lambda x: x[0])
        nq = len(vals); layers = []
        for qi in range(5):
            seg_us = [u for _, u in vals[qi*nq//5:(qi+1)*nq//5]]
            if not seg_us: continue
            wr = sum(u["won"] for u in seg_us)/len(seg_us); pp = sum(u["pnl"] for u in seg_us)/len(seg_us)
            lo_v = vals[qi*nq//5][0]; hi_v = vals[min(nq-1,(qi+1)*nq//5-1)][0]
            layers.append((qi+1, len(seg_us), wr, pp, lo_v, hi_v))
        if layers:
            wseq = [L[2] for L in layers]
            mono = "单调" if (all(wseq[i]<=wseq[i+1]+0.03 for i in range(len(wseq)-1)) or
                              all(wseq[i]>=wseq[i+1]-0.03 for i in range(len(wseq)-1))) else "非单调(噪声?)"
            print(f"  [{fam(k)}] {lab(k)} (5分位, 赢面{mono}):")
            for qn, n, wr, pp, lov, hiv in layers:
                print(f"    Q{qn} [{lov:+.3g},{hiv:+.3g}] n{n:>3} 赢面{100*wr:>3.0f}% 均PnL{pp:+.4f}")
            J["quantile_layers"][k] = {"monotonic": mono.startswith("单调"),
                "layers": [{"q": qn, "n": n, "win_rate": round(wr,4), "mean_pnl": round(pp,4),
                            "range": [round(lov,4), round(hiv,4)]} for qn,n,wr,pp,lov,hiv in layers]}

    # ============ ② 参数阈值扫描 × 赢家捕获 (调参) ============
    print("\n" + "-" * 78)
    print("② 参数阈值扫描 × 赢家捕获 (每因子扫门槛: 过门盘 赢面/错过赢家/PnL)")
    print("   方向: 赢家高→「≥门槛」入场; 赢家低→「≤门槛」。★=候选新参数")
    print("   ⚠ 评审小蒋: '◀最优总PnL'是 in-sample 拟合噪声峰值, 照搬调参会过拟合上线更差(老板红线: in-sample假象)!")
    print("   正确读法: 看【赢面是否随门槛收紧单调上升】(结构规律可信), 别取峰值; 定阈值须 OOS≥150结算点再验。")
    total_w = sum(u["won"] for u in settled)  # 全集赢家总数 (算"错过赢家"=机会错过前沿)
    def sweep(k, direction):
        vals = sorted(set(round(v, 4) for v in feat_vals(settled, k)))
        if len(vals) < 4: return None
        # 候选门槛 = 十分位
        cuts = [vals[int(q*(len(vals)-1))] for q in (0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9)]
        cuts = sorted(set(cuts))
        rows = []
        for c in cuts:
            if direction == ">=": passed = [u for u in settled if isinstance(u["f"].get(k),(int,float)) and u["f"][k] >= c]
            else:               passed = [u for u in settled if isinstance(u["f"].get(k),(int,float)) and u["f"][k] <= c]
            if len(passed) < min_support: continue
            nw = sum(u["won"] for u in passed)
            tot = sum(u["pnl"] for u in passed)
            missed_w = total_w - nw  # 被该门槛挡掉的赢家数 (机会错过前沿: 收门→错过赢家↑)
            rows.append((c, len(passed), nw/len(passed), tot/len(passed), tot, missed_w))
        return rows
    shown = 0
    for disc, k, au, *_ in scored:
        if shown >= 8: break
        if disc < 0.08: break
        direction = ">=" if au > 0.5 else "<="
        rows = sweep(k, direction)
        if not rows: continue
        shown += 1
        cand = "★" if k not in GATED else ""
        best = max(rows, key=lambda r: r[4])  # 总PnL 最大
        print(f"\n  {cand}{lab(k)} ({direction}门槛, 判别{disc:.2f}) [捡漏↔错过前沿: 全集共{total_w}赢家]:")
        # 赢面单调性 (结构规律, 比峰值可信): 门槛由松到紧赢面是否上升
        wr_seq = [r[2] for r in rows]
        mono = "单调↑(结构稳)" if all(wr_seq[i] <= wr_seq[i+1]+0.02 for i in range(len(wr_seq)-1)) else "非单调(慎)"
        for c, n, wr, ppl, tot, mw in rows:
            mark = "  ◀in-sample峰值(勿照搬)" if (c,n)==(best[0],best[1]) else ""
            print(f"    {direction}{c:<10.4g} → 过门{n:>3} 赢面{100*wr:>3.0f}% 错过赢{mw:>3} 均PnL{ppl:+.4f} 总PnL{tot:+.2f}{mark}")
        print(f"    基准(全进,含反事实非真实账面): 赢面{100*base_wr:.0f}% 错过赢0 总PnL{sum(u['pnl'] for u in settled):+.2f}  | "
              f"赢面随收紧: {mono} ← 看这个(结构可信), 别取峰值阈值(in-sample); 收越紧每笔越净但错过越多")
        J["sweep"][k] = {"direction": direction, "monotonic": mono.startswith("单调"),
                         "rows": [{"cut": round(c,4), "n_pass": n, "win_rate": round(wr,4),
                                   "missed_winners": mw, "mean_pnl": round(ppl,4), "total_pnl": round(tot,2)}
                                  for c,n,wr,ppl,tot,mw in rows]}

    # ============ ③ 2维组合挖矿 (获利模式)【假设清单, 非结论】 ============
    print("\n" + "-" * 78)
    print(f"③ 2维组合挖矿 (top因子两两, 同时满足时 赢面/PnL; 样本≥{min_support})")
    print(f"   ⚠ 评审小蒋: 组合挖矿=多重比较×多重选择, n<200 时 top 组合几乎全是噪声 → 仅【生成假设清单】,")
    print("   每条必须 OOS 独立验证(每组合 cell n≥30)才能当'获利模式'。现阶段当线索看, 别当结论。")
    top = [(k, au) for d,k,au,*_ in scored if d >= 0.20][:7]
    def split_mask(k, au):
        # 用中位数二分, 取"有利侧" (赢家方向)
        vals = feat_vals(settled, k)
        if len(vals) < 4: return None
        med = sorted(vals)[len(vals)//2]
        fav_high = au > 0.5
        def fn(u):
            v = u["f"].get(k)
            if not isinstance(v,(int,float)) or not math.isfinite(v): return None
            return (v >= med) if fav_high else (v <= med)
        return fn, med, fav_high
    combos = []
    for i in range(len(top)):
        for j in range(i+1, len(top)):
            ka, aua = top[i]; kb, aub = top[j]
            ma = split_mask(ka, aua); mb = split_mask(kb, aub)
            if not ma or not mb: continue
            fa, meda, hia = ma; fb, medb, hib = mb
            cell = [u for u in settled if fa(u) is True and fb(u) is True]
            if len(cell) < min_support: continue
            nw = sum(u["won"] for u in cell); tot = sum(u["pnl"] for u in cell)
            combos.append((tot/len(cell), nw/len(cell), len(cell), ka, hia, meda, kb, hib, medb, tot))
    combos.sort(reverse=True)
    if not combos:
        print(f"  暂无样本≥{min_support}的双因子组合 → 等累积")
    for ppl, wr, n, ka, hia, meda, kb, hib, medb, tot in combos[:10]:
        oa = "≥" if hia else "≤"; ob = "≥" if hib else "≤"
        print(f"  {lab(ka)}{oa}{meda:.4g} & {lab(kb)}{ob}{medb:.4g} → n{n} 赢面{100*wr:.0f}% 均PnL{ppl:+.4f} 总{tot:+.2f}")
    J["combos"] = [{"a": ka, "a_op": ("≥" if hia else "≤"), "a_thr": round(meda,4),
                    "b": kb, "b_op": ("≥" if hib else "≤"), "b_thr": round(medb,4),
                    "n": n, "win_rate": round(wr,4), "mean_pnl": round(ppl,4), "total_pnl": round(tot,2)}
                   for ppl,wr,n,ka,hia,meda,kb,hib,medb,tot in combos[:20]]

    # ============ ④ 单个标志性赢家深挖 ============
    print("\n" + "-" * 78)
    print("④ 单个标志性赢家深挖 (最大盈利进场赢家的 进场→出场 轨迹)")
    ent_win = [u for u in ent if u["won"] == 1 and u["pnl"] is not None]
    if not ent_win:
        print("  暂无已结算的进场赢家 → 等累积")
    else:
        star = max(ent_win, key=lambda u: u["pnl"] * u["f"].get("qty", 1))
        f = star["f"]; cond = f.get("cond"); tok = f.get("tok")
        print(f"  赢家: {f.get('sport')}/{f.get('mkt')} cond={cond[:14]}.. {'YES' if f.get('yes')==1 else 'NO'} "
              f"进场@{f.get('px'):.3f} qty{f.get('qty'):.1f} → 结算PnL/股{star['pnl']:+.3f}")
        print(f"  进场因子: edge {f.get('edge_ci')} sharp速度 {f.get('sh_vel')} 收敛率 {f.get('sh_conv')} "
              f"簿失衡 {f.get('bk_imb')} 阶段 {f.get('g_period')} 赔率龄 {f.get('odds_age')}")
        if pp_p and tok:
            star_yes = f.get("yes")
            traj = [r for r in load(pp_p) if r.get("tok") == tok and r.get("bvalid") == 1 and r.get("mid", 0) > 0]
            traj.sort(key=lambda r: r.get("ts", 0))
            if traj:
                t0 = traj[0].get("ts", 0)
                # F-4: mid/sharp 都换算到【持仓边】(NO 仓取 1−x), 否则 NO 仓两者在不同边, 收敛对比无意义
                print(f"  轨迹 ({len(traj)} 有效采样点, 30s/点; 均持仓边: 边fair=赔率源真值, 边mid=PM价, 二者收敛=持仓变对):")
                step = max(1, len(traj)//14)  # 最多 ~14 行
                for r in traj[::step]:
                    hm = r.get("mid",0) if star_yes==1 else 1.0-r.get("mid",0)
                    hf = r.get("sharp",0) if star_yes==1 else 1.0-r.get("sharp",0)
                    gr = r.get("g_remain"); gtxt = f" 剩{gr/60:.0f}min" if isinstance(gr,(int,float)) and gr>0 else ""
                    print(f"    +{(r.get('ts',0)-t0)//1_000_000_000:>5}s 边mid {hm:.3f} 边fair {hf:.3f} "
                          f"簿失衡 {r.get('imb',0):+.2f} L1卖 {r.get('a1sz',0):>7.0f} 簿龄 {r.get('bk_age_ms',0):.0f}ms{gtxt}")
            else:
                print("  (无该 token 的有效 position_path 采样)")
    print("=" * 78)
    print(f"样本: 已结算决策点 {len(settled)} (进场{len(ent)}+被挡{len(blk)}). 被挡盘是大数据主力, 越积越准。")
    print("读法(评审后): ①只信★过Bonferroni的强候选(假设, 需OOS); ②看赢面单调性别取in-sample峰值; ③当线索非结论。")
    if json_mode:
        J["caveats"] = ["赢面/PnL含反事实(被挡盘假设入场), 非真实成交战绩; 真实期望≤此值(逆选→更亏)",
                        "candidates/combos/sweep峰值=in-sample假设清单, 入参前须OOS独立验证(进场≥150结算/组合每cell≥30)",
                        "factors的win/loss_p25_med_p75是该因子本身的分布值, 不是PnL",
                        "未过bonferroni_strong的因子=噪声候选, 仅假设不可入参",
                        f"样本n={len(settled)}决策点(进场{len(ent)}真实+被挡{len(blk)}反事实)"]
        print("\n===JSON_BEGIN===")
        print(json.dumps(J, ensure_ascii=False, default=lambda o: None))
        print("===JSON_END===")

if __name__ == "__main__":
    main()
