# 架构 Challenge 投票会 v1

- **主持:** 老郭 (F 协调, chief-architecture-reviewer, E-016)
- **会议日期:** 2026-06-01 (Sprint-2 W5 末, Wave 27)
- **性质:** 架构 Challenge 投票会 — 不是修补 v0.6, 是从根本上 challenge v0.6 的 5 个核心设计
- **与会投票人 (6 票):** 老周 (A) / 老韩 (B) / 小梁 (C) / 小余 (D) / 老胡 (E) / 老郭 (F, 本人)
- **Dial-in 不投票:** 老雷 (GM, 拍板) / 老钱 (CPO, 产品视角)
- **关联文档:**
  - `laozhou-architecture-v0.6-e2e.md` (v0.6 设计基准)
  - `2026-06-01-input-laozhou-arch-deviation.md` (老周 W5 偏离审查)
  - `2026-06-01-input-laohan-redline-deviation.md` (老韩 RM 偏离审查)
  - `2026-06-01-input-laoguo-F-status.md` (老郭 F 协调 W5 末 status)
  - `laowu-cross-region-deployment-v0.1.md` (跨洋部署现状)
  - `xiaodeng-ml-roadmap-v2.md` (ML 路线图现状)
  - `laojiang-latency-budget-v1.md` (延迟预算基准)
- **owner:** 老郭
- **last_review:** 2026-06-01

> 协调人声明: 本文档我作为主持人 + 投票人 (F 视角) 撰写. 我只填自己的 5 票 + 论证; 其他 5 主管的投票位置标注 TBD, 等各主管 input file 到位后更新表格. 我不替任何主管投票, 不替 GM 拍板, 不替 CPO 说产品方向.

---

## Part 1: 5 议题背景 + 选项 + 利弊分析

---

### 议题 1: paper/live 共享 binary (R-2 红线) 是否真合理?

**背景:**
R-2 红线 ("paper/live 共享 binary") 由 ADR-003 closeout 确立. 原始动机是防止 paper 与 live 代码分叉后出现"paper 跑通但 live 有 bug"的不一致. 然而 W5 末代码审查揭示: paper 路径 (PaperSigner + VirtualMatcher) 与 live 路径 (真 ECDSA signer + 真 CLOB 提交) 的实现栈几乎没有共享的热路径代码. 两者只共享 RiskGateway + Signal Engine 这两个核心模块. 强行共享 binary 反而要在 transport 层 (signer / matcher) 维护大量 `#ifdef PAPER_MODE` 或 CMake build-time switch, 增加了编译路径的复杂度.

**选项:**

| 选项 | 描述 | Pros | Cons |
|---|---|---|---|
| **A 完全共享 (现状)** | paper 和 live 同一 binary, build-time switch R-7 分流 | 防止代码分叉; RM + Signal 只有一份 | transport 层 `#ifdef` 累积; paper signer 和真 signer 同 binary 互相干扰; 单一 binary 体积膨胀 |
| **B 完全分叉** | paper binary 独立, live binary 独立 | 两套代码各自简洁; paper 可以用 in-memory transport, live 用真链 | 最大分叉风险: RM / Signal 修改只改一边 → 生产 bug; 维护双份 CI |
| **C 中间方案 — 共享 core, 分离 transport** | RM + Signal Engine 共享 (作为静态库), paper transport (PaperSigner + VirtualMatcher) 和 live transport (真 Signer + 真 CLOB) 分离为独立 executable | 共享 RM + Signal 防止分叉 bug; transport 层简洁不带 `#ifdef`; 两个 executable 体积小 | 接口契约 (OrderIntent / RiskDecision) 必须严格 stable; lib 版本管理增加一层复杂度 |
| **D 其他** | 例如: 运行时模式切换 (config flag), 或 paper 作为 live 的 dry-run 模式 | 灵活 | runtime if-else 比 build-time switch 更难静态验证; 老韩 RM 视角不接受 runtime bypass |

**现状参考:**
- 老周 v0.6 §2: R-7 `ExecutionContext::Init` build-time switch 4 target (paper / live / backtest / shadow)
- 老韩 input: R-7 build-time mode 0 偏离, 物理隔离 OK
- 老周 arch-deviation input Part 5: e2e 3.9us 数据来自 in-memory mock + 单线程, 说明 paper transport 与 live transport 本来就是不同栈

---

### 议题 2: WSS 订阅拓扑

**背景:**
Polymarket WSS 订阅拓扑有多种演化方向. 小冯 W5 实际落地了 1 conn × 8 sub topic 方案 (简单可工作). v0.6 老郭设计了 2 conn × 4 sub (故障域分离). 老李 v3 提出 4-5 conn × 2 sub (生产级多连接). 老叶 Standby 视角建议 3 进程各 1 conn × 3 sub (进程级隔离). W5 末 PM WSS subscriber 已有 8 topic (market / game / outcomes / book / price_change / last_trade_price / tick_size_change / system), 单连接 SPOF 风险在 paper 阶段可容忍, 但 live 阶段不可容忍.

**选项:**

