# Phase 4 实盘开闸 + 前后端对接 + 前端 + 量化 评审会纪要

- **日期:** 2026-05-31
- **主持:** 老雷 (GM)
- **参与:** 老韩(风控) / 老周(架构) / 小苏(前端) / 小程(量化) / 老郑(可观测) / 小杜(产品统筹)
- **背景:** Phase 1-3 完成 —— Polymarket CLOB V2 实盘下单生产链路已打通并真网验证成交(生产 C++ 模块 `stcpp_crypto_eip712` + `stcpp_crypto_secp256k1` + `stcpp_polymarket_clob_wire` + `LiveOrderSubmitter`,orderID 0x6f11be4d…)。议 Phase 4 = 把 submitter 接进策略执行路径让策略自动下单。

---

## 0. 全员一致的核心结论(GM 须知)

1. **Phase 4 不能直接接策略。第一步必须先建 `LiveOrderGate`(RM 强制门)+ 封死裸 `LiveOrderSubmitter::Submit`。**
   - 老韩:当前 live 链路 RM **不是绕过,是根本没接** —— `live_order_submitter.cpp:120` 从 token 到 POST 全程无 RM 调用。红线 §8 第一条。
   - 老周:同意。不得把 submitter 塞进 PaperLoop(R-11/ToS 语义污染);走 `IOrderExecutor` 适配,LiveExecutor 只接 `is_approved()==true` 的 intent。
2. **真正的瓶颈不是下单管道,是量化信号。** 小程:`n_eff=30`(paper_loop.hpp:124)把 5-14¢ edge 的单子全拍死(CI half-width≈15¢ @ p=0.5)→ 这很可能是"系统什么都不交易"的根因。advisory gate 未解除(7 gates 无法验证)。**不解决就是"随机烧钱"**(小杜)。
3. **回测-实盘口径不一致 = 红线。** 小程:回测用 `gross_edge≥0.06`(backtest/types.hpp:199)无 CI 折扣,实盘走 `edge_ci_lower` 门 —— 违 CLAUDE.md §8"回测与实盘用不同逻辑→不许上线"。
4. **节奏:稳健 + 并行**(小杜)。激进直接开闸与 §3 铁律"纪律高于收益"冲突,不是合理选项。

---

## 1. 风控(老韩)

- **Phase 4 第一个 PR = 建 LiveOrderGate + 封死裸 submitter + `live_order_cli` 自动 binary 不许 link**(绕 RM 活口)。CI 加 include 白名单门禁。
- **独立 LiveRiskConfig**(不复用 paper 100k 默认)。灰度初值[需 GM 拍板]:per-order **$1** / per-condition+outcome **$2** / bankroll **$25** / 日亏硬 kill **$5 绝对值** / 连亏 **3** / **日 ≤20 单**。
- **新红线 OrderRateCap**(RM 目前无单数/频率门)——机器连发必须有频控。
- **arm/kill 三层 fail-closed**:编译期(Live binary 显式)+ 运行期双因子(`LIVE_TRADING_ARMED=1` env + arm 文件,默认 SAFE_MODE/HALTED 不默认 RUNNING)+ 进程外 kill(<1s)。
- **灰度阶梯** G0 干跑→G1 单笔手动→G2 自动极小→G3 放量→G4 扩。G1→G2(手动→自动)是真正风险跃迁,**单独签字+老黄会签**。
- **实盘新风险**(paper 没有):部分成交(FOK 须 G1 核对真原子性,以 CLOB 回执实际 fill 为唯一真相回写账本)/ 拒单**禁自动重试** / 幂等须**持久化**(signal_id→orderID 落盘,salt 每次随机 submitter 自身无幂等)/ **超时绝不自动重发**(超时≠没成交,进 PENDING_UNKNOWN 冻结+对账,对账闭环是硬前置不可砍)。
- **签字前置清单**:LiveOrderGate 建成 + RM 默认非 RUNNING + LiveRiskConfig 灰度值咬死(单测)+ OrderRateCap + feed-liveness 全绿(无 NEVER FED)+ 幂等持久化 + 对账闭环 + kill 演练 + audit(orderID↔audit_id↔tx)+ 老黄 ToS 会签。

## 2. 架构(老周)

