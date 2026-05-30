# 延迟预算拆解 v1 (Sprint-1 S1-011)

- Owner: 老姜 (performance-owner)
- Last review: 2026-05-28
- 验收人: 老周 (chief-architect)
- 协作: 小石 (data-structures-expert), 跨洋 RTT 实测求证 @老陈 (network-ops), 整体架构求证 @老周

---

## 0. TL;DR

两条端到端目标:

| 路径 | 目标 (p99) | 含跨洋? | 说明 |
|---|---|---|---|
| **决策内环**: 数据已在内存 → 信号 → 风控 → 签名 → 出网 socket write | **< 500 us** | 否 (纯本机 CPU) | 唯一可控部分, 必须卡死 |
| **数据外环**: Polymarket/Goalserve push → 策略层可读 | **< 20 ms** | 是 (含跨洋 RTT) | 跨洋占大头, 我们能优化的只剩反序列化 + 入队 |

约定:
- 决策内环不含网络 RTT (那是物理常数), 只衡量"事件到达我机器网卡 → 我机器网卡发出去"
- 外环 20 ms 是"可用 budget", 跨洋单程 ~160 ms 不在此预算内 — 是给市场延迟容忍度做参考, 不是我们的优化对象

---

## 1. 决策内环 budget 拆解 (信号 → 下单, p99 < 500 us)

参考 LMAX Disruptor (2010) + Aeron 论文 + Optiver/IMC 公开 talk 的典型量级.

| 阶段 | p50 | p99 | 上限 (kill switch) | 测量点 | 实现选型 |
|---|---|---|---|---|---|
| 1. 网卡 RX → 用户态 (kernel socket recv) | 5 us | 15 us | 50 us | RDTSC at recv() return | 起步 epoll + SO_BUSY_POLL; 后续 io_uring; 极端 DPDK/AF_XDP |
| 2. WSS 帧解析 (mask + 拼帧) | 2 us | 8 us | 20 us | chrono::steady_clock | 自研 zero-copy, 复用 ring buffer |
| 3. JSON 反序列化 (Polymarket book update) | 15 us | 50 us | 150 us | RDTSC pair | simdjson on-demand; 禁用 nlohmann/json in hot path |
| 4. 入队 (worker thread 间) | 0.1 us | 0.5 us | 2 us | RDTSC pair | 小石选型: rigtorp SPSC; cache-line padded |
| 5. 订单簿 apply (skiplist/B-tree update) | 3 us | 12 us | 40 us | chrono | 小石选型: 详见文档 2 §6 |
| 6. 信号计算 (定价 + 边界扫描) | 20 us | 80 us | 200 us | chrono | SoA + AVX2 vectorize price ladder |
| 7. 风控 (仓位 / 限额 / kill-switch) | 5 us | 20 us | 60 us | chrono | branchless check; bitset 限额位图 |
| 8. 订单签名 (ECDSA secp256k1) | 30 us | 80 us | 150 us | chrono | libsecp256k1 + context cache; **此处是大头, 单独优化** |
| 9. 序列化下单 payload | 3 us | 10 us | 25 us | chrono | 预分配 buffer, std::format 或自研 |
| 10. send() 出网 | 5 us | 15 us | 40 us | RDTSC at send() return | TCP_NODELAY + SO_SNDBUF tune |
| **合计 p99** | **~88 us** | **~310 us** | **737 us** | — | 预留 ~190 us 余量 |

**关键观察:**
1. ECDSA 签名 (80 us p99) 是单笔最大头. 优化方向: 预签 nonce / 异步签名管线 / 硬件加速 (待评估)
2. JSON 反序列化 (50 us p99) 第二大. simdjson on-demand 已是当下 SOTA, 进一步要靠 schema-aware 自研 parser
3. 入队 0.5 us 几乎可忽略 — 小石选 SPSC 就够, 不要为此过度设计 MPMC

