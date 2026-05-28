# Dogfood 剧本 v1

- Owner: 小宫 (dogfood-tester)
- Last review: 2026-05-28
- 验收人: 老胡 (pm)
- 关联 ticket: S1-023
- 关联依赖: S1-001 (老周 架构) / S1-004 (老韩 RM) / S1-018 (小郑 metrics) / S1-022 (小尤 UX 评分卡)

---

## 0. 这份东西是什么 / 不是什么

**是什么:**
- 一个"操作员把系统从早跑到晚"的脚本.
- 不是测试用例, 不是 unit test, 是**真人坐在终端前每天要干的事**.
- 每条剧本都长这样: `HH:MM 做什么 → 看哪里 → 如果 X 触发就 Y`.

**不是什么:**
- 不是自动化测试规范 (那是小宋 S1-028 的活).
- 不是体感打分卡 (那是小尤 S1-022 的活, 我引用她的维度).
- 不是性能基准 (那是老姜 S1-011 的活).

**前提假设 (与老周架构 v0.1 对齐):**
- 主进程二进制名 `stcpp-trader`, 旁路三个 `stcpp-recorder` / `stcpp-recon` / `stcpp-metrics-agent` (老周 §8).
- 启动序列已由老周 + 老吴 (S1-010) 确认 = systemd unit `stcpp-trader.service`, 先起 metrics → recorder → trader → recon. **如有出入以老周拍板为准, 我留 placeholder.**
- 风控状态机 4 态: RUNNING / WARNING / HALTED / DRAIN (老韩 §4).
- Grafana 在 `http://ops.internal:3000`, 三个面板: `live-trading` / `risk-state` / `data-health` (小郑 S1-018, 命名我先占位).
- 操作员 CLI 工具 `stcpp-ctl` (我建议命名, 待老周/老胡确认), 含子命令 `status / halt / drain / resume-ack / cancel-all / replay`.

---

## 1. 新人冷启动剧本 (Onboarding)

> 假想读者: 公司新招的 ops/trader 助理, 看完 90 分钟 demo 能独立看面板 + 发出第一笔 dry-run 意向, **不允许碰真实下单**.

### 1.1 Day 1 — 90 分钟跑通 demo

| 时刻 | 动作 | 看哪里 | 通过条件 |
|---|---|---|---|
| 0:00 | 入职拿到 onboarding 包: 包含 `.env.example` 副本 (无真凭证), 公司宪法 `CLAUDE.md`, 本剧本, sandbox endpoint 列表 | 终端 + 浏览器 | 能 `cat CLAUDE.md` + 打开 Grafana 看到 login |
| 0:05 | 看完 CLAUDE.md 4 条铁律 + §8 红线, 当面问老胡一遍 "如果我看到 HALTED 我能干嘛", 答案: **只能撤单, 不能开仓, 不能 ack 恢复** | CLAUDE.md | 复述对 4 条红线 |
| 0:15 | 跑 `stcpp-ctl --version` + `stcpp-ctl status --sandbox` | 终端 | 能看到 `state=RUNNING (SANDBOX)` |
| 0:20 | 打开 Grafana 三个面板, 让老胡口播: "这是 book depth", "这是 PnL", "这是 risk state", "这是 heartbeat 三色灯" | Grafana | 能指认每个面板用途 |
| 0:30 | `stcpp-ctl status` 详读输出: bankroll / open orders / today's PnL / 三个数据源 freshness / NonceManager state | 终端 | 能解释 freshness 三个数字含义 |
| 0:40 | 跑 `stcpp-ctl replay --fixture s1-fixture-nba-2024-1015.bin --speed 4x` (沙盘回放 4 倍速 1 场 NBA), 看面板 PnL 曲线动起来 | Grafana live-trading | PnL 曲线动, audit log tail 持续输出 |
| 1:00 | 触发 1 次 mock-halt: `stcpp-ctl halt --reason "drill"` → 看面板变红 → `stcpp-ctl resume-ack --operator xiaogong --witness laohu` 恢复 | Grafana risk-state | 状态机走 RUNNING → HALTED → RUNNING, audit 两条 |
| 1:15 | 跑 1 次 "新人 5 问" 小考 (小尤的入门评分卡): "看到 freshness=45s 你做什么 / NonceManager mismatch 你做什么 / Grafana 全红你做什么 / 风控 reject 80% 你做什么 / 主进程崩溃你做什么" | 口头 | 5 题答对 ≥ 4 题 |
| 1:30 | demo end, 写一行 "今天最不爽的一件事" 进 `docs/RESEARCH/dogfood-feedback/dayN-xxx.md`, 同步钉/Slack 给小尤 | 文档 | 提交 1 条主观反馈 |

**90 分钟 PASS 标准:**
1. 能在 Grafana 三个面板间切换并说出每个图的口径.
2. 跑通了一次 mock-halt + 双人 ack 恢复.
3. 跑通了一次 4x 沙盘回放, 看到 PnL 动起来.
4. 5 问小考通过.

**90 分钟 FAIL 信号 (任一即 fail, 回炉):**
- 把 sandbox 当真盘操作.
- HALTED 状态下手贱试着发 intent.
- 看不懂 RiskManager `reject_code` 含义 (尤其 `STALE_DATA` / `EDGE_CI_NEGATIVE` / `INSUFFICIENT_BANKROLL`).

### 1.2 Week 1 — 跟班

| 天 | 内容 |
|---|---|
| D1 | 90 分钟 demo (上节) + 旁观老胡跑一次完整交易日 (§2) |
| D2 | 自己用 sandbox 跑一遍交易日剧本, 老胡背后看, 全程口播 |
| D3 | NBA 周末剧本沙盘 (§3) — 沙盘 4x 倍速, 1 小时跑完整周末 |
| D4 | NFL 周日 3-game slate 沙盘 (§4), 重点感受 13:00 三场同开的认知负担 |
| D5 | MLB 周日 10+ 场并发沙盘 (§5), 这是新人最大压力测试 |

Week 1 末交一份 "我看不懂的 5 个面板/指标" 给小尤 + 老胡.

