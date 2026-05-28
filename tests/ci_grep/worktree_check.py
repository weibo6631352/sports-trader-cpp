#!/usr/bin/env python3
# tests/ci_grep/worktree_check.py — ADR-021 worktree 隔离 enforce (老高 v1.5 W8 Wave 35)
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)
# 关联: docs/RESEARCH/laogao-pr-review-v1.5.md §2.17
#       .github/workflows/pr.yml job ci-grep-worktree-check
#       ADR-021 (worktree 隔离, W8 起强约束)
#
# 规则:
#   扫 docs/MEETINGS/ 下的派单 md 文件 (或 git diff 中改动的 md 文件):
#     - 任意行含 subagent_type= 时, 在 ±12 行窗口内必须有 isolation="worktree"
#     - 若 subagent_type= 存在但缺 isolation="worktree" → FAIL
#
# 豁免:
#   - 文件行或附近含 "P0 例外, 老板 ack" (紧急 P0 < 2h)
#
# 状态: FAIL W8 起强约束
#
# 扫描范围:
#   1. git diff 中新增/改动的 docs/MEETINGS/*.md 文件
#   2. 若 git 不可用, 扫 docs/MEETINGS/ 下所有 .md 文件 (本地验证模式)
#
# 用法:
#   python3 tests/ci_grep/worktree_check.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 正则                                                                        #
# --------------------------------------------------------------------------- #

_SUBAGENT_TYPE_RE    = re.compile(r'subagent_type\s*=')
_ISOLATION_WT_RE     = re.compile(r'isolation\s*=\s*["\']?worktree["\']?')
_P0_EXEMPT_RE        = re.compile(r'P0\s*例外.*老板\s*ack|P0\s*exemption.*boss\s*ack', re.IGNORECASE)

# ±12 行窗口
_WINDOW = 12


def get_changed_md_files(repo_root: Path) -> list[Path]:
    """Return changed docs/MEETINGS/*.md files from git diff."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD~1...HEAD"],
            capture_output=True, text=True, cwd=repo_root, timeout=10,
        )
        if result.returncode != 0:
            result = subprocess.run(
                ["git", "diff", "--name-only", "--cached"],
                capture_output=True, text=True, cwd=repo_root, timeout=10,
            )
        files = [f.strip() for f in result.stdout.splitlines() if f.strip()]
        md_files = [
            repo_root / f for f in files
            if f.replace("\\", "/").startswith("docs/MEETINGS/") and f.endswith(".md")
        ]
        return [p for p in md_files if p.exists()]
    except (subprocess.SubprocessError, FileNotFoundError):
        return []


def scan_file(path: Path) -> list[str]:
    """Scan a single md file. Return list of violation messages."""
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []

    violations: list[str] = []
    for i, line in enumerate(lines):
        if not _SUBAGENT_TYPE_RE.search(line):
            continue

        # 检查 P0 豁免 (当前行本身或附近)
        window_start = max(0, i - _WINDOW)
        window_end   = min(len(lines), i + _WINDOW + 1)
        window_lines = lines[window_start:window_end]
        window_text  = "\n".join(window_lines)

        if _P0_EXEMPT_RE.search(window_text):
            continue  # P0 例外豁免

        # 检查 ±12 行窗口内是否有 isolation="worktree"
        if not _ISOLATION_WT_RE.search(window_text):
            violations.append(
                f"{path}:{i+1}: subagent_type= 出现但 ±{_WINDOW} 行内无 isolation=\"worktree\" "
                f"(ADR-021 worktree 隔离, W8 起强约束)"
            )

    return violations


def main() -> int:
    ap = argparse.ArgumentParser(description="ADR-021 worktree 隔离 enforce")
    ap.add_argument("--repo-root", type=Path,
                    default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    md_files = get_changed_md_files(repo_root)

    if not md_files:
        # fallback: 本地验证模式 — 扫 docs/MEETINGS/ 全部 md
        meetings_dir = repo_root / "docs" / "MEETINGS"
        if meetings_dir.exists():
            md_files = list(meetings_dir.glob("*.md"))
        if not md_files:
            print("[worktree_check] SKIP: 无 docs/MEETINGS/*.md 变更文件, pass")
            return 0

    all_violations: list[str] = []
    for md_path in md_files:
        all_violations.extend(scan_file(md_path))

    if not all_violations:
        print("[worktree_check] PASS: 所有派单 md 中 subagent_type= 均含 isolation=\"worktree\"")
        return 0

    print(f"[worktree_check] FAIL: {len(all_violations)} ADR-021 worktree 隔离违例:", file=sys.stderr)
    for v in all_violations:
        print(f"  ::error::{v}", file=sys.stderr)
    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  在 subagent_type= 同行或附近 (±12 行) 加:\n"
        "    isolation=\"worktree\"\n"
        "  豁免条件: 紧急 P0 < 2h 含 'P0 例外, 老板 ack'\n"
        "  见 ADR-021 worktree 隔离规范 (W8 起强约束)",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
