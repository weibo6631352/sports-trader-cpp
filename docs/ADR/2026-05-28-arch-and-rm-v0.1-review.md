# ADR-001: 架构 v0.1 + RM v0.1 评审决议

- Owner: 老郭 (chief-architecture-reviewer)
- Date: 2026-05-28
- Status: **Conditional Accept** (老周 5 项, 老韩 4 项 — 见 §2 / §3)
- Reviewers: 老郭 主审, 老雷 会签
- Related:
  - `docs/RESEARCH/laozhou-architecture-v0.1.md`
  - `docs/RESEARCH/laohan-riskmanager-design-v0.1.md`
  - 交叉验证: `laojiang-latency-budget-v1.md`, `laowu-cross-region-deployment-v0.1.md`,
    `xiaoshi-data-structures-selection-v1.md`, laosun-key-management-v1.md (Rust 版已删; 当前 signer SSOT: v5.1)

---

## 1. 评审范围

本次评审两份 Wave 2 提交:

1. **老周 系统架构 v0.1**: 5 层分层 / 数据流 / 依赖图 / 10 项技术决策 (D1-D10) /
   通信机制 / 配置与状态 / 进程模型 / 失败恢复 / 性能预算.
2. **老韩 RiskManager v0.1**: 红线声明 / 同步门禁接口 / 10 条规则 / 状态机 / audit / 参数表.

老周特别请决: **D1 / D2-§2.4 / D3-§9.3** 三项.
老韩特别请决: **R-2 (STALE 阈值)** 与 **R-3 (audit fsync 性能)** 两项.

老郭出场口径:
- 不替老周老韩重写文档.
- 每条决议带反方观点 + "为什么不那样".
- 风险标注影响半径 (一行 / 一模块 / 全系统).
- 跨专业争议明确点名 @ 责任人, 不抢活.

---

## 2. 老周架构评审

### 2.1 D1 — C++ 标准 = C++20 + 自研 Result<T,E>

**决议: ACCEPT.** 拍板 C++20, 不上 C++23.

**为什么不是 C++23:**
- `std::expected` 是甜点, 不是刚需. 自研 `Result<T, Status>` 等效, 且能内嵌
  `audit_id` / `error_code` enum, 比 std 更贴风控审计.
- clang 17 对 C++23 库支持 (尤其 `<flat_map>`, `<expected>`) 仍不完整;
  跨洋部署节点工具链锁定带来的 CI 难度收益不抵.
- C++20 已经够: concepts (策略多态), ranges (特征管道), `std::span` (zero-copy IO),
  `std::atomic_ref` (lock-free), `<bit>` (price ladder bit tricks).
  我们用得到的 C++23 特性几乎没有.

**为什么不直接用 `tl::expected` / `boost::outcome`:**
- 第三方 expected 都各有取舍 (tl 是 header-only 但无 monadic chain 风险, outcome
  ABI 沉重). 我们的 `Result<T, Status>` 只需 ~150 行 + concept 约束, 维护成本远低于
  讨论选哪个三方库.

**附加约束 (强制写入 ADR):**
- `Status` 必须是 trivially copyable POD, 且 `sizeof(Status) <= 16 byte`.
- `Result<T, Status>` 在热路径必须 `[[nodiscard]]`, 编译期断言不抛异常.
- 自研实现 PR 需 @小石 + @老姜 review (lock-free / cache 行为对齐).

**影响半径: 全系统** (工具链 + ABI).
**风险**: 低. C++20 在 clang 16+ / gcc 13+ 完全可用, 跨洋节点 (Ubuntu 22.04 / 24.04)
默认编译器即支持.

---

### 2.2 D2 — RiskGateway 唯一对外符号 + 编译期 link 阻断

**决议: ACCEPT, 增强版.** 老周提"唯一对外符号"不算过严, 实际还不够严.

**为什么必须这样而不是"代码 review 拦":**
- 红线 D-02 + 老韩 G1 (0 起绕过事故/年) 是合规命题, 不是工程偏好.
  人 review 在 6 个月后疲劳期一定漏, 必须**编译期机器拦**, 不留人治空间.
- `risk` 库只 export `RiskGateway::evaluate()` 一个符号; `risk/limits` / `risk/audit` /
  `risk/halt` 在 link 层不暴露, 任何 L5 模块尝试 `#include <risk/limits.hpp>` 直接编译失败.
