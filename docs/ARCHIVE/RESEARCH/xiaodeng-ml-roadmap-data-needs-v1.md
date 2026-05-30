# ML / AI 路线图 + 数据需求 v1

- Owner: 小邓 (ml-engineer)
- Date: 2026-05-28
- 验收人: 小梁 (financial-expert) + 老钱 (cpo-product-strategy) + 老雷 (GM)
- Wave: 6 (ML 前瞻 + 数据团队对接)
- 关联: xiaoliang-market-structure-v1.md, xiaocheng-signal-catalog-v1.md, laoqian-mvp-scope-rejection-v1.md, laopeng-betting-industry-analysis-v1.md, laoxu-external-tools-inventory-v1.md
- Status: v1 草稿, MVP 期 (M0-M5) 不进生产, 但数据团队 NOW 起为 M5+ 蓄数据

> 小邓按: 我是 ML 顾问, 不下场 v1. 这份文件唯一目的: 告诉数据团队 (小余 / 小段 / 小冯 / 小田) 现在就要做的事 + 给小梁 / 小程 + 老钱 / 老雷 一份 M5 后 ML 路线的 "蓝图". MVP 不上 ML, 但 MVP 期产出的数据如果 schema 不对, M5 后再回头补字段会推迟 ML 上线 6-12 周, 这是我必须在 Sprint-1 就提的需求.

---

## 0. 立场 (一句话)

**v1 (MVP, T+0 → T+24 周) 不上 ML, 信号靠小梁 / 小程的 rule-based + 解析公式 (Pinnacle no-vig / 比分回归 lookup) 就够. ML 是 M5 后 (T+24+ 周) 的扩展工具, 主要补三个洞: (1) 比分模型从 lookup → GBM, (2) 微观结构信号的非线性识别, (3) 在线 concept drift 自适应. 这份文件主要交付物是 "数据团队 NOW 该做什么" — 不是 ML 模型代码.**

老钱 scope (laoqian-mvp-scope-rejection-v1.md §3.3) 明确写 "模型类型: ❌ ML/DL/transformer/RL/在线学习", 这条我 100% 同意, MVP 期不挑战.

---

## 1. ML 在 MVP 后的角色

### 1.1 替代什么 (rule-based 的天花板)

| Rule-based 信号 (v1) | 天花板在哪 | ML 能补的 alpha |
|---|---|---|
| SIG-P0-01 pinnacle-novig-revert | multiplicative no-vig 在低概率区 (< 0.15) 偏差大; 不分 sport / not adaptive | GBM 学习 (sport, league, p_pinn, depth) → 修正 fair price, 期望 edge +0.5 ¢ |
| SIG-P0-02 score-price-mismatch (lookup table) | 状态空间组合爆炸 (sport × time × diff × possession × clutch × elo); 极端 state 样本 < 100 不可信 | GBM 把 lookup → 平滑外推, 高维状态可识别, 极端 state 用模型而不是 NaN |
| SIG-P1-03 goalserve-lead-taker (二值触发) | 事件类型粒度粗 (得分 / 红牌 / 受伤), 没考虑事件 magnitude (NBA 三分 vs 罚球) | 多分类 + 回归: 用事件 features 预测 maker 调整幅度 Δp |
| SIG-P2-07 maker-cascade-detect (规则: > 50% size 消失) | 阈值人工拍, 在不同流动 regime 下不 robust | LSTM / 1D-CNN 学 book imbalance 时间序列模式 |

**总结**: ML 不替代任何 v1 信号, 是 **每个 v1 信号 v2.0 升级时的可选工具**. 升级路径 = "rule v1 → 实盘 4-8 周 → 数据足够 → ML 替代 / 增强 v2".

### 1.2 补充什么 (rule 覆盖不到的 alpha)

1. **跨信号融合 (signal ensemble)**: 当 P0-01 + P0-02 + P1-03 同时触发或互相矛盾, rule-based 难以协同. ML stacking (logistic / lightGBM) 学最优 weight.
2. **非线性微观结构**: book imbalance / order arrival / cancel cascade 这类高维时序信号, rule 难写, GBM / sequence model 适合.
3. **Narrative / news / lineup 信号**: 文本类输入 (Twitter / Rotoworld) MVP 不接, M5 后可以加 BERT-mini embedding + GBM 做 fade 决策.
4. **Adaptive 阈值**: rule 阈值 (3 ¢ 偏离 / 2 σ z-score) MVP 期是常数, M5 后用 online learning 让阈值随 regime (常规赛 vs 季后赛 / pre 1月 vs 圣诞密集赛程) 自适应.

### 1.3 不做什么 (永不进生产)

| 类别 | 理由 |
|---|---|
| ❌ DL (transformer / LSTM 多层) 直接出仓位决策 | 黑盒, 老韩风控不签字; 推理 latency > 50ms 老姜不签字 |
| ❌ RL (强化学习) 端到端策略 | 训练样本 (live trade) 不够 + 探索 vs 利用在实盘代价太高 |
| ❌ ML 替代 RiskManager | 风控必须 rule-based 可解释 (老韩硬性要求, S1-004) |
| ❌ ML 替代 Kelly sizing | 凯利公式 closed-form, 没必要 ML; ML 只能改 p_win 输入 |
| ❌ "AI 选股" 式宣传 / pitch | 公司是量化, 不是 AI 公司; 老雷战略锚 |
| ❌ 任何模型权重 > 100MB | 跨洋部署 + 内存 / 加载时间, ONNX 模型必须 < 100MB (与老姜对齐) |

### 1.4 与小程信号目录的对应表

