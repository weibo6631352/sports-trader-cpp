# Goalserve 全 Endpoint 复用率矩阵 v2

- Owner: 小段
- Date: 2026-05-28
- 测试窗口: 2026-05-28 NBA 季后赛结束次日, MLB 正赛季, 法网 (Roland Garros) 进行中, IPL 收尾, PGA 进行中, F1 间歇期
- 验收人: 老雷 + 小邓 (ETL) + 小余 (ETL)
- 关联: `xiaoduan-goalserve-api-spec-v1.md` (v1, 覆盖 5 sport), 长期 sweep 政策 ADR `2026-05-28-gm-policy-api-monitoring-longterm.md`
- 原始数据: `docs/RESEARCH/data/xiaoduan-goalserve-v2-probe-20260528-133314.txt` + `xiaoduan-goalserve-samples/`
- 探针总次数: 219 个 HTTP 请求, 跨 17 sport / 16 类 endpoint family

---

## 0. v1 -> v2 覆盖增量

v1 仅 5 sport (basketball/baseball/football/soccer/tennis) 8 endpoint family.
v2 新增 / 修订:

| 类别 | v1 状态 | v2 状态 |
|---|---|---|
| sport 数 | 5 | 17 (含 esports/MMA/golf/cricket/F1/horse-racing/darts/snooker/handball/volleyball/rugby/badminton/tabletennis) |
| endpoint family | 8 | 16 (新增 fighters / standings / drivers / leaderboard / d-1 历史 / worldcup / pga / racing-uk / soccernew/leagues) |
| odds 权限 | 假定可用 | **实证: 全 sport, 所有 odds endpoint = 0 字节 (无授权)** |
| schedule 拼写 | 未知 | 实证 `shedule` 单 `c` 正确, `schedule` 双 `c` 多数 500 |
| 序列化策略 | json 优先 | XML / JSON / XML-with-`?json=1` 混合, 见 §5 |
| inplay 复用 | 假定 sport 全可用 | 实证 8 sport inplay 返回真 XML 空壳 (非 500), 5 sport 0 字节 |

---

## 1. 当下覆盖率 (含 OUT-OF-SEASON 标记)

按季节, 截至 2026-05-28:

| Sport | 当下状态 | 主可用 endpoint | 备注 |
|---|---|---|---|
| MLB | IN-SEASON | `baseball/usa` (29-161KB), `baseball/mlb-scores` | 主力数据源, 已 4 次抓样确认稳定 |
| WNBA | IN-SEASON | `bsktbl/wnba-scores` (8.9KB), `bsktbl/wnba-shedule` (13.9KB / 抓样 162KB) | NBA 替代窗口 |
| Tennis (法网) | IN-SEASON | `tennis/home` (12-90KB), `tennis/atp` / `tennis/wta` (4.4KB) | home 含 inplay+today 整合, atp/wta 是排名 |
| Cricket (IPL) | IN-SEASON 收尾 | `cricket/livescore` (46-269KB), `cricket/home` | livescore 是唯一可用 endpoint, 其他全 500 |
| Golf (PGA) | IN-SEASON | `golf/pga` (2.6-8.4KB) | PGA 2024 schedule 含 winner, lpga/european/masters 全 0 字节 |
| F1 | 间歇期 | `f1/drivers` (958-2569B) 唯一非空 | `f1/home`/`shedule`/`results`/`standings` 全 168 字节 XML `<scores />` 空壳 |
| MMA (UFC) | IN-SEASON | `mma/schedule` (14-71KB), `mma/fighters` (32-111KB) | `mma/shedule` (单c) 反而 0 字节, **mma 用 double c**, 反 schedule 拼写惯例 |
| Esports | IN-SEASON | `esports/home` (4.3-30KB) CS GO 比赛 | `esports/csgo`/`lol`/`dota2`/`valorant` 全 0 字节, 数据要从 home 里 dispatch |
| MLS | IN-SEASON | 走 `soccer/home` / `soccernew/home` 混合, **专路 `soccer/mls` 0 字节** | 必须从 soccer/home XML 里 grep MLS league |
| NBA | OUT-OF-SEASON (季后赛刚结束), retest 2026-10 | `bsktbl/nba-shedule` (116-755KB, 新季 schedule), `bsktbl/nba-standings` (2.2-8.1KB) | scores 仅 265 字节空壳 |
| NHL | OUT-OF-SEASON (季后赛刚结束), retest 2026-10 | `hockey/nhl-shedule` (600KB-3.7MB), `hockey/nhl-standings` (2.6-10.7KB) | scores 3.7KB 残值, schedule 是新季表 |
| NFL | OUT-OF-SEASON, retest 2026-09 | `football/nfl-scores` (4-21KB), `football/nfl-standings` (2-8.9KB) | scores 是上季余韵, shedule 仅 201B |
| NCAA football | OUT-OF-SEASON, retest 2026-09 | 全 0 字节 / 500 | 待秋季重测 |
| NCAA basketball | OUT-OF-SEASON, retest 2026-11 | `bsktbl/ncaa-shedule` (540KB-3.2MB) 新季 schedule | scores 261B 空 |
| EPL/La Liga/Bundesliga/Champions/Europa | OUT-OF-SEASON, retest 2026-08 | 全 0 字节 (`soccer/epl` 等专路全空) | 用 `soccer/home`+`soccernew/home` 兜底, league filter 走客户端 |
| Boxing | 散在, 无固定季 | `boxing/home` 188B XML 空, retest monthly | 真空响应非 500, schedule 也 500 |
| 世界杯预选 | 散在 | `soccer/worldcup` (39-340KB) | 含 qualification 比赛 |
| KHL (俄罗斯冰球) | OUT-OF-SEASON, retest 2026-09 | `hockey/khl` 239B 空壳 | 待重测 |
| Badminton / Snooker / Darts | tournament 驱动 | `*/home` 1-7KB, `*/inplay` 多数 0 | 用 home 兜底 |
| NPB (棒球日本) | IN-SEASON | `baseball/japan` (5.6-43.7KB) | v1 漏 |
| KBO (棒球韩国) | IN-SEASON | `baseball/korea` (3.3-24.1KB) | v1 漏 |
| UK 赛马 | IN-SEASON | `racing/uk` (159KB-1.4MB) | 单 endpoint 全数据, 巨型 payload |

**当下完全可订阅 sport 数: 11** (MLB / WNBA / Tennis / Cricket / Golf / MMA / Esports / MLS / NPB / KBO / UK赛马) + F1 半可用 + 散在 (Boxing/世界杯/handball/volleyball/rugby/badminton/darts/snooker home 兜底)

---

## 2. 全 sport x endpoint 矩阵

格式: `endpoint -> bytes (status)`. `500` = 服务端错误, `0B` = 200 但空 body, `XML empty` = 200 但 XML 真空壳 (e.g. `<scores/>`).

### 2.1 Basketball (bsktbl)

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `bsktbl/inplay` | 107-217 | XML empty | 季外, 含 `<scores/>` |
| `bsktbl/home` | 1.2-4.2KB | OK | 含跨联赛汇总 |
| `bsktbl/nba-scores` | 190-265 | XML empty | 季后赛结束 |
| `bsktbl/nba-shedule` | 116KB-755KB | OK | 新季 schedule, **单 c 拼写** |
| `bsktbl/nba-schedule` | - | 500 | 双 c 拼写错 |
| `bsktbl/nba-standings` | 2.2-8.1KB | OK | 新增, v1 漏 |
| `bsktbl/wnba-scores` | 8.9-52.7KB | OK | WNBA 在赛 |
| `bsktbl/wnba-shedule` | 13.9-162KB | OK | |
| `bsktbl/ncaa-scores` | 178-261 | XML empty | 季外 |
| `bsktbl/ncaa-shedule` | 540KB-3.2MB | OK | 新季全表, 巨型 |
| `bsktbl/euroleague` | - | 500 | 不可用 |
| `bsktbl/d-1` | 5-20KB | OK | **新发现, 前一天历史, v1 漏** |
| `bsktbl/livescore`/`results`/`nba-players/teams/roster/injuries/stats/leaders/h2h/odds/getodds` | 1.7KB | 500 | 全无授权 |

