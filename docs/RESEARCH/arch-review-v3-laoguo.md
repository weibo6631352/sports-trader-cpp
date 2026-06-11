# 架构评审 — 盈利方案 v3 (纯 in-play 事件延迟套利)

> owner: 老郭 (架构评审 + 顾问团协调人, 架构否决权) | last_review: 2026-06-03
> 评审对象: docs/RESEARCH/profit-scheme-v3-arb-only.md + 三份配套 (mm-risk-assessment-data-v1 / polymarket-mechanics-verified-2026-06-03 / goalserve-event-interface-research)
> 评审依据: Read 真代码 (paper_loop.cpp / inplay_feed_thread.cpp / fair_value_model.cpp / seq_arb_model.{hpp,cpp} / arb_signal.hpp / arb_risk.hpp / feature_history.hpp / state_provider.hpp)
> 触发: 老板点名, §8 红线「重大变更必过评审」

---

## 0. 架构裁决 (TL;DR)

**v3 架构【有条件可行】。无架构层面的否决项 —— 这是本次评审最重要的结论。**

理由: v3 砍掉做市后,剩下的事件套利【全部能落在现有脚手架上】。我把 v3 的 6 个组成件逐个对到代码,发现:

| v3 组成件 | 代码现状 | 缺口性质 |
|---|---|---|
| 事件套利信号决策核 | `arb_signal.hpp` **已完整** (CI 下界门 + sizing + horizon 选优) | 已就绪 |
| 套利专属风控 | `arb_risk.hpp` **已完整** (RJ-ARB-1..5 + OpenLegLedger 强平) | 已就绪 |
| 序列模型接口 + ONNX 推理 | `seq_arb_model.{hpp,cpp}` **骨架已在** (Stub + OnnxSeqArbModel 多输出) | 待训练 + 装 onnxruntime |
| per-market tick 缓冲 | `feature_history.hpp` **已完整** (128 ring, PIT-safe, 已挂 paper_loop `ts_history_`) | 已就绪 |
| 事件码捕获 | `gs_state_code` **已抓进 rec, 下游零消费** | 加性扩 parser + 新建消费层 |
| 事件触发管道 | **不存在** (现是 500ms 轮询) | 真正的新建模块 (本评审重点) |

**唯一真正"新建"的是事件触发管道 + 事件码消费层。其余是"接线"不是"重构"。** v3 没有要求推翻线程模型、没有要求改 hub、没有要求动 RM 核心 —— 这正是 v3 砍做市后的架构红利:整个逆选-quoting 风险面 + 状态机接力 + 幻影簿处理全消失了。

**两个架构前置 (必须先解决, 但都不是否决, 是"先做对"):**
- **前置 A (事件触发管道的正确归属)**: 事件检测必须放在 inplay 采集线程侧, 不能放进决策环轮询 (详见 §1)。这是 R-12 合规 + 延迟正确性的硬约束, 做错了整个 v3 的"抢窗"前提就垮。
- **前置 B (序列模型的"序列"必须名副其实)**: 现 `seq_arb_model` 吃的是 **flat FeatureVector (单截面)**, 不是 tick 序列张量。若 v3 要 GRU/Transformer 吃轨迹, 输入契约要从 flat 升到 [1,T,F], 这是一处真实的接口演进 (详见 §2)。在升级前, 用 `FeatureHistory` 派生的窗口特征喂现有 flat 模型已经能跑 P3 第一版。

---

## 1. 事件触发管道 (最硬, 架构判断)

### 1.1 现状 (Read 确认)

- `paper_loop.cpp::RunLoop` (L207-247): 单 `jthread`, `while` 循环 `TickAll()` + `sleep(tick_interval_ms)`, 实测配置 500ms。**纯轮询, 无事件驱动。**
- `inplay_feed_thread.cpp::RunSportLoop` (L528-651): 每 sport 一条 `std::thread`, HTTP 拉 gz → parse → `merged_map_` merge → `ScoreSnapshotStore::Publish(shared_ptr swap)`。**采集线程已经是事件数据的第一落点**, 且 Publish 是原子 swap (~5ns 持锁, 已标 R-12 合规)。
- 决策环读比分: `TickAll` (L265) `score_store_->GetSnapshot()` 是 RCU 单次 load, 整 tick 冻结一份。

### 1.2 架构判断: 事件检测放采集线程, 不放决策环

**这是 v3 落地的第一个架构裁决, 我给定论:**

