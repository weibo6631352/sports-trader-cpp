# 2026-05-30 复盘评审会 — 综合纪要 + GM 拍板

owner: 老雷 (GM) | last_review: 2026-05-30
参会: 老周(架构)/老姜(性能)/老郭(独立第二意见)/老胡(PM) — 各出分报告(见同目录 4 份 retro-*.md)

> 范围: 复盘本会话(P0-2 单位契约全闭环 + P0-6 fee + P0-1 step1 exposure 红线 + 伦敦 EC2 实连 Polymarket)的架构/数据流/性能/流程。

---

## 0. 四方收敛的核心结论(一句话）

**方向对(类型护栏 + 红线接通 + 部署打通),但两个"边界画错"必须纠正:① 单位债的"已治理"边界画在 RiskConfig,真源在 PositionLedger(还会撞第 4 次,且第 4 次已躺 main = PnL bug);② 风控"已活"是 1/3(exposure)不是全部,MVP"零风控失效"当前是假绿。**

---

## 1. 🔴 单位债根因 — 四方一致:根在 PositionLedger,不在 RiskConfig

- **同一种病**: `PositionLedger.size_usdc` 名带 `_usdc` 却存 **whole pUSD 整数**(`position_ledger.cpp:54` `(int64)fill_size_usdc`),与 RiskConfig"名 usdc 值 micro"完全同型。c2 治了 RiskConfig,PositionLedger 原封不动。
- **第 4 次已躺 main**: `PublishLedgerSnapshot:659` `÷1e6` 把 whole 当 micro → **PnL 低估 1e6 倍**。同一 `size_usdc` 字段在同一文件被 FeedRiskGateway(×1e6 对)与 Publish(÷1e6 错)当两种单位。
- **比单位更脏的坑(老郭)**: whole 整数存仓位 → **fill < 1 pUSD 被 `(int64)` 截成 0 = 静默丢仓位**,比单位失配更难发现。
- **c5 grep 射程只 3 文件,守不到 PositionLedger** → 不根治只能打地鼠。

**GM 拍板:** 走 **R-4 把 PositionLedger + VirtualFill 金额统一 micro/MicroPUSD**,**否决在 PublishLedgerSnapshot 单点补 ×1e6**(老郭事前否决)。改名触发编译错强制 audit 全部消费点;×1e6 可删、÷1e6 bug 自然消、<1pUSD 截断消;CI grep 扩域纳 position_ledger.cpp。**owner: 老韩 spec + 老周下游审计 + 老郭 ADR(R-4)+ GM 落地。**

## 2. 🔴 DD 喂数 — 老郭事前否决:必须先做 PositionLedger 根治

**"修 PnL bug 再喂 DD" 重新校准为 "先 PositionLedger micro 根治,再喂 DD"。** 老郭事前否决:谁在 ledger 单位根治前接 DD = 把错 1e6 的 PnL 喂进熔断阈值,比不接更危险。

## 3. 防"假已治理 / 假已活"错觉(老郭 + 老周)

- **风控红线状态必须逐条标,禁"P0-1 已接通"整体措辞**:
  - exposure = ✅ 已活(真敞口咬 cap,有门禁测试)
  - DD = ❌ 纸面 + 源头 PnL bug 堵着(逻辑对,喂数断)
  - consec = ❌ 纸面但 M1 业务无源(恒 0 正确,M2 接平仓)
- **架构建议(采纳): RM 加 feed-liveness 自检** —— 每红线记 last-fed ts,启动 emit "DD: never fed"。一条红线从没被喂过 = 生产里不存在,RM 必须自己喊出来。**owner: 老韩 + GM。**
- **MVP "零风控失效" 当前假绿** —— 没有活的风控会失效。验收拆两问:已接通的 exposure 越界真拒吗 + 未接通的 DD/consec 被显式标 not-fed 吗。

## 4. 🔴 性能 P0(老姜):bench 已编译断裂

`bench_risk_gateway.cpp` + `bench_e2e_latency.cpp` 用 OrderIntent v0.4 废字段 + int 字面量赋 MicroPUSD(explicit 禁)→ **无法编译**。CI perf 回归门自 v0.5 起已哑,MicroPUSD 全面接入后恶化。`BM_RiskGateway_Approved` 还会因 timestamp_ms==0 走 TS_V2_MISSING 拒,测不到热路径。**MicroPUSD 零成本结论编译器层成立,但无 bench 数据佐证。owner: 老姜 + GM 修 bench。**

## 5. 性能 — 其余结论

- **FeedRiskGateway**: MVP(N<20 仓位)可忽略(~10-20us / 500ms tick);N=500 有退化(N× `s_->mu` 锁),需 O(N) bench baseline。全量覆盖正确。
- **生产实例**: **推荐 c6i.xlarge**(Ice Lake + AVX-512 → libsodium ed25519 ~8-12us,对齐 8us 目标);t2.xlarge(老 Broadwell 无 AVX-512)30us 不达标,生产应替换。paper 阶段 t2 够用。
- **延迟**: signer 30us 占 Polymarket 2ms RTT < 1.5%,paper/实战可接受;live 高频做市需关注 p99.9 尾刺。

