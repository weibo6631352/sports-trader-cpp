# 特征基座 → 模型 → 业务链路 联合评审会

> owner: 老雷 (GM) ｜ last_review: 2026-05-31
> 参与: 量化信号(小程) / 量化微观结构(小袁) / ML-AI(小邓) / 量化金融(小梁) / 数值算法(小肖) / 回测(小蒋) → 架构综合(老郭)
> 对象: ml-feature-spec-v0.4 (75 列) + BaselineFairValueModel + 目标仓位控制器
> 纪律: 全员先 Read 真实代码, 每条结论带 `file:line` 证据 (老板令「大家都要看代码, 不能凭空说」)
> 全文产出: workflow `feature-model-strategy-review` (7 agent / 68 万 token / 53 工具调用)

---

## 0. 一句话结论

**基座地基扎实(契约锁死、控制器纯函数干净、ML 旁路红线真守住),但 6 团队一致诊断：系统当前「采集就位、信号未消费」—— 75 列里只有 2 个参数 (score_diff + time_frac) 真正驱动决策,大量真 alpha 锁在契约里没进 fair。** 立即做 Phase 0 五项纯接线(零新数据/零红线/零控制器改动);ML 走残差 LightGBM→ONNX(非 from-scratch)。

---

## 1. 三处仲裁 (团队冲突, 架构定夺)

**仲裁 A — 驳回小肖 P0「平局时钟失效是 bug」。** 小肖主张把 `fair_value_estimator.hpp:327` 的 `alpha·diff + beta·diff·tf` 改成独立加性 `alpha·diff + beta·tf`。老郭 Read line:307-311 原文注释「time 项耦合(带符号)score 领先, 故 0:0 恒 0.5」—— **这是作者显式设计意图**,小肖的"修正"反而让 0:0 平局 fair 随时钟单调偏离 0.5(真错)。**不改一行。** §8.1.1「引用必粘原文禁转述」正面案例:小肖转述语义没读 line:310。

**仲裁 B — 确认列 60-63 泄漏 (6 票共识), 走残差框架消解。** `x_log_odds_fair/edge/pin_risk/pin_x_expiry` 含 baseline fair 变换(`model_feature_spec.hpp:151` 自标)。裁决:**不**在 `extract_full` 加 mode 参数(污染契约纯抽取);改训练侧 feature-set 配置(SET-A 残差全列 / SET-B from-scratch 砍 16,60-63)。**写进 ADR-037:首个 ML 模型走残差框架(标签=outcome−baseline_p_fair),60-63 合法保留。**

**仲裁 C — 暂缓 power de-vig 升主 fair。** 小肖主张 `devig_binary_power`(`fair_value_estimator.hpp:266`)替 multiplicative。理论对(修 favorite-longshot),但 Newton 在 yes_mid→1 发散风险。**裁决:无数据时不切,列入回测 P1,仅低概率市场(p<0.2)验证 IC 提升后再切。**

---

## 2. 数据怎么用 (特征分层)

**核心 alpha 第一梯队(优先进 fair):**
| 列 | 特征 | 现状 |
|---|---|---|
| 18 | `g_bm_inplay_fair` (bet365 sharp inplay) | NaN 待白名单, `paper_loop.cpp:443` 已接线 |
| 65 | `g_time_x_lead` = score_diff×(1−tf) | 已算 `paper_loop.cpp:576`, 体育最大非线性 |
| 73 | `g_goal_freshness` = exp(−Δt/120s) | 已算 `:588`, 进球后 30-120s 延迟 edge 窗口 |
| 30/40 | `b_ofi`/`no_b_ofi` | **真数据已算**, `quote_snapshot_hub.hpp:161`「微结构最强信号」 |

> ⚠ **关键浪费**:第一梯队里只有列 18 是 NaN, 其余(65/73/30/40)**都已有真实数据但完全没进 fair 计算**(`paper_loop.cpp:559-573` 核实 — fair 只用 score_prior+devig+book_blend)。

**辅助层(质量权重非 alpha):** `devig_ok`(55)/`ts_window_samples`(56/57)/`cross_spread`(45=vig) → 进 sample_weight / reservation margin, 不进方向预测主路。
**该砍/降权:** 泄漏 16,60-63(残差框架外砍);冗余 `b_dislocation`(64)≡`x_microprice_minus_mid`(17);派生 `pos_net_qty`(52)=yes−no。
**做市 vs 方向两套 + 分 market_type 训练**(Moneyline/Totals 至少分两套, score_diff 在 Totals 语义不同;无时钟运动走独立集)。

---

## 3. 模型架构 + I/O (零改控制器落地)

**路线: 残差 LightGBM → ONNX (6 团队全票, 非 from-scratch NN)** —— 天然规避 60-63 泄漏 / 冷启动安全(残差→0 退回 baseline) / 数据少能跑 / `blend_prob`(`fair_value_estimator.hpp:333`)现成做 ensemble。