| 选项 | 描述 | Pros | Cons |
|---|---|---|---|
| **A 1 conn × 8 sub (小冯 W5 现状)** | 一条连接订阅全 8 topic | 最简单; paper 阶段够用; 已有代码 | 单连接 SPOF; 任何 topic 错误可能影响全部订阅 |
| **B 4-5 conn × 2 sub (老李 v3)** | 多连接并发, 每条连接少量 sub | 生产标准; 故障隔离; 连接级 backpressure 隔离 | W6 paper 阶段工程量大; reconnect 逻辑复杂 4 倍 |
| **C 2 conn × 4 sub (老郭 v0.6 设计)** | 2 连接各订阅 4 topic; 按 critical / non-critical 分组 | 故障域分离; 工程量适中; 对比 A 多一层保护 | 仍有 2 个 SPOF 点 (每条 conn); 不是生产最优 |
| **D 1 conn × 3 sub × 3 process (老叶 Standby 方案)** | 进程级隔离; 每进程独立 WSS | 最高隔离度; 进程崩溃不影响其他 | 3 进程协调复杂 (共享 RM 状态 / nonce / position); 跨进程 IPC 开销 |
| **E 阶段性方案: paper 1 conn, M4.5 后升 4-5 conn** | 现在不动, live 阶段前升级 | 避免 W6 过早复杂化; 按需演进 | 升级窗口可能与其他 live 上线工作冲突 |

**现状参考:**
- 小冯 W5 v0.1: 1 conn × 8 sub + exp backoff 自愈 + M1-G7 WSS reconnect chaos test ✓
- 老周 arch-deviation Part 1: WSS module 状态 ✓, 但 ISpscEventSink 尚未接真 SPSC ring (W6 派单)
- `laojiang-latency-budget-v1.md`: 跨洋 RTT 160ms, WSS 连接断线重连代价高

---

### 议题 3: 跨洋部署

**背景:**
老吴 `laowu-cross-region-deployment-v0.1.md` 设计了 AWS us-east-1 主 + Hetzner Ashburn standby 的主备方案. 当前为 active/standby 模式 (不做 active/active 以避免双发 nonce). 但在 live 阶段, 若 us-east-1 出现抖动, 切 standby 需要 RTO 5min. 另一方向是 Tokyo 节点 (ap-northeast-1) 部分分担亚洲体育赛事信号 (但 Polymarket origin 在 us-east, Tokyo 多 150ms 反向延迟).

**选项:**

| 选项 | 描述 | Pros | Cons |
|---|---|---|---|
| **A 单点 us-east-1 (现 plan)** | 主节点 us-east-1, Hetzner standby 冷备 | 架构最简; 成本最低 (~320 USD/月); 延迟最优 | us-east-1 单点风险; standby 切换 RTO 5min; live 资金暴露 |
| **B 多 region 多活 (us-east + Tokyo)** | 双活 active/active | 延迟均衡; 亚洲赛事就近 | 双发 nonce 需要分布式协调; 极难正确实现; 成本 2x; 老叶设计未就绪 |
| **C M4.5 前单点, M5+ 评估多点** | paper 阶段单点; 真实 live 资金上线后再评估是否需要多点 | 分期投入; 不过早复杂化; M5+ 时有真实流量数据做决策 | M5+ 改造窗口可能与 live 并行运行有风险 |
| **D 其他** | 例如: 纯 Hetzner 省成本 (~75 USD); 或 GCP us-east4 替代 AWS | 成本灵活 | 老吴已验证 AWS 与 Polymarket origin 同区; GCP 跨云多 5-10ms |

**现状参考:**
- 老吴方案: us-east-1 <-> Polymarket origin RTT < 5ms; Goalserve us-east NJ 直连 < 15ms
- 月度成本 ~320 USD (主) + ~100 USD (RPC) = 420 USD, 在老钱 MVP 预算 500 USD 内
- 老雷 ADR: `2026-05-28-gm-decision-defer-aws-until-profitable` — AWS 延后盈利后决策 (注意: 此与 us-east-1 作主节点的选址是两个不同问题)
- 当前 paper 阶段无真实资金风险, 单点可接受

---

### 议题 4: ML 进生产时机

**背景:**
小邓 ML 路线图 v2 立了 ML-R1 ("ML 不进 RM 决策路径") 和 ML-R2 ("paper 期 ML 不进 OrderIntent 路径"). 当前 ML 仅作为 shadow 数据收集 (FeatureSnapshot → mldata.wal). v0.6 设计了 vCPU4 ML hook, 但 MLSignalCandidate struct 未实施 (老周 arch-deviation Part 3, P2 偏离). shadow inference 是指: ML 模型并行推理但不影响 if-else 决策, 仅输出到独立账本验证 alpha. 这不违反 ML-R1/R2 红线, 但增加 W6 工程复杂度.

**选项:**

