# 架构债审查会 — 统一债务登记表 + GM 分级

- **日期:** 2026-05-30
- **主持:** 老雷 (GM)
- **议题:** PaperDaemon 重构 + A0→A2 一大轮后, 系统盘点架构问题 + 遗留债 (问题清单, 非前向计划)
- **审查:** 老周 (架构主权, 全盘) + 老郭 (独立第二意见, 挑老周盲区)
- **owner:** 老雷  **last_review:** 2026-05-30

---

## 0. 整体判断 (两份审查收敛)

**骨架健康** —— 契约库瘦身 (debug_api 回 Threads-only)、app 编排层依赖汇聚、映射线程独立、R-7/R-11/R-12 物理隔离是真功夫。**但本轮是典型「为第一笔成交, 用补丁 + 保守默认硬推过线」**, 留下一笔「看着已治理、实际在裸奔」的隐性债。**两处最深裂缝**: ① 风控逻辑齐全却无生产喂数管道 (安全网是空的); ② 单位 bug 没根治, 只在 RM 一侧打补丁 + sizing 一侧还当 pUSD, 靠一个 magic clamp 摁住。

---

## 1. P0 — 实盘前必清 (阻塞 MVP「零风控失效」)

### P0-1 三条风控红线纸面化 (RM 无生产喂数) — 最危险
- DD 熔断 / consec_loss / exposure 限额**逻辑都写了**, 但生产路径**从不喂数** → 阈值永不触发 → 虚假安全。`paper_loop.cpp:140` 只 `set_bankroll`, 全仓零处喂 realized PnL / consec / exposure 进 RM。**实质等效「绕过 RM」(踩红线 §8)。**
- 处置: 实盘前必接三条喂数管道 (PnL/consec 从 position_ledger → RM, exposure 从成交回执 → RM)。**owner:** 老韩定喂数契约 + 老周定数据流向, GM 实现。

#### P0-1 spec + step1 已落 (2026-05-30, 老韩契约 `laohan-p0-1-rm-feed-contract-v1.md` + 老周架构 `laozhou-p0-1-rm-feed-architecture-v1.md`)
- **✅ step1 exposure 红线接通 (已 commit):** PaperLoop TickOne 尾部加 `FeedRiskGateway()` (Step 8c, apply_fill 后) → `PositionLedger.get_per_condition/outcome_exposure()` **×1e6 (whole pUSD→micro)** 全量覆盖喂 RM。loop_thread_ 串行 (R-12 满足, 不在 WSS io_thread)。RM 侧零改动。
- **🔴 单位 P0 门禁已焊 (老周强制):** PositionLedger 存 whole pUSD, RM 比 micro, 漏 ×1e6 = exposure 红线静默架空。`test_paper_loop P0_1_ExposureRedLine_UnitGate` end-to-end 验: fill 45pUSD 喂入后 +10pUSD 越 50pUSD condition cap → EXCEED_CONDITION_EXPOSURE (漏乘则红)。
- **⚠ 单位冲突已亲验解决:** 老韩 spec 误判"零换算直搬", 老周对 (whole vs micro 差 1e6)。GM 追 `vord.size_usdc=micro/1e6`(paper_loop:560)→ matcher `fill_size×rate`(:95)→ apply_fill `(int64)whole`(:54) 钉死 = whole pUSD。
- **⏳ step2 daily_pnl (DD) deferred — GM 实施时挖出新债:** `PublishLedgerSnapshot:655` 有**预存 PnL 单位 bug** (`net_qty = size_usdc/1e6` 把 whole 当 micro → unrealized PnL 错 1e6)。daily_pnl 的 unrealized 源在此, 故 DD 喂数须先修 ledger PnL 单位。M1 只买不平 → realized=0, 此 bug 不咬决策但堵 DD 喂数。**新 backlog: PublishLedgerSnapshot PnL 单位修正 (派小肖/老彭, 修后接 daily_pnl)。**
- **⏳ consec deferred:** M1 只买不平 → 无平仓结果序列 → 恒 0 正确 (非 bug), M2 接平仓回路。

