# GM 红线 R-20 — 所有数据源使用必须标记时间信息

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Hard Redline（一票否决，违者 P0）
- **关联:** 用户 2026-05-28 指令、`docs/RESEARCH/data-contract-v1.md` §2 (小邓 4 时间戳契约)、`docs/RESEARCH/laotang-audit-schema-v1.md` (老唐 BLAKE3 schema)、CLAUDE.md §8 红线

---

## 1. 用户原话

> "所有数据源使用都必须标记时间信息。"

## 2. 红线 R-20

**任何数据从外部进入、跨模块流转、被消费、被审计、被回测使用，都必须携带可追溯的时间戳。** 违者 = P0。

### 2.1 强制时间戳契约

**4 时间戳必须全程携带（小邓 data-contract-v1.md §2）：**

| 时间戳 | 含义 | 谁产生 | 何时填 |
|---|---|---|---|
| `event_ts` | 事件在真实世界发生的时间 | 上游（Goalserve / Polymarket）| 数据源声明 |
| `data_source_ts` | 数据源**发布**该数据的时间 | 上游 | 数据源声明（HTTP Date 头 / payload 字段） |
| `ingestion_ts` | 我们**抓到 / 收到**该数据的时间 | 我们 | 数据进入 L0 摄入层时打 |
| `as_of_ts` | 该数据在我们系统**被使用**的时间 | 我们 | feature / decision / audit 引用时打 |

**不等式必须满足：** `event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts`

违反不等式 → 数据穿越未来（time leakage）→ **PIT correctness 破** → 回测 / paper / live 三层一致性破。

### 2.2 时间戳来源优先级（用户 2026-05-28 二次澄清）

> **"时间信息优先用数据源内部自带的。"** — 用户原话

**强制规则：** 上游数据源自带 ts → **必须用**，禁止用本地 `now()` 替代。

| 字段 | 来源优先级 |
|---|---|
| `event_ts` | 1) 上游 payload 内字段（如 Polymarket WSS `timestamp`、Goalserve XML `<match @date>`、Polygon block `timestamp`）<br>2) 上游无 → `data_source_ts_source = "INFERRED_FROM_DS_TS"` 退化用 `data_source_ts` |
| `data_source_ts` | 1) **上游 payload 内字段**（首选）<br>2) HTTP `Date` 响应头（次选）<br>3) 上游完全无 ts → `data_source_ts_source = "INFERRED_FROM_INGESTION"` 退化用 `ingestion_ts` |
| `ingestion_ts` | 本地 `clock_gettime(CLOCK_MONOTONIC_RAW)`（不能用 `gettimeofday`，防 NTP 跳变）|
| `as_of_ts` | 本地，使用时刻打 |

**为什么必须优先上游 ts：**
1. **市场真实状态时间** — 上游 ts 是市场状态在彼时彼刻的真值，我们端的 ts 已经加了网络延迟（跨洋 880ms）
2. **避免时钟偏移** — 我们 server 时间和上游 server 时间可能差几秒，用上游 ts 保证三方（backtest / paper / live）一致
3. **回测可复现** — 用上游 ts 跑回测，重放时序保持一致
4. **跨数据源对齐** — 同一比赛在 Goalserve 与 Polymarket 双源对齐时，必须用各自的上游 ts，不能用我们 ingestion_ts（会差几百 ms）

**退化标识必须明示：**

```cpp
struct DataSourceTimestamp {
    int64_t epoch_ns;
    enum Source : uint8_t {
        UPSTREAM_PAYLOAD = 0,    // 首选, 上游 payload 字段
        UPSTREAM_HEADER = 1,     // 次选, HTTP Date header
        INFERRED_FROM_DS_TS = 2, // 退化 1: 用上一级 ts
        INFERRED_FROM_INGESTION = 3, // 退化 2: 用我们 ingestion_ts
        UNKNOWN = 255            // 异常, 触发 P1 alert
    } source;
};
```

**退化时必须 audit emit + dashboard 计数**，连续 N 条 INFERRED → 告警（说明上游数据源 ts 字段缺失或格式变更）。

### 2.3 时间戳异常退化

| 场景 | 处理 |
|---|---|
| 上游 ts 明显错误（> now + 60s 未来 / < event_ts） | 拒收 + alert P1 + audit emit |
| 上游 ts 字段不存在 | 退化用 `data_source_ts_source = INFERRED_*` + audit + 月度 sweep（小冯）|
| 上游 ts 格式变更（如从 ISO 改 epoch） | 立刻 P0（schema 静默变更红线触发）|
| 多个上游 ts 字段冲突（payload vs Date header） | payload 优先，Date header 退到 audit 字段做对照 |

## 3. 强制覆盖范围