| 选项 | 描述 | Pros | Cons |
|---|---|---|---|
| **A 严守 ML-R1 (M4.5 后才进生产)** | ML 永远 shadow, M4.5 7 gate 通过后才评估进生产 | 最保守; 红线不移动; M4.5 gate 干净 (只看 rule signal) | paper 期 ML 完全不产出; 错过 paper 跑 shadow 验证 alpha 的机会 |
| **B W6 起 shadow inference (不影响 if-else)** | W6 立即接 shadow signal path; ML 推理跑但不影响 RM 决策 | 尽早验证 alpha; paper 数据直接喂 shadow | W6 工程量增加; MLSignalCandidate struct 未实施 (P2 偏离需先补); 小邓 ML 路线 阶段 0 (6/15-7/24) 说明 "阶段 0 NOT 做: 训练任何模型" — W6 没有可推理的模型 |
| **C M2 (8/6) 后 shadow, M4.5 后 active** | paper 先跑 rule-only 建立 baseline (M1-M2); M2 后接 shadow inference (有 paper 数据了); M4.5 后按 ML-R6 (4 类 case "rule 输 ML 赢" ≥ 20%) 才解锁 active | 与 P0-02 上线时机配套; paper 先出 baseline 再加 ML 变量; 阶段 0/1 数据积累后 shadow 才有意义 | M2 → M4.5 之间有 ~6 周 shadow 窗口, 时间不长 |
| **D 其他** | 例如: shadow inference 从 M3 (9 月) 开始, 给阶段 1 (LightGBM baseline) 更多时间 | 模型更成熟时再接 | 推迟 alpha 验证窗口 |

**现状参考:**
- 小邓 ML roadmap v2 §3: 阶段 0 (6/15→7/24) = 数据基设, **不训练模型**; 阶段 1 (7/24→9/26) = LightGBM baseline + ONNX C++ 雏形
- 老周 arch-deviation Part 3: MLSignalCandidate 未实施 (P2, W7+ 小邓 + 小卢 联调)
- M4.5 gate 由小董 W4 已就位 (7 gate framework v1), G1-G7 只看 rule signal (ML-R3)

---

### 议题 5: vCPU pin

**背景:**
v0.6 §2 设计了 7 vCPU 分工 (vCPU0=Ingest / vCPU1=Signal / vCPU2=RM+Audit / vCPU3=PaperSigner+WAL / vCPU4=ML / vCPU5=Settlement / vCPU6=Reserved). 老姜 `laojiang-latency-budget-v1.md` 对应了这个设计. 但 W5 末实测: paper engine e2e p99 3.9us (单线程 in-memory mock). 老周 arch-deviation 明确指出: "当前是全 main thread + 全 in-memory queue, 没有 SPSC ring + 没有 vCPU pin + 没有跨洋 WSS + 没有真 WAL fsync." 跨洋 RTT 200ms 才是真正的瓶颈. 在 paper 阶段, 是否需要立刻上 7 vCPU pin?

**选项:**

| 选项 | 描述 | Pros | Cons |
|---|---|---|---|
| **A 7 vCPU pin (v0.6 设计)** | 立刻上 7 核 pin + isolcpus + nohz_full | 与 v0.6 架构一致; 发现真实跨核 SPSC latency | paper 阶段跨洋 RTT 200ms, 7 核 pin 收益边际极低; 老姜 W6 工程量大; macOS dev 无真等价的 sched_setaffinity |
| **B 2-3 vCPU (精简)** | paper 阶段: vCPU0=Ingest+Signal / vCPU1=RM+Signer+WAL / vCPU2=ML+Settlement; 3 核够 | 工程复杂度低; paper e2e 3.9us 说明单线程性能余量大; 把工程资源放在更关键的 SPSC 落地 (P0 偏离) | 与 v0.6 设计不一致, 需要 v0.7 更新; live 阶段需再重构 |
| **C M4.5 前 single thread (或 2-3 vCPU), M5+ 7 vCPU** | paper 期保持简单; live 资金上线前再上 7 核 | 分期; paper 不需要 500us 内环优化; 先把 SPSC 拓扑 (P0) 解决再上 pin | 两次架构改动; live 上线窗口工程量集中 |
| **D 其他** | 例如: 5 vCPU (去掉 ML vCPU4, ML 异步进 vCPU2 空闲时间) | 进一步精简 | ML 异步 + RM 共核增加 R-12 风险 (ML 慢拖垮 RM) |

**现状参考:**
- 老周 arch-deviation Part 2: vCPU 拓扑 + pin W5 全未实施 (P1 偏离)
- 老周 arch-deviation Part 4: SPSC 5 queue 全未实施 (P0 偏离 — 比 vCPU pin 更优先)
- 老姜 latency-budget-v1: 决策内环 p99 < 500us 目标; 跨洋 RTT 160ms 是"物理常数不在 budget 内"
- 关键洞察: P0 偏离 (SPSC 拓扑) 必须先于 vCPU pin 落地, 因为 pin 要依赖 SPSC ring 才有意义

---

## Part 2: 6 人投票表