| 小程 SIG-ID | v1 实现 (M0-M5) | v2 ML 升级 (M5+) | 期望 alpha 增益 |
|---|---|---|---|
| P0-01 | rule + Pinnacle multiplicative no-vig | GBM (sport-aware no-vig) | +0.3-0.5 ¢ edge/trade |
| P0-02 | lookup table score_model | LightGBM 替代 lookup | hit rate +1-2%, 极端 state 可用 |
| P1-03 | 二值事件触发 | 多分类 + 幅度回归 | trades +30%, edge 持平 |
| P1-04 lineup-news-lag | rule (lineup ts 比对) | + NLP sentiment | 仍以 rule 为主, NLP 是补强 |
| P1-05 favorite-overpay-fade | narrative 标签 (老彭手标) | narrative 自动分类 (BERT-mini) | 标注成本归零 |
| P1-06 event-overreaction-fade | 事件分类 rule | 事件 magnitude 回归 | edge +20% |
| P2-07 maker-cascade-detect | 阈值 rule | 1D-CNN / GBM over book features | hit rate +3-5% |
| P2-08 cross-market-arb | 数学约束 rule | 仍 rule (套利不需要 ML) | ML 不上 |
| P2-11 steam-follow | size 阈值 rule | GBM (size, time, sport, depth) | hit rate +2% |

---

## 2. 路线图 (T+24 → T+36 周, 跨 12 周)

### 2.1 总览

```
T+0 ─────── T+24 ─────── T+30 ─────── T+36 ─────── T+48 ─────
   MVP rule    阶段1        阶段2         阶段3       v3+
   only        feature      onnx         online
               + baseline   推理框架      learning
```

**前置条件 (T+24 周必须满足才进阶段1)**:
- MVP 实盘已稳定 4 周 (Sharpe > 1.0)
- P0-01 + P0-02 v1 累计 > 500 trades, 数据完整
- 数据团队 (小余 / 小段 / 小冯 / 小田) 已交付 §3 数据需求
- 老钱 + 老雷 签字 ML scope 解锁

### 2.2 阶段 1: 离线特征工程 + baseline 模型 (T+24 → T+28, 4 周)

**目标**: 用 Python (.venv) 离线训练 baseline GBM, 在历史数据上证明 ML 替代 lookup 有 +1-2% hit rate 提升.

**Deliverables**:
- `ml/feature_pipeline.py` — 从 parquet 数据仓 (小余) 抽取 §4 列表的 30+ 特征, 输出 train/test split
- `ml/baseline_lgbm.ipynb` — LightGBM 5-fold walk-forward, 输出 Brier score / hit rate / calibration plot
- `ml/calibration.py` — Platt / isotonic 校准 (Brier 之外, 我们要的是 calibrated probability)
- 第一份模型评测报告 (跑两个 P0 信号的 v2 候选)

**数据需求 (本阶段必须 ready)**:
- 历史 Polymarket book snapshot (5s 频率, 6 个月)
- 历史成交 (tick-by-tick, 6 个月)
- 历史 Pinnacle 赔率 (30s 频率, 6 个月)
- 历史 Goalserve inplay 比分流 (1-3s 频率, 1 年)
- 终局 outcome label (每场比赛 home win/loss + final score)

**工程依赖**:
- 老吴 S1-025 .venv (lightgbm / sklearn / pyarrow / polars) 已就位
- 小余 S1-D 数据仓 (parquet on local disk 即可, 不需要 DWH)
- 小蒋 walk-forward backtest 框架 (与 v1 共用)

**验收门槛**:
- LightGBM Brier < 0.18 (vs lookup baseline 0.20)
- Walk-forward 5-fold 中 4 fold OOS hit rate > rule v1
- 模型解释性: 至少前 10 features 的 SHAP plot 可被老韩 read

### 2.3 阶段 2: 在线模型推理框架 (T+28 → T+32, 4 周)

**目标**: 把阶段 1 的 LightGBM 通过 ONNX Runtime / Treelite 导入 C++ 主进程, 推理 latency < 50ms p99.

**Deliverables**:
- `src/ml/onnx_inference.cpp` — ONNX Runtime C++ wrapper, 单例 + thread-local session
- `src/ml/treelite_inference.cpp` — 备选 (treelite 编译为 .so, 调用更快但不灵活)
- `src/ml/model_registry.cpp` — 模型版本管理, hash + ts + git_commit, 启动时校验
- `bench/ml_inference_bench.cpp` — 推理 latency p50/p90/p99 (与老姜对齐, target < 50ms p99)
- `tests/ml/parity_test.cpp` — Python LightGBM 输出 vs C++ ONNX 输出, 误差 < 1e-5

**数据需求**: 与阶段 1 相同, 不新增

**工程依赖**:
- 老周 architecture v0.2 已留 `src/ml/` 子目录的口子 (待 review)
- 老姜延迟预算 (laojiang-latency-budget-v1.md): 我们要 < 50ms 是策略 hot path 边界的一半, 给老姜 buffer
- 老郭 code review: ML 模型注入主进程必须双签 (老周 + 老郭), 这是新进程边界

**验收门槛**:
- 推理 latency p99 < 50ms (在 MVP 部署机器上)
- 模型 hot-reload: 不重启进程即可换模型 (atomic ptr swap)
- 模型 / feature spec 版本绑定: 推理时强制 assert feature schema match

**关键技术决策**:
- **选 ONNX Runtime 而不是 LibTorch / TensorFlow Lite**: LightGBM / sklearn 原生支持导出 ONNX, ONNX RT C++ binding 成熟, 跨平台一致.
- **不选 Python embedding (pybind11 把 Python 嵌进 C++)**: GIL + 跨语言开销, 老姜会否决.
- **Treelite 作为备选**: 若 ONNX latency 不达标, Treelite 把 GBM 编译成 if-else 树, 推理更快 (但部署灵活性低, 换模型要重编译).

### 2.4 阶段 3: online learning v2 (T+32 → T+36, 4 周)

**目标**: 给 P0-02 score-model 加 online update (每场比赛后更新 leaf weights), 适应 regime shift (季后赛 vs 常规赛).

**Deliverables**:
- `ml/online_updater.py` — incremental LightGBM (refit_n_iters), 每日离线跑, 输出新 ONNX
- `ml/drift_detector.py` — KS test on feature distribution + PSI (population stability index), 报警阈值
- `ml/champion_challenger.py` — A/B framework, champion 跑实盘 90%, challenger 跑 paper 10%, 4 周决出
- `dashboards/ml_health.json` — Grafana 看板 (与小郑协同), 监控 prediction drift / feature drift / model age

