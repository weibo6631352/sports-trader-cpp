# 测试覆盖 Gap 报告 v1

- **owner:** 小宋 (test + replay, E 部)
- **last_review:** 2026-05-30
- **适用基线:** main @ 79bfbf4 (942 测试全绿)
- **近期改动参考:** dogfood 整改 (P0-1/P0-2/P0-3/P0-4 + P1-1/P1-2/P1-3/P1-8), de-vig/prior, gamma 覆盖面, 死代码清除
- **用途:** 供 GM 写代码时参考 — 指出当前哪些路径仍缺自动化测试守护

---

## §1 Gap 概述 (按风险排序)

### Gap-1 [最高风险] paper_loop + ScoreStore 真实 in-play 路径从未被测 (has_real_fair=true 分支)

**受影响代码:** `src/stcpp/paper/paper_loop.cpp` `TickOne()` 中 `has_real_fair=true` 分支 (约 L296–L305)

**问题:**
`TickOne()` 在 `has_real_fair=true` 时执行 `blend_prob(p_prior, p_market_devig, conf)` 混合路径，此时 `conf = terminal ? 1.0 : prior_confidence(/*time_frac=*/0.0)`。这里 `time_frac` 被硬编码为 `0.0`（M1 TODO，因为真实 elapsed_sec 尚未从 ScoreSnapshotStore 注入 game_row），意味着即使实际比赛进行了 80% 时钟，`conf` 也恒为 `kBasePriorConfidence=0.15`，先验权重不随时钟增长。

当前所有 `test_paper_loop.cpp` 中的 `T01`–`T14` 均使用 `time_status=NotStarted`，即 `has_real_fair=false` 路径。**`has_real_fair=true` 的任何路径（InPlay 比分驱动、终态确定、time_frac 正确传入）完全没有端到端测试覆盖**。

`test_fair_value_devig_prior.cpp` 的 D11/D12 仅测纯函数层 (`blend_prob` + `inplay_score_prior_yes`)，不覆盖 `TickOne` 的集成语义：`paper_loop` 内 `p_prior = fv_result.prior_yes`（来自 `BaselineFairValueModel`），而不是直接调用 `inplay_score_prior_yes`，且 `conf` 强制 `time_frac=0.0` 与实际 elapsed_sec 脱节。

**风险:** 当 M2 接入真实 Goalserve 数据使 `has_real_fair=true` 时，此分支将在零覆盖下进入生产，prior 置信度错误 (恒 0.15 而非应有的高值)，产生错误 edge 估算。这是 P0-3 问题的变体 — 以前是假阳性，这里可能是置信误标。

**应补测试方向:**
- 构造 `OrderBookSnapshotHub` + `InPlay` game_row (比分 2:0, elapsed_sec=4500, soccer) 注入 PaperLoop，验证 `quote.edge_bps > 0`、`predict_ok=true`、`suggested_notional > 0`
- 验证终态场景 (time_status=Ended, 领先): `quote.fair_value > 0.99`
- 验证 `prior_confidence(time_frac)` 在 `time_frac=0.8` 时的混合权重确实 > `kBasePriorConfidence`（当前 M1 硬编码 0.0 应是一个 TODO 断言）

---

### Gap-2 [高风险] gamma 发现解析器 (`debug_server_main.cpp` 中的私有 namespace 函数) 零测试覆盖

**受影响代码:** `src/stcpp/debug_api/debug_server_main.cpp` 内 `namespace {}` 下的六个静态函数:
- `ExtractJsonStr()`
- `ExtractClobTokenIds()` — 处理两种 gamma 编码格式 (native array vs JSON-encoded string)
- `NormalizeSportsMarketType()`
- `ExtractNextObject()`
- `ExtractMarketsArray()`
- `DiscoverSportsEvents()` / `DiscoverSportsMarketsFlat()`

**问题:** 这些函数是 gamma 体育市场发现的核心解析逻辑，直接决定 token_map 的构建（进而影响 WSS 订阅与所有 book 端点）。目前**零测试覆盖** — 在 `tests/` 全库搜索 `ExtractClobTokenIds`、`ExtractJsonStr`、`DiscoverSports`、`NormalizeSports` 均无结果。

`ExtractClobTokenIds()` 特别脆弱：它需要处理两种 gamma API 编码格式（native JSON array 和 JSON-encoded string `"[\\"tok0\\",\\"tok1\\"]"`），内含手写 unescape 逻辑，任何 off-by-one 都会导致 token_map 为空，WSS 静默不订阅，全部 book 端点返回 `found=false`。