- **接线落点在 `PaperDaemon`(装配层),不在 PaperLoop 类内部。** 抽 `IOrderExecutor{Execute(approved)}` 两实现(VirtualExecutor/LiveExecutor)。次序铁律不变:SelectSide→sizing→gate→RM evaluate→**[必须 APPROVED]**→Sign→Executor。
- **否决**:不得把 submitter 塞进 PaperLoop;不得让 debug_api 契约层 include submitter/curl(破坏热路径零反向依赖)。
- **D-1 裁定**:Submit 同步阻塞最坏 8s(curl timeout)→ **必须独立下单线程/SPSC 队列**,决策线程永不阻塞网络。引入 **in-flight 去重台账**(防 Submit 返回前对同 condition 二次下单)。
- **G-2**:`PositionLedger::apply_fill` 去 `VirtualFill` 化,抽中性 `FillEvent`(R-4 级 schema 变更走审计)。MVP **锁死 FOK**,只有 `status=="matched"` 才 apply_fill;GTC/部分成交留 M3。
- **前后端**:复用现有 `debug_api`(cpp-httplib REST,`:8080`,SSH 隧道)——本就为 operator 前端设计。positions/PnL/RM/quote 已就绪(走 StateProvider double-buffer 快照,零热路径耦合)。**D-2**:MVP 纯 REST 轮询;order/audit 用游标轮询;**WSS 推送推到 M3**。新增 live order 视图走 POD 投影 + 独立 OrderSnapshotHub。
- **拍板点**:G-1(给 PaperLoop 加 executor 注入[快,类名脏] vs 抽 DecisionCore[干净,工作量大])/ G-3(in-flight 去重粒度)。

## 3. 前端(小苏)

- **现状:已有 SolidJS+Vite 前端**(`frontend/`,v8.1,4 页:盯盘/Ops/PnL/市场详情,5s 轮询)。观测侧基础完整,可"看着跑"。
- **致命缺口:控制能力(arm/kill)完全没有。** debug_api 全是只读 GET,StateProvider 无写方法,无 arm/kill/drain 的 POST 端点。
- **P0 缺口**:① arm/kill 大开关(后端需 `POST /api/v1/control/state` + 前端二次确认大红按钮)② fill 流水(`GET /api/v1/fills`)③ 风控余量仪表(日亏已用/限额、连亏当前/阈值)④ arm 门禁(Gate pass 才启用)。
- **MVP(不上不能 arm)**:SystemState 大开关 + 风控余量仪表 + fill 流水 + arm 门禁。
- **拍板点**:arm/kill 后端写端点(派老韩+老周,硬阻塞)/ arm 权限模型(派小白+老韩)/ operator WSS 是否 Phase 4 必做(建议否,REST 够)。

## 4. 量化(小程)

- **A1 fair value 先验未校准**(alpha/beta/book_blend 是文献值,fill_rate=0.65/slippage=8bps 是 M1 占位符)→ 误差量级与 5¢ 阈值重叠,可造假阳性 edge。**派老彭**历史数据校准。
- **A2 advisory gate 未解除**(`advisory_markets_no_intent=true`)→ 7 gates 永远无法验证,Phase 4 准入无从谈起。**最直接阻塞。**
- **A3 n_eff=30 把策略卡死**:CI half-width≈15¢ @ p=0.5 → edge<0.15 全被 NO_EDGE 拒。**[GM 拍板 Q1]** n_eff 取值(建议先 n=200 跑起来观察,或老彭给 books_used 实测值)。
- **A4 回测-实盘 CI 口径不一致 = §8 红线**,回测须引同套 CI gate 或正式豁免(老郭审)。
- **fee 不是主障碍**:75bps 峰值费在 n_eff 合理时对 5¢ edge 影响约 15-18%,可存活。n_eff 才是瓶颈。
- **上线后监控**:realized vs predicted edge / RM reject 分布 / fill_rate 实测 / DD 实时 / 分 sport edge hit rate / 延迟 alpha decay。
- **拍板点 Q1**(n_eff 值)/ Q2(回测-实盘豁免?)/ Q3(advisory 解除是否分 sport 各 20 笔)。

## 5. 可观测(老郑)

- **现状基建完整**:debug_api 13 endpoint(positions/pnl/risk/gate/market/book/quote/score)+ Prometheus `/metrics`(uptime/wss/loop_latency/rm_decision/fill/net_edge/staleness…)+ Audit WAL(BLAKE3 链,5 通道)+ WSS transport。
- **Phase 4 硬阻塞**(3):① `cum_net_pnl` 未接 LedgerHub 真实值(恒 0)② **实盘下单失败 counter 未接**(`LiveOrderSubmitter` 错误路径无观测点,网络/鉴权/限流失败完全不可见)③ `wss_sports_api_connected`(Goalserve)未接真实连接状态。
- **传输**:实盘监控走 Prometheus pull(告警用 alertmanager,非 UI 轮询);UI 盯盘 REST poll;**WSS push 不值得在 Phase 4 做**。
- **拍板点**:4 个告警阈值(RM 拒单率/异常 PnL 跌幅/下单失败率/Goalserve staleness)/ Prometheus 是否 Phase 4 同期部署(建议同期,引擎侧已就绪零改动)。

