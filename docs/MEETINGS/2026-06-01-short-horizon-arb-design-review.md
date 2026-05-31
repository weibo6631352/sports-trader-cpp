# 短时多窗口价格预测 → 套利信号引擎 — 设计评审 + 老板补充修正

> owner: 老雷 (GM) | last_review: 2026-06-01
> 评审: 9-agent workflow (量化微观结构/signal/ml/架构/性能/风控/数据/金融 + 老郭综合)
> 状态: CONDITIONAL-GO (Phase 0 双门禁未过不进实盘)

---

## §0 核心动机修正 (老板 2026-06-01 三条补充 — 评审后追加, 优先级高于下方原综合)

评审 BACKGROUND 基于"跨洋高延迟"前提。老板随后给了三条关键补充, **修正方案前提**:

### 补充1 — 标签效率是真正的核心动机 (比套利更根本)
老板: "锁定最终结算概率要等收盘, 训练数据获取效率太低。仅预测未来一段时间(如 1 分钟内)的结果, 信息获取就非常简单。"
- 结算标签: 每场比赛 **1 个**最终标签, 等几小时 → 极稀疏、滞后。
- 短窗 mid 标签: **每个 tick** 在 Δt 后自动产标签 (自监督), 一场几千样本 → 即时、稠密。
- **立为 §0 设计支柱**: 窗口选择的首要驱动从"套利窗口大小"改为"标签密度 × 可执行性"。稠密即时标签还解锁 online/持续再训练 (制度切换快速适应)。

### 补充2 — 就近部署, 跨洋链路非约束 (推翻评审头号风险前提)
老板: "跨洋延迟不用担心, 服务器已经买好了 (就近数据中心)。"
- 评审 §1.1 端到端预算里 "WSS 跨洋 ~400ms + REST 下单跨洋 ~600ms" 这部分**大幅压缩**。
- **但诚实纠正**: 延迟大头未必是网络。Polymarket REST 下单 + 成交确认的**真实**延迟仍未知 —— 评审引的 `paper_signer kBlockTimeNs=2s` 是 **paper 模拟的 block time, 不是实测 taker 成交确认**。真实可能快得多 (off-chain 撮合) 或仍有数百 ms。**就近消除了"跨洋"那部分, 但端到端真实延迟必须 G1 实测才能定窗口下界。**

### 补充3 — 窗口推荐相应放宽
- 就近部署下, 窗口下界从评审的 **30s 可下探** —— 但下探到多少由 G1 实测的端到端 (含成交确认) p99 决定, 不是拍脑袋。
- 评审的"3s/5s 永久排除实盘"在就近部署下改为 **"待 G1 实测后定"** (网络不再是杀手, 但成交确认延迟可能仍排除 3s/5s)。
- 老板的 **~1 分钟窗口** 是最安全候选 (任何合理成交确认延迟都占比极小) + 标签仍高效 (分钟级 vs 小时级)。
- 首期采集 {15, 30, 60}s 三窗标签, G1 实测后定哪个上实盘。

### 不受影响、全部保留的评审结论
LightGBM 多输出起步 (非 NN — ring 序列样本稀疏, 与延迟无关) / Phase 0 先证伪再建设 / 回归打底 + CI 下界过滤 / 套利专用 Kelly + RJ-ARB 拒单码 + open-leg fail-safe / book-event 落帧 (G3) / 80% 技术栈复用。这些结论与延迟前提无关, 原样有效。

### 修正后 Phase 0 G1 (替换原"跨洋 RTT 体检")
G1 = 实测**就近部署的端到端延迟** (WSS 接收 → 特征 → 推理 → 下单 → **Polymarket 成交确认**), 取 p50/p99, 定窗口下界。这仍是上任何模型前的 go/no-go 闸 (G2 net-of-cost 正期望依赖它)。

---

## §0.1 触发源 + 多窗口网格 (老板 2026-06-01 二次补充)

### 触发源 = WSS book 更新 ∪ Goalserve 比赛事件 (进球/比分变动) — 后者是核心 alpha
老板: "触发不只是 WS 订阅, 还有直播源的事件, 比如进球、比分变动。"
- 原方案 §5.1 触发 = `OrderBookSnapshotHub::Publish` (PM book 更新)。**补充: ScoreSnapshotStore 更新 (进球/比分变动) 也触发决策。**
- **进球触发 = 信息优势套利, 大概率是最强 alpha 来源**: Goalserve 直播源先于 PM 散户市场知道进球 → sharp fair 瞬间跳变, PM mid 滞后几秒~几十秒收敛 → 这个 gap = 套利窗口。我们抢在散户反应【之前】(因, 非果)。
- **与刚加的 `x_inplay_fair_minus_mid` (102-103) 完美咬合**: 进球瞬间 sharp-vs-market gap 最大, 该特征直接是触发信号 + 方向。
- 实现: score store 更新检测 score_diff 跳变 → push 该 condition 进决策队列 (同 §5.1 book-event notify 范式; score 注入 `SetScoreStore`/`ts_history` paper_loop.cpp:331 已存)。R-12 守: notify 端只 push+notify。
- 套利升级为【微结构 + 信息优势】双引擎; 信息优势侧可验证性更强 (进球离散可标注)。

