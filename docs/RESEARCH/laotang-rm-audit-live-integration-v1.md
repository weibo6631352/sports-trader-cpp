# RM Audit 真实拒单接入一致性审计报告

owner: 老唐 (B 风控合规部, audit-expert)
last_review: 2026-05-29

---

## 0. 审计范围与文件依据

本报告覆盖以下文件的实际代码：

- `include/stcpp/risk/rm_debug_snapshot.hpp` — lock-free ring 快照 (老沈)
- `include/stcpp/risk/risk_gateway.hpp` — OrderIntent / AuditRecord (risk 命名空间) / AuditEmitter 抽象
- `src/stcpp/risk/risk_gateway.cpp` — evaluate() 主入口 + emit_audit_ + push_reject 实现
- `include/stcpp/observability/audit_record.hpp` — WAL 落盘 AuditRecord v1.4 (observability 命名空间)
- `include/stcpp/observability/audit_emitter.hpp` — AuditEmitter 真实实现 + AuditEmitterPool
- `src/stcpp/observability/audit_emitter.cpp` — build_record / apply_hash_chain / write 实现
- `src/stcpp/debug_api/real_state_provider.hpp` — RmDebugSnapshot → RiskRejectRow 转换路径
- `src/stcpp/debug_api/endpoint_risk.cpp` — GET /api/v1/risk/rejects 串联

---

## 1. 看板 ↔ audit WAL 一致性分析

### 1.1 两路数据来源

一笔被 RM 拒绝的 OrderIntent 在系统中产生两路记录，必须同源：

**路径 A（看板）：** `evaluate()` → `reject_here()` → `build_reject_row(d, intent)` → `snap->push_reject(row)` → `RmDebugSnapshot ring` → `RealStateProvider::risk_rejects()` → `endpoint_risk.cpp` → GET /api/v1/risk/rejects JSON

**路径 B（audit WAL）：** `evaluate()` → `reject_here()` → `emit_audit_(intent, d)` → `emitter_->emit(rec)` → `stcpp::risk::AuditEmitter::emit(AuditRecord)` → WAL 落盘

**同源点：** 两路均在 `reject_here()` lambda 内部按顺序执行，使用同一个 `d`（RiskDecision）和同一个 `intent`（OrderIntent），没有任何中间状态变更。`audit_id` 在 `evaluate()` 入口一次性生成（`d.audit_id = next_audit_id(t0)`），不可能分叉。

### 1.2 字段对应关系

| 看板字段 (RejectRow / RiskRejectRow) | 来源 | WAL 字段 (risk::AuditRecord) | 来源 |
|---|---|---|---|
| `reason_code` | `reject_code_to_str(decision.reject)` | `reject` (RejectCode 枚举) | `d.reject` |
| `market_id` | `intent.condition_id` | `condition_id` | `it.condition_id` |
| `intent_ref` | `audit_id` 前 8 字节 hex | `audit_id` | `d.audit_id` |
| `side` | `intent.side` (BUY/SELL 字符串) | `side_val` (uint8) | `static_cast<uint8_t>(it.side)` |
| `size_usdc` | `intent.size_pUSD_micro / 1e6` | `(WAL AuditRecord 无独立 size 字段在 risk:: 版本)` | — |
| `price` | `intent.price` | — | — |
| `rejected_ts_ns` | `decision.decision_ts_ns` | — | — |

**关键审计点：** `stcpp::risk::AuditRecord`（risk_gateway.hpp 定义的内部结构）与 `stcpp::observability::AuditRecord`（audit_record.hpp，WAL 落盘的真实结构）**是两个独立的 struct**，通过抽象接口 `stcpp::risk::AuditEmitter::emit(AuditRecord const&)` 解耦。WAL 落盘侧的真实 AuditRecord 包含 `size_pUSD_micro` / `price` 等字段，但 risk 命名空间内的 `AuditRecord` 是一个**不同的、更简化的结构体**，其 `emit()` 调用链最终到达真实 WAL emitter 实现。

