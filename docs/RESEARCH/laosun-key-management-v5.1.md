# Signer v5.1 (整改 C-2 R-20 4 ts IPC)

- Owner: 老孙 (crypto-signing-expert)
- Date: 2026-05-28 UTC
- Status: Active (v5 保留, v5.1 仅追加 §5.4 + 与 v0.3.1 sub_reason 对齐)
- 验收人: 老沈 (security) + 老郭 (architecture-second-opinion), 6/26
- 关联:
  - v5 主体: `docs/RESEARCH/laosun-key-management-v5-simplified.md` (§1~§7 不动)
  - 整改源: `docs/ADR/2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md` §10 C-2
  - R-20 红线: `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md` §2/§7/§9
  - 老韩 v0.3.1 (待出): `INVALID_INTENT.sub_reason` 枚举 — 字段化 (保 21 reject enum 总数)
  - 老唐 audit v1.1 (待出): BLAKE3 envelope 加 event_ts + data_source_ts + source enum

---

## 0. v5 → v5.1 变更摘要

| 维度 | v5 | v5.1 | 影响 |
|---|---|---|---|
| **IPC SignRequest 4 ts 字段** | 未声明 | **新增 4 ts + source enum (§5.4)** | 与 R-20 §7 PIT 4 assert + 老周 §21 enforcement 一一对应 |
| **signer 端 PIT assert** | 隐含 (B5 typed-data) | **显式 4 不等式 + now+60s 上界 (§5.4)** | 拒绝穿越未来 → REJECT(INVALID_INTENT.sub_reason=...) |
| **sub_reason 子枚举** | 无 | **与老韩 v0.3.1 INVALID_INTENT 字段对齐** | 不增 21 reject enum 总数 (老韩 §14.5 C-4) |
| **audit emit** | 4 event (v5 §5.2) | **每 event payload 加 4 ts (BLAKE3 hash chain 一并)** | 老唐 v1.1 envelope schema |

**其余 v5 §1~§7 全不动** — 不动 KMS / Shamir / 8 Blocker / nonce_mgr / WAL.

---

## 5.4 IPC msgpack schema 4 ts 字段 (新增)

### 5.4.1 SignRequest schema (msgpack over UDS, v4 §2.3 + §3.5 复用 + 加 4 ts)

```cpp
// signer IPC v5.1 SignRequest, trader 端打 4 ts, signer 端 PIT assert
struct SignRequest {
  // -- v5 原字段 (不动) --
  uint64_t request_id;          // ULID, trader 端单调
  std::string wallet_addr;      // 0x-hex, lower
  TypedData typed_data;         // Polymarket EIP-712 (CTF + neg-risk), B5 二次校验
  uint64_t nonce;               // 老叶 nonce_mgr 预留
  uint8_t peer_pid_hint;        // SO_PEERCRED 比对 (B1)
  uint8_t hmac[32];             // session HMAC (B1)

  // -- v5.1 新增 4 ts (R-20 §2.1) --
  int64_t event_ts;             // ns, 上游事件真实发生时间
  int64_t data_source_ts;       // ns, 上游 payload 发布时间
  uint8_t data_source_ts_source; // enum DataSourceTimestamp::Source (R-20 §2.2)
                                //   0=UPSTREAM_PAYLOAD, 1=UPSTREAM_HEADER,
                                //   2=INFERRED_FROM_DS_TS, 3=INFERRED_FROM_INGESTION, 255=UNKNOWN
  int64_t ingestion_ts;         // ns, trader L0 摄入 (CLOCK_MONOTONIC_RAW)
  int64_t as_of_ts;             // ns, OrderIntent 进 signer 时刻
};

// signer 写回 SignResponse 时打 2 个 signer 端 ts (R-20 §3 表"PaperSigner/LiveSigner")
struct SignResponse {
  uint64_t request_id;
  uint8_t sig[65];              // r||s||v, B5 二次校验后写
  int64_t sign_request_ts;      // ns, signer 收到 SignRequest 时刻
  int64_t sign_complete_ts;     // ns, sig 写回 trader 前
  uint8_t result;               // 0=OK, 非 0 = reject code (见 §5.4.3)
};
```

