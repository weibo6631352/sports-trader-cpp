# 会议纪要 — 盘口上下文对象设计会

> owner: 老雷 (GM) · last_review: 2026-05-31 · 类型: 架构/数据建模评审
> 召集背景: 老板提出「不管观测/训练/决策,盘口的参考信息应含①父级市场引用 ②双边 YES/NO 订单簿」。
> 与会(并行供料): 老周(架构主权) · 老郭(架构否决权) · 小邓(ML) · 小石(QuoteFeatures owner) · 小程(量化信号)

---

## 1. 诊断核对 — GM 诊断一半已过时(关键纠正)

| 平面 | 双边 book | 父级引用 |
|---|---|---|
| 静态目录 (EventInfo→MarketInfo→Token) | ✓ | ✓ **完整** (event_id/neg_risk/condition_ids[]/兄弟) |
| 观测 (BinaryMarketBookView, ADR-040) | ✓ token0+token1+cross_spread | ✗ 仅 condition_id |
| **决策 (BinaryMarketSnapshot)** | ✓ **已落地** (老周 2026-05-31, 栈上双边 SideView, de-vig 真读两边) | ✗ |
| 训练 (FeatureSnapshot 33维 / QuoteFeatures JSONL) | △ 标量投影 (top3_yes/no) | ✗ |

**结论:GM 诊断「三平面普遍塌单边」高估了。决策层双边 book 老周上周已做(`binary_market_snapshot.hpp`),观测层亦已双边。真缺口收窄为一条:三平面齐缺「父级上行引用」。**

## 2. 架构拍板 (老周 + 老郭 一致)

- ❌ **不做「三平面共用单一 canonical struct」** —— 三平面物理约束冲突无法合一:决策 `QuoteFeatures` 是 `static_assert(trivially_copyable)` 的 R-12 double-buffer POD(禁 std::string/禁变长 book);训练 `FeatureSnapshot` 是固定宽 float + column 锁死;观测 `BinaryMarketBookView` 是带 string/vector 的 JSON DTO。合一必破 R-12 或炸训练宽度。老郭additional:这等于复活老周 v1 §2 已架构否决的「condition 级聚合 slot」(破坏单边刷新无锁性)。
- ✅ **SSOT + 三投影**:SSOT = market_discovery 目录(结构真相) + 两 hub(报价真相);三平面各自投影,只在需要处加**轻量父级 ID**,需全量时回查目录。
- **父级引用存 ID 不内嵌;兄弟变长一律不进热路径/训练 POD。**
- **双边完整 book 不内嵌进 QuoteFeatures/FeatureSnapshot** —— 只活在决策栈上 BinaryMarketSnapshot(输入) + 观测 BinaryMarketBookView(展示)。

## 3. 时机 (老郭 实盘优先 + 全体共识)

**父级引用(neg_risk 兄弟 / 跨盘口一致性)在 moneyline MVP 阶段不咬** —— moneyline 二元市场不用 neg_risk 多腿,单盘口无 event-join 需求。动 QuoteFeatures/FeatureSnapshot ABI = 为未上线盘口提前付费。→ **父级引用 plumbing 推迟到「第二类盘口(neg_risk/outright/同event多盘口)milestone」。**

## 4. 但有 MVP-正向、便宜、无需父级引用的子集值得现在做

小石 + 小邓 + 小程 三方收敛到同一批「便宜且现在就有收益」的改动:

1. **【小程 #1, MVP 最高 ROI】cross_spread → 动态 n_eff**:当前 `n_eff` 是固定配置值,宽 vig(流动性差)市场仍可能产 `edge_ci_lower>0` 假信号。把已有的 `BinaryMarketBookView.cross_spread` 接入 paper_loop,按实时 vig 宽窄动态调 CI 宽度。估计降流动性差市场假阳性 ~20-30%。不需父级引用。
2. **【小石】回收被丢弃的 NO 边信息进 QuoteFeatures**:de-vig 用了 NO 边 microprice 后**当帧丢弃**。加 `no_token_microprice / cross_spread / devig_ok`(+`condition_id`/`event_id` 定长 char,保 trivially_copyable)。+~208B,R-12 不破。恢复可追溯性。
3. **【小邓 P0】双边微观结构标量进 QuoteFeatures**:`book_imbalance(双边)` / `microprice` / `best_bid_size` / `best_ask_size`。**关键洞察**:模型现在有 `slippage_bps`/`expected_fill_rate` 作输出,却无微观结构输入 → ML 天花板被规则模型 SlippageModel 锁死。补输入解锁 α。
4. **【小邓 关键纠正】FeatureRecorder 攒的是 `QuoteFeatures`(JSONL),不是 33 维 FeatureSnapshot(parquet 还是 W6 stub 没生产 writer)。新特征必须先进 `QuoteFeatures` 才真攒到数据;只进 FeatureSnapshot enum = 写空 schema。**

## 5. 红线/纪律