**为什么 budget 是 500 us 不是 100 us:**
- Optiver/Jane Street 内环 < 10 us 是 colo + FPGA + kernel bypass 全套
- 我们跨洋部署, 网络 RTT 已经 160 ms, 内环抠到 100 us vs 500 us 对总延迟影响 < 0.3% — ROI 不值
- 500 us 是"不丢机会的下限": Polymarket maker 改单频率 ~Hz 级, 500 us 内环足够吃到 99% 的 alpha 窗口

---

## 2. 数据外环 budget (摄入 → 策略可读, p99 < 20 ms)

| 阶段 | p99 | 备注 |
|---|---|---|
| Polymarket WSS push 到我机器网卡 | **160 ms (跨洋 RTT/2 单程)** | **不在 budget 内**, 物理常数 |
| 网卡 → 用户态 recv | 15 us | 同 §1 阶段 1 |
| WSS 解帧 | 8 us | 同 §1 阶段 2 |
| JSON 反序列化 | 50 us | 同 §1 阶段 3 |
| 投递到策略 ring buffer | 1 us | SPSC enqueue |
| 策略线程消费 (调度抖动) | **~10 ms** | **本环主要不确定性来源** |
| **可控合计 (不含跨洋 + 调度)** | **~75 us** | — |
| **含调度抖动** | **~10.1 ms** | 离 20 ms 还有 10 ms 余量 |

**外环重点:**
- 跨洋 RTT 由 @老陈 实测给数 (机房选址 / 专线 vs 公网 / GeoDNS)
- 调度抖动是 OS 给的"礼物": SCHED_FIFO + isolcpus + CPU affinity 把策略线程钉死, p99 抖动可压到 < 2 ms
- 20 ms 留 10 ms buffer 给突发 (GC-like pause / page fault / 网络抖动)

---

## 3. 跨洋 RTT 加成预算 (求证 @老陈)

**已知假设 (待 @老陈 确认):**

| 链路 | RTT 假设 | 备注 |
|---|---|---|
| 国内机房 → AWS us-east-1 (Polymarket 主) | ~180 ms 公网 / ~140 ms 专线 | 需老陈实测 |
| 国内机房 → Goalserve (欧洲 IDC) | ~250 ms 公网 | 需老陈实测 |
| 香港中转 → us-east-1 | ~210 ms | 备选方案 |
| 美西 colo → us-east-1 | ~70 ms | 终极方案, 成本待评估 |

**结论 (待确认):**
- 短期: 国内 + 专线, 总延迟 ~160 ms 单程 — 接受
- 中期: 香港中转 + WSS keepalive 优化
- 长期: us-east 部署 (合规问题 @老周 拍板)

**对内环的影响:** 无. 跨洋只决定"我们看到的市场状态有多旧", 不决定"我们处理多快". 内环 budget 不因 RTT 变化而调整.

---

## 4. Benchmark 门禁机制 (CI 回归如何挡)

### 4.1 三层防线

| 层 | 触发 | 数据 | 失败动作 |
|---|---|---|---|
| **L1 micro-bench (per-PR)** | 每次 PR | google-benchmark, 单函数 ns 级 | regression > 10% → CI 红, block merge |
| **L2 component-bench (nightly)** | 每晚 main | 单模块端到端, ms 级 | regression > 15% → 自动开 issue, assign 责任人 |
| **L3 system-bench (weekly)** | 每周一 | 全链路 replay 历史 tick | regression > 20% → 召集 perf review |

### 4.2 Rolling baseline 算法

- 不用单点 baseline (易抖动), 用 **过去 7 次成功跑的中位数** 作 baseline
- 新 PR 跑 5 次取中位数 vs baseline 比较
- 引入 **waiver** 机制: 故意慢的改动 (e.g. 新增风控规则) PR 描述里写 `perf-waiver: <module> +X%` 由我 (老姜) review 批

### 4.3 Benchmark 集成测试归属

埋点 / 测试集成 — 不是我做的, 派给 testing-coach (老练). 我只定:
- benchmark 入口 (哪些函数必须 bench)
- 通过/失败阈值
- baseline 存储位置 (建议 `tests/perf/baselines/*.json`)

### 4.4 阈值 (初版, 跑 2 周再 tune)

