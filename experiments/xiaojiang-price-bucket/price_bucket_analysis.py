#!/usr/bin/env python3
"""
price_bucket_analysis.py — 价区假设验证 (小蒋, 2026-06-04)

目的: 按 Polymarket 市场价分桶, 分析各价区的 net EV 结构
     验证低价区 (<0.20) 是否为 -EV, 主流区 (0.20-0.80) 是否为 +EV

数据来源:
    1. data/ml_capture/quotes.jsonl — 真实 paper 采集的 quote snapshots
       (含 market_mid=pm_mid, fair_value=sharp_fair; 无结算, 无 edge 触发)
       采集时间: 2026-05-30 04:06 ~ 07:40 (3.5h 窗口)
    2. data/paper_mldata/**/*.parquet — 合成 stub 数据 (SEED=42, 0.30-0.70 均匀)
       [明确标注: 此数据不可用于价区分析, 低价区数据不存在]
    3. ml_research/data/training_sample.jsonl — paper daemon 实时快照 (2026-06-02)
       [无结算 outcome, 不可用于 EV 计算]

结论框架:
    真实结算数据不存在 → 用理论模型 + 数据驱动的参数估计做 break-even 分析
    参数来源:
        - fee_rate: 0.03 (Polymarket sports 3%, 来自老彭/小袁实测)
        - slippage: 0.003 (老彭 §1.1 小袁 §3.3)
        - bet365 devig 精度损失: 0.007~0.012 (老彭 W8 W2 ack §3.2)
        - bet365 price adjustment speed: 老彭 W9 ack 低价区最快最大 (专家输入)

Owner: 小蒋 (quant-backtest, #20)
"""

import json
import os
import sys
import numpy as np
import pandas as pd

REPO_ROOT = "/Users/wangweibo/code/sports-trader-cpp"

# ---------------------------------------------------------------------------
# 1. 加载 quotes.jsonl (真实市场价分布)
# ---------------------------------------------------------------------------
quotes_path = os.path.join(REPO_ROOT, "data/ml_capture/quotes.jsonl")

quotes_rows = []
with open(quotes_path) as f:
    for line in f:
        line = line.strip()
        if line:
            quotes_rows.append(json.loads(line))

df_quotes = pd.DataFrame(quotes_rows)
df_quotes["pm_mid"] = df_quotes["market_mid"]
df_quotes["sharp_fair"] = df_quotes["fair_value"]
df_quotes["gross_edge"] = df_quotes["sharp_fair"] - df_quotes["pm_mid"]

print(f"quotes.jsonl rows: {len(df_quotes)}")
print(f"Timestamp range: {pd.Timestamp(df_quotes['as_of_ts_ns'].min())} ~ "
      f"{pd.Timestamp(df_quotes['as_of_ts_ns'].max())}")
print()

# ---------------------------------------------------------------------------
# 2. 价区分桶定义
# ---------------------------------------------------------------------------
BINS   = [0.00, 0.10, 0.20, 0.35, 0.65, 0.80, 0.90, 1.01]
LABELS = ["[0,0.10)", "[0.10,0.20)", "[0.20,0.35)", "[0.35,0.65)",
          "[0.65,0.80)", "[0.80,0.90)", "[0.90,1.0]"]

df_quotes["price_bucket"] = pd.cut(
    df_quotes["pm_mid"], bins=BINS, labels=LABELS, right=False
)

print("=" * 70)
print("Section A: 真实市场价分布 (quotes.jsonl, 无结算, 仅分布参考)")
print("=" * 70)
bucket_dist = df_quotes["price_bucket"].value_counts().sort_index()
total = len(df_quotes)
print(f"{'Bucket':<15} {'Count':>7} {'%':>7}  {'pm_mid median':>14}  {'sharp_fair median':>18}  {'gross_edge median':>18}")
print("-" * 90)
for label in LABELS:
    sub = df_quotes[df_quotes["price_bucket"] == label]
    n = len(sub)
    pct = 100.0 * n / total
    if n == 0:
        print(f"{label:<15} {n:>7} {pct:>6.1f}%  {'--':>14}  {'--':>18}  {'--':>18}")
    else:
        med_pm  = sub["pm_mid"].median()
        med_fv  = sub["sharp_fair"].median()
        med_ge  = sub["gross_edge"].median()
        print(f"{label:<15} {n:>7} {pct:>6.1f}%  {med_pm:>14.4f}  {med_fv:>18.4f}  {med_ge:>18.4f}")

print()
print("注: gross_edge=sharp_fair-pm_mid。sharp_fair std=0.040, 覆盖极窄(0.41-0.60)。")
print("    边缘价区 (<0.20 和 >0.80) 的 sharp_fair 不覆盖真实极端行情, 此处 edge 为噪声。")

# ---------------------------------------------------------------------------
# 3. 理论 net EV 分析
# ---------------------------------------------------------------------------
print()
print("=" * 70)
print("Section B: 理论 net EV 分析 (price × bucket)")
print("=" * 70)
print()
print("成本参数 (来源: 老彭 laopeng-w9-inplay-edge-gross-net-confirm + 小袁实测):")

