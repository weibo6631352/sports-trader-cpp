# 架构评审 v3 — 纯 in-play 事件延迟套利 (系统/线程/模块/技术栈)

> owner: 老周 (系统工程部主管 + 架构主权) | last_review: 2026-06-03
> 评审对象: `docs/RESEARCH/profit-scheme-v3-arb-only.md` (老雷)
> 评审范围: **系统架构 / 线程模型 / 模块边界 / 技术栈选型** (否决级总判 + 红线归老郭, 我不重)
> 立场: 读真代码后的工程可行性裁决 + 最小步演进路径 + 诚实工程量级 + 风险点

---

## 0. 一句话裁决

**v3 的目标架构在工程上可行, 而且地基比方案文档以为的更厚** —— `FeatureHistory` ring buffer / `HotSwapHolder` 热加载 / `SeqArbModel` ONNX 接口 / `ScoreSnapshotStore` RCU / 条件编译的 onnxruntime 链路 **全部已落地**。但方案有一个**致命的物理误判必须先纠正**: 「500ms 轮询吃掉薄窗口」这个痛点的真凶**不是 paper_loop 的 500ms tick, 而是 inplay feed 本身 1s 一次的 HTTP 拉取**。在上游数据颗粒度是 1s 的前提下, 把 paper_loop 重构成纯事件驱动**拿不到方案设想的低延迟收益**。

**我的裁决: 不做全事件驱动重构 (大爆炸, 高风险, 收益被上游 1s 颗粒度封顶)。走「局部事件唤醒 + 缩短 tick」的最小步演进。** 详见 §2 / §6。

---

## 1. 现状盘点 (Read 真代码后的事实, 非方案假设)

| 组件 | 文件 | 现状 | v3 复用度 |
|---|---|---|---|
| inplay 采集 | `src/stcpp/data/inplay_feed_thread.cpp` | **每 sport 独立 std::thread**, while 循环内 `FetchGz`(阻塞 HTTP) → 解压 → parse → merge → `store_.Publish`。`poll_interval_ms=1000` + `min_fetch_interval_ms=1000` (Goalserve per-sport 1 req/s 限速硬封顶) | 复用; 加事件检测钩子 |
| 比分快照 store | `score_snapshot_store.hpp` | `shared_ptr<const ScoreMap>` + mutex 短锁 swap (RCU-lite)。**Publish 无任何下游唤醒回调** | 复用; 加 cv/seq 唤醒 |
| 决策环 | `src/stcpp/paper/paper_loop.cpp` | 单 `std::jthread`, `RunLoop` while 循环, `tick_interval_ms=500` sleep。`TickAll` 入口冻结所有快照 (RCU load) 整轮共享 → 遍历 296 市场 catalog → `TickOne` | 复用; tick 唤醒机制改造 |
| 事件码 | `inplay_score_parser.cpp:388` | `info.state` 已捕获进 `rec.gs_state_code` (5位码)。**但 `ToEventScore`(inplay_feed_thread.cpp:690) 未透传到 `EventScore` → 下游 paper_loop 零消费** | **低垂果实** (纯加法) |
| 时序 ring | `ml/feature_history.hpp` | `FeatureHistory` 定长 128 样本 ring, observe-always, PIT-safe, 已派生 OFI/Amihud/vol/变化率。paper_loop 已 per-condition `ts_history_`/`ts_history_no_`/`game_history_` 维护 | **序列模型 tick 缓冲地基已就位** |
| 模型热加载 | `ml/hot_swap_model.hpp` | `HotSwapHolder<Model>` 短锁 swap, watcher 线程原子换 ONNX。paper_loop `ml_holder_` 已接 | 复用 |
| 序列模型接口 | `ml/seq_arb_model.hpp` + `.cpp` | `SeqArbModel` 抽象 + `OnnxSeqArbModel` 工厂 + `StubSeqArbModel`(恒 ok=false fail-safe)。**8 horizon 多输出 {dmid,ci_low,ci_high,confidence} 已设计** | **接口已成形, 缺 ① 序列输入 tensor ② 真模型** |
| ONNX runtime | `ml/CMakeLists.txt` + `fair_value_model.cpp` | 条件编译 `STCPP_ONNX_ENABLED`, `find_library(onnxruntime)` 命中则链真 `Ort::Session`。**服务器 onnxruntime 待装** (CLAUDE.md §13) | 复用; 装机即活 |
| daemon 装配 | `src/stcpp/app/paper_daemon.cpp` | InplayFeedThread + PaperLoop + LiveBookPublisher(WSS) + 5 个 Refresh jthread(odds/livestats/resolution/tennis/team) + watcher + recorder。**已是多线程编排** | 目标架构落点 |

