#!/usr/bin/env python3
"""Parse __NEXT_DATA__ from curl-fetched HTML, extract React Query dehydrated state."""
import re
import json
import os

HTML = "/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/polymarket-sports-live-curl.html"
OUT = "/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data"

with open(HTML, encoding="utf-8") as f:
    html = f.read()

m = re.search(r'<script id="__NEXT_DATA__"[^>]*>(.*?)</script>', html, re.S)
if not m:
    print("not found"); raise SystemExit

raw = m.group(1)
data = json.loads(raw)

with open(os.path.join(OUT, "polymarket-sports-live-NEXT_DATA.json"), "w") as f:
    json.dump(data, f, indent=2, ensure_ascii=False)

print("top-level keys:", list(data.keys()))
print("page:", data.get("page"))
print("query:", data.get("query"))

pp = data.get("props", {}).get("pageProps", {})
print("pageProps keys:", list(pp.keys()))

dh = pp.get("dehydratedState", {})
queries = dh.get("queries", [])
print(f"\n=== React Query dehydrated queries: {len(queries)} ===")
for i, q in enumerate(queries):
    qkey = q.get("queryKey")
    state = q.get("state", {})
    dlen = len(json.dumps(state.get("data") or {}))
    print(f"[{i}] queryKey={qkey}  data_size={dlen}")

# Dump each queryKey + data preview
for i, q in enumerate(queries):
    print(f"\n--- query[{i}] ---")
    print("queryKey:", json.dumps(q.get("queryKey"), ensure_ascii=False))
    data_obj = q.get("state", {}).get("data")
    if data_obj is None:
        print("data: null")
        continue
    s = json.dumps(data_obj, ensure_ascii=False)
    print(f"data (first 1500):\n{s[:1500]}")