### 2.2 Football (NFL/NCAA)

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `football/inplay` | 107-219 | XML empty | 季外 |
| `football/home` | 82-192 | XML empty | |
| `football/nfl-scores` | 4-21KB | OK | 上季余韵 |
| `football/nfl-shedule` | 97-208 | XML empty | 新季未出 |
| `football/nfl-standings` | 2-8.9KB | OK | |
| `football/nfl-players/injuries/stats/leaders/rankings` | 0B | empty | 无授权或季外 |
| `football/ncaa-scores`/`ncaa`/`results`/`livescore` | 0B | empty | 季外 |
| `football/ncaa-shedule` | - | 500 | |

### 2.3 Baseball

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `baseball/inplay` | 82-192 | XML empty | inplay 入口空 (用 usa 替代) |
| `baseball/home` | 408-742 | OK | 跨联赛 |
| `baseball/usa` | 29-161KB | OK | **MLB 主数据源**, 同 mlb-scores |
| `baseball/mlb-scores` | 29-161KB | OK | 同 usa, 别名 |
| `baseball/japan` | 5.6-43.7KB | OK | **NPB, v1 漏** |
| `baseball/korea` | 3.3-24.1KB | OK | **KBO, v1 漏** |
| `baseball/mlb-shedule/standings/players/injuries/stats/leaders/pitchers/batters/milb/odds/getodds` | 1.8KB | 500 | 全 500, 无授权 |

### 2.4 Soccer

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `soccer/inplay` | 276-327 | XML empty | 真空壳 |
| `soccernew/inplay` | 276-327 | XML empty | 新版别名, 行为同 |
| `soccer/home` | 21KB-145KB | OK | **主聚合源** |
| `soccernew/home` | 18KB-114KB | OK | 新版, JSON |
| `soccer/d-1` | 72KB-455KB | OK | 前一天比赛, 大批量 |
| `soccer/worldcup` | 39KB-340KB | OK | **新发现, qualification 比赛** |
| `soccernew/leagues` | 10-54KB | OK | league directory, 给 ETL 做 league enum |
| `soccernew/1204` | - | 500 | league-id 单查走 home 过滤 |
| `soccer/epl/laliga/bundesliga/mls/champions-league/europa-league` | 0B | empty | **所有专路 0 字节, 必须走 home 客户端过滤** |
| `soccer/livescore`/`standings`/`shedule`/`schedule`/`comments`/`getodds` | 0B | empty | 无授权 |

### 2.5 Hockey

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `hockey/inplay` | 283-336 | XML empty | 季后赛刚结束 |
| `hockey/home` | 2.1-9.4KB | OK | |
| `hockey/nhl-scores` | 3.7-20.5KB | OK | 残值 |
| `hockey/nhl-shedule` | 600KB-3.7MB | OK | 新季 schedule, **巨型** |
| `hockey/nhl-standings` | 2.6-10.7KB | OK | |
| `hockey/khl` | 153-239B | XML empty | 季外 |
| `hockey/nhl-players/injuries/stats/leaders`/`livescore`/`getodds` | 1.7KB | 500 | 无授权 |

### 2.6 Tennis

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `tennis/home` | 12-90KB | OK | **主源, 含 inplay + 当日 + ATP/WTA**, 数据非常密 |
| `tennis/atp` | 4.4-15.8KB | OK | ATP 排名 |
| `tennis/wta` | 4.4-15.8KB | OK | WTA 排名 |
| `tennis/inplay`/`livescore`/`atp-shedule`/`wta-shedule`/`results`/`tournaments`/`d-1`/`getodds` | 1.7KB | 500 | 全 500 |

