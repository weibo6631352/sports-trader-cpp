#!/usr/bin/env python3
# tests/ci_grep/abi_lock.py — ABI lock enforce (老李-老孙 handshake v1 + crypto + struct v1.7)
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.7 W9 Wave 60
# 关联: docs/RESEARCH/laoli-laoSun-handshake-v1.md (ABI lock 规则 + 4 等级)
#       docs/RESEARCH/laogao-pr-review-v1.7.md §2
#       docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md §3.4
#       docs/RESEARCH/laosun-w9-signer-v53-abi-align-spec-v1.md §4
#       .github/workflows/pr.yml job ci-grep-abi-lock
#       ADR-027 Enforce-3 (老高 W9 W4 上线)
#
# v1.7 新增 (W9 Wave 60):
#   - ABI_LOCKED_STRUCTS: OrderIntent struct (老韩 W9 W2 改, 4 ABI break)
#   - ABI_LOCKED_STRUCTS: Position struct (老周 W8 W4 ABI gap audit 发现)
#   - ABI_LOCKED_ENUMS: Outcome enum (新增, ADR-027)
#   - ABI_LOCKED_ENUMS: Side enum (新增, ADR-027)
#   - ABI_LOCKED_IPC: SignV52Request (老孙 W9 W2 改, +token_id +side +outcome)
#   - ABI_LOCKED_KEYS: PositionLedger key 从 MarketId → PositionKey (老周 W8 W4)
#   - Rule 4: 改上述 struct/enum 时 PR description 必须含 ABI ref 行
#
# v1.5 规则 (保留):
#   Rule 1: PR 改 ABI_LOCKED_FILES 中任一文件时,
#           PR description 必须含:
#             "ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>"
#           格式: "ABI ref: ...handshake-v1.md F-" + 数字 + 空格 + "L" + 数字
#
#   Rule 2: 改 static_assert 数值 (sizeof / offsetof / enum count) 时,
#           若等级为 L2 或 L3 (handshake 文档中三方签要求),
#           PR description 必须含 "三方签" 字样.
#
#   Rule 3 (v1.5): CMakeLists.txt 含 stcpp_crypto_ed25519 关键词变更时,
#           PR description 必须含 ABI ref 行 (crypto INTERFACE target = ABI 边界).
#
#   Rule 4 (v1.7 新): PR diff 改动 OrderIntent / Position / SignV52Request struct
#           定义文件时, PR description 必须含 ABI ref 行.
#           (ADR-027 Enforce-3 配套)
#
# 扫描方式:
#   本脚本通过 git diff --name-only origin/<base>...HEAD 探测变更文件.
#   若 git 不可用 (本地非 PR 环境), 则静默 pass (只在 CI PR 上下文 enforce).
#
# 豁免:
#   - 含 // CI-EXEMPT: <理由> 的行 (需走老郭 24h 仲裁)
#   - CI 环境变量 PR_BODY 为空 (非 PR 触发, pass)
#
# 用法:
#   python3 tests/ci_grep/abi_lock.py [--repo-root <path>]
#   PR_BODY="ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-01 L1" \
#     python3 tests/ci_grep/abi_lock.py

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# ABI 锁定文件 (改动必须引用 handshake 文档)                                    #
# --------------------------------------------------------------------------- #

ABI_LOCKED_FILES = {
    "include/stcpp/polymarket/pm_client.hpp",
    "include/stcpp/polymarket/live/live_pm_client.hpp",
    # v1.5 W8: 老孙 ed25519 wrapper ABI lock (SecureBuffer + Ed25519 sign/verify 接口)
    "include/stcpp/crypto/ed25519.hpp",
    # v1.7 W9: OrderIntent 定义文件 (老韩 W9 W2 4 ABI breaks, ADR-027 Enforce-3)
    "include/stcpp/risk/risk_gateway.hpp",
    # v1.7 W9: SignV52Request IPC 结构 (老孙 W9 W2, +token_id +side +outcome)
    "include/stcpp/signer/signer_iface.hpp",
    # v1.7 W9: Position / PositionRecord ABI (老周 W8 W4 gap audit, PositionKey 变更)
    "include/stcpp/infra/wal/position_record.hpp",
    "include/stcpp/infra/wal/position_ledger.hpp",
    # v1.7 W9: Side + Outcome enum (signal_iface.hpp, ADR-027 新增)
    "include/stcpp/strategy/signal_iface.hpp",
}

# v1.5: CMake target 含此关键词时触发 ABI lock 检查 (crypto INTERFACE target = ABI 边界)
ABI_LOCKED_CMAKE_KEYWORDS = {
    "stcpp_crypto_ed25519",
}