**已有测试中最接近的:** `tests/chaos/rest_timeout/c_rest_02_502_gamma.cpp` 模拟 gamma 502 场景，但不验证解析正确性；`tests/unit/test_coverage_metrics.cpp` 测 `MarketInfoMap` 注入后行为，但 catalog 是手动构造的，不经过解析器。

**风险:** gamma 改变 API 响应格式（已有历史：`clobTokenIds` 曾在 native array 和 JSON string 两种格式间变化）时，解析静默失败，启动后 hub 为空，系统无声音运行却无任何 book 数据。

**应补测试方向:**
- 将六个静态函数移出 `namespace {}` 或提取到可测试头文件，配合 inline fixture JSON 串（native array 格式、JSON string 格式、两个 token、缺 conditionId 等边界）做 unit test
- 特别验证 `ExtractClobTokenIds` 对两种编码的 round-trip
- 验证 `NormalizeSportsMarketType("Moneyline")` 返回 `"moneyline"`，大小写不敏感

---

### Gap-3 [高风险] R-12 的 paper_loop 线程隔离 — r12_sim 全部 placeholder，无真实 SUT 参与

**受影响代码:** `tests/sim/r12_sim/s1_rest_slow_wss_unblock_test.cpp` 至 `s4_vcpu0_burst_test.cpp`

**问题:** 四个 r12 sim 测试全部是 W3 占位 placeholder，直接向 `wss_tick_latencies_ns_` 喂合成样本数据，**没有真实 SUT (system under test) 参与**。注释明确写 `"W3 placeholder: 直接喂样本 (W4 接真 SUT)"`，但截至 79bfbf4 仍未接真 SUT。

`test_paper_loop.cpp` T04/T09 使用 `sleep_for(200ms)` 验证 tick 数量，但完全不测 tick 延迟分布，也不涉及 WSS event loop。`tests/chaos/latency/c_lat_04_paper_e2e.cpp` 有 WSS p99 < 50us 断言，但 `paper_loop` 的 `TickOne` 耗时（包括 `SizingCalculator::compute`、`devig_binary`、`blend_prob` 调用链）对 event loop 的影响未被基准测量。

**风险:** 如果 `TickOne` 在高频场景下超过 100us（R-12 硬上限），会直接触发 R-12 红线。当前没有任何测试能自动发现此回归。

**应补测试方向:**
- 在 `tests/perf/` 补 `bench_paper_loop_tick.cpp`：直接调用 `TickOne` (无 jthread)，用 `std::chrono` 测 1000 次 p99，断言 < 100us
- 将 r12_sim s1–s4 从 placeholder 升级到真实 SUT：用 `LiveBookPublisher` stub 注入帧，实测 hub.Publish() 到 paper_loop.TickOne() 延迟链路

---

### Gap-4 [中高风险] R-20 四时间戳合规 — paper_loop 的 game_row ts 赋值路径无端到端集成验证

**受影响代码:** `src/stcpp/paper/paper_loop.cpp` L251–L255，`TickOne` 内构造 `FeatureStoreGameRow` 时将 4 ts 从 `feat`（OrderBookFeatures hub 快照）复制到 `game_row`

**问题:** 当前 `test_paper_loop.cpp` T03 验证 `LedgerFeatures.ts_chain_ok()`（手动构造），T05 验证 `QuoteFeatures.ts_chain_ok()`（手动构造），但**没有端到端测试验证从 hub snapshot 到 game_row 到 quote 的 4ts 传播链**。

具体缺口：
1. `game_row.event_ts_ns` 来自 `feat.event_ts_ns`（hub 快照）。P0-2 修复后 `ingestion_ts = max(recv, data_source)`，但 game_row 的 4ts 赋值直接用 `feat` 的值，若 feat 的 ts 链在极端情况下失效，game_row 就带着坏 ts 传到 FairValueEstimator 和 QuoteFeatures。
2. `tests/replay/r20/r_r20_violations.cpp` 验证历史 WAL 记录违规检测，但不验证 paper_loop 实时产出的 QuoteFeatures ts 链。
3. 代码在 Step 5（构造 OrderIntent）有 ts 链校验（L370–L376），但 Step 4 的 `PublishQuoteSnapshot` 在校验前执行，意味着 ts 坏的 quote 仍会发出。

**已有覆盖接近点:** `test_score_snapshot_store.cpp` T4 (Goalserve inplay 4ts)，`test_clob_subscriber.cpp` T6 (P0-2 max 修复)。

**应补测试方向:**
- 在 `test_paper_loop.cpp` 补：注入有效 book（`event_ts < data_source_ts < ingestion_ts < as_of_ts`），tick 后读 `quote_hub_->Read(cid)`，断言 quote ts 链单调
- 补边界测试：`ingestion_ts < data_source_ts` 时 TickOne 的 QuotePublish 行为（当前未被 gate，quote 仍会以坏 ts 发出）

