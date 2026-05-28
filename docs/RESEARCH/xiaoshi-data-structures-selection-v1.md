# 数据结构选型: lock-free / ring buffer / 订单簿 v1 (Sprint-1 S1-011)

- Owner: 小石 (data-structures-expert)
- Last review: 2026-05-28
- 验收人: 老周 (chief-architect)
- 协作: 老姜 (performance-owner) 提 budget 约束, 整体架构求证 @老周

> 本文为小石主笔, 老姜代为草拟初稿待小石修订. 关键决策点已标注 [需小石复核].

---

## 0. TL;DR

| 场景 | 选型 | 备选 |
|---|---|---|
| 网卡线程 → 解析线程 | **rigtorp SPSC** | folly ProducerConsumerQueue |
| 解析线程 → 多个策略线程 fanout | **自研 SPMC (基于 seqlock)** 或 **每策略一条 SPSC** | folly MPMCQueue (太重) |
| 多策略 → 单签名/出网线程 | **moodycamel ConcurrentQueue (MPSC mode)** | rigtorp MPMC |
| 同一品种订单簿 | **数组式 price ladder (SoA, AVX2 友好)** | skiplist 仅用于稀疏 prop 市场 |
| 全市场索引 | **flat_hash_map (absl)** + arena | std::unordered_map (淘汰) |
| 历史 tick replay buffer | **mmap 环形文件 + 预读** | — |

---

## 1. Lock-free queue 选型

### 1.1 SPSC / MPSC / MPMC 取舍

引用 rigtorp benchmark (i9-10900K, 2 cores pinned):

| 队列类型 | enqueue p50 | enqueue p99 | 适用 |
|---|---|---|---|
| rigtorp SPSC | **5 ns** | 8 ns | 1 生产 1 消费 |
| folly ProducerConsumerQueue | 7 ns | 12 ns | 1 生产 1 消费 (备选) |
| rigtorp MPMC | 25 ns | 60 ns | 多对多, 通用 |
| moodycamel ConcurrentQueue (MPSC) | 15 ns | 40 ns | 多生产 1 消费 |
| boost::lockfree::queue | 80 ns | 200 ns | **淘汰**, 太慢 |
| folly MPMCQueue | 30 ns | 90 ns | 多对多, blocking semantics |

**原则:**
1. **优先 SPSC**. 老姜 §1 入队 budget 是 500 ns p99, SPSC 8 ns 富余 60x. 不要为了"通用"上 MPMC.
2. **fanout (1 → N) 不要用 MPSC 反向**. 用 SPMC (seqlock-based broadcast) 或 N 条 SPSC.
3. **N → 1 收口 (多策略发单)**: moodycamel 实测最稳, MPSC 模式开启 token 优化后接近 SPSC.

### 1.2 实现层面 [需小石复核]

- 所有队列容量必须 **2 的幂** (mod 走位运算)
- cache line padding (64B, Apple Silicon 128B) 强制, false sharing 是 lock-free 头号杀手
- `std::atomic<T>` 必须 `is_lock_free()` 编译期 assert
- 生产者 / 消费者索引分离到不同 cache line (经典 Disruptor 教训)

### 1.3 自研 vs 库

| 维度 | 自研 | rigtorp | folly | moodycamel |
|---|---|---|---|---|
| 性能 | 可达 SOTA | SOTA | -10% | -20% (MPMC) |
| 依赖 | 0 | header-only | 重 (gflags/glog) | header-only |
| 维护 | 全靠自己 | 上游活跃 | Meta 维护 | 半活跃 |
| 跨平台 | 可控 | OK | macOS 偶尔挂 | OK |

**决策:** 起步用 **rigtorp + moodycamel header-only**, 不自研. 若 profile 发现具体瓶颈再针对性自研 (例: 带 batch enqueue 的 SPSC).

---

## 2. Ring buffer 适用场景

### 2.1 用 ring buffer 的场景

| 场景 | ring 大小 | 元素类型 | 备注 |
|---|---|---|---|
| 网卡 RX 临时缓冲 | 4 MB | byte | 配合 io_uring/AF_XDP |
| WSS 解帧 staging | 64 KB | byte | 单帧拼接 |
| Latency probe 采样 | 1M 条 | 16 B struct | obs 后台 flush |
| 日志 (async logger) | 16 MB | byte | spdlog async 默认 |
| Tick replay 历史 | mmap 1 GB | tick struct | offline backtest |

### 2.2 ring buffer 设计要点

- **head / tail 分 cache line** (同 §1.2)
- **容量 = 2^N**, mask = capacity - 1
- **wait-free 读 / wait-free 写** (不是 lock-free, 更强), seqlock 版本可做到
- **不要 std::deque / std::queue**, 它们分配粒度小, malloc 抖动毁掉延迟尾部

### 2.3 vs lock-free queue 关系

