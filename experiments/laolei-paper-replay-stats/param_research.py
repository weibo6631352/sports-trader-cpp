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
    print(f"决策全集: 进场 {len(ent)} (真实结局) + 被挡 {len(blk)} (反事实) = 已结算 {len(settled)} 个决策点")
    print(f"settlements: 已解析 {len(outcome)} / 丢弃 {n_unres}")
    if len(settled) < 12:
        print(f"⚠ 已结算决策点 {len(settled)} < 12 → 结论不可信, 仅演示能力。等大数据累积 (被挡盘是主力)。")
    print("=" * 78)
    if not settled:
        print("无已结算决策点 → 无法分析。等账本累积。"); return

    wins = [u for u in settled if u["won"] == 1]
    loss = [u for u in settled if u["won"] == 0]
    base_wr = len(wins) / len(settled)
    print(f"全集基准: 赢面 {len(wins)}/{len(settled)} = {100*base_wr:.0f}% | "
          f"均PnL/股 {sum(u['pnl'] for u in settled)/len(settled):+.4f}")

    def feat_vals(rows, k):
        out = []
        for u in rows:
            v = u["f"].get(k)
            if isinstance(v, (int, float)) and math.isfinite(v): out.append(v)
        return out

    # ============ ① 赢家 vs 输家画像 + 判别力 → 候选新参数 ============
    print("\n" + "-" * 78)
    print("① 赢家 vs 输家画像 (判别力=|AUC−0.5|×2, 大=能分输赢; ★候选新参数=判别强但当前无门)")
    scored = []
    for k in POOL:
        wv = feat_vals(wins, k); lv = feat_vals(loss, k)
        if len(wv) < 4 or len(lv) < 4: continue
        au = auc(wv, lv)
        if au is None: continue
        disc = abs(au - 0.5) * 2
        wm = sum(wv)/len(wv); lm = sum(lv)/len(lv)
        scored.append((disc, k, au, wm, lm, len(wv), len(lv)))
    scored.sort(reverse=True)
    if not scored:
        print("  样本不足, 无可算判别的因子")
    for disc, k, au, wm, lm, nw, nl in scored[:18]:
        cand = "" if k in GATED else "  ★候选新门"
        arrow = "赢家高" if au > 0.5 else "赢家低"
        print(f"  {lab(k):<10} 判别 {disc:.2f} ({arrow}) | 赢均 {wm:+.4g} vs 输均 {lm:+.4g}{cand}")
    cands = [k for d,k,*_ in scored[:18] if k not in GATED and d >= 0.30]
    if cands:
        print(f"  → 候选新参数 (判别≥0.30 且当前无门): {', '.join(lab(k) for k in cands)}")

    # ============ ② 参数阈值扫描 × 赢家捕获 (调参) ============
    print("\n" + "-" * 78)
    print("② 参数阈值扫描 × 赢家捕获 (每个强判别因子扫门槛: 过门盘的 赢面/PnL/捕获; 推最优门槛)")
    print("   方向: 赢家高→设「≥门槛」入场; 赢家低→设「≤门槛」入场。★=候选新参数")
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
        for c, n, wr, ppl, tot, mw in rows:
            mark = "  ◀最优总PnL" if (c,n)==(best[0],best[1]) else ""
            print(f"    {direction}{c:<10.4g} → 过门{n:>3} 赢面{100*wr:>3.0f}% 错过赢{mw:>3} 均PnL{ppl:+.4f} 总PnL{tot:+.2f}{mark}")
        print(f"    基准(全进): 赢面{100*base_wr:.0f}% 错过赢0 总PnL{sum(u['pnl'] for u in settled):+.2f}  → "
              f"门槛{direction}{best[0]:.4g} 总PnL{best[4]:+.2f} 但错过{best[5]}个赢家 (收得越紧每笔越净, 错过越多→看总PnL拐点)")

    # ============ ③ 2维组合挖矿 (获利模式) ============
    print("\n" + "-" * 78)
    print(f"③ 2维组合挖矿 (top因子两两组合, 同时满足时的 赢面/PnL; 样本≥{min_support})")
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
            traj = [r for r in load(pp_p) if r.get("tok") == tok and r.get("bvalid") == 1 and r.get("mid", 0) > 0]
            traj.sort(key=lambda r: r.get("ts", 0))
            if traj:
                t0 = traj[0].get("ts", 0)
                print(f"  轨迹 ({len(traj)} 有效采样点, 30s/点; mid=PM中价 sharp=赔率源真值, 二者收敛=持仓变对):")
                step = max(1, len(traj)//14)  # 最多 ~14 行
                for r in traj[::step]:
                    print(f"    +{(r.get('ts',0)-t0)//1_000_000_000:>5}s mid {r.get('mid',0):.3f} sharp {r.get('sharp',0):.3f} "
                          f"簿失衡 {r.get('imb',0):+.2f} L1卖 {r.get('a1sz',0):>7.0f} 簿龄 {r.get('bk_age_ms',0):.0f}ms")
            else:
                print("  (无该 token 的有效 position_path 采样)")
    print("=" * 78)
    print(f"样本: 已结算决策点 {len(settled)} (进场{len(ent)}+被挡{len(blk)}). 被挡盘是大数据主力, 越积越准。")
    print("用法: 候选新门→看②扫描验证→定门槛; 组合→看③获利模式; 调参→对比②基准 vs 最优门槛 PnL")

if __name__ == "__main__":
    main()
