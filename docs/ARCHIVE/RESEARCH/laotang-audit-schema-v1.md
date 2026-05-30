# Audit Log Schema v1

- Owner: 老唐 (audit-expert)
- Date: 2026-05-28
- 验收人: 老韩 (RM) + 小郑 (observability) + 老黄 (compliance)
- 关联: `laohan-riskmanager-design-v0.1.md` (§5 / §6), `xiaozheng-observability-v0.1.md` (§7), ADR-001 (§3.3 audit WAL + group commit)
- 状态: DRAFT v1, 待会签 (老韩 接 RM v0.2 联签, 小郑 接 trace_id 字段 OQ-4, 老黄 接 7 年留存 + R5/R8 红线映射)
- Sprint 锚: Sprint-1 收尾 / Sprint-2 落实施

---

## 0. TL;DR (一页纸)

- **Audit ≠ Metric ≠ Log.** Audit 是 **fail-closed 同步签收** 的取证体系, 一条不丢. 小郑的 metric 允许丢, 老周的 infra/log 是 debug 流.
- **双 WAL 物理分离 (ADR-001 决议):** `audit.wal` 独立于 `position/nonce.wal`, 同框架不同 instance, 不同 fd, 不同目录, 可不同权限.
- **链式 hash + Merkle anchor.** 每条 audit 携带 `prev_hash` (BLAKE3, 32B), 每小时 Merkle root 落对象存储 + 链上 Polygon (可选, 见 §4.3), 任何篡改后不上溯都查不回去.
- **同步预算 6us, 异步 fsync batch=64 或 1ms.** 同步路径不做 fsync, 不做 hash 全量, 只做 buffer append + ULID 生成. 详见 §7.
- **保留 7 年 (老黄).** 本地热 90d + S3 IA 5y + Glacier 7y, 链下 Merkle anchor 永久.
- **Event 类型 12 个** (v1 封闭枚举, 新增必须 schema bump + ADR):
  ORDER_DECISION / ORDER_PLACED / ORDER_FILLED / ORDER_CANCELLED /
  RISK_HALT (状态转移) / SAFE_MODE_ENTER / SAFE_MODE_EXIT /
  CONFIG_RELOAD / KEY_ROTATION / APPROVAL_REQUESTED / APPROVAL_GRANTED /
  RECON_DRIFT.

---

## 1. 定位 + 边界 (与 metric / incident 区别)

### 1.1 三套体系的职能边界

| 体系 | 拥有者 | 目的 | 完整性 | 写入路径 | 故障策略 |
|---|---|---|---|---|---|
| **Audit** (本文) | 老唐 | 决策可复盘 + 财务对账 + 操作可追溯 + 合规取证 | **不可丢一条** (fail-closed) | 同步 buffer + 异步 group fsync | 写失败 → RM REJECT(INTERNAL_ERROR) |
| **Metric/Trace** | 小郑 | 监控 + 告警 + SLO | 允许丢 (采样 + ring drop) | 异步, fire-and-forget | 写失败 → counter+1, 继续 |
| **Operational Log** | 老周 (`infra/log`) | debug + post-hoc 排查 | 允许丢 (drop oldest) | ring buffer + 后台 flush | 写失败 → drop + counter |

**为什么必须三套不能合并:**

- Audit 同步阻塞决策 (写不进就拒单), metric/log 不能阻塞热路径 (老姜预算).
- Audit 字段是业务全量 (bankroll / exposure / rule_trace), 高基数; metric label 必须低基数; log 是非结构化 free-form. 合并 = 一方背另一方的缺点.
- 故障域不同: audit WAL 损坏 = 监管事件, metric 丢点 = 运维事件, log 缺段 = debug 麻烦. 隔离才能分别处置.

### 1.2 与 incident post-mortem 的关系

- **Post-mortem 是 audit 的消费者**, 不是替代者.
- 每次事故复盘 (incident-mortician 老凡 owner) 必须在 24h 内出 `incident-replay-{id}.md`, 内嵌:
  - 时间窗 audit 全量 dump (按 trace_id + evaluated_at_ns 切片)
  - rule_trace 命中分布
  - 同窗 metric / trace 截图
- Audit 是事实, post-mortem 是叙事. 二者绑定但不混淆.
- **禁忌:** post-mortem 不允许修改 audit 原始记录, 只能引用. 修改 audit = 篡改取证 = 红线.

### 1.3 监管 / 合规审计的需求 (老黄)

对齐 `laohuang-compliance-redline-v1.md`:

| 老黄红线 | Audit 覆盖项 | 字段 / event |
|---|---|---|
| R1 多账户 | 每条 audit 带 `wallet_address` (低基数 label 化), 单 wallet 校验 | ORDER_PLACED.wallet, KEY_ROTATION.from/to_wallet |
| R2 Wash trading | 同账户对手方检测需要全量 fill audit | ORDER_FILLED.counterparty_address |
| R3 Spoofing | 撤单率 = ORDER_CANCELLED / ORDER_PLACED 时间窗内可算 | ORDER_CANCELLED.reason + age_ms |
| R5 OFAC 关联 | 对手方地址留痕, 事后扫描 | ORDER_FILLED.counterparty_address |
| R6 内幕信息 | 信号来源 trace, signal_source 字段 | ORDER_DECISION.signal_source_id |
| R8 私钥落盘 | KEY_ROTATION 全留痕 + audit hash 链, 私钥本身**不入** audit | KEY_ROTATION (元数据), 私钥永远在 signer 进程 |
| R12 AI 自主提币 | 提币 = APPROVAL_REQUESTED + APPROVAL_GRANTED 双人 audit | APPROVAL_* event 类型 |

监管场景假设: Polymarket 风控部门 / CFTC inquiry / 国内税务核查 → 我们必须能在 30 天内交付指定时间窗的完整 audit dump (含链式 hash 验证证书).

---

## 2. Event Schema 字段 (核心 + 各 event 类型)

### 2.1 公共信封 (Envelope, 所有 event 必备)

