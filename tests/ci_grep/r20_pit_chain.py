#!/usr/bin/env python3
# tests/ci_grep/r20_pit_chain.py — R-20 第 7/8 项: PIT chain 时间戳反模式闸
#
# Owner: 老高 (#17, code-quality-reviewer, F 顾问团)  v1.2 W6 Wave 28
# 关联: CLAUDE.md §8 红线 R-20 (4 时间戳契约)
#       docs/RESEARCH/laogao-pr-review-v1.2.md §2.1
#       .github/workflows/pr.yml job redline-grep-v1.2
#
# 规则 (来自决议 #3 + old §2.1):
#   第 7 项: data_source_ts 严禁赋值 = now() / system_clock / clock_realtime
#            (时间戳优先用数据源自带, 禁本地 now() 替代上游 ts)
#   第 8 项: event_ts 与 ingestion_ts 严禁直接互赋
#            (4 ts 不可混 — event_ts 是事件发生上游标, ingestion_ts 是本地落地时)
#
# 豁免:
#   - tests/unit/** tests/sim/** tests/replay/** tests/chaos/**
#     (测试 fixture 允许伪造 ts — 但建议注释 // TEST_TS_FIXTURE)
#   - 注释行 (以 // 或 * 开头的行)
#   - 含 // CI-EXEMPT: <理由> 的行 (需走老郭 24h 仲裁)
#
# 用法:
#   python3 tests/ci_grep/r20_pit_chain.py [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 扫描根 (src + include, 排除 tests/)                                          #
# --------------------------------------------------------------------------- #
SCAN_DIRS_REL = ["src", "include"]

EXCLUDE_SUFFIXES = {".md", ".txt", ".json", ".yaml", ".yml"}

# --------------------------------------------------------------------------- #
# 反模式 (每条: description, compiled_re, error_label)                         #
# --------------------------------------------------------------------------- #

# 第 7 项: data_source_ts = <任何本地时钟>
# 匹配赋值语句, 排除注释行和含 CI-EXEMPT 的行
_R20_7_RE = re.compile(
    r"""data_source_ts\s*=\s*          # 赋值左侧
        (?:                            # 右侧任意本地时钟表达式
            std::chrono::|             # std::chrono::...
            clock_|                    # clock_gettime / clock_realtime 等
            system_clock|              # system_clock::now()
            monotonic_now|             # 项目 helper 如 monotonic_now()
            epoch_now\(\)|             # epoch_now()
            \bNowRealtimeNs\(\)|       # pit::NowRealtimeNs()  — 这是 ingestion 的合规写法
            \bnow\(\)                  # 裸 now()
        )""",
    re.VERBOSE,
)

# 第 8 项: event_ts <=> ingestion_ts 互赋
_R20_8_RE = re.compile(
    r"event_ts\s*=\s*ingestion_ts"
    r"|ingestion_ts\s*=\s*event_ts"
)

# 用于判断"注释行": 去掉开头空白后以 // 或 * 开头
_COMMENT_LINE_RE = re.compile(r"^\s*(?://|\*)")


def is_exempt(line: str) -> bool:
    """True if the line is a comment or carries CI-EXEMPT tag."""
    if _COMMENT_LINE_RE.match(line):
        return True
    if "CI-EXEMPT:" in line:
        return True
    return False


def scan_file(path: Path) -> list[tuple[int, str, str]]:
    """Return list of (lineno, pattern_label, line) for violations."""
    if path.suffix in EXCLUDE_SUFFIXES:
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    violations: list[tuple[int, str, str]] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        if is_exempt(line):
            continue
        if _R20_7_RE.search(line):
            violations.append((lineno, "R-20-7 data_source_ts=now()", line.rstrip()))
        if _R20_8_RE.search(line):
            violations.append((lineno, "R-20-8 event_ts<=>ingestion_ts", line.rstrip()))
    return violations


def main() -> int:
    ap = argparse.ArgumentParser(
        description="R-20 第 7/8 项 PIT chain 时间戳反模式扫描"
    )
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
    )
    args = ap.parse_args()

    scan_roots = [args.repo_root / d for d in SCAN_DIRS_REL]

    all_violations: list[tuple[Path, int, str, str]] = []
    for root in scan_roots:
        if not root.exists():
            continue
        for path in sorted(root.rglob("*")):
            if not path.is_file():
                continue
            if path.suffix not in {".cpp", ".cc", ".hpp", ".h", ".cxx"}:
                continue
            hits = scan_file(path)
            for lineno, label, line in hits:
                all_violations.append((path, lineno, label, line))

    if not all_violations:
        print("[r20_pit_chain] PASS: 0 R-20-7/8 violations found")
        return 0

    print(f"[r20_pit_chain] FAIL: {len(all_violations)} violation(s):", file=sys.stderr)
    for path, lineno, label, line in all_violations:
        rel = path.relative_to(args.repo_root)
        print(f"  ::error file={rel},line={lineno}::{label}: {line}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  R-20-7: data_source_ts 必须从 wire payload 抽取 (上游字段), 严禁本地 now() 替代.\n"
        "  R-20-8: event_ts 与 ingestion_ts 语义不同, 不可互赋.\n"
        "  合规例外: 加 // CI-EXEMPT: <理由> 后提单 @老郭 24h 仲裁.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
