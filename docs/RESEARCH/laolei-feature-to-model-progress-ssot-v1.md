# 特征基座 → 模型 → 业务链路 进度 SSOT v1

> owner: 老雷 (GM) + 小米 (doc-curator)
> last_review: 2026-05-31
> 关联文档: [评审纪要](../MEETINGS/2026-05-31-feature-model-strategy-review.md) | [Phase 2 标签管道](laolei-phase2-label-pipeline-v1.md)
> 纪律: 每个论断带 file:line 或 commit hash 证据 (老板令「看代码，不能凭空说」)

---

## 0. 一句话结论

**基座代码全部就位，信号未消费。** 75 列特征契约锁死、ML 推理旁路接通、双边对称采集完成、Phase 0 五项接线已落、Phase 2 标签+完整向量就绪。当前决策仍由 baseline（score_diff + time_frac）驱动；大量真 alpha 锁在契约里等待 Goalserve 白名单开通 + 离线训练替换 stub。

---

## A. 已完成 (按 commit 时序, 带证据)

### A1. MlFeature 契约 v0.4 — 75 列锁死

| 版本 | commit | 新增列 | 范围 |
|---|---|---|---|
| v0.1 | (初始骨架) | 0-17 | game 侧/book 侧/cross |
| v0.2 | b97edac | 18-23 | inplay bet365 de-vig + 5 live_stats 差 |
| v0.3 | 44999f8 | 24-53 | 双边时序微结构 (YES 24-33, NO 34-43) + 双边 L1 (44-47) + 双边持仓 (48-53) |
| v0.4 | 4b1d939 | 54-74 | 手续费/数据质量/生命周期/cross-log-odds/sports 动态 (老板「都要进」) |

**当前状态 (核实)**

- `include/stcpp/ml/model_feature_spec.hpp:53`: `kSpecVersion = "ml-feature-spec-v0.4"`
- `include/stcpp/ml/model_feature_spec.hpp:168`: `kMlFeatureCount = 75`
- 四把编译期 static_assert 锁 (`:538-547`): 末列 g_net_momentum_5m=74, 旧末列 x_microprice_minus_mid=17 / g_corner_diff=23 / pos_condition_exposure=53 不变

**列分组结构**

| 索引 | 分组 | 抽取函数 |
|---|---|---|
| 0-7 | game 侧 (Goalserve 归一化) | `extract_from_game_row` |
| 8-15 | book 侧 (Polymarket 归一化) | `extract_from_book_row` |
| 16-17 | cross (de-vig vs mid, microprice 压力) | `fill_cross_features` |
| 18-23 | inplay 赔率 + live_stats 差 (v0.2) | `extract_from_game_row` |
| 24-53 | 双边时序微结构 + 持仓 (v0.3) | `extract_from_quote` |
| 54-74 | 手续费/质量/生命周期/cross-log-odds/sports 动态 (v0.4) | `extract_from_quote` |

排除项 (有据非信号): 决策输出 (kelly/target/reservation)、模型自身输出 (ml_advisory)、provenance 元数据、原始时间戳 ×6、字符串主键、重复列。详见 commit 4b1d939 分类注。

---

### A2. ML 推理管线接线 (步④) — commit 3f0b58e

链路:
```
TickOne (game_row + book_row 就位)
  → PublishQuoteSnapshot 末尾 (v0.3: qf 全特征就位后)
  → ml::extract_full(game_row, book_row, qf)  ← include/stcpp/ml/model_feature_spec.hpp:522
  → ml_model_->predict(fv)                     ← include/stcpp/ml/fair_value_model.hpp:189
  → ModelPrediction → QuoteFeatures.ml_advisory_p_yes + provenance
```

**ML-R1/R2 旁路守法 (核实, src/stcpp/paper/paper_loop.cpp:754-770)**

