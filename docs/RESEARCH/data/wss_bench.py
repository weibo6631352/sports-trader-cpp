#!/usr/bin/env python3
"""老陈 — WSS 基准: 连接建立 + 心跳间隔 + 消息频率 + 重连恢复.

用法:
  wss_bench.py <name> <url> [--subscribe JSON] [--duration 30] [--reconnects 3]
输出 CSV: laochen-network-bench-wss-<name>.csv (event,t_rel_ms,size)
"""
import asyncio, json, sys, time, ssl, argparse, pathlib, websockets

OUT_DIR = pathlib.Path("/Users/wangweibo/code/sports-trader-cpp/docs/RESEARCH/data")


async def run_once(name, url, subscribe, duration, conn_idx, fout):
    t0 = time.perf_counter()
    try:
        ssl_ctx = ssl.create_default_context() if url.startswith("wss://") else None
        async with websockets.connect(url, ssl=ssl_ctx, open_timeout=10, ping_interval=None) as ws:
            t_open = (time.perf_counter() - t0) * 1000
            fout.write(f"conn{conn_idx},open,{t_open:.2f},0\n")
            if subscribe:
                await ws.send(subscribe)
                t_sub = (time.perf_counter() - t0) * 1000
                fout.write(f"conn{conn_idx},subscribe_sent,{t_sub:.2f},{len(subscribe)}\n")
            deadline = time.perf_counter() + duration
            msg_count = 0
            last_msg_t = None
            while time.perf_counter() < deadline:
                try:
                    msg = await asyncio.wait_for(
                        ws.recv(), timeout=max(0.1, deadline - time.perf_counter())
                    )
                except asyncio.TimeoutError:
                    break
                except websockets.ConnectionClosed as e:
                    t = (time.perf_counter() - t0) * 1000
                    fout.write(f"conn{conn_idx},closed,{t:.2f},{e.code}\n")
                    return
                msg_count += 1
                t = (time.perf_counter() - t0) * 1000
                size = len(msg) if isinstance(msg, (str, bytes)) else 0
                # 只记 size 不记内容 (合规)
                fout.write(f"conn{conn_idx},msg,{t:.2f},{size}\n")
                last_msg_t = t
            fout.write(f"conn{conn_idx},summary,{(time.perf_counter()-t0)*1000:.2f},{msg_count}\n")
    except Exception as e:
        t = (time.perf_counter() - t0) * 1000
        fout.write(f"conn{conn_idx},error,{t:.2f},{type(e).__name__}:{str(e)[:80]}\n")


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("url")
    ap.add_argument("--subscribe", default=None)
    ap.add_argument("--duration", type=float, default=30)
    ap.add_argument("--reconnects", type=int, default=2)
    args = ap.parse_args()

    out = OUT_DIR / f"laochen-network-bench-wss-{args.name}.csv"
    with out.open("w") as f:
        f.write("conn,event,t_rel_ms,size_or_code\n")
        for i in range(args.reconnects + 1):
            await run_once(args.name, args.url, args.subscribe, args.duration, i, f)
            f.flush()
            if i < args.reconnects:
                f.write(f"conn{i},reconnect_wait,0,0\n")
                await asyncio.sleep(1.0)
    print(f"wrote {out}")


if __name__ == "__main__":
    asyncio.run(main())