这意味着看板的 `size_usdc` / `price` / `rejected_ts_ns` 这三个字段**在当前 audit WAL 落盘路径中没有直接对应写入**，因为 `stcpp::risk::AuditRecord` 缺少这些字段（见 risk_gateway.hpp 第 265-285 行，该结构体无 size、price、rejected_ts 字段）。

### 1.3 一致性校验方法

**方法一：intent_ref 交叉比对（审计首选）**

`intent_ref` = `audit_id` 前 8 字节 hex，是同源锚。给定一个看板中的 `intent_ref`，在 WAL 中执行：

```sql
-- DuckDB 对 WAL replay 后的 audit 表
SELECT
    audit_id_hex,
    reject_code,
    condition_id,
    side_val,
    event_ts,
    data_source_ts,
    ingestion_ts,
    as_of_ts,
    decision_ts
FROM audit_records
WHERE
    substr(hex(audit_id_bytes), 1, 16) = '<intent_ref>'   -- 前 8 字节 = 16 hex chars
    AND event_type = 'ORDER_REJECTED';
```

断言：查询结果必须恰好一行，且 `reject_code` 字符串化后等于看板 `reason_code`。

**方法二：时间窗口对账**

对任意 5 分钟窗口，WAL 中 `event_type = ORDER_REJECTED` 的行数必须 ≥ 看板 ring 内同时间窗口的行数（ring 容量上限 256，WAL 无上限）。

```sql
-- 看板侧: 通过 rejected_ts_ns 过滤时间窗口
-- WAL 侧:
SELECT COUNT(*) AS wal_reject_count
FROM audit_records
WHERE event_type = 'ORDER_REJECTED'
  AND decision_ts BETWEEN :window_start_ns AND :window_end_ns;
```

差值 > 0 表示 ring 已满覆盖旧条目（正常），差值 < 0 表示 WAL 丢行（P0 事故）。

**方法三：reject_code 分布比对**

```sql
SELECT reject_code, COUNT(*) as n
FROM audit_records
WHERE event_type = 'ORDER_REJECTED'
  AND decision_ts BETWEEN :t0 AND :t1
GROUP BY reject_code
ORDER BY n DESC;
```

与同时间窗口看板 `/api/v1/risk/rejects` JSON 中的 `reason_code` 频次对比。

---

## 2. 已发现的审计风险

### F-1 (高) WAL 落盘缺失 size / price / rejected_ts_ns

`stcpp::risk::AuditRecord`（risk_gateway.hpp 第 265 行起）定义的结构体字段为：

```
audit_id / event_ts_ns / data_source_ts_ns / ingestion_ts_ns / as_of_ts_ns /
reject / sub_reason / decision / condition_id / token_id / outcome / side_val /
signal_id / timestamp_ms / metadata / builder
```

注意：**没有 `size_pUSD_micro`、没有 `price`、没有 `rejected_ts_ns`（decision_ts_ns 即 t0 但不在结构体中）。**

看板 `RejectRow` 有 `size_usdc`（来自 `intent.size_pUSD_micro / 1e6`）、`price`（来自 `intent.price`）、`rejected_ts_ns`（来自 `decision.decision_ts_ns`）。这三个字段在当前 WAL AuditRecord 中不存在。

**影响：** 事后复盘时无法从 WAL 独立验证订单名义大小和报价——这是财务对账的必要字段。

**建议：** 在 `stcpp::risk::AuditRecord` 补充 `size_pUSD_micro: int64_t`、`price: double`、`decision_ts_ns: int64_t`，并在 `emit_audit_()` 中写入。或在 `stcpp::observability::AuditRecord` v1.4 中已有 `size_pUSD_micro` 和 `price` 的情况下，验证真实的 WAL 写端是否实际写入这两个字段（需确认 risk 侧 `emit()` 接口到 observability 侧的映射路径）。

### F-2 (中) 两个 AuditRecord 命名空间存在语义漂移风险

系统存在两个独立的 `AuditRecord`：
- `stcpp::risk::AuditRecord`（risk_gateway.hpp），RM 内部使用，由 `emit_audit_()` 构造
- `stcpp::observability::AuditRecord`（audit_record.hpp），WAL 落盘真实结构

