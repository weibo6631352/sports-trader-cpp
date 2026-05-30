# M1 路线评审会 — PaperDaemon 落地后下一步

- **日期:** 2026-05-30
- **主持:** 老雷 (GM)
- **议题:** PaperDaemon 重构落地 (commit 34cb464) 后, 锁定 M1 下一步主攻方向
- **出席 (立场供料):** 老周 (架构) / 老韩 (风控) / 小梁 (量化) / 老胡 (PM) / 老钱 (CPO)
- **owner:** 老雷  **last_review:** 2026-05-30

---

## 1. 现状

paper daemon 能跑但**零成交**: `paper_loop.cpp` M1 stub —— `time_status` 硬编码
`NotStarted` → `has_real_fair=false`, 叠加 `advisory_markets_no_intent=true`, 两道 gate
在产生 intent 前 return。只发 quote (edge 清零) + 攒 ML 数据 (predict_ok=false), 零 paper 成交。
保守正确 (P0-3 宁可空不可假), 但 MVP「第一笔 paper 成交」未解锁。

## 2. 候选方向 + 投票

| 方向 | 内容 | 票 |
|---|---|---|
| **A** | 接真实 fair value (Goalserve in-play → FairValueEstimator) → 第一笔 paper 成交 + ML 有信号 | 老周 / 小梁 / 老胡 / 老钱 (4) |
| B | Frankfurt 72h 长跑验收 (验稳定性) | 老韩 (1, 纪律>收益) |
| C | 补 backlog (发现刷新 / parser / ADR) | 0 (全员排第三) |

## 3. 评审炸出的两个关键发现

### 3.1 [老周·架构] 真正的隐藏 blocker = condition_id ↔ Goalserve event 映射桥

接 Goalserve 进 FairValue 的接线**半通, 断在最关键一节**:
- 已具备: `InplayFeedThread` → `ScoreSnapshotStore` 在跑 (R-12 合规);
  `FairValueEstimator` score-prior + de-vig + blend 算法就绪, 喂真 game_row 即出真 prior。
- **缺口 1 (致命):** `ScoreSnapshotStore` 的 key 是 `inplay_match_id`, paper_loop 手里只有
  gamma `condition_id`, **全 src 零处对上号**。`DiscoveredEvent` 连 home/away/start_ts
  都没有, 无法做 team-name + kickoff 锚定。
- **缺口 2:** `PaperLoop` ctor 根本没接 `score_store` (paper_daemon.cpp:202 没传),
  `game_row.time_status` 硬编码 NotStarted (paper_loop.cpp:249-250)。接线物理断开。
- **架构风险:** ① 错配下单 (张冠李戴比分 → 假 fair → 真亏), 映射必须 **fail-closed**
  (匹配不上退回 has_real_fair=false, 绝不猜); ② R-20: game_row 四戳现借 book ts,
  接真数据后必须切回 `EventScore.ts`, 否则 R-20 违规。

### 3.2 [老韩·风控] 合约矛盾 = advisory gate 拆不拆

`advisory_markets_no_intent` + `has_real_fair` 是**双防线**, 两道都拆才下单。老韩主张
「paper 恒 advisory (ML-R2 契约), advisory gate 绝不拆」。但 config 注释明确: advisory=false
是「has_real_fair=true 且真实模型接入后」的预期路径。若 advisory 永真 → **永远零成交**,
MVP「第一笔 paper 成交」不可能。**此为需 GM 拍板的合约解释 (见 §4 决议 D3)。**

## 4. GM 决议 (老雷拍板)

### D1 — 主攻方向: A (接真实 fair value)
4:1 多数 + mission 对齐 (老钱: 「72h 长跑只证明稳定地不赚钱」; 小梁: 「越早解封越早攒
有效训练数据」)。**A 锁为 M1 唯一关键路径, GM 亲写主线** (§10.2)。

### D2 — 次序: A 主线 + B 并行背景 + C 滚动
- **A**: GM 主线, 内部强串行 A0→A1→A2 (见 §5)。
- **B (Frankfurt 长跑)**: 采纳老韩「先证稳定」的精神, 但**不作为 A 的前置阻塞** ——
  stub 即可起跑验进程/链路/隔离稳定性, 与 A 完全解耦。**老吴并行背景起跑**,
  其暴露的 stale/freshness/重连分布反哺 A2 的风控阈值实测依据。
