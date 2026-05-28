#!/usr/bin/env python3
# tests/ci_grep/persona_boundary_check.py — persona 拒绝任务边界检查 v2
#
# Owner: 老徐 (#33, ai-ops-collaboration, F 顾问团)  v2 W6 Wave 30
# 关联: CLAUDE.md §10 拒接边界
#       docs/RESEARCH/laoxu-subagent-escalate-flow-v0.2.md §6.1 (R-39 边界 enforce)
#       docs/META/escalate-decision-log.md (后置决议落库)
#       docs/RESEARCH/laoxu-w6-persona-boundary-dryrun-v1.md (W6 W3 dry-run)
#
# 规则 (Wave 26 决议 5, GM 错 #4):
#   扫描 docs/MEETINGS/ + docs/RESEARCH/ 下派单 prompt 文件 (*.md),
#   检查派单是否越过对应 persona 的"拒绝任务"边界.
#
# 实现:
#   3 精确规则 (subagent_type 字段) + 47 persona 广义矩阵 (名字 + 越界词)
#   --dry-run <dir>: 只扫指定目录, 不扫全仓 (GM 派单时 pre-check 用)
#   --json: 输出 JSON (老高 PR v1.3 grep 集成兼容)
#
# 豁免:
#   - 含 "CI-EXEMPT:" 的行
#   - 注释行 (<!-- 或 #)
#
# 用法:
#   python3 tests/ci_grep/persona_boundary_check.py [--repo-root <path>]
#   python3 tests/ci_grep/persona_boundary_check.py --dry-run docs/MEETINGS/
#   python3 tests/ci_grep/persona_boundary_check.py --dry-run docs/MEETINGS/ --json

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# --------------------------------------------------------------------------- #
# 3 精确规则 (subagent_type 字段派单 prompt 越界)                               #
# --------------------------------------------------------------------------- #

# 每条: (subagent_type 值, persona 名, 禁止出现的词模式列表)
PRECISE_RULES: list[tuple[str, str, list[str]]] = [
    (
        "quant-signal-research",
        "小程",
        [
            r"落\s*代码",          # "落代码"
            r"\.cpp\b",            # .cpp 文件后缀 (落 C++ 实现)
            r"\.hpp\b",            # .hpp 文件后缀
            r"unit\s*test",        # unit test
            r"单\s*测",            # 单测
            r"实施.*cpp|cpp.*实施", # "实施 cpp" 类表达
        ],
    ),
    (
        "cpo-product-strategy",
        "老钱",
        [
            r"写\s*代码",          # "写代码"
            r"落\s*代码",
            r"cpp\s*实现",
            r"\.cpp\b",
            r"代码\s*实现",
        ],
    ),
    (
        "pm-project-manager",
        "老胡",
        [
            r"cpp\s*实现",         # "cpp 实现"
            r"实现.*\.cpp",
            r"落.*\.cpp",
            r"写.*代码",
        ],
    ),
]

# --------------------------------------------------------------------------- #
# 47 persona 广义矩阵 (中文名 → 禁止越界的派单词组)                               #
# --------------------------------------------------------------------------- #
# 格式: (persona_name_cn, subagent_type_or_number, forbidden_patterns)
# forbidden_patterns 匹配到则说明派单越过了该 persona 的"拒绝任务"边界
# 只对 docs/MEETINGS/ + docs/RESEARCH/ 中明确 "派给 <persona>" 的行做检查