## 6. 产品统筹(小杜)

- **硬依赖序**:方案 B 单位契约(MicroPUSD)必须先于一切金额接线 → 纸面红线接线(exposure/DD/consec_loss)→ time_frac 真接入 + Goalserve 恢复 → 量化调参 → dogfood P0 全修 + ctest 绿 → 老韩 6 硬门 → **Phase 4 开闸**。
- **MVP 必须**:风控(单位契约/红线接线/mode_tag fail-closed/live 过 RM/nonce 隔离/kill/拒单 audit)+ 量化(time_frac/Goalserve/n_eff 150-200/无假 fair)+ 最小 UI(gate 仪表/kill/PnL 拆分)。
- **延后**:Phase B 买 NO / Pinnacle pregame / 完整 8 panel / Prometheus 全链 / ML ONNX。
- **建议:稳健 + 并行提速**(主线串行风控+量化,前端 P1/Phase B 单测/信号评估并行)。
- **拍板点**:P1(6 硬门全过 vs 豁免限额)/ P2(M1 6-25 是否顺延)/ P3(Phase B 时机)/ P4(信号源 The Odds API vs Pinnacle)/ P5(最小 UI vs 完整看板)/ P6(dogfood P0 验收签字人)。

---

## GM 拍板清单(汇总,按优先级)

| # | 决策 | 谁提 | 建议 |
|---|---|---|---|
| **D-A** | Phase 4 第一步 = 建 LiveOrderGate + 封死裸 submitter(不是接策略) | 老韩/老周 | **共识,直接执行** |
| **D-B** | 节奏:稳健(补齐风控+量化阻塞再开闸) vs 激进(限额先开) | 小杜/小程 | 稳健+并行 |
| **D-C** | n_eff 取值(当前 30 把策略卡死) | 小程 | 先 n=200 跑起来观察,或等老彭实测 |
| **D-D** | advisory gate 解除 + 回测-实盘 CI 对齐(红线) | 小程 | 解除前置 + 回测引 CI 或老郭豁免 |
| **D-E** | 灰度初值($1/$2/$25/$5/连亏3/日20单) | 老韩 | 接受作灰度,小梁/老钱终定 |
| **D-F** | arm/kill 后端写端点 + 权限模型 | 小苏/老韩 | 派老韩+老周+小白 |
| **D-G** | G-1 executor 注入 vs 抽 DecisionCore;G-2 FillEvent 中性化;G-3 in-flight 去重 | 老周 | — |
| **D-H** | Prometheus Phase 4 同期部署 + 4 告警阈值 | 老郑 | 同期(零引擎改动) |
| **D-I** | M1 demo 6-25 是否顺延 | 小杜 | 收老周/老韩工期后定 |

---

## 执行进展(2026-05-31 会后,GM 落地)

已做完(主干 + 测试 + commit,全程 1108 ctest 绿):

| 项 | 内容 | commit |
|---|---|---|
| ✅ **D-A** | `LiveOrderGate` 强制风控门 —— 策略到 CLOB 唯一通路,RM 拒→下单 sink 零调用(6 单测守红线) | Phase4 D-A |
| ✅ **量化阻塞3(红线)** | 回测-实盘 CI 口径统一:抽 `strategy/edge_ci.hpp` 单一 `ComputeEdgeCiLower`,paper+backtest 共用;回测加 edge_ci_lower 门 | b10f28c |
| ✅ **量化阻塞1** | `n_effective` 30→200(实盘+回测对齐),解开 6-14¢ edge 被误杀 | b10f28c |

待办(需 GM 拍板 / 跨域 / 外部依赖,未做):

| 项 | 阻塞原因 | owner |
|---|---|---|
| 量化阻塞2 advisory 解除 | 需 Goalserve 数据跑 50 笔(数据源白名单协商中) | GM + 小梁 |
| LiveRiskConfig 独立灰度值 + OrderRateCap 新红线 | 灰度阈值待小梁/老钱定;OrderRateCap 需老韩 spec | 老韩 |
| Phase 4 接线(executor 抽象 / FillEvent 中性化 / in-flight 去重 / 独立下单线程) | 需 GM 拍 G-1(executor 注入 vs DecisionCore);G-2 是 R-4 schema 变更走审计 | 老周 + IC |
| 前端 arm/kill 控制端点 + fill 流水 + 风控余量仪表 | 需老韩+小白定 arm 权限模型;后端需加 POST 写端点 | 小苏 + 老韩 |
| 可观测 P0(cum_net_pnl 真值 / 下单失败 counter / Goalserve WSS 状态) | 部分依赖 live 接线完成 | 老郑 |

> **owner:** 老雷 / **last_review:** 2026-05-31
