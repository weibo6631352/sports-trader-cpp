# Audit Schema v1.1 (整改 C-2 R-20 + STRATEGY_DECAYED/UNLOCK)

- Owner: 老唐
- Date: 2026-05-28 UTC
- 验收人: 老郭 (6/26 ADR-003 C-2 闭环) + 老韩 (RM v0.3.1 §5) + 老孙 (v5.1 §5.4 IPC) + 老王 (WAL v0.2 header)
- 状态: DRAFT v1.1, **v1 文不动**, 本文仅 diff
- 关联: `laotang-audit-schema-v1.md` / `2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md` §6/§7.2/§10 (C-2) / `2026-05-28-gm-redline-data-source-timestamping.md` (R-20) / `laohan-riskmanager-design-v0.3.md` §16 / `laowang-wal-framework-v0.2.md` §2.1

---

## 0. v1 → v1.1 变更点

| # | 章节 | 变更 |
|---|---|---|
| Δ1 | §2.1 envelope | 4 时间戳进**老王 WAL v0.2 64B header** (framework 层, 不进 BLAKE3 payload) + payload 内 `data_source_ts_source` enum (R-20 §2.2) |
| Δ2 | §2.3 AuditEventType | 12 → **14** (新 `AET_STRATEGY_DECAYED=30`, `AET_STRATEGY_UNLOCK=31`) |
| Δ3 | §2.4 payload | 新 §2.4.11 `StrategyDecayedPayload` + §2.4.12 `StrategyUnlockPayload` (三签) |
| Δ4 | §4.1 hash chain | BLAKE3 `current_hash` canon v1.1 含 4 ts (取证不丢时间) |
| Δ5 | §6.2 老孙 signer | IPC SignRequest 4 ts 同 schema, signer emit `AET_ORDER_PLACED` 透传不重算 |
| Δ6 | §9.1 R-20 映射 | `data_source_ts_source = INFERRED_FROM_*` 进 audit 备查 + 月度 sweep (小冯) |

---

## 2.x BLAKE3 record header 加 4 ts 字段 (落老王 WAL v0.2 header)

**对齐老王 v0.2 §2.1 header v2 64B**, **不**自定义 "AUD2", 复用 MAGIC "WAL2" (framework / index sqlite 不解析 payload 即可做 PIT assert + 监管 export):

```
偏移  长度  字段
  0     4   MAGIC "WAL2"                       (老王 framework)
  4     4   LEN (u32 LE)
  8     8   SEQ (u64 LE, framework 单调)
 16     8   event_ts        (i64 epoch_ns UTC, R-20)        ← Δ1
 24     8   data_source_ts  (i64 epoch_ns UTC, R-20)        ← Δ1
 32     8   ingestion_ts    (i64 CLOCK_MONOTONIC_RAW, R-12) ← Δ1
 40     8   as_of_ts        (i64 epoch_ns UTC, R-20)        ← Δ1
 48    16   audit_id (ULID, caller 填)
 64     N   PAYLOAD (envelope-without-4-ts + §2.4 payload + BLAKE3 三 hash)
64+N    4   CRC32C (over MAGIC..PAYLOAD, 老王算)
```

**双轨保留 (OQ-12 答):** 4 ts 进 header 给 framework/PIT; payload 内 v1 `evaluated_at_ns`/`ingested_at_ns` 保留**仅作 BLAKE3 hash 完整性双校验**, 启动期 replay 验 `header.ingestion_ts == payload.ingested_at_ns`, 不一致 = P0 (T-04 篡改).

**R-20 §2.3 不等式 (framework + caller 双校验):**
`event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ≤ now_utc_ns()`

**R-20 §2.2 enum 进 payload (老唐 owner):**
```
message DataSourceTimestamp {
  enum Source { UPSTREAM_PAYLOAD=0; UPSTREAM_HEADER=1; INFERRED_FROM_DS_TS=2; INFERRED_FROM_INGESTION=3; }
  Source data_source_ts_source = 1;  // 月度 sweep (小冯)
  Source event_ts_source       = 2;
}
```

