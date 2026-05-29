# Goalserve Pregame Moneyline 历史回填计划 v1

- **Owner**: 老彭 (betting-industry-expert, C 单元 IC)
- **Last review**: 2026-05-29
- **验收人**: 小梁 (C 主管)
- **关联文档**:
  - `xiaocheng-p0-02-alpha-v2-backtest-spec-v0.2.md` (小程 alpha v2 spec, 数据需求 SSOT)
  - `laopeng-bookmaker-history-backfill-v1.md` (W6 回填规范, 8-9 家 bookmaker 定义)
  - `laopeng-multiplicative-devig-calibration-v1.md` (校准方法论)
  - `xiaoduan-goalserve-official-doc-v3.md` (Goalserve API 实测 SSOT)
  - `data-contract-v1.md` (C-02 Pinnacle/Goalserve 历史数据 gap)
  - `xiaojiang-backtest-framework-v0.2-cpp.md` (BR-2 三路径并行风险)
- **截止**: 6-12 (小程 alpha v2 spec §4.4 Q1 要求)
- **派单来源**: 小梁 §8.1 + GM 加派 P0 前置

---

## §1 历史数据现状盘点

### §1.1 W6 已回填了什么

W6 (laopeng-bookmaker-history-backfill-v1.md) 完成的是**规范文档和 ETL 接口契约**，不是真实的历史 Parquet 落库。具体状态如下：

| 项目 | W6 状态 | 说明 |
|---|---|---|
| Bookmaker 列表确认 | DONE | 8 家 (id 14/15/16/17/18/65/105/144) + 第 9 家 bwin TBD |
| ETL 接口契约 (4 时间戳字段定义) | DONE | R-20 合规, 字段语义 SSOT |
| 数据模型 schema 定义 | DONE | odds_snapshot schema, 给小余/小段联调 |
| 校准方法论 (multiplicative de-vig) | DONE | laopeng-multiplicative-devig-calibration-v1.md |
| **实际历史 Parquet 数据落库** | **NOT DONE** | 小段联调 + 小余 ETL 未执行, W6 仅定义规范 |
| **Goalserve API 历史深度实测** | **NOT DONE** | getodds `&ts=` 增量协议只解决实时增量, 历史回拉深度未实测验证 |

**现有 data/paper_mldata/ Parquet**:

data/ 目录下存有 paper_mldata (sport × market_type × year=2024 分区)，经 DuckDB 实测：
- Basketball Moneyline: 313 行，时间范围 2024-01-02 ~ 2024-12-29
- Soccer Moneyline: 279 行，时间范围 2024-01-01 ~ 2024-12-30

这批数据是 **ML 特征工程用的合成 sample**（feat_00 到 feat_31 匿名特征列），**不是真实 Goalserve bookmaker 赔率历史快照**。字段含 `feature_snapshot_id`、`settlement_outcome`、`realized_pnl_usdc`，符合小蒋 backtest framework schema，但没有 `bookmaker_id`、`odds_eu`、`overround` 等 de-vig 计算必需字段。

**结论: 截至 2026-05-29，pregame bookmaker 赔率历史 Parquet 实际上是零。**

### §1.2 小程 spec 要求的 Walk-Forward 窗口

小程 alpha v2 §3.2 定义四个窗口：

| 窗口 | 时间范围 | 作用 | 所需数据深度 |
|---|---|---|---|
| IS | 2024-10-01 ~ 2025-02-28 | 参数调优 | 5 个月，覆盖 NBA/NFL 赛季中段 |
| Embargo | 2025-03-01 ~ 2025-03-07 | 防泄漏 | 7 天 |
| OOS 1 | 2025-03-08 ~ 2025-06-30 | 主要验证 | 4 个月，NBA 季后赛 + MLB 开季 |
| OOS 2 | 2025-07-01 ~ 2025-10-31 | 季节外推验证 | 4 个月，MLB 赛季 + NFL 开季 |
| 二段 OOS | 2025-11-01 ~ 2026-02-28 | Regime shift 检验 | 4 个月，NBA/NFL 新赛季 |

**总计: 2024-10-01 ~ 2026-02-28，约 17 个月。**