| 议题 | 老周 (A) | 老韩 (B) | 小梁 (C) | 小余 (D) | 老胡 (E) | 老郭 (F, 你) | 多数 | 一致性 |
|---|---|---|---|---|---|---|---|---|
| **1 paper/live 共享** | TBD | TBD | TBD | TBD | TBD | **C** (见 Part 3) | ? | 待集成 |
| **2 WSS 拓扑** | TBD | TBD | TBD | TBD | TBD | **E** (见 Part 3) | ? | 待集成 |
| **3 跨洋部署** | TBD | TBD | TBD | TBD | TBD | **C** (见 Part 3) | ? | 待集成 |
| **4 ML 时机** | TBD | TBD | TBD | TBD | TBD | **C** (见 Part 3) | ? | 待集成 |
| **5 vCPU pin** | TBD | TBD | TBD | TBD | TBD | **C** (见 Part 3) | ? | 待集成 |

**说明:** 老周 / 老韩 / 小梁 / 小余 / 老胡 5 位主管的投票位置标注 TBD, 等各主管 input file 到位后, 我作为协调人更新表格, 不替任何主管填投票. 老雷 (GM) 与老钱 (CPO) 不投票, 看完 6 票后拍板.

---

## Part 3: 老郭 (F 协调) 5 议题投票 + 理由

---

### 议题 1: 我投 C (共享 core, 分离 transport)

**一句话:** R-2 红线的核心价值是防止 RM + Signal 分叉 bug, 这个价值通过共享 core 静态库完全可以保留; 强行共享 transport 层 (signer/matcher) 没有额外价值反而带来 `#ifdef` 噪音.

**具体理由:**

1. **分叉 bug 的真正来源是 RM + Signal, 不是 transport.** RM 的 21 RejectCode 逻辑、Signal Engine 的 PinnacleNoVig 计算, 如果 paper 和 live 用不同版本就会产生"paper 通过但 live reject"的隐患. 这两个模块必须共享. 但 PaperSigner (虚拟签名, 无私钥) 和真 Signer (ECDSA secp256k1) 本来就是不同实现, VirtualMatcher 和真 CLOB matcher 本来就是不同实现, 没有共享的必要.

2. **当前 R-7 build-time switch 已经是 C 方向的雏形.** `CMake if-else` + 4 build target 本质上就是把 transport 层分开编译. C 方案只是把这个逻辑显式化为"core 静态库 + transport executable", 更清晰.

3. **`#ifdef PAPER_MODE` 累积是技术债.** 老周 arch-deviation Part 6 已经看到: SingleInstanceLock 这个"小"改动没进 v0.6 文档就上代码了. 随着 live 路径复杂度上升, `#ifdef` 分支只会越来越多越来越难 audit.

4. **反对 B (完全分叉):** 分叉 RM 是红线级风险. 老韩 B 主管 input §5 明确: R-1 RM 唯一入口必须 0 偏离. 完全分叉就是为 R-1 开洞.

5. **实施路径:** C 方案不需要 W6 立刻重构. 可以在 v0.7 ADR 中锁定方向, M4.5 前 live transport 落代码时自然按 C 方案实施.

**申辩窗口:** 24h 开放 (至 2026-06-02 EOD), 任何主管可提出反对意见入档.

---

### 议题 2: 我投 E (阶段性: paper 1 conn, M4.5 后升 4-5 conn)

**一句话:** paper 阶段 SPSC 拓扑 (P0 偏离) 未落地, 上多连接没有意义; 但 live 阶段前必须升到生产标准; E 方案是最务实的分期路径.

**具体理由:**

1. **P0 偏离 (SPSC 5 queue 未实施) 优先于 WSS 多连接.** 老周 arch-deviation Part 4 标注 P0. 没有 SPSC ring, 多连接带来的事件流只会全部拥入同一个 `std::deque` — 多连接的故障隔离意义完全消失. 先上 SPSC 拓扑 (小石 W6-A-03), 再评估多连接, 才是正确的顺序.

2. **paper 阶段 1 conn 自愈已验证.** 小冯 M1-G7 WSS reconnect chaos test (kill -STOP 30s 自愈) W5 已 ✓. 对 paper engine 来说 1 conn 风险可接受 (没有真实资金损失).

3. **M4.5 → live 上线前的窗口是最合适的升级时机.** 此时 SPSC 已落地 (W6), paper 跑了 M1→M4.5 的数据, 连接行为有实测数据支撑, 可以按老李 v3 标准真正评估 4-5 conn 的必要性.

4. **反对立刻上 B 或 C:** W6 工程资源应优先用于 SPSC P0 偏离 + Position Ledger + SettlementWatcher (老周 Part 9 W6 P0 派单 3 项). 多连接是 W7+ 的工作.

5. **关于 D (老叶 3 进程):** 进程级隔离最终目标合理, 但 3 进程需要共享 nonce / position 状态的 IPC 机制, 这是 M5+ 的复杂度. 现阶段不考虑.

**申辩窗口:** 24h 开放.

---

### 议题 3: 我投 C (M4.5 前单点, M5+ 评估多点)

**一句话:** 跨洋部署的多点问题本质上是 SRE + 财务 + nonce 协调三个维度的问题; paper 阶段没有真实资金风险, 单点足够; live 阶段前才是做这个评估的合适时机.

**具体理由:**

