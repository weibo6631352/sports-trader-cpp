# 系统架构 v0.2 (修 ADR-001 C-Z1..C-Z7)

- Owner: 老周 (cpp-chief-architect)
- Last review: 2026-05-28
- 验收人: 老郭 (24h 内 sign-off)
- 关联:
  - `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (ADR-001)
  - `docs/ADR/2026-05-28-gm-signoff-adr-001.md` (GM W-1..W-8)
  - `docs/RESEARCH/laozhou-architecture-v0.1.md` (历史 trail, 不废)
  - 配套同改: 老韩 RM v0.2 (C-H1..C-H6) / 老吴 部署 v0.2 (c6i.xlarge) / 小郑 SLO v0.2 / 老姜 latency budget v1
- 状态: v0.2 自包含, 不强求读 v0.1

---

## 0. v0.1 → v0.2 变更摘要 + C-Z 修复对照表

### 0.1 总览 (一句话版)

- **部署位置前置声明** (us-east-1 主, Hetzner Ashburn warm standby) — 取消所有"跨洋 RTT 进预算"假设, 内环 500us 现在是**同区净 CPU**
- **RiskGateway 三层防御落地** (build link 阻断 + CI 静态扫描 + 运行时 trip-wire), 不再只靠 PR review
- **fail-fast abort 加 4 条边界** (best-effort flush / 白名单触发 / systemd 速率限制 / SAFE_MODE 重启)
- **实例规格**: c6i.large (2 vCPU) → c6i.xlarge (4 vCPU), 八核分配映射到 4 物理核 + HT (§15)
- **C++ 锁** C++20 + 自研 `Result<T, Status>` (W-1 已批)
- **新增三章**: §13 部署假设 / §14 安全模式 / §15 c6i.xlarge 核分配
- **新增一章**: §16 与 RM v0.2 的接口边界 (与老韩同步)

### 0.2 C-Z 修复对照表

| # | ADR-001 整改项 | v0.2 落地位置 | 验收口径 | 状态 |
|---|---|---|---|---|
| C-Z1 | §11 预算前置声明 us-east-1 同区, 跨区作废 | **§11.0 前置声明 + §13 部署假设** | §11 第一句必含 "本预算假设主节点部署在 us-east-1..." | 修 |
| C-Z2 | §12 OQ-5 CLOSED, 引用老孙 v1 §2.1 | **§12 OQ-5 标 CLOSED + reference** | OQ-5 状态字段 = CLOSED, 引用 `laosun-key-management-v1.md §2.1` | 修 |
| C-Z3 | §8.1 与老吴实例规格对齐 (走 GM 批 c6i.xlarge) | **§8.1 + §15 八核映射到 4 vCPU** | §8.1 核分配按 c6i.xlarge 4 物理核 + HT 重画 | 修 |
| C-Z4 | §7.3 自研 WAL 最小可证明设计 | **§7.3 WAL 设计 + §7.4 audit / position WAL 分离** | 给出 record format / fsync 策略 / crash 恢复 | 修 |
| C-Z5 | §6.2 跨进程 SHM ring 选型 (派 @小石) | **§6.2 收口 + 派单 S1-011 增节** | 起步推荐 boost::ipc + 自研 SPSC over SHM, 明示派单 | 修 |
| C-Z6 | §9.3 fail-fast 增 4 条边界条件 | **§9.3 重写 + §14 SAFE_MODE 章** | 4 条边界全部明示 | 修 |
| C-Z7 | RiskGateway 升级 build + CI + runtime 三层 | **§2.4 重写 + §16 接口边界** | 三层缺一不可, 每层明确 owner | 修 |

**全部 C-Z1..C-Z7 已落地. 残留开放问题清单见 §17.**

### 0.3 与 v0.1 的兼容性

- 5 层分层 (§2) / 数据流 (§3) / 依赖图 (§4) / 10 项技术决策 (D1-D10) 主体**不变**
- §6 通信机制 / §7 配置 主体不变, 只增强 §7.3 WAL 设计
- §8 进程模型 物理核分配重画 (因为 c6i.xlarge 只有 4 物理核, 不是 8)
- §9 失败恢复 §9.3 重写, 加 §14 SAFE_MODE 章节
- §11 性能预算数字**不变**, 但加了前置部署假设

---

## 1. 总体目标 + 约束 (与 v0.1 一致, 摘要)

### 1.1 业务目标

- 覆盖 Polymarket 体育全盘口 (Moneyline / Totals / Spreads / Period / Half / Series / Prop / Outright)
- MVP (M+6): Moneyline 单盘口实盘跑通, 72h 无崩溃, 零风控失效
- 北极星: Sharpe >= 1.5, 年化 PnL >= $5M, 在线率 >= 99.9%

### 1.2 硬约束

| # | 约束 | 来源 | 影响 |
|---|---|---|---|
| C1 | 热路径全 C++, 禁 GC 语言 | CLAUDE.md §1 | 排除 Go / Java / Python 在 critical path |
| C2 | 下单必经 RiskManager (红线) | D-02 | 架构层强制单一出口 (本文 §2.4 + §16) |
| C3 | 热路径 p99 < 500us (signal → order intent) | CLAUDE.md | 决定通信机制 / 锁选型 |
| C4 | 摄入到策略层 p99 < 20ms | A-01 | 决定 WSS 解析 + 状态发布机制 |
| C5 | 跨洋链路 (高延迟 + 带宽紧) | CLAUDE.md §1 | **v0.2 重申: 这是"团队 → 主节点"的链路, 不是"主节点 → 外部依赖"的链路** (见 §13) |
| C6 | 数据 30s 无更新自动暂停市场 | D-06 | 架构层埋 heartbeat watchdog, RM v0.2 §3.2 阈值表 |
| C7 | 回测/实盘共用特征管道 | D-04 | Feature pipeline 不允许双套实现 |
| C8 | 私钥严禁明文落盘 | D-03 | Signer 模块隔离, 老孙 v1 本地 software signer (§2.5) |

### 1.3 非目标 (v0.1 不解)

- 多链 (只跑 Polygon)
- 多账户 / 多 wallet 并发 (V2 再说)
- 实时 ML 推理

---

## 2. 5 层分层 (与 v0.1 一致, 复述 §2.4 增强)

```
+-------------------------------------------------------------+
|  L5  EXECUTION    下单/撤单/状态机/链上签名/nonce            |
+-------------------------------------------------------------+
|  L4  RISK         RiskManager (唯一下单网关, 老韩拥有内部)    |
+-------------------------------------------------------------+
|  L3  STRATEGY     定价/信号/做市/对冲, 回测共用              |
+-------------------------------------------------------------+
|  L2  DATA         摄入/解析/normalize/orderbook/特征/state    |
+-------------------------------------------------------------+
|  L1  INFRA        runtime/log/metrics/config/IPC/clock/net   |
+-------------------------------------------------------------+
```

**规则:**
1. 只允许向下依赖 (L5 → L4 → L3 → L2 → L1), 同层间允许窄接口, 严禁向上回调
2. 跨层只通过 §6 通信原语 (SPSC / MPSC / RCU 快照)
3. 每个模块必须有 owner, 跨 owner 改接口走 PR + 老郭评审

### 2.1 L1 — INFRA (基础设施)

(同 v0.1, 不重复)

### 2.2 L2 — DATA (数据层)

(同 v0.1, 不重复)

### 2.3 L3 — STRATEGY (策略层)

**输出口径**: `OrderIntent { intent_id, idempotency_key, market_id, market_type, side, price, size_usdc, edge_bps, edge_ci_low_bps, signal_ts_ns, data_freshness_ms, strategy_tag }` — 字段集与老韩 RM v0.1 §2.2 对齐, 见 §16.

### 2.4 L4 — RISK (风控层) — C-Z7 重写

**职责:** 唯一下单网关. 所有 `OrderIntent` 必经此层. 内部规则由老韩拥有 (S1-004).

#### 2.4.1 三层防御纵深 (build + CI + runtime, 缺一不可)

**第一层: build system link 阻断** (owner: 老周 + 老吴 CMake 配合)

- `risk/` 目录拆为 4 个 target:
  - `risk_gateway` (public) — 只 export 一个符号 `RiskGateway::evaluate()`, 一个类型 `RiskDecision`, 一个 enum `RejectReason`
  - `risk_limits` (private) — 限额规则实现, **不导出**, 仅 `risk_gateway` 内部 link
  - `risk_audit` (private) — audit WAL, **不导出**
  - `risk_halt` (private) — halt switch + 状态机, **不导出**
- CMake 约束:
  ```
  add_library(risk_gateway STATIC ...)
  target_link_libraries(risk_gateway PRIVATE risk_limits risk_audit risk_halt)
  # L5 只能 link gateway, 不能 link internal
  target_link_libraries(exec PRIVATE risk_gateway)
  # L3 只能见 OrderIntent, 见不到 gateway
  target_link_libraries(strategy PRIVATE order_intent_types)
  ```
- 任何 L5 模块尝试 `#include <risk/limits.hpp>` 或 link `risk_limits` 直接 **编译失败**, 不允许 waiver
- header layout: `include/stcpp/risk/gateway.hpp` (public), `src/risk/internal/*.hpp` (private, 不进 install tree)

