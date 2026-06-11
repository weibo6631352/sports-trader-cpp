# 盈利方案 v2 (终稿) — 三引擎复合 + 状态机接力

> owner: 老雷 (GM, 主持) | last_review: 2026-06-03 | 状态: 设计会收敛终稿
> 输入: round-1×4(finance 小梁/trader 操盘手/quant 小袁/protocol 老李)+ round-2 红队×4 + 延迟实测 + Polymarket 机制核实 + Goalserve 事件调研。
> 配套: polymarket-mechanics-verified-2026-06-03.md / profit-scheme-{finance,trader,quant,polymarket}-{v1,round2}.md / goalserve-event-interface-research.md / profit-scheme-legacy-and-future-v1.md

## 0. 一句话
**用 <10ms 低延迟 + bet365 in-play 速度优势 + maker 零费,做「事件延迟收敛套利(alpha)+ maker 双边做市(carry)+ 双头预测(大脑)」三引擎复合,单盘状态机接力调度。**

## 1. 论点收敛(设计会四轮辩论结论)
1. **本质 = 事件延迟收敛套利**(B类时间维度):进球/得分 → bet365 in-play 价瞬跳 → PM 散户滞后 ~13s → 我们抢窗口吃低估边。连续预测裸 Δmid 已证伪。
2. **"双边" = 执行选边 + 库存灵活 + 做市双挂,不是同盘对冲锁定**(YES+NO=1 对冲=负期望)。
3. **延迟决定一切**:实测网络 RTT **3-6ms**(非跨洋)→ 撤单跑赢逆选 → **做市从"必排除"翻成"可做"**(推翻 round-2 基于 200ms 的反做市结论)。
4. **maker 零费是连接组织**:套利出场腿免费 → BE 从 2.8% 砍到 **1.4%**(对错价 2-5% 安全边际变厚);做市 spread 留存转正。
5. **激励是 cherry 不是主利润**:逐市场配置(gate `rewardsMaxSpread>0`)+ pro-rata 稀释(小 size O($0.1-1/场))。真做市利润 = maker 零费 spread + rebate 0.25。
6. **脚手架已 90%**:arb_signal/sizing/risk + OpenLegLedger + position_controller + walk-forward + CLV 本就为"预测 Δmid→套利"建。

## 2. 事实底座(实测/核实,数字说话)
| 项 | 真值 | 来源 |
|---|---|---|
| 网络 RTT → CLOB | **3-6ms** | 老雷实测 |
| maker 费 | **0(永不收费)** | 官方文档 |
| taker rebate→maker | **0.25** | 老李实测 |
| 体育激励 | 逐市场;`rewardsMaxSpread`=2.5¢ `rewardsMinSize`=100sh;57/100 盘开 | 老李实测 |
| 单场池 | NBA $7.7k / EPL $10k(赛前+盘中分) | 官方 |
| in-play 撮合 | PM 有 `acceptingOrders` 字段(进球是否 suspend **待观测**) | 老李 |
| 事件源 | `inplay-<sport>.gz` 的 `info.state`(11003 进球等,已抓未消费)+ stats/extra,挂 inplay_match_id | 小段 |
| in-play sharp | **bet365 单家 square book**(非共识,有 home/over 偏 0.7-1.2pp) | 操盘手 |
| feed 新鲜度 | inplay ~1.9s 陈旧(非广告 1s) | 小段 |

## 3. 三引擎设计

### 引擎 A — 事件延迟收敛套利(alpha / 深 / 窄)
- **触发**:`info.state` 跳变(进球/点球/红牌/破发)或 stats 计数跳变 → 抢窗(替 500ms 轮询;事件驱动)。
- **动作**:taker 吃低估边进 → maker 挂出场(BE 1.4%);bet365 偏见修正(出场目标扣 0.7-1.2pp);OpenLegLedger deadline 强平防退化成赌。
- **范围**:仅有 bet365 in-play 的 tennis/soccer(sharp 覆盖限制 A1-3)。
- **预测量**:`y_h=sign(gap)·(mid(t+Δ)−mid(t))`,gap=bet365_fair−microprice;horizon {5,10,12,15}s;LightGBM quantile → CI 下界。
- **门**:`ci_low − BE > 0`(arb_signal 已写);net-EV;延迟红线。
- **命门**:① PM 进球时 acceptingOrders 连续性(suspend→死)② G2 真进球收敛复测。

