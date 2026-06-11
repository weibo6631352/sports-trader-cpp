#!/usr/bin/env python3
# capture_clob.py — PM CLOB WSS tick 采集 (book + trades) 给做市真回测
#   连 market channel, 订阅 token_ids, 每条消息带本地 recv_ts_ns 落 jsonl。
#   心跳 ping (防 idle 踢, 见 clob-wss-heartbeat 记忆) + 断线重连。
#   跑: timeout 1800 .venv/bin/python3 capture_clob.py
import json
import time
import sys

import websocket  # websocket-client

ASSETS = json.loads(sys.argv[1]) if len(sys.argv) > 1 else []
OUT = sys.argv[2] if len(sys.argv) > 2 else "data/ml_capture/clob_ticks.jsonl"
URL = "wss://ws-subscriptions-clob.polymarket.com/ws/market"

f = open(OUT, "a", buffering=1)
n = [0]


def on_open(ws):
    ws.send(json.dumps({"assets_ids": ASSETS, "type": "market"}))
    sys.stderr.write(f"[capture] subscribed {len(ASSETS)} assets\n")


def on_message(ws, msg):
    f.write(json.dumps({"r": time.time_ns(), "m": msg}, ensure_ascii=False) + "\n")
    n[0] += 1
    if n[0] % 500 == 0:
        sys.stderr.write(f"[capture] {n[0]} msgs\n")


def on_error(ws, err):
    sys.stderr.write(f"[capture] err {err}\n")


while True:
    try:
        ws = websocket.WebSocketApp(URL, on_open=on_open, on_message=on_message, on_error=on_error)
        ws.run_forever(ping_interval=10, ping_timeout=5)
    except Exception as e:
        sys.stderr.write(f"[capture] reconnect after {e}\n")
    time.sleep(2)
