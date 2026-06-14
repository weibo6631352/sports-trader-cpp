#!/usr/bin/env python3
# experiments/laolei-paper-replay-stats/backfill_settlements.py
# owner: 老雷 | 2026-06-14 | 非生产·离线结算补全 (CLAUDE.md 允许离线脚本)
#
# 问题 (2026-06-14 实测确认): live 结算 poller 系统性丢【orphan 结算】—— 比赛结束后盘掉出 discovery
#   → token_map 重建丢弃 → SettlementPoller 不再轮询该盘 → 已 resolve 的结算永不入库
#   (记忆 orphan-settle-entry-leadlag-2026-06-10 的坑仍在咬)。实测: 碰过17盘只捕获10结算,
#   抽查2个"碰过>3h未结算"全部已在 CLOB resolve = 100% 真 orphan。这直接饿着三个分析器
#   (绑定约束=独立结算盘数, 越丢越难出可信结论)。
#
# 安全修 (零生产风险): 不碰运行中的 trader / 不改 live 结算 poller / 不重启 / 不写 live 文件。
#   纯离线: 读现有结算 + 查 CLOB(权威 market 状态) → 把被丢的 orphan 按【正确 YES-canonical】
#   补成 settlement 记录 → 写【merged 文件】给分析脚本读。可复用(每次分析前跑一遍, 自动追回新 orphan)。
#
# YES-canonical (2026-06-14 实测验证): 我们的 yes_tok(market_tape/position_path/fills 的 tok 字段)
#   == CLOB market tokens[0] 的 token_id。settlement_value = 1 当 YES(=yes_tok, 缺则 tokens[0])是 winner。
#   验证: orphan CoD 盘 我们yes_tok=2142..=CLOB tokens[0]=LA Thieves=winner → sv=1 ✓。
#   ⚠ side-label 是本项目踩过的坑(拿错边→赢输标反), 故此映射经实测确认非假设, 且优先用我们自己的 tok 锚定。
#
# 用法: python3 backfill_settlements.py <data_dir> [--out <merged.jsonl>] [--sleep 0.25] [--dry]
import sys, json, time, urllib.request, urllib.error, collections

UA = {"User-Agent": "Mozilla/5.0"}

def jl(p):
    out = []
    try:
        for l in open(p):
            l = l.strip()
            if l:
                try: out.append(json.loads(l))
                except: pass
    except FileNotFoundError: pass
    return out

def clob_market(cond):
    req = urllib.request.Request(f"https://clob.polymarket.com/markets/{cond}", headers=UA)
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)

def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]
    flags = sys.argv[1:]
    if not a:
        print("用法: backfill_settlements.py <data_dir> [--out <merged.jsonl>] [--sleep 0.25] [--dry]"); return
    D = a[0].rstrip("/")
    out_p = flags[flags.index("--out")+1] if "--out" in flags else f"{D}/quotes.jsonl.settlements_merged.jsonl"
    sleep = float(flags[flags.index("--sleep")+1]) if "--sleep" in flags else 0.25
    dry = "--dry" in flags

    settle_rows = jl(f"{D}/quotes.jsonl.settlements.jsonl")
    have = set(s.get("condition_id") for s in settle_rows)       # 已有记录(不论 parse_ok)的 cond, 不重查
    # 我们碰过的盘 + 自带 yes_tok (market_tape/fills 都带 tok=yes token; gate_blocks 无 tok)
    #   (position_path 已退役 2026-06-14: market_tape 覆盖全 in-play 盘 + fills 覆盖成交盘, 二者已够)
    yt = {}
    touched = set()
    for f in ("market_tape.jsonl", "fills_journal.jsonl"):
        for r in jl(f"{D}/{f}"):
            c = r.get("cond")
            if not c: continue
            touched.add(c)
            t = r.get("tok")
            if t and c not in yt: yt[c] = t
    for r in jl(f"{D}/gate_blocks.jsonl"):
        if r.get("cond"): touched.add(r["cond"])

    missing = [c for c in touched if c not in have]
    print(f"碰过盘 {len(touched)} | 已有结算记录 {len(have)} | 待查(碰过但无结算记录) {len(missing)}")
    recovered = []
    for i, cond in enumerate(missing):
        try:
            m = clob_market(cond)
        except Exception as e:
            print(f"  {cond[-8:]} 查询失败({str(e)[:40]}) → 跳过"); continue
        toks = m.get("tokens", []) or []
        closed = m.get("closed")
        winner = next((t for t in toks if t.get("winner") is True), None)
        if not closed or winner is None or not toks:
            continue  # 未结算/未 resolve → 不补 (还在比赛或裁决中)
        yes_id = yt.get(cond) or toks[0].get("token_id")          # 我们的 YES: 优先自己的 tok, 缺则 tokens[0]
        sv = 1 if str(yes_id) == str(winner.get("token_id")) else 0
        rec = {"condition_id": cond, "settlement_value": sv, "parse_ok": 1, "closed": True,
               "backfilled": True, "yes_anchor": ("our_tok" if cond in yt else "clob_tokens0"),
               "question": m.get("question"), "winner_outcome": winner.get("outcome")}
        recovered.append(rec)
        print(f"  ✓追回 {cond[-8:]} sv={sv} (YES={'我方tok' if cond in yt else 'tokens[0]'}, winner={winner.get('outcome')}) — {str(m.get('question'))[:50]}")
        if sleep > 0 and i < len(missing) - 1: time.sleep(sleep)

    print(f"追回 orphan 结算: {len(recovered)} (其中 sv=1 YES赢 {sum(r['settlement_value'] for r in recovered)} / sv=0 NO赢 {len(recovered)-sum(r['settlement_value'] for r in recovered)})")
    if dry:
        print("--dry: 不写文件。"); return
    # merged = 现有结算(原样) + 追回(去重: 现有优先)
    merged = list(settle_rows) + recovered
    with open(out_p, "w") as f:
        for r in merged: f.write(json.dumps(r, ensure_ascii=False) + "\n")
    n_settled_before = sum(1 for s in settle_rows if s.get("parse_ok") != 0 and s.get("settlement_value", -1) != -1)
    n_settled_after = n_settled_before + len(recovered)
    print(f"写 merged → {out_p}  (有效结算 {n_settled_before} → {n_settled_after}; +{len(recovered)})")
    print("用法: 三器把 settlements 参数指向此 merged 文件即纳入追回的 orphan。live 文件/trader 全程未动。")

if __name__ == "__main__":
    main()