PERSONA_MATRIX: list[tuple[str, list[str]]] = [
    # A 系统工程部
    ("老周",    [r"代码\s*实施", r"需求\s*评估"]),          # cpp-chief-architect: 拒绝 代码实施/需求评估
    ("小马",    [r"业务\s*策略", r"网络\s*解析", r"DB\s*写"]),  # cpp-hot-path-engineer
    ("老陈",    [r"业务\s*字段", r"JSON\s*解析"]),           # cpp-network-engineer
    ("小赵",    [r"业务\s*字段", r"DB\s*schema"]),           # cpp-serialization-engineer
    ("老王",    [r"业务\s*字段", r"运行时.*读.*DB"]),         # cpp-persistence-engineer
    # B 风控合规部
    ("老韩",    [r"策略\s*本身", r"签名\s*实现"]),           # risk-engineer
    ("老沈",    [r"代码", r"合规"]),                         # security-engineer
    ("老黄",    [r"安全\s*实施", r"审计\s*实施"]),           # compliance-legal
    # C 量化研究部
    ("小梁",    [r"代码\s*实施", r"体育\s*专精", r"微观\s*结构"]),  # financial-expert
    ("小程",    [r"落\s*代码", r"回\s*测", r"\.cpp\b"]),     # quant-signal-research
    ("小蒋",    [r"生产\s*代码", r"数据\s*清洗"]),           # quant-backtest
    ("小袁",    [r"代码\s*实施", r"信号.*α|α.*信号"]),        # quant-microstructure
    # D 数据基础设施部
    ("小余",    [r"实时\s*业务流", r"查询"]),                # data-etl
    ("小董",    [r"数据\s*清洗", r"实施"]),                  # data-stats
    ("小田",    [r"实时\s*DB", r"ETL"]),                     # data-warehouse
    # E 产品业务保障部
    ("老胡",    [r"cpp\s*实现", r"决策", r"spec\s*拆解"]),   # pm-project-manager
    ("小颖",    [r"PRD", r"ticket\s*跟进"]),                 # requirements-analyst
    ("小杜",    [r"战略", r"进度"]),                         # product-manager
    ("小宋",    [r"生产\s*代码"]),                           # test-replay-engineer
    ("小苏",    [r"backend\s*endpoint", r"权限\s*模型"]),     # frontend-engineer
    ("小米",    [r"spec", r"PRD"]),                          # doc-curator
    # F 顾问团
    ("老郭",    [r"架构\s*设计\s*本身", r"代码\s*review"]),  # chief-architecture-reviewer
    ("老高",    [r"架构\s*设计", r"性能", r"安全\s*audit"]), # code-quality-reviewer
    ("老何",    [r"架构\s*设计", r"性能\s*调优"]),           # modern-cpp-advisor
    ("老张",    [r"C\+\+\s*idiom", r"toolchain\s*部署"]),    # rust-advisor
    ("老徐",    [r"实施\s*迁移", r"Goalserve\s*深度"]),      # ai-ops-collaboration
    ("小白",    [r"agent\s*班底", r"传统.*ML", r"决策\s*核心"]),  # ai-llm-advisor
    ("老叶",    [r"私钥\s*存储", r"下单\s*决策"]),           # crypto-signing-expert
    ("老钱",    [r"代码", r"技术\s*选型", r"PRD"]),          # cpo-product-strategy
    # 其他
    ("老孙",    [r"wire\s*实现", r"JSON\s*parse"]),          # polymarket-protocol-expert (老李)
    ("老李",    [r"wire\s*实现", r"JSON\s*parse"]),          # polymarket-protocol-expert
    ("老彭",    [r"wire", r"代码"]),                         # betting-industry-expert
    ("小邓",    [r"v1\s*不参与", r"ETL"]),                   # ml-engineer
    ("老唐",    [r"技术\s*实施", r"合规"]),                  # audit-expert
    ("老姜",    [r"代码", r"埋点"]),                         # performance-engineer
    ("小林",    [r"技术\s*决策", r"战略.*产品", r"代码.*架构"]),  # hr-talent-manager
    ("小尤",    [r"bug\s*修复", r"设计\s*实现", r"性能\s*优化"]),  # ux-experience-evaluator
    ("小宫",    [r"代码.*修\s*bug", r"自动化\s*测试", r"性能\s*基准"]),  # dogfood-tester
]

# --------------------------------------------------------------------------- #
# 扫描配置                                                                       #
# --------------------------------------------------------------------------- #

SCAN_DIRS_REL = [
    "docs/MEETINGS",
    "docs/RESEARCH",
]