- CMake 层用 `target_link_libraries(exec PRIVATE risk_gateway)`, 不依 `risk_internal`.
  策略层 `target_link_libraries(strategy PRIVATE risk_intent_only)` — 只见 `OrderIntent`
  类型, 见不到 `RiskGateway`.

**增强项 (老郭加的, 不在老周 v0.1 里):**

1. **CI 静态扫描** (派 @老练 testing-coach 落地):
   - grep 检测 `exec/signer` / `exec/clob` / `exec/router` 任何文件中, 凡调用签名 API
     的位置, 必有同函数内**前序调用** `RiskGateway::evaluate()` 且 decision==APPROVED 分支检查.
   - 不通过 = CI block-merge, 不允许 waiver.

2. **运行时 trip-wire** (派 @小郑 metrics):
   - signer 内部维护 `last_audit_id` 计数器, evaluate 调用与 sign 调用 1:1 匹配.
     任何 sign 没有对应 audit_id 前缀 = 立即 abort + 高优告警.

3. **架构 review 红线** (老郭一票否决项, 永久):
   - 任何 PR 修改 `risk/` 目录的对外接口 (新增 public class / 增加 friend 声明 /
     放宽 link visibility) 必须老郭 + 老韩双签, 老雷知情.

**反方 ("是否过严?") 回应:**
- 老周自己问"是否过严" — 我答**还不够严**. RM 不是性能模块, 是安全模块. 安全模块的
  防御纵深永远是"代码 review + 编译期 + 运行时" 三层, 缺一不可.
- 唯一接受的便利是: 测试代码 (`tests/`) 允许通过 friend test fixture 注入 mock,
  但 production binary 必须 link 时排除 test target.

**影响半径: 全系统** (定义了 L4-L5 边界, 改不动).
**风险**: 低. 老周 §4 依赖图已明示这条边, ADR 只是把它从"文档约定"升级为
"build system + CI + runtime trip-wire".

---

### 2.3 D3 — §9.3 热路径线程 fail-fast 整进程 abort

**决议: ACCEPT, 加 4 条边界条件.** 整进程 abort 是对的, 但不能 naive 实现.

**为什么不是线程级 try-recover:**

| 维度 | 线程级 try-recover | 整进程 abort + WAL replay |
|---|---|---|
| 状态一致性 | 极难保证 (nonce/position/queue 半状态) | systemd 重启 + WAL 重放 = 确定性 |
| 风控审计可追溯 | 局部恢复后 audit 出现"幽灵决策" | 重启起点清晰, audit 闭合 |
| 调试 | core dump 部分线程 = 现场被破坏 | full core dump 现场完整 |
| MVP 收益 | -- | 简单, 可测试, 可演练 |

线程级 try-recover 在 trading system 的工程教训是: **十次有八次后续事故根因是上次
"已修复" 的局部恢复留下的脏状态**. Knight Capital 2012 事故的近因就是局部状态污染.
我们 MVP 阶段绝不开这个口子.

**ACCEPT 的边界条件 (4 条, 老周 v0.2 必须补):**

1. **abort 前必须 flush** (在 signal handler 内 best-effort, 不阻塞):
   - audit log ring buffer flush (老韩 §5)
   - nonce / position WAL fsync (§9.4)
   - 当前线程 core context 写入 core dump
   - metrics last-known-state 推 1 帧

2. **abort 触发条件白名单**, 不能"任何 errno 就 abort":
   - 不变量违反 (assert / invariant_check 失败) → abort
   - 内存损坏迹象 (double free / use after free, asan trap) → abort
   - 锁状态不一致 (RCU epoch 异常) → abort
   - 上游 IO 错误 / WSS 断 / parse 失败 → **不 abort**, 走重连或 RM halt 路径
   - 业务规则触发 (例如 RM 检查到资金不一致) → **不 abort**, 走 SAFE_MODE

3. **abort 速率限制**: systemd 配 `StartLimitIntervalSec=300, StartLimitBurst=3`,
   5 分钟内连续 abort 3 次 = 锁定不再重启 (避免崩溃风暴 + 反复重放 WAL 把数据搞坏).
   锁定后必须人工 ack (@老吴 配 systemd unit).

4. **SAFE_MODE 启动**: 重启后默认进 `SAFE_MODE` (只撤不开仓, 老韩 §4 DRAIN 等价),
   对账完成 + heartbeat 正常 5 分钟才解锁交易. 老周 §9.2 已经画出, ADR 把它从
   "建议路径" 升级为 "硬强制".