**数据需求**: 与阶段 1 相同 + 实时 prediction log (每次推理写 KV, 含 input features + output prob + actual outcome 反查)

**工程依赖**:
- 小董 data-stats 协同做 drift detection 统计设计
- 小郑 observability 协同看板

**验收门槛**:
- PSI > 0.2 触发 yellow, > 0.3 触发 red (业界标准)
- Challenger 上线条件: 4 周 Sharpe > champion + 0.2 且 OOS hit rate > champion
- Model age > 90 天强制 retrain (即使无 drift)

### 2.5 阶段总览表

| 阶段 | 时间 | 主交付 | 数据需求里程碑 | 风险 |
|---|---|---|---|---|
| 1 (offline + baseline) | T+24 → T+28 | LightGBM 替代 lookup | 6 月历史数据 ready | 数据缺字段 (尤其 Pinnacle) |
| 2 (online inference) | T+28 → T+32 | ONNX C++ 集成 | 不新增 | 推理 latency 超预算 |
| 3 (online learning) | T+32 → T+36 | drift + A/B | 实时 prediction log | 模型频繁切换导致 PnL 抖动 |

---

## 3. 支撑数据需求清单 (给小余 / 小段 / 小冯 / 小田)

**这是本文档的核心交付物.** 数据团队 NOW (Sprint-1, T+0 → T+4) 起就要照这个 schema 攒数据, 不然 M5 后回头补字段会推迟 ML 上线 6-12 周.

### 3.1 历史成交数据 (给小余 S1-D)

**字段需求**:

| 字段 | 类型 | 必需性 | 用途 | 备注 |
|---|---|---|---|---|
| `market_id` | string | 必需 | 市场标识 | Polymarket condition_id |
| `token_id_yes` / `token_id_no` | string | 必需 | 二元 token | |
| `trade_ts` | int64 (ns) | 必需 | 成交时间 | UTC, ns 精度 |
| `trade_price` | float64 (cents) | 必需 | 成交价 | 0-100, ¢ 整数 |
| `trade_size` | float64 (USDC) | 必需 | 成交量 | notional 而不是 shares |
| `trade_side` | enum {BUY, SELL} | 必需 | taker 方向 | 若 API 不直给, 推断 |
| `taker_wallet` | string (hex) | 推荐 | sharp 钱包识别 | P2-11 用 |
| `maker_wallet` | string (hex) | 推荐 | maker 行为分析 | P2-07 用 |
| `trade_hash` | string | 必需 | dedup + 追溯 | 链上 tx hash |
| `block_number` | int64 | 必需 | 链上时序 | UMA 仲裁追溯用 |
| `data_source_ts` | int64 (ns) | 必需 | 数据接入时间 | 用于测 push 延迟 vs trade_ts |

**时间窗**: 6 个月初始 + 之后实时累积 (每日落 parquet 一份)

**粒度**: tick-by-tick, 不预聚合

**存储建议** (与小余沟通):
- Parquet 列存, 按 (year, month, sport) partition
- 单 parquet 文件 < 1 GB, 用 zstd 压缩
- 总量预估: 6 月 NBA + NFL + MLB Moneyline ≈ 2-5 GB compressed

**关键 (易漏的)**:
- `trade_side` 必须有, 没有的话 sharp money 信号全废
- `data_source_ts` vs `trade_ts` 的 lag 分布是 5.1 / 5.8 信号的核心特征, 必须保留

### 3.2 历史 orderbook snapshot (给小余 + 小冯)

**频率分级**:

| 用途 | 采样频率 | 说明 |
|---|---|---|
| Pregame baseline (信号 P0-01) | 5 min | 节流, 24h 内一场 ~ 288 snapshots |
| Pregame 临开赛 (T-60min → tipoff) | 30 s | T-60min 内加密采样, 120 snapshots |
| Inplay (信号 P0-02 / P1-03 / P2-07) | 1 s | 高频, 4h 一场 ~ 14400 snapshots |
| 极端 (信号 P2-07 cascade detect) | tick by tick (WSS event-driven) | 不需要 polling, WSS push 全留 |

**字段需求** (每个 snapshot):

| 字段 | 类型 | 必需性 | 备注 |
|---|---|---|---|
| `market_id` | string | 必需 | |
| `snapshot_ts` | int64 (ns) | 必需 | |
| `bids[0..10].price/size` | array | 必需 | 前 10 档, < 10 档补 NaN |
| `asks[0..10].price/size` | array | 必需 | 同 |
| `mid_price` | float64 | 必需 | (bid[0] + ask[0]) / 2, 预计算 |
| `spread_cents` | float64 | 必需 | ask[0] - bid[0], 预计算 |
| `depth_top5_bid_usdc` | float64 | 必需 | 前 5 档 bid 总 size, ML 特征 |
| `depth_top5_ask_usdc` | float64 | 必需 | 同 ask |
| `last_update_ts` | int64 | 必需 | API 提供的 book 更新时间 |
| `data_source_ts` | int64 | 必需 | 我们接入时间, 测 stale |

**时间窗**: 6 个月初始

**粒度选择理由**:
- 1 s inplay snapshot 是 ML 的 sweet spot: 比 tick-by-tick 数据量小 30 倍, 但保留所有秒级信号 (P0-02 / P1-03)
- WSS event-driven 是 P2-07 cascade detect 的必需, 不能 polling

**存储建议**:
- Inplay snapshot 量大 (1 NBA 场 14400 × 100 bytes ≈ 1.4 MB), 6 月 ~ 4 TB raw, 压缩后 ~ 800 GB
- 建议分两库: hot (近 30 天, 1 s 全留) + cold (30 天前, 降采样到 10 s)
- @小余: 这是 §3.1 之外最大的存储项, 提前预算

### 3.3 实时特征流 (给小田)