### 1.3 Month 1 — 持证上岗

| 周 | 任务 |
|---|---|
| W2 | 影子模式: 全程在岗观察老胡, 不操作, 但写自己的"假决策日志" (我会怎么 ack), 跟老胡复盘差异 |
| W3 | 半正式: 操作员 = 新人, 老胡旁边只看不动, 任何动作必须**口头宣布再做** |
| W4 | 接 1 场低流动性 sandbox 实盘 (real CLOB + 实 USDC 的 testnet, 上限 $10/order), 跑通从开盘到平仓 |
| M1 末 | 老胡 + 小尤 + 老雷 三方面试, PASS 才能拿到生产 `stcpp-ctl` 权限 |

**Month 1 PASS 红线:**
- 至少独立处理过 1 次 STALE_DATA WARNING (不升级 HALTED).
- 至少独立处理过 1 次 NonceManager 不一致 (走 §6.7).
- 0 次 "看到红色面板不通知任何人" 的事故.

---

## 2. 正常交易日剧本 (Weekday, 单 sport, 2-3 场)

> 适用: 工作日, 美东傍晚开始的 NBA/NHL 场次, 或者亚洲早盘网球.
> 时间以**美东 ET** 为准 (与 Polymarket 服务器同区), 国内操作员对应 +13h (冬令时 +13, 夏令时 +12, **以老彭确认为准**).

### 2.1 时间轴 (美东 ET)

| 时刻 ET | 国内对应 | 动作 | 触发条件 |
|---|---|---|---|
| **08:00** | 21:00 | 进岗, 跑 `stcpp-ctl status` 自检: state / bankroll / open orders / 三源 freshness / NonceManager / WAL 上次 fsync | 看 freshness 三个都 < 5s |
| 08:05 | 21:05 | 打开 Grafana 三面板, 看夜间是否有 alert (alertmanager 历史), 翻 audit log 看夜里有没有 reject 异常激增 | grep `reject_code` 计数, 异常激增 → 报小尤 |
| 08:10 | 21:10 | 跑 `stcpp-ctl recon --since 24h`, 看链上 vs ledger 对账 | diff = 0 |
| 08:20 | 21:20 | 看老彭 / 小程发的 "今晚关注点" (signal watchlist) | — |
| 08:30 | 21:30 | 看 Goalserve pregame 拉的今日赛程 (`stcpp-ctl schedule --today`) | 有 X 场, 起开时间 |
| **赛前 6h** | — | 系统应自己从 Polymarket gamma 拉到这些 market_id 进 `active_markets` 列表, 我手动 `stcpp-ctl markets --status enabled` 复核 | enabled count 等于 schedule count |
| **赛前 1h** | — | 看 Grafana book depth 面板, 验证我们关心的市场盘口 WSS 都在更新 (freshness < 2s) | 不在 → 报老李 |
| **赛前 15min** | — | NBA: 18:15 ET lineup 出 → Goalserve 推送 inactive 名单 → 看 RiskManager 是否在 lineup 落地后短暂转 WARNING (新数据进来 stale window 重新计时), 再回 RUNNING | RM 状态正常切回 |
| **开赛 (T0)** | — | 比赛开始, inplay 数据流上, FeatureStore 开始喂 strategy. 我**不动**, 只盯 4 个数字: PnL / open_orders / reject_rate / heartbeat 三色灯 | 任何一个偏离 → §2.2 |
| **比赛中** | — | 每 15min 跑 `stcpp-ctl status`, 看 NonceManager 是否在上链 (nonce 在涨), bankroll 是否在抖动. 写一行口述记录到当日 journal | nonce 不涨且有挂单 → 报老孙 |
| **节间/换边** | — | NBA 节间是 sharp 进场窗口, 看 BboTracker 价差是否瞬时拉大 → strategy/mm 应主动撤单, 看 audit `CANCEL_BURST` 是否出现 | mm 撤单 burst 正常 |
| **比赛末段** | — | 结束前 5min, RM 会自动收紧 sizing (老韩 §4 WARNING 模式), 看面板 sizing 因子下降 | sizing 因子 ≤ 0.5 |
| **比赛结束** | — | 等 Polymarket UMA resolve (3-30min 不等), `stcpp-ctl positions --pending-resolve` 看待结算单 | 待结算单全部 resolve 后跑 `stcpp-ctl pnl --today` |
| **收盘 +30min** | — | 写当日 journal, 提交到 `docs/RESEARCH/dogfood-feedback/`, 包含: PnL / 最爽 1 件事 / 最不爽 1 件事 / 想砍的 1 个面板 / 想加的 1 个按钮 | 文件存在 |

### 2.2 异常分支 (交易日内出现时怎么办)

| 现象 | 我第一动作 | 升级路径 |
|---|---|---|
| Grafana data-health 黄色 (单源 freshness 15-30s) | 不动, 看 30s 内能不能恢复 | 30s 不恢复 → §6.4 / §6.5 |
| reject_rate 突然 > 50% | 看 audit log 前 100 条的 `reject_code` 分布 | 全是 `STALE_DATA` → §6.4; 全是 `INSUFFICIENT_BANKROLL` → 报老彭 (是不是策略 size 写错了) |
| NonceManager 不动但有挂单 | 不立刻撤, 先 `stcpp-ctl chain-status` 看 RPC | 60s 不动 → §6.8 |
| 单笔 PnL 异常 (一笔 > 日均 5x) | 立刻截图 + audit_id 发老韩 + 老彭 | 老韩判定是否暂停策略 |
| 操作员自己手抖按了 halt | 不要慌, 不要立即 resume, 跟老胡口播一遍 "我按了 halt 因为 X", 走 §6.6 流程恢复 | — |

---

## 3. NBA 周末剧本 (Sat-Sun, 美东 19:00-23:00 高峰)

> 重点: NBA 周六晚 + 周日下午 4-8 场并发, **22:00 ET 是西海岸大场和东海岸尾盘重叠的最尖峰**, 决策密度最高.

### 3.1 周六时间轴 (ET)