**关键结论: v3 不是从零起。地基覆盖约 70%, 缺口集中在 ① 事件检测/唤醒管道 ② 序列输入张量 (per-market tick 窗口 → ONNX 序列 tensor) ③ 真序列模型训练→导出。**

---

## 2. 痛点真凶纠正 (方案最重要的一处事实修正)

方案 §2 引擎A 写「事件驱动抢窗(替 500ms 轮询)」, 操盘手指出「500ms 轮询吃掉本就薄的窗口」。**这个因果链有误。**

**数据流真相 (从代码读出):**

```
Goalserve inplay.gz  ──1s 一拉(限速硬顶)──>  InplayFeedThread  ──Publish──>  ScoreSnapshotStore
                                                                                      │ (RCU, 无唤醒)
                                                                                      ▼
PM CLOB WSS  ──实时 delta(<100us)──>  OrderBookSnapshotHub  <──每 500ms 读──  PaperLoop.TickAll
```

- **比分/事件码 (info.state) 的新鲜度上限 = 1s** (Goalserve per-sport 限速 1 req/s, `min_fetch_interval_ms=1000`)。这是 ToS 硬约束, 不可绕 (no-pinnacle-use-goalserve memory: 不接别的源)。
- paper_loop 500ms tick 平均增加 **250ms** 检测延迟 (期望值半个 tick)。
- 即把 tick 砍到 0 (纯事件驱动), **比分事件最快也要等下一次 1s 拉取才进系统** —— 上游 1s 颗粒度封顶了一切。

**所以: 把 paper_loop 改成事件驱动, 对「比分事件→决策」延迟的改善上限是 ~250ms (砍掉半个 tick), 但代价是上游仍有 0-1000ms 的拉取抖动。投入产出比差。**

**真正吃掉窗口的延迟构成 (按可压榨性排序):**

| 延迟源 | 量级 | 可压榨性 |
|---|---|---|
| Goalserve 拉取颗粒度 | 0-1000ms (期望 500ms) | **不可压** (ToS 限速; 除非 push feed, Goalserve 不提供) |
| paper_loop tick 半周期 | 250ms (500ms tick) | **可压** (缩 tick / 事件唤醒) |
| 跨洋链路 RTT | ~31ms (CLOB) + Goalserve 拉取 RTT | 部分 (就近部署已做, eu-west-2) |
| PM WSS book 更新 | <100ms (实时 delta) | 已最优 |

**裁决: 最大可压榨延迟在 paper_loop 这 250ms。压它不需要全事件重构 —— 缩 tick + 局部事件唤醒就够, 风险低一个量级。** 见 §6 演进。

---

## 3. 线程模型重构 (核心裁决)

### 3.1 目标线程拓扑

保持现有线程数, **只加一条「事件唤醒」边**, 不新增决策线程 (单决策线程是对的 —— 避免 per-market 锁竞争, TickAll 入口冻结 RCU 快照的设计已经把跨线程一致性处理干净了)。