**关键约定**: ML 在线推理必须用 **与 C++ 策略层完全相同的 feature 计算**, 否则线上线下不一致 = 模型不可信.

**SOP**:
1. 小田的实时 feature pipeline (in-process feature store, F-01 ~ F-17 见小程 §5.1) 产出的 feature 写入 Prometheus metrics + 落 KV (Redis-like)
2. ML 推理时 **不重新计算 feature**, 直接读 KV (或共享内存)
3. 离线训练时 **回放 KV** 而不是从原始数据重算, 保证 PIT (point-in-time) correctness

**字段需求**: 与小程 §5.1 完全一致, 不重复列. 我额外要求小田补:

| 字段 ID | 名称 | 我加的理由 |
|---|---|---|
| F-18 | `polymarket.book.flow_imbalance_5s` | order arrival 速率, P2-07 必需 |
| F-19 | `polymarket.trade.vwap_5min` | 短期 VWAP, ML 学价格趋势 |
| F-20 | `goalserve.event_ts_lag_p90` | 滚动 5min 的 Goalserve push 延迟 p90, ML 判定信号可靠性 |
| F-21 | `model.last_prediction_age_ms` | 模型上次推理至今时长, 监控 |
| F-22 | `regime.is_postseason` | 季后赛 flag, 静态查表 |

**接口约定**:
- Feature store 必须暴露 atomic snapshot API (一致性视图, 防止读到半更新)
- 拉取延迟 < 5ms (策略 hot path 内)
- ML 推理拉取所有需要的 feature ≤ 1 次 call (不要 N+1)

### 3.4 标签 (label) 生成 (给小余 + 小董)

**MVP 期 label 来源**:

| Label 类型 | 来源 | 用途 | 注意事项 |
|---|---|---|---|
| `outcome_binary` (home_win 0/1) | Goalserve 终局比分 | 全部分类信号 | UMA 仲裁后再 confirm, 避免争议比赛 |
| `final_score_home / away` | Goalserve | totals 类信号 (M5 后) | NFL 加时影响 totals, 需保留 |
| `closing_mid_price` | Polymarket book at T-0s (kickoff) | CLV evaluation | 与 Pinnacle closing line 对比 |
| `settle_price` | Polymarket UMA 结算 | 真实 PnL | 注意挑战期 2h, 用 settled 字段 |
| `forward_return_t+τ` | (price_{t+τ} - price_t) | 短期信号回归 | τ ∈ {30s, 5min, 1h, 6h} 多 horizon |

**关键 SOP** (避免 label leakage):

1. **PIT (point-in-time) correctness**: feature 在 t 时刻只能用 t 时刻 *可观察到* 的信息. 不能 leak future. 例如 "本场比赛 home 胜率" 这种 hindsight feature 禁用.
2. **Look-ahead bias**: forward_return_t+τ 在训练时是 label, 但在 t 时刻不可知, 必须严格 mask.
3. **Survivorship bias**: 数据集必须包含所有比赛 (包括我们没下单的), 不能只看 "我们触发的". 小余的数据仓必须是 universe 级.
4. **Settlement label vs market label**: 用 UMA settled 而不是 Goalserve 比分, 因为有罕见的 0.5% 比赛 Goalserve 错而 UMA 改判.

### 3.5 外部数据源 (给老段 / 小段评估, 优先级排序)

| 数据源 | 用途 | 优先级 | 接入成本 | M5 之前要不要做 |
|---|---|---|---|---|
| Pinnacle 历史 + 实时赔率 | P0-01 核心 + 全 ML 价值锚 | **P0** | 中 (API or 第三方) | **必须** (Sprint-1 起拉) |
| ESPN play-by-play | P0-02 score_model 训练 | **P0** | 低 (公开 API) | **必须** |
| Goalserve lineup | P1-04 | P1 | 已有 | M5 前 ready |
| Rotoworld lineup webhook | P1-04 双源备份 | P1 | 中 (付费) | M5 前评估 |
| Twitter / X firehose | NLP narrative 信号 | P2 | 高 (API + 存储) | M5 后再说 |
| Weather API (NFL outdoor / MLB) | totals 信号 (totals M5 后) | P2 | 低 (openweather) | M5 后接 |
| News API (GNews / NewsAPI) | catalyst 信号 (5.12) | P3 | 中 | M9 后再说 |
| FiveThirtyEight Elo (历史) | score_model 静态特征 | P1 | 低 (CSV download) | M5 前手工灌 |
| 538 实时 win prob | 对照基线 | P2 | 已停更, 用 ESPN bpi 代替 | M5 后 |
| Sportsbook Review odds | NFL key number 信号 (P2-09) | P2 | 中 | M9 后 |

**老段评估接口**: 我希望小段在 Sprint-2 给一份 "外部数据源接入工时 + 月成本" 表, 让老钱 / 老雷批预算.

### 3.6 数据治理 (给小余 + 老周)

**Point-in-Time correctness (最重要)**:
- 所有特征落盘必须带 `as_of_ts` (数据可观察时间), 训练时 join 必须用 `feature.as_of_ts <= label_window_start`
- 任何 backfill / 修正都必须记录 `corrected_at_ts`, 训练时按 as_of 不按 corrected
- 这条违反 = 模型回测无效

**版本化**:
- Feature 定义 `F-XX@vN` (与小程 §5.2 一致), 任何变更 bump 版本
- 数据 schema 变更走 ADR (与老周对齐)
- 模型 model_id = `{name}@{git_sha}-{trained_ts}`, registry 强制唯一

**回放可复现 (replayability)**:
- 训练数据集快照保留至少 1 年 (合规 + 复盘)
- 任何模型必须能从 `(model_id, training_dataset_snapshot_id)` 二元组完全复原
- @小蒋 backtest 框架要支持 "给定 (model, dataset, ts), 重跑回测" 的接口

**数据质量监控** (与小田 + 小董协同):
- 每日产出 DQ report: missing rate / outlier rate / distribution shift
- Goalserve 比分数据与 ESPN 对比, 不一致比例 < 0.5%, 否则报警
- Polymarket book snapshot stale rate (data_source_ts - last_update_ts > 30s) < 1%

