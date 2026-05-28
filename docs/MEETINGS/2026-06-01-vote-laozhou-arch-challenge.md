---
owner: 老周 (cpp-chief-architect, A 主管)
adr_ref: ADR-009 v2 (Sonnet)
last_review: 2026-06-01
purpose: W5 末 Wave 27 架构 challenge 投票 — 老周 5 议题 + 自抛 architecture smell
audience: 老郭 (主持集成), GM (老雷拍板), 5 主管
scope: 我自己投票; 不替他人投; 不替 GM 拍
note: >
  老板原话: "发现问题和架构优化, 需要协商投票讨论, 不是死板的硬套最初版本".
  v0.6 是 W4 设计, W5 末实测后重新评估 — 即使 v0.6 错了也承认.
---

# W5 末架构 challenge 投票 — 老周 (A 主管)

---

## §1 五议题投票

### 议题 1: paper/live 共享 binary (R-2 红线)

**我投: C**

理由: 共享 RM + signal core 是正确的 (两套 RM 逻辑等于两套风控红线, 维护地狱); transport
层 (signer / CLOB client / audit WAL 落盘路径) 物理分离才能满足 R-11 paper 不污染真账本.
纯共享 A 选项会把 paper 测试路径耦合进 live 签名链路; 纯分叉 B 选项会导致 RM 逻辑双头维护,
一旦有 reject enum 改动两边都要改, 这比 R-2 红线本身更危险. C 是原则上正确的中间路.

---

### 议题 2: WSS 订阅拓扑

**我投: B (老李 v3 的 4-5 conn × 2 sub)**

理由 (含 v0.6 自我挑战):

我的 v0.6 选 C (2 conn × 4 sub) 的出发点是故障域隔离. 但 W5 实测 + 老郭 6/01 代码 review
暴露了一个问题: 小冯 v0.1 目前是 1 conn × 8 sub, WSS 单连接承接 8 个 topic, 断开即全停.
我 v0.6 提的 2 conn 缩小了故障域, 但没有基于真实 Polymarket WSS 行为的实测依据.

老李 v3 的 4-5 conn × 2 sub 设计基础是: 每条 conn 只挂 2 个强耦合 topic
(如 market_state + l2_book 一条, game_state + odds 一条), 一条断线不影响其他 topic 的实时性.
这在跨洋高延迟场景下故障域更细, 单条 conn 重连 (exp backoff 30s) 期间其余 conn 继续喂数据.

我承认: v0.6 C 是"我推断的合理拓扑", 不是"基于跨洋实测的拓扑". B 更符合实盘优先原则.
唯一保留意见: 4-5 conn 会带来 fd 资源 + heartbeat 维护代价, 需老李 W6 给出跨洋环境下
conn overhead 数据再锁定. 若实测 overhead 可忽略, B 优于 C.

---

### 议题 3: 跨洋部署

**我投: C (M4.5 前单点, M5+ 多点)**

理由: MVP 阶段把运维复杂度集中到一点. US East 单点 + WAL 本地持久化, 故障恢复走重启而不走
failover. 多 region 多活需要分布式一致性协议保证 position ledger 不双写,
这是 M4.5 前不该碰的复杂度. 但 M5+ 真签上链后单点 = 单点故障 = 整体停摆,
那时多点是必须的. C 是分阶段降风险的合理路径.
GM 2026-05-28 决议已 defer AWS 直到盈利, 这与 C 选项方向一致.

---

### 议题 4: ML 进生产时机

**我投: C (M2 后 shadow, M4.5 后 active)**

理由: ML-R1 不进生产决策是当前红线, 但"shadow inference 不影响 if-else 决策"和"验证 alpha"
是合理的基础设施工作, 越早积累 feature 数据越好. B (W6 起 shadow) 的时机可能太早 —
W6 paper engine 真数据才开始进来, feature distribution 还没稳定,
过早开 shadow 会积累噪音数据. M2 (8/6) 后数据量和分布基本稳定,
那时开 shadow 可以积累有质量的训练集. M4.5 后 active = 严守 ML-R1 红线.

