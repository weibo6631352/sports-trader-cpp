# 复盘 tape 整合 + 落盘清理 (2026-06-14)

owner: 老雷 (GM) | 2026-06-14 | 落地完成 (build + ctest 1247 全过)

## 老板原话

> 「我们做的回测脚本需要很多数据 …… 没有整场订单簿的信息吗」
> 「你要的文件 = 一份完整、细粒度、可复盘的市场全程 tape」→ 「对，调研一下如何做，旧的如何清理。」
> 「写吧 全做。合理 优化 性能 彻底 清理干净 不留尾巴债务。」
> 「我觉得写入点会不会太散了，比如我们在决策附近代码不是可以拿到几乎所有的信息吗 …… 决策附近 应该大部分信息都已经处理好了。」

## 一句话

把零散拼出来的「复盘语料」(market_tape 60s 单独扫 + position_path 30s 单独扫 + scores.jsonl 全事件帧 +
fair_cache 桥接) **整合成一份 tape，落盘点就在 TickOne 决策处** —— 决策已算好的簿/fair/源/比赛进度当场落帧，
不再单独扫、不重读 hub、不桥接。旧的重复/无消费者的文件全退役。

## 一、复盘 tape：就在决策处落帧 (老板「写入点别太散」)

### 之前 (散)
- `SampleMarketTape()`：每 60s 单独扫全 catalog，对每盘**重新** `hub_.Read` + 查 sharp/ts/game_prog → 落帧。
- `fair_cache_`：本想 TickOne 算完 fair 缓存给那个独立扫用 = 桥接。
- 问题：决策处 (`TickOne`) **已经**把这盘的 `mkt.yes.book`(完整 OrderBookFeatures)、`p_fair`、`fair_src`、
  `game_row.core_*`(冻结)、比赛进度全算好了。单独再扫一遍 = 重读 + 桥接 + 两处写 = 散。

### 之后 (一处)
`TickOne` 算完最终 fair (过完 sanity/flip/odds-stale/sharp_gate) 后，**当场**调
`MaybeEmitMarketTape(mkt, game_row, p_fair, fair_src_dbg)`：
- **直接用决策已算好的** `mkt.yes.book`(不重读 hub)、`p_fair`、`fair_src`、`game_row.core_*`。
- cond-keyed 因子 (sh_vel/conv/vol、ofi/rvol/mom5、g_remain/sdiff/period、held) 只在**真落帧时**廉价补读。
- `fair_cache_` 删除 (不需要桥接了)。

### 事件驱动 (老板「动了才落 + 心跳兜底」)
不再固定 60s。每盘维护 `tape_state_{last_sharp,last_mid,last_emit_ns,last_core}`，满足任一即落一帧：
- `|sharp − last_sharp| ≥ 0.004` (0.4pp sharp 移动)
- `|mid − last_mid| ≥ 0.004` (0.4pp 市场移动)
- 冻结位翻转 (core_stopped/blocked/finished 变化)
- 距上次落帧 ≥ 20s (心跳兜底，没动也留锚)

→ **崩盘逐变化忠实记录** (favorite 几秒崩，每次移动都落)，**静默盘几乎不写** (只 20s 心跳)。
比旧的 60s 固定**更细**(活跃时)且**更省**(静默时)。"正在比赛"闸保留 (sharp 新鲜度 >120s 跳过，只录 in-play)。

### 新增字段 (老板「账单说清用的哪个源」)
旧 schema (全簿 bid/ask/mid/micro/spread/imb/bd5/ad5 + sharp 全因子 + 比分 + 量化) 之上加 4 个：
- `fair`：决策刻引擎 fair (最终值，过完所有 sanity)。
- `fsrc`：fair 选源字符串 `sharp_inplay/score_prior_blend/market_devig/derivative`。
- `core`：冻结位 bitfield (1=停表 | 2=封盘 | 4=完赛)。
- `held`：这一帧我们是否持有此盘 (0/1) —— 决策标记，复盘可只看持有期。

