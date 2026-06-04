#!/usr/bin/env python3
# lead_lag.py — 测 Polymarket 价格相对 bet365(经Goalserve) 的领先/滞后
#
# 老板 2026-06-04「先测 PM 滞后 bet365 多少」: 决定 odds-edge 在我们 2.3s 数据劣势下是否还存在。
#   逻辑: 若 PM 滞后 bet365 > 我们 2.3s 劣势 → edge 真存在, 追低延迟(bet365直采)才值;
#         若 PM 几乎不滞后(与 bet365 同步动) → 任何 bet365 数据都没用, 该换策略。
#
# 数据: paper_server feature capture (/data/ml_capture/quotes.jsonl), 每匹配市场每 tick 一行,
#   含 g_bm_inplay_fair (bet365 de-vig) + market_mid (PM) + as_of_ts_ns。复用生产 de-vig+匹配+采集。
#
# 方法: 每市场建 (bet365_fair, pm_mid) 时序 → 1s 网格 ffill → 一阶差分 → 互相关。
#   Δbet365(t) 与 Δpm(t+lag) 相关最高的 lag = PM 相对 bet365 的滞后 (正=PM 慢)。
#
# 跑: /home/ec2-user/sports-trader-cpp/.venv/bin/python3 lead_lag.py [quotes.jsonl] [start_epoch_sec]

import json
import sys
import urllib.request
from collections import defaultdict

import numpy as np

_LIQ_CACHE = {}