### 多窗口网格 — 短期密集, 越往后越稀疏 (老板)
老板: "更关注短期, 短期内时间稍密一点: 2s/3s/5s/10s/12s/15s/30s/1min, 越往后越稀疏。延时很低。"
- **预测 horizon 网格 = {2, 3, 5, 10, 12, 15, 30, 60}s** (近似对数间隔, 短期密集长期稀疏)。
- 模型 = **8-horizon 多输出** (LightGBM 多 head, 共享 104 列输入), 每 tick 造 8 个标签 (mid_{t+Δ} − mid_t) → 标签效率极高 (每 tick 8 标签 × book 帧密度)。
- 延迟低 → **2s/3s 短窗回到桌上** (评审排除 3s/5s 是基于跨洋延迟, 已失效); 实盘上哪些 horizon 由 G1 实测端到端 (含成交确认) + G2 各 horizon 净期望逐一决定。全 8 窗都采集标签 (标签便宜), 实盘按"net-of-cost 正期望 ∧ 端到端延迟 < horizon×安全系数"筛选放行。
- 进球事件后单独按 horizon 看 mid 收敛曲线 (进球后 2s/5s/15s/30s 各收敛多少) → 定进球套利的最优 entry/exit horizon。

### G2 体检聚焦进球事件 + 逐 horizon (修正)
- 原 G2 = 通用多窗净期望。**修正: 优先验证"进球/比分变动后, PM mid 在每个 horizon {2..60}s 的移动分布"** — 进球离散可标注 (g_goal_freshness / score_diff 跳变), 验证最干净: 逐 horizon 算扣成本后正期望 + 收敛速度, 直接定哪些窗口实盘放行。

---

## §0.2 事件时钟 vs 墙钟 — 触发不规律, 标签该按事件不按秒 (老板 2026-06-01 三次补充)

老板: "时间好像也不是固定的, 因为每个事件触发的又不是我们轮询的, 不会那么严格遵照这个时间线。"

老板点中本质张力: 触发是【事件驱动】(进球/book 更新, 到达不均匀), 非轮询, 故固定墙钟网格 {2..60}s 是近似, 不是市场真实节奏。两层处理:

### 第一层 — 触发不均匀 ∧ 墙钟标签可并存 (as-of 语义)
- 触发时刻 t = 事件到达 (不均匀)。**每次事件 = 一个训练样本** (非定时采样)。
- 墙钟标签 = mid(t+Δ 时刻的【最新 book】) − mid(t), as-of-forward 取 t+Δ 后第一个真实 mid, **中间无事件即平的, 不插值** (老郭 §7.1 已这么设计; gap>Δ 标 label_stale 降权)。
- "不严格遵照时间线"恰是 as-of 语义在处理 —— 不假设每 Δ 秒都有更新。

### 第二层 — 更贴合: 加【事件时钟】维度 (信息按事件流动, 非按秒)
- 微结构铁律: 信息按【事件】流动 (成交/book 更新), 不按秒。进球后 mid 收敛速度取决于【发生多少笔成交/更新】, 非过了多少秒 — 活跃则 5s 收敛, 冷清则 30s 没动。墙钟网格模糊这点。
- **加事件钟标签**: mid(t 之后第 N 个事件) − mid(t), N ∈ {1,2,5,10,20,50} 事件 (book 更新数 / 成交笔数)。
- **双时钟并采**: 墙钟 {2..60}s + 事件钟 {1..50}事件。**G2 回测对比哪个预测力强 / 净期望高** (预期: 事件钟活跃期更准, 墙钟跨事件对齐更稳)。
- 最终形态: **事件钟做主预测 horizon (信号), 墙钟做执行 deadline** (RJ-ARB-5 延迟红线 + open-leg timeout 仍是墙钟 —— 因为成交确认/资金占用是墙钟)。

### 对触发架构的确认 (坐实 §0.1 / §5.1)
- 决策【只在事件发生时触发】(进球 + book 更新 notify), **不在 500ms tick 空转** (tick 仅 liveness 兜底)。每个触发 = 一个样本 + 一次预测。事件驱动是这套引擎的物理基础, 不是优化项。

---

# 以下为 8 维度评审 + 老郭综合原文 (前提以 §0 修正为准)

三处关键事实核对完毕, 与各维度的转述有几处出入需要在裁决里点名纠正:

1. **fv_recorder 是 5s 轮询 (`poll_interval_sec=5`, line 33), 落盘列数注释写 "75 列 / f0..f85", 不是 104。** 多个维度说 "已落 as_of_ts + 完整向量 + mid" 大体对, 但要点名: 它是 SnapshotAll 轮询去重 (line 72-77), 不是 book-event 触发。这印证了 signal/perf 维度 "落帧频率是前提门禁" 的判断。
2. **FeatureHistory ring 已存 6 字段含 best_ask/best_ask_size (line 49-50), 单调去重门 line 61 确实 PIT-safe。** arch 维度说的 "ring 拍平成序列 tensor" 字段基础属实, 但 line 40 注释明确 "book 更新 ~1-5s/次", 128 cap 覆盖 2-10min —— 短窗序列样本稀疏是硬约束, 不是假设。
3. **trade_flow `snapshot(now_ns, window_ns)` 确实参数化 (line 43-44), 缩窗到 30s/10s 是纯参数改动, 零结构改。** 各维度此点属实。

下面出统一方案。

