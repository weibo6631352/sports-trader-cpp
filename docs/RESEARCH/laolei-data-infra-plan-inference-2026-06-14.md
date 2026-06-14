# 数据基建 + 统计分析计划 — 让"捡漏 / 该离场"可推断

owner: 老雷 (GM) | last_review: 2026-06-14 | 状态: ✅ A/B/C 全部已执行 + 部署 (老板「按顺序一次做完, 合理高效不留债」)

## 执行结果 (2026-06-14)
- **Phase A (C++, f716c9b0):** position_path 补 `sh_conv/sh_vel/sh_age_ms`; gate_blocks + fills 补 `sh_age_ms`。ctest 1249/1249。已部署 (git=32c3d9bd, paper 重启验证 schema 带新字段)。
- **Phase B (8dd6a691):** `exit_research.py` 四段 (B1画像/B2回撤vs退化判别/B3翻盘率分桶/B4阈值假设回放)。归档 n=7 验证: B1 已现"赢家 held_fair 0.72→0.85 守住 vs 输家 0.74→0.41 退化; 输家 MAE 0.62 vs 赢家 0.14"。
- **Phase C (32c3d9bd):** param_research ② 加"机会错过前沿"(每档门槛显示错过赢家数)。
- **老板中途加 (已并入 A):** 「这么依赖 sharp, 新鲜度也很关键」→ `sh_age_ms`(最近 sharp 样本龄) 跟 sharp 走到哪带到哪 (position_path/gate_blocks/fills 三处); exit_research B1/B2 把它当一等信号 + 陈旧样本单列/可滤 (`--stale-ms`)。
- **待:** 数据累积 (含回撤样本的已结算仓 ~几十个) 后跑 exit_research 看真结论。

## 0. 定位 (老板 2026-06-14 原话校正)

> "我的主要目的是让你做**基建数据支撑**,我们通过统计分析,可以推断出来捡漏或者该离场就行了。然后我们根据这些信息可以制定赢利策略。"

- **我的活 = ① 基建(采全"推断捡漏/离场"所需的数据) + ② 统计分析(把信息摆出来 + 算判别力)。**
- **我不做:** 推断结论 / 推荐阈值 / 设离场规则 / 定策略 —— 那是我们**看了数据后一起定**。
- **原则(老板「我另有想法」纠偏):** 信号**铺广,别预设答案**。`sh_conv` 只是候选之一,让数据在判别力排序里自己说话,我不先认定离场该锚谁。

---

## 1. 现有基建盘点 (已具备,别重造)

### 数据采集 (data/ml_capture/)
| 文件 | 内容 | 关键字段 |
|---|---|---|
| `fills_journal.jsonl` | 进场/离场成交 (全因子) | yes/px/qty/fair/mark/fee/exit/sport/mkt + bk_*(spread/imb/micro/age/sizes) + ofi/rvol/mom5 + sh_fair/sh_vel/**sh_conv/sh_vol**(新) + edge_ci/devig/kelly/m_* + d5_* + odds_age/g_remain/g_sdiff/g_period + cash/n_open/equity + hold_sec/mae/mfe(结算回填) + close_mid/final_* |
| `gate_blocks.jsonl` | 被挡决策点 (新:全因子向量) | gate/yes/fair/px/would + ofi/rvol/mom5/sh_*/vol24h/liq/deploy/cash/n_open/equity +(ExecuteControllerSide门)bk_*/d5_*/edge_ci/devig/g_* |
| `position_path.jsonl` | 持仓全程轨迹 (30s/点) | et(进场ts)/yes/qty/avg/bvalid/bid/ask/mid/micro/spread/imb/b1sz/a1sz/bd5/ad5/d5imb/bk_age_ms/**sharp**/eng |
| `*.settlements.jsonl` | 结局 | condition_id/settlement_value/parse_ok |
| fills version 行 | 样本分段 | git + config 参数 (不混版本) |

### 统计分析 (experiments/laolei-paper-replay-stats/)
- `replay_stats.py` — 进场质量复盘 (胜率/expectancy/CLV/校准/分段/PnL不对称/判别力/机会错过/费/出场分布/累积ETA)
- `param_research.py` — 参数研究 (①赢家vs输家画像+判别力AUC标候选新门 ②阈值扫描×赢家捕获 ③2维组合挖矿 ④单赢家深挖)。决策全集=进场+被挡。
- `monitor.py` — 实时盯盘快照