### 2.7 Cricket

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `cricket/livescore` | 46KB-269KB | OK | **唯一可用 endpoint**, IPL 在内 |
| `cricket/home` | 618B-1.7KB | OK | 小, 用作发现 |
| `cricket/inplay`/`shedule`/`fixtures`/`squads`/`ipl`/`icc`/`rankings`/`getodds` | 1.7KB | 500 | 全 500 |

### 2.8 Rugby

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `rugby/inplay` | 280-328 | XML empty | |
| `rugby/home` | 437-574B | OK | 小聚合 |
| `rugbyleague/home` | 430-572B | OK | rugby league 另一分支 |
| `rugbyleague/inplay` | 1.7KB | 500 | |
| `rugby/livescore`/`shedule`/`getodds` | 1.7KB | 500 | 全 500 |

### 2.9 Handball / Volleyball

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `handball/inplay` | 105-213 | XML empty | |
| `handball/home` | 463-900B | OK | |
| `volleyball/inplay` | 107-214 | XML empty | |
| `volleyball/home` | 78-187B | XML empty | |
| 两 sport 的 `livescore`/`shedule`/`getodds` | 1.7KB | 500 | 全 500 |

### 2.10 小众 indoor (badminton / tabletennis / snooker / darts)

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `badminton/home` | 1.9-11.3KB | OK | |
| `badminton/inplay` | 0B | empty | |
| `tabletennis/inplay` | 479-562B | OK | 小活动 |
| `tabletennis/home` | 476-560B | OK | |
| `table-tennis/home` (dash) | 476-560B | OK | 别名 |
| `snooker/home` | 1.3-7KB | OK | |
| `snooker/inplay` | 0B | empty | |
| `darts/home` | 1KB-7.1KB | OK | |
| `darts/inplay` | 0B | empty | |

### 2.11 Boxing / MMA / UFC

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `boxing/home` | 75-188B | XML empty (`<scores/>`) | 真空壳, 非 500, 无近期比赛 |
| `boxing/shedule`/`schedule`/`fighters`/`results`/`getodds` | 1.8KB | 500 | |
| `mma/schedule` (**双 c**) | 14-71KB | OK | **UFC + 其他 MMA 全 schedule** |
| `mma/shedule` (单 c) | 0B | empty | **反惯例, 这里单c 才是空** |
| `mma/fighters` | 32-110KB | OK | **全 fighter directory** |
| `mma/home` | 77-188B | XML empty | |
| `mma/results/rankings/getodds` | 0B | empty | |
| `ufc/schedule` | 481-564B | OK | UFC 专路 |

### 2.12 Golf

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `golf/pga` | 2.6-8.4KB | OK | **PGA 唯一可用**, 含 winner |
| `golf/lpga` | 107-208B | XML empty | |
| `golf/home`/`inplay`/`leaderboard`/`european`/`masters`/`tournaments`/`shedule`/`players`/`rankings`/`getodds` | 0B | empty | 全 0 |

### 2.13 Motorsport

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `f1/drivers` | 958B-2.5KB | OK | **F1 唯一非空 endpoint** |
| `f1/home`/`shedule`/`schedule`/`results`/`standings`/`races`/`livetiming`/`getodds` | 50-168B | XML empty (`<scores/>`) | 间歇期 |
| `formula1/home` | 476-560B | small | 别名 |
| `nascar/*` | 1.8KB | 500 | 全无 |
| `motogp/*` | 1.8KB | 500 | 全无 |
| `indycar/home` | 476-560B | small | |

