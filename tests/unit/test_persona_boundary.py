#!/usr/bin/env python3
# tests/unit/test_persona_boundary.py — persona_boundary_check.py 单元测试
#
# Owner: 老徐 (#33, ai-ops-collaboration, F 顾问团)  v1 W6 Wave 30
# 关联: tests/ci_grep/persona_boundary_check.py v2
#       docs/RESEARCH/laoxu-w6-persona-boundary-dryrun-v1.md
#       Wave 26 决议 5, GM 错 #4
#
# 测试分级: Python 测试 (pytest), 独立于 ADR-010 cpp/gtest 分级体系
#   目录: tests/unit/ (Python 文件, 非 CMakeLists.txt 管辖)
#   运行: pytest tests/unit/test_persona_boundary.py
#
# 5 case:
#   Case 1: 小程 (quant-signal-research) 越界 — 含 "落代码" → 期望 FAIL
#   Case 2: 小程 越界 — 含 ".cpp" → 期望 FAIL
#   Case 3: 小程 越界 — 含 "unit test" → 期望 FAIL (GM 错 #4 复现)
#   Case 4: 小程 合规 — 纯 spec 派单, 无越界词 → 期望 PASS
#   Case 5: 老胡 合规 — PM 任务 (进度/协调), 无 cpp 实现要求 → 期望 PASS

from __future__ import annotations

import sys
from pathlib import Path

import pytest

# 将 tests/ci_grep 加入 path
_CI_GREP = Path(__file__).resolve().parents[1] / "ci_grep"
sys.path.insert(0, str(_CI_GREP))

from persona_boundary_check import (  # noqa: E402
    check_persona_matrix,
    check_precise_rules,
    scan_file,
)


# --------------------------------------------------------------------------- #
# 工具函数                                                                       #
# --------------------------------------------------------------------------- #


def make_tmp_md(tmp_path: Path, content: str) -> Path:
    """创建临时 markdown 文件供测试扫描."""
    p = tmp_path / "test_dispatch.md"
    p.write_text(content, encoding="utf-8")
    return p


# --------------------------------------------------------------------------- #
# Case 1 — 小程 越界 (含 "落代码")                                              #
# --------------------------------------------------------------------------- #


def test_case1_xiaocheng_violation_luodaima(tmp_path: Path) -> None:
    """
    派单: 小程 (quant-signal-research) + 含 "落代码" — 期望 FAIL (boundary violation).
    对应 GM 错 #4 类型: 派单 prompt 越过 persona 拒绝任务边界.
    """
    content = """\
## W6 Wave 30 派单

subagent_type=quant-signal-research

**任务:** 信号研究完成后请落代码到 signal_stub.cpp.
"""
    path = make_tmp_md(tmp_path, content)
    lines = content.splitlines()
    issues = check_precise_rules(path, lines)
    assert len(issues) >= 1, (
        "Case 1 期望检测到越界: 小程 persona 拒绝'代码', "
        "但 prompt 含 '落代码' — 应 FAIL"
    )
    assert any("小程" in msg and "落" in msg for _, msg in issues), (
        f"Case 1 violation message 应提及 '小程' 和 '落代码', 实际: {issues}"
    )


# --------------------------------------------------------------------------- #
# Case 2 — 小程 越界 (含 ".cpp")                                                #
# --------------------------------------------------------------------------- #


def test_case2_xiaocheng_violation_cpp_extension(tmp_path: Path) -> None:
    """
    派单: 小程 (quant-signal-research) + 含 ".cpp" 文件后缀 — 期望 FAIL.
    GM 错 #4 复现: P0-01 派单含 "signal_stub.cpp" 等 6 个 C++ 交付物.
    """
    content = """\
## P0-01 信号 stub 实施

subagent_type=quant-signal-research

**交付物:**
1. src/stcpp/signal/signal_stub.cpp
2. include/stcpp/signal/signal_stub.hpp
3. CMakeLists.txt 接入

请实施上述文件.
"""
    path = make_tmp_md(tmp_path, content)
    lines = content.splitlines()
    issues = check_precise_rules(path, lines)
    assert len(issues) >= 1, (
        "Case 2 期望检测到越界: 小程 persona 拒绝'代码', "
        "prompt 含 .cpp 后缀 — 应 FAIL"
    )


