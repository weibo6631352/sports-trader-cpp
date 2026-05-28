# Goalserve 完整数据结构 SSOT v1

- Owner: 小段 (D 单元 IC, Goalserve 调研专精)
- Date: 2026-05-29
- Last review: 2026-05-29
- Status: ACTIVE (Wave 40 P0 交付)
- 依据:
  - `xiaoduan-goalserve-api-spec-v1.md` (v1, 字段填充率 + 延迟实测)
  - `xiaoduan-goalserve-endpoint-matrix-v2.md` (v2, 17 sport × 16 endpoint family, 219 探针)
  - `xiaoduan-goalserve-official-doc-v3.md` (v3, 4 domain 官方文档 + 真实 odds 实证)
  - `xiaoduan-w8-goalserve-inplay-node-verification-v1.md` (W8, Sofia BG 节点实测)
  - `xiaoduan-goalserve-odds-by-sport-v2.1.md` (v2.1, sport × odds 三态矩阵)
  - `laopeng-w8-oq-p02-3-inplay-single-source-ack.md` (老彭 W8 W2 ack, bet365 单源确认)
  - 实测 raw sample: `docs/RESEARCH/data/xiaoduan-goalserve-samples/`
  - 实测探针: `docs/RESEARCH/data/xiaoduan-goalserve-v3-probe.sh`
  - 官方文档: `docs/GOALSERVER/`

---

## §1 老板 verbatim (约束入文)

> "数据结构很重要, 快点补齐吧, 摸清楚后起码大家看到后可以对市场结构和数据源结构有个清楚的认知"
> — 老板 2026-05-29

本文是整个 Goalserve 接入层的 SSOT. 读者: 工程 (老周 / 小余 / 小冯), 量化 (小梁 / 小程 / 老彭), 产品 (老胡), GM (老雷).

---

## §2 Goalserve 数据源结构 (全 sport 关系图)

### §2.1 四域名分工

Goalserve 基础设施横跨 4 个独立域名, **不是一个服务器**:

```
Goalserve API (4 domains)
  |
  +-- www.goalserve.com   (US, Phoenix AZ, Codero AS18501)
  |     +-- /getfeed/<KEY>/<sport>/<endpoint>   pregame scores / livescore / schedule / home
  |     +-- /getfeed/<KEY>/getodds/soccer?cat=<sport>_10   pregame odds (9 bookmakers)
  |
  +-- inplay.goalserve.com   (EU, Sofia BG, CloudWall AS58294)
  |     +-- /inplay-<sport>.gz   inplay odds (bet365 单源, 1s refresh)
  |     +-- /dictionaries/odds-markets/<sport>   market_id → name
  |     +-- /dictionaries/states/<sport>   state code → event name
  |     +-- /results/<yyyyMM>/<MATCH_ID>.json   结算结果
  |
  +-- livescore.goalserve.com   (US, Phoenix AZ, Codero AS18501)
  |     +-- /getfeed/<KEY>/...   (livescore 专路, 少用)
  |
  +-- oddsfeed.goalserve.com   (US, Phoenix AZ, Codero AS18501)
        +-- /api/v1/odds/pre-game/settlement?...   pregame 结算
        +-- /api/v1/odds/pre-game/settlements?...   批量结算
        +-- /api/v1/odds/pre-game/settlements/matches?...   按 match 批量结算
```

**关键约束**:
- `inplay.goalserve.com` 是唯一的实时 odds 推送源, 地理位置在欧洲 Sofia BG (AS58294)
- 其余 3 个域名全在美国 Phoenix AZ (AS18501)
- 建议生产节点部署在 eu-west-2 (London), 使得 inplay Sofia BG → 生产节点 RTT ~20-30ms

### §2.2 数据维度 × Sport 全覆盖