```protobuf
// 伪 protobuf, 实际实现 C++ struct + 自研 serde (flatbuffers 或紧凑二进制)
message AuditEnvelope {
  // === 身份 ===
  bytes  audit_id        = 1;  // ULID 16B, 全局唯一, 排序友好
  string schema_version  = 2;  // "v1.0.0", semver
  AuditEventType type    = 3;  // enum, 见 §2.3

  // === 时间 ===
  int64  evaluated_at_ns = 10; // CLOCK_REALTIME ns, NTP-sync
  int64  monotonic_ns    = 11; // CLOCK_MONOTONIC_RAW, 用于相对时序
  int64  ingested_at_ns  = 12; // audit 进 WAL buffer 的时刻 (用于诊断写延迟)

  // === 关联 ===
  bytes  trace_id        = 20; // W3C 16B, 与小郑 OTel trace 联签 (OQ-4)
  bytes  span_id         = 21; // W3C 8B, 当前 span
  bytes  parent_audit_id = 22; // ULID 16B, 因果链 (e.g. PLACED 指向 DECISION); 0 if root
  string intent_id       = 23; // UUID v4 字符串, 策略层意图 ID (RM v0.1 §2.2)

  // === Actor ===
  Actor  actor           = 30; // 谁触发 (人 / module / agent), 见 §2.2

  // === 链式完整性 ===
  bytes  prev_hash       = 40; // BLAKE3 32B, 上一条 audit 的 hash, 启动期为 32B 0
  bytes  payload_hash    = 41; // BLAKE3 32B, 本 event 业务 payload 的 hash
  bytes  current_hash    = 42; // BLAKE3 32B, = hash(prev_hash || envelope_without_hash || payload_hash)
  uint64 sequence        = 43; // 单调递增 u64, gap 检测; 进程重启不复用 (启动期读 last+1)

  // === Payload (oneof, 按 type 决定) ===
  oneof payload {
    OrderDecisionPayload      decision   = 100;
    OrderPlacedPayload        placed     = 101;
    OrderFilledPayload        filled     = 102;
    OrderCancelledPayload     cancelled  = 103;
    RiskHaltPayload           halt       = 104;
    SafeModeEnterPayload      safe_in    = 105;
    SafeModeExitPayload       safe_out   = 106;
    ConfigReloadPayload       config     = 107;
    KeyRotationPayload        key        = 108;
    ApprovalRequestedPayload  appr_req   = 109;
    ApprovalGrantedPayload    appr_grant = 110;
    ReconDriftPayload         recon      = 111;
  }
}
```

**不变量 (必须 CI 静态校验):**

- `audit_id` 永不复用. 进程重启读 WAL last entry, 下一条递增.
- `sequence` 单调递增, 任何 gap → P0 告警 (类似老孙 nonce_gap 红线).
- `prev_hash == 0` 仅启动期 first record 允许; 之后任何 0 = 篡改嫌疑.
- `payload_hash` 和 `current_hash` 必须可重算验证 (replay 工具自动跑).

### 2.2 Actor 子结构

```protobuf
message Actor {
  ActorKind kind = 1;  // MODULE / HUMAN / AGENT / SYSTEM
  string    id   = 2;  // module name (e.g. "risk_gateway"), human handle (e.g. "laolei"), agent name
  string    host = 3;  // hostname / instance ID
  uint32    pid  = 4;  // OS pid
  string    git_sha = 5; // 本进程 git commit, 取证关键
}

enum ActorKind {
  MODULE = 0;  // 代码模块自动触发
  HUMAN  = 1;  // 人手动操作 (CLI / API)
  AGENT  = 2;  // AI agent 触发 (老黄 R12 强制留痕)
  SYSTEM = 3;  // systemd / kernel signal
}
```

### 2.3 AuditEventType 封闭枚举 (v1 = 12 个)

```protobuf
enum AuditEventType {
  AET_UNKNOWN              = 0;  // 永远不允许写入 (CI 拦)

  // ---- 决策 / 执行 (5) ----
  AET_ORDER_DECISION       = 1;  // RM evaluate 输出 (含 APPROVED / REJECTED / DEFERRED)
  AET_ORDER_PLACED         = 2;  // 上链成功 (clob submit ack)
  AET_ORDER_FILLED         = 3;  // 成交回报 (full / partial)
  AET_ORDER_CANCELLED      = 4;  // 撤单
  AET_RECON_DRIFT          = 5;  // 对账漂移 (老彭 recon)

  // ---- 状态 / 风控 (3) ----
  AET_RISK_HALT            = 10; // RM 状态转移 (RUNNING ↔ WARNING ↔ HALTED ↔ DRAIN)
  AET_SAFE_MODE_ENTER      = 11; // SAFE_MODE 进 (重启后, W-3 红线)
  AET_SAFE_MODE_EXIT       = 12; // SAFE_MODE 出 (人工 unlock)

  // ---- 运维 (4) ----
  AET_CONFIG_RELOAD        = 20; // 热 reload (RM §8.5)
  AET_KEY_ROTATION         = 21; // 私钥轮换 (老孙)
  AET_APPROVAL_REQUESTED   = 22; // 阈值审批请求 (老孙 §threshold-policy)
  AET_APPROVAL_GRANTED     = 23; // 审批通过

  // 未来扩展走 schema bump, 不允许塞 AET_OTHER
}
```

**封闭性:** 与老韩 `RejectReason` 同样规矩 — 不允许 `OTHER` 兜底, 新增 = 改 enum + bump schema_version + 双签.

### 2.4 各 event payload (核心字段)

#### 2.4.1 OrderDecisionPayload (最核心, RM v0.1 §5.2 升级版)

```protobuf
message OrderDecisionPayload {
  // 入参 (来自 RM v0.1 §2.2 OrderIntent)
  string idempotency_key   = 1;  // hex32
  string strategy_tag      = 2;
  string market_id         = 3;
  MarketType market_type   = 4;  // enum MONEYLINE/TOTAL/SPREAD/PROP/...
  OrderSide  side          = 5;  // BUY_YES / BUY_NO
  Decimal  requested_size_usdc = 6;
  Decimal  price           = 7;
  int32    edge_bps        = 8;
  int32    edge_ci_low_bps = 9;
  int32    data_freshness_ms = 10;
  string   signal_source_id = 11;  // R6 内幕信息追溯

  // 决策
  RiskDecision decision    = 20;  // APPROVED / REJECTED / DEFERRED
  Decimal      approved_size_usdc = 21;
  RejectReason reject_code = 22;  // enum, REJECTED 时必填
  string       reject_detail = 23; // 人类可读
  repeated string rule_trace = 24; // 规则命中顺序, debug + 复盘

  // 快照 (取证完整性, RM v0.1 §5.3)
  Decimal bankroll_snapshot   = 30;
  Decimal market_exposure_now = 31;
  Decimal daily_pnl_unreal    = 32;
  int32   consec_loss_count   = 33;
  RiskState state_before      = 34;
  RiskState state_after       = 35;

  // 性能
  int32 latency_us = 40;
}
```

