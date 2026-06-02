# ML 特征有效性审计 + 修复执行计划 v1

> owner: 老雷 (GM) | last_review: 2026-06-02
> 触发: 老板「检查我们所有特征的有效性来源，挨个检查」+「全部列个计划挨着解决，顺带把其他能验证的都验证一遍」
> 证据源: 服务器 `/api/v1/features/health` 实时运行数据 (n=126~132 样本, ~9-10 匹配直播盘) + 源码链路追踪

---

## 0. 总览 (实测)

| 状态 | 数量 | 含义 |
|---|---|---|
| healthy | 76 | 有数据 + 有方差, 已抽查值域 sane (b_mid~0.5 / elapsed / ofi / vol 合理) |
| const | 7 | 有数据无方差 |
| dead | 31 | 从没非零 |
| **total** | **114** | kMlFeatureCount |

**核心结论: 31 个 dead 不是 31 个 bug。必分三类:**
- **真断 (代码缺口)** — 字段全库无写入点, 改代码才活。**这才是该修的。**
- **条件性 0 (数据状态)** — 代码对, 等状态触发 (持仓/比赛末段/无时钟运动)。非 bug。
- **覆盖缺口 (数据源)** — 代码对, 上游数据极少到 (白名单/赛季/plan)。非代码活。

---

## 1. 真断 — 代码缺口 (该修, 按价值/成本比排序)

| 组 | 特征 # | 根因 (已验证) |
|---|---|---|
| **A 跨庄家 de-vig** | 5 g_bm_devig_p_yes / 6 overround_avg / 7 valid_bm_count / 16 x_devig_minus_mid | `GameRecord.bm_slots` **全库零写入**(`grep 'bm_slots['` 无命中)。Goalserve OddsRecord 没接进 bm_slots → valid_bm_count 恒 0。#16 级联。**双重价值: 既是特征也是 sharp fair 锚。** |
| **B period int** | 2 g_period | `goalserve::GameRecord.period`(`optional<uint8_t>`, record.hpp:99)**全库无赋值**。inplay parser 出的是 `EventScore.period`**字符串**(parser.cpp:374), 没转进喂模型的 GameRecord。 |
| **C soccer stats** | 19 danger_attack / 20 shot_on_target / 21 possession / 22 red_card / 23 corner | `game_row.soccer_*` **全库只读不写**(paper_loop:694 是读 `>=0`)。即使 Goalserve 发 stats 也没人填。(仅足球有意义; 前提: plan 给不给 stats + 是否有足球直播) |
| **D book 派生** | 11 b_spread_bps / 12 b_top3_depth_usdc | 计算**存在**(orderbook_adapter:312/335), 但喂模型的 book row 走了只填 mid/microprice/best 的轻路径, 没带 spread/depth。 |
| **E 类别编码** | 82 cat_asset_class / 84 cat_market_type | 全库未填。gamma sport taxonomy 已有 ([[polymarket-market-taxonomy]]), 纯编码缺口。 |
| **F 杂项派生** | 26 b_bid_absence_frac / 78 x_yes_no_book_skew_sec / 105 x_arb_free_edge / 59 resolution_status | 派生量未算 / 生命周期码恒默认。 |

## 2. 条件性 0 — 非 bug, 等状态触发

| 特征 # | 为何 0 | 改进点 (可选) |
|---|---|---|
| 67 g_remaining_sec / 70 g_game_phase / 71 garbage_time / 72 clutch | 派生自 `time_frac = elapsed / total_game_seconds(sport)`。当前直播多为**无时钟运动**(tennis/baseball/volleyball `total=0`, fair_value_estimator.hpp:441-445) 或**早段**(time_frac<0.33→phase=0) 或**非末段**(<0.85→clutch/garbage=0)。纯早段=正确为 0。 | 无时钟运动用 set/inning 进度替代 time_frac 当 phase 锚 (真改进) |
| 48-53 pos_* | **空仓** → 预期全 0。有成交即活。 | 零改动 |
| 68/69 g_periods_won | 需分节比分 `score_*_periods[]` 填充 + 多节运动 | 接分节比分 |

## 3. 覆盖缺口 — 非代码

