#!/usr/bin/env python3
# tests/ci_grep/adr005_dispatch.py — ADR-005 派单链 PR description 检查
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.2 W6 Wave 28
# 关联: CLAUDE.md §7 协作规范 + ADR-005 部门主管派单 mandate
#       docs/RESEARCH/laogao-pr-review-v1.2.md §2.6
#       .github/workflows/pr.yml job pr-meta-grep (CI 层已内联; 本脚本供本地 dry-run)
#
# 规则:
#   PR description 必须含一行符合以下任一格式:
#     - "spec by <主管>"  (5 战斗单元主管 / 老郭 / 老雷 / 老钱)
#     - "self-spec (F 顾问)"  (顾问团 self 派)
#     - "self-spec (bot)"     (dependabot / forge 元 PR)
#
#   允许的 spec by 对象 (ADR-005 §3):
#     老周 / 老韩 / 小梁 / 小余 / 老胡   — 5 战斗单元主管
#     老郭                               — F 协调人
#     老雷                               — GM (例外: 顾问团 / 紧急 P0 / 跨单元统筹)
#     老钱                               — CPO (平级 GM)
#
#   不写代码 persona (小程/老钱/小杜/小苏) 如出现在 spec by 后:
#     PR description 必须同时含 "spec only" 或 "不写代码" 声明
#
# 本脚本在 CI 中通过环境变量 PR_BODY 接收 PR description.
# 本地 dry-run: echo "spec by 老周 (W6-A-05)" | python3 tests/ci_grep/adr005_dispatch.py
#
# 用法:
#   PR_BODY="..." python3 tests/ci_grep/adr005_dispatch.py
#   cat pr_body.txt | python3 tests/ci_grep/adr005_dispatch.py

from __future__ import annotations

import os
import re
import sys


# --------------------------------------------------------------------------- #
# ADR-005 允许的 spec by 对象 (主管 + 协调人 + GM + CPO)                        #
# --------------------------------------------------------------------------- #
ALLOWED_SPEC_BY = [
    "老周", "老韩", "小梁", "小余", "老胡",  # 5 战斗单元主管
    "老郭",                                    # F 协调人
    "老雷",                                    # GM
    "老钱",                                    # CPO
]

# persona 不写代码 — 若 spec by 这些人, 必带 spec-only 声明
SPEC_ONLY_PERSONAS = ["小程", "老钱", "小杜", "小苏"]

_SPEC_BY_RE = re.compile(
    r"spec\s+by\s+(" + "|".join(re.escape(p) for p in ALLOWED_SPEC_BY) + r")",
    re.IGNORECASE,
)
_SELF_SPEC_RE = re.compile(
    r"self-spec\s*\(\s*(?:F\s*顾问|F\s*advisor|bot)\s*\)",
    re.IGNORECASE,
)

# persona 边界: spec by <不写代码 persona>
_PERSONA_SPEC_RE = {
    p: re.compile(r"spec\s+by\s+" + re.escape(p), re.IGNORECASE)
    for p in SPEC_ONLY_PERSONAS
}
_SPEC_ONLY_DECL_RE = re.compile(
    r"spec\s+only|不写代码|no\s+code",
    re.IGNORECASE,
)


def check_body(body: str) -> list[str]:
    """Return list of error strings; empty = PASS."""
    errors: list[str] = []

    has_spec_by = bool(_SPEC_BY_RE.search(body))
    has_self_spec = bool(_SELF_SPEC_RE.search(body))

    if not has_spec_by and not has_self_spec:
        errors.append(
            "ADR-005: PR description 必须含 'spec by <主管>' (老周/老韩/小梁/小余/老胡/"
            "老郭/老雷/老钱) 或 'self-spec (F 顾问)' 或 'self-spec (bot)'"
        )
        return errors  # 后续检查无意义

    # persona 边界检查
    for persona, pat in _PERSONA_SPEC_RE.items():
        if pat.search(body):
            if not _SPEC_ONLY_DECL_RE.search(body):
                errors.append(
                    f"persona 边界: 'spec by {persona}' 检测到, 但缺少 'spec only, 不写代码' 声明. "
                    f"{persona} 是 spec-only persona, 若 PR diff 含代码改动, 必须声明清楚."
                )

    return errors


def main() -> int:
    # 优先环境变量 PR_BODY (CI 注入), 其次 stdin
    body = os.environ.get("PR_BODY", "")
    if not body and not sys.stdin.isatty():
        body = sys.stdin.read()

    if not body:
        print("::error::PR_BODY 为空, 无法校验 ADR-005 派单链. 请确认 CI 正确注入 PR body.",
              file=sys.stderr)
        return 1

    errors = check_body(body)
    if not errors:
        print("[adr005_dispatch] PASS: ADR-005 spec by 主管标记验证通过")
        return 0

    print(f"[adr005_dispatch] FAIL: {len(errors)} error(s):", file=sys.stderr)
    for e in errors:
        print(f"  ::error::{e}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  在 PR description §0 派单链 节填写:\n"
        "    spec by 老周 (A 单元 W6-A-05 派单 XXX)\n"
        "  或顾问自派:\n"
        "    self-spec (F 顾问)\n"
        "  见 .github/PULL_REQUEST_TEMPLATE.md §0 节说明.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