# 豁免文件: 规则文档本身 + PR review 文档 (里面引用了各 persona 名做说明)
EXEMPT_FILE_KEYWORDS = [
    "laogao-pr-review",
    "laogao-code-conventions",
    "laoxu-subagent-escalate",
    "gm-self-mistakes-log",
    "employee-registry",
    "hr-pulse",
    "sop-v1",
]

_EXEMPT_RE = re.compile(r"CI-EXEMPT:|^\s*<!--|^\s*#")
_COMMENT_LINE_RE = re.compile(r"^\s*(?:<!--|#|\s*$)")


def is_exempt_line(line: str) -> bool:
    if "CI-EXEMPT:" in line:
        return True
    stripped = line.lstrip()
    if stripped.startswith("<!--") or stripped.startswith("#"):
        return True
    return False


# --------------------------------------------------------------------------- #
# 精确规则检查: subagent_type= 行 + 上下文 20 行                                 #
# --------------------------------------------------------------------------- #

_SUBAGENT_TYPE_RE = re.compile(r"subagent_type\s*=\s*([A-Za-z0-9_-]+)")


def check_precise_rules(path: Path, lines: list[str]) -> list[tuple[int, str]]:
    issues: list[tuple[int, str]] = []
    for i, line in enumerate(lines):
        if is_exempt_line(line):
            continue
        m = _SUBAGENT_TYPE_RE.search(line)
        if not m:
            continue
        agent_type = m.group(1).lower()
        # 查找匹配的精确规则
        for rule_type, persona_name, forbidden_pats in PRECISE_RULES:
            if agent_type != rule_type.lower():
                continue
            # 扫描上下文 (当前行 + 后 30 行, 因为 prompt 一般在声明后)
            ctx_start = max(0, i - 5)
            ctx_end = min(len(lines), i + 31)
            ctx = "\n".join(lines[ctx_start:ctx_end])
            for pat in forbidden_pats:
                if re.search(pat, ctx, re.IGNORECASE):
                    issues.append((
                        i + 1,
                        f"persona 越界: subagent_type={rule_type} ({persona_name}) "
                        f"派单 prompt 含越界词 '{pat}' — "
                        f"{persona_name} 拒绝此类任务 (CLAUDE.md §10 拒绝任务边界)"
                    ))
                    break  # 同一 pat 只报一次
    return issues


# --------------------------------------------------------------------------- #
# 广义矩阵检查: "派给 <persona>" 行 + 上下文                                      #
# --------------------------------------------------------------------------- #

def _build_dispatch_re(name: str) -> re.Pattern[str]:
    return re.compile(
        r"(?:派给|forward[：:]\s*|派\s*单\s*给|spec\s*by)\s*" + re.escape(name),
        re.IGNORECASE,
    )


_PERSONA_DISPATCH_RES = [
    (name, _build_dispatch_re(name), [re.compile(p, re.IGNORECASE) for p in pats])
    for name, pats in PERSONA_MATRIX
]


def check_persona_matrix(path: Path, lines: list[str]) -> list[tuple[int, str]]:
    issues: list[tuple[int, str]] = []
    for i, line in enumerate(lines):
        if is_exempt_line(line):
            continue
        for persona_name, dispatch_re, forbidden_res in _PERSONA_DISPATCH_RES:
            if not dispatch_re.search(line):
                continue
            # 取上下文 40 行
            ctx_start = max(0, i - 2)
            ctx_end = min(len(lines), i + 41)
            ctx = "\n".join(lines[ctx_start:ctx_end])
            for forbidden_re in forbidden_res:
                if forbidden_re.search(ctx):
                    issues.append((
                        i + 1,
                        f"persona 越界: 派单给 {persona_name} 但 prompt 含越界词 "
                        f"'{forbidden_re.pattern}' — {persona_name} 的拒绝任务边界"
                    ))
                    break
    return issues


# --------------------------------------------------------------------------- #
# 主扫描                                                                        #
# --------------------------------------------------------------------------- #

def is_exempt_file(path: Path) -> bool:
    name_lower = path.name.lower()
    return any(kw in name_lower for kw in EXEMPT_FILE_KEYWORDS)