---

# 统一架构方案 — 短时多窗口价格预测 → 套利信号引擎

> 评审主持: 老郭 (架构 second opinion)
> 输入: 8 维度评审 (小袁/signal/ml/arch/perf/risk/data/fin) + 老郭 file:line 复核
> 日期: 2026-06-01 | 状态: **CONDITIONAL-GO** (带硬门禁)
> 红线复核: 本方案所有改动归类见 §8.1 carve-out / R-12 / R-20 / BR-1 / ML-R8

---

## 1. 可行性总判

### 1.1 跨洋延迟是否致命 — 不致命, 但**杀死 3s/5s 窗口**

8 个维度**全部独立**把跨洋延迟列为头号风险, 这不是巧合, 是结构性事实。perf 维度给了唯一一份完整端到端预算 (我采信, 因为它逐阶段拆且引了 `paper_signer.hpp:61 kBlockTimeNs=2s` 实证):

```
信号产生→订单确认 端到端:  p50 ≈ 800ms-1.05s   p99 ≈ 3.5-4s
其中不可压缩的硬约束:
  - WSS 跨洋单程 ingestion    p99 ~400ms
  - REST 下单跨洋单程         p99 ~600ms (Polymarket CLOB 无 WSS 下单, 硬约束)
  - CLOB 撮合确认             p50 500ms / p99 2s
  - 500ms tick 等待           期望 250ms (可消除, 见 §5)
```

**裁决: 跨洋延迟不是 go/no-go 的否决项, 而是 window 下界的决定项。** 它吃掉的是固定的几百 ms~数 s, 在 30s+ 窗口里占 3-13%, 可控; 在 3s 窗口里占 30-130%, 套利窗口在订单到达前就关闭。

### 1.2 最短可行窗口下界 — **MVP 锁 30s, 实测后可能下探 10s, 3s 永久排除 (除非 co-location)**

这是全场最大分歧点, 我点名仲裁:

| 维度 | 主张窗口下界 | 依据 |
|---|---|---|
| 小袁 | 30s | RTT 占 30s 仅 2-3% |
| signal | 10s (MVP), 3s 仅采集 | 延迟余量 |
| perf | **30s (当前架构最小可行)**, 10s 需 100ms tick+就近 | 完整 p99 预算 |
| fin | T≥10s 才批, 3s/5s 预研 | BE+延迟漂移惩罚 |
| risk | RM 只能 enforce 下界保护, 不能创造 alpha | STALE_PREDICTION |

**仲裁结论:**
- **MVP 训练+实盘窗口下界 = 30s。** perf 的完整 p99 预算最硬, 30s 给 500ms-tick + 跨洋的容忍空间足够。signal 的 "10s MVP" 过于乐观 —— 它假设 tick 已改事件驱动, 但那是 §5 的改造项, 未完成前 10s 不成立。
- **10s = conditional, 解锁条件有二且**: ① tick 改 WSS 事件驱动 (§5); ② 从生产 fv.jsonl 提取 `b_ingestion_lag_ms` (feature 79) 实测 p99 RTT 分布 < 1s。两者都满足才放 10s 实盘。
- **3s/5s = 永久排除实盘 (当前部署), 仅离线采集标签做研究。** fin/perf/risk 三方一致, 我背书。解锁唯一路径 = co-location (CLAUDE.md 未列, 属公司级决策, 不在本方案范围)。
- **首期同时采集 {10s, 30s, 60s} 三窗标签**, 30s 上实盘, 10s 备用, 60s 作稳健兜底。3s/5s 也落盘但 `arb_tradeable=0` 标记, 只喂研究不路由。

### 1.3 整体 go/no-go — **CONDITIONAL-GO**

放行, 但**三道硬门禁未过不许进实盘** (任一不过, 引擎停在 advisory):

1. **G1 (延迟体检, 派量化+工程):** 从生产 fv.jsonl 提 `b_ingestion_lag_ms` p50/p99 实测分布。**这是上任何模型之前的 go/no-go 闸** —— arch/perf/fin/risk 四方都点名要求。没有实测 RTT, 盈亏平衡模型是空中楼阁。
2. **G2 (可预测性体检, 派量化):** 用现有 fv.jsonl 离线构造多窗口 mid 标签, 验证 "**扣掉 G1 实测端到端延迟 + 往返 fee + spread 后**, 30s 窗口 Δmid 是否仍正期望"。net-of-cost 标签为负 = 这条路本身不成立, 立即停。
3. **G3 (落帧门禁, 派工程):** fv 采集从 5s 轮询改为 WSS book-event 触发 (§5/§7)。当前 5s 粒度 (`feature_vector_recorder.hpp:33` 已复核) **物理上造不出 ≤30s 的密集标签**, 这是 signal/data 两方点名的前提门禁。

G1+G2 可在现有数据上**两周内**出结论, 不需写任何热路径代码。**先证伪, 再建设。**

---

## 2. 预测目标 & 标签管道

### 2.1 预测目标 — **方向分类为主 + 净幅度 CI 下界过滤, 不用 triple-barrier (首期)**

分歧仲裁:

| 维度 | 主张 | 
|---|---|
| signal | 方向分类 dir + tradeable 布尔 |
| ml | 4-horizon Δmid 回归 |
| fin | 回归 y_ret_Δ + 信号质量用 CI 下界 |
| arch | forward-fill Δmid 多列 |