---

### Gap-5 [中风险] P0-2 修复的 dogfood 回归守护不完整 — LiveBookPublisher 无独立 unit test

**受影响代码:** `src/stcpp/debug_api/live_book_publisher.hpp`

**问题:** `test_clob_subscriber.cpp` T6 (R-20 P0-2) 验证了 CLOB subscriber 内的跨洋时钟修复，通过 mock transport 注入跨洋场景。但 **`LiveBookPublisher` 没有独立 unit test**。它在 debug_server_main 中负责将 WSS 帧转为 `OrderBookFeatures` 并调用 `hub.Publish()`，其内部的 `recv_ts_ns` 赋值路径（来自 `on_text_frame` 回调的时间戳参数）是否与 P0-2 修复保持一致，没有任何自动化验证。

P0-2 端到端路径（WSS 帧 → CLOB subscriber → hub snapshot → paper_loop game_row ts → quote ts）只有各段独立测试，**缺跨层集成测试**。

**应补测试方向:**
- 对 `LiveBookPublisher` 单独构建 unit test（构造 stub hub），注入跨洋时钟帧，验证 `hub.Read(token_id)` 返回的快照 ts 链正确
- 在 `tests/integration/` 补 P0-2 regression test：从 CLOB subscriber 到 paper_loop quote 的全链路 ts 验证

---

## §2 次要缺口（备查，非立即阻塞）

| 缺口 | 现状 | 风险等级 |
|---|---|---|
| `endpoint_score.cpp` score_store 未命中时输出的 JSON 结构 | `test_observability_endpoints.cpp` 无覆盖 | 低 |
| `devig_binary` 与 `no_token_mid` 从 NO 边 book 提取路径 (`paper_loop.cpp` L236–L241) | 测试固定传 `NaN`，未测双边市场实际 de-vig 流程 | 中 |
| `prior_confidence(time_frac)` 的 `time_frac=0.0` 硬编码（M1 TODO）缺 TODO-断言守护 | 单函数测试已覆盖，集成层无守护 | 中 |
| `debug_server_main.cpp` 启动流程 Step 1–6 无集成测试 | 仅 `test_debug_api_integration.cpp` 测 HttpServer 生命周期 | 中 |

---

## §3 最优先补测试：推荐 Gap-1

**理由:** Gap-1（has_real_fair=true 的 paper_loop 端到端路径）是唯一一个将在下个里程碑（M2 Goalserve 真实接入）被激活的路径，且当前代码存在已知问题（`time_frac=0.0` 硬编码），零覆盖下进生产直接影响 edge 估算正确性，触发类 P0-3 的假信号风险。补测成本低（复用现有 `PaperLoopTest` fixture，注入 InPlay game_row 即可），防御价值最高。

建议 GM 在 `tests/unit/test_paper_loop.cpp` 追加：

```
// T15: has_real_fair=true path — InPlay game_row 注入, 验证 quote.predict_ok 和 edge_bps > 0
// T16: 终态 (Ended, 领先) → fair_value > 0.9, advisory=true 仍拦截 intent
// T17: time_frac=0.0 hardcode TODO 断言 — 当 Goalserve 真实接入后此处应改为真实 elapsed_sec
```

---

## §4 红线缺口速查表

| 红线 | 现有自动化覆盖 | 缺口描述 |
|---|---|---|
| **R-7** (paper mode only build, mode tag) | 充分：`test_paper_signer`, `test_signer_v52/v62`, `test_ingest_raw`, `test_single_instance`, `r11_paper_pollution_test` | 无重大缺口 |
| **R-11** (paper 不污染真账本) | 充分：`r11_paper_pollution_test` (50笔e2e), `test_paper_loop` T02/T10, `r_r11_01`–`r_r11_04` replay | paper_loop has_real_fair=true 路径未验证 (见 Gap-1) |
| **R-12** (WSS event loop 无同步 >100us) | 形式覆盖：r12_sim 全 placeholder；`c_lat_04_paper_e2e` 有 p99 断言但无真实 SUT | 实质性缺口：见 Gap-3 |
| **R-20** (4ts 单调链) | 广泛覆盖：`test_clob_subscriber` T6, `test_score_snapshot_store` T4, `r_r20_violations`, `r_r20_01_normal_7d` | paper_loop game_row 到 quote 端到端 ts 链无集成测试 (见 Gap-4) |

---

*本报告基于代码静态分析（tests/ + src/ 只读），不含运行时数据。供 GM 写代码时参考，不代表 942 测试存在失败。*