#### 2.4.2 OrderPlacedPayload (上链)

```protobuf
message OrderPlacedPayload {
  string client_order_id    = 1;  // 派生自 audit_id (老韩 §6.2)
  string polymarket_order_id = 2; // clob ack 回的
  string wallet_address     = 3;  // R1 多账户监控
  Decimal effective_price   = 4;
  Decimal effective_size_usdc = 5;
  uint64  nonce             = 6;  // 老孙 nonce_current
  string  tx_hash           = 7;  // 上链 tx (空字符串 = clob 内部 order book, 非链上)
  int32   submit_latency_us = 8;
}
```

#### 2.4.3 OrderFilledPayload (成交)

```protobuf
message OrderFilledPayload {
  string  polymarket_order_id  = 1;
  FillKind fill_kind           = 2;  // FULL / PARTIAL
  Decimal fill_price           = 3;
  Decimal fill_size_usdc       = 4;
  Decimal remaining_size_usdc  = 5;
  string  counterparty_address = 6;  // R2 wash 检测, R5 OFAC 扫
  string  tx_hash              = 7;
  int64   chain_block_number   = 8;
  Decimal fees_usdc            = 9;
}
```

#### 2.4.4 OrderCancelledPayload

```protobuf
message OrderCancelledPayload {
  string polymarket_order_id = 1;
  CancelReason reason        = 2;  // USER / RM_HALT / EXPIRED / CLOB_REJECT / RECONNECT
  int32 age_ms               = 3;  // 挂单存续时间, R3 spoofing 监控用
}
```

#### 2.4.5 RiskHaltPayload (状态转移, RM v0.1 §5.4)

```protobuf
message RiskHaltPayload {
  RiskState from           = 1;
  RiskState to             = 2;
  HaltTrigger trigger      = 3;   // STALE / CONSEC_LOSS / DAILY_LOSS / MANUAL / DEAD_MAN
  string operator_id       = 4;   // 人工 ack 时填 (HUMAN actor)
  string second_operator_id = 5;  // 双人复核 (RM §4.3)
  string note              = 6;
}
```

#### 2.4.6 SafeModeEnter / Exit (ADR-001 W-3)

```protobuf
message SafeModeEnterPayload {
  SafeModeTrigger trigger = 1;  // PROCESS_RESTART / OPERATOR_TRIGGER / RECON_DRIFT
  string note             = 2;
}

message SafeModeExitPayload {
  string operator_id        = 1;  // 必填, 人工 unlock
  string second_operator_id = 2;  // 双人
  Decimal bankroll_snapshot_at_exit = 3;
  bool   recon_clean        = 4;  // 必须 true 才能 exit
}
```

#### 2.4.7 ConfigReloadPayload

```protobuf
message ConfigReloadPayload {
  string config_path   = 1;
  string old_sha256    = 2;
  string new_sha256    = 3;
  repeated ConfigDiff diffs = 4;  // key / old_value / new_value
  bool   accepted      = 5;       // 单调性校验通过吗
  string reject_reason = 6;       // 不通过原因 (e.g. "PER_ORDER_CAP_SOFT 调高被拒")
}
```

#### 2.4.8 KeyRotationPayload (R8 私钥不入 audit, 只留元数据)

```protobuf
message KeyRotationPayload {
  string from_wallet_address = 1;  // 旧 wallet (公开信息)
  string to_wallet_address   = 2;  // 新 wallet
  KeyRotationKind kind       = 3;  // SCHEDULED / EMERGENCY / SHAMIR_RESHARE
  string approval_audit_id   = 4;  // 引用 APPROVAL_GRANTED 的 audit_id
  // 注: 私钥 / mnemonic / shamir 分片本身永远不进 audit. 见 §9.
}
```

#### 2.4.9 ApprovalRequested / Granted (老孙阈值审批 + R12)

```protobuf
message ApprovalRequestedPayload {
  ApprovalKind kind         = 1;  // WITHDRAWAL / KEY_ROTATION / PARAM_LOOSEN / SAFE_MODE_EXIT
  string requester_id       = 2;  // AGENT 或 MODULE
  string description        = 3;
  Decimal amount_usdc       = 4;  // 提币时填
  string target_address     = 5;
  int64  expires_at_ns      = 6;
  string proposal_sha256    = 7;  // proposal 文档哈希
}

message ApprovalGrantedPayload {
  string request_audit_id   = 1;  // 指回 APPROVAL_REQUESTED
  string approver_id_a      = 2;  // 双人复核 A
  string approver_id_b      = 3;  // 双人复核 B
  bool   granted            = 4;  // true=granted, false=denied
  string note               = 5;
}
```

#### 2.4.10 ReconDriftPayload (财务对账漂移)

```protobuf
message ReconDriftPayload {
  ReconKind kind            = 1;  // LEDGER_VS_CHAIN / FILL_VS_POSITION / BANKROLL_VS_RPC
  Decimal expected_usdc     = 2;
  Decimal observed_usdc     = 3;
  Decimal drift_usdc        = 4;
  Decimal drift_ratio       = 5;
  ReconAction action_taken  = 6;  // ALERT_ONLY / RM_WARNING / RM_HALT / SAFE_MODE
}
```

---

## 3. 存储 + 保留策略

### 3.1 双 WAL 物理分离 (ADR-001 §3.3.5)

```
/var/lib/stcpp/audit/
  audit.wal.000001            # 当前 active
  audit.wal.000002            # 已封闭, 等冷储上传
  audit.wal.000001.sha256     # roll 时计算的整文件 hash
  audit.wal.000001.merkle     # 当 segment 内部 Merkle root
  index/
    audit_id.sqlite           # 主键索引 (audit_id → offset)
    by_intent.sqlite          # 二级 (intent_id → audit_id list)
    by_trace.sqlite           # 二级 (trace_id → audit_id list)
    by_event_type.sqlite      # 二级 (type, evaluated_at_ns → audit_id)

/var/lib/stcpp/position/
  position.wal.*              # 独立 WAL, 老周 §7.3 / 老孙 nonce manager
  (与 audit 完全独立的 fd / 目录 / 进程权限可不同)
```