**事件跳变检测 (state 翻码 / IGoal 计数 diff) 必须在 inplay 采集线程内做, 紧挨 parse 之后、Publish 之前。决策环只消费"已检测的事件信号", 不自己做轮询比对。**

理由 (三条, 都是硬的):

1. **延迟正确性**: v3 的 alpha 命脉是"比 PM 散户的 ~13s 滞后早几秒"。500ms 轮询决策环天然引入 0-500ms 的检测抖动 —— 事件在 t 进 feed, 决策环最坏 t+500ms 才轮到这个 condition。把检测前移到采集线程 parse 后, 检测延迟 = 0 (数据一到就比对)。这把 500ms 抖动从套利窗口里抠掉。**采集线程已经是 ~2s/match 的节奏, 它本来就在"事件数据刚到"的时刻, 检测放这里是零额外延迟。**

2. **R-12 合规天然满足**: 采集线程是 HTTP I/O 线程, 本就不是 WSS event loop, R-12 (WSS event loop 禁同步 REST/阻塞 IO/锁>100us) 根本不约束它。事件检测是纯内存比对 (上一帧 state vs 本帧 state, 上一帧 IGoal vs 本帧 IGoal), 微秒级, 不碰 WSS loop。**反例**: 若把检测放进 paper_loop 决策环, 决策环本身不是 WSS loop 也不直接违 R-12, 但会把"事件检测"和"500ms 轮询"耦死, 延迟正确性垮 (见理由1)。

3. **状态归属清晰**: "上一帧事件态"是 per-match 的, 采集线程已经 per-sport 持有 `merged_map_` 全量 (L605-633), 它做帧间 diff 最自然 —— 数据在它手里。决策环只有冻结快照, 做不了"跨帧 diff"(它每 tick 看到的是 RCU 新版本, 没有"上一帧"概念)。

### 1.3 跨线程传递设计 (建议, 不越界到实现)

事件信号从采集线程 → 决策环, 走【与 ScoreSnapshotStore 同款的原子 swap 快照】, 不要引新的锁/队列:

- **方案 (推荐)**: 在 `EventScore` (state_provider.hpp:323, G-FREEZE-W 只增) 末尾加性追加事件态字段 (`gs_state_code` 已在 GameScoreRecord, 透传到 EventScore; 再加 `event_seq` 单调计数 / `last_event_code` / `last_event_ts_ns` / `igoal_home/away` 等计数)。采集线程检测到跳变时, 把"本帧事件标志"写进 EventScore, 随 `merged_map_` 一起 Publish。决策环 `GetSnapshot()` 读到时, 比对 EventScore 里携带的"事件序号是否比我上次见的新" → 若新且在窗口内 → 进套利分支。
  - 优点: **零新增同步原语**, 复用已验证的 RCU swap 通路 (R-12 已签); 加字段是 §8.1 加性变更 (struct 末尾追加 + PR 通知下游), 免 R-4 全审计、免架构会签。
  - 决策环侧维护一个 `unordered_map<condition_id, last_seen_event_seq>` (loop_thread 单 writer, 无锁), 比 EventScore.event_seq, 检测"我还没处理过这个事件"。
- **不推荐**: 引 SPSC 队列 / condition_variable 唤醒决策环。会引入新的线程交互面 + 唤醒/丢事件语义, 收益(省 500ms 中位 250ms)不抵复杂度。v3 是"小而正"的 alpha (操盘手估 $5-50/天), 不值得为它上低延迟队列基建。**先用 RCU 快照 + 决策环把 tick 间隔从 500ms 收到 100-200ms (sleep 改小, 零架构改动), 把检测抖动压到可接受。** 真要到微秒级抢窗 (A类瞬时锁套利 P4) 再单独立项事件唤醒通路。

### 1.4 线程模型结论

**不需要重构线程模型。** 现有"采集线程 N (per-sport) + 决策环 1 + WSS loop"三类线程的拓扑不变。v3 只是:
- 采集线程内**加一段帧间 diff** (纯内存, 微秒级);
- EventScore **加性扩字段**承载事件信号;
- 决策环**读快照时多一个"事件新鲜度"判断** + tick 间隔调小。

