#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/exit_research.py
# owner: 老雷 | 2026-06-14 | 非生产离场研究 (CLAUDE.md 允许离线脚本)
#
# 老板目标 (2026-06-14): 「基建数据支撑, 通过统计分析推断出该不该离场即可, 然后我们据此制定策略。」
#   离场难点: 持仓赢家画像若退化成输家→该离; 但①赢家本身有波动(别卖飞)②离太急阻碍翻盘。
#   关键: 价格波动赢输都有→不能锚价格。区分"暂时回撤 vs 结构退化"靠信念信号(sharp是否还认我们)。
#   老板补: 「这么依赖 sharp, 新鲜度也很关键」→ sharp 陈旧时 conv/vel 失真, 全程带 sh_age 一起看。
#
# 定位: 只做基建+统计分析, 不下结论/不推荐阈值/不设离场规则。让数据指出哪个信号能分"该离/不该离"。
#
# 四段 (老板四选全要):
#   B1 赢家 vs 输家 持仓轨迹画像 (持有期 held_fair/held_vel/sh_conv/gap/回撤深度MAE/回撤时长/sharp新鲜度 对比)
#   B2 回撤 vs 退化 判别力 (水下样本上, 各信号 赢家 vs 输家 AUC 排序 → 数据指出离场该看谁, 不预设)
#   B3 翻盘率 by 信号分桶 (水下样本按信号分桶, 各桶最终翻盘率)
#   B4 离场阈值"假设回放" (扫某信号的离场阈值, 算 总PnL vs 持有到底基准 → 省亏损/卖飞利润权衡曲线, 信息性非推荐)
#
# 全部信号【按 yes 定向到持仓边】(held_fair=sharp if yes else 1-sharp); 退出估值用 token 自己的 bid (真实卖价)。
# 归档数据无 sh_conv/sh_vel/sh_age → 从 sharp 时序兜底推导 (归档也能跑)。
#
# 用法: python3 exit_research.py <fills.jsonl> <position_path.jsonl> <settlements.jsonl> \
#            [--signal held_vel] [--stale-ms 20000] [--ver <git>]
import sys, json, math, collections

def wilson(k, n, z=1.96):
    if n == 0: return (0.0, 0.0)
    p = k/n; d = 1+z*z/n
    c = p+z*z/(2*n); m = z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))
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

def fin(v): return isinstance(v,(int,float)) and math.isfinite(v)

def auc(wins, loss):
    if not wins or not loss: return None
    allv = sorted([(v,0) for v in loss]+[(v,1) for v in wins])
    ranks=[0.0]*len(allv); i=0
    while i < len(allv):
        j=i
        while j+1<len(allv) and allv[j+1][0]==allv[i][0]: j+=1
        r=(i+j)/2.0+1
        for k in range(i,j+1): ranks[k]=r
        i=j+1
    sw=sum(ranks[k] for k in range(len(allv)) if allv[k][1]==1)
    nw=len(wins); nl=len(loss)
    return (sw-nw*(nw+1)/2.0)/(nw*nl)

# 持仓边信号 (按 yes 定向) — 离场研究的候选信号池 (含比赛进度: 老板「比赛进度也非常关键」)
SIGS = ["held_fair","held_vel","sh_conv","gap","drawdown","imb","sh_age_ms","a1sz",
        "g_remain","held_sdiff","g_period","g_age_ms"]
SLAB = {"held_fair":"持仓边fair","held_vel":"持仓边fair速度","sh_conv":"收敛率","gap":"fair−mid缺口",
        "drawdown":"水下深度","imb":"簿失衡","sh_age_ms":"sharp新鲜度ms","a1sz":"卖1量",
        "g_remain":"剩余秒(翻盘空间)","held_sdiff":"持仓边比分差","g_period":"赛段","g_age_ms":"进度新鲜度ms"}
# 信号族标注 (老板「未来一段时间 vs 赢家 别混淆, 但结合用; 未来信号对预测赢家也有用; 标清意义即可」):
#   不排除任一信号(未来信号也留在赢/输判别里), 只打族标让专业人士看清每个数字的意义、自行结合。
FAM = {"held_fair":"赢家","held_sdiff":"赢家","gap":"赢家","edge_ci":"赢家",        # 终局胜负/赢面
       "held_vel":"未来","sh_conv":"未来","imb":"未来","ofi":"未来","mom5":"未来","rvol":"未来",  # 动态方向(对赢家预测也有用)
       "g_remain":"窗口","g_period":"窗口",                                          # 剩余时间/赛段
       "sh_age_ms":"执行","g_age_ms":"执行","a1sz":"执行","bk_spread":"执行",        # 成本/新鲜度
       "drawdown":"状态"}                                                            # 当前水下(条件量)