### 性能 (R-12)
- 决策环零新增网络/磁盘：用已读的簿 (无 `hub_.Read`)，写盘走异步 `journal_writer_`。
- 事件门先判 (cheap: 2 比较 + 1 map lookup)，**不落帧就早退**，昂贵的 depth_metrics/snprintf/string alloc
  只在真落帧时走 → 静默盘零分配。`slot 缓存`在当前盘数 (~24) 无必要 (event-gate 即性能解)。

## 二、fair_src 进账单 (老板「落盘账单说清用 sharp/赔率源/比分源」)

同一个 `fair_src_dbg`，在决策处一次定，流到三处 (一处真相)：
- **tape**：每帧 `fsrc` (上面)。
- **fills_journal (账单)**：`EntryCtx.fair_src` ← 决策刻设 → `FillRow.fair_src` (两条 fill 路径: 同步 + pending live) → JournalFill 落 `fsrc` 字符串。
- **gate_blocks (拒点)**：`LogGateBlock` 经 ectx 落 `fsrc`。

## 三、旧的清理 (老板「彻底 清理干净 不留尾巴债务」)

| 对象 | 处置 | 依据 |
|---|---|---|
| `scores.jsonl` (429MB/天) | **删** writer + 文件 | 生产者 ScoreFrameRecorder，**消费者 0** (backtest_sharp 早改读 market_tape)；我们能交易的盘 tape 全覆盖 |
| `score_frame_recorder.hpp` + test | **删** | 仅为写 scores 而存在；trader_daemon 接线 (include/member/build/start/stop) 全摘 |
| `feature_recorder.hpp` / `feature_vector_recorder.hpp` | 早已删 | ML 2026-06-05 砍后 0 引用 (本次确认无残留) |
| `SamplePositionPaths()` + `position_path.jsonl` writer | **删** | 市场状态与 tape 完全重复 (老板「写入点别太散」)；持仓上下文 avg/qty 在 fills |
| `pos_path_` (C++ tracker) | **留** | 不是给 position_path 用 —— 它喂 fills 账单的 MAE/MFE/hold_sec (结算行)，独立保留 |
| `fills_journal` / `gate_blocks` / `settlements` / ledger snapshot | **留** | 决策事件 / 结局 / 恢复，不可替代 |

净磁盘：tape 事件驱动 ~+15-25MB/天，但退 scores −429MB/天 → **净大降**。

## 四、分析器迁移 (position_path 退役的下游)

- **exit_research.py**：position_path → `fills × market_tape (held 旗界定持有窗) × settlements`。入场 avg/qty/yes/ts 从 fills 聚合，持有期市场轨迹从 tape held==1 帧。伪重复诚实保留 (按独立结算盘数报)。
- **monitor.py**：`pp_conds` 改从 tape held 帧derive (readiness 指标)。
- **backfill_settlements.py**：touched 源 tuple 去掉 position_path (market_tape + fills 已够)。
- **param_research.py** ④：赢家轨迹源 position_path → market_tape held 帧 (字段同名，加 held 过滤)。

历史 `position_path.jsonl` 文件保留 (旧数据可读)；新数据全走 tape。

## 五、验证

- `cmake --build` 全量无错；`ctest` **1247/1247 全过**。
- 部署后核验：`market_tape.jsonl` 出现 `fsrc/core/held` 字段 + 事件驱动行距；`fills_journal` 出现 `fsrc`；
  `scores.jsonl` 不再增长 (writer 已摘)；`position_path.jsonl` 不再增长。

## 关联

- [[laolei-data-infra-plan-inference-2026-06-14]] / [[paper-data-accumulation-plan-2026-06-14]] (本次是其收口：把分散落盘整合成一份决策处 tape)
- `backtest-equivalence-spec-v1.md` 的红线#3 ScoreFrameRecorder 已随回测模块退役 (该 spec 框架 2026-06 治理已裁，scores 是其遗留)。