| Sport | inplay odds | pregame odds | livescore/scores | schedule | standings | 实测状态 (2026-05-28) |
|---|---|---|---|---|---|---|
| Soccer | ✅ | ✅ (18.6 MB, soccer_10) | ✅ soccer/home XML + soccernew/home JSON | ✅ d-1 历史, worldcup | via home | 五大联赛季外, MLS 季内 |
| Basketball (NBA) | ✅ (季内) | ✅ (10.0 MB, basket_10) | ✅ bsktbl/nba-scores | ✅ bsktbl/nba-shedule (755KB) | ✅ nba-standings | NBA 季后赛结束, WNBA 在赛 |
| Tennis | ✅ (21 events 法网) | ✅ (19.1 MB, tennis_10) | ✅ tennis/home (含 inplay+当日) | via home | ✅ atp/wta rankings | 法网进行中 |
| Baseball (MLB) | ✅ (季内) | ✅ (2.0 MB, baseball_10) | ✅ baseball/usa (161KB) | ✅ mlb-shedule | — | MLB 正赛季 |
| Hockey (NHL) | ✅ (季内) | ✅ (1.2 MB, hockey_10) | ✅ nhl-scores | ✅ nhl-shedule (3.7MB) | ✅ nhl-standings | NHL 季后赛结束 |
| AmFootball (NFL) | ✅ (季内) | ✅ (football_10 待重测) | ✅ nfl-scores | ✅ nfl-shedule | ✅ nfl-standings | NFL 季外 |
| Cricket | — | ✅ (cricket_10) | ✅ cricket/livescore (269KB, ball-by-ball) | — | — | IPL 收尾 |
| MMA/UFC | — | ✅ (52KB, mma_10) | ✅ mma/schedule (71KB) | ✅ | — | 在赛 |
| Golf | — | — (getodds 全 0) | ✅ golf/pga (8.4KB) | — | — | PGA 在赛 |
| Volleyball | ✅ | — | ✅ volleyball/home | — | — | |
| Handball | — | ✅ (1.4 MB, handball_10) | ✅ handball/home | — | — | |
| Rugby League | — | ✅ (998KB, rugbyleague_10) | ✅ rugbyleague/home | — | — | NRL 等 |
| Futsal | — | ✅ (224KB, futsal_10) | ✅ futsal/home | — | — | |
| Esports | ✅ (时差) | ✅ (esports_10) | ✅ esports/home (CS GO) | — | — | CS GO 季内 |
| Boxing | — | ✅ (boxing_10) | XML 空壳 | — | — | 无近期比赛 |
| Darts | — | ✅ (darts_10) | ✅ darts/home (7KB) | — | — | |
| Table Tennis | — | ✅ (table_tennis_10) | ✅ tabletennis/home | — | — | |
| UK Horse Racing | — | N/A | ✅ racing/uk (1.4MB, 含 odds!) | — | — | 唯一 base-feed 内嵌 odds |
| F1 | — | — | ✅ f1/drivers (2.5KB) | XML 空壳 | — | 间歇期 |
| NPB/KBO (棒球) | — | — | ✅ baseball/japan + baseball/korea | — | — | 季内 |

### §2.3 三层 odds 数据路径

```
Goalserve odds 数据 (三路径)

路径 A — inplay odds (实时, 1s, bet365 单源)
  inplay.goalserve.com/inplay-<sport>.gz
  → gzip 解压 → JSON
  → bm = "bet365" (结构固定, 无其他 bookmaker)
  → events.<id>.odds.<market_id>.participants.<pid>.value_eu

路径 B — pregame odds (周期, 30s 增量, 9 家 bookmaker)
  www.goalserve.com/getfeed/<KEY>/getodds/soccer?cat=<sport>_10&json=1
  → 首拉全量 (1-19 MB)
  → 后续用 &ts=<unix-sec> 增量 (实测 hockey 1.2MB → 14KB, 压缩比 83x)
  → scores.category[].matches.match[].bookmaker[n].@value
  → 9 家: 10Bet/14, WilliamHill/15, bet365/16, Marathon/17, Unibet/18, BetVictor/65, 1xBet/105, Betano/144

路径 C — base feed 内嵌 odds (UK 赛马唯一)
  www.goalserve.com/getfeed/<KEY>/racing/uk?json=1
  → 1.5MB, 18 bookmakers, 7 tournaments × 47 races × 446 horses
  → scores.tournament[].race[].odds.horse[].bookmakers.bookmaker[].{name, odd, odd_id}
  → 18 家: Bet365/WilliamHill/Coral/Betfred/Boylesports/Ladbrokes/Unibet/...
```

---

## §3 字段表 (各 sport, 各 endpoint)

### §3.1 Event/Game 共享字段 (跨 sport pregame base feed)