**关键不变量:**

- audit WAL fd 仅 `audit-writer` 子进程 / 线程持有, 其他进程 `O_RDONLY` (rotator / replay / S3 uploader).
- position WAL 与 audit WAL **不同 inode**, 任何文件损坏不连坐.
- 索引 sqlite 可重建 (从 WAL 全扫), 不算"原始数据", 损坏不算事故.

### 3.2 WAL record 格式 (伪二进制)

```
+---------+----------+--------+---------------+----------+
| len u32 | crc32 u32 | type u8 | envelope bin | endmark  |
+---------+----------+--------+---------------+----------+
   4B        4B          1B       len bytes      4B "EOR\0"
```

- `crc32` 检测单条腐败 (磁盘 bit flip), 与 §4 链式 hash 互补 (一个查物理, 一个查篡改).
- `endmark` 用于 crash recovery 时判定该条是否写完 (write 一半 → 无 endmark → 丢弃 + 报警).
- 文件头 (segment 起始 4KB) 写 magic + schema_version + segment_seq + prev_segment_last_hash + 启动时刻 git_sha.

WAL 实现 @老王 (我**不**实现 WAL framework, 借老王 / 老周 §7.3 的). 我只定 record format + 字段约束.

### 3.3 Segment 切片 + 冷储

| 阶段 | 介质 | 保留 | 触发条件 |
|---|---|---|---|
| Hot (active) | 本地 SSD (audit 节点) | until roll | 当前 segment, 写满 256MB 或 跨日 0:00 UTC roll |
| Warm | 本地 SSD | 90d | 已 roll segment, 等异地上传 |
| Cold-1 | S3 IA (us-east-1, 同主交易区) | 5y | roll 后 1h 上传 |
| Cold-2 | S3 Glacier Deep Archive (跨区域复制) | **7y** | roll 后 24h 复制 |
| Anchor | Polygon L2 链上 (可选) / 公证服务 | **永久** | 每小时 Merkle root, 见 §4.3 |

**为什么 7 年:**
- 老黄 §6 KYC/AML, 大多数司法管辖区财务记录 ≥ 5 年 (美国 IRS 7 年, 中国大陆 5 年, 欧盟 GDPR 例外).
- Polymarket ToS 未明示, 但 CFTC 类比反操纵调查回溯期 6 年, 取 7 年含安全垫.
- 老黄 v1 §10 签收要求 "出事能追溯", 不留 7 年事后没法自证.

**保留期外的删除:**
- 严格按 retention policy 删除, 留删除 audit 自身 (递归: 删 audit 也要 audit).
- 链上 Merkle anchor 不删 (反正小), 留作"我们曾经有过这条数据"的存在性证明.

### 3.4 加密

- WAL 落盘**明文** (本地 SSD 已 LUKS 全盘加密, 加密交给老吴 deploy 层).
- S3 上传**强制 SSE-KMS** + 跨账户 IAM 限定: 只 audit-archiver 角色能 PUT, on-call 角色只能 GET, 谁也不能 DELETE (S3 Object Lock 启 governance mode).
- KMS key 与签名私钥 KMS key **物理隔离** (不同 region, 不同 IAM 体系), 防一次密钥泄漏全军覆没.

---

## 4. 完整性保护 (链式 hash + anchor)

### 4.1 链式 hash 计算

```
prev_hash      := 上一条 audit 的 current_hash (或启动期 32B 0)
payload_hash   := BLAKE3(payload_serialized_bytes)
envelope_canon := serialize(envelope WITHOUT current_hash)
current_hash   := BLAKE3(prev_hash || envelope_canon || payload_hash)
```

- BLAKE3 选型理由: 性能 (~1GB/s 单核), AVX2 友好, 同步预算 6us 内能算完一条小 record (~1KB).
- 选 BLAKE3 不选 SHA-256: SHA-256 单核 ~400MB/s, audit 突发 500 ops/s 会吃满, BLAKE3 富余 10×.
- 选 BLAKE3 不选 Blake2b: API 更干净, 没有 personalization 字符串, replay 工具兼容性强.

**实现细节 @老孙 (crypto-expert)** — 我定算法, 实现 review 你来.

### 4.2 链式校验 (replay 工具)

启动期 + 定时 + on-demand 都跑链验:

```
for record in wal_iter():
    assert record.crc32 == compute_crc32(record.raw)        # 物理完整性
    assert record.payload_hash == BLAKE3(record.payload)    # payload 未篡改
    expected = BLAKE3(prev_hash_var || canonical_env(record) || record.payload_hash)
    assert record.current_hash == expected                  # 链未断
    prev_hash_var = record.current_hash
```

任何 assert 失败 → 立即报警 P0 + 进程 abort + 隔离磁盘 + 通知老黄 (合规事件).

### 4.3 Merkle anchor (外部公证)

链式 hash 防"在 audit log 文件内随机改一条 + 重算后续 hash" — 但攻击者拿到磁盘可以**整段重写**. Anchor 是为了挡这种攻击.

**方案:**

- 每小时 (整点) audit-archiver 计算"上一小时 WAL records 的 Merkle root", 32B.
- Anchor 落点 (双轨, 防一处不可用):
  1. **链上 (优选):** 派一个 anchor wallet, 调用 Polygon L2 上一个 immutable contract `recordAnchor(bytes32 root, uint64 hour_epoch)`. 成本 ~$0.001 / tx, 月成本 ~$0.7.
  2. **公证服务 (兜底):** opentimestamps.org 免费, BTC 链确认, 慢但权威.
- Anchor tx_hash / OTS receipt 本身回写一条 `AET_RECON_DRIFT` 的扩展 event? 或单列 `AET_ANCHOR_PUBLISHED` — **v1 暂列开放问题 OQ-1**, 等老黄拍是否需要单列.

**为什么不每条 audit 都上链:**
- 体育市场可能突发 500 ops/s, 每条上链 → gas 月费 $10k+, 不可接受.
- 每小时 Merkle root: 攻击窗口最多 1h, 篡改一条要重算整 segment 还要骗过 anchor → 实操不可能.