def scan_file(path: Path) -> list[tuple[int, str]]:
    if is_exempt_file(path):
        return []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return []

    lines = text.splitlines()
    issues: list[tuple[int, str]] = []
    issues.extend(check_precise_rules(path, lines))
    issues.extend(check_persona_matrix(path, lines))
    return issues


def scan_directory(scan_root: Path) -> list[tuple[Path, int, str]]:
    """Scan all .md files under scan_root recursively."""
    violations: list[tuple[Path, int, str]] = []
    if not scan_root.exists():
        return violations
    for path in sorted(scan_root.rglob("*.md")):
        if not path.is_file():
            continue
        for lineno, msg in scan_file(path):
            violations.append((path, lineno, msg))
    return violations


def format_json(
    violations: list[tuple[Path, int, str]],
    repo_root: Path,
    dry_run: bool,
    scan_target: str,
) -> str:
    """Return JSON-formatted result (老高 PR v1.3 grep 集成兼容)."""
    result = {
        "tool": "persona_boundary_check",
        "version": "v2",
        "dry_run": dry_run,
        "scan_target": scan_target,
        "violation_count": len(violations),
        "status": "PASS" if not violations else "FAIL",
        "violations": [
            {
                "file": str(p.relative_to(repo_root)) if repo_root else str(p),
                "line": lineno,
                "message": msg,
            }
            for p, lineno, msg in violations
        ],
    }
    return json.dumps(result, ensure_ascii=False, indent=2)


def main() -> int:
    ap = argparse.ArgumentParser(description="persona 拒绝任务边界检查 v2")
    ap.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help="仓库根目录 (默认自动检测)",
    )
    ap.add_argument(
        "--dry-run",
        metavar="DIR",
        default=None,
        help=(
            "dry-run 模式: 只扫指定目录 (相对或绝对路径). "
            "GM 派单时 pre-check 使用. "
            "例: --dry-run docs/MEETINGS/"
        ),
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="JSON 输出 (老高 PR v1.3 grep 集成兼容)",
    )
    args = ap.parse_args()
    repo_root: Path = args.repo_root
    use_json: bool = args.json

    all_violations: list[tuple[Path, int, str]] = []

    if args.dry_run is not None:
        # dry-run 模式: 只扫指定目录
        target = Path(args.dry_run)
        if not target.is_absolute():
            target = (repo_root / target).resolve()
        scan_target = str(target)
        all_violations = scan_directory(target)
    else:
        # 全仓扫描
        scan_target = ", ".join(SCAN_DIRS_REL)
        for scan_dir_rel in SCAN_DIRS_REL:
            scan_root = repo_root / scan_dir_rel
            all_violations.extend(scan_directory(scan_root))

    if use_json:
        dry_run_flag = args.dry_run is not None
        print(format_json(all_violations, repo_root, dry_run_flag, scan_target))
        return 0 if not all_violations else 1

    if not all_violations:
        mode = f"dry-run({args.dry_run})" if args.dry_run else "full"
        print(f"[persona_boundary_check] PASS [{mode}]: 0 persona 越界违例")
        return 0

    mode = f"dry-run({args.dry_run})" if args.dry_run else "full"
    print(
        f"[persona_boundary_check] FAIL [{mode}]: {len(all_violations)} violation(s):",
        file=sys.stderr,
    )
    for path, lineno, msg in all_violations:
        try:
            rel = path.relative_to(repo_root)
        except ValueError:
            rel = path
        print(f"  ::error file={rel},line={lineno}::{msg}", file=sys.stderr)

    print("", file=sys.stderr)
    print(
        "修复指引:\n"
        "  1. 确认派单 prompt 未超出该 persona 的'拒绝任务'边界\n"
        "     (见 .claude/agents/NN-*.md §拒绝任务 + CLAUDE.md §10)\n"
        "  2. 若确认合理: 在该行加 // CI-EXEMPT: <理由> 后 @老郭 24h 仲裁\n"
        "  3. 参考 CLAUDE.md §7.8 第 4 题: 派单 prompt 是否越过该 persona 拒绝任务边界?\n"
        "  4. escalate 路径见 docs/RESEARCH/laoxu-subagent-escalate-flow-v0.2.md §2",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