**仲裁: 双层结构, 回归打底 + 方向决策。**
- **训练目标 = 回归 `y_ret_Δ = mid(as_of+Δ) − mid(as_of)`** (ml/fin/arch 一致, 回归保留套利幅度量纲, 直接可算 BE 是否够本)。分类会丢掉 "动多少" 的信息, 而 BE 地板判断需要幅度。
- **决策层 = 方向 + 净幅度 CI 下界过滤** (fin 主权): 信号触发条件 = `预测位移 CI 下界 (raw − z·σ/√n_eff) − BE > 0`。**点估计严禁直接进 sizing** (fin 红线, 我背书: 微利高频靠点估计必死)。复用 `edge_ci.hpp:16 ComputeEdgeCiLower` 范式, BR-1 回测实盘共用。
- **triple-barrier 推迟到 v2.5** (signal/ml 一致), 需帧级时序完整, 落帧改造 (G3) 完成后再建。
- **样本不平衡处理 (signal 点名):** 静态比分期 dir=0 占 70-85%, label 构造时过滤 `|y_ret| < min_move_bps` 的 0 类或类权重欠采样, 否则模型退化为永远预测不动。

### 2.2 窗口 — **固定多窗 {10,30,60}s 打底 + 事件 bucket 标记, 不做纯事件驱动**

signal 提的 "双轨" 我采纳但收口: 固定窗口供基础训练 (样本多), 进球/OFI 突变样本打 `event_triggered=1` 标记作类别特征 (复用 `g_goal_freshness` paper_loop.cpp:625)。**纯 volume-clock 事件驱动否决** —— Polymarket 体育成交稀疏 (data 维度: book ~1-5s/次), 撑不起 volume clock。

---

## 3. 模型选型

### 3.1 明确结论 — **LightGBM 多输出 (per-horizon) 起步, NN 一律推迟到 v2.5 挑战者, 且必须先过采样频率体检**

这是 ml/arch/perf 三方**高度一致**的结论, 我强背书并给最硬的那条理由:

**ring 实际序列样本只有 1-10 个 (复核 `feature_history.hpp:40` 注释 "book 更新 ~1-5s/次" + `:42` cap 128), 一个 10s lookback 窗口实际 2-10 个不等间隔含 NaN 的样本 (`:57` observe-always 保留无 bid 样本)。LSTM/TCN/Transformer 的价值在长序列 (50-200 步) 时序依赖, 喂 2-10 个稀疏样本 = NN 优势归零 + 过拟合 + 推理延迟翻倍。这是采样频率决定的硬约束, 调参救不了。**

- **首发 = LightGBM 4-horizon 多输出** (共享 104 列输入 extract_full, 4 棵树或 MultiOutputRegressor → ONNX 4 head)。理由: FeatureHistory 已把序列信息压成截面特征 (RateOfChangePerSec/RealizedVol/OFI, `feature_history.hpp:85-241`), 等于人工做完了 NN 该学的 temporal encoding。微结构短时预测 "OFI + signed flow + 多窗 ROC + realized vol → GBM" 是文献+实盘双重验证的强基线。
- **简单逻辑回归/线性做 sanity baseline** (ml 点名), 不过它就别谈 GBM 增益。
- **NN (TCN 优先于 Transformer) = v2.5 挑战者**, 必须证明在**真实采样频率下** walk-forward 净胜 GBM 才上。arch 提的 "先 D (特征化喂 GBM) 后 A (TCN)" 我采纳为路径。Transformer 直接排除 (attention 算子在跨洋紧带宽 CPU 节点延迟方差大, arch/perf 一致)。
- **online learning v2 不上** (ml 主权): 跨洋高延迟 + paper 期样本不足下漂移 > 收益。drift 用现成 `b_vol_ratio` (model_feature_spec.hpp:115, 短/长窗 vol 比 = 天然 regime 探测器) 触发周级 retrain 告警。

### 3.2 训练→ONNX→C++ 推理路径 — **复用已就绪的 OnnxFairValueModel, 不造新 runtime**

arch 维度核对到一个关键事实, 我采信并放大: **`fair_value_model.cpp:41-135` 的 Ort::Session 推理已写完整 (动态 IO 名/softmax/fail-closed try-catch), 当前只是 `STCPP_ONNX_ENABLED` 没开 + libonnxruntime 没装 (`:155` 返 nullptr fallback stub)。第一步是装机 + 开编译开关, 不是写新代码。**

- **新增 ISeqArbModel 接口与 FairValueModel 平行**, 复用同一 Ort::Session 封装。输出从 per-outcome prob 扩成 per-horizon {Δmid, ci_low, ci_high, confidence} (`ModelPrediction` append-safe, `fair_value_model.hpp:117`)。
- **延迟预算 (perf 维度, 我采信):** LightGBM via **Treelite** (编译 .so) p99 <1ms; ONNX GBM p99 1-3ms (`intra_op_threads=1`, `fair_value_model.hpp:327`)。**首选 Treelite 路径** (`ModelKind::Treelite` 已预留 `:158`)。LSTM/TCN p99 10-50ms = 危险, benchmark 未过 <2ms p99 **禁入热路径**。
- **多输出 ONNX 导出验证 (ml 点名):** 4-head 导出需验 C++ session 输出顺序与训练锁定一致 (列序锁同精神, `model_feature_spec.hpp:21`)。treelite 多输出回归支持需老吴确认。