### 3.7 给数据团队的 NOW 任务清单 (Sprint-1 → Sprint-3)

| 任务 | Owner | 截止 | 优先级 |
|---|---|---|---|
| 历史 Polymarket book + trade 字段 schema 定稿 | 小余 + 我 | 6/12 (Sprint-1 末) | P0 |
| Pinnacle 数据接入方案 (路径 A/B/C) | 老李 + 老彭 | 6/12 | P0 |
| Goalserve inplay 比分流落盘 (1s 频率) | 小段 + 小余 | 6/19 (Sprint-2) | P0 |
| 6 月 NBA + NFL + MLB 历史成交 + book 灌库 | 小余 | 7/10 (Sprint-3 末) | P0 |
| ESPN PBP API 接入 (score_model 训练数据) | 小段 | 7/10 | P0 |
| Feature store API spec (含 PIT) | 小田 + 我 | 7/24 (Sprint-4) | P1 |
| Label 生成 SOP + leakage 检查工具 | 小董 + 我 | 7/24 | P1 |
| FiveThirtyEight Elo CSV 灌历史 | 我自己 | 8/7 | P2 |

---

## 4. 特征工程清单 (28 候选, ≥ 20 要求)

按类别分组. 每条标注 (用于哪个 ML 信号 / 实时性 / 计算复杂度).

### 4.1 价格特征 (8)

| ID | 名称 | 频率 | 用途 | 复杂度 |
|---|---|---|---|---|
| ML-F-P01 | `mid_price` | tick | 全部 | O(1) |
| ML-F-P02 | `spread_bps` | tick | P0-01, P0-02, P2-07 | O(1) |
| ML-F-P03 | `log_return_5s / 30s / 5min` | 5s | P0-02, P2-11 | O(1) (滑窗) |
| ML-F-P04 | `realized_volatility_5min` | 5s | P0-02, regime detect | O(N) 滑窗 |
| ML-F-P05 | `bid_ask_imbalance` | tick | P2-07, P2-11 | O(1) |
| ML-F-P06 | `depth_top5_bid_usdc / ask_usdc` | tick | 全部 (流动性 gate) | O(1) |
| ML-F-P07 | `vwap_5min` | tick | P2-11 | O(N) 滑窗 |
| ML-F-P08 | `price_distance_from_24h_ma` | 1min | 价值回归类 | O(1) (precomputed MA) |

### 4.2 体育特征 (8)

| ID | 名称 | 频率 | 用途 | 复杂度 |
|---|---|---|---|---|
| ML-F-S01 | `score_diff` | 比分事件 | P0-02 | O(1) |
| ML-F-S02 | `time_remaining_seconds` | 1s | P0-02 | O(1) |
| ML-F-S03 | `possession_home` (0/1/none) | 1s | P0-02 | O(1) |
| ML-F-S04 | `is_clutch` (last 5min + diff <= 5) | 1s | P0-02 | O(1) |
| ML-F-S05 | `pace_actual_vs_expected` | 30s | P0-02 (NBA), totals | O(1) (precomputed expected) |
| ML-F-S06 | `home_team_elo / away_team_elo` | 比赛级 | P0-02 | O(1) (static) |
| ML-F-S07 | `injury_flag_home / away` | 事件 | P0-02 滤波, P1-04 | O(1) (event-driven) |
| ML-F-S08 | `lineup_completeness` (有多少 starter) | pregame | P1-04 | O(1) (lineup snapshot) |

### 4.3 衍生特征 (z-score / momentum / mean-reversion) (6)

| ID | 名称 | 频率 | 用途 | 复杂度 |
|---|---|---|---|---|
| ML-F-D01 | `z_score_vs_pinnacle_novig` | 30s | P0-01 | O(1) |
| ML-F-D02 | `z_score_vs_score_model` | 比分事件 | P0-02 | O(1) (model lookup) |
| ML-F-D03 | `momentum_5min` (price - lag_5min) | 5s | P2-11, regime | O(1) (ring buf) |
| ML-F-D04 | `mean_reversion_score` (price - kalman_filter) | 1min | M9 后实验 | O(N) Kalman |
| ML-F-D05 | `price_drift_since_open` | 1min | P0-01 价值监控 | O(1) |
| ML-F-D06 | `regime_volatility_bucket` (low/med/high) | 5min | adaptive 阈值 | O(N) clustering |

### 4.4 跨市场特征 (Polymarket vs Pinnacle vs 内部 cross) (4)

| ID | 名称 | 频率 | 用途 | 复杂度 |
|---|---|---|---|---|
| ML-F-X01 | `pm_pinnacle_basis_cents` | 30s | P0-01 | O(1) |
| ML-F-X02 | `pm_pinnacle_basis_zscore_24h` | 30s | P0-01 ML 版本 | O(N) 24h 滑窗 |
| ML-F-X03 | `internal_consistency_residual` (ML + spreads + totals) | 1min | P2-08 | O(N_markets) |
| ML-F-X04 | `cross_book_steam_lag_ms` (PM vs Pinnacle 跳价时差) | 事件 | M9 后 P2-08 衍生 | O(1) (event match) |

### 4.5 微观结构 + 流动 (2)

| ID | 名称 | 频率 | 用途 | 复杂度 |
|---|---|---|---|---|
| ML-F-M01 | `cancel_rate_5s` | 5s | P2-07 | O(N) event count |
| ML-F-M02 | `large_trade_count_5min` (size > $5K) | 5min | P2-11 | O(N) 滑窗 |

**总计 28 候选**, 满足 ≥ 20 要求.

**特征筛选策略 (阶段 1 实施)**:
- 第一轮: 全 28 进 LightGBM, 看 feature importance
- 第二轮: 留 importance > 0.5% 的, 通常 12-18 个
- 第三轮: 看相关性 (correlation > 0.85 drop 一个)
- 终态: 8-15 features in production model (够用且不过拟合)

---

## 5. 模型推理框架