### §1.3 缺口分析

| 缺口 | 严重度 | 说明 |
|---|---|---|
| 无 pregame bookmaker odds Parquet | P0 | 全部 walk-forward 窗口所需数据均缺 |
| Goalserve API 历史回拉深度未验证 | P0 | getodds `&ts=` 只做增量协议，无文档保证能回拉 17 个月 |
| settlement result 历史回填方案未验证 | P0 | oddsfeed.goalserve.com settlement 端点仅实测过活跃比赛，历史批量能力未知 |
| bwin (第 9 家) ID 未确认 | P1 | 不影响 MIN_BOOKMAKERS=3 通过，但影响 8 家均值精度 |
| 小余/小段 ETL pipeline 未启动 | P0 | W6 接口契约已定，但联调工作量未排期 |

**重点**: IS 窗口从 2024-10-01 开始。若历史数据只能从今日 (2026-05-29) 起实时录制，则需等待 **约 17 个月**才能积累足够 IS 数据，远超 6-12 deadline，8-31 回测数字彻底无法交付。这是本 plan 的最高优先级风险。

---

## §2 三路径可行性评估

### §2.1 路径 A: Goalserve API 历史回拉

**原理**: Goalserve getodds 支持 `&ts=<unix_sec>` 增量参数。理论上若服务器端有历史快照存档，可以用较早的 ts 值拉取历史数据。

**可行性分析**:

| 维度 | 评估 | 说明 |
|---|---|---|
| 官方文档声明 | 未明确支持历史回拉 | 文档 (full_package_feed_cn.md) 只说"保存 ts，下次带上只拿更新内容"，是增量协议，不是历史查询 |
| 实测行为 | 未测试 | 小段 v3 实测了增量 (1.2MB → 14KB)，但没有测试用历史 ts 回拉 |
| 服务端存档深度 | 未知 | Goalserve 是否在服务端保留 17 个月历史快照，官方无说明 |
| 速率限制 | 16 cat × 60s/次 | 历史批量回拉若每 60s 一个快照点，17 个月 × 86400s/60s = ~740K 次，不现实 |
| ToS 风险 | 低 (合法使用 API) | 只要不 resell 数据，正常 API 调用合规 |

**结论**: 路径 A 存在重大不确定性。`&ts=` 历史回拉是否真正工作，需要小段在 **6-05 前实测验证**（用 2024-10-01 的 unix timestamp 拉一次 basketball_10，看返回是否含历史数据）。若服务端无存档，路径 A 完全不可行。

**6-12 前能通概率: 20-35%**（高度取决于 Goalserve 服务端是否保留历史快照，属于黑盒）。

### §2.2 路径 B: The Odds API 付费历史数据

**原理**: The Odds API (the-odds-api.com) 提供多家 bookmaker 历史赔率，含 bet365/William Hill/Unibet 等我们需要的家。

**可行性分析**:

| 维度 | 评估 | 说明 |
|---|---|---|
| 历史深度 | 约 2-3 年 (付费档) | 官网标注含 NBA/NFL/Soccer 历史赔率，深度覆盖 2023 至今 |
| 成本 | ~$500/月 (历史档) | 小程 signal catalog v1 §路径 B 引用，data-contract-v1.md C-02 备选方案，需老钱批 OQ-14 |
| bookmaker 覆盖 | 需核对 8 家 ID 对应关系 | The Odds API 用自己的 key name (eg "williamhill", "bet365")，需映射到 Goalserve id |
| 数据格式 | JSON REST API | 有 Python SDK，离线拉历史数据后转 Parquet，不进生产 |
| 与 Goalserve 的一致性 | 需交叉验证 | 两个数据源的赔率可能有细微时间戳差异，需抽样比对确认偏差 < 0.5% |
| 速率限制 | 付费档宽松 | 历史端点通常批量友好 |
| ToS 风险 | 低 | 付费订阅用于内部回测，不 resell，合规 |

**关键问题**: The Odds API 历史数据包含的 bookmaker 是否与 Goalserve 8 家完全重叠，特别是 Marathon (id 17) 和 1xBet (id 105)。这两家是非欧洲主流，The Odds API 覆盖率需实测确认。