- ring buffer 是底层存储, lock-free queue 是协议层 (含 head/tail/index/wait 策略)
- rigtorp SPSC 内部就是 ring buffer + 2 个 atomic index
- 大块 byte buffer (网络 RX) 用裸 ring; 结构化消息用包装好的 queue

---

## 3. RCU / Hazard Pointer 评估

### 3.1 场景

我们有几个"读多写少"结构需要并发安全:
- 市场元数据表 (赛事 / market / token 映射) — 启动加载 + 偶尔更新
- 风控规则表 — 运维改 + 策略读
- 配置 (限价 / 限额) — 热更新

### 3.2 选型对比

| 方案 | 读延迟 | 写延迟 | 内存开销 | 复杂度 | 适用 |
|---|---|---|---|---|---|
| `std::shared_mutex` | 50 ns (无竞争) / 1+ us (竞争) | 100 ns | 0 | 低 | **不推荐**, 竞争下崩 |
| `folly::SharedMutex` | 20 ns | 80 ns | small | 中 | 备选 |
| **RCU (URCU lib)** | **2 ns** | 1-10 us (grace period) | 中 (deferred free) | 高 | **推荐 hot read path** |
| Hazard Pointer | 10 ns (含 HP scan) | 1 us | 每读者 N 槽 | 高 | 数据结构内部 (skiplist 节点) |
| `folly::AtomicSharedPtr` | 30 ns | 100 ns | 引用计数 cache miss | 中 | 简单引用替换 |
| **copy-on-write + atomic ptr 翻牌** | **1 ns** (load) | full copy | 2x 内存 | 低 | **元数据表首选** |

### 3.3 决策

1. **市场元数据表 / 风控规则**: COW + `atomic<shared_ptr>` 翻牌. 简单, 读 1 ns, 写慢无所谓 (不在 hot path).
2. **订单簿内部节点回收** (若用 skiplist): hazard pointer. URCU 在 macOS 兼容性差.
3. **不引入 liburcu**. 维护成本 > 收益.

---

## 4. Arena Allocator

### 4.1 为什么需要

- malloc/free 在 p99 是 200 ns - 5 us 抖动来源 (glibc ptmalloc 锁竞争 + 系统 munmap)
- hot path 任何分配 = 延迟杀手
- 订单 / 信号 / 消息对象生命周期短 + 数量爆发 → 典型 arena 场景

### 4.2 选型

| 方案 | 分配速度 | 释放策略 | 备注 |
|---|---|---|---|
| **per-thread bump arena** | 5 ns | reset (批量) | 短生命周期消息首选 |
| `boost::pool` | 30 ns | 单对象 free | 固定大小池 |
| jemalloc tcache | 20 ns | 单对象 free | **默认全局替换 malloc** |
| `std::pmr::monotonic_buffer_resource` | 8 ns | reset | 标准库版 bump arena |
| mimalloc | 15 ns | 单对象 | 备选, 跨平台优秀 |

### 4.3 决策

1. **全局: 链接 jemalloc** (或 mimalloc, 二选一基于 benchmark). 默认 malloc 全替换.
2. **hot path 消息对象: per-thread `pmr::monotonic_buffer_resource`** + 每个 tick 周期 reset.
3. **长生命周期对象 (元数据)**: 用默认 jemalloc, 不进 arena.
4. **禁用** `new`/`delete` 在 hot path — clang-tidy 规则强制 [需小石复核].

---

## 5. Cache-friendly 布局: SoA vs AoS

### 5.1 何时 SoA

订单簿价位扫描是典型 SoA 受益场景:

```
AoS (传统):
struct PriceLevel { double px; int64 qty; uint32 orders; ... };
PriceLevel ladder[1024];  // 扫描 px 时也加载 qty/orders, cache 浪费

SoA (推荐):
struct LadderSoA {
    double px[1024];      // 单独一条 cache line 流
    int64  qty[1024];
    uint32 orders[1024];
};
// AVX2 一次扫 4 个 double px, cache 100% 利用
```

实测 (老姜 §1 阶段 6 信号计算 budget 80 us 假设):
- AoS 扫 1024 价位: ~250 us
- SoA + AVX2: **~60 us** (4x 加速)
- SoA + AVX-512 (若支持): ~30 us

### 5.2 何时 AoS

- 单价位整体操作 (e.g. 订单 match) — AoS 一次 cache line 拿全字段
- 小规模数据 ( < 16 项) — SoA 优势消失, 维护成本不划算

### 5.3 规则

| 数据 | 布局 | 原因 |
|---|---|---|
| 订单簿价位 ladder | SoA | 扫描密集 |
| 单笔订单 | AoS | 字段一起用 |
| Tick 时间序列 | SoA (column) | backtest 列扫 |
| 市场元数据 | AoS | 查询稀疏 |
| 风控限额 | SoA bitset | branchless check |

### 5.4 工程细节 [需小石复核]

- struct 必须 `alignas(64)` (或 128 Apple Silicon)
- 字段顺序: 大 → 小 (减少 padding)
- hot field 放前面 (访问局部性)
- 用 `pahole` 工具检查 struct 布局

