#!/usr/bin/env python3
# tests/ci_grep/r33_5host_paper.py — R-33 paper 严禁 /ws/user + 旧 clob host
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.2 W6 Wave 28
# 关联: docs/RESEARCH/laoli-polymarket-endpoint-matrix-v3.md §C 第 5 host
#       docs/RESEARCH/laogao-pr-review-v1.2.md §2.5
#       CLAUDE.md §8 红线 R-33 四维扫描流程红线
#
# 2 项规则:
#   1. paper mode 路径 (src/**/paper/** 或 src/**/paper_*) 严禁出现 /ws/user
#      WHY: paper 不下单, 不需要私有 HMAC user channel
#   2. 全代码 (src/ + include/) 严禁旧 host ws-subscriptions-clob.polymarket.com/ws/market
#      WHY: 已废弃, 应用第 5 host wss://sports-api.polymarket.com/ws
#
# 豁免:
#   - docs/** (复盘合法引用)
#   - tests/**_test.cpp + tests/unit/** (测试 mock 允许旧 host 常量)
#   - 含 // CI-EXEMPT: <理由> 的行 (需老郭 24h 仲裁)
#   - 注释行
#
# 用法:
#   python3 tests/ci_grep/r33_5host_paper.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 反模式正则                                                                    #
# --------------------------------------------------------------------------- #

# 规则 1: paper 路径下含 /ws/user (任意 wss:// 或 http:// 或裸字符串)
_PAPER_USER_CHANNEL_RE = re.compile(r"/ws/user\b")

# 规则 2: 旧 clob market WSS host
_OLD_CLOB_HOST_RE = re.compile(
    r"ws-subscriptions-clob\.polymarket\.com"
    r"(?:/ws/market)?"
)

_COMMENT_LINE_RE = re.compile(r"^\s*(?://|\*)")


def is_exempt_line(line: str) -> bool:
    if _COMMENT_LINE_RE.match(line):
        return True
    if "CI-EXEMPT:" in line:
        return True
    return False


def is_paper_path(path: Path, repo_root: Path) -> bool:
    """True if file lives in a paper-mode source directory."""
    rel = str(path.relative_to(repo_root)).replace("\\", "/")
    return "/paper/" in rel or re.search(r"/paper_[^/]+\.", rel) is not None


def is_excluded_from_rule2(path: Path, repo_root: Path) -> bool:
    """tests/**_test.cpp, tests/unit/**, docs/** are excluded from rule 2."""
    rel = str(path.relative_to(repo_root)).replace("\\", "/")
    if rel.startswith("docs/"):
        return True
    if rel.startswith("tests/"):
        return True
    return False


def scan_file_rule1(path: Path, repo_root: Path) -> list[tuple[int, str, str]]:
    """Rule 1: paper paths must not contain /ws/user."""
    if not is_paper_path(path, repo_root):
        return []
    # exclude tests under paper (mock allowed)
    rel = str(path.relative_to(repo_root)).replace("\\", "/")
    if rel.startswith("tests/"):
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []
    hits = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if is_exempt_line(line):
            continue
        if _PAPER_USER_CHANNEL_RE.search(line):
            hits.append((lineno, "R-33-paper /ws/user", line.rstrip()))
    return hits


def scan_file_rule2(path: Path, repo_root: Path) -> list[tuple[int, str, str]]:
    """Rule 2: no old clob host anywhere in src/ + include/."""
    if is_excluded_from_rule2(path, repo_root):
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []
    hits = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if is_exempt_line(line):
            continue
        if _OLD_CLOB_HOST_RE.search(line):
            hits.append((lineno, "R-33-old-host ws-subscriptions-clob", line.rstrip()))
    return hits


def main() -> int:
    ap = argparse.ArgumentParser(description="R-33 paper /ws/user + 旧 clob host 扫描")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    cpp_exts = {".cpp", ".cc", ".cxx", ".hpp", ".h"}
    all_dirs = [
        repo_root / "src",
        repo_root / "include",
    ]

    all_violations: list[tuple[Path, int, str, str]] = []

    for root in all_dirs:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if path.suffix not in cpp_exts:
                continue
            for lineno, label, line in scan_file_rule1(path, repo_root):
                all_violations.append((path, lineno, label, line))
            for lineno, label, line in scan_file_rule2(path, repo_root):
                all_violations.append((path, lineno, label, line))

    if not all_violations:
        print("[r33_5host_paper] PASS: 0 R-33 violations found")
        return 0

    print(f"[r33_5host_paper] FAIL: {len(all_violations)} violation(s):", file=sys.stderr)
    for path, lineno, label, line in all_violations:
        rel = path.relative_to(repo_root)
        print(f"  ::error file={rel},line={lineno}::{label}: {line}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  R-33-paper: paper mode 不下单, 不接 /ws/user 私有 HMAC channel. 移除该 URL.\n"
        "  R-33-old-host: 市场 WSS 必须用 wss://sports-api.polymarket.com/ws\n"
        "                 (第 5 host, 老李 v3 §C). 移除旧 ws-subscriptions-clob host.\n"
        "  合规例外: 加 // CI-EXEMPT: <理由> 后提单 @老郭 24h 仲裁.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