---

## 4. 特征

### 4.1 复用现有 (零新增即可跑基线)

全部已在 104 列契约内, 训练侧直接用:
- **多窗动量:** `b_mp_roc_per_sec` (24) / `b_mp_roc_30s` (32) / `b_mp_roc_5m` (33)
- **OFI / 微价压力:** `b_ofi` (30) / `x_microprice_minus_mid` (17)
- **L2-L5 深度分布:** `b_bid_depth_5lvl`/`b_ask_depth_5lvl` (86-93) / `b_l1_concentration` / `depth_imbalance_5lvl` (via `compute_depth_metrics`, `orderbook_snapshot_hub.hpp:180-208`)
- **trade-flow:** `b_trade_signed_vol_5m` 等 (96-101)
- **inplay-vs-market:** `x_inplay_fair_minus_mid` (102-103)
- **realized vol:** 延迟漂移惩罚的输入 (fin 维度: `Δmid* += vol·√latency`)

### 4.2 必须新增 (append-only, bump kSpecVersion) — **3-10s 套利尺度短窗特征**

ml/arch/小袁一致指出: 现 spec 只有 per_sec/30s/5m, **缺 3-10s 套利尺度**。新增 (走 append-only 锁 `model_feature_spec.hpp:70` + bump version):
1. **短窗 OFI** `b_short_ofi` (1-tick / 5s 队列净变化, 比 5min 快)
2. **L1 ask 衰竭率** `b_l1_ask_depletion_rate` (asks[0].size 连续两 tick 差/Δt, 从 ring `best_ask_size` 字段派生 —— 复核 `feature_history.hpp:50` 字段已在, 零新数据源)
3. **深度变化率** `b_depth_change_rate` (depth_imbalance_5lvl 方向性变化速度)
4. **短窗 trade-flow** (trade_flow `snapshot(now, 30s/10s)` 参数化即得, 复核 `:43-44` 零结构改, 但 QuoteFeatures 载体需新增字段承载短窗值)

### 4.3 砍掉的噪声 / 老板草案纠正

- **Kyle lambda 暂不列** (小袁): 需匹配成交量与价格冲击序列, trade_flow 刚接入, 数据基建未打通。v2.5 再说。
- **5min trade-flow 不作短时主触发** (小袁/signal): 窗口太慢, 仅辅助确认。
- **老板 "fee 3% = 3¢" 是误解, 必须纠正 (fin 维度, 我点名背书):** `sizing_calculator.hpp:157` 已是 Polymarket 官方公式 `fee = coef·p·(1−p)`, 体育 coef=0.03, p=0.5 时 fee=**0.75¢/share** 单边, 往返 ~1.5¢。**盈亏平衡 BE ≈ 2.8-3.3¢** (fee 往返 1.5¢ + spread 往返 1¢ + slip 0.3-0.8¢)。**预测幅度门槛 ≥ 3¢ (300bps) 才进信号池**, 低于此不论置信度多高都不发单。老板例子 0.52→0.58 (动 6¢) 够本有余, 但动 2¢ 是净亏。

---

## 5. 系统架构

### 5.1 事件驱动触发 — **tick 兜底 + WSS 事件唤醒双轨, 不删 tick**

arch 维度给的方案最完整且守住 R-12, 我采信并定为正式架构:
- **`OrderBookSnapshotHub::Publish` (`:246`, WSS vCPU0 热路径) swap 成功后, 无锁 SPSC ring push token_idx + cv.notify_one** (<1us, 守 R-12 100us 红线), 唤醒决策线程**只处理被触动的 condition** (非全表轮询)。
- **500ms tick 保留为 liveness 兜底 + 组合权益采样** (`paper_loop.cpp:276`)。
- **R-12 红线死守 (risk/arch/我三方强调):** Publish 端**只做 push token_idx + notify, 绝不碰 RM/推理/IO/锁**。重活全在决策线程。**此处 code review 必经我 + 老韩** —— notify 写脏 (误加锁/误做推理) 直接踩 R-12 = P0。
- 跨线程原语具体实现派高频系统工程师 (老陈/小赵), 我定边界。perf 否决 "100ms tick" 中间态 (仍是 30s 窗口 1/30 钝边 + 全表轮询浪费 CPU), 我背书直接上事件驱动。

### 5.2 新旧模型共存 — **双 estimator 物理隔离, 严禁 blend**

arch/ml/risk 三方一致, 这是有**现成血的教训** (`paper_loop.cpp:651` 注释: moneyline ONNX 不可 blend 进 derivative 盘口):
- **PaperLoop 新增 `SetSeqArbModel(ISeqArbModel*)` 与现 `SetMlModel` (`paper_loop.hpp:361`) 并列。**
- 短时 Δmid 信号走**独立分支**产 `ArbSignal{predicted_dmid, ci_low, confidence, horizon_ns, target_exit_px, liquidity_depth}`, **不进 SelectSide/edge_ci/Kelly 结算链**。
- 两套各自 advisory publish 进 quote_hub 不同字段, **靠 code 边界物理隔离, 不靠自觉**。
- 共用同源特征 (extract_full 104 列 + ring), 满足 BR-1。
- **v2 首发锁单盘口 (moneyline)** (ml 点名): 混盘口训练会污染, totals/spreads 等 `cat_market_type` (model_feature_spec.hpp:187) 真接通再扩。