| 时刻 ET | 国内 | 动作 |
|---|---|---|
| 08:00 | 21:00 周六 | 例行自检 (同 §2.1) |
| 10:00 | 23:00 | 看老彭"今晚关注点", NBA 周六通常 8-12 场 |
| 13:00 | 02:00 周日 | (操作员 A 接班) 看 ESPN 是否有 load management 早爆料, 标红影响场次 |
| 17:00 | 06:00 周日 | NBA 18:00 lineup 公布前 1h 进入"高警戒", reject_rate 可能短暂飙 (lineup 进来时 stale 重新计时) |
| **18:30 ET** | 07:30 周日 | NBA lineup 公布, **PM 流动性 5-10min 内才反应** (老彭洞察), 这是我们的 sharp window. 看 reject_rate / sizing 是否在这 10min 内放大 |
| **19:00 ET** | 08:00 周日 | 东海岸场次开赛, 1-2 场, FeatureStore 喂数据稳定后系统应该开始报价 |
| 20:30 ET | 09:30 周日 | 中部场次开赛, 并发上到 3-4 场 |
| **22:00 ET** | 11:00 周日 | **西海岸开赛, 整晚最高并发 6-8 场**, 这是压力峰值 (见 §3.2) |
| 24:30 ET | 13:30 周日 | 东海岸尾盘进 4Q, 西海岸进 2Q, 同时段最 chaotic |
| 02:00 ET 周日 | 15:00 周日 | 多数场次结束, 待 resolve |
| 04:00 ET 周日 | 17:00 周日 | 周六全部 resolve, 跑 §7 复盘 |

### 3.2 22:00 ET 高峰检查清单 (我在 21:45 跑一遍)

```
[ ] stcpp-ctl status: RUNNING, bankroll > today_open * 0.95
[ ] Grafana data-health: 三色灯全绿 (poly_wss / goalserve / chain)
[ ] open_orders 数量 < per_market_cap * active_markets (没有挂单堆积)
[ ] WAL fsync lag < 100ms (老韩 §5.1 N=10 fsync)
[ ] log volume: <1k lines/sec (高峰会涨, 涨到 5k+ → 告警阈值)
[ ] CPU per-core util < 80% (单核飙 100% 意味着 NUMA 或 SPSC 堵了)
[ ] Polymarket WSS RTT p99 < 50ms (老吴 us-east-1 节点应该稳)
[ ] NonceManager pending tx < 5 (堆积超 5 个意味着 gas 估错或 RPC 卡)
[ ] 没有 active alert in alertmanager
```

任何一项不过 → 21:55 前必须有结论: 要么解决, 要么 `stcpp-ctl drain --reason "peak-prep-failed"` 进 DRAIN 模式跳过本峰.

### 3.3 周日剧本

周日 NBA 通常 13:00 ET 开始 (早场), 19:00-22:00 进入晚高峰, 流程同 §3.1 但 watch hour 提前 5h.

### 3.4 NBA 专属边缘场景

- **球星临场 inactive** (LeBron load management 经典): Polymarket 滞后 5-15min, 我们的 stale_detector **应该**因为新 odd 进来 reset, 但如果 Goalserve 没及时推 status, 我们就在用"老 lineup"报价 → 主观感受: 看到 PM 价格突变但我们没动 → 立即 `stcpp-ctl markets --pause <market_id>` 手动暂停该市场, 报小程 (是不是 signal 没接 lineup feed).
- **加时赛**: 单场 +5min, FeatureStore 需要处理 "时钟回 0 + 加赛 flag", 看 match_state 是否正常切到 OT, 不切 → 报小董.

---

## 4. NFL 周日剧本 (3-Game Slate, 13:00 ET 三场同开)

> 这是 NFL 的标志性场景. 周日 13:00 ET (美东早场), 通常 7-10 场**同时**开球, **三场被 sharp 重点关注的"slate"**就是我们要盯的.

### 4.1 时间轴 (ET)

| 时刻 ET | 国内 (冬令时, 周日 = 国内周一) | 动作 |
|---|---|---|
| **08:00 ET 周日** | 21:00 周日 | 进岗, 例行自检 |
| 10:00 | 23:00 | 看老彭推送的 3-game slate, 通常是 sharp side 集中的 3 场 |
| **11:30 ET** | 00:30 周一 | **NFL inactives 公布**, PM 早盘几乎死, 是 sharp 抢手区 (老彭 §3.2). 看 reject_rate, edge_ci 应放大 |
| 12:30 ET | 01:30 周一 | 赛前 30min, 检查 3 场 slate book depth, 应都有挂单 |
| **13:00 ET** | 02:00 周一 | **3 场同开**, 这是 Sprint-1 的最难场景 (见 §4.2) |
| 13:00-16:00 | 02:00-05:00 | 1-3Q 进行中, 我盯 4 个数字 + 切换 3 场 market view |
| **16:25 ET** | 05:25 周一 | 早场结束, 同时晚场 (Sunday Night Football, 国家电视) 开始预热 |
| 20:20 ET | 09:20 周一 | SNF 开赛, 1 场, 但流动性是周末最高的 |
| 23:30 ET | 12:30 周一 | SNF 结束, 等 resolve |

### 4.2 13:00 ET 三场同开特殊处理

**问题:** 3 场同时大量 WSS 消息涌入, 摄入层瞬时 burst. 老姜 §11 预算给的是稳态 20ms, burst 可能短暂破.

**我看的:**
1. SPSC drop counter (老周 §6.3) — 突发期 drop 增加但 < 10 帧/s 内可容忍, > 100 帧/s → §6.4.
2. feature pipeline lag — 通常稳态 < 5ms, burst 期 < 15ms 容忍, > 50ms 直接 `stcpp-ctl drain`.
3. RiskManager P99 latency — 单线程模型 (老韩 §8.1), 200us 硬线, 突破 → 立即报老韩.