1. **double-spending / nonce 冲突是多活方案的最大障碍.** 老吴 v0.1 §5.2 明确: "写交易只有 primary 出 (避免双发同 nonce)". 真正的 active/active 需要分布式 nonce 协调, 这是老叶 + 老孙 M5+ 的工作范围. 多活不是 WSS 拓扑问题, 是链上事务一致性问题.

2. **Tokyo 节点延迟反而比 us-east 差.** 老吴测点: ap-east-1 (HK) RTT 到 Polymarket 180ms, 远高于 us-east-1 的 <5ms. Polymarket origin 在 us-east, Tokyo 主节点对决策链路不是 "avg latency 降" 而是 "avg latency 升".

3. **paper 阶段运维资源有限.** 老吴目前是单人 SRE. 多 region 部署把 Ansible playbook 复杂度 × 2, 不是 MVP 阶段应该做的.

4. **反对 B (多 region 多活):** 如上所述, active/active 需要解决分布式 nonce 协调 + 双向数据同步, 这不是 ADR 级别的决议能解决的, 需要专项设计 (老叶 Standby + 老孙 signer 协调).

5. **M5+ 评估的触发条件:** live 资金上线后, 若出现 us-east-1 区域性故障导致 5min+ 停机, 再评估 Tokyo secondary active 或 Hetzner active standby RTO 缩短. 有真实 SLA 数据再决策.

**申辩窗口:** 24h 开放.

---

### 议题 4: 我投 C (M2 后 shadow, M4.5 后 active)

**一句话:** shadow inference 不破 ML-R1/R2 红线; 但 W6 没有可推理的模型 (小邓阶段 0 不训练模型), 最早也要阶段 1 (7/24+) 出 LightGBM baseline 后才有意义; M2 (8/6) 配套是合理时机.

**具体理由:**

1. **W6 接 shadow inference 是无用功.** 小邓 ML roadmap v2 §4.1 明确: 阶段 0 (6/15→7/24) "NOT 做: 训练任何模型". 没有模型的 shadow inference 就是空跑推理框架, 资源浪费. MLSignalCandidate struct 也未实施 (老周 P2 偏离, W7+ 才联调).

2. **M2 (8/6) 是正确的时机窗口.** M2 时: (a) paper engine 已跑 6+ 周, FeatureSnapshot 数据积累; (b) 阶段 0 数据基设已完成 (D0-1~D0-8, 截止 7/24); (c) 阶段 1 LightGBM baseline 开始训练. shadow inference 此时接入才有真实数据.

3. **M4.5 (9/12) 后 active 与 ML-R6 配套.** ML-R6: "4 类 case 中 rule 输 ML 赢 ≥ 20% 才解锁 production". M2→M4.5 之间的 shadow 窗口 (~6 周) 就是积累 4 类 case 对比数据的时间.

4. **反对 A (严守 M4.5 不接 shadow):** 严守 ML-R1 是正确的. 但 shadow inference 本来就是 ML-R2 允许的模式 ("paper 期 ML 不进 OrderIntent 路径" — shadow 不进 OrderIntent). A 选项过于保守, 白白浪费 M2→M4.5 的 6 周验证窗口.

5. **不耻下问:** @小邓: M2 (8/6) 时机是否可以有最简版 LightGBM 模型可用? @小梁: C 量化视角对 M2 后 shadow 是否有不同看法?

**申辩窗口:** 24h 开放.

---

### 议题 5: 我投 C (M4.5 前 2-3 vCPU, M5+ 7 vCPU)

**一句话:** SPSC 拓扑 P0 偏离是当前最紧迫的基础设施问题; paper 阶段跨洋 RTT 200ms 使得内环 pin 的边际收益极低; 7 vCPU 设计在 live 上线前才真正需要.

**具体理由:**

1. **SPSC 拓扑未落地 → vCPU pin 没有意义.** 老周 arch-deviation Part 4: SPSC 5 queue 全未实施 (P0). 在 in-memory `std::deque` + 单线程的当前实现下, pin 7 个核是给空壳 pin. pin 有意义的前提是: 有真实的 producer/consumer 在不同 vCPU 上运行, 依赖 SPSC ring 通信. 先做 SPSC (小石 W6-A-03), 再考虑 pin.

2. **e2e 3.9us 的安全余量极大.** 老周 caveat: 当前 3.9us 是 in-memory mock 数据, 加上 SPSC + pin + 真 WAL fsync 后预计 10-100x 上涨 (40us-400us). M1 G2 目标是 80ms p99, 余量仍有 200x+. 不需要现在精细优化 vCPU pin.

3. **跨洋 RTT 200ms 是决策链路的绝对瓶颈.** 老姜 §0: "跨洋 RTT 160ms 不在 budget 内 — 是物理常数". 内环从 500us 优化到 300us 对总延迟影响 < 0.15%. vCPU pin 在 paper 阶段是过度设计.

4. **2-3 vCPU 方案 (B) 的合理性:** paper 阶段的实际并发需求: (a) Ingest+Signal 可以共 vCPU0 (Goalserve poll 2-5s 一次, 不是真实 hot path); (b) RM+Signer+WAL 共 vCPU1; (c) ML+Settlement 低频共 vCPU2. 3 核完全够, 且更容易在 macOS dev 环境跑通.

