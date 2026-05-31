# 时序特征地基 — Spec v1 (老板 2026-05-31 提)

> owner: 老雷 (GM) · last_review: 2026-05-31 · 状态: 地基已落地 (slice-1)
> 起点 (老板原话): "我觉得我们的大模型应该接入一些时序信息因为单点的切片无法获取
> 比如变化率啊，波动率啊，还有单边倒挂的那种，卖不出的情况，被结算了归零。"

---

## 0. 问题

模型现吃 `MlFeature` v0.1 = **18 个单点截面特征** (score/microprice/imbalance/spread/depth/
devig…)，**零时序维度**。单点切片看不到「动态」：变化率、波动率、流动性演化、临近结算。
`model_feature_spec.hpp:32` 早挂号「时间衰减 / rolling 窗口 @小程」，一直没动。

老板列的四样**不是同类，分属不同的家**：

| # | 老板说的 | 真正归属 |
|---|---|---|
| 1 | 变化率 | 时序特征 (进模型) |
| 2 | 波动率 | 时序特征 (进模型) |
| 3 | 单边倒挂 / 卖不出 | **一半特征 (流动性留存) + 一半风控 (退出流动性进 sizing)** |
| 4 | 被结算归零 | **不是特征，是生命周期 + 范式断点** (time-to-resolution 特征 + 账本结算口径 + 临近结算退化为 hold-to-settlement 二元赌注) |

## 1. 地基 (slice-1, ✅ DONE)

**`include/stcpp/ml/feature_history.hpp` — `ml::FeatureHistory`**: 单 instrument 定长时序环形缓冲 + 窗口派生纯函数。

- **存**: per condition 一个 ring, 样本 = (ts_ns, microprice)。kCapacity=128。
- **派生 (slice-1 两个最稳特征, 老板「先 2 个」)**:
  - `RateOfChangePerSec(W)`: (mp_last − mp_first_in_window) / Δsec。变化率/动量。
  - `RealizedVol(W)`: 窗口内相邻微价变化 RMS = sqrt(mean(Δp²))。波动率 (prob 绝对变化, 规避 p→0/1 的 return 爆炸)。
- **接 paper_loop**: TickOne push YES-canonical 微价 (key=condition_id); PublishQuoteSnapshot 派生进 `QuoteFeatures.{mp_roc_per_sec, realized_vol, ts_window_samples}` (加性, 训练数据捕获 + 观测)。窗口 = `cfg.ts_feature_window_ns` (默认 30s)。

### 红线 / 契约 (做歪一个 tick 的前视 → 回测金光实盘亏穿)

1. **PIT (ML-R8)**: 只 push 已观测样本; ts = 上游 `data_source_ts_ns` (**禁本地 now()**)。窗口 `[as_of−W, as_of]` 只含过去 (ring 从不存未来)。as_of = 最新样本 ts。
2. **BR-1 (回测/实盘同源)**: 纯逻辑 / 无 IO / 无锁 / 无 now()。回测按事件序 replay 喂同一组件 → 派生逐位一致 (FH-09 单测钉死)。组件本身无时间观念，事件序由调用方保证 (push-then-read)。
3. **R-12**: 定长 ring，无堆分配 / 无 unbounded 扫描; loop_thread_ 单 writer 无锁。
4. **单调门**: ts ≤ last 跳过 (停滞 book 重复读不污染 vol; 乱序不入)。脏样本 (非有限 / ∉(0,1)) 拒。
5. **缺失语义**: 样本不足 → NaN (与 MlFeature 一致)。

**测试**: `test_feature_history.cpp` 9 测 (空/单样本/变化率/vol/单调门/脏样本/**PIT 窗口边界 FH-07**/容量回绕/**BR-1 确定性 FH-09**) + `test_paper_loop.cpp::TS1` 端到端 (推进-ts 序列 → 特征 populate)。1167/1167 全绿。

### slice-1 未做 (诚实标注)

- **未进 live `MlFeature` enum**: 现 ML 推理是 baseline/stub (无训练模型)。时序特征先在 `QuoteFeatures` 捕获进训练数据**积累**; 待真 ONNX 模型训练时再 append 进 `MlFeature` (列序锁 + bump kSpecVersion + retrain, 小邓契约)。**先积累数据，后接推理** = 安全增量路径。
- **未接回测**: 组件 BR-1-ready (纯 + 无依赖)，但回测引擎 (小蒋) 的 replay 喂数尚未接。回测接入时复用同一 `FeatureHistory` (这正是 BR-1 的意义)。

## 2. 路线 (下两块硬骨头, 独立立项)

- **slice-2 「卖不出」流动性留存** (特征 + 风控双归属): 扩 `FeatureHistory.Sample` 存 (best_bid, bid_size) → 派生「窗口内无 bid tick 占比 / bid 留存率 / 退出流动性窗口均值」。**关键: 不只喂模型 —— 进 sizing**: 退出流动性薄 → target 该更小 (目标仓位范式假设「能连续 rebalance 调回目标」, 退出流动性塌了这假设就塌)。owner: 小程 (特征) + 老韩 (sizing 口径) + 小袁 (microstructure)。
- **slice-3 「被结算归零」生命周期**: ① time-to-resolution / resolution-proximity 特征; ② **账本结算口径** (持仓→0 或 ×$1 payout; 现 paper PnL 在结算时是否正确? 待查); ③ **范式断点**: 临近结算 + 无退出流动性时, 仓位退化为「hold-to-settlement 二元赌注」, reservation/限价不追 (假设有连续退出) 失效 → 模型/风控须识别并切二元盈亏口径。owner: 老韩 (账本/风控) + 小梁 (sizing) + 小余 (resolution 数据源)。

## 3. 决策记录

- 老板选「**先把时序地基钉死**」(2026-05-31)，非四样齐做。地基 = 上面 1/2/3 全部的前置。
- slice-1 范围 = 环形缓冲 + 2 个最稳特征 (变化率 + vol) + PIT/BR-1 守死 + 跑通整条链。已达成。