### P0-2 单位 bug「未关闭」(非「已 hotfix」) — 老郭挖出真相
- **同一个 `RiskConfig.per_order_cap_usdc` 被两条路当两种单位**: paper_daemon→RM 当 **micro** (10'000'000); paper_loop sizing 用默认 `RiskConfig{}` (10'000) → SizingCalculator 当 **pUSD** (`sizing_calculator.cpp:175`)。**差 1000 倍, 只靠 `paper_loop.cpp:480` 一行 `min(notional, 10.0)` magic clamp 摁住没爆。**
- `RiskConfig` 字段**名叫 `_usdc` 值是 micro** (`risk_gateway.hpp:298`) — 名实不符。`MicroPUSD` c1 是**零接入孤儿** (生产零调用, 制造「已治理」错觉)。
- 处置: 升级认定为 **P0 未关闭**。c2-c5 必做 + `RiskConfig` 字段「改名 `_micro` 或换 `MicroPUSD`」二选一, 不许名实不符躺 main。**owner:** 老周 (spec) + 老韩 (cap 真值) + IC。

#### P0-2 第一刀已落 (2026-05-30, GM 写码 + 老韩 RM 主权评审放行) — clamp 遮羞布已拆
- **本次范围 (已 commit):** 把 caps 纳入 `PaperLoopConfig` (pUSD 单一真值源 10/50/25), sizing 用同源 pUSD caps 构造 `sizing_cfg` (替代脱节的默认 `RiskConfig{}`), paper_daemon 给 RM 的 micro caps 改由该 pUSD 源 × 1e6 派生。**删掉 `paper_loop.cpp` 的 `min(notional, 10.0)` clamp** —— sizing 自然受 per_order_cap 约束, ×1e6 后必 ≤ RM micro cap。**未动 RM evaluate 逻辑 / 未改 RiskConfig 字段名 (ABI-locked, 留 c2-c5)。** 全量 1057/1057 绿, `A2_DaemonProducesPaperFill` 拆 clamp 后仍产 fill。
- **老韩评审结论 (放行, agentId a6fdcd24):** RM evaluate 4 个 cap 比较点 (risk_gateway.cpp:432/452/465/473, 全 `>`) 原文未变 → **RM 主权实质未削**。cap1(per_order) 单位闭环数值验证通过, 边界 `notional==cap` 时 `intent_micro==rm_cap`, `>` 不触发, 无 ±1 micro 偶发拒。
- **⚠ c2-c5 必须带走的 3 个前提/隐患 (老韩挖出, 均安全方向, 非阻塞):**
  1. **per_outcome/condition 闭环靠的是「exposure 恒 0」不是单位推理:** paper 全程不调 RM exposure setter (RM 侧 `cur` 恒 0), sizing 的 `current_*_exposure_usdc` 也硬编码 0 (paper_loop:407-408)。累加比较退化成单笔。**c2-c5 若让 paper 回写 exposure, sizing 必须从 RM 真实 exposure 取值, 否则第 2 笔后 per_outcome/condition 双轨偶发拒且 sizing 无感。**
  2. **int64 截断改 cap 经济含义:** `sizing_cfg.per_order_cap_usdc = (int64)cfg_.per_order_cap_usdc` 把 pUSD 小数 cap 截整 (10.0/50.0/25.0 无损; 7.7→7 偏小)。方向是 sizing 更严 → 不触发 RM 拒 (安全), 但 per_order 实际生效值被悄悄收紧。**名实统一后 pUSD→micro 转换只能一处 (paper_daemon ×1e6), sizing 不该再吃截断的 int64 pUSD。**
  3. **最小 1 pUSD 兜底可被 RM 拒 (sub-1-pUSD cap 下):** paper_loop `size<=0 → 1'000'000` 兜底; 若 cap < 1 pUSD (如 per_order=0.1 → RM cap=100000 micro), 兜底写 1 pUSD = 1000000 micro `> 100000` → RM 拒。当前默认 10/50/25 远大于 1 不触发。**c2-c5 兜底应 clamp 到 `min(1pUSD, per_order_cap)`。**
- **c2-c5 RM 侧清单 (老韩):** 字段名实统一 (micro 显名, 需老韩+老周 spec, ABI-locked) → 消 int64 截断 (sizing 吃 double pUSD 或吃 micro) → exposure 累加路径对齐 → 兜底与 cap 关系。**走 R-4 (schema 静默变更红线) 流程, audit 全部 RiskConfig 消费/构造点, 引用红线粘原文 (§8.1 第 3 条)。**

### P0-3 enable_paper_fills 默认开火, 无 kill switch — 老郭挖出安全缺口
- `paper_daemon.hpp:118` `enable_paper_fills{true}`, `paper_runtime` **无命令行开关能关** → headless 生产 daemon 一启动就默认解封成交, 运维无法降级「仅观测」。违反价值观 #1/#2 (实盘优先 + 纪律>收益 保守默认)。**无评审记录** (D3/D4 签的是「隔离正确」非「默认开火」)。
- 处置: **默认改 `false` + 加 `--enable-fills` 显式开关**。小改、安全默认、即时可做。**owner:** GM。

### P0-4 live 下单链路整条 stub (14 接口) — 已知计划性
- `live_pm_client.cpp` 14 接口全 stub (M5+ deferred)。MVP 实盘前接, 排在 P0-1 之后 (RM 喂数先于真下单)。**owner:** 老李 + 老韩 gate。

### P0-5 动态市场发现缺失 + popen("curl") shell
- gamma boot-once (盘中新盘不发现, 卡全盘口北极星) + `market_discovery.cpp:489` popen curl (无超时/无错误码, 跨洋脆, ToS 速率不可控)。
- 处置: boot-once → 周期 rediscover; 实盘前 popen → C++ client。**owner:** 老李 + 老周 (线程模型, R-12)。

### P0-6 sizing↔RM fee 门用不同 p (fair_value vs price) — c3 揭开 (小袁定性: 真 bug)
- **根因 (小袁核实):** sizing `compute_net_ci_edge` 用 `fair_value` 算 fee (`sizing_calculator.cpp` Step 3); RM `EDGE_NEGATED_BY_SLIPPAGE` 门用入场价 `price` 算 fee (`risk_gateway.cpp` check_signal_ L587)。p≠c 时极小 edge 下方向不一致 → sizing 显正而 RM 拒 (37/9863, edge 20-50bps)。**独立于单位, 非 c3。**
- **暴露过程 (c3 副产物):** c3 修 `test_sizing_rm_consistency` 的 `make_minimal_intent` 时间戳 (原固定 2025 常量已 stale >1yr → 每 intent BOOK_TS_STALE → **Test1/4/6 evaluate 路径长期空过假绿**) 为真实 wall-clock, 激活后才暴露此 fee gap。**测试空过本身也是债 (测试质量洞)。** c3 已收口: Test1 edge 下界 0.001→0.02 避 fee-gap 噪声带 + 本项另立。
- **语义争议点 (待裁):** fee 的 p 用 fair_value 还是 price? 小袁微观结构意见: fee 是 taker 付平台 on 市场价 c → 应用 `price`, sizing 的 fair_value 错。**裁定权老韩 (RM fee 公式 SSOT), 抄送小梁 (影响 Kelly f* 分子)。** 裁定后 GM 落地改 `compute_net_ci_edge` 传参 + 加极小 edge 一致性专项 (恢复 Test1 边界覆盖)。**owner:** 老韩 (裁) + 小袁/小梁 (量化) + GM (落地)。
- **✅ 已解决 (2026-05-30):** 老韩正式裁定 **canonical p = price**(RM 代码 risk_gateway.cpp:587 + RM spec §2.3.2 本就 price, sizing 跑偏)。GM 落地: sizing_calculator.cpp:126 fee 参数 `p(fair_value)→c(price)`(老韩查实 fair_value 在 sizing 无其他消费点, 不动 edge/Kelly); Test1 edge 下界恢复 0.001(fee 同源后 37/9863 归零); 加回归哨兵 `SmallEdge_FeeP_Canonical_PriceConsistency`(fair_value≠price 验 net_ci_edge 用 price); C6/DirectionSymmetry 注释口径更新。全量 1059/1059 绿。老韩复核 C6/对称断言不变。

## 2. P1 — M1 后清

| # | 债 | 证据 | owner |
|---|---|---|---|
| P1-1 | **data→debug_api 反向依赖** (EventScore POD 错位在 debug_api, 底层 include 展示层; CMake「零反向依赖」是自欺) | score_snapshot_store.hpp:45 / state_provider.hpp:284 | 老周定下沉层 + GM |
| P1-2 | **god object 榜首 live_wss_transport.hpp 816 行** (超 real_state_provider; 多文件逼近拆分线) | live_wss_transport.hpp | 老周拆 |
| P1-3 | app 层换壳膨胀 (stcpp_paper_app link 12 target, paper_daemon Build 524 行 = W9 1000行 main 小一号复刻; M2 多盘口会再冲千行) | app/CMakeLists.txt:50 / paper_daemon.cpp | 老周立行数/职责红线 (Build 拆 3 段) |
| P1-4 | signer 双版本 v52+v62 并行维护 | signer CMake | 老孙确认收敛 |
| P1-5 | 跨 mode (paper/backtest) fair/de-vig 同源未验 (铁律#3) | goalserve_devig.cpp:50 vs state_provider.hpp:405 | 小梁 (SSOT) + 小蒋 |
| P1-6 | 测试参数脱节: 测试 n_eff=30, 生产 edge_ci_lower_floor=-1.0; **无「生产真实参数成交率」测试**; fill_rate/slippage 硬编码常数 → paper fill 分布不代表实盘但正被当 ML 训练数据存 | test_paper_loop.cpp:147 / paper_loop.cpp:404 | 小宋 + 小梁 |
| P1-7 | 更多名实不符垫片: market/condition 双写 / halt_usdc vs hard_pct 二选一靠注释 / market_id=condition_id alias | risk_gateway.cpp:161 / .hpp:305 | 老韩随 c2-c5 收 |
| P1-8 | de-vig/mid/microprice 派生量多处散落字面公式 | paper_loop.cpp:268,345 / state_provider.hpp:405 | 小梁定 SSOT inline |

## 3. P2 — nitpick / 卫生

- P2-1 orientation 模糊二值化 (`event_matcher.cpp:112` direct/cross 打平时任意拍一边 → 反向下单 seam; **实盘前升 P1**: 置信低于 margin 应 fail-closed) — GM
- P2-2 5s 轮询刷新线程 (RefreshEventMapping O(N×M) 全量重算, 应 score_store 新 event callback 增量) — GM, M2 前
- P2-3 11 个 build 目录散落根 (build_/build- 命名混用) — GM/小米清
- P2-4 R-NN 编号非 SSOT (散注释; 已立 §8.1 治理纪律) — 小米
- P2-5 3 flaky test (Concurrent100 端口 + 2 SignerV5x 延迟 -j8 争用) — 小宋 (端口动态/perf 串行 RESOURCE_LOCK)
- P2-6 pm_client.hpp 449 行 42 类型聚合 — 老李拆
- P2-7 改名 commit (cffee8b) 部署侧下游 (systemd/provision) 未扫确认 — 老吴 confirm
- P2-8 6 个陈旧 .claude/worktrees 残留 (含旧 bin/paper.cpp, 上次事故尸体) — 小米清

---

## 4. GM 拍板

### 最危险 3 (实盘前不可妥协)
1. **P0-1 风控纸面化** — 实盘安全网是空的, live client 接通前必须焊死喂数管道。
2. **P0-2 单位 bug 未关闭** — 不是「已修」是「clamp 摁住, 强类型在岸上」; c2-c5 + RiskConfig 名实统一必做。
3. **P0-3 fills 默认开火无 kill switch** — 保守默认违反, 最易修, 即时改。

### 即时动作 (GM 本轮收尾可做的小而安全项)
- **P0-3 修**: `enable_paper_fills` 默认 `false` + `paper_runtime --enable-fills` 显式开关。**(本轮做)**
- **P2-1 orientation fail-closed**: 匹配上但 orientation 模糊 (direct/cross 打平) 时不入映射。**(本轮做, 防实盘反向下单 seam)**

### 流程红线 (老郭挖出, 比单位 bug 本身更值得固化)
> **任何金额字段的「单位语义变更」(哪怕不改名) = 契约变更, 必扫全 include 链下游。** 改名会触发编译错强制审计, 「改值不改名」是静默的 — 正是这次踩雷根因。入 CLAUDE.md §8.1 (老高 CI grep 守护)。

### 排期
- **M1 (实盘前)**: P0-1 喂数管道 / P0-2 c2-c5 + 名实统一 / P0-3 (本轮) / P0-5 动态发现。
- **M1 后**: P0-4 live 接 RM / 全部 P1 / P2 卫生。
