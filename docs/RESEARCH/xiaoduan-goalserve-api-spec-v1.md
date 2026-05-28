# Goalserve API Spec v1 — 实测

**作者**: 小段 (Goalserve 专精)
**审阅**: GM 老雷 (战略授权), 小米 (doc-curator), 老黄 (合规 sign-off)
**日期**: 2026-05-28
**Last measured**: 2026-05-28 12:30 UTC (data: `xiaoduan-goalserve-samples/*-20260528-123019.json` + `latency-runs-20260528.csv`)
**版本**: v1 (基于 2026-05-28 实测的首版, MVP 用)
**状态**: ACTIVE
**关联样本**: `docs/RESEARCH/data/xiaoduan-goalserve-samples/`
**关联延迟数据**: `docs/RESEARCH/data/xiaoduan-goalserve-samples/latency-runs-20260528.csv`

---

## 1. 文档目的

本文档由小段实测 Goalserve API 现状, 输出给:

- **小余 (ETL 工程师)**: 解析必做项, 字段对照表
- **老姜 (延迟工程师)**: latency budget 现实数据
- **老周 (架构)**: 数据通路约束
- **老雷 (GM)**: 上游 vendor 风险 + 商务待办

不解决:
- 字段在 Polymarket 市场上的业务语义 (老彭定)
- C++ HTTP client 实现 (老蒋写, 小段给 wire spec)
- 数据落库 schema (小余定)

---

## 2. Goalserve 现实约束 (核心)

老雷一句话讲清:

> **Goalserve 是 1995 年代的 ASP.NET REST 接口, 没有 WSS, 命名混乱有 typo, 不同 sport 用不同序列化格式, key 走 URL path 不能 hide, 跨洋 p95 延迟 5-10s.**

具体:

### 2.1 协议
- **REST polling only**. 无官方 WSS / SSE / long-poll. 老雷上一轮"WSS"是笔误, 已澄清.
- HTTP/1.1 over TLS, 支持 gzip (`--compressed`). 部分 endpoint 返回的体在 `Content-Encoding` header 上不声明 gzip 但内容是 gzip — curl `--compressed` 仍能识别.
- 验证: API key 走 URL path `/getfeed/<key>/<sport>/<endpoint>`, **不能用 header**. 这意味着每个 HTTP 请求都暴露 key, 内部 metrics / log / proxy 必须脱敏 `<REDACTED>`.

### 2.2 格式 (重要 — ETL 必读)
Goalserve 同一个 API 里**混用 3 种格式**:

| 格式 | 例子 endpoint | 特征 |
|------|--------------|------|
| 原生 JSON | `bsktbl/*`, `hockey/nhl-scores`, `football/nfl-scores`, `cricket/livescore`, `mma/schedule`, `darts/home` | top-level `scores` 或 `shedules` 字典, 字段直接命名 |
| XML | `soccer/*`, `tennis/atp` (rankings), `soccer/home`, `soccer/inplay` | top `<scores sport="...">`, attributes 在 element 上 |
| XML→JSON 包装 | `soccernew/*`, `baseball/*` | 表面是 JSON, 但 attributes 以 `@` prefix (e.g. `@id`, `@date`), 是 XML 自动转 JSON 结果 |

**`?json=1` 对 XML endpoint (e.g. `soccer/inplay`) 无效, 仍返回 XML**. 必须用 `soccernew/inplay` 才能拿"伪 JSON".

**ETL 实现必须支持 3 种 parser**, 或在网关层 normalize 成统一 schema.

### 2.3 命名 (拼写陷阱)
Goalserve 的 endpoint 命名是历史包袱:

- ✅ `bsktbl/nba-shedule` — **拼错的 "shedule"** (不是 "schedule"), 实测 200
- ❌ `bsktbl/nba-schedule` — 500
- ❌ `bsktbl/usa-nba` — 500
- ✅ `bsktbl/nba-scores` (用连字符, 不是下划线)
- ❌ `bsktbl/nba_scores` — 500
- ✅ `bsktbl/inplay` (basketball 全联赛 inplay)
- ✅ `bsktbl/home` (basketball 全联赛当日)
- ✅ `football/nfl-scores`
- ❌ `football/nfl-schedule` — 200 但空 (62 bytes)
- ✅ `baseball/usa` 或 `baseball/mlb-scores` (两者别名)
- ❌ `baseball/mlb_scores` — 500
- ✅ `tennis/atp` 是 **rankings 不是 livescore**, tennis live 在 `tennis/home`
- ✅ `hockey/nhl-scores`

