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

## 2. slice-2 「卖不出」退出流动性 (✅ DONE, **feature-first**)

> **老板 2026-05-31 校准**: "我说的卖不出和结算归零写硬逻辑，希望他们也是能被量化模型包含的。"
> → 卖不出/结算归零**做成量化特征喂模型, 不写硬门**。一致于老板「新鲜度/信号质量是输入不是草率守门」。

**observe-always 架构修正 (关键)**: 旧 `TickOne` 在 YES `best_bid` 无效时 **early-return** —— 「卖不出」(bid 没了) 这个**恰恰要观测的事件被代码提前 bail 掉、根本没记录** = 特征审查偏置 (censoring bias)。修正: 把时序样本 push 移到**交易有效性门之前** (观测永远发生, 交易决策另说)。`FeatureHistory.Push` 改 observe-always (不因价/bid 无效拒绝; 价无效存 NaN, 价 derive 内部跳过)。

**两特征 (进 `QuoteFeatures`, 喂模型, 非硬门)**:
- `bid_absence_frac` ∈[0,1]: 窗口内无可执行 bid 占比 (1=整窗卖不出/单边倒挂)。
- `exit_depth_mean`: 窗口内 best_bid_size 均值 (退出流动性薄 = 难卖出)。

**明确不设硬 gate**: 退出流动性如何影响规模 (target 是否该更小) **由模型/sizing 学**, 不在控制器/账本钉硬规则。Sample 扩存 (best_bid, best_bid_size)。测试: FH06 observe-always / FH10 卖不出 / FH11 流动性窗口 PIT + `TS2` 端到端 (无 bid tick 被捕获, 旧码会审查掉)。1170/1170 全绿。

> 注: 老韩曾提「退出流动性进 sizing 硬规则」—— 按老板校准**降级为模型特征**。若未来量化证明需硬保命门 (e.g. 退出流动性 0 时账户级拦), 另立, 不在本时序特征层。

## 3. slice-3 「被结算归零」(feature-first)

老板校准同样适用: 做成**量化特征**, 让模型学临近结算的归零风险, 而非硬逻辑。

### resolution 数据源摸底 (2026-05-31)

| 信号 | 解析? | 到决策路径? |
|---|---|---|
| Goalserve 终态 `IsTerminal` | ✅ | ✅ (fair conf=1.0) |
| Goalserve 时钟 `elapsed/total → time_frac` | ✅ | ✅ (**临近度可直接派生**) |
| Polymarket `ResolutionStatus` (kOpen/Resolving/Resolved) | ✅ WSS kOutcomes 已订阅+解析 | ⚠ 此前缺字段载体 |
| 账本结算 realize | — | ❌ (无 settle/redeem) |

**关键澄清 (老板质疑「市场 WSS 为什么不通, 没白名单啊」)**: **不是白名单/连通问题。** `pm_wss_subscriber.cpp:178` 连上即 `Subscribe(kOutcomes)`, resolution_status 也解析进 `WssEvent`。"不通" 纯粹是**最后一公里**: ① `OrderBookFeatures` (paper_loop 读的 POD) 缺 resolution 字段; ② `orderbook_adapter.cpp:82` 只处理 `kBook`, market-keyed 的 `OutcomesUpdate` 流过不消费。信号到了 sink 就停。

### slice-3a 临近度特征 (✅ DONE, 体育免新数据源)

`time_to_resolution_frac` = clamp(1 − time_frac, 0, 1); terminal → 0; 无时钟 → NaN。从 Goalserve 时钟派生 (已在 paper_loop)。进 `QuoteFeatures`。与 `bid_absence_frac` 组合 = 「临近结算 ∧ 卖不出」归零陷阱 (**模型学, 不硬门**)。TS3 端到端 (60min soccer → 0.333)。

### slice-3c 载体 (✅ 字段接通 / ⚠ live ingest 待接)

`OrderBookFeatures.resolution_status` (uint8, 加性 POD, §8.1 #5) + 流到 `QuoteFeatures.resolution_status`。**决策路径现可消费** (TS3 合成注入 kResolving=1 验证流通)。**剩 1 跳**: `orderbook_adapter` 接 `kOutcomes` 事件 (market-keyed → 需 market→token 映射, 属 live-path 设计), 把真 WSS resolution 填进 `OrderBookFeatures`。dormant 安全 (默认 0=Open, fail-closed)。

### slice-3b 账本结算 realize (✅ DONE)

市场 resolve 时持仓→0 或 ×$1 payout 是**事实**, 账本须正确记 (旧: 持仓永远挂账本, 结算时无 bid → MtM 贡献 0, paper PnL 错)。

**实现**: 比赛 **Ended** (Goalserve 终态) → `SettleCondition`: winner 从终态比分 + orientation 派生 (`score_home_total` = YES 边比分; yes>opp → YES=1/NO=0; 平局 → 0.5 push)。`SettleToken`: realize = (settle − avg_entry) × qty 累加进 `cum_realized_pnl_pusd_`, apply_fill 负 delta 平仓归零。`FeedRiskGateway` 的 daily_pnl 并入 realized (兑现其早留的 M2 口子: `pnl = 开仓 MtM + realized − cum_fee`)。幂等 (`settled_conditions_`), 已定盘口跳过决策 (市场已定不交易)。仅 Ended (清晰 winner); Postponed/Cancelled 留作 refund edge。**PnL 正确性 plumbing, 非交易硬门** —— 范式自然涌现 (临近结算 fair→0/1 → Kelly target 自然趋小), 不写硬平仓规则。

TS4 端到端: 买 YES @0.30 → 2:0 终场 → realize +6.92 pUSD + 平仓 + `positions_settled`。1172/1172 全绿。

### slice-3c-ingest live resolution 接入 (⬜ 摸底定位, 非现在硬塞)

老板「接着上 3c-ingest」—— 摸底后**诚实定位**: 最后一公里**不在 orderbook_adapter** (它出 `OrderBookSnapshot`, 非 hub 的 `OrderBookFeatures`), 而在 **`live_book_publisher.hpp:286`** (CLOB WSS JSON → `OrderBookFeatures` → hub.Publish)。它 line 207 **只收 `event_type=="book"`, 直接丢 outcomes 帧**。

要接 = publisher 加: ① 解析 `event_type=="outcomes"` 帧的 `resolution_status` 字段; ② per-token 状态记住; ③ 后续 book publish 时 attach 到 `feat.resolution_status`。

**为何没现在硬塞** (一致于 §10.2 反过度设计): ① `live_book_publisher` **零测试覆盖** —— 写未测的 live JSON 解析 = 投机 plumbing; ② 仓里有**两个并行 WSS client** (`live_book_publisher` debug_api / `pm_wss_subscriber` polymarket/wss), 得先确认哪个真喂 live hub; ③ 需 outcomes 帧真实 JSON shape。**字段+特征已就位** (决策路径 ready, TS3 注入验证流通), 此跳留给 live WSS 端到端验证时连 publisher 测试夹具一起做 (届时 kResolved 还能当 3b 的权威触发器补充 Goalserve Ended)。

## 3. 决策记录

- 老板选「**先把时序地基钉死**」(2026-05-31)，非四样齐做。地基 = 上面 1/2/3 全部的前置。
- slice-1 范围 = 环形缓冲 + 2 个最稳特征 (变化率 + vol) + PIT/BR-1 守死 + 跑通整条链。已达成。