- **C (backlog)**: 各 owner 滚动消化, 不立项不进 M1 关键路径。

### D3 — advisory gate 合约澄清 (解 §3.2 矛盾)
裁定: **「advisory」语义 = 不自动路由 live, 非「不产生 paper 成交」**。paper 成交正是
paper 模式的目的与 MVP 验收项。故 A2 解封时:
- **拆 `has_real_fair` 这道** (接真 Goalserve game_row, fail-closed)。
- advisory gate 改为**仅拦截 live 路由, 放行 paper fill** —— 即 paper daemon 允许产生
  VirtualFill + 写私有 paper 账本, 但**永不向 Polymarket CLOB 下单** (ToS + R-7 build-time
  paper 锁双保险)。
- **此条须老韩复核签字** (§6 风控可叫停): 若老韩坚持 ML-R2 是硬红线, 升全体争议会。
  GM 立场: paper fill ≠ live order, 不构成红线; 但尊重风控主权, 实施前过老韩。

### D4 — 解封门: 老韩 8 条红线 checklist 前置
A2 解封 (第一笔真 intent 触发) 前, 老韩的解封 checklist 必须逐条绿:
① has_real_fair 仅真 game_row 触发 (非 stub 冒充); ② advisory 仅拆 live 路由 (D3);
③ edge_ci_lower 真信号无假阳性回归测试; ④ Kelly suggested_notional 低 edge/高 vig/极端价
不爆 size, 解封首日保留 demo notional 上限; ⑤ 三 cap + DD 软硬熔断 (-3%/-5%) 测过命中;
⑥ R-11 一笔受控 fill 实测 (写私有 paper ledger 不碰 live WAL, mode_tag/mode 运行时
enforce, **冷写路径首次走通**); ⑦ 每笔 reject/approve emit audit; ⑧ R-20 4ts 真 Goalserve
ts 下不退化 now()。任一不绿, A2 不放, 老韩单独叫停权。

## 5. A 的 ticket 拆解 + owner

| 步 | 内容 | owner | 阻塞 |
|---|---|---|---|
| **A0** | condition_id ↔ Goalserve event 映射桥: `DiscoveredEvent`/`EventInfo` 加 home/away/start_ts/sport + gamma 解析补字段 + `EventMatcher` (team 归一化 + kickoff 窗口锚定, **fail-closed**) | GM 主写, 数据组 **小余/小段** 供 Goalserve 字段口径 | 阻塞 A1 |
| **A1** | `PaperLoop` ctor 接 `score_store` + `TickOne` 用 condition→event→`Get()` 填真 game_row + R-20 四戳切 `EventScore.ts` | GM 主写, 架构 **老周** 评审 | 阻塞 A2 |
| **A2** | 解封 has_real_fair gate (fail-closed) + advisory gate 改 live-only (D3) + 过老韩 §4 D4 checklist → 第一笔 paper 成交 | GM 主写, **老韩** 风控复核签字 | MVP「第一笔成交」 |
| 量化 | 先验参数范围限定 (足球 alpha 0.25-0.35 / 篮球 0.10-0.15, 棒球/网球先排除); n_eff 保守 30-50; Goalserve 时延 P50/P95 监控 (数据组供) | **小梁** 供料 | A2 联调 |
| B | Frankfurt 部署预案 + 72h 长跑 (stub 起跑) | **老吴** 并行 | 不阻塞 A |

## 6. M1 达标线 (老胡, 6-25 评审前)

**本地 paper daemon 接真 Goalserve inplay, 单 Moneyline 市场 has_real_fair=true → 产出
≥1 笔过 RM 的 paper 成交 + 落 paper pnl_ledger + advisory(live)reject 清零。** 这一笔
成交 = M1 达标。Frankfurt 72h 长跑作为 M1→MVP 拉伸目标, 不绑 6-25。

## 7. scope 红线 (老钱)

**只接 Moneyline 单盘口 + 单一高流动性联赛**, fair value 用最简模型 (de-vig + 已有 prior),
**禁止借机上 totals/spreads/分节/ML 模型**。判据单一: 第一笔 paper 成交 + edge 分布可观测。
全盘口是 T+36 的事。

---

**待办 (GM 下一步):** 启动 A0 映射桥 (GM 亲写) + 派小余/小段供 Goalserve 字段口径 +
老吴并行起 B + 实施 A2 前过老韩签字 (D3/D4)。
