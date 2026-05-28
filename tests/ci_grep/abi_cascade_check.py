#!/usr/bin/env python3
# tests/ci_grep/abi_cascade_check.py — ABI cascade 下游 audit 清单检查
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.4 W7 Wave 33
# 关联: docs/RESEARCH/laogao-pr-review-v1.4.md §2.14
#       docs/RESEARCH/laoli-laoSun-handshake-v1.md (ABI 改动等级)
#       .github/workflows/pr.yml job ci-grep-abi-cascade
#
# 背景 (老王 W6 Wave 32 新发现):
#   PR 声称 "ABI changed" 或 "struct ... added field" 时,
#   下游使用方可能静默受影响 (二进制布局变化 / 序列化不兼容).
#   本 check 要求 ABI 变更 PR 显式列"下游 audit 清单".
#
# 规则:
#   触发条件 (PR description 含以下任一关键词时触发):
#     - "ABI changed" / "ABI 变更" / "struct.*added field" / "struct.*new field"
#     - "added member" / "新增字段" / "新增成员"
#   触发后必须含:
#     "下游 audit 清单" (不含则 WARNING)
#
# 规则等级: WARNING (不 FAIL), 因 ABI cascade 有时是合理渐进式变更.
# 若 PR description 含 "ABI cascade reviewed" → 视同已 ack, pass.
#
# 用法:
#   python3 tests/ci_grep/abi_cascade_check.py [--repo-root <path>]
#   PR_BODY="ABI changed: added field ts_ns to MarketState" \
#     python3 tests/ci_grep/abi_cascade_check.py

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 正则                                                                          #
# --------------------------------------------------------------------------- #

# 触发关键词 (PR description 含任一则检查下游清单)
_ABI_CHANGE_TRIGGER_RE = re.compile(
    r"ABI\s+changed|ABI\s+变更|struct\s+\w+.*added\s+field|struct\s+\w+.*new\s+field"
    r"|added\s+member|新增字段|新增成员|field\s+added",
    re.IGNORECASE | re.UNICODE,
)

# 下游 audit 清单标记
_DOWNSTREAM_AUDIT_RE = re.compile(
    r"下游\s*audit\s*清单|downstream\s+audit\s+list|下游.*审计.*清单",
    re.IGNORECASE | re.UNICODE,
)

# 已 ack 豁免标记
_CASCADE_REVIEWED_RE = re.compile(
    r"ABI\s+cascade\s+reviewed|abi_cascade\s*=\s*reviewed|ABI\s+cascade\s*[:：]\s*reviewed",
    re.IGNORECASE | re.UNICODE,
)


def main() -> int:
    ap = argparse.ArgumentParser(description="ABI cascade 下游 audit 清单检查")
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    ap.parse_args()  # 解析但本脚本只看 env

    pr_body = os.environ.get("PR_BODY", "")

    if not pr_body:
        print("[abi_cascade_check] SKIP: PR_BODY 未注入 (非 PR 触发), pass")
        return 0

    # 检查是否触发
    trigger_match = _ABI_CHANGE_TRIGGER_RE.search(pr_body)
    if not trigger_match:
        print("[abi_cascade_check] PASS: PR description 未含 ABI 变更关键词, 跳过")
        return 0

    triggered_by = trigger_match.group(0)
    print(f"[abi_cascade_check] 检测到 ABI 变更声明: '{triggered_by}'")

    # 检查豁免标记
    if _CASCADE_REVIEWED_RE.search(pr_body):
        print("[abi_cascade_check] PASS: 已含 'ABI cascade reviewed' ack, 通过")
        return 0

    # 检查下游 audit 清单
    if _DOWNSTREAM_AUDIT_RE.search(pr_body):
        print("[abi_cascade_check] PASS: 已含下游 audit 清单声明")
        return 0

    # 未含下游清单 → WARNING (不 FAIL)
    print(
        "::warning::[abi_cascade_check] ABI 变更 PR 缺少下游 audit 清单.\n"
        "  触发词: '" + triggered_by + "'\n"
        "  建议: 在 PR description 加 '下游 audit 清单:' 章节, 列出受影响的下游使用方\n"
        "  (consumers / 序列化用到该 struct 的模块 / WAL 格式等)\n"
        "  若已确认无下游影响: 在 PR description 加 'ABI cascade reviewed: no downstream'"
    )

    return 0  # WARNING only, 不 FAIL


if __name__ == "__main__":
    sys.exit(main())