**反方 (是否过度设计):**
- 我们是私人项目, 不是上市公司, 公证链上真的需要吗?
- 答: 老黄 R5 OFAC + 反操纵指控如果发生, 我们要自证"audit 没改过". 没 anchor 的话 audit 文件谁都能说是后补的, 法庭采信度低. 月成本 $0.7 的保险.

### 4.4 防篡改红线 (写入侧)

- audit-writer 进程**只 append**, 不 seek 不 truncate. fcntl `O_APPEND` 强制.
- 文件系统层 (本地): chattr +a (append-only) 启 (Linux ext4 / xfs), 改文件必须 root + 单独命令.
- S3 Object Lock (governance mode): 7 年内任何人不可删 / 不可覆盖.
- audit 线程**永远不**接收 "rewrite" / "patch" / "fix" 这类指令. 错了就 append 一条新 audit 解释, 不改老的. 这是红线.

---

## 5. 查询接口

### 5.1 索引设计

主索引 (sqlite WAL mode, synchronous=NORMAL):

| 表 | 主键 | 二级索引 | 用途 |
|---|---|---|---|
| `audit_by_id` | audit_id | — | O(1) 按 audit_id 取 |
| `audit_by_intent` | (intent_id, audit_id) | intent_id | 一个 intent 关联多个 event (DECISION → PLACED → FILLED) |
| `audit_by_trace` | (trace_id, audit_id) | trace_id | 与小郑 Tempo trace 联签查询 |
| `audit_by_type_time` | (type, evaluated_at_ns, audit_id) | (type, evaluated_at_ns) | 按事件类型 + 时间窗扫 |
| `audit_by_market` | (market_id, evaluated_at_ns) | market_id | 单市场调查 (e.g. 老黄想看某市场反操纵证据) |
| `audit_by_actor_human` | (actor.id, evaluated_at_ns) | actor.id (ActorKind=HUMAN) | 人工操作回溯 (谁 ack 了 halt) |

索引 sqlite 损坏 = 重建, 不影响 WAL. 可承受.

### 5.2 查询 API (CLI + gRPC)

CLI (运维 + 老黄合规):

```
stcpp-audit query \
  --since 2026-05-28T00:00:00Z \
  --until 2026-05-28T01:00:00Z \
  --type ORDER_DECISION,ORDER_FILLED \
  --strategy moneyline_v1 \
  --decision REJECTED \
  --format json | jq ...

stcpp-audit show <audit_id>                # 单条详细
stcpp-audit chain <audit_id>               # 显示完整因果链 (parent / children 递归)
stcpp-audit replay <intent_id>             # 重放 RM evaluate, 校对 decision 一致性 (与小宋集成)
stcpp-audit verify --segment audit.wal.000001  # 校验链式 hash + crc + Merkle root
stcpp-audit export --since ... --until ... --output bundle.tar.gz --anchor-proof
                                            # 出合规交付包 (含 Merkle proof)
```

gRPC 端口 (内网 only, on-call dashboard / 小尤 UX 调):

```protobuf
service AuditQuery {
  rpc Get(GetRequest) returns (AuditEnvelope);
  rpc List(ListRequest) returns (stream AuditEnvelope);
  rpc Chain(ChainRequest) returns (stream AuditEnvelope);
  rpc Verify(VerifyRequest) returns (VerifyResponse);
}
```

**禁忌:**
- 查询接口**只读**. 没有任何 Update/Delete RPC.
- 查询接口不暴露公网 (与 obs Grafana 一样走 bastion).
- 大窗口 (> 1d) 查询走异步任务 + S3, 不阻塞 RM 节点磁盘 IO.

### 5.3 复盘 query 库 (固化的常用问句)

| 问句 | SQL/CLI | 用途 |
|---|---|---|
| Q1: 过去 24h 拒单分布 | `SELECT reject_code, count(*) FROM decision GROUP BY 1` | 拒单率监控 (小郑 metric 已 cover, audit 用于审计核对) |
| Q2: 某市场某时间窗的所有决策 | `--market <id> --since ... --until ...` | 反操纵指控应对 |
| Q3: 某 trace_id 端到端因果链 | `stcpp-audit chain <audit_id from trace>` | incident post-mortem 主菜 |
| Q4: 私钥轮换历史 | `--type KEY_ROTATION` | 老孙年度审计 |
| Q5: 所有人工操作 | `--actor-kind HUMAN` | 操作可追溯红线 |
| Q6: 对账漂移历史 | `--type RECON_DRIFT` | 月度财务对账 (老彭) |
| Q7: 月度合规报告 | `stcpp-audit export --since <month-1> --format compliance-report` | 老黄定期巡检 |
| Q8: 双人 ack 历史 | `--type APPROVAL_GRANTED` | 提币 / SAFE_MODE_EXIT 审计 |
| Q9: 跨进程 fill vs 链上 | join audit_by_intent + 老彭 recon | 财务一致性 |
| Q10: 撤单率 (R3 spoofing 监控) | `count(CANCELLED) / count(PLACED) WINDOW 5min` | 老黄红线监控 |

这 10 个 query 落地为 `tools/audit-cookbook/` 脚本, 由小冯 (dev-ux) 包成命令.

### 5.4 报告生成

| 报告 | 频率 | 受众 | 内容 |
|---|---|---|---|
| 月度合规报告 | 月初 | 老黄 | 拒单分布, 撤单率, 多账户检查 (理论上都是 1), KYC 触发次数, 提币审批 |
| 月度财务对账 | 月初 | 老彭 / 老雷 | 决策→fill→bankroll 三方对账, 净 PnL 归因 |
| 周度运维报告 | 每周一 | 老仓 / 老吴 | RM halt 次数, SAFE_MODE 进出, config reload 失败 |
| 季度审计交付 | 季度末 | 老黄 + 外部律师 (可选) | 完整 segment + Merkle proof + anchor receipt |

---

## 6. 上下游联签接口

### 6.1 老韩 RM (audit 主要 producer)

