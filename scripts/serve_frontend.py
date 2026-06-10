#!/usr/bin/env python3
"""前端静态服务 (SolidJS SPA) — 多线程 + gzip + SPA fallback + HTML no-cache。

修复历史:
  2026-06-01 (老板「前端断断的很不稳定」): 单线程 TCPServer(backlog=5) → 跨洋并发被串行/拒绝 →
    ThreadingHTTPServer + backlog 128; end_headers 错误响应崩连接 → getattr 兜底; SPA 深链 404 → 回退 index.html。
  2026-06-04 (老板「页面初次加载好慢呀」): http.server 不压缩 → 跨洋传 293KB 未压缩 JS = 首屏慢根因。
    加按需 gzip (293KB→~82KB, ~3.5×): 客户端 Accept-Encoding 含 gzip 且文件可压(js/css/html/json/svg)即压。

用法: serve_frontend.py [dist_dir] [port]   (默认 ../frontend/dist  :7081)
"""
import gzip
import io
import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

_HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(_HERE, "..", "frontend", "dist")
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 7081

# 可压缩扩展名 (文本类; 图片/字体已压不再压)
_COMPRESSIBLE = (".js", ".mjs", ".css", ".html", ".json", ".svg", ".map", ".txt", ".xml", ".wasm")


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=DIST, **kwargs)

    @staticmethod
    def _cache_control(url_path: str) -> str:
        # HTML / 路由 / 无扩展名 → no-cache (始终拿最新 index.html 指向新 bundle)。
        # 带 hash 的静态资源 (assets/index-XXXX.js) → 永久强缓存 (immutable)。
        last = url_path.rsplit("/", 1)[-1]
        if url_path.endswith(".html") or url_path == "/" or "." not in last:
            return "no-cache, no-store, must-revalidate"
        return "public, max-age=31536000, immutable"

    def _serve(self, head_only: bool) -> None:
        url_path = self.path.split("?")[0]
        fs_path = self.translate_path(url_path)
        # SPA fallback: 非真实文件且看着像路由(无扩展名) → 回退 index.html。
        if not os.path.isfile(fs_path) and "." not in url_path.rsplit("/", 1)[-1]:
            url_path = "/index.html"
            fs_path = self.translate_path(url_path)
        if not os.path.isfile(fs_path):
            self.send_error(404, "Not Found")
            return
        try:
            with open(fs_path, "rb") as f:
                body = f.read()
        except OSError:
            self.send_error(404, "Not Found")
            return

        ext = os.path.splitext(fs_path)[1].lower()
        accepts_gzip = "gzip" in (self.headers.get("Accept-Encoding") or "").lower()
        use_gzip = accepts_gzip and ext in _COMPRESSIBLE and len(body) > 512
        if use_gzip:
            buf = io.BytesIO()
            with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=6) as gz:
                gz.write(body)
            body = buf.getvalue()

        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(fs_path))
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", self._cache_control(url_path))
        if use_gzip:
            self.send_header("Content-Encoding", "gzip")
        self.send_header("Vary", "Accept-Encoding")
        self.end_headers()
        if not head_only:
            self.wfile.write(body)

    def do_GET(self):
        try:
            self._serve(head_only=False)
        except (BrokenPipeError, ConnectionResetError):
            pass  # 客户端断开 (跨洋常见) — 不刷错误栈

    def do_HEAD(self):
        try:
            self._serve(head_only=True)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, fmt, *args):  # 静音逐请求日志
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
        sys.stderr.write(f"[serve_frontend] serving {DIST} on :{PORT} (threaded, gzip, SPA fallback)\n")
        sys.stderr.flush()
        httpd.serve_forever()
