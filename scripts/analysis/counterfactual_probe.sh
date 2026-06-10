cat > /tmp/cf.py <<'PYEOF'
import json,sys,urllib.request
mid,side,avg,qty,actual = sys.argv[1],sys.argv[2],float(sys.argv[3]),float(sys.argv[4]),float(sys.argv[5])
def fetch(url):
    req=urllib.request.Request(url, headers={"User-Agent":"Mozilla/5.0","Accept":"application/json"})
    with urllib.request.urlopen(req, timeout=8) as r:
        return json.load(r)
try:
    yes_won=None; state="?"
    try:
        c=fetch("https://clob.polymarket.com/markets/"+mid)
        toks=c.get("tokens") or []
        if toks and any(t.get("winner") for t in toks):
            yes_won=bool(toks[0].get("winner"))   # tokens[0]=outcome0=YES
            state="resolved"
        elif toks and toks[0].get("price") is not None:
            state="open yes=%.3f" % float(toks[0]["price"])
    except Exception:
        pass
    if yes_won is None and state.startswith("?"):
        rows=fetch("https://gamma-api.polymarket.com/markets?condition_ids="+mid)
        if rows:
            p=rows[0].get("outcomePrices")
            if isinstance(p,str): p=json.loads(p)
            if rows[0].get("closed") and p:
                yes_won=float(p[0])>0.5; state="resolved"
            elif p: state="open yes=%s" % p[0]
    if yes_won is not None:
        we_won=(side=="YES")==yes_won
        cf = qty*(1.0-avg) if we_won else -qty*avg
        print("%s %s 入%.3f 割=%.2f | 终局%s | 反事实=%.2f Δ(不割-割)=%+.2f" % (mid[:10],side,avg,actual,"我方赢" if we_won else "我方输",cf,cf-actual))
    else:
        print("%s %s 入%.3f 割=%.2f | %s" % (mid[:10],side,avg,actual,state))
except Exception as e:
    print("%s 查询失败: %s" % (mid[:10],str(e)[:60]))
PYEOF
TOT=0
while read mid side avg qty actual; do python3 /tmp/cf.py "$mid" "$side" "$avg" "$qty" "$actual"; done < /tmp/cut_mkts.txt
PYEOF2=""
