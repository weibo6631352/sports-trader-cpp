# Goalserve 真实比分接看板 + event↔market 映射物化 — 接入方案 v1

- **Owner**: 小余 (#D 数据基础设施主管 + ETL 主权)
- **Date**: 2026-05-29
- **Last review**: 2026-05-29
- **Status**: PROPOSED (待 GM 合并 / 老周 ABI ack / 小段 feed owner ack)
- **派单**: GM wave (分工并行) — "Goalserve 真实比分接看板 + event↔market 映射物化"

---

## §0 目标与现状

### 0.1 一句话目标

把 `/api/v1/score/{event_id}` 从 `DemoStateProvider` 的**确定性假数据**(用 `now()-偏移` 模拟链路, demo 语义) 升级为
**Goalserve inplay 真实比分快照**，途中物化 `condition_id ↔ event_id` 映射表供看板反查，全程守 R-12 (不在 event loop 拉 Goalserve) + R-20 (4 时间戳用 Goalserve 自带 ts)。

### 0.2 现状盘点 (已读代码)

| 组件 | 文件 | 现状 |
|---|---|---|
| score endpoint | `src/stcpp/debug_api/endpoint_score.cpp` | 已实现, 只调 `sp.score(event_id)` 拼 JSON; **endpoint 本身无需改** |
| StateProvider 契约 | `src/stcpp/debug_api/state_provider.hpp` | `virtual EventScore score(const std::string&) const = 0;` 已定; `EventScore` POD 已定 (§0.3) |
| Demo provider | `src/stcpp/debug_api/demo_state_provider.hpp` L217-279 | `score()` 返回硬编码 LAL-BOS / ARS-CHE 等; `data_source()` 返回 `"demo"` |
| Stub provider | `state_provider.hpp` L385-390 | `found=false` 合法空值 |
| Goalserve IR schema | `include/stcpp/data/goalserve_adapter.hpp` | `GameScoreRecord` 已定 (period/score/status/clock/FourTs) — **真值的上游来源** |
| Goalserve 4ts | `include/stcpp/data/goalserve_record.hpp` | `FourTs` + `TimeStatus` (11 enum 0-9+99) + `IsMonotonic()` |
| 映射 SSOT | `docs/RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md` | gameId↔pregame_match_id 两步 lookup + fuzzy 兜底要求 (§1.2 已知陷阱) |
| Goalserve client | `include/stcpp/data/goalserve_client.hpp` + `goalserve_stub.cpp` | `Fetch()` 现为 W4 stub; 真 HTTP 接入 W5 (小段/小冯) |

**关键结论**: endpoint 层 + 契约层 (`EventScore` / `score()`) 已 G-FREEZE-W 冻结, 本任务**只换 provider 实现 + 新建映射物化 ETL**, 不改 ABI、不改 endpoint。这是最小惊喜路径。

### 0.3 EventScore 字段 (现契约, vendor-agnostic)

```cpp
struct EventScore {
    bool found{false};
    std::string event_id;       // Polymarket 内部 event id (= gamma event slug/gameId 派生)
    std::string sport;          // "basketball"/"soccer"/"tennis"/...
    std::string status;         // pregame/inplay/halftime/final (vendor-agnostic)
    std::string period;         // "Q3"/"2H"/"Set 3"/"T7"
    std::int64_t clock_sec{0};  // 场内计时秒; 0=不适用
    std::string home; std::string away;
    int home_score{0}; int away_score{0};
    FourTs ts{};                // R-20 4 时间戳
    std::string source{"goalserve"};  // vendor 降为 source 标签
};
```

字段已对齐 GM 要求 (home/away/score/period/clock/status, source=goalserve)。**无需改字段**。仅需 provider 把 `GameScoreRecord` → `EventScore` 投影。

---

## §1 架构: feed 落 in-mem 最新态 → score endpoint 只读快照 (R-12)

### 1.1 红线约束 (不可越)

- **R-12**: WebSocket / debug_api 线程**绝不**同步拉 Goalserve (RTT 跨洋 ~200-400ms, 远超 100us)。
  → score endpoint 调用 `provider.score()` **只读 in-mem snapshot**, O(1) hash lookup, 无 IO、无锁 > 100us。
- **R-20**: `EventScore.ts` 的 `data_source_ts_ns` 必须来自 Goalserve 自带 ts (inplay `updated_ts` ms / `scores@ts` sec / `.NET ticks`)，**禁** debug_api 层用本地 `now()` 替代 data_source_ts。
  - `as_of_ts_ns` 允许 = endpoint 读快照时刻 (R-20 允许 — 这是"观测读取"维度, 非上游维度)。
  - `event_ts_ns` = 比赛事件发生时刻 (inplay 无独立 event ts 时, 可 = data_source_ts; 但不可用 now())。
  - Demo 现用 `now()-偏移` 是 **demo 语义合规** (provider 自报 `data_source()=="demo"`), 真接入后必须透传 Goalserve ts。

### 1.2 数据流 (三段解耦, 经典 producer/consumer)

```
 [Goalserve inplay feed]              [in-mem 最新态]            [score endpoint]
 inplay.goalserve.com/                ScoreSnapshotStore         GET /api/v1/score/{id}
   inplay-<sport>.gz (1s)             (double-buffer / atomic     │
        │                              shared_ptr swap)           │
        │ 小段 feed 线程 (非热路径)        ▲                         │
        ▼                              │ publish(swap front)       ▼
   GoalserveClient::Fetch  ──parse──►  AdaptGameScore ──project──► provider.score(event_id)
   (W5 真 HTTP, 跨洋, gzip)            GameScoreRecord              只读 front snapshot (R-12)
        │                                  │                       O(1) by event_id
        │                                  │ key = event_id          │
        │                                  ▼ (经 §2 映射反查)         ▼
        │                          map: event_id → GameScoreRecord   EventScore JSON 200/404
   data_source_ts = updated_ts (R-20)
```

**三段职责**:
1. **采集段** (小段 owner): feed 线程定时 `Fetch` inplay-<sport>.gz → parse → `AdaptGameScore` → `GameScoreRecord[]`。跨洋高延迟/带宽紧, 走老吴 proxy 限频 (见 `laowu-proxy-goalserve-bandwidth-v1.md`)。**非热路径, 独立线程**。
2. **物化段** (小余 owner): `GameScoreRecord` 用 `inplay_match_id → pregame_match_id → event_id` (§2 映射表) 重 key, 写入 `ScoreSnapshotStore`。映射表本身是 ETL 物化产物。
3. **读取段** (小卢/小余, 接现契约): `ScoreStateProvider::score(event_id)` 只读 store front snapshot, 投影成 `EventScore`。

### 1.3 ScoreSnapshotStore (in-mem 最新态, R-12 合规读)

新增 `src/stcpp/debug_api/score_snapshot_store.hpp` (或归 `include/stcpp/data/`, 由老周定归属):

```cpp
// 线程安全只读 store: 采集线程 publish, debug_api 线程 read (R-12)
// 实现: std::shared_ptr<const Map> + atomic load/store (RCU-lite, 无读锁)
//   或 double-buffer + seqlock; 读侧 O(1), 绝不阻塞采集侧
class ScoreSnapshotStore {
public:
    // 采集线程调用 (非热路径): 整批 swap, 读侧无锁感知
    void publish(std::shared_ptr<const std::unordered_map<std::string, EventScore>> next);
    // debug_api 线程调用 (R-12: 无 IO, 无锁 > 100us): atomic load front
    std::optional<EventScore> get(const std::string& event_id) const;
private:
    std::atomic<std::shared_ptr<const Map>> front_;  // C++20 atomic<shared_ptr>
};
```

- 读侧 `get()` = 1 次 atomic load + 1 次 hash lookup, 纳秒级, R-12 安全。
- 写侧 `publish()` = 构建新 map + atomic store, 不阻塞读侧 (旧 snapshot 引用计数归零自动释放)。
- staleness 由 `EventScore.ts.data_source_ts_ns` 表达; 看板/metrics 算 `as_of - data_source_ts`, store 不主动过期 (过期判定留给消费侧, 数据仍可读 + 标 stale)。

### 1.4 ScoreStateProvider (接现契约)

新增 `ScoreStateProvider`: 持 `const ScoreSnapshotStore&`, 实现 `score()` 真值; 其余方法 (positions/pnl/market/book...) 仍委托 Demo 或 Stub (分模块逐步接真)。

```cpp
EventScore ScoreStateProvider::score(const std::string& event_id) const override {
    if (auto s = store_.get(event_id)) return *s;        // 真值, found=true
    EventScore miss; miss.found = false; miss.event_id = event_id;
    miss.source = "goalserve"; return miss;              // 404
}
const char* data_source() const override { return "live"; }  // 老钱红线: 非 demo 标 live
```

main 注入: `--score-live` flag 时用 `ScoreStateProvider`, 否则维持 `DemoStateProvider` (前端集成/dogfood 不阻塞)。

---

## §2 event↔market (condition_id↔event_id) 映射物化 + 看板反查

### 2.1 为什么要物化 (而非每次实时 lookup)

依 `laoli-xiaoduan-cross-source-mapping-v1.md` §1.2, 一次比分接看板需要两步 lookup:
```
inplay_match_id → pregame_match_id → event.gameId → event_id (+ markets[].condition_id)
```
第二步要 join Polymarket gamma `/events`。**每次请求都跑 = R-12 违规 + 浪费跨洋带宽**。
→ 物化成静态映射表, 周期性刷新 (gamma events 变化频率低, pregame 映射赛前定), score 读取时 O(1) 反查。

### 2.2 物化映射表 schema (ETL 产物)

`event_market_map` — 物化表 (Parquet 分区 `dt=YYYY-MM-DD/sport=<sport>/` + in-mem 加载副本):

| 列 | 类型 | 来源 | 说明 |
|---|---|---|---|
| `event_id` | string | gamma event.gameId 派生 / slug | 看板主键 (score endpoint 入参) |
| `gamma_event_id` | string | gamma `event.id` | Polymarket 数值 event id |
| `slug` | string | gamma `event.slug` | polymarket_url 用 |
| `game_id` | string | gamma `event.gameId` | join key (可空 → 触发 fuzzy) |
| `pregame_match_id` | string | Goalserve pregame 6-digit | = game_id (string 比较) |
| `inplay_match_id` | string | Goalserve inplay 134xxxxxx | feed 重 key 用 (经 inplay-mapping) |
| `sport` | string | 两侧一致化 | basketball/soccer/tennis/... |
| `condition_ids` | list\<string\> | gamma event.markets[].conditionId | 该 event 全盘口 (ML/Total/Spread...) |
| `home` / `away` | string | gamma + Goalserve | 一致化队名 (fuzzy 校验用) |
| `match_method` | string | ETL | "gameId_exact" / "fuzzy_name_time" / "manual" |
| `match_confidence` | double | ETL | fuzzy 时的相似度 [0,1]; exact=1.0 |
| `start_ts_ns` | int64 | gamma + Goalserve | 开赛时间 (fuzzy 兜底 join key) |
| `built_ts_ns` | int64 | ETL | 物化时刻 (新鲜度审计) |

**反查方向 (双向索引)**:
- 看板正查: `event_id → condition_ids[]` (盯盘从赛事钻到盘口)
- score 重 key: `inplay_match_id → event_id` (feed 落 store 时)
- 盘口反查: `condition_id → event_id` (market endpoint 已有 `event_id` 字段, 此表做权威校验)

### 2.3 映射构建 ETL (物化段)

```
Step 1  拉 gamma /events?tag_id=<sport>&closed=false → 提取 event_id/gameId/slug/markets[].conditionId/teams/start
Step 2  拉 Goalserve pregame <sport>/home + inplay-mapping → pregame_match_id ↔ inplay_match_id + teams + start
Step 3  PRIMARY JOIN: gamma.gameId == goalserve.pregame_match_id (string 相等)
Step 4  FUZZY FALLBACK (gameId 空或 join miss):
          score = 0.6 * team_name_sim(home,away 双向, token Jaccard + 别名表)
                + 0.4 * time_proximity(|start_gamma - start_gs| < 90min 归一)
          阈值 >= 0.85 接受为 fuzzy match, 记 match_method=fuzzy_name_time
Step 5  写 Parquet 分区 + publish in-mem 映射副本
Step 6  质量报告: 填充率 / fuzzy 占比 / unmatched 清单 (§4)
```

**fuzzy 兜底目标 (GM 指定)**: gameId 填充率不足时, **整体映射覆盖率 ≥ 90%**。
- exact (gameId) 优先; gameId 实测填充率待小段 W9 W2 确认 (SSOT §6 待确认项)。
- 若 exact < 90%, fuzzy 补到 ≥ 90%; 仍 < 90% 的 sport 该 MVP 阶段标 yellow, 列入质量报告升级。

### 2.4 队名一致化 (fuzzy 关键)

gamma 队名 (e.g. "Los Angeles Lakers") vs Goalserve (e.g. "LA Lakers" / "Lakers") 不一致。
→ 维护 `team_alias` 映射表 (per-sport, 物化资产), 先归一化再算 Jaccard。别名表初版人工 seed (各联赛 top 队) + fuzzy unmatched 清单迭代补充 (体育专家 review)。
inplay-mapping 的 `inplay_team1_id` 是字符串名称 (SSOT §1.2 陷阱), 也走此别名表。

---

## §3 实现归属 + MVP 范围

### 3.1 谁实现 (分工)

| 工作项 | 归属 | 理由 |
|---|---|---|
| 采集段: inplay feed 线程 + `Fetch` + `AdaptGameScore` parse | **小段** (#37, Goalserve SSOT) | feed/parse 语义主权; W5 真 HTTP 已是小段/小冯 既定 backlog (adapter §6 gap) |
| `ScoreSnapshotStore` (in-mem store, R-12 读写解耦) | **小余 + 老周 ack** | ETL 主权; 双 buffer/atomic shared_ptr 实现敏感, 老周 ABI/线程模型 ack |
| 映射物化 ETL (gamma+goalserve join + fuzzy + Parquet) | **小余** (主力) | ETL 主权 + schema 归一化 + fuzzy + Parquet 分区全在我领域 |
| gamma /events 拉取 + gameId 提取 | 小李 (#07) 给字段 / 小余落 ETL | 老李 Polymarket 协议给 gameId 语义 (SSOT 已有) |
| 队名别名表 seed | 各 sport 体育专家 seed → 小余物化 | 字段语义归体育专家 (协作边界) |
| `ScoreStateProvider` 接契约 + main 注入 flag | **小卢** (#senior-ic-pool, ADR-038 owner) 或小余 | endpoint/provider 是小卢 ADR-038 领地; 小余提 store 接口 |
| 看板反查前端 (event→盘口钻取) | 前端 (小苏/小尤) | 非数据基础设施 |

**协作边界自检** (我的 persona): 我做 ETL + schema + fuzzy + Parquet + in-mem store 接口设计; **不碰**实时业务流热路径 (归小段/小冯)、不碰查询前端 (归产品)。feed 字段语义问小段, schema 终裁问数据仓库/老周。

### 3.2 MVP 范围 (季内绿 → 后续)

| 阶段 | sport | 内容 | 验收 |
|---|---|---|---|
| **MVP (季内绿)** | soccer / basketball / tennis | inplay 真比分接 score endpoint; 映射物化覆盖率 ≥ 90%; R-20 透传 Goalserve ts; R-12 读快照 | 三 sport score endpoint 返真值 + 4ts `IsMonotonic()` 通过 + 映射质量报告 ≥ 90% |
| **后续 (W10+)** | NFL (amfootball) / NHL (hockey) | 补 inplay-<sport>.gz 接入 + 映射 (NFL 分节/NHL 三节语义差异) | 同 MVP 标准 |
| **不在本期** | baseball / volleyball / esports / prop / outright | period 语义复杂 (棒球局/局上下) 或盘口结构特殊, 留待映射 SSOT 补 | — |

MVP 选 soccer/basketball/tennis 理由:
- inplay-mapping 端点 soccer/basketball/tennis 均已实证 (SSOT §1.3, basketball 用 `bsktbl/` 路径);
- period/clock 语义清晰 (足球 1H/2H + 补时, 篮球 Q1-Q4 + clock, 网球 Set);
- 与老彭 inplay alpha v2 (soccer/basketball 优先) 对齐。

### 3.3 status / period 投影规则 (GameScoreRecord → EventScore)

`TimeStatus` (11 enum 0-9+99) → `EventScore.status` (vendor-agnostic 4 态) 投影:

| Goalserve TimeStatus | EventScore.status | period 处理 |
|---|---|---|
| NotStarted(0) | `pregame` | period="", clock_sec=0 |
| InPlay(1) | `inplay` (+ period 含 "HT"/"Half" 时 → `halftime`) | period 透传 (映射体育专家给的阶段名) |
| Ended(3) | `final` | period="FT"/"Final" |
| Postponed/Cancelled/Walkover/Interrupted/Abandoned/Retired/Removed/ToBeFixed | `pregame` 或专门 status (待产品定) | 看板需标异常态; MVP 先归 pregame + 留 gs_state_code 审计 |

`clock_sec`: 来自 `GameScoreRecord.elapsed_min*60 + elapsed_sec` (inplay 才有; 篮球用倒计时则取剩余秒, 体育专家确认每 sport 语义)。
`period`: 来自 `GameScoreRecord.period` (optional string), 半场无 clock 时 status→halftime。

---

## §4 质量报告 (ETL 产物, 随物化每次刷新)

映射物化每次跑出质量报告 (JSON + 看板 metrics), 字段:

```
{
  "built_ts_ns": ...,
  "per_sport": {
    "basketball": {
      "gamma_events": 28, "goalserve_matched": 26,
      "exact_gameId": 21, "fuzzy_name_time": 5, "unmatched": 2,
      "coverage_pct": 92.9,          // (exact+fuzzy)/gamma_events; 目标 >= 90
      "gameId_fill_pct": 75.0,       // gameId 非空占比 (验证 SSOT §6 待确认项)
      "fuzzy_avg_confidence": 0.89,
      "unmatched_events": ["nba-...", ...]   // outlier 清单, 体育专家/小段 review
    },
    "soccer": {...}, "tennis": {...}
  },
  "alerts": [
    "soccer coverage 87% < 90% target → 升级补别名表"  // 低于目标触发
  ]
}
```

**outlier / 数据质量监控** (我领域):
- coverage < 90% per-sport → alert + unmatched 清单升级 (补别名表 / 人工 manual match)。
- fuzzy_avg_confidence < 0.85 → 别名表质量不足, 体育专家 review。
- score endpoint 4ts `IsMonotonic()==false` 或 `ds_origin==IngestionFallback` → R-20 告警 (Goalserve ts 缺失, 不可静默用 now())。
- staleness `as_of - data_source_ts > 阈值` (inplay 应 < 5s) → feed 卡顿告警 (归小段 feed 健康)。

---

## §5 落地步骤 (分工并行, 不阻塞)

1. **(小余)** 定 `event_market_map` schema + Parquet 分区 + `ScoreSnapshotStore` 接口 → 本文 §1.3/§2.2 即 SSOT 草案, 待老周 ABI ack。
2. **(小余)** 映射物化 ETL: gamma /events 拉取 + Goalserve pregame/inplay-mapping join + fuzzy + Parquet 写 + 质量报告。先离线跑 (C++ 小程序 `experiments/xiaoyu-event-market-map/`), 实测 3 sport 覆盖率。
3. **(小段)** W5 inplay feed 真 HTTP + `AdaptGameScore` parse 落 `GameScoreRecord` (adapter §6 既定 gap)。
4. **(小余+小段)** 物化段: `GameScoreRecord` 经映射重 key → `ScoreSnapshotStore::publish`。
5. **(小卢/小余)** `ScoreStateProvider` 接现契约 + main `--score-live` flag 注入; `data_source()=="live"`。
6. **(联合)** 验收: 3 sport score endpoint 返真值 + 4ts 单调 + 映射 ≥ 90% + R-12 读快照零 IO。

**不阻塞前端**: DemoStateProvider 仍是默认; `--score-live` 切真值。前端集成/dogfood 不等本任务。

---

## §6 红线自检

| 红线 | 状态 |
|---|---|
| R-12 (event loop 零同步 REST/阻塞) | ✓ score endpoint 只读 in-mem store (atomic load + hash, ns 级); feed 拉取在独立采集线程 |
| R-20 (4 时间戳 + Goalserve 自带 ts, 禁本地 now() 替 data_source_ts) | ✓ `EventScore.ts.data_source_ts_ns` 透传 `GameScoreRecord` 的 `updated_ts`/`scores@ts`/`.NET ticks`; as_of 才用读取时刻 |
| schema 静默变更 (通知下游) | ✓ `EventScore` 字段不改 (G-FREEZE-W 只增不改名); 新增 `event_market_map` 表通知回测 (小蒋) + 看板 |
| ToS (速率/带宽) | ✓ feed 走老吴 proxy 限频; 映射物化周期刷新非高频 |
| 回测/实盘同逻辑 | ✓ 映射表 + AdaptGameScore IR 回测实盘共用 (vendor-agnostic IR) |
| 越界 | ✓ 我做 ETL/schema/fuzzy/Parquet/store 接口; 实时业务流归小段/小冯, 查询前端归产品 |

---

## §7 待确认 (不耻下问)

| 问谁 | 问题 |
|---|---|
| @小段 | gameId 实测填充率 (SSOT §6 未结)? inplay-mapping basketball `bsktbl/` 现 OK? W5 inplay feed + AdaptGameScore 落地排期? |
| @老周 | `ScoreSnapshotStore` 归 `include/stcpp/data/` 还是 `debug_api/`? atomic<shared_ptr> double-buffer 线程模型 ack? |
| @老李 | gamma /events 提 gameId/markets[].conditionId 字段稳定性 (3 sport)? |
| @体育专家 (篮/足/网) | 每 sport clock_sec 语义 (篮球倒计时 vs 足球累计)? period 阶段名标准? team_alias seed? |
| @产品 (老胡/老钱) | Postponed/Abandoned 等异常 TimeStatus 看板如何呈现 (是否扩 EventScore.status 枚举)? |
| @小蒋 | 映射表 Parquet 分区供回测消费的字段/分区契约确认? |

---

**最后更新**: 2026-05-29 by 小余 (D 数据基础设施主管) | PROPOSED, 待 GM 合并