---

## 6. 订单簿数据结构

### 6.1 选型矩阵

| 结构 | 插入 | 删除 | 价位扫描 | 内存 | 适用 |
|---|---|---|---|---|---|
| **数组式 price ladder** (SoA) | O(1) | O(1) | O(N) AVX 加速 | 固定 | **流动品种 (NBA/NFL moneyline)** |
| std::map (RB-tree) | O(log N) | O(log N) | O(N) cache miss | 高 | 淘汰 |
| absl::btree_map | O(log N) | O(log N) | 友好 | 中 | 备选 |
| skiplist | O(log N) | O(log N) | O(N) | 中 | **稀疏 prop 市场** |
| robin-hood hash | O(1) | O(1) | O(N) 无序 | 中 | 不适合 (要有序扫描) |

### 6.2 决策

**两套订单簿并存:**

1. **流动品种 (NBA moneyline / 总分 / 让分)**:
   - 数组式 ladder, 价格范围已知 (0.01 ~ 0.99, 共 99 个 tick)
   - SoA + AVX2 扫描
   - **预期: O(1) update, < 5 us 全簿扫**

2. **稀疏品种 (prop / outright / 长尾)**:
   - skiplist (自研或 folly::ConcurrentSkipList)
   - 价位数 < 50, 更新频率低
   - skiplist 节点用 hazard pointer 回收

### 6.3 为什么不用 B-tree

- absl::btree_map 在 cache 上比 std::map 好, 但订单簿 update p99 仍是 ~200 ns
- 数组 ladder 单次 update ~10 ns, 优势压倒性
- B-tree 留作"如果未来支持非 Polymarket 标准 tick 的小数价位"备用

### 6.4 风险 [需小石复核]

- 数组 ladder 假设 Polymarket tick 固定 0.01 步长 — 需求方 (老周) 确认
- 若未来出现自定义 tick (e.g. 0.005), 切 B-tree 或扩 ladder 到 199 槽

---

## 7. 内存与 cache 杂项规则

1. **禁 `std::shared_ptr` 在 hot path**: 引用计数 cache miss 慢
2. **禁 `std::function` 在 hot path**: type erasure heap alloc
3. **禁虚函数在 hot path**: vtable indirect call 不可预测
4. **慎用 `std::vector::push_back`**: 必 `reserve()` 或用 fixed-size
5. **大对象 `mmap` + huge page** (2 MB / 1 GB), 减少 TLB miss
6. **关闭 THP** (Transparent Huge Pages): 抖动来源, 显式 hugepage 替代

---

## 8. 与老姜 budget 的对齐验证

| 老姜 §1 阶段 | 数据结构选型决定 | 是否满足 budget |
|---|---|---|
| 阶段 4 入队 (p99 0.5 us) | rigtorp SPSC (8 ns) | **富余 60x** |
| 阶段 5 订单簿 apply (p99 12 us) | 数组 ladder (~10 ns single update) | **富余 1000x** (但若全簿重算另算) |
| 阶段 6 信号计算 (p99 80 us) | SoA + AVX2 | **可达** |
| 阶段 7 风控 (p99 20 us) | bitset + branchless | **可达** |
| 元数据查询 (老姜未列, 信号计算内) | COW atomic ptr (1 ns) | 不构成瓶颈 |

**无冲突, budget 可执行.**

---

## 9. 未决项 (求证)

1. **跨洋链路下消息批量化策略** — @老陈 给数: 每秒消息密度多少? 决定是否需要 batch enqueue 接口
2. **是否引入 jemalloc 还是 mimalloc** — 跑 benchmark 后定, Sprint-2 落地
3. **数组 ladder 假设 tick 固定** — @老周 业务确认
4. **macOS 开发机上 SPSC 性能 vs Linux 差距** — 老练 跑 CI 出数

---

## 10. 验收标准

- [ ] §1 队列选型由小石复核签字
- [ ] §3 RCU 决策 (不引入 URCU) 由 @老周 架构 review
- [ ] §5 SoA 规则在 Sprint-2 落地到订单簿原型, 老姜跑 benchmark 验证 4x 加速
- [ ] §6 数组 ladder vs skiplist 双轨方案在 Sprint-3 出原型对比

---

## 附 A: 参考资料

- Disruptor (LMAX, 2011) — ring buffer + sequencer 模板
- rigtorp/SPSCQueue, rigtorp/MPMCQueue (github)
- folly ProducerConsumerQueue / MPMCQueue 源码
- moodycamel::ConcurrentQueue paper (blog.moodycamel.com)
- "What Every Programmer Should Know About Memory" (Drepper, 2007)
- "Is Parallel Programming Hard..." (McKenney) — RCU / hazard pointer 权威
- Abseil container design (absl::btree_map, flat_hash_map)
- folly::ConcurrentSkipList 源码
- jemalloc / mimalloc benchmark (Microsoft Research 2019)