**反方 ("线程级 try-recover 救机会") 回应:**
- 跨洋 + 高延迟环境下, 一次进程重启 2-5 秒走 WAL replay + 对账, 错过的是 2-5 秒的
  alpha 窗口. 而局部恢复留下的脏状态可能在数小时后变成 $50k 亏损. ROI 一边倒.

**影响半径: 全系统** (定义了崩溃语义).
**风险**: 中. 4 条边界条件不落地, 会演成 "atexit handler 死循环" 或者
"systemd restart loop", 加上 abort 速率限制后可控.

---

### 2.4 其他发现 (老周 架构 v0.1)

#### F-1 [全系统] §11 性能预算与 §8 部署模型耦合不清晰, ADR 要求老周 v0.2 明示

老周 §11 写 "热路径 p99 < 500us 不含跨洋 RTT". 这对的, 但**前提是节点部署在 us-east-1
与 Polymarket origin 同区** (老吴 S1-010 的方案). 如果哪天有人 (例如老雷)
临时把节点放回国内做演示, 这套预算全废, 但**老周文档没标这个前提**.

**整改要求**: §11 第一句必须加 "本预算假设主节点部署在 us-east-1, 与 Polymarket
WSS origin / Polygon RPC edge 同区 RTT < 5ms; 主节点选址变更 (例如回国内部署)
本预算作废, 需重做". 否则下一个新人接手会以为内环 500us 在任何地点都成立.

#### F-2 [一模块] OQ-5 KMS 同步阻塞 vs 异步预签 — 已被老孙 v1 隐式决了

老周 OQ-5 问 "KMS 调用同步 vs 异步预签对 reactor 模型的影响". 老孙 v1 §2.1 决议是
**本地 software signer + age-encrypted at-rest** (方案 G), 单核 20k ops/s, p99 50us.
不走 KMS 远程签名. 所以 OQ-5 在 v0.2 可以关闭, 改注 "已由 S1-005 老孙 v1 决议:
本地 signer, p99 50us, 同步调用即可".

**整改要求**: 老周 v0.2 §12 OQ-5 → CLOSED, 引用老孙 v1 §2.1.

#### F-3 [一模块] OQ-7 NUMA 策略 — 老吴 v0.1 已隐式决了

老吴 S1-010 §4 选 AWS c6i.large (单 socket, 2 vCPU 实际 1 个物理核 + HT).
老周 §8.1 列了 8 个 core, c6i.large 装不下. 这是**容量与拓扑不一致**, 必须修一面.

**整改要求** (二选一):
- (a) 老周 §8 重画 core 分配 (4 个 core 物理: net-io / parse+book / strategy+risk+exec / bg),
  接受性能预算的 ~10% 退步.
- (b) 老吴 升级到 c6i.xlarge (8 vCPU = 4 物理 + HT) 或 c6id.2xlarge, 月费多 $100,
  老钱 S1-008 评估.

老郭倾向 **(b)**: 多 $100/月换跨洋部署一辈子的核分配自由度, 划算; 但需要老钱过预算.
@老雷 拍板.

#### F-4 [一模块] §7.3 自研轻量 WAL — 老周 v0.2 必须给最小可证明设计

老周 §7.3 写 "自研轻量 WAL + 内存 hash + 启动重放, 不上 RocksDB". 方向对 (RocksDB
对 MVP 太重), 但**自研 WAL 是事故重灾区**. 老周 v0.2 必须补:

- WAL format (record layout / checksum / fsync 策略).
- crash 边界 (write 一半 / fsync 一半 怎么恢复).
- 与老韩 audit log fsync (§5.1) 是否复用同一个 WAL 文件 (老郭倾向**分离**, 见 R-3).

如果老周自己没空写, 老郭建议 派给 @小段 (replay engineer) 联合设计. **不允许在
没有 WAL 设计文档前 merge 任何 WAL 代码**.

#### F-5 [一模块] §6.2 跨进程 SHM ring 选型未定

老周 §6.2 说 "主交易进程与旁路进程通过 SHM ring + UDS 控制信道", 没指定实现.
小石 v1 §1.1 列了 rigtorp/folly/moodycamel, 但没覆盖**跨进程**.

**整改要求**: 派 @小石 在 S1-011 补一节 "跨进程 SHM ring 选型", 起步推荐
`boost::interprocess::shared_memory_object` + 自研 SPSC over SHM (跟 in-process 同协议).
低风险, MVP 可接受.

---

## 3. 老韩 RM 评审

### 3.1 总体定性

