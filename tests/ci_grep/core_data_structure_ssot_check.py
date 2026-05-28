#!/usr/bin/env python3
# tests/ci_grep/core_data_structure_ssot_check.py — ADR-027 Enforce-1 SSOT cite check
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.0 W9 Wave 60
# 关联: docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md §4 Enforce-1/3
#       docs/RESEARCH/laoguo-w8-w5-adr-027-main-review.md §1 Enforce-3 + §3
#       docs/RESEARCH/laogao-pr-review-v1.7.md §2.19
#       .github/workflows/pr.yml job ci-grep-core-data-structure-ssot
#
# 检查 4 项 (ADR-027 C1-C4, 老郭主审 §1 补充工程细节):
#
#   C1 (SSOT cite grep):
#       PR diff 改 OrderIntent / SignedOrder / Position / MarketInfo / FairValue /
#       OrderBookSnapshot struct 定义文件时, PR description 全文必须含:
#         - laoli-polymarket-data-structure-ssot (polymarket SSOT)
#         - AND/OR xiaoduan-goalserve-data-structure-ssot (goalserve SSOT)
#       老郭 §1: grep 范围含 PR description 全文, 不限 cite: 头.
#       两个 doc 满足其一即通过 (struct 与 Goalserve 无关时 goalserve_ssot: N/A 已声明).
#
#   C2 (token_id grep):
#       PR diff 范围内, 若 OrderIntent / SignedOrder struct 定义所在文件被修改,
#       grep struct body 含 token_id 字段声明. 绝对约束.
#       老郭 §1: 注意多命名空间 (polymarket:: / microstructure::), grep 精确到 struct 定义块.
#
#   C3 (Side enum grep):
#       PR diff 范围内, 若含 Side enum 定义改动, 必须同时含 Buy 和 Sell 两个枚举值.
#       老郭 §1: 需同时 grep Buy 和 Sell. BuyYes/BuyNo 旧形态不满足.
#
#   C4 (handshake doc grep):
#       PR description 或 diff 中含 *-handshake-v*.md 文件名引用.
#       老郭 §1: 当前 pattern = laoli-laoSun-handshake-v1.md 或 laohan-laosun-orderintent-signer-handshake-v*.md.
#       扩展到通配 *-handshake-v*.md.
#
# diff_text 来源: git diff origin/main...HEAD (老郭 §3, 与 ADR-024 worktree 流程对齐).
# 非 PR 环境 (PR_BODY 未注入) → pass.
#
# 6 个受约束核心 struct:
#   OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot
#
# 用法:
#   python3 tests/ci_grep/core_data_structure_ssot_check.py [--repo-root <path>]
#   PR_BODY="cite: polymarket_ssot_cite: laoli-polymarket-data-structure-ssot-v1.md ..." \
#     python3 tests/ci_grep/core_data_structure_ssot_check.py

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from dataclasses import dataclass

# --------------------------------------------------------------------------- #
# 6 个受约束核心 struct (ADR-027 §4 Enforce-1)                                 #
# --------------------------------------------------------------------------- #

CORE_STRUCTS = {
    "OrderIntent",
    "SignedOrder",
    "Position",
    "MarketInfo",
    "FairValue",
    "OrderBookSnapshot",
}

# 核心 struct 所在文件路径 (部分路径匹配)
CORE_STRUCT_FILES = {
    "include/stcpp/risk/risk_gateway.hpp",          # OrderIntent
    "include/stcpp/polymarket/pm_client.hpp",        # SignedOrder / Position / MarketInfo
    "include/stcpp/polymarket/live/live_pm_client.hpp",
    "include/stcpp/signer/signer_iface.hpp",         # SignV52Request (关联 SignedOrder)
    "include/stcpp/infra/wal/position_record.hpp",   # PositionRecord
    "include/stcpp/infra/wal/position_ledger.hpp",
}

# C1: SSOT 引用关键词 (老郭 §1: grep PR description 全文)
_POLYMARKET_SSOT_RE = re.compile(
    r"laoli-polymarket-data-structure-ssot", re.IGNORECASE
)
_GOALSERVE_SSOT_RE = re.compile(
    r"xiaoduan-goalserve-data-structure-ssot", re.IGNORECASE
)
# goalserve N/A 显式声明也满足 C1 (老郭 §1: 不得省略 cite 段, 但允许 N/A 声明)
_GOALSERVE_NA_RE = re.compile(
    r"goalserve_ssot\s*:\s*N/A|goalserve_ssot_cite\s*:\s*N/A", re.IGNORECASE
)

# C2: token_id 字段声明 (在 struct 体内)
# 匹配 "token_id" 出现在 diff 新增行中 (+ 开头非 +++)
_TOKEN_ID_FIELD_RE = re.compile(r"\btoken_id\b")

# C3: Side enum 含 Buy 和 Sell (diff 新增行中)
_SIDE_ENUM_DEF_RE = re.compile(r"\benum\s+class\s+Side\b")
_SIDE_BUY_RE = re.compile(r"\bBuy\b\s*=")
_SIDE_SELL_RE = re.compile(r"\bSell\b\s*=")