**第二层: CI 静态扫描** (owner: 老练 testing-coach, 派单 Sprint-2 S1-024)

- CI step 1 — **link 边界扫描**: 解析 `compile_commands.json`, 凡 link `signer` / `clob` / `router` 的 binary 不允许同时 link `risk_limits` / `risk_audit` / `risk_halt`. 违反 = CI 拒绝 merge
- CI step 2 — **调用对齐扫描**: 用 clang AST matcher 扫 `exec/signer` / `exec/clob` / `exec/router` 所有 `.cpp`, 凡调用签名 API (例如 `Eip712Signer::sign()`) 的位置, 必须在**同函数调用栈**中前序看到 `RiskGateway::evaluate()` 且对其返回 `decision == APPROVED` 做分支判定. 不通过 = block-merge
- CI step 3 — **friend 检查**: `risk/*.hpp` 内**禁止** `friend` 任何非 `risk_internal_test` namespace 的符号. 检查器 grep + AST 双重保险
- **不允许 waiver**, 任何想"临时关掉"的 PR 必须老郭 + 老韩 + 老雷三签

**第三层: 运行时 trip-wire** (owner: 小郑 metrics, 派单 Sprint-2 S1-018)

- `Eip712Signer` 内部维护 `last_seen_audit_id` 计数器 + bloom filter
- 每次 `sign()` 入参必带 `audit_id`, signer 校验:
  1. `audit_id` 必须存在于 RM 最近 N=10000 条 audit ring (RM 暴露只读 RCU snapshot 给 signer, 走 §6 通信原语)
  2. `audit_id` 必须未被本 signer 见过 (防重放)
- 校验失败 → **立即 abort + core dump + alert P0** (走 §9.3 fail-fast)
- metrics 埋点:
  - `signer.audit_id_mismatch_total` (counter, 任何非零 = P0)
  - `risk.evaluate_to_sign_pair_count` (counter, evaluate 与 sign 必须 1:1, 偏差 > 1% 告警)

#### 2.4.2 反方回应

- "唯一接口符号是否过严?" — ADR-001 §2.2 已驳回, 答**还不够严**. 三层缺一不可
- "测试代码怎么注入 mock?" — `tests/` 允许通过 `risk_internal_test` namespace 的 friend test fixture, 但 production binary CMake 配置必须 exclude test target. CI step 3 检查 production link 不含 test fixture symbol

#### 2.4.3 模块表

| 模块 | 主要接口 | Owner | 备注 |
|---|---|---|---|
| `risk/gateway` (public) | `RiskGateway::evaluate(OrderIntent) -> RiskDecision` | 老韩 | 唯一对外符号, build/CI/runtime 三层守 |
| `risk/limits` (private) | 见 RM v0.2 | 老韩 | 不导出 |
| `risk/audit` (private) | 见 RM v0.2 §5 | 老韩 + 小郑 | audit WAL, group commit (RM v0.2 §3.3) |
| `risk/halt` (private) | 见 RM v0.2 §4 | 老韩 | 状态机 + halt switch |

### 2.5 L5 — EXECUTION (执行层)

(同 v0.1, 不重复; signer 改本地 software signer, 老孙 v1 §2.1)

---

## 3. 数据流图

(同 v0.1, 不重复; 唯一变化是 "跨洋链路" 注释从图顶部移走 — 因为主节点同区, 跨洋只是"团队 → 主节点", 不在数据流图)

---

## 4. 依赖图

(同 v0.1, 不重复; **强化**: §2.4.1 的 4 个 target 边界是这张依赖图的物理实现, CMake 即真理)

---

## 5. 关键技术决策

### D1. C++ 标准 = **C++20 + 自研 `Result<T, Status>`** (锁死, GM W-1 已批)

- C++20: concepts / ranges / `std::span` / `std::atomic_ref` / `<bit>` / coroutines (限非热路径)
- 不上 C++23: clang 17 对 `<expected>` / `<flat_map>` 库支持仍不完整, 跨区工具链锁版成本不抵
- **自研 `Result<T, Status>`** (约束):
  - `Status` 是 trivially copyable POD, `sizeof(Status) <= 16 byte`
  - 内嵌 `audit_id` (u64) + `error_code` enum + 1 byte severity
  - `Result<T, Status>` 在热路径强制 `[[nodiscard]]`, 编译期断言不抛异常
  - 实现 PR review: @小石 (lock-free cache 行为) + @老姜 (latency 影响)
- **不上 `tl::expected` / `boost::outcome`**: 第三方 expected 维护成本高于自研 ~150 行 + concept 约束
- **GM 锁死**: Sprint-6 前不再讨论 C++23 升级

### D2. 构建系统 = CMake + Ninja + Conan

(同 v0.1; **新增**: CMake 必须支持 §2.4.1 的 4 个 risk target 隔离)

### D3. 错误处理 = `Result<T, Status>`, 禁异常贯穿核心路径

(同 v0.1, 实现细节并入 D1)