### 5.3 热路径推理预算 (硬上限)

```
WSS Publish notify        < 1us    (R-12 死守)
特征 extract_full         < 0.5ms  (104 列纯 C++ 内联)
Treelite GBM 推理         < 1ms    p99 (首选)
决策 + RM 评估            < 1ms    p99
构造 OrderIntent + Sign   < 1ms
───────────────────────────────
本地决策段总计            < 4ms    p99  (跨洋 RTT 另计, §1.1)
```
LSTM/TCN benchmark 未过 <2ms p99 **禁入**。

---

## 6. 风控范式

risk 维度 (老韩团队) 给的最系统, 我整体采信并定为正式范式。核心: **RiskGateway 的 "必经+audit+fail-closed+短路链" 骨架整体复用, 新增 taker-arb 平行约束链** (全是 §8.1 carve-out 加性扩展, 走普通 PR review, 不需架构会签 —— 我确认归类正确)。

### 6.1 套利专用 Kelly — 不复用结算 Kelly (risk + fin 一致, 我背书)

- **现 `sizing_calculator.hpp:103` 的 `f*=net_ci_edge/(1-c)` 是结算赔付 Kelly (赢赔 $1), 对 "预测 mid 移动" 下注结构上错。**
- 新 `SizingArbCalculator` 并存: `f* = edge_arb/σ²_Δmid` (连续型 Kelly), `edge_arb = E[Δmid]−fee_roundtrip−E[adverse_slip]`, 分母用预测误差方差 (来自模型 CI)。
- **λ (Kelly 分数) 从 0.25 降到 0.10 起步** (fin 主权拍板 + 老韩 RM 联签): 微利高频估计误差占边际利润比例远高于结算策略。稳定 (>2 周实盘胜率验证) 后升 0.15。

### 6.2 新增拒单码 (RJ-ARB-1..5) + evaluate() 第 6.5 档 (caps 后 / liquidity 前)

沿用 `reject_enum.hpp:65` 扩展位模式, 不动 21 active 契约:
- **RJ-ARB-1 PRED_CI_CROSSES_ZERO** (预测 CI 下界跨 entry → 方向不确定拒)
- **RJ-ARB-2 EXIT_DEPTH_INSUFFICIENT** (L1-L5 累计深度 < 预期平仓量 → **套利第一杀手 "进得去出不来"**)
- **RJ-ARB-3 ROUND_TRIP_BUDGET_EXCEEDED** (in-flight 未平腿超并发上限)
- **RJ-ARB-4 HORIZON_BUDGET_EXCEEDED** (窗口内累计 taker 敞口超 cap)
- **RJ-ARB-5 STALE_PREDICTION** (pred_gen_ts→order_ts 超 horizon×0.5 → 跨洋延迟吃掉窗口 → 拒)

**RJ-ARB-5 是可行性核心的 RM enforce 点** (老板点名延迟): RM 必须有独立于策略的延迟红线, 预测年龄 + 预估 RTT > 窗口一半 → expected_edge 已侵蚀过半, 直接拒。需策略层把 `pred_gen_ts` 透传进 intent。

**RJ-ARB-2 纠正现 RM 不足:** `check_liquidity_` (`risk_gateway.cpp:611`) 只喂 `book_depth_l1_usdc` (仅 L1), 套利平仓需 L1-L5 累计深度。复用 `compute_depth_metrics` 的 `l1_concentration` 检 "L1 撑门面后断档"。

### 6.3 fail-safe = 强制平仓 timeout (现 RM 完全缺失的维度)

risk 维度核对到关键事实: **`evaluate()` 是单次开仓 gate, 全文件无 exit/timeout/unwind 概念。** 套利单进场即背 "horizon 内必须平掉" 的义务:
- 新增 **OpenArbLeg 台账** (token_id, entry_px, entry_ts, target_exit_px, deadline_ts) + **非热路径 sweeper**: 到 deadline 未平 → 自动市价平仓 (吃 bid 止损)。
- **预测源 stale / ONNX 推理超时 / book 断流 → 不仅拒新开, 还把所有 in-horizon open leg 标记 "立即市价平"** (预测失效后继续持有 = 裸方向暴露, 违背套利初衷)。复用 `RmState` 五档 SAFE_MODE (放平仓不放开仓)。

### 6.4 DD 熔断细化 — 日 PnL → 滚动窗 + 连错方向双轨

现 `-3%/-5%` 日熔断对高频抢单太粗 (日内数百单, 触发太晚)。新增三轨任一降级: ① 滚动 N 单胜率熔断; ② 连续反向移动熔断 (复用 `consec_loss_` `:601` 语义改); ③ 平仓滑点熔断 (累计实际滑点 vs 预期 > 阈值 = 模型高估流动性)。

### 6.5 其他点名

- **幂等带 TTL** (risk): 现 `seen_signal_ids` 只增不清 (`:421`), 3s 级数百单/分钟会无限膨胀, 需按 horizon 过期。
- **相关性聚合** (fin): 同 condition YES/NO 双边 + 同赛事多盘口高度相关, VaR 按相关性聚合, 不能当独立。
- **STRATEGY_DECAYED 阈值重校准** (risk/fin): `strategy_decay_min_ev_ratio=0.3` 是为结算策略调的, 套利 EV 分布不同, 量化定阈值 RM enforce。