### 2.14 Esports

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `esports/home` | 4.3-30KB | OK | **CS GO 比赛, league + match level**, v1 漏 |
| `esports/inplay`/`shedule`/`lol`/`csgo`/`cs2`/`dota2`/`valorant` | 0B | empty | 全 0, 数据要从 home dispatch |
| `esoccer/inplay` | 479-562B | OK | esoccer 单独可用 |

### 2.15 Horse racing / others

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `racing/uk` | 159KB-1.4MB | OK | **巨型, UK 赛马全数据** |
| `horseracing/home`/`horse-racing/home` (dash 别名) | 476-560B | small | 小聚合 |
| `racing/home` | 0B | empty | |
| `greyhound/home` | 476-560B | small | |
| `athletics/home` | 476-560B | small | |
| `cycling/home` | 0B | empty | |

### 2.16 通用 odds endpoint

| Endpoint | Bytes | Status | 备注 |
|---|---|---|---|
| `getodds/{sport}` (跨 17 sport) | 0B | empty | **全部 0 字节, 见 §4** |
| `getodds/soccer?bookmakers=bet365` | 0B | empty | bookmaker 过滤无效 |
| `getodds/soccer?bookmakers=pinnacle` | 0B | empty | |
| `odds/soccer`/`basketball`/`feed` | 65-181B | XML empty | 路径前缀 `odds/` 也无 |

---

## 3. 复用率排名 Top 10

按 "可用 + 信息密度 + 跨日有效" 排序:

| 排名 | Endpoint | 用途 | 字节级 | 复用价值 |
|---|---|---|---|---|
| 1 | `baseball/usa` (= `baseball/mlb-scores`) | MLB 主源 | 29-161KB | 当下季最关键, MLB 全 inplay+today |
| 2 | `tennis/home` | 网球主源 (含 inplay + 当日 + 排名) | 12-90KB | 法网期间数据非常密 |
| 3 | `cricket/livescore` | 唯一 cricket 可用 | 46KB-269KB | IPL 期间饱满 |
| 4 | `soccer/home` + `soccernew/home` | 足球主聚合, 客户端 league 过滤 | 18-145KB | 跨联赛单 endpoint |
| 5 | `bsktbl/nba-shedule` | NBA 新季 schedule (静态) | 116KB-755KB | TTL 长, 跨日复用极高 |
| 6 | `hockey/nhl-shedule` | NHL 新季 schedule | 600KB-3.7MB | 同上, 巨型 |
| 7 | `mma/fighters` | UFC 全 fighter directory | 32-110KB | TTL 长, fighter master |
| 8 | `mma/schedule` (双c) | UFC + MMA 全 schedule | 14-71KB | 在赛期主源 |
| 9 | `bsktbl/wnba-scores` + `bsktbl/wnba-shedule` | WNBA 在赛 | 8.9-162KB | NBA 替代窗口 |
| 10 | `racing/uk` | UK 赛马 | 159KB-1.4MB | 单 endpoint 全数据 |

亚军 (常驻可用但低密度): `bsktbl/d-1` 历史 (5-20KB), `soccer/d-1` 历史 (72-455KB), `tennis/atp`+`wta` 排名, `golf/pga`, `esports/home`, `baseball/japan`+`korea`, `soccer/worldcup`.

---

## 4. odds endpoint 权限实证

**结论: 所有 odds endpoint 在当前授权下不可用.** 涉及 33 个变体, 全 0 字节 或 65-181B XML 空壳 (非 500). 无论:

- per-sport getodds (`bsktbl/nba-odds`, `baseball/getodds`, `tennis/getodds`, ...) -> 多数 500
- global getodds (`getodds/basketball`, `getodds/soccer`, ...) -> 0B
- 路径前缀 `odds/{sport}` -> 65-181B XML 空壳
- bookmaker 过滤参数 (`?bookmakers=bet365` / `pinnacle`) -> 不改变结果

**实证**: 200 OK + 0 字节 (或空 XML root) 表示账户无 odds feed 权限, 与 500 (path 错) 不同语义.

