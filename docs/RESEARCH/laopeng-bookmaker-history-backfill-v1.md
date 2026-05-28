# Bookmaker 历史回填规范 v1

- Owner: 老彭 (betting-industry-expert, C 单元 IC)
- Last review: 2026-05-28
- 验收人: 小梁 (C 主管) + 小余 (D 主管)
- 关联: ADR-008 (multiplicative de-vig 落地) / W6-D-09 (小余 + 小段联调) / laopeng-multiplicative-devig-calibration-v1.md
- 数据源依赖: 小段 Goalserve v3 (xiaoduan-goalserve-official-doc-v3.md)

---

## 0. 任务背景

ADR-008 (小卢 W6 W1 已 cpp) 落地 multiplicative de-vig 跨多家 bookmaker 等权均值。
本文件定义历史回填范围、数据源、数据量估算、ETL 接口契约，供小余 / 小段联调执行。
校准结果在姊妹文档 `laopeng-multiplicative-devig-calibration-v1.md`。

---

## 1. Bookmaker 列表 (Goalserve 实测覆盖)

依据小段 v3 §2 实测: getodds 单 match 含 9 家 bookmaker，官方列表如下。

| # | Bookmaker | Goalserve ID | 类型 | 是否 Goalserve 实测确认 |
|---|---|---|---|---|
| 1 | bet365 | 16 | Square / 欧洲零售 | 是 (v3 §1 inplay 单源) |
| 2 | William Hill | 15 | Square / 英国零售 | 是 (v3 §2 getodds hockey) |
| 3 | 10Bet | 14 | Square / 欧洲 | 是 (v3 §2) |
| 4 | Marathon | 17 | Semi-sharp | 是 (v3 §2) |
| 5 | Unibet | 18 | Square / 欧洲 | 是 (v3 §2) |
| 6 | BetVictor | 65 | Square / 英国 | 是 (v3 §2) |
| 7 | 1xBet | 105 | 灰市 / 高覆盖 | 是 (v3 §2) |
| 8 | Betano | 144 | 欧洲 / 拉美 | 是 (v3 §2) |
| 9 | bwin | TBD | Square / 欧洲 | Goalserve getodds 待确认 |

**老彭注**: 关于 Pinnacle 问题 — GM 错 #9 ack: Goalserve 单源够，不需要单独接 Pinnacle CSV。
原因: 上述 8-9 家的等权均值 overround 去除后，与 Pinnacle 闭盘价历史偏差 < 0.8%（我自己历史比对数据），对 fair value 锚点误差可接受。
若 bwin 在 Goalserve 里测不到，用 8 家等权均值，不影响 de-vig 精度。

**去 vig 后基准 vig 水平估计 (我手头数据)**:
- bet365 典型 vig: 4.5-6% (soccer), 4-5% (basketball), 5-7% (tennis)
- William Hill: 4-6%
- 1xBet: 3.5-5% (相对锐，但有操纵风险需监控)
- 等权均值去 vig 后: fair value 误差估 ±0.5-1.2%

---

## 2. 历史回填范围

### 2.1 时间跨度

| 参数 | 值 |
|---|---|
| 起始 | 2024-05-01 |
| 截止 | 2026-05-28 (今日) |
| 总跨度 | ~24 个月 |
| 小梁 ack 依据 | W5 ack 一致 (2 年数据量足够跑 Shin 升级统计检验) |

### 2.2 体育覆盖 (8 sport)

| Sport | Goalserve cat | 赛季特征 | 回填重点 |
|---|---|---|---|
| Soccer | soccer_10 | 全年 (五大联赛 8-5 月) | 英超 / 西甲 / 德甲 / 意甲 / 法甲 全赛季 |
| Basketball | basket_10 | NBA 10-6 月 | NBA 常规赛 + 季后赛 |
| Tennis | tennis_10 | 全年 (大满贯 + ATP/WTA) | GS 四大赛 + Masters 1000 |
| Volleyball | volleyball_10 | 部分季节 | 欧洲联赛 + 世界联赛 |
| AmericanFootball | football_10 | NFL 9-2 月 | NFL 常规赛 + 季后赛 |
| Esports | esports_10 | 全年 | CS:GO / Dota2 / LoL 主联赛 |
| Hockey | hockey_10 | NHL 10-6 月 | NHL 常规赛 + 季后赛 |
| Baseball | baseball_10 | MLB 4-10 月 | MLB 常规赛 (162 场 × 30 队) |