### D4. 序列化 / 解析 = simdjson (摄入) + 自研 POD (内部)

(同 v0.1)

### D5. 并发模型 = "每物理核 1 个 reactor + SPSC pin" 而非全局线程池

(同 v0.1; **核分配 v0.2 变化**: 从 8 个 core 改成 4 物理核 + HT, 详见 §8 + §15)

### D6. 内存 = 启动期预分配 + 池化 + 无 `std::shared_ptr` 在热路径

(同 v0.1)

### D7. 事件总线 = SPSC ring + MPSC queue + RCU

(同 v0.1; **新增 v0.2 子项**: 跨进程 SHM ring, 见 §6.2)

### D8. Polygon RPC 接入 = 多 provider + 健康检查 + 故障切换

(同 v0.1, 老叶 S1-009)

### D9. 配置 = TOML + RCU 热加载, 红线参数不允许热改

(同 v0.1)

### D10. 部署 = 单进程多线程 + 旁路进程

(同 v0.1; **v0.2 变化**: 实例 c6i.large → c6i.xlarge, 主节点 us-east-1, 详 §8 + §13 + §15)

---

## 6. 通信机制

### 6.1 三种模式

(同 v0.1)

### 6.2 跨进程 SHM ring 选型 — C-Z5

**起步方案 (Sprint-1 MVP)**:
- 跨进程载体: `boost::interprocess::shared_memory_object` + `mapped_region` (成熟, 跨平台, MVP 不引第三方)
- ring 协议: 自研 SPSC over SHM, 与 in-process `infra/ipc/SpscRing` **共用 trait + protocol** (写出来一份, 两处实例化)
- 同步: `std::atomic_ref<uint64_t>` over SHM 头部 (head/tail 计数), C++20 `std::atomic_ref` 保证跨进程 atomic visibility (前提 SHM 同一 CPU socket, c6i.xlarge 单 socket 满足)
- 容量: 4096 个 record per ring, 单 record <= 256 byte (`stcpp-recorder` 录 raw event POD)

**关键风险与缓解**:
| 风险 | 缓解 |
|---|---|
| 主交易进程崩溃, 旁路进程残留 SHM 段 | systemd `ExecStartPre` 清理 `/dev/shm/stcpp-*` |
| 写者崩溃 ring 状态半 | reader 容忍 (drop on tail mismatch) + counter; 这是录制场景, 不影响交易 |
| SHM 文件权限泄露 | mode 0600 + owner=stcpp 用户; 老沈 S1-016 review |

**派单确认**:
- 详细选型 + benchmark → @小石 在 **S1-011 v0.2 第 1.1 节** 补 "跨进程 SHM ring"
- 老周 ack: 跨进程 SHM 不在热路径 (主交易 → 旁路录制是单向, 旁路慢不影响交易), 起步方案够用

### 6.3 反压策略

(同 v0.1)

---

## 7. 配置 / 状态管理

### 7.1 配置三档

(同 v0.1)

### 7.2 热加载机制

(同 v0.1)

### 7.3 自研 WAL 最小可证明设计 — C-Z4

#### 7.3.1 设计原则

| 原则 | 做法 |
|---|---|
| 一切 critical state 必先 WAL 后内存 | nonce 递增 / position 变动 / open-order 状态 必须 WAL fsync 后才视为生效 |
| append-only, 不允许就地改 | 任何状态修正用补偿 record, 不动历史 |
| 故障域隔离 | **audit WAL 与 position/nonce WAL 物理分离** (ADR-001 §3.3.5 裁定), §7.4 |
| crash 重启确定性恢复 | 启动期 replay, replay 失败 = SAFE_MODE + 人工 |
| 不上 RocksDB | 依赖太重, MVP 不需要 LSM 索引能力, K/V append + 启动 replay 够用 |

#### 7.3.2 Record format (binary, framed)

```
+--------+--------+----------------+----------------+------------+--------+
| magic  | ver    | record_type    | payload_len    | payload    | crc32c |
| 4 byte | 2 byte | 2 byte (enum)  | 4 byte (u32)   | N byte     | 4 byte |
+--------+--------+----------------+----------------+------------+--------+

magic = 0x53544350 ('STCP')
ver   = 0x0001
crc32c over (record_type || payload_len || payload), 校验整条 record
record_type enum:
  0x01 NONCE_INC          (payload: u64 new_nonce, u64 tx_intent_id)
  0x02 POSITION_DELTA     (payload: market_id, side, delta_size, delta_cost, fill_tx_id)
  0x03 OPEN_ORDER_OPEN    (payload: client_order_id, market_id, side, size, price)
  0x04 OPEN_ORDER_CLOSE   (payload: client_order_id, reason)
  0x05 CHECKPOINT         (payload: ledger_snapshot_offset, ts)
```

**单条 record 上限 1 KB** (足够 MVP, 后续可扩 length-prefix variable).

#### 7.3.3 fsync 策略

- **每条 critical record 单独 fsync** (nonce / position 必须强一致, 不走 group commit)
- 高频但非 critical 的 record (例如 OPEN_ORDER_OPEN 在批量挂 100 单时) 走 **group fsync 64 条 OR 5ms 兜底**
- group commit 策略与老韩 audit WAL (RM v0.2 §3.3) **代码框架共用**, 实例分离 (见 §7.4)

#### 7.3.4 crash 边界 (write 一半 / fsync 一半 怎么恢复)

| crash 情景 | 状态 | 恢复策略 |
|---|---|---|
| write() 已返回, 未 fsync | record 在 page cache, 可能丢 | replay 时遇 crc32c 不匹配 = 截断到上一条 valid record, 标 "tail_truncated" alert |
| write() 中途崩溃 (record 半写) | record 半条, crc 必错 | 同上, 截断 + alert |
| fsync 已完成, 进程立即崩溃 | record 持久 | replay 重放即可 |
| 磁盘满 | write() 返回 EIO | **fail-closed**: 主进程 abort (走 §9.3), systemd 重启进 SAFE_MODE; 不允许 silent skip |
| 文件损坏 (例如硬件错误) | replay 时遇 crc 错 | 截断到第一个错点; 之后 record 丢; 进 SAFE_MODE + 人工 |

#### 7.3.5 启动重放协议

```
1. open WAL file, seek to 0
2. while not EOF:
     read header (12 byte fixed)
     read payload (payload_len byte)
     read crc32c (4 byte)
     verify magic + crc32c
       OK    -> apply record to in-memory state
       FAIL  -> log "tail truncated at offset=X", stop replay, mark dirty
3. 若 dirty: 触发 SAFE_MODE
4. 若 clean: 与链上对账 (老彭 §9.2)
5. 对账 OK 5min 内 heartbeat 正常 -> 转 RUNNING (RM 主导, 见 §14)
```

#### 7.3.6 WAL 文件 rotate

- 单文件上限 256 MB, 满即 rotate
- 周期 checkpoint (`record_type=0x05`) 每 60s 写一次, 含 ledger snapshot offset
- 启动重放从最近 checkpoint 之后开始, 不必从头扫
- 历史 WAL 文件保留 7 天, 之后由老吴 systemd timer 归档到 S3 (跨洋传)