| 指标 | warning | error | block-merge |
|---|---|---|---|
| 内环 p99 | +10% | +20% | +30% |
| 外环 p99 | +15% | +25% | +40% |
| memory RSS | +5% | +10% | +20% |
| 单函数 ns | +15% | +30% | — (只在 L1) |

---

## 5. 测量方法

### 5.1 工具栈

| 场景 | 工具 | 精度 | 何时用 |
|---|---|---|---|
| 生产长期监控 | `chrono::steady_clock` | ~20 ns overhead | 默认埋点, 所有阶段计时 |
| 微基准纳秒级 | RDTSC (`__rdtsc()` + invariant TSC) | ~1 ns | hot path 内部, 例: 签名各阶段 |
| 离线 profile | Linux `perf record` / macOS Instruments | sampling | 找瓶颈 |
| 火焰图 | `perf script` + flamegraph.pl | sampling | 找 hot function |
| 系统级 trace | `xctrace` (macOS) / `bpftrace` (Linux) | event-level | 调度抖动 / 页错误调查 |
| 队列 / 锁竞争 | `perf c2c`, `perf lock` | event | 排查 false sharing |

### 5.2 RDTSC 使用规范

- 必须用 `__rdtscp()` (序列化版本) 防 OoO 重排
- 必须校准 TSC frequency (启动时跑 1 秒标定)
- 必须 pin thread (`pthread_setaffinity_np`), 否则跨核 TSC 漂移
- macOS 上 TSC 不稳定 (P/E core 切换), macOS 一律用 `mach_absolute_time` 或 `chrono::steady_clock`

### 5.3 埋点规范 (派给 obs-engineer, 我只定接口)

```cpp
// 接口 (我定义, obs-engineer 实现)
struct LatencyProbe {
    void mark(StageId);     // RDTSC 取点
    void emit();             // 汇总, 不在 hot path
};
```

要求:
- hot path 单次 mark < 5 ns
- 不允许 syscall / malloc / log
- 数据走 ring buffer, 后台线程 flush

### 5.4 度量哪些百分位

p50 / p95 / p99 / p99.9 / max — **必须 p99.9 也看**, 因为做市策略最怕尾延迟撕单.

---

## 6. 风险与未决项

| 风险 | 影响 | 缓解 | Owner |
|---|---|---|---|
| ECDSA 签名 80 us 可能压不住 | 内环超 budget | 评估异步签名 / 预签 nonce 池 | 老姜 + crypto 同事 |
| macOS TSC 不稳, 本地开发与生产 Linux 测量基线不一致 | benchmark 不可比 | CI 必须跑 Linux, 本地数仅参考 | 老练 (testing) |
| 跨洋 RTT 实测未做, 假设可能偏差 ±50 ms | 外环 20 ms 可能不够 | @老陈 一周内出实测报告 | 老陈 |
| simdjson 在小 payload (< 1 KB) 优势不明显 | 反序列化 budget 不一定达成 | 小 payload 走自研 schema parser | 老姜 |
| 业务线程被 GC-like jitter 打断 | 内环 max 拖尾 | 用 jemalloc + huge page + 关 THP | 老姜 |

---

## 7. 验收标准

- [ ] §1 / §2 budget 表通过 @老周 架构 review
- [ ] §3 跨洋 RTT 假设由 @老陈 实测覆盖
- [ ] §4 CI 门禁机制由 @老练 (testing) 在 Sprint-2 落地
- [ ] §5 RDTSC + chrono 埋点接口由 obs-engineer Sprint-2 实现
- [ ] 在 Sprint-3 跑出第一版真实 baseline, 替换本文假设值

---

## 附 A: 参考资料

- LMAX Disruptor paper (Thompson, 2011) — 内环 < 50 ns 的工程范本
- Aeron Transport (Real Logic) — UDP 低延迟传输设计
- simdjson paper (Langdale & Lemire, VLDB 2019)
- rigtorp SPSCQueue (github.com/rigtorp/SPSCQueue) — 实测 ~10 ns enqueue
- Intel TSC manual SDM Vol.3 §17.17 — RDTSC 正确用法
- "Latency Numbers Every Programmer Should Know" (Dean, 2012)