**预案:**
- 12:55 ET 跑一次 "三场预热": `stcpp-ctl markets --list --filter "today,kick=13:00"` 应输出确切 3 场 (老彭 slate). 不是 3 场 → 报老彭确认.
- 13:00:00 ET 那一秒**我不动**, 让系统自己处理 burst, 我盯 SPSC drop + feature lag.
- 如果 13:00:30 时三场都建立稳定 book + 都开始报价 → 通过, 进常规巡查.
- 任何一场 13:01 还没 book → `stcpp-ctl markets --pause <market_id>`, 报老李 (WSS 订阅可能漏了).

### 4.3 NFL 专属边缘场景

- **天气突变**: 风速 > 15mph 总分 -3.5 (老彭 §2.2). Goalserve 推 weather 字段, 我们应自动 stale + 重定价. 看不到 weather field 触发 → 报小董检查 Goalserve schema.
- **故意吃罚旗 / clock kill**: 比赛末段 clock feed 异常常见, 看 `match_state.clock` 是否抖.
- **TV 广告暂停**: NFL inplay 流动性低 (老彭), book 可能 30s 无更新但比赛没暂停 → 这是个**正常**现象, **不应**触发 stale halt, 看 stale_detector 是否区分 "book stale" vs "match stale" (这是 §6.4 的关键).

---

## 5. MLB 周日剧本 (10+ 场并发)

> MLB 周日整天 13:00-23:00 ET 滚动开球, 通常**全天 12-15 场**, 不是同开但**长时间高并发** 8 场以上是常态.

### 5.1 时间轴 (ET)

| 时刻 | 动作 |
|---|---|
| 08:00 | 自检 + 看老彭 starting pitcher 关注名单 (SP 是 MLB 70% 信号源, 老彭 §2.3) |
| 11:00 | MLB SP 确认窗口, 看是否有 scratch (临场更换先发) → 系统应 detect SP 变化重定价 |
| **13:05 ET** | 早场 4-6 场开赛 (美东周日下午标准时间) |
| 16:00 | 中场 3-5 场叠加 (西海岸早场) |
| **19:10 ET** | 晚场 + 全国转播场 (ESPN Sunday Night Baseball) |
| 23:00 | 最后一场结束 |

### 5.2 高并发应对 (这是与 NFL 不同的)

| 维度 | NFL slate (§4) | MLB Sunday |
|---|---|---|
| 同开峰值 | 3-10 场 13:00 同秒 | 不同开, 但**12 小时窗口内 12+ 场**滚动 |
| 单场 inplay 频率 | ~150 plays/game, 慢 | ~250-300 pitches/game, 密 |
| 信号源依赖 | 多 (天气/伤情/spread movement) | 集中 (SP + 风向 + 击球数据) |
| 操作员压力 | 短时高强度 (3h) | 长时疲劳 (10h+) |

**MLB 的疲劳是真问题:**
- 单人**不能**连续 ops 超过 6h, 周日下午我和老胡轮班.
- 我 08:00-14:00 + 20:00-23:00, 老胡 14:00-20:00.
- 交班时跑 `stcpp-ctl handoff --from xiaogong --to laohu`, 输出 5 行 "你接手时的 5 个数字" — 这个 CLI 命令是我要提需求给老周/小郑 (列在 §11 backlog).

### 5.3 MLB 专属边缘场景

- **SP scratch (临场换先发)**: line 全盘重画 (老彭 §2.3). 看 RiskManager 是否短暂 DEFERRED 后恢复, 不恢复 → §6.4.
- **Doubleheader (一日双赛)**: 同两队同日两场, market_id 必须区分 game1/game2, 看 normalize 层 id_mapper 有没有混淆 (小余 §2.2) → 主观感受: 如果两场 line 是一样的, **几乎肯定是 bug**, 立即 pause + 报小余.
- **延期 (rain delay)**: MLB 雨延常见, match_state.status 从 IN_PROGRESS → DELAYED, 系统应自动 pause 该 market, 但**不**自动 cancel open orders, 等 status → CANCELLED 才撤. 这个状态机走对很关键, 见 §6.2.

---

## 6. 边缘场景剧本 (≥ 7 种)

> 这一节我会**故意激活**异常 (chaos drill), 与小宋 (test-replay) 配合, 她出自动化, 我出手工剧本.

### 6.1 比赛取消 (Cancelled)

**触发:** Goalserve 推 `status=cancelled` (例如台风/抗议).

**系统应:**
1. data/match 把该 game 标记 cancelled.
2. data/heartbeat 不再视为 stale (因为不期待更新).
3. RiskManager 收到 inplay 的 reject 所有 intent → market 也应被 markets manager pause.
4. exec 撤所有 open orders (这是 Polymarket UMA 还没 resolve 前的窗口).

**我的步骤:**
1. 看到 Goalserve cancel 推送 → Grafana data-health 该市场置灰.
2. 立即 `stcpp-ctl markets --status <market_id>` 验证 = CANCELLED.
3. `stcpp-ctl orders --market <market_id>` 应输出 0 open orders (系统已撤).
4. **如果还有 open orders** → 立刻 `stcpp-ctl cancel-all --market <market_id> --reason "game-cancelled"`, 报老李 (CLOB 撤单逻辑没接上).
5. 等 UMA 最终 resolve (取消通常 24-48h), 资金应原路退回, 跑 §7 复盘看是否退到位.

**主观感受要点:**
- 我**最怕的是**系统说 "市场取消" 但 Polymarket UMA 投票 resolve YES/NO (历史争议, 老彭 §10), 这时手工 audit 介入, 报老韩 + 老沈.

### 6.2 比赛延期 (Postponed / Suspended)

**触发:** MLB rain delay, NBA 球员场上重伤暂停 > 30min, NFL hurricane.

**关键区别:** 延期 ≠ 取消. 比赛可能晚 2h 重启, 也可能转明天.

**系统应:**
1. match_state.status → SUSPENDED.
2. **不**自动撤所有挂单 (因为可能很快恢复).
3. RiskManager → 该市场进 DRAIN (只允许平仓, 拒绝开仓).
4. heartbeat 改为长 stale 阈值 (30s 不再适用, 改 30min, **这是配置项还是硬编码我要问老韩** → 列 §11 backlog).