5. **C 而非 B 的理由:** M5+ live 上线时, 真实的跨洋 + 真实的签名延迟会让 7 vCPU pin 的价值显现 (老姜 latency-budget §1: ECDSA 签名 p99 80us 是最大头, 独立 vCPU 隔离是合理的). 所以 7 vCPU 是 M5+ 需要做的, 不是永远不做.

6. **macOS 开发兼容性:** macOS 没有 `sched_setaffinity` 等价, 7 vCPU pin 在 dev 环境无法真实测试. 先用简单模型跑通 paper, 在 Linux 生产环境再引入 pin.

**申辩窗口:** 24h 开放.

---

## Part 4: 多数 + 一致性分析 (待 5 主管投票后更新)

**当前状态:** 仅老郭 (F) 1 票已投. 5 主管 (老周/老韩/小梁/小余/老胡) TBD. 以下分析为预测框架, 等实际票数后更新.

### 4.1 预期一致性分析

| 议题 | 老郭初票 | 预期分歧点 | 一致性预测 | 需要争议会? |
|---|---|---|---|---|
| 1 paper/live 共享 | C | 老周 (A) 可能倾向 A (现状派), 老韩 (B) 可能倾向 A (RM 分离风险) | 中等分歧 (可能 3-4 vs 2-3) | 视实际票 |
| 2 WSS 拓扑 | E | 老周可能 E/C, 小冯实施者视角可能 A, 分歧较小 | 较高一致 | 可能不需要 |
| 3 跨洋部署 | C | 所有主管可能都倾向 C (分期最务实), 一致性预期高 | 高一致 | 不需要 |
| 4 ML 时机 | C | 小梁 (C 量化) 可能想更激进, 老韩 (B 风控) 可能更保守; 分歧中等 | 中等分歧 | 视实际票 |
| 5 vCPU pin | C | 老周可能 C/A (架构设计意图), 老姜方向是 A; 中等分歧 | 中等分歧 | 视实际票 |

### 4.2 一致性规则 (本次会议立)

- **≥ 4/6 票一致 → 直接立 ADR** (无需全体争议会)
- **3-3 分裂或 2-2-2 三分 → 全体争议会** (老雷召集, 含顾问 + IC)
- **5/6 或 6/6 一致 → 快速立 ADR** (24h 申辩窗口后直接 closeout)

### 4.3 待更新

等 5 主管 input file 交回后, 我更新此节:
- 实际票数矩阵
- 每个议题的多数选项
- 是否触发全体争议会

---

## Part 5: ADR 候选 (从投票结果派生)

以下 ADR 候选基于老郭初票 (C/E/C/C/C). 实际 ADR 内容在 6 票汇总后确定.

---

### ADR-011 候选: paper/live binary 共享策略

**议题:** 1
**老郭初票:** C (共享 core + 分离 transport)
**多数选项:** 待 6 票汇总

**ADR 候选内容:**
- **决策:** 共享 stcpp::core 静态库 (RM + Signal Engine + 4 ts struct); transport 层 (Signer + Matcher) 分离为独立 executable
- **反对方理由 (若有):** 老周 / 老韩 若投 A: 认为 `CMake R-7 build-time switch` 已足够, C 方案引入额外 lib 版本管理复杂度
- **实施 owner:** 老周 (A 主管) — v0.7 ADR 锁定, M4.5 前 live transport 落代码时按 C 方案实施
- **时间节点:** v0.7 ADR (W6), 代码实施 M4.5 前 (约 9/12 前)
- **申辩记录:** 24h 窗口 → 2026-06-02 EOD

---

### ADR-012 候选: WSS 订阅拓扑

**议题:** 2
**老郭初票:** E (paper 1 conn, M4.5 后升 4-5 conn)
**多数选项:** 待 6 票汇总

**ADR 候选内容:**
- **决策:** paper 阶段维持小冯 W5 v0.1 (1 conn × 8 sub + exp backoff); M4.5 paper 达 OKR 后, live 上线前升级至老李 v3 标准 (4-5 conn × 2 sub)
- **前置条件:** 小石 W6-A-03 SPSC 拓扑落代码 (P0 偏离) 必须先于多连接改造
- **反对方理由 (若有):** 老周若投 C/B: 认为 2 conn 故障域分离应立刻上; 老李若投 B: 认为生产标准应从一开始就立
- **实施 owner:** 小冯 (W5 现状维护) + 老李 (M4.5 后 4-5 conn 升级)
- **时间节点:** paper 现状不动; M4.5 (9/12) 后升级计划 (老李 Sprint-4+ 派单)
- **申辩记录:** 24h 窗口 → 2026-06-02 EOD

---

### ADR-013 候选: 跨洋部署策略

**议题:** 3
**老郭初票:** C (M4.5 前单点, M5+ 评估多点)
**多数选项:** 待 6 票汇总 (预期高一致)

