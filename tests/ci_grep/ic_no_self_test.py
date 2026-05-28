#!/usr/bin/env python3
# tests/ci_grep/ic_no_self_test.py — IC 漏写测试 WARN (ADR-023, 老高 v1.5 W8 Wave 35)
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)
# 关联: docs/RESEARCH/laogao-pr-review-v1.5.md §2.16
#       .github/workflows/pr.yml job ci-grep-ic-no-self-test
#       ADR-023 (IC 自测唯一裁判, 撤 ADR-020/022 Tester review 层)
#
# 规则 (ADR-023 反转版):
#   PR 改了 src/<X>.cpp 或 include/<X>.hpp
#   但没有对应改动 tests/.../<X>_test.cpp (或 tests/.../<X>_test.hpp)
#   → WARN: IC 漏写 / 漏更新对应测试
#
# 豁免 (3 种):
#   1. sanity check / ABI lock 文件: 文件路径含 "sanity" / "abi_lock" / "abi"
#      或文件注释行含 "sanity check" / "ABI lock" / "CI-EXEMPT"
#   2. 紧急 P0 hotfix: PR_BODY 含 "P0 hotfix" + "老板 ack" (大小写不敏感)
#   3. GM 紧急例外: PR_BODY 含 "ADR-005 §3.2 例外" 或 "GM 紧急例外"
#
# 状态: WARN W8-W9 → FAIL W10 (见 pr.yml job 说明)
#
# 用法:
#   python3 tests/ci_grep/ic_no_self_test.py [--repo-root <path>]
#   PR_BODY="..." python3 tests/ci_grep/ic_no_self_test.py

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 豁免正则                                                                    #
# --------------------------------------------------------------------------- #

_P0_HOTFIX_RE  = re.compile(r"P0\s*hotfix", re.IGNORECASE)
_BOSS_ACK_RE   = re.compile(r"老板\s*ack|boss\s*ack", re.IGNORECASE)
_GM_EXEMPT_RE  = re.compile(r"ADR-005\s*§3\.2\s*例外|GM\s*紧急例外", re.IGNORECASE)

# 路径级豁免 (sanity / abi lock 文件)
_PATH_EXEMPT_RE = re.compile(
    r"(sanity|abi_lock|abi[-_]lock)",
    re.IGNORECASE,
)


def get_changed_files(repo_root: Path) -> list[str]:
    """Return changed files via git diff HEAD~1...HEAD (CI context)."""
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
        return [f.strip() for f in result.stdout.splitlines() if f.strip()]
    except (subprocess.SubprocessError, FileNotFoundError):
        return []


def stem_of(path: str) -> str:
    """Extract bare stem (no ext) from a path string."""
    return Path(path).stem


def is_path_exempt(path: str) -> bool:
    return bool(_PATH_EXEMPT_RE.search(path))


def is_pr_p0_exempt(pr_body: str) -> bool:
    return bool(_P0_HOTFIX_RE.search(pr_body) and _BOSS_ACK_RE.search(pr_body))


def is_pr_gm_exempt(pr_body: str) -> bool:
    return bool(_GM_EXEMPT_RE.search(pr_body))


def main() -> int:
    ap = argparse.ArgumentParser(description="IC 漏写测试 WARN (ADR-023)")
    ap.add_argument("--repo-root", type=Path,
                    default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root
    pr_body = os.environ.get("PR_BODY", "")

    # PR 级豁免检查
    if is_pr_p0_exempt(pr_body):
        print("[ic_no_self_test] SKIP: P0 hotfix + 老板 ack 豁免")
        return 0
    if is_pr_gm_exempt(pr_body):
        print("[ic_no_self_test] SKIP: GM 紧急例外 (ADR-005 §3.2) 豁免")
        return 0

    changed = get_changed_files(repo_root)
    if not changed:
        print("[ic_no_self_test] SKIP: 无法读取变更文件列表 (git 不可用), pass")
        return 0

    # 找出所有改动的 src/*.cpp 和 include/*.hpp
    src_changed: list[str] = []
    for f in changed:
        if is_path_exempt(f):
            continue
        norm = f.replace("\\", "/")
        if (norm.startswith("src/") and norm.endswith(".cpp")) or \
           (norm.startswith("include/") and norm.endswith(".hpp")):
            src_changed.append(f)

    if not src_changed:
        print("[ic_no_self_test] PASS: 无 src/*.cpp / include/*.hpp 改动, 跳过")
        return 0

    # 找出所有改动的 tests/.../*_test.cpp 的 stem 集合
    test_stems: set[str] = set()
    for f in changed:
        norm = f.replace("\\", "/")
        if norm.startswith("tests/") and norm.endswith("_test.cpp"):
            test_stems.add(stem_of(f))  # e.g. "foo_test"

    warnings: list[str] = []
    for src_file in src_changed:
        src_stem = stem_of(src_file)  # e.g. "ed25519"
        # 对应测试 stem = "<src_stem>_test"
        expected_test_stem = src_stem + "_test"
        if expected_test_stem not in test_stems:
            warnings.append(
                f"IC 漏写测试: 改了 {src_file} 但未找到对应 tests/.../{expected_test_stem}.cpp"
            )

    if not warnings:
        print("[ic_no_self_test] PASS: 所有 src/include 改动均有对应测试更新")
        return 0

    # W8-W9: WARN (exit 0); W10 起 CI job 升 FAIL (exit 1)
    print(f"[ic_no_self_test] WARN: {len(warnings)} IC 漏写测试警告 (ADR-023):")
    for w in warnings:
        print(f"  ::warning::{w}")
    print("")
    print("修复指引: IC 改 src/<X>.cpp 或 include/<X>.hpp 时,")
    print("  必须同步新增或更新 tests/.../<X>_test.cpp (ADR-023 IC 自测唯一裁判).")
    print("  豁免条件: P0 hotfix + 老板 ack / GM 紧急例外 / sanity check / ABI lock 文件.")
    return 0  # W8-W9 warning-only; pr.yml W10 将改为 exit 1


if __name__ == "__main__":
    sys.exit(main())
