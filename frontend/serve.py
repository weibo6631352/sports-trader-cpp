#!/usr/bin/env python3
"""
serve.py — 观测看板本地静态文件服务器
owner: 小苏
last_review: 2026-05-29

注意: 本脚本仅供本地调试使用 (ADR-037 本地优先), 不上生产.
标准库 http.server + CORS 宽松 (loopback only).
"""

import http.server
import socketserver
import sys
import os

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
DIRECTORY = os.path.dirname(os.path.abspath(__file__))


class CORSRequestHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIRECTORY, **kwargs)

    def end_headers(self):
        # CORS: 允许前端从任意 localhost 端口访问 debug_api
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Cache-Control", "no-cache, no-store")
        super().end_headers()

    def log_message(self, fmt, *args):  # noqa: D401
        print(f"[serve] {self.address_string()} - {fmt % args}")


with socketserver.TCPServer(("127.0.0.1", PORT), CORSRequestHandler) as httpd:
    print(f"[serve] 观测看板 http://127.0.0.1:{PORT}/")
    print(f"[serve] stub 模式: http://127.0.0.1:{PORT}/?stub=1")
    print(f"[serve] Ctrl-C 停止")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[serve] 已停止")
