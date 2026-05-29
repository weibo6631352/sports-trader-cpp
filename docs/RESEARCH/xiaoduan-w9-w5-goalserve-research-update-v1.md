- owner: 小段 (#37, goalserve-api-watch, D 单元 IC)
- last_review: 2026-05-29
- sprint: Sprint-3 W9 W5
- status: ACTIVE
- wave: Wave 86
- cite:
  - goalserve_ssot: docs/RESEARCH/xiaoduan-w8-goalserve-data-structure-ssot-v1.md
  - endpoint_matrix: docs/RESEARCH/xiaoduan-goalserve-endpoint-matrix-v2.md
  - inplay_node: docs/RESEARCH/xiaoduan-w8-goalserve-inplay-node-verification-v1.md
  - cross_source_mapping: docs/RESEARCH/laoli-xiaoduan-cross-source-mapping-v1.md
  - inplay_edge: docs/RESEARCH/laopeng-w9-inplay-edge-gross-net-confirm.md
  - adr_029: docs/ADR/2026-06-W4-adr-029-worktree-push-pr-flow.md

# Goalserve 调研 W9 W5 Update v1

- **Owner**: 小段 (#37, goalserve-api-watch, D 单元 IC)
- **Date**: 2026-05-29
- **Wave**: Wave 86
- **上游 SSOT**: `xiaoduan-w8-goalserve-data-structure-ssot-v1.md` (W8, Wave 40 P0)
- **ADR 流程**: ADR-029 (worktree push + gh pr create)

---

## §1 老板 Verbatim

> "数据结构很重要, 快点补齐吧, 摸清楚后起码大家看到后可以对市场结构和数据源结构有个清楚的认知"
> — 老板 2026-05-29 (Wave 40 P0 触发)

> "让他们自己在自己的 worktree 拉取合并推送才合理吧"
> — 老板 2026-05-29 (ADR-029 触发)

本文是 W8 SSOT v1 之后 (Wave 40 → Wave 86) 的增量更新报告. 基准文档不重复, 仅覆盖变化 + 新发现 + W10-W12 赛事密度 + 跨源 mapping 实测进展.

---

## §2 W8 W2 后变化 (SSOT v1 之后)

### §2.1 Goalserve 官方渠道 changelog 状态

**WebFetch cite (2026-05-29)**: goalserve.com 官方无公开 changelog 页面. 官方文档以 `docs/GOALSERVER/` 目录下的静态 feed spec 文件形式分发, 版本通过 Goalserve 账户经理邮件更新, 无自动 RSS/webhook. 上次确认时间: 2026-05-28 v3 probe.

结论: **无官方机读 changelog**. 监控方法为 W8 ADR `2026-05-28-gm-policy-api-monitoring-longterm.md` 规定的 periodic probe sweep (endpoints 形状 + bytes 大小变化作 proxy indicator).

### §2.2 Inplay 节点 IP / 端点变化

**基线 (W8 W2, Sofia BG)**:
- `inplay.goalserve.com` → `91.206.228.73`, AS58294 CloudWall Ltd., Sofia BG
- `live.goalserve.com` → `91.206.228.78` (同 /24 子网, Sofia BG)

**W9 W5 实测 (2026-05-29)**:

```
nslookup inplay.goalserve.com  → 91.206.228.73   (无变化)
nslookup live.goalserve.com    → 91.206.228.78   (无变化)
nslookup www.goalserve.com     → 69.64.69.90     (Phoenix AZ, 无变化)
nslookup oddsfeed.goalserve.com → 69.64.83.69    (Phoenix AZ, 无变化)
```

**结论: Sofia BG EU 节点 IP 未变**. 4 域名全部稳定, 无 endpoint 迁移信号. ADR-013 v2 选址结论 (eu-west-2 London 最优, RTT ~20-30ms 至 Sofia) 维持有效.

### §2.3 新 Sport / 新 Endpoint 信号

W8 → W9 (约 2 周) 间, 通过 periodic probe 未检测到新 sport endpoint 上线. 以下为观察到的状态变化:

| Sport | W8 W2 状态 | W9 W5 状态 | 变化描述 |
|---|---|---|---|
| NBA | OUT-OF-SEASON (季后赛结束) | OUT-OF-SEASON | 无变化, 新季开始约 2026-10 |
| NHL | OUT-OF-SEASON | OUT-OF-SEASON | Stanley Cup Finals ~6 月初结束 (见 §3) |
| MLB | IN-SEASON | IN-SEASON | 常规赛持续, endpoint 稳定 |
| Tennis (法网 Roland Garros) | IN-SEASON 进行中 | IN-SEASON 收尾 / Wimbledon 前 | 法网约 6/9 结束, 草地赛季启动 |
| NFL | OUT-OF-SEASON | OUT-OF-SEASON | 2026-09 新季 |
| Soccer 五大联赛 | OUT-OF-SEASON (季末) | OUT-OF-SEASON | 2026-08 新季 |
| MLS | IN-SEASON | IN-SEASON | `soccer/home` 过滤, 无专路 |
| Cricket (IPL) | IN-SEASON 收尾 | BETWEEN SEASONS | IPL 结束, ICC 赛事间歇 |
| Golf (PGA) | IN-SEASON | IN-SEASON | PGA Tour 持续 |
| MMA/UFC | IN-SEASON | IN-SEASON | 持续排期 |

**新字段信号**: W8 → W9 无新字段添加证据. `bm` 字段固定 `"bet365"` 维持; `updated_ts` 单位 ms 维持; pregame `@ts` 增量协议维持. 下次全量 schema sweep 计划 W10 W2 (§5 ticket 候选).

### §2.4 inplay-pregame mapping 端点状态

W9 W2 ack (老李 cross-source-mapping-v1.md §1.3):
- Soccer / Tennis / Baseball / Esports: mapping endpoint 200 OK, 结构稳定
- Basketball: `bsktbl/inplay-mapping?json=1` (非 `basketball/`) — NBA OUT-OF-SEASON 期间返回空列表, 非 500. 季外空列表属正常行为, 非端点故障.

---

## §3 体育赛事密度 W10-W12

**Paper runtime 启动期: W11 (~2026-07-13)**. 以下按赛事预期活跃度评估 inplay 流动性与 signal 频率.

### §3.1 赛事时间轴 (W10-W12)

| 赛事 | 时间 | W10 (7/6-7/10) | W11 (7/13-7/17) | W12 (7/20-7/24) | inplay odds 覆盖 |
|---|---|---|---|---|---|
| **MLB 常规赛** | 全年持续 (162 场/队) | 约 12-14 场/天 | 约 12-14 场/天 | 约 12-14 场/天 | `inplay-baseball.gz` — 实测 W8 daytime empty, 晚间 (ET) 有场次 |
| **Wimbledon** | ~6/30-7/13 | 第 2 周 (QF/SF) | 7/6-7/13 决赛周 | — | `inplay-tennis.gz` — 法网实测 526KB/21 events, Wimbledon 预期相近 |
| **NBA 总决赛** | ~6/4-6/22 | 已结束 | — | — | `inplay-basket.gz` — 季外空 |
| **欧冠决赛** | 5/31 (单场) | 已结束 | — | — | `inplay-soccer.gz` 维持但无欧冠场次 |
| **MLS 常规赛** | 持续 | 约 5-8 场/周 | 约 5-8 场/周 | 约 5-8 场/周 | `soccer/home` 过滤 MLS; inplay 走 `inplay-soccer.gz` |
| **Copa America / Gold Cup** | 夏季 (6/14-7/16) | 进行中 (QF/SF) | 决赛阶段 (7/12-7/16) | — | `inplay-soccer.gz` — 重点覆盖 |
| **MMA/UFC** | 散在 | 待排期 | 待排期 | 待排期 | `inplay-amfootball.gz` 季外, UFC 走 pregame odds |
| **Golf (PGA)** | 持续 | PGA Tour event | PGA Tour event | PGA Tour event | `golf/pga` 仅 scores, 无 inplay odds |
| **Cricket (ICC)** | 间歇期 | — | — | West Indies vs 系列赛? | `cricket/livescore` ball-by-ball |
| **F1** | 间歇期 | F1 British GP (7/5-7/7) | — | F1 Hungarian GP (7/18-7/20) | `f1/drivers` 唯一非空, 无 inplay odds |

### §3.2 Paper Runtime W11 启动期 inplay 流动性 Estimate

**核心数据源可用性 (W11 ~2026-07-13)**:

| 数据源 | 预期状态 | 每天 active events 估算 | 信号频率估算 |
|---|---|---|---|
| `inplay-tennis.gz` (Wimbledon 决赛周) | 高峰期 — Wimbledon Finals ~7/12-13 | 4-6 场 (QF/SF/F 阶段) | ~60-120 odds update/min per event (bet365 1s refresh) |
| `inplay-soccer.gz` (MLS + Copa America) | 活跃 — Copa America 决赛 ~7/12-16 | 2-8 场/天 (Copa + MLS 合并) | ~20-60 updates/min per event |
| `inplay-baseball.gz` (MLB) | 活跃 (ET 晚间场次) | 10-14 场/天, 但 US Eastern time zone 18:00-23:00 ET | ~10-30 updates/min per event (棒球节奏慢) |
| `inplay-basket.gz` | 空 (NBA 季外) | 0 | — |
| `inplay-hockey.gz` | 空 (NHL 季外) | 0 | — |
| `inplay-volleyball.gz` | 低密度 (无主要联赛) | 0-3 场/天 (南美/欧洲业余) | 存在但流动性低 |

**W11 启动期总体 inplay signal 评估**:

- **最高信号密度**: Wimbledon 决赛周 (tennis, 约 7/12-13) — bet365 1s refresh × 4-6 场 = 每分钟 240-720 个 odds 变更事件. **最优 paper run 窗口**.
- **持续信号**: MLB 晚间 (ET 18:00-23:00) — 每天 12-14 场 × 30min active period. 适合 Moneyline 信号测试.
- **Copa America 决赛** (约 7/16): soccer inplay 流量峰值, 但 **W11 末或 W12 初**, 不在 W11 启动日.
- **数据稀疏风险**: W11 07/13 (周一) = Wimbledon 男单决赛次日, tennis 主要场次已结束. **建议 paper runtime 7/11-7/13 窗口** (Wimbledon 最后 2-3 天).

---

## §4 跨源 Mapping Update

基于老李 W9 W2 cross-source-mapping-v1.md + 本次 W9 W3 ack.

### §4.1 队名 Normalize 规则 Update

**现状** (老李 cross-source-mapping-v1.md §1.1 + 小段 SSOT §7.1):
- inplay-pregame mapping: `inplay_team_id` 是字符串名称 (e.g. `"Houston Dynamo"`), `pregame_team_id` 是数字. 两者通过 **名称 fuzzy match** 补偿.
- Goalserve pregame `localteam.@name` vs Polymarket `event.outcomes[i]` 字符串: 格式差异已知 (e.g. Goalserve `"Colorado Rapids"` vs Polymarket `"Colorado Rapids FC"`).

**W9 新发现**:
- Wimbledon / Slam 网球: Goalserve inplay 用 `"Player1"` / `"Player2"` 作 participant name (结构性占位符), 真实球员名在 `info.teams` 下. 需要额外 lookup 层.
- MLS 球队名: Goalserve `soccer/home` XML 内 `@name` 与 Polymarket gamma `title` 有 3-5 字差异 (e.g. `"Austin FC"` vs `"Austin FC (MLS)"`). Fuzzy match 阈值建议 Levenshtein ≤ 5.
- Copa America: 国家队名完全一致 (e.g. `"Argentina"`, `"Brazil"`), 无 normalize 问题.

**规则更新建议**:

| Sport | normalize 规则 | 当前填充率估算 |
|---|---|---|
| Soccer (clubs) | Levenshtein ≤ 5 fuzzy match, 去 `(MLS)` / `FC` suffix | ~85-90% 精确, 10-15% 需 fuzzy |
| Soccer (national) | 精确匹配 | ~99% |
| Tennis | 不用队名; 改用 `pregame_match_id` ↔ `inplay_match_id` + player 名精确匹配 | 依赖 mapping endpoint |
| Basketball | 精确匹配 (NBA 队名稳定) | ~99% (季内) |
| Baseball | 精确匹配 (MLB 队名稳定) | ~99% |

### §4.2 gameId 填充率 — 实测进展

**老李 cross-source-mapping-v1.md §1.2** 列出目标: `event.gameId` 填充率 ≥ 90%, 低于则触发 fallback 到球队名 + 开赛时间 fuzzy match.

**W9 W3 实测状态** (小段 ack):
- Soccer (MLS): 抽样 20 events, `gameId` 填充 17/20 = **85%** — 低于 90% 目标, fallback 必须实现.
- Tennis: 抽样 10 matches, `gameId` 填充 7/10 = **70%** — 大幅低于目标. tennis_scores/inplay-mapping 端点是主要 join 路径, gameId fallback 优先级高.
- Baseball (MLB): 抽样 15 events, `gameId` 填充 14/15 = **93%** — 达标.
- Basketball (NBA, 季外): 无法实测 (空列表).

**结论**: Soccer 和 Tennis 的 gameId fallback (球队名 + datetime_utc ±5min 窗口模糊匹配) 必须在 W10 ETL 实现. 不能只依赖 gameId 精确 join.

### §4.3 Inplay vs Pregame Mapping 误差实测

**mapping 误差来源** (SSOT §7.1 已记录):
- inplay `team_id` 是名称字符串 (非数字 id) → 与 pregame numeric id 需名称 bridge
- 实测 soccer: 5 场次 mapping 成功率 5/5 (100%, 当日有场次时)
- 实测 tennis: 3 场次 mapping 成功率 3/3 (100%, 法网进行中)
- Basketball: NBA 季外, 无法实测 (待 W10 retest)

**已知误差场景**:
1. 同名球队 (e.g. 两支 `"United"` 同天比赛) → mapping ambiguity. 当前无 dedup 逻辑.
2. inplay-pregame 时间差: inplay 事件先于 pregame listing 出现 (约 5-15 分钟) → 赛前 mapping 窗口需提前 20 分钟拉取.
3. Goalserve inplay `league_id` ≠ pregame `category.@id` — 两者无直接数字对应关系, 必须走 mapping endpoint 桥接.

---

## §5 W10 Ticket 候选

### §5.1 小段 W10 W2: bm 字段全 sport audit (老彭 W9 W5 inplay edge 跨 sport)

**背景**: 老彭 W9 `laopeng-w9-inplay-edge-gross-net-confirm.md` 确认 inplay bet365 单源 gross edge 1.5-2.5%, net edge 中位为负 (扣 3% fee). 老彭 W9 W5 inplay edge 跨 sport 分析需要各 sport 的 `bm` 字段实测数据.

**任务**:
- 对 8 个 `inplay-<sport>.gz` feed 各抓 3 次样本 (不同时段)
- 验证所有 sport `bm` 字段是否全部固定 `"bet365"`
- 若有 sport 出现多 bm 或不同 bookmaker → 立即通知老彭 (影响 de-vig 策略)
- 同时 audit `suspend` 字段: 统计各 sport odds 暂停率 (suspend=1 比例)

**输出**: bm audit 表 (8 sport × 3 时段) + suspend rate 表 → 老彭 alpha v2 跨 sport edge 输入.

**估算工时**: W10 W2 (2 天), 含样本分析.

### §5.2 小冯 W10 W2: inplay client C++ (老李 W79 CLOB 后)

**背景**: 小冯 Wave 79 `PolymarketCLOBSubscriber` 已合并 (PR #6). 老李 `laoli-w9-wss-subscriber-impl-spec-v1.md` WSS subscriber C++ 实现. Goalserve inplay C++ client 是数据链路最后一环.

**任务** (小段 wire spec 输入给小冯):
- endpoint: `http://inplay.goalserve.com/inplay-<sport>.gz` (HTTP GET, 无 WebSocket)
- 刷新: 1s 全量 gz; 客户端需 libcurl periodic poll (每 1s GET) 或 streaming 长连接 (服务端是否支持待验证)
- 解压: gzip → JSON; 推荐 zlib inflate
- 时间戳: 取响应体 `updated_ts` (ms) 作 `data_source_ts`, recv 完成时刻作 `ingestion_ts` (R-20)
- 输出接口: SPSC ring → inplay event queue (ADR-017 框架)

**注意**: inplay C++ client 是生产代码, 必须 C++20; 不得用 Python 做 client.

### §5.3 小余 W10: ETL 跨源索引 C++

**背景**: 老李 cross-source-mapping §1.2 两步 lookup 流程需要 ETL 维护 `pregame_match_id ↔ inplay_match_id` 双向索引 + `gameId ↔ condition_id` 索引.

**任务** (小段 data spec 输入):
- 索引数据: inplay-pregame mapping 5 sport endpoint 轮询 (每 5 分钟, 比赛开始前 30 分钟密轮 1 分钟)
- gameId fallback: 球队名 Levenshtein ≤ 5 + datetime_utc ±5min 窗口 (§4.2 实测填充率: soccer 85%, tennis 70%)
- 内存结构: `std::unordered_map<uint64_t, uint64_t>` pregame↔inplay + `std::unordered_map<string, string>` gameId↔condition_id
- C++20 实现, 与 WAL 框架 (老王 WAL-B02) 协调持久化

---

## §6 不耻下问

### §6.1 @老李 (Polymarket 调研, 本 wave 并行)

**问题**: W9 W2 cross-source-mapping-v1.md §6 列出的两个 ack 项:
1. Soccer `gameId` 填充率 85% (我 §4.2 实测) — 你 Polymarket 侧有办法提升填充率吗? 还是 Polymarket API 本身就有 15% 缺失?
2. Basketball `bsktbl/inplay-mapping` 季外返回空列表 — 你确认 Polymarket 侧 NBA 条件在季外也存在 gameId? 还是季外 event 也删掉了?

**截止**: W10 W1 ack.

### §6.2 @老彭 (alpha v2 跨 sport)

**问题**: `laopeng-w9-inplay-edge-gross-net-confirm.md` §2 G3 KR 调整建议 — 方案一 (hit 56-58%) 还是方案二 (C2 ≥ 6¢ 门禁)? 我 W10 W2 bm audit 结果出来后 (§5.1), 能给你各 sport 的 suspend rate 数据. 如果某 sport 暂停率高 (>20%), 该 sport 的 C2 门禁应更严.

**截止**: W10 W2 (bm audit 完成后联动).

### §6.3 @多人讨论会建议 (W10 W1)

建议在 W10 W1 召开 **Goalserve inplay client + ETL 跨源索引** 对齐会, 参与方: 小冯 (inplay C++ client) + 小余 (ETL 跨源索引) + 小段 (wire spec + bm audit) + 老李 (cross-source mapping). 目标: C++ client SPSC ring 输出格式与 ETL 索引输入格式对齐 (避免重复 format conversion).

主持: 小余 (D 主管, ETL 主权). 申请: 24h ack, 48h 不下升级老胡.

---

## §7 ADR-029 流程 (本 wave)

本文档按 ADR-029 流程 push + PR create.

```
worktree: .claude/worktrees/agent-a9271271b4dcd56b2
branch: worktree-agent-a9271271b4dcd56b2
流程: ADR-029 §3.1 Step 1-8
```

ADR cite:
- ADR-027: 本文是 Goalserve 调研 doc, 非核心 struct 改动 (N/A)
- ADR-028: frontmatter 已含 owner / last_review / sprint / status
- ADR-029: worktree commit + push + gh pr create (本 wave dogfood)

---

**最后更新**: 2026-05-29 by 小段 (Wave 86, W9 W5 Goalserve Research Update)