- 推理结果只写 `ml_advisory_p_yes` + provenance，绝不改 `fair_value/edge/sizing/intent`
- 决策仍由 baseline `fv_result` 驱动 (`paper_loop.cpp:532`)
- advisory gate (P0-4): `cfg_.advisory_markets_no_intent=true` (默认) → 不产 OrderIntent (`paper_loop.cpp:768`)

**当前 baseline 驱动逻辑**

- `fair_value_model.hpp:332`: `make_onnx_fair_value_model` 当前返回 `nullptr`
- daemon 回落 `StubFairValueModel(kMlFeatureCount=75)` 占位 (`paper_daemon.cpp`, commit 3f0b58e)
- Stub 是调和加权 `w=1/(i+1)` 确定性映射，零预测力，仅打通接口契约 (`fair_value_model.hpp:251-259`)

---

### A3. 数据采集链路 — inplay odds + live_stats + settlement

| 链路 | commit | 状态 |
|---|---|---|
| inplay bet365 odds 解析器 (`ParseInplayOddsDevig`) | f4d4fcd | 代码就位，按真实 Goalserve 结构 (name 匹配 Home/Draw/Away，suspend 感知) |
| inplay odds 全链路 (`InplayScoreParser` → `EventScore` → `paper_loop`) | 707e6c3 | 端到端就位，1216 测试全绿 |
| inplay 解析器按真实结构加固 (market key 锚定，participant name 匹配，旧合成兼容) | ad9d2a9 | 加固完成 |
| YES-canonical orientation 修复 (`ToYesCanonical` 纯函数，三边透传) | 4b17294 | 修复完成 |
| live_stats 采集 hop (`CommentariesParser/Poller/Store`，30s，队名 join) | c1359be | 代码就位，1224 测试全绿 |
| settlement 全链路 (`SettlementStore` + `SettlementPoller`) | aa269de | 已落 |

**当前阻塞 (诚实)**: 上述采集链路代码全部就位，但 Goalserve inplay 白名单未开 (403) + 无 odds plan → 列 18-23 在真实场景中为 NaN。白名单一开，零改码即流入。

---

### A4. 双边对称 — 赔率三边 + 微结构 YES/NO + 持仓各边

| 对称维度 | 实现 | 核实 |
|---|---|---|
| inplay 赔率三边 (home/away/draw) + YES-canonical 翻转 | 4b17294 | `inplay_odds_parser.hpp:ToYesCanonical` |
| NO 边时序微结构 (10 个 no_* 字段，`ts_history_no_` 环) | 5c18450 | `paper_loop.hpp:ts_history_no_`, `quote_snapshot_hub.hpp:no_*` |
| 双边持仓 (pos_yes_qty/pos_no_qty 各边各量，fix break-on-first bug) | 09dfcfd | `paper_loop.cpp` PositionLedger per-token 读; `quote_snapshot_hub.hpp:116-122` |

原则来源 (老板): 「双边都要有，不能模糊；可能两边都买」— 双边 OFI/amihud 是独立信号，非 YES 镜像。

---

### A5. Phase 0 五项接线 — commit 5fe919b

联合评审 (2026-05-31, 6 团队) 后立即落地。零新数据、零控制器结构改动。

| 项 | 内容 | lib 默认 | daemon 生产 | 核实位置 |
|---|---|---|---|---|
| 1 | 动态 n_eff = clamp(min(YES,NO 样本), n_min=10, n_max=500) | false | true (`dynamic_reservation`) | `paper_loop.cpp:641-649` |
| 2 | margin_floor 接半 vig (0.5×cross_spread) + amihud_coef (默认 0=off) | false | true | `paper_loop.cpp:651-658` |
| 3 | net-EV 预筛: edge < 2×fee_per_unit + slippage → target=0 | false | true (`net_ev_gate`) | `paper_loop.cpp:741` |
| 4 | goal_freshness force_cross (>0.6) + OFI 确认 → 绕死区 | always-on | — | `paper_loop.cpp:806-808` |
| 5 | 组合度量层 `PortfolioMetrics` (Sharpe/maxDD/VaR, 北极星 KPI 采集) | — | TickAll 每周期 | `paper_loop.cpp:275`; `eval/portfolio_metrics.hpp` |