**老韩 v0.1 质量高于平均**, 这是一份合规导向 + 工程实战的设计. 红线声明 (§0)、
封闭 enum (§3.10)、状态机不可自动恢复 (§4.2)、参数单调调整 (§7) 全是经验之谈,
老郭 没意见. 集中评 R-2 / R-3 + 几个跨文档对齐问题.

---

### 3.2 R-2 — STALE_THRESHOLD_MS / STALE_HALT_MS 阈值

**决议: REJECT 老韩建议值 (30s/60s), 改 ACCEPT 老郭新值 — 见下.**

**为什么 30s/60s 不行:**

老韩自己说 "建议 30000ms, @老郭 跨洋链路你比我清楚". 老郭背景: 部署在 us-east-1
与 Polymarket origin 同区 (老吴 S1-010), Polymarket WSS RTT **< 10ms p99**, 不是
跨洋 200ms+. 30s 阈值意味着:
- WSS 实际断连 28 秒, 系统还在以 28 秒前的盘口下单. 28 秒在 Polymarket
  比赛中段足够发生进球 / 黄牌 / 受伤 / 比分变 — **risk-on 状态下的 30s 是灾难**.
- 老雷给的硬约束 (CLAUDE.md D-06): "数据 30s 无更新自动暂停市场". 老韩拿了这个
  最终值作 WARNING 阈值, 等于把 HALT 推到 60s, **违反了 D-06 红线**.

**老郭新值 (基于老吴部署假设):**

| 数据源 | STALE_THRESHOLD_MS (DEFERRED / WARNING) | STALE_HALT_MS (REJECT / HALT) | 依据 |
|---|---|---|---|
| Polymarket clob WSS | **2,000 ms** | **10,000 ms** | 同区 RTT < 10ms; WSS 心跳应 < 1s; 2s 已属可疑; 10s 必须停 |
| Goalserve inplay | **5,000 ms** | **15,000 ms** | poll 间隔 1s; 5s 内 5 次 miss 可疑; 15s 数据死透 |
| 资金对账 | **10,000 ms** | **30,000 ms** | 老韩 §6.3 说 "滞后 ≤ 5s", 留 5s 容忍 |

**为什么 HALT 阈值 ≤ 30s 而非老韩的 60s:**
- D-06 红线写 "30s 无更新自动暂停市场". 30s 是**系统级硬上限**, 任何单源 HALT
  阈值都不能超过 30s.
- 资金对账 30s 是底线 (老韩 §6.3 也提到 30s); WSS 不到 30s 就该 HALT, 这个比对账
  优先级还高 (盘口直接影响下单), 必须更严.

**反方 ("阈值这么小, 跨洋抖动会频繁触发 DEFERRED") 回应:**
- 那是因为老韩 v0.1 假设了**跨洋部署场景**. 老吴 v0.1 已经决定主节点在 us-east-1,
  Polymarket / Goalserve / Polygon RPC 全部同区或近距离. **风险登记 R-2 中提到的
  "跨洋抖动" 在选址决定后已经基本消失**, 老郭不接受这个反方.
- 如果哪天 (例如演示) 又把节点放回国内, 这套阈值要重做 — 但**这个分支老郭不预留**,
  谁要回国部署谁负责重写 RM 参数 + 重过 ADR.

**配套要求** (@老陈 + @老吴):
- S1-021 网络实测必须出 us-east-1 节点到 Polymarket WSS / Goalserve / Polygon 的
  实测 p50 / p99 / p99.9 数据. 实测 p99.9 > 2s 反推 WSS 阈值, 老郭重新定.
- 实测前 (Sprint-1 内), 老韩用本表的"老郭新值"实现, 配 metric 监控 DEFERRED 频率,
  Sprint-2 retro 时根据实测数据校准.

**影响半径: 一模块 (RiskManager) + 跨模块行为变化.**
**风险**: 中. 阈值偏小可能在前两周触发 false-positive DEFERRED, 但 fail-closed
是 RM 第一原则, 容忍 false-positive 不容忍漏报.

---

### 3.3 R-3 — audit fsync vs 200us 性能预算

**决议: ACCEPT 老韩 fail-closed 立场 + REJECT 老韩 "N=10 per fsync" 建议 + 提出 group commit + WAL 方案.**

**为什么 N=10 per fsync 不能直接用:**

老韩 §5.1 写 "同步 fsync 每 N 条 (N=10)". 这隐含两个问题:
- (a) N=10 在低 QPS 下 (体育市场实际 < 10 QPS) 意味着**最后一条 audit 可能丢失** —
  违反 G2 (任意拒单 audit 可查) 与 R5 (签名审计完整).