### 引擎 B — maker 双边做市(carry / 广 / 稳)
- **动作**:双 BUY(YES_buy + NO_buy)贴中点(≤2.5¢ spread)、≥100 shares,各锁各 notional。免 split/inventory/wash。
- **赚**:maker 零费 spread 留存 + rebate 0.25 + 激励(cherry)+ a+b<1 双买白送。
- **避逆选**(命门):危险头(引擎C jump_h)预测 bet365 将跳 → 提前撤/偏;3-6ms 下撤单跑赢。无模型时三阈值规则兜底:① |sharp_gap|>k·spread 撤被扫腿 ② goal_freshness 高全撤 10s ③ book 陈旧不挂。
- **准入 gate**:`rewardsMaxSpread>0`(逐市场)+ 厚簿 + 平静期。**甜区**:pregame(激励独占)+ 极价区(mid<0.15/>0.85,逆选被价格地板夹住)+ 棒球(得分稀疏=平静长);**禁** near_half + 冷门薄簿。
- **sizing**:非 Kelly-edge,用「激励收益 vs 库存 VaR」约束(mm_sizing,Avellaneda-Stoikov 简化;逆选预算 Q≤0.067·B)。
- **ToS**:撤改节流(hold ≥ 一个 ~60s sample),非 spoofing/wash。

### 引擎 C — 双头预测(大脑,A/B 共用)
- **两头一 backbone**(小袁,正交不可合一):
  - `jump_h` 危险检测(无符号,预测 bet365 未来跳幅 max|Δsharp|,high recall,喂引擎B 撤单)horizon {3,5}s
  - `y_h` 机会择时(有符号,预测收敛,high precision,喂引擎A)horizon {5,10,12,15}s
- **落地**:规则基线先上(引擎B 三阈值,今天可写)→ jump_h 危险头(低样本,recall 优先,ROI 最直接)→ y_h 机会头(walk-forward,≥5 折)→ 序列模型(GBDT 基线站稳后)。M5(torch+MPS)训,ONNX 服务器推理,isotonic 校准(C++ 仅 Platt a/b,校准教训 A1-1)。

### 调度 — 单盘状态机接力
- **平静期 → 引擎B 做市收租**;**事件触发 → 引擎B 全撤 + 引擎A 进场吃错价**;**抢完 → 切回B**。
- 两引擎甜区时间轴相反(B=静/A=动)→ 完美分工非矛盾。
- **self-trade prevention**(套利吃单别吃自己做市单,wash 嫌疑)+ 资金物理分账(B_arb:B_mm 起 70:30)+ 同 condition 引擎互斥。
- **低延迟新机会**:A类瞬时锁(`x_arb_free_edge>成本`)快路径(白送,最高优先,不需模型)。

## 3b. 研发回路(贯穿三引擎,离线,不进热路径)

### 时序模型(引擎C 的顶点,不是可选项 — 老板首要目标)
- **为什么必做**:GBDT/规则只看单 tick 快照;**事件延迟套利和避逆选本质是「轨迹形态」问题**(价格怎么演变、bet365 跳前有无前兆、收敛快慢)。只有吃 tick 序列(book/score/sharp/事件码 的时间轴)的序列模型(GRU/小 Transformer,causal mask,参数 <50k)能抓到。jump_h(危险)/y_h(机会)两头都升级成序列版。
- **落地**:数据实测可行(296 市场/841 tick/5s,持续增长);M5(Apple M5,torch+MPS 已装)训练 → 导 ONNX → 服务器 C++ 推理(状态:每市场维护 tick 缓冲)。先 GBDT 基线站稳 OOS IC,再上序列(296 市场撑不起大模型,样本边攒边升)。walk-forward + PIT 纪律不变。
- **序列推理的热路径成本**:比无状态 predict 复杂(per-market 序列缓冲 + 变长),归 C++ 工程;但仍 µs-ms 级,不违延迟红线(模型在本地推理,非外部调用)。

### LLM 参数·特征发现回路(老板:大模型做参数发现 — 离线)
- **角色**:大模型【离线】当「研究员/优化器的提案引擎」,**绝不进每-tick 决策**(违延迟红线)。三件事:
  1. **特征发现**:从原始 tick 数据 + 领域知识,提案新特征(如"bet365 跳前的 book imbalance 前兆""事件码×时段交互""跨盘口联动"),工程实现 → walk-forward 验证 → 留下有 IC 的。
  2. **参数发现**:对策略大量旋钮(引擎A: horizon/BE/bet365 偏见 0.7-1.2pp/deadline/事件码→信号映射;引擎B: 最优价差 s*/撤单阈值 k/goal_freshness 窗/per-market gate;sizing: λ/逆选预算/VaR cap)做 LLM-in-the-loop 搜索 —— LLM 读 walk-forward 回测结果 → 推理提案下一组配置 → 跑回测 → 反馈迭代(比网格/贝叶斯更懂结构)。
  3. **回测结果归因**:LLM 分析亏损样本/失效模式 → 提案修正(如"near_half 失血→收紧 gate""某联赛 bet365 偏见特别大→单独修正系数")。
