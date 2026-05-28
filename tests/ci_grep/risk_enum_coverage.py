#!/usr/bin/env python3
# tests/ci_grep/risk_enum_coverage.py — 21 reject enum × (unit + sim/replay) 全覆盖闸
#
# Owner: 小宋 (test-replay-engineer)  Sprint-2 W3 Wave 18
# 关联: include/stcpp/risk/reject_enum.hpp (老韩 v0.3.1, 21 enum 不增 — ADR-003 C-4)
#       tests/unit/risk_enum_coverage_test.cpp (unit 入口)
#       tests/sim/**/*.cpp, tests/replay/**/*.cpp, tests/chaos/**/*.cpp (sim/replay 层)
#
# 规则:
#   - 21 enum 名必须 每一个 在 tests/unit/ 出现 (unit 覆盖)
#   - 21 enum 名必须 每一个 在 tests/{sim,replay,chaos}/ 出现 (sim/replay/chaos 覆盖)
#   - W3 placeholder 期: sim/replay 覆盖暂作 WARN 不 FAIL (W4-W5 转 FAIL)
#   - unit 缺一 → exit 1
#
# 用法:
#   python3 tests/ci_grep/risk_enum_coverage.py [--strict-sim] [--repo-root <path>]

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


HEADER_REL = "include/stcpp/risk/reject_enum.hpp"
UNIT_DIRS_REL = ["tests/unit"]
SIM_DIRS_REL = ["tests/sim", "tests/replay", "tests/chaos"]

# 解析 enum class RejectCode { ... } 段落, 提取所有大写枚举名
ENUM_DECL_RE = re.compile(
    r"enum\s+class\s+RejectCode\s*:\s*[\w:]+\s*\{(?P<body>.*?)\}",
    re.DOTALL,
)
ENUM_NAME_RE = re.compile(r"^\s*([A-Z][A-Z0-9_]+)\s*=\s*\d+", re.MULTILINE)


def parse_reject_enums(header: Path) -> list[str]:
    text = header.read_text(encoding="utf-8")
    m = ENUM_DECL_RE.search(text)
    if not m:
        print(f"::error::cannot parse enum class RejectCode in {header}", file=sys.stderr)
        sys.exit(2)
    body = m.group("body")
    names = ENUM_NAME_RE.findall(body)
    # 去重保序
    seen: dict[str, None] = {}
    for n in names:
        seen.setdefault(n, None)
    return list(seen.keys())


def scan_dirs_for_enum_refs(roots: list[Path], enums: list[str]) -> dict[str, list[Path]]:
    """返回 enum_name -> 引用它的文件列表."""
    hits: dict[str, list[Path]] = {e: [] for e in enums}
    enum_set = set(enums)
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if path.suffix not in {".cpp", ".cc", ".hpp", ".h", ".yaml", ".yml"}:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for name in enum_set:
                # 边界匹配: 前后非字母数字下划线
                if re.search(rf"(?<![A-Za-z0-9_]){re.escape(name)}(?![A-Za-z0-9_])", text):
                    hits[name].append(path)
    return hits


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    ap.add_argument("--strict-sim", action="store_true",
                    help="W4+ 打开: sim/replay 覆盖缺一也 FAIL (W3 placeholder 期默认 WARN)")
    args = ap.parse_args()

    header = args.repo_root / HEADER_REL
    if not header.exists():
        print(f"::error::missing {header}", file=sys.stderr)
        return 2

    enums = parse_reject_enums(header)
    print(f"[risk_enum_coverage] parsed {len(enums)} RejectCode enum names from {HEADER_REL}")
    if len(enums) != 21:
        print(f"::error::ADR-003 C-4: 21 enum 总数不增 (got {len(enums)})", file=sys.stderr)
        return 1

    unit_roots = [args.repo_root / d for d in UNIT_DIRS_REL]
    sim_roots = [args.repo_root / d for d in SIM_DIRS_REL]

    unit_hits = scan_dirs_for_enum_refs(unit_roots, enums)
    sim_hits = scan_dirs_for_enum_refs(sim_roots, enums)

    unit_missing = [e for e, paths in unit_hits.items() if not paths]
    sim_missing = [e for e, paths in sim_hits.items() if not paths]

    print(f"[risk_enum_coverage] unit covered: {len(enums) - len(unit_missing)}/{len(enums)}")
    print(f"[risk_enum_coverage] sim/replay/chaos covered: "
          f"{len(enums) - len(sim_missing)}/{len(enums)}")

    failed = False
    if unit_missing:
        print("::error::unit 层缺 reject enum 覆盖:", file=sys.stderr)
        for e in unit_missing:
            print(f"  - {e}", file=sys.stderr)
        failed = True

    if sim_missing:
        level = "error" if args.strict_sim else "warning"
        print(f"::{level}::sim/replay/chaos 层缺 reject enum 覆盖 "
              f"(W3 placeholder 期 = warning, --strict-sim 转 error):", file=sys.stderr)
        for e in sim_missing:
            print(f"  - {e}", file=sys.stderr)
        if args.strict_sim:
            failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