#### 7.3.7 派单

- WAL framework 实现: 老周 主笔 + @小段 (replay engineer) 联合, **Sprint-1 末出代码 + 单测**
- replay 单测: @小段 主, 至少覆盖 §7.3.4 五种 crash 场景
- 压测: @老姜 在 S1-011 加 "WAL append throughput / fsync latency" 两个指标

### 7.4 audit WAL 与 position/nonce WAL 分离

| 维度 | audit WAL (老韩 ownership) | position/nonce WAL (老周 ownership) |
|---|---|---|
| 路径 | `/var/lib/stcpp/audit/audit-*.wal` | `/var/lib/stcpp/exec/exec-*.wal` |
| fsync 策略 | group commit (RM v0.2 §3.3) | nonce / position 每条强 fsync; open-order group |
| crc 校验 | 是 | 是 |
| 故障域 | 合规, 文件损坏不影响交易状态 | 业务, 文件损坏影响资金 |
| 权限 (老沈 S1-016) | mode 0640 owner=stcpp group=audit | mode 0600 owner=stcpp |
| 代码 framework | 共用 `infra/wal::WalWriter<RecordT>` | 同左, 两个 instance |

**为什么不合并**: ADR-001 §3.3.5 已裁定故障域隔离, 老周 ack.

---

## 8. 进程模型

### 8.1 主进程: `stcpp-trader` — c6i.xlarge 4 物理核 + HT 映射 (C-Z3)

**实例规格 (GM W-6 已批 +$100/月):**
- AWS c6i.xlarge: **4 vCPU = 4 物理核 (Sapphire Rapids 已禁 SMT 的 AWS 公开规格)**, 8 GiB RAM, 单 socket
- 注: AWS c6i 系列 vCPU = 物理核 (Intel HT 禁用), 不是 c5 时代的 1 物理核 = 2 vCPU. 老吴 S1-010 v0.2 重新核对该口径

**核分配** (详见 §15):

```
vCPU 0: net-io + parse        (TLS/WSS 收发 + simdjson 解析)
vCPU 1: book + match + feature (orderbook 增量 + match state + feature pipeline)
vCPU 2: strategy + risk + exec (pricing + signal + mm + RM + signer + router)
vCPU 3: bg                     (chain-io + log fsync + metrics + config watcher + WAL fsync)
```

**为什么不是 v0.1 的 8 个核分配**:
- c6i.xlarge 只有 4 物理核, 单纯 pin 8 个线程到 4 核会被 OS 调度抢占, 抵消 pinning 收益
- 合并相邻阶段 (net-io + parse / book + match + feature / strategy + risk + exec) 减少 SPSC 跨核次数, 节省 cache miss
- bg 核独立: log fsync / metrics / WAL fsync / chain-io 全部 IO bound, 不与热路径竞争

**pinning 策略**:
- `pthread_setaffinity_np` 绑定 + `SCHED_FIFO` (优先级 50, 不抢占 kernel) 给 vCPU 0/1/2
- vCPU 3 走 `SCHED_OTHER` 默认调度
- 老吴 systemd unit 配 `CPUAffinity=0-3`, `IRQAffinity=0` 把网卡中断也 pin 到 vCPU 0
- 关闭 `cpufreq` 节能 (governor=performance), 关闭 C-state, 老吴 systemd 启动 hook

### 8.2 旁路进程

| 进程 | 职责 | 跑哪 | 失联影响 |
|---|---|---|---|
| `stcpp-recorder` | 订阅 SHM, 落原始事件 + 决策日志 | 同机, 非 pin (OS 默认调度) | 不影响主交易 |
| `stcpp-recon` | 周期对账 (链上 vs ledger) | 同机, 非 pin | 离线告警 |
| `stcpp-metrics-agent` | Prometheus exporter + alertmanager push | 同机, 非 pin | 不影响主交易 |

注: 旁路进程不占 vCPU 0-3 的 pinning, 走 cgroup 限 CPU quota (老吴 配 `CPUQuota=50%`).

### 8.3 边界外服务

- **本地 software signer** (老孙 v1, S1-005): **改 v0.1 的 KMS gRPC 方案**, 现在 signer 直接在主进程内 (`exec/signer`), 私钥 age 加密 at-rest, boot-up 解封. p99 50us, 单核 20k ops/s. 这条改动**不需要旁路进程**, 简化部署
- **Polygon 自有节点** (老叶 S1-009): 独立运维, 主交易作为 client

---

## 9. 失败恢复

### 9.1 故障域分级

(同 v0.1)

### 9.2 主进程崩溃恢复链 (重写以接 §14 SAFE_MODE)

```
systemd restart (受 §9.3 速率限制)
  ↓
load config (TOML, schema 校验, 红线参数从二进制读不读文件)
  ↓
replay audit WAL → 重建 RM 状态机 (老韩 v0.2 §4)
replay exec WAL → 重建 PositionLedger / NonceManager / open orders (§7.3.5)
  ↓
启动 RiskGateway (默认 state = SAFE_MODE, RM 主导, 见 §14)
  ↓
连 Polygon RPC → 拉链上 nonce & 持仓 → 与 ledger 对账
  ↓
连 Polymarket REST → 拉 open orders 实况 → 与 ledger 对账
  ↓
对账不一致 → 保持 SAFE_MODE, 告警, 等人工 unlock
  ↓
对账一致 → 连 WSS → 等待 first heartbeat → 5min 持续 OK → 转 RUNNING (见 §14)
```

### 9.3 fail-fast abort (整进程) — C-Z6 重写

**核心政策**: 热路径线程出非预期错误, **整进程立即 abort + core dump + alert**, 不做线程级 try-recover.

**ADR-001 §2.3 落地的 4 条边界条件**:

#### 9.3.1 边界 1: abort 前 best-effort flush (signal handler 内)

signal handler 注册在 `SIGABRT` / `SIGSEGV` / `SIGBUS` / `SIGILL`, **不阻塞过久** (< 100ms 总预算):

1. **audit log ring flush** — 老韩 audit WAL 主线程的 ring buffer 调 `fsync_now_best_effort()` (timeout 50ms)
2. **position / nonce WAL fsync** — `exec_wal_writer.flush_pending(timeout=30ms)`
3. **当前线程 core context 写入 core dump** — 默认 systemd `LimitCORE=infinity` + `kernel.core_pattern=/var/lib/stcpp/cores/%e.%p.%t`
4. **metrics last-known-state 推 1 帧** — `prom_exporter.flush_oneshot(timeout=10ms)`, 用于事后 root cause

**实现位置**: `infra/runtime/PanicHandler.cpp`, 老周 主笔, @老沈 review (signal-safety: 只调用 async-signal-safe syscall, 不分配堆)

#### 9.3.2 边界 2: abort 触发白名单 (不能"任何 errno 就 abort")