### 5.4.2 signer 端 PIT assert (R-20 §7 4 assert + now+60s 上界)

```cpp
// 接受 SignRequest 第一步, B5 typed-data 二次校验之前
bool signer::pit_assert(const SignRequest& r) {
  const int64_t now_ns = clock_gettime_ns(CLOCK_REALTIME);  // UTC ns
  // 4 不等式 (R-20 §2.1)
  if (!(r.event_ts <= r.data_source_ts))      return reject(SUB_REASON_TS_ORDER, 1);
  if (!(r.data_source_ts <= r.ingestion_ts))  return reject(SUB_REASON_TS_ORDER, 2);
  if (!(r.ingestion_ts <= r.as_of_ts))        return reject(SUB_REASON_TS_ORDER, 3);
  if (!(r.as_of_ts <= now_ns + 60'000'000'000LL)) return reject(SUB_REASON_TS_FUTURE, 4); // +60s 上界
  // 退化标识 sweep (R-20 §2.3) — UNKNOWN 直拒, INFERRED_* 放行但 audit P1
  if (r.data_source_ts_source == 255)         return reject(SUB_REASON_TS_UNKNOWN_SRC, 5);
  if (r.data_source_ts_source >= 2)           audit_emit_p1("signer.pit.inferred_src", r);
  return true;  // 通过 → 进 B5 typed-data 二次校验
}
```

### 5.4.3 reject code (与老韩 v0.3.1 INVALID_INTENT.sub_reason 字段对齐, 不增 21 enum 总数)

| signer reject result | 映射 老韩 reject_code (21 enum) | INVALID_INTENT.sub_reason (字段) |
|---|---|---|
| TS_ORDER (4 不等式任一违反) | INVALID_INTENT (#19) | `TS_ORDER_VIOLATED` |
| TS_FUTURE (as_of > now+60s) | INVALID_INTENT (#19) | `TS_FUTURE` |
| TS_UNKNOWN_SRC (source=255) | INVALID_INTENT (#19) | `TS_UNKNOWN_SRC` |
| (其他 v5 既有, 不动) | ... | ... |

sub_reason 是 **字段** 不是新 enum (老韩 v0.3.1 §14.5 C-4 决议 — 保 21 reject enum 总数封闭).

### 5.4.4 audit emit (与老唐 v1.1 envelope 对齐)

每 PIT reject + 通过 → 1 条 BLAKE3 hash chain event:

```
signer.pit.passed   → payload { request_id, 4 ts, source }
signer.pit.rejected → payload { request_id, 4 ts, source, sub_reason, violation_idx (1~5) }
signer.pit.inferred_src → payload { request_id, source } (P1 dashboard 计数)
```

老唐 v1.1 envelope 字段已规划 `event_ts + data_source_ts + source enum` (ADR-003 §6 6/19), signer 端 4 ts 全数透传, **不另开 audit schema**.

---

## 完成汇报

- **v5.1 已完成**: `docs/RESEARCH/laosun-key-management-v5.1.md` (本文档, ~95 行新增, ≤ 100 行 budget 内)
- **整改 C-2 闭合**: IPC SignRequest 加 4 ts + source enum + signer 端 PIT 4 不等式 assert + now+60s 上界 + REJECT(INVALID_INTENT.sub_reason=TS_*) → 拒绝穿越未来
- **与 v0.3.1 sub_reason 对齐**: 不耻下问 @老韩 — 推荐字段化 (TS_ORDER_VIOLATED / TS_FUTURE / TS_UNKNOWN_SRC), 不增 21 reject enum 封闭性
- **与 v1.1 BLAKE3 audit 对齐**: 不耻下问 @老唐 — 4 ts 透传 envelope, 不另开 schema
- **待 sync**: 老韩 v0.3.1 + 老唐 v1.1 (6/19 出) → 6/26 联合验收 (老沈 + 老郭)
- **v5 §1~§7 全不动** — 不动 KMS / Shamir / 8 Blocker / nonce_mgr / WAL

---

*v5.1 提交: 2026-05-28 UTC*
*Owner: 老孙 (crypto-signing-expert)*
*验收: 老沈 (security) + 老郭 (architecture-second-opinion), 6/26*
*v1~v5 保留作 review trail*
