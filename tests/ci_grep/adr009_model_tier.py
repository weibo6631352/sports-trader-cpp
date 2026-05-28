#!/usr/bin/env python3
# tests/ci_grep/adr009_model_tier.py — ADR-009 v2 model 分级检查
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.2 W6 Wave 28
# 关联: docs/ADR/2026-05-28-agent-model-tiering.md (ADR-009)
#       docs/RESEARCH/laogao-pr-review-v1.2.md §2.7
#
# 规则 (ADR-009 v2 = 全员 Sonnet, Opus 严格收口):
#   派单 prompt 文件 (.claude/agents/*.md) 或 docs/ 文档中含 "model: opus" 时:
#     必须紧跟 "例外:" 段落 (同文件内 20 行内)
#
#   不允许的 Opus 例外理由 (ADR-009 v2 老板二次校正 2026-05-28):
#     - "管理层身份"
#     - "我觉得 Sonnet 不够好"
#     - "task 复杂"
#
#   Sonnet 是默认. Opus 只允许:
#     - 经 GM 老雷明确 ack 的例外 (带 GM signoff 链接)
#     - 多轮深度推理 + GM ack (不允许 "task 复杂" 作为理由)
#
# 扫描范围: .claude/agents/*.md  +  docs/**/*.md  +  .github/**/*.md
#   (不扫 src/ — 代码文件不写 model: opus)
#
# 用法:
#   python3 tests/ci_grep/adr009_model_tier.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 检测目标                                                                      #
# --------------------------------------------------------------------------- #

SCAN_PATTERNS = [
    ".claude/agents",
    "docs",
    ".github",
]

# 这些文件本身是规则说明文档, 允许出现任何 model: opus 格式举例和不允许理由枚举
# 否则规则文档自身会触发误报
EXEMPT_PATHS_PATTERNS = [
    "agent-model-tiering",   # ADR-009 规则定义本身
    "laogao-pr-review-",     # 老高 PR review 规则文档 (引用 ADR-009 规则)
    "PULL_REQUEST_TEMPLATE", # PR 模板 (模板中示例性列举)
]

# 匹配 "model: opus" (大小写不敏感)
_MODEL_OPUS_RE = re.compile(r"model\s*:\s*opus", re.IGNORECASE)

# 匹配 "例外:" 或 "例外段" 或 "exception:"
_EXCEPTION_LABEL_RE = re.compile(r"例外[：:]|exception\s*:", re.IGNORECASE)

# 不允许的理由 (原文匹配)
_DISALLOWED_REASONS: list[tuple[str, re.Pattern[str]]] = [
    ("管理层身份", re.compile(r"管理层身份")),
    ("我觉得 Sonnet 不够好", re.compile(r"我觉得\s*Sonnet\s*不够好")),
    ("task 复杂 (不够具体)", re.compile(r"\btask\s*复杂\b")),
]

EXCEPTION_WINDOW = 20  # lines after "model: opus" to look for 例外:


def check_file(path: Path) -> list[tuple[int, str]]:
    """Return list of (lineno, error_message)."""
    try:
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except OSError:
        return []

    issues: list[tuple[int, str]] = []
    for i, line in enumerate(lines):
        if not _MODEL_OPUS_RE.search(line):
            continue
        lineno = i + 1

        # Check: 在接下来 EXCEPTION_WINDOW 行内是否有 "例外:" 段
        window = lines[i: i + EXCEPTION_WINDOW]
        window_text = "\n".join(window)
        has_exception = bool(_EXCEPTION_LABEL_RE.search(window_text))

        if not has_exception:
            issues.append((
                lineno,
                f"ADR-009 v2: 'model: opus' 缺少 '例外:' 段 "
                f"(必须在 {EXCEPTION_WINDOW} 行内). "
                "全员默认 Sonnet, Opus 使用须 GM ack + 例外说明."
            ))
            continue  # 没有例外段就不检查理由了

        # Check: 例外理由是否使用了不允许的说法
        for reason_name, reason_re in _DISALLOWED_REASONS:
            if reason_re.search(window_text):
                issues.append((
                    lineno,
                    f"ADR-009 v2: 'model: opus' 例外理由不合规 — '{reason_name}' 不被接受. "
                    "允许的理由: 多轮深度推理 + GM 老雷明确 ack (含 signoff 链接)."
                ))

    return issues


def main() -> int:
    ap = argparse.ArgumentParser(description="ADR-009 v2 model 分级检查")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root

    all_violations: list[tuple[Path, int, str]] = []

    for pattern in SCAN_PATTERNS:
        scan_root = repo_root / pattern
        if not scan_root.exists():
            continue
        # 若是目录则递归, 若是单文件直接扫
        paths = list(scan_root.rglob("*.md")) if scan_root.is_dir() else [scan_root]
        for path in sorted(paths):
            if not path.is_file():
                continue
            # 跳过规则说明文档本身 (ADR-009 定义文档 / PR review 规则文档 / PR 模板)
            path_str = path.name
            if any(exempt in path_str for exempt in EXEMPT_PATHS_PATTERNS):
                continue
            for lineno, msg in check_file(path):
                all_violations.append((path, lineno, msg))

    if not all_violations:
        print("[adr009_model_tier] PASS: 0 ADR-009 v2 model tier violations found")
        return 0

    print(f"[adr009_model_tier] FAIL: {len(all_violations)} violation(s):", file=sys.stderr)
    for path, lineno, msg in all_violations:
        rel = path.relative_to(repo_root)
        print(f"  ::error file={rel},line={lineno}::{msg}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  1. 如需用 Opus: 在 'model: opus' 后 20 行内加 '例外:' 段,\n"
        "     说明具体理由 + GM 老雷 ack signoff 链接.\n"
        "  2. 不可接受的理由: '管理层身份' / '我觉得 Sonnet 不够好' / 'task 复杂'.\n"
        "  3. 默认改回 Sonnet (ADR-009 v2 全员 Sonnet).",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