# v1.7: struct/enum 关键词触发检查 (diff 中新增/修改这些 struct/enum 定义时需 ABI ref)
# 匹配 diff 新增行中的 "struct OrderIntent" / "enum class Side" 等模式
ABI_LOCKED_STRUCT_KEYWORDS = {
    "struct OrderIntent",
    "struct Position",
    "struct SignV52Request",
    "enum class Side",
    "enum class Outcome",
    # PositionLedger key 类型 (PositionKey)
    "PositionKey",
    "PositionLedger",
}

# Rule 1: PR description 必须含 ABI ref 行
# 格式: "ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>"
_ABI_REF_RE = re.compile(
    r"ABI\s+ref\s*:.*laoli-laoSun-handshake-v1(?:\.md)?\s+F-\d+\s+L\d+",
    re.IGNORECASE,
)

# Rule 2: 改 static_assert 数值 + 等级 L2/L3 → 必须含"三方签"
_L2_L3_LEVEL_RE = re.compile(r"\bL[23]\b")
_TRIPLE_SIGN_RE = re.compile(r"三方签")

# static_assert 数值改动正则 (简单判断: 改 sizeof/offsetof/enum count 的 static_assert)
_STATIC_ASSERT_VALUE_RE = re.compile(
    r"static_assert\s*\(.*(?:sizeof|offsetof|count)\b",
    re.IGNORECASE,
)