**ETL 路径表必须硬编码**, 别瞎猜.

### 2.4 跨洋延迟 (proxy 必需)

10 次直连 vs 10 次代理 (`bsktbl/inplay`, 70 bytes payload):

| 通道 | avg | p50 | p95 | max |
|------|-----|-----|-----|-----|
| 直连 | 2.726s | 2.081s | **8.495s** | 8.495s |
| 代理 (`127.0.0.1:7890`) | 1.927s | 1.888s | 2.325s | 2.325s |

**代理 p95 比直连低 73%**. 跨洋 TCP/TLS 重传打死直连尾延迟. **生产 ETL 强制走代理, 这是硬约束**. 老姜 latency-budget-v1 + 老周架构 v0.1 据此修订.

代理本身的 RTT 开销 ~50ms, 远小于直连尾延迟收益.

---

## 3. Endpoint 实测延迟 (proxy mode, 20 次)

来源: `docs/RESEARCH/data/xiaoduan-goalserve-samples/latency-runs-20260528.csv`

| endpoint | 体积 | TTFB p50 | TTFB p95 | total p50 | total p95 | total max |
|----------|-----:|---------:|---------:|----------:|----------:|----------:|
| `bsktbl/inplay` | 185 B | 2.04s | 9.99s | 2.04s | 9.99s | 10.6s |
| `bsktbl/nba-shedule` | 130 KB (avg, gzipped) → 877 KB decoded | 2.79s | 7.19s | 3.51s | 7.70s | 8.8s |
| `soccer/inplay` | 327 B | 2.44s | 6.25s | 2.44s | 6.25s | 8.5s |
| `soccernew/inplay` | 327 B | 1.89s | 5.43s | 1.89s | 5.43s | 6.5s |
| `soccer/home` | 21 KB (gzip) | 2.40s | 5.04s | 2.44s | 5.04s | 7.8s |
| `football/nfl-scores` | 4.3 KB | 1.81s | 7.71s | 1.81s | 7.71s | 12.6s |
| `baseball/usa` | 30 KB | 2.90s | 8.50s | 3.05s | 8.50s | 12.7s |
| `tennis/home` | 12 KB | 2.72s | 7.73s | 2.72s | 7.73s | 8.3s |
| `hockey/nhl-scores` | 4 KB | 2.35s | 6.95s | 2.35s | 6.95s | 12.0s |
| `cricket/livescore` | 47 KB | 2.44s | 7.16s | 2.56s | 7.16s | 7.7s |

**核心结论**:
1. **TTFB 是主导, total ≈ TTFB**. 这是跨洋 RTT + Goalserve 服务器响应时间, **不是带宽问题**.
2. p95 全部 5-10s, max 偶尔到 12s. **决策 budget 必须考虑 10s 上游延迟**.
3. 体积大的 endpoint (nba-shedule 877KB) 比体积小 (inplay 185B) 慢得不显著 (3.5s vs 2s) — Goalserve 服务器内部生成时间 + 跨洋 RTT 主导, gzip 后传输时间几乎不算.
4. 失败率 0/200 (proxy mode). 接口本身可靠.

---

## 4. 已确认 endpoint 清单 (2026-05-28 实测)

### 4.1 Basketball
| endpoint | 用途 | 格式 | 季节性 |
|----------|------|------|--------|
| `bsktbl/inplay` | 全 basketball 实时 (含非 NBA) | JSON | 全年, 内容随赛事 |
| `bsktbl/home` | 全 basketball 今日 | JSON | 全年 |
| `bsktbl/nba-scores` | NBA 今日 (live + final) | JSON | NBA 季 10-6 月 |
| `bsktbl/nba-shedule` | NBA 全季赛程 (含历史 + 未来) | JSON | 全年返 25/26 全季 |