# --------------------------------------------------------------------------- #
# Case 3 — 小程 越界 (含 "unit test") — GM 错 #4 直接复现                       #
# --------------------------------------------------------------------------- #


def test_case3_xiaocheng_violation_unit_test(tmp_path: Path) -> None:
    """
    Case 3 直接复现 GM 错 #4:
    派小程 '落代码 signal_stub.cpp + unit test' — 期望 FAIL.
    persona_boundary_check.py 若在 W4 Wave 19 已部署则可提前拦截此错误.
    """
    content = """\
派给小程: 请完成信号模块 unit test, 覆盖率 >= 80%.
subagent_type=quant-signal-research

任务内容: 写单测 test_signal_stub.cpp, 覆盖所有信号计算路径.
"""
    path = make_tmp_md(tmp_path, content)
    lines = content.splitlines()
    # 精确规则检查 (subagent_type 字段)
    precise_issues = check_precise_rules(path, lines)
    # 广义矩阵检查 (派给小程)
    matrix_issues = check_persona_matrix(path, lines)
    all_issues = precise_issues + matrix_issues
    assert len(all_issues) >= 1, (
        "Case 3 期望检测到越界 (GM 错 #4 直接复现): "
        "小程 拒绝 unit test / 代码, 应 FAIL"
    )


# --------------------------------------------------------------------------- #
# Case 4 — 小程 合规 (纯 spec, 无越界词)                                        #
# --------------------------------------------------------------------------- #


def test_case4_xiaocheng_compliant_spec_only(tmp_path: Path) -> None:
    """
    Case 4 合规: 派小程做信号研究报告 (纯 spec, 无代码/回测要求) — 期望 PASS.
    小程 persona 接受: 信号研究报告 + α 数字 + decay 曲线 + spec.
    """
    content = """\
## W6 信号研究

subagent_type=quant-signal-research

**任务:** 分析 Goalserve vs Polymarket 价差信号, 输出研究报告:
- α 数字估算
- decay 曲线草图
- factor 分类 (sport/league/time)

输出格式: 信号研究报告 + spec (不需要代码实现).
"""
    path = make_tmp_md(tmp_path, content)
    lines = content.splitlines()
    issues = check_precise_rules(path, lines)
    matrix_issues = check_persona_matrix(path, lines)
    all_issues = issues + matrix_issues
    assert len(all_issues) == 0, (
        f"Case 4 合规 prompt 不应有 violation, 实际: {all_issues}"
    )


# --------------------------------------------------------------------------- #
# Case 5 — 老胡 合规 (PM 协调任务, 无 cpp 实现)                                 #
# --------------------------------------------------------------------------- #


def test_case5_laohu_compliant_pm_coordination(tmp_path: Path) -> None:
    """
    Case 5 合规: 派老胡做 Sprint 进度跟踪和协调 — 期望 PASS.
    老胡 persona 接受: PM 项目管理 / Sprint 协调 / 风险跟踪.
    """
    content = """\
## Sprint-1 进度同步

subagent_type=pm-project-manager

**任务:** 老胡请整理本周各单元进度, 输出 Sprint 周报草稿:
- A 单元 blockers
- B 单元 risk 状态
- W6 W3 会议 agenda

输出格式: 周报 markdown.
"""
    path = make_tmp_md(tmp_path, content)
    lines = content.splitlines()
    issues = check_precise_rules(path, lines)
    matrix_issues = check_persona_matrix(path, lines)
    all_issues = issues + matrix_issues
    assert len(all_issues) == 0, (
        f"Case 5 合规 PM 任务不应有 violation, 实际: {all_issues}"
    )