---

### 议题 5: vCPU pin

**我投: C (M4.5 前 single thread, M5+ 7 vCPU)**

理由 (含 v0.6 自我挑战):

我在 v0.6 提 7 vCPU pin 是因为 paper engine 需要模拟 live 环境. 但 W5 实测给了一个关键
数据点: paper engine e2e p99 3.9us (单线程, in-memory mock). 这个数字有 caveat
(没有 SPSC 跨核 cache bounce, 没有真 IO fsync), 但它说明一件事: 在 M4.5 前跑 paper 的
目标是积累数据质量, 不是压极限延迟. 此阶段 7 vCPU pin 带来的工程复杂度
(pthread_setaffinity_np + isolcpus + nohz_full kernel tuning) 远超实际收益.

我承认 v0.6 7 vCPU 选项在 M4.5 前是过度设计. C 选项更务实:
- M4.5 前: single thread 跑通 paper 全链路, 数据干净, 运维简单
- M5+: vCPU pin 才是真正有意义的 (live 路径 latency sensitive + 真 SPSC 跨核场景)

唯一保留: SPSC 的 W6 P0 落地 (小石 W6-A-03) 应该在 single thread 模式下先跑通,
然后再接 vCPU pin, 不要两件事并行导致 debug 困难.

---

## §2 我自己抛出的 Architecture Smell (W5 末新发现)

以下 3 个问题不在 v0.6 设计范围内, 是读 12000 行 cpp 和 4 张 mermaid 图后新发现的
structural 问题. 不预设答案, 提给老郭集成讨论.

---

### Smell A: observability 模块是隐性单点 — 5 个上游全压在 AuditEmitter → WAL

**问题描述:**

依赖图 Part 4 显示: risk / signer / ml / stats / strategy 5 个模块全都依赖
`stcpp_observability_audit` → `stcpp_infra_wal`. 这意味着 AuditEmitter 是整个系统的
隐性 "single chokepoint": 一旦 AuditEmitter 内部实现有问题 (比如 BLAKE3 hash chain
在 W6 从 stub 切真实现时出回归), 5 个上游模块的 audit 全部受影响.

**为什么这是 W5 新发现:** v0.6 设计时 observability 还是轻量 stub, 但 W5 末
AuditEmitter 已承接 12 AET + 4 ts + BLAKE3 chain, 变成了有实质逻辑的中枢.
当初设计依赖关系时没有意识到它会长成这样.

**核心疑问:** AuditEmitter 是否应该拆成 "audit router" + "wal writer" 两层,
让上游依赖轻量的 router 接口, WAL 细节下沉? 或者现在的 flat 依赖可以接受?

**面向: 老郭 (架构评审) + 老唐 (owner) + 老王 (WAL owner)**

---

### Smell B: WAL 走 append-only + group commit, 但 ML 训练管道还没有 compaction / rotation 方案

**问题描述:**

shadow_audit.wal + paper_audit.wal 用 group commit 追加写, 这对 audit 完整性是对的.
但 ML 训练管道 (小邓) 的上游是这两个 WAL. 随着 paper engine 长跑, WAL 文件会无限增长.
W5 末没有任何 WAL rotation / compaction / export-to-Parquet 方案.

这不是 W5 的问题 — paper engine 才刚启动. 但如果 W6/W7 真数据进来后, 几周内
shadow_audit.wal 涨到几十 GB, 而 ML 训练 pipeline 还在从头全量读 WAL,
训练速度会指数级下降.

**核心疑问:** WAL 的"终点不是 sink"这个问题 (W5 末 Part 2 §2 引导问题 2 里我自己提过)
现在仍然开放. ML 消费 WAL 的方式 (全量 replay vs checkpoint + delta) 需要在 W6 末定稿,
不然 W7 ML baseline 训练会撞墙. 这个设计决策应该由谁拍 (老周 + 小邓 联拍, 还是升 ADR)?

**面向: 老郭 + 小邓 (ML owner) + 老王 (WAL owner)**

---

