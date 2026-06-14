#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/exit_research.py
# 2026-06-14 迁移: position_path 退役 → fills_journal × market_tape
#   动机: position_path 的市场状态已被 market_tape 完全覆盖(事件驱动+20s心跳, 比旧30s轮询更细);
#         持仓上下文(entry价/边/cond/sport)从 fills_journal 聚合;
#         持有期轨迹从 tape held==1 帧取 — held 旗由 C++ 引擎实时打标, 是持有窗口的权威界定。
#
# 用法: python3 exit_research.py [fills_journal.jsonl] [market_tape.jsonl] [settlements.jsonl]
#                                 [--stale-ms 20000] [--ver <git>] [--signal held_vel] [--json]
# 默认路径: data/ml_capture/{fills_journal,market_tape,settlements}.jsonl
#
# ⚠ 伪重复诚实: tape 每盘多帧, 每帧≠独立样本 → 所有显著性按【独立结算盘数】(仓数)报, 非帧数/笔数。
#   n<~12 盘当线索非结论, 显式打印独立盘数。
#
# MAE 口径: 从 tape 轨迹帧计算 max(epx - held_mid) — 与 per-sample drawdown 同比例(per-share, [0,1]);
#   填报 fills close 行的 mae/mfe 字段需单独解读(C++ 可能用不同单位)。tape 衍生 MAE 口径一致, 是主口径。
#
# 分析维度 (与 position_path 版本完全一致):
#   B0 逐仓明细   B1 赢家vs输家画像   B2 判别力AUC   B2b 赢家×未来
#   B3 翻盘率分桶  B4 离场阈值回放     B5 α-decay
#
# owner: 老雷 | 2026-06-14 | 非生产离场研究 (CLAUDE.md 允许离线脚本)
import sys, json, math, collections

DEFAULT_FILLS  = "data/ml_capture/fills_journal.jsonl"
DEFAULT_TAPE   = "data/ml_capture/market_tape.jsonl"
DEFAULT_SETTLE = "data/ml_capture/settlements.jsonl"

# ── helpers ───────────────────────────────────────────────────────────────────

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
    """Rank-based AUC = P(winner value > loser value). 0.5=no separation."""
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

def _peek(path):
    try:
        for l in open(path):
            l = l.strip()
            if l:
                try: return json.loads(l)
                except: pass
    except FileNotFoundError: pass
    return {}

# ── signal pool ───────────────────────────────────────────────────────────────

# 持仓边信号 (按 yes 定向)
SIGS = ["held_fair","held_vel","sh_conv","gap","drawdown","imb","sh_age_ms","a1sz",
        "g_remain","held_sdiff","g_period","g_age_ms"]
SLAB = {
    "held_fair":"持仓边fair","held_vel":"持仓边fair速度","sh_conv":"收敛率",
    "gap":"fair−mid缺口","drawdown":"水下深度","imb":"簿失衡",
    "sh_age_ms":"sharp新鲜度ms","a1sz":"卖1量",
    "g_remain":"剩余秒(翻盘空间)","held_sdiff":"持仓边比分差",
    "g_period":"赛段","g_age_ms":"进度新鲜度ms",
}
# 信号族标注 (老板「标清意义, 结合用; 未来信号对预测赢家也有用」)
FAM = {
    "held_fair":"赢家","held_sdiff":"赢家","gap":"赢家","edge_ci":"赢家",
    "held_vel":"未来","sh_conv":"未来","imb":"未来","ofi":"未来","mom5":"未来","rvol":"未来",
    "g_remain":"窗口","g_period":"窗口",
    "sh_age_ms":"执行","g_age_ms":"执行","a1sz":"执行","bk_spread":"执行",
    "drawdown":"状态",
}
def slab(k): return SLAB.get(k,k)
def fam(k):  return FAM.get(k,"?")

# ── data loading: new tape path ───────────────────────────────────────────────