两者通过 `stcpp::risk::AuditEmitter` 虚接口解耦。但这意味着 risk 侧 `AuditEmitter::emit()` 的实现者（生产环境下应当是对接 `stcpp::observability::AuditEmitter` 的桥接类）需要做字段映射转换。**当前没有找到这个桥接类的实现**（audit_emitter.cpp 中 `stcpp::observability::AuditEmitter` 是完全独立的，接收 `RiskDecisionInput` 而非 `stcpp::risk::AuditRecord`）。

**影响：** 存在 RM emit 调用链断裂的风险——risk 侧 `emitter_->emit(rec)` 中的 `emitter_` 是 `std::shared_ptr<stcpp::risk::AuditEmitter>`，但如果没有对接真实 WAL 的实现，WAL 永远不写盘（等同于空 emitter）。

**建议：** 老沈确认进程启动时 `emitter_` 注入的是什么实现类，并在代码中显式 `static_assert` 或注释说明桥接实现在哪个文件。

### F-3 (低) 看板 ring 满时旧拒单丢失，不影响 WAL 但影响看板复盘

`RmDebugSnapshot` 容量 256 条。ring 满后 `push_reject` 覆盖最旧条目（fail-open 设计，符合 R-12）。看板只能显示最近 256 笔拒单，事后复盘须依赖 WAL，不能依赖 `/api/v1/risk/rejects`。

**建议：** 在 `endpoint_risk.cpp` 响应中增加 `"ring_capacity": 256` 和 `"total_rejects_since_start"` 字段（来自 `snap_->count()`），让消费方感知 ring 是否满过。

---

## 3. 真实接入路径确认

### 3.1 paper runtime 两路同源路径

```
Orchestrator/策略层
    │
    ▼ OrderIntent (4 ts 填好, timestamp_ms 非零)
RiskGateway::evaluate()
    │
    ├─ [step 1] t0 = now_realtime_ns()
    ├─ [step 2] d.audit_id = next_audit_id(t0)    ← 唯一锚，两路共用
    ├─ [step 3] d.decision_ts_ns = t0
    │
    ├─ 10 rule short-circuit (state→...→strategy_decayed)
    │
    └─ REJECTED 确定后 → reject_here() lambda:
         │
         ├─ [A] emit_audit_(intent, d)
         │       └─ stcpp::risk::AuditRecord 构造 (risk 命名空间)
         │           → emitter_->emit(rec)
         │               → WAL 落盘 (PaperAudit 路径)
         │                 R-11: WalKind::PaperAudit
         │                 路径: /var/lib/stcpp/paper/
         │
         └─ [B] g_rm_debug_snapshot.load(acquire)
                 → build_reject_row(d, intent) → RejectRow
                 → snap->push_reject(row)
                     → ring slot 原子写入
                         → debug_api 线程 snapshot() 读取
                             → RealStateProvider::risk_rejects()
                                 → GET /api/v1/risk/rejects
```

**R-11 合规（paper 不污染真账本）：**

build-time 宏 `STCPP_EXEC_MODE_paper` 控制 `AuditWalKindForBuild()` 返回 `WalKind::PaperAudit`，WAL 写路径为 `/var/lib/stcpp/paper/`，与 live 的 `/var/lib/stcpp/audit/` 物理隔离。`WalWriter::Open()` 中 `PathPrefixOk()` 硬校验路径前缀，不命中则 `std::abort()`，不允许运行时绕过。

**R-20 合规（4 时间戳）：**

`OrderIntent` 的 4 个时间戳（event_ts_ns / data_source_ts_ns / ingestion_ts_ns / as_of_ts_ns）由上游 Orchestrator 填入，禁止 RM 内部 `now()` 替代（代码审查：risk_gateway.cpp 中仅 `t0 = now_realtime_ns()` 用于 `decision_ts_ns`，不写回 4 ts 字段）。`check_invalid_intent_()` 中 `pit_violation_to_sub()` 在 evaluate 入口处校验 `event_ts_ns ≤ data_source_ts_ns ≤ ingestion_ts_ns ≤ as_of_ts_ns` 的单调性，违反则 INVALID_INTENT/TS_ORDER_VIOLATED 拒单。`RejectRow.rejected_ts_ns = decision.decision_ts_ns = t0`（本地决策时刻，不在 4 ts 链内，符合 R-20：decision_ts 单独存）。

