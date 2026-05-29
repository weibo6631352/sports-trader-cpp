---
- owner: 老彭 (C 单元 IC, betting-industry-expert)
- last_review: 2026-05-29
- status: Draft
- wave: 92 (W10 W1)
- scope: P1-04 lineup-news-lag 信号 spec v0.1
- adr_cite: ADR-027 (N/A — 无核心 struct 改动, 纯 doc); ADR-029+032 (push 后不等 CI)
- report_to: 小梁 (C 主管)
---

# P1-04 lineup-news-lag — 信号 spec v0.1

**Wave 92 — 老彭, 2026-05-29**

---

## §1 ADR-027 cite

本文件为纯博彩业务 spec, 无核心数据结构变更. 适用 ADR-027 Enforce-1 判断:

- OrderIntent / SignedOrder / Position / MarketInfo / FairValue / OrderBookSnapshot: **均未修改**
- ADR-027 cite: **N/A**

后续 P1-04 C++ 实施 (小程 W10 W2) 若涉及 FairValue 或 MarketInfo 字段扩展, 须在实施 PR 独立补 ADR-027 cite 段, 格式:

```
cite:
  - polymarket_ssot: docs/RESEARCH/laoli-w8-polymarket-data-structure-ssot-v1.md (§<section>)
  - goalserve_ssot: docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md (§<section>)
```

---

## §2 信号原理

### §2.1 信号定义

SIG-P1-04 (lineup-news-lag) 捕捉**明星球员缺阵公告**与 **Polymarket 价格反应**之间的时间差所产生的定向 alpha.

核心逻辑: bookmaker (Pinnacle / DraftKings) 接到 lineup 新闻后 5-30 秒内调线; Polymarket 散户定价滞后 30-90 秒甚至更长. 这个时差形成可交易窗口.

### §2.2 lineup 数据源

老彭视角的数据源优先级:

| 来源 | 延迟估算 | 可靠度 | 备注 |
|---|---|---|---|
| **NBA 官方 injury report** (官网 PDF) | 45-90 min 赛前公布 | 高, 但提前量大 | 定时公布, 非实时 |
| **Goalserve inplay lineups** | 赛前 60-90 min; 变化时推 update | 中高 | 小段 W10 W2 审计确认字段 |
| **X (Twitter) — 球队记者爆料** | 0-5 min (reporter 发推) | 高, 但非结构化 | 是市场最快信号源; DK/Pinnacle 也靠这个调线 |
| **ESPN / The Athletic breaking news** | 1-10 min | 中 | 结构化差, 需 NLP |

**老彭判断:** 实际 alpha 窗口的起点是 **X reporter 爆料**, 终点是 Polymarket 散户完全消化价格. Goalserve lineup 字段是次级确认, 比 X 慢, 但结构化可机读. 系统设计上:

- **主触发:** Goalserve lineup update (小段 audit, 可机读, 延迟 10-60s vs X)
- **辅助验证:** X / ESPN 新闻流 (非结构化, M3+ 后考虑 NLP 接入)
- **当前 MVP 范围:** 仅 Goalserve lineup update 触发, X 不接

### §2.3 "news" 触发: load management announcement

典型触发场景 (按频率排):

1. **赛前 60-90 min load management 公告** — Finals/Playoffs 频发; 明星 (LeBron / Curry / Jokic) 分钟限制或 DNP (Did Not Play)
2. **赛前突发伤病退出** — 热身期扭伤; 0-30 min 赛前发生
3. **lineup scratch (先发变轮换)** — 先发名单换人, 赛前 1-2h 公布

**老彭按:** NBA Finals 期间, 每场 load management 触发概率 40-60% (某主力缺阵或限时). 常规赛约 15-25%. 这是 Finals 窗口选 P1-04 作 #1 alpha 的核心依据.

### §2.4 "lag" 测量: Polymarket vs sportsbook 反应延迟

**测量定义:**