def _tape_row_to_sample(r, yes, epx):
    """Convert one market_tape frame → sample dict. Returns None if unusable."""
    mid = r.get("mid")
    if not fin(mid) or mid <= 0: return None

    sharp = r.get("sharp")
    bid   = r.get("bid")
    ask   = r.get("ask")

    held_fair = (sharp if yes==1 else 1.0-sharp) if fin(sharp) and sharp > 0 else None
    # held_mid: token mid price oriented to our held side
    held_mid  = mid if yes==1 else 1.0-mid
    # held_bid: best exit price for our side
    #   YES: bid (best bid for YES shares)
    #   NO:  1-ask (NO bid = complement of YES ask, per CLOB complementarity)
    if yes == 1:
        held_bid = bid if fin(bid) and 0 < bid < 1 else held_mid
    else:
        held_bid = (1.0-ask) if fin(ask) and 0 < ask < 1 else held_mid

    gap      = (held_fair - held_mid) if held_fair is not None else None
    drawdown = epx - held_mid  # >0 = underwater vs weighted-avg entry price

    sh_conv  = r.get("sh_conv")
    sh_vel_r = r.get("sh_vel")
    sh_age   = r.get("sh_age_ms")

    held_vel = None
    if fin(sh_vel_r):
        held_vel = sh_vel_r if yes==1 else -sh_vel_r

    g_sd = r.get("g_sdiff")
    held_sdiff = (g_sd if yes==1 else -g_sd) if fin(g_sd) else None

    return {
        "ts": r.get("ts", 0),
        "held_fair": held_fair,
        "held_mid":  held_mid,
        "held_bid":  held_bid,
        "gap":       gap,
        "drawdown":  drawdown,
        "held_vel":  held_vel,
        "sh_conv":   sh_conv if fin(sh_conv) else None,
        "conv_src":  "eng",  # tape frames are always engine-derived, no derived fallback
        "imb":       r.get("imb"),
        "a1sz":      r.get("a1sz"),
        "sh_age_ms": sh_age if fin(sh_age) else None,
        "g_remain":  r.get("g_remain") if fin(r.get("g_remain")) else None,
        "held_sdiff":held_sdiff,
        "g_period":  r.get("g_period") if fin(r.get("g_period")) else None,
        "g_age_ms":  r.get("g_age_ms") if fin(r.get("g_age_ms")) else None,
        "sharp":     sharp if fin(sharp) else None,
        "mid_yes":   mid,  # YES-canonical mid (kept for cross-compat with pp-derived fields)
    }