def slab(k): return SLAB.get(k,k)
def fam(k):  return FAM.get(k,"?")

def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = sys.argv[1:]
    def flagval(name, default=None):
        return flags[flags.index(name)+1] if name in flags else default
    stale_ms = float(flagval("--stale-ms", "20000"))
    sweep_sig = flagval("--signal", "held_vel")
    ver = flagval("--ver")
    json_mode = "--json" in flags  # 机器可读 (老板「喂第三方agent决策」); 文本仍打, JSON 末尾 marker 内
    J = {"tool": "exit_research", "version": ver, "data_source": "real_positions(position_path)"}
    if len(a) < 3:
        print("用法: exit_research.py <fills.jsonl> <position_path.jsonl> <settlements.jsonl> [--signal held_vel] [--stale-ms 20000] [--ver git]")
        return
    fills_p, pp_p, settle_p = a[0], a[1], a[2]

    outcome = {}
    for s in load(settle_p):
        if s.get("parse_ok") == 0 or s.get("settlement_value",-1) == -1: continue
        outcome[s["condition_id"]] = s["settlement_value"]

    fills = load(fills_p)
    ver_cut = None
    if ver:
        for r in fills:
            if r.get("type")=="version" and r.get("git")==ver: ver_cut=r.get("ts",0); break

    # fills 仅作可选富化 (sport 标签); 离场分析【不依赖 fills 进场行】
    fills_meta = {}
    for r in fills:
        if r.get("buy")==1 and r.get("type")!="version":
            tok = r.get("tok")
            if tok and tok not in fills_meta: fills_meta[tok] = r.get("sport")

    # 轨迹: tok → 按 ts 排序的样本。进场信息(avg成本基/yes持仓边/cond)直接从轨迹自带取。
    #   【修重启选择偏差, 老板「遇到问题就赶快解决」】原从 fills 取进场 → 持仓跨重启carry时进场fill在归档里
    #   → carry仓被排除 → 重启越多长持仓越被系统性挤出离场分析。position_path 自带 avg/yes/cond,
    #   离场分析无需 fills → carry仓全纳入, 偏差根除。ver_cut 改按轨迹ts切段(=离场行为所属版本, 更对)。
    traj = collections.defaultdict(list)
    for r in load(pp_p):
        if ver_cut is not None and r.get("ts",0) < ver_cut: continue
        tok = r.get("tok")
        if tok: traj[tok].append(r)
    for t in traj: traj[t].sort(key=lambda r: r.get("ts",0))

    # 建仓位记录: 进场(avg/yes/cond)取自轨迹末样本; 已结算 + 有有效样本
    positions = []
    for tok, trows in traj.items():
        if not trows: continue
        last = trows[-1]; cond = last.get("cond"); yes = last.get("yes"); epx = last.get("avg")
        if cond not in outcome or yes not in (0,1) or not fin(epx) or epx <= 0: continue
        e = {"sport": fills_meta.get(tok, "?")}  # sport 标签 (可选, 无 fills 则 "?")
        won = 1 if yes == outcome[cond] else 0
        samples = []
        prev = None
        for r in traj[tok]:
            if r.get("bvalid") != 1: continue
            mid = r.get("mid"); sharp = r.get("sharp"); bid = r.get("bid")
            if not fin(mid) or mid <= 0: continue
            held_fair = (sharp if yes==1 else 1.0-sharp) if fin(sharp) and sharp>0 else None
            held_mid = mid                      # token 自己价 = 持仓边
            held_bid = bid if fin(bid) and bid>0 else mid
            gap = (held_fair - held_mid) if held_fair is not None else None
            drawdown = epx - held_mid           # >0 = 水下
            # 信念信号: 优先用引擎落的(细窗); 缺则从 sharp 时序兜底(粗30s)
            sh_conv = r.get("sh_conv"); sh_vel_raw = r.get("sh_vel"); sh_age = r.get("sh_age_ms")
            held_vel = None
            if fin(sh_vel_raw) and ("sh_vel" in r):
                held_vel = sh_vel_raw if yes==1 else -sh_vel_raw
            ts = r.get("ts",0)
            # 兜底: 从相邻样本算 (归档无引擎值时)
            if prev is not None:
                dt = (ts - prev["ts"])/1e9
                if dt > 0 and prev["held_fair"] is not None and held_fair is not None:
                    if held_vel is None: held_vel = (held_fair - prev["held_fair"])/dt
                    if not fin(sh_conv) or "sh_conv" not in r:
                        # YES-canon 收敛率: |gap_yes| 缩小=收敛<0
                        gy0 = abs((prev["sharp"] - prev["mid_yes"])) if prev["sharp"] is not None else None
                        gy1 = abs((sharp - (mid if yes==1 else 1.0-mid))) if fin(sharp) else None
                        if gy0 is not None and gy1 is not None: sh_conv = (gy1-gy0)/dt
            # F-5: 标 sh_conv 来源 (eng=引擎细窗~10s / derived=兜底粗30s), B2 跨窗口混算时提示
            conv_src = "eng" if ("sh_conv" in r and fin(r.get("sh_conv"))) else "derived"
            # 比赛进度 (2026-06-14 老板「比赛进度也非常关键」): g_remain剩余秒(=翻盘空间)/g_period赛段/g_age新鲜度;
            #   g_sdiff 按持仓边定向 (YES队−对手 → NO 仓取负, 正=我方领先)。归档无这些字段 → None。
            g_rem = r.get("g_remain"); g_per = r.get("g_period"); g_age = r.get("g_age_ms"); g_sd = r.get("g_sdiff")
            held_sdiff = (g_sd if yes==1 else -g_sd) if fin(g_sd) else None
            s = {"ts":ts, "held_fair":held_fair, "held_mid":held_mid, "held_bid":held_bid, "gap":gap,
                 "drawdown":drawdown, "held_vel":held_vel, "sh_conv":sh_conv if fin(sh_conv) else None,
                 "conv_src":conv_src, "imb":r.get("imb"), "a1sz":r.get("a1sz"),
                 "sh_age_ms":sh_age if fin(sh_age) else None,
                 "g_remain":g_rem if fin(g_rem) else None, "held_sdiff":held_sdiff,
                 "g_period":g_per if fin(g_per) else None, "g_age_ms":g_age if fin(g_age) else None,
                 "sharp":sharp if fin(sharp) else None, "mid_yes":(mid if yes==1 else 1.0-mid)}
            samples.append(s); prev = s
        if not samples: continue
        mae = max((s["drawdown"] for s in samples), default=0.0)   # 最深水下
        uw = [s for s in samples if s["drawdown"] > 0.0]           # 水下样本
        uw_dur = len(uw) * 30                                       # 水下时长估计 (30s/点)
        positions.append({"tok":tok, "yes":yes, "epx":epx, "won":won, "sport":e["sport"],
                          "samples":samples, "mae":mae, "uw":uw, "uw_dur_s":uw_dur,
                          "t0":samples[0]["ts"], "t1":samples[-1]["ts"]})

    nW = sum(p['won'] for p in positions); nL = len(positions)-nW
    print("="*80)
    print(f"离场研究 (老板「基建支撑离场推断」)  版本={ver or '全部'}")
    print(f"数据口径: 【真实持仓轨迹】(position_path, 我们真的持有过的仓, 非反事实模拟) + 真实结算。")
    print(f"已结算且有轨迹的仓: {len(positions)} (赢 {nW} / 输 {nL})   ← 全程注意这个 n!")
    print(f"  ⚠ 当前 n 极小: 输家仅 {nL} 个 → 任何'赢家 vs 输家'对比都接近零信息(单点不成均值), 下面全是【方向演示】非结论。")
    print(f"  注: 此处仓数(轨迹口径)可能与 param_research 的'进场N笔'(成交口径)不同 — 同token多笔/无轨迹/未结算所致, 非矛盾。")
    print(f"sharp 陈旧阈值 --stale-ms={stale_ms:.0f} (超此 sh_conv/vel 视为失真)")
    print("="*80)
    if len(positions) < 6:
        print("⚠ 仓位<6 → 仅演示能力, 结论不可信。等持仓轨迹累积 (含回撤样本的已结算仓 ~几十个)。")
    if not positions:
        print("无可分析仓位。等累积。"); return
    W = [p for p in positions if p["won"]==1]
    L = [p for p in positions if p["won"]==0]

    # 逐仓明细辅助: 某仓某信号在某阶段的均值
    def psig(p, key, phase=None):
        ss = p["samples"]
        if phase=="early": ss = ss[:max(1,len(ss)//3)]
        elif phase=="late": ss = ss[-max(1,len(ss)//3):]
        vs = [s.get(key) for s in ss if s.get(key) is not None and math.isfinite(s.get(key))]
        return sum(vs)/len(vs) if vs else None
    def hold_sec(p): return (p["t1"]-p["t0"])/1e9
    # ============ B0 逐仓明细 (老板「逐仓明细」): 每仓一行, 供 agent 逐仓挖, 非只聚合 ============
    print("\n"+"-"*80)
    print(f"B0 逐仓明细 ({len(positions)} 仓真实持仓; 列: 运动/边/进场价→结局/MAE最深水下/持有s/fair入→末/速度均/收敛均/剩余s)")
    pos_json = []
    for p in sorted(positions, key=lambda x: -x["mae"]):  # 按最深水下排序 (最危险的在前)
        f0 = psig(p,"held_fair","early"); f1 = psig(p,"held_fair","late")
        vel = psig(p,"held_vel"); conv = psig(p,"sh_conv"); grem = psig(p,"g_remain"); shage = psig(p,"sh_age_ms")
        side = "YES" if p["yes"]==1 else "NO"
        res = "赢" if p["won"]==1 else "输"
        f0s = f"{f0:.2f}" if f0 is not None else "—"; f1s = f"{f1:.2f}" if f1 is not None else "—"
        vels = f"{vel:+.1e}" if vel is not None else "—"; convs = f"{conv:+.1e}" if conv is not None else "—"
        grems = f"{grem:.0f}" if grem is not None else "—"
        print(f"  {(p['sport'] or '?'):<9} {side} @{p['epx']:.3f}→{res} MAE{p['mae']:+.3f} 持{hold_sec(p):.0f}s "
              f"fair{f0s}→{f1s} vel{vels} conv{convs} 剩{grems}s")
        pos_json.append({"tok": p["tok"], "sport": p["sport"], "side": side, "entry_px": round(p["epx"],4),
                         "won": p["won"], "mae": round(p["mae"],4), "hold_sec": round(hold_sec(p),0),
                         "uw_dur_s": p["uw_dur_s"], "n_samples": len(p["samples"]),
                         "held_fair_early": f0, "held_fair_late": f1, "held_vel_mean": vel,
                         "sh_conv_mean": conv, "g_remain_mean": grem, "sh_age_ms_mean": shage})
    J["positions_total"] = len(positions); J["won"] = len(W); J["lost"] = len(L)
    J["positions"] = pos_json

    # ============ B1 赢家 vs 输家 持仓轨迹画像 ============
    print("\n"+"-"*80)
    print("B1 赢家 vs 输家 持仓轨迹画像 (持有期均值; 看输家怎么退化、赢家怎么抖动守住)")
    print("   注: '解读'列是【预期方向】; n 小时实际数据可能与之不符(如收敛率), 看大趋势别看个例; 速度类 e-4 量级需 n 够才有意义。")
    def agg(pset, fn):
        vals = [fn(p) for p in pset]; vals = [v for v in vals if v is not None]
        return sum(vals)/len(vals) if vals else None
    def smean(p, key, phase=None):
        ss = p["samples"]
        if phase=="early": ss = ss[:max(1,len(ss)//3)]
        elif phase=="late": ss = ss[-max(1,len(ss)//3):]
        vs = [s[key] for s in ss if s.get(key) is not None]
        return sum(vs)/len(vs) if vs else None
    # (name, fn, 预期方向): "hi"=赢家应更高 / "lo"=赢家应更低 / None=无强预期。解读列就地 ✓/✗ 数据感知,
    #   防"列了预期但数据相反却没就地标"→赶时间的人照预期设反规则 (盲读agent#2 逮的残留)。
    rows = [
        ("持仓边fair 入场", lambda p: smean(p,"held_fair","early"), None),
        ("持仓边fair 后段", lambda p: smean(p,"held_fair","late"),  "hi"),
        ("持仓边fair速度 均", lambda p: smean(p,"held_vel"),         "hi"),
        ("收敛率 均",        lambda p: smean(p,"sh_conv"),           "lo"),
        ("fair−mid缺口 均",  lambda p: smean(p,"gap"),               None),
        ("最深水下 MAE",     lambda p: p["mae"],                     "lo"),
        ("水下时长 s",       lambda p: p["uw_dur_s"] if p["uw"] else 0, None),
        ("sharp新鲜度ms 均", lambda p: smean(p,"sh_age_ms"),         "lo"),
    ]
    hints = {"持仓边fair 后段":"赢家应更高(守住涨)","持仓边fair速度 均":"赢应>0(sharp越认我们)",
             "收敛率 均":"赢应更低(更收敛)","最深水下 MAE":"赢应更浅(别误杀回撤)","sharp新鲜度ms 均":"输家应更陈旧?"}
    print(f"  {'指标':<18}{'赢家':>12}{'输家':>12}   预期 / ✓符合·✗与预期反(n小勿据此设规则)")
    for name, fn, exp in rows:
        w = agg(W, fn); l = agg(L, fn)
        ws = f"{w:+.4g}" if w is not None else "—"
        ls = f"{l:+.4g}" if l is not None else "—"
        mark = ""
        if exp and w is not None and l is not None:
            ok = (w > l) if exp == "hi" else (w < l)
            mark = "  ✓符合" if ok else "  ✗与预期反(n小,勿据此设规则)"
        print(f"  {name:<18}{ws:>12}{ls:>12}   {hints.get(name,''):<18}{mark}")

    # ============ B2 回撤 vs 退化 判别力 (per-position; 评审小蒋: 按样本点会被长持仓主导+自相关→CI虚窄) ============
    print("\n"+"-"*80)
    print("B2 回撤 vs 退化 判别力 [per-position: 每仓取水下期信号均值=1点, n=仓数, 非样本点]")
    print("   赢家=暂时回撤 vs 输家=结构退化, 各信号 AUC; 大=能分'该离/不该离'。不预设答案, 数据说话。")
    print("   信号族(老板「标清意义, 结合用」): [赢家]终局胜负 | [未来]动态方向(对预测赢家也有用) | [窗口]剩余时间 | [执行]成本/新鲜度")
    print("   sharp 陈旧(>--stale-ms)样本先剔除(信号失真); n 小=噪声, 看判别方向别看精确值")
    def pos_uw_mean(p, k):  # 一仓的水下期(剔陈旧)某信号均值 = 该仓代表值
        vs = [s.get(k) for s in p["uw"] if not (s.get("sh_age_ms") is not None and s["sh_age_ms"] > stale_ms)]
        vs = [v for v in vs if v is not None and math.isfinite(v)]
        return sum(vs)/len(vs) if vs else None
    Wd = [p for p in W if p["uw"]]; Ld = [p for p in L if p["uw"]]  # 曾水下的赢/输仓
    disc = []
    for k in SIGS:
        if k=="drawdown": continue
        wv = [v for v in (pos_uw_mean(p,k) for p in Wd) if v is not None]
        lv = [v for v in (pos_uw_mean(p,k) for p in Ld) if v is not None]
        if len(wv) < 3 or len(lv) < 3: continue
        au = auc(wv, lv)
        if au is None: continue
        disc.append((abs(au-0.5)*2, k, au, sum(wv)/len(wv), sum(lv)/len(lv), len(wv), len(lv)))
    disc.sort(reverse=True)
    if not disc:
        print(f"  曾水下的赢/输仓不足(各需≥3) → 等累积 (当前 水下赢仓{len(Wd)}/输仓{len(Ld)})")
    for d,k,au,wm,lm,nw,nl in disc:
        arrow = "赢家高" if au>0.5 else "赢家低"
        warn = " ⚠n小慎读" if (nw<8 or nl<8) else ""
        print(f"  [{fam(k)}] {slab(k):<14} 判别 {d:.2f} ({arrow}) | 回撤中赢仓均 {wm:+.4g} vs 输仓均 {lm:+.4g}  (n赢仓{nw}/输仓{nl}){warn}")
    # F-5: sh_conv 窗口混合提示 (引擎细窗 vs 兜底粗窗口)
    n_eng = sum(1 for p in positions for s in p["uw"] if s.get("conv_src")=="eng")
    n_der = sum(1 for p in positions for s in p["uw"] if s.get("conv_src")=="derived")
    if n_eng and n_der:
        print(f"  注(F-5): sh_conv 来源混合 — 引擎细窗~10s {n_eng}点 / 兜底粗30s {n_der}点; 口径不一, 同源(同版本)数据更可信")
    # 陈旧样本占比 (新鲜度坑提示)
    n_uw_all = sum(len(p["uw"]) for p in positions)
    n_stale = sum(1 for p in positions for s in p["uw"] if s.get("sh_age_ms") is not None and s["sh_age_ms"]>stale_ms)
    if n_uw_all:
        print(f"  ⚠ 水下样本中 sharp 陈旧(>{stale_ms:.0f}ms)占 {n_stale}/{n_uw_all}={100*n_stale/n_uw_all:.0f}% → 已剔除(信念信号失真)")

    # ============ B2b 赢家×未来 结合 (老板「别混淆但结合用」) — 离场真判据 ============
    print("\n"+"-"*80)
    print("B2b 赢家×未来 结合: 在'水下但【仍是赢家】(held_fair≥0.5)'的仓里, 看【未来】信号能否分'守住 vs 退化'")
    print("   = 离场真判据: [赢家]说还是不是赢家 × [未来]说正朝哪变, 结合不混淆")
    print("   注: 此处'仍是赢家'=持有期 held_fair≥0.5(实时身份); 与 B1/B2 的'最终赢家'(结算赢)定义不同, 故仓数可能不一致。")
    uw_pos = [p for p in positions if p["uw"]]
    still_win = [p for p in uw_pos if (pos_uw_mean(p,"held_fair") or 0) >= 0.5]  # 水下期均仍是赢家
    lost_stat = [p for p in uw_pos if 0 < (pos_uw_mean(p,"held_fair") or 0) < 0.5]  # 水下期已失赢家身份
    if still_win:
        sw = sum(p["won"] for p in still_win)
        print(f"  '仍是赢家'水下仓 {len(still_win)}: 最终翻盘 {sw}/{len(still_win)}={100*sw/len(still_win):.0f}%")
    if lost_stat:
        lw = sum(p["won"] for p in lost_stat)
        print(f"  '已失赢家身份'水下仓 {len(lost_stat)}: 最终翻盘 {lw}/{len(lost_stat)}={100*lw/len(lost_stat):.0f}% (失身份→该离的候选信号?)")
    sw_w = [p for p in still_win if p["won"]==1]; sw_l = [p for p in still_win if p["won"]==0]
    if len(sw_w)>=3 and len(sw_l)>=3:
        print(f"  在'仍是赢家'子集里, [未来]信号判别(守住{len(sw_w)} vs 退化{len(sw_l)}):")
        rows=[]
        for k in [s for s in SIGS if fam(s)=="未来"]:
            wv=[v for v in (pos_uw_mean(p,k) for p in sw_w) if v is not None]
            lv=[v for v in (pos_uw_mean(p,k) for p in sw_l) if v is not None]
            if len(wv)>=3 and len(lv)>=3:
                au=auc(wv,lv)
                if au is not None: rows.append((abs(au-0.5)*2,k,au))
        rows.sort(reverse=True)
        print("    (条件IC=在'仍是赢家'子集里 该未来信号 对'守住vs退化'的 rank-biserial IC; 量化大师『条件IC』)")
        for d,k,au in rows:
            print(f"    [未来] {slab(k):<12} 条件IC {2*au-1:+.2f} ({'守住高' if au>0.5 else '守住低'})")
    else:
        print(f"  '仍是赢家'子集 守住/退化 不足各≥3 (当前 {len(sw_w)}/{len(sw_l)}) → 等累积")

    # ============ B3 翻盘率 by 信号分桶 ============
    print("\n"+"-"*80)
    print("B3 翻盘率 by 信号分桶 (曾水下的仓, 按【最深水下时】信号分桶, 各桶最终翻盘率)")
    drew = [p for p in positions if p["uw"]]
    print(f"  曾水下的仓: {len(drew)} (其中最终翻盘={sum(p['won'] for p in drew)})")
    def trough_sig(p, key):
        # 最深水下那个样本的信号值
        s = max(p["uw"], key=lambda s: s["drawdown"])
        return s.get(key)
    for k in [d[1] for d in disc[:2]] or ["held_vel"]:
        vals = [(trough_sig(p,k), p["won"]) for p in drew]
        vals = [(v,w) for v,w in vals if v is not None and math.isfinite(v)]
        if len(vals) < 6: continue
        vs = sorted(v for v,_ in vals)
        terc = [vs[len(vs)//3], vs[2*len(vs)//3]]
        buckets = {"低":[], "中":[], "高":[]}
        for v,w in vals:
            b = "低" if v<=terc[0] else ("高" if v>terc[1] else "中")
            buckets[b].append(w)
        print(f"  按【{slab(k)}】(最深水下时):")
        for b in ["低","中","高"]:
            ws = buckets[b]
            if ws:
                lo,hi = wilson(sum(ws),len(ws))
                # 就地标 CI 过宽=噪声 (盲读agent#2: 防有人据'低桶100%'设规则, 实则纯噪声)
                noise = "  ⚠CI过宽=噪声,勿据此设规则" if (hi-lo) > 0.5 else ""
                print(f"    {b}桶 n{len(ws):>2} 翻盘率 {100*sum(ws)/len(ws):>3.0f}% (CI[{100*lo:.0f},{100*hi:.0f}]){noise}")

    # ============ B4 离场阈值假设回放 ============
    print("\n"+"-"*80)
    print(f"B4 离场阈值假设回放 (信号=【{slab(sweep_sig)}】, 扫离场阈值: 水下且信号触线即离, 算总PnL/股)")
    print("   ⚠ in-sample 双重选择(方向取自B2同数据 + 阈值同数据扫) → '净收益最大处'是拟合噪声, 不可直接当离场参数!")
    print("   只读结构: '省亏损 vs 卖飞利润'的量级权衡 + 大方向。真要定阈值须样本外(OOS≥60仓含20+水下)再验。")
    base = sum((p["won"]-p["epx"]) for p in positions)/len(positions)
    print(f"   基准(持有到底): 均PnL/股 {base:+.4f}  总 {sum((p['won']-p['epx']) for p in positions):+.2f}")
    # exit-Kelly: 持有到底的几何增长 g (金融专家『exit侧Kelly』) — 出场决策也看几何不只算术
    hold_r = [(p["won"]-p["epx"])/p["epx"] for p in positions if p["epx"] > 0]  # return on stake
    if len(hold_r) >= 3:
        fK, gK = 0.0, 0.0
        for fi in [i/100 for i in range(1,100)]:
            if all(1+fi*r > 0 for r in hold_r):
                g = sum(math.log(1+fi*r) for r in hold_r)/len(hold_r)
                if g > gK: gK, fK = g, fi
        flag = f"f*={fK:.2f} 1/4-Kelly={fK/4:.2f}" if gK > 0 else "⚠g≤0(持有到底几何不增长/破产)"
        print(f"   exit-Kelly(持有到底几何): g_max={gK:+.4f} {flag} ← 出场策略也要看几何增长非只算术EV")
    # 方向: 由 B2 该信号 au 定 (赢家低→信号<阈值时离=坏边在低; 赢家高→反)
    au_sig = next((au for d,k,au,*_ in disc if k==sweep_sig), None)
    if au_sig is None:
        print(f"   (信号 {sweep_sig} 判别样本不足, 跳过回放; 可换 --signal)")
    else:
        bad_low = au_sig > 0.5   # 赢家高 → 低值=坏 → 信号<阈值离场
        allv = sorted(s.get(sweep_sig) for p in positions for s in p["uw"]
                      if s.get(sweep_sig) is not None and math.isfinite(s.get(sweep_sig))
                      and not (s.get("sh_age_ms") is not None and s["sh_age_ms"]>stale_ms))
        if len(allv) < 6:
            print("   水下样本不足, 跳过")
        else:
            cuts = sorted(set(allv[int(q*(len(allv)-1))] for q in (0.1,0.25,0.4,0.55,0.7,0.85)))
            print(f"   方向: {'信号<阈值离场(赢家高=低值坏)' if bad_low else '信号>阈值离场(赢家低=高值坏)'}")
            for c in cuts:
                tot=0.0; n_exit_w=0; forfeit=0.0; n_exit_l=0; saved=0.0
                for p in positions:
                    fired=None
                    for s in p["uw"]:
                        if s.get("sh_age_ms") is not None and s["sh_age_ms"]>stale_ms: continue
                        v=s.get(sweep_sig)
                        if v is None or not math.isfinite(v): continue
                        if (bad_low and v<c) or ((not bad_low) and v>c): fired=s; break
                    if fired is not None:
                        r = fired["held_bid"] - p["epx"]   # 离场实现 (按真实卖价 bid)
                        tot += r
                        if p["won"]==1: n_exit_w+=1; forfeit += (p["won"]-p["epx"]) - r
                        else: n_exit_l+=1; saved += r - (p["won"]-p["epx"])
                    else:
                        tot += (p["won"]-p["epx"])
                tot/=len(positions)
                delta = (tot-base)
                print(f"   阈值{c:+.4g} → 触发离场 赢{n_exit_w}/输{n_exit_l} | 省亏损{saved:+.2f} 卖飞利润{forfeit:+.2f} | "
                      f"均PnL/股{tot:+.4f} (Δ基准{delta:+.4f})")
    # ============ B5 α-decay 曲线 (量化大师#3): edge(gap=持仓边fair−mid) 随持有时长怎么衰减 → 调出场时机核心 ============
    print("\n"+"-"*80)
    print("B5 α-decay: edge(=持仓边fair−边mid) 随进场后时长的衰减 (>0=还有低估空间; 归零=edge吃完该走)")
    print("   赢家应: 入场有正edge → 持有中市场收敛 → edge趋0 (alpha被市场吃完=该离的时点); 看几分钟归零")
    bins = [(0,60),(60,180),(180,300),(300,600),(600,1200),(1200,3600),(3600,10**9)]
    blab = ["0-1m","1-3m","3-5m","5-10m","10-20m","20-60m",">60m"]
    def decay_for(pset):
        out = []
        for (b0,b1),bl in zip(bins, blab):
            gaps = []
            for p in pset:
                t0 = p["samples"][0]["ts"]
                for s in p["samples"]:
                    el = (s["ts"]-t0)/1e9
                    if b0 <= el < b1 and s.get("gap") is not None and math.isfinite(s["gap"]): gaps.append(s["gap"])
            if gaps: out.append((bl, len(gaps), sum(gaps)/len(gaps)))
        return out
    dw = decay_for(W); dl = decay_for(L)
    if dw:
        print("  赢家 edge 衰减:  " + "  ".join(f"{bl}:{g:+.3f}(n{n})" for bl,n,g in dw))
    if dl:
        print("  输家 edge 衰减:  " + "  ".join(f"{bl}:{g:+.3f}(n{n})" for bl,n,g in dl))
    if not dw and not dl:
        print("  轨迹不足 → 等累积")
    J["alpha_decay"] = {"winners": [{"bin":bl,"n":n,"mean_edge":round(g,4)} for bl,n,g in dw],
                        "losers": [{"bin":bl,"n":n,"mean_edge":round(g,4)} for bl,n,g in dl]}

    print("="*80)
    print(f"仓位 {len(positions)} (曾水下 {len(drew) if 'drew' in dir() else '?'}). 越积越准。")
    print("读法: B2 判别力强的信号=离场该看的(数据说话, 非预设); B3 看该信号多少值还值得等翻盘;")
    print("      B4 看该信号离场阈值的省亏损/卖飞权衡; B5 edge衰减到0=alpha吃完该离。结论与阈值我们一起定, 脚本只摆信息。")
    if json_mode:
        J["b2_discrimination"] = [{"key": k, "family": fam(k), "disc": round(d,4), "auc": round(au,4),
                                   "direction": "winner_high" if au>0.5 else "winner_low",
                                   "n_win_pos": nw, "n_loss_pos": nl}
                                  for d,k,au,wm,lm,nw,nl in disc] if disc else []
        J["caveats"] = ["这些是真实持仓轨迹(非反事实); 但 n 极小(输家少), 全是方向演示非结论",
                        "B2/B2b/B4 多因样本不足跳过; B4阈值是in-sample双重选择不可直接当离场参数",
                        "信号族: [赢家]终局胜负/[未来]动态方向(对预测赢家也有用)/[窗口]剩余/[执行]成本新鲜度",
                        f"positions={len(positions)}(赢{len(W)}/输{len(L)}); OOS须≥60仓含20+水下才能定离场参数"]
        print("\n===JSON_BEGIN===")
        print(json.dumps(J, ensure_ascii=False, default=lambda o: None))
        print("===JSON_END===")

if __name__ == "__main__":
    main()
