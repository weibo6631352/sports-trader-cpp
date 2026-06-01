#!/usr/bin/env python3
"""前端静态服务 (SolidJS SPA) — 多线程 + SPA fallback + HTML no-cache。

修复 (2026-06-01, 老板「前端断断的很不稳定」诊断):
  - 单线程 TCPServer(backlog=5) → 跨洋并发请求被串行/拒绝 = "断断" 根因 → ThreadingHTTPServer + backlog 128。
  - end_headers 在错误响应(self.path 未设)时抛 AttributeError 崩连接 → getattr 兜底。
  - SimpleHTTPRequestHandler 对 SPA 路由返 404(深链/刷新) → 非文件路径回退 index.html。

用法: serve_frontend.py [dist_dir] [port]   (默认 ../frontend/dist  :8081)
"""
import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

_HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(_HERE, "..", "frontend", "dist")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8081


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIST, **kwargs)

    def end_headers(self):
        # self.path 在错误响应路径可能未设 → getattr 兜底, 绝不抛断连接。
        p = getattr(self, "path", "/").split("?")[0]
        last = p.rsplit("/", 1)[-1]
        if p.endswith(".html") or p == "/" or "." not in last:
            self.send_header("Cache-Control", "no-cache, no-store, must-revalidate")
        else:
            self.send_header("Cache-Control", "public, max-age=31536000, immutable")
        super().end_headers()

    def send_head(self):
        # SPA fallback: 请求的不是真实文件且看着像路由(无扩展名) → 回退 index.html。
        p = self.path.split("?")[0]
        fs_path = self.translate_path(p)
        if not os.path.isfile(fs_path) and "." not in p.rsplit("/", 1)[-1]:
            self.path = "/index.html"
        return super().send_head()

    def log_message(self, fmt, *args):  # 静音逐请求日志 (web.log 只留错误)
        pass


class Server(ThreadingHTTPServer):
    daemon_threads = True       # 请求线程随主进程退出, 不挂起
    request_queue_size = 128    # backlog 5→128: 跨洋并发不再被拒
    allow_reuse_address = True


if __name__ == "__main__":
    if not os.path.isdir(DIST):
        sys.stderr.write(f"[serve_frontend] dist 目录不存在: {DIST}\n")
        sys.exit(1)
    with Server(("0.0.0.0", PORT), Handler) as httpd:
        sys.stderr.write(f"[serve_frontend] serving {DIST} on :{PORT} (threaded, SPA fallback)\n")
        sys.stderr.flush()
        httpd.serve_forever()
