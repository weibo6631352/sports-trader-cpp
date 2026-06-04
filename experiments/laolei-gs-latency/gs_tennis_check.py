#!/usr/bin/env python3
# 查 Goalserve inplay-tennis 对法网/WTA 大牌的覆盖 + bet365 赔率
import gzip
import json
import urllib.request

req = urllib.request.Request("http://inplay.goalserve.com/inplay-tennis.gz",
                             headers={"User-Agent": "Mozilla/5.0"})
raw = urllib.request.urlopen(req, timeout=12).read()
d = json.loads(gzip.decompress(raw).decode())
evs = d.get("events", {})

print("=== 法网/WTA 大牌命中 ===")
hit = 0
KEYS = ["kostyuk", "andreeva", "shnaider", "chwal", "sabalenka", "swiatek", "gauff"]
for eid, e in evs.items():
    info = e.get("info", {})
    core = e.get("core", {})
    name = info.get("name", "")
    league = info.get("league", "")
    nm = name.lower()
    lg = league.lower()
    if any(k in nm for k in KEYS) or "roland" in lg or "french" in lg or "wta" in lg or "atp" in lg:
        nodds = len(e.get("odds", {}))
        print("  %-40s | %-24s | %s %s | stopped=%s odds_mkts=%s" % (
            name[:40], league[:24], info.get("period"), info.get("score"),
            core.get("stopped"), nodds))
        hit += 1
print("  命中 %d 场" % hit if hit else "  没命中 (法网可能没在打/名字不同)")

print("\n=== 所有 league 名 (看 Goalserve 怎么标大满贯) + 活跃场数 ===")
from collections import Counter
lgc = Counter()
for eid, e in evs.items():
    core = e.get("core", {})
    if core.get("stopped") == "1" or core.get("finished") == "1":
        continue
    lg = e.get("info", {}).get("league", "")
    if lg:
        lgc[lg] += 1
for lg, c in lgc.most_common(30):
    print("   %3d  %s" % (c, lg))