- QuoteFeatures 改动必须保持 `static_assert(is_trivially_copyable)` 绿(新增父级引用必须定长 char[],禁 std::string)。
- FeatureSnapshot 若动:append-only 禁 renumber,bump spec_version,同步小田 DWH(中间插入≠加性, 触发 R-4)。
- §8.1 carve-out:QuoteFeatures 纯末尾加性 + PR 通知小邓(ML)/小程/观测下游 → 免会签普通 PR;但 FeatureRecorder JSONL 列变更是对外数据 schema,必须显式通知。
- state_provider.hpp(G-FREEZE-W)改动单 owner 串行(§10.1 #2)。

## 6. 行动项

| # | 项 | owner | 时机 |
|---|---|---|---|
| A1 | cross_spread → 动态 n_eff 接入 paper_loop | GM 写 + 小程评审 | 现在 (MVP 正向) |
| A2 | QuoteFeatures 加 NO 边回收 + 双边微观结构标量 + condition_id/event_id | GM 写 + 小石/小邓评审 | 现在 |
| A3 | FeatureRecorder JSONL 同步记新字段 | GM 写 + 小邓评审 | 随 A2 |
| B1 | 父级引用 plumbing (event_id/neg_risk + 决策接 EventInfo.condition_ids) | 老周排期 | **推迟**→ 第二类盘口 milestone |
| B2 | neg_risk 组一致性 / 跨盘口(ML×spread×totals)一致性 信号 | 小程/小蒋 | 推迟 → M2 |
| B3 | FeatureSnapshot ABI(父级 + 对称特征) | 小邓/小田 | 推迟 → 需会签 |
| C1 | 数据可得性确认: QuoteFeatures 上游能否拿 best 两档 size | 小石/小肖 | A2 前置 |

---

# Round-2 — 升维评估「所有数据=一棵树」+ 直播源节点

> 老板补充主张: ①「咱们所有数据本质是一棵树」②直播源(Goalserve)也进树挂 Event 节点。
> 与会: 老周(架构) · 小余(数据 SSOT/ETL 主权, round-1 未参加) · Goalserve 专精。三人独立同结论。

## R2-1. 「一棵树」= 逻辑对、物理错(三人一致)

- ✅ **逻辑树 = SSOT** —— 老板的「树」就是老周 round-1 的「market_discovery 目录 SSOT」,同一回事。批准为**导航/投影/共享语义的逻辑模型**。
- ❌ **物理一等树(GameTree struct + 整树锁/整树刷新)= 否决** —— 三源频率差 3 个数量级(book 毫秒 / score 秒 / discovery 几十秒),焊成一棵树 = 整树锁撞 **R-12(WSS 锁>100us=P0)** 或整树 COW 写放大(跨洋扛不住)。物理永远是**三独立 RCU store(score by inplay_match_id / book by token_id / catalog by condition_id)+ EventMatcher 概率桥**,树是它们的**惰性 join 投影**。
- **关键精修(老周):树到 token_id 为止是「拓扑骨架」(慢变);book/score 是「挂在骨架上的流式投影」(高频),用 ID 关联、物理分离。绝不能把 book/score 塞进 discovery 目录对象**(慢变结构承载高频写=撞 R-12/G-FREEZE-W)。
- **一等树对象 / Sport 顶层节点 = 过度设计,不做**(现状 map+ID 引用够用;Sport 在 Polymarket 无独立实体)。

## R2-2. 直播源挂 Event 节点 = 真问题 + 确认一个 bug(三人一致)

- **确认 read-skew bug**:`TickAll` 对每个 condition 独立 `score_store_->Get()`;采集线程在两次 Get 间 swap → 同 event 的 moneyline 看 2:1、spread 看 2:2 → fair 基于不一致比分(小程 round-1 指控成立)。
- **修法**:`TickAll` 入口 `GetSnapshot()` 取一次冻结快照,tick 内全子盘口共享。改动极小(接口已有),只读 RCU 零新锁**不碰 R-12**,根除 read-skew。「挂 Event 节点」的正确工程翻译 = 共享快照,非物理树。
- **时机分歧**:老周「现在做」(独立正确性 bug);小余「MVP 后」(单盘口无兄弟、窗口不存在不阻塞 MVP)。

## R2-3. 小余补的数据质量缺口

EventMatcher 是 fuzzy 队名+时间窗匹配、fail-closed,**join 边是概率性的会断会翻转**。`EventMapEntry` 现在只有 `{inplay_match_id, yes_is_home}`,**丢了 match_confidence + match_as_of_ts**。树的 Event→Market 边应一等暴露 confidence/orientation_certain/as_of,否则埋静默数据质量陷阱。加性 append(§8.1 carve-out),可现在补。

## R2-4. R-20 红线(小余 + Goalserve 一致)

树是**多 ts 共存的森林**不是单 ts 统一树。每节点带自源 4ts(Event/score 来自 Goalserve updated_ts;Token/book 来自 CLOB WSS),join 取较旧 as_of。**严禁树根造「全树统一 as_of=now()」往下灌 = 踩 R-20 P0**。现状 paper_loop 已正确(切真 Goalserve ts 整组一起切)。Goalserve inplay feed 天然 per-event(updated_ts 整批共享),挂 Event 节点语义成立。

## R2-5. 不改 round-1 结论

SSOT+投影不合一仍成立,升维只是给了「树」的语言。**净落地增量**:TickAll 共享 score 快照(消 read-skew)+ EventMapEntry 补 confidence/as_of(加性);其余(父级引用形式化/一等树/Sport 层/物理 GameTree)不落代码,只入认知对齐。

---

# 合并行动项(两轮)

| # | 项 | owner | 时机 | 性质 |
|---|---|---|---|---|
| A1 | cross_spread → 动态 n_eff | GM+小程 | **现在** | MVP 正向,降假阳性~20-30% |
| A2 | QuoteFeatures 回收 NO 边 + 双边微观结构标量(imbalance/microprice/best size)+ condition_id/event_id | GM+小石/小邓 | **现在** | 解锁 ML 天花板 + 喂训练数据 |
| A3 | FeatureRecorder JSONL 同步记新字段 | GM+小邓 | 随 A2 | 训练数据 |
| A4 | TickAll 共享 score 快照(消 read-skew) | GM+高频 | 现在/MVP后 | 正确性 bug,单盘口不咬 |
| A5 | EventMapEntry 补 match_confidence/as_of | GM+小余 | 现在 | 加性,数据质量 |
| B1 | 父级引用 plumbing(event_id/neg_risk + 决策接 condition_ids) | 老周排期 | 推迟→第二类盘口 milestone | |
| B2 | neg_risk/跨盘口一致性信号 | 小程/小蒋 | 推迟→M2 | |
| B3 | FeatureSnapshot ABI(父级+对称特征) | 小邓/小田 | 推迟→需会签 | |
| ✗ | 物理 GameTree / 一等树对象 / Sport 层 | — | **永不做** | 过度设计/撞 R-12 |

**GM 拍板**: 待定。
