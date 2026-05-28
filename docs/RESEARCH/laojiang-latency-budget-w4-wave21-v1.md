# 延迟预算 + benchmark 框架反向需求 v1 (W4 Wave 21)

- Owner: 老姜 (performance-owner)
- Last review: 2026-05-28
- 关联前作: docs/RESEARCH/laojiang-latency-budget-v1.md (Sprint-1 S1-011 内/外环二分版, 仍生效)
- 关联本作 code: tests/perf/ + scripts/perf_compare.py + .github/workflows/perf-regression.yml
- 验收人: 老周 (chief-architect), 老雷 (GM)
- 协作 (不耻下问):
  - WAL fsync 协议 / group commit batch 语义 @老王
  - SPSC capacity / cache-line align @小石
  - 跨洋 RTT 实测 @小冯 (W3 已落, 引用)
  - vCPU 分配 / event loop 边界 @老周

---

## 0. TL;DR

W4 后端 5420 行 C++ 落地 (老韩 RM / 老唐 audit / 小蒋 paper signer + virtual matcher / 小段 goalserve client / 小卢 P0-01), **除小肖 SlippageModel p99 = 6.5ns 已 bench, 其余全部 TBD**. 本文档:

1. 给 9 个 hot path 模块定 **p99 预算**
2. 落 **Google Benchmark framework** (6 bench .cpp + CMake + scripts)
3. 落 **CI perf 回归门禁** (PR p99 退化 > 10% → fail)
4. 反向给 5 个后端 owner 提 **hard ask**

总目标 (老周 v0.6 M1 验收): **WSS recv → VirtualFill emit p99 < 50ms**. 本文档拆分到模块级 budget 后, **本机 critical path 合计 p99 < 2ms**, 留 48ms 余量给跨洋 jitter / WSS reconnect / OS scheduling.

---

## 1. 端到端 latency budget (WSS recv → VirtualFill emit)

| # | 阶段 | 模块 / 文件 | Owner | p99 预算 | 备注 |
|---|---|---|---|---|---|
| 1 | WSS recv → JSON parse | 小赵 simdjson (W5) | 200 us | PM book delta ~ 4KB |
| 2 | parse → SPSC enqueue | 小石 ring (W5) | 1 us | rigtorp::SPSCQueue, 2^n + cache-line align |
| 3 | SPSC dequeue → Signal tick | 小卢/小程 `p0_01_pinnacle_no_vig` | 500 us | no-vig + 5 触发 AND + Kelly·0.25 |
| 4 | Signal → RM evaluate | 老韩 `risk_gateway` | 100 us | 21 enum + slippage + sub_reason short-circuit |
| 5 | RM → Audit emit | 老唐 `audit_emitter` | 50 us | hash chain stub + WAL append (含 PIT) |
| 6 | RM → PaperSigner.Sign | 小蒋 `paper_signer` | 50 us | mock sig + nonce + virtual gas (不含 confirm wait) |
| 7 | PaperSigner → VirtualMatcher.Match | 小蒋 `virtual_matcher` | 30 us | SlippageModel + Bernoulli draw |
| 8 | VirtualMatcher → fill emit (paper_audit) | 小蒋 (走 §5) | 50 us | 复用 audit_emitter |
| 9 | Fill → Position Ledger update | 老周 W5 | 30 us | atomic + ring update |
| 10 | WAL fsync (group commit, batch 64 OR 1ms) | 老王 `wal_writer` | 1 ms | **不在 critical path**, async commit thread |
| 11 | ML hook (async, SPSC) | 小邓 W5 | 不阻塞 | 独立 vCPU4 consumer, 不挡 critical path |
| 12 | Polygon confirm watcher | 小蒋 `virtual_confirm_watcher` | **2s** | **不在 critical path**, async vCPU3 |
| **合计 critical (1+2+3+4+5+6+7+8+9)** | | | | **~1.0 ms p99** | 远低于 50ms M1 目标 |

### 余量分配 (50ms - 1ms = 49ms)

- 跨洋 WSS network jitter: ~5-15ms (小冯 W3 实测, US east → AWS 200ms ± 50ms RTT, 我们只关心抖动不关心 RTT)
- WSS reconnect bursts: ~5-10ms (老李 R-12 backoff)
- OS scheduling / page fault / minor GC-like jitter: ~5ms (老姜 CI 跑 huge page + jemalloc 后实测)
- **safety margin: ~20ms** (避免 M1 撕单)

