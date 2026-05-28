#!/usr/bin/env python3
# tests/ci_grep/worktree_commit_check.py — ADR-024 §3.1 worktree + pwd verify 强约束
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.0 W9 Wave 60
# 关联: docs/ADR/2026-06-W3-adr-021-worktree-isolation.md (ADR-021 worktree 隔离)
#       docs/ADR/ (ADR-024 worktree 标准流程)
#       docs/RESEARCH/laogao-pr-review-v1.7.md §2.20
#       .github/workflows/pr.yml job ci-grep-worktree-commit-check
#       GM 错 #19 (sub-agent 在 main tree 而非 worktree 内 Edit)
#
# 背景 (GM 错 #19 防重演):
#   ADR-024 §3.1: sub-agent 派单 prompt 若含 isolation=worktree,
#   则该 wave 完成时必须在 worktree branch commit, 不得直接写 main.
#   GM 错 #19 根因: 派单 prompt 有 "isolation=worktree" 但 sub-agent 实际在 main tree Edit.
#
# 检查规则:
#
#   Rule A (派单 prompt 强约束):
#     扫 docs/MEETINGS/*.md 文件 (git diff 改动) 中含 isolation=worktree 的行.
#     同一 ±12 行窗口内必须同时含 "pwd verify" 字样.
#     理由: isolation=worktree 派单必须配套 pwd verify 步骤, 防止 sub-agent 不验证路径.
#
#   Rule B (跨写 main 检测):
#     扫 git log 最近 1 个 merge commit (ci 环境 merge PR).
#     若 commit message 含 "isolation=worktree" 或 "agent-" worktree 标识符,
#     但 git branch --contains 只有 main (无 worktree branch 中转),
#     则 WARN: 疑似 sub-agent 跨写 main (未经 worktree branch 中转).
#     注: Rule B 为 WARN (exit 0), 不 FAIL (仅告警, 不阻断 PR).
#
# 豁免:
#   - 含 "P0 例外, 老板 ack" 行 (紧急 P0 < 2h)
#   - 含 "pwd verify: skip (N/A)" 显式声明
#
# 状态: Rule A FAIL; Rule B WARN (exit 0)
#
# 用法:
#   python3 tests/ci_grep/worktree_commit_check.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 正则                                                                        #
# --------------------------------------------------------------------------- #

_ISOLATION_WT_RE     = re.compile(r'isolation\s*=\s*["\']?worktree["\']?')
_PWD_VERIFY_RE       = re.compile(r'pwd\s+verify', re.IGNORECASE)
_PWD_VERIFY_SKIP_RE  = re.compile(r'pwd\s+verify\s*:\s*skip|pwd\s+verify.*N/A', re.IGNORECASE)
_P0_EXEMPT_RE        = re.compile(r'P0\s*例外.*老板\s*ack|P0\s*exemption.*boss\s*ack', re.IGNORECASE)
_WORKTREE_AGENT_RE   = re.compile(r'agent-[a-f0-9]{8,}', re.IGNORECASE)

# ±12 行窗口 (与 worktree_check.py 一致)
_WINDOW = 12