- (b) N=10 在高 QPS 突发 (cancel/replace 风暴 500 ops/s) 下, fsync 频率 50 Hz,
  每次 fsync 在 SSD 上 100us~1ms, 直接吃光老韩 §G3 的 200us 预算.

老韩自己列在 §10.3 R-3 已经识别这个矛盾, ACK.

**老郭推荐方案: Audit WAL + Group Commit + 异步 fsync, RM evaluate 同步可见 audit_id.**

### 3.3.1 设计原则

| 原则 | 做法 |
|---|---|
| evaluate 必须同步拿到 audit_id | audit_id 在 RM evaluate 时生成 (ULID), 同步写内存 buffer |
| audit 内容必须持久 | append-only WAL, group commit, **不允许丢** |
| 单 evaluate 不阻塞 fsync | fsync 走专用线程 (背景 bg core 7) |
| fail-closed | evaluate 返回前必须确认 audit 写入 WAL (内存 buffer + 准入许可), 但不必 fsync 落盘 |
| 崩溃可恢复 | 启动期 WAL replay 重建未确认 audit |

### 3.3.2 路径设计

```
RM 主线程 (core 5)                bg fsync 线程 (core 7)
─────────────────────             ─────────────────────────
evaluate(intent):
  decision = check(...)
  audit = build_record(...)
  audit_id = ulid_gen()
  audit.audit_id = audit_id
  wal_buffer.append(audit)  ──>  WAL ring buffer (SPSC)
                                  ┌──> batch read (up to 64 records OR 1ms timeout)
                                  ├──> write(fd, batch) + fdatasync()
                                  └──> mark high_watermark = audit_id
  return decision_with_audit_id  ◄── (此处不等 fsync)
```

### 3.3.3 关键不变量

1. **WAL append-only**: 一旦 `wal_buffer.append()` 返回 OK, audit 视为"逻辑持久".
2. **崩溃恢复**: 进程崩溃 → systemd 重启 → 读 WAL → 重建未确认 audit 序列 →
   对账 (RM ledger / position ledger / nonce manager) → SAFE_MODE 启动.
3. **WAL buffer 满 = fail-closed**: 如果 fsync 线程跟不上, SPSC ring 满,
   `wal_buffer.append()` 返回 FAIL → evaluate 立即返回 `REJECTED(INTERNAL_ERROR)`.
   宁可拒单不允许 audit 缺漏.
4. **签名前不必等 fsync 落盘**: 因为如果崩溃 → WAL 重放 → audit 可恢复. 与
   nonce manager 必须 fsync 才递增 (§9.4) 不矛盾, 这是两条 WAL.

### 3.3.4 性能预算

| 阶段 | 预算 |
|---|---|
| ulid_gen + build_record (内存) | < 5 us |
| SPSC append (老姜 §1 阶段 4: 0.5us p99) | < 1 us |
| **同步部分合计** | **< 6 us** |
| 异步 fsync 批量 (64 条 / 1ms) | 不在同步路径 |

老韩 §G3 200us 预算: 现在 audit 部分**只占 6us**, 富余巨大. 老韩剩余 194us 留给
规则评估 (R0-R9) 完全够用.

### 3.3.5 与老周自研 WAL (F-4) 的关系

老郭强烈建议 audit WAL **独立于** position/nonce WAL:
- 故障域隔离 (audit 文件损坏不影响交易状态恢复).
- IO pattern 不同 (audit 高频小记录, position WAL 中频; 合并会让一方背另一方的延迟).
- 安全分级不同 (audit 是合规, position 是业务, 权限可不一样).

实现可复用同一个 WAL framework, 但跑两个独立 instance, 写两个独立文件.

### 3.3.6 fsync 兜底策略

| 场景 | 策略 |
|---|---|
| 正常 | group commit batch=64 OR 1ms timer (取先到) |
| 低 QPS (< 1 QPS) | 1ms timer 兜底, 每条几乎独立 fsync, 也只 < 10 Hz |
| 高 QPS (突发 500 ops/s) | 攒满 64 即 fsync, ~8 Hz, IOPS 友好 |
| 关停 (SIGTERM) | drain SPSC + 最后一次 fsync + sync syscall |
| 崩溃 (signal handler) | 尽 best-effort 调 fsync (不阻塞过久), core dump 前 |

**派单**:
- 设计 → @老郭 本节即设计草案, 实现 owner 派给 @老韩 (代码) + @小段 (replay 协议).
- 性能压测 → @老姜 在 L2 component-bench 里加 "audit WAL throughput / fsync latency"
  两个指标.
