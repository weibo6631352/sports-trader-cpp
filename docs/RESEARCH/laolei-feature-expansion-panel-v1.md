# 特征扩展 — 全员评审汇总 + 落地方案 v1

> owner: 老雷 (GM) · last_review: 2026-05-31 · 性质: 6 主权评审收敛 → 落地批次
> 触发: 老板「卖不出/结算归零名字太抽象,上网查行业标准 + 全员讨论 + 根据已有数据要更多特征信号」
> 评审人: 小袁(微结构) · 小程(信号/α) · 体育市场 · 小肖(数值算法) · 老郭(架构裁定) · 小蒋(回测/防过拟合)
> 网调参照: Polymarket 微结构论文(arxiv 2604.24366) · OddsJam/Unabated/BetStamp 计算器 · de-vig 方法学 · SharpAPI

---

## 1. 命名 taxonomy 裁定 (老郭拍板,消小袁 vs 小程分歧)

**前缀只编码一个轴 = 数据血缘 (provenance)。否决小程 `l_/m_/r_` 信号族前缀** (15-30 特征规模下 Amihud/OFI/pin_risk 会前缀撞车,每个新特征先吵归类 = §8.1 要消的语义漂移)。

| 前缀 | 含义 (血缘) | 例 |
|---|---|---|
| `g_` | Goalserve game 派生 (比分/状态/时钟/Poisson/sports 动态) | `g_score_diff` `g_poisson_p_yes` |
| `b_` | Polymarket book 派生 (截面 + 时序微结构,统一归此) | `b_microprice` `b_ofi` `b_amihud` |
| `x_` | game×book join 派生 (跨源偏离) | `x_devig_minus_mid` `x_clv` |

**时域 + 信号族走后缀 + 文档分组,不进标识符**: 时序带动作后缀 `_roc`/`_vol`/`_frac`/`_mean`/`_ewma`;截面无后缀。信号族 (momentum/liquidity/resolution) 只做文档目录 tag。

### 改名表 (Batch 0)

| 现名 (口语) | 最终名 | 说明 |
|---|---|---|
| `bid_absence_frac` | `b_bid_absence_frac` | absence 比 illiquidity 准 (机械占比非判断);`_frac` 已表时域 |
| `exit_depth_mean` | `b_bid_depth_mean` | = quoted bid depth 窗口均值 |
| `mp_roc_per_sec` | `b_mp_roc_per_sec` | `_roc` 含变化率义;单位保留 |
| `realized_vol` | `b_realized_vol` | 已标准,加血缘前缀 |
| `time_to_resolution_frac` | `g_time_to_expiry_frac` | **归 g_ 非 r_** (体育从比赛时钟来 = Goalserve 血缘) |
| `resolution_status` | `g_market_state` | 归 g_;**且避雷** WSS 链路同名异义 `resolution_status` 枚举 |
| `ts_window_samples` | `b_window_samples` | 观测质量代理 |

---

## 2. 两层演进 + 晋升 gate (老郭)

| 层 | 文件 | 列序锁 | 改动代价 | 进出 |
|---|---|---|---|---|
| **捕获层** QuoteFeatures | quote_snapshot_hub.hpp | 否 (加性自由) | 低 | 任何候选末尾 append |
| **契约层** MlFeature enum | model_feature_spec.hpp | 是 (append-only + bump kSpecVersion + retrain) | 高 | 仅验证有信息量才晋升 |

**晋升 gate (候选→契约,四条全过才 bump spec):** ① 已捕获足量训练样本;② 小梁/小邓出特征重要性证据 (数字说话);③ enum 末尾 append;④ retrain + 列序锁 static_assert 更新。

**两条防「约束咬人」反模式 (写进文档):**
- 加候选捕获特征**不需要** bump kSpecVersion (kSpecVersion 只锁 MlFeature enum)。
- 本次改名**不是 R-4** (非单位/非语义/非静默/未进列序锁 → 走「受控重命名 SOP」,非 R-4 全审计)。

---

## 3. 特征目录 (全员汇总,按血缘 + 优先级)

### 立即可做 (P0,现有数据/低成本,无需扩 ring)

| 特征 | 名 | 公式 | 来源 |
|---|---|---|---|
| logit fair | `x_log_odds_fair` / `x_log_odds_edge` | `logit(fair)`;`logit(fair)−logit(mid)` | 小肖 (ML 线性度,**第一个落**) |
| 时间×领先交互 | `g_time_x_lead` | `score_diff × (1−elapsed_frac)` | 体育 (**最大未捕捉非线性**) |
| 归一化进度 | `g_elapsed_frac` / `g_remaining_sec` | `(period−1)·period_sec+elapsed)/total_sec` | 体育 (所有时间特征的分母) |
| pin risk | `x_pin_risk` / `x_pin_x_expiry` | `min(fair,1−fair)`;`×g_time_to_expiry_frac` | 小肖 (结算风险) |
| 多尺度动量/波动 | `b_mp_roc_30s/5m/15m` `b_vol_ratio` | 调现有 `RateOfChangePerSec/RealizedVol` 多窗口 | 小肖 |
| 各节胜负 | `g_periods_won_home/away` | 从 `score_*_periods[]` 统计 (已有数据没用!) | 体育 |

### 需小扩 ring (Sample 加 best_ask/best_ask_size)

| 特征 | 名 | 公式 | 来源 |
|---|---|---|---|
| 订单流失衡 | `b_ofi` | `Δbid_size − Δask_size` (Cont-Kukanov-Stoikov) | 小袁/小肖 (微结构最强信号) |
| Amihud 近似 | `b_amihud` | `mean(|Δmicroprice|/bid_size)` (现有 ring 即可) | 小袁/小肖 |
| 退出深度波动 | `b_bid_depth_vol` | `RMS(Δbid_size)` (现有 ring 即可) | 小袁 |
| 微价错位 | `b_dislocation` | `microprice − mid` | 小肖 |