**ADR 候选内容:**
- **决策:** AWS us-east-1 主节点 + Hetzner Ashburn standby, active/standby 模式不变; M5 live 资金上线后评估是否需要 secondary active region
- **触发条件 (M5+ 多点评估):** us-east-1 区域故障导致 5min+ 停机 > 2 次/月; 或真实资金损失 > $500 (standby 切换窗口)
- **反对方理由:** 若有主管投 B: 认为 live 前应提前建 Tokyo 节点
- **实施 owner:** 老吴 (SRE, 现状维护)
- **时间节点:** 现状不动; M5+ 评估 (12 月+)
- **申辩记录:** 24h 窗口 → 2026-06-02 EOD

---

### ADR-014 候选: ML shadow inference 时机

**议题:** 4
**老郭初票:** C (M2 8/6 后 shadow, M4.5 后 active)
**多数选项:** 待 6 票汇总

**ADR 候选内容:**
- **决策:** M2 (8/6) 后接 shadow inference (阶段 1 LightGBM baseline 就绪后); ML-R1/R2/R3 红线不动; shadow 走独立账本, 不影响 if-else 决策; M4.5 后按 ML-R6 (4 类 case rule 输 ML 赢 ≥ 20%) 才解锁 active
- **前置条件:** (a) 小邓阶段 0 数据基设完成 (7/24); (b) LightGBM baseline 就绪 (阶段 1); (c) MLSignalCandidate struct 补全 (老周 P2 偏离, W7+)
- **反对方理由 (若有):** 老韩若投 A: 认为 shadow 增加 W6 复杂度, M4.5 前应保持 rule-only 干净; 小梁若投 B: 认为应 W6 就起 shadow framework
- **实施 owner:** 小邓 (ML) + 老周 (MLSignalCandidate struct W7+) + 小蒋 (shadow path 联调)
- **时间节点:** M2 (8/6) 后启动 shadow; M4.5 (9/12) gate 评估 active
- **申辩记录:** 24h 窗口 → 2026-06-02 EOD

---

### ADR-015 候选: vCPU pin 策略

**议题:** 5
**老郭初票:** C (M4.5 前 2-3 vCPU 简化, M5+ 7 vCPU)
**多数选项:** 待 6 票汇总

**ADR 候选内容:**
- **决策:** paper 阶段 (现在→M4.5): 精简为 2-3 vCPU 逻辑分区 (不强 pin, 或软 pin), 工程资源优先用于 SPSC P0 偏离; M5+ live 上线前实施 7 vCPU 完整 pin (含 isolcpus + nohz_full)
- **前置条件 (M5+ 7 vCPU):** (a) 小石 SPSC 5 queue 落地 (W6-A-03 P0); (b) 老姜跨洋 Linux 环境实测 `pthread_setaffinity_np` 性能数据
- **反对方理由 (若有):** 老周若投 A: 认为 v0.6 §2 vCPU7 设计应从 W6 起落; 老姜若投 A: 认为延迟预算应从 paper 就开始严格执行
- **实施 owner:** 老姜 (vCPU pin) + 小石 (SPSC 依赖)
- **时间节点:** paper 现阶段精简; M5+ (12 月+) 7 vCPU 完整实施
- **申辩记录:** 24h 窗口 → 2026-06-02 EOD

---

## Part 6: 给 GM 老雷 + CPO 老钱 的拍板包

### 6.1 概览

本次架构 Challenge 投票会 5 议题, 目标是打破"不向不合理架构妥协"的惯性. 老郭已投 5 票 (全部 C 或 E — 阶段性 / 中间方案), 5 主管 input 待补全.

**核心立场 (老郭协调人 evaluate):**

v0.6 架构的 5 个争议点, 都存在"设计意图合理但 paper 阶段过度超前"的问题. 不是 v0.6 错了, 而是 MVP → M1 阶段的工程优先级排序需要务实:
1. SPSC 拓扑 (P0 偏离) > WSS 多连接 > vCPU pin — 这是 W6 的正确优先级
2. paper 阶段 < live 阶段 — 很多设计在 live 资金上线前才真正需要落地
3. 分期实施 > 一步到位 — 在代码尚未积累真实性能数据的阶段, 过早优化是浪费

### 6.2 直接 ack 清单 (预期高一致, 若 ≥ 4/6 同意)

| ADR 候选 | 老郭预测 | 建议 GM 直接 ack |
|---|---|---|
| ADR-013 跨洋部署 C (单点 M4.5 前) | 预期 5-6/6 一致 | 直接 ack |
| ADR-012 WSS 拓扑 E (paper 1 conn, M4.5 后升) | 预期 4-5/6 一致 | 可直接 ack |

### 6.3 升全体争议会清单 (若 ≤ 3/6 一致)

| 议题 | 争议点 | 召集条件 |
|---|---|---|
| 议题 1 paper/live 共享 | C vs A — 老周/老韩 是否接受 transport 分离 | 若 3-3 分裂 |
| 议题 4 ML 时机 | C vs A vs B — 风控 vs 量化 vs 工程三方分歧 | 若 2-2-2 三分 |
| 议题 5 vCPU pin | C vs A — 老周/老姜 架构合规性 vs 务实工程 | 若 3-3 分裂 |

**全体争议会触发时:** 老雷召集, 含老郭 + 相关顾问 (老何 modernC++ / 老姜 perf / 小邓 ML), IC 代表参与 (老姜 / 小石 / 小蒋), 出会议纪要 + ADR.