> 当前 `StubFairValueModel`(`fair_value_model.hpp:251-259`)是 `w=1/(i+1)` 调和加权占位, 零预测力, `make_onnx`(:332)返 nullptr。看板须按 `predict_ok/calibrated` 灰显。

**输入:** 75 列 flat 锁死(`model_feature_spec.hpp:538` static_assert),新增只 append+bump v0.5。**NaN 必须 train/serve 对齐**(LightGBM native-missing → ONNX 推理保留 NaN 不 impute;Stub 的 NaN→0 是 stub 专属, 真模型继承会 skew)。

**输出接控制器(最干净落地点, 控制器一行不改):**
```
ML.predict → p_fair_yes → SelectSide → p_fair_selected
  → ReservationInput.fair        (position_controller.hpp:68)  ← 替换 baseline
  → ModelPrediction.ci → margin_floor (:72)
  → ts_window_samples → n_eff         (:74, 替换硬编码 200)
```
ML 只产 (p_fair, confidence→margin, n_eff) 三个量驱动 target+reservation 链;ML 不直接吐 notional(守 ML-R1, 保 fair→Kelly→target 可解释链)。

---

## 4. 业务逻辑链路缺口 (按共识强度)

| 缺口 | 证据 | 共识 | 改动 |
|---|---|---|---|
| **n_eff 硬编码 200, CI 被架空** | `position_controller.hpp:74` + `paper_loop.cpp:687-695` | **5 票** | `n_eff = clamp(min(ts_window_samples, no_ts_window_samples), 10, 500)` |
| OFI/amihud/cross_spread 不进 reservation | `quote_snapshot_hub.hpp:159-161` 只进 ML advisory | 微观 P0 | margin_floor 动态化 |
| goal_freshness 不触发 force_cross | `paper_loop.cpp:757` 只看 `\|Δp_fair\|` | 微观 P1 | `force_cross \|= (goal_freshness>0.6 && \|b_ofi\|>thr)` |
| SelectSide 不看 vig 门槛 | `paper_loop.cpp:294` 纯代数选边 | 微观/小梁 | net-EV 预筛 `\|fair−devig\| ≥ 2·fee+slippage` |
| 双边持仓裸量入模型, 锁定对资本被隐藏 | `quote_snapshot_hub.hpp:116-122` | 小梁/微观 | 派生 locked_pair/directional(append v0.5, §8.1.5 carve-out 走普通 PR) |

> **n_eff 动态化风险(小肖):** samples=1 时 sigma=0.5, reservation 极宽永不成交(静默失效)。必须配 clamp 下限 `n_min=10`。

---

## 5. 分阶段 Roadmap

**Phase 0 (现在, 无新数据, 纯 C++ 接线) — 本 sprint:**
1. **n_eff 动态化**(5 票, 1 行 + clamp) — 最高 ROI
2. margin_floor 接 amihud/cross_spread(微观 P0)
3. net-EV 预筛(SelectSide 前加 vig 门, 防 fee 流血)
4. goal_freshness→force_cross OR 项(打通 inplay 延迟窗口, 数据已有)
5. **组合度量层**(小梁: Sharpe/maxDD/VaR 采集 — 北极星 KPI「连分母都没采」)

**Phase 1 (待 Goalserve 白名单 + livescore client) — P0 阻塞解除:**
- 接通 inplay 流(`has_real_fair=false` → 所有市场 target=0, `paper_loop.cpp:699-701`);列 18-23 才有非 NaN
- `g_bm_inplay_fair` 三元 blend 进 fair(bet365 sharp 锚)

**Phase 2 (待训练数据 + 标签管道):**
- **标签管道先行**(小邓 P0, 所有后续前提):`condition_id → resolved_outcome` join(`resolution_status` 已在 `model_feature_spec.hpp:150`)。没有 y 训不出东西。
- **book-only Tier-1 walk-forward**(小蒋 P0):Polymarket 真数据 ~30 列跑 baseline, train 30d/test 7d/步进 7d/≥6 fold。指标 IC/fill/CLV@30s,5m/PnL,turnover。
- 残差 LightGBM → ONNX → 实现 OnnxFairValueModel(`fair_value_model.hpp:332` 当前 nullptr)

**风险登记:** ① 60-63 from-scratch 泄漏(残差框架消解) ② NaN 主导(~70% 列, 先建档 NaN 率) ③ n_eff 静默失效(配 clamp) ④ 持仓列前视泄漏(walk-forward 仿真账本非回填) ⑤ 样本量(75 列需 ≥5000 独立样本/~50 场, Bonferroni α/75 + Deflated Sharpe) ⑥ 跨洋延迟吃 OFI alpha(秒级窗口 vs 200-500ms, 先测 OFI autocorr decay)。

---

## 6. 总裁决

**基座合格 → 立即做 Phase 0 五项纯接线(零新数据/零红线/零控制器改动/5 票共识);ML 走残差 LightGBM→ONNX(ADR-037 锁死, 60-63 合法保留);从 baseline 到 ML 是「替换 fair 一环」非另起炉灶。**