### 红线

- step 1-9 任一 p99 超预算 50% → P1 incident (告警 + 老周 review)
- step 1-9 累计 p99 超 5ms → P0 (M1 验收风险)
- step 10/11/12 阻塞到 critical path (event loop 同步 wait fsync / confirm) → P0 (R-12 违反)

---

## 2. 模块级 p99 预算 + 当前实测 (9 模块)

| # | 模块 | 函数 | Owner | p99 预算 | 当前实测 | bench .cpp | 风险点 |
|---|---|---|---|---|---|---|---|
| M1 | SlippageModel | `compute()` | 小肖 | **50 ns** | **6.5 ns ✓ (W3)** | tests/bench/bench_slippage_model.cpp (老旧, 跑通) + tests/perf/bench_slippage.cpp (新, baseline JSON) | constexpr 优化已用足 |
| M2 | RiskGateway | `evaluate()` | 老韩 | **100 us** | TBD | tests/perf/bench_risk_gateway.cpp | 21 enum short-circuit, 信号 / 仓位 unordered_map 热查 |
| M3 | AuditEmitter | `emit_decision()` | 老唐 | **50 us** | TBD | tests/perf/bench_audit_emitter.cpp | hash chain stub + WalAppend sync, 大头在 framework Append |
| M4 | PaperSigner | `Sign()` (不含 confirm wait) | 小蒋 | **50 us** | TBD | tests/perf/bench_paper_signer.cpp | nonce atomic + 32B sig copy + gas const, 应很轻 |
| M5 | VirtualMatcher | `Match()` | 小蒋 | **30 us** | TBD | tests/perf/bench_virtual_matcher.cpp | SlippageModel.compute + mt19937_64.draw + R-20 透传 |
| M6 | Goalserve client | `BuildUrl()` + `ParseLastUpdateNs()` | 小段 | **1 ms (parse_batch)** / **5 us (BuildUrl)** | TBD | tests/perf/bench_goalserve_parse.cpp | W4 stub 不打 HTTP, 仅测 parse helpers |
| M7 | WAL Writer | `Append()` (非 fsync) | 老王 | **5 us** | TBD (老王 W3 自测过 6us) | (本 wave 暂不落, 因 wal_writer 还在 framework skeleton; W5 老王 自落) | seq atomic + frame copy + ring write |
| M8 | WAL Writer | group commit fsync | 老王 | **1 ms** | TBD | 同上 | 配 batch=64 OR 1ms; commit thread vCPU2 |
| M9 | Signal P0-01 | `tick()` | 小卢/小程 | **500 us** | TBD | (W4 Wave 21 暂不落 — 5 条件需要 mock IPinnacleSource/IPmSnapshotSource, 单独 W5 wave 落) | unordered_map 三次 lookup + Kelly 浮点 + clip |

**本 Wave 21 落 6 个 bench .cpp** (M1, M2, M3, M4, M5, M6).
**M7/M8 (WAL) 由老王 W5 自落** — 因 framework 模板内联, 单独 bench TU 需要 #include .cpp, 应由 owner 主导.
**M9 (Signal) 留 W5 wave** — 需要 mock 3 source 设备, 与小程/小卢 协作.

### 红线 (PR 必过)

每模块 p99 退化 > 10% → PR fail (本 wave 落 CI gate, 见 §5).

---

## 3. 跨洋 jitter budget (小冯 W3 实测引用)

小冯 W3 报告 (docs/RESEARCH/xiaofeng-*) 实测:
- US east 国内 → AWS Polygon RPC: **RTT ~200ms ± 50ms** (公网, 不走专线)
- Polymarket gamma REST: ~180ms ± 30ms
- Polymarket WSS (clob): ~190ms 初连握手, 之后 keepalive ~210ms RTT
- Goalserve REST (欧洲): ~250ms ± 60ms

### 对策 (架构 / 不在 bench 内, 留给 W5+ 实现)

1. **关键路径禁止跨洋同步 RPC**
   - RM/Signal/Audit 决策**绝不**依赖 Polygon RPC
   - confirm watcher 在 vCPU3 异步, 与 critical path 物理隔离 (老周 v0.6)

2. **WSS heartbeat 跨洋容忍**
   - send ping: 10s (vs 同地 1s)
   - timeout: 30s (vs 同地 5s)
   - reconnect backoff: 1s → 2s → 5s → 10s 上限 (R-12)