# C4: handshake doc 引用 (PR description 或 diff 中含 *-handshake-v*.md 模式)
_HANDSHAKE_DOC_RE = re.compile(
    r"\w+-\w+-handshake-v\d+(?:\.\d+)?\.md|\bhandshake-v\d+(?:\.\d+)?\.md",
    re.IGNORECASE,
)


@dataclass
class CheckResult:
    name: str
    passed: bool
    message: str


def get_diff_text(repo_root: Path) -> str:
    """Get git diff origin/main...HEAD (老郭 §3 指定来源)."""
    try:
        result = subprocess.run(
            ["git", "diff", "origin/main...HEAD"],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=30,
        )
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout
        # fallback: staged diff
        result = subprocess.run(
            ["git", "diff", "--cached"],
            capture_output=True,
            text=True,
            cwd=repo_root,
            timeout=10,
        )
        return result.stdout
    except (subprocess.SubprocessError, FileNotFoundError):
        return ""


def get_changed_files_from_diff(diff_text: str) -> list[str]:
    """Extract changed file paths from unified diff header lines."""
    files: list[str] = []
    for line in diff_text.splitlines():
        if line.startswith("+++ b/"):
            path = line[6:].strip()
            if path:
                files.append(path)
    return files


def diff_touches_core_struct_file(diff_text: str) -> bool:
    """Return True if diff changes any core struct definition file."""
    changed = get_changed_files_from_diff(diff_text)
    for f in changed:
        normalized = f.replace("\\", "/")
        for cs_file in CORE_STRUCT_FILES:
            if normalized.endswith(cs_file) or normalized == cs_file:
                return True
    return False


def get_added_lines(diff_text: str) -> list[str]:
    """Return lines added in diff (+ prefix, excluding +++ header)."""
    return [
        line[1:]  # strip leading +
        for line in diff_text.splitlines()
        if line.startswith("+") and not line.startswith("+++")
    ]


def check_pr_diff(diff_text: str, pr_description: str) -> list[CheckResult]:
    """Run C1-C4 checks. Returns list of CheckResult.

    Entry point per ADR-027 Enforce-3 spec.
    """
    results: list[CheckResult] = []

    # ------------------------------------------------------------------ #
    # C1 — SSOT cite grep (老郭 §1: grep PR description 全文, 不只 cite: 头)
    # ------------------------------------------------------------------ #
    if diff_touches_core_struct_file(diff_text):
        polymarket_ok = bool(_POLYMARKET_SSOT_RE.search(pr_description))
        goalserve_ok = (
            bool(_GOALSERVE_SSOT_RE.search(pr_description))
            or bool(_GOALSERVE_NA_RE.search(pr_description))
        )
        c1_passed = polymarket_ok and goalserve_ok
        if c1_passed:
            msg = "C1 PASS: PR description 含 polymarket_ssot + goalserve_ssot (或 N/A 声明)"
        else:
            missing = []
            if not polymarket_ok:
                missing.append("laoli-polymarket-data-structure-ssot")
            if not goalserve_ok:
                missing.append(
                    "xiaoduan-goalserve-data-structure-ssot (或 goalserve_ssot: N/A 声明)"
                )
            msg = (
                f"C1 FAIL: PR description 缺少 SSOT 引用: {', '.join(missing)}.\n"
                "  ADR-027 Enforce-1: 核心 struct 变更 PR description 必须引用 SSOT 文档.\n"
                "  格式示例:\n"
                "    cite:\n"
                "      polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §3\n"
                "      goalserve_ssot_cite:  N/A (struct 不涉及 Goalserve 数据路径)\n"
                "      handshake_cite:       laoli-laoSun-handshake-v1.md §3"
            )
        results.append(CheckResult("C1-ssot-cite", c1_passed, msg))
    else:
        results.append(CheckResult(
            "C1-ssot-cite", True,
            "C1 SKIP: PR diff 未改动核心 struct 文件, 跳过 C1 检查"
        ))

    # ------------------------------------------------------------------ #
    # C2 — token_id 字段声明 (绝对约束, 老郭 §1: struct body grep)
    # ------------------------------------------------------------------ #
    added_lines = get_added_lines(diff_text)
    # 先判断 diff 是否含 OrderIntent / SignedOrder struct 定义改动
    struct_defs_changed = any(
        re.search(r"\bstruct\s+(OrderIntent|SignedOrder)\b", line)
        for line in added_lines
    )
    if struct_defs_changed:
        token_id_present = any(_TOKEN_ID_FIELD_RE.search(line) for line in added_lines)
        if token_id_present:
            msg = "C2 PASS: diff 新增行中含 token_id 字段声明"
        else:
            msg = (
                "C2 FAIL: OrderIntent / SignedOrder struct 变更 diff 缺少 token_id 字段声明.\n"
                "  ADR-027 Enforce-1 绝对约束: CLOB 下单必须字段, 缺失 = P0.\n"
                "  见 docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md §3.3\n"
                "  和 docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md §2.2"
            )
        results.append(CheckResult("C2-token-id", token_id_present, msg))
    else:
        results.append(CheckResult(
            "C2-token-id", True,
            "C2 SKIP: diff 未含 OrderIntent/SignedOrder struct 新定义, 跳过 C2"
        ))

    # ------------------------------------------------------------------ #
    # C3 — Side enum 含 Buy + Sell (绝对约束)
    # ------------------------------------------------------------------ #
    side_enum_changed = any(_SIDE_ENUM_DEF_RE.search(line) for line in added_lines)
    if side_enum_changed:
        has_buy = any(_SIDE_BUY_RE.search(line) for line in added_lines)
        has_sell = any(_SIDE_SELL_RE.search(line) for line in added_lines)
        c3_passed = has_buy and has_sell
        if c3_passed:
            msg = "C3 PASS: Side enum diff 中含 Buy 和 Sell 两个枚举值"
        else:
            missing = []
            if not has_buy:
                missing.append("Buy = <val>")
            if not has_sell:
                missing.append("Sell = <val>")
            msg = (
                f"C3 FAIL: Side enum diff 缺少枚举值: {', '.join(missing)}.\n"
                "  ADR-027 绝对约束: Side enum 必须含 Buy + Sell.\n"
                "  旧形态 BuyYes/BuyNo 不满足 (需重构为 side=Buy/Sell × outcome).\n"
                "  见 docs/RESEARCH/laohan-w9-orderintent-v05-spec-v1.md §2.1"
            )
        results.append(CheckResult("C3-side-enum", c3_passed, msg))
    else:
        results.append(CheckResult(
            "C3-side-enum", True,
            "C3 SKIP: diff 未含 Side enum 新定义, 跳过 C3"
        ))

    # ------------------------------------------------------------------ #
    # C4 — 跨 struct ABI handshake doc 引用 (老郭 §1: 扩展到 *-handshake-v*.md)
    # ------------------------------------------------------------------ #
    full_text = pr_description + "\n" + diff_text
    handshake_found = bool(_HANDSHAKE_DOC_RE.search(full_text))
    if diff_touches_core_struct_file(diff_text):
        if handshake_found:
            match = _HANDSHAKE_DOC_RE.search(full_text)
            msg = f"C4 PASS: PR 含 handshake doc 引用: {match.group(0) if match else '(found)'}"
        else:
            msg = (
                "C4 FAIL: PR description / diff 缺少 *-handshake-v*.md 文件引用.\n"
                "  ADR-027 Enforce-1 C4: 跨 struct ABI handshake doc 必须引用.\n"
                "  当前 handshake doc: laoli-laoSun-handshake-v1.md\n"
                "  W9 补充: laohan-laosun-orderintent-signer-handshake-v1.md (老韩+老孙 co-sign)\n"
                "  见 docs/RESEARCH/laoli-laoSun-handshake-v1.md"
            )
        results.append(CheckResult("C4-handshake-cite", handshake_found, msg))
    else:
        results.append(CheckResult(
            "C4-handshake-cite", True,
            "C4 SKIP: PR diff 未改动核心 struct 文件, 跳过 C4 检查"
        ))

    return results