**我的步骤:**
1. 收到 Goalserve suspended → 看面板.
2. `stcpp-ctl markets --pause-open <market_id>` (新开仓 freeze, 老持仓保留).
3. 设个 1h 闹钟, 每小时 check `stcpp-ctl schedule --market <market_id> --refresh` 看新开赛时间.
4. **如果延到次日** → 跟老胡商量是否 close-out (撤单 + 接受当前 PnL) 还是 hold-overnight.
5. hold-overnight 是有 carry risk 的, 默认我倾向 close-out, 由老胡决策.

**实测 drill (Sprint-2 计划):** 用 `stcpp-ctl replay --inject suspended-at-q3` 注入 Q3 暂停事件, 看系统行为. 与小宋协同.

### 6.3 主庄暂停 / 盘口暂停

**触发:** Polymarket 主动暂停某 market (e.g. UMA 争议期, oracle dispute).

**信号:** WSS 推 `market_status=paused` 或 REST market detail 状态变.

**系统应:**
- 不再接受新 intent (RM 已读 market_status, reject `MARKET_TYPE_NOT_ENABLED` 或新拒因 `MARKET_PAUSED` — 老韩 §3.10 enum 需 v0.2 加).
- 持仓保留.

**我的步骤:**
1. 看面板该 market 状态变.
2. `stcpp-ctl markets --status <market_id>` 验证 = PAUSED.
3. **重点**: open orders 是不是被 Polymarket 系统自动撤了? 看 fill tracker 是否收到 cancel ack. 没收到 → 报老李.

### 6.4 Goalserve 断 30s (sharp window 杀手)

> 这是**最常见**的 chaos 场景, 也是 RiskManager STALE 阈值最敏感的边界.

**触发:** 我手工 `stcpp-ctl chaos --inject goalserve-down --duration 30s` (与小宋 chaos tool 协同).

**预期:**
- 0-30s: WARNING 状态, evaluate 返回 DEFERRED.
- 30-60s: 仍 WARNING (老韩 STALE_THRESHOLD_MS=30000, STALE_HALT_MS=60000).
- 60s 仍未恢复: HALTED, 全拒.
- 恢复后**不自动 RUNNING**, 需双人 ack (老韩 §4.2).

**我的步骤:**
1. T+0 注入.
2. T+5s 看 data-health 黄色, RM 状态 → WARNING. **通过.**
3. T+30s 看 reject_rate 飙 (DEFERRED 实际策略层不重试, 应跌到 0 throughput). PnL 横盘 (没新成交). **通过.**
4. T+30s 取消注入 (`stcpp-ctl chaos --revoke goalserve-down`).
5. T+35s 看 Goalserve 重连, heartbeat 恢复, RM 5min 持续 OK 后从 WARNING → RUNNING (老韩 §4.2).
6. T+5min 看是否真的自动回 RUNNING. **关键: 这是唯一的"自动恢复路径", HALTED 不自动恢复.**

**主观感受要点:**
- 如果 30s 恰好卡在我们 sharp window (e.g. NBA lineup 公布后 5min), 我们**错过了** sharp money 入场. 30s 阈值是否过短? 这是老韩 + 老郭 待会签 Q9. 我作为 dogfooder 的 input: **30s 偏激进**, 跨洋 jitter 极易触发, 但激进是好事 (D6 红线), 我倾向保留, 改为"WARNING 期内策略仍可下单但 size *= 0.5" 这种软退化, 但**这是策略层 (小梁) 的决定, 不是我能拍**. 我把这条作为反馈塞给老胡.

### 6.5 WSS 断

**与 6.4 区别:** WSS 是 Polymarket 主行情, 断了意味着 book 全死. 比 Goalserve 断更严重.

**触发:** 模拟 TCP RST.

**预期:**
- 0-5s: infra/net BackoffPolicy 自动重连 (老周 §2.1).
- 5-15s: 重连成功后**全量重订阅** (老周 §10), 看 book builder 是否重建 L2.
- 15-30s: stale_detector WARNING.
- > 30s: HALTED.

**我的步骤:**
1. 注入 WSS 断.
2. T+10s 看 net 模块自动重连日志 + book builder rebuild log.
3. T+15s book 应已 rebuild, freshness 应回 < 5s.
4. **如果 15s 没 rebuild** → 看 `infra/net` BackoffPolicy 退避是否过长, 报老陈.

**已知风险:** 重订阅期间挂单状态可能与 PM 服务端短暂不一致, 见 §6.7 nonce/recon.

### 6.6 风控熔断 → 恢复

**触发:** 连续亏损 5 笔 → CONSEC_LOSS_HALT, 或日亏 3% → DAILY_LOSS_HALT.

**系统应:**
- evaluate 全返 REJECTED(STATE_HALTED).
- 不允许自动恢复 (老韩 §4.2).
- 需双人 ack: 我 + 老胡 (Sprint-1) / 我 + 老雷 (生产), 双签 audit.

**我的步骤 (恢复):**
1. 收到电话告警 (老韩 §6.4 HALTED 立即电话).
2. 进岗, `stcpp-ctl audit --tail 20` 看最近 20 条 reject + 最后一条 STATE_TRANSITION.
3. 跟老胡口播复盘: "我看到 X 触发熔断, 我判断 Y, 建议 Z".
4. 老胡同意 → `stcpp-ctl resume-ack --operator xiaogong --witness laohu --reason "<one-liner>"`.
5. 状态机 HALTED → RUNNING 或 HALTED → DRAIN (DRAIN 更保守, 平仓优先).
6. **绝不在没复盘的情况下 ack**, 哪怕老彭催 "马上要错过 sharp window 了".

**红线:** 任何"为了赶上市场恢复"的 ack 都是违规, audit 会发现, 老韩会找上门.

### 6.7 Signer 崩 (KMS 失联)

**触发:** 模拟 KMS 服务 5xx.

**系统应:**
- exec/signer 调 KMS 失败 → router 报 error → RM 收到 fill-track 异常.
- 已 approved 但未签名的 intent → drop + audit reject `INTERNAL_ERROR` (技术性).
- 新 intent: RM 不知道 signer 死了, 仍 approve, 但执行层签不出 → 同样 drop.
- **关键**: nonce 不能跳号. 若 signer 部分成功部分失败, NonceManager state 是否乱?

