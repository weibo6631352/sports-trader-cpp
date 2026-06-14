#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/monitor.py
# owner: 老雷 | 2026-06-14 | 非生产盯盘快照 (15min 盯盘循环用; CLAUDE.md 允许离线脚本)
#
# 一条命令出实况全貌: live API(/healthz /account /positions) + 账本健康(fills/settle/gate 分布 + 出单率)。
# 替代脆弱的 shell grep 转义 (cycle-1 实测 grep \"buy\":1 转义失败误报 0)。用 python json, 数字准。
# 用法: python3 monitor.py [api_base=http://127.0.0.1:7080] [data_dir=data/ml_capture]
import sys, json, urllib.request, collections

API  = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:7080"
DATA = sys.argv[2] if len(sys.argv) > 2 else "data/ml_capture"

def get(ep):
    try: return json.load(urllib.request.urlopen(API + ep, timeout=5))
    except Exception as e: return {"_err": str(e)[:50]}

def jl(path):
    rows = []
    try:
        for l in open(path):
            l = l.strip()
            if l:
                try: rows.append(json.loads(l))
                except: pass
    except FileNotFoundError: pass
    return rows

fills  = jl(f"{DATA}/fills_journal.jsonl")
buys   = [r for r in fills if r.get("buy") == 1 and r.get("close") != 1 and r.get("type") != "version"]
closes = [r for r in fills if r.get("close") == 1]
settle = [r for r in closes if r.get("exit") == "settlement"]
gb     = jl(f"{DATA}/gate_blocks.jsonl")
vers   = [r for r in fills if r.get("type") == "version"]
# 结算 outcome (分析就绪判定用): drop -1 / parse_ok=0
_st    = jl(f"{DATA}/quotes.jsonl.settlements.jsonl")
outc   = {s["condition_id"]: s.get("settlement_value") for s in _st
          if s.get("parse_ok") != 0 and s.get("settlement_value", -1) != -1}

hz = get("/healthz"); acct = get("/api/v1/account").get("account", {}); pos = get("/api/v1/positions").get("positions", [])
gd = collections.Counter(g.get("gate") for g in gb)

# 健康
threads = hz.get("threads", {})
dead = [t for t, s in threads.items() if s != "alive"]
print(f"健康: {'✓存活' if hz.get('ok') else '⚠'} uptime={hz.get('uptime_sec','?')}s" +
      (f" ⚠死线程{dead}" if dead else "") + (f" API_ERR={hz.get('_err')}" if hz.get('_err') else ""))
# 版本
if vers:
    v = vers[-1]; print(f"版本: git={v.get('git')} min_edge={v.get('sharp_only_min_edge')} min_open_fair={v.get('min_open_fair')} (标签{len(vers)}条)")
# 账本/资金
print(f"成交: {len(buys)} buy / {len(closes)} close (其中结算 {len(settle)})  持仓: {len(pos)}")
print(f"资金: 现金 ${acct.get('cash_available',0):.2f} | 净值 ${acct.get('equity',0):.2f} | "
      f"realized ${acct.get('cum_realized_pnl',0):.2f} | 未实现 ${acct.get('cum_unrealized_pnl',0):.2f} | 净PnL ${acct.get('net_pnl',0):.2f}")
# 拒单 + 出单率
print(f"拒单: {len(gb)} → " + (", ".join(f"{g}:{n}" for g, n in gd.most_common()) or "无"))
attempts = len(buys) + len(gb)
if attempts:
    print(f"出单率: {len(buys)}/{attempts} = {100*len(buys)/attempts:.1f}% (过闸/总尝试; 低=策略挑剔/市场efficient)")
# 分析就绪判定 (跨重启偏差已修: exit_research 从轨迹建仓, 不依赖 fills 进场)
blk_settled = sum(1 for g in gb if g.get("cond") in outc)
ent_settled = sum(1 for b in buys if b.get("cond") in outc)
pm = "✓就绪" if blk_settled >= 30 else f"✗差{max(0,30-blk_settled)}"
print(f"分析就绪: settlements {len(outc)} | 被挡已结算 {blk_settled} | 进场已结算 {ent_settled} "
      f"→ param_research {pm} (被挡已结算≥30)")
# 三器就绪全貌 (老板「在等数据够量出可信结论」→ 把等待变可观测: 各分析器离可信结论还差多少)
mt           = jl(f"{DATA}/market_tape.jsonl")
mt_conds     = set(r.get("cond") for r in mt if r.get("cond"))
# 持有过的盘 = tape 中 held==1 的帧 (position_path 已退役 2026-06-14; exit_research 改用 fills×tape held 旗)
pp_conds     = set(r.get("cond") for r in mt if r.get("held") and r.get("cond"))
exit_settled = len(pp_conds & set(outc))                                    # exit_research: 持有过且已结算的盘
disc_settled = len(mt_conds & set(outc))                                    # market_discovery: 已结算的in-play宇宙盘
exr = "✓就绪" if exit_settled >= 6 else f"✗差{max(0,6-exit_settled)}"
dsc = "✓就绪" if disc_settled >= 6 else f"✗差{max(0,6-disc_settled)}"
print(f"   exit_research {exr} (有轨迹已结算盘 {exit_settled}/≥6) | "
      f"market_discovery {dsc} (宇宙已结算盘 {disc_settled}/{len(mt_conds)}, ≥6=可信; 当前market_tape {len(mt)}快照)")
print(f"   注: ✓就绪=过最低演示门; 真OOS结论要被挡≥150结算/进场结算够/宇宙跨多比赛时段。")
# 持仓 mark 概览 (赢面/水下) — 显示【水下最深】4 个 (风险仓/崩盘仓不漏看), 非任意前 N
if pos:
    under = sum(1 for p in pos if p.get("pnl_unrealized", 0) < 0)
    worst = sorted(pos, key=lambda p: p.get("pnl_unrealized", 0))[:4]
    print(f"持仓 MTM: {len(pos)-under} 浮盈 / {under} 水下; 水下最深: " +
          " ".join(f"{p.get('outcome')}{p.get('net_qty',0):.1f}@{p.get('avg_entry_price',0):.2f}→{p.get('mark_price',0):.2f}(${p.get('pnl_unrealized',0):+.1f})" for p in worst))
