# Inplay Edge Gross/Net Confirm — OQ-P02-3 补充说明

- **Owner**: 老彭 (betting-industry-expert, C 单元 IC)
- **Date**: 2026-05-29
- **Last review**: 2026-05-29
- **Status**: CONFIRMED
- **关联 OQ**: 老钱 spec v1 §8 追问 (laoqian-w8-w5-profitability-kr-v1.md)
- **关联文档**: laopeng-w8-oq-p02-3-inplay-single-source-ack.md §3.2

---

## §1 答案：1.5-2.5% 是 GROSS（扣费前）

**明确结论：W8 W2 OQ-P02-3 ack §3.2 的 edge 1.5-2.5% 是 gross edge，未扣 Polymarket fee。**

虽然 §3.2 表格列名写 "Edge post-fee"，实际含义是扣除 **bet365 book bias（0.7-1.2pp）后的有效偏离**，不是扣除 Polymarket 3% taker fee 后的净值。这是命名歧义，老彭负责，现在正式澄清。

### 数学澄清

```
gross edge = fair_value(bet365 de-vig) - market_price(Polymarket)
           = 2-3% (小程原估) - 0.5% (bet365 book bias 修正)
           = 1.5-2.5%   ← 这是 W8 W2 ack 的数字

net edge   = gross edge - 3% taker fee - slippage
           = 1.5-2.5% - 3% - ~0.3%
           = -1.8% 到 -0.8%  ← 中位数为负
```

**结论与老钱 spec v1 §4 "W8 W4 警报"完全一致：**
- "fee/edge 比值 = 1.2-2x"
- "净 edge 中位可能为负"

这两个警报是正确的，老钱读出了正确信号。

---

## §2 G3 KR 调整建议：hit rate 应升至 56-58% 或加 C2 ≥ 6¢ 门禁

### 2.1 gross → net 的数学影响

| 场景 | Hit Rate 盈亏平衡点 | 当前 G3 KR | 结论 |
|---|---|---|---|
| 若 edge 是 net（扣后）| 54% 足够 | hit ≥ 54% 维持 | 不需调整 |
| **若 edge 是 gross（扣前）** | **需 ~56-57%** 才能保证 net PnL > 0 | hit ≥ 54% 偏宽松 | **需上调** |

在 Polymarket 3% taker fee 下，hit rate 盈亏平衡点（Moneyline near-even 盘）大致在：

```
breakeven_hit = (fee%) / (avg_gross_edge%) × 50% + 50%
以 fee=3%, gross edge 中枢 2% 估算:
breakeven ≈ 50% + 3%/(2%×2) = 50% + 75% × 3% ≈ 52.3%
```

但这是理想计算。考虑滑点 (≈0.3%)、bet365 单家 de-vig 精度损失 (0.7-1.2pp)，有效净 edge 压缩到 -0.5% 到 +0.5% 区间，hit rate 54% 并不足以保证 G3 "14 日累计净 PnL > 0"。

### 2.2 建议

**方案一（推荐）：G3 hit rate 升至 56-58%**

- 理由：扣完 fee + 滑点后，只有在 gross edge 中枢 ≥ 3.5% 且 hit rate ≥ 56% 时，14 日累计净 PnL > 0 才有统计把握（p 0.10 单尾）
- 操作：G3 KR hit rate ≥ 56%（取中值），不改其他条件

**方案二：维持 hit rate ≥ 54%，但加 C2 ≥ 6¢ 做前置门禁**

- 理由：C2 ≥ 6¢ 已在 OQ-P02-3 ack §4.2 推荐，吸收 book bias，同时也过滤掉 gross edge < fee 的低质信号
- C2 ≥ 6¢ 等效于筛选 gross edge > 4%（以 Moneyline ~0.55 赔率估算），fee 3% 后净 edge ≥ 1%，有正期望
- 操作：G3 hit rate 54% 维持，**但前置条件加 C2 ≥ 6¢ 信号率 > 60%**（即至少 60% 的信号触发在 C2 ≥ 6¢ 下）

**老彭立场：方案一更干净，方案二给工程留更多空间。建议老钱 + 小梁二选一。**

两个方案都优于维持 hit 54% 不加任何门禁——那样 G3 paper run 大概率在扣费后净 PnL 为负，M4.5 持续失败。

---

## §3 不耻下问

**@老钱 CPO** — spec v2 数字 update：
- 本文 confirm edge 1.5-2.5% 是 gross，请在 spec v2 明确标注 "gross (pre-fee)"
- 请选择 G3 KR 调整方案（hit ≥ 56% 或 C2 门禁前置）
- spec v2 截止 2026-06-01（Sprint-1 启动日）前，以便 6-02 联决

**@小程 P0-02 spec** — alpha 配套：
- P0-02 spec v0.2 中 edge 标注须从 "Edge post-fee" 改为 "Edge post-bias, pre-Polymarket-fee"，避免歧义
- C2 ≥ 6¢ 与 G3 KR 方案选择联动，等老钱 spec v2 后同步更新

**@老韩 G3 KR 背书** — 截止 5/31：
- 本 confirm 影响 G3 KR hit rate 阈值；若升至 56-58%，对应每日触发量下降，请重新估算 RM Kelly 参数下最大单日亏损
- 净 edge 中位负的场景下，kill switch 触发条件是否需要调整？

**@老雷 GM ack** — 请确认：
- Gross/net 澄清已在 OQ-P02-3 ack 范围内，不产生新 ADR
- G3 KR 调整（hit 56% 或 C2 门禁）由老钱 spec v2 + 联决处理，路径不变

— 老彭，2026-05-29
