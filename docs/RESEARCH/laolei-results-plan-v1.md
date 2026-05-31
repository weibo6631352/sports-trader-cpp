# 成果方案 v1 — 打出第一个可测量成果

> owner: 老雷 (GM) · last_review: 2026-05-31 · 性质: 结果导向执行方案 (非特征清单)
> 老板指令: 「不用最低成本,先出成果方案。」
> 上位: laolei-strategic-vision-v1.md (北极星) · 对齐 CLAUDE.md MVP (Moneyline 实盘跑通+第一笔成交)

---

## 0. 成果的定义 (我们要打出的那个「结果」)

**成果 = 用 CLV 证明 in-play 延迟 edge 真实存在 → 第一笔正期望实盘成交。**

不是「跑通流程」,是**可测量的正期望证明**:
- **G1 (paper 成果)**: 真实市场 paper 上,in-play 延迟单的 **30 天 CLV 均值 > 1.5%**(老彭门槛),CLV>0 命中率 > 55%。
- **G2 (实盘成果)**: G1 达标 → RM armed → **第一笔真实成交**(tiny size),实盘 CLV 与 paper 一致。

**为什么是 in-play 延迟 edge 当第一个成果**(小梁/老彭收敛): 它 Sharpe 最高(2-3)、信号最清晰、命中率高,是「启动 alpha」—— 最快能打出可信成果、攒资本攒数据、验证整条执行链。$5M 主力(做市)是后话。

**为什么 CLV 是成果的尺子**(全员收敛): realized PnL 方差大、要等结算 1-3 天;CLV 方差小 1/5~1/10、比赛中就有信号、数据少也能评。**没有 CLV 测量,我们打出成果也不知道。**

---

## 1. 关键洞察: 策略已有,缺的是「快数据 + 测量」

In-play 延迟 edge 的机制 = **Goalserve 比分快 + Polymarket LP 慢**,进球后 30-120s LP 没调价,我们的 fair(快)已跳、市场(慢)还旧 → reservation 限价门过 → 吃便宜 → CLV 正。

**这个策略,控制器已经实现了**(进球 → game_row 比分变 → fair 跳 → has_real_fair → 选边 → reservation 过 → 建仓)。所以成果的关键路径**不是写新策略,是**:
1. **喂快数据**: 真实 Goalserve in-play 实时进 paper_loop(「快 fair」那一半)。
2. **建测量**: SettlementRecord(收盘 fair + winner)→ 算每笔 CLV。
3. **跑 + 证**: 真实市场跑 paper,累计 CLV,过门 → 实盘。

---

## 2. 关键路径 (里程碑 × 成功指标 × owner)

### M1 — 喂「快 fair」: 真实 Goalserve in-play → paper_loop (数据组主力)
- **做什么**: Goalserve inplay poller 实时拉比分 → ScoreSnapshotStore → paper_loop(已有 SetScoreStore 接口,已 wire)。确认 paper_daemon 真接了 live 比分(非 stub)。
- **成功指标**: paper_loop 的 has_real_fair=true 比例 > 0(真实 in-play 市场);进球事件 → fair 跳变在日志可见。
- **owner**: 小余(统筹)+ 小段(Goalserve poller)。**成本: 中**(已有 score_store 接口,需接 live poller)。

### M2 — 建「测量尺」: SettlementRecord 收盘/结算产品 (数据组)
- **做什么**: clob `/markets/{cid}` poller → 每 condition 写 `{close_ts, close_fair_mid, settlement_value(0/1), winner_token}`(小冯已设计)。给 CLV 算参考价 + 给 3b 权威 winner。
- **成功指标**: 已结算市场能拿到 close_fair + winner;3b 结算用 REST winner(已接 SetResolutionByCondition,只差喂数)。
- **owner**: 小余(settlement poller)。**成本: 低**(轮询 + 写 hub)。