```
lag = T_PM_adjust - T_line_move_sharp

T_line_move_sharp: Pinnacle / DraftKings 调线时间戳
                   (从 Goalserve bm 字段赔率变化推断)
T_PM_adjust:       Polymarket CLOB 中间价 mid 偏移达到预设阈值的时间戳
                   阈值: |mid_t - mid_pre_news| >= 3 cents (可配)
```

**历史案例参照 (老彭 Wave 87 §4.1):**

2024 Celtics vs Mavericks G4: Jayson Tatum 限时公告后, PM Mavericks ML 有 8 min lag, 期间 7pp gap. 类似场景 30-90 秒是更典型窗口 (小 lineup 调整); 8 min 是极端案例 (流动性低赛段).

**实测 lag 分布 (老彭行业估算, 需小蒋 W10 W3 backtest 验证):**

| 场景 | 预期 lag 中位 | 预期 lag 90th |
|---|---|---|
| NBA Finals load management | 45s | 120s |
| NBA Playoffs 突发伤病 | 60s | 180s |
| NBA 常规赛 lineup scratch | 90s | 300s |
| 非 NBA (MLB SP scratch) | 120s | 600s |

**alpha 窗口定义:** 信号触发后 **T+0 到 T+90s** 为核心窗口; T+90s 之后 PM 价格快速收敛, edge 衰减显著. 系统必须在 T+15s 内完成定价估算 + 下单决策.

---

## §3 数学模型

### §3.1 隐含 win prob 估计: 含 / 不含明星

**基础模型 — multiplicative devig:**

老彭文件 `laopeng-multiplicative-devig-calibration-v1.md` 已建立 PM implied prob 的 devig 方法. P1-04 在此基础上增加"缺阵修正项".

**含明星 (pre-news) 基准:**

```
p_base = devig(PM_mid_pre_news)
       = PM 赛前中间价经 multiplicative devig 后的隐含胜率
```

**不含明星 (post-news 预期) 估算:**

```
delta_p = -W_star * RAPM_star / RAPM_team_avg

参数:
  W_star:          明星 projected minutes share (缺阵前)
                   例: Jokic 35 min / 48 min = 0.729
  RAPM_star:       明星当赛季 RAPM (Regularized Adjusted Plus-Minus)
                   例: Jokic +8.5 (每 100 possession)
  RAPM_team_avg:   全队加权 RAPM 平均

p_post_news = p_base + delta_p * direction_factor
direction_factor: +1 if star on home team, -1 if star on away team
```

**简化版 (MVP 阶段):**

复杂 RAPM 数据 NBA Finals 阶段暂不接入 (M3 后). MVP 用查表法:

```
star_impact_lookup:
  S+ player (RAPM > +6): delta_p = -6% to -10%
  S  player (RAPM +3~6): delta_p = -3% to -6%
  A  player (RAPM +1~3): delta_p = -1% to -3%

star tier 由老彭手动标注 (静态配置文件), Finals 10 强参赛球员覆盖
```

### §3.2 edge 计算

**edge = 预期值 - 当前 PM 价格:**

```
fair_value_post_news = p_post_news   (含明星修正后的真实胜率估计)

edge_raw = fair_value_post_news - PM_ask    (做多 YES 方向)
         或
edge_raw = (1 - fair_value_post_news) - PM_ask_NO  (做多 NO 方向)

net_edge = edge_raw - fee_estimate - slippage_estimate

fee_estimate:      Polymarket taker fee ~1-2 cents (当前 maker = 0)
slippage_estimate: 单笔 $500 下单预期滑点 ~0.5-1 cent (Finals 流动性高)
```

**触发条件 (gate):**

```
触发: net_edge >= 2 cents AND lag_elapsed < 90s AND lineup_confidence >= HIGH
```

lineup_confidence 三级: HIGH (Goalserve 官方确认) / MEDIUM (单一新闻源) / LOW (社交媒体未确认). MVP 阶段只触发 HIGH.

### §3.3 decay 模型: tau ~30s

PM 价格在 lineup news 后呈指数收敛. 基于行业类比 (Polymarket election markets 信息 decay 观测):