**我的步骤:**
1. 注入 KMS 失败.
2. 看 sign_failure_rate metric 立即飙.
3. `stcpp-ctl halt --reason "signer-down"` **我手工 halt**, 不等 stale 超时. 因为 signer 死亡不是数据问题, stale 不会触发.
4. KMS 恢复后, `stcpp-ctl recon --since 5min` 看 nonce 是否一致.
5. 不一致 → `stcpp-ctl nonce --resync` (重新与链上对齐, 取 max), 由老孙双确认.
6. resume-ack 走 §6.6.

**主观感受要点:**
- Signer 死和数据死, **检测路径不同**, 这点新人极易混淆. 我建议 Grafana risk-state 面板加一个 "signer health" 子图 (与 freshness 三色灯并列). 列 §11 backlog 给小郑.

### 6.8 Polygon RPC 切备

**触发:** 模拟主 RPC (Alchemy) 5xx, 应切到备 (QuickNode) (老叶 S1-009).

**预期:**
- 5xx → 客户端检测 (老叶健康检查) → 自动 failover.
- 切备期间 (~3s) chain.OrderFillWatcher 短暂无 fill 更新.
- 切备后 fill 流恢复.

**我的步骤:**
1. 注入主 RPC 失败.
2. 看 `chain_provider_active` metric 从 alchemy → quicknode.
3. T+5s 跑 `stcpp-ctl chain-status` 应输出 active=quicknode.
4. **不**触发 HALTED (这是双 provider 设计的目的, 老叶 D8).
5. 如果切备失败 (两个都死) → 自动 HALTED 走 §6.6.

**Drill 频率:** 我每月跑 1 次, 与老叶 + 老吴 协同.

### 6.9 主进程重启 (状态恢复)

**触发:** `systemctl restart stcpp-trader.service`, 或 SIGKILL 强杀.

**系统应 (老周 §9.2):**
1. systemd 拉起.
2. 载 config TOML.
3. replay WAL → 重建 PositionLedger / NonceManager / open orders.
4. 启 RM (HALTED 启动, 不允许立即下单).
5. 连 RPC → 拉链上 nonce/持仓 → 与 ledger 对账.
6. 连 PM REST → 拉 open orders 实况 → 与 ledger 对账.
7. 对账一致 → 进 SAFE_MODE → 等 first WSS heartbeat → 解锁 RUNNING (需我手工 ack).
8. 对账不一致 → 卡 SAFE_MODE, 告警, 我介入.

**我的步骤:**
1. 重启前先 `stcpp-ctl drain --reason "restart-prep"` (能优雅就优雅).
2. `systemctl restart stcpp-trader`.
3. 看 systemd status, etc..
4. `stcpp-ctl status` 观察启动序列输出 (replay → recon → ready).
5. 启动完成后**主动**跑 `stcpp-ctl recon --verify` 双确认对账.
6. ack RUNNING.

**最严重案例:** WAL 损坏 → replay 失败 → 进程不起. 这时**必须**人工介入, 跑 `stcpp-tool wal-repair`, 由老陈/老周指导, 我**不自己来**.

### 6.10 (额外) 配置热改翻车

**触发:** 我手工改 `config/runtime.toml`, 把 `PER_ORDER_CAP_SOFT` 调高 (违反单调性).

**系统应 (老韩 §8.5, 老周 §7.2):**
- ConfigWatcher inotify 检测.
- schema 校验 + validate 钩子.
- 单调性违反 → 拒绝 reload + alert, 沿用旧 config.
- 旧 config 继续生效, **不**进 HALTED.

**我的步骤:**
1. 改文件.
2. 看 alert 在 1-2s 内弹.
3. `stcpp-ctl config --diff` 看 current vs file, 应输出 "rejected: SOFT cap monotonicity violated".
4. 改回, 看 alert 消失.

**这条主要是验证"配置错改不会炸生产", 是新人最容易犯的错.**

### 6.11 (额外) Recorder 落盘失败

**触发:** 旁路 `stcpp-recorder` 进程崩.

**系统应:**
- 主交易**不**受影响 (老周 §8.2 解耦).
- 回放质量受损 (失去这段录制).
- alert 立即弹.

**我的步骤:**
1. 看 alert.
2. `systemctl restart stcpp-recorder`.
3. 跑 `stcpp-ctl replay --verify --since "崩溃时刻"` 看录制完整性 (有几秒空洞是正常).
4. 告知小段 (replay 工具 owner) 漏的窗口.

---

## 7. 复盘剧本 (Post-Trade Review)

### 7.1 每日复盘 (收盘 + 30min)

**模板** (写到 `docs/RESEARCH/dogfood-feedback/YYYY-MM-DD.md`):

```
# YYYY-MM-DD 日 dogfood 日报
- Owner: 小宫
- Date: YYYY-MM-DD
- 操作员: <我自己 / 老胡 / 轮班>

## 1. 数字
- Bankroll 开 / 收: X / Y (Δ Z)
- 成交单数: N
- 拒单数 + 主要拒因 TOP3: 
- WARNING 次数: 
- HALTED 次数 (含 drill): 
- 三源 stale 累计时间: poly_wss / goalserve / chain

## 2. 最爽 1 件事
<一句话>

## 3. 最不爽 1 件事 (→ 谁)
<一句话> → 报到 <小尤 / 小宋 / 老胡>

## 4. 想砍的 1 个面板/按钮
<具体>

## 5. 想加的 1 个 CLI 子命令
<具体>

## 6. 触发的边缘场景 (§6 编号)
- §6.X: <怎么处理的>

## 7. audit_id 异常列表 (送老韩 / 老沈)
- <audit_id>: <一句话>
```

### 7.2 周复盘 (周日收盘后)

- 汇总 7 份日报.
- 跑 `stcpp-ctl replay --since "本周一" --speed 16x`, 加速看本周关键时刻 (HALTED / 高 reject 期).
- 输出 1 份周报到 `dogfood-feedback/week-W.md`, 给老胡 + 小尤.
- 标记 3 个**可砍的功能** + 3 个**想加的功能**, 送到产品 backlog.