**风险**: 唯一的真实风险是"事件 seq 在 RCU swap 里被决策环漏读"—— 若采集线程 2s 内连发两个事件 (进球+点球), 决策环 200ms tick 一定能读到中间所有版本吗? 不一定 (RCU 只保最新版本, 中间版本被 swap 覆盖)。**缓解**: 用单调 `event_seq` + "本帧最近一次事件码/计数" 而非"事件流", 决策环只关心"自上次见过后有没有新事件 + 当前比分态", 不要求重放每个事件 —— 对"吃低估边"这个用途, 知道"刚进球了 + 现在比分 2:1"就够, 不需要事件流完整性。这个语义对齐 §goalserve-research 的"IGoal 计数跳变 = 确权"(用计数态而非事件流)。

---

## 2. 序列模型 C++ 推理 (可行性 + 设计)

### 2.1 现状 (Read 确认)

- `feature_history.hpp::FeatureHistory`: **已完整**。128 容量 ring, PIT-safe (ts 单调门), 已派生 RateOfChange/RealizedVol/Amihud/OFI/BidDepthVol 等窗口特征。已在 paper_loop `ts_history_` / `ts_history_no_` 双边挂载 (paper_loop.cpp L384-398)。**per-market tick 缓冲的内存/生命周期/线程安全问题已经被这个组件解决了** —— 定长 ring 无堆分配 (R-12 bounded), loop_thread 单 writer 无锁 (BR-1 标注)。
- `seq_arb_model.cpp::OnnxSeqArbModel::predict`: 输入 **`std::vector<float> input(fv.values...)`, shape `{1, feature_count}`** —— 即 **flat 2D [1, F], 不是 3D 序列张量 [1, T, F]**。输出读 flat float[8] 或 [8×3]。
- onnxruntime: 服务器**待装** (CLAUDE.md §13 确认), 本地 `STCPP_ONNX_ENABLED` 未开时全 fallback stub。

### 2.2 ONNXRuntime 跑不跑 GRU/LSTM/Transformer? — 跑

**结论: ONNXRuntime CPU EP 原生支持 LSTM/GRU/RNN/Attention/MatMul 等全部序列算子。** 这不是可行性问题。PyTorch/TF 导出的 GRU/小 Transformer 转 ONNX 后, ONNXRuntime CPU 直接跑, 单样本 [1,T,F] 推理在 T≤128/F≤110/小隐层 (≤64) 规模下亚毫秒级, 远低于套利窗口要求。**M5/MPS (Apple Metal) 只用于训练侧**, 服务器推理走 ONNXRuntime CPU EP, 与训练后端无关 —— 这点方案写对了。

唯一要确认的实现细节 (归小邓/老吴, 不归我): 装的 onnxruntime build 默认含全部 standard ops (官方 release 含), 不要裁剪成 minimal build 漏掉 LSTM/GRU。

### 2.3 架构上怎么接 — 前置 B (输入契约从 flat 升到序列)

**这是 v3 唯一一处真实的接口演进, 但是加性的、可分阶段的:**

- **P3 第一版 (零接口改动)**: 用 `FeatureHistory` 派生的窗口特征 (RateOfChange/RealizedVol/OFI...) 塞进现有 flat FeatureVector, 喂现有 `OnnxSeqArbModel` 的 flat [1,F] 输入。**这已经能跑** —— 时序信息以"派生特征"形式进单截面。LightGBM quantile (方案 P3 的 GBDT 档) 本来就吃 flat 特征, 这条路完全通, 无需任何接口变更。
- **P3 顶点版 (真序列张量, 加性升级)**: GRU/Transformer 要吃原始 tick 轨迹时, `SeqArbModel::predict` 增一个重载或新接口 `predict_seq(span<FeatureVector> window)`, 内部把 `FeatureHistory` 最近 T 帧组装成 [1,T,F] 张量。**这是给 SeqArbModel 加方法, 不动 FairValueModel, 不动现有 flat predict 路径** —— 加性, 现有 stub/flat 模型零影响。