3. **batch RPC 调用**
   - confirm batch: 32 笔合并 eth_call (减少 RTT 次数)
   - gas estimate: 缓存 60s (Polygon 网络费稳定)

4. **本机 critical path 全部 RDTSC 测**, **跨洋 RTT 单独埋点**, 不混入 p99 budget.

---

## 4. 性能红线 (PR 回归门禁)

任何 PR 必过 perf-regression CI:

| 指标 | warning | error (PR block) | P0 (post-mortem) |
|---|---|---|---|
| 模块 p99 退化 (vs main baseline) | +5% | **+10%** | +20% |
| 模块 p50 退化 | +10% | +25% | +50% |
| 内存 RSS 增长 | +10% | +25% | +50% |
| WAL fsync 频率 | > 50 Hz | > 100 Hz (group commit 失效) | > 1000 Hz (每 record fsync) |
| Benchmark 新失败 (现 6 + 后续 9 个) | — | **任一 1 个** | — |

### Waiver 机制

故意慢的改动 (e.g. 新增风控规则 / 大型 audit field), PR 描述里写:

```
perf-waiver: <module> +<X>%  reason: <一句话>
```

老姜 review 批. 单 PR ≤ 2 个 waiver, 跨多个模块 → 强制开 ADR.

---

## 5. CI 集成 (落地, 本 wave 真代码)

新工作流 `.github/workflows/perf-regression.yml` 触发条件:
- PR vs main
- main push 之后, 跑一次更新 baseline (推 `tests/perf/baselines/main.json`)

步骤 (单 job, ubuntu-latest):
1. checkout PR
2. cmake -DSTCPP_BUILD_BENCH=ON -DCMAKE_BUILD_TYPE=Release
3. cmake --build build --target bench_*
4. 跑 6 个 bench, 输出 JSON 到 `build/perf-results/`
5. checkout main, 同样跑一次 (cache 命中可跳)
6. `scripts/perf_compare.py build/perf-results/ tests/perf/baselines/main.json` 输出 markdown diff
7. p99 退化 > 10% → exit 1, PR block

详见本仓库 `.github/workflows/perf-regression.yml`.

---

## 6. 给后端 owner 的 5 个 hard ask

### Ask 1 (老韩 RM): evaluate() 函数标识

请在 `risk_gateway.hpp` 的 `evaluate()` 加 `__attribute__((noinline))` 编译 hint (仅 `STCPP_PERF_BENCH` define 下, 不影响生产):

```cpp
#ifdef STCPP_PERF_BENCH
__attribute__((noinline))
#endif
[[nodiscard]] RiskDecision evaluate(OrderIntent const& intent) noexcept;
```

理由: perf bench 时需要精准测单次调用 latency, 不被编译器 inline 进 bench loop 影响.

**ETA: W5 第一天**. 不影响生产 (无 STCPP_PERF_BENCH define 时 noinline 不生效).

---

### Ask 2 (老唐 AuditEmitter): observability metric 字段

请在 `audit_emitter.hpp` 暴露:

```cpp
[[nodiscard]] std::uint64_t last_emit_latency_ns() const noexcept;
[[nodiscard]] std::uint64_t emitted_count() const noexcept;  // ✓ 已有
```

`emit_*()` 内部用 `pit::NowMonotonicNs()` 起止采样 (overhead ~20ns), 存最近一次结果到 atomic. 双用:
- observability metric (W5 老周 prometheus expose)
- perf bench 不需要外部 RDTSC 二次测 (减少 instrumentation 偏差)

**ETA: W5 中**. 老唐 + 老周 协调.

---

### Ask 3 (小蒋 PaperSigner): live mode 切真 ECDSA 时必须异步

**已撤销 — paper mode 现状 ok.**

老姜原以为 `VirtualConfirmWatcher::Wait()` 真睡 2s, 实读 `src/stcpp/signer/paper/paper_signer.cpp:Wait` 后发现:
> "注: 真等 2s 会让单测慢, 这里只算 virtual ts, 不真 sleep."

实测 `BM_Paper_Sign_FullPath` = **1us** (远低于 50us 预算). 小蒋 W4 W4 设计正确.

