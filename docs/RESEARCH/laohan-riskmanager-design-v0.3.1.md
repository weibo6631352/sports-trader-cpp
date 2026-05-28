# RM v0.3.1 (整改 C-3 + C-4)

- Owner: 老韩 (risk-engineer)
- Date: 2026-05-28 UTC (R-20)
- 验收人: 老郭 (6/26)
- 范围: **增量整改 only**, v0.3 全文保留 (`docs/RESEARCH/laohan-riskmanager-design-v0.3.md` 不删不改)
- 关联:
  - ADR-003 整改清单: `docs/ADR/2026-06-15-arch-rm-key-v0.4-v0.3-v5-review.md` §10 (C-3 + C-4)
  - sub_reason 源头: `docs/RESEARCH/xiaoxiao-kelly-slippage-model-v1.md` §2 / §4.2 / §5 Case 6
  - audit emit 联签: `docs/RESEARCH/laotang-audit-schema-v1.md` (老唐 v1.1 联签)
  - PREGAME 取齐对端: 老周 v0.4 §17.6.1 (改 v0.5 PREGAME_FAR HALT 15000ms)

---

## 0. v0.3 → v0.3.1 变更摘要

| 域 | v0.3 立场 | v0.3.1 立场 | 触发 |
|---|---|---|---|
| `INVALID_INTENT` reject | 单 enum, "book_snapshot_ts_ns == 0 OR < (now - 60s)" 二者任一 | **保持 enum 单条, 内部新增 `sub_reason` 字段细分 5 子原因** | ADR-003 C-4 老郭裁定 |
| PREGAME HALT | 15000ms (v0.3 §14.1) | **15000ms 确认 (不变)**, 由老周 v0.5 PREGAME_FAR 从 30000ms 改齐到 15000ms | ADR-003 C-3 老郭裁定 |
| 21 reject enum 总数 | 21 | **21 (不增)**, sub_reason 细分仅在 INVALID_INTENT 内部 | ADR-003 C-4 硬约束 |
| audit emit | v0.3 §5 含 `reason_code` | **新增 `reason_sub_reason` 字段**, 仅当 `reason_code == INVALID_INTENT` 时非 NONE, 与老唐 v1.1 联签 | C-4 派生 |

**封闭性**: 21 enum 不破, sub_reason 是 INVALID_INTENT enum 的二级展开, 不算第 22 enum.

---

## 11.x PREGAME 15000ms 确认 (C-3 取齐)

### 11.x.1 现状 + 决议

v0.3 §14.1 / §14.6 PREGAME WARNING=5000ms / HALT=**15000ms**, ≤ D-06 红线 30s. **老韩侧不动一行**, 由老周 v0.5 §17.6.1 将 PREGAME_FAR HALT 从 30000ms 改齐到 15000ms.

**取 15000ms 而非 30000ms 理由**: 更保守, 留 SETTLED 才用满 30s; 与 INPLAY_COLD 10000ms 留 5s 阶梯缓冲状态抖动.

### 11.x.2 config self-check 加固

```cpp
static_assert(threshold_of(MarketState::PREGAME).halt_ms == 15000,
              "PREGAME HALT must be 15000ms (ADR-003 C-3)");
static_assert(threshold_of(MarketState::PREGAME).halt_ms <= 30000,
              "D-06 redline: any HALT must be <= 30000ms");
```

hot-reload 同样校验, 违反 → 启动失败 / reload 拒绝.

---

## 3.10.x INVALID_INTENT.sub_reason (C-4, 21 enum 不增)

### 3.10.x.1 接口定义

```cpp
namespace stcpp::risk {

enum class RejectCode : uint8_t {
    // ... v0.3 既有 14 + 4 + AET + STRATEGY_DECAYED = 20 ...
    INVALID_INTENT = 17,         // v0.3 强化, v0.3.1 细分 sub_reason
    // ... STRATEGY_DECAYED = 20 ...
};
// 总数: 21 enum (不增) — ADR-003 C-4 硬约束

// 仅当 code == INVALID_INTENT 时使用 (其他 reject 一律 NONE)
enum class InvalidIntentSubReason : uint8_t {
    NONE                     = 0,  // sentinel, 非 INVALID_INTENT 时填这个
    SUB_REASON_BOOK_TS_ZERO  = 1,  // book_snapshot_ts_ns == 0 (未填)
    SUB_REASON_BOOK_TS_STALE = 2,  // book_snapshot_ts_ns < (now_ns - 60s)
    SUB_REASON_NAN_OR_INF    = 3,  // 任一浮点字段 NaN / Inf (小肖 §1 R-1)
    SUB_REASON_NEGATIVE      = 4,  // size / book_depth / tick / price 非正
    SUB_REASON_ILLEGAL_TICK  = 5,  // tick_size ∉ {0.001, 0.01} (老李 S1-002)
};

struct RejectDetail {
    RejectCode             code;
    InvalidIntentSubReason sub_reason;  // 仅 code==INVALID_INTENT 时 != NONE
    // ... v0.3 既有 rule_trace / bankroll_snapshot / ts ...
};

}
```

