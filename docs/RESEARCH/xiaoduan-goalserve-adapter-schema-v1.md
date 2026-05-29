---
owner: 小段 (#37, D 单元 IC, Goalserve 调研专精)
last_review: 2026-05-29
status: ACTIVE
wave: ADR-037 数据地基
cite:
  - ssot: docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md
  - endpoint_matrix: docs/RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md
  - official_doc_v3: docs/RESEARCH/xiaoduan-goalserve-official-doc-v3.md
  - w9_update: docs/RESEARCH/xiaoduan-w9-w5-goalserve-research-update-v1.md
  - code: include/stcpp/data/goalserve_adapter.hpp
  - test: tests/unit/test_goalserve_adapter.cpp
---

# Goalserve Inplay Full Feed 深挖 + Vendor-Agnostic Adapter Schema v1

## §0 任务背景

主管小余派单 (ADR-037 数据地基): 盘点 Goalserve inplay/livescore 能拿到的全部信息类型, 设计 vendor-agnostic adapter schema, 落代码 + ctest.

约束: Goalserve 付费已订阅 (key 在 .env, 绝不碰); ToS 速率红线; 本地优先不上云; R-20 四时间戳契约强制.

---

## §1 Goalserve Inplay Full Feed — 信息类型盘点

Goalserve 数据横跨三路径, 覆盖四类信息:

### §1.1 路径 A: inplay.goalserve.com/inplay-<sport>.gz (每秒刷新)

源: EU Sofia BG (AS58294), bet365 单源, 1s 推送

信息类型:

| 类型 | 字段 | 来源路径 |
|---|---|---|
| scores/时钟 | `info.period` / `info.minute` / `info.seconds` / `info.score` | events.<id>.info |
| scores/赛况 | `info.time_status` (11 enum: 0-9+99) / `info.state` (5位状态码) | events.<id>.info |
| scores/ID | `info.id` / `info.mid` / `info.bet365id` / `info.league_id` | events.<id>.info |
| odds/单源赔率 | `value_eu` (欧赔) / `suspend` / `handicap` | events.<id>.odds.<market_id>.participants.<pid> |
| odds/盘口 | `market_id` (从 dictionaries/odds-markets/<sport> 查表) | events.<id>.odds.<market_id>.name |
| ts/时间戳 | `updated_ts` (Unix ms, feed 整体刷新) | 顶层 JSON |

字段注意:
- `bm` 固定 = `"bet365"`, 无 numeric bookmaker_id
- `suspend` = "0" 或 "1" (字符串), 不是 bool
- `handicap` 仅 AH / Total 市场才有

### §1.2 路径 B: www.goalserve.com/getfeed/<KEY>/<sport>/home (每 5s)

源: US Phoenix AZ (AS18501)

信息类型:

| 类型 | 字段 | 备注 |
|---|---|---|
| scores/比分 | `match@minute` / `localteam@score` / `awayteam@score` | 进行中才有 minute |
| scores/赛况 | `match@status` | 字符串 enum (HH:mm / 数字 / HT / FT / ET / AET / P / ...) |
| scores/补时 | `match@timer` / `match@inj_time` / `match@inj_minute` | 足球补时字段 |
| events/赛事 | goal / yellowcard / redcard / subst / var_cancelled | events 节点 |
| stats/统计 | ICorner / IAttacks / IDangerousAttacks / IOnTarget / IPosession | live_stats 节点 |
| ts/时间戳 | `scores@updated` (.NET DateTime ticks, 须转换) | .NET ticks → unix_sec = (ticks - 621355968000000000) / 10_000_000 |

### §1.3 路径 C: www.goalserve.com/getfeed/<KEY>/getodds/soccer?cat=<sport>_10 (增量)

源: US Phoenix AZ (AS18501), 9 家 bookmaker, 增量 ts 协议

信息类型:

| 类型 | 字段 | 备注 |
|---|---|---|
| odds/多源赔率 | `bookmaker[k].@value` (decimal) / `bookmaker[k].@ts` (sec, per-bm) | 9 bm: 10Bet/WilliamHill/bet365/Marathon/Unibet/BetVictor/1xBet/Betano |
| odds/盘口 | `type@value` (market name) / `type@id` / `handicap@name` / `total@name` | pregame 市场, stop 标记 |
| scores/预赛 | `match@status` / `match@date` / `match@time` | 无实时时钟 |
| ts/时间戳 | `scores.@ts` (Unix sec, 全局) / `bookmaker.@ts` (sec, per-bm) | per-bm ts 可差 5-30min |
| ts/增量key | 去元音简写 schema (见 §3 增量 key 映射) | 增量响应与全量 key 不同 |

### §1.4 路径 D: commentaries/live_stats (每 30s)

| 类型 | 字段 | 备注 |
|---|---|---|
| stats/球队 | shots total/ongoal/offgoal/blocked, fouls, corners, offsides, possession, saves, passes | 足球 live stats |
| stats/球员 | shots_total/on_goal, goals, assists, fouls_drawn/committed, tackles, blocks, passes, rating | 球员实时统计 |
| events/PBP | comment type (Shot/Goal/Corner/VAR...) / pl_name / minute / x,y 坐标 | play-by-play 逐球 |

---

## §2 Vendor-Agnostic Adapter Schema 设计

### §2.1 设计原则

1. 四类 IR (中间表示): scores / stats / events / odds
2. 上层 strategy / signal 只看 IR, 不看 Goalserve 原始字段名
3. Adapter 函数负责 Goalserve raw → IR 映射
4. 未来换源: 实现 AdaptFromSportradarXxx() 等函数, 上层代码不动
5. R-20 四时间戳契约在所有 IR struct 中强制

### §2.2 四类 IR struct (见 include/stcpp/data/goalserve_adapter.hpp)

```
namespace stcpp::data::adapter

GameScoreRecord          — 比分 + 时钟 + 赛况
  FourTs ts              — R-20 4 ts (data_source_ts 优先 updated_ts/scores.@ts)
  MatchId match_id       — vendor_match_id + inplay_match_id + league_id + VendorId
  string home_team / away_team
  TimeStatus status      — 11 enum (0-9+99, 与 goalserve_client.hpp 共享)
  optional<string> period           — 当前阶段 (1st Half / Q2 / Set 3 ...)
  optional<int32> elapsed_min/sec   — 比赛时钟
  optional<int32> stoppage_min      — 补时 (足球)
  int32 home/away_score_total       — 总分
  array<int32, 12> home/away_periods — 分节比分 (最多 12 槽)
  uint8 periods_used
  optional<string> gs_state_code    — Goalserve 5位状态码

GameStatsRecord          — 球队统计
  FourTs ts
  MatchId match_id
  TeamStats home_stats / away_stats
    optional<int32> shots_total/on_goal/off_goal/blocked
    optional<int32> possession_pct / corners / fouls / yellow_cards / red_cards
    optional<int32> passes_total/accurate / attacks / dangerous_attacks
    optional<int32> assists / rebounds / turnovers (篮球)
    optional<int32> hits / errors (棒球)
    optional<int32> wickets; optional<string> overs (板球)

GameEventRecord          — 赛事事件
  FourTs ts
  MatchId match_id
  GameEventType event_type  — Goal/YellowCard/YellowRed/RedCard/Substitution/ShotOnTarget/...VAR/ScoringPlay/Other
  optional<int32> minute / extra_min
  string team_side          — "home" / "away"
  optional<string> player_name / player_id / assist_name / assist_id
  optional<string> description
  optional<bool> own_goal / penalty_goal
  optional<double> pitch_x / pitch_y  — 球场坐标 (PBP 才有)

OddsQuoteRecord          — 单 bookmaker 单 outcome 报价
  FourTs ts              — R-20: data_source_ts 优先 bookmaker.@ts (pregame) 或 updated_ts (inplay)
  MatchId match_id
  OddsSource source      — InplayGz / PregameOdds / RacingUk
  int32 bookmaker_id     — 0=bet365 inplay (无 numeric id); 14-144 pregame
  string bookmaker_name
  string market_id / market_name
  string outcome         — "Home" / "Draw" / "Away" / "Over" / "Under" / "Player1"
  optional<double> handicap
  double value_eu        — 欧赔, > 1.0 才有效
  bool suspended
```

### §2.3 AdaptBatch — 批量 IR 容器

```
AdaptBatch
  int64 data_source_ts_ns / ingestion_ts_ns  — R-20
  VendorId vendor
  GoalserveEndpointPath endpoint_path
  vector<GameScoreRecord> scores
  vector<GameStatsRecord> stats
  vector<GameEventRecord> events
  vector<OddsQuoteRecord> odds
```

上层按 InfoKind 分发到对应 SPSC queue (ADR-017).

### §2.4 时间戳转换 helper (R-20 强制)

```cpp
// inplay updated_ts (Unix ms) → epoch_ns
InplayTsMsToNs(int64 ts_ms) → ts_ms * 1_000_000

// pregame @ts (Unix sec) → epoch_ns
PregameTsSecToNs(int64 ts_sec) → ts_sec * 1_000_000_000

// .NET DateTime ticks → epoch_ns (Goalserve base feed updated 字段)
NetTicksToEpochNs(int64 ticks)
  → ((ticks - 621355968000000000) / 10_000_000) * 1_000_000_000
  → 返回 0 if ticks <= epoch_offset

// GameEventType 字符串映射
MapGoalserveEventType(string_view gs_type) → GameEventType
```

---

## §3 R-20 四时间戳契约落位

```
event_ts  <=  data_source_ts  <=  ingestion_ts  <=  as_of_ts

各路径 data_source_ts 来源:
  inplay.goalserve.com:  updated_ts (Unix ms) → InplayTsMsToNs()
  pregame getodds:       bookmaker.@ts (Unix sec, per-bm) → PregameTsSecToNs()
                         或 scores.@ts (Unix sec, 全局) 兜底
  base livescore home:   scores.updated (.NET ticks) → NetTicksToEpochNs()
  racing/uk:             同 base livescore

禁止: ingestion_ts 替代 data_source_ts (IngestionFallback 须 flag + 告警)

增量响应 key 映射 (pregame getodds ts 增量):
  全量 key → 增量 key
  scores   → scors
  category → catgoris
  matches  → matchs
  bookmaker → ookmakrs
  value    → valu
  bet365   → t365
  (共 16 个映射, 见 xiaoduan-goalserve-official-doc-v3.md §3)
adapter 层统一 normalize, 上层看不到 raw key 差异.
```

---

## §4 3-way Soccer 处理

Goalserve soccer 1x2 = Home/Draw/Away 三个 outcome, 每个独立 OddsQuoteRecord.

Strategy 层对三条记录联合 de-vig (ADR-008 §4):
```
fair_p_home = p_home_raw / (p_home_raw + p_draw_raw + p_away_raw)
fair_p_draw = p_draw_raw / (...)
fair_p_away = p_away_raw / (...)
```

Polymarket binary condition 映射: 每个 outcome → 独立 YES/NO condition.
若 Polymarket 只开 Home/Away (无 Draw condition), 用二值 fair_p:
```
fair_p_home_binary = p_home / (p_home + p_away)
fair_p_away_binary = p_away / (p_home + p_away)
```

---

## §5 各 Sport 比分差异 (ScorePair 12 槽映射)

| Sport | 阶段字段 | 最多节数 | 备注 |
|---|---|---|---|
| Soccer | 1st Half / 2nd Half / ET / P | 2 正+1 ET+PK = 4 | HT/FT/AET/Pen 字段 |
| Basketball (NBA) | Q1/Q2/Q3/Q4 + OT | 4+5 OT = 9 | OT 最多 5 次 |
| Baseball (MLB) | inn1-inn9 + extras | 9+3 = 12 | 满 12 槽 |
| Tennis | set1-set5 | 5 | 值如 "6.3" = 6-3 (非 odds!) |
| Hockey (NHL) | P1/P2/P3 + OT + SO | 5 | 含 shootout |
| AmFootball | Q1/Q2/Q3/Q4 + OT | 5 | |
| Cricket | 分 inning | 2 | 复杂: wickets/overs 另记 |
| Volleyball | set1-set5 | 5 | BO5 |

---

## §6 Adapter Gap 清单 (工程派单)

| Gap | 说明 | 负责人 |
|---|---|---|
| 增量 key 双 schema parser | pregame ts 增量 key 去元音映射 (16 条), 需独立 ctest | 小余 ETL |
| inplay JSON → AdaptBatch 真解析 | W5 真 HTTP 接入后, 实现 AdaptFromGoalserveInplayJson() | 小冯 |
| pregame getodds → AdaptBatch 真解析 | 含 bookmaker[] 数组 + per-bm ts | 小余 |
| 3-way → Polymarket binary mapping | per-outcome condition_id join | 老李 (Polymarket SSOT) |
| soccer 3-way de-vig ABI | FairValue struct 需加 per-outcome 三值 | 老周 (ABI lock) |
| dictionaries/odds-markets cache | market_id → name 每日拉一次 (8 sport, basket/esport 单数别名) | 小余 |
| .NET ticks → epoch_ns 精度验证 | 当前实测 sample "635712985484037849" → 具体日期确认 | 小段 自测 |

---

## §7 ctest 覆盖 (28 tests, 全绿)

```
T1 (4 tests): enum 闭合 — InfoKind / VendorId / GameEventType / OddsSource
T2 (6 tests): R-20 四时间戳契约 — 四类 IR struct + 违反检测
T3 (4 tests): 时间戳转换 helper — InplayTsMsToNs / PregameTsSecToNs / NetTicksToEpochNs
T4 (4 tests): OddsQuoteRecord.IsValid() — value/suspend/R-20 三重门
T5 (4 tests): MapGoalserveEventType — 8 类型 + VAR variants + Other
T6 (2 tests): AdaptBatch 四类 record 共存 + data_source_ts monotonic
T7 (1 test):  3-way soccer 1x2 — Home/Draw/Away 三个独立 OddsQuoteRecord
T8 (3 tests): .NET ticks 边界 — epoch_offset / 负值 / 典型值

build: cmake --build build_adr037 --target test_goalserve_adapter -j8
run:   ./build_adr037/tests/unit/test_goalserve_adapter
result: 28 passed, 0 failed
```

---

**最后更新**: 2026-05-29 by 小段 (ADR-037 数据地基, Wave P0)