**真 Ask 3 (W5+ live mode)**: live mode 切真 secp256k1 ECDSA (~80us p99 v1 估算) + 真 Polygon
`eth_call confirm watch` (200ms+ RTT 跨洋), 此时必须切异步:
- `LiveSigner::Sign()` 仅返回 signed tx 串 (≤ 100us)
- `LiveConfirmWatcher::Watch(tx_hash)` 走独立 vCPU3 worker, callback / promise 返
- caller (orchestrator) 处理 confirm 与 Sign 解耦

**ETA: W6 live mode 进场前**. 小蒋 + 老周 (vCPU3 worker pool 调度) 协调.

(本 wave bench `BM_Paper_Sign_FullPath_INCLUDES_CONFIRM_2S` 名字保留, 实测 ≤ 1us, 命名是
为了 W6 live mode 切换时一眼能看见 "为什么突然 2s")

---

### Ask 4 (老王 WAL): group commit metric

请在 `wal_writer.hpp` 暴露:

```cpp
struct GroupCommitStats {
    std::uint64_t commit_batch_size_p50;
    std::uint64_t commit_batch_size_p99;
    std::uint64_t commit_latency_ns_p50;
    std::uint64_t commit_latency_ns_p99;
    std::uint64_t commits_per_sec;
};
[[nodiscard]] GroupCommitStats stats() const noexcept;
```

理由:
1. group commit batch=64 OR 1ms 配置是否生效 — 看 `commits_per_sec` 是否 < 100Hz
2. p99 fsync latency — 看 `commit_latency_ns_p99` 是否 ≤ 1ms
3. observability + bench 双用

**ETA: W5 第二周 (group commit 真上线时同步)**.

---

### Ask 5 (小石 SPSC ring): capacity hard constraint

W5 落 ring 时必须:

```cpp
// 静态约束 (compile-time)
static_assert(N > 0 && (N & (N - 1)) == 0, "SPSC capacity 必须 2^n");

// Cache-line align (rigtorp 已自带, 显式保证)
struct alignas(64) SPSCQueue { ... };
```

理由:
- 2^n → modulo 退化为 bitmask, p99 enqueue 从 ~10ns 降到 ~5ns
- cache-line align → 防 false sharing, 多核 reader/writer 抢同一 cache line p99 从 ~100ns 涨到 ~1us

**ETA: W5 第一天**. 小石 落 ring framework 时 enforce, 老姜 W5 wave bench 验证.

---

## 7. Bench framework 设计原则 (落地的 6 bench .cpp 共同约定)

### 7.1 命名 + 文件结构

```
tests/perf/
├── CMakeLists.txt              # Google Benchmark FetchContent + 6 target
├── README.md                   # 跑法 / baseline 协议
├── bench_slippage.cpp          # M1 SlippageModel (复测 + JSON output)
├── bench_risk_gateway.cpp      # M2 RiskGateway (21 reject path / approved path)
├── bench_audit_emitter.cpp     # M3 AuditEmitter (12 AET / hash chain)
├── bench_paper_signer.cpp      # M4 PaperSigner (sign-only, 不跑 confirm)
├── bench_virtual_matcher.cpp   # M5 VirtualMatcher (Bernoulli draw + Slippage)
├── bench_goalserve_parse.cpp   # M6 Goalserve URL build + parse helper
└── baselines/
    └── .gitkeep                # main.json 由 CI 写入 (artifact / commit on push main)
```

### 7.2 编译选项 (统一)

```cmake
target_compile_options(bench_* PRIVATE -O2 -DNDEBUG -DSTCPP_PERF_BENCH=1)
```

- `-O2`: 与生产对齐 (Release build, 不开 -O3 因为生产用 -O2)
- `-DNDEBUG`: 关 assert
- `-DSTCPP_PERF_BENCH=1`: 触发 §6 Ask 1 的 noinline (老韩 evaluate 用)

### 7.3 输出 JSON 格式 (Google Benchmark 标准)

```
build/bench_* --benchmark_format=json --benchmark_out=build/perf-results/<bench>.json
```

JSON 结构示例:
```json
{
  "benchmarks": [
    {
      "name": "BM_RiskGateway_Approved",
      "real_time": 32500.0,
      "cpu_time": 32100.0,
      "time_unit": "ns",
      ...
    }
  ]
}
```

`scripts/perf_compare.py` 提 `name + cpu_time`, 比 baseline (median + p99 / iteration · 仅 cpu_time).

### 7.4 RDTSC vs Google Benchmark `cpu_time`