- **机制**:回路 = 历史/paper 数据 → LLM 提案(特征/参数/归因) → walk-forward 验证(数字闸门) → 采纳/否决 → 迭代。可工程化为 agent/workflow 跑在回测之上。**输出是配置 + 特征代码,人/数字闸门把关后才上,LLM 不直接动生产。**
- **红线对齐**:§12.4 LLM 仅离线;回测=实盘同逻辑(R-5);所有 LLM 提案必过 walk-forward 才采纳(不信 LLM 拍脑袋,信数字)。

## 4. 分阶段路线(数字闸门,赢了才进下一步)
- **Phase 0 验证(挡门,基本无代码风险)**:① 测试组被动观测进球前后 PM acceptingOrders + 订单簿连续性(引擎A 生死)② 实测激励真实到手份额 ③ 扩 InplayScoreParser 读 info.state/stats/extra(已捕获未消费,低风险)。
- **Phase 1 引擎B 规则做市**:双 BUY 安全区(pregame+极价区+厚簿)+ 三阈值避逆选 + rewardsMaxSpread gate + mm_sizing。无模型,立即收 carry,攒数据,解除引擎A 过拟合压力。
- **Phase 2 引擎A 事件套利**:info.state 事件触发管道 + bet365 偏见修正 + G2 → tennis/soccer 事件窗。
- **Phase 3 引擎C 模型**:jump_h 危险头(开引擎B 中价区)→ y_h 机会头(walk-forward)→ **时序序列模型(M5/MPS,引擎C 顶点,GRU/小 Transformer 吃 tick 轨迹)**。
- **Phase 4**:A类快路径 + 持仓激励(holdingRewardsEnabled 76%)评估 + 真钱开闸(老韩 RM+小白安全会签)。
- **贯穿(Phase 1 起并行)LLM 参数·特征发现回路**:有了 walk-forward 回测 + paper 数据就启动 —— LLM 离线提案特征/参数/归因 → 数字闸门验证 → 采纳。加速 A/B/C 三引擎调参 + 喂序列模型特征。绝不进热路径。

## 5. 资源对照
- **已有(复用)**:arb_signal/sizing/risk(RJ-ARB 门)、OpenLegLedger 强平、position_controller(reservation 不追价)、walk-forward + walk_forward_ic、mid_return_label 双时钟、CLV tracker、连续 Kelly、WSS 心跳看门狗、EventMatcher 映射、fill_rate_model(adverse_selection_score 可复用 mm)。
- **缺(新建)**:InplayScoreParser 扩 state/stats/extra 消费、market parse rewardsMaxSpread/MinSize/acceptingOrders、mm_sizing、jump_h 危险头、单盘状态机、事件触发管道(替轮询)、self-trade prevention、isotonic 校准(C++)、激励份额观测;**序列数据管道(tick→per-market 序列)、序列模型(M5/MPS 训→ONNX 序列推理,per-market tick 缓冲)、walk-forward 框架补全(现只覆盖 1/6 输入 A2-9)、LLM 参数·特征发现回路(离线 agent/workflow over 回测)**。

## 6. 风险/红线(加性,落已有 RJ-ARB/RiskConfig 包络,§8.1)
RR-MM-1 做市库存 VaR≤0.5%/condition · RR-MM-2 逆选预算≤0.3%/场 · RR-MM-3 事件窗禁挂(撤后才套利)· RR-MM-5 撤单触发器存在性 · RR-FUND-1 资金分账 · RR-FUND-2 引擎互斥 · RR-BE-1 出场 maker BE+0.5%margin · RR-BE-2 强平腿 taker BE 回退告警 · RR-MM-6 冷门薄簿禁做市 · **acceptingOrders fail-closed(suspend→不下单)· self-trade prevention · ToS 撤改节流**。套利 VaR/Kelly/强平阈值不变。

## 7. 遗留问题处置
- 引擎B 绕开 sharp 覆盖缺口(A1-3)+ "无 alpha"瓶颈(A3-12,激励/spread 不靠预测精度)。
- maker 零费治 BE(A 边际薄)。walk-forward 治过拟合(A2-9)。isotonic 治校准(A1-1)。
- derivative 格式 bug(A1-2):新方案不靠 derivative fair(靠 bet365 sharp + 市场 microprice),守卫保留即可。
- 跨洋认知纠错(A2-11→已实测 3-6ms,需改 CLAUDE.md §13 + 老姜 latency doc)。

## 8. 待办 P0 核实(GM WebFetch / 测试组,挡在写代码前)
- PM 进球 acceptingOrders 连续性(测试组被动观测)· reward 是否认 BUY-NO 为 NO 侧满额深度(老李 R2-Q1)· in-play suspend 时长(R2-Q5)· postOnly 语义 · CLOB rate-limit 真值(引擎A 高频)。