## 6. 🔴 R-12 架构裁决(老周,入 ADR)

**FeedRiskGateway 回喂边永久绑定 loop_thread,绝不许重构搬到 WSS io_thread**(否则 `s_->mu` 成 WSS event loop 上的锁 = R-12 P0)。R-11/R-12 本会话守住。

## 7. c4 未真闭合(老郭第 2 否决)

`sz_in.current_*_exposure_usdc = 0.0` 仍硬编码(paper_loop:407-408)→ sizing headroom 永远满额,RM 用真值 / sizing 用 0 = 双轨。**M2 接平仓/多笔并发前,sizing exposure 必须从 RM 取真值,不许再硬编码 0(老郭闭合门)。** owner: GM + 老韩。

## 8. 流程复盘(老胡)

- **spec→签→GM 实现→测试→复签→push 双闸有效**:本会话抓 4 个真问题,2 个编码前/中被拦(老周抓单位 1e6 冲突 / 实施挖出 PnL bug)。契约/风控级 ROI 极高(一错静默烧钱,编译器测试都防不住)。
- **过度流程**: 单向安全改动(P0-3 默认 false)走重流程是浪费 → 降级 GM 直接做 + 周报追认。
- **瓶颈**: GM 单点写码 throughput(6 P0 串行一支笔)+ 老韩复签往返(≥4/天)。**建议 P2 卫生项回流 IC,释放 GM 带宽专注契约/风控/架构敏感路径。**
- **MVP 推进**: P0 净 3 全清(P0-2/3/6)+ 1 部分(P0-1 exposure)+ 2 计划性未动(P0-4/5)。M1 demo 真实剩余阻塞 = **Goalserve 白名单(外部运营)** + 量化参数。

---

## GM 行动项(优先级）

| # | 行动 | owner | 优先级 |
|---|---|---|---|
| A1 | **PositionLedger + VirtualFill micro 根治(R-4)** — 单位债真源,解锁 DD + 消 PnL bug + 消 <1pUSD 截断 | 老韩 spec / 老周审计 / 老郭 ADR / GM | ✅ 2026-05-30 (commit 19c84b0) — 1061/1061 serial 绿, 7 钳制 + <1pUSD 守卫 + c5 F4 grep |
| A2 | **修 bench**(bench_risk_gateway / bench_e2e_latency 编译断裂)+ 补 BM_Feed/BM_Approved | 老姜 + GM | ✅ 2026-05-30 — 两 bench v0.6 字段对齐编译通 (build-bench 全绿); BM_Approved 修 timestamp_ms 走真热路径(294ns); BM_RmFeed_NPositions O(N) 基线 (N=20→485ns / N=500→14.4us, MVP 可忽略); E2E 真产 fill (fill_count=1). **副产: P1-9 RM slippage gate 单位失配 (risk_gateway.cpp:526 漏/1e6) → 老韩** |
| A3 | **Goalserve 白名单**(挂 Elastic IP + 报白名单) | 用户(运营) | 🔴 M1 唯一外部 blocker |
| A4 | **RM feed-liveness 自检** + 风控状态逐条标(防假已活) | 老韩 + GM | ✅ 2026-05-30 (commit 8a641c2) — FeedKey 10 红线 last_fed_ns atomic 数组 + report() + daemon 首 tick 自检喊 NEVER FED; 零 ABI (私有成员); 逐条标降级 M2. RiskGateway 55 + paper_loop 23 绿 |
| **P1-9** | **RM slippage gate 单位失配根治** (A2 副产, MVP「第一笔成交」拦路石) | 老韩 spec / GM | ✅ 2026-05-30 (commit 44f10eb) — order_size 走 .to_pusd() micro→whole; 4 test 重校(R13/17/17b/18)+ 新 sanity 守卫 500pUSD ρ=0.1 过; F5 CI 护栏; 1061/1061 绿 |
| A5 | DD 喂数(A1 完成后)/ c4 闭合(sizing 取 RM 真值,M2 前)/ consec(M2) | 老韩 + GM | 🟠 排在 A1/A4 后; **DD 喂数验证需 A3 (Goalserve 白名单, 用户运营) 接通 live 数据** |
| A6 | 流程: 改动分层(契约级全闭环 / 单向安全免陪跑)+ P2 回流 IC | 老胡 | 🟡 |

**一句话校准:** 下一步**不是**接 DD,而是 **PositionLedger micro 根治(R-4)** —— 它一刀解决单位债真源 + PnL bug + DD 喂数前置 + <1pUSD 静默丢仓,是本会话复盘挖出的最高杠杆动作。
