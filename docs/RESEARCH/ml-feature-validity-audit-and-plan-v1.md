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

---

## 7. 执行结果 (2026-06-02 部署验证, commit ef1dc67e)

**已修 (代码缺口快赢):**
- **P1.1 g_period** — `pricing::parse_period_ordinal` 运动感知字符串→节序数, inplay→game_row 桥接通。
- **P3.1 book 派生** — 喂 ML 的 book_row 改透传 feat 全 5 档 + 算 spread_bps_f + top3_depth_usdc。
- **P3.2 无时钟 phase** — `regulation_periods` + period 进度算 phase_frac (不动 time_frac/定价)。

**验证 (重启后 features/health, n=127):**

| 指标 | 修前 | 修后 |
|---|---|---|
| healthy | 76 | **86** (+10) |
| dead | 31 | **26** |
| const | 7 | **2** |

翻转特征: #2 g_period (dead→healthy, [0,8]) · #11 b_spread_bps (→[50,19600]) · #12 b_top3_depth (→[10,1.46M]) · #15 b_book_levels_valid (const2→[2,10]) · #67 g_remaining_sec · #70 g_game_phase ([0,2])。
未翻转但属预期: #71 garbage / #72 clutch (条件未触发, 需末段+比分差; 会在末段比赛点亮)。

**剩余 26 dead 全为非代码快赢类 (已分类, 不当 bug):**
- 大管线 (genuine, 待建 odds feed): bm_slots 跨庄家 #5/6/7/16。
- 覆盖依赖 (需数据流): soccer stats #19-23/#111 (无足球直播或 commentaries 未 join) · NO book #78/#105 · 多节比分 #68/69。
- 假阳性 (正确的 0/常量): #26 bid_absence (bid 在场=健康) · #48-53 pos (空仓) · #59 resolution (市场开放) · #82/84 cat (体育 moneyline 恒值) · #71/72 (条件未触发)。

**下一步 (老板决策):** 唯一剩的真代码缺口是 bm_slots 跨庄家赔率, 需新建 Goalserve odds feed 采集管线 (中大工程, 双重价值: 既是特征也是 sharp fair 锚)。其余非代码, 靠数据覆盖 (足球直播 + commentaries + NO book 双边订阅) 自然填充。

---

## 8. 10-代理 fan-out 审计新发现 (2026-06-02, workflow ml-feature-validity-sweep)

> 10 代理分片 (每片 ~12 特征) + 1 综合, 对抗式核验值语义。**在原单遍审计三类 (假阳性/覆盖/大管线) 之外, 找出 5 类原审计漏掉的真问题** — 印证 fan-out 价值。

**真 bug (genuine, 按严重度):**
1. **#66 g_fld_signal (HIGH) — 设计失效, 非数据缺口。** 二元市场 multiplicative de-vig 与 power de-vig 数学收敛, 差值退化为浮点噪声 (~1e-16; 实测 min=-1.6e-16/max=1.1e-16)。populated=127 假装健康, 实为机器精度垃圾, 模型会对噪声维度过拟合。**修: 换真有区分力的 FLS 代理 (sharp 偏离比) 或删列。属量化 (小梁/小程) 决策。**
2. **#71 g_garbage_time / #72 g_clutch (MEDIUM) — 默认值 bug。已修 (commit 见下)。** 默认 0.0 而非 NaN (paper_loop.hpp:180-181), 非赛中 85 记录静默输出 0.0, 模型无法区分"无比赛数据"vs"赛中非关键时刻"。与同结构 game_phase/goal_freshness (默认 NaN) 不一致。**已改默认为 NaN。**
3. **#78 x_yes_no_book_skew_sec (MEDIUM) — 双 bug。** ① 注释符号反 (model_feature_spec.hpp:696 "正=YES更旧" 与公式 b.ds−no.ds 语义相反); ② 当前 WSS YES/NO 共享 @ts → 差值恒 0。**修: 正注释 + 评估 YES/NO book ts 能否独立 (否则改用 ingestion_ts)。**
4. **#77 g_score_age_sec (LOW) — 语义污染。** paper_loop:458 对所有记录写 game_row.data_source_ts=YES book WSS ts; 非匹配 85 记录测的是 book 龄非比分龄。**修: 仅匹配成功才写, 否则留 0 → age_s(0)=NaN。**
5. **#24/#32 b_mp_roc_per_sec vs b_mp_roc_30s (LOW) — 重复列。** cfg_.ts_feature_window_ns 默认 30s == b_mp_roc_30s 硬编码窗口 → 两列逐位相同 (完美共线)。NO 侧同。**修: cfg 默认改 60s/120s 或删一列填空白窗口。**

**共线/零增量列 (浪费模型容量, CTF 架构必然):**
- #75 b_book_age_sec == #76 no_b_book_age_sec (YES/NO 共享 WSS @ts → 完美副本)。
- #86 b_bid_depth_5lvl == #91 no_b_ask_depth_5lvl; #87 b_ask_depth_5lvl == #90 no_b_bid_depth_5lvl (CTF: YES bid=NO ask 同底层池 → 2 对完美共线)。
- #79/#80 ingestion_lag 同源近重复。

**suspect-healthy (看似健康待人复核):** #1 g_score_total (max=41 篮球混足球, 无运动感知) · #3 g_elapsed_sec (42 条仅 1 非零, clock_sec 语义存疑/按节重置?) · #10 b_imbalance (缺失 fallback 0.0 非 NaN) · #21 g_possession_home (0-100 非 0-1, 开通后需归一) · #30 b_ofi (净卖压均值 −4183 但 mp_roc 为正, 方向需小梁验) · #44 b_no_microprice (实为 no_token_mid, thin book 静默降级) · #94 mkt_volume_24h (24% NaN, 而同端点 #95 liquidity 全有值, 提取逻辑存疑)。

**结论:** fan-out 找出 #66 (噪声列) 和 #71/72 (默认值不一致) 是原单遍漏掉的真问题; 其余多为 CTF 架构共线 (信息论冗余, 非 bug) + 待人复核的语义疑点。#66 与共线列在下次 retrain 前由量化决定删/换。