```
┌─────────────────────────────────────────────────────────────────────┐
│ 线程拓扑 (v3 目标; ★=新增/改造点, 其余复用)                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  [InplayFeedThread × N sport]  (阻塞 HTTP, 1s 颗粒, 已有)               │
│         │ Parse → ToEventScore (★透传 gs_state_code)                    │
│         │ Publish(ScoreMap)                                            │
│         ▼                                                              │
│  [ScoreSnapshotStore] RCU  ──★EventDetector(轻量, 在采集线程内联)──┐    │
│         │ (现有: 读侧无锁)                       检测 state_code 跳变 │    │
│         │                                       /比分跳变 → set seq++ │    │
│         │                                       + cv.notify_one()    │    │
│         ▼                                              │             │    │
│  [PaperLoop loop_thread]  ◄────★事件唤醒 (cv wait_for)─┘             │    │
│         │  RunLoop: 不再纯 sleep(500ms),                              │    │
│         │  改 cv.wait_for(tick_floor, pred=有新事件)                   │    │
│         │  TickAll 入口冻结 RCU 快照 (现有, 不动)                       │    │
│         │  遍历 catalog → TickOne                                     │    │
│         ▼                                                             │    │
│  [OrderBookSnapshotHub] ◄──[LiveBookPublisher]◄──[WSS io_thread]      │    │
│       (现有, R-12: WSS event loop 零阻塞, 不动)                         │    │
│                                                                       │    │
│  [5× Refresh jthread] odds/livestats/resolution/... (现有, 不动)       │    │
│  [watcher jthread] ONNX 热加载 (现有, 不动)                            │    │
│  [train 编排线程] 边训边跑 (现有, 不动)                                  │    │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.2 事件检测在哪个线程 (裁决)

**事件检测内联在 InplayFeedThread 的采集线程, 不新建线程。**

- 理由: 采集线程 parse 完 `GameScoreRecord` 时, **本来就持有上一帧和这一帧的比分 + state_code**。检测「state_code 跳变 / 比分跳变」是纯内存比较 (per-sport key set diff), O(events) 哈希操作, 与现有 `merged_mu_` 临界区同量级, 远 < R-12 的 100us。新建线程反而要再传一份快照, 多一道锁。
- 检测产物: 一个进程级 `std::atomic<uint64_t> score_event_seq_`(单调递增) + 一个 `std::condition_variable score_cv_`。检测到跳变 → `score_event_seq_.fetch_add(1)` + `score_cv_.notify_one()`。
- **R-12 合规**: 采集线程不是 WSS event loop (HTTP 阻塞本就在采集线程, 红线管的是 WSS io_thread)。`notify_one` 是 futex, ns 级, 合规。

### 3.3 决策环怎么被唤醒 (裁决)

`PaperLoop::RunLoop` 的 sleep 循环改成 **「事件唤醒 + tick floor 兜底」** 混合:

```
while (!stop):
    last_seen = score_event_seq_.load()
    TickAll()
    # 等待: 要么新事件来了, 要么 tick_floor 到了 (兜底, book-only 变化也要扫)
    cv.wait_for(lk, tick_floor_ms, pred = [score_event_seq_ != last_seen])
