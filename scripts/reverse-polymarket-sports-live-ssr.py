#!/usr/bin/env python3
"""
拿 polymarket.com/zh/sports/live 的初始 HTML, 提取 __NEXT_DATA__
看 SSR (Next.js server-side props) 用了什么 gamma 调用拿 events list.
"""

from playwright.sync_api import sync_playwright
import json
import os
import re

OUT_DIR = "/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data"
os.makedirs(OUT_DIR, exist_ok=True)
TARGET = "https://polymarket.com/zh/sports/live"

with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    ctx = browser.new_context(
        user_agent="Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    )
    page = ctx.new_page()
    try:
        page.goto(TARGET, wait_until="domcontentloaded", timeout=60000)
    except Exception as e:
        print(f"warn: {e}")
    html = page.content()
    browser.close()

# 保存 HTML
with open(os.path.join(OUT_DIR, "polymarket-sports-live.html"), "w", encoding="utf-8") as f:
    f.write(html)

# 提取 __NEXT_DATA__
m = re.search(r'<script id="__NEXT_DATA__" type="application/json">(.*?)</script>', html, re.S)
if not m:
    print("ERROR: no __NEXT_DATA__ found")
    # 试备用 RSC payload
    rsc_keys = re.findall(r'self\.__next_f\.push\(\[1,"(.{0,300})"', html)
    print(f"RSC keys count: {len(rsc_keys)}")
    for k in rsc_keys[:5]:
        print(repr(k))
else:
    data = json.loads(m.group(1))
    with open(os.path.join(OUT_DIR, "polymarket-sports-live-NEXT_DATA.json"), "w") as f:
        json.dump(data, f, indent=2)
    print(f"__NEXT_DATA__ saved, keys: {list(data.keys())}")
    # 看 pageProps
    pp = data.get("props", {}).get("pageProps", {})
    print(f"pageProps keys: {list(pp.keys())}")
    print(f"page name: {data.get('page')}")
    print(f"query: {data.get('query')}")

# 也提取 next_f flight payload (App Router)
flight_chunks = re.findall(r'self\.__next_f\.push\(\[1,(".*?")\]\)', html, re.S)
print(f"flight chunks: {len(flight_chunks)}")
if flight_chunks:
    with open(os.path.join(OUT_DIR, "polymarket-sports-live-flight.txt"), "w") as f:
        for c in flight_chunks:
            f.write(c + "\n---CHUNK---\n")
    # 在 flight 里找 gamma URL
    full = "\n".join(flight_chunks)
    gamma_urls = re.findall(r'(gamma[-_]api[^"\\\\]+)', full)
    print(f"gamma URLs found in flight: {len(set(gamma_urls))}")
    for u in set(gamma_urls):
        print(f"  {u[:200]}")
    # 找 tag_slug / tag references
    tag_refs = re.findall(r'(tag[_-]?slug[^,}\\\\]{0,100})', full)
    print(f"tag_slug references: {len(set(tag_refs))}")
    for t in set(tag_refs)[:20]:
        print(f"  {t}")
    # 找 sports / live keywords
    live_refs = re.findall(r'("?live"?[: ][^,}]{0,80})', full[:200000])
    print(f"'live' references (first 20):")
    for l in list(set(live_refs))[:20]:
        print(f"  {l[:150]}")