### 5.1 离线训练栈 (Python, 在 .venv)

```
.venv/
  ├── lightgbm (主力模型框架)
  ├── sklearn (preprocessing, calibration, metrics)
  ├── pyarrow / polars (数据加载)
  ├── onnx / onnxmltools / skl2onnx (模型导出)
  ├── jupyter (notebook 分析)
  └── shap (模型解释, 给老韩看)
```

**约定** (与老吴 S1-025 对齐):
- ML 代码统一在 `ml/` 目录, **不进 src/ (C++ 生产)**
- Jupyter notebook 走 `notebooks/`, git 提交前必须 nbstripout (清 output)
- Python 代码风格 = black + ruff (与公司 Python 风格一致)

### 5.2 在线推理栈 (C++)

**首选: ONNX Runtime C++**

```
src/ml/
  ├── inference_engine.h     // 抽象接口
  ├── onnx_session.cpp        // ONNX RT wrapper (thread-local session)
  ├── feature_assembler.cpp   // 从 feature store 拼装 input tensor
  ├── prediction_logger.cpp   // 落 KV 用于 drift detect (§2.4)
  └── model_registry.cpp      // 版本管理 + atomic ptr swap (hot reload)
```

**编译依赖** (与老周 / 老姜对齐):
- onnxruntime 1.18+ (C++ headers + .so / .dylib)
- 建议 vcpkg 或 conan 管理 (与项目其他 C++ 依赖一致)
- 模型加载 mmap (不 copy 到内存), 节省 RSS

**备选: Treelite**

若 ONNX latency 不达 < 50ms p99, 备选 Treelite:
- LightGBM → Treelite → 编译为 .so (本质上是 if-else 树 + SIMD)
- 推理 latency 通常比 ONNX 快 2-5 倍 (尤其小模型 < 1000 trees)
- 缺点: 换模型要重编译 + 部署, 灵活性低

### 5.3 Latency budget (与老姜 laojiang-latency-budget-v1.md 对齐)

| 阶段 | budget | 说明 |
|---|---|---|
| Feature assembly (读 feature store) | < 5 ms | 内存 + atomic snapshot |
| ONNX inference (1 sample, < 15 features) | < 10 ms | LightGBM 500 trees 量级 |
| Calibration + threshold | < 1 ms | Platt scaling 闭式 |
| Logging (async, 不阻塞 hot path) | 0 ms (后台线程) | |
| **总计 ML 推理 latency** | **< 50 ms p99** | 与老姜对齐, 是策略 hot path 边界的 1/10 |

**老姜对齐点**:
- 策略 hot path 总预算 500 us (KR-A-3) — ML 推理 50 ms 不在这条 hot path 上
- ML 推理在 **决策路径** 上, 不在 **下单路径** 上. 决策路径预算 200-500 ms (跨洋链路占大头).
- 因此 ML 50 ms 是合理预算, 但必须 hard cap.

### 5.4 模型版本管理

**Model ID 格式**:
```
{signal_name}@{model_arch}-{git_sha7}-{trained_ts}
例: score-model@lgbm-a1b2c3d-20260911T1200Z
```

**Registry**:
- 单一文件 `models/registry.yaml`, 记 model_id → onnx_path + feature_spec_path + metrics
- 启动时 C++ 加载 active model_id, 校验 feature_spec 与代码 hash match
- Hot reload: SIGHUP 触发 registry reload, atomic ptr swap

**生命周期**:
- 训练 → staging → champion (live 90%) → archive (90 天保留) → delete

---

## 6. 过拟合 / 衰减检测

### 6.1 Walk-forward validation (与小蒋协同)

**SOP** (训练时强制):
```
将 6 月历史数据按时间切 5 fold:
  fold 1: train [m1-m2], test [m3]
  fold 2: train [m1-m3], test [m4]
  fold 3: train [m1-m4], test [m5]
  fold 4: train [m1-m5], test [m6]
  (扩张窗口 expanding window, 不滚动 rolling)
```

**通过门槛**:
- 5 fold 中 4 fold OOS hit rate > rule baseline
- OOS / IS Sharpe ratio ≥ 0.6 (与小程 §3.6 一致)
- 不同 fold 的 feature importance top 10 重合度 > 70% (稳定性)

### 6.2 上线后 α 监控 (与小董协同)

**实时指标** (每日落盘):

| 指标 | 计算 | 报警阈值 |
|---|---|---|
| 滚动 4 周 hit rate | live trades 实际命中率 | < rule baseline - 2%, yellow; < baseline - 5%, red |
| 滚动 4 周 Sharpe | live PnL Sharpe | < 0.5, yellow; < 0.2, red |
| Prediction drift (KL div) | live pred 分布 vs train pred 分布 | KL > 0.1, yellow; > 0.3, red |
| Feature drift (PSI) | live feature 分布 vs train feature 分布 | PSI > 0.2, yellow; > 0.3, red |
| Live / Shadow PnL ratio | live PnL / sandbox 相同信号 PnL | < 0.7 持续 2 周, red (实盘冲击吃 alpha) |
| Calibration ECE | expected calibration error on live | > 0.05, yellow; > 0.10, red |

**A/B (champion-challenger)**:
- Champion 跑实盘 90%, Challenger 跑 paper / 10% 实盘
- Challenger 上线条件: 4 周 Sharpe > champion + 0.2 且 OOS hit rate > champion 且 calibration 不差
- Champion 退役条件: 4 周 Sharpe < 0.5 或被 challenger 显著击败

### 6.3 模型回炉 SOP

**触发条件 (任一)**:
- Sharpe red alert (4 周 < 0.2)
- PSI > 0.3 持续 1 周
- Model age > 90 天 (强制刷新)
- 重大 regime shift (季后赛开始 / 规则变更)

**回炉流程**:
1. 自动: PagerDuty 告警 → 我接手 (小邓)
2. 抽近 30 天数据 + 老训练集, retrain
3. Walk-forward 通过 → 进 challenger 流程
4. 4 周 A/B 通过 → champion 切换 (atomic, 无停机)
5. 旧 champion 进 archive 保留 90 天