---

## 7. 数据管道

data 维度 (小余团队) 给的最扎实, feasibility 标 **high** (全场唯一非 medium), 我采信。核心: **全部复用现有 4ts 链 + b_mid/b_microprice 列, 不引新数据源。**

### 7.1 三层

- **层1 — MidTapeRecorder (新增, 与 fv_recorder 解耦):** 挂 `OrderBookSnapshotHub` 每次 book 更新事件 (**不是 5s 轮询** —— 这就是 G3), 每 book 帧落一行 mid tape, 时间锚强制 `data_source_ts_ns` (R-20, 禁 now())。YES/NO 各一 tape (复用 `ts_history_`/`ts_history_no_` 双边结构, `paper_loop.cpp:331/344`)。落盘走 fv_recorder 同款独立线程 JSONL append (IO 离决策线程, R-12)。
- **层2 — MidLabelJoiner (离线, 改造而非删 label_pipeline):** 对每个 fv 样本行 (t=as_of, cid), 在 mid tape 上做 **as-of-forward 查找** (取 t+Δ 后第一个真实 mid, **不插值** —— 插值=造伪标签), 产 `fwd_mid_{10,30,60}s` + `y_ret_Δ`。复用 `ExtractConditionId` (`label_pipeline.hpp:70`) + JSONL 流式 join, 时间维换 merge-asof (DuckDB/polars 离线, §12.4 允许)。**现结算 label_pipeline 不删, 两套并存** (结算标签另有用途)。
- **层3 — Δt 对齐:** book 事件不均匀 (1-5s/次), 落 `fill_gap_ns` 元数据, gap > Δ 的样本标 `label_stale`, 训练侧 drop/降权。**不均匀 tick 显式量化, 不静默吞。**

### 7.2 PIT 安全 (铁律)

- 层1 tape 与层2 fwd_mid 是**两套物理文件**, 标签只在离线 join 阶段写入 X 行, **绝不回灌特征** (沿用 `label_pipeline.hpp:11-13` CLV/前视红线 + ML-R8)。
- fv.jsonl 特征 b_mid 永远只用 ts≤as_of 的 book (`feature_history.hpp:61` 单调门已 PIT-safe, 我复核确认)。
- **join 工具对 `fwd_/y_` 前缀列做 X-leak 断言剔除** (ml/data 点名: 这是回测金光实盘亏穿的命门), 标签管道单测覆盖 "as_of 后的行绝不进 X"。

### 7.3 存储 (data 实测基准)

- 实测 `quotes.jsonl` 18313 行/193 conditions/8.7MB (~475B/行)。mid tape 行更窄 (~150B) 频率更高 → 满盘口 ~数百万行/天, JSONL ~数百MB-1GB/天, Parquet 列存压缩后 ~50-150MB/天。
- **带宽紧约束: 训练数据本地落盘后离线 batch 转 Parquet 分区, 不跨洋传 raw。** 热路径不引 Parquet 写依赖 (避 R-12)。
- **同时落 ingestion_ts** 供离线算 RTT 分布 (= G1 体检数据源)。

---

## 8. 落地路线 (分阶段, 第一个可证伪里程碑)

### Phase 0 — 双门禁体检 (两周, 不写热路径代码) ★ 第一个可证伪里程碑

> **可证伪点: G2 — 扣掉实测端到端延迟 + 往返 fee + spread 后, 30s 窗口净 Δmid 是否仍正期望。为负 = 这条路不成立, 立即停, 不进 Phase 1。**

- **G1 延迟体检** (量化+工程): 从生产 fv.jsonl 提 `b_ingestion_lag_ms` (feature 79) p50/p99 实测 RTT 分布。
- **G2 可预测性体检** (量化): 现有 fv.jsonl 离线构造 {10,30,60}s mid 标签, 算 net-of-cost (扣 G1 延迟 + 1.5¢ fee + spread) 正期望验证。
- 交付: go/no-go 报告进 `docs/MEETINGS/`。**G2 不过 → 全案停。**

### Phase 1 — 落帧改造 + 数据管道 (G3)

- MidTapeRecorder (book-event 触发) + MidLabelJoiner (离线) + X-leak 断言单测。
- fv 采集从 5s 轮询改 WSS 事件触发。
- 交付: 密集多窗标签训练集就绪, PIT 单测全绿。

### Phase 2 — 基线模型 + 推理打通

- 装 libonnxruntime + 开 `STCPP_ONNX_ENABLED`; LightGBM 4-horizon → Treelite/ONNX 导出。
- ISeqArbModel 接口并列注入; walk-forward 验证净胜简单 baseline。
- benchmark 推理 p99 (Treelite <1ms 验证)。
- 交付: 30s 窗口 walk-forward 净期望 > 0 + 推理预算达标。

### Phase 3 — 事件驱动触发 + 风控范式

- WSS SPSC ring + cv.notify 事件唤醒 (R-12 review 必经老郭+老韩)。
- SizingArbCalculator + RJ-ARB-1..5 + OpenArbLeg 台账 + sweeper + DD 三轨熔断。
- 交付: 全链路 advisory 跑通, RM 拒单审计完整。