| 字段 | 类型 | 含义 | 实测样本 | 注意 |
|---|---|---|---|---|
| `id` / `@id` | string | match primary key, 跨赛季不变 | `"310218"` | `@`前缀 = XML→JSON, 两套 parser |
| `datetime_utc` | string | UTC 开赛时间 | `"02.10.2025 16:00"` | 唯一可信时间字段, 无秒无 tz offset, parse 加 `Z` |
| `date` / `formatted_date` | string | 显示用日期 | `"20.05.2026"` | 别用作时间戳 |
| `time` | string | 当地时间显示 | `"11:00 AM"` | 别用 |
| `timezone` | string | 当地时区缩写 | `"EDT"` | 对应 `time` 字段 |
| `status` | string | 比赛状态 | `"In Play"` / `"Final"` / `"Not Started"` | 枚举不闭合, 见 §3.4 |
| `hometeam.id` / `name` | string | 主队 id + 名称 | `"Denver Nuggets"` | 100% 填充 |
| `awayteam.id` / `name` | string | 客队 id + 名称 | `"Miami Heat"` | 100% 填充 |
| `venue_id` / `venue_name` | string | 球场 | `"Ball Arena"` | 99.6% 填充 |
| `updated` | string | Goalserve 内部更新时间 | `"635712985484037849"` | .NET DateTime ticks! 转换: unix_sec = (ticks - 621355968000000000) / 10000000 |

### §3.2 Inplay 字段 (inplay.goalserve.com, bet365 单源)

**顶层 JSON 结构**:

| 字段 | 类型 | 含义 |
|---|---|---|
| `bm` | string | 固定 = `"bet365"`, 结构性单源 |
| `updated_ts` | int ms | feed 整体 last update Unix ms |
| `events` | object | key = inplay match id (134xxxxxxx 格式) |
| `events.<id>.info.id` | string | inplay match id |
| `events.<id>.info.mid` | string | second internal id |
| `events.<id>.info.bet365id` | string | bet365 原生 id |
| `events.<id>.info.league_id` | string | league id |
| `events.<id>.info.period` | string | 当前节/盘 | 
| `events.<id>.info.score` | string | 当前比分 `"0:1"` |
| `events.<id>.info.state` | string | 5 位状态码, 查 dictionaries/states/<sport> |
| `events.<id>.info.minute` | string | 比赛分钟 |
| `events.<id>.info.seconds` | string | 比赛秒数 |
| `events.<id>.info.time_status` | string | 11 enum: 0-9 + 99, 见 §5 |
| `events.<id>.odds.<market_id>` | object | market_id 查 dictionaries/odds-markets/<sport> |
| `events.<id>.odds.<mid>.name` | string | market 名称, e.g. `"1x2 (1st Half)"` |
| `events.<id>.odds.<mid>.participants.<pid>.name` | string | outcome 名称, e.g. `"Home"` / `"Draw"` / `"Away"` |
| `events.<id>.odds.<mid>.participants.<pid>.value_eu` | string decimal | 欧赔值, e.g. `"8.5"` |
| `events.<id>.odds.<mid>.participants.<pid>.suspend` | string | `"0"` = 活跃, `"1"` = 暂停 |
| `events.<id>.odds.<mid>.participants.<pid>.handicap` | string decimal | AH/Total 让分线 |

**实测 sample (soccer inplay, 2026-05-28, soccernew-inplay)**:
```json
{
  "scores": {
    "@sport": "soccer", "@updated": "635712985484037849",
    "match": {
      "@id": "27116800", "@minute": "86:31", "@v": "0", "@stop": "False",
      "localteam": {"@name": "Houston Dynamo", "@score": "1"},
      "awayteam":  {"@name": "Colorado Rapids", "@score": "0"}
    }
  }
}
```

注: soccernew/inplay (www 节点) 只含比分. 含 odds 的 inplay feed 在 `inplay.goalserve.com/inplay-soccer.gz` (EU Sofia 节点).

**inplay.goalserve.com 实测 soccer inplay odds 样本 (2026-05-28 15:30 UTC)**:
```json
{
  "bm": "bet365",
  "events": {
    "134181543": {
      "info": { "id": "134181543", "mid": "9544365", "bet365id": "195235332",
                "league_id": "6374", "period": "1st Half", "score": "0:1",
                "state": "11007", "minute": "28" },
      "odds": {
        "27": {
          "name": "1x2 (1st Half)",
          "participants": {
            "...270": { "name": "Home", "value_eu": "8.5", "suspend": "0" },
            "...271": { "name": "Draw", "value_eu": "...",  "suspend": "0" },
            "...272": { "name": "Away", "value_eu": "...",  "suspend": "0" }
          }
        },
        "12": { "name": "Asian Handicap", "...": "handicap + value_eu" },
        "421": { "name": "Match Goals", "...": "Over/Under + handicap + value_eu" }
      }
    }
  }
}
```

