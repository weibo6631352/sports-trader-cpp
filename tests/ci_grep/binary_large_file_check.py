#!/usr/bin/env python3
# tests/ci_grep/binary_large_file_check.py — binary 大文件 + ML artifact + data/ 入 git 检查
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.4 W7 Wave 33
# 关联: docs/RESEARCH/laogao-pr-review-v1.4.md §2.12
#       docs/INCIDENTS/gm-self-mistakes-log.md #14 (GM 误推 24 Parquet stub data)
#       .github/workflows/pr.yml job ci-grep-binary-large-file
#       .gitignore (data/ + *.parquet 已列)
#
# 背景 (GM 错 #14 配套):
#   commit af36066 误推 24 个 Parquet stub data 文件进 git,
#   与 #12 build_adr010 同模式重复.
#   .gitignore 已加 data/ + *.parquet 通配, 本 grep 为 CI 双保险.
#
# 规则:
#   WARN:  PR 改动文件大小 > 1MB
#   FAIL:  PR 改动文件后缀 in {.parquet, .pkl, .pt, .ckpt, .onnx, .h5, .feather}
#   FAIL:  PR 改动 data/ 目录下任意文件 (与 .gitignore 一致)
#
# 豁免:
#   - .gitignore / .gitattributes 本身改动 (元配置文件)
#   - CI 环境变量 PR_BODY 未注入 且 git 不可用 → pass
#
# 用法:
#   python3 tests/ci_grep/binary_large_file_check.py [--repo-root <path>]

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 规则常量                                                                      #
# --------------------------------------------------------------------------- #

LARGE_FILE_WARN_BYTES = 1 * 1024 * 1024  # 1 MB

# ML artifact / 数据序列化格式 — 禁止入 git
BANNED_EXTENSIONS = {
    ".parquet",
    ".pkl",
    ".pickle",
    ".pt",
    ".pth",
    ".ckpt",
    ".onnx",
    ".h5",
    ".hdf5",
    ".feather",
    ".arrow",
    ".npy",
    ".npz",
}

# 禁止目录前缀
BANNED_DIR_PREFIXES = ("data/",)

# 豁免文件 (元配置)
EXEMPT_FILES = {".gitignore", ".gitattributes", ".gitmodules"}


def get_changed_files(repo_root: Path) -> list[str]:
    """Return list of changed files relative to repo root."""
    for cmd in (
        ["git", "diff", "--name-only", "origin/main...HEAD"],
        ["git", "diff", "--name-only", "HEAD~1...HEAD"],
        ["git", "diff", "--name-only", "--cached"],
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


def get_file_size(repo_root: Path, filepath: str) -> int | None:
    """Return file size in bytes, or None if file doesn't exist."""
    full_path = repo_root / filepath
    if full_path.exists() and full_path.is_file():
        return full_path.stat().st_size
    return None


def format_size(size_bytes: int) -> str:
    if size_bytes >= 1024 * 1024:
        return f"{size_bytes / 1024 / 1024:.1f}MB"
    elif size_bytes >= 1024:
        return f"{size_bytes / 1024:.1f}KB"
    return f"{size_bytes}B"


def main() -> int:
    ap = argparse.ArgumentParser(description="Binary 大文件 + ML artifact 入 git 检查")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    changed_files = get_changed_files(repo_root)
    if not changed_files:
        print("[binary_large_file_check] SKIP: 无法读取变更文件列表 (git 不可用), pass")
        return 0

    warnings: list[str] = []
    errors: list[str] = []

    for filepath in changed_files:
        filename = Path(filepath).name

        # 豁免元配置文件
        if filename in EXEMPT_FILES:
            continue

        # Rule 3: data/ 目录 → FAIL
        if any(filepath.startswith(d) for d in BANNED_DIR_PREFIXES):
            errors.append(
                f"data/ 目录文件入 git: {filepath}\n"
                f"  data/ 已在 .gitignore, 禁止入 git (GM 错 #12 #14 配套)\n"
                f"  修复: git rm --cached {filepath} 并确认 .gitignore 已覆盖"
            )
            continue

        # Rule 2: 禁止扩展名 → FAIL
        ext = Path(filepath).suffix.lower()
        if ext in BANNED_EXTENSIONS:
            errors.append(
                f"ML artifact / 数据序列化格式入 git: {filepath} (后缀 {ext})\n"
                f"  禁止入 git: {', '.join(sorted(BANNED_EXTENSIONS))}\n"
                f"  修复: git rm --cached {filepath}, 用 DVC / S3 管理数据文件\n"
                f"  ONNX 模型: 只允许存在 experiments/<owner>/ 归档目录, 不入主 git"
            )
            continue

        # Rule 1: 文件大小 > 1MB → WARN
        size = get_file_size(repo_root, filepath)
        if size is not None and size > LARGE_FILE_WARN_BYTES:
            warnings.append(
                f"大文件 ({format_size(size)}): {filepath}\n"
                f"  文件 > 1MB 请确认是否应入 git, 若为二进制数据请移至 DVC / S3"
            )

    # 输出结果
    for w in warnings:
        print(f"::warning::[binary_large_file_check] {w}")

    if not errors and not warnings:
        print(
            f"[binary_large_file_check] PASS: {len(changed_files)} 个改动文件均无 "
            f"大文件 / ML artifact / data/ 问题"
        )
        return 0

    if errors:
        print(
            f"[binary_large_file_check] FAIL: {len(errors)} error(s), {len(warnings)} warning(s)",
            file=sys.stderr,
        )
        for e in errors:
            print(f"::error::[binary_large_file_check] {e}", file=sys.stderr)
        return 1

    print(
        f"[binary_large_file_check] WARN: {len(warnings)} warning(s), 0 error(s). "
        f"请 PR description 确认大文件 ack."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