**给老雷的建议**: 不要在 v2 阶段假设 Goalserve odds 可用. 我们项目核心是 Polymarket 价格, Goalserve odds 是补盘. 若日后需要 odds, 必须先升级 Goalserve plan -> 见 §10 sales 清单.

---

## 5. 序列化矩阵

Goalserve URL 默认 XML, 加 `?json=1` 转 JSON. 本次探针默认 `json=1`, 实证 3 类响应:

| 序列化类型 | 典型 endpoint | 处理 |
|---|---|---|
| pure JSON | `baseball/usa?json=1`, `bsktbl/nba-shedule?json=1`, `tennis/home?json=1`, `cricket/livescore?json=1` | 直接 nlohmann::json parse |
| XML -> JSON@prefix (Goalserve 把 XML 字段名加 `@` 前缀转 JSON) | `soccer/home?json=1`, `soccernew/home?json=1`, `hockey/nhl-shedule?json=1` | 字段读 `["@attr"]`, 用 jq-like 路径解析 |
| pure XML (即使加 `?json=1`) | `soccer/inplay`, `f1/home`, `boxing/home`, 所有 `<scores/>` 真空壳 | 改回 pugixml / tinyxml 解析, fallback |

**给小邓 ETL 注意**: 同 sport 不同 endpoint 序列化可能不一致 (e.g. soccer/home XML, soccernew/home JSON). 必须 endpoint-level 路由解析器, 不能 sport-level.

---

## 6. 命名陷阱清单

| 陷阱 | 正确拼写 | 错误拼写 | 实证 |
|---|---|---|---|
| schedule (单 c) | `nba-shedule`, `nfl-shedule`, `nhl-shedule`, `wnba-shedule`, `ncaa-shedule`, `atp-shedule`, `wta-shedule` | `*-schedule` (双 c) | 多数双 c 返回 500 或空 |
| MMA 反例 | `mma/schedule` (**双 c**), `ufc/schedule` (**双 c**) | `mma/shedule` (单 c) -> 0B | **唯一反惯例** sport |
| 连字符 vs 下划线 | `nba-scores`, `nba-shedule`, `nfl-scores` | `nba_scores` (未测, 多半 500) | 全连字符 |
| dash 别名 | `horse-racing/home` == `horseracing/home`, `table-tennis/home` == `tabletennis/home`, `formula1/home` == `f1/home` (近似) | - | 两种都活, 但 size 一致 |
| `soccer/` vs `soccernew/` | 同时存在, 行为略不同 (soccernew 返回 JSON 更干净), 数据基本同源 | - | inplay 都是空壳 276B |
| odds 路径前缀 | `odds/{sport}` 65-181B 空, `getodds/{sport}` 0B, `{sport}/{league}-odds` 500 | - | 全无授权 |

---

## 7. 推荐 TTL 矩阵

| Endpoint 类 | 当下 TTL | 季外 TTL | 备注 |
|---|---|---|---|
| `*/inplay`, `*/livescore` | 10-15s | 跳过 | inplay 在赛季 |
| `*/home` 聚合 | 30-60s | 5-15min | 季外极少变化 |
| `*/{league}-scores` | 30s | 5min | 季后赛后 freeze |
| `*/{league}-shedule` (新季表) | 6h | 24h | 巨型 (NBA 755KB, NHL 3.7MB), TTL 长 |
| `*/{league}-standings` | 5min | 30min | |
| `mma/fighters` | 24h | 24h | master data |
| `mma/schedule` | 1h | 6h | event 前 1h 加密 |
| `tennis/atp` / `wta` rankings | 24h | 24h | 周更 |
| `racing/uk` (1.4MB) | 5min in-day | - | 当天比赛, 跨日重抓 |
| `cricket/livescore` | 15s | 30min | IPL 期 |
| `soccer/d-1` 历史 | 6h | 24h | |
| `bsktbl/d-1` 历史 | 6h | 24h | |
| `*/getodds`, `getodds/*` | **不抓** | **不抓** | 全 0, 抓也无用 |
| `soccernew/leagues` | 24h | 24h | league enum master |