| 错误类 | 是否触发 abort | 处理路径 |
|---|---|---|
| **不变量违反** (`assert` / `STCP_INVARIANT(...)` 失败) | YES | abort + core + alert P0 |
| **内存损坏迹象** (asan trap / double free / use-after-free) | YES | abort + core |
| **锁状态不一致** (RCU epoch 异常 / SPSC head/tail 越界) | YES | abort + core |
| **`Result<T, Status>` 返回 `FATAL` severity** | YES | abort + core |
| 上游 IO 错误 (WSS 断 / TLS error / connection reset) | NO | 重连 + backoff, 走 RM heartbeat halt 路径 |
| parse 失败 (simdjson schema mismatch) | NO | drop + counter + audit, 不停整机 |
| 业务规则触发 (RM 检测到资金不一致) | NO | RM 状态机走 HALTED, 不 abort 进程 |
| 限流 (Goalserve 429) | NO | backoff + 切备用 |

**实现约束**: `STCP_INVARIANT(cond)` 宏 = `if (!(cond)) std::abort()`; 不做 `throw`, 不做 `return Status::error()`. 这是"语义级红线", 写不通过 inline review 不准 merge.

#### 9.3.3 边界 3: systemd 速率限制 (5min/3 次否则 emergency stop)

老吴 systemd unit 配置 (v0.2 必须):

```
[Service]
Restart=always
RestartSec=2s
StartLimitIntervalSec=300
StartLimitBurst=3
# 5min 内 3 次 abort 后, systemd 拒绝再重启
# 进入 emergency stop, 需 `systemctl reset-failed stcpp-trader` 才能恢复
```

**触发后行为**:
- systemd 不再拉起进程
- alertmanager 推 P0 告警: "stcpp-trader emergency stop, manual ack required"
- 老吴 SOP: 人工查 core dump → root cause → 决定是 patch + 重启, 还是进 disaster recovery
- Hetzner Ashburn standby **不会**自动接管, 因为 emergency stop = 系统已不可信, 需要人决策

#### 9.3.4 边界 4: 重启默认 SAFE_MODE (只读, 显式 unlock 才下单)

详见 §14. 简言之: 重启后 RM 状态机自动进 `SAFE_MODE` (`SAFE_MODE` 是 v0.2 新引入状态, 与 RM v0.2 §4 状态机的 `DRAIN` 类似但更严: SAFE_MODE 连撤单都需要双人 ack):

- **只读模式**: WSS 接, feature pipeline 跑, 策略层跑但**所有 OrderIntent 全部被 RM `REJECTED(SAFE_MODE)`**
- **解锁需 4 个条件全过**:
  1. exec WAL replay 干净 (无 tail truncation)
  2. 链上 nonce 对账一致
  3. Polymarket open-orders 对账一致
  4. heartbeat (WSS + Goalserve + 对账) 持续 5min 全绿
- 全过 → RM 自动转 `RUNNING`; 任何一项 fail → 保持 SAFE_MODE, 告警 + 等人工 `systemctl ... unlock` 命令

### 9.4 链上 nonce 治理

(同 v0.1; 每笔下单 nonce 递增前先 WAL fsync, 走 §7.3 exec WAL)

---

## 10. 外部边界 (协议接入点)

(同 v0.1; **重要变化**: KMS / HSM 删掉了 — 老孙 v1 §2.1 改本地 software signer, 没有 KMS 边界. 其他不变)

| 边界 | 接入点模块 | 协议 | Owner | 同区 RTT |
|---|---|---|---|---|
| Polymarket WSS | `data/ingest/poly_wss` | WSS + JSON | 老李 | < 5ms |
| Polymarket REST | `data/ingest/poly_rest`, `exec/clob` | HTTPS + JSON | 老李 | < 10ms |
| Goalserve | `data/ingest/goalserve` | HTTPS poll + XML/JSON | 小董 | < 15ms |
| Polygon RPC (读) | `data/ingest/chain` | JSON-RPC over HTTPS | 老叶 | 5-15ms |
| Polygon RPC (写) | `exec/clob` 经 router | JSON-RPC | 老李+老叶 | 5-15ms |
| Prometheus pull | `infra/metrics` | HTTP /metrics | 小郑 | LAN |

**全部外部 IO 必须经 §2.1 `infra/net` 适配层**, 不允许任何业务模块直接 syscall socket.

---

## 11. 性能预算 (拆到模块级) — C-Z1

### 11.0 前置部署假设声明 (硬性)

> **本性能预算的所有数字假设主节点部署在 AWS us-east-1, 与 Polymarket WSS origin / Polygon RPC edge / Goalserve NJ 出口同区, 测量基线 RTT < 15ms. 主节点选址变更 (例如临时部署在国内做演示, 或迁到 ap-east-1) 本预算全部作废, 必须重做并重过 ADR. 谁选址变更, 谁负责重写 §11 + 重过 ADR.**

(对应 ADR-001 F-1 整改要求 + GM Sign-off §4 红线再申明)

### 11.1 热路径 p99 < 500us (signal → CLOB write 出网卡前)

| 阶段 | 模块 | p99 预算 |
|---|---|---|
| FeatureSnapshot 读取 | RCU snapshot deref | < 1 us |
| 信号 + 定价 | strategy/pricing + signal | < 80 us |
| 做市报价生成 | strategy/mm | < 50 us |
| IntentAggregator + MPSC 入队 | strategy/portfolio | < 10 us |
| **RiskGateway::evaluate** | risk/gateway (老韩) | **< 200 us** (ADR-001 收口为 G3 200us, 含 audit WAL append) |
| Router + 选 endpoint | exec/router | < 10 us |
| EIP-712 签名 (本地 signer, 老孙 v1) | exec/signer | < 80 us (ADR-001 §5.1 收口) |
| CLOB 序列化 + TLS write | exec/clob + infra/net | < 100 us |
| **小计 (本地, 同区)** | | **~ 531 us** |
| 余量 (含 RM 已含 audit, 不重复) | | RM 200us 内自含 audit 5us; 总预算需老姜 v1 阶段表收口 |

**与老姜 v1 阶段表 (§1) 对齐**: 老姜口径是 88us p50 / 310us p99 全程, 本表是模块级上限. 实际实测以老姜口径为主, 本表为防火门 (任意一阶段超本表上限即定位 owner).

### 11.2 摄入 → 策略可用 p99 < 20ms

| 阶段 | p99 预算 | 备注 |
|---|---|---|
| Polymarket WSS push 到我机器网卡 | **不计** (跨大西洋单程 80-100ms 是物理常数, 但主节点同区后 < 5ms) | §11.0 前提 |
| TLS 解密 + WSS 分帧 | < 2 ms | |
| simdjson 解析 → POD | < 1 ms | |
| normalize + id map | < 1 ms | |
| book builder 增量 | < 3 ms | |
| feature pipeline 更新 | < 5 ms | |
| RCU snapshot 发布 | < 1 ms | |
| **小计** | **~ 13 ms** | |
| 余量 | ~ 7 ms | |

---

## 12. 开放问题 (v0.2 状态)

