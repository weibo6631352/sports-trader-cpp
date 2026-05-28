#!/usr/bin/env python3
"""
逆向 polymarket.com/sports/live 真实 XHR 调用
Owner: 老李
Date: 2026-05-28

约束:
- headless 匿名 (无登录, 无凭证)
- 不 click, 不 KYC, 仅 page.goto
- 抓所有 network requests + responses
- 输出 JSON 到 docs/RESEARCH/data/
"""

from playwright.sync_api import sync_playwright
import json
import os
import sys
from datetime import datetime
from urllib.parse import urlparse, parse_qsl

OUT_DIR = "/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data"
os.makedirs(OUT_DIR, exist_ok=True)

TARGET_URL = "https://polymarket.com/zh/sports/live"

# Polymarket 已知 API host
API_HOSTS = [
    "gamma-api.polymarket.com",
    "clob.polymarket.com",
    "data-api.polymarket.com",
    "polymarket.com/api",
    "ws-subscriptions",
    "ws-live-data",
]

requests_log = []
responses_log = []


def is_api_call(url: str) -> bool:
    return any(h in url for h in API_HOSTS)


def on_request(req):
    try:
        entry = {
            "url": req.url,
            "method": req.method,
            "headers": dict(req.headers),
            "post_data": req.post_data,
            "resource_type": req.resource_type,
            "timestamp_utc": datetime.utcnow().isoformat() + "Z",
        }
        requests_log.append(entry)
    except Exception as e:
        print(f"[req-err] {e}", file=sys.stderr)


def on_response(resp):
    try:
        url = resp.url
        if not is_api_call(url):
            return
        body_snippet = None
        try:
            # 只对 API 调用拿 body, 防止下载大资源
            if resp.request.resource_type in ("xhr", "fetch", "document"):
                text = resp.text()
                body_snippet = text[:2000] if text else None
        except Exception:
            body_snippet = None
        responses_log.append({
            "url": url,
            "status": resp.status,
            "headers": dict(resp.headers),
            "body_snippet": body_snippet,
            "timestamp_utc": datetime.utcnow().isoformat() + "Z",
        })
    except Exception as e:
        print(f"[resp-err] {e}", file=sys.stderr)


def main():
    start_utc = datetime.utcnow().isoformat() + "Z"
    print(f"[start] {start_utc} target={TARGET_URL}")

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        context = browser.new_context(
            user_agent="Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
                       "AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/120.0.0.0 Safari/537.36",
            locale="zh-CN",
        )
        page = context.new_page()
        page.on("request", on_request)
        page.on("response", on_response)

        try:
            page.goto(TARGET_URL, wait_until="domcontentloaded", timeout=60000)
        except Exception as e:
            print(f"[goto-warn] {e}", file=sys.stderr)

        # 等 networkidle 但容错
        try:
            page.wait_for_load_state("networkidle", timeout=30000)
        except Exception as e:
            print(f"[idle-warn] {e}", file=sys.stderr)

        # 多等 8 秒看延迟 XHR
        page.wait_for_timeout(8000)

        # 截图 + 当前 URL (验证是否被重定向)
        final_url = page.url
        title = page.title()
        try:
            page.screenshot(path=os.path.join(OUT_DIR, "polymarket-sports-live.png"), full_page=False)
        except Exception:
            pass

        browser.close()

    end_utc = datetime.utcnow().isoformat() + "Z"

    api_requests = [r for r in requests_log if is_api_call(r["url"])]

    # 提取每个 API 调用的 query 参数
    parsed_api = []
    for r in api_requests:
        u = urlparse(r["url"])
        qs = dict(parse_qsl(u.query, keep_blank_values=True))
        parsed_api.append({
            "url": r["url"],
            "method": r["method"],
            "host": u.netloc,
            "path": u.path,
            "query_params": qs,
            "post_data": r["post_data"],
            "resource_type": r["resource_type"],
            "timestamp_utc": r["timestamp_utc"],
            "key_headers": {
                k: v for k, v in r["headers"].items()
                if k.lower() in ("origin", "referer", "user-agent",
                                 "authorization", "x-api-key", "poly-address")
            },
        })

    out = {
        "meta": {
            "owner": "laoli",
            "target": TARGET_URL,
            "final_url": final_url,
            "page_title": title,
            "start_utc": start_utc,
            "end_utc": end_utc,
            "playwright_version": "1.60.0",
            "anonymous": True,
        },
        "stats": {
            "total_requests": len(requests_log),
            "api_requests": len(api_requests),
            "api_responses": len(responses_log),
        },
        "api_requests": parsed_api,
        "api_responses": responses_log,
        "all_requests_brief": [
            {"url": r["url"], "method": r["method"], "type": r["resource_type"]}
            for r in requests_log
        ],
    }

    out_path = os.path.join(OUT_DIR, "polymarket-sports-live-xhr.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)

    print(f"[done] total={len(requests_log)} api={len(api_requests)} -> {out_path}")
    print(f"[final_url] {final_url}")
    print(f"[title] {title}")

    # 控制台打印关键 API URL
    print("\n=== API CALLS (sequence) ===")
    for i, r in enumerate(parsed_api):
        print(f"[{i:02d}] {r['method']} {r['host']}{r['path']}")
        if r["query_params"]:
            for k, v in r["query_params"].items():
                print(f"      ?{k}={v}")


if __name__ == "__main__":
    main()
