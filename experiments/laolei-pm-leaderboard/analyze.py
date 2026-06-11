#!/usr/bin/env python3
"""Rank top traders by 'steady upward last month'. Reads data.json from fetch.py."""
import json

d = json.load(open("data.json"))

def fmt(x, w=12):
    if x is None: return " "*w
    return f"{x:>{w},.0f}"

# classify each trader's last-month curve shape
rows = []
for t in d:
    m = t["month"]; life = t["life"]
    if not m:
        rows.append((t, None)); continue
    delta = m["delta"]
    maxdd = m["maxdd"]
    up_frac = m["up_frac"]
    # steady-upward score: reward big positive monthly delta with low intramonth drawdown
    # calmar-ish (delta / drawdown), tempered, * up-day fraction
    calmar = delta / (maxdd + 1e3) if delta > 0 else (delta/1e6)
    score = (delta if delta>0 else 0) * up_frac / (1 + maxdd/ (abs(delta)+1e3))
    rows.append((t, dict(delta=delta, maxdd=maxdd, up_frac=up_frac,
                         calmar=calmar, score=score,
                         life_maxdd_pct=(life or {}).get("life_maxdd_pct"))))

print("="*135)
print("ALL-TIME LEADERBOARD TOP 50  —  last-month curve behaviour")
print("="*135)
print(f"{'#':>2} {'name':<16} {'alltime$':>12} {'month_Δ$':>12} {'mo_maxDD$':>11} {'up_d%':>6} {'calmar':>7} {'life_maxDD%':>11}  shape")
print("-"*135)
for t, r in rows:
    name = (t["name"] or "?")[:16]
    at = t["alltime_pnl"]
    if r is None:
        print(f"{t['rank']:>2} {name:<16} {fmt(at)} {'(no curve)':>12}")
        continue
    # shape label
    delta, dd, uf = r["delta"], r["maxdd"], r["up_frac"]
    if abs(delta) < 0.01*abs(at) and abs(delta) < 50000:
        shape = "FLAT (dormant)"
    elif delta <= 0:
        shape = "DOWN / giving back"
    elif r["calmar"] and r["calmar"] > 8 and uf >= 0.55:
        shape = "*** STEADY CLIMB ***"
    elif delta > 0 and r["calmar"] and r["calmar"] > 3:
        shape = "up, some chop"
    elif delta > 0:
        shape = "up but lumpy/volatile"
    else:
        shape = "?"
    lm = r["life_maxdd_pct"]
    lm_s = f"{lm*100:>10.1f}%" if lm is not None else " "*11
    cal = r["calmar"]
    cal_s = f"{cal:>7.1f}" if cal is not None and cal==cal and abs(cal)<1e6 else f"{'inf':>7}"
    print(f"{t['rank']:>2} {name:<16} {fmt(at)} {fmt(delta)} {fmt(dd,11)} {uf*100:>5.0f}% {cal_s} {lm_s}  {shape}")

# steady climbers ranked
print()
print("="*135)
print("STEADY CLIMBERS (positive month, ranked by steady-score)")
print("="*135)
climb = [(t,r) for t,r in rows if r and r["delta"]>0]
climb.sort(key=lambda x: x[1]["score"], reverse=True)
print(f"{'name':<16} {'wallet':<44} {'month_Δ$':>12} {'maxDD$':>10} {'up%':>5} {'calmar':>7}")
for t, r in climb[:15]:
    cal=r["calmar"]; cal_s=f"{cal:>7.1f}" if abs(cal)<1e6 else "inf"
    print(f"{(t['name'] or '?')[:16]:<16} {t['wallet']:<44} {r['delta']:>12,.0f} {r['maxdd']:>10,.0f} {r['up_frac']*100:>4.0f}% {cal_s}")
print()
print("TOP-8 wallets for deep dive:")
print(" ".join(t["wallet"] for t,_ in climb[:8]))