| # | 议题 | v0.2 状态 | 引用 |
|---|---|---|---|
| OQ-1 | C++20 vs C++23 | **CLOSED** | D1 锁 C++20, GM W-1 已批 |
| OQ-2 | lock-free 原语具体实现 | OPEN | 派 @小石 @老姜 S1-011 |
| OQ-3 | TOML vs JSON-with-comments | **CLOSED** | TOML, 已写入 §7 |
| OQ-4 | 局部 Rust 子模块 | OPEN (倾向不引入) | 老张 评估, Sprint-3 复议 |
| OQ-5 | **KMS 调用模式** | **CLOSED** | 老孙 v1 §2.1 改本地 software signer, 无 KMS 调用, 单核 20k ops/s p99 50us 同步即可 |
| OQ-6 | 跨洋失联 dead-man switch | OPEN | 老韩 + 老叶 Sprint-2 |
| OQ-7 | NUMA 策略 | **CLOSED** | c6i.xlarge 单 socket, 无 NUMA 跨域, §15 详 |
| OQ-8 | 回测引擎复用 recorder 流格式 | OPEN | 小梁 Sprint-2 |
| OQ-9 | KMS 拉签名 vs 进程内派生延迟 | **CLOSED** | 老孙 v1 已决, 进程内本地 signer |
| OQ-10 | metrics 抽样率 vs 精度 | OPEN | 小郑 S1-018 |

**剩余 OPEN: 4 项** (OQ-2 / OQ-4 / OQ-6 / OQ-8 / OQ-10 — 注: OQ-10 也 OPEN, 实际 5 项).

---

## 13. 部署假设 — 新增章节

### 13.1 主节点

- **位置**: AWS `us-east-1` (N. Virginia)
- **规格**: c6i.xlarge (4 vCPU, 8 GiB RAM, EBS gp3 200 GB)
- **同区前提**: Polymarket WSS origin / CLOB / gamma-api / Polygon RPC edge / Goalserve NJ 出口均在 us-east-1 区或同区域 < 15ms RTT
- **owner**: 老吴 S1-010 v0.2

### 13.2 Warm standby

- **位置**: Hetzner `Ashburn (ash)` 物理机 AX52 NVMe 1TB
- **模式**: active/standby (不 active/active), 不写交易
- **RTO**: < 5min (人工 + Route53 切换)
- **不自动接管**: GM 红线 — 主节点 emergency stop 后不允许程序自动 failover, 必须人工决策

### 13.3 团队接入

- 团队主力在国内
- Tailscale mesh via HK derp → us-east-1, RTT ~110ms
- 主节点**不开发**, 只跑 prod binary
- 代码开发本地, CI 在 GitHub Actions, deploy 通过 us-east-1 自托管 runner

### 13.4 部署假设不变量 (架构红线)

| # | 不变量 | 违反后果 |
|---|---|---|
| DA-1 | 主节点必须在 us-east-1 (或经 ADR 评审同区域) | §11 性能预算作废 |
| DA-2 | 主节点单进程 stcpp-trader, 不允许 active/active 双发 | nonce 冲突 / 资金双倍 |
| DA-3 | 旁路进程 (recorder/recon/metrics) 同机, 不跨网 | SHM ring 物理依赖 |
| DA-4 | Hetzner standby 不写交易, 只接 DB 复制 | 同 DA-2 |
| DA-5 | 实例规格 ≥ c6i.xlarge (4 vCPU) | §15 核分配作废 |

---

## 14. 安全模式 + 启动序列 — 新增章节

### 14.1 状态机 (架构层视角, 与老韩 RM v0.2 §4 对齐)

```
[启动]
  ↓
SAFE_MODE  ← 重启后默认 (§9.3.4) ; 只读, 所有 OrderIntent 被 REJECT
  ↓ (4 个条件全过)
RUNNING    ← 正常下单
  ↓ (stale / consec loss)
WARNING    ← 收紧 sizing, 部分 DEFERRED
  ↓ (daily loss / stale halt / consec loss N+K)
HALTED     ← 全停, 需人工 ack
  ↑ (人工 ack: 双人复核) ← DRAIN ← (人工 ack 转 close-only)
```

**SAFE_MODE 与老韩 RM v0.2 § 状态机的关系** (与老韩同步):
- 架构层 SAFE_MODE ≡ RM 状态机的 `SAFE_MODE`, 一对一映射
- SAFE_MODE 是 v0.2 新引入的状态 (老韩 v0.1 没有), RM v0.2 §4.1 必须加这个状态
- SAFE_MODE 与 DRAIN 的区别:
  - DRAIN: 只允许平仓, 撤单/平仓 OK; 用于"主动收尾"
  - SAFE_MODE: 任何下单/撤单都 REJECT; 用于"启动后/崩溃后/对账失败"的不可信状态
- SAFE_MODE 解锁需 4 条件 + 人工 unlock; DRAIN 解锁需人工 ack 转 RUNNING 或 HALTED

@老韩 确认: SAFE_MODE 业务语义请你在 RM v0.2 §4.1 明示, 我这边只定架构边界

### 14.2 启动序列 (详)

```
T+0    systemd ExecStartPre 清理 /dev/shm/stcpp-*
T+0.1  load config/runtime.toml (schema 校验)
T+0.3  分配 vCPU 0-3 affinity, set SCHED_FIFO priority
T+0.5  open exec WAL (§7.3) -> replay
T+1.0  open audit WAL (老韩 RM v0.2 §5) -> replay
T+1.5  RiskGateway 启动, state=SAFE_MODE  ←── 进入 SAFE_MODE
T+1.6  启动 metrics exporter (旁路 vCPU 3)
T+1.7  启动 net-io threads (vCPU 0): TLS 连 Polymarket WSS / Goalserve / Polygon RPC
T+2.0  启动 book/match/feature (vCPU 1)
T+2.1  启动 strategy/risk/exec (vCPU 2)
       ↓ (此时所有线程 ready, 但 RM 拒所有 intent)
T+2.5  recon 子进程拉链上 nonce + 持仓
T+3.0  recon 拉 Polymarket open-orders
T+4.0  对账完成
       ↓
       条件 A: WAL clean? ──┐
       条件 B: nonce 一致?  ├── 全 OK → 等待 heartbeat 5min
       条件 C: order 一致?  │
       条件 D: heartbeat 持续 5min OK? ──┘
       ↓
T+9.0  RM 自动转 RUNNING (从 SAFE_MODE)
       (任何条件 fail: 保持 SAFE_MODE, 告警, 等人工)
```

### 14.3 人工 unlock 命令 (老吴 SOP)

```bash
# 强制从 SAFE_MODE 转 RUNNING (双人 ack)
systemctl --user --machine=stcpp@.host exec stcpp-trader \
  -- /usr/local/bin/stcpp-admin unlock \
     --operator $USER \
     --reviewer $REVIEWER \
     --reason "对账完成, 手动确认安全"
# 命令内部要求 stdin 输入双方 1Password OTP, audit 记录双签名
```

老吴在 S1-010 v0.2 给 systemd unit 文件草稿, 老沈 S1-016 review unlock 命令安全性.

### 14.4 SAFE_MODE 期间允许的操作