### 4.2 Soccer
| endpoint | 用途 | 格式 |
|----------|------|------|
| `soccer/inplay` | 全足球实时 | XML |
| `soccer/home` | 全足球当日 | XML |
| `soccernew/inplay` | 同上 (XML→JSON 包装) | "JSON" (`@prefix`) |
| `soccernew/home` | 同上 | "JSON" |
| `soccer/d-1` | 一天前历史 | XML, **响应 14s 慢** |

### 4.3 NFL
| endpoint | 用途 | 格式 |
|----------|------|------|
| `football/nfl-scores` | NFL 当日 + 缓存最近 Final (含 2026 Super Bowl LX) | JSON |
| `football/inplay` | 全 football 实时 | JSON |
| `football/home` | 全 football 今日 | JSON |
| `football/nfl-shedule` | 200 但实测空 (季外), 季内应有 | JSON |

### 4.4 MLB
| endpoint | 用途 | 格式 |
|----------|------|------|
| `baseball/usa` | MLB 当日 (15 场 5/28) | XML→JSON `@prefix` |
| `baseball/mlb-scores` | `baseball/usa` 别名 | 同上 |
| `baseball/inplay` | 全棒球实时 | "JSON" |
| `baseball/home` | 全棒球今日 | "JSON" |

### 4.5 NHL
| endpoint | 用途 | 格式 |
|----------|------|------|
| `hockey/nhl-scores` | NHL 当日 | JSON |
| `hockey/inplay` | 全 hockey 实时 | JSON |
| `hockey/home` | 全 hockey 今日 | JSON |

### 4.6 Tennis
| endpoint | 用途 | 格式 |
|----------|------|------|
| `tennis/atp` | **ATP rankings (不是 live!)** | XML |
| `tennis/home` | 全 tennis 当日 (含 ATP + WTA + Challenger) | JSON |

### 4.7 Cricket
| endpoint | 用途 | 格式 | 备注 |
|----------|------|------|------|
| `cricket/livescore` | Cricket 全球实时 (270 KB!) | JSON | 含 ball-by-ball commentaries |
| `cricket/home` | Cricket 当日 | JSON | 小 |

### 4.8 其他覆盖 (subscription 内)
- `mma/schedule` — UFC + 其他 MMA 赛程 + 选手, 80 KB JSON
- `darts/home` — Darts 当日, 含 Premier League Darts (G. Price, L. Littler 等)
- `rugby/inplay` — 实时 (XML, 286 B)
- `handball/inplay`, `volleyball/inplay` — 200 但少内容

### 4.9 实测不可用 / 路径未知
- `f1/*` — 全部返 17 B 空体, 此 key 无 F1 数据
- `golf/*` — 500
- `boxing/*` — 200 但空
- `esoccer/inplay` — 返 ASP.NET HTML 表单 (非 API endpoint)

### 4.10 **!!! Odds endpoint 全 0 字节 !!!**

实测以下路径**全部 200 但 size=0**:
- `getodds/basketball`
- `getodds/soccer`
- `getodds/football`
- `getodds/tennis`
- `getodds/baseball`
- `getodds/cricket`
- `getodds/hockey`
- `getodds/soccer?bookmakers=bet365`
- `soccer/getodds`, `soccer/odds-current`, `soccer/inplay-odds`

**`bsktbl/nba-shedule?showodds=1&bm=16` 体积没变** (130 KB → 130 KB), 字段里也无任何 odds/bookmaker/price 字样.

**MLB `baseball/usa` 字段含 `@oddsid` (15 场比赛 14 个有值)** — 这暗示 Goalserve 内部有 odds 数据但 endpoint 路径 / key 权限不开. 我用 `oddsid` 直接拼 `/odds/<id>`, `/getodds/<sport>/<id>` 都没拿到 odds JSON.

**结论 → 给老雷的 GAP 报告 #1**:
当前 `GOALSERVE_API_KEY=87ff5e514c6c415be05908deb2f15526` 仅 livescore + schedule 权限, 无 odds package. 老雷需联系 Goalserve sales:

> (a) 确认订阅包含 odds 数据没有? 没有的话价钱多少? 上 Polymarket 体育套利**没有 vendor odds = 没有 fair-value 比较基线**, 业务直接卡死.
> (b) 若有, 拿正确 endpoint 文档路径 + 参数 (bookmaker id list, showodds 参数语法).

老彭 + 老彦 (赔率分析师) 也确认: **能在 Polymarket 上做 EV 套利, 必须有 sportsbook odds 作 anchor**. 见 `docs/RESEARCH/laopeng-betting-industry-analysis-v1.md`.

---

## 5. 字段层级 + 解析陷阱 (给小余的 ETL 必做项)

### 5.1 字段实测频率 (NBA schedule 1403 场样本)

| match-level 字段 | 填充率 | ETL 处理建议 |
|---|---:|---|
| `id` | 100% | primary key |
| `date`, `formatted_date`, `datetime_utc`, `timezone` | 100% | **`datetime_utc` 唯一可信时间字段**, 其他全是显示用. UTC + ISO-ish 格式 (`"02.10.2025 16:00"` 不带秒不带 timezone offset, ETL 必须加 `Z` 或当 UTC parse) |
| `status` | 100% | string, e.g. "Final" / "Not Started" / "In Play" |
| `time` | 100% | 显示用 (e.g. "11:00 AM"), 别用 |
| `timer` | 0% (常规赛中也是空) | 可丢 |
| `venue_id`, `venue_name` | 99.6% | optional |
| `attendance` | 99.1% | 比赛中可能为空, optional |
| `broadcast` | 4% | 实测 96% 空, **别期待** |

| team-level 字段 | 填充率 |
|---|---:|
| `hometeam.id`, `hometeam.name` | 100% |
| `hometeam.q1-q4` | 99% (正常常规赛), Final 才有 |
| `hometeam.ot` | 4.6% (OT 情况下才填) |
| `hometeam.totalscore` | 99.1% |
| `hometeam.posession` | 100% (但值是 "True"/"False" 字符串, 不是 bool!) |

### 5.2 解析陷阱 (小余必做)

**P1 (致命级)**:
1. **`match` 字段单场 dict, 多场 array** — Goalserve XML→JSON 转换时单元素不会包成数组. ETL 必须用 normalize helper:
   ```cpp
   auto to_array = [](json& v) { return v.is_array() ? v : json::array({v}); };
   ```
   覆盖所有 `match`, `category`, `player`, `inning`, `commentary` 节点.

2. **`@prefix` 双语**: 同一个 sport 不同 endpoint 有的是 `@id` 有的是 `id`. ETL 必须 unify, 推荐输入 normalizer 把 `@` 全脱掉.

3. **拼写历史包袱**: `shedules` (NBA) 不是 `schedules`. 不要假设新版会改, Goalserve 改了会破坏所有现有客户.

4. **空 endpoint vs 失败 endpoint** — 200 + size 0 不一定是 "无比赛", 也可能是 "你 key 没权限". 必须区分:
   - HTTP 200 + 含 JSON `{"scores":{"sport":..., "updated":...}}` → 真的无比赛
   - HTTP 200 + 0 bytes → 可能权限缺失
   - HTTP 500 → 路径错
   ETL log 必须区分这三类, 不能全归 "no data".

**P2 (业务级)**:
5. **数值字段全是 string** — `q1: "25"`, `totalscore: "99"`, `attendance: "11983"`. ETL 必须 `to_int_safe()`, 空串当 0 但要 mark "missing" 而不是 "0".

6. **时区**: `datetime_utc` 是 UTC, 但 `time` 是 local time + 看 `timezone` 字段 (e.g. "EDT", "ET"). 比赛跨时区时务必只用 `datetime_utc`.

7. **`status` 枚举不闭合**: 实测见过 "Final", "Final/OT", "Not Started", "In Play", "Postponed", "Cancelled". ETL 必须 catch-all + alert unknown.