**6-12 前能通概率: 65-75%**（付费即可用，主要风险在 bookmaker 覆盖率和与 Goalserve 数据一致性校验时间）。

### §2.3 路径 C: CSV 免费历史数据

**原理**: football-data.co.uk 提供免费 Soccer 历史赔率 CSV，含 bet365/William Hill 等多家 bookmaker，覆盖英超/西甲/德甲/意甲等。

**可行性分析**:

| 维度 | 评估 | 说明 |
|---|---|---|
| Soccer 覆盖 | 英超/西甲/德甲/意甲 (2019 至今) | 历史深度足够，包含 IS 窗口 2024-10 起 |
| Basketball 覆盖 | 无 | football-data.co.uk 仅 Soccer |
| Tennis 覆盖 | 无 | Tennis 需另找数据源 (OddsPortal / 老彭手头数据) |
| bookmaker 字段 | B365 (bet365) + WHH (William Hill) + IWH (bwin) 等 | 通常含 5-7 家，缺 Marathon/1xBet/Betano |
| 数据格式 | CSV 逐场比赛，含赔率快照 (通常是开盘/收盘两个时点) | 时间粒度粗，不含分钟级快照 |
| 成本 | 免费 | |
| 局限 | 仅 Soccer，且时间粒度仅开盘/收盘，无法模拟 pregame -6h 时刻的赔率 | 对 pregame IS 窗口而言，覆盖率严重不足 |

**结论**: 路径 C 只能作为 Soccer Moneyline 的**粗粒度 sanity check**，不能作为主数据源。原因：第一，只覆盖 Soccer，Basketball 和其他运动无替代；第二，时间粒度是开盘/收盘，无法还原 T_kickoff - 6h 的精确 fair_value，会引入 look-ahead 风险；第三，缺 Marathon/1xBet，8 家均值无法构造。

**6-12 前能通概率 (作为辅助验证): 90%（下载即可）。作为主数据源: 不可行。**

### §2.4 路径汇总与推荐

| 路径 | 历史深度 | Basketball 覆盖 | 8 家完整度 | 时间粒度 | 成本 | 6-12 能通 |
|---|---|---|---|---|---|---|
| A: Goalserve API 历史回拉 | 未知 (黑盒) | 是 | 是 (同源) | 分钟级 (若可用) | 0 (已订阅) | 20-35% |
| B: The Odds API 付费 | ~2-3 年 | 是 | 70-85% (估) | 30min-1h | $500/月 | 65-75% |
| C: CSV 免费 | 2019 至今 | 否 | 40-50% | 开盘/收盘 | 0 | 不适合主路径 |

**推荐主路径: B (The Odds API)**

理由：
1. 历史深度确定，覆盖 IS 窗口 (2024-10) 无悬念
2. 含 Basketball/Soccer/Tennis 三大 sport
3. $500/月 对比 8-31 回测数字决定 12 月 GM-PAPER-G 上线的战略价值，投入产出合理，data-contract-v1.md 已将此列为 C-02 备选方案
4. OQ-14 老钱需批，sprint-1 启动前先提

**推荐备路径: A + C 并行验证**

路径 A 小段 6-05 前实测一次历史回拉（成本零，用已有 API key）。若成功，则 A 提升为主路径，节省 $500/月。路径 C 用于 Soccer 数据的 sanity check 和 bookmaker 覆盖率校验。

**执行建议**:

| 行动 | Owner | 截止 | 说明 |
|---|---|---|---|
| 路径 A 实测: 用 2024-10-01 unix ts 拉 basketball_10 一次 | 小段 | 6-05 | 单次实测即可判断可行性 |
| OQ-14 批复申请 (The Odds API $500/月) | 老彭 提议 → 老钱/老雷 批 | 6-01 (Sprint-1) | 已在 data-contract-v1.md C-02 列出 |
| The Odds API bookmaker 覆盖率核对 | 老彭 | 6-05 (批复后即查) | 确认 Marathon/1xBet 是否在覆盖列表 |
| 路径 B 历史数据拉取 (若批复) | 小余/小段 | 6-12 | Parquet 转换 + R-20 4 时间戳合规 |