**回炉时的紧急 fallback**:
- 若 model red alert 后没新模型 ready, 自动切回 rule baseline (即 v1 信号)
- Rule baseline 永远保留作为 fallback, 不删

---

## 7. ML 在 paper trading 阶段的角色

### 7.1 老钱 scope 边界 (laoqian-mvp-scope-rejection-v1.md §3.3)

明文写: "模型类型: ❌ ML/DL/transformer/RL/在线学习". 这指 **MVP 实盘** 不上 ML.

### 7.2 我对 paper trading 阶段 ML 的判断

**结论: paper trading 期可以做 ML 研究, 但不算"红线参数", 只算"candidate signal".**

理由:
1. 用户高优指令 "必须 paper trading 稳定盈利才上实盘" — 红线是 paper trading 的 PnL / Sharpe, 不是 ML 是否用
2. Paper trading 是研究环境, 跑 ML candidate 在 sandbox 里不影响实盘
3. Paper trading 期 ML 仅作为 **影子信号 (shadow signal)**, 与 rule signal 并行跑, **不下单**
4. Paper trading 期产生的 ML vs rule 对比数据, 是 M5 后阶段 1 (§2.2) 的输入

**具体约束**:
- ML 信号在 paper trading 期 **只读 not write**: 不进入下单决策路径
- ML 信号的 PnL 单独记账 (sandbox PnL), 不计入 paper trading 验收
- ML 信号的训练 + 推理代码留在 `ml/` 不进 `src/`, 老郭 code review 时可拒
- Paper trading 验收门槛 (Sharpe > 1.0) **只看 rule signal**, ML 不算

### 7.3 老钱 scope 与 paper trading ML 研究的兼容性

| 老钱 scope 文 | 我的解读 |
|---|---|
| "MVP 实盘不上 ML" | ✅ 我同意, 实盘只跑 rule |
| "MVP 期不引入新数据源" | ⚠️ Pinnacle / ESPN 是 rule 信号 (P0-01) 必需, 不算新增, 算原本就要的 |
| "M5 后再评估 ML" | ✅ 我的路线图 §2 完全对齐 (T+24 = M5) |
| "Anti-creep 防护" | ✅ ML 研究在 `ml/` 隔离, 不污染 `src/` |

**结论**: 我的路线图 NOT scope creep. Paper trading 期我做的事:
- 攒数据 (§3, 数据团队对接)
- 写 baseline notebook (sandbox, 不进生产)
- 评测 ML candidate (shadow mode)

M5 实盘验收前我 0 行代码进 `src/`. M5 后老钱 + 老雷签字才进阶段 2 (ONNX C++).

---

## 8. 数据科学栈使用约定

### 8.1 Python .venv (与老吴 S1-025 对齐)

```
.venv/
  ├── lightgbm >= 4.3
  ├── scikit-learn >= 1.4
  ├── pandas >= 2.2
  ├── polars >= 0.20 (大数据 join 用 polars 不用 pandas)
  ├── pyarrow >= 15
  ├── numpy >= 1.26
  ├── matplotlib + seaborn (可视化)
  ├── jupyter + jupyterlab
  ├── onnx >= 1.16, onnxmltools, skl2onnx
  ├── shap (模型解释)
  ├── statsmodels (统计检验)
  ├── nbstripout (commit hook)
  └── black + ruff (格式)
```

**装包命令** (留给老吴 + 自己):
```bash
.venv/bin/pip install lightgbm scikit-learn pandas polars pyarrow \
  onnx onnxmltools skl2onnx shap statsmodels jupyter jupyterlab \
  nbstripout black ruff matplotlib seaborn
```

### 8.2 目录约定

```
sports-trader-cpp/
├── src/                    # C++ 生产代码 (不放 ML 训练)
│   └── ml/                 # ONNX 推理 C++ (M5 后才有)
├── ml/                     # Python 训练 + 离线分析
│   ├── feature_pipeline/
│   ├── models/             # 训练脚本
│   ├── notebooks/          # Jupyter
│   ├── evaluation/
│   └── tools/              # CLI utility
├── models/                 # 训练好的 ONNX 文件 + registry.yaml
└── docs/RESEARCH/          # 包含本文件
```

### 8.3 代码约定

- ML 实验 in notebook, 重要 finding 提炼成 .py 脚本入库
- Notebook 提交前必须 nbstripout (clean output, 避免 git diff 噪声)
- 大数据 (> 100 MB) 不进 git, 走 data lake (小余 / DVC TBD)
- ONNX 模型 < 100 MB 可进 git LFS, > 100 MB 走 model registry storage
- 所有训练脚本必须能 deterministic 复现 (random_seed 固定, 数据快照 id 固定)

### 8.4 不进生产的红线

| 不做 | 理由 |
|---|---|
| Python 代码进 C++ 主进程 (pybind11 embed) | GIL + 跨语言开销, 老姜否决 |
| 模型 > 100MB | 加载 + 部署 + 跨洋更新代价 |
| 不可解释的 DL (transformer, RNN) 直接出仓位 | 老韩风控否决 |
| 在线训练 (incremental train) 在 prod 进程 | 训练 + 推理共进程, 内存 + 安全风险 |
| ML 训练数据写入生产 KV | 训练写 cold storage, prod 进程不应有训练 IO |

---

## 9. 开放问题

### 9.1 待 [实测] 数字 (Sprint-1 → Sprint-3 内闭环)

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| MQ-1 | 6 月 NBA + NFL + MLB Polymarket book snapshot 总数据量实测 | 小余 | 6/26 |
| MQ-2 | Pinnacle 历史数据可获取深度 (6 月 vs 1 年 vs 3 年) | 老李 + 老彭 | 6/19 |
| MQ-3 | ESPN PBP API rate limit + 历史回溯深度 | 小段 | 7/3 |
| MQ-4 | Goalserve inplay 比分 push 延迟分布 (与 P0-02 共享) | 小段 | 6/12 |
| MQ-5 | ONNX Runtime C++ 在我们部署机器上的推理 latency baseline | 我 + 老姜 | T+26 周 |
| MQ-6 | LightGBM 替代 lookup table 的 hit rate 增益是否真有 1-2% | 我 + 小蒋 | T+28 周 |
| MQ-7 | Feature store atomic snapshot API 的 < 5ms 拉取延迟实测 | 小田 + 我 | T+24 周 |