- **入口:** RM evaluate 完成后, 同步调 `audit::emit_decision(envelope)` (§7 同步 6us 路径).
- **契约:** evaluate 返回前必须拿到 `audit_id`. RM v0.1 §5.3 "audit 写失败 → evaluate REJECT(INTERNAL_ERROR)" 老唐 ack.
- **新增字段需求 (与 RM v0.1 §5.2 diff):**
  - `trace_id` / `span_id` (新, OQ-4)
  - `signal_source_id` (新, R6 内幕信息)
  - `parent_audit_id` (新, 因果链)
  - `monotonic_ns` / `ingested_at_ns` (新, 诊断)
  - `payload_hash` / `current_hash` / `prev_hash` / `sequence` (新, 完整性)
  - `wallet_address` (新, R1)
- **派单:** 老韩 RM v0.2 §5.2 schema 升级到本文 §2.4.1, 字段同名.

### 6.2 老孙 signer (KEY_ROTATION / 签名前后 emit)

- **签名前:** RM 已 emit DECISION + audit_id 落 buffer. signer 拿 audit_id 派生 client_order_id (老韩 §6.2), **不**重复 emit (DECISION 已经 cover).
- **签名后 / 上链 ack:** signer emit `ORDER_PLACED`, parent_audit_id = DECISION.audit_id.
- **签名失败 (低频):** signer emit `ORDER_PLACED` with empty tx_hash + reason — 或单列 `AET_SIGN_FAILED`? **v1 暂不单列**, 用 ORDER_PLACED + 空 tx + reject_detail 表达, 减少 enum 膨胀. 等老孙反馈是否需要单列. **开放问题 OQ-2.**
- **KEY_ROTATION:** 私钥本体永远不入 audit, payload 只含 wallet address + 审批引用. 见 §2.4.8.
- **派单:** 老孙 v2 key-management 文档加一节"audit emission points", 与本文 §6 互相引用.

### 6.3 老李 / 老叶 协议层 (FILL / CANCEL)

- **上链 ack (clob submit success):** 老李 emit `ORDER_PLACED`. parent = DECISION.
- **fill 回报 (WSS):** 老李 emit `ORDER_FILLED`. parent = PLACED. (因果链: DECISION → PLACED → FILLED)
- **撤单:** 老李 emit `ORDER_CANCELLED`. parent = PLACED.
- **RPC error / 链上 revert:** 不直接 emit audit, 走 metric (小郑) + log (老周). audit 只记**业务事件**, 不记每个网络抖动.
- **派单:** 老李 polymarket API spec v1 加 §audit-emit-points.

### 6.4 小郑 observability (trace_id 联签 = OQ-4)

- audit 每条 `trace_id` (16B) 与小郑 OTel `trace_id` 同一个值 (W3C 格式).
- 小郑 OQ-4 已锁 `audit_id` 作为 Tempo span attribute (低基数环境安全).
- **双向跳转:** Grafana 看到异常 trace → 拿 trace_id → `stcpp-audit list --trace <id>`; audit 看到决策异常 → 拿 trace_id → Tempo deeplink.
- 集成测试 (小宋): 一个 intent 端到端跑通后, 验证 trace_id 在 Tempo 和 audit 都能拿到.

### 6.5 小宋 QA (replay 模式)

- `stcpp-audit replay <intent_id>` 模式:
  - 从 audit 取出当时的 OrderIntent + state snapshot
  - 喂给 RM (test fixture 模式, 用同时刻的 bankroll / exposure)
  - 比对决策一致性 (decision / approved_size / reject_code 必须 bit-exact)
- 用途: RM 改规则后回归 (历史 audit 是测试集), 验证向后兼容.
- 派单: 小宋集成测试加 `audit_replay_consistency` 用例.

### 6.6 老彭 recon (RECON_DRIFT 唯一 producer)

- 老彭对账 (ledger vs 链上 / fill vs position / bankroll vs RPC) 每次发现 drift > 阈值 → emit `AET_RECON_DRIFT`.
- drift 阈值: 1% (P1) / 5% (P0), 与小郑 §3 SLO 一致.
- 关键: drift 触发的状态机动作 (RM_HALT / SAFE_MODE) 由 RM 独立 emit `AET_RISK_HALT`, 不重复.

### 6.7 老雷 / 老乔 (HUMAN actor)

- CLI 工具 (manual halt / unlock / config push / approval) 必须走 `stcpp-cli` 二进制, 不允许直接改文件.
- `stcpp-cli` 调 audit emission, ActorKind = HUMAN, actor.id = OS 用户名 + 强制要求 git_sha (取证).
- 双人复核 (halt unlock / 提币 / key rotation) 走 APPROVAL_REQUESTED + APPROVAL_GRANTED 两条 event, 不允许"单人 force".

---

## 7. 性能预算

ADR-001 §3.3 已经定调, 这里复述 + 落细:

| 阶段 | 预算 | 实现 |
|---|---|---|
| ULID 生成 | < 200ns | 老姜 RDTSC + 自增 + per-thread cache |
| Envelope build (struct fill) | < 1us | stack-allocated, 无 malloc |
| Payload serialize | < 1.5us | 紧凑二进制 (自研 / flatbuffers), 启动期 schema 校验 |
| BLAKE3 payload_hash + current_hash | < 2us | BLAKE3 ~1GB/s, 单条 ~1KB → 1us per hash, 两次 = 2us |
| SPSC buffer append | < 1us | lock-free ring, RDTSC 测过 |
| Index 内存预登记 (audit_id → in-flight) | < 0.3us | 同 SPSC 路径 |
| **同步总计** | **< 6us** | ADR-001 §3.3 锁定 |

异步 fsync 线程 (bg core 7, 不在 hot path):

- group commit: 满 64 条 或 1ms timer 先到为准 (ADR-001 §3.3.6)
- fsync 一次 ~100us-1ms (SSD), 平摊到 64 条 → 单条 ~15us 异步成本
- 低 QPS (< 10 QPS) 走 1ms timer 兜底, 每秒 1000 次 fsync → IOPS 友好

**SPSC 满怎么办 (fail-closed):**

- ring 容量 16384 条 (~16MB, 一条 ~1KB)
- 16384 条 @ 500 ops/s 突发 = 33s 余量, 远大于 fsync 1ms 周期
- 满 → audit::emit 同步返回 ERR → RM evaluate REJECT(INTERNAL_ERROR), 老韩 G5 fail-closed
- 真满了说明 fsync 线程 hang, 直接 P0 告警 + 进程 abort (systemd 重启 + WAL replay)