### §3.3 Pregame Odds 字段 (getodds, 9 bookmakers)

**URL 格式**: `http://www.goalserve.com/getfeed/<KEY>/getodds/soccer?cat=<sport>_10&json=1`

**顶层 JSON (全量)**:

| 字段 | 类型 | 含义 |
|---|---|---|
| `scores.@ts` | int | Unix 秒, 用于下次增量 `&ts=<value>` |
| `scores.sport` | string | sport 名称 |
| `scores.category[n].@id` | string | league/category id |
| `scores.category[n].@name` | string | league 名称 |
| `scores.category[n].matches.match[m].@id` | string | pregame match id (6位数, 与 inplay id 不同) |
| `scores.category[n].matches.match[m].@date` | string | 开赛日期 |
| `scores.category[n].matches.match[m].@time` | string | 开赛时间 |
| `scores.category[n].matches.match[m].localteam.@name` | string | 主队名 |
| `scores.category[n].matches.match[m].awayteam.@name` | string | 客队名 |
| `scores.category[n].matches.match[m].odds.bookmaker[k].@id` | string | bookmaker id |
| `scores.category[n].matches.match[m].odds.bookmaker[k].@name` | string | bookmaker 名 |
| `scores.category[n].matches.match[m].odds.bookmaker[k].@ts` | string | bookmaker 自己的报价时间 (秒), 各家独立 |
| `scores.category[n].matches.match[m].odds.bookmaker[k].@name (outcome)` | string | outcome 名 (`Home`/`Draw`/`Away`) |
| `scores.category[n].matches.match[m].odds.bookmaker[k].@value` | string decimal | 欧赔值 |

**实测 hockey (Carolina vs Montreal) 关键数值**:
- 76 个 markets, 主: 3Way Result / Home-Away / Total / Asian Handicap / Period Winner / Correct Score
- 9 bookmakers, bookmaker id: 10Bet/14, WilliamHill/15, bet365/16, Marathon/17, Unibet/18, BetVictor/65, 1xBet/105, Betano/144

**增量响应 (ts 参数模式)**: 响应中 key 名"去元音简写", 需要两套 schema:

| 全量 key | 增量 key |
|---|---|
| `scores` | `scors` |
| `category` | `catgoris` |
| `matches`/`match` | `matchs` |
| `localteam`/`awayteam` | `localtam`/`awaytam` |
| `bookmaker` | `ookmakrs` |
| `value` | `valu` |
| `False` | `Fals` |
| `bet365` | `t365` |
| `events` | `vnts` |
| `firstperiod` | `irstpriod` |

### §3.4 比赛状态 (time_status) 枚举 — 官方闭合 11 值

| code | 名称 | Polymarket 处理建议 |
|---|---|---|
| 0 | Not Started | 赛前监控 |
| 1 | InPlay | 实时交易窗口 |
| 2 | TO BE FIXED | 异常, 不下单 |
| 3 | Ended | 结算触发 |
| 4 | Postponed | Polymarket stake refund (规则确认) |
| 5 | Cancelled | stake refund |
| 6 | Walkover | 对手弃赛, 按规则结算 |
| 7 | Interrupted | 暂停, 可能 resume, 持仓等待 |
| 8 | Abandoned | 大概率 refund |
| 9 | Retired | 选手退赛 (tennis 常见), 按规结算 |
| 99 | Removed | 从 feed 移除, 不再监控 |

ETL 必须: catch-all unknown code → alert, 不能 default-to-settled.

### §3.5 各 Sport 比分字段差异

| Sport | 比分模型 | 关键字段 |
|---|---|---|
| Basketball (NBA) | Q1+Q2+Q3+Q4 + OT + totalscore | `q1`/`q2`/`q3`/`q4`, `ot`, `totalscore`, `posession="True/False"` (字符串!) |
| AmFootball (NFL) | Q1+Q2+Q3+Q4 + OT + totalscore + player stats | `q1..q4`, `ot`, `totalscore`, player-level offensive/defensive |
| Baseball (MLB) | 分局 inning by inning | `innings.inning[n].@score/@hits`, `@totalscore`, `@oddsid` (外键引用, 无 value) |
| Hockey (NHL) | P1+P2+P3 + OT + SO + PP | `p1`/`p2`/`p3`, `ot`, `so`, `pp`, `goalkeeper_stats`, scoring events, penalties |
| Soccer | 90min + stoppage | `localteam.score`, `awayteam.score`, `minute`, `inj_time` |
| Tennis | set-by-set + game + point | `set1`/`set2`... 注: 值如 `"6.3"` = 局比分 6-3, 非 odds! |
| Cricket | inning-by-inning + ball-by-ball commentary | `innings`, `wickets`, `overs` (注: `"2.1"` = 2 over 1 ball, 非 odds!) |