WAL header 实现 @老王, 我只定 4 ts 语义 + canon 计算 (老王 v0.2 §2.1 已含, 无 framework 改动).

---

## 2.y 2 新 AET event type (12 → 14)

```
enum AuditEventType {
  // ... v1 12 个不变 ...
  AET_STRATEGY_DECAYED = 30;  // Bayesian BLACK (P(μ<0|data)>0.3 持续 14 天, 老韩 §16.1)
  AET_STRATEGY_UNLOCK  = 31;  // 三签解锁 (老钱 CPO + 老雷 GM + 小梁 financial, 老韩 §16.4)
}

message StrategyDecayedPayload {
  string  strategy_tag        = 1;
  double  p_mu_negative       = 2;  // 触发时 P(μ<0|data)
  int32   sustained_days      = 3;  // >=14
  double  threshold           = 4;  // BAYES_DECAY_P_THRESHOLD (默认 0.3)
  string  monitor_snapshot_sha256 = 5;  // bayes_decay_monitor.py 输出 hash (@小董)
  RiskState state_before = 6;
  RiskState state_after  = 7;  // = BLACK
  string  pagerduty_incident_id = 8;  // 小尤 §16.6
}

message StrategyUnlockPayload {
  string  strategy_tag       = 1;
  string  decay_audit_id     = 2;  // 指回 AET_STRATEGY_DECAYED (因果链)
  string  adr_document_sha256 = 3;
  string  approver_cpo       = 4;  // 老钱 必填 ActorKind=HUMAN
  string  approver_gm        = 5;  // 老雷
  string  approver_financial = 6;  // 小梁
  int64   monitoring_days    = 7;  // 默认 7
  double  kelly_multiplier   = 8;  // MONITORING 期 0.5x
  RiskState state_after      = 9;  // = MONITORING
}
```

**封闭性硬约束 (CI 拦):** UNLOCK 写入校验 3 approver 全非空 + ActorKind=HUMAN + decay_audit_id 配对 BLACK; AGENT 不允许 emit UNLOCK (与 R12 提币同); paper 同样走 (R-11 不豁免 kill switch).

---

## 与老孙 v5.1 IPC schema 4 ts 字段一致

| 链路 | 4 ts 落点 | Owner |
|---|---|---|
| L0 ingest (Goalserve/Polymarket WSS) | data_source_ts 上游 payload, ingestion_ts = MONOTONIC_RAW < 50us | 小余 |
| L1 feature | feature_compute_ts + feature_snapshot_id (含 4 ts) | 小邓 |
| L2 RM evaluate | as_of_ts = evaluate 入口; audit envelope **复制全 4 ts** 进 header | 老韩 v0.3.1 |
| L3 IPC SignRequest (UDS) | SignRequest 头 4 ts **同 schema** + sign_request_ts/sign_complete_ts (老孙 v5.1 §5.4) | 老孙 |
| L4 ORDER_PLACED audit | signer **透传 L3 的 4 ts**, 不重算 (取证: 决策 vs 签名时刻可区分) | 老孙+老唐 |

**反向校验:** 月度对账跨 audit (header 4 ts) + signer local WAL (sign_*_ts) 合并, 任一 ts 不等式破 = `AET_RECON_DRIFT` (kind=TIMESTAMP_DRIFT, @老彭).

**不耻下问已落:**
- WAL header @老王: 已读 v0.2 §2.1, MAGIC "WAL2" 不另立 "AUD2"; 4 ts offset 16/24/32/40 复用; framework 仅 CRC32C 不算 BLAKE3.
- STRATEGY_DECAYED 触发 @小董: 阈值 `BAYES_DECAY_P_THRESHOLD=0.3` + `BAYES_DECAY_SUSTAINED_DAYS=14`; monitor 输出 `bayes_decay_monitor.py` hash 进 payload, 月度 sweep 小董+小梁联签; 误报缓解走解锁 7 天 MONITORING + KELLY 0.5x.

---

**END v1.1.** 签收: 老郭 (6/26 C-2) / 老韩 (v0.3.1 §5) / 老孙 (v5.1 §5.4) / 老王 (header) / 小董 (Decayed payload) / 老雷 (GM-1 三签).