---

## 2. Gap (针对"捡漏推断 / 离场推断")

### 进场侧 (捡漏) — 基本齐
- 决策全集(进场+被挡)带全因子已通 → "什么样的便宜盘是真赢家"可挖。
- **差 C1:** 机会错过的"**前沿曲线**"未明确化 (保守收门↔错过赢家→总利润降, 这条权衡没画出来)。

### 离场侧 (该离场) — 主要 gap
- **基建 G1 (关键):** `position_path` 轨迹只有**瞬时 `sharp`**,缺**信念信号时序 `sh_conv`(收敛率)/`sh_vel`(速度)** → 持仓中"变对(收敛)/变错(发散)"的**时序看不到**。`sharp_history_` 那对象轨迹里已经查到了,加两行即得。
- **基建 G2:** 离场没有"**决策快照**"(我们离场=settlement/game_decided, 没像 gate_blocks 那样在离场点记因子) → 回撤/退化时序只能从 `position_path` 轨迹**重建**(采样够,30s/点)。先靠重建,够用再说。
- **分析 G3:** 没有**群体**"持仓轨迹画像"分析(赢家vs输家持有期对比 / 回撤vs退化判别 / 翻盘率分桶)。param_research ④只是**单赢家**深挖,不是统计。

---

## 3. 计划 (分阶段; 全是基建+分析, 不碰策略/不推荐阈值)

### Phase A — 离场基建补强 (C++, 一次重启)
- **A1 (做):** `position_path` 轨迹补 `sh_conv` + `sh_vel` (sharp_history_ 已查到, +2 行) → 持仓中信念信号时序齐。这是离场推断的命根子数据。
- **A2 (评估):** `gap = sharp − mid` 分析侧能从现有 sharp+mid 直接算,不必落盘; 评估 odds_age/g_remain 等持仓中会变的量要不要补进轨迹。
- 边界: 加性异步日志 (journal_writer_), R-12 安全; **不改任何离场逻辑**。

### Phase B — 离场分析模块 (Python; exit_research.py)
- **B1:** 赢家 vs 输家 **持仓轨迹画像叠图** — 群体对比 sh_conv/sh_vel/gap/簿失衡/回撤深度(MAE)/回撤时长 的演化 → 看输家怎么退化、赢家怎么抖动守住/翻盘。
- **B2:** **回撤 vs 退化 判别力** — 赢家回撤期各信号 vs 输家退化期各信号, 按判别力排序 → **让数据指出哪个信号能分"该离/不该离"**(不预设 sh_conv)。
- **B3:** **翻盘率分桶** — 回撤过的仓, 按回撤时各候选信号分桶, 各桶最终翻盘率 → 信息性输出。
- **B4 (信息性, 非推荐):** 离场规则"**假设回放**"视图 — 给定一条假设规则, 回放算 省亏损/卖飞利润; 供**我们**试不同假设, 不是我推荐某条。

### Phase C — 进场前沿明确化 (Python, 并入 param_research ②)
- **C1:** 机会错过"**前沿曲线**" — 每收紧一档门槛: 捕获赢家数 / 错过赢家数(被挡本会赢) / 总利润 → 保守↔错过权衡曲线, 让老板看"松一点值不值"。

---

## 4. 数据成熟度

- 2026-06-14 00:47 重启后从 0 累积; 被挡盘(进场10-20×)是大数据主力。
- 离场分析需 **持仓轨迹够多 + 含回撤样本的已结算仓 (~几十个)** → 估数天 (具体看比赛密度)。
- **不重置 / 不改参** 才积得出可信样本 (改参=换版本, 用 fills version 行分段)。

## 5. 边界 (我明确不做)

- 不推断"该捡哪个漏 / 该何时离" — 看数据**我们**定。
- 不推荐 / 不写死任何**阈值或离场规则**。
- 不碰**离场执行逻辑** (除非老板定了策略再单独派)。
- 不预设离场锚信号 — 候选铺广, 判别力数据说话。