- 故障注入 → @小宋 (qa) 加 "fsync 线程 hang / WAL 文件满 / 磁盘满" 三个混沌场景.

**反方 ("WAL 这套太复杂, 直接 fsync 每条不行吗?") 回应:**
- 直接 fsync 每条在低 QPS 没问题, 但高 QPS 突发就崩 (老韩自己 §10.3 R-3 已 ACK).
- group commit 是 1990 年代 Oracle / MySQL 就普及的成熟模式, 不是异类设计.
- 实现量级: SPSC 我们 anyway 要做 (老周 D7), group commit fsync 线程 ~200 行代码,
  比"自研 expected" 还简单.

**影响半径: 一模块 (RM audit subsystem).**
**风险**: 低. 模式成熟, 验证手段齐备.

---

### 3.4 其他发现 (老韩 RM v0.1)

#### F-6 [一行 ⟶ 一模块] §3.2 EDGE_CI_NEGATIVE 单列拒因 — 老郭推荐**单列**

老韩 Q2 待定 ("size=0 软放行" vs "EDGE_CI_NEGATIVE 单列"). 老郭决议: **单列**.

理由:
- 审计可读性 (rule_trace 里 "approved size=0" vs "rejected EDGE_CI_NEGATIVE" 差太多).
- 策略层 DEFERRED 重试逻辑应该清晰知道"是数据问题"还是"是 edge 问题", 后者重试无意义.
- 不增加任何运行时开销 (就是 enum 多一个值).

@小梁 同意请回执. 不同意请提反方理由, 老郭 再裁.

#### F-7 [一模块] §3.8 idempotency_key sqlite 持久化 — 风险点

老韩 §3.8 写 "LRU + TTL 24h, 持久化到 sqlite". sqlite 在跨洋部署 + WAL mode 下
性能良好, 但 ADR 提一个潜在坑:

- sqlite 默认 fsync 频率与 audit fsync 是**独立**的两条路径. 如果 sqlite 走
  `PRAGMA synchronous=FULL`, 每个 evaluate 多一次 fsync, 又吃延迟.
- **整改建议**: sqlite 走 `synchronous=NORMAL` + WAL mode + checkpoint 频率配
  60s. 接受崩溃丢最多 60s idempotency 状态 (即崩溃后最多 60s 内的同 key 重提
  会被放行二次), 这个窗口比 audit_id 重提的危害低: 因为下单要走链上 nonce, nonce 不会复用.

**派单**: @老韩 v0.2 §3.8 补 sqlite PRAGMA 配置 + 崩溃恢复语义.

#### F-8 [一模块] §7 KELLY_FRACTION_WARNING — 概念清楚, 但缺触发与恢复语义

老韩 §7 提到 "KELLY_FRACTION_WARNING = 0.5 × KELLY_FRACTION (WARNING 状态再缩半)".
方向对, 但 §4.2 状态转移图里没标 "进入 WARNING 时 Kelly 自动切到 WARNING 值, 回到
RUNNING 时切回原值" 这条副作用. 容易忘.

**派单**: @老韩 v0.2 §4.2 补 "状态转移副作用表", 列每个状态对 (KELLY_FRACTION /
取消未成交意向 / 是否允许 DEFERRED) 等参数的影响.

#### F-9 [一模块] §8.1 RM 单线程 vs §8.2 ledger MPSC 写入 — 边界要明示

老韩 §8.1 "RM 单线程主循环", §8.2 "fill MPSC 推入 RiskManager". 这两句合起来意味着
RM 线程同时承担 (a) 处理 evaluate 请求 + (b) 应用 fill 更新 position. 200us 预算下,
fill 风暴 (500/s) 与 evaluate 风暴可能争抢同一个线程.

**整改建议**: 老韩 §8.1 v0.2 明示:
- RM 主线程内部用**事件循环**, 输入是两个 SPSC: `intent_in` + `fill_in`.
- 优先级: `fill_in` 高于 `intent_in` (position 必须先一致再做新决策).
- fill batch apply: 每次进 evaluate 前一次性 drain `fill_in` (最多 N 条), 不交错.

**派单**: @老韩 v0.2 §8.1 + @老姜 review event loop 调度策略.

---

## 4. 通过条件 (Conditional Accept)

### 4.1 老周 必须 v0.2 落实