def get_changed_md_files(repo_root: Path) -> list[Path]:
    """Return changed docs/MEETINGS/*.md files from git diff."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "origin/main...HEAD"],
            capture_output=True, text=True, cwd=repo_root, timeout=10,
        )
        if result.returncode != 0 or not result.stdout.strip():
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


def check_rule_a(md_files: list[Path]) -> list[str]:
    """Rule A: isolation=worktree 派单必须配套 pwd verify.

    Returns list of violation messages (FAIL).
    """
    violations: list[str] = []

    for path in md_files:
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue

        for i, line in enumerate(lines):
            if not _ISOLATION_WT_RE.search(line):
                continue

            window_start = max(0, i - _WINDOW)
            window_end   = min(len(lines), i + _WINDOW + 1)
            window_lines = lines[window_start:window_end]
            window_text  = "\n".join(window_lines)

            # P0 例外豁免
            if _P0_EXEMPT_RE.search(window_text):
                continue

            # pwd verify: skip 显式豁免
            if _PWD_VERIFY_SKIP_RE.search(window_text):
                continue

            # 检查 ±12 行窗口内是否有 "pwd verify"
            if not _PWD_VERIFY_RE.search(window_text):
                violations.append(
                    f"{path}:{i+1}: isolation=\"worktree\" 存在但 ±{_WINDOW} 行内无 "
                    f"\"pwd verify\" 强约束 "
                    f"(ADR-024 §3.1 防 GM 错 #19: worktree 派单必须配套 pwd verify 步骤)"
                )

    return violations


def check_rule_b(repo_root: Path) -> list[str]:
    """Rule B (WARN-only): 检测疑似 sub-agent 跨写 main.

    Returns list of warning messages (non-fatal).
    """
    warnings: list[str] = []
    try:
        # 获取最近 5 个 commit message, 检查是否含 worktree agent 标识
        result = subprocess.run(
            ["git", "log", "--oneline", "-5"],
            capture_output=True, text=True, cwd=repo_root, timeout=10,
        )
        log_lines = result.stdout.splitlines()
        for log_line in log_lines:
            if _WORKTREE_AGENT_RE.search(log_line):
                # 含 worktree agent 标识的 commit — 检查是否直接在 main 上
                commit_hash = log_line.split()[0] if log_line.split() else ""
                if commit_hash:
                    branch_result = subprocess.run(
                        ["git", "branch", "--contains", commit_hash],
                        capture_output=True, text=True, cwd=repo_root, timeout=10,
                    )
                    branches = [
                        b.strip().lstrip("* ")
                        for b in branch_result.stdout.splitlines()
                        if b.strip()
                    ]
                    # 若只有 main 包含此 commit (没有 worktree branch 中转)
                    # 这是 heuristic WARN, 不是强 FAIL
                    non_main = [b for b in branches if b not in ("main", "HEAD")]
                    if not non_main:
                        warnings.append(
                            f"Rule B WARN: commit {commit_hash} "
                            f"含 worktree agent 标识 ({log_line.strip()!r}) "
                            f"但 git branch --contains 显示无 worktree branch 中转, "
                            f"疑似 sub-agent 跨写 main "
                            f"(ADR-024 §3.1, GM 错 #19 风险模式). "
                            f"请确认该 commit 来自合法 worktree branch merge."
                        )
    except (subprocess.SubprocessError, FileNotFoundError):
        pass
    return warnings


def main() -> int:
    ap = argparse.ArgumentParser(
        description="ADR-024 §3.1 worktree + pwd verify 强约束 (防 GM 错 #19)"
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    md_files = get_changed_md_files(repo_root)

    if not md_files:
        # fallback: 本地验证模式 — 扫 docs/MEETINGS/ 全部 md
        meetings_dir = repo_root / "docs" / "MEETINGS"
        if meetings_dir.exists():
            md_files = list(meetings_dir.glob("*.md"))
        if not md_files:
            print("[worktree_commit_check] SKIP: 无 docs/MEETINGS/*.md 变更文件, pass")
            return 0

    # Rule A (FAIL)
    rule_a_violations = check_rule_a(md_files)

    # Rule B (WARN only)
    rule_b_warnings = check_rule_b(repo_root)

    exit_code = 0

    if rule_b_warnings:
        print("[worktree_commit_check] WARN (Rule B — 跨写 main 检测, 非阻断):")
        for w in rule_b_warnings:
            print(f"  ::warning::{w}")

    if not rule_a_violations:
        print(
            "[worktree_commit_check] PASS: 所有 isolation=worktree 派单均配套 pwd verify"
        )
    else:
        exit_code = 1
        print(
            f"[worktree_commit_check] FAIL: {len(rule_a_violations)} Rule A 违例 "
            f"(isolation=worktree 缺 pwd verify):",
            file=sys.stderr,
        )
        for v in rule_a_violations:
            print(f"  ::error::{v}", file=sys.stderr)
        print("", file=sys.stderr)
        print(
            "修复指引:\n"
            "  在 isolation=\"worktree\" 同行或附近 (±12 行) 加:\n"
            "    pwd verify\n"
            "  或显式声明 'pwd verify: skip (N/A)' + 理由\n"
            "  豁免条件: 紧急 P0 < 2h 含 'P0 例外, 老板 ack'\n"
            "  见 ADR-024 §3.1 worktree 标准流程 (防 GM 错 #19)",
            file=sys.stderr,
        )

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