**注**: 项 1-3 行为变更门 lib 默认 false (1243 契约测试静态行为不变)；daemon `enable_phase0_gates` 默认 true 置 dynamic_reservation + net_ev_gate = true。

---

### A6. Phase 2 — 标签管道 + 完整 75 列 fv-hub + recorder

| 子项 | commit | 产物 | 核实 |
|---|---|---|---|
| 标签管道 (`label_pipeline.hpp`, 纯函数核 + 文件流式 JoinFile) | e802165 | `training.jsonl` (X,y join) | `include/stcpp/ml/label_pipeline.hpp:46-59`; 1249 测试全绿 |
| X 信号列补全 (`FeatureRecorder.WriteLine` 扩到 QuoteFeatures 全信号列 18-74) | e802165 | 含 b_ofi/g_time_x_lead 等第一梯队 alpha | `include/stcpp/ml/feature_recorder.hpp` |
| 完整 75 列 fv-hub + recorder 线程 (0-17 原始 game/book 列补全) | c457dad | `*.fv.jsonl` (f0-f74) | `include/stcpp/ml/feature_vector_hub.hpp`; `feature_vector_recorder.hpp`; 1253 测试全绿 |

**完整训练 X 链路 (核实)**:
```
PublishQuoteSnapshot → ml::extract_full(game_row, book_row, qf)  # 总是算，record+predict 共用
  → fv_hub_->Publish (loop_thread_ 短锁 POD copy ~320B <1us)
  → FeatureVectorRecorder 独立线程 SnapshotAll
  → <ml_path>.fv.jsonl  {"condition_id":..,"as_of_ts_ns":..,"spec_version":..,"f0":..,"f74":..}
```

两份训练源并存: `quotes.jsonl` (QuoteFeatures 人读字段名) + `*.fv.jsonl` (完整 75 列 f0-f74，列序=enum，建模用此)。训练=推理同源 (BR-1)，零漂移。

---

## B. 当前阻塞 / 待办 (诚实)

| 阻塞 | 性质 | 影响 | 解除条件 |
|---|---|---|---|
| Goalserve inplay 白名单 (403) | 业务动作 | 列 18-23 (inplay fair + live_stats 差) 恒 NaN；`has_real_fair=false` → 所有 `paper_loop` 市场 target=0 不开仓 (`paper_loop.cpp:473,743`) | 白名单开通 |
| Goalserve odds plan (五大运动 NO_ODDS) | 业务采购 | `g_bm_inplay_fair` (列 18) 无数据 | odds plan 升级 |
| 赛季数据缺口 | 数据现实 | 大量特征 NaN (无历史赛季样本); 评审纪要 §5 ② | paper daemon 持续跑出 `*.fv.jsonl` + 结算积累 |
| 标签/训练待真数据 | 离线 | `JoinFile` 有管道，但需真实场次结算 | white list + 跑完场次结算 |
| LightGBM → ONNX 离线训练 | 量化/ML 离线工作 | `make_onnx_fair_value_model` 当前返 nullptr，stub 驱动决策 (`fair_value_model.hpp:332`) | 小邓 (ML) + 小蒋 (walk-forward) |
| X 残余列 0-17 在 `FeatureRecorder` (quotes.jsonl) 缺失 | 已知技术债 | quotes.jsonl 不含 score_diff/b_mid 等原始列，但 fv.jsonl 已完整 | 已由 fv-hub 解决 (commit c457dad)；quotes.jsonl 仅人读 |

---

## C. 关键决策记录

### C1. 残差框架 (仲裁 B — 6 票共识)

