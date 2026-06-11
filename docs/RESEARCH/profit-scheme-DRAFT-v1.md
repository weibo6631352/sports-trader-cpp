# 盈利方案 草案 v1 (round-1 综合 → 待 round-2 红队批判)

> owner: 老雷 (GM) | last_review: 2026-06-03 | 状态: DRAFT,等专家红队攻
> 综合 4 份 round-1(finance 小梁 / trader / quant 小袁 / polymarket 老李)+ 官方机制核实 + 遗留/未来。
> 这是【靶子】,不是结论。专家任务 = 攻它。

## round-1 共识(4 人一致,可信度高)
1. **本质 = 事件延迟收敛套利(B类)**:进球/得分 → bet365 in-play 价跳 → PM 散户滞后 ~13s → 5-15s 窗口吃低估边。**连续预测裸 Δmid 已证伪**(无事件 60s 内中位移动 0 / 够本仅 2.9%,扣费负期望)。
2. **"双边仓位管理" ≠ 同锁双边**:YES+NO=1 下同盘对冲 = 净 0 + 双费 = 负期望。真"双边" = 执行层选最优边 / 库存平仓灵活(买对边代替卖本边)。A类无风险锁(a+b<1)被 <50ms bot 秒杀,跨洋抢不到(`x_arb_free_edge` 留作 feature)。
3. **脚手架已搭 90%**:Phase 1-5 短时套利引擎(mid_return_label 双时钟 / seq_arb_model per-horizon / arb_signal·sizing·risk / walk-forward / RJ-ARB 门 / OpenLegLedger 强平 / 连续 Kelly λ=0.10)本就为"预测 Δmid→套利"建。
4. **预测量**(小袁定):`y_h = sign(gap)·(mid(t+Δ)−mid(t))`, gap=inplay_sharp−microprice;horizon {5,10,12,15}s(12s≈中位滞后);LightGBM quantile → CI 下界。
5. **边际薄**:中价 taker/taker BE≈2.8% vs 错价 2-5% → 勉强正;甜区 = 极端价位(BE 1.7c);**禁 near_half(BE 6.45c)、禁冷门/cricket/esports 流动性陷阱**。
6. **上线前置**:G2(真进球数据验证 PM 收敛可预测)+ 样本不够(296 市场/20.5h,需再攒 1-2 周)。

## ★ round-1 盲点(3/4 不知道,重塑方案)
**官方文档核实**(polymarket-mechanics-verified doc):**maker 永不收费 + $5M/月流动性激励(体育电竞全覆盖,赛前/盘中分池 pro-rata)+ 双边贴中点二次奖励(单边只 1/3 分,c=3)+ taker 费 rebate。** 小梁 round-1 "排除做市"是在【不知道激励】下做的判断 —— 激励把做市从"被逆选的成本方"变成"结构性拿钱的利润方"。

## 草案:双引擎 + 一个大脑(待攻)

### 引擎 1 — 流动性激励做市(广 / 结构性 / 可先上 / 不依赖预测精度)
- 双边挂 **BUY YES + BUY NO 限价单**(都是买,不需做空 → 绕开 controller long-only 限制 + 绕开 split/merge 缺失),贴中点、够 min_incentive_size。
- 赚:① 激励池份额(全体育电竞,远 > sharp 套利的网球/足球)② maker 零费 ③ taker rebate ④ a+b<1 时双买白送。
- **绕开两大遗留瓶颈**:sharp 覆盖缺口(A1-3)+ "无 alpha"(A3-12)—— 激励不靠预测精度。
- 风险:逆向选择(bet365 跳时挂单被扫)→ 引擎 3 避险 + 极价区做(深度厚)。

### 引擎 2 — 事件延迟收敛套利(窄 / 深 / 需验证 / 叠加)
- 仅 tennis/soccer(有 bet365 in-play)。round-1 共识的事件套利。事件触发管道(替 500ms 轮询)+ bet365 偏见修正(扣 0.7-1.2pp,操盘手:in-play 锚的是 bet365 单家 square book 非共识,有 home/over 系统偏)+ G2。

### 引擎 3 — 预测模型(双引擎共用大脑)
- 对引擎 1:预测 bet365 即将跳 → 提前拉/偏报价避逆选。
- 对引擎 2:预测收敛方向+幅度 → 择时触发。
- M5(torch+MPS)训,ONNX 服务器推理。先规则化基线(引擎 1 立即可上),序列模型后置(样本攒够)。校准必做(A1-1 教训:排序≠校准,C++ 仅支持 Platt a/b)。

### 优先级
- **P0 核实(证伪风险)**:① in-play 进球后 PM 是否暂停撮合(暂停→引擎 2 死;但我们有连续 in-play tick 数据 5s/次 → 市场游戏中是开放的,只剩"进球瞬间微暂停"需 G2 实测)② 流动性激励资格(min size/spread/我们 scale 能领多少)。
- **P1**:引擎 1 规则化做市(广,立即跑,赚激励,不需模型)。
- **P2**:引擎 2 事件触发管道 + G2 验证(深)。
- **P3**:引擎 3 预测模型(避逆选 + 择时)。

## 红队要攻的点(专家别客气)
1. 引擎 1 激励做市:逆向选择会不会吃光激励?净期望真为正吗?我们 paper→真钱 scale 能领多少激励(pro-rata 被大鲸鱼稀释)?
2. 双引擎是否冲突(激励要双边贴中点挂单 ↔ 套利要单边吃单)?同一资金/同一市场能不能并存?
3. 引擎 2 "supplement $5-50/天"天花板 vs 引擎 1 激励规模 —— 谁是主引擎?
4. P1 先上规则化做市(无模型)是否真可行,还是必须先有避逆选模型否则被扫爆?
5. in-play 暂停证伪风险有多大?
6. 机制坑:两 buy-limit 共用 balance reserve、tick 对齐、心跳 10s、ToS 反操纵(快速撤挂 = spoofing 嫌疑?)。
