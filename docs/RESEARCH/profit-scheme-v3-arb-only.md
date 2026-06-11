# 盈利方案 v3 (重新梳理) — 纯 in-play 事件延迟套利 (不做市)

> owner: 老雷 (GM) | last_review: 2026-06-03 | 状态: 老板 2026-06-03「不做市,重新梳理」拍板
> 取代 v2-FINAL 的"三引擎复合"。**engine B(双边做市)全部砍掉**(风控数据裁决:风险大/尾部咬人/卡检测地基/延迟非救星,详见 mm-risk-assessment-data-v1.md)。
> 保留 v2 的事件套利(engine A)+ 预测(engine C 为套利服务)+ 时序 + LLM 参数发现。

## 0. 一句话
**只吃单套利、不挂单做市:in-play 事件(进球/得分)→ bet365 sharp 瞬跳 → PM 散户滞后 ~13s → 在窗口内吃低估边 → 收敛后平仓。** 已证实的正期望 alpha,maker 零费把 BE 砍到 1.4%。

## 1. 为什么砍做市(数据裁决,一句)
全量 197 万 tick 实测:做市 carry 身体微正但**尾部崩盘每 300 场吃掉 3000 场 carry**,且**撤单触发器形同虚设**(sharp 仅 11% 覆盖、92% 大移动无前兆)—— 命门是检测延迟不是网络延迟,14ms 救不了。砍做市 = 去掉逆选-quoting 整个风险面 + 系统简化。**纪律 > PnL。**

## 2. 重排后的结构(无做市)

### 地基(P0,最优先,套利命脉)——「检测」
- **消费 inplay `info.state` 事件码**(11003进球/11008点球/11006红牌/网球破发,**已抓进 rec.gs_state_code 但下游零消费** ← 低垂果实)+ stats/extra 计数。这是套利【触发】的命根。
- **提升 sharp(bet365 de-vig)覆盖 + 降 book 新鲜度**(当前 sharp 11% / 新鲜度 1.9-5s)。覆盖越广套利可做面越大。
- 零新增 endpoint/带宽(用已拉的 inplay feed),只扩 parser。

### 引擎 A — 事件延迟收敛套利(唯一核心 alpha)
- **触发**:`info.state` 跳变 / stats 计数跳变 → 事件驱动抢窗(替 500ms 轮询)。
- **进场**:taker 吃低估边(bet365 sharp vs PM microprice 的 gap,扣 bet365 偏见 0.7-1.2pp)。
- **出场**:被动限价(maker 零费,BE 1.4%);deadline 未成交 → taker 平(BE 回退,告警)+ OpenLegLedger 强平防退化成赌。**注:这是单腿执行,不是做市(无常驻双边报价、不吃激励、无 quoting 逆选面)。**
- **范围**:有 bet365 in-play 的 tennis(连续重定价,真甜区)/ soccer(进球冲击大但簿薄,大联赛大错价小 size)。
- **门**:`ci_low − BE > 0`(arb_signal 已写)+ net-EV + 延迟红线。
- **命门**:① PM 进球时 `acceptingOrders` 连续性(suspend→死;测试组被动观测,零下单风险)② G2 真进球收敛复测。

### 引擎 C — 预测(为套利服务,不再为做市)
- **主预测量 y_h** = `sign(gap)·(mid(t+Δ)−mid(t))`(收敛方向+幅度),gap=bet365_fair−microprice;horizon {5,10,12,15}s(12s≈中位滞后);+ σ(CI 下界 sizing)+ fill-prob。
- **落地**:规则基线 → LightGBM quantile(直出 CI 下界)→ **时序序列模型(顶点;GRU/小 Transformer 吃 tick 轨迹,M5/MPS 训→ONNX 服务器推理 per-market 缓冲)**。walk-forward + PIT,isotonic 校准。
- (jump_h 危险头原为做市避逆选,做市砍了 → 降级为可选的"事件早检测/前兆",边际价值,后置。)

### A类瞬时锁套利(机会快路径,非做市)
- `YES_ask+NO_ask < 1` 时双买锁无风险差(`x_arb_free_edge>成本`)。低延迟下值得试(虽与 <50ms bot 竞争)。这是【吃单套利】不是做市。

### LLM 参数·特征发现回路(离线,贯穿)
- 大模型离线调 A 的旋钮(horizon/BE/bet365 偏见系数/事件码→信号映射/阈值)+ 发现特征 + 回测归因 → walk-forward 数字闸门验证才采纳。**绝不进热路径(§12.4)。**

## 3. 分阶段路线(数字闸门)
- **P0 验证**:① 测试组观测进球前后 PM `acceptingOrders` 连续性(引擎A 生死)② 扩 InplayScoreParser 消费 info.state/stats(已捕获未消费,低风险纯加法)。
- **P1 检测地基**:事件码消费打通 → 事件触发管道(替 500ms 轮询)+ sharp 覆盖/新鲜度提升。
- **P2 引擎A 套利**:事件触发 → bet365 偏见修正 → tennis/soccer 事件窗吃错价;G2 真进球验证 + walk-forward(≥5 折)才上。
- **P3 引擎C 模型**:y_h 规则→GBDT→时序序列模型;LLM 回路并行调参。
- **P4**:A类快路径 + 真钱开闸(老韩 RM + 小白安全会签)。

## 4. 砍掉的(v2 → v3)
engine B 全部:双边做市 / mm_sizing / RR-MM-* 红线 / 激励做市 / 状态机接力 / 幻影簿处理 / 做市甜区分析。**流动性激励/holdingRewards 不主动追**(taker rebate 0.25 仍作吃单副产品自动到账)。

## 5. 风险面变化(砍做市后)
- **去掉**:逆选-quoting 风险(做市最大风险)、库存爆、幻影簿送期权、做市尾部崩盘。
- **剩(套利执行风险,可控)**:fill/滑点(exit_depth 门)、G2 命门未验、PM 进球 suspend(acceptingOrders fail-closed)、退化成赌(deadline 强平)、bet365 单家偏见(修正系数)、sharp 覆盖窄(可做面小)。
- **诚实规模**:套利单独是"小而正"(操盘手 round-1 估 $5-50/天量级),离 $5M 北极星远。规模化靠**扩 sharp 覆盖(更多运动/源)+ 资本 + 后续新 alpha**,不靠做市。先把这条干净正期望的 alpha 跑稳、验真(G2 + CLV)。

## 6. 脚手架
已有复用:arb_signal/sizing/risk(RJ-ARB 门)、OpenLegLedger 强平、position_controller(限价不追)、walk-forward + walk_forward_ic、mid_return_label 双时钟、CLV、连续 Kelly、EventMatcher 映射、inplay feed 采集。
缺(新建):InplayScoreParser 扩 state/stats 消费、事件触发管道(替轮询)、market parse acceptingOrders、y_h 标签精化、序列数据管道 + 序列模型(M5→ONNX)、bet365 偏见修正、walk-forward 补全、LLM 参数发现回路。