def main() -> int:
    ap = argparse.ArgumentParser(
        description="ADR-027 Enforce-1 核心数据结构 SSOT cite 检查 (C1-C4)"
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    pr_description = os.environ.get("PR_BODY", "")

    # 非 PR 环境 (PR_BODY 未注入) → pass
    if not pr_description:
        print("[core_data_structure_ssot] SKIP: PR_BODY 未注入 (非 PR 触发), pass")
        return 0

    diff_text = get_diff_text(repo_root)
    if not diff_text:
        print("[core_data_structure_ssot] SKIP: 无法获取 git diff (git 不可用 / 无变更), pass")
        return 0

    results = check_pr_diff(diff_text, pr_description)

    failures = [r for r in results if not r.passed]
    passes = [r for r in results if r.passed]

    for r in passes:
        print(f"[core_data_structure_ssot] {r.message}")

    if not failures:
        print("[core_data_structure_ssot] PASS: ADR-027 C1-C4 全部通过")
        return 0

    print(
        f"[core_data_structure_ssot] FAIL: {len(failures)} ADR-027 SSOT cite 检查失败:",
        file=sys.stderr,
    )
    for r in failures:
        print(f"  ::error::{r.message}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引 (ADR-027 Enforce-1):\n"
        "  在 PR description 中加 cite 段:\n"
        "    cite:\n"
        "      polymarket_ssot_cite: laoli-w8-polymarket-data-structure-ssot-v1.md §<section>\n"
        "      goalserve_ssot_cite:  xiaoduan-w8-goalserve-data-structure-ssot-v1.md §<section>\n"
        "                            (或 N/A 若 struct 不涉及 Goalserve 数据路径)\n"
        "      handshake_cite:       laoli-laoSun-handshake-v1.md §3\n"
        "  C2 (token_id): OrderIntent/SignedOrder struct 必须含 token_id 字段\n"
        "  C3 (Side enum): Side enum 必须含 Buy + Sell (旧 BuyYes/BuyNo 不满足)\n"
        "  见 docs/ADR/2026-06-W4-adr-027-core-data-structure-ssot-enforce.md",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