### 3.10.x.2 评估顺序 (R3 子项细化)

R3 (intent 字段合法性) 内部按从廉价到昂贵顺序短路, 命中即返回 INVALID_INTENT + sub_reason:

| 顺序 | 检查 | sub_reason |
|---|---|---|
| R3.1 | 任一 `double` 字段 `std::isnan() \|\| std::isinf()` | `SUB_REASON_NAN_OR_INF` |
| R3.2 | `size <= 0 \|\| book_depth_l1_usdc <= 0 \|\| tick_size <= 0 \|\| !(0 < price < 1)` | `SUB_REASON_NEGATIVE` |
| R3.3 | `tick_size != 0.001 && tick_size != 0.01` | `SUB_REASON_ILLEGAL_TICK` |
| R3.4 | `book_snapshot_ts_ns == 0` | `SUB_REASON_BOOK_TS_ZERO` |
| R3.5 | `book_snapshot_ts_ns < (now_ns - 60'000'000'000LL)` | `SUB_REASON_BOOK_TS_STALE` |

**fail-closed**: 5 项任一命中 → 立即 REJECT(INVALID_INTENT, sub_reason=X), 不评估后续 R4..R12.

**性能**: 5 个 short-circuit 比较, ≤ 50ns, 远低于 G3 evaluate 200μs 预算.

### 3.10.x.3 与小肖对齐 + SETTLED 勘误

- 小肖 §1 R-1 NaN/Inf P1 → `SUB_REASON_NAN_OR_INF`; §4.2 book_snapshot_ts_ns 必填 → `SUB_REASON_BOOK_TS_ZERO`.
- 小肖 §5 Case 6 "quote=NaN → INTERNAL_ERROR" **v0.3.1 修正**: 改走 INVALID_INTENT + `SUB_REASON_NAN_OR_INF` (输入污染, 非内部错).
- **v0.3 §14.5 末行勘误**: "SETTLED → INVALID_INTENT + sub-reason" 撤回; sub_reason 5 子语义均不匹配 SETTLED (市场关闭非字段污染). 仍用 v0.3 既有 `MARKET_NOT_ACTIVE` enum, 不动 21 总数. — 老郭 6/19 复核.

---

## 5.x audit emit 加 sub_reason 字段 (与老唐 v1.1 联签)

### 5.x.1 schema 增量

v0.3 §5.2 既有 audit record `reason_code` 字段, v0.3.1 新增 `reason_sub_reason`:

```protobuf
// 在 RiskDecisionPayload (老唐 v1 §2.4.x) 内
message RiskDecisionPayload {
  // ... v0.3 既有 ...
  uint32 reason_code        = 50;  // RejectCode enum
  uint32 reason_sub_reason  = 51;  // (v0.3.1 新) InvalidIntentSubReason, 仅 reason_code==17 时 !=0
}
```

### 5.x.2 编码约束 (老唐 v1.1 联签项)

- `reason_sub_reason` 仅当 `reason_code == INVALID_INTENT (17)` 时允许 != 0.
- 其他所有 reject code (含 STRATEGY_DECAYED / STALE_DATA / AET_SIGN_FAILED 等) → `reason_sub_reason = 0 (NONE)`.
- audit verifier (老唐 §3.x replay 校验器) 加 invariant assert: `code != 17 ⟹ sub_reason == 0`, 违反 = corrupt audit.

### 5.x.3 Grafana / replay 联动

- `stcpp-audit replay --reject-only` 输出列加 `sub_reason` (派单 @小宋 v0.3 §15 已有, 加列).
- Grafana panel `rm_reject_count{code=INVALID_INTENT, sub_reason=...}` 5 个时间序列, G8 月度报表能定位 stale book / NaN / illegal tick 各占多少.
- @老唐 v1.1 schema 加 `reason_sub_reason` field id=51 (老唐 6/19 deadline 同 ADR-003 整改).

### 5.x.4 向前兼容

- v0.3 之前 audit 文件 `reason_sub_reason` 缺失 → replay 工具读取 default 0 (NONE), 不破老数据.
- protobuf 增字段语义安全 (老唐 §3 schema evolution policy 允许 add optional field).

---

## 附录 — 派单回执 (v0.3.1 新增 3 条)

| 我问谁 | 问题 | deadline |
|---|---|---|
| @小肖 | `SUB_REASON_NAN_OR_INF` 是否需要覆盖 `expected_fill_price_hint` optional 字段 (传了 NaN 也算?) | 6/19 |
| @老唐 | audit schema v1.1 加 `reason_sub_reason` field id=51 + replay verifier invariant | 6/19 |
| @老周 | v0.5 §17.6.1 PREGAME_FAR HALT 从 30000ms 改齐到 15000ms 确认 | 6/19 |

---

**END v0.3.1.** 等老郭 6/19 关 C-3 + C-4 + 6/26 配合 C-2 (R-20 4 ts audit schema, 不在本档).
