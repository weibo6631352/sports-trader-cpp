# 短时套利引擎 — 全局主计划 v1

> owner: 老雷 (GM) | last_review: 2026-06-01
> 配套: 设计评审 docs/MEETINGS/2026-06-01-short-horizon-arb-design-review.md (9-agent + §0/§0.1/§0.2 修正)
> 本文 = 可执行主计划 (执行层); 评审文档 = 论证层 (file:line 佐证)

---

## §0 一句话 + 设计哲学

**做什么:** 不瞄准最终结算胜率, 瞄准【未来短时窗口内 PM 中间价的移动】, 用于事件驱动的方向性套利抢单。
核心动机是**标签效率**(结算每场 1 标签等几小时 → 短窗每 tick 自监督即时稠密, 数据量差几个数量级)。

**核心 alpha:** 进球/比分变动 → Goalserve 直播源**先于** PM 散户市场知道 → sharp fair 瞬间跳变、PM mid 滞后收敛 → 抢在散户反应前吃单。= **信息优势 + 微结构动量** 双引擎。

### 设计哲学 (老板 2026-06-01): 喂信号给模型自主决策, 少硬编码守卫 — 但分两类
| 类型 | 处理 | 例 |
|---|---|---|
| **策略阈值** (该不该发单、幅度够不够、信号强弱) | ✅ **软化成特征喂模型自主决策**, 不写 if-else 硬门 | "幅度<3¢不发"→ 把 fee/spread/预测幅度/置信度全喂模型, 它自学何时值得 |
| **安全红线** (灾难防护, 非策略选择) | ⚠️ **保留**, 不交给模型 | 下单必经 RM (CLAUDE.md 红线)、延迟熔断、亏损强平 fail-safe、私钥、paper 不污真账本 |

> 区分准则: 一个 bug 会不会掏空账户? 会 → 安全红线必保留 (兜底模型犯错); 不会, 只是错过/抓住机会 → 软化成特征。老板原话"让模型有自主决策权, 把信号喂给他"指前者, 不是拆掉防灾兜底。

---

## §1 现状盘点 — 套利原料已基本就绪 (110 列特征)

这套引擎的输入**大部分已在** v0.1-v0.12 特征契约里 (`model_feature_spec.hpp`), 是前几轮陆续补的:

| 类别 | 列 | 用途 |
|---|---|---|
| 多尺度动量 | b_mp_roc_5s(106)/per_sec(24)/30s(32)/5m(33) + accel(109) | 短时方向 + 加速度 |
| OFI / 微价压力 | b_ofi(30)/b_ofi_10s(107)/x_microprice_minus_mid(17) | 主动性 + 即时压力 |
| 短窗波动 | b_realized_vol_10s(108)/b_vol_ratio(31) | 预测幅度 σ + 制度切换 |
| L2-L5 深度 | 86-93 (双边各档深度/集中度/失衡) | 流动性厚度 + 悬崖 |
| trade-flow | 96-101 (双边签名净流/买占比/强度) | 成交方向 |
| **双边一致性/锁定** | x_yes_no_bid_sum(104)/x_arb_free_edge(105)/b_cross_spread(45) | YES+NO 套利/锁损空间 |
| **信息优势** | x_inplay_fair_minus_mid(102)/x_inplay_market_absdev(103) | sharp 直播源 vs PM 散户 gap |
| 比赛事件 | g_goal_freshness(73)/g_score_diff(0)/g_time_x_lead(65) | 进球触发 + 时间感知 |
| 数据延迟 | 75-81 (双边 book 龄/传输延迟/联合最旧) | 信号新鲜度 |
| 市场活跃度 | mkt_volume_24h(94)/liquidity(95) | 可执行性/滑点代理 |

**结论: 特征侧不用从零开始。** 要换的是【标签 + 模型 + 触发 + 风控范式】, 不是特征。

---

## §2 核心机制

### 2.1 触发 = 事件驱动 (非 500ms 轮询)
- **进球/比分变动** (ScoreSnapshotStore 更新, score_diff 跳变) — 最强 alpha
- **PM book 更新** (OrderBookSnapshotHub::Publish notify)
- 每个事件 = 一个样本 + 一次预测。tick 仅 liveness 兜底。R-12: notify 端只 push+notify。

### 2.2 双时钟标签 (老板: 触发不规律, 不按固定秒)
- **墙钟** {2,3,5,10,12,15,30,60}s: as-of-forward (t+Δ 最新 book, 不插值) → 执行 deadline / 资金占用
- **事件钟** {1,2,5,10,20,50} 事件: mid(第N事件)−mid(t) → 主预测信号 (贴合信息按事件流动)
- G2 回测对比哪个预测力强。最终: 事件钟做信号 + 墙钟做执行约束。

### 2.3 套利逻辑闭环
进球(事件触发) → sharp fair 跳变、PM mid 滞后 → 模型预测 mid 朝 sharp 收敛多少 → CI 下界过滤够本 → 吃单 → horizon 内挂限价平仓 (墙钟 deadline 强平兜底)。

---

## §3 模型与标签