**设计建议 (定方向, 不写实现)**:
1. **张量组装在哪**: 决策环侧 (loop_thread)。`FeatureHistory` 已经 per-condition 持有最近 128 帧, 推理前从 ring 取最近 T 帧 (T 在模型 meta 里声明, 如 32), 按列序 (MlFeature enum) 铺成 [1,T,F]。组装是栈上/预分配 buffer, 不堆分配 (R-12)。
2. **有状态 vs 无状态**: **强烈建议无状态推理 (stateless), 每次喂完整窗口张量, 不在 ONNXRuntime session 里维护 GRU hidden state。** 理由: ① 有状态推理要求逐 tick 严格顺序喂、不能丢帧、不能乱序 —— 跨 RCU swap + per-condition 几千个市场, 维护几千份 hidden state 的生命周期 (市场结束要释放、断流要 reset) 是 bug 温床且违 BR-1 (回测 replay 要逐位复现 hidden state 几乎不可能)。② 无状态 + 完整窗口张量天然 PIT-safe + 回测-实盘逐位一致 (BR-1), 与 `FeatureHistory` 的"窗口 [as_of−W, as_of]"语义完美对齐。代价是每次重算窗口 (T×F 次前向), 但 T≤32/F≤110 规模 CPU 亚毫秒, 完全可接受。**这是架构上的硬建议: stateless 窗口推理, 不要有状态会话。**
3. **per-market 缓冲生命周期**: `ts_history_` 是 `unordered_map<condition_id, FeatureHistory>`, 已存在。**缺一个清理路径** —— 结束的市场 (settled_conditions_) 的 FeatureHistory 永不释放会内存无界增长。建议加性补一个周期清理 (市场 settle 后 N 分钟从 ts_history_ erase), 这是已存在的小债, v3 之前就该补, 归小肖/小李。

### 2.4 可行性结论

**序列模型推理架构上完全可行, 无否决项。** per-market 缓冲已解决 (FeatureHistory), ONNXRuntime 跑序列算子无问题, 输入契约升级是加性的可分阶段的。唯一硬建议: **stateless 窗口推理**, 不上有状态会话。

---

## 3. 检测地基模块边界 (防 god module)

方案 §7 + goalserve-research §7 已经把边界划得基本对, 我确认并钉死归属, 防止堆成一个 god module:

| 职责 | 归属 | 边界 |
|---|---|---|
| **wire 解析** (读 inplay `stats`/`extra`/`core` 块, 消费 `info.state`) | 小田#8/小段 (数据) | 扩 `InplayScoreParser::ParseEventBlocks`, 产出**纯字段** (state 码 / IGoal 计数 / core.updated_ts), 挂 GameScoreRecord 加性字段。**只解析, 不判断"这是不是套利信号"。** |
| **字段语义 + 4ts** (event_ts 用 soccernew `<event ts>` / data_source_ts 用 core.updated_ts) | 小田#8 (字段契约) | EventScore 加性字段 + R-20 4ts 正确归位。**不碰触发逻辑。** |
| **帧间 diff 检测** (state 翻码 / 计数跳变 → event_seq++) | 小段/小田 (在采集线程内) | 纯内存比对, 产出"事件发生了 + 哪类 + 何时"的**事实**, 不产交易决策。 |
| **触发逻辑 → 交易信号** (事件码 → 该不该吃低估边 / 哪个 horizon / 偏见修正) | 小梁/小袁 (量化) | 在决策环, 读 EventScore 事件态 → 喂 `ComputeArbSignal`。**这是 alpha 逻辑, 量化主权。** |
| **风控门** (RJ-ARB + OpenLegLedger) | 老韩 (风控) | `arb_risk.hpp` 已就绪, 不动。 |

**关键纪律 (我钉死)**: **wire 层 (数据) 只产事实字段, 绝不产"信号语义"。** 一旦让 parser 去判断"11003 = 该买 YES", 就是 god module + 跨域越界 (数据替量化做了 alpha 判断)。事件码→信号映射是 §LLM 参数发现的旋钮 (方案 §LLM), 属量化可调参数, 必须在量化侧, 不能硬编进 parser。这条边界违反就是架构债。

---

## 4. 现有套利脚手架够不够 (逐个对)