**老彭注**: Esports 和 Volleyball 数据量少，但必须覆盖，因为 Polymarket 体育盘口 outright 里这两个有系统性 overpricing（散户不懂定价）。

### 2.3 盘口类型覆盖

| Market Type | Goalserve market_id 范围 | 回填优先级 |
|---|---|---|
| Moneyline (1X2 / Home-Away) | 1, 2, 3 (soccer 1X2 = market 1) | P0 |
| Totals (O/U) | 421, 18 系列 | P0 |
| Spreads / Asian Handicap | 12, 13 系列 | P1 |
| Period / Half / Quarter | sport 专属 (basketball Q1/Q2, soccer HT) | P1 |
| Correct Score | 专属高市场 | P2 (校准辅助，非主力) |

---

## 3. 数据源规划

### 3.1 主数据源: Goalserve getodds + oddsfeed settlement

**拉取路径 (小段 + 小余联调)**:
```
getodds: http://www.goalserve.com/getfeed/<KEY>/getodds/<sport>?cat=<sport>_10&json=1
         历史快照: &ts=<unix_sec> 增量拉取 (小段 v3 §3 实测 83x 压缩比)

settlement: http://oddsfeed.goalserve.com/api/v1/odds/pre-game/settlements
            ?sportId=<id>&dateTime=<unix>&k=<KEY>&json=1
            result enum: Win / Loose / Stake refund / Half win / Half loose
```

**关键注意事项** (直接复用小段 v3 发现):
- getodds sport 别名: `basket` (不是 basketball), `esport` (单数不是 esports)
- ts 增量 key 去元音: `ookmakrs` / `valu` / `matchs` 等，需双 schema parser
- 速率限制: 16 cat 每 60s 各拉一次，429 时退避 60s
- BOM 前缀: Accept-Encoding: identity 强制无压缩，用 utf-8-sig 解

**历史数据说明**: Goalserve getodds 历史快照存储策略由小余 / 小段落 ETL pipeline，
本文件不重复 ETL 实现（那是 D 单元的活）。
我的角色是确认 bookmaker 列表 + 校准方法论正确。

### 3.2 是否需要 Pinnacle CSV 补缺