| 场景 | 必须打的时间戳 | Owner |
|---|---|---|
| **外部 API 抓数据**（Goalserve / Polymarket / Polygon） | `data_source_ts` + `ingestion_ts` | 小余 ETL |
| **特征计算** | `feature_compute_ts` + `feature_snapshot_id`（含 4 ts 链路） | 小邓 ML / 小程 信号 |
| **信号决策** | `decision_ts` + `feature_snapshot_id` | 小程 / 小肖 |
| **风控审批** | `rm_eval_ts` | 老韩 RM v0.3 |
| **PaperSigner / LiveSigner** | `sign_request_ts` + `sign_complete_ts` | 老孙 signer v5 |
| **paper 虚拟成交** | `virtual_match_ts` + `virtual_confirm_ts` | 小蒋 paper engine |
| **历史回放** | PIT correctness 严格守 4 ts 不等式 | 小蒋 backtest |
| **Cache 命中** | `cache_entry_ts` + `ttl` | 老周 lifecycle v1 §8 |
| **WAL audit** | ULID + `event_ts_ns`（老唐 BLAKE3 schema 已有） | 老唐 + 老王 |
| **Shadow signal (ML)** | `model_id` + `feature_snapshot_id` + `inference_ts` | 小邓 shadow framework |
| **文档引用数据** | 数据采集时间必须明示（如"实测于 2026-05-28 13:30 UTC"）| 文档作者（所有 agent） |

## 4. 红线违例 = P0 事故

- 任何 PR 引入"无时间戳"的数据流 → **直接 reject**
- 任何 cache entry 无 `cache_entry_ts` → P0
- 任何 backtest 跑出来 PIT 违例（穿越未来）→ 信号回炉
- 任何文档引用 API 实测数据但**没标采集时间** → 小米归档时拒收

## 5. 与已有红线协同

- **R-11**（paper 不污染真账本）→ paper 时间戳进 `paper_audit.wal`，绝不流到 `risk_audit.wal`
- **R-12**（WebSocket 不阻塞）→ event loop 打 `ingestion_ts` 必须 < 50us（用 `clock_gettime(CLOCK_MONOTONIC_RAW)`，不能 syscall stall）
- **D-04**（回测与实盘共用特征管道）→ `feature_snapshot_id` 跨 backtest / paper / live 一致
- **数据契约 §2**（小邓 4 时间戳契约）→ 本红线是 §2 的全局强制版

## 6. 时间戳精度 + 时区

- 所有时间戳统一 **UTC**，不允许本地时间
- 精度：**纳秒 (ns)**（`std::chrono::system_clock::now()` + `time_point_cast<nanoseconds>`）
- 序列化：epoch_ns int64（不存 ISO 字符串，省 4 字节 + 解析快）
- 显示给人时再转 ISO（UI / 日志层）

## 7. PIT CI 强制（小蒋 Wave 14 接 owner）

`PaperSigner` / `LiveSigner` / `BacktestSigner` 任一接受 OrderIntent 前，强制断言：

```cpp
assert(intent.event_ts <= intent.data_source_ts);
assert(intent.data_source_ts <= intent.ingestion_ts);
assert(intent.ingestion_ts <= intent.as_of_ts);
assert(intent.as_of_ts <= now_utc_ns());  // 不允许未来时间
```

任一 fail → `REJECT(INVALID_INTENT)` + audit emit + alert P1。

CI grep job（老高 / 老练 接）拦：
- `now()` 不带显式 UTC
- `localtime()` / `mktime()` 出现
- 时间相关 `int` 而不是 `int64`（防溢出）
- 缺 `feature_snapshot_id` 的 signal struct

## 8. 文档约定

所有引用 API / 实测数据的文档**必须**在数据来源处明示采集时间：

✅ 正确：
> "Polymarket gamma `/events?tag_slug=mlb` 实测于 **2026-05-28 13:25 UTC**，返回 1977 markets / 7.6MB"

❌ 错误：
> "Polymarket gamma 拿到 1977 markets"（无时间，无法判断是否还有效）

小米归档时检查此条，不达标拒收。

## 9. 派单

| Owner | 动作 | 截止 |
|---|---|---|
| 老韩 | RM v0.3 § audit schema 加 4 时间戳字段 + assert 强制 | Sprint-2 W2 |
| 老唐 | audit schema BLAKE3 hash chain 加 4 时间戳到 payload | Sprint-2 W1 末 |
| 小蒋 | paper engine + backtest 都加 PIT CI grep job | Sprint-2 W2 |
| 小邓 | ML shadow signal 加 `model_id` + `feature_snapshot_id` + `inference_ts` | Sprint-2 W3 |
| 老高 | PR 模板 + clang-tidy 加时间戳缺失检查规则 | Sprint-2 W1 末 |
| 老孙 | Signer v5 IPC schema 加 4 时间戳字段 | Sprint-2 W2 |
| 老李 | endpoint v3.1 给每个 endpoint 标"数据源声明时间戳字段位置"（`@last_updated` / `Date` header / payload `ts`） | Sprint-2 W2 |
| 小段 | Goalserve v3 标各 endpoint 的 `data_source_ts` 字段位置 | 已在跑 |
| 小余 | ETL pipeline 每 record 必加 `ingestion_ts`，进 WAL 必校 4 ts 不等式 | Sprint-2 W2 |
| 小米 | 文档审核加"数据采集时间是否明示" CI grep | Sprint-2 W1 末 |
| 老郭 | ADR-004 评审本红线 + 全员落地路径 | Sprint-2 W1 末 |

## 10. 与公司 4 价值观对齐

- **数字说话** → 没有时间戳的数字 = 不可信
- **不耻下问** → "这条数据是几点的？" 任何人随时可问，必须能答
- **公开失败** → PIT 违例不准藏，立刻 incident
- **实盘优先** → backtest 不打时间戳 → 实盘必然崩

---

**Decided by 老雷, 2026-05-28**

CLAUDE.md §8 红线节同步更新（加 R-20）。