| 脚手架 | 够不够 v3 | 要改什么 |
|---|---|---|
| **arb_signal.hpp** (ComputeArbSignal) | **够**。CI 下界门 (ci_low−BE>0)、per-horizon 选优、signal_quality 排序、RJ-ARB 集成全在。 | 不改核心。`ArbMarketState` 已有 entry/exit 价 + 深度 + 延迟字段。事件触发只是"何时调它", 不改它本身。 |
| **arb_risk.hpp** (RJ-ARB-1..5 + OpenLegLedger) | **够**。RJ-ARB-5 STALE_PREDICTION 正是延迟红线 enforce 点 (pred_age+RTT > horizon/2 拒); OpenLegLedger 强平防退化成赌。 | 不改。OpenLegLedger 的 SweepExpired 需要一个 sweeper 调用点 (非热路径周期) —— 接进决策环 tick 末或单独 sweeper 线程, 这是接线不是改结构。 |
| **OnnxSeqArbModel + StubSeqArbModel** | **够** (骨架)。stub 恒 ok=false (无真模型不发单, fail-safe)。 | 待训练真模型 + 装 onnxruntime。输入契约见 §2.3 (P3 可先 flat)。 |
| **FeatureHistory** (ts_history_) | **够**。per-market ring + PIT + 双边已挂。 | 补生命周期清理 (§2.3.3), 否则内存无界。 |
| **position_controller** (限价不追) | **够**。出场被动限价 (maker 零费 BE 1.4%) 用它。 | 接线。 |
| **walk_forward + walk_forward_ic** | 框架在, 但 **event_replayer 只覆盖 1/6 输入 (book)**。 | **这是 v3 最实质的脚手架缺口 (见下)。** |
| **event_replayer** (backtest) | **不够**。paper_loop.cpp L260-270 注释明说: 6 个决策输入 (event_map/score/catalog/resolution/live_stats/odds + book), **只有 book 这 1/6 能经 ReplayDriver 注入**, 其余 5 个 live 路径恒走 store 读。 | **v3 套利的核心输入是 score/事件态 (在那 5/6 里, 不是 book)** → 现 event_replayer **回放不了事件套利**。要补"事件态 + score 帧序列"的回放注入, 才能 walk-forward 验 G2 真进球收敛 (方案 P2 验收前置)。 |

### 4.1 walk-forward 补全是 v3 的真前置 (但不是否决, 是验收门)

方案 P2 写"G2 真进球验证 + walk-forward (≥5 折) 才上"。**但现 event_replayer 注入不了事件态/score** → 没法在回测里复现"进球→抢窗"。这不是 v3 上线的架构否决 (paper 可以先在 live feed 上观测/跑), 但**是"真钱开闸 (P4) 前必须补的验收基础设施**:

- 现状: `tick_inputs_` 6 输入里, `replay_inputs_` 非空时整体替换 (L261-262), 但 ReplayDriver 实际只填了 book (老蒋 P2 harness 只闭合了红线#3 的 book)。
- 补法 (归小蒋, 架构上加性): 让 ReplayDriver 能注入 `tick_inputs_.score` (事件态帧序列) —— `tick_inputs_` 结构已经把 6 输入聚合成可整体替换的 struct, **架构已经为此预留了** (L261 的 `tick_inputs_ = *replay_inputs_`)。所以这是"把 score 帧序列喂进 replay_inputs_", 不是改 paper_loop 结构。**架构地基已在, 补的是 replay 数据通路。**

---

## 5. 红线合规 (逐条)

| 红线 | 原文要点 | v3 触不触 | 合规路径 |
|---|---|---|---|
| **R-12** (WSS event loop 禁同步 REST/阻塞 IO/锁>100us) | §8 | **不触**。事件检测在 inplay 采集线程 (HTTP I/O 线程, 非 WSS loop), 帧间 diff 是微秒级纯内存。跨线程传递走 RCU swap (~5ns, 已签)。**唯一要守的: 别把检测/推理塞进 WSS loop, 也别在决策环引阻塞唤醒。** | §1.2/§1.3 设计已天然合规。 |
| **R-11** (paper 不污染真账本) | §8 | **不触**。v3 全程 paper, paper_loop 已有 build-time + 运行期双 gate (L148-158 kCompiledMode 断言)。套利下单走同一 VirtualExecutor。 | 不动现有 gate。真钱开闸 (P4) 走 §8.1 会签 (老韩 RM + 小白)。 |
| **R-20** (4ts, 事件级 event_ts) | §8 | **要主动守**。事件套利的 event_ts 是 alpha 时序锚, 做歪直接前视污染。 | **硬要求**: event_ts 优先 soccernew `<event ts>` (秒 epoch 真事件时刻), data_source_ts 用 inplay per-match `core.updated_ts` (比顶层 updated_ts 精确), **禁 now() 替代** (goalserve-research §5 已给正确归位)。FeatureHistory 的 ts 单调门 (L61) 已 PIT-safe。 |
| **R-5** (C++ 推理) | §10 语言纪律 | **不触**。序列模型训练 Python (离线), 导 ONNX, 推理走 OnnxSeqArbModel (C++)。LLM 参数发现离线 (方案明示绝不进热路径)。 | 已合规。 |
| **§8.1** (加性/已通知/非重大免会签) | §8.1 carve-out #5/#6 | **适用, 是 v3 低仪式的关键**。EventScore/GameScoreRecord 加事件字段 = struct 末尾纯加性 + PR 通知下游 → **免 R-4 全审计、免架构会签**, 走普通 PR review。写 default-disarmed 的套利 plumbing (stub 永不发单) **免会签** (§8.1 #6: 风险在钱动不在码写)。 | 真钱开闸才会签。 |

**红线层无否决。R-20 是唯一需要施工时主动守的 (event_ts 归位), 其余 v3 天然合规或免仪式。**

---

## 6. 架构裁决 + 分阶段演进

### 6.1 裁决

**v3 架构【有条件可行】。无否决项。**

这是一个**架构上比 v2 干净得多的方案** —— 砍做市消掉了逆选-quoting/状态机接力/幻影簿/库存爆整个风险面 + 复杂度面, 剩下的事件套利 90% 落在已就绪脚手架上 (arb_signal/arb_risk/FeatureHistory/seq_arb 骨架全在)。真正新建的只有"事件触发管道 + 事件码消费层", 且不需要重构线程模型。

### 6.2 两个架构前置 (开工前钉死, 非否决)

- **前置 A**: 事件检测放 inplay 采集线程 (parse 后 Publish 前), 不放决策环轮询。跨线程走 EventScore 加性字段 + RCU swap, 不引新锁/队列。(§1)
- **前置 B**: 序列模型推理用 **stateless 窗口张量** (不上有状态会话); P3 先 flat 特征跑通, 顶点版再加性升 [1,T,F] 接口。(§2.3)

### 6.3 分阶段演进 (与方案 P0-P4 对齐, 不一次性大重构)

```
P0  [加性, 免会签]  扩 InplayScoreParser 读 stats/extra/core/消费 info.state → GameScoreRecord 加性字段
                    + EventScore 加性扩事件态字段 (event_seq/last_event_code/igoal计数/core_ts)
                    + R-20 event_ts 归位 (soccernew <event ts> / core.updated_ts, 禁 now())
                    + 测试组被动观测进球前后 PM acceptingOrders 连续性 (引擎A 生死命门, 零下单)
                    门: 加字段不改 schema 语义 → PR review 即可

P1  [新建模块]      采集线程内帧间 diff 检测 (state翻码/IGoal跳变 → event_seq++)
                    + 决策环读 EventScore 事件态 + tick 间隔 500ms→100-200ms
                    + FeatureHistory 生命周期清理 (settle 后释放, 补内存无界债)
                    门: 架构评审签 (本报告) 后即可写 plumbing (default 不发单, stub)

P2  [接线 + 验收]   事件触发 → ComputeArbSignal (已就绪) → bet365 偏见修正 → tennis/soccer 吃错价
                    + OpenLegLedger sweeper 接点
                    门: G2 真进球收敛复测 + walk-forward ≥5 折 (需 P3.5 event_replayer 补全)

P3  [模型]          y_h 规则 → LightGBM quantile (flat, 现接口直接跑) → 序列模型 (顶点, 加性升序列接口)
                    + LLM 离线调参 (事件码→信号映射/horizon/BE/偏见系数)
P3.5[基础设施]      event_replayer 补 score/事件态帧序列回放注入 (架构已预留 tick_inputs_, 补数据通路)
                    → 闭合 walk-forward 对事件套利的回测能力 (P2 验收门的前提)

P4  [真钱]          A类瞬时锁套利快路径 (此时若要微秒抢窗, 单独立项事件唤醒通路, 不绑 P0-P3)
                    + 真钱开闸 → §8.1 会签 (老韩 RM + 小白安全)
```

### 6.4 给 GM 的三句话

1. **可以做, 架构干净, 我不否决。** 砍做市是对的 —— 不只是风控裁决 (尾部毒 + 撤单形同虚设), 架构上也消掉了最大的复杂度/风险面。剩下的事件套利大部分是接线不是重构。
2. **开工前钉死两件事**: 事件检测放采集线程别放轮询环 (延迟正确性 + R-12); 序列模型走 stateless 窗口张量别上有状态会话 (BR-1 回测一致性 + 生命周期可控)。这俩做错会埋后患, 但都不是设计推翻, 是"一开始就做对"。
3. **诚实记一笔**: event_replayer 只覆盖 1/6 输入, 而 v3 套利的核心输入恰恰在没覆盖的 5/6 里 (score/事件态)。这不挡 paper 上线, 但**挡真钱开闸的 walk-forward 验收** —— P4 前必须补 (P3.5)。别让 "P2 走了 walk-forward" 变成纸面验收 (回测里根本回放不了进球)。这是我盯得最紧的一条。