- macOS dev 本地: `--benchmark_min_time=2s --benchmark_repetitions=5` 取 median
- Linux CI (ubuntu-latest): 同上, 但 `--benchmark_enable_random_interleaving=true` 抗噪
- 不直接用 RDTSC (跨平台兼容; Google Benchmark 内部用 `chrono::steady_clock`, 已足够 ns 级)

### 7.5 测试数据 + 4 ts (R-20)

所有 bench input 必带 4 ts:
- `event_ts < data_source_ts < ingestion_ts < as_of_ts < now`
- bench 自带 helper `MakeFreshTs(t_now)` 灌入合法 ts (复用单测 helper 模式)

---

## 8. 风险 + 未决

| 风险 | 影响 | 缓解 | Owner |
|---|---|---|---|
| Google Benchmark FetchContent v1.8.3 与 ubuntu-latest CI cmake 3.27+ 兼容 | bench build 失败 | 已与 tests/bench (W3) 同 tag, 已验证 | 老姜 |
| paper signer Sign() 内含 ~2s confirm wait (现 bench 跳过) | 真 critical path 不达标 | §6 Ask 3 改异步 | 小蒋 + 老周 |
| audit emitter 内部 WalWriter.Append 写文件 → bench 测出来包含 IO | 不纯净 CPU latency | bench 用 in-mem WalWriter (write to /dev/null path 或 tmpfs) | 老姜 (本 wave 落: tmpfs path `/tmp/stcpp-perf/`) |
| macOS dev 本地与 Linux CI 数据不可比 | 误报 regression | baseline 仅由 Linux CI 产, dev 本地仅参考 | 老姜 |
| baseline 抖动 (单次 run 噪声) | 误报 | 5 次 median + interleaving + 至少 2s warmup | 老姜 |
| TODO: bench_signal_p0_01 + bench_wal_append + bench_wal_fsync 缺 | M7/M8/M9 无门禁 | W5 wave 22 补 | 老王 + 小程 + 老姜 |

---

## 9. 验收标准 (W4 Wave 21 完成认定)

- [ ] §1 端到端 budget 表通过老周 review (W5 周一会签)
- [ ] §2 9 模块预算入 SSOT (本文档)
- [ ] §5 CI workflow `perf-regression.yml` 跑通一次 (基线写入 main.json)
- [ ] §6 5 hard ask 各 owner 给 ETA 回复 (≤ W5 周三)
- [ ] §7 6 bench .cpp 落地 + 跑通 (`cmake -DSTCPP_BUILD_BENCH=ON && ctest -L bench`)
- [ ] W5 wave 22 backlog: bench_signal_p0_01 + bench_wal (Owner: 老姜 + 小卢 + 老王)

---

## 附 A: 与 Sprint-1 latency-budget-v1 的关系

- Sprint-1 v1 (200 行, 老姜 + 老周 + 老陈) 给的是 **架构级二分**: 决策内环 < 500us / 数据外环 < 20ms. **W4 Wave 21 (本文档) 继承且不冲突**.
- v1 §1 阶段 6 "信号计算 p99 80us" → 本文档 M9 Signal P0-01 500us (扩大, 因 5 触发条件 + Kelly + clip; 仍在内环 500us 内**整体 < 500us** 总预算下 — 因为 v1 §1 是包含 simdjson parse + book apply 的, 本文档 critical path 用的是 §1 端到端 50ms 大盘)
- v1 §1 阶段 8 ECDSA 签名 80us — paper mode 用 mock sig (~50us, 见 M4), live mode (M9 后续) 切真 secp256k1 时回到 v1 假设, 单独 wave 测
- v1 §4 CI 门禁机制 (L1/L2/L3 三层) → 本文档 §5 落地 **L1 micro-bench per-PR** 部分; L2 nightly + L3 weekly 留 W5+

---

## 附 B: 参考资料 (本 wave 引用)

- Google Benchmark v1.8.3 (`https://github.com/google/benchmark`)
- LMAX Disruptor (Thompson 2011) — SPSC ns 级 enqueue
- rigtorp::SPSCQueue (github.com/rigtorp/SPSCQueue) — 小石 W5 落地选型
- 小冯 W3 网络实测 (docs/RESEARCH/xiaofeng-*)
- 小肖 W3 bench v1 (tests/bench/bench_slippage_model.cpp) — 老姜 W4 Wave 21 复测 + JSON 化
- ADR R-12 (event loop 同步 IO 禁) — §6 Ask 3 的根据
- 老周 v0.6 M1 验收 — §1 50ms 总预算的来源
