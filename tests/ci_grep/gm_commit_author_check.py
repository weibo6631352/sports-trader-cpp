#!/usr/bin/env python3
# tests/ci_grep/gm_commit_author_check.py — GM commit author warning + audit counter
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.4 W7 Wave 33
# 关联: docs/RESEARCH/laogao-pr-review-v1.4.md §2.11
#       docs/INCIDENTS/gm-self-mistakes-log.md #13 (GM 越权代修)
#       .github/workflows/pr.yml job ci-grep-gm-commit-author
#       ADR-005 §3.2 (GM 例外条款: 紧急 hotfix)
#
# 背景 (GM 错 #13 配套):
#   GM 自承 W6 W2/W3 共越权代修 10+ 处别人代码 (越权代修红线).
#   本 check 不 block GM 提 PR, 但强制显式 ack:
#     - PR description 必须含 "GM 紧急 hotfix 理由: <段>" (ADR-005 §3.2 例外)
#     - 满足则 WARNING + audit counter +1 (老胡 §9 KPI 数据源)
#     - 不满足则 FAIL (强迫显式声明, 不允许静默越权)
#
# 规则:
#   扫 src/ 和 include/ 下被改动的文件.
#   对每个改动文件, 检查 commit author.
#   若 commit author = "weibo wang" (GM 老雷) 且 PR description 未含 GM 例外声明 → FAIL.
#   若 commit author = "weibo wang" 且 PR description 含 GM 例外声明 → WARNING + counter.
#   非 GM author → pass (正常通道).
#
# GM 例外声明格式 (ADR-005 §3.2):
#   PR description 必须含: "GM 紧急 hotfix 理由: <段落>"
#   (段落至少 10 字, 不允许空填)
#
# 豁免:
#   - PR 仅改 .github/ / tests/ci_grep/ / docs/ / CMakeLists.txt / .gitignore (元文件)
#     → 这些不是别人的生产代码, 不触发 check
#   - CI 环境变量 PR_BODY 未注入 (非 PR 触发) → pass
#
# 用法:
#   python3 tests/ci_grep/gm_commit_author_check.py [--repo-root <path>]
#   PR_BODY="GM 紧急 hotfix 理由: libsodium vcpkg guide 紧急修 build fail" \
#     python3 tests/ci_grep/gm_commit_author_check.py

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 常量                                                                          #
# --------------------------------------------------------------------------- #

GM_AUTHOR_NAME = "weibo wang"

# GM 例外声明正则 (ADR-005 §3.2)
# 格式: "GM 紧急 hotfix 理由: <至少 10 字段落>"
_GM_HOTFIX_RE = re.compile(
    r"GM\s*紧急\s*hotfix\s*理由\s*[:：]\s*(.{10,})",
    re.IGNORECASE | re.UNICODE,
)

# 豁免路径前缀 (这些是元文件, GM 维护合法)
EXEMPT_PATH_PREFIXES = (
    ".github/",
    "tests/ci_grep/",
    "docs/",
    "CMakeLists.txt",
    ".gitignore",
    ".clang-tidy",
    ".clang-format",
    "CLAUDE.md",
    "AGENT.md",
    "README.md",
    ".env.example",
)

# 生产代码目录 (只 check 这些)
PROD_PATH_PREFIXES = (
    "src/",
    "include/",
)

# audit counter 文件路径
COUNTER_FILE = Path(__file__).parent / "gm_author_warning_counter.json"