**与老韩 G3 200us 总预算:**

- audit 占 6us
- RM 规则评估 < 50us (10 条规则, 每条 < 5us)
- 剩余 ~140us 给位置账本读 + 决策序列化 + 路由
- 富余巨大, 老姜满意

---

## 8. 灾难恢复

### 8.1 WAL 损坏分级处理

| 损坏种类 | 检测 | 处理 |
|---|---|---|
| 单条 crc 错 (磁盘 bit flip) | replay 时 crc32 mismatch | 跳过 + 报警, 后续仍可读. 该条标"corrupt", 永久留印 |
| 单条 hash 链断 (篡改嫌疑) | BLAKE3 prev/current 不匹配 | **P0**, 停 RM, 通知老黄 + 老雷, 进 SAFE_MODE, 启外部取证 |
| segment header 坏 | magic / schema 校验失败 | 拒绝读该 segment, 从 prev_segment_last_hash 重建链续 (有缺口标记) |
| 整文件丢 (磁盘故障) | 启动期 segment 索引扫描发现缺号 | **P0**, 进 SAFE_MODE, 从 S3 拉回 segment, 验 Merkle anchor |
| sqlite 索引坏 | 启动期完整性检查 (PRAGMA integrity_check) | 删 sqlite 重建 (从 WAL 全扫). 不阻塞 RM (起 SAFE_MODE 等重建完成) |

### 8.2 启动期 self-check

启动顺序 (audit 优先, 没 audit 系统不允许 RUNNING):

```
1. 打开 audit WAL 目录, fsck:
   a. 列出所有 segment 文件, 按 segment_seq 排序
   b. 验 segment 间 prev_segment_last_hash 一致
   c. 取 last segment, 读到末尾, 找 last valid record
   d. last_audit_id, last_hash, last_sequence 加载到内存
2. 验 sqlite 索引一致性 (与 WAL 抽样比对)
3. 启动 audit-writer 线程, 接收 emit
4. 启动 fsync-bg 线程
5. RM 启动期间 emit 一条 AET_SAFE_MODE_ENTER (trigger=PROCESS_RESTART, W-3)
6. 等运维显式 unlock (APPROVAL_GRANTED + AET_SAFE_MODE_EXIT) 后 RM 进 RUNNING
```

任一步失败 → 进程退出, 不允许"降级启动" (与老韩 §8.4 一致).

### 8.3 双 WAL 互相校对 (audit ↔ position)

老郭 ADR-001 §3.3.5 强调双 WAL 隔离, 但**互查**是必要的:

| 校对项 | 频率 | 异常处理 |
|---|---|---|
| audit 所有 ORDER_PLACED.client_order_id ↔ position WAL 入仓记录一一对应 | 启动期 + 每小时 | 不对应 → 老彭 recon 触发, emit RECON_DRIFT, RM 进 WARNING |
| audit ORDER_FILLED.fill_size_usdc 累计 ↔ position 当前 holdings | 启动期 + 每小时 | 不匹配 → P1, RECON_DRIFT |
| audit 最新 sequence vs position WAL 最新 sequence (粗对齐, 进程崩溃时间窗) | 启动期 | 差距 > N → 报警, 启动期手动确认 |

实现 owner: 老彭 (recon), 我提供 audit query API.

### 8.4 SAFE_MODE 进出 (W-3 红线)

- **进 SAFE_MODE:**
  - 自动: 进程重启 / WAL 完整性异常 / RECON_DRIFT > 阈值
  - 手动: CLI `stcpp-cli safe-mode enter --reason ...`
  - Emit `AET_SAFE_MODE_ENTER`, RM 状态机进 SAFE 子态 (不开仓, 不撤老单)
- **出 SAFE_MODE:**
  - 必须 APPROVAL_REQUESTED + APPROVAL_GRANTED (双人), bankroll recon clean, 无 P0/P1 未消
  - Emit `AET_SAFE_MODE_EXIT` with operator_id + second_operator_id + recon_clean=true
  - 老雷 / 老乔 任一人 + 一名 ops (老彭 / 老吴) 复核

---

## 9. 合规审计映射 (老黄红线对应)

### 9.1 R1-R12 与 audit 字段 / event 的对应表

| 红线 | Audit 提供的证据 |
|---|---|
| R1 多账户 | 所有 ORDER_PLACED.wallet_address; 月报核对单一 wallet |
| R2 Wash trading | ORDER_FILLED.counterparty_address, 自动扫描"对手方 == 自家任何 wallet" |
| R3 Spoofing/Layering | ORDER_PLACED + ORDER_CANCELLED 时间窗, 撤单率统计 (Q10) |
| R4 美国 IP/KYC | Actor.host (服务器 IP 反查地理), KEY_ROTATION wallet KYC 元数据 |
| R5 OFAC 关联 | ORDER_FILLED.counterparty_address, KEY_ROTATION.to_wallet_address, 离线扫 Chainalysis |
| R6 内幕信息 | ORDER_DECISION.signal_source_id (强制非空) |
| R7 绕过速率限制 | 不在 audit 范围 (走小郑 metric L2 rate_limit_remaining), 但单 wallet × IP × token 三元组 audit cover |
| R8 私钥落盘 | audit 中**永远不包含**私钥 / mnemonic / shamir share, CI 静态扫拒提交 |
| R9 数据转售 | 不直接 cover, 走老黄手动 review (audit 不能阻止人工导出) |
| R10 公开宣传 | 不在 audit 范围 |
| R11 跨链桥 | ORDER_PLACED.tx_hash 链上验证只 Polygon |
| R12 AI 自主提币 | APPROVAL_REQUESTED + APPROVAL_GRANTED 双 event, AGENT actor 不允许直接 emit GRANTED (CI 拦) |

### 9.2 监管交付清单 (老黄说明书)

如果发生:
- Polymarket 询问账户行为 → 用 §5.4 月度合规报告 + 指定时间窗 export
- CFTC subpoena → §5.2 `export --anchor-proof` 含 Merkle root + Polygon anchor tx 证明
- 国内税务 → 月度财务对账 + KYC audit + 提币 audit

交付物格式: `bundle.tar.gz` 含
- audit WAL segment 原文 (二进制)
- canonical JSON 转换 (人类可读)
- segment 文件 sha256 + Merkle proof 链
- anchor receipt (Polygon tx_hash + OTS receipt)
- 验证脚本 (`verify.sh`, 跑一遍证明完整性)

