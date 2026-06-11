#!/usr/bin/env python3
"""
Polymarket leaderboard steady-climber analysis (one-off data exploration).
Owner: 老雷 (GM)  last_review: 2026-06-10

Pipeline:
  1. all-time profit leaderboard top N   (lb-api /profit)
  2. per-trader last-month PnL curve      (user-pnl-api interval=1m fidelity=1d)
     + all-time curve                     (interval=all) for lifetime drawdown
  3. smoothness / steady-upward metrics
  4. deep-dive top steady climbers: activity + positions -> market mix & trade style

Output -> data.json (raw) consumed by analyze.py
"""
import json, time, sys, urllib.request, urllib.parse, math

UA = {"User-Agent": "Mozilla/5.0 research"}
def get(url, tries=4, timeout=20):
    last = None
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return json.loads(r.read().decode())
        except Exception as e:
            last = e; time.sleep(0.6*(i+1))
    print(f"  ! fail {url}: {last}", file=sys.stderr)
    return None

LB   = "https://lb-api.polymarket.com/profit?period=month&limit={n}"
PNL  = "https://user-pnl-api.polymarket.com/user-pnl?user_address={a}&interval={iv}&fidelity={f}"
ACT  = "https://data-api.polymarket.com/activity?user={a}&limit={n}&offset={o}"
POS  = "https://data-api.polymarket.com/positions?user={a}&limit=500&sortBy=CURRENT&sortDirection=DESC"

N_LB = int(sys.argv[1]) if len(sys.argv) > 1 else 100

def curve_metrics(c):
    """c = list of {t,p} cumulative-pnl points. returns dict of shape metrics."""
    if not c or len(c) < 3:
        return None
    p = [float(x["p"]) for x in c]
    delta = p[-1] - p[0]
    incs  = [p[i+1]-p[i] for i in range(len(p)-1)]
    up    = sum(1 for d in incs if d > 0)
    down  = sum(1 for d in incs if d < 0)
    # intramonth max drawdown of cumulative curve (peak-to-trough $)
    peak = p[0]; maxdd = 0.0
    for v in p:
        peak = max(peak, v)
        maxdd = max(maxdd, peak - v)
    # downside deviation of daily increments
    neg = [d for d in incs if d < 0]
    dd_dev = math.sqrt(sum(d*d for d in neg)/len(incs)) if incs else 0.0
    mean_inc = sum(incs)/len(incs) if incs else 0.0
    sortino = (mean_inc/dd_dev) if dd_dev > 1e-9 else (float('inf') if mean_inc>0 else 0.0)
    rng = max(p) - min(p)
    return dict(n=len(p), first=p[0], last=p[-1], delta=delta,
                up=up, down=down, up_frac=up/max(up+down,1),
                maxdd=maxdd, maxdd_vs_delta=(maxdd/delta if delta>0 else None),
                sortino=sortino, range=rng, pmin=min(p), pmax=max(p))

def main():
    print(f"[1] leaderboard top {N_LB} ...", file=sys.stderr)
    lb = get(LB.format(n=N_LB)) or []
    out = []
    print(f"[2] per-trader month curves ({len(lb)}) ...", file=sys.stderr)
    for i, t in enumerate(lb):
        a = t["proxyWallet"]
        m = get(PNL.format(a=a, iv="1m", f="1d"))
        al = get(PNL.format(a=a, iv="all", f="1d"))
        time.sleep(0.12)
        mm = curve_metrics(m) if m else None
        # lifetime drawdown from all-time curve
        life = None
        if al and len(al) > 3:
            p = [float(x["p"]) for x in al]
            peak=p[0]; dd=0.0
            for v in p:
                peak=max(peak,v); dd=max(dd, peak-v)
            life = dict(life_first=p[0], life_last=p[-1], life_peak=max(p),
                        life_min=min(p), life_maxdd=dd,
                        life_maxdd_pct=(dd/max(p) if max(p)>0 else None),
                        life_pts=len(p))
        rec = dict(rank=i+1, wallet=a, name=t.get("name") or t.get("pseudonym"),
                   alltime_pnl=t.get("amount"), month=mm, life=life,
                   month_curve=[[x["t"], round(float(x["p"]))] for x in (m or [])])
        out.append(rec)
        if (i+1) % 20 == 0:
            print(f"   ...{i+1}/{len(lb)}", file=sys.stderr)
    with open("data.json","w") as f:
        json.dump(out, f)
    print(f"[done] wrote data.json ({len(out)} traders)", file=sys.stderr)

if __name__ == "__main__":
    main()