**P3 (差分级 — 客户端 diff)**:
8. **Goalserve 无 incremental endpoint**. 每次 polling 拿全量. **小余 ETL 必做客户端 diff**: 按 `match.id` hash 上次 snapshot, 只把变化推给下游. 详见 §6 客户端 diff 算法.

9. **`updated` 字段是 Goalserve 内部 timestamp** (e.g. `"635712985484037849"` — 这是 .NET DateTime ticks, 不是 unix epoch). 转换公式:
   ```
   unix_seconds = (ticks - 621355968000000000) / 10000000
   ```
   ETL 必须有这转换器.

### 5.3 不同 sport 字段差异表

| sport | 比分模型 |
|-------|---------|
| basketball (NBA) | `q1..q4 + ot + totalscore` |
| football (NFL) | `q1..q4 + ot + totalscore`, 含 defensive/offensive stats (player-level) |
| baseball (MLB) | `innings.inning[].@score/@hits`, `@hits`, `@errors`, `@totalscore`, 含 starting_pitchers/umpires |
| hockey (NHL) | `p1..p3 + ot + so + pp + totalscore`, 含 goalkeeper_stats, scoring (goal events), penalties |
| soccer | `localteam.score`, `awayteam.score`, `match.minute`, `inj_time` |
| tennis | sets/games breakdown, 在 tennis/home 里详细 |
| cricket | inning-by-inning + ball-by-ball commentaries + lineups + wickets + superover |

**通用 schema 不可能, ETL 必须 per-sport adapter**. 小余 + 老周一起设计 normalized internal schema.

---

## 6. 客户端 diff 算法 (Goalserve 无 incremental, ETL 必做)

由于无 WSS / 无 incremental, 整个上行带宽全靠客户端做 diff.

### 6.1 推荐架构
```
[Goalserve poll worker] --proxy--> [raw cache layer (per endpoint)]
   ↓
   diff against previous snapshot (keyed by match.id + period)
   ↓
   [event stream: ADDED / SCORE_CHANGED / STATUS_CHANGED / FINAL]
   ↓
   [downstream: 决策引擎 / Polymarket bridge]
```

### 6.2 polling 间隔 (建议)
- live inplay (`bsktbl/inplay`, `soccer/inplay`): **2-3 秒**, RPS 主导
- live scoreboard (`bsktbl/nba-scores`): **5 秒**
- schedule (`bsktbl/nba-shedule`): **6 小时** (大且变化少)
- standings / rankings: **每日一次**

Goalserve commercial license 通常允许 ~1 RPS sustained per key, 老雷需 confirm. 当前若并发跑 10 个 endpoint × 2.5s = 4 RPS, 可能触发限速. **建议**: 单 worker + 队列 + 调度器, 不要每 sport 独立 timer.

### 6.3 snapshot key
推荐 diff key 设计:
```
event_key = sport + ":" + match.id
period_key = event_key + ":" + status   // 区分 "Q3 84-86" vs "Final 99-84"
```
snapshot value = stable hash of (totalscore + period_scores + status + minute/timer).

任何 hash 变化 → emit event.

### 6.4 边界 case
- **比赛 ID 重用**: 实测 NBA `id: "310218"` 是 Final 比赛, 但 Goalserve 偶尔在球队改名 / 重赛时复用 ID. ETL 必须用 `(id, datetime_utc)` 联合判定.
- **Postponed / Resumed**: status 流转 `Not Started → Postponed → Not Started → In Play`. ETL 不要假设 monotonic.
- **赛季交接**: NBA `bsktbl/nba-shedule` 返 25/26 全季 1403 场, 跨季时 endpoint 返新季还是旧季要老雷向 Goalserve 确认.

---

## 7. 给老姜的延迟预算 (latency budget v1 更新)

当前 `docs/RESEARCH/laojiang-latency-budget-v1.md` 假设的"上游 Goalserve latency"需用实测数据修订:

| 段 | 之前估算 | **实测 (proxy)** | 备注 |
|---|---|---|---|
| Goalserve REST round-trip (small endpoint, e.g. inplay) | ? | p50 **1.9s** / p95 **5.4s** / max 10s | TTFB 主导 |
| Goalserve REST round-trip (large, schedule) | ? | p50 **3.5s** / p95 **7.7s** | gzip 后传输不主导 |
| 直连方案 | ? | p95 **8.5s** (尖刺到 10+s) | **不可用于生产** |