### 9.2 待 [咨询] 问题

| # | 问题 | 咨询对象 |
|---|---|---|
| MQ-8 | LightGBM vs XGBoost vs CatBoost 在我们数据规模下的差异 | 小董 (data-stats) |
| MQ-9 | Calibration 用 Platt scaling 还是 isotonic 对二元体育 hit rate 更好 | 小董 |
| MQ-10 | Pinnacle no-vig 多种方法 (multiplicative / Shin / power) 对 ML feature 的影响 | 老彭 |
| MQ-11 | 跨洋链路下 model hot reload 怎么做最安全 (rsync vs registry pull) | 老吴 + 老周 |
| MQ-12 | ONNX vs Treelite 在 LightGBM 推理上的实际 latency 对比 | 老姜 + 我 |
| MQ-13 | Sharp money 钱包识别要不要单独训一个分类器, 还是当 feature 用 | 老彭 + 小程 |

### 9.3 待 [战略] 决策

| # | 问题 | 决策人 |
|---|---|---|
| MQ-14 | M5 后是否真的解锁 ML scope, 还是再延 6 个月 (M11) | 老钱 + 老雷 |
| MQ-15 | ML 模型的下单决策权: 完全自动 vs 人工审 vs rule 兜底 | 老韩 + 老钱 |
| MQ-16 | ML 团队 headcount (M5 后是否要扩到 2 人 ML) | 老雷 |
| MQ-17 | ML 训练用 GPU 还是 CPU (LightGBM 是 CPU, DL 才要 GPU, 当前 scope 不要) | 老吴 + 我 |
| MQ-18 | 容量极限 (小梁 §7.4) 触及时 ML 是否能突破, 还是结构性上限 | 老雷 + 小梁 + 我 |

### 9.4 已知未知 (我无法回答, 但知道存在)

- Polymarket maker 的 quote 调整算法是否本身就是 ML, 我们 ML vs ML 竞争胜算如何
- UMA 仲裁规则变更对历史 label 的追溯影响 (label 可能被改, 我们的训练集需要 invalidate 多久数据)
- Polymarket 体育市场未来是否会引入 native ML / oracle 类产品, 影响我们 alpha 衰减速度
- 跨洋链路 stable 度对 ML 推理的实际影响 (老姜测出 p99 后才能定)
- 极端事件 (球员长期受伤 / 球队事件 / 罢工) 对 score_model 的 OOD impact

---

## 附录 A: 与其他 Sprint-1 文档的依赖关系

```
本文件 (xiaodeng-ml-roadmap)
  ├── 输入依赖 →
  │     ├── xiaoliang-market-structure-v1.md: 12 候选信号 + 容量上限
  │     ├── xiaocheng-signal-catalog-v1.md: 12 SIG-ID + 特征清单 F-01 ~ F-17
  │     ├── laoqian-mvp-scope-rejection-v1.md: ML scope 红线
  │     ├── laopeng-betting-industry-analysis-v1.md: Pinnacle no-vig + sharp money
  │     └── laoxu-external-tools-inventory-v1.md: Python .venv 工具栈
  │
  ├── 直接产出 →
  │     ├── 小余 (S1-D): 历史数据 schema + PIT correctness 要求
  │     ├── 小段 (S1-003): Goalserve + ESPN PBP 字段需求
  │     ├── 小冯: Polymarket book snapshot 频率分级
  │     ├── 小田: 实时 feature store API + F-18 ~ F-22
  │     ├── 小董: drift detection + A/B + label leakage 检查
  │     ├── 小蒋: walk-forward + ML candidate replay
  │     ├── 老姜: ML 推理 latency budget < 50ms p99
  │     ├── 老周: src/ml/ 子目录 + ONNX 依赖
  │     └── 老韩: model 解释性 + risk fallback
  │
  └── 等候反馈 →
        ├── 小梁验收 (是否同意 ML 路线对齐 v1 信号路线)
        ├── 老钱验收 (是否同意 paper trading ML shadow + M5 后实盘解锁)
        └── 老雷验收 (战略 MQ-14 ~ MQ-18)
```

## 附录 B: ML 不接受的 13 个反模式 (我会拒绝的 PR)

1. 在 prod C++ 进程里跑 Python (pybind11 embed)
2. ONNX 模型 > 100MB
3. 模型推理 latency p99 > 100ms (硬 cap)
4. 任何 unsupervised clustering 直出仓位 (老韩否决)
5. RL / bandit 算法在 live trading 决策路径
6. 模型不带 model_id 进生产
7. 训练数据没 PIT, 用 future leak 的 feature
8. 模型上线无 fallback rule
9. Feature 计算线上线下不一致 (用不同 codebase 算)
10. 不带 calibration 的概率直接当 Kelly 输入
11. Walk-forward 中作弊 (用 test set 的统计反过来 normalize train)
12. 模型 hot reload 没有 atomic swap (会读到半个模型)
13. 上线决策只看 IS, 不看 OOS

## 附录 C: 修订记录

| 版本 | 日期 | 修订 | 人 |
|---|---|---|---|
| v1.0 | 2026-05-28 | 初版, Wave-6 交付, MVP 期 0 行 ML 进 src | 小邓 |

---

**v1 收尾**. 本文件不是 MVP 阻塞物, 是 M5 后路线的 "提前 24 周给数据团队的需求清单". MVP 期 (T+0 → T+24) 我 0 代码进生产, 但数据团队 NOW 起照 §3 攒数据是关键. v2 在 T+12 周 (M3) 发布, 灌入 §3 实际进度 + 阶段 1 baseline notebook 雏形.

— 小邓, 2026-05-28