### M3 — CLV 测量 harness: 每笔 fill 的 CLV (GM + 量化)
- **做什么**: 每笔 paper fill 记 `{entry_fair, entry_price, condition, entry_ts}`;事后 join SettlementRecord.close_fair → `CLV = close_fair − entry_price`(买被低估边视角)。聚合 CLV 均值 + CLV>0 命中率 + 按 alpha 来源(进球后/红牌/常规)分层。
- **红线(小蒋)**: CLV **只是离线评估 label,绝不进特征/实时推理**(需未来参考价=前视)。
- **成功指标**: 能输出「按 in-play 事件分层的 CLV 报告」。**这是成果的体温计。**
- **owner**: GM 写 harness + 小蒋定指标口径。**成本: 中**。

### M4 — 跑真实市场 paper + 调参 → 证明正 CLV (GM)
- **做什么**: paper_daemon 跑真实足球 in-play 市场(五大联赛 ML/Totals,老彭选的最肥盘口)。累计 fill + CLV。调 n_eff / reservation margin / 触发事件(进球/红牌)。
- **成功指标 (G1)**: **30 天 CLV 均值 > 1.5%,CLV>0 命中率 > 55%**,且进球后/红牌分层 CLV 显著正。
- **owner**: GM 主跑 + 老彭看操盘合理性 + 小蒋看统计显著性(防小样本假阳性,Bonferroni)。

### M5 — 第一笔实盘 (G2,GM + 风控)
- **做什么**: G1 达标 → 老韩 RM armed(真钱门,需人签:老韩+小白)→ LiveOrderGate.Arm() → tiny size 真实成交。
- **成功指标 (G2)**: 第一笔实盘成交;实盘 CLV 与 paper 一致(±方差内);零风控失效。
- **owner**: GM 执行 + 老韩 RM 签 + 小白 安全签(真钱开闸,§8.1 人签门)。

---

## 3. 关键路径图 (什么阻塞什么)

```
M1 快 Goalserve in-play ─┐
                         ├─→ M3 CLV harness ─→ M4 跑+证正CLV(G1) ─→ M5 首笔实盘(G2)
M2 SettlementRecord ─────┘                          ↑
                                          (老彭调操盘 + 小蒋防假阳性)
```

**M1 + M2 是并行前置(数据组),都到位 M3 才能测,M3 到位才能 M4 证成果。** M1/M2 是数据组多人活 → 必须正式派单立项(不是 GM 单干)。M3/M4 是 GM 主线 + 量化供料。

---

## 4. 这个成果路径上「不做什么」(避免散焦)

为了最快打出成果,以下**暂缓**(都进 backlog,不阻塞 G1/G2):
- 特征批 1-4(微结构/de-vig/sports 动态)—— 成果靠现有 fair 引擎够了,特征是后续 α 增强。
- 组合 Kelly —— 单盘口阶段单笔 Kelly 够(P2 做市才必须,见愿景)。
- 做市(B)、跨平台、链上钱包复制 —— 全是 P2/P3,不碰。
- L2 全档 / trade feed —— G1 不需要(in-play 延迟 edge 用 L1 + 比分就够),P2 微结构才要。

**成果聚焦: 一条线打穿 —— 快数据 + CLV 测量 + 现有控制器 → 证明正 CLV → 首笔实盘。**

---

## 5. 派单 (谁干什么,GM 拍)

| 里程碑 | owner | 类型 | 阻塞级 |
|---|---|---|---|
| M1 Goalserve in-play live | 小余 + 小段 | 数据组立项 | G1 前置 |
| M2 SettlementRecord poller | 小余 + 小冯 | 数据组立项 | G1 前置 |
| M3 CLV harness | GM + 小蒋(指标) | GM 主线 | G1 前置 |
| M4 跑真实市场证 CLV | GM + 老彭 + 小蒋 | GM 主线 + 供料 | = G1 |
| M5 首笔实盘 | GM + 老韩 + 小白 | 真钱人签门 | = G2 |

**月度策略评审(小梁主持)跟踪 G1 CLV 指标;G1 达标 → 全体争议会评 G2 实盘开闸。**

---

## 6. 一句话

**成果不是「跑通」,是「CLV 证明正期望 → 第一笔真钱」。策略骨架已有,关键路径是给它喂快数据(M1)+ 装尺子(M2/M3)+ 跑真实市场证明(M4)→ 实盘(M5)。先打穿这一条线,其余全是后续 α 增强。**