### 7.3 Sprint 末复盘 (每 Sprint 最后一周五)

- 与 §9 节绑定, 跑核心 4 条 + 至少 2 条边缘场景.
- 出 sprint dogfood 报告 (5 页内): 跑通哪些 / 卡哪些 / 主观打分 (§8) / 给下个 sprint 的 3 条 actionable.

### 7.4 月度可用性热力图 (我的 persona §输出格式)

- 横轴: 模块 (data/strategy/risk/exec/infra + CLI + Grafana + alert).
- 纵轴: 体验维度 (易上手 / 易诊断 / 易恢复 / 文档清晰 / 错误信息).
- 颜色: 1-10 分 (§8 维度) 染色.
- 月底交给老胡 + 老雷.

---

## 8. 主观评分维度 (1-10, 小尤 S1-022 的扩展)

> 小尤的 UX 评分卡 (S1-022) 关注"体感", 我这里关注"在用是否流畅". 不重复, 互补.

| # | 维度 | 1 分 | 5 分 | 10 分 |
|---|---|---|---|---|
| D1 | **状态可见性** | 我不知道系统现在干嘛 | 知道 state, 但要翻 3 个面板 | 一眼就懂, 单屏 |
| D2 | **错误可解释** | 拒单原因看不懂英文/缩写 | 看得懂但要查文档 | 错误信息自带"下一步" |
| D3 | **CLI 上手** | 命令名记不住, 参数难 | 5 个常用记得 | 命令名 = 我会想到的词 |
| D4 | **告警精度** | 一天 50 条, 30 条是噪音 | 一天 10 条, 5 条噪音 | 一天 5 条, 全 actionable |
| D5 | **故障恢复时长** | 我不知道怎么恢复 | 有 runbook 但要翻 | 系统主动提示下一步 |
| D6 | **跨场切换** | 多场并发我会漏看 | 多场切换有面板支持 | 自动聚焦"应关注"的场 |
| D7 | **dry-run 友好度** | 沙盘和实盘命令不一样 | 命令一样, 标识不同 | 一个 flag 切换, 误操作不可能 |
| D8 | **审计可追溯** | grep audit 要写 awk | CLI 能查 | 链路图可视化 |
| D9 | **配置安全** | 误改能上生产 | 有校验但 alert 后置 | 校验在保存时, 错的根本进不来 |
| D10 | **疲劳负载** | 8h 后我开始漏 | 8h 后注意力下降但不漏 | 系统主动接管低风险决策, 我专注高风险 |

**评分给谁:**
- D1-D3, D5-D7, D10 → 报小尤 (UX 主观).
- D4, D8, D9 → 报老韩 (风控 / 运维强相关).
- D2 → 报老李 + 老韩 (错误信息文案).

**评分频率:**
- 每 Sprint 末打一遍 (10 个维度 × 1-10).
- 出热力图, 任何维度 < 4 = P1 issue, 与 owner 当周谈.

---

## 9. 每个 Sprint 末我必跑的剧本 (优先级排序)

### 9.1 必跑 4 条 (any sprint)

| 优先级 | 剧本 | 时长 | 为什么必跑 |
|---|---|---|---|
| **P0** | §6.6 风控熔断 → 恢复 | 30min | 红线兜底, 任何 sprint 都不能跳 |
| **P0** | §6.9 主进程重启状态恢复 | 30min | WAL + 对账, 是产品定海神针 |
| P1 | §2 正常交易日 (沙盘 4x 倍速) | 1h | 主流程基线 |
| P1 | §6.4 Goalserve 断 30s | 15min | 最常见 chaos |

**4 条加起来 ~2.5h, 周五下午跑.**

### 9.2 按 sport 季节加跑 (季节性轮换)

| 季节 (北半球) | 加跑 |
|---|---|
| Q1 (1-3) NBA 高峰 + Super Bowl | §3 NBA 周末 (沙盘) |
| Q2 (4-6) NBA playoff + MLB | §3 + §5 |
| Q3 (7-9) MLB 主场 | §5 MLB 周日 |
| Q4 (10-12) NFL + NBA 开季 | §4 NFL slate (重点!) |

### 9.3 季度必跑 (每 3 月 1 次, 我 + 小宋协同)

- §6.1 取消
- §6.2 延期
- §6.7 Signer 崩
- §6.8 Polygon RPC 切备

**这 4 条是低频但高影响, 每季度 + 重大版本前必跑.**

### 9.4 Sprint-1 末 (本 sprint, 2026-06-12) 最优先 1 条

**当前 sprint 还没有代码**, 所以我跑不了真系统. 我能做的是:

- **跑 §1.1 Day 1 90 分钟 demo 的"剧本预演"** (用伪命令 + 假面板 mock 一遍), 验证剧本本身是否可执行 + 老胡能否当 onboarding 教练.
- 输出: 1 份 "剧本预演反馈" 文档, 标出哪些命令我假设了但老周还没设计, 哪些面板小郑还没建 — 这些都喂回 sprint backlog.

**Sprint-2 末 (假设 RM skeleton + 1 个数据源 mock 跑通)**: 最优先跑 **§6.6 风控熔断恢复**, 因为它最纯粹是 RM 内部状态机, 不依赖完整链路, 也是 D-02 红线最直接的验证.

---

## 10. 与小尤 / 小宋 / 老胡的分工

| 我 (小宫) | 小尤 (UX) | 小宋 (test-replay) | 老胡 (pm) |
|---|---|---|---|
| 跑真流程 | 看真体感 | 写自动化 | 拍板 |
| 主观日报 | 评分卡 | 回放 + 注入 | 接 escalation |
| 边缘场景剧本 | 用户旅程图 | 把我的剧本变成 chaos 脚本 | 决定 sprint 跑哪几条 |
| 月度热力图 | 季度可用性报告 | 持续回归 | 月度 review |