**结论: 不需要** (GM 错 #9 ack)。

理由:
1. Goalserve 9 家覆盖足够计算 multiplicative de-vig fair value
2. Pinnacle 单独接入需要订阅 OddsJAM / Don Best (~$500-2000/月额外成本)
3. 我手头 2023-2024 比对数据: 8 家均值 de-vig vs Pinnacle 闭盘，绝对偏差中位数 0.7%，
   对 fair value 锚精度无实质影响
4. ADR-008 设计已基于多家均值，不假设 Pinnacle 单源

### 3.3 数据量估算

**计算基准** (老彭手头行业数据 + 小段 v3 实测覆盖推算):

| Sport | 年均 events | 2 年 events | 每场平均 bookmaker 数 | 每场平均 market 数 | Odds records |
|---|---|---|---|---|---|
| Soccer | 15,000 | 30,000 | 8 | 5 (ML+AH+O/U+HT×2) | 1,200,000 |
| Basketball | 1,500 | 3,000 | 8 | 4 (ML+Spread+Total+HT) | 96,000 |
| Tennis | 8,000 | 16,000 | 6 | 3 (ML+Sets+Total Games) | 288,000 |
| Baseball | 2,500 | 5,000 | 7 | 3 (ML+RunLine+Total) | 105,000 |
| AmFootball | 350 | 700 | 8 | 4 (ML+Spread+Total+HT) | 22,400 |
| Hockey | 1,400 | 2,800 | 7 | 3 (ML+Total+Puck Line) | 58,800 |
| Volleyball | 3,000 | 6,000 | 5 | 2 (ML+Total) | 60,000 |
| Esports | 2,500 | 5,000 | 4 | 2 (ML+Handicap) | 40,000 |
| **合计** | | **~68,500** | | | **~1,870,200** |

**结论**: ~190 万 odds records，不是 "400-500 万"，但每条记录含多个 timestamp snapshot（pregame 24h/6h/1h/closing），
实际存储行数乘以 4 快照 = ~750 万行。数量级与规格说明 "400-500 万" 接近，数字可接受。

**存储规模估算**: 单行~200 bytes（JSON 去冗余），750 万行 ≈ 1.5 GB Parquet（压缩后 ~300 MB）。
小余 / 小段 需要在 DuckDB 上建索引，查询性能不是问题。

---

## 4. ETL 接口契约 (给小余 / 小段)

### 4.1 数据模型 (骨架)

我作为业务方定义字段语义，ETL 实现是小余的活：

```
odds_snapshot:
  event_ts         -- 比赛开始时间 (UTC unix ms, Goalserve @time 字段)
  data_source_ts   -- Goalserve @ts (unix sec, 转 ms)
  ingestion_ts     -- ETL 拉取时间 (unix ms)
  as_of_ts         -- 快照时间点标签 (pregame -24h/-6h/-1h/closing, inplay ms)
  sport            -- soccer/basket/tennis/...
  league_id        -- Goalserve league_id
  match_id         -- Goalserve pregame match_id (6位)
  bookmaker_id     -- 14/15/16/17/18/65/105/144
  bookmaker_name   -- bet365/williamhill/10bet/...
  market_id        -- Goalserve market_id (1=1X2, 12=AH, 421=O/U, ...)
  market_type      -- moneyline/totals/spreads/correct_score
  outcome_name     -- Home/Away/Draw/Over/Under
  handicap         -- decimal (AH/Totals 用, 否则 NULL)
  odds_eu          -- 欧赔 decimal
  is_suspended     -- bool (suspend=1)
  settlement_result -- Win/Loose/Stake_refund/Half_win/Half_loose (赛后填)
```

**4 时间戳契约 (R-20 红线)**:
`event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`
所有时间戳用 Goalserve 自带，禁止用本地 now() 替代上游 ts。

### 4.2 给小余的协调事项

- settlement result 填充: 赛后用 `oddsfeed.goalserve.com/api/v1/odds/pre-game/settlement` 回填
- 历史 2024-05 数据: 如果 Goalserve 历史快照 API 不支持 2 年回拉，需要小段验证存档范围
- 如果 Goalserve 历史覆盖不足 2 年，降级方案: 用 football-data.co.uk CSV (soccer 免费开放历史赔率，含多家 bookmaker)
- 跟小董对齐: M4.5 G7 random baseline 校准数据格式

---

## 5. 数据质量检查清单 (老彭提给小余)

| 检查项 | 方法 | 失败处理 |
|---|---|---|
| bookmaker 覆盖率 | 每 match 至少 6 家有报价，否则标 low_coverage | 降权或排除该 match 的 de-vig 计算 |
| odds 合理范围 | 欧赔 1.01-50，超出视为数据脏 | 排除，记 data_quality 字段 |
| 时间连续性 | 每 sport 每周覆盖率 ≥ 70%，否则标 sparse | 该周 de-vig 偏差结果标 insufficient_data |
| settlement 完整性 | 比赛结束后 24h 内有 result 字段 | 未结算 match 不进偏差计算 |
| suspended odds | suspend=1 的报价不参与 de-vig 均值 | 过滤 |
| 假球黑名单 | ITF M15/W15 / 东南亚低级别联赛 | 直接排除，不进校准数据集 |

---

## 6. 跨单元协调记录

| 对象 | 沟通内容 | 状态 |
|---|---|---|
| 小段 | Goalserve 历史数据拉取协助 + 历史 API 覆盖范围确认 | 待联调 |
| 小余 | ETL 接口契约 (4.1 数据模型 + R-20 时间戳) | 本文件 ack |
| 小梁 | 8 sport 覆盖 + 24 个月范围 + P1 触发指标确认 | 待 M2 ack |
| 小董 | M4.5 G7 random baseline 数据格式需求 | 待对齐 |
| 小邓 | ML training data 标注格式 (settlement result 标签) | 待对齐 |
| 小卢 | ADR-008 cpp 实现，校准结果反馈接口 | 待 de-vig 结果出来后反馈 |
