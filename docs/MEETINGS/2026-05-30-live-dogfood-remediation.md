# 全体 dogfood 整改会议纪要 + 计划

- **主持:** 老雷(GM/总裁)
- **日期:** 2026-05-30
- **触发:** 老板令 测试/运营/UX/量化AI 各组实地使用 live 系统反馈问题 → 总裁带各部门开会定整改
- **输入:** `docs/FEEDBACK/{xiaosong-test,xiaogong-ops,xiaoyou-ux,xiaoliang-quant,xiaodeng-ai}-feedback.md`
- **结论:** **当前 live 看板不具备实盘/操作员盯盘条件**,P0 红线先修,再走 paper gate。

---

## §1 问题清单(去重合并,按严重度)

### P0 — 红线 / 阻塞(必须先修)
| # | 问题 | 多组印证 | 根因 | owner |
|---|---|---|---|---|
| P0-1 | **拒单双写** 128 唯一 reject 各出现 2 次(纳秒级完全重复)→ 计数翻倍 | 小宋 BUG-4 / 小宫 B1 | RmDebugSnapshot push 路径被调用两次 | 老沈/小肖 |
| P0-2 | **R-20 四时间戳违反** 5/10 市场 `data_source_ts > ingestion_ts`(跨洋时钟偏差 10-29ms) | 小宋 BUG-8 / 小宫 B2 | ingestion_ts 用本地 recv 时刻,落后 Polymarket 服务端 ts | 小冯(数据路径) |
| P0-3 | **fair_value stub 是有害假信号** 低价 outright(Spain 0.169)被拉到 0.434 → 伪造 edge 1076bps + suggested_notional → 诱导买高估冷门(假阳性) | 小梁 P0 / 小邓 | paper_loop 无 Goalserve game_row,prior 恒 0.5 + 固定 kappa 混合 | 小肖(先 gate)+ 小梁(真模型) |
| P0-4 | **advisory 市场仍生成 BUY intent** 依赖 RM 兜底,RM 松动即真下单 | 小宋 BUG-5 | paper_loop 对 advisory-only 仍走 intent | 小肖 |

### P1 — 可观测失效 / 功能损坏
| # | 问题 | owner |
|---|---|---|
| P1-1 | `/api/v1/market/{cid}` 全 404(market 详情页废、accepting/tick/fee/event_id 拿不到) | 小卢(接真实 gamma 发现) |
| P1-2 | `/metrics` 多项恒 0(uptime/rm_reject/fill/staleness)→ Prometheus 全平无告警 | 小卢 |
| P1-3 | **WSS 状态矛盾** /status=false 但 book.wss_state="CONNECTED"(旧快照)→ 前端绿点误导 | 小卢 |
| P1-4 | WSS 全断 + book 数据 2.4h stale 无任何告警(staleness 恒 0) | 小卢 + 小冯 |
| P1-5 | 前端**赛事标题错乱** `title.split(' vs ')` 对 outright 拆不出 → 首屏全是错乱英文 | 小苏 |
| P1-6 | 前端 WSS 全断无全局 Alert(红点太隐蔽) | 小苏 |
| P1-7 | 前端 StaleDot 用 book_as_of_ts(恒新)非 event_ts → 延迟指示恒绿;book endpoint key 错配 isEndpointFailing 永不触发 | 小苏 |
| P1-8 | de-vig fair prob 缺失,edge 锚在带 vig 的 mid 上 | 小梁(量化) |

### P2 / 后续(W11 / 排期)
- score 全 miss(event_id=condition_id ≠ Goalserve ID + 当前全 outright 无 inplay)→ 小余/小段 映射 + outright 优雅"无比分"
- 真实 fair value ML 模型(接 Goalserve 比分)+ ONNX → 小梁/小邓(W11)
- AI 推理延迟 + drift 可观测 → 小邓(W11,接 ONNX 前先就位)
- sport 字段空(gamma futures 不填 tag)、净PnL$0/positions 空 语境文案、INVALID_INTENT i18n、敞口聚合/fill_rate 硬编码

---

## §2 整改计划(派单)

**本轮立即修(P0 + 关键 P1):**
- **小肖**(paper_loop):P0-1 拒单双写(去重 publish)+ P0-3 不在无真 fair/outright 时伪造 edge(无 game_row/低置信 → 不产 quote 假值、不产 intent)+ P0-4 advisory-only 市场不生成 BUY intent。
- **小冯**(数据路径):P0-2 R-20 ingestion_ts 时钟偏差处理(ingestion_ts = max(本地 recv, data_source_ts),保证单调)+ P1-4 staleness 真实计算。
- **小卢**(debug_api):P1-1 market() 接真实 gamma 发现数据(非 found=false)+ P1-2 /metrics 真实值(uptime/rm_reject/fill/staleness)+ P1-3 book wss_state 读 live 连接态非旧快照。
- **小苏**(前端):P1-5 标题错乱(outright 不 split ' vs ')+ P1-6 WSS 全断全局 Alert + P1-7 StaleDot 用 event_ts + book endpoint key 修 + 空态语境文案。

**后续排期(老胡 milestone 跟):** 真实 fair 模型 + de-vig(小梁/C 单元)、score 映射(小余/小段)、AI 可观测(小邓 W11)、advisory→RM enforce 复核(老韩)。

**验收门:** P0 修完重跑各组 dogfood + 全量 ctest 绿 + 重走 paper gate,方可议实盘。

---

*纪要 owner: 老雷(GM)。各 owner 修复走 worktree feature 分支,GM 集成 + 重验。*