---

## §4 Goalserve Outcome 表达方式

### §4.1 各 Sport Outcome 名称

| Sport | Market | Outcomes | 备注 |
|---|---|---|---|
| Soccer | 1x2 Moneyline | `Home` / `Draw` / `Away` | **3-way**, Polymarket binary 需特殊处理 |
| Soccer | Asian Handicap | `Home` / `Away` + `handicap` 参数 | |
| Soccer | Match Goals (Over/Under) | `Over` / `Under` + `handicap` 参数 | binary |
| Basketball | 3Way Result | `Home` / `Draw` / `Away` | NBA 无平局, 但 getodds 结构里保留 Draw |
| Basketball | Home-Away | `Home` / `Away` | binary (实用) |
| Basketball | Total | `Over` / `Under` + handicap | binary |
| Tennis | Moneyline | `Player1` / `Player2` | binary (无 Draw) |
| Tennis | Set Handicap | `Player1` / `Player2` + handicap | |
| Baseball | Moneyline | `Home` / `Away` | binary (MLB 无平局) |
| Hockey | Moneyline | `Home` / `Draw` / `Away` | 3-way (含加时结果) |
| Hockey | 60min Result (Home-Away) | `Home` / `Away` | binary |
| Cricket | Match Winner | Team1 / Team2 / Draw | 3-way (Test) 或 2-way (T20/ODI) |
| MMA/UFC | Moneyline | Fighter1 / Fighter2 | binary |

### §4.2 3-way 市场 → Polymarket binary mapping

**核心问题**: Goalserve soccer 1x2 = 3 outcomes (Home/Draw/Away), Polymarket 通常是 binary conditional market.

**Polymarket 体育市场实际结构** (老李 v3 实测确认):
- Polymarket 通常把 Soccer 拆成多个独立 binary condition:
  - `"Will [Home Team] win?"` → YES/NO (2 token)
  - `"Will [Away Team] win?"` → YES/NO (2 token)
  - `"Will the match end in a draw?"` → YES/NO (2 token)
- 或者 outright 结构: `"Who will win [Match]?"` → 3 outcomes, 但 Polymarket 的 condition 仍是 binary per token

**Goalserve → Polymarket 映射规则**:

```
Goalserve 3-way {Home, Draw, Away}
       |
       v
  [Mapping Layer (ETL 责任)]
       |
       +-- Home outcome → Polymarket condition_id(Home Win) token YES
       +-- Draw outcome → Polymarket condition_id(Draw)     token YES  (若存在)
       +-- Away outcome → Polymarket condition_id(Away Win) token YES
```

**实际操作**: 每个 outcome 对应 1 个 Polymarket binary condition 的 YES token. 3-way 市场 = 3 个独立 condition + 约束 (Home_p + Draw_p + Away_p ≈ 1, de-vig 后).

**关键约束**: Soccer Home/Draw/Away 三值 de-vig 方法与 binary de-vig 不同:
- binary multiplicative: `fair_p = implied_p_home / (implied_p_home + implied_p_away)`
- 3-way multiplicative (ADR-008): `fair_p_i = implied_p_i / (implied_p_home + implied_p_draw + implied_p_away)` 分别算三个

FairValue 工程 ABI 必须支持 per-outcome 三值, 单值 FairValue 字段在 soccer 3-way 场景下语义不清 (见 §6 工程 gap).

### §4.3 inplay market_id 常见值 (soccer)

| market_id | 名称 | outcomes |
|---|---|---|
| 1 | 1X2 (Full Time) | Home / Draw / Away |
| 2 | Asian Handicap | Home / Away + handicap |
| 3 | Goals Over/Under | Over / Under + handicap |
| 27 | 1X2 (1st Half) | Home / Draw / Away |
| 11 | 3-Way Handicap | Home / Draw / Away + handicap |
| 12 | Asian Handicap | Home / Away |
| 421 | Match Goals | Over / Under + handicap |

完整 market_id 字典: `http://inplay.goalserve.com/dictionaries/odds-markets/soccer`

---

## §5 时间信息字段 (R-20 4-ts 来源)

