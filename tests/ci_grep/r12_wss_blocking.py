#!/usr/bin/env python3
# tests/ci_grep/r12_wss_blocking.py — R-12 WSS event loop 阻塞反模式闸
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.2 W6 Wave 28
# 关联: CLAUDE.md §8 红线 R-12 (WebSocket event loop 同步 IO/锁 > 100us → P0)
#       docs/ADR/2026-05-28-gm-redline-websocket-non-blocking.md
#       docs/RESEARCH/laogao-pr-review-v1.2.md §2.4
#
# 扫描路径: src/ 下含 wss/ 或 ws_handler 的路径
#   - src/stcpp/polymarket/wss/**
#   - src/**/wss/**
#   - src/**/ws_handler*.cpp
#
# 4 反模式:
#   1. std::lock_guard / unique_lock / scoped_lock / shared_lock 在 WSS 路径
#      (spin < 100us 豁免: 加 // SPIN_OK 或 // LOCK_OK_SPIN_<100US 注释)
#   2. .blocking_read / .blocking_write / recv( / read_until(
#   3. synchronous_http / http_client::Get / http_client::Post / curl_easy_perform
#   4. fsync( / fdatasync(
#
# 豁免:
#   - 含 // SPIN_OK 的行 (spin_lock < 100us 实测通过)
#   - 含 // CI-EXEMPT: <理由> 的行 (需老郭 24h 仲裁)
#   - tests/** 路径下 (mock WSS handler 测试允许用锁)
#
# 用法:
#   python3 tests/ci_grep/r12_wss_blocking.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# WSS 路径判断                                                                  #
# --------------------------------------------------------------------------- #

def is_wss_path(path: Path, repo_root: Path) -> bool:
    """True if the file lives in a WSS-related source path."""
    rel = str(path.relative_to(repo_root))
    if rel.startswith("tests/"):
        return False
    return (
        "/wss/" in rel
        or "\\wss\\" in rel
        or re.search(r"ws_handler", rel) is not None
    )


# --------------------------------------------------------------------------- #
# 4 反模式正则                                                                   #
# --------------------------------------------------------------------------- #

_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    (
        "R-12-1 std::lock_guard/unique_lock (WSS event loop 禁锁 >100us)",
        re.compile(
            r"std\s*::\s*(?:lock_guard|unique_lock|scoped_lock|shared_lock)\s*<"
        ),
    ),
    (
        "R-12-2 blocking_read/recv/read_until (WSS event loop 禁阻塞读)",
        re.compile(
            r"\.\s*(?:blocking_read|blocking_write|read_until)\s*\("
            r"|\brecv\s*\("
        ),
    ),
    (
        "R-12-3 sync HTTP (WSS event loop 禁同步 REST)",
        re.compile(
            r"synchronous_http"
            r"|http_client\s*::\s*(?:Get|Post|Put|Delete|Head)\s*\("
            r"|curl_easy_perform\s*\("
        ),
    ),
    (
        "R-12-4 fsync/fdatasync (WSS event loop 禁阻塞 IO)",
        re.compile(r"\bfsync\s*\(|\bfdatasync\s*\("),
    ),
]

_COMMENT_LINE_RE = re.compile(r"^\s*(?://|\*)")
_SPIN_OK_RE = re.compile(r"SPIN_OK|LOCK_OK_SPIN")


def is_exempt(line: str) -> bool:
    if _COMMENT_LINE_RE.match(line):
        return True
    if "CI-EXEMPT:" in line:
        return True
    return False


def has_spin_ok(line: str) -> bool:
    return bool(_SPIN_OK_RE.search(line))


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []
    violations: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if is_exempt(line):
            continue
        for label, pat in _PATTERNS:
            if pat.search(line):
                # lock_guard 行如有 SPIN_OK 豁免
                if "lock_guard" in label or "unique_lock" in label:
                    if has_spin_ok(line):
                        continue
                violations.append((lineno, label, line.rstrip()))
    return violations


def main() -> int:
    ap = argparse.ArgumentParser(
        description="R-12 WSS event loop 阻塞反模式扫描"
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    src_root = repo_root / "src"
    if not src_root.exists():
        print("[r12_wss_blocking] PASS: src/ 不存在, 跳过")
        return 0

    all_violations: list[tuple[Path, int, str, str]] = []
    for path in sorted(src_root.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix not in {".cpp", ".cc", ".cxx", ".hpp", ".h"}:
            continue
        if not is_wss_path(path, repo_root):
            continue
        hits = scan_file(path)
        for lineno, label, line in hits:
            all_violations.append((path, lineno, label, line))

    if not all_violations:
        print("[r12_wss_blocking] PASS: 0 R-12 WSS blocking violations found")
        return 0

    print(f"[r12_wss_blocking] FAIL: {len(all_violations)} violation(s):", file=sys.stderr)
    for path, lineno, label, line in all_violations:
        rel = path.relative_to(repo_root)
        print(f"  ::error file={rel},line={lineno}::{label}: {line}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  R-12-1: WSS event loop 禁同步锁. spin < 100us 例外: 加 // SPIN_OK 注释.\n"
        "  R-12-2: 禁阻塞读写. 用 async_read / post 到线程池.\n"
        "  R-12-3: 禁同步 HTTP. 用 async HTTP 或 post 到专用 REST 线程.\n"
        "  R-12-4: 禁 fsync. WAL 写入走 SPSC queue + 独立 bg thread.\n"
        "  合规例外: 加 // CI-EXEMPT: <理由> 后提单 @老郭 24h 仲裁.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