- **模型: LightGBM 多输出起步 (非 NN)** — ring 序列样本稀疏(1-10个), NN 优势归零+过拟合+推理延迟翻倍。8-horizon 多 head, 共享 110 列输入, 离线 Python 训练 → Treelite/ONNX → C++ 推理(已写好待装机)。NN(TCN)→ v2.5 挑战者, 须真实采样频率下 walk-forward 净胜 GBM 才上。
- **预测目标: 回归 Δmid 打底 + 方向 + CI 下界过滤** — 点估计禁直接进 sizing(微利高频必死), 用 `ComputeEdgeCiLower` 范式。
- **标签管道: book-event 落帧 (现 5s 轮询造不出密集标签) + 离线 as-of-forward join + X-leak 断言** (PIT 铁律: 未来 mid 绝不回灌特征)。
- **样本不平衡: 静态期 dir=0 占 70-85%**, label 构造过滤 |Δ|<min_move 的 0 类 / 类权重。
- **跨状态污染: g_time_status(4)/resolution_status(59) 过滤跨 Ended 的标签对。**

---

## §4 风控范式 (策略软化 + 安全红线保留)

按 §0 哲学双轨:

**软化成特征 (喂模型):** 信号强弱、幅度够不够、置信度、流动性深度、双边锁定空间 — 全喂模型自学, 不写硬阈值门。

**保留的安全红线 (灾难兜底, 非策略):**
- 下单必经 RM (CLAUDE.md 红线, 不可绕)
- **RJ-ARB-5 STALE_PREDICTION**: 预测年龄 + 预估 RTT > 窗口×0.5 → 拒 (延迟吃掉窗口的硬熔断; 这是延迟可行性的 RM enforce 点)
- **open-leg 强平 fail-safe**: 进场即背"horizon 内必平"义务, 到 deadline 未平 → 市价强平; 预测源 stale/断流 → 所有 in-flight leg 立即平 (裸方向暴露违背套利初衷)
- **DD 三轨熔断**: 滚动胜率 / 连续反向 / 滑点累计 (日 PnL 对高频太粗)
- 套利专用 Kelly λ=0.10 起 (估计误差占微利比例高), 老韩 RM 联签
- EXIT_DEPTH 检查: 平仓需 L1-L5 累计深度 (现 RM 只看 L1) — "进得去出不来"是套利第一杀手

> 这些不是"我们替模型设阈值", 是"防止模型一个 bug 掏空账户"。删了它们 = 把灾难防护交给一个还没验证的模型。

---

## §5 分阶段路线 — 先证伪, 再建设

### Phase 0 — 双门禁体检 (不写热路径; 第一个可证伪里程碑) ★命门
- **G1 延迟体检**: 实测就近部署端到端延迟 (含 PM 成交确认) p50/p99 + book 更新频率分布 → 定窗口下界。
- **G2 可预测性体检 (聚焦进球)**: 用历史采集逐 horizon 验"进球/比分变动后 PM mid 在 {2..60}s/{1..50}事件 的移动分布, 扣 fee(往返~1.5¢)+spread 后是否正期望 + 收敛多快"。
- **G2 为负 → 全案停。** 没有维度敢拍胸脯说一定正期望 — 这是真 conditional。
- **当前阻塞: 需数据。** 开发环境无白名单/未在 AWS 跑 → 现有 fv.jsonl 可能没有真实进球+之后 PM mid 序列。**G0 前置 = 让采集在有白名单的 AWS 环境跑一段 (含进球场次)。**

### Phase 1 — 落帧 + 标签管道
book-event 落帧 (替 5s 轮询) + MidTapeRecorder + 双时钟 MidLabelJoiner + X-leak 断言单测。

### Phase 2 — 基线模型 + 推理打通
装 onnxruntime + 开 STCPP_ONNX_ENABLED; LightGBM 8-horizon → Treelite 导出; ISeqArbModel 接口并列注入; walk-forward 净胜 baseline + 推理 p99 <1ms 验证。

### Phase 3 — 事件驱动触发 + 套利风控
WSS+score-event notify 唤醒 (R-12 review 必经老郭+老韩) + SizingArbCalculator + RJ-ARB + open-leg fail-safe + DD 三轨。

### Phase 4 — Paper 实盘 (advisory)
30s/事件钟窗口 paper 跑, 净利/fee > 1.5 上线闸; LiveOrderGate 仍 disarmed (真钱开闸需老韩+小白会签)。

### v2.5 候选
TCN 挑战者 / triple-barrier / online learning / 多盘口 / 更短窗 / Kyle lambda。

---

## §6 红线合规

- **§8.1 carve-out**: RM 加性扩展(新 check_/拒单码/末尾加字段) + ML append-only 特征 + 新模块(MidTapeRecorder 等) = 普通 PR review, **不需架构会签**。
- **需会签**: 仅真钱开闸 `LiveOrderGate.Arm()` → 老韩 RM + 小白安全。MVP 止于 paper advisory, 不触会签门。
- **必经老郭+老韩 review**: §2.1 WSS/score-event notify (R-12 红线区)。
- BR-1 (回测=实盘同特征) / R-20 (4ts data_source 锚) / ML-R8 (PIT) 全程守。

---

## §7 下一步 (唯一卡点)

整个方向成不成 = **G2 能不能证实"进球后 PM mid 朝可预测方向够本收敛"**。
不需写任何生产代码, 但**需要含真实进球的采集数据**。
→ **行动: 确认 AWS+白名单环境能跑采集 → 跑一段含进球场次的 fv.jsonl/mid tape → 我离线跑 G1+G2 → go/no-go 报告进 docs/MEETINGS/。**
