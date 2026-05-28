#!/usr/bin/env python3
# tests/ci_grep/build_verification.py — 派单验证条款完整率检查 (GM 错 #11 配套)
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.3 W6 Wave 30
# 关联: docs/INCIDENTS/gm-self-mistakes-log.md #11 (GM 错 #11: 无验证条款派单)
#       docs/RESEARCH/laogao-pr-review-v1.3.md §2.10
#       .github/workflows/pr.yml job ci-grep-build-verification
#
# 规则 (GM 错 #11 enforce, W6 紧急):
#   扫描 docs/MEETINGS/ + docs/RESEARCH/ 中的派单 prompt 文件 (*.md),
#   若 prompt 含 "subagent_type=" + 代码任务关键词 (.cpp / .hpp / 实施 / 落代码),
#   则该 prompt 文件必须含以下验证 hard 约束:
#     - "cmake --build build" 字样
#     - "ctest" 字样
#   缺失其一 → grep FAIL (派单缺验证条款)
#
# WHY (GM 错 #11):
#   多次 wave 派单要求 sub-agent 落代码但未强制要求 build+ctest 验证,
#   导致交付"写 py 不算交付, 跑通才算". GM 决议: 代码任务派单必须含 build+ctest 硬约束.
#
# 代码任务判断词 (任一命中即视为代码任务):
#   .cpp / .hpp (文件后缀)
#   落代码 / 实施 / 落地 / 写代码
#   unit test / 单测
#
# 验证条款 (两个均需存在于同一文件):
#   cmake --build build   (构建)
#   ctest                 (测试)
#
# 豁免:
#   - 文件名含 "pr-review" / "conventions" / "laogao" (本规则文档本身)
#   - 含 "CI-EXEMPT: build_verification" 的行
#   - 文件名含 "gm-self-mistakes-log" (GM 错误日志, 非派单文件)
#
# 用法:
#   python3 tests/ci_grep/build_verification.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 扫描范围                                                                       #
# --------------------------------------------------------------------------- #

SCAN_DIRS_REL = [
    "docs/MEETINGS",
    "docs/RESEARCH",
]

# 豁免文件名关键词 (规则文档本身 + GM 错误日志 + 非派单文档)
EXEMPT_FILE_KEYWORDS = [
    "laogao-pr-review",
    "laogao-code-conventions",
    "gm-self-mistakes-log",
    "employee-registry",
    "hr-pulse",
    "backlog",
    "sprint",       # sprint backlog/retro 不是派单 prompt
    "weekly-report",
    "kpi",
    "okr",
]

# --------------------------------------------------------------------------- #
# 正则                                                                          #
# --------------------------------------------------------------------------- #

# subagent_type= 行 → 确认是派单文件
_SUBAGENT_TYPE_RE = re.compile(r"subagent_type\s*=\s*[A-Za-z0-9_-]+")

# 代码任务关键词 (任一命中 → 视为代码任务派单)
_CODE_TASK_PATTERNS: list[re.Pattern[str]] = [
    re.compile(r"\.cpp\b"),
    re.compile(r"\.hpp\b"),
    re.compile(r"落\s*代码"),
    re.compile(r"落\s*地.*(?:实现|代码|文件)|(?:实现|代码|文件).*落\s*地"),
    re.compile(r"实\s*施"),
    re.compile(r"写\s*代码"),
    re.compile(r"unit\s*test", re.IGNORECASE),
    re.compile(r"单\s*测"),
]

# 验证条款 (两者均须出现在文件中)
_BUILD_VERIFY_RE = re.compile(r"cmake\s+--build\s+build", re.IGNORECASE)
_CTEST_VERIFY_RE = re.compile(r"\bctest\b", re.IGNORECASE)

# 豁免标记
_EXEMPT_RE = re.compile(r"CI-EXEMPT:\s*build_verification")
_COMMENT_LINE_RE = re.compile(r"^\s*(?:<!--|#)")


def is_exempt_file(path: Path) -> bool:
    name_lower = path.name.lower()
    return any(kw in name_lower for kw in EXEMPT_FILE_KEYWORDS)


def has_code_task(text: str) -> bool:
    for pat in _CODE_TASK_PATTERNS:
        if pat.search(text):
            return True
    return False


def has_build_verification(text: str) -> tuple[bool, bool]:
    """Return (has_cmake_build, has_ctest)."""
    return (
        bool(_BUILD_VERIFY_RE.search(text)),
        bool(_CTEST_VERIFY_RE.search(text)),
    )


def scan_file(path: Path) -> list[tuple[int, str]]:
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    # 豁免检查
    if _EXEMPT_RE.search(text):
        return []

    # 必须是派单文件 (含 subagent_type=)
    if not _SUBAGENT_TYPE_RE.search(text):
        return []

    # 是否含代码任务
    if not has_code_task(text):
        return []

    # 检查验证条款
    has_cmake, has_ctest = has_build_verification(text)
    issues: list[tuple[int, str]] = []

    if not has_cmake:
        issues.append((
            1,
            f"GM 错 #11: 派单含代码任务但缺 'cmake --build build' 验证条款 "
            f"(文件: {path.name}). "
            "派单 prompt 必须包含 build + ctest hard 约束 (跑通才算交付)."
        ))

    if not has_ctest:
        issues.append((
            1,
            f"GM 错 #11: 派单含代码任务但缺 'ctest' 验证条款 "
            f"(文件: {path.name}). "
            "派单 prompt 必须包含 build + ctest hard 约束 (跑通才算交付)."
        ))

    return issues


def main() -> int:
    ap = argparse.ArgumentParser(description="派单 build+ctest 验证条款检查 (GM 错 #11)")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    all_violations: list[tuple[Path, int, str]] = []

    for scan_dir_rel in SCAN_DIRS_REL:
        scan_root = repo_root / scan_dir_rel
        if not scan_root.exists():
            continue
        for path in sorted(scan_root.rglob("*.md")):
            if not path.is_file():
                continue
            if is_exempt_file(path):
                continue
            for lineno, msg in scan_file(path):
                all_violations.append((path, lineno, msg))

    if not all_violations:
        print("[build_verification] PASS: 0 派单验证条款缺失 (GM 错 #11 enforce)")
        return 0

    print(
        f"[build_verification] FAIL: {len(all_violations)} violation(s):",
        file=sys.stderr,
    )
    for path, lineno, msg in all_violations:
        rel = path.relative_to(repo_root)
        print(f"  ::error file={rel},line={lineno}::{msg}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引 (GM 错 #11):\n"
        "  代码任务派单必须在 prompt 文件中明确包含:\n"
        "    cmake --build build && ctest -j 8 --output-on-failure\n"
        "  例:\n"
        "    验证 hard 约束: 本地 cmake --build build && ctest -j 8 --output-on-failure 全过才回汇\n"
        "  若确认豁免: 在文件首行加 <!-- CI-EXEMPT: build_verification <理由> -->\n"
        "  参见 docs/INCIDENTS/gm-self-mistakes-log.md #11",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