---

## §3 Odds Quality 闸 (De-Vig 输入质量监控)

### §3.1 监控指标定义

接小董 stats validation framework，老彭在此定义 de-vig 输入的四项质量闸：

**Q1: Bookmaker 覆盖率闸**

```
per_match_books_used = COUNT(bookmaker_id WHERE odds_eu IS NOT NULL
                             AND odds_eu > 1.0
                             AND is_suspended = false)

规则:
  books_used >= 3  → 正常通过 (MIN_BOOKMAKERS 红线)
  books_used == 2  → 标 LOW_COVERAGE, 计入 quality_flag，不出信号
  books_used <= 1  → 标 SINGLE_SOURCE，完全排除，不进回测数据集
```

**Q2: Overround 分布闸 (Vig 分布异常检测)**

```
per_book_overround = (1/odds_yes + 1/odds_no) - 1.0

正常区间 (业界基准，老彭实测):
  Basketball Moneyline: overround ∈ [0.03, 0.07]  (3%-7%)
  Soccer Moneyline 2-way: overround ∈ [0.04, 0.09]
  Tennis Moneyline: overround ∈ [0.04, 0.08]

异常判定:
  overround < 0.01  → 疑似数据脏 (几乎无 vig，罕见)
  overround > 0.15  → 疑似陈旧赔率或市场恐慌定价
  处理: 该 bookmaker 本场排除，不参与均值计算
```

**Q3: 陈旧赔率闸 (Staleness)**

```
staleness_s = as_of_ts_ns/1e9 - data_source_ts_ns/1e9

规则 (pregame 窗口 T_kickoff - 6h 内):
  staleness_s <= 300   (5 分钟内)  → 新鲜，正常使用
  staleness_s ∈ (300, 1800]        → 标 STALE_WARNING，计入 diagnostic
  staleness_s > 1800  (超 30 分钟) → 标 STALE_REJECT，该 bookmaker 本场排除

说明: pregame 赔率在 T_kickoff - 6h 窗口通常每 5-30 分钟更新一次；
若某家 30 分钟无更新而其他家持续更新，大概率是 feed 中断或该家已挂起。
```

**Q4: 缺家模式监控 (系统性缺失告警)**

```
per_week_coverage = AVG(books_used) grouped by (sport, week_of_year)

告警阈值:
  per_week_coverage < 5.0   → COVERAGE_DROP 告警 (正常应 ≥ 6)
  同 sport 连续 2 周 < 4    → 升级到 DATA_SOURCE_INCIDENT，通知小余/小段

说明: 单场缺家是正常现象 (某家未给该场报价)；
若同一周内系统性覆盖率下降，则是 Goalserve feed 层面问题，
不是数据质量检查能修的，需要 D 单元排查 feed 中断。
```

### §3.2 与小董 Stats Validation 的接口

小董 stats validation framework §4.4 提到信号监控面板对接。老彭这四项 OQ 闸的输出字段建议：

```
quality_flag   -- enum: OK / LOW_COVERAGE / SINGLE_SOURCE / STALE_WARNING / STALE_REJECT / COVERAGE_DROP
books_used     -- int (有效参与 de-vig 的 bookmaker 数)
overround_avg  -- float (8 家均值 overround，诊断用)
max_staleness_s -- int (该场最大 staleness，秒)
```

这四个字段已在小程 alpha v2 spec §5.1 Feature 清单中列入 `goalserve_books_used` 和 `goalserve_overround_avg`。max_staleness_s 建议补入 Feature 清单，等 D↔C 接口签字会确认。

### §3.3 历史回填数据的 Quality Gate

历史回填数据进回测之前，小余需对整个历史 Parquet 做一次全量 quality scan：

| 检查项 | 通过标准 | 失败处理 |
|---|---|---|
| books_used 分布 | 全集中位数 ≥ 6，< 3 的行占比 ≤ 5% | 超 5% 则该时间段数据不进 IS，标 insufficient_coverage |
| overround 分布 | 各 sport overround 均值在预期区间 ±2% 内 | 异常段排除，记 data_quality_notes |
| R-20 时间戳单调 | event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts 100% 满足 | 单条违例即拒绝整批入库 |
| settlement 完整性 | 赛后 72h 内 settlement_result 非 NULL | 未结算场次标 unresolved，不进偏差计算 |