**可追溯铁律合规：**

拒单 100% 经过 `reject_here()`，该路径先调 `emit_audit_`（WAL 写），再写 ring（看板）。WAL 先于 ring，保证审计记录不晚于看板记录。如果 WAL backpressure（`emit_audit_` 返回 false），`d.reject` 被改写为 `AUDIT_WAL_BACKPRESSURE`，ring 写入的也是这个 code，两路保持一致（但此时原始 reject_code 丢失，是设计上的 tradeoff，符合 fail-closed 意图）。

---

## 4. audit chain replay 对真实 reject 流的验证方案

### 4.1 单条 reject 验证（事故复盘标准流程）

**Step 1：从看板取 intent_ref**

```bash
curl http://localhost:8080/api/v1/risk/rejects | jq '.rejects[] | select(.reason_code == "EXCEED_PER_ORDER_CAP")'
# 输出: { "intent_ref": "a3f2c1b0e9d47a8c", ... }
```

**Step 2：WAL replay 中匹配 audit_id**

```sql
-- WAL replay 解包后 audit 表 (DuckDB)
SELECT
    substr(hex(audit_id_bytes), 1, 16) AS intent_ref_hex,
    hex(audit_id_bytes)               AS full_audit_id,
    reject_code,
    sub_reason,
    condition_id,
    side_val,
    size_pUSD_micro,          -- 注：当前 risk::AuditRecord 缺此字段，见 F-1
    price,                     -- 同上
    event_ts,
    data_source_ts,
    ingestion_ts,
    as_of_ts,
    decision_ts,
    prev_hash,
    current_hash
FROM audit_records
WHERE
    substr(hex(audit_id_bytes), 1, 16) = 'a3f2c1b0e9d47a8c'
    AND event_type = 'ORDER_REJECTED';
```

**Step 3：BLAKE3 hash chain 完整性验证**

```sql
-- 验证某时间段内 reject chain 无断裂
-- 使用 AuditEmitter::hash_chain_verify(start_seq, end_seq)
-- 生产侧接 WalReader replay (Sprint-3 实现后替换内存快照)

-- 临时方案：对 WAL audit_records 按 seq 顺序遍历，验证每行：
--   current_hash[n] == BLAKE3(prev_hash[n] || payload_hash[n])
--   prev_hash[n] == current_hash[n-1]

WITH ordered AS (
    SELECT
        seq,
        prev_hash,
        payload_hash,
        current_hash,
        LAG(current_hash) OVER (ORDER BY seq) AS expected_prev
    FROM audit_records
    WHERE event_type = 'ORDER_REJECTED'
      AND decision_ts BETWEEN :t0 AND :t1
    ORDER BY seq
)
SELECT
    seq,
    CASE WHEN prev_hash = expected_prev THEN 'OK' ELSE 'CHAIN_BREAK' END AS chain_status
FROM ordered
WHERE expected_prev IS NOT NULL;
```

chain_status = CHAIN_BREAK 的行为事故调查的起点。

### 4.2 批量 reject 流对账（定期自动化）

**对账 Query 1：WAL 行数 ≥ ring 行数（每5分钟窗口）**

```sql
-- WAL 侧
SELECT
    date_trunc('minute', to_timestamp(decision_ts / 1e9)) AS minute,
    COUNT(*) AS wal_count,
    reject_code,
    COUNT(DISTINCT substr(hex(audit_id_bytes), 1, 16)) AS unique_intent_refs
FROM audit_records
WHERE event_type = 'ORDER_REJECTED'
GROUP BY minute, reject_code
ORDER BY minute DESC;
```

与 `/api/v1/risk/rejects` JSON 中同时间窗口记录对比，`wal_count >= ring_visible_count`（ring 容量 256，可能比 WAL 少）。

**对账 Query 2：reject_code 分布一致性**

看板 JSON 的 `reason_code` 分布与 WAL `reject_code` 分布在重叠时间窗口内必须一致（ring 内的每一条在 WAL 中都能找到 intent_ref 对应行）。

