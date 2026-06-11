# Goalserve 比赛事件接口对接调研 (event-delay-arb / 做市避逆选)

> owner: 小段 (goalserve-specialist, #37)
> last_review: 2026-06-03
> 任务: 老板点名 — Goalserve 比赛事件 (进球/伤情/换人/红黄牌/点球/破发/得分) 能否对接 PM 比赛, 喂事件延迟套利触发 + 做市避逆向选择
> 前提纠正: 我们【不跨洋】, 服务器 <10ms 到 Polymarket → "早 1 秒知事件" 能真兑现成交
> 实测环境: 部署节点 eu-west-2 (已白名单, paper_server 已停 → 速率预算空闲), 2026-06-03 11:24 UTC 真 feed 实拉

---

## 0. 一句话结论 (TL;DR)

**核心发现 (推翻团队既有理解): 最低延迟的事件源不是 soccernew/live 或 commentaries, 而是我们【已经在拉】的 `inplay-<sport>.gz` 1 秒 feed 本身。** 每个 event 块除了已解析的 `info`, 还带 **`stats` (实时事件计数) + `extra` (带 minute 的事件日志) + `info.state` (5 位实时事件状态码) + `core` (clock 状态 + 毫秒级 updated_ts)** —— 这些字段当前 parser **完全没读** (只读了 `info`)。

- **进球/红黄牌/点球/换人/破发/得分** 全部能在 1s feed 里直接拿到, **键 = `inplay_match_id`, 零跨源桥接** (直接是 paper_loop 的 `es.event_id`)。
- **事件级真实时间戳**: inplay feed 用 `core.updated_ts` (毫秒 epoch, 每 match 各自); soccernew/live 的 `<event ts="…">` 是 **唯一带秒级 epoch 的 per-event ts** (R-20 event_ts 真值), 但需 inplay-mapping 桥。
- **实测延迟下限**: inplay feed 每 match `core.updated_ts` **约每 2s 推进一次** (非广告的 1s), 抓取时数据约 **1.5-2.0s 陈旧**。这是事件延迟套利窗口的硬底。
- **优先级**: 足球 (state 11003 进球 / 11008 点球 / 11005·11006 牌) + 网球 (11118 破发 / 11119 赢盘局 / 11117 ace) 价值最高且零桥接; 篮球/棒球次之; 伤情 (soccernew/injuries) 是赛前 1h 刷新, 非 in-play, 只对赛前做市有用。

---

## 1. Goalserve 事件 feed 全景 (实测盘点)

### 1.1 三条事件通道 (按延迟从低到高)

| 通道 | endpoint | 刷新 | 键 | 带 per-event epoch ts? | 白名单 | 当前已接? |
|---|---|---|---|---|---|---|
| **A. inplay-`<sport>`.gz** | `inplay.goalserve.com/inplay-<slug>.gz` | ~2s/match (广告 1s) | **`inplay_match_id`** | 否 (用 `core.updated_ts` ms) | IP 白名单 (仅服务器) | **score/odds 已接, 事件块未读** |
| **B. soccernew/live** | `www.goalserve.com/getfeed/{KEY}/soccernew/live` | ~实测 5s 响应 | soccernew match id (pregame) | **是 (`<event ts="…">` 秒 epoch)** | key-in-URL (本机可拉) | live_stats 已接, event 节点未读 |
| **C. commentaries** | `www.goalserve.com/getfeed/{KEY}/commentaries/{league}.xml` | 30s (顶级联赛) | static_id / (league+队名) | 否 (event 无 ts, comment 有 ISO ts) | key-in-URL | 仅 live_stats KV 已接 |

**通道选择结论**: 事件延迟套利走 **通道 A (inplay feed)** —— 我们已经在拉、延迟最低、键直接是系统 join key。通道 B 作为 **event_ts 真值校准 + 足球进球时刻确权** 的旁路 (秒级 epoch)。通道 C 只在需要换人/VAR 细节 (球员名/助攻/伤病换人标志) 时按需拉, 30s 太慢不进套利热路径。

### 1.2 inplay feed event 块完整结构 (实测 inplay-soccer.gz, 2026-06-03)

每个 `events.<inplay_match_id>` 下的子块 (parser 当前只读 `info`):

```
"134464683": {
  "core":      {"stopped":"0","blocked":"0","finished":"0","updated":"…","updated_ts":"1780485842365"},  ← 时钟态 + ms epoch
  "info":      {"id","name","league_id","period","score","state","minute","seconds","start_ts",…},        ← 已接
  "team_info": {"home":{"name","score","Serve",…},"away":{…}},                                            ← Serve=网球发球指示
  "stats":     {"0":{"name":"IGoal","home":"1","away":"3"},"1":{"name":"ICorner",…},…},                   ← 实时事件计数 ★
  "extra":     {"0":{"code":"255","minute":"15","value":"15' - 1st Goal - (Tuggeranong Utd)"},…},          ← 事件日志 ★
  "bonus","stream","sts","history","odds": {…}                                                            ← sts=统计 blob/history 空
}
```

---

## 2. 每类事件的关键属性 (实测样例 + R-20 时间戳)

### 2.1 `info.state` —— 最低延迟的瞬时事件状态码 (★ 套利触发主源)

5 位码, 已被 parser 抓进 `rec.gs_state_code` 但 **未被任何下游消费**。实拉 `dictionaries/states/<sport>` 确认语义。这是 feed 里 **最即时的事件信号** —— state 翻到事件码时, 往往 **领先 `info.score` 自增** (先报 "Goal" 态, 再更新比分)。

**足球 (实测 states/soccer):**
| state | 语义 | 套利价值 |
|---|---|---|
| 11003 / 21003 | Home/Away **Goal** | ★★★ 进球瞬时 (领先 score 自增) |
| 11008 / 21008 | Home/Away **Penalty** | ★★★ 点球判罚 (xG≈0.76, 价格将大跳) |
| 10008/20008 / 10009/20009 | Penalty Score / Penalty Miss | ★★★ 点球结果 |
| 11006 / 21006 | Home/Away **Red Card** | ★★★ 红牌 (人数差, 价格跳) |
| 11005 / 21005 | Home/Away **Yellow Card** | ★ (二黄风险) |
| 11011/21011 | Shot on goal | ★★ 危险进攻先行指标 (做市避逆选) |
| 11000/21000 / 11001/21001 | Dangerous Attack / Attack | ★ 动量 (做市偏报价) |

**网球 (实测 states/tennis):**
| state | 语义 | 套利价值 |
|---|---|---|
| 11118 / 21118 | **Break point** | ★★★ 破发点 (赔率剧烈摆动) |
| 11119 / 21119 | **Win a Game** | ★★★ 赢局 (盘局结构变化) |
| 11123 / 21123 | Game Set Match | ★★★ 结束瞬间 |
| 11124 / 21124 | End of Set | ★★ 盘结束 |
| 11117 / 21117 | Ace | ★ |
| 11116 / 21116 | Double Fault | ★ |
| 11126 / 21126 | **Injury Break** | ★★ 伤停 (in-play 唯一伤情信号!) |
| 11129/11130 | Challenge Success/Failed | ★ |

篮球/棒球同构 (state 21077/21244 等, 各 sport 自有 dict)。

### 2.2 `stats` 块 —— 实时事件累计计数 (★ 事件发生确权)

足球实测 (比 soccernew live_stats KV 更全, 含 IGoal/IPenalty/ISubstitution):
```
IGoal=1:3 | ICorner=4:7 | IYellowCard=0:0 | IRedCard=0:0 | IPenalty=0:0 |
ISubstitution=2:3 | IAttacks=89:149 | IDangerousAttacks=61:101 | IOnTarget=4:10 | IOffTarget=6:6 | IPosession=42:58
```
**用法**: 监控 `IGoal`/`IRedCard`/`IPenalty`/`ISubstitution` 计数 **跳变** = 事件发生确权 (比 state 稳, state 是瞬时态会回落)。

- 网球 stats: `POINTS` (当前局点分) / `S1`/`S2` (逐盘局) / `TURN` (谁发球) / `T` (tiebreak)。
- 篮球 stats: `1/2/3/4/OT/T` 逐节得分。
- 棒球 stats: `1..15/R/H` 逐局 + 总得分/安打。

### 2.3 `extra` 块 —— 事件日志 (带 minute, 无 epoch ts)

足球 code: **255=Goal, 252=Corner, 253=Yellow Card, 12=Race milestone, 10=时间桶标记, 1/2=半场/全场比分**。样例 `{"code":"255","minute":"15","value":"15' - 1st Goal - (Tuggeranong Utd)"}`。
棒球 extra 含 `"Solo HR by  (SSG Landers)"`; 网球 extra 含 `"Game 4 - breaks to 40"` 逐局文字。
**局限**: 只有 game-minute 没有 epoch ts, 不能直接做 R-20 event_ts; 但配 `core.updated_ts` 可定位事件进入 feed 的时刻。

### 2.4 soccernew/live `<event ts="…">` —— 唯一秒级 epoch event_ts (R-20 真值)

实测样例:
```xml
<event type="goal" minute="60" extra_min="" team="visitorteam" player=""
       result="[1 - 3]" playerId="" assist="" assistid="" eventid="66785954" ts="1780483752" />
```
- `ts="1780483752"` = **2026-06-03T10:49:12Z** (Unix 秒, 实测与 minute 推算的墙钟开赛+用时一致 → 真事件时刻, **R-20 event_ts 真值**)。
- `eventid` 稳定 → 去重锚。`type` ∈ {goal, yellowcard, redcard, subst}。`team`/`result`/`player`/`assist`。
- **注意**: feed 头 `updated="03.06.2026 03:22:50"` 是账号配置时区 (非 UTC), **别用头时间**, 用 `ts` epoch (R-20 纪律: 用源自带 ts)。

### 2.5 commentaries `<substitution>` / `<var>` —— 换人/VAR 细节 (30s, 无 epoch ts)

```xml
<substitution off="Bryan Castrillón" on="Teófilo Gutiérrez" minute="86" on_id="22353" off_id="545541" injury="False" />
<event type="redcard" minute=".." team="localteam" player="…" playerId="…" eventid="…" />
<var />   <!-- 实测今日多为空节点; 文档载 event_type/ref_decision/var_decision -->
```
- `injury="True"` = 伤病换人 (in-play 伤情代理信号)。`var` 节点文档有 ref_decision/var_decision, 实测今日为空。
- play-by-play `commentaries/{league}-text.xml` 的 `<comment>` 节点 **带 ISO8601 `timestamp="2025-01-15T08:04:14Z"`** (文档 soccer-data-feed §6.428), 但 30s 刷新 + 顶级联赛限定, 不进套利热路径。

### 2.6 伤情 (soccernew/injuries) —— 赛前 1h, 非 in-play

`www.goalserve.com/getfeed/{KEY}/soccernew/injuries`, **每 1h 刷新, 仅未来 2-4 天未开赛比赛**。`<player name="…" status="Foot Injury" id="…"/>` + to_miss/doubtful 节点。**对 in-play 套利无用**, 只对 **赛前做市初始报价** 有价值 (开盘前知道主力伤缺 → 偏移初始 fair)。in-play 伤情唯一信号是网球 `Injury Break` state (2.1) 和 commentaries subst `injury="True"`。

---

## 3. 能否匹配到 PM 比赛 (键桥接分析)

### 3.1 通道 A (inplay feed): 零桥接 ✓✓✓

inplay event 块的键 **就是 `inplay_match_id`** —— 这正是 `InplayScoreParser` 写进 `rec.match_id.inplay_match_id` 的主键, 也是 `paper_loop` 的 `es.event_id`, 已经过 `EventMatcher` 桥到 PM `event_id`。**所以 inplay feed 里所有事件 (state/stats/extra) 天然挂在已映射的比赛上, 不需任何额外桥接。** 这是相对 soccernew/commentaries 的决定性优势。

### 3.2 通道 B (soccernew/live): 需 inplay-mapping 桥 (已有现成代码)

soccernew/live 的 `<event ts>` 键 = soccernew match id (pregame 空间), 与 inplay_match_id **不同空间** (已知教训: league_id "Asean U19 2417 vs 1362" + 队名 "China U20 vs China PR Youth" 两端都不同 → 直接 join 永不命中, 真 bug)。**桥**: `soccernew/inplay-mapping` (`pregame_match_id → inplay_match_id`), 与 `RefreshLiveStats` / `RefreshOdds` 共用同一 `ParseInplayMappingXml` 桥, 现成。

### 3.3 通道 C (commentaries): static_id 或 (league+队名), 不推荐进热路径

commentaries id (7位) ≠ inplay id (134xxx); 走 (league_id + 队名) exact join 或 static_id (待 inplay→static_id 映射落地, cross-source-mapping W9)。30s 刷新本就不进套利热路径, 桥接不是瓶颈。

---

## 4. 代码库已接 / 缺什么

| 字段/通道 | 已接? | 位置 | gap |
|---|---|---|---|
| inplay `info` (score/period/state/minute/start_ts) | ✓ | `inplay_score_parser.cpp` ParseEventInfo | — |
| inplay `info.state` 5位码 | 抓取但**未消费** | 存进 `rec.gs_state_code`, 无下游读 | **缺事件态消费层** |
| inplay `stats` 块 (IGoal/IRedCard/IPenalty/ISubstitution) | **未接** | parser 只切 info+odds, 不读 stats | **缺 stats 解析** |
| inplay `extra` 块 (事件日志) | **未接** | — | **缺 extra 解析** |
| inplay `core.updated_ts` (per-match ms epoch) | **未接** | 顶层 updated_ts 已读, per-match core 未读 | **缺 per-match 新鲜度** |
| inplay `team_info.Serve` (网球发球) | **未接** | — | 缺 (网球做市有用) |
| soccernew/live `<event ts>` (进球 epoch) | **未接** | `ParseSoccernewLiveInto` 只取 live_stats KV, 跳过 `<events>` | **缺 event 节点解析** |
| soccernew/live `live_stats` KV | ✓ | `live_stats_parser.cpp` + `RefreshLiveStats` 桥 | — |
| live_stats 缺失键 IPenalty/ISubstitution/IOffTarget | **未接** | `apply_kv` 只 map 10 个键, 漏这 3 个 (实测 feed 有) | **补 3 键** |
| commentaries `<substitution>`/`<var>`/injury | **未接** | `CommentariesParser` 只取 live_stats KV | 缺 (低优先) |
| soccernew/injuries (赛前伤情) | **未接** | — | 缺 (赛前做市用) |

**小结**: 采集管道、id 桥接、score/odds/live_stats 全通; **缺的纯粹是 "事件块解析 + 事件态消费层"** —— 数据已经在我们手里 (每 2s 拉的 inplay feed 里), 只是 parser 没去读 `stats`/`extra`/`core`/`state`。这是最低成本的对接: 不加 endpoint、不加带宽、不加桥接, 只扩 parser。

---

## 5. 实测延迟与时序 (R-20 关注点)

实测 inplay-soccer.gz 在白名单服务器连拉 14 个样本 (1s 间隔):
- **抓取延迟**: http=200, gz ~30KB, `time_total≈0.13s` (服务器→inplay.goalserve.com 极快)。
- **feed 推进**: 顶层 `updated_ts` 与活跃 match `core.updated_ts` **约每 2s 推进一次** (实测序列 …929738 → 930843 → 933859 → 937883 → 939895 → 941909 → 943920, 步进 ~2000ms)。**广告的 "1s 刷新" 实际是 ~2s/match。**
- **数据陈旧度**: 抓取时刻 wall=1780485945.82, 活跃 match core_ts=1780485943.92 → **数据约 1.9s 旧**。
- **state 实时性**: 同一活跃 match `state` 在窗口内 11002 (In possession) → 11001 (Attack) 翻动, 证明 state 跟随实况近实时刷新。
- 本窗口无进球落点, 进球→feed 的端到端延迟 (Goalserve 数据采集商→inplay feed) **未在本次实测捕获到落点** (需活跃比赛进球时刻蹲守, 后续开赛复测)。文档级估计: Goalserve in-play 数据延迟通常 5-15s (取决于其上游采集), 叠加我们 2s 抓取周期。

**R-20 时间戳契约落地建议** (事件块对接时):
- `event_ts_ns`: 优先用 soccernew/live `<event ts>` ×1e9 (秒 epoch, 真事件时刻); 无则用 inplay `extra` minute 不可换 epoch → 回落 `core.updated_ts` (此时 event_ts=data_source_ts, R-20 允许但标记)。
- `data_source_ts_ns`: inplay **per-match `core.updated_ts`** (ms ×1e6), **比顶层 updated_ts 更精确** (顶层是整包时间, core 是该 match 数据时刻)。
- 禁本地 now() 替代 (R-20 红线)。`core.updated_ts` 是源自带, 用它。

---

## 6. 对接可行性结论 + 盈利方案价值 (优先级)

### P0 — 立即可做, 零新增依赖, 高价值 (事件延迟套利主源)

1. **扩 InplayScoreParser 读 `stats` + `extra` + `core` + 消费 `info.state`** (通道 A)。
   - 产出: 每 match 的 {IGoal/IRedCard/IPenalty/ISubstitution 计数, state 事件码, core.updated_ts}。
   - 套利触发: **state 翻到 11003/21003 (进球) / 11008 (点球) / 11006 (红牌)**, 或 IGoal/IRedCard 计数跳变 → 触发 "PM 散户滞后" 抢窗。键已是 inplay_match_id, 直接挂已映射 PM event。
   - 网球: state 11118 (破发点) / 11119 (赢局) / 11123 (赛点) → 网球价格摆动最剧, 套利价值最高之一。
   - 做市避逆选: state 11011 (shot on goal) / 11000 (dangerous attack) / IDangerousAttacks 跳升 → 提前拉/偏报价。
   - **延迟**: 受 ~2s feed 周期 + Goalserve 上游延迟限制, 不是 0 延迟; 但我们 <10ms 到 PM, 只要早于 PM 散户的 ~13s 滞后 (memory: 事件→bet365→PM 散户滞后) 就有窗口。

### P1 — 旁路确权 / R-20 校准 (低成本)

2. **soccernew/live 读 `<event ts>`** (通道 B) → 进球秒级 epoch event_ts, 经 inplay-mapping 桥 (现成代码)。
   - 价值: 给通道 A 的进球事件补 **R-20 真 event_ts** (核对、防 state 抖动误触发)、确认进球时刻。不是套利首触发 (5s 响应慢于 inplay), 是确权层。
3. **补 live_stats_parser 漏的 3 键** (IPenalty/ISubstitution/IOffTarget) —— 实测 feed 有, 现 `apply_kv` 漏 map。一行表的事。

### P2 — 赛前 / 细节 (非 in-play 热路径)

4. **soccernew/injuries** (赛前 1h) → 赛前做市初始 fair 偏移 (主力伤缺)。非 in-play, 价值有限。
5. **commentaries `<substitution injury>` / `<var>`** (30s) → 换人细节 + VAR。30s 太慢不进套利, 仅作 in-play 伤情/VAR 弱代理。

### 拿不到 / 匹配不上 / 不划算

- **in-play 伤情**: Goalserve 无独立 in-play injury feed。唯一 in-play 信号 = 网球 `Injury Break` state (11126) + commentaries subst `injury="True"` (30s 慢)。足球场上伤停只能靠 state 停表 (core.stopped) + dangerous_attack 归零间接推断, 不精确。**结论: in-play 伤情套利不可靠, 降级。**
- **VAR 实时**: commentaries `<var>` 实测今日空节点, 30s 刷新 → 不可靠不及时, 不进套利。
- **冰球 (hockey)**: 实测 inplay-hockey events=0 (休赛/无 live), 结构推断同构待开赛复测。
- **延迟硬底**: inplay feed ~2s/match + Goalserve 上游 5-15s, 不是 "早 1 秒" 而是 "比 PM 散户的 ~13s 滞后早几秒" —— **价值在于我们 <10ms 到 PM 能真兑现这个差**, 但事件发生到我们知道仍有秒级延迟, 套利 edge 需用真比赛进球落点实测确认 (本次窗口未捕获落点, 列后续蹲守任务)。

---

## 7. 给下游的对接 wire 要点 (派给网络/架构落地, 我只给 spec)

- 采集层不变 (inplay_feed_thread 已在拉 inplay-`<sport>`.gz)。只需在 `InplayScoreParser::ParseEventInfo` 同级新增 `ParseEventBlocks(event_block)` 读 `stats`/`extra`/`core`/消费 `info.state`, 挂到 `GameScoreRecord` 新增字段 (加性, struct 末尾追加 → §8.1 加性免全审计)。
- 事件态消费层 (state 跳变检测 + IGoal/IRedCard 计数 diff → 触发信号) 是新模块, 归量化/策略侧定义触发逻辑 (字段语义派小田/小梁, 我只给 feed 字段)。
- soccernew/live event 解析复用 `RefreshLiveStats` 的 inplay-mapping 桥, 在 `ParseSoccernewLiveInto` 同文件加 `ParseSoccernewEventsInto` (读 `<events>` 节点)。
- R-20: data_source_ts 用 per-match `core.updated_ts`; event_ts 优先 soccernew `<event ts>`。禁 now()。

---

## 附: 本次实测命令 (可复现, 服务器白名单)

```bash
# inplay 1s feed (含 stats/extra/core/state) — 仅白名单服务器
~/stcpp-ops/run.sh "curl -s 'http://inplay.goalserve.com/inplay-soccer.gz' | gunzip -c"
# state 字典
~/stcpp-ops/run.sh "curl -s 'http://inplay.goalserve.com/dictionaries/states/{soccer|tennis}'"
# soccernew/live (含 <event ts>) — 本机 key-in-URL 可拉
curl -s -x $GOALSERVE_PROXY "https://www.goalserve.com/getfeed/$KEY/soccernew/live"
# commentaries (含 substitution/var) — 本机可拉
curl -s -x $GOALSERVE_PROXY "https://www.goalserve.com/getfeed/$KEY/commentaries/1.xml"
```