### Phase 4 — Paper 实盘 (advisory, ML-R2 已放开 paper 期 ML 决策)

- 30s 窗口 paper 模式跑, 监控净利/fee 比 > 1.5 (fin 上线闸)。
- LiveOrderGate 仍 disarmed (真钱开闸需老韩+小白会签, §8.1)。
- 交付: paper 期 net-of-cost 正期望 + 胜率 >60% (fin Sharpe 前提)。

### v2.5 候选 (不进 MVP)

TCN 序列模型挑战者 / triple-barrier / online learning / 多盘口扩展 / 10s 窗口 (G1+G3 满足后) / Kyle lambda。

---

## 9. 关键未决 & 风险 (诚实)

### 9.1 维度分歧的最终裁决记录

| 分歧 | 各方 | 老郭裁决 |
|---|---|---|
| 最短窗口下界 | 小袁 30s / signal 10s / perf 30s / fin ≥10s | **MVP 30s; 10s 待 G1+G3; 3s/5s 永久排除实盘** |
| 预测目标 | signal 分类 / ml·fin 回归 | **回归打底 + 方向决策 + CI 下界过滤** |
| 模型 | 全员 GBM 起步, NN 推迟 | **LightGBM 多输出, NN→v2.5 (无分歧, 强背书)** |
| Kelly | risk·fin 新连续型 + λ=0.10 | **采信, 老韩 RM 联签** |
| 数据 feasibility | data=high, 余=medium | **data 侧确实最成熟, 但卡在 G3 落帧门禁** |

### 9.2 诚实标注的盲点 (无人能在 Phase 0 前回答)

1. **G2 可能直接证伪全案。** 没有任何维度敢拍胸脯说 net-of-cost 30s Δmid 正期望 —— fin 明说 "BE 模型是空中楼阁, 要 RTT 才能定死延迟惩罚系数"。这是诚实的 conditional, 不是包装过的 go。
2. **真实 book 更新频率分布未实测** (arch/ml/perf 全点名): "1-5s/次" 是 `feature_history.hpp:40` 注释, 不是测量值。若实际更稀, 连 30s 窗口都喂不饱特征。**Phase 0 应附带实测 book 更新频率**, 我加进 G1。
3. **容量天花板** (fin): PM 体育盘口浅, 单票容量几百刀级。$5M 年化必须靠盘口数量横向铺开, 不能单票加杠杆 —— 这是商业可行性而非技术, 归老钱/老雷, 但工程上意味着引擎要能并发管理大量 condition 的 open leg, 台账设计要可扩展。
4. **校准失效风险** (fin): `fair_value_model.hpp:236` confidence 当前 stub 恒 0。真 conformal/isotonic 校准是上线前提, 不是可选。裸 softmax 进 Kelly = 高频连环踩空爆仓。
5. **多输出 ONNX/Treelite 导出未验证** (ml): 4-head 输出顺序锁定 + treelite 多输出回归支持需老吴 Phase 2 确认, 有返工风险。

### 9.3 红线合规确认 (老郭归类)

- **§8.1 carve-out:** RM 加性扩展 (新 check_ / 新拒单码 / struct 末尾加字段) + ML append-only 特征 = 走普通 PR review, **不需架构会签**。MidTapeRecorder/MidLabelJoiner 是新模块同理。
- **需会签的只有:** 真钱开闸 (`LiveOrderGate.Arm()`) → 老韩 RM + 小白安全。本方案 MVP 止于 paper advisory, **不触会签门**。
- **必经老郭+老韩 code review 的唯一热路径点:** §5.1 WSS Publish notify (R-12 红线区), 写脏 = P0。
- BR-1 / R-20 / ML-R8 全程守: 同源特征、data_source_ts 锚、PIT 物理隔离。

---

**一句话总判:** 跨洋延迟不致命但杀死 3s/5s, **MVP 锁 30s, CONDITIONAL-GO**, 放行的前提是 Phase 0 两周内用现有数据跑完 G1 延迟体检 + G2 net-of-cost 可预测性体检 —— **G2 为负全案停**。技术栈 80% 复用 (ONNX runtime 已写好只待装机、RM 骨架、ring、trade_flow、4ts 链、label_pipeline join 框架), 真正要新建的是: 多窗 mid 标签管道、book-event 落帧、套利专用 Kelly+拒单码+open-leg fail-safe、事件驱动唤醒。**先证伪, 再建设。**

相关文件 (绝对路径): `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/ml/feature_vector_recorder.hpp` (G3 落帧改造点), `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/ml/feature_history.hpp` (ring 序列源 + PIT 门), `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/microstructure/trade_flow.hpp` (短窗参数化), `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/ml/label_pipeline.hpp` (join 框架复用 + 前视红线), `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/ml/fair_value_model.cpp` (ONNX runtime 已就绪待装机), `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/risk/risk_gateway.cpp` (RM 骨架 + fail-safe 缺口), `/Users/wangweibo/code/sports-trader-cpp/include/stcpp/sizing/sizing_calculator.hpp` (fee 公式 + 套利 Kelly 改造点), `/Users/wangweibo/code/sports-trader-cpp/src/stcpp/paper/paper_loop.cpp` (tick→事件驱动 + 双模型并列注入)。