**带宽估算**: 当下季 11 sport 同时跑 inplay (10s) + schedule (6h) + home (30s) + standings (5min), 单实例峰值 ~3-5 Mbps in. 跨洋链路压力可控.

---

## 8. 给小余 ETL 必做项 v2

1. **endpoint-level 解析器路由**: 不能 sport-level. 同 sport 的 home (XML) 和 inplay (JSON 或空壳) 必须分路.
2. **空壳识别**: 200 OK + body 长度 < 500B 且符合 `<scores/>` / `<results/>` / `{}` 模式 -> 视作 "true empty" 而非错误, 不重试.
3. **500 vs 0B vs XML-empty 三态**: 分别 alerting, 500 是路径错, 0B 是无授权, XML-empty 是季外, 三种处理不同.
4. **MMA 拼写双 c 单 c 反惯例**: 在 endpoint registry 里硬编码 `mma/schedule` 双 c, 其他全单 c.
5. **soccer league 过滤走客户端**: 所有 `soccer/{league}` 专路 0 字节, 用 `soccer/home` + `soccernew/home` + `soccernew/leagues` 做发现, 客户端 filter league_id.
6. **esports dispatch**: `esports/home` 是唯一源, 按 league name (CS GO / Dota2 / LoL / Valorant) 分流, 不要查专路 endpoint.
7. **大 payload streaming**: NCAA-shedule 3.2MB, NHL-shedule 3.7MB, racing/uk 1.4MB, 必须 streaming parse + gzip on, 不要 eager load.
8. **跨日 d-1 历史补丁**: `bsktbl/d-1` 和 `soccer/d-1` 跑日终回补, 弥补 inplay 中断窗口.
9. **NPB/KBO 接入**: `baseball/japan` + `baseball/korea` 与 MLB 同 family, 套用相同 parser.
10. **lastupdate 增量协议** (实证): `?lastupdate=<ts>` 当前返回不变 (NBA/MLB 实测同 size), 暂时不依赖, 先全量抓 + 客户端 diff. 待 Goalserve 确认协议见 §10.

---

## 9. 待长期 sweep 补测 (off-season sport)

按 GM policy ADR, 长期监控政策 + 季节 retest. 优先级排序:

| Sport / Endpoint | retest 月 | 重要性 | 备注 |
|---|---|---|---|
| NBA (`bsktbl/nba-scores`, `bsktbl/inplay`) | **2026-10** | 极高 | Polymarket 篮球盘开盘前 |
| NFL (`football/nfl-scores`, NFL inplay) | **2026-09** | 极高 | Polymarket 美橄主战场 |
| NHL (`hockey/nhl-scores`, `hockey/inplay`) | **2026-10** | 高 | 季前赛 |
| EPL / La Liga / Bundesliga / Champions / Europa (专路 + soccer/home) | **2026-08** | 极高 | 五大联赛开赛 |
| NCAA football (`football/ncaa*`) | **2026-09** | 中 | 大学橄榄 |
| NCAA basketball (`bsktbl/ncaa-scores`) | **2026-11** | 中 | 大学篮 |
| KHL (`hockey/khl`) | **2026-09** | 低 | 俄罗斯冰球 |
| MotoGP / NASCAR (`*/home`, `*/shedule`) | **2026-07** | 中 | 实证全 500, 待重测 |
| Boxing (`boxing/home`, `boxing/shedule`) | monthly | 中 | 散在, monthly retest |
| Golf European / Masters / LPGA | **2026-07** | 低 | 当下全 0, 锦标周可能开 |
| Esports lol/dota2/valorant 专路 | **2026-06** | 中 | 月内 major 期可能开 |
| Cycling (`cycling/home`) | **2026-07** (Tour de France 前) | 低 | 0 字节 |

