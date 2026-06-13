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
# 持仓 mark 概览 (赢面/水下)
if pos:
    under = sum(1 for p in pos if p.get("pnl_unrealized", 0) < 0)
    print(f"持仓 MTM: {len(pos)-under} 浮盈 / {under} 水下" +
          (" — " + " ".join(f"{p.get('outcome')}{p.get('net_qty',0):.1f}@{p.get('avg_entry_price',0):.2f}→{p.get('mark_price',0):.2f}" for p in pos[:6])))