| 操作 | SAFE_MODE | DRAIN | RUNNING | WARNING | HALTED |
|---|---|---|---|---|---|
| 接 WSS / 接收行情 | YES | YES | YES | YES | YES |
| feature pipeline 跑 | YES | YES | YES | YES | YES |
| 策略层生成 intent | YES | YES | YES | YES | YES |
| RM 评估 intent | YES (拒所有) | YES | YES | YES | YES |
| 下新单 | NO | NO (close-only) | YES | YES (收紧) | NO |
| 撤已有单 | NO (人工 unlock) | YES | YES | YES | YES |
| 平仓 | NO | YES | YES | YES | NO (人工 ack) |
| audit 记录 | YES | YES | YES | YES | YES |

---

## 15. c6i.xlarge 4 vCPU 八核映射 — 新增章节 (取舍 + pinning)

### 15.1 v0.1 (c6i.large, 假设 8 个逻辑核) → v0.2 (c6i.xlarge, 4 物理核)

**关键事实**: AWS c6i 系列 vCPU = 物理核 (Intel HT 禁用). 老吴 S1-010 v0.2 重新核对该口径.

**v0.1 的 8 个核分配**:
```
core 0: net-io
core 1: parse
core 2: book + match
core 3: feature
core 4: strategy
core 5: risk + exec
core 6: chain-io
core 7: bg
```

**v0.2 合并到 4 物理核**:
```
vCPU 0: net-io + parse                       (v0.1 的 core 0+1)
vCPU 1: book + match + feature               (v0.1 的 core 2+3)
vCPU 2: strategy + risk + exec               (v0.1 的 core 4+5)
vCPU 3: chain-io + bg (log/metrics/WAL/config) (v0.1 的 core 6+7)
```

### 15.2 合并的理由 (按对延迟影响排序)

| 合并 | 影响 | 评估 |
|---|---|---|
| net-io + parse | **轻微负影响** (SPSC 跨核变同核, 省 cache miss 但 net-io 偶发 epoll wakeup 抢占 parse 时间片) | 净收益: 省 ~5us SPSC pass; 净开销: parse 时偶发上下文切换 ~2us. **净省 ~3us** |
| book + match + feature | **轻微正影响** (三阶段都依赖最新 book state, 同核避免 RCU snapshot 多次发布) | 净收益: 省 2 次 RCU swap (~2us each); 净开销: feature 计算偶发占用 match state 更新窗口. **净省 ~3us** |
| strategy + risk + exec | **关键路径核** (热路径最长一段, 全在同核) | 这是 v0.2 最重要的优化点. v0.1 这一段跨 2 个核, 含至少 2 次 SPSC + 2 次 cache 迁移, ~10-20us 成本; v0.2 同核 = 直接函数调用, 省下来 |
| chain-io + bg | **无影响** (都是非热路径 IO bound) | 自然合并, 不占热路径预算 |

**预算变化**: 合并后热路径净省 ~15-20us, 缓冲 v0.1 → v0.2 的 SCHED_FIFO 调度抖动 (~5-10us p99).

### 15.3 pinning 策略

| vCPU | pin 策略 | 优先级 | 用途 |
|---|---|---|---|
| 0 | `pthread_setaffinity_np` + `SCHED_FIFO 50` + 网卡 IRQ pin | 最高 | 关键: 不能漏 WSS 包 |
| 1 | `pthread_setaffinity_np` + `SCHED_FIFO 50` | 高 | book 必须新 |
| 2 | `pthread_setaffinity_np` + `SCHED_FIFO 50` | 最高 | 热路径主体 |
| 3 | `SCHED_OTHER` 默认 | 中 | IO bound, 不抢 |

**old-school 操作 (老吴 systemd 启动 hook)**:
- 关闭 `irqbalance` (避免 IRQ 漂移), 手动 `echo 1 > /proc/irq/N/smp_affinity`
- 关闭 `cpufreq` 节能 (`cpupower frequency-set -g performance`)
- 关闭 C-state (`intel_idle.max_cstate=0` kernel cmdline)
- 关闭 transparent huge pages (`echo never > /sys/kernel/mm/transparent_hugepage/enabled`)
- 关闭 swap (`swapoff -a`)
- 锁内存 (`mlockall(MCL_CURRENT | MCL_FUTURE)`)

**实测验证派单**: @老姜 在 S1-011 v0.2 加 "4 vCPU pinning latency benchmark" 一节, 至少:
- 单核 SPSC roundtrip < 0.5us p99
- 跨核 SPSC roundtrip < 2us p99
- 全热路径 (信号 → CLOB write) < 500us p99

### 15.4 NUMA 策略 (OQ-7 关闭)

- c6i.xlarge 单 socket, 单 NUMA 域, 无跨 socket 问题
- `numactl --membind=0 --cpunodebind=0` 即可 (老吴 systemd unit 配)
- **不需要 v0.1 OQ-7 的双 socket pinning 讨论**, OQ-7 关闭

### 15.5 升级路径 (Sprint-3+ 若需要)

- 若热路径单 vCPU 跑不下 (strategy + risk + exec 合并后超 500us), 升级到 c6i.2xlarge (8 vCPU), 把 risk + exec 拆出独立 vCPU
- 若 IO 抖动严重, 升级到 c6id (本地 NVMe) 替代 EBS gp3
- 升级时机由 @老姜 在 Sprint 末 retro 给数据, @老钱 过预算

---

## 16. 与 RM v0.2 的接口边界 (老韩同步在改)

### 16.1 共同遵守的边界

| 项 | 老周 (架构) 责任 | 老韩 (RM) 责任 | 共同协议 |
|---|---|---|---|
| 唯一入口 `RiskGateway::evaluate()` | build/CI/runtime 三层防御 (§2.4.1) | 函数实现 + audit | 签名: `RiskDecision evaluate(const OrderIntent&) noexcept` |
| `OrderIntent` schema | 定义在 `include/stcpp/risk/types.hpp` (公共头) | 字段使用 + 校验 | v0.2 字段集与老韩 v0.1 §2.2 一致 |
| `RiskDecision` schema | 同上 | 同上 | v0.2 字段集与老韩 v0.1 §2.3 一致 |
| SAFE_MODE 状态 | 架构层定义 (§14) | RM 状态机实现 (RM v0.2 §4) | 名字 `SAFE_MODE` 锁死, 含义见 §14.4 表 |
| audit WAL | framework 共用 `infra/wal::WalWriter` (§7.4) | audit record schema (RM v0.2 §5) | 文件路径分离, framework 共用 |
| heartbeat watchdog | `data/heartbeat` 模块定义 (§2.2) | RM 订阅 + 阈值判断 (RM v0.2 §3.7) | STALE 阈值表与 ADR-001 §3.2 对齐 |
| trip-wire audit_id | signer 校验 (§2.4.1 第三层) | RM 生成 ULID + 发布 RCU snapshot | 校验失败 = abort (走 §9.3) |

### 16.2 老周对老韩 RM v0.2 的依赖

老韩 v0.2 必须落地以下接口, 否则架构 v0.2 无法实施:

1. **`RiskGateway::evaluate()` `noexcept`** — 老韩 v0.1 §2.1 已承诺, v0.2 保持
2. **状态机增加 `SAFE_MODE`** — v0.2 新增, 详 §14
3. **暴露只读 audit_id RCU snapshot** 给 `exec/signer` — 用于 trip-wire (§2.4.1 第三层)
4. **STALE 阈值表** 按 ADR-001 §3.2: WSS 2s/10s, Goalserve 5s/15s, 对账 10s/30s
5. **audit WAL 用 group commit + 共用 framework** — RM v0.2 §3.3 已设计, framework 老周 §7.3 提供

### 16.3 共同 owner 的模块

| 模块 | 老周 | 老韩 | 备注 |
|---|---|---|---|
| `infra/wal::WalWriter<RecordT>` | framework owner | RM 用 RecordT=AuditRecord | §7.4 |
| `data/heartbeat` | 模块定义 owner (隶属 L2) | RM 消费者 | §2.2 |
| `risk/audit` | 不入内部 | owner | §2.4.3 |

---

## 17. 残留开放问题

经 C-Z1..C-Z7 修复后, v0.2 剩余 OPEN 问题清单:

### 17.1 架构层 OPEN (老周 主)

| # | 议题 | 期限 | owner |
|---|---|---|---|
| OQ-2 | lock-free 原语具体实现 (SPSC 大小, RCU 库) | Sprint-1 末 (S1-011 v0.2) | @小石 + @老姜 |
| OQ-10 | metrics 抽样率 vs 精度 (热路径埋点不能成瓶颈) | Sprint-2 | @小郑 (S1-018) |

### 17.2 跨模块 OPEN (架构相关)

| # | 议题 | 期限 | owner |
|---|---|---|---|
| OQ-4 | 局部 Rust 子模块 (e.g. WAL / 加解密) | Sprint-3 复议 (倾向不引入) | @老张 (rust-advisor) |
| OQ-6 | 跨洋失联 dead-man switch 链上化 | Sprint-2 | @老韩 + @老叶 |
| OQ-8 | 回测引擎复用 recorder 流格式 | Sprint-2 | @小梁 (data/replay) |

### 17.3 v0.2 内已闭合 (不算 OPEN)

OQ-1 / OQ-3 / OQ-5 / OQ-7 / OQ-9 全部 CLOSED (见 §12 表).

### 17.4 v0.2 引入的新待办 (不算原 OQ, 但需 owner 跟进)

| # | 议题 | 期限 | owner |
|---|---|---|---|
| NEW-1 | c6i.xlarge 实测 4 vCPU pinning latency 是否达 §15.3 benchmark | Sprint-2 | @老姜 + @老吴 |
| NEW-2 | SAFE_MODE 业务语义最终对齐 (老韩 RM v0.2 §4 落地后老周复核) | Sprint-1 末 | @老韩 主, @老周 复核 |
| NEW-3 | 跨进程 SHM ring 选型 benchmark | Sprint-1 末 | @小石 (S1-011 v0.2) |
| NEW-4 | WAL framework 实现 + 5 种 crash 场景单测 | Sprint-1 末 | @老周 + @小段 |
| NEW-5 | RiskGateway 三层防御实际落地 (CI 静态扫描 + signer trip-wire) | Sprint-2 | @老练 + @小郑 |
| NEW-6 | systemd unit + unlock 命令 + emergency stop SOP | Sprint-1 末 | @老吴 + @老沈 |

### 17.5 验收 checklist (给老郭)

- [ ] §0.2 表所有 C-Z1..C-Z7 全部对照到 v0.2 章节
- [ ] §11.0 前置部署假设声明明示, 取消跨洋 RTT 假设 (C-Z1)
- [ ] §12 OQ-5 标 CLOSED + 引用 (C-Z2)
- [ ] §8.1 + §15 c6i.xlarge 4 vCPU 核分配清晰 (C-Z3)
- [ ] §7.3 WAL record format + fsync 策略 + crash 恢复明示 (C-Z4)
- [ ] §6.2 跨进程 SHM ring 起步方案 + 派单 @小石 (C-Z5)
- [ ] §9.3 + §14 fail-fast 4 条边界条件全部明示 (C-Z6)
- [ ] §2.4 + §16 RiskGateway 三层防御 (build + CI + runtime) (C-Z7)
- [ ] §13 部署假设 / §14 SAFE_MODE / §15 八核映射 三个新章节齐备
- [ ] §16 与 RM v0.2 接口边界明示, 与老韩同步

---

## 18. 附录

### 18.1 命名与目录约定 (与 v0.1 一致, §2.4.1 强化)

```
src/
  infra/{runtime,ipc,log,metrics,config,clock,net,serde,error,wal}
  data/{ingest,normalize,book,match,feature,heartbeat,replay}
  strategy/{pricing,signal,mm,direction,hedge,portfolio}
  risk/                 # 内部 (private)
    gateway/            # public, 只 export RiskGateway 一个符号
    internal/           # private, 不进 install tree
      limits/
      audit/
      halt/
  exec/{router,clob,signer,nonce,fill,recon}
include/stcpp/
  risk/
    gateway.hpp         # public, 唯一对外
    types.hpp           # OrderIntent / RiskDecision schema
  # 注意: include/stcpp/risk/internal/ 不存在; private header 在 src/risk/internal/
tests/{unit,integration,replay}
tools/{recorder,recon,bench,admin}
```

### 18.2 与 ticket 的对应

- S1-001: 本架构 v0.2
- S1-002 (老李 Polymarket): §2.2 + §2.5 + §10
- S1-003 (小董 Goalserve): §2.2 + §10
- S1-004 (老韩 RM): §2.4 + §16 + §14
- S1-005 (老孙 私钥): §2.5 (本地 software signer)
- S1-009 (老叶 RPC): §2.2 + §10
- S1-010 (老吴 部署): §8 + §13 + §15, v0.2 同步出
- S1-011 (老姜+小石 lock-free): §6 + §15 + NEW-3, v0.2 同步出
- S1-016 (老沈 安全): §9.3 signal handler + §14.3 unlock 命令 + §7.4 WAL 权限
- S1-018 (小郑 metrics): §2.4.1 第三层 trip-wire + NEW-5
- S1-021 (网络实测): 直接喂 §11 校准
- S1-024 (老练 testing-coach): §2.4.1 第二层 CI 静态扫描 + NEW-5

---

**评审请求 (老郭, 24h 内 sign-off):**

C-Z1..C-Z7 全部修, 一一对照 §0.2 表与 ADR-001 §4.1 整改清单. 三个新章节 (§13/§14/§15) 落地. 残留 OPEN 5 项 + NEW 待办 6 项, 都有明确 owner 和期限.

W-3 SAFE_MODE 红线 (重启默认只读, 显式 unlock) 已写入 §14 + §9.3.4.
W-2 RiskGateway link 阻断 已写入 §2.4.1 三层.
W-6 c6i.xlarge 已写入 §8 + §15, 老吴 S1-010 v0.2 配合.

会签:
- 老周 (主笔): __签__
- 老郭 (评审): ____
- 老韩 (RM 接口对齐 §14 + §16): ____
- 老吴 (部署 §13 + §15 配合): ____