### de-vig / CLV (cross/game,不碰 ring)

| 特征 | 名 | 说明 | 来源 |
|---|---|---|---|
| favorite-longshot 偏差 | `g_fld_signal` | `p_mult − p_power` (de-vig 方法分歧当特征,**非替换主路径**) | 小肖 |
| CLV | `x_clv` | **仅离线 label,绝不进特征** (前视红线,见 §5) | 小肖/小蒋 |

### sports in-play 动态 (体育组,部分需 Goalserve feed 字段确认)

- **立即**: `g_goal_freshness`(进球后衰减,edge 最浓 30-120s)、`g_net_momentum_5min`、`g_lead_expanding`、garbage-time/clutch flag。
- **Poisson 胜率** (`g_poisson_p_yes`,足球专项): 剩余进球 Poisson → in-play 胜率,取代/补充现 sigmoid score-prior。先**当特征差值** `g_poisson_minus_sigmoid` 验证再考虑替换。
- **待数据组确认 Goalserve feed 覆盖** (高 α,需小余/小段查 feed): 红牌、射门/危险进攻(xG 代理)、MLB outs/pitcher、网球发球方、NFL down/possession、伤停补时编码。

---

## 4. de-vig 裁定 (小肖,数值主权拍板)

**维持 multiplicative 作主 fair 输出** (9 家均值已压缩方法误差 <50bps;二元极端冷门少;改方法要全量 retrain;power 迭代加 ~200ns 不值)。**补 power 差值当特征** `g_fld_signal` (让模型学 favorite-longshot 偏差,不改主路径)。二元市场 Shin ≡ additive (有负概率风险),排除。

---

## 5. 验证 SOP + 红线 (小蒋,防过拟合主权)

**CLV 前视红线 (P0):** CLV 需「未来参考价」→ **只能当离线评估 label,绝对禁止进特征/实时推理/QuoteFeatures**。CLV 参考价 = 结算前 5min fair (体育 T-5min ≈ 结算值 >95%);或固定 horizon fair (微结构特征用 60s)。

**防过拟合门槛:**
- 上线前逻辑三问淘汰 (经济机制? PIT-safe? 与现有特征冗余?) → 30 候选砍到 ≤15。
- 多重比较 Bonferroni: N=30 → p<0.0017;数据少时 IC 门槛高 (T=500,N=30 → IC≥0.07)。
- IC 门槛 IC_mean>0.02 + IR>0.3 + 连续 3 fold 正;walk-forward 6+ fold,禁单次 IS/OOS。
- 树深 ≤4 叶子 ≤16 (早期强正则)。

**CLV > realized PnL 作早期评估信号** (方差小 1/5~1/10,反馈快)。**de-vig 方法对比用 Brier score 对 3b 结算真值** (50 结算事件即可初判;`g_bm_overround_avg` 分位数条件评估)。

**数据时间线:** Month1 (50 结算) → de-vig Brier + 简单特征 IC;Month2 (200) → OFI/Amihud IC;Month3 (500) → Bonferroni 选择 + walk-forward;Month4+ (1000) → Deflated Sharpe。

**BR-1 防泄漏:** 捕获用 `as_of_ts_ns` 非 now();NaN 不回填;ring 窗口 [as_of−W,as_of] 无前视;新 Sample 字段 live/replay 同源填充 (一条路径填一条留默认 = 红线#3)。

---

## 6. 落地批次 (老郭,「怎么弄」) — 绝不一次加 15-30

| 批 | 内容 | 列序锁 | 依赖 |
|---|---|---|---|
| **批 0** | 改名 + taxonomy 立规 (0 新特征) | 否 | **先行,现在做,成本全局最低点** |
| **批 1** | Sample 扩 best_ask/ask_size + book 微结构 (`b_ofi`/`b_amihud`/`b_dislocation`/`x_log_odds`/`b_mp_roc` 多尺度) | 否 (候选区) | 批 0 |
| **批 2** | de-vig 变体 + CLV (`g_fld_signal`/`x_clv` 离线) | 否 (候选区) | 批 0 |
| **批 3** | sports in-play 动态 (Poisson/比分动态/`g_time_x_lead`/pin_risk) | 否 (候选区) | 批 0 + feed 字段确认 |
| **批 4** | 滚动晋升: 候选→MlFeature enum,小步 (一次 2-4 个) + bump spec + retrain | **是 (唯一动列序锁)** | 批1-3 捕获足量 + 验证 |

批 1-3 全进**候选捕获区** (不进 enum,可放开加);**只有批 4 走晋升 gate 动列序锁,永远小步**。捕获放开、晋升克制。

---

## 7. 立即行动 (本轮 = 批 0)

1. 改名 7 字段 (§1 表),避雷 WSS `resolution_status`,一次性原子改 4 文件 (quote_snapshot_hub.hpp 定义 + paper_loop.cpp/.hpp 填充 + test_paper_loop.cpp 断言) + feature_history 方法名同族改。
2. QuoteFeatures 加注释分「契约镜像区 / 候选捕获区」+ 写明候选区不受 kSpecVersion 管辖。
3. feature_history.hpp 头注释补 ring 扩展纪律 (为批 1 铺路)。
4. 本文档入 ADR (小米归档)。

后续批 1-4 按 §6 推进,每批一 commit + 一轮 test 闭环。