**长期 sweep job 设计**: 每周一次, 跑全 §2 矩阵的 endpoint set, 生成 size diff 报告, 任何 0B -> non-0B 转换告警, 自动重测 retest 月列表. owner: 小余 ETL.

---

## 10. 开放问题 + 给 Goalserve sales 清单

### 10.1 开放问题 (技术)

1. `lastupdate` 协议: 实证 NBA-shedule + MLB-usa 加 `?lastupdate=` 后 size 完全一致 (116020/29954), 不像增量协议. **待问**: 是 server-side 实现, 还是必须配 `If-Modified-Since` header?
2. `soccer/home` 默认 XML, `soccernew/home` 默认 JSON. **两者数据源是否一致?** soccernew 是否替代品?
3. `mma/schedule` 双 c, `mma/shedule` 单 c 0 字节. **是 typo 还是新旧路径并存?**
4. `esports/home` 当前只有 CS GO, 是否需要 league_id 参数才能拿 lol/dota2/valorant?
5. 大 payload (NBA-shedule 755KB, NHL-shedule 3.7MB) 是否有 gzip + chunked, 探针 curl 没明确量化, 待 wire 同事验.

### 10.2 给 Goalserve sales 的清单 (老雷转发)

1. **odds feed 升级**: 实证当前账号所有 odds endpoint 0 字节, 需要明确报价 + 包含哪些 bookmaker (Pinnacle / bet365 / 等), 与 v1 推算差距大.
2. **lastupdate 协议规范**: 请发文档, 是 query param 还是 If-Modified-Since header, 服务器端实现细节.
3. **NCAA football / NCAA basketball** scores endpoint 季外 0B 是预期还是 bug? 是否有 `*-shedule` 之外的 player-level prop data?
4. **F1 / NASCAR / MotoGP**: 当前几乎只有 `f1/drivers` 可用, 期望中应有 `f1/live-timing`, `nascar/results`, 是否需升级.
5. **Boxing**: `boxing/home` 188B XML 空, `boxing/shedule` 500. 请确认是否覆盖近期 fight card.
6. **Golf**: `golf/pga` 唯一可用, 期望 `golf/leaderboard` (赛中) + `golf/european` (DP World Tour) + `golf/masters` (大满贯). 当前 0B.
7. **Esports**: 当前 `esports/home` 只 CS GO, 期望按 game (`/lol`, `/dota2`, `/valorant`) 分路. 请确认 plan 是否覆盖.
8. **KHL / J-League / 其他海外联赛**: 当前几乎只有 NPB/KBO (baseball) 和 worldcup (soccer), 其他海外联赛 (J-League, K-League, A-League 等) 是否覆盖.

---

## 附录 A. 测试方法学

- 探针: curl + http proxy 127.0.0.1:7890, `--max-time 15`, 每 endpoint 1-4 次抓样 (inplay 4 次确认刷新).
- timestamp: 2026-05-28 13:33:14 UTC+8 .
- raw dump: `/tmp/xiaoduan_v2_20260528-133314` + `docs/RESEARCH/data/xiaoduan-goalserve-samples/`.
- 探针总数: 219 HTTP 请求. 完整结果: `docs/RESEARCH/data/xiaoduan-goalserve-v2-probe-20260528-133314.txt`.
- 不在 v2 范畴 (派别人):
  - wire 层 latency/budget -> 老蒋
  - Polymarket-side endpoint -> 老李
  - 字段语义 (e.g. "first innings", "OT period") -> 体育专家

## 附录 B. v1 -> v2 文档关系

- v1 (`xiaoduan-goalserve-api-spec-v1.md`) 保留, 作 5 sport 字段级 spec 基线.
- v2 (本文) 是 **endpoint matrix**, 覆盖广度优先, 字段细节走 v1 + 长期 sweep 增量.
- 后续 v2.1: 季节重测后增量, 不重写.