| 特征 # | 现状 | 动作 |
|---|---|---|
| 18 g_bm_inplay_fair / 102 x_inplay_fair_minus_mid / 103 absdev | 路径**存在**(paper_loop:511, inplay_feed:620), pop=1 → inplay feed 极少带 odds | 验证 feed odds 覆盖 (白名单/sport/赛季), 非代码 |

## 4. const — 真常量 (可选裁剪)

| # | 值 | 判定 |
|---|---|---|
| 54 fee_rate_coef | 0.03 | 体育盘恒定费率, 信息量 0。裁维度需 retrain + 列序锁, 成本>收益, 低优先。 |
| 55 devig_ok | 1 | 恒真, 信息量 0。同上。 |
| 15 b_book_levels_valid | 2 | 暴露 PM 体育盘**深度浅**(真相非 bug)。也是 D 组深度派生算不出的物理原因。 |
| 58 time_to_resolution_frac | (现已转 healthy, live 多了) | — |

---

## 5. 执行计划 (挨个解决, 按价值/成本比)

> 原则: 每改一组 → 重启 paper_server → `features/health` 复查 status 翻转 → 落审计行。
> 约束: 改 kMlFeatureCount / 列序 / 单位 → 走 R-4 通知下游 + retrain + 训练数据版本号。纯加性末尾列走 §8.1 低仪式 carve-out。全为 paper 期 advisory 特征质量, 不碰真钱开闸, 无需会签。

### 阶段 1 — 低成本高价值 (纯解析/接桥, 不改维度)
- **P1.1 g_period string→int**(#2): inplay→GameRecord 桥加运动感知映射表 (soccer 1H=1/2H=2/ET=3; basket Q1-4=1-4/OT=5; tennis Set N=N; baseball inning N=N)。复活 1 + 为 P3.2 clock-less phase 打基础。
- **P1.2 cat_asset_class/market_type**(#82/#84): gamma taxonomy 已有, 纯编码填充。复活 2。

### 阶段 2 — 中成本高价值 (接数据源)
- **P2.1 bm_slots 跨庄家赔率**(#5/#6/#7/#16): 先验证 Goalserve odds feed 多庄家覆盖 → 接 OddsRecord→`GameRecord.bm_slots`。复活 4 + sharp fair 锚 (方向性信号最强外部输入)。
- **P2.2 inplay_fair 覆盖验证**(#18/#102/#103): 查 feed odds 为何 pop=1 (白名单?)。非代码则记录覆盖缺口, 不强修。

### 阶段 3 — 中成本中价值
- **P3.1 book 派生补填**(#11/#12): 定位喂模型轻路径, 补 spread_bps_f/top3_depth_usdc。复活 2。
- **P3.2 clock-less phase 改进**(#67/#70 for tennis/baseball): set/inning 进度当 phase 锚。
- **P3.3 soccer live-stats**(#19-23): 验证 plan 给不给 stats → 解析 commentaries 填 soccer_*。复活 5 (仅足球)。

### 阶段 4 — 低价值/可选
- **P4.1 杂项派生补算**(#26/#78/#105/#59)。
- **P4.2 const 裁剪决策**(#54/#55): 与下次 retrain 合并, 不单独动。

### 横切验证 (已完成部分)
- ✅ 76 healthy 抽查值域 sane (b_mid [0.005,0.989] mean 0.507; elapsed [0,1434]; ofi/vol 合理)。
- ✅ 模型本体: 8 个 ~112KB `model_seq_arb_h*.onnx` (h2s..h60s) — 但那是 **SeqArb 短时模型, 已停摆** ([[shorthorizon-locking-feasibility]])。喂这 114 特征的 **FairValueModel 身份单列待确认**。
- ⏳ 每阶段后 features/health 复查 + 审计。

---

## 6. 重要提醒 (给老板的决策点)

1. **这些是"喂模型的输入质量"。** 即便全修活, 当前 fair_value 模型本身若退化, 也用不上。**修特征源 vs 训练真模型** 是两件事 — 建议 P2.1 (赔率链) 优先, 因为 sharp 赔率既是特征也能直接当 fair 锚, 双重收益。
2. **不要为修而修。** 条件性 0 (clutch/pos_*) 和 const (fee/devig_ok) 不该当 bug。真正该修的是第 1 节 6 组真断。
