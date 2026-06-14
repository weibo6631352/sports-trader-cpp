# 参数发现 / 获利模式挖掘 — 2026-06-15

owner: 老雷 | 目标(老板 /goal): 统计分析→追溯→寻找利益最大的赢利参数/机会→不断从大数据迭代。
方法: 前端盯盘 + 后端 API + 文件存储 + 脚本分析。

## ⚠ 过拟合红线 (老板 2026-06-15「只有6个持仓你就这么大样本量」)
真实持仓只有: 当前盘 6 笔 | 全历史 29 开仓 / **24 唯一市场 / 13 真平仓结算**。这是全部真实证据。
"392 结算 / 1234 决策点 / 流动性三档"里 **~95% 是反事实**(看过/被挡但没下单, 有逆选偏差)。
**拿反事实当样本量 = 过拟合。** 铁律:
- 真实策略表现只认 ⓪(n=13-24, CI 跨0 = 和运气区分不开, 未验证)。
- 任何候选新参数(含流动性下限, 真实证据仅 liq<3000 n=5)**在被独立真实持仓样本 OOS 验证前, 一个门都不改**。
- 严格统计(BH-FDR q<5%)本轮【无任何因子达标】= 现在没有能下结论的新参数。
- 总瓶颈 = 真实样本太少(出单率 3.7%)。正路是【安全提高真实出单量】攒样本, 不是把 24 笔+反事实反复挖。

## 可重复流水线 (每次分析前跑一遍, 数据越积越准)
```bash
# 1. 服务器侧合并全量语料 (current + 所有 archive_*)
~/stcpp-ops/run.sh 'cd ~/sports-trader-cpp/data/ml_capture && mkdir -p /tmp/bf && \
  for f in market_tape fills_journal gate_blocks quotes.jsonl.settlements; do \
    cat $f.jsonl archive_*/$f.jsonl 2>/dev/null > /tmp/bf/$f.jsonl; done'
# 2. 补 orphan 结算 (查 CLOB 权威; 本机被 geoblock, 必须在都柏林服务器跑)
~/stcpp-ops/run.sh 'cd ~/sports-trader-cpp && python3 experiments/laolei-paper-replay-stats/backfill_settlements.py /tmp/bf --sleep 0.15'
# 3. 拉回本地 + 跑参数研究
~/stcpp-ops/run.sh 'cat /tmp/bf/gate_blocks.jsonl' > /tmp/rej/gate_blocks_all.jsonl   # 同理 fills/tape/settlements_merged
python3 experiments/laolei-paper-replay-stats/param_research.py \
  /tmp/rej/fills_all.jsonl /tmp/rej/gate_blocks_all.jsonl /tmp/rej/settlements_merged.jsonl /tmp/rej/market_tape_all.jsonl --min-support 8
```
关键基建修复: backfill 把【被挡盘结算覆盖 24→95】(总 392 个唯一结算市场)。结算覆盖是所有反事实分析的绑定约束。

## 本轮快照 (2026-06-15, git=1a3b0663)
- 真实持单: 29 开仓 / 24 唯一市场 / 13 平仓结算 / realized **+10.85** (费 1.04)
- 反事实画布: 95 被挡盘 + 392 总结算盘 (被挡盘=持单 ~4×, 大数据在此)

### ⓪ 真实策略表现 (仅 24 笔真实进场)
赢面 **79% (19/24, Wilson[60,91])** | EV/股 +0.066 但 **95%CI[-0.091,+0.223] 跨0 → 未显著** | 净 +$15.71 | Kelly f*=0.23 g=+0.011
→ 方向为正但 n 太小, 不能下结论。按运动: baseball n14 赢面 **93%** +0.19 | tennis n5 80% +0.07。