```bash
# 提取看板 intent_ref 列表
curl -s http://localhost:8080/api/v1/risk/rejects \
  | jq -r '.rejects[].intent_ref' > /tmp/ring_intent_refs.txt

# WAL 侧验证全部存在（DuckDB）
duckdb -c "
  SELECT intent_ref, COUNT(*) as found
  FROM (SELECT substr(hex(audit_id_bytes), 1, 16) AS intent_ref FROM audit_records WHERE event_type='ORDER_REJECTED') w
  WHERE intent_ref IN ($(cat /tmp/ring_intent_refs.txt | awk '{print "\x27" $0 "\x27"}' | paste -sd,))
  GROUP BY intent_ref
  HAVING found = 0;
  -- 输出非空 = WAL 缺行，P0 事故
"
```

### 4.3 R-20 时间戳链路验证

对任意 reject WAL 记录，验证：

```sql
SELECT
    audit_id_hex,
    event_ts,
    data_source_ts,
    ingestion_ts,
    as_of_ts,
    decision_ts,
    CASE
        WHEN event_ts <= 0                   THEN 'FAIL: event_ts zero'
        WHEN data_source_ts < event_ts       THEN 'FAIL: ds < event'
        WHEN ingestion_ts < data_source_ts   THEN 'FAIL: ing < ds'
        WHEN as_of_ts < ingestion_ts         THEN 'FAIL: asof < ing'
        WHEN decision_ts < as_of_ts          THEN 'FAIL: dec < asof'
        ELSE 'OK'
    END AS pit_status
FROM audit_records
WHERE event_type = 'ORDER_REJECTED'
  AND decision_ts BETWEEN :t0 AND :t1
HAVING pit_status != 'OK';
-- 应当返回空结果集
```

---

## 5. 待确认事项（派给实施方）

| 编号 | 问题 | 派给 | 优先级 |
|---|---|---|---|
| Q-1 | `stcpp::risk::AuditEmitter::emit(AuditRecord const&)` 的生产实现类是什么，桥接到 observability::AuditEmitter 的代码在哪里 | 老沈 | P0 (F-2) |
| Q-2 | risk::AuditRecord 中缺少 `size_pUSD_micro` / `price` / `decision_ts_ns`，是设计决策还是遗漏，是否需要补充 | 老韩 + 老沈 | P1 (F-1) |
| Q-3 | `snap_->count()` 是否应该在 `/api/v1/risk/rejects` 响应中暴露，让消费方知道 ring 历史丢失了多少 | 小卢 | P2 (F-3) |
| Q-4 | `AuditWalKindForBuild()` 的 build-time 宏，CI 是否强制要求 `-DSTCPP_EXEC_MODE_paper` 或 `_live` 二选一，防止缺省 fallback 无声产生 paper 路径的 live 数据 | 老吴 (CMake) | P1 |

---

## 6. 结论

**一致性状态：** 两路在同一 `reject_here()` lambda 内同源产生，`audit_id` 是唯一锚，没有分叉点。**看板 ↔ WAL 的 reason_code / intent_ref / side 字段对称一致，已就绪。** size / price / rejected_ts_ns 这三个看板有、WAL 缺（F-1）的字段是当前最大的审计盲点，事后财务对账会受影响。

**真实接入路径：** `g_rm_debug_snapshot` 全局原子指针机制已实现，`attach_rm_debug_snapshot()` 在 startup 注入，`RealStateProvider` 已接 `const risk::RmDebugSnapshot*`，`risk_rejects()` 已从 ring 读取并转换为 `RiskRejectRow`。R-11 / R-20 / 可追溯铁律在代码路径层面合规，但 F-2（emitter 桥接）需老沈确认闭环。

**replay verify：** 4.1 节 intent_ref 交叉比对 + 4.2 节批量对账 + 4.3 节 R-20 校验查询已可直接用于事故复盘，目前受限于 Sprint-3 WAL 真写盘未完成（当前 `WalWriter::Append` 是 skeleton，写 high_watermark 但不落 fd），query 在 skeleton 阶段是对内存状态查询，Sprint-3 WAL-B01 完成后切真实文件 replay。