### 9.3 老黄一票否决兜底

老黄 v1 §9.4 紧急叫停权 → 单方面调 `stcpp-cli halt --operator laohuang --emergency`:
- 单人即可 halt (紧急例外)
- emit AET_RISK_HALT 带 second_operator_id = "EMERGENCY_BYPASS_LAOHUANG"
- 事后 24h 内必须双人复核 (走 APPROVAL flow 补单)

---

## 10. 开放问题

| # | 议题 | 决谁 |
|---|---|---|
| OQ-1 | Anchor publish 是否单列 `AET_ANCHOR_PUBLISHED` event | 老黄 + 我 |
| OQ-2 | Sign failed 是否单列 `AET_SIGN_FAILED` 或复用 ORDER_PLACED + 空 tx | 老孙 |
| OQ-3 | trace_id 在 audit 是 16B 二进制还是 32 hex char 字符串 (磁盘空间 vs 兼容性) | 小郑 + 我 |
| OQ-4 | Merkle anchor 频率 1h 是否够 (vs 15min / 24h tradeoff) | 老黄 + 老郭 (成本) |
| OQ-5 | KEY_ROTATION 的 SHAMIR_RESHARE 是否需要每个 share holder 独立 audit 子事件 | 老孙 + 老沈 |
| OQ-6 | audit-archiver 上传 S3 失败的告警阈值 (多久没上传算 P1) | 老仓 + 老吴 |
| OQ-7 | 是否对 audit 节点做独立物理机隔离 (与主交易) | 老吴 (deploy) |
| OQ-8 | replay 模式遇 schema bump 旧 audit 反序列化策略 (向后兼容 N 个版本) | 我 + 小宋 |
| OQ-9 | HUMAN actor 的 id 取 OS 用户名是否够 (vs SSO / OAuth identity) | 老沈 (安全) + 老雷 |
| OQ-10 | ORDER_DECISION 中 rule_trace 是否会泄露策略机密 (合规交付时是否脱敏) | 老黄 + 小梁 |

---

## 11. 不在 v1 范围 (留 v2+)

- **多 wallet 风控:** v1 假设单 wallet, audit 字段已留 `wallet_address` 接口
- **跨进程 audit 联签:** signer 独立进程当前用同一 WAL framework, 未来若多语言 signer 走 OTLP-like 协议
- **链上 anchor 之外的二级公证** (e.g. Arweave / IPFS): 评估成本后定
- **AI agent 决策因果追溯 (完整 prompt + response audit):** 老黄 R12 + agent operator 责任, v2 与小苏 / 老凡 联设计
- **跨年 audit retention 自动归档脚本:** v1 手动, v2 写 cron

---

## 附录 A — 与 RM v0.1 §5 schema 的 diff

| RM v0.1 字段 | v1 等价 | 变更 |
|---|---|---|
| `audit_id` (ULID) | 同 | 不变 |
| `schema_version` | 同 | 升 v1.0.0 |
| `evaluated_at_ns` | 同 | + 新增 `monotonic_ns`, `ingested_at_ns` |
| `intent_id` | 同 (envelope.intent_id) | 不变 |
| `idempotency_key` (hex32) | payload.idempotency_key | 移到 payload |
| `strategy_tag` ... `state_after` | payload.* | 整体下沉到 OrderDecisionPayload |
| `decision` / `reject_code` / `reject_detail` | 同 | 不变 |
| `rule_trace` | 同 | 不变 |
| `latency_us` | 同 | 不变 |
| (无) | `trace_id` / `span_id` | **新增** (OQ-4 联签) |
| (无) | `parent_audit_id` | **新增** (因果链) |
| (无) | `signal_source_id` | **新增** (R6) |
| (无) | `wallet_address` | **新增** (R1) |
| (无) | `prev_hash` / `payload_hash` / `current_hash` / `sequence` | **新增** (§4 链式完整性) |
| (无, §5.4 单独 STATE_TRANSITION) | `AET_RISK_HALT` event | 形式统一到 envelope + payload |

**派单:** 老韩 v0.2 §5 schema 字段以本文 §2 为准, RM v0.1 §5 视为"早期粗稿", 不冲突.

---

## 附录 B — 与小郑 observability v0.1 §7 联签项

| 小郑 §7 项 | v1 落实 |
|---|---|
| audit 落盘的 `trace_id` 字段 | §2.1 envelope 已含, 见 OQ-3 二进制 vs 字符串 |
| audit_id 当 Tempo span attribute | OK, 低基数环境安全 (小郑确认) |
| metric → trace → audit 一键跳 | §5.2 gRPC + Grafana exemplar |
| metric 反推 audit 禁止 / audit 当 metric 源禁止 | 本文 §1.1 重申 |

---

## 附录 C — 工程实现派单

| 项 | 派给谁 | 期限 |
|---|---|---|
| WAL framework (record append / fsync / segment roll) | 老王 (内核 IO 专家) / 老周 §7.3 | Sprint-2 |
| BLAKE3 链式 hash 实现 + replay 校验 | 老孙 (crypto) | Sprint-2 |
| Audit emission C++ 接口 + 同步 6us 路径 | 我 + 老姜 (perf review) | Sprint-2 |
| sqlite 索引 + gRPC query API | 我 + 小冯 (dev-ux CLI) | Sprint-3 |
| Merkle anchor 上链 | 老叶 (RPC) + 我 (设计) | Sprint-3 |
| S3 冷储 uploader | 老吴 (deploy) | Sprint-3 |
| Compliance export bundle | 我 + 老黄 (格式 review) | Sprint-4 |
| Replay 集成测试 | 小宋 (QA) | Sprint-4 |
| 月度 / 季度报告自动化 | 我 + 老彭 (recon) | Sprint-5 |

---

**END v1.**

签收:
- [ ] 老韩 (RM schema 联签 + v0.2 字段升级)
- [ ] 小郑 (trace_id 字段 + OQ-3 二进制/字符串)
- [ ] 老黄 (合规 7 年留存 + R5/R8/R12 映射)
- [ ] 老郭 (双 WAL 隔离 + group commit 已 ADR-001 决议, 此为细化)
- [ ] 老雷 (knowing)