这份 quality scan 是小余 8-15 attestation 签字的前置条件（小程 spec §4.2）。

---

## §4 6-12 Deadline 风险评估

### §4.1 风险矩阵

| 风险 | 发生概率 | 影响 | 缓解 |
|---|---|---|---|
| R1: 路径 A 和 B 均在 6-12 前失败 | 25% | 极高 — 回测无数据，8-31 数字无法产出 | C 路径 Soccer sanity check 先跑，争取 IS 部分窗口数据；同时向小梁上报需延期 |
| R2: The Odds API (路径 B) 老钱 6-01 前不批 | 30% | 高 — 主路径延误 2 周 | 老彭在 Sprint-1 启动日 (6-01) 即提 OQ-14 请求，附本 plan 说明战略价值 |
| R3: 路径 B bookmaker 覆盖缺 Marathon/1xBet | 40% | 中 — books_used 均值从 8 降至 5-6，仍 ≥ MIN_BOOKMAKERS=3，但 fair_value 精度下降约 0.3-0.8pp | 接受精度损失，记录在 backtest report；或补其他家 (Pinnacle no-vig 单家补) |
| R4: 历史数据时间戳质量差 (R-20 违例) | 20% | 中 — 小余 attestation 不签字，数据返工 | 提前让小余做 quality scan，6-12 前至少有 IS 窗口 (2024-10 ~ 2025-02) 通过 |
| R5: 小段 ETL pipeline 排期未到 (D 单元工作量冲突) | 50% | 高 — 路径 B 数据拉下来但无法转 Parquet | 老彭 + 老雷升级优先级；小余主管协调 |

### §4.2 对 8-31 Net Edge 数字的连锁影响

**场景 1: 6-12 历史数据到位 (路径 B 成功)**

- IS 窗口 (2024-10 ~ 2025-02) 数据完整 → 参数扫描 81 组合正常推进
- 小蒋 backtest framework 7-09 ready → 老彭 8-31 回测数字可交付
- 概率加权: ~55-65% 可能

**场景 2: 6-12 数据部分到位 (仅 Soccer，或仅路径 C)**

- IS 窗口仅 Soccer Moneyline 可用 → Basketball/Tennis/NFL 缺失
- net edge 数字只能产出 Soccer Moneyline 子集，n_trades 可能 < 100 (30d)
- 影响: 8-31 数字置信度下降，bootstrap CI 可能过宽，小梁和老韩需决策是否接受部分数字上报 GM-PAPER-G
- 概率加权: ~20-25% 可能

**场景 3: 6-12 历史数据全部失败**

- 无历史数据 → 无法跑 IS 参数扫描 → 无法跑 OOS 回测
- 8-31 净 edge 数字无法产出
- 连锁影响: GM-PAPER-G 12 月门禁 (n_trades ≥ 100 + bootstrap CI 下界 > 0) 直接无法满足
- 概率加权: ~15-20% 可能

### §4.3 对 12 月 GM-PAPER-G 的连锁影响

小程 spec §4.1 指出 GM-PAPER-G 门禁要求 n_trades ≥ 100 (30d) + bootstrap CI 下界 > 0。这两个数字的产出链条是：

```
历史数据 Parquet (6-12)
    → IS 参数扫描 + 数据 attestation (8-15)
        → 老彭 backtest 跑实际回测 (8-31 数字)
            → 9-30 checkpoint (净 edge 数字为正?)
                → paper run 启动 (Q4 2026)
                    → 12 月 GM-PAPER-G 评估
```

若 6-12 历史数据缺口未弥补，整个链条从最前端断裂，12 月 GM-PAPER-G 评估将推迟 1-3 个月，直接威胁 MVP 12 月目标。

### §4.4 缓解策略 (6-12 前不齐的应急预案)

**应急方案 A (推荐): 部分窗口先跑**