def get_changed_files(repo_root: Path) -> list[str]:
    """Return list of changed files relative to repo root via git."""
    try:
        result = subprocess.run(
            ["git", "diff", "--name-only", "HEAD~1...HEAD"],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        if result.returncode != 0:
            # fallback: diff staged
            result = subprocess.run(
                ["git", "diff", "--name-only", "--cached"],
                capture_output=True,
                text=True,
                cwd=repo_root,
                timeout=10,
            )
        return [f.strip() for f in result.stdout.splitlines() if f.strip()]
    except (subprocess.SubprocessError, FileNotFoundError):
        return []


def check_static_assert_changes(repo_root: Path, changed_files: list[str]) -> bool:
    """Return True if any ABI-locked file has static_assert value changes in diff."""
    try:
        result = subprocess.run(
            ["git", "diff", "HEAD~1...HEAD", "--", *changed_files],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        diff_text = result.stdout
        # 看 diff 中新增行 (+ 开头但非 +++) 是否含 static_assert sizeof/offsetof/count
        for line in diff_text.splitlines():
            if line.startswith("+") and not line.startswith("+++"):
                if _STATIC_ASSERT_VALUE_RE.search(line):
                    return True
    except (subprocess.SubprocessError, FileNotFoundError):
        pass
    return False


def check_struct_keyword_changes(repo_root: Path, changed_files: list[str]) -> list[str]:
    """Return list of ABI struct keywords found in diff new lines.

    v1.7: scan diff for struct/enum definition changes in ABI_LOCKED_STRUCT_KEYWORDS.
    Returns matched keywords found in added lines of the diff.
    """
    triggered: list[str] = []
    if not changed_files:
        return triggered
    try:
        result = subprocess.run(
            ["git", "diff", "origin/main...HEAD", "--", *changed_files],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        diff_text = result.stdout
        for line in diff_text.splitlines():
            if line.startswith("+") and not line.startswith("+++"):
                for kw in ABI_LOCKED_STRUCT_KEYWORDS:
                    if kw in line and kw not in triggered:
                        triggered.append(kw)
    except (subprocess.SubprocessError, FileNotFoundError):
        pass
    return triggered


def main() -> int:
    ap = argparse.ArgumentParser(description="ABI lock enforce (handshake v1 + struct v1.7)")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    pr_body = os.environ.get("PR_BODY", "")

    # 非 PR 环境 (PR_BODY 未注入) → pass
    if not pr_body:
        print("[abi_lock] SKIP: PR_BODY 未注入 (非 PR 触发), pass")
        return 0

    changed_files = get_changed_files(repo_root)
    if not changed_files:
        print("[abi_lock] SKIP: 无法读取变更文件列表 (git 不可用), pass")
        return 0

    # 检查是否有 ABI 锁定文件被修改
    changed_abi_files = [
        f for f in changed_files
        if any(f.endswith(abi_f.replace("/", os.sep)) or f == abi_f
               for abi_f in ABI_LOCKED_FILES)
    ]

    # Rule 3 (v1.5): CMakeLists.txt 改动含 stcpp_crypto_ed25519 关键词
    changed_cmake_files = [
        f for f in changed_files
        if Path(f).name == "CMakeLists.txt"
    ]
    cmake_abi_triggered = False
    for cmake_file in changed_cmake_files:
        cmake_path = repo_root / cmake_file
        if cmake_path.exists():
            try:
                content = cmake_path.read_text(encoding="utf-8", errors="replace")
                if any(kw in content for kw in ABI_LOCKED_CMAKE_KEYWORDS):
                    cmake_abi_triggered = True
                    if cmake_file not in changed_abi_files:
                        changed_abi_files.append(cmake_file)
            except OSError:
                pass

    # Rule 4 (v1.7): struct/enum 关键词出现在 diff 新增行
    struct_kw_triggered = check_struct_keyword_changes(repo_root, changed_files)

    if not changed_abi_files and not struct_kw_triggered:
        print("[abi_lock] PASS: 本 PR 未修改 ABI 锁定文件, 检查跳过")
        return 0

    if changed_abi_files:
        print(f"[abi_lock] 检测到 ABI 锁定文件变更: {changed_abi_files}")
    if cmake_abi_triggered:
        print("[abi_lock] (Rule 3 v1.5) CMakeLists.txt 含 stcpp_crypto_ed25519 关键词 — crypto INTERFACE ABI 边界触发")
    if struct_kw_triggered:
        print(f"[abi_lock] (Rule 4 v1.7) diff 新增行含 ABI 锁定 struct/enum 关键词: {struct_kw_triggered}")

    errors: list[str] = []

    # Rule 1: PR description 必须含 ABI ref 行
    if not _ABI_REF_RE.search(pr_body):
        errors.append(
            "ABI lock Rule 1: 修改 ABI 锁定文件时, "
            "PR description 必须含:\n"
            "  'ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>'\n"
            "  示例: 'ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-02 L1'"
        )

    # Rule 2: 若 PR 声明了 L2/L3 等级, 必须含"三方签"
    abi_ref_match = _ABI_REF_RE.search(pr_body)
    if abi_ref_match:
        matched_text = abi_ref_match.group(0)
        if _L2_L3_LEVEL_RE.search(matched_text):
            if not _TRIPLE_SIGN_RE.search(pr_body):
                errors.append(
                    "ABI lock Rule 2: L2/L3 等级改动必须在 PR description 含 '三方签' 行\n"
                    "  L2 = 老李 + 老孙 + GM 三方签\n"
                    "  L3 = 老郭 (架构评审) + 老韩 (RM) + GM 三方签\n"
                    "  见 docs/RESEARCH/laoli-laoSun-handshake-v1.md §1 改动等级"
                )
        # static_assert 数值改动额外校验
        if check_static_assert_changes(repo_root, changed_abi_files):
            if _L2_L3_LEVEL_RE.search(matched_text) and not _TRIPLE_SIGN_RE.search(pr_body):
                # 已在 Rule 2 报过了, 不重复
                pass
            elif not _L2_L3_LEVEL_RE.search(matched_text):
                errors.append(
                    "ABI lock Rule 2 (static_assert): diff 含 sizeof/offsetof/enum count 变更, "
                    "等级应为 L2 或 L3, 需要'三方签'. "
                    "请确认 ABI ref 行中的等级标记."
                )

    # Rule 4 (v1.7): 改 OrderIntent / Position / Side / Outcome / SignV52Request / PositionKey
    #   时必须含 ABI ref 行 (ADR-027 Enforce-3)
    if struct_kw_triggered and not _ABI_REF_RE.search(pr_body):
        struct_kw_str = ", ".join(struct_kw_triggered)
        errors.append(
            f"ABI lock Rule 4 (v1.7 ADR-027 Enforce-3): diff 含 ABI 锁定 struct/enum 关键词 "
            f"({struct_kw_str}), PR description 必须含:\n"
            "  'ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>'\n"
            "  OrderIntent/Side/Outcome/SignV52Request/PositionKey 变更均为 ABI breaking.\n"
            "  见 docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md §3.4 等级表"
        )

    if not errors:
        print("[abi_lock] PASS: ABI lock 检查通过")
        return 0

    print(f"[abi_lock] FAIL: {len(errors)} error(s):", file=sys.stderr)
    for e in errors:
        print(f"  ::error::{e}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  在 PR description §Why 或专门一行加:\n"
        "    ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-<编号> L<等级>\n"
        "  L2/L3 需额外加: 三方签 (老李 / 老孙 / GM 或 老郭 / 老韩 / GM)\n"
        "  详见 docs/RESEARCH/laoli-laoSun-handshake-v1.md §1 改动等级表\n"
        "  v1.7 新增 Rule 4: OrderIntent/Side/Outcome/SignV52Request/PositionKey\n"
        "  改动均需 ABI ref 行 (ADR-027 Enforce-3)",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
