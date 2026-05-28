#!/usr/bin/env python3
# tests/ci_grep/gm_merge_audit.py — ADR-024 §6 GM merge commit message audit
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.0 W9 Wave 60
# 关联: docs/RESEARCH/laoguo-w6-gm-commit-audit.md (老郭 W6 spec)
#       docs/RESEARCH/laogao-pr-review-v1.7.md §2.21
#       .github/workflows/pr.yml job ci-grep-gm-merge-audit
#       ADR-024 §6 (GM merge commit message 格式规范)
#
# 背景 (老郭 ADR-024 §6 spec):
#   GM 老雷 merge worktree branch → main 时, commit message 格式必须含:
#     "merge: <persona> <Wave NN>"
#   例: "merge: 老高 Wave 60"
#       "merge: laosun Wave 52"
#   用途: PR timeline 可追溯哪个 agent wave 的产出何时 merge.
#
# 检查规则:
#
#   扫 PR 关联的 merge commit (PR merge 时 GitHub 自动创建).
#   若 commit author = "weibo wang" (GM 老雷) 且 commit message 含 "Merge pull request"
#   或 "Merge branch", 则检查 commit message 是否符合格式:
#     "merge: <persona> <Wave NN>"
#   不符合 → WARN (exit 0, 不阻断 PR)
#
# 说明:
#   本 check 为 WARN-only (exit 0). GM merge 操作本身不阻断 PR.
#   目的: 提醒 GM 规范 merge commit 格式, 方便 audit trail.
#   强 FAIL 由老郭 ADR-024 §6 后续版本决定是否升级.
#
# 豁免:
#   - PR 非 GM author → skip
#   - PR_BODY 含 "merge audit: exempt" → skip
#
# 用法:
#   python3 tests/ci_grep/gm_merge_audit.py [--repo-root <path>]
#   PR_BODY="merge: 老高 Wave 60" \
#     python3 tests/ci_grep/gm_merge_audit.py

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 正则                                                                        #
# --------------------------------------------------------------------------- #

# GM merge commit message 规范格式
# 例: "merge: 老高 Wave 60" / "merge: laosun Wave 52" / "merge: 小梁 Wave 47"
_MERGE_FORMAT_RE = re.compile(
    r"\bmerge\s*:\s*\S+\s+Wave\s+\d+",
    re.IGNORECASE,
)

# GitHub 自动生成的 merge commit 标识
_GITHUB_MERGE_RE = re.compile(
    r"Merge pull request|Merge branch",
    re.IGNORECASE,
)

# GM author
_GM_AUTHOR = "weibo wang"

# 豁免
_EXEMPT_RE = re.compile(r"merge audit\s*:\s*exempt", re.IGNORECASE)


def get_recent_commits(repo_root: Path, n: int = 10) -> list[tuple[str, str, str]]:
    """Return last n commits as (hash, author, message) tuples."""
    try:
        result = subprocess.run(
            ["git", "log", f"-{n}", "--format=%H\x1f%an\x1f%s"],
            capture_output=True, text=True, cwd=repo_root, timeout=10,
        )
        commits = []
        for line in result.stdout.splitlines():
            parts = line.split("\x1f", 2)
            if len(parts) == 3:
                commits.append((parts[0], parts[1], parts[2]))
        return commits
    except (subprocess.SubprocessError, FileNotFoundError):
        return []


def main() -> int:
    ap = argparse.ArgumentParser(
        description="ADR-024 §6 GM merge commit message audit (老郭 spec)"
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    pr_body = os.environ.get("PR_BODY", "")

    # 豁免
    if _EXEMPT_RE.search(pr_body):
        print("[gm_merge_audit] SKIP: PR_BODY 含 'merge audit: exempt', 跳过")
        return 0

    commits = get_recent_commits(repo_root, n=10)
    if not commits:
        print("[gm_merge_audit] SKIP: 无法获取 git log, pass")
        return 0

    warnings: list[str] = []

    for commit_hash, author, message in commits:
        # 只检查 GM 老雷的 merge commit
        if author.lower() != _GM_AUTHOR.lower():
            continue
        if not _GITHUB_MERGE_RE.search(message):
            continue
        # 是 GM 的 merge commit, 检查格式
        if not _MERGE_FORMAT_RE.search(message):
            warnings.append(
                f"commit {commit_hash[:8]} ({message!r}): "
                f"GM merge commit message 缺少规范格式.\n"
                f"  期望格式: \"merge: <persona> <Wave NN>\"\n"
                f"  示例: \"merge: 老高 Wave 60\" / \"merge: laosun Wave 52\"\n"
                f"  见 ADR-024 §6 (老郭 spec)"
            )

    if not warnings:
        print("[gm_merge_audit] PASS: 未发现 GM merge commit message 格式问题 (或无 GM merge commit)")
        return 0

    # WARN-only (exit 0)
    print(f"[gm_merge_audit] WARN: {len(warnings)} GM merge commit message 格式问题 (非阻断):")
    for w in warnings:
        print(f"  ::warning::{w}")

    print("")
    print(
        "提醒 (WARN-only, 不阻断 PR):\n"
        "  GM 老雷 merge worktree branch 时, commit message 建议加:\n"
        "    merge: <persona中文名或英文> <Wave NN>\n"
        "  目的: PR timeline audit trail 可追溯 (ADR-024 §6 老郭 spec)\n"
        "  暂为 WARN, 后续版本可能升 FAIL (老郭 ADR-024 §6 决定)"
    )
    return 0  # WARN-only, 不阻断


if __name__ == "__main__":
    sys.exit(main())