| # | 整改项 | 期限 |
|---|---|---|
| C-Z1 | §11 注明 "本预算假设主节点 us-east-1 同区, 跨区部署预算作废" | v0.2 |
| C-Z2 | §12 OQ-5 CLOSED, 引用老孙 v1 §2.1 决议 | v0.2 |
| C-Z3 | §8.1 与老吴 c6i.large 容量对齐 (选 F-3 a 或 b 方案, @老雷 拍) | v0.2 |
| C-Z4 | §7.3 自研 WAL 出最小设计 (与 F-4 / R-3 协调) | Sprint-1 末 |
| C-Z5 | §6.2 跨进程 SHM ring 选型 (派 @小石 补 S1-011) | Sprint-2 |
| C-Z6 | §9.3 fail-fast 增 4 条边界条件 (本 ADR §2.3) | v0.2 |
| C-Z7 | §2.4 RiskGateway 升级为 build 系统 link 阻断 + CI 扫描 + 运行时 trip-wire (本 ADR §2.2) | v0.2 + Sprint-2 落地 |

### 4.2 老韩 必须 v0.2 落实

| # | 整改项 | 期限 |
|---|---|---|
| C-H1 | §3.7 STALE 阈值改本 ADR §3.2 表 (2s/5s/10s + 10s/15s/30s) | v0.2 |
| C-H2 | §5.1 audit fsync 改本 ADR §3.3 group commit + WAL 设计 | v0.2 设计, Sprint-2 实现 |
| C-H3 | §3.2 + §3.10 EDGE_CI_NEGATIVE 单列 enum 拒因 | v0.2 |
| C-H4 | §3.8 sqlite PRAGMA 配置 + 崩溃恢复语义 | v0.2 |
| C-H5 | §4.2 状态转移副作用表 (Kelly fraction / DEFERRED) | v0.2 |
| C-H6 | §8.1 RM 事件循环明示 fill_in 优先级 | v0.2 |

### 4.3 通用要求

- 老周 v0.2 / 老韩 v0.2 必须 **同步** 在 Sprint-1 末提交, 老郭重审一遍合并发 v1.0.
- 任何整改项延期 = 这条单独标 "Conditional, 延 Sprint-2", **不允许整体 Reject 回炉**.
- 若 v0.2 不能在 Sprint-1 末完成, 当前 v0.1 在 ADR 通过条件下**可启动实现**, 但
  C-H1 (STALE 阈值) 与 C-Z7 (build link 阻断) 是**实现前置项**, 必须先落.

---

## 5. 跨文档一致性 (架构 / RM / 私钥 / 部署 / 延迟)

### 5.1 一致性检查 (已 cross-validate)

| 项 | 老周 | 老韩 | 老孙 | 老吴 | 老姜 | 老郭裁定 |
|---|---|---|---|---|---|---|
| 部署位置 | us-east colo | (隐含) us-east | us-east colo (强制) | **us-east-1 主** | us-east-1 假设 | **us-east-1, 不可改** |
| 签名延迟 p99 | < 100us (§11) | -- | 50us (本地 signer) | -- | 80us (§1 阶段 8) | **80us 收口** |
| RM 评估延迟 p99 | < 50us (§11) | < 200us (G3) | -- | -- | < 20us (§1 阶段 7) | **老韩 200us 是 evaluate 全程, 老周 50us 是规则评估部分, 兼容** |
| 30s 数据停盘 (D-06) | §1 C6 引用 | §3.7 误用为 WARNING 阈值 | -- | -- | -- | **D-06 是 HALT 上限, 不是 WARNING — RM v0.2 必改** |
| WAL 数量 | 1 个 (隐含 §7.3) | 1 个 (audit) | -- | -- | -- | **2 个独立: audit WAL + position/nonce WAL (本 ADR §3.3.5)** |

### 5.2 跨文档冲突 (本 ADR 已裁定)

1. **冲突**: 老韩 §3.7 把 D-06 的 30s 当 WARNING 阈值 → RM 实际 HALT 推到 60s, 违反 D-06.
   **裁定**: RM v0.2 改本 ADR §3.2 表, HALT ≤ 30s 是硬上限.

2. **冲突**: 老周 §11 性能预算未声明部署前提, 与老吴 S1-010 隐式耦合.
   **裁定**: 老周 v0.2 §11 加 "us-east-1 同区" 前提声明.

3. **冲突**: 老周 §8 8 个 core 分配 vs 老吴 c6i.large 实际 2 vCPU.
   **裁定**: F-3, 老雷拍板 (倾向升级到 c6i.xlarge).