### 拒单审计 (老板 2026-06-15 问"好多拒单是否有bug")
**不是 bug。** 三检全过: ①无阈值反转(当前 build 全部落在失败侧, 0 违规) ②无刷屏(5min/cond×gate 节流, 0 违规) ③当前 build 正确记录被挡边(yes 0 缺失; 旧归档 891 缺是加字段前的)。
反事实: 6 道门全部负 EV/股(放行这批已结算被挡盘合计 −13.63) = 门在挡输家不是赢家。
`sharp_gap_low` 是唯一边际门(70% 赢面 / EV −0.03 ≈ 打平)= 老板砍 0.03→0.015 想抓的那批, 待实测放进来后的真实 realized。

### 候选新参数 (假设, 需 OOS 验证, 别照搬 in-sample 峰值)
1. **流动性下限 (最强, 赢面单调↑)** — 目前进场无此门:
   liq≥2819→赢面65%/+0.014 | ≥6328→71%/+0.072 | ≥10980→78%/+0.126。保守地板 ~2000-3000 跳过冷门噪声盘。
   **de-risk (2026-06-15): 不是 baseball 单运动假象。** 真实持单 liq<3000 赢面 60%(n5) vs liq≥3000 **84%(n19)**;
   低液输家是 soccer(liq646×2 全输), 高液赢家横跨 esports/soccer/tennis/baseball; 反事实三档 40%→80%→83% 单调。
   三证(真实持单+反事实+跨运动)同向。硬伤: liq<3000 真实样本仅 5。
2. **devig 单调↑** — 印证只买强 favorite, 与 min_open_fair 0.65 同向。
3. 获利模式线索(每 cell n 仍小, 当线索): `剩余进度≤720 & 流动性≥924` n29 赢面93% +0.18; `24h量≥777 & 剩余进度≤720` n30 赢面93% +0.17 → **流动性 + 比赛早期**。

### 解读纪律 (工具内置, 评审固化)
- 只信过 BH-FDR q<5% 的★强候选(本轮: 暂无, n 不够)。
- 看赢面单调性, 别取 in-sample 峰值阈值。
- 被挡盘反事实有逆选偏差(记录价高估) → 当线索非结论。
- 真实策略表现只看 ⓪(进场盘); 别把"结算样本"当"持单"(老板 2026-06-15 提醒: 持单很少, 结算样本是反事实画布)。

## 迭代周期 #1 闭环: 流动性门 OOS walk-forward (2026-06-15)
完整跑了一遍 propose→OOS测试→refine:
- 反事实画布(n16, 按 ts 切 train9/test7): train 学到 liq≥5741(分离+67%) → **test OOS 仅 +17%, 且 n3 vs n4 = 噪声级**。
- 真实持仓(n24, 切 12/12): 早段 liq≥3000 赢面100%(n8) 但 **晚段唯一 1 个低流动性盘反而赢(n1), 高流动性掉到 73%** → 不复现。
- **refine 结论: 无法验证也无法证伪 —— 样本小到连 walk-forward 切分都没有统计意义。** ∴ 不上门; 该周期确认绑定约束 = 真实样本量, 不是参数选择。

## 出单漏斗 (为什么真单这么少, 2026-06-15)
108 想下单市场 → 24 真成交 = **22% 过门率**。84 个"想下单从未成交"里: min_open_fair(fair<0.65) 挡 **87%** | sharp_gap_low 39% | stable_window 30% | no_chase 27%。
**关键: 主力门挡的是输家(反事实 min_open_fair 赢面 47.8%≈抛硬币), 放松=加输家加噪声(=过拟合坑)。** ∴ 真实样本唯一安全增量来源 = 上游候选供给(联赛白名单 + totals/spreads), 不是松门。见 [[coverage-expansion-market-types-longterm]]。

## 下一步 (按杠杆排序)
1. **流动性下限**: 数据最支持的可调参数, 但需更多真实样本验证 → 建议先在 paper 加观测(不下门), 看新进场盘的 liq 分布与赢面, 再决定地板。
2. **样本积累**: n=24 真实 / 95 被挡 仍薄 → 出单率 3.7% 是真瓶颈; 每日跑上面流水线让结论收敛。
3. **sharp_gap_low 0.015 实测**: 复盘 0.015-0.03 新放进来的边际盘真实 realized 扣费后是否转正。