### Smell C: 14 模块 5 个 paper-only sub-lib — CMake 物理隔离的方向正确, 但 live stub 是永久占位符还是设计缺口?

**问题描述:**

`live_pm_client.hpp` 是 14 接口全返回 Unknown 的 stub, CMake R-7 build-time switch
保证 paper build 不 link live, live build 不 link paper. 这个设计 W5 是合理的.

但问题是: live stub 里的 14 个接口返回 Unknown 是"占位符 (placeholder)"还是"接口契约已锁"?
如果是前者, M5+ 老孙 signer v4 接入时可能发现 14 接口需要改动 (比如 submitOrder 需要
gas estimator 参数), 那时改接口会破坏 paper_pm_client 也实现的同一套接口.
如果是后者, 我们现在就需要明确 "14 接口 ABI = live 接入时不改动" 这个承诺.

当前文档里没有这个承诺. v0.6 没有明确说 IPolymarketClient 的 14 接口是 ABI locked.
老李 + 老孙 两边没有明确 handshake.

**核心疑问:** IPolymarketClient 14 接口是否需要在 M5+ live 接入前做一次 ABI review
(老李 + 老孙 + 老郭 三方)? 现在锁还是 M4.5 后锁?

**面向: 老郭 + 老李 (pm_client owner) + 老孙 (signer owner)**

---

## §3 给老郭: 我最关心的议题排序

1. **议题 2 (WSS 拓扑)** — 跨洋链路数据源是整个系统的血管, 拓扑错了后面全错.
   而且这是我 v0.6 自认为设计有偏差的地方, 最需要外部视角纠正.

2. **Smell C (IPolymarketClient ABI 锁时机)** — 这是个跨模块接口契约问题,
   一旦 M5+ 发现要改接口, 代价极高. 宁可现在多花半天审查, 不要到时候爆.

3. **议题 5 (vCPU pin)** — 我承认 v0.6 7 vCPU 在 M4.5 前是过度设计,
   但 M5+ 的 pin 方案还没有人拿过真跨洋机器的 NUMA 拓扑做设计.
   老姜 W6 需要给出基于实测的 pin 方案, 不是照搬我的 7 vCPU 表格.

4. **Smell A (AuditEmitter 单点)** — 影响面广, 但 W6 BLAKE3 切真实现之前还有时间窗口.

5. **议题 1 (paper/live 共享 binary)** — 方向基本确定 (C), 细节留实施.

---

## §4 不耻下问 (我不懂的部分)

- **@老姜 (#39, 性能工程):** vCPU pin + isolcpus + nohz_full 在跨洋 AWS c6a.2xlarge
  上的真实 NUMA 拓扑和 cache line 行为, 我设计 v0.6 7 vCPU 表格时没有真机数据.
  W6 你能给我一次 1:1 过一遍 kernel tuning 方案吗? 我不想替你设计 pin 方案,
  但我需要理解约束才能在架构层正确 spec.

- **@老李 (#07, polymarket-protocol-expert):** IPolymarketClient 14 接口里,
  你主观判断哪几个接口在 M5+ live 接入时有改动风险?
  特别是 submitOrder 的参数 schema 和 gas 相关接口.
  (Smell C 的核心问题)

- **@老王 (#05, WAL owner) + @小邓 (#31, ML owner):**
  WAL rotation / compaction / Parquet export 这件事, 你们俩之间有没有讨论过?
  还是目前都假设 "这是对方的问题"? W6 前需要有人明确 ownership.
  (Smell B 的核心问题)

- **@老郭 (#16, 架构评审):** Smell A 的问题, AuditEmitter 拆层 vs 保持 flat 依赖,
  你有历史项目的类比吗? 我倾向保持 flat 但做 interface 隔离,
  但我不确定这在 5 上游并发写 audit 的场景下是否足够.

---

**主管签字:** 老周 (cpp-chief-architect, A 主管, ADR-009 v2 = Sonnet)
**约束声明:** 本文件只含我自己的投票 + 发现, 不替他人投票, 不替 GM 拍板
**last_review:** 2026-06-01