**协作流程:**
- 我跑出"卡顿" → 小尤复评是不是 UX 问题, 是的就她接手 → 不是则到老胡.
- 我跑出"bug" → 直接报小宋, 她写最小复现 → 给到 owner (IC pool).
- 我跑出"流程错" → 报老胡, 老胡判定是产品定义还是实现错 → 分诊给老钱 / owner.

---

## 11. 我向上提需求的 backlog (S1-023 副产物)

| # | 需求 | 给谁 | 优先级 |
|---|---|---|---|
| B1 | `stcpp-ctl` CLI 整体设计 + 子命令清单 (status/halt/drain/resume-ack/cancel-all/replay/handoff/recon/chain-status/markets/config/audit/orders/pnl/schedule/chaos/nonce) | 老周 + 老胡 | P0 |
| B2 | Grafana 三面板命名落地: `live-trading` / `risk-state` / `data-health` + signer health 子图 | 小郑 | P0 |
| B3 | 长 stale 阈值 (suspended 比赛专用) 是 config 还是硬编码 | 老韩 | P1 |
| B4 | `RejectReason` enum 加 `MARKET_PAUSED` / `MATCH_SUSPENDED` (v0.2) | 老韩 | P1 |
| B5 | `stcpp-ctl handoff` 交班子命令 (输出"接手时 5 个数字") | 老周 + 小郑 | P1 |
| B6 | sandbox 与生产命令同名但需明确视觉区分 (终端配色 / prompt 前缀) | 老吴 + 小尤 | P2 |
| B7 | NBA load management feed 是否单独接 ESPN/Woj 通道 (Goalserve 慢 5-15min, 老彭 §7.3) | 小董 + 小程 | P2 (策略相关) |
| B8 | 跨洋夏令时切换日的双人值守剧本 (3 月第 2 周日, 11 月第 1 周日) | 老吴 + 老胡 | P3 |

---

## 12. 已知缺失 / 我不耻下问的清单

我承认有几个地方我自己不熟, 在 Sprint-1 内必须问:

| # | 问什么 | 找谁 |
|---|---|---|
| Q1 | 真实 NBA/NFL/MLB 各赛季开闭幕日期 (regular / playoff / off-season), 我剧本里写 "Q4=NFL" 是粗的, 要细到周 | **老彭** |
| Q2 | NFL slate 的"3-game"是行业惯例还是我们自己挑的? 怎么算"重点 3 场"? | **老彭** + **小程** |
| Q3 | 主进程实际启动序列 + systemd unit 名 + binary 路径 | **老周** + **老吴** |
| Q4 | `stcpp-ctl` CLI 是否真的会做, 还是我们走 web admin | **老周** + 老胡 |
| Q5 | sandbox 环境是否会有 Polymarket testnet, 还是只有沙盘 replay | **老李** + 老吴 |
| Q6 | onboarding 90min demo 用什么 fixture (历史哪场 NBA 我能 replay), 数据从哪来 | **小段** (replay 工具 owner) |
| Q7 | "双人 ack" 的"另一个人"在小公司怎么操作 (我 + 老胡 24h 不可能两人都在线) | **老雷** (S1-004 Q13) |

**Sprint-1 内我会逐个去问, 答复回写到本文档 v1.1.**

---

## 附录 A — Demo Day 命令清单 (假设 stcpp-ctl 存在)

```bash
# 自检
stcpp-ctl --version
stcpp-ctl status                    # 总览
stcpp-ctl status --json             # 程序化消费
stcpp-ctl recon --since 24h         # 对账
stcpp-ctl audit --tail 20           # 最近 audit

# 市场视角
stcpp-ctl markets --status enabled
stcpp-ctl markets --filter "today,kick=13:00"
stcpp-ctl markets --pause <market_id>
stcpp-ctl markets --pause-open <market_id>   # 仅冻新开仓
stcpp-ctl orders --market <market_id>

# 风控操作 (高危, 红色提示)
stcpp-ctl halt --reason "<text>"
stcpp-ctl drain --reason "<text>"
stcpp-ctl resume-ack --operator <name> --witness <name> --reason "<text>"
stcpp-ctl cancel-all --market <market_id> --reason "<text>"

# 链上 / nonce
stcpp-ctl chain-status
stcpp-ctl nonce --resync                # 与链上重对齐 (老孙手动操作)

# 回放 / chaos
stcpp-ctl replay --fixture <name> --speed 4x
stcpp-ctl replay --since "<ts>" --speed 16x
stcpp-ctl chaos --inject goalserve-down --duration 30s
stcpp-ctl chaos --revoke goalserve-down

# 交班
stcpp-ctl handoff --from <a> --to <b>

# PnL / 调度
stcpp-ctl pnl --today
stcpp-ctl schedule --today
stcpp-ctl schedule --market <market_id> --refresh

# 配置
stcpp-ctl config --diff
```

> **上述命令名是我作为"使用者的合理期望", 实际 owner 老周 + 老胡 拍板.**

---

## 附录 B — 与现有 Sprint-1 文档的对齐点

- 老周架构 §2 (5 层) → 本剧本 §6 边缘场景按层归责.
- 老韩 RM §3.10 RejectReason enum → 本剧本 §2.2 / §6 引用拒因.
- 老韩 §4 状态机 → 本剧本 §6.4 / §6.6 状态转移.
- 老周 §8 进程模型 → 本剧本 §6.9 重启恢复.
- 老周 §9.2 崩溃恢复链 → 本剧本 §6.9 步骤.
- 老吴 §1 主节点 us-east-1 → 本剧本 §2.1 / §3.1 / §4.1 ET 时间口径.
- 老彭 §2 各 sport 特性 → 本剧本 §3 / §4 / §5 sport-specific.
- 老彭 §7.3 PM 滞后 5-10min → 本剧本 §3.4 / §11 B7.
- 小郑 S1-018 Prometheus → 本剧本 §3.2 高峰检查清单引用 metric.

---

## END v1

下一步:
1. 老胡过一遍, 拍板要不要砍 / 加.
2. 按 §11 B1-B8 提需求 issue.
3. 按 §12 Q1-Q7 私聊找人问.
4. v1.1 在 Sprint-1 retro (2026-06-12) 前出.