若 6-12 前仅 Soccer 或部分 sport 数据到位：
- IS 窗口用可用数据子集跑，Basketball 若缺则跳过
- 把 IS 窗口起点从 2024-10 推迟到数据最早可用时点
- 向小梁上报: "8-31 数字基于 Soccer Moneyline 单 sport，n_trades 目标降至 ≥ 50 (30d)，bootstrap CI 宽度预期扩大"
- 向 GM 上报: 数据缺口风险，12 月 GM-PAPER-G 评估时间线可能右移

**应急方案 B: 实时录制 + 延期**

从今日 (2026-05-29) 起，立刻启动实时 Goalserve pregame getodds 录制 (小余 ETL)。虽然 17 个月数据无法立刻积累，但可以建立一个小型 2026-06 ~ 2026-08 的快速校验集：
- 用 2 个月实时数据做 mini backtest (n_trades ≥ 30，不满足 GM-PAPER-G 但可作为方向性验证)
- 同时继续推进路径 A/B 回填
- 成本: 实盘 paper 延期 3-4 个月（最差情况）

---

## §5 行动清单 (老彭视角)

| 优先级 | 行动 | Owner | 截止 | 说明 |
|---|---|---|---|---|
| P0 | 提 OQ-14 批复 (The Odds API $500/月)，附本 plan 战略价值说明 | 老彭 → 老钱/老雷 | 6-01 | Sprint-1 启动日第一件事 |
| P0 | 路径 A 一次性实测: 用 2024-10-01 unix ts 拉 basketball_10，看是否有历史数据 | 小段 (老彭 @小段) | 6-05 | 成本 0，即可判断 |
| P0 | The Odds API bookmaker 覆盖率核对 (Marathon/1xBet 是否在列) | 老彭 | 6-05 (批复后) | 决定是否需要补充其他 bookmaker |
| P0 | 路径 B 历史数据拉取 + Parquet 转换 + R-20 时间戳验证 | 小余/小段 (老彭协调) | 6-12 | 需 D 单元排期优先 |
| P1 | 路径 C: football-data.co.uk Soccer CSV 下载，做 Soccer Moneyline sanity check | 老彭 | 6-12 | 与路径 B 并行，验证赔率数量级正确 |
| P1 | 对 The Odds API 数据 vs Goalserve 数据做赔率一致性抽样比对 (目标: 偏差 < 0.5%) | 老彭 | 6-19 | 确保历史数据与实盘数据源一致性 |
| P1 | Odds quality 闸 (§3 四项) 写入小余 ETL quality scan 脚本，作为 8-15 attestation 前置 | 老彭 spec → 小余 执行 | 6-19 | 本文件 §3 已定义指标 |
| P2 | bwin (第 9 家) Goalserve ID 确认 | 小段 (老彭 @小段) | 6-19 | 不影响主路径，但影响均值精度 |

---

## §6 核心结论

**6-12 能否交付历史数据**: 有条件，概率 55-65%。

前提条件：
1. 老钱在 6-01 前批 The Odds API OQ-14 ($500/月)
2. 小余/小段 D 单元在 6-12 前完成路径 B 数据拉取 + Parquet 转换
3. The Odds API 对 Basketball/Soccer Moneyline 的 bookmaker 覆盖率 ≥ 5 家 (MIN_BOOKMAKERS=3 通过无障碍)

若上述三个前提全部满足，IS 窗口 (2024-10 ~ 2025-02) 数据可在 6-12 到位，8-31 回测数字按计划产出。

若 OQ-14 不批或 D 单元工作量无法排期，6-12 数据缺口概率升至 35-45%，需立即向小梁 + 老雷上报风险，触发 §4.4 应急预案 A 或 B。

**我的立场**: 路径 B (The Odds API) 是当前最可落地的主路径，应在 Sprint-1 (6-01) 第一天完成 OQ-14 提案。路径 A 同步实测，成功则更优（数据源一致性最好，且省钱）。路径 C 只做 Soccer sanity check，不作主路径。

---

*— 老彭 (betting-industry-expert, C 单元 IC), 2026-05-29*
*上报小梁 first review → 老雷 ack*
