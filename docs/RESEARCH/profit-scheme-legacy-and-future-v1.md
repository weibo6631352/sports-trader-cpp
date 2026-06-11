# 盈利方案设计会 — 项目遗留问题 + 未来展望 (讨论输入)

> owner: 老雷 (GM) | last_review: 2026-06-03
> 老板要求: 盈利方案讨论必须结合「现在的遗留问题」+「未来没做的展望」。本文给专家 round-2 互批用。
> 来源: 本会话踩坑 + memory/ 记忆 + 代码库现状。

## A. 遗留问题 (设计新方案时必须正视/规避/复用)

### A1. 模型/定价
1. **结算预测 ML 欠校准** (本会话发现): AUC 0.84 排序对, 但输出压缩到 ~0.5(欠校准), 满权重驱动 → 假 edge → paper 出血 −$15。**教训: 排序≠校准; 新预测模型必做校准(isotonic/温度), C++ 侧目前只支持 Platt a/b。**
2. **derivative 定价格式 bug** (本会话, 已加守卫未根治): totals/spreads 模型单位错配 —— 网球总局模型撞 "Total Sets 2.5"(盘数 line)、电竞总图模型撞 "Total Kills 27.5"(击杀 line)、BO3 假设撞 BO5 → fair 钉 0.999/0.001 垃圾单。已加单位守卫 + 通用兜底(钉极端→市场兜底)。**根治需 per-子盘口建模 + BO 自适应。**
3. **sharp 覆盖缺口**: 只 tennis(87%)/soccer(73%)有 bet365 de-vig sharp;**basketball/baseball/esports/cricket = 0 sharp**(MatchWinnerSpec 缺 / inplay 无 odds / 格式不匹配)。新方案若依赖 sharp 收敛, 覆盖面受限。
4. **b<0.03 便宜尾部外推**: 低价市场模型外推假 edge。drop_decided 训练已滤, serve 侧仍需防。
5. **ML 特征死/缺** (memory ml-feature-validity-audit): 116 特征里仍有条件性死特征(bm_slots 大管线等);网球 games 本会话刚补(v0.14)。
6. **fair 显示与决策脱节** (本会话): grid/前端 `fair`=score-prior(fv_result), 决策 `p_fair`=ResolveFair(sharp/ML), 两者解耦 → UI 误导, 看着像"sharp 没用"。新方案要统一显示口径。
7. **score 语义逐运动核对**: 网球 sets vs games(刚补), 电竞 maps, 足球 goals, 棒球 runs —— 喂模型前需逐个确认单位正确(见 A2 derivative 同源教训)。

### A2. 数据/覆盖/匹配
8. **覆盖/匹配 candidate-bound** (memory coverage-candidate-bound): 匹配率受候选池 + 分母虚高限制, 非名字匹配 bug; 杠杆 = 加 feed + 按运动收窄分母(22.8%→53%)。
9. **回测只覆盖 1/6 输入** (memory architecture-review): event_replayer 只回放部分输入, **无 walk-forward, 无全 PIT 验证**。新预测模型必须先有真 walk-forward 回测才可信。
10. **冷门盘流动性薄**: 吃单滑点大;甜区是大联赛/流动盘(memory why-no-trades)。
11. **跨洋链路高延迟 + 请求预算** (memory frontend-request-budget): 前端/采集有请求预算; 决策热路径禁阻塞 IO>100us(红线 R-12)。

### A3. 系统/流程
12. **paper 不出单历史真因** (memory why-no-trades): 是无 alpha(覆盖/sharp/模型三缺)非 bug; 转向后需重新评估"双边做市+激励"是否绕开这个瓶颈(激励不依赖 alpha → 可能绕开)。
13. **WSS 心跳/重连** (memory clob-wss-heartbeat): 已修(看门狗补心跳/重连/半死检测)。做市挂单依赖 WSS 稳, 这条是地基。
14. **R-11 paper 不污染真账本** + **真钱开闸需会签**(老韩 RM + 小白安全): live plumbing 在但 disarmed。
15. **订阅生命周期/黑名单**(memory subscription-lifecycle): final→退订+释放+拉黑, 按 event_id+TTL。

## B. 未来展望 (没做的, 该不该纳入新方案)

1. **时序/序列价格预测模型** (现焦点): tick 序列(book/score/sharp 轨迹)→ 预测未来 Δt 价。M5(torch+MPS 已装)训, ONNX 服务器推理。数据实测可行(296 市场/841 tick/5s)。
2. **★ 流动性奖励做市策略** (本会话官方文档新发现): $5M+/月体育电竞激励, 双边贴中点二次奖励 + maker 零费 → **结构性利润线, 不依赖预测精度**。详见 polymarket-mechanics-verified-2026-06-03.md。
3. **maker rebates**: taker 费 20–25% 返 maker, 叠加在 #2 上。
4. **自建 MCP**: polymarket-mcp(老李)/ goalserve-mcp(小段), Sprint-2。
5. **观测栈**: prometheus/grafana/loki/tempo, Sprint-3(小郑)。做市策略强依赖实时盯单/撤挂监控。
6. **online learning v2** (小邓): 在线更新替离线重训。
7. **真钱 live 开闸**: 老韩 RM + 小白安全会签(§8.1)。
8. **derivative 子盘口根治** + per-sport/per-market 模型扩展。
9. **多腿/跨市场套利**: 同赛事不同盘口、neg_risk 多结果间。
10. **walk-forward 回测框架**: 新预测模型的可信度前置(配套 A2-9)。
11. **sharp 覆盖扩展**: 更多运动接源(配套 A1-3)。

## C. 给讨论的关键张力 (round-2 重点辩)

- **预测驱动套利 vs 做市赚激励**: 哪个是主利润线?激励($5M/月+零费)可能比"预测价差套利"更稳更大, 且绕开"无 alpha"瓶颈(A3-12)。但激励要求双边贴中点报价 → 逆向选择风险(sharp 跳动扫单), 这里恰恰需要预测模型避险。**两者是互补还是主次?**
- **覆盖现实**: sharp 只覆盖网球/足球(A1-3), 但激励池覆盖所有体育/电竞盘 → 做市激励的可做面 >> sharp 套利可做面。是否先吃激励(广)再叠套利(深)?
- **小样本/过拟合**(A2-9): 296 市场训序列模型易过拟合; 做市激励策略对模型精度要求低 → 是否先上规则化做市(立即能跑), 模型预测作为避险增强后置?
