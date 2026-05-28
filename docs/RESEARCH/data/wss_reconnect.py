#!/usr/bin/env python3
"""老陈 — WSS 重连恢复测试.

流程: 连5次, 每次连上+订阅+收到首条消息后主动关闭, 记录 (open_ms, first_msg_ms, close_ms).
求 reconnect-to-first-msg 中位数.
"""
import asyncio, json, ssl, sys, time, websockets, pathlib

TOKENS = [
    "78433024518676680431174478322854148606578065650008220678402966840627347604025",
    "50346565575310273995396997144874891836871065259829083228393044602519086496922",
    "62125911194459356377883400729034971285539783764180402613717505645465050447006",
]
URL = "wss://ws-subscriptions-clob.polymarket.com/ws/market"
OUT = pathlib.Path("/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data/laochen-network-bench-wss-reconnect.csv")


async def cycle(idx, fout):
    sub = json.dumps({"type": "Market", "assets_ids": TOKENS})
    t0 = time.perf_counter()
    open_ms = first_msg_ms = close_ms = None
    try:
        async with websockets.connect(URL, ssl=ssl.create_default_context(), open_timeout=8, ping_interval=None) as ws:
            open_ms = (time.perf_counter() - t0) * 1000
            await ws.send(sub)
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=10)
                first_msg_ms = (time.perf_counter() - t0) * 1000
            except asyncio.TimeoutError:
                first_msg_ms = -1
            await ws.close()
            close_ms = (time.perf_counter() - t0) * 1000
    except Exception as e:
        fout.write(f"{idx},error,0,0,0,{type(e).__name__}\n")
        return
    fout.write(f"{idx},ok,{open_ms:.2f},{first_msg_ms:.2f},{close_ms:.2f},\n")


async def main():
    with OUT.open("w") as f:
        f.write("cycle,status,open_ms,first_msg_ms,close_ms,note\n")
        for i in range(5):
            await cycle(i, f)
            f.flush()
            await asyncio.sleep(1.5)
    print(f"wrote {OUT}")


asyncio.run(main())