FEE_RATE    = 0.030   # Polymarket sports 3%
SLIPPAGE    = 0.003   # 小袁 NBA/Soccer $500-1K 单笔 §3.3
SPREAD_COST = 0.005   # 等效 vig 中位 1¢/0.5 ≈ 2%, 半进半吃
DEVIG_ERR   = 0.009   # bet365 单家 devig 精度损失均值 0.7-1.2pp → 取 0.9pp 中枢

TOTAL_COST  = FEE_RATE + SLIPPAGE + SPREAD_COST + DEVIG_ERR
print(f"  fee_rate:    {FEE_RATE:.3f}  (Polymarket sports)")
print(f"  slippage:    {SLIPPAGE:.3f}  (小袁 §3.3)")
print(f"  spread_cost: {SPREAD_COST:.3f}  (1¢/0.5 半进半吃)")
print(f"  devig_err:   {DEVIG_ERR:.3f}  (bet365 单家精度损失中枢)")
print(f"  TOTAL_COST:  {TOTAL_COST:.3f}  ({TOTAL_COST*100:.1f}%)")
print()

# 老彭特别说明: 低价区 bet365 repricing 最快最大
# 在低价区, 实际 devig_err 比中价区大 (sharp 追移动靶效应更强)
# 量化估算: 低价区额外 drift 约 0.5-1.5pp (old彭专家判断)
LOW_PRICE_EXTRA_DRIFT = 0.010   # 低价区额外移动靶效应 (保守估算)

print("低价区额外 drift 说明 (老彭 W9 诊断):")
print(f"  bet365 在 <0.20 价区 repricing 最快最大 → 追移动靶额外偏差 ~+{LOW_PRICE_EXTRA_DRIFT:.3f}")
print(f"  等效: 低价区有效 devig_err ≈ {DEVIG_ERR + LOW_PRICE_EXTRA_DRIFT:.3f}")
print()

# edge_threshold = 0.05 (当前策略触发门: sharp ≥ 5%)
# 对各价区计算: required gross edge to be +EV
# net_ev = gross_edge - (fee + slippage + spread + devig_err) - low_price_drift(?)
# breakeven gross_edge = total_cost

EDGE_THRESHOLDS = [0.05, 0.10, 0.15]

print(f"{'Bucket':<15} {'pm_mid':>8} {'cost':>8} {'5%edge netEV':>14} "
      f"{'10%edge netEV':>14} {'15%edge netEV':>14} {'BE edge':>10} {'Status':>12}")
print("-" * 105)

bucket_results = []
for label, pm_mid_rep, extra_drift in [
    ("[0,0.10)",    0.05,  LOW_PRICE_EXTRA_DRIFT * 1.5),
    ("[0.10,0.20)", 0.15,  LOW_PRICE_EXTRA_DRIFT * 1.0),
    ("[0.20,0.35)", 0.28,  LOW_PRICE_EXTRA_DRIFT * 0.3),
    ("[0.35,0.65)", 0.50,  0.000),
    ("[0.65,0.80)", 0.72,  LOW_PRICE_EXTRA_DRIFT * 0.3),
    ("[0.80,0.90)", 0.85,  LOW_PRICE_EXTRA_DRIFT * 1.0),
    ("[0.90,1.0]",  0.95,  LOW_PRICE_EXTRA_DRIFT * 1.5),
]:
    # Effective cost for this bucket
    effective_cost = TOTAL_COST + extra_drift

    # Net EV at different gross edge levels
    net_ev_5  = 0.05  - effective_cost
    net_ev_10 = 0.10  - effective_cost
    net_ev_15 = 0.15  - effective_cost
    be_edge   = effective_cost  # breakeven gross edge

    status = "+EV" if net_ev_5 > 0 else ("-EV(5%)" if net_ev_5 < 0 else "breakeven")

    print(f"{label:<15} {pm_mid_rep:>8.2f} {effective_cost:>8.4f} "
          f"{net_ev_5:>+14.4f} {net_ev_10:>+14.4f} {net_ev_15:>+14.4f} "
          f"{be_edge:>10.4f} {status:>12}")

    bucket_results.append({
        "bucket": label, "pm_mid_repr": pm_mid_rep,
        "effective_cost": effective_cost,
        "net_ev_5pct": net_ev_5,
        "net_ev_10pct": net_ev_10,
        "net_ev_15pct": net_ev_15,
        "breakeven_gross_edge": be_edge,
        "status_at_5pct": status,
        "extra_drift": extra_drift,
    })

print()
print("注: 低价区/高价区 extra_drift 为老彭专家估计 (bet365 极端价位 repricing 最快)。")
print("    symmetric: >0.80 区与 <0.20 区对称 (NO token 视角的 <0.20 问题)。")

# ---------------------------------------------------------------------------
# 4. 交叉表: 偏离幅度 × 价区
# ---------------------------------------------------------------------------
print()
print("=" * 70)
print("Section C: 偏离幅度 × 价区 net EV 交叉表 (理论值)")
print("=" * 70)

df_cross = pd.DataFrame(bucket_results).set_index("bucket")

