#!/usr/bin/env python3
# tests/ci_grep/fom_check.py — FOM ref 检查 (ADR-005 §3.4 配套)
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.4 W7 Wave 33
# 关联: docs/RESEARCH/laogao-pr-review-v1.4.md §2.13
#       ADR-005 §3.4 (FOM — File Ownership Matrix)
#       .github/workflows/pr.yml job ci-grep-fom
#
# 背景:
#   ADR-005 §3.4 W7 W4 立: 关键基础文件 (CMakeLists.txt / pr.yml / .clang-tidy 等)
#   须在 FOM 中列 lead owner. PR 改这些文件必须:
#     1. PR description 含 "FOM ref: <wave>" 引用
#     2. 改非 FOM 列出的文件 → fail (检查文件是否超出 FOM 范围)
#     3. 关键文件改动必须有 lead owner ack
#
# 规则:
#   Rule 1: PR description 必须含 "FOM ref: <wave或文档路径>" 格式引用
#           (仅当 PR 改动了 FOM_GATED_FILES 中的任意文件时触发)
#   Rule 2: FOM_GATED_FILES 的改动必须在 PR description 含 lead owner ack 声明
#           格式: "lead owner ack: <name>" 或 "lead owner: <name> ack"
#
# 状态: W7 W4 ADR-005 §3.4 立后激活 (CI job 含 if: 条件控制)
# 当前: 若 FOM_ACTIVE 环境变量未设置为 "1", 本脚本 warning-only (不 FAIL)
#
# 用法:
#   python3 tests/ci_grep/fom_check.py [--repo-root <path>]
#   FOM_ACTIVE=1 python3 tests/ci_grep/fom_check.py

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# FOM 管控文件列表 (ADR-005 §3.4, W7 W4 填充)                                  #
# --------------------------------------------------------------------------- #

# 关键基础文件 — 改动须 FOM ref + lead owner ack
FOM_GATED_FILES = {
    # CI / 工作流
    ".github/workflows/pr.yml",
    ".github/workflows/nightly.yml",
    ".github/workflows/perf-regression.yml",
    ".github/PULL_REQUEST_TEMPLATE.md",
    # 构建系统
    "CMakeLists.txt",
    "tests/unit/CMakeLists.txt",
    # 代码质量工具配置
    ".clang-tidy",
    ".clang-format",
    # 环境 / 忽略
    ".gitignore",
    ".env.example",
    # 运营手册
    "CLAUDE.md",
    "AGENT.md",
}

# FOM ref 正则 (ADR-005 §3.4)
_FOM_REF_RE = re.compile(
    r"FOM\s+ref\s*[:：]\s*\S+",
    re.IGNORECASE | re.UNICODE,
)

# lead owner ack 正则
_LEAD_OWNER_ACK_RE = re.compile(
    r"lead\s+owner\s+ack\s*[:：]\s*\S+|lead\s+owner\s*[:：]\s*\S+.*?ack",
    re.IGNORECASE | re.UNICODE,
)


def get_changed_files(repo_root: Path) -> list[str]:
    for cmd in (
        ["git", "diff", "--name-only", "origin/main...HEAD"],
        ["git", "diff", "--name-only", "HEAD~1...HEAD"],
    ):
        try:
            result = subprocess.run(
                cmd, capture_output=True, text=True, cwd=repo_root, timeout=10
            )
            if result.returncode == 0 and result.stdout.strip():
                return [f.strip() for f in result.stdout.splitlines() if f.strip()]
        except (subprocess.SubprocessError, FileNotFoundError):
            continue
    return []


def main() -> int:
    ap = argparse.ArgumentParser(description="FOM ref 检查 (ADR-005 §3.4 配套)")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    pr_body = os.environ.get("PR_BODY", "")
    fom_active = os.environ.get("FOM_ACTIVE", "0") == "1"

    if not pr_body:
        print("[fom_check] SKIP: PR_BODY 未注入 (非 PR 触发), pass")
        return 0

    changed_files = get_changed_files(repo_root)
    if not changed_files:
        print("[fom_check] SKIP: 无法读取变更文件列表 (git 不可用), pass")
        return 0

    # 检查是否有 FOM 管控文件被改动
    gated = [f for f in changed_files if f in FOM_GATED_FILES]

    if not gated:
        print(f"[fom_check] PASS: 本 PR 未改动 FOM 管控文件 ({len(FOM_GATED_FILES)} 项), 跳过")
        return 0

    print(f"[fom_check] 检测到 FOM 管控文件改动 ({len(gated)} 项):")
    for f in gated:
        print(f"  - {f}")

    errors: list[str] = []

    # Rule 1: PR description 必须含 FOM ref
    if not _FOM_REF_RE.search(pr_body):
        errors.append(
            "Rule 1: PR description 缺少 FOM ref 声明\n"
            "  格式: 'FOM ref: W7-F-04' 或 'FOM ref: docs/ADR/adr-005-...'\n"
            "  改动的 FOM 管控文件: " + ", ".join(gated)
        )

    # Rule 2: 必须有 lead owner ack
    if not _LEAD_OWNER_ACK_RE.search(pr_body):
        errors.append(
            "Rule 2: PR description 缺少 lead owner ack 声明\n"
            "  格式: 'lead owner ack: 老高' 或 'lead owner: 老郭 ack'\n"
            "  FOM 管控文件须 lead owner 显式 ack"
        )

    if not errors:
        print("[fom_check] PASS: FOM ref + lead owner ack 均已声明")
        return 0

    if not fom_active:
        # W7 W4 前: warning-only 模式
        for e in errors:
            print(f"::warning::[fom_check] (warning-only, FOM_ACTIVE 未激活) {e}")
        print(
            "[fom_check] WARNING-ONLY: FOM_ACTIVE 未设置为 '1', 本次不 FAIL. "
            "ADR-005 §3.4 W7 W4 立后激活 FAIL 模式."
        )
        return 0

    print(f"[fom_check] FAIL: {len(errors)} error(s)", file=sys.stderr)
    for e in errors:
        print(f"::error::[fom_check] {e}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