4. **隐含矛盾**: 老韩 §5.1 audit fsync 与老周 §7.3 自研 WAL 是否同源.
   **裁定**: 独立两个 WAL (本 ADR §3.3.5), 故障域隔离.

### 5.3 跨文档对齐确认

- 老孙 v1 与老周 D1 关于私钥模块隔离一致 (signer 独立 UDS 进程).
- 老姜 v1 性能预算与老周 §11 数字一致 (老周引用了老姜).
- 老吴 v0.1 跨洋部署与老韩 R-2 (跨洋抖动假设) **不一致** — 已由本 ADR §3.2 裁定
  ("老韩按 us-east-1 同区假设重做 R-2 阈值").
- 小石 v1 数据结构选型 (SPSC rigtorp / MPSC moodycamel / RCU COW) 与老周 §6 一致.

---

## 6. 待会签项 (上老雷)

| # | 项 | 决议 | 待老雷确认 |
|---|---|---|---|
| W-1 | C++20 锁定 | Accept | 是否同意以 ADR 形式锁死, 不再讨论 C++23 升级 (至少 Sprint-6 前) |
| W-2 | RiskGateway link 阻断 + CI 扫描 + runtime trip-wire | Accept | 是否同意把"绕过 RM" 升级为 build / CI / runtime 三层防御 (而不只是 review) |
| W-3 | fail-fast abort + SAFE_MODE 重启 | Accept | 是否同意 "崩溃后默认 SAFE_MODE 只撤不开仓, 对账完成 + 5min OK 才解锁" |
| W-4 | STALE 阈值收紧 (WSS 10s HALT) | Accept | 是否同意 — 影响交易"暂停频率", 业务上你可能更敢冒一点 |
| W-5 | audit WAL group commit 方案 | Accept | 是否同意单独跑 audit WAL (与 position WAL 分离), 增加运维项 1 个 |
| W-6 | c6i.large vs c6i.xlarge (F-3) | 倾向 xlarge, +$100/月 | **本项老雷拍板**, 老钱过预算 |
| W-7 | EDGE_CI_NEGATIVE 单列拒因 | Accept (F-6) | 小梁 回执即可, 老雷知情 |
| W-8 | v0.2 期限 = Sprint-1 末 | -- | 是否同意 (老周老韩各加约 2 天) |

---

## 7. 决议汇总

- **D1 (C++20)**: ACCEPT
- **D2 (RiskGateway link 阻断)**: ACCEPT, 增强版 (build + CI + runtime 三层)
- **D3 (fail-fast)**: ACCEPT, 加 4 条边界条件
- **R-2 (STALE 阈值)**: REJECT 老韩 30s/60s, ACCEPT 老郭新值 (WSS 2s/10s, Goalserve 5s/15s, 对账 10s/30s)
- **R-3 (audit fsync)**: ACCEPT fail-closed 立场, REJECT N=10, ACCEPT 老郭 group commit + audit WAL 方案

老周 v0.1: **Conditional Accept** (7 项整改 C-Z1..C-Z7)
老韩 v0.1: **Conditional Accept** (6 项整改 C-H1..C-H6)

合计: **通过 0**, **条件通过 2** (老周 + 老韩 各一份, 加共计 13 项整改), **驳回 0**.

---

## 8. 老郭附言

老周老韩两份都是 Sprint-1 Wave 2 的硬通货, 不是水货. 老郭 13 项整改不是挑刺, 都是
跨文档一致性和"红线必须比建议更狠"的硬要求.

特别表扬两点:
- 老韩 §0 "红线声明" 写法 (违者停职复盘) 把合规意志钉死, 这是对的.
- 老周 §9.3 fail-fast 立场坚决, 没向 "线程级 try-recover" 妥协, 这是有经验的设计.

特别批评一点 (对老韩):
- §3.7 STALE 阈值参考了"跨洋链路"假设, 没去交叉读老吴的部署方案. **跨文档读不
  全是评审前的硬功课, Sprint-2 起所有人提交设计前必须先扫一遍同 Sprint 其他人的
  关键文档, 否则评审会重复打回**.

下次评审 (老周 v0.2 / 老韩 v0.2) 老郭直接基于本 ADR 整改清单逐条核对, 不通过的项
单独标 Conditional 延 Sprint-2, 但 W-2 / W-3 / C-H1 三项是 hard block, 不到位不准 merge.

**会签:**
- 老郭 (主审): __签__
- 老雷 (会签, 待 W-1..W-8 8 项确认): ____