### 6.4 W6 起按新 ADR 落代码

老周 v0.7 需要基于投票结果更新的章节:

| 变更 | v0.7 章节 | 触发 ADR |
|---|---|---|
| paper/live binary 策略 | §0 架构变更摘要 + §1 链路图 | ADR-011 |
| WSS 拓扑阶段性方案 | §4 SPSC ring 拓扑 (WSS 连接数章节) | ADR-012 |
| vCPU pin 精简 (若 C 通过) | §2 vCPU 线程模型 (paper 阶段精简为 2-3 vCPU) | ADR-015 |
| ML shadow inference 时机 | §2 vCPU4 ML Hook 章节 | ADR-014 |

---

## Part 7: 协商投票讨论原则 (本次会议立)

基于 GM 老雷原话: "协商投票讨论, 不死守最初版本; 不向不合理架构妥协, 因为我们要做的是高质量的产品."

### 7.1 本次会议 5 原则

1. **不预设答案.** 5 议题各列 4-5 选项, 没有"默认正确答案", v0.6 的选项只是候选之一.

2. **投票多数 ≥ 4/6 → 直接立 ADR.** 不需要全体同意, 多数即可推进. ADR 内必须记录反对方理由 (申辩入档).

3. **投票分歧 (≤ 3/6) → 全体争议会.** 老雷召集, 扩大参与面 (含顾问 + 相关 IC), 争议会出 ADR + 多数决.

4. **反对方理由必入 ADR (24h 申辩窗口).** 即使最终未采纳, 反对理由入 ADR §申辩记录. 透明可追溯.

5. **不向不合理架构妥协.** 即使需要重做部分设计 (如 transport 分离) 也要做对. "高质量产品"比"不破坏已有文档"更重要.

### 7.2 永久协商 SOP (本次会议后固化)

- 每次重大架构 challenge → 走 Part 1 (背景 + 选项 + 利弊) + Part 2 (投票表) + Part 3 (主持人先投 + 理由) 格式
- 会议文档在 `docs/MEETINGS/` 归档 (小米 doc-curator)
- ADR 候选在 `docs/ADR/` 归档, 24h 申辩窗口后 closeout
- 下次月度架构评审 (每月第 1 个周四): 老郭主持, 回顾本次投票落地情况

---

## Part 8: 协调人行动项

### 8.1 等 5 主管 input 到位后 (老郭更新)

- [ ] 更新 Part 2 投票表 (填入 5 主管实际投票)
- [ ] 更新 Part 4 多数 + 一致性分析 (真实票数)
- [ ] 更新 Part 5 ADR 候选多数选项
- [ ] 标记哪些 ADR 候选直接 ack, 哪些升全体争议会

### 8.2 给 GM 老雷 + CPO 老钱

- [ ] 6 票汇总后通知 GM dial-in 拍板
- [ ] 分歧 ADR 升全体争议会 (老雷召集)
- [ ] 一致 ADR (≥ 4/6) 请 GM 24h 内 ack
- [ ] 通知老周更新 v0.7 (按 ADR 决议)

### 8.3 不耻下问 (我自己的 open questions)

- **@老周 (A):** 议题 1 C 方案 (transport 分离) 是否与你 v0.7 启动期 11 步序列冲突? 你对 CMake 静态库拆分有无工程层面的顾虑?
- **@老韩 (B):** 议题 1 C 方案, RM 作为共享 core 静态库, 是否满足你的 R-1 audit 要求?
- **@老姜 (#39, 性能):** 议题 5 C 方案 (paper 2-3 vCPU), 你的 latency-budget-v1 对 paper 阶段是否有不同约束? SPSC P0 补上后, 你建议的最小 vCPU 数是多少?
- **@小邓 (#31, ML):** 议题 4 C 方案 (M2 8/6 后 shadow), M2 时 LightGBM baseline 是否会就绪? 若不就绪, 应该推到 M3 (选项 D)?
- **@老雷 (GM) + @老钱 (CPO):** 5 议题中哪个对产品路线影响最大? 特别是议题 4 (ML 时机) 是否有 CPO 视角的偏好?

---

**完成汇报:**

架构 Challenge 投票会 v1 文档完成. 包含:
- 5 议题完整背景 + 选项 + 利弊分析 (Part 1)
- 6 人投票表 (老郭 5 票已投, 5 主管 TBD, Part 2)
- 老郭 5 议题投票 + 详细理由 (Part 3, 全投阶段性/中间方案)
- 多数 + 一致性分析框架 (Part 4, 待 5 主管票)
- 5 ADR 候选 (ADR-011 ~ ADR-015, Part 5)
- GM + CPO 拍板包 + 全体争议会触发条件 (Part 6)
- 协商投票讨论 SOP 5 原则 (Part 7)

**下一步:** 等 5 主管 input file 到位 → 更新 Part 2 投票表 → 通知 GM 拍板

24h 申辩窗口: 2026-06-01 → **2026-06-02 EOD**

— 老郭 (F 协调, chief-architecture-reviewer), 2026-06-01