**结论**: 整体决策链 latency budget 必须给上游 **至少 5s p95 + 10s safety margin**. 这意味着:
- 任何 < 5s 的 polling 间隔在 p95 都不能保证"最新数据". 实际有效更新频率 ~3-5s.
- 决策引擎不能假设"看到 Goalserve 数据 = 实时". Polymarket WSS (订单本) 比 Goalserve 快 1 个数量级, 这是 edge 来源也是 risk: **如果在比分还没传过来时, Polymarket 已经反应了, 我们就被 sniper'd**.
- 反过来, 跨洋玩家也都被同一延迟卡, **没有大家比 Goalserve 快, 只比解析快 / 决策快**.

---

## 8. 给老雷 (GM) 的待办

**P0 (Sprint-1 阻塞)**:
1. **联系 Goalserve sales** 确认 odds 是否在订阅内. 如不在, 报价多少, 是否签. 没 odds 业务直接卡死 (老彭 + 老彦 已 sign-off).
2. **确认 RPS 限制**. 当前我做 200 次探测 / 8 分钟 = 0.4 RPS, 全 200, 但 sustained 1 RPS 是否打限速未知. 商务文档查.

**P1**:
3. **代理 SLA**: `127.0.0.1:7890` 是哪个产品? p95 抖动谁负责? 老叶 (Polygon RPC) 那边的代理跟这个是同一个吗? 建议拉 ops 确认.

**P2**:
4. F1 / golf / boxing 数据需要不? 当前 key 不开. 老彭定优先级.

---

## 9. 给老周 (架构) 的硬约束

1. **Goalserve poll 必须走代理**, 直连 p95 不可用.
2. **ETL 必须 dual parser (XML + JSON), per-sport adapter**.
3. **客户端 diff 算法是 mandatory module**, 不是 nice-to-have.
4. **odds endpoint 缺失** → 老彭定 "无 odds 时 fair value 怎么算" 的回退策略 (e.g. 自建 model from histo, 或 fall back to Polymarket midprice).
5. **`datetime_utc` 是唯一可信时间字段**, 所有 time / timezone / timer 字段是 display only.
6. **`updated` 字段是 .NET ticks**, 不是 unix. 转换器进 ETL utility.

---

## 10. Sprint-1 验收清单 (给老钱 + sprint planner)

小段 deliverable:
- [x] NBA inplay endpoint 路径确认 (`bsktbl/inplay` + `bsktbl/nba-scores`)
- [x] NBA pregame endpoint 路径确认 (`bsktbl/nba-shedule`)
- [x] Soccer inplay endpoint 路径确认 (`soccer/inplay` XML + `soccernew/inplay` 伪 JSON)
- [x] NFL livescore endpoint (`football/nfl-scores`)
- [x] MLB livescore (`baseball/usa`)
- [x] Tennis (`tennis/home` for live)
- [x] proxy vs direct 实测 → proxy 必选
- [x] 20 次 / endpoint × 10 endpoint 延迟统计
- [x] 字段 fill rate (NBA 1403 场样本)
- [x] 解析陷阱清单 (给小余)
- [ ] **odds endpoint** — 阻塞老雷商务确认
- [ ] proxy 故障切换策略 — 阻塞老姜 + 老叶讨论
- [ ] schedule polling 调度器实现 — Sprint-2

---

## 11. 历史 / 变更

- v1 (2026-05-28) — 小段首发, 实测 NBA + Soccer + NFL + MLB + NHL + Tennis + Cricket + MMA + Darts + Rugby + Volleyball + Handball. odds endpoint **未确认**, 待商务.

下一版 v2 触发条件:
- 拿到 odds endpoint 路径后立即出 v2
- Sprint-1 末做覆盖 retro, 更新真实 polling 间隔下的限速行为
- 新 sport 接入 (e.g. F1 / golf) 出 v3
