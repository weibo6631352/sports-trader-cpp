#!/usr/bin/env python3
# scripts/perf_compare.py — 老姜 W4 Wave 21 perf 回归比对
#
# 用法:
#   python3 scripts/perf_compare.py <results_dir> <baseline_json> [--threshold 0.10]
#
# 参数:
#   results_dir       Google Benchmark JSON 输出目录 (本 PR 跑的结果)
#                     每个 .json 内含一组 BM_* (1 个 bench TU 一个 .json)
#   baseline_json     单一 JSON, 合并了所有模块的 main branch baseline
#                     ({"benchmarks": [...]} 同 Google Benchmark 格式)
#   --threshold       p99 退化 fail 阈值 (默认 0.10 = 10%)
#
# 退出码:
#   0  无回归 / baseline 缺失 (首跑)
#   1  至少一条 bench 超阈值 → PR block
#
# 输出:
#   stdout markdown table — CI 直接 cat 到 PR comment / step summary
#
# 注: 该脚本属于离线工具 (非热路径), Python 合规 (CLAUDE.md §10 离线脚本类).
#     不允许进入生产 critical path.

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path


def load_results(results_dir: Path) -> dict[str, dict]:
    """读 results_dir 下所有 *.json, 合并所有 benchmark 记录."""
    by_name: dict[str, dict] = {}
    for jf in sorted(results_dir.glob("*.json")):
        try:
            with jf.open() as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            print(f"WARN: load {jf}: {e}", file=sys.stderr)
            continue
        for b in data.get("benchmarks", []):
            name = b.get("name", "")
            if not name:
                continue
            # Google Benchmark 在 aggregate 模式下会输出 _mean/_median/_stddev 子条目.
            # 我们偏好 median (抗噪).
            if name.endswith("_median"):
                base_name = name[: -len("_median")]
                by_name[base_name] = b
            elif (
                "_mean" not in name
                and "_stddev" not in name
                and "_cv" not in name
                and base_name_of(name) not in by_name
            ):
                # 没有 aggregate 时直接收单条
                by_name[name] = b
    return by_name


def base_name_of(name: str) -> str:
    for suf in ("_mean", "_median", "_stddev", "_cv"):
        if name.endswith(suf):
            return name[: -len(suf)]
    return name


def load_baseline(path: Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    try:
        with path.open() as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"WARN: load baseline {path}: {e}", file=sys.stderr)
        return {}
    by_name: dict[str, dict] = {}
    for b in data.get("benchmarks", []):
        name = b.get("name", "")
        if name.endswith("_median"):
            base = name[: -len("_median")]
            by_name[base] = b
        elif not any(name.endswith(s) for s in ("_mean", "_stddev", "_cv")):
            by_name.setdefault(name, b)
    return by_name


def fmt_time(ns: float, unit_hint: str = "ns") -> str:
    """Google Benchmark 的 cpu_time 默认是 ns, 但 Iterations()/Unit() 可能改单位."""
    if unit_hint == "ms":
        # 转回 ns 以便统一比较
        ns = ns * 1_000_000.0
    if ns < 1_000:
        return f"{ns:.1f} ns"
    if ns < 1_000_000:
        return f"{ns/1000:.2f} us"
    if ns < 1_000_000_000:
        return f"{ns/1_000_000:.2f} ms"
    return f"{ns/1_000_000_000:.2f} s"


def main() -> int:
    ap = argparse.ArgumentParser(description="perf regression comparator (老姜 W4 Wave 21)")
    ap.add_argument("results_dir", type=Path)
    ap.add_argument("baseline_json", type=Path)
    ap.add_argument("--threshold", type=float, default=0.10,
                    help="p99 退化 fail 阈值 (默认 0.10 = 10%)")
    ap.add_argument("--warn-threshold", type=float, default=0.05,
                    help="warning 阈值 (默认 0.05 = 5%)")
    args = ap.parse_args()

    if not args.results_dir.exists():
        print(f"ERROR: results dir not found: {args.results_dir}", file=sys.stderr)
        return 2

    results = load_results(args.results_dir)
    if not results:
        print("ERROR: no benchmark JSON parsed from results dir", file=sys.stderr)
        return 2

    baseline = load_baseline(args.baseline_json)

    if not baseline:
        print("# perf-compare: baseline 缺失 (首跑或 main 未更新)")
        print()
        print("跳过 regression check, 仅汇报当前结果.")
        print()
        print("| bench | cpu_time (median) | iters |")
        print("|---|---|---|")
        for name, b in sorted(results.items()):
            t = b.get("cpu_time", 0.0)
            unit = b.get("time_unit", "ns")
            print(f"| `{name}` | {fmt_time(float(t), unit)} | {b.get('iterations', 0)} |")
        return 0

    fail_count = 0
    warn_count = 0
    lines = [
        "# perf-compare report (老姜 W4 Wave 21)",
        "",
        f"threshold: warning ≥ +{args.warn_threshold*100:.0f}%, "
        f"fail ≥ +{args.threshold*100:.0f}%",
        "",
        "| bench | baseline | current | Δ | verdict |",
        "|---|---|---|---|---|",
    ]

    all_names = sorted(set(results) | set(baseline))
    for name in all_names:
        cur = results.get(name)
        base = baseline.get(name)

        if cur and not base:
            t = float(cur.get("cpu_time", 0.0))
            unit = cur.get("time_unit", "ns")
            lines.append(f"| `{name}` | (new) | {fmt_time(t, unit)} | — | new |")
            continue
        if base and not cur:
            t = float(base.get("cpu_time", 0.0))
            unit = base.get("time_unit", "ns")
            lines.append(f"| `{name}` | {fmt_time(t, unit)} | (missing) | — | DROPPED |")
            warn_count += 1
            continue

        cur_t = float(cur.get("cpu_time", 0.0))
        base_t = float(base.get("cpu_time", 0.0))
        cur_unit = cur.get("time_unit", "ns")
        base_unit = base.get("time_unit", "ns")
        # 转换到 ns 统一
        cur_ns = cur_t * 1_000_000.0 if cur_unit == "ms" else cur_t
        base_ns = base_t * 1_000_000.0 if base_unit == "ms" else base_t

        if base_ns <= 0:
            lines.append(f"| `{name}` | {fmt_time(base_ns)} | {fmt_time(cur_ns)} | (base=0) | skip |")
            continue

        delta = (cur_ns - base_ns) / base_ns

        if delta >= args.threshold:
            verdict = f"FAIL (+{delta*100:.1f}%)"
            fail_count += 1
        elif delta >= args.warn_threshold:
            verdict = f"WARN (+{delta*100:.1f}%)"
            warn_count += 1
        elif delta <= -args.warn_threshold:
            verdict = f"WIN ({delta*100:+.1f}%)"
        else:
            verdict = f"ok ({delta*100:+.1f}%)"

        lines.append(
            f"| `{name}` | {fmt_time(base_ns)} | {fmt_time(cur_ns)} "
            f"| {delta*100:+.1f}% | {verdict} |"
        )

    lines.append("")
    lines.append(f"**summary**: {fail_count} fail, {warn_count} warn")

    print("\n".join(lines))
    return 1 if fail_count > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