老板红线 R-20: **时间戳优先用数据源自带, 禁本地 `now()` 替代上游 ts**.

### §5.1 各 endpoint 时间字段来源

| Endpoint | event_ts (赛事时刻) | data_source_ts (Goalserve 推送) | ingestion_ts (本地 client) | 备注 |
|---|---|---|---|---|
| **inplay.goalserve.com/inplay-<sport>.gz** | `events.<id>.info.minute` + `info.seconds` (比赛内时钟) | `updated_ts` (Unix ms) | 本地 `CLOCK_MONOTONIC_RAW` recv 时刻 | `updated_ts` 是 feed 整体更新, ~3s 延迟实测 |
| **getodds/soccer?cat=<sport>_10** | — (pregame, 无事件时钟) | `scores.@ts` (Unix 秒) + 各 bookmaker `@ts` | 本地 recv 时刻 | bookmaker `@ts` 可做 bookmaker-level 增量; 各家可差 5-30 分钟 |
| **baseball/usa** | `@datetime_utc` | `scores.@updated` (.NET ticks, 须转换) | 本地 recv 时刻 | .NET ticks 公式: unix_sec = (ticks - 621355968000000000) / 10000000 |
| **bsktbl/nba-shedule** | `match.datetime_utc` | `scores.updated` (.NET ticks) | 本地 recv 时刻 | 同上 |
| **soccer/home** / **soccernew/home** | `match.@minute` (进行中) | `scores.@updated` (.NET ticks) | 本地 recv 时刻 | |
| **cricket/livescore** | `innings.over` + ball commentary timestamp | `scores.updated` (.NET ticks) | 本地 recv 时刻 | |
| **oddsfeed settlement** | `dateTime` param (Unix 秒) | response `ts` | 本地 recv 时刻 | |

### §5.2 4-ts 完整链路 (R-20 强制)

```
event_ts  ≤  data_source_ts  ≤  ingestion_ts  ≤  as_of_ts
   |               |                 |                |
   |               |                 |                +-- 决策引擎 tick 时刻 (本地 MONOTONIC_RAW)
   |               |                 +-- recv() 完成时刻 (本地 MONOTONIC_RAW)
   |               +-- updated_ts (inplay ms) 或 @ts (pregame sec) 或 .NET ticks
   +-- match 开赛 UTC (datetime_utc) 或 比赛内时钟 minute:second
```

**禁止**: `ingestion_ts = now()` 替代 `data_source_ts` (即 `updated_ts` / `@ts` / `.NET ticks 转换值`).

### §5.3 时间字段解析陷阱

1. **.NET DateTime ticks**: `updated` = `"635712985484037849"`. 必须转换:
   - 公式: `unix_sec = (ticks - 621355968000000000) / 10000000`
   - C++ 用 `int64_t` 避免溢出

2. **updated_ts (inplay) 单位 ms**, `@ts` (pregame) 单位 **sec**, 两者不同. ETL 统一用 ms 存储 (ETL-11).

3. **datetime_utc 格式**: `"02.10.2025 16:00"` 无秒无 tz offset, 解析时必须 append `":00 +0000"`.

4. **bookmaker @ts 各家不同**: pregame getodds 中 9 家各自有独立 `@ts`, 滞后 5-30 分钟. fair value 计算时按 `@ts` 加权衰减旧报价.

---

## §6 工程 vs Goalserve 真实结构 gap