def load_positions(fills_path, tape_path, outcome, ver_cut):
    """
    fills_journal × market_tape → positions list (2026-06-14, position_path 退役后唯一路径)。

    Entry: all buy==1, close!=1 fills for a tok → weighted-avg price (epx),
           entry_ts = min buy ts, yes = from first buy, sport = from fill.
    Exit:  close==1 fill (exit_ts), or settlement-only if held to end.
    Trajectory: tape held==1 frames for that tok, ts ∈ [entry_ts, exit_ts].
                held flag is the authoritative holding window.

    MAE: max(drawdown) across tape frames — per-share [0,1], consistent with samples.
    uw_dur_s: actual elapsed time between first and last underwater tape frame +20s
              (one heartbeat period; replaces old len(uw)*30 constant).
    """
    fills_rows = load(fills_path)

    # Group fills by tok (YES token = unique per market)
    pos_map = {}  # tok → {cond, yes, buys, close_row, sport}
    for r in fills_rows:
        if r.get("type") == "version": continue
        tok = r.get("tok"); cond = r.get("cond")
        if not tok or not cond: continue
        if ver_cut is not None and r.get("ts", 0) < ver_cut: continue
        if tok not in pos_map:
            pos_map[tok] = {"cond": cond, "yes": r.get("yes", -1),
                            "buys": [], "close_row": None,
                            "sport": r.get("sport") or "?"}
        if r.get("buy") == 1 and not r.get("close"):
            pos_map[tok]["buys"].append(r)
        elif r.get("close") == 1:
            cr = pos_map[tok].get("close_row")
            if cr is None or r.get("ts", 0) > cr.get("ts", 0):
                pos_map[tok]["close_row"] = r  # keep latest close (partials)

    # Load tape grouped by tok — sort once
    tape_by_tok = collections.defaultdict(list)
    for r in load(tape_path):
        tok = r.get("tok")
        if tok: tape_by_tok[tok].append(r)
    for tok in tape_by_tok:
        tape_by_tok[tok].sort(key=lambda r: r.get("ts", 0))

    positions = []
    for tok, pm in pos_map.items():
        buys = pm["buys"]
        if not buys: continue
        cond = pm["cond"]
        if cond not in outcome: continue

        yes = pm["yes"]
        if yes not in (0, 1):
            ys = [b.get("yes") for b in buys if b.get("yes") in (0, 1)]
            yes = ys[0] if ys else -1
        if yes not in (0, 1): continue

        # pUSD-weighted avg entry price
        total_qty = sum(b.get("qty", 0) for b in buys
                        if fin(b.get("qty")) and b.get("qty", 0) > 0)
        if total_qty > 0:
            epx = sum(b.get("px", 0)*b.get("qty", 0)
                      for b in buys if fin(b.get("px")) and fin(b.get("qty"))) / total_qty
        else:
            pxs = [b.get("px") for b in buys if fin(b.get("px"))]
            epx = sum(pxs)/len(pxs) if pxs else None
        if not fin(epx) or epx <= 0: continue

        entry_ts  = min(b.get("ts", 0) for b in buys)
        close_row = pm["close_row"]
        exit_ts   = close_row.get("ts", 0) if close_row else None

        won = 1 if yes == outcome[cond] else 0

        # Tape frames: held==1, within [entry_ts, exit_ts]
        tape_frames = [
            r for r in tape_by_tok.get(tok, [])
            if r.get("held") == 1
            and r.get("ts", 0) >= entry_ts
            and (exit_ts is None or r.get("ts", 0) <= exit_ts)
        ]

        samples = []
        for r in tape_frames:
            if r.get("bvalid") != 1: continue
            s = _tape_row_to_sample(r, yes, epx)
            if s is not None: samples.append(s)

        if not samples: continue

        # MAE from tape (primary, consistent scale)
        mae = max((s["drawdown"] for s in samples), default=0.0)
        uw  = [s for s in samples if s["drawdown"] > 0.0]
        if len(uw) >= 2:
            uw_dur_s = (uw[-1]["ts"]-uw[0]["ts"])/1e9 + 20  # +20s for last frame duration
        elif uw:
            uw_dur_s = 20   # single frame, estimate one heartbeat interval
        else:
            uw_dur_s = 0

        positions.append({
            "tok": tok, "cond": cond, "yes": yes, "epx": epx,
            "won": won, "sport": pm["sport"],
            "samples": samples, "mae": mae,
            "uw": uw, "uw_dur_s": uw_dur_s,
            "t0": samples[0]["ts"], "t1": samples[-1]["ts"],
            "n_frames": len(samples),
            "data_src": "fills×tape",
        })

    return positions


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    a     = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = sys.argv[1:]

    def flagval(name, default=None):
        return flags[flags.index(name)+1] if name in flags else default

    stale_ms  = float(flagval("--stale-ms", "20000"))
    sweep_sig = flagval("--signal", "held_vel")
    ver       = flagval("--ver")
    json_mode = "--json" in flags

    J = {"tool": "exit_research", "version": ver}

    # Positional args with defaults
    fills_p  = a[0] if len(a) > 0 else DEFAULT_FILLS
    data_p   = a[1] if len(a) > 1 else DEFAULT_TAPE
    settle_p = a[2] if len(a) > 2 else DEFAULT_SETTLE

    # Parameter-order sanity check (peek characteristic fields)
    sig_settle = _peek(settle_p)
    sig_data   = _peek(data_p)

    if sig_settle and "condition_id" not in sig_settle and "settlement_value" not in sig_settle:
        print(f"✗ 第3参数 {settle_p} 不像 settlements (缺 condition_id/settlement_value)。"
              f" 正确顺序: <fills> <market_tape> <settlements>"); return

    if sig_data and "tok" not in sig_data:
        print(f"✗ 第2参数 {data_p} 不像 market_tape (缺 tok)。"
              f" 正确顺序: <fills> <market_tape> <settlements>"); return

    # Load settlements
    outcome = {}
    for s in load(settle_p):
        if s.get("parse_ok") == 0 or s.get("settlement_value", -1) == -1: continue
        outcome[s["condition_id"]] = s["settlement_value"]

    # Version cut from fills version rows
    ver_cut = None
    if ver:
        for r in load(fills_p):
            if r.get("type") == "version" and r.get("git") == ver:
                ver_cut = r.get("ts", 0); break

    positions = load_positions(fills_p, data_p, outcome, ver_cut)
    data_src_label = "fills_journal × market_tape (tape held旗=持有窗口)"
    J["data_source"] = data_src_label

    nW = sum(p["won"] for p in positions); nL = len(positions)-nW
    W  = [p for p in positions if p["won"]==1]
    L  = [p for p in positions if p["won"]==0]

    print("="*80)
    print(f"离场研究 (老板「基建支撑离场推断」)  版本={ver or '全部'}")
    print(f"数据口径: 【{data_src_label}】")
    print(f"已结算且有轨迹的仓: {len(positions)} 个独立结算盘 (赢 {nW} / 输 {nL})")
    print(f"  ⚠ 独立样本 = 仓位数(非帧数/笔数): tape每盘多帧但只1个独立结算 → 显著性由此 n={len(positions)} 决定")
    if nL > 0:
        print(f"  ⚠ 输家仅 {nL} 个 → 任何'赢家 vs 输家'对比都接近零信息(单点不成均值), 下面全是【方向演示】非结论。")
    print(f"  注: 仓数(轨迹口径)可能与 param_research 的'进场N笔'(成交口径)不同 — 同token多笔/无轨迹/未结算所致, 非矛盾。")
    print(f"sharp 陈旧阈值 --stale-ms={stale_ms:.0f} (超此 sh_conv/vel 视为失真)")
    print("="*80)

    if len(positions) < 6:
        print("⚠ 仓位<6 → 仅演示能力, 结论不可信。等持仓轨迹累积 (含回撤样本的已结算仓 ~几十个)。")
    if not positions:
        print("无可分析仓位。等累积。"); return

    def psig(p, key, phase=None):
        ss = p["samples"]
        if phase=="early": ss = ss[:max(1,len(ss)//3)]
        elif phase=="late": ss = ss[-max(1,len(ss)//3):]
        vs = [s.get(key) for s in ss if s.get(key) is not None and fin(s.get(key))]
        return sum(vs)/len(vs) if vs else None

    def hold_sec(p): return (p["t1"]-p["t0"])/1e9

    # ── B0 逐仓明细 ──────────────────────────────────────────────────────────
    print("\n"+"-"*80)
    print(f"B0 逐仓明细 ({len(positions)} 仓真实持仓; 独立结算盘 n={len(positions)})")
    print(f"   列: 运动/边/进场价→结局/MAE最深水下/持有s/fair入→末/速度均/收敛均/剩余s/[帧数]")
    pos_json = []
    for p in sorted(positions, key=lambda x: -x["mae"]):
        f0   = psig(p,"held_fair","early"); f1  = psig(p,"held_fair","late")
        vel  = psig(p,"held_vel");          conv = psig(p,"sh_conv")
        grem = psig(p,"g_remain");          shage= psig(p,"sh_age_ms")
        side = "YES" if p["yes"]==1 else "NO"
        res  = "赢" if p["won"]==1 else "输"
        f0s  = f"{f0:.2f}" if f0  is not None else "—"
        f1s  = f"{f1:.2f}" if f1  is not None else "—"
        vels = f"{vel:+.1e}" if vel  is not None else "—"
        convs= f"{conv:+.1e}" if conv is not None else "—"
        grems= f"{grem:.0f}"  if grem is not None else "—"
        print(f"  {(p['sport'] or '?'):<9} {side} @{p['epx']:.3f}→{res} "
              f"MAE{p['mae']:+.3f} 持{hold_sec(p):.0f}s "
              f"fair{f0s}→{f1s} vel{vels} conv{convs} 剩{grems}s [{p['n_frames']}帧]")
        pos_json.append({
            "tok": p["tok"], "sport": p["sport"], "side": side,
            "entry_px": round(p["epx"],4), "won": p["won"],
            "mae": round(p["mae"],4), "hold_sec": round(hold_sec(p),0),
            "uw_dur_s": round(p["uw_dur_s"],0), "n_frames": p["n_frames"],
            "held_fair_early": f0, "held_fair_late": f1,
            "held_vel_mean": vel, "sh_conv_mean": conv,
            "g_remain_mean": grem, "sh_age_ms_mean": shage,
        })
    J["positions_total"] = len(positions); J["won"] = len(W); J["lost"] = len(L)
    J["positions"] = pos_json
    J["note_independence"] = (f"n={len(positions)} 独立结算盘; "
                               "tape帧数仅作轨迹样本, 所有显著性均按仓数计")

    # ── B1 赢家 vs 输家 持仓轨迹画像 ─────────────────────────────────────────
    print("\n"+"-"*80)
    print("B1 赢家 vs 输家 持仓轨迹画像 (持有期均值; 看输家怎么退化、赢家怎么抖动守住)")
    print("   注: '解读'列是【预期方向】; n 小时实际数据可能与之不符, 看大趋势别看个例。")
    def agg(pset, fn):
        vals = [fn(p) for p in pset]; vals = [v for v in vals if v is not None]
        return sum(vals)/len(vals) if vals else None
    def smean(p, key, phase=None):
        ss = p["samples"]
        if phase=="early": ss = ss[:max(1,len(ss)//3)]
        elif phase=="late": ss = ss[-max(1,len(ss)//3):]
        vs = [s[key] for s in ss if s.get(key) is not None]
        return sum(vs)/len(vs) if vs else None
    b1_rows = [
        ("持仓边fair 入场",   lambda p: smean(p,"held_fair","early"), None),
        ("持仓边fair 后段",   lambda p: smean(p,"held_fair","late"),  "hi"),
        ("持仓边fair速度 均", lambda p: smean(p,"held_vel"),           "hi"),
        ("收敛率 均",         lambda p: smean(p,"sh_conv"),            "lo"),
        ("fair−mid缺口 均",   lambda p: smean(p,"gap"),                None),
        ("最深水下 MAE",      lambda p: p["mae"],                      "lo"),
        ("水下时长 s",        lambda p: p["uw_dur_s"] if p["uw"] else 0, None),
        ("sharp新鲜度ms 均",  lambda p: smean(p,"sh_age_ms"),          "lo"),
    ]
    hints = {
        "持仓边fair 后段":"赢家应更高(守住涨)",
        "持仓边fair速度 均":"赢应>0(sharp越认我们)",
        "收敛率 均":"赢应更低(更收敛)",
        "最深水下 MAE":"赢应更浅(别误杀回撤)",
        "sharp新鲜度ms 均":"输家应更陈旧?",
    }
    print(f"  {'指标':<18}{'赢家':>12}{'输家':>12}   预期 / ✓符合·✗与预期反(n小勿据此设规则)")
    for name, fn, exp in b1_rows:
        w = agg(W, fn); l = agg(L, fn)
        ws = f"{w:+.4g}" if w is not None else "—"
        ls = f"{l:+.4g}" if l is not None else "—"
        mark = ""
        if exp and w is not None and l is not None:
            ok = (w > l) if exp=="hi" else (w < l)
            mark = "  ✓符合" if ok else "  ✗与预期反(n小,勿据此设规则)"
        print(f"  {name:<18}{ws:>12}{ls:>12}   {hints.get(name,''):<18}{mark}")

    # ── B2 判别力 ────────────────────────────────────────────────────────────
    print("\n"+"-"*80)
    print("B2 回撤 vs 退化 判别力 [per-position: 每仓取水下期信号均值=1点, n=仓数, 非帧数]")
    print("   赢家=暂时回撤 vs 输家=结构退化, 各信号 AUC; 大=能分'该离/不该离'。数据说话。")
    print("   信号族: [赢家]终局胜负 | [未来]动态方向(对预测赢家也有用) | [窗口]剩余时间 | [执行]成本/新鲜度")
    print("   sharp 陈旧(>--stale-ms)样本先剔除(信号失真); n 小=噪声, 看判别方向别看精确值")

    def pos_uw_mean(p, k):
        vs = [s.get(k) for s in p["uw"]
              if not (s.get("sh_age_ms") is not None and s["sh_age_ms"] > stale_ms)]
        vs = [v for v in vs if v is not None and fin(v)]
        return sum(vs)/len(vs) if vs else None

    Wd = [p for p in W if p["uw"]]; Ld = [p for p in L if p["uw"]]
    disc = []
    for k in SIGS:
        if k == "drawdown": continue
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
        warn  = " ⚠n小慎读" if (nw<8 or nl<8) else ""
        print(f"  [{fam(k)}] {slab(k):<14} 判别 {d:.2f} ({arrow}) | "
              f"回撤中赢仓均 {wm:+.4g} vs 输仓均 {lm:+.4g}  (n赢仓{nw}/输仓{nl}){warn}")

    # sh_conv 口径提示: tape 全为引擎值, 口径一致 (无 position_path 时代的兜底衍生混合问题)
    print("  注: sh_conv 全为引擎值(eng), 口径一致")

    n_uw_all = sum(len(p["uw"]) for p in positions)
    n_stale  = sum(1 for p in positions for s in p["uw"]
                   if s.get("sh_age_ms") is not None and s["sh_age_ms"] > stale_ms)
    if n_uw_all:
        print(f"  ⚠ 水下样本中 sharp 陈旧(>{stale_ms:.0f}ms)占 {n_stale}/{n_uw_all}="
              f"{100*n_stale/n_uw_all:.0f}% → 已剔除(信念信号失真)")

    # ── B2b 赢家×未来 ────────────────────────────────────────────────────────
    print("\n"+"-"*80)
    print("B2b 赢家×未来 结合: 在'水下但【仍是赢家】(held_fair≥0.5)'的仓里, 看【未来】信号分'守住 vs 退化'")
    print("   = 离场真判据: [赢家]说还是不是赢家 × [未来]说正朝哪变, 结合不混淆")
    print("   注: '仍是赢家'=持有期 held_fair≥0.5(实时身份); 与 B1/B2 的'最终赢家'(结算赢)定义不同。")
    uw_pos    = [p for p in positions if p["uw"]]
    still_win = [p for p in uw_pos if (pos_uw_mean(p,"held_fair") or 0) >= 0.5]
    lost_stat = [p for p in uw_pos if 0 < (pos_uw_mean(p,"held_fair") or 0) < 0.5]
    if still_win:
        sw = sum(p["won"] for p in still_win)
        print(f"  '仍是赢家'水下仓 {len(still_win)}: 最终翻盘 {sw}/{len(still_win)}={100*sw/len(still_win):.0f}%")
    if lost_stat:
        lw = sum(p["won"] for p in lost_stat)
        print(f"  '已失赢家身份'水下仓 {len(lost_stat)}: 最终翻盘 {lw}/{len(lost_stat)}={100*lw/len(lost_stat):.0f}% (失身份→该离的候选信号?)")
    sw_w = [p for p in still_win if p["won"]==1]; sw_l = [p for p in still_win if p["won"]==0]
    if len(sw_w) >= 3 and len(sw_l) >= 3:
        print(f"  在'仍是赢家'子集里, [未来]信号判别(守住{len(sw_w)} vs 退化{len(sw_l)}):")
        rows2 = []
        for k in [s for s in SIGS if fam(s)=="未来"]:
            wv=[v for v in (pos_uw_mean(p,k) for p in sw_w) if v is not None]
            lv=[v for v in (pos_uw_mean(p,k) for p in sw_l) if v is not None]
            if len(wv)>=3 and len(lv)>=3:
                au=auc(wv,lv)
                if au is not None: rows2.append((abs(au-0.5)*2,k,au))
        rows2.sort(reverse=True)
        print("    (条件IC=在'仍是赢家'子集里 该未来信号 对'守住vs退化'的 rank-biserial IC)")
        for d,k,au in rows2:
            print(f"    [未来] {slab(k):<12} 条件IC {2*au-1:+.2f} ({'守住高' if au>0.5 else '守住低'})")
    else:
        print(f"  '仍是赢家'子集 守住/退化 不足各≥3 (当前 {len(sw_w)}/{len(sw_l)}) → 等累积")

    # ── B3 翻盘率 by 信号分桶 ─────────────────────────────────────────────────
    print("\n"+"-"*80)
    print("B3 翻盘率 by 信号分桶 (曾水下的仓, 按【最深水下时】信号分桶, 各桶最终翻盘率)")
    drew = [p for p in positions if p["uw"]]
    print(f"  曾水下的仓: {len(drew)} (其中最终翻盘={sum(p['won'] for p in drew)})")

    def trough_sig(p, key):
        s = max(p["uw"], key=lambda s: s["drawdown"])
        return s.get(key)

    for k in ([d[1] for d in disc[:2]] or ["held_vel"]):
        vals = [(trough_sig(p,k), p["won"]) for p in drew]
        vals = [(v,w) for v,w in vals if v is not None and fin(v)]
        if len(vals) < 6: continue
        vs   = sorted(v for v,_ in vals)
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
                noise = "  ⚠CI过宽=噪声,勿据此设规则" if (hi-lo)>0.5 else ""
                print(f"    {b}桶 n{len(ws):>2} 翻盘率 {100*sum(ws)/len(ws):>3.0f}% "
                      f"(CI[{100*lo:.0f},{100*hi:.0f}]){noise}")

    # ── B4 离场阈值假设回放 ───────────────────────────────────────────────────
    print("\n"+"-"*80)
    print(f"B4 离场阈值假设回放 (信号=【{slab(sweep_sig)}】, 扫离场阈值: 水下且信号触线即离, 算总PnL/股)")
    print("   ⚠ in-sample 双重选择(方向取自B2同数据 + 阈值同数据扫) → '净收益最大处'是拟合噪声, 不可直接当离场参数!")
    print("   只读结构: '省亏损 vs 卖飞利润'的量级权衡 + 大方向。真要定阈值须OOS≥60仓含20+水下再验。")

    base = sum((p["won"]-p["epx"]) for p in positions)/len(positions)
    print(f"   基准(持有到底): 均PnL/股 {base:+.4f}  总 {sum((p['won']-p['epx']) for p in positions):+.2f}")

    hold_r = [(p["won"]-p["epx"])/p["epx"] for p in positions if p["epx"] > 0]
    if len(hold_r) >= 3:
        fK=gK=0.0
        for fi in [i/100 for i in range(1,100)]:
            if all(1+fi*r>0 for r in hold_r):
                g=sum(math.log(1+fi*r) for r in hold_r)/len(hold_r)
                if g>gK: gK,fK=g,fi
        flag = (f"f*={fK:.2f} 1/4-Kelly={fK/4:.2f}" if gK>0
                else "⚠g≤0(持有到底几何不增长/破产)")
        print(f"   exit-Kelly(持有到底几何): g_max={gK:+.4f} {flag} ← 出场策略也要看几何增长非只算术数EV")

    au_sig = next((au for d,k,au,*_ in disc if k==sweep_sig), None)
    if au_sig is None:
        print(f"   (信号 {sweep_sig} 判别样本不足, 跳过回放; 可换 --signal)")
    else:
        bad_low = au_sig > 0.5
        allv = sorted(
            s.get(sweep_sig) for p in positions for s in p["uw"]
            if s.get(sweep_sig) is not None and fin(s.get(sweep_sig))
            and not (s.get("sh_age_ms") is not None and s["sh_age_ms"] > stale_ms)
        )
        if len(allv) < 6:
            print("   水下样本不足, 跳过")
        else:
            cuts = sorted(set(allv[int(q*(len(allv)-1))]
                              for q in (0.1,0.25,0.4,0.55,0.7,0.85)))
            print(f"   方向: {'信号<阈值离场(赢家高=低值坏)' if bad_low else '信号>阈值离场(赢家低=高值坏)'}")
            for c in cuts:
                tot=0.0; n_exit_w=0; forfeit=0.0; n_exit_l=0; saved=0.0
                for p in positions:
                    fired=None
                    for s in p["uw"]:
                        if s.get("sh_age_ms") is not None and s["sh_age_ms"]>stale_ms: continue
                        v=s.get(sweep_sig)
                        if v is None or not fin(v): continue
                        if (bad_low and v<c) or ((not bad_low) and v>c): fired=s; break
                    if fired is not None:
                        r=fired["held_bid"]-p["epx"]; tot+=r
                        if p["won"]==1: n_exit_w+=1; forfeit+=(p["won"]-p["epx"])-r
                        else: n_exit_l+=1; saved+=r-(p["won"]-p["epx"])
                    else:
                        tot+=(p["won"]-p["epx"])
                tot/=len(positions); delta=tot-base
                print(f"   阈值{c:+.4g} → 触发离场 赢{n_exit_w}/输{n_exit_l} | "
                      f"省产损{saved:+.2f} 卖飞利润{forfeit:+.2f} | "
                      f"均PnL/股{tot:+.4f} (Δ基准{delta:+.4f})")

    # ── B5 α-decay 曲线 ──────────────────────────────────────────────────────
    print("\n"+"-"*80)
    print("B5 α-decay: edge(=持仓边fair−边mid) 随进场后时长的衰减 (>0=还有低估空间; 归零=edge吃完该离)")
    print("   赢家应: 入场有正edge → 持有中市场收敛 → edge趋0 (看几分钟归零)")

    bins = [(0,60),(60,180),(180,300),(300,600),(600,1200),(1200,3600),(3600,10**9)]
    blab = ["0-1m","1-3m","3-5m","5-10m","10-20m","20-60m",">60m"]

    def decay_for(pset):
        out = []
        for (b0,b1),bl in zip(bins,blab):
            gaps = []
            for p in pset:
                t0 = p["samples"][0]["ts"]
                for s in p["samples"]:
                    el = (s["ts"]-t0)/1e9
                    if b0<=el<b1 and s.get("gap") is not None and fin(s["gap"]):
                        gaps.append(s["gap"])
            if gaps: out.append((bl,len(gaps),sum(gaps)/len(gaps)))
        return out

    dw=decay_for(W); dl=decay_for(L)
    if dw: print("  赢家 edge 衰减:  "+"  ".join(f"{bl}:{g:+.3f}(n{n})" for bl,n,g in dw))
    if dl: print("  输家 edge 衰减:  "+"  ".join(f"{bl}:{g:+.3f}(n{n})" for bl,n,g in dl))
    if not dw and not dl: print("  轨迹不足 → 等累积")
    J["alpha_decay"] = {
        "winners":[{"bin":bl,"n":n,"mean_edge":round(g,4)} for bl,n,g in dw],
        "losers": [{"bin":bl,"n":n,"mean_edge":round(g,4)} for bl,n,g in dl],
    }

    # ── Footer ───────────────────────────────────────────────────────────────
    print("="*80)
    print(f"独立结算盘 {len(positions)} (曾水下 {len(drew)}). 越积越准。")
    print("读法: B2 判别力强的信号=离场该看的(数据说话); B3 看该信号多少值还值得等等盘;")
    print("      B4 看该信号离场阈值的省产损/卖飞权衡; B5 edge衰减到0=alpha吃完该离。结论与阈值我们一起定, 脚本只摆信息。")

    if json_mode:
        J["b2_discrimination"] = [
            {"key":k,"family":fam(k),"disc":round(d,4),"auc":round(au,4),
             "direction":"winner_high" if au>0.5 else "winner_low",
             "n_win_pos":nw,"n_loss_pos":nl}
            for d,k,au,wm,lm,nw,nl in disc
        ] if disc else []
        J["caveats"] = [
            f"独立样本=结算仓数(n={len(positions)})非帧数; tape每盘多帧但显著性由仓数决定",
            "B2/B2b/B4 多因样本不足跳过; B4阈值是in-sample双重选择不可直接当离场参数",
            f"positions={len(positions)}(赢{len(W)}/输{len(L)}); OOS须≥60仓含20+水下才能定离场参数",
        ]
        print("\n===JSON_BEGIN===")
        print(json.dumps(J, ensure_ascii=False, default=lambda o: None))
        print("===JSON_END===")


if __name__ == "__main__":
    main()