def fetch_liq(cond):
    """查 PM gamma 该 condition 的流动性 (筛掉没人交易的薄盘)。失败返 -1。"""
    if cond in _LIQ_CACHE:
        return _LIQ_CACHE[cond]
    try:
        req = urllib.request.Request(
            "https://gamma-api.polymarket.com/markets?condition_ids=" + cond,
            headers={"User-Agent": "Mozilla/5.0"})
        d = json.loads(urllib.request.urlopen(req, timeout=8).read().decode())
        m = d[0] if isinstance(d, list) and d else None
        liq = float(m.get("liquidity") or 0) if m else 0.0
    except Exception:
        liq = -1.0
    _LIQ_CACHE[cond] = liq
    return liq


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/data/ml_capture/quotes.jsonl"
    start_ts = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0  # 只看此 epoch 秒之后的行 (本次 daemon run)

    rows_by_cond = defaultdict(list)
    n_total = 0
    n_kept = 0
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                d = json.loads(line)
            except Exception:
                continue
            n_total += 1
            cond = d.get("condition_id") or d.get("cond") or ""
            bet365 = d.get("g_bm_inplay_fair")
            pm = d.get("market_mid")
            ts = d.get("as_of_ts_ns") or d.get("joint_as_of_ts_ns")
            if cond == "" or bet365 is None or pm is None or ts is None:
                continue
            try:
                bet365 = float(bet365)
                pm = float(pm)
                ts = float(ts) / 1e9  # ns → s
            except Exception:
                continue
            if not (0.0 < bet365 < 1.0 and 0.0 < pm < 1.0):
                continue
            if start_ts and ts < start_ts:
                continue
            rows_by_cond[cond].append((ts, bet365, pm))
            n_kept += 1

    print(f"读取 {n_total} 行, 有效配对 {n_kept} 行, 涉及 {len(rows_by_cond)} 个市场")

    MAX_LAG = 8  # 秒, 互相关搜索范围 ±8s
    results = []  # (cond, n, bet365_std, best_lag, best_corr, corr_at_0)

    for cond, rows in rows_by_cond.items():
        rows.sort()
        ts = np.array([r[0] for r in rows])
        b = np.array([r[1] for r in rows])
        p = np.array([r[2] for r in rows])
        # 去重时间 (同 ts 取最后)
        if len(ts) < 30:
            continue
        # bet365 必须真有变动 (排除常数 0.5 占位 / 不动盘)
        if np.std(b) < 0.004 or np.std(p) < 0.002:
            continue
        # 1s 网格 ffill (step interpolation)
        t0, t1 = ts[0], ts[-1]
        if t1 - t0 < 60:  # 至少 1 分钟
            continue
        grid = np.arange(t0, t1, 1.0)
        idx = np.searchsorted(ts, grid, side="right") - 1
        idx = np.clip(idx, 0, len(ts) - 1)
        bg = b[idx]
        pg = p[idx]
        db = np.diff(bg)
        dp = np.diff(pg)
        if np.std(db) < 1e-6 or np.std(dp) < 1e-6:
            continue

        # 互相关: 对每个 lag, corr(db[t], dp[t+lag]); lag>0 = pm 滞后 bet365
        best_lag, best_corr, corr0 = 0, -2.0, 0.0
        for lag in range(-MAX_LAG, MAX_LAG + 1):
            if lag >= 0:
                x = db[: len(db) - lag] if lag > 0 else db
                y = dp[lag:]
            else:
                x = db[-lag:]
                y = dp[: len(dp) + lag]
            n = min(len(x), len(y))
            if n < 20:
                continue
            x = x[:n]
            y = y[:n]
            if np.std(x) < 1e-9 or np.std(y) < 1e-9:
                continue
            c = float(np.corrcoef(x, y)[0, 1])
            if lag == 0:
                corr0 = c
            if c > best_corr:
                best_corr, best_lag = c, lag
        liq = fetch_liq(cond)
        results.append((cond, len(rows), float(np.std(b)), best_lag, best_corr, corr0, liq))

    if not results:
        print("\n没有足够变动的市场可测 (bet365 fair 不动 / 样本不够 / 时长不足)。")
        print("→ 多跑几分钟, 或当前 in-play 盘 bet365 太静。")
        return

    print(f"\n{'condition':<26} {'n':>5} {'bet365σ':>8} {'best_lag':>9} {'corr@lag':>9} {'corr@0':>8} {'PM_liq':>9}")
    for cond, n, bstd, lag, corr, c0, liq in sorted(results, key=lambda r: -r[6]):
        print(f"{cond[:26]:<26} {n:>5} {bstd:>8.4f} {lag:>+8d}s {corr:>9.3f} {c0:>8.3f} {liq:>9.0f}")

    # ---- 关键: 只在【有流动性的盘】上下结论 (老板「是不是你找的市场不行」) ----
    LIQ_MIN = 20000.0  # PM 流动性下限; 薄盘(ITF等)价格是噪声, 测了无意义
    liquid = [r for r in results if r[6] >= LIQ_MIN]
    print(f"\n>>> 流动性 ≥ {LIQ_MIN:.0f} 的盘: {len(liquid)}/{len(results)} (只在这些上下结论) <<<")
    for cond, n, bstd, lag, corr, c0, liq in sorted(liquid, key=lambda r: -r[6]):
        print(f"    {cond[:26]:<26} lag={lag:>+3d}s corr={corr:>6.3f} liq={liq:>9.0f} n={n}")

    # 加权汇总: 流动盘 + corr 显著 (>0.15)
    OUR_BET365_DELAY = 2.3  # 实测 median: 我们经 Goalserve 看到 bet365 比真实变更晚 ~2.3s
    sig = [r for r in liquid if r[4] > 0.15]
    print(f"\n=== 汇总 (流动盘中 {len(sig)}/{len(liquid)} 个 corr>0.15 显著) ===")
    if sig:
        lags = np.array([r[3] for r in sig], dtype=float)
        weights = np.array([r[4] * np.log1p(r[1]) for r in sig])
        wmean_lag = float(np.sum(lags * weights) / np.sum(weights))
        obs = float(np.median(lags))  # 观测 lag (PM 相对我们【已延迟】的 bet365)
        true_lag = obs + OUR_BET365_DELAY  # 真实 PM 相对 bet365 (扣回我们 2.3s 延迟)
        print(f"观测 best_lag (PM vs 我们延迟的bet365): 中位={obs:+.1f}s  加权均值={wmean_lag:+.1f}s  范围=[{lags.min():+.0f},{lags.max():+.0f}]s")
        print(f"分布: PM 滞后(>0)={int(np.sum(lags>0))}  同步(=0)={int(np.sum(lags==0))}  PM 领先(<0)={int(np.sum(lags<0))}")
        print(f"\n推算真实 PM-vs-bet365 滞后 = 观测 + 我们2.3s延迟 = {true_lag:+.1f}s")
        print()
        if obs > 0.5:
            print(f"【判读】观测 PM 滞后 bet365 {obs:.1f}s > 0 → 即使现在(2.3s延迟)我们看到 bet365 信号时 PM 还没动 →")
            print(f"        当前路径就有 edge! 真实 PM 滞后 ~{true_lag:.1f}s。→ 现在就该交易 + 追低延迟扩大窗口。")
        elif true_lag > 0.8:
            print(f"【判读】当前 2.3s 延迟下没 edge (观测 lag {obs:.1f}s ≤ 0, PM 在我们眼里已同步/领先)。")
            print(f"        但真实 PM 滞后 bet365 ~{true_lag:.1f}s > 0 → **把 bet365 延迟砍到亚秒(直采)能解锁 ~{true_lag:.1f}s 窗口** →")
            print(f"        edge 存在但被我们 2.3s 数据劣势吃掉了。追 bet365 直采【值得】(若能解决 Cloudflare/住宅IP)。")
        else:
            print(f"【判读】真实 PM 滞后 bet365 仅 ~{true_lag:.1f}s (≤0.8s) → PM 与 bet365 近乎同步/PM领先 →")
            print(f"        即使瞬时拿到 bet365 也几乎无窗口。→ odds-edge 收敛套利对我们 -EV, 追延迟无用, 应换策略(做市carry/双头预测)。")
    else:
        print("无显著相关市场 — 可能 bet365/PM 在此窗口都太静, 或 PM 这些盘根本不随 bet365 动 (流动性/参与者不同)。")


if __name__ == "__main__":
    main()
