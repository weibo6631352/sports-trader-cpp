# vCPU Pin Baseline + S2-011 压测计划 v1

- Owner: 老姜 (performance-owner, A 单元 IC)
- Last review: 2026-06-01
- 关联 ADR: docs/ADR/2026-06-01-adr-015-vcpu-pin.md (paper 现状不强 pin)
- 关联 bench: tests/perf/bench_e2e_latency.cpp / bench_spsc_latency.cpp
- 关联前作: docs/RESEARCH/laojiang-latency-budget-w4-wave21-v1.md (9 模块预算)
- 关联会议: 2026-06-01-code-review-summit-v1.md §3.2 决议 #2
- 协作:
  - WAL fsync bg thread 行为 @老王
  - SPSC W6 framework @小石
  - paper engine E2E @小蒋
  - vCPU 拓扑 @老周

---

## 0. TL;DR

ADR-015 决议 (GM + 老郭 Wave 27 投票 4C): paper 阶段维持 **main + 4 WAL bg = 5 thread**,
不强 pin 7 vCPU. 跨洋 RTT 200ms 是绝对瓶颈, 本机 e2e p99 = 3.9us 余量 12821x.

本文档:
1. 5 thread 现状 CPU 占用 + cache miss + context switch 实测方法
2. 2-3 vCPU pin 与 7 vCPU pin 预估对比
3. S2-011 压测计划 (6/22 EOD)
4. 不达标降级路径 (老胡 PM smell #1)

---

## 1. paper 现状: 5 thread 架构

### 1.1 线程清单 (老王 WAL v0.2 + 老周 v0.6 架构)

| # | 线程名 | 职责 | vCPU affinity | 备注 |
|---|---|---|---|---|
| T0 | main (decision) | WSS recv → RM → Signer → Matcher | 未 pin (OS 调度) | critical path |
| T1 | wal_bg_paper_audit | PaperAudit WAL group commit fsync | 未 pin | 老王 v0.2 4 bg |
| T2 | wal_bg_risk_audit | RiskAudit WAL group commit fsync | 未 pin | 老王 v0.2 |
| T3 | wal_bg_decision | Decision WAL group commit fsync | 未 pin | 老王 v0.2 |
| T4 | wal_bg_pit | PIT WAL group commit fsync | 未 pin | 老王 v0.2 |

注: 小余 W26 申辩 "WAL v0.2 core 6/7 已分配 4 bg fsync" 是对 7 vCPU 拓扑的担忧,
GM 已 ack "paper 现状不强 pin, 仍 5 thread 自由调度".

### 1.2 实测方法 (macOS darwin 25.5 + Linux CI)

macOS (本机 dev):
```
# 获取 paper 进程 PID (假设 binary 名 stcpp_paper)
PID=$(pgrep stcpp_paper)

# 1. 每 thread CPU 占用 (sample 5s)
top -pid $PID -stats pid,command,cpu,th

# 2. 跨核 cache miss rate (dtrace)
#    注: macOS dtrace 需 sudo; CI 走 perf stat (Linux)
sudo dtrace -n '
  profile:::tick-1ms
  / pid == $1 /
  {
    @cache_miss[tid] = count();
  }
  END { printa(@cache_miss); }
' $PID

# 3. context switch 频率
sudo dtrace -n '
  sched:::off-cpu
  / pid == $1 /
  { @cs[tid] = count(); }
  END { printa(@cs); }
' -- -p $PID 5 2>/dev/null
```

Linux CI (ubuntu-latest, 参考 S2-011 实测):
```bash
# 1. CPU 占用 per-thread
ps -p $PID -L -o tid,pcpu,comm

# 2. cache miss rate (perf stat)
sudo perf stat -p $PID -e cache-misses,cache-references,L1-dcache-loads,L1-dcache-load-misses \
    -- sleep 10

# 3. context switch
cat /proc/$PID/status | grep -i ctxt
# 或
pidstat -w -p $PID 1 5
```

### 1.3 预期实测范围 (基于架构推算, S2-011 前估算)

| 指标 | T0 main | T1-T4 WAL bg (各) | 合计 |
|---|---|---|---|
| CPU 占用 (空载 paper) | ~0.1% (等 WSS) | ~0.05% (等 fsync timer) | ~0.3% |
| CPU 占用 (1000 qps 压测) | ~15-30% | ~5-10% | ~50-70% |
| L1 dcache miss rate (压测) | ~2-5% (hot path cached) | ~8-15% (fsync IO path) | — |
| context switch /s (空载) | ~10-50 cs/s | ~5-20 cs/s | ~50-150 cs/s |
| context switch /s (压测) | ~100-500 cs/s | ~50-200 cs/s | ~300-1500 cs/s |

注: 实测数字 S2-011 (6/22) 填入, 本行为估算区间.

---

## 2. 方案对比: 现状 vs 2-3 pin vs 7 pin

### 2.1 评估维度

| 维度 | 现状 (5 thread, 不 pin) | 2-3 vCPU pin (仅 main) | 7 vCPU pin (老周 v0.6) |
|---|---|---|---|
| **决策 latency (p99)** | 3.9us (实测) 估计 +2-10us OS jitter | ~3.9us (主要节省 OS preempt) | ~3.9us (同上, 差异在跨核通信) |
| **WAL bg 抢 core 风险** | 低 (OS 调度, ETL ingest 同竞争) | 中 (若 ETL ingest 绑 vCPU1-2) | 低 (4 WAL bg 各有 vCPU3-6) |
| **跨洋 RTT 掩盖** | 是 (200ms RTT >> 10us jitter) | 是 | 是 |
| **实施复杂度** | 零 (现状) | 低 (1 行 pthread_setaffinity) | 高 (老周 v0.7 §8 11步启动期) |
| **paper 阶段必要性** | ★ 充分 (GM ADR-015) | ☆ 不必要 | ☆ 过度设计 (老周已承认 v0.6) |
| **M5+ live 前** | 需升级 | 需升级 | 直接用 |
| **S2-011 压测风险** | 主要风险: WAL fsync 集中爆发 | 同左 | 低 (但 paper 阶段跑不到) |

### 2.2 2-3 vCPU pin 细化预估 (paper 中期 M3-M4 可选)

如果 S2-011 发现 T0 main 被 T1-T4 WAL bg 抢占导致 p99 > 5ms:
- 可仅 pin T0 main 到 vCPU0 (`pthread_setaffinity_np(t0, {vCPU0})`)
- 老王 WAL bg 4 thread 保持 OS 调度 (不 pin, 避免 ETL ingest 竞争)
- 预期效果: 消除 OS preempt jitter ~1-5us; 不改变 WAL bg 调度

实施前提: S2-011 证明 p99 > 5ms 且瓶颈在 OS jitter 而非 business logic.

### 2.3 7 vCPU pin 路径 (M5+ live 前)

老周 v0.7 §8 启动期 11 步 (老姜 bench 驱动):
1. S2-011 压测数据 → 确认瓶颈 in T0 vs T1-T4 vs ETL ingest
2. 老姜出 7 vCPU pin spec (基于 S2-011 数据)
3. 老周 v0.7 §8 实施 11 步
4. 老姜 re-bench (bench_e2e_latency + bench_spsc_latency)
5. 老周 first review → 老高 PR v1.3 → GM ack

---

## 3. S2-011 压测计划 (6/22 EOD)

### 3.1 压测目标

S2-011 是 W6 末 / M1 评审 (老胡 smell #1 conditional B) 的量化依据:
- 6/22 EOD 提交实测数据
- 老胡 M4.5 时机依据: 若 p99 超 5ms → 触发 2-3 vCPU pin; 超 50ms → NBA only 降级路径

### 3.2 压测矩阵

| 批次 | 笔数 | 并发 | 目的 |
|---|---|---|---|
| S2-011-A | 1,000 笔 | 单 goroutine (顺序) | baseline; paper 最轻负载 |
| S2-011-B | 10,000 笔 | 单 goroutine (顺序) | WAL group commit batch 累积效果 |
| S2-011-C | 100,000 笔 | 单 goroutine (顺序) | WAL fsync 集中爆发 (老王 R-38a 风险) |
| S2-011-D | 10,000 笔 | mock 200ms ± 50ms RTT | 跨洋 WSS RTT 影响 (小冯 W3 实测参数) |
| S2-011-E | 10,000 笔 + WAL burst | 顺序 + forced fsync | WAL fsync 集中爆发模拟 (同 C 但强制每 64 笔一次) |

### 3.3 度量指标

每批次输出:
- p50 / p90 / p99 / p99.9 端到端 latency (WSS recv mock 起 → FillQueue 落)
- WAL fsync 频率 (commit/s, 正常 < 50 Hz; 异常 > 100 Hz)
- context switch 频率 (T0 main, /s)
- L1 dcache miss rate (T0 main)
- throughput (笔/s)

### 3.4 跨洋 WSS RTT 影响模拟 (S2-011-D)

小冯 W3 实测: US east 国内 → AWS Polygon = 200ms ± 50ms RTT.
关键路径禁止跨洋同步 RPC (R-12), 所以 RTT 不进 critical path.
**但**: WSS keepalive + reconnect backoff 会引入 event 到达 jitter.

模拟方法:
```cpp
// bench_e2e_latency.cpp 中增加 WSS jitter 模拟
// 在 WSS recv mock 处加 std::this_thread::sleep_for + uniform_real_distribution(0, 100ms)
// 模拟跨洋 jitter (非 RTT 本身, 而是抖动)
```

预期结论: jitter 影响 p99 不超过 ±5ms (远低于 50ms 上界).

### 3.5 WAL fsync 集中爆发风险 (老王 R-38a)

老王 v0.2 group commit: batch=64 OR 1ms, 正常 < 50 commits/s.
压测 100,000 笔时, 若 WAL 队列积压 → batch size 突破 64 → 单次 fsync IO 阻塞 T1-T4.

风险指标:
- `commit_latency_ns_p99 > 1ms` (超老王 M8 预算)
- `commits_per_sec > 100 Hz` (group commit 失效)

应对:
- 若超: 老王增大 batch size 或降低 WAL kind 数量
- 若严重: paper 阶段临时切 in-mem WAL (不 fsync)

### 3.6 不达标降级路径 (老胡 PM smell #1 引用)

| 实测结果 | 决策 | Owner |
|---|---|---|
| p99 ≤ 5ms | 维持现状 5 thread; 继续 MVP NBA 全盘口 | 老周 + 老胡 ack |
| 5ms < p99 ≤ 50ms | S2-011 后 2 周: 2-3 vCPU pin; 老姜出 spec | 老姜 + 老周 |
| p99 > 50ms (M1 上界) | **MVP NBA only 降级路径** (老胡 smell #1 触发) | 老胡 P0 升级 → GM |
| WAL commits > 100 Hz | 老王 group commit 参数调优 P1 | 老王 + 老周 |
| WAL fsync p99 > 1ms | 老王 R-38a patch P1 | 老王 |

**降级路径细节 (NBA only)**:
- 关闭 NHL / MLB / NFL / Soccer / 其他盘口
- 只跑 NBA Moneyline (最高流量 + 最熟悉)
- 重新评估 vCPU 需求后扩回

---

## 4. 性能预算更新 (v1.2, Wave 28)

基于 ADR-015 决议 (paper 5 thread 不 pin) 更新 W4 Wave 21 预算表:

| # | 阶段 | p99 预算 | W5 末实测 | W6 上界 guard | 备注 |
|---|---|---|---|---|---|
| E2E | WSS recv → FillQueue | < 50 ms | 3.9 us (小宋 fixture) | 50 ms | bench_e2e_latency upper guard |
| E2E | 下界 guard | > 100 ns | — | 100 ns | bench_e2e_latency lower guard (Wave 26 决议 2) |
| SPSC | enqueue (mock) | < 1 us | TBD (W6 bench) | 100 ns per op | bench_spsc_latency upper guard |
| T0 main | OS jitter (无 pin) | < 5 us | ~0 (paper 轻负载) | — | S2-011 实测 |
| T1-T4 | WAL bg fsync p99 | < 1 ms | TBD | — | 老王 M8 预算 |

注: 小宋 fixture 3.9us 是 in-process gtest, 不含真 WSS RTT + 跨洋链路.
    S2-011 加 mock RTT jitter 后预期回落到 10-20us 量级 (仍远低于 50ms).

---

## 5. W6 Owner 执行计划

| 任务 | Owner | ETA | 状态 |
|---|---|---|---|
| bench_e2e_latency.cpp (Wave 28) | 老姜 | W6 EOD | 完成 |
| bench_spsc_latency.cpp (Wave 28) | 老姜 | W6 EOD | 完成 |
| bench_spsc_latency 换真 SPSC | 老姜 + 小石 | 小石 W6 SPSC 就位后 | pending |
| S2-011 实测 (1k/10k/100k 笔) | 老姜 | 6/22 EOD | planned |
| S2-011 跨洋 jitter mock | 老姜 | 6/22 EOD | planned |
| S2-011 WAL burst 测试 | 老姜 + 老王 | 6/22 EOD | planned |
| vCPU pin spec (M5+ 前) | 老姜 | 基于 S2-011 数据后 | blocked on S2-011 |
| 老周 first review (W6 EOD) | 老周 | W6 EOD | pending |
| 老高 PR v1.2 | 老高 | W6 EOD 后 | pending |
| GM ack | 老雷 | W6+1 | pending |

---

## 6. 不耻下问

- @老王: WAL v0.2 group commit batch=64 OR 1ms 触发频率预期多少? R-38a fsync 集中爆发阈值?
- @小石: W6 SPSC framework ETA? 接口是 `SPSCQueue<T, N>` 还是有变化? 老姜 bench_spsc_latency 换真接口时告知
- @小蒋: paper E2E 真链路 (paper_pm_client + virtual_confirm_watcher) 加 bench fixture 需要什么 hook?
- @老周: v0.7 §8 启动期 11 步 vCPU pin 流程草案, 6/22 S2-011 数据出来后我来驱动 — 你先给框架

---

**完。S2-011 6/22 EOD 实测数据将更新本文档为 v1.1.**