```

- **`tick_floor_ms` 兜底必须留** (建议 200-300ms): 因为 book 价格变化 (WSS delta) **不经过 score store**, 不会触发 score_cv。套利收敛是看 PM book 向 sharp 靠拢, book 变化才是平仓时机的主信号。纯靠 score 事件唤醒会漏掉 book-only 的决策窗口。
- 事件来了立即醒 → 抢窗延迟从「半个 500ms tick」压到「cv 唤醒延迟 ~μs + 一次 TickAll 全扫」。
- **TickAll 全扫成本**: bench 记录 per-market ~380ns (architecture-review-2026-06 memory), 296 市场约 110μs/全扫。即便每秒被唤醒数十次也 < 1% CPU。**全扫不需要分片** (那条 memory 的结论), 这里复用。

### 3.4 为什么不做「per-market 精准唤醒」

理论上可以只唤醒「出事件的那个 market 的决策」, 但:
1. 决策环是单线程遍历 catalog, 没有 per-market 的可唤醒单元 (要拆成 296 个任务 + 线程池 = 重构 god loop, 引入锁)。
2. TickAll 全扫 110μs, 精准唤醒省不下多少, 反而毁掉「入口冻结 RCU 快照整轮一致」这个已经很干净的设计。
3. **裁决: 唤醒粒度 = 整个 TickAll, 不做 per-market。** 简单、已验证、够快。

---

## 4. 事件触发 vs 轮询的取舍 + 演进路径

**裁决: 分三小步, 每步独立可上、可回退, 不一次性重构。**

### Step E1 (零风险, 纯加法): 透传 gs_state_code

`ToEventScore` (inplay_feed_thread.cpp:690) 把 `rec.gs_state_code` 填进 `EventScore` 新增字段 (struct 末尾加, §8.1 carve-out: 纯加性 + 已通知 → 不触发 R-4 全审计)。**这是方案 P0 的「低垂果实」, 也是后面一切的前提。** 1 人天。

### Step E2 (低风险): 缩短 tick + 内联事件检测计数器

- `tick_interval_ms` 500 → 250 (直接砍掉一半检测延迟, 零架构改动, 改个配置)。CPU 成本: TickAll 110μs × 4/s = 0.04% → 可忽略。
- InplayFeedThread merge 块内加事件检测 (上帧 vs 本帧 state_code/比分 diff) → `score_event_seq_.fetch_add`。**先只计数 + 日志, 不接唤醒** (观测事件频率, 验证检测逻辑正确, 测试组被动观测 acceptingOrders 连续性也在这步)。2-3 人天。

### Step E3 (中风险, 真事件驱动): 接 cv 唤醒

- `score_event_seq_` + `score_cv_` 接进 `RunLoop` 的 wait_for。保留 `tick_floor_ms` 兜底。
- **最大风险点**: cv 唤醒引入「惊群/抖动」—— 高频比分跳变 (网球每分都跳) 可能让决策环被频繁唤醒。缓解: `tick_floor_ms` 同时也是「最小间隔」(coalescing: 两次唤醒间至少隔 floor), 把突发事件合并成一次全扫。3-4 人天 + 测试。

**为什么这条路径对**: E1/E2 拿到 80% 的延迟收益 (砍 tick + 打通事件码), 且零/低风险随时可上。E3 是锦上添花, 收益被上游 1s 颗粒度封顶, 放最后、可选。**绝不一次性把 RunLoop 重写成纯事件驱动** —— 那会同时动唤醒机制 + 失去 tick 兜底 + 惊群风险, 三个变量一起变, 出问题无法二分定位。

---

## 5. 序列模型推理集成 (裁决)

### 5.1 per-market tick 缓冲 — 复用 FeatureHistory, 不新建

**裁决: 不为序列模型新建 ring buffer。** `FeatureHistory` (128 样本定长 ring) + paper_loop 已有的 `ts_history_`/`ts_history_no_`/`game_history_` per-condition map **就是序列输入的物理载体**。它已经是 observe-always + PIT-safe + 单 writer 无锁 + R-12 bounded。

缺的只是一个**「ring → 序列 tensor」的导出函数**: 给定 window + 序列长度 L (建议 L=16-32, 覆盖 ~30-60s @ 2-4s/帧), 从 ring 取最近 L 个样本, 按特征列展平成 `[1, L, F]` tensor。这是纯函数, 加在 `feature_history.hpp` 或新 `seq_feature_extract.hpp`。

### 5.2 内存预算 (诚实算账)

- 现状 ring: `FeatureHistory` = 128 × `Sample`(6 double = 48B) = ~6KB/condition。三个 map (YES/NO/game) ≈ 18KB/condition。
- 296 市场 → ~5.3MB。**完全可接受** (服务器 15GiB)。
- 序列 tensor 是推理时**栈上临时构造** (L×F floats, L=32/F=110 → ~14KB), 不常驻。零额外常驻预算。
- **裁决: 内存非约束。不需要为序列模型做任何内存优化。** ring 容量 128 足够 (覆盖 ~4-10min, 远超 5-60s horizon)。

### 5.3 有状态 vs 无状态推理 (关键技术裁决)

**裁决: 用「无状态 + 滑窗张量」, 不用「有状态 GRU hidden carry」。**

- 有状态推理 (LSTM/GRU 跨调用 carry hidden state) 的线程安全 + per-market state 管理 + 热加载时 state 怎么办 (换模型 hidden 维度变了) = **一堆坑**, 且与 `HotSwapHolder` 的「无状态 shared_ptr swap」语义冲突。
- 无状态: 每次推理喂完整 `[1, L, F]` 窗口, 模型内部自己跑 RNN/Transformer over L, 输出当前预测。模型对调用方是**纯函数** (同 `OnnxFairValueModel::predict`)。热加载、多线程、回测 replay 全部天然安全 (BR-1)。
- 代价: 每次推理重算 L 步 (而非增量 1 步)。L=32 的小 GRU/Transformer 在 CPU 上单次推理 ~亚毫秒级, 决策环每 markets×横扫即便全做也够 (见 §5.4 频率控制)。**这点算力换来的工程简洁性, 绝对值。**

### 5.4 ONNX 跑 GRU/LSTM 算子可行性 + 推理频率

- **算子可行性: 没问题。** onnxruntime 原生支持 `LSTM`/`GRU`/`RNN`/`Transformer`(MatMul+Softmax+LayerNorm) 全套算子, opset 7+ 即有 LSTM/GRU。M5/MPS (Apple Metal) 训练导出标准 ONNX, 服务器 CPU EP 推理。**唯一前提: 服务器装 onnxruntime** (CLAUDE.md §13 已标待装; CMake `find_library` 命中即编真模型, 不命中 stub fallback — 已有降级路径)。
- **推理频率控制 (重要)**: 序列模型不要每 tick 每 market 都跑。**只在「有事件 + 有 sharp + 候选套利边」的 market 跑** (E1 透传的 state_code + 现有 `fair_sharp_yes>=0` 门已能筛)。296 市场里同时在套利窗口的通常个位数 → 序列推理 QPS 很低, CPU 无压力。
- **裁决: 序列模型走独立 `SeqArbModel ml_holder` (已有 `HotSwapHolder` 模式), 与 `FairValueModel` 物理并列、严禁 blend** (seq_arb_model.hpp 注释已立此纪律, 对的)。FairValue 出结算胜率 (锚 fair), SeqArb 出 horizon mid 移动 (出/平仓 timing 信号), 两个正交用途。

### 5.5 训练→导出链路

- M5/MPS 训练 (Python, 离线, CLAUDE.md §12.4 许可) → `torch.onnx.export` / tf2onnx → ONNX 文件 → 服务器 watcher 热加载。**链路与现有 FairValue 残差模型完全同构** (auto-train 编排线程已存在), 复用。
- 唯一新增: 训练侧要产**序列样本** (per-market tick 轨迹 window), 不是单帧。但 `FeatureRecorder` 已经在录 per-tick 特征向量 (daemon Step), 离线按 condition+时间排序切窗即可。**不需要新采集管道。**

---

## 6. 模块边界裁决 (避免 god module)

方案把「检测 / 事件信号 / 套利决策 / 执行」混在 paper_loop 的危险已经存在 —— `TickOne` 已经 800+ 行, 是潜在 god function。**v3 必须趁机切干净, 否则套利逻辑塞进去就彻底失控。**

```
┌──────────────────────────────────────────────────────────────┐
│ 模块边界 (v3; ★新建, 其余复用/已存在)                            │
├──────────────────────────────────────────────────────────────┤
│ data/        InplayFeedThread (采集)                           │
│              ★EventDetector — state_code/比分跳变检测 (内联在    │
│                采集线程, 纯函数 diff, 产 score_event_seq)        │
│              ScoreSnapshotStore (RCU, 加 cv 唤醒)               │
├──────────────────────────────────────────────────────────────┤
│ ml/          FeatureHistory (ring, 已有)                       │
│              ★seq_feature_extract — ring→[1,L,F] tensor (纯函数) │
│              SeqArbModel (ONNX, 接口已有) + HotSwapHolder        │
├──────────────────────────────────────────────────────────────┤
│ strategy/    arb_signal (已有: ci_low−BE>0 门)                 │
│              ★ArbDecision — 纯函数: (sharp_fair, microprice,    │
│                seq_pred, ts_features) → {进场边, target, 出场限价}│
│                ← 从 TickOne 抽出, 单测覆盖, 不碰线程/IO          │
├──────────────────────────────────────────────────────────────┤
│ control/     position_controller (已有: 限价不追)              │
│ execution/   VirtualExecutor / OpenLegLedger 强平 (已有)        │
│ risk/        RiskGateway (RJ-ARB 门, 已有, 不动)                │
├──────────────────────────────────────────────────────────────┤
│ paper/       PaperLoop — ★瘦身为「编排者」: 冻结快照→调          │
│                ArbDecision(纯)→调 executor→记账。决策逻辑搬出   │
└──────────────────────────────────────────────────────────────┘
```

**核心边界纪律 (我拍板):**
1. **EventDetector 不持有决策逻辑** —— 只产「哪些 market 出了什么事件」的信号 (seq + 可选的 dirty set), 不判断要不要交易。
2. **ArbDecision 是纯函数, 零线程/零 IO/零 now()** (BR-1)。输入全部是已冻结快照 + ring 派生特征 + seq_pred。这样回测 replay 和 live 走同一函数 (红线: 回测实盘同逻辑)。
3. **PaperLoop 退回「编排者」角色** —— 冻结快照、调纯决策、调 executor、记账、发 quote。**不再往 TickOne 里堆 if。** 套利的进/出场判断全在 ArbDecision。
4. **SeqArbModel 与 FairValueModel 物理隔离** (§5.4), 不 blend。

**裁决: ArbDecision 抽取是 v3 的架构关键动作 —— 不抽, paper_loop 必成无法维护的 god module。这一步本身有重构风险 (要保证逐位等价 + 回归测试), 安排在序列模型接入之前, 单独一个 PR。**

---

## 7. 技术栈裁决

| 选型项 | 裁决 | 理由 / 有没有更简的 |
|---|---|---|
| 序列模型 C++ 推理 | **ONNXRuntime, 复用现有 `OnnxSeqArbModel` 封装** | 接口已成形, 条件编译已搭, 装机即活。无更简选项 —— 自己写 GRU 前向 = 重复造轮子 + 数值不一致风险 |
| 序列模型类型 | **小 GRU 或 1-layer Transformer, L=16-32** | 不上大模型 (热路径推理预算 + 训练数据量都不够撑大 Transformer)。小 GRU 在 tick 序列上够用, 算子 onnxruntime 全支持 |
| 推理模式 | **无状态滑窗 (§5.3), 非有状态 carry** | 线程安全 + 热加载 + 回测一致性, 工程简洁性碾压 |
| tick 缓冲数据结构 | **复用 FeatureHistory ring (128 定长), 不新建** | 已 PIT-safe/observe-always/无锁/bounded。新建 = 重复且引入不一致 |
| 事件唤醒原语 | **std::condition_variable + atomic<uint64_t> seq** | 标准库, 零依赖。不引入 eventfd/io_uring (跨平台 + 过度工程) |
| M5 训练→导出 | **PyTorch/TF → torch.onnx.export → 文件 → watcher 热加载** | 与现有 FairValue 残差链路同构, 复用 auto-train 编排 |
| 训练算力 | M5/MPS 本地训练, 服务器 CPU EP 推理 | onnxruntime CPU EP 对小 GRU 亚毫秒级, 服务器无 GPU 也够 |

**唯一技术栈前置动作: 服务器装 onnxruntime** (现 CMake 不命中走 stub, 套利分支 fail-safe 不发单)。这是 P3 的硬前提, 老吴 toolstack 已有装机经验 (laowu-toolstack-install)。

**没有需要引入 Rust / 新框架 / 新依赖的地方。** 全部标准库 C++20 + 已有 onnxruntime。符合 CLAUDE.md 语言纪律。

---

## 8. 目标架构 + 最小步演进 (汇总)

```
演进步骤 (每步独立可上/可回退; ★=风险标注):