```
edge_t = edge_0 * exp(-t / tau)

tau = 30s (Finals 高流动性场景)
    = 60s (常规赛低流动性)

理论: 下单必须在 T+tau (30s) 内完成, edge 衰减到 37%.
      T+2*tau (60s) 后 edge 衰减到 14%, 基本无利可图.
```

**实际操作含义:** 收到 Goalserve lineup update → 系统 15s 内完成定价 → 下单. 留 15s buffer 对应 ~60% edge 保留 (exp(-15/30) = 0.607).

**decay tau 校准:** 需小蒋 W10 W3 backtest 用历史 PM price time series + Goalserve event ts 对齐验证. 当前 tau=30s 是老彭经验估算, 视为初始值.

---

## §4 实施依赖

### §4.1 Goalserve lineup endpoint (小段 W10 W2 audit)

**老彭需要小段确认的字段:**

```
/soccer/{competition}/matches/{match_id}/lineups   (足球参考路径)
/basketball/nba/matches/{match_id}/lineups         (NBA 实际路径待确认)

关键字段:
  - player.status: "active" | "inactive" | "dnp" | "questionable"
  - player.name + player.id (跨源 mapping 用)
  - lineup.confirmed: bool (是否官方确认还是预测)
  - event_ts: 上次更新时间戳 (R-20 四时间戳契约必须)
  - lineup.type: "starting" | "bench" | "inactive_list"
```

**老彭 ask @小段:** W10 W2 lineup audit 产出文档须包含:
1. NBA inplay lineup endpoint 实测路径 (含 host + 认证)
2. player.status 枚举值全集 (实测, 不只看文档)
3. lineup update 推送机制: 是 polling 还是 subscription event?
4. Finals 场景下 lineup update 触发频率 (小段实测或查 Goalserve 文档)
5. event_ts 字段存在性确认 (R-20 红线)

**备用方案 (若 Goalserve lineup 字段不可用):** 退回 X/Twitter 新闻流 + 人工确认. 该方案 M3 前不做自动化, Finals 期间老彭人工 monitor.

### §4.2 老李 PM CLOB V2 接 (Wave 91 V2 升级后)

P1-04 下单需要 PM CLOB V2 接口:

```
依赖:
  - CLOB V2 market order API (taker, time-sensitive)
  - orderbook snapshot 订阅 (实时 mid price 更新)
  - 老李 Wave 91 V2 升级产出: laoli-w9-wss-subscriber-impl-spec-v1.md

关键需求:
  - T+0 到 T+15s 内从 lineup event 到 order submit
  - 要求 taker order (maker 等待不合适, 时间窗口不允许)
  - order size: $200-1000 单笔 (Finals 流动性足以吸收)
```

**阻塞项:** 老李 PM CLOB V2 接已在 Wave 91, P1-04 C++ 实施 (小程 W10 W2) 必须等老李 V2 接口稳定. 小梁协调排期.

### §4.3 小冯 inplay subscriber (Wave 79)

```
依赖:
  - 小冯 inplay event subscriber (Goalserve WSS / polling)
  - 产出文档: (小冯 Wave 79 产出, 待确认路径)
  - P1-04 lineup event 需要从 inplay subscriber 的事件流中过滤出
    lineup change event (player.status 变化)

接口需求 (老彭 → 小冯 / 小梁协调):
  - 事件过滤: event_type == "lineup_update" AND player.status == "inactive"
  - 推送延迟: < 5s (从 Goalserve 服务器到本地消费者)
  - 时间戳传递: event_ts 必须保留 (R-20 红线), 不可用本地 now() 替代
```

---

## §5 NBA Finals 6 月窗口

### §5.1 G1-G7 预期触发频率

**基于老彭 Wave 87 §4.1 + 博彩行业历史数据:**