edge_levels = [0.05, 0.07, 0.10, 0.12, 0.15]
header_parts = [f"edge={e:.0%}" for e in edge_levels]
print(f"{'Bucket':<15} " + " ".join(f"{h:>10}" for h in header_parts) + f"  {'BE':>8}")
print("-" * (15 + 11 * len(edge_levels) + 10))

for label in LABELS:
    if label not in df_cross.index:
        continue
    row = df_cross.loc[label]
    cost = row["effective_cost"]
    vals = [f"{e - cost:>+10.4f}" for e in edge_levels]
    be = f"{cost:>8.4f}"
    print(f"{label:<15} " + " ".join(vals) + f"  {be}")

print()
print("解读: 每格 = net EV (gross_edge - effective_cost)。正值 = +EV, 负值 = -EV。")
print("      主流区 [0.35,0.65]: cost=0.047, 5%edge → net EV=+0.003 (薄, 需 6-7%+ 才稳健)")
print("      低价 [0.10,0.20): cost=0.057, 5%edge → net EV=-0.007 (负), 需 ~6% 才打平")
print("      极低 [0,0.10):    cost=0.062, 5%edge → net EV=-0.012 (最差)")

# ---------------------------------------------------------------------------
# 5. 数据局限性声明 (关键)
# ---------------------------------------------------------------------------
print()
print("=" * 70)
print("Section D: 数据局限性 (回测结论的可信度评级)")
print("=" * 70)
print()
print("【回测跑不了的原因 — 缺 X 清单】")
print()
print("X1: 无历史结算数据")
print("    data/paper_mldata/ = 合成 stub (SEED=42, feat_04/feat_00 均匀 0.30-0.70)")
print("    训练数据 feat_04 被硬限制 rng.uniform(0.3,0.7) → 零低价区样本")
print("    ml_research/data/training_sample.jsonl = paper daemon 实时快照 (2026-06-02)")
print("    无任何 settlement_outcome 可用于计算 realized PnL")
print()
print("X2: 无真实 sharp edge 触发记录")
print("    quotes.jsonl 18313 条 quote snapshot, edge_bps 全为 0 (advisory 模式未触发)")
print("    sharp_fair std=0.040, 范围 0.41-0.60 → 覆盖不包含极端盘口")
print()
print("X3: 回测 harness 不完整")
print("    replay_driver.hpp 明确标注 replay_coverage=BOOK_ONLY")
print("    缺 Goalserve 比分+sharp 赔率回放管道 → has_real_fair 恒 false")
print("    红线 #3: 补齐前回测数字不得作为 MVP 上线依据")
print()
print("X4: sharp 77%/+0.20 数字来源")
print("    来自老彭 laopeng-w9-inplay-edge-gross-net-confirm.md §1.1:")
print("    净 edge = C2≥6¢ 子集 gross ~4% - fee 3% - slip 0.3% - spread 0.5% = +0.2%")
print("    这是理论估算, 不是历史回测数字。77% 未找到来源文档。")
print()

# ---------------------------------------------------------------------------
# 6. 核心结论
# ---------------------------------------------------------------------------
print("=" * 70)
print("Section E: 核心结论")
print("=" * 70)
print()
print("问题1: 低价区 (<0.20) 是不是真的 -EV?")
print("  理论: YES, 系统性 -EV。effective_cost=[0.057,0.062], 5%edge net_ev=[-0.007,-0.012]。")
print("  机制: bet365 低价区 repricing 最快最大 (老彭 W9 诊断) + 相同 fee 但更大 devig 误差。")
print("  数据: 无法从现有数据直接验证 (零低价区结算样本)。")
print()
print("问题2: 主流区 (0.20-0.80) 是不是 +EV?")
print("  理论: 条件性 +EV。[0.35,0.65]: cost=0.047, 5%edge→+0.003 (极薄)。")
print("        需 gross_edge ≥ 7-8% 才稳健 (+EV with margin)。")
print("  注意: [0.20,0.35) 和 [0.65,0.80): cost=0.050, 5%edge→+0.000 (打平), 需 ≥6%。")
print()
print("问题3: 最优价区截断建议?")
print("  建议截断: [0.20, 0.80], 与老彭诊断一致。")
print("  额外建议: 在 [0.35,0.65] 区间配合更高 edge 门 (≥7%) 才有可测净正。")
print()
print("问题4: 实施 [0.20,0.80] 价区 gate 的决策依据强度?")
print("  理论支撑: 强 (fee/cost 结构 + 低价区 repricing 速度 → 成本更高)。")
print("  实证支撑: 弱 (无结算数据)。")
print("  结论: 支持上 [0.20,0.80] 价区 gate, 但应视为 risk-reducing 措施 (降低已知")
print("        -EV 区域的暴露), 而非 confirmed alpha gate (后者需真实历史结算数据)。")
print()
print("  优先级建议: 先解决 X1 (真实结算数据入库) → 才能做真正的价区分桶验证。")
print("  当前 gate 是「有理论依据的预防性剪枝」, 不是「回测验证的最优截断」。")