P0  E1: gs_state_code 透传 EventScore          [1人天, 零风险, 纯加法]
        + 测试组被动观测 PM acceptingOrders 进球连续性 (引擎A 生死命门)
        ──────────────────────────────────────────────────────────
P1  E2: tick 500→250ms + 内联 EventDetector(只计数+日志)  [2-3人天, 低风险]
        sharp 覆盖/新鲜度提升 (量化/数据部, 不在我边界)
        ──────────────────────────────────────────────────────────
P1.5 ★ ArbDecision 抽取 (paper_loop 瘦身, 逐位等价 + 回归)  [3-5人天, 中风险]
        ← 架构关键, 防 god module; 序列模型接入前必做
        ──────────────────────────────────────────────────────────
P2  E3: cv 事件唤醒接入 RunLoop (留 tick_floor 兜底+coalescing) [3-4人天, 中风险]
        ★最大风险: 惊群/抖动 (网球高频跳分); coalescing 缓解
        引擎A 套利: bet365 偏见修正 + G2 真进球验证 + walk-forward(量化)
        ──────────────────────────────────────────────────────────
P3  序列模型: 服务器装 onnxruntime → seq_feature_extract(ring→tensor)
        → SeqArbModel ONNX 接入 (独立 holder, 不 blend FairValue) [5-8人天]
        训练侧产序列样本 (复用 FeatureRecorder, 离线切窗)
        ──────────────────────────────────────────────────────────