| 场次 | 预期触发次数 (P1-04) | 说明 |
|---|---|---|
| G1 (6/5 左右) | 5-8 次 | 第一场双方保守, lineup 变化相对少 |
| G2 | 6-10 次 | 节奏摸清后调整增加 |
| G3 | 8-12 次 | 系列赛焦虑, load management 升频 |
| G4 | 8-12 次 | 同 G3 |
| G5 (match point) | 10-15 次 | 压力最大, 临场决定多 |
| G6 (若有) | 8-12 次 | |
| G7 (若有) | 10-15 次 | 全力出战但也有临场决定 |
| **全系列赛合计 (G5 胜出)** | **~37-52 次** | |
| **全系列赛合计 (G7 胜出)** | **~55-84 次** | |

**说明:** "触发次数" = P1-04 信号满足条件 (Goalserve lineup update + net_edge >= 2 cents) 的事件数, 不等于成交次数. 预期成交率 ~50-70% (流动性 + 系统延迟过滤).

**容量估算:** 单触发 $300-800 下单, 全系列赛 37-84 次触发, 成交 20-60 笔, 总仓位 $6,000-$48,000. 在 Finals 流动性下 (单场 $10-30M PM volume) 完全可吸收.

### §5.2 Paper runtime W11 能否赶 Finals 尾声?

**时间线分析:**

```
当前时间:    2026-05-29 (W9 W5)
W10 W2:      小程 P1-04 C++ 实施开始
W10 W3:      小蒋 backtest 开始
W11 估算:    2026-06-09 ~ 2026-06-13 (假设 W = 周)
NBA Finals:  约 6/5 G1 → 最早 6/17 G5 结束 (5局), 最晚 6/24 G7 结束

Finals G1:   6/5 左右 → W10 W1 (本周)
Finals G3:   6/10 左右 → W10 W3
Finals G5:   6/15 左右 → W11 W1
Finals G7:   6/22 左右 → W11 W3 或 W12
```

**老彭判断:** W11 paper runtime 启动期可以赶上 **Finals G5-G7** (若系列赛打满). 如果系列赛 5 场结束 (6/17 前), 只能观测赛后数据.

**建议:**

1. **加速路径 (赶 Finals 中段):** 小程 W10 W2 完成 P1-04 C++ skeleton + 单测; W10 W3 小梁 review; W10 W4-W5 paper mode 热身. 目标 W11 W1 (6/9) paper trading 上线. 可覆盖 Finals G3 (约 6/10) 开始.

2. **保守路径 (赶 Finals G5+):** 按正常排期 W11 paper runtime. 可覆盖 G5-G7 (若系列赛打满). 若 5 场结束则错过全部 Finals.

3. **数据收集优先:** 即使 paper trading 来不及, 老彭建议 **W10 W2 起就跑 lineup event logger** (只记录, 不下单). 积累 Finals G1-G7 的 lineup event + PM price time series 数据, 为 小蒋 backtest + M2 实盘提供真实 Finals 数据集.

**老彭立场:** 推荐加速路径. 如果 V2 接口依赖 (老李) 无法在 W10 W3 前 ready, 至少跑数据采集模式 (不接 CLOB 下单).

---

## §6 W10 派单

以下为老彭作为 C 单元 IC 提出的 W10 ticket 候选, **须经小梁 (C 主管) review + 派单, 不由老彭直接派.** (ADR-005 §派单层级: IC 提需求, 主管派 IC.)

### §6.1 小程 W10 W2: P1-04 C++ 实施

```
提案人:    老彭
建议 owner: 小程 (C 单元 IC, signal research)
截止:       W10 W2 EOD
产出:
  - P1-04 信号引擎 C++ skeleton
    src/signals/p1_04_lineup_news_lag.hpp / .cpp
  - 依赖: Goalserve lineup event 接口 (小段 audit 结果)
  - 依赖: PM CLOB V2 taker order 接口 (老李 Wave 91)
  - 单测: 覆盖 lineup event parsing + edge 计算 + decay 截止判断
  - 不含: 完整 paper trading 集成 (W11 里程碑)
内容要点:
  - 实现 §3 数学模型 (star_impact_lookup 静态配置)
  - 实现 tau=30s decay gate (可配)
  - 触发条件: net_edge >= 2 cents + lag_elapsed < 90s + lineup_confidence == HIGH
  - 时间戳链路: event_ts 全程传递 (R-20 四时间戳契约)
  - ADR-027 cite: 若涉及 FairValue / MarketInfo 字段扩展须补 cite 段
验收方: 小梁
```