| 工程字段/假设 | Goalserve 真实 | Gap 描述 | 影响 | 负责人 |
|---|---|---|---|---|
| FairValue 单值 (scalar) | 每 outcome 一个 value: Home/Draw/Away 三值 (soccer 1x2) | 单值 FairValue 只适配 binary market (tennis/basketball). Soccer 3-way 场景下单值语义不清 | SignalIntent / FairValue ABI 需加 outcome 维度 | 老周 W8 W5 工程 ABI 派单 |
| inplay / pregame 用同一个解析器 | inplay (inplay.goalserve.com, 无 bookmaker 层) vs pregame (www, bookmaker[n] 数组嵌套) 结构根本不同 | 代码路径必须分离: inplay 走 bet365 单源 value_eu, pregame 走多源 bookmaker 数组 | 若共用 parser 会漏多源 bookmaker, 或误把 inplay 单源当多源 | 小余 + 小冯 ETL 分路 |
| ADR-008 multiplicative de-vig (多源) 直接应用 inplay | inplay bm 字段固定 = "bet365", 结构性单源, 不是测试时机问题 | inplay 不能用多源 de-vig. 需 ADR-008 §5 inplay 例外条款 (单家 multiplicative + C2 门槛上调) | P0-02 alpha 损失 0.7-1.2pp, 已老彭 ack | 老郭 W8 W5 立 ADR-008 §5 例外条款 |
| Goalserve odds endpoint 可用 (`/getodds/`) | v1/v2 测到 0 字节 → 真相是缺少 `?cat=<sport>_10` 必填参数 | 原先误判 "无权限", 实为参数缺失. v3 已修正 | 已修正, 无遗留 gap | 已 close |
| inplay.goalserve.com 不存在 / 未接入 | 真实存在, Sofia BG (AS58294 CloudWall), EU 节点 | 原先只测 www.goalserve.com (US), 漏了 inplay 子域名 | inplay odds 未接 = 无实时 fair value 锚源 | 小冯 W8 inplay client 接入派单 |
| Soccer league 专路 endpoint | `soccer/epl` / `soccer/laliga` 等专路全 0 字节 | 必须走 `soccer/home` + 客户端 league_id 过滤 | ETL 不能假设专路可用 | 小余 ETL 修订 |
| Esports 专路 (lol/dota2/valorant) | `esports/home` 是唯一数据源 (当下 CS GO), 专路全 0 | dispatch 从 home 做客户端 sport 过滤 | | 小余 ETL |
| MMA schedule 拼写 (单 c `shedule`) | `mma/schedule` 双 c 才是正确路径 (反惯例) | mma 是 Goalserve 里唯一用双 c 的 sport | ETL endpoint registry 必须硬编码 | 已标注 v2 |
| `?lastupdate=` 增量参数 | 真正增量参数是 `?ts=` (pregame getodds), inplay 无增量 (每秒全量 gz) | v1 测 `lastupdate` 全失败 → 判无增量 → 错, 是参数名错 | pregame 增量已 v3 修正; inplay 无增量 (秒级全量) | 已修正 |

---

## §7 与 Polymarket 数据结构 mapping

### §7.1 三层 ID 关系

```
Goalserve                         Polymarket
---------                         ----------
pregame_match_id (6921246)  ←→   condition_id
inplay_match_id  (134180558) ←→   condition_id (同一场)
  两者通过 inplay-pregame mapping 端点关联:
  www.goalserve.com/getfeed/<KEY>/soccernew/inplay-mapping
  → { @pregame_match_id, @inplay_match_id, @pregame_team1_id, @pregame_team2_id,
      @inplay_team1_id (name!), @inplay_team2_id (name!) }

注意: inplay team id 是 name 字符串 (非 numeric id), pregame team id 是 numeric
```

**5 sport mapping endpoint**:

| Sport | URL |
|---|---|
| Soccer | `getfeed/<KEY>/soccernew/inplay-mapping?json=1` |
| Tennis | `getfeed/<KEY>/tennis_scores/inplay-mapping?json=1` |
| Baseball | `getfeed/<KEY>/baseball/inplay-mapping?json=1` |
| Esports | `getfeed/<KEY>/esports/inplay-mapping?json=1` |
| Basketball | `getfeed/<KEY>/basketball/inplay-mapping?json=1` (实测 500, 待重测 `bsktbl/` 路径) |

### §7.2 Outcome → Token 三层 lookup

```
Goalserve odds                   Polymarket
--------------                   ----------
outcome name ("Home")
  + market_id (1 = 1X2)
  + inplay_match_id (134180558)
         |
         v
  [Mapping Layer]
         |
         v
  condition_id (Polymarket, 对应该场比赛)
  + outcome ("Home Win" or "Away Win" or "Draw")
         |
         v
  token_id (YES token of that condition)
         |
         v
  Polymarket CLOB price (0-1)
```

**价格单位换算**:

| 来源 | 单位 | 含义 | 转换 |
|---|---|---|---|
| Goalserve `value_eu` | decimal odds (e.g. `"8.5"`) | 欧赔 (含 bookmaker margin) | implied_prob = 1 / value_eu |
| 9 bookmakers 均值去 vig | fair probability | de-vig 后公平概率 | ADR-008 multiplicative |
| Polymarket price | probability 0-1 | market 最新 mid price | best_bid + best_ask / 2 |

**Edge 计算**: `edge = abs(fair_p - polymarket_mid)`. 如果 `edge > C2_threshold` → signal.

### §7.3 3-way Soccer → Polymarket binary

Polymarket soccer 市场通常以独立 binary condition 存在:

| Goalserve outcome | Polymarket condition 例 | token |
|---|---|---|
| `Home` (value_eu 对应 p_home) | `"Will [Team A] win [Match]?"` | YES token |
| `Draw` (value_eu 对应 p_draw) | `"Will [Match] end in a draw?"` | YES token |
| `Away` (value_eu 对应 p_away) | `"Will [Team B] win [Match]?"` | YES token |

三个 condition 各自独立 binary. 约束: de-vig 后 `p_home + p_draw + p_away ≈ 1.0`.

若 Polymarket 只开了 "Home/Away" 两个 condition (不开 Draw), 则需从 pregame 3-way fair_p 重算 binary fair_p:
- `fair_p_home_binary = p_home / (p_home + p_away)`
- `fair_p_away_binary = p_away / (p_home + p_away)`

---

## §8 派单 backlog

| 派单 | 负责人 | 截止 | 依据 |
|---|---|---|---|
| 工程 ABI: FairValue / SignalIntent struct 加 outcome 维度, 支持 3-way soccer (Home/Draw/Away 各自一个 fair_p) | 老周 | W8 W5 | §4.2 + §6 gap |
| alpha v2 confirm: P0-01/P0-02 是否涉及 3-way sport (soccer 占比), 策略层 FairValue 接口是否需要同步修改 | 老彭 | W8 W5 | §4.2 |
| ADR-008 §5 inplay 例外条款: 描述单源 bet365 降级 + 精度损失声明 + C2 门槛上调依据 + 未来升级路径 | 老郭 | W8 W5 | §6 gap + 老彭 OQ-P02-3 ack |
| ADR SSOT 锁定: 将本文 (data-structure SSOT) 作为数据结构决策的权威来源, 后续变更必须更新本文 + 通知下游 | 老雷 GM | W8 W5 ack | 老板 verbatim |
| ETL: inplay client 接入 inplay.goalserve.com (EU Sofia), 与 www.goalserve.com pregame 分开 code path | 小冯 | W8 W5 | §2.1 + §6 |
| ETL: pregame getodds 增量 ts 协议 + 去元音 key 双 schema parser | 小余 | W8 W5 | §3.3 + v3 ETL-3 |
| 工程: inplay-pregame mapping 5 sport 双向索引 (pregame_match_id ↔ inplay_match_id) | 小余 | W8 W5 | §7.1 |

---

## §9 不耻下问 (协作请求)

**@老李 (Polymarket SSOT)**:
- §7 mapping 需要你 confirm: Polymarket soccer condition 是否标准拆成 3 个独立 binary (Home/Draw/Away)? 还是有些市场是 outright 3-way?
- condition_id ↔ Goalserve pregame_match_id 的 join key 是赛事名称字符串匹配还是有数字 ID 一一对应?
- 截止: W8 W3 联动 doc

**@老周 (工程 ABI)**:
- FairValue struct 当前是单值. §6 gap: soccer 3-way 需要 per-outcome 三值 (或 map<outcome_name, float>)
- SignalIntent 同上. 请 ack 或提反设计 (若有更好方案)
- 截止: W8 W5 ABI 决策

**@老彭 (alpha v2 confirm)**:
- §4 确认: P0-01 PinnacleNoVig 当前用的是 pregame getodds 9-家均值, 是否覆盖 soccer 3-way? (soccer 占 Polymarket 体育比例多少?)
- inplay 单源 bet365 de-vig 已老彭 OQ-P02-3 ack. 本 §4.2 soccer 3-way 是否影响你的 C2 门槛设计?
- 截止: W8 W5

**@老郭 (ADR-008 §5)**:
- §6 gap: ADR-008 §5 inplay 例外条款需要你在 W8 W5 立 ADR. 内容见老彭 OQ-P02-3 ack §5.2
- 本文 §4.2 3-way mapping 逻辑是否触发 ADR-008 v1.1 需求? (3-way 的 multiplicative de-vig 公式与 binary 不同)

**@老雷 GM (ack)**:
- Goalserve 完整数据结构 SSOT §1-9 交付. 数据源关系图 (§2.1 四域名), 3-way mapping (§4.2), R-20 时间戳来源 (§5), 工程 gap 清单 (§6), 派单 backlog (§8) 已齐.
- 请 ack: 本文作为 Goalserve 数据结构决策 SSOT, 后续变更必须更新本文.

---

**最后更新**: 2026-05-29 by 小段 (Wave 40 P0, 老板 verbatim 触发)
