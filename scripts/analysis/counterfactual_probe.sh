cat > /tmp/cf.py <<'PYEOF'
import json,sys,urllib.request
mid,side,avg,qty,actual = sys.argv[1],sys.argv[2],float(sys.argv[3]),float(sys.argv[4]),float(sys.argv[5])
try:
    req=urllib.request.Request("https://gamma-api.polymarket.com/markets?condition_ids="+mid, headers={"User-Agent":"Mozilla/5.0","Accept":"application/json"})
    with urllib.request.urlopen(req, timeout=8) as r:
        m=json.load(r)[0]
    p=m.get("outcomePrices")
    if isinstance(p,str): p=json.loads(p)
    closed=m.get("closed")
    if closed and p:
        yes_won=float(p[0])>0.5
        we_won=(side=="YES")==yes_won
        cf = qty*(1.0-avg) if we_won else -qty*avg
        print("%s %s 入%.3f 割=%.2f | 已结算 %s | 反事实=%.2f Δ=%.2f" % (mid[:10],side,avg,actual,"我方赢" if we_won else "我方输",cf,cf-actual))
    else:
        print("%s %s 入%.3f 割=%.2f | 未结算 (yes现价=%s)" % (mid[:10],side,avg,actual,p[0] if p else "?"))
except Exception as e:
    print("%s 查询失败: %s" % (mid[:10],str(e)[:60]))
PYEOF
while read mid side avg qty actual; do python3 /tmp/cf.py "$mid" "$side" "$avg" "$qty" "$actual"; done < /tmp/cut_mkts.txt