### §6.2 小段 W10 W2: Goalserve lineup audit

```
提案人:    老彭
建议 owner: 小段 (D 单元 IC, Goalserve 专家)
截止:       W10 W2 EOD
产出:       docs/RESEARCH/xiaoduan-w10-nba-lineup-endpoint-audit-v1.md
内容要点 (见 §4.1 详细 ask list):
  1. NBA lineup endpoint 实测路径 + host + 认证
  2. player.status 枚举值全集 (实测)
  3. lineup update 推送机制 (polling / subscription)
  4. Finals 场景下 lineup update 频率
  5. event_ts 字段存在性确认 (R-20)
  6. 与 xiaoduan-w8-goalserve-data-structure-ssot-v1.md §相关节 的 delta 更新
注意: 小段是 D 单元 IC, 须经小余 (D 主管) 派单. 老彭的需求走小梁 → 小余协商渠道.
验收方: 小余 (D 主管) + 小梁 (需求方确认)
```

### §6.3 小蒋 W10 W3: P1-04 backtest

```
提案人:    老彭
建议 owner: 小蒋 (C 单元 IC, backtest)
截止:       W10 W3 EOD
产出:       docs/RESEARCH/xiaojiang-p1-04-backtest-report-v0.1.md
内容要点:
  - 数据集: 2024 NBA Finals (Celtics vs Mavericks G1-G5) + 2025 NBA Finals
    (需小余 / 小冯 提供历史 inplay event + PM price time series)
  - 验证指标:
    * lag 分布 (中位 / 90th): 对照 §2.4 老彭估算
    * tau 实测 (decay 拟合): 对照 §3.3 tau=30s 假设
    * edge_0 分布 (触发时 raw edge): 确认 2-5 cents 范围
    * net_edge hit rate (net > 0 的比例)
  - 输出: tau 校准值 + hit rate + 容量上限建议
依赖:
  - 小段 W10 W2 lineup audit (确认 event_ts 字段可用)
  - 历史数据: 小余协调 小冯 提供 (D 单元)
  - 小程 W10 W2 P1-04 skeleton (或仅数学模型, 不依赖 C++ 完成)
验收方: 小梁
```

---

## §7 ADR-029+032 流程记录

```
Step 1. pwd verify: .../sports-trader-cpp/.claude/worktrees/agent-aa491e3f7ac123658
Step 2. git add -A
Step 3. git commit -m "docs(research): P1-04 lineup-news-lag spec v0.1 (老彭 Wave 92)"
Step 4. git fetch origin
Step 5. git merge origin/main --no-edit
Step 6. git push origin worktree-agent-aa491e3f7ac123658
Step 7. gh pr create
Step 8. 回汇小梁 + GM
ADR-032: push 后不等 CI (本地 pre-push hook 为裁判, 远端 CI 不阻塞)
```

---

**汇报小梁 (C 主管):**

Wave 92 P1-04 lineup-news-lag spec v0.1 完成. 核心结论:

1. **信号结构清晰:** Goalserve lineup update 触发 → delta_p 估算 (star_impact_lookup) → decay gate (tau=30s) → net_edge >= 2 cents 下单. 数学模型可直接实施.

2. **Finals 窗口紧张:** G1 约 6/5. 加速路径目标 W11 W1 paper trading 上线 (覆盖 G3+). 若 V2 接口未 ready, 最低限度跑 lineup event logger 积累真实 Finals 数据.

3. **跨单元协调需求:** 小段 lineup audit (D 单元) 需小余协商; 老李 V2 接 (A 单元) 排期需小梁确认. 建议小梁在本周内协调好这两条依赖.

**W10 票候选 (待小梁派单):** 小程 W10 W2 C++ 实施 + 小段 W10 W2 lineup audit + 小蒋 W10 W3 backtest.

— 老彭, 2026-05-29