**决策**: 首个 ML 模型走「残差 LightGBM → ONNX」，非 from-scratch NN。
- 标签 y_residual = outcome − baseline_p_fair；模型学 baseline 的系统误差
- 列 60-63 (`x_log_odds_fair/edge/pin_risk/pin_x_expiry`) 含 baseline fair 变换，残差框架下**合法保留** (`model_feature_spec.hpp:151` 自标注)
- from-scratch 框架须砍 16,60-63；特征选择在 train 侧配置 (SET-A/SET-B)，C++ 契约不动
- 根据 (评审纪要 §1 仲裁 B): 不在 `extract_full` 加 mode 参数 (污染契约纯抽取)

### C2. 模型路线: LightGBM → ONNX (非 from-scratch NN)

- 6 团队全票理由: 天然规避 60-63 泄漏 / 冷启动安全 (残差→0 退回 baseline) / 数据少能跑 / `blend_prob` (`fair_value_estimator.hpp:333`) 现成做 ensemble
- ADR-037 锁死，推理走 ONNX C++ session，Python 仅离线训练 (CLAUDE.md §12.4)

### C3. 门禁移除 (commit 94436c5 — 2026-05-31 老板明确决定)

**决策**: 移除 pre-push hook + PR gating CI (scripts/pre-push.sh, .github/workflows/pr.yml + pr-linux.yml)。
- 老板在充分知情下三次告知风险后点名担下豁免，选「全删含私钥防线」
- **含移除 secret_blacklist (私钥泄漏防线)**，CLAUDE.md §8 红线 (私钥明文落盘 → 系统权限暂停) 的自动拦截已失效
- 后果: `.env` 真实钱包私钥若误提交/进日志，不再有自动门禁拦截。日常须人工守住「私钥绝不进 git/日志」
- 恢复方式: `git revert 94436c5` 即可恢复全部门禁

### C4. inplay 数据源澄清 (commit ad9d2a9)

- `v2.1 NO_ODDS` 是 **www 节点** base feed，不是 inplay 节点
- `inplay.goalserve.com` (EU Sofia) 有 `value_eu` 赔率字段 (真实结构由 xiaoduan-w8 §3.2 实测确认)
- 解析器已按真实结构加固 (market key 锚定 `"<id>":` / participant 按 name 而非位置匹配 / suspend 感知)

### C5. 仲裁 A — 平局时钟设计 (驳回小肖修改提案)

**保持原有设计**: `fair_value_estimator.hpp:327` `alpha·diff + beta·diff·tf` 中 time 项耦合 score 领先，0:0 恒 0.5 是作者显式设计意图 (line:310 注释)。小肖主张的独立加性修改会让 0:0 平局 fair 随时钟单调偏离 0.5 (真错)。不改一行。

---

## D. 下一步 Roadmap

### Phase 1 (阻塞: Goalserve 白名单 + odds plan)

| 任务 | 依赖 | 负责 |
|---|---|---|
| 接通 inplay 流，列 18-23 有非 NaN 真值 | 白名单开通 + odds plan | 数据组 (小余统筹) |
| `g_bm_inplay_fair` 三元 blend 进 fair (bet365 sharp 锚) | 列 18 有真值 | 老雷 (GM) |
| 验 live_stats join 端到端 (commentaries poller → game_row.soccer_* → 特征非 NaN) | 白名单 | 小段 |

### Phase 2 剩余 (阻塞: 真数据积累 + 离线训练)