P4  A类瞬时锁套利快路径 + 真钱开闸 (老韩 RM + 小白安全会签, §8.1)
```

### 哪步风险最大 (诚实标注)

1. **P2 (E3 cv 唤醒) 是线程层最大风险** —— 改了决策环的唤醒机制。惊群、coalescing 边界、与 stop_token 的交互、tick_floor 兜底是否真覆盖 book-only 变化, 都要测。缓解: 保留 tick_floor 既是兜底也是最小间隔, 出问题可一行配置退回纯轮询 (E2 状态)。
2. **P1.5 (ArbDecision 抽取) 是模块层最大风险** —— 从 800 行 TickOne 抽纯函数, 要保证逐位等价 (现有测试 + 新单测双保险)。这是「重构既有热路径」, 比加新模块险。但**不做的风险更大** (god module 永久化)。
3. **P3 序列模型本身风险被 stub fallback 兜住** —— ONNX 没装/没训出来 → StubSeqArbModel 恒 ok=false → 套利分支不发单。**工程上是 fail-safe 的, 真正风险在量化侧 (模型有没有 alpha, 不在我边界, 归小梁)。**

### 全程不变量 (我守的边界)

- R-12: WSS io_thread 永不阻塞 (本方案的事件检测在采集线程, 不碰 WSS loop)。
- 单决策线程不拆 (避免 per-market 锁竞争; TickAll 入口冻结 RCU 已解决一致性)。
- 序列模型无状态 + 纯函数推理 (BR-1: 回测实盘同逻辑)。
- ArbDecision 纯函数零 IO (可单测 + 可 replay)。

---

## 9. 给老郭 (否决级总判) 的交接

我的裁决聚焦**系统/线程/模块/技术栈可行性**, 结论是「可行, 走最小步演进, 不大爆炸重构」。以下归你/红线判定, 我不越界:

- **R-12 适配性**: 事件检测内联采集线程 (非 WSS loop), notify ns 级 — 我判合规, 请你复核红线原文。
- **R-20 4ts**: 事件检测用上游 data_source_ts diff (禁 now()), EventScore 透传链已有 4ts — 沿用现状。
- **§8.1 加性 carve-out**: gs_state_code 透传是 struct 末尾纯加字段 + 通知下游 → 我判走普通 PR (不触发 R-4 全审计)。请确认。
- **真钱开闸**: P4 才碰, 老韩 RM + 小白安全会签 — 与本架构评审解耦。
- **ML-R**: SeqArb stub 永不驱动决策 (接口已立), 与 FairValue 同纪律 — 合规。

---

## 10. 一页结论

- **方案 v3 工程可行, 地基 ~70% 已落地** (ring/热加载/ONNX 接口/RCU/seq 接口全有)。
- **纠正方案核心误判**: 痛点真凶不是 paper_loop 500ms tick, 是 inplay feed **1s 拉取颗粒度**(ToS 硬顶)。事件驱动重构的延迟收益被上游封顶, 不值得大爆炸重构。
- **裁决: 局部事件唤醒 + 缩 tick (E1→E2→E3) 三小步, 拿 80% 收益于低风险。**
- **线程**: 不新增决策线程; 事件检测内联采集线程; cv 唤醒 + tick_floor 兜底 (book-only 变化靠 floor 兜)。
- **模块**: 趁机抽 ArbDecision 纯函数, 救 paper_loop 不成 god module (P1.5, 中风险但必做)。
- **序列模型**: 复用 FeatureHistory ring + 无状态滑窗 ONNX 推理, 内存非约束 (~5MB), 装机即活。
- **技术栈**: 全 C++20 + 现有 onnxruntime, 零新依赖, 无 Rust。
- **最大风险**: P2 cv 唤醒 (惊群) + P1.5 god function 抽取 (逐位等价)。序列模型本身被 stub fallback 兜住, 真风险在量化侧 alpha (不在我边界)。

工程量级合计约 **14-21 人天** (不含量化侧模型训练/验证 + 数据侧 sharp 覆盖提升, 那些在小梁/小余边界)。
