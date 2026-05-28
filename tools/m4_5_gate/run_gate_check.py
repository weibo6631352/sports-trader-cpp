#!/usr/bin/env python3
"""
M4.5 Gate 自动判定脚本

GM Wave 6 红线 + 用户高优指令 (2026-05-28):
  "我们要支持虚拟盘测试, 开始不投入真实资金. 等虚拟盘测试能稳定盈利后才跑实盘测试."

判定 6 条门禁:
  G-A duration   : 连续运行 >= 14 天
  G-B PnL        : 累计 paper PnL > 0
  G-C Sharpe     : 日 Sharpe > 1.0
  G-D Risk       : 风控失效次数 = 0
  G-E Uptime     : 在线率 > 99.5%
  G-F OOS decay  : paper / baseline Sharpe ratio > 0.6

全部 PASS  → verdict = PASS_FOR_LIVE   (等 GM 最终拍板)
任一  FAIL → verdict = HOLD_OR_REWORK  (列失败 + 处置建议)

输入文件契约 (与 xiaojiang-paper-trading-engine-v0.1.md 附录 C 一致):
  --paper-run-dir/audit.wal       : 老韩 RM audit WAL (jsonl)
  --paper-run-dir/signer.log      : signer 子进程日志 (jsonl)
  --paper-run-dir/daily_pnl.parquet : 每日 PnL 快照 (我自己 emit)
  --paper-run-dir/uptime_events.parquet : 状态变更事件 (小郑 metrics)
  --baseline-backtest             : backtest framework 输出的 metrics.json

Owner: 小蒋 (quant-backtest)
Reviewers: 老韩 + 老雷 + 老周
Date: 2026-05-28
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as dt
import json
import math
import pathlib
import sys
from typing import Any


# ---------------------------------------------------------------------------
# 数据结构
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class Check:
    name: str
    passed: bool
    value: Any
    threshold: Any
    message: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "passed": self.passed,
            "value": self.value,
            "threshold": self.threshold,
            "message": self.message,
        }


@dataclasses.dataclass
class RiskFailure:
    kind: str
    detail: str
    count: int

    def to_dict(self) -> dict[str, Any]:
        return dataclasses.asdict(self)


@dataclasses.dataclass
class GateDecision:
    verdict: str
    paper_period: tuple[str, str]
    checks: dict[str, Check]
    risk_failures: list[RiskFailure]
    ratified_at: str
    ratified_by: str

    def to_dict(self) -> dict[str, Any]:
        return {
            "verdict": self.verdict,
            "paper_period": list(self.paper_period),
            "checks": {k: v.to_dict() for k, v in self.checks.items()},
            "risk_failures": [f.to_dict() for f in self.risk_failures],
            "ratified_at": self.ratified_at,
            "ratified_by": self.ratified_by,
        }


# ---------------------------------------------------------------------------
# 数据加载 (惰性 import, 不强求 polars 在线)
# ---------------------------------------------------------------------------


def _load_parquet(path: pathlib.Path):
    """读 parquet, 优先 polars, fallback pyarrow."""
    try:
        import polars as pl  # type: ignore

        return pl.read_parquet(str(path))
    except ImportError:
        try:
            import pyarrow.parquet as pq  # type: ignore

            return pq.read_table(str(path)).to_pandas()
        except ImportError:
            raise RuntimeError(
                f"读 {path} 需要 polars 或 pyarrow, "
                f"请 source scripts/activate-quant.sh 后重跑"
            )


def _load_jsonl(path: pathlib.Path) -> list[dict[str, Any]]:
    """逐行读 jsonl."""
    out = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            out.append(json.loads(line))
    return out


def _load_baseline(path: pathlib.Path) -> dict[str, Any]:
    """读 backtest framework 输出的 metrics.json."""
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# 6 条门禁 check
# ---------------------------------------------------------------------------


def check_duration(
    paper_first_ts: dt.datetime,
    paper_last_ts: dt.datetime,
) -> Check:
    """G-A: 连续运行 >= 14 天."""
    duration_days = (paper_last_ts - paper_first_ts).total_seconds() / 86400.0
    passed = duration_days >= 14.0
    return Check(
        name="G-A_DURATION",
        passed=passed,
        value=round(duration_days, 2),
        threshold=14.0,
        message=(
            f"Paper duration: {duration_days:.2f} days "
            f"({paper_first_ts.isoformat()} ~ {paper_last_ts.isoformat()})"
        ),
    )


def check_pnl(daily_pnl_series: list[float]) -> Check:
    """G-B: 累计 paper PnL > 0."""
    total_pnl = float(sum(daily_pnl_series))
    passed = total_pnl > 0.0
    return Check(
        name="G-B_PNL",
        passed=passed,
        value=round(total_pnl, 2),
        threshold=0.0,
        message=(
            f"Total paper PnL: ${total_pnl:.2f} over {len(daily_pnl_series)} days "
            f"(positive days: {sum(1 for x in daily_pnl_series if x > 0)})"
        ),
    )


def check_sharpe(daily_pnl_pct_series: list[float]) -> Check:
    """G-C: 日 Sharpe > 1.0 (annualized, sqrt(252))."""
    if len(daily_pnl_pct_series) < 5:
        return Check(
            name="G-C_SHARPE",
            passed=False,
            value=None,
            threshold=1.0,
            message=f"Insufficient data: only {len(daily_pnl_pct_series)} days",
        )
    n = len(daily_pnl_pct_series)
    mean = sum(daily_pnl_pct_series) / n
    var = sum((x - mean) ** 2 for x in daily_pnl_pct_series) / max(n - 1, 1)
    std = math.sqrt(var)
    if std < 1e-12:
        sharpe = 0.0  # 没波动 = 没意义
    else:
        sharpe = mean / std * math.sqrt(252.0)
    passed = sharpe > 1.0
    return Check(
        name="G-C_SHARPE",
        passed=passed,
        value=round(sharpe, 3),
        threshold=1.0,
        message=(
            f"Daily Sharpe (annualized): {sharpe:.3f} "
            f"(mean={mean*100:.3f}%, std={std*100:.3f}%, n={n})"
        ),
    )


def scan_risk_failures(
    audit_log: list[dict[str, Any]],
    signer_log: list[dict[str, Any]],
) -> list[RiskFailure]:
    """
    §7.2 中 7 条 risk failure 定义:
      1. signer 收到 SignRequest 但 audit log 无对应 APPROVED → BYPASS
      2. WSS / Goalserve / Recon stale 超过 HALT 阈但 RM 状态未 HALTED → STALE_NOT_HALTED
      3. 单笔 size > PER_ORDER_CAP_HARD 被 approved → HARD_CAP_VIOLATED
      4. SAFE_MODE 期间被允许开仓 → SAFE_MODE_OPEN
      5. EDGE_CI_NEGATIVE 被 approve → EDGE_CI_VIOLATED
      6. fill_rate < FILL_RATE_FLOOR 被 approve → LOW_FILL_RATE_APPROVED
      7. 任何 NaN / Inf 进入 RM 决策 → NAN_INF_IN_DECISION
    """
    failures: list[RiskFailure] = []

    # 1. BYPASS
    approved_audit_ids = {
        rec["audit_id"]
        for rec in audit_log
        if rec.get("decision") == "APPROVED" and "audit_id" in rec
    }
    signer_audit_ids = {
        rec["audit_id"] for rec in signer_log if "audit_id" in rec
    }
    bypass_ids = signer_audit_ids - approved_audit_ids
    if bypass_ids:
        failures.append(
            RiskFailure(
                kind="BYPASS",
                detail=f"signer received non-approved audit_id (sample: {list(bypass_ids)[:5]})",
                count=len(bypass_ids),
            )
        )

    # 2. STALE_NOT_HALTED — 用 audit 的 state_snapshot 推
    # 简化: 任何 reject_code != STALE_DATA 但 audit 中 freshness > HALT 阈的 record
    HARD_HALT_MS = {"wss": 10_000, "goalserve": 15_000, "recon": 30_000}
    for rec in audit_log:
        if rec.get("decision") == "APPROVED":
            for src, halt_ms in HARD_HALT_MS.items():
                f_ms = rec.get(f"{src}_freshness_ms", 0)
                if f_ms > halt_ms:
                    failures.append(
                        RiskFailure(
                            kind="STALE_NOT_HALTED",
                            detail=f"{src} freshness {f_ms}ms > {halt_ms}ms but APPROVED (audit={rec.get('audit_id')})",
                            count=1,
                        )
                    )
                    break  # 一条 record 算一次

    # 3. HARD_CAP_VIOLATED
    HARD_CAP = 5000  # USDC, 与 prod_config 锁死 (待 6/12 小梁会签实数)
    for rec in audit_log:
        if (
            rec.get("decision") == "APPROVED"
            and rec.get("approved_size_usdc", 0) > HARD_CAP
        ):
            failures.append(
                RiskFailure(
                    kind="HARD_CAP_VIOLATED",
                    detail=f"approved size {rec['approved_size_usdc']} > HARD {HARD_CAP} (audit={rec.get('audit_id')})",
                    count=1,
                )
            )

    # 4. SAFE_MODE_OPEN
    for rec in audit_log:
        if (
            rec.get("state_snapshot") == "SAFE_MODE"
            and rec.get("decision") == "APPROVED"
            and rec.get("is_open_order", False)
        ):
            failures.append(
                RiskFailure(
                    kind="SAFE_MODE_OPEN",
                    detail=f"open order approved during SAFE_MODE (audit={rec.get('audit_id')})",
                    count=1,
                )
            )

    # 5. EDGE_CI_VIOLATED (在 v0.2 单列后理论不可能, 兜底检测)
    for rec in audit_log:
        if rec.get("decision") == "APPROVED":
            edge_ci_low = rec.get("edge_ci_low_bps")
            if edge_ci_low is not None and edge_ci_low <= 0:
                failures.append(
                    RiskFailure(
                        kind="EDGE_CI_VIOLATED",
                        detail=f"edge_ci_low_bps {edge_ci_low} <= 0 but APPROVED (audit={rec.get('audit_id')})",
                        count=1,
                    )
                )

    # 6. LOW_FILL_RATE_APPROVED
    FILL_RATE_FLOOR = 0.50  # 小肖 §7
    for rec in audit_log:
        if rec.get("decision") == "APPROVED":
            f_rate = rec.get("expected_fill_rate")
            if f_rate is not None and f_rate < FILL_RATE_FLOOR:
                failures.append(
                    RiskFailure(
                        kind="LOW_FILL_RATE_APPROVED",
                        detail=f"expected_fill_rate {f_rate} < floor {FILL_RATE_FLOOR} but APPROVED (audit={rec.get('audit_id')})",
                        count=1,
                    )
                )

    # 7. NAN_INF_IN_DECISION
    for rec in audit_log:
        for key in ("approved_size_usdc", "expected_fill_price", "expected_fill_rate", "slippage_bps"):
            v = rec.get(key)
            if v is None:
                continue
            if isinstance(v, float) and (math.isnan(v) or math.isinf(v)):
                failures.append(
                    RiskFailure(
                        kind="NAN_INF_IN_DECISION",
                        detail=f"{key} = {v} in audit {rec.get('audit_id')}",
                        count=1,
                    )
                )

    return failures


def check_risk(audit_log: list[dict], signer_log: list[dict]) -> tuple[Check, list[RiskFailure]]:
    """G-D: 风控失效次数 = 0."""
    failures = scan_risk_failures(audit_log, signer_log)
    total = sum(f.count for f in failures)
    kinds = sorted({f.kind for f in failures})
    check = Check(
        name="G-D_RISK",
        passed=(total == 0),
        value=total,
        threshold=0,
        message=(
            f"Risk failures: {total} "
            + (f"(kinds: {', '.join(kinds)})" if kinds else "(none)")
        ),
    )
    return check, failures


def check_uptime(uptime_events: list[dict[str, Any]], period_seconds: float) -> Check:
    """
    G-E: 在线率 > 99.5%.

    uptime_events: list of {ts_ns, state} 状态变更事件 (RUNNING/WARNING/HALTED/SAFE_MODE).
    在线 = state in {RUNNING, WARNING}, 不在线 = HALTED / SAFE_MODE / process down (event 间断).
    """
    if not uptime_events:
        return Check(
            name="G-E_UPTIME",
            passed=False,
            value=None,
            threshold=0.995,
            message="No uptime events recorded",
        )

    events = sorted(uptime_events, key=lambda e: e["ts_ns"])
    uptime_s = 0.0
    for i in range(len(events) - 1):
        cur = events[i]
        nxt = events[i + 1]
        dur = (nxt["ts_ns"] - cur["ts_ns"]) / 1e9
        if cur["state"] in ("RUNNING", "WARNING"):
            uptime_s += dur

    ratio = uptime_s / period_seconds if period_seconds > 0 else 0.0
    passed = ratio > 0.995
    return Check(
        name="G-E_UPTIME",
        passed=passed,
        value=round(ratio, 4),
        threshold=0.995,
        message=(
            f"Uptime: {ratio*100:.2f}% "
            f"({uptime_s:.0f}s / {period_seconds:.0f}s)"
        ),
    )


def check_oos_decay(paper_sharpe: float, baseline_sharpe: float | None) -> Check:
    """G-F: paper / baseline Sharpe ratio > 0.6."""
    if baseline_sharpe is None or baseline_sharpe <= 0:
        return Check(
            name="G-F_OOS_DECAY",
            passed=False,
            value=None,
            threshold=0.6,
            message=f"Baseline Sharpe missing or non-positive ({baseline_sharpe})",
        )
    ratio = paper_sharpe / baseline_sharpe
    passed = ratio > 0.6
    return Check(
        name="G-F_OOS_DECAY",
        passed=passed,
        value=round(ratio, 3),
        threshold=0.6,
        message=(
            f"Paper Sharpe {paper_sharpe:.3f} / Baseline Sharpe {baseline_sharpe:.3f} "
            f"= {ratio:.3f}"
        ),
    )


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------


def run_gate(args) -> GateDecision:
    paper_dir = pathlib.Path(args.paper_run_dir)
    audit_path = paper_dir / "audit.wal"
    signer_path = paper_dir / "signer.log"
    daily_pnl_path = paper_dir / "daily_pnl.parquet"
    uptime_path = paper_dir / "uptime_events.parquet"

    # 容错: 文件不存在时给清晰错误
    for p in (audit_path, signer_path, daily_pnl_path, uptime_path):
        if not p.exists():
            print(f"[FATAL] 必需输入缺失: {p}", file=sys.stderr)
            sys.exit(2)

    audit_log = _load_jsonl(audit_path)
    signer_log = _load_jsonl(signer_path)
    daily_pnl_df = _load_parquet(daily_pnl_path)
    uptime_df = _load_parquet(uptime_path)

    # 提取 paper 时间窗
    paper_first_ts = dt.datetime.fromisoformat(args.paper_start)
    paper_last_ts = dt.datetime.fromisoformat(args.paper_end)
    period_seconds = (paper_last_ts - paper_first_ts).total_seconds()

    # 提取 daily series (容忍 pandas / polars)
    if hasattr(daily_pnl_df, "to_pandas"):
        df_pd = daily_pnl_df.to_pandas()
    else:
        df_pd = daily_pnl_df
    daily_pnl_series = list(df_pd["pnl_usdc"])
    daily_pnl_pct_series = list(df_pd["pnl_pct"])

    if hasattr(uptime_df, "to_pandas"):
        up_pd = uptime_df.to_pandas()
    else:
        up_pd = uptime_df
    uptime_events = up_pd.to_dict(orient="records")

    # baseline
    baseline = _load_baseline(pathlib.Path(args.baseline_backtest))
    baseline_sharpe = (
        baseline.get("sharpe", {}).get("oos")
        if isinstance(baseline.get("sharpe"), dict)
        else baseline.get("sharpe_oos")
    )

    # 跑 6 条 check
    c_duration = check_duration(paper_first_ts, paper_last_ts)
    c_pnl = check_pnl(daily_pnl_series)
    c_sharpe = check_sharpe(daily_pnl_pct_series)
    c_risk, risk_failures = check_risk(audit_log, signer_log)
    c_uptime = check_uptime(uptime_events, period_seconds)
    c_oos = check_oos_decay(c_sharpe.value if c_sharpe.value else 0.0, baseline_sharpe)

    checks = {
        c.name: c
        for c in (c_duration, c_pnl, c_sharpe, c_risk, c_uptime, c_oos)
    }

    all_pass = all(c.passed for c in checks.values())
    verdict = "PASS_FOR_LIVE" if all_pass else "HOLD_OR_REWORK"

    return GateDecision(
        verdict=verdict,
        paper_period=(args.paper_start, args.paper_end),
        checks=checks,
        risk_failures=risk_failures,
        ratified_at=dt.datetime.utcnow().isoformat() + "Z",
        ratified_by="auto-gate-script-v0.1",
    )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="M4.5 Paper Trading Gate 自动判定 (小蒋 v0.1)",
    )
    p.add_argument("--paper-start", required=True, help="paper 起始 ISO 时间 e.g. 2026-09-01")
    p.add_argument("--paper-end", required=True, help="paper 终止 ISO 时间 e.g. 2026-09-14")
    p.add_argument("--paper-run-dir", required=True, help="paper 运行目录 (audit.wal / signer.log / *.parquet)")
    p.add_argument("--baseline-backtest", required=True, help="backtest metrics.json 路径")
    p.add_argument("--output", default="m4_5_gate_decision.json", help="判定 JSON 输出路径")
    p.add_argument("--quiet", action="store_true", help="只输出 verdict")
    return p


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    decision = run_gate(args)

    with open(args.output, "w", encoding="utf-8") as f:
        json.dump(decision.to_dict(), f, indent=2, ensure_ascii=False)

    if args.quiet:
        print(decision.verdict)
    else:
        print(f"=== M4.5 Gate Decision: {decision.verdict} ===")
        print(f"Period: {decision.paper_period[0]} ~ {decision.paper_period[1]}")
        print()
        for name, chk in decision.checks.items():
            mark = "PASS" if chk.passed else "FAIL"
            print(f"  [{mark}] {name:20s} value={chk.value} threshold={chk.threshold}")
            print(f"         {chk.message}")
        if decision.risk_failures:
            print()
            print(f"Risk failures ({len(decision.risk_failures)} total):")
            for f in decision.risk_failures[:20]:
                print(f"  - {f.kind:25s} {f.detail}")
        print()
        print(f"Output: {args.output}")

    return 0 if decision.verdict == "PASS_FOR_LIVE" else 1


if __name__ == "__main__":
    sys.exit(main())