def get_changed_files(repo_root: Path) -> list[str]:
    """Return list of changed files relative to repo root."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "origin/main...HEAD"],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        if result.returncode != 0 or not result.stdout.strip():
            result = subprocess.run(
                ["git", "diff", "--name-only", "HEAD~1...HEAD"],
                capture_output=True,
                text=True,
                cwd=repo_root,
                timeout=10,
            )
        return [f.strip() for f in result.stdout.splitlines() if f.strip()]
    except (subprocess.SubprocessError, FileNotFoundError):
        return []


def get_commit_author_for_file(repo_root: Path, filepath: str) -> str | None:
    """Return the author name of the last commit touching filepath."""
    try:
        result = subprocess.run(
            ["git", "log", "-1", "--format=%an", "--", filepath],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        name = result.stdout.strip()
        return name if name else None
    except (subprocess.SubprocessError, FileNotFoundError):
        return None


def is_exempt_path(filepath: str) -> bool:
    """Return True if filepath is an exempt meta-file path."""
    return any(filepath.startswith(p) for p in EXEMPT_PATH_PREFIXES)


def is_prod_path(filepath: str) -> bool:
    """Return True if filepath is under production code directories."""
    return any(filepath.startswith(p) for p in PROD_PATH_PREFIXES)


def load_counter(counter_file: Path) -> dict:
    if counter_file.exists():
        try:
            return json.loads(counter_file.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    return {"gm_author_warning_count": 0, "history": []}


def save_counter(counter_file: Path, data: dict) -> None:
    try:
        counter_file.write_text(json.dumps(data, ensure_ascii=False, indent=2))
    except OSError as e:
        print(f"[gm_commit_author_check] WARNING: 无法写 counter 文件: {e}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description="GM commit author warning + audit counter")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    pr_body = os.environ.get("PR_BODY", "")

    if not pr_body:
        print("[gm_commit_author_check] SKIP: PR_BODY 未注入 (非 PR 触发), pass")
        return 0

    changed_files = get_changed_files(repo_root)
    if not changed_files:
        print("[gm_commit_author_check] SKIP: 无法读取变更文件列表 (git 不可用), pass")
        return 0

    # 只检查生产代码路径
    prod_files = [f for f in changed_files if is_prod_path(f) and not is_exempt_path(f)]

    if not prod_files:
        print("[gm_commit_author_check] PASS: 本 PR 未改动 src/ 或 include/ 生产代码, 跳过")
        return 0

    # 找 GM author 改动的生产文件
    gm_files: list[str] = []
    for f in prod_files:
        author = get_commit_author_for_file(repo_root, f)
        if author and author.lower() == GM_AUTHOR_NAME.lower():
            gm_files.append(f)

    if not gm_files:
        print("[gm_commit_author_check] PASS: 本 PR 生产代码均非 GM 直接 commit")
        return 0

    print(f"[gm_commit_author_check] 检测到 GM 直接 commit 的生产文件 ({len(gm_files)}):")
    for f in gm_files:
        print(f"  - {f}")

    # 检查是否含 GM 例外声明
    hotfix_match = _GM_HOTFIX_RE.search(pr_body)

    if not hotfix_match:
        print(
            "::error::GM commit author check FAIL: "
            "PR description 必须含 'GM 紧急 hotfix 理由: <至少 10 字说明>' "
            "(ADR-005 §3.2 例外条款, 禁止静默越权代修别人代码)",
            file=sys.stderr,
        )
        print(
            "::error::修复指引: 在 PR description 加一行:\n"
            "  GM 紧急 hotfix 理由: <具体说明, 为什么 GM 必须亲自改这个文件, 不能派回 owner>",
            file=sys.stderr,
        )
        return 1

    # 有例外声明 → WARNING + audit counter
    hotfix_reason = hotfix_match.group(1).strip()
    print(
        f"::warning::GM commit author check WARNING: "
        f"GM 直接 commit 了 {len(gm_files)} 个生产文件. "
        f"已找到 GM 例外声明 (ADR-005 §3.2): '{hotfix_reason}'. "
        f"audit counter +1 (老胡 §9 KPI 数据源). "
        f"本 PR warning-only, 不 block."
    )

    # 更新 audit counter
    counter_data = load_counter(COUNTER_FILE)
    counter_data["gm_author_warning_count"] = counter_data.get("gm_author_warning_count", 0) + 1
    pr_number = os.environ.get("PR_NUMBER", "unknown")
    pr_title = os.environ.get("PR_TITLE", "unknown")
    counter_data.setdefault("history", []).append(
        {
            "pr_number": pr_number,
            "pr_title": pr_title,
            "gm_files": gm_files,
            "hotfix_reason": hotfix_reason,
        }
    )
    save_counter(COUNTER_FILE, counter_data)

    total = counter_data["gm_author_warning_count"]
    print(f"[gm_commit_author_check] 累计 GM author warning: {total} 次")
    return 0


if __name__ == "__main__":
    sys.exit(main())
