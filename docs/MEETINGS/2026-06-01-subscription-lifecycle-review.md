# 订阅生命周期评审 + 退订机制纠正 (2026-06-01)

> owner: 老雷 (GM) · 评审: 老周 (架构主权) + 算法B (状态机/流程) · last_review: 2026-06-01
> 触发: 老板「主要订阅已开赛的盘口, 比赛结束的不订阅甚至退订」+「不能优雅退订吗」

## 背景
WSS 订阅范围两个病: ① 订阅集曾从全部发现盘构建(已被 discovery 时间窗 gate 修复, commit 1d4f908);
② 比赛结束如何退订。

## 评审结论 (老周 架构主权)
1. **订阅 gate 锚在 discovery 时间窗(在打+imminent≤1h), 不是 match 集**。match 集驱动订阅会误杀
   电竞/未覆盖运动 + 自咬尾巴(没订→没 book→永不匹配→永不订) + 匹配抖动死循环。**拒绝 match-gate**。
2. **订阅集已收敛**(实测 ~250-372 market, 非 18181), 不需大改。
3. match 集 = 下单授权子集, 单向消费 book, **绝不反向驱动 subscribe**。
4. R-12: 退订/重连放映射刷新线程(非 WSS event loop), 零风险。

## 算法B 贡献的安全护栏 (无论何种退订都要)
- **持仓盲点闸**: 有未平持仓的盘**绝不退订**(book 断=持仓变瞎), 等平仓/结算才放。**硬规则**。
- **feed 健康闸**: inplay feed 挂(429/白名单)时别把全部盘误判结束退掉。

## ⚠️ 关键纠正: Polymarket **支持优雅退订**, 我们 spec 过时了
- 老李 `laoli-w9-wss-subscriber-impl-spec-v1.md:229` 写「无官方 unsubscribe frame, 唯一方法关闭重连」——**已过时**。
- **2026-06-01 对照 Polymarket 官方文档 + 官方 real-time-data-client/agent-skills 仓库确认**:
  market 频道支持动态增删订阅, 不用重连:
  ```jsonc
  { "assets_ids": ["token"], "operation": "unsubscribe" }                              // 退订
  { "assets_ids": ["token"], "operation": "subscribe", "custom_feature_enabled": true } // 加订
  ```
  源: https://docs.polymarket.com/market-data/websocket/overview ;
      https://github.com/Polymarket/real-time-data-client ;
      https://github.com/Polymarket/agent-skills/blob/main/websocket.md
- **教训**(已记 memory [[verify-against-authoritative-source]]): 拿自己代码没实现/旧 spec 当"协议不支持"
  = 又一次"内部结论当事实"。老板「不能优雅退订吗」的追问直接推翻了错误前提。

## GM 决定 (最终方案 — 因 unsubscribe 存在而极简化)
- **比赛结束(discovery 剔除该盘)→ 对其 token 发 `operation:unsubscribe`** → 单盘停推, 其他盘 book 不断流。
- 新盘 → `operation:subscribe` 追加。**无重连、无 book blip、无累积、无撞上限风险**。
- **持仓盲点闸保留**: 有持仓不发 unsubscribe。
- **作废**之前讨论的「攒着 + 定期重连 flush」「2h 清一次」「每日重启」——unsubscribe 存在后全不需要。
- **落地前置**: 现有订阅帧是老格式 `{"type":"Market","assets_ids":[...]}`(能用); 需**实测** `operation:unsubscribe`
  帧在真 WSS 生效(发帧→看该 token 帧是否真停)再落地。

## 当前状态 (2026-06-01)
- 订阅范围: discovery 时间窗已落地(commit 1d4f908: 在打+imminent, 剔已结束/远期/outright)。
- 优雅退订(operation:unsubscribe): **待实测 + 落地**。
- 匹配: matched 0→9(Goalserve Roland Garros 网球 ∩ PM 网球盘); 剩余未匹配排查中(名字 vs 结构 vs 覆盖)。