| 任务 | 依赖 | 负责 |
|---|---|---|
| paper daemon 跑出第一份完整 (*.fv.jsonl + 结算) → `JoinFile` → training.jsonl | 白名单 + 场次结算 | 自动积累 |
| book-only Tier-1 walk-forward (Polymarket 真数据 ~30 列, IC/fill/CLV/PnL) | 有足够样本 (~6 fold × 7d) | 小蒋 (#20 回测) |
| 残差 LightGBM 训练 → ONNX 导出 → `OnnxFairValueModel` 实现 (`fair_value_model.hpp:332`) | training.jsonl + walk-forward 验证 | 小邓 (#31 ML) |
| 把 ML p_fair 接入控制器 (替换 baseline，控制器一行不改) | ONNX 就绪 | 老雷 (GM) |

**控制器接入点 (评审纪要 §3)**:
```
ML.predict → p_fair_yes → SelectSide → p_fair_selected
  → ReservationInput.fair   (position_controller.hpp:68)
  → ModelPrediction.ci      → margin_floor (:72)
  → ts_window_samples       → n_eff (:74, 已替换静态 200 by Phase 0 项1)
```

### 补充项 (backlog)

| 任务 | 背景 |
|---|---|
| R-NN 命名空间歧义消解 (`RL-`/`RJ-`/`RR-` 拆分) | CLAUDE.md §8.1 第4条，派小米 |
| 双边持仓派生 locked_pair/directional (append v0.5) | 评审纪要 §4，§8.1.5 carve-out 走普通 PR |
| power de-vig 回测验证 (低概率市场 p<0.2) | 仲裁 C: 暂缓，待数据后 P1 |
| NaN 率建档 (75 列各列缺失率统计) | 评审纪要 §5 ②，样本积累后 |

---

## E. 风险登记

沿用评审纪要 §5 六条 + 新增一条:

| # | 风险 | 严重度 | 消解路径 |
|---|---|---|---|
| RR-1 | 列 60-63 from-scratch 泄漏 (x_log_odds_fair 含 baseline fair 变换) | 高 (训练失效) | 残差框架消解 (仲裁 B)；from-scratch 须砍 SET-B 配置 |
| RR-2 | NaN 主导 (~70% 列白名单前全 NaN) | 高 (模型无效) | 先建档各列 NaN 率；白名单后逐列观测；LightGBM native-missing 可处理 |
| RR-3 | n_eff 静默失效 (samples=1 → sigma=0.5 → reservation 极宽永不成交) | 中 | Phase 0 项1 已配 clamp 下限 n_min=10 (`paper_loop.cpp:649`) |
| RR-4 | 持仓列前视泄漏 (walk-forward 须用仿真账本，非回填真实持仓) | 中 | walk-forward 设计中加仿真账本 (小蒋) |
| RR-5 | 样本量不足 (75 列需 ≥5000 独立样本 / ~50 场，Bonferroni α/75 + Deflated Sharpe) | 高 | 优先 book-only ~30 列 Tier-1，降维度需求；积累场次数 |
| RR-6 | 跨洋延迟吃 OFI alpha (秒级窗口 vs 200-500ms RTT) | 中 | 先测 OFI autocorr decay；已有跨洋 RTT 实测 (老吴 `laowu-w10-w2-rtt-3region-results-v1.md`) |
| RR-7 | 私钥防线移除 (commit 94436c5) | 高 (安全) | 人工守住私钥不进 git/日志；`git revert 94436c5` 可恢复；已登记 C3 |

---

## 附: 测试计数轨迹 (按 commit 时序，验证无退化)

| commit | 新增测试 | 总计 |
|---|---|---|
| f4d4fcd (inplay odds parser) | test_inplay_odds_parser 3 case | 1214 |
| 707e6c3 (全链路) | test_inplay_score_odds_integration 2 case | 1216 |
| c1359be (live_stats hop) | test_commentaries_parser 8 case | 1224 |
| b97edac (v0.2 契约) | V02Columns | 1225 |
| 3f0b58e (推理接线) | T11b | 1226 |
| ad9d2a9 (加固) | IO04-IO07 | 1230 |
| 4b17294 (orientation) | IO08-IO10 | 1233 |
| 5c18450 (NO 边微结构) | T11c | 1234 |
| 09dfcfd (双边持仓) | T11d | 1235 |
| 44999f8 (v0.3 契约) | V03Columns | 1236 |
| 4b1d939 (v0.4 契约) | V04Columns | 1237 |
| 5fe919b (Phase 0 五项) | PM01-05 + T11e | 1243 |
| e802165 (Phase 2 标签) | LP01-06 | 1249 |
| c457dad (Phase 2 fv-hub) | FVH01-03 + T11f | 1253 |
