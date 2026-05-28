# Acceptance Spec v1 — M1 / M4.5 / M5 三阶段验收标准

- Owner: 小颖 (requirements-analyst)
- Last review: 2026-05-28
- 验收人: 老雷 (GM) + 老钱 (CPO) + 老胡 (PM)
- 关联输入 (权威):
  - `CLAUDE.md` §2 (Vision + Mission + M1/M4.5/M5 北极星) + §8 (红线)
  - `docs/MEETINGS/2026-05-28-gm-business-needs-must-haves.md` (老雷 GM must-have, 触发文档)
  - `docs/RESEARCH/laoqian-business-capabilities-v1.md` (W4 Wave 22 并行, **未落盘, 等落盘后做 cross-ref**)
  - `docs/RESEARCH/xiaodu-prd-v2-user-journey.md` (W4 Wave 22 并行, **未落盘, 等落盘后做 cross-ref**)
- 状态: v1 草稿. 三 stage 验收 baseline, 后续与老钱 BC / 小杜 UC 整合.

---

## 0. 文档目的

把 GM 10 条 must-have (M-01 ~ M-10) + CLAUDE.md §2 三 stage 业务目标 (M1 / M4.5 / M5), 倒推为 **testable + measurable** acceptance criteria, 交给 QA (小宋) 写测试用例, 交给 dogfood (小宫) 跑端到端, 交给 GM / CPO / Operator 做 UAT.

### 0.1 SSOT 边界

- **本文是验收 SSOT** — M1/M4.5/M5 通过判定的最终裁判
- 不重写老钱 BC (BC 是业务能力 SSOT, W22 v1 落盘后做 cross-ref)
- 不重写小杜 UC (UC 是 user journey SSOT, W22 v2 落盘后做 cross-ref)
- 不重写老韩 RM 规则 (RM SSOT 在 `laohan-riskmanager-design-v0.x.md`, 本文仅引用)

### 0.2 格式约定

每条 acceptance 一行式:
`[ID] [标题] | 方法: <Unit/Integration/Sim/Chaos/Manual/Benchmark> | 测试人: <persona> | 度量: <阈值> | 过: <bool>`

### 0.3 不允许的描述 (主观 / 非测)

- ❌ "UI 好看"
- ❌ "用户满意"
- ❌ "性能良好"
- ❌ "代码优雅"

所有 acceptance 必须能用 `assert(...)` / 测试脚本 / 度量数字判定.

---

## 1. M1 (T+6 月, 2026-11-12) 验收标准 — Moneyline paper 跑通

**M1 业务目标:** Moneyline 单盘口 paper 实盘跑通, 端到端无崩溃 72h, 0 风控失效.

(注: CLAUDE.md §2 写 "Moneyline 单盘口实盘跑通, 第一笔成交". 老钱 mvp-scope v1 §1 锁定 "T+24 周 paper 跑通". 这里 M1 = paper 跑通, **不进真钱**. 真钱首笔留到 M5.)

### 1.A 风控有效性 (源: GM M-02 + 永久红线 §8 ①)

- `M1-A01` RM 启动默认 SAFE_MODE | Unit+Integration | 老韩+小宋 | 冷启动 1s 内 state==SAFE_MODE, evaluate 返 SAFE_MODE_BLOCK | 过: bool
- `M1-A02` RM 三签解锁 | Unit+Manual | 老韩+老唐+老雷 | SAFE_MODE→RUNNING 需 3 签 (GM+risk+audit), 缺一拒 | 过: 单/双签时 state 不变
- `M1-A03` OrderIntent 100% 经 RM (0 绕过) | CI grep+WAL audit | 老高+老唐 | (1) 静态扫无绕 RM 路径; (2) 1h 窗 signer.wal audit_id 数 == risk.wal allow 数 | 过: (1)+(2)
- `M1-A04` 21 种 RejectCode 各 ≥1 单测 | Unit | 小宋 | coverage 21/21 触发 | 过: ==100%
- `M1-A05` RM HALTED 全员告警 <5s | Integration+Chaos | 老韩+小郑+小宫 | 邮件+notification+Grafana 三通道 t_alert-t_halt p99<5s | 过: 3 通道 100% + p99<5s
- `M1-A06` audit chain hash 不可篡改 | Unit+Sim | 老唐+小宋 | verify <2s; 篡改 1 条 → 失败并定位 | 过: 检测率 100%
- `M1-A07` RM evaluate p99 <1ms | Benchmark | 老姜+老韩 | 1M evaluate, p99<1ms p999<5ms | 过: p99<1ms
- `M1-A08` HALTED 自愈禁止 | Manual | 老雷+老韩 | HALTED 后 24h 内任何自动/单签/重启回 RUNNING 被拒 | 过: 24h state==HALTED

### 1.B 数据接入 (源: BC-01+BC-04, R-20, 永久 §8 ⑥⑦)

- `M1-B01` PM WSS sports channel 订阅 | Integration | 老陈+小冯 | 启动 30s 内 book_delta_count>0 | 过: >0
- `M1-B02` Goalserve inplay 接入 (NBA+NFL+MLB) | Integration | 小段+小冯 | 3 sport 各 ≥1 live event 时 5min 内 tick 入 wal | 过: 3/3
- `M1-B03` WSS reconnect <30s | Chaos | 老吴+老陈 | 注入 60s 中断后 reconnect p95<30s | 过: p95<30s
- `M1-B04` 4 时间戳契约全链路 | Unit+Integration | 小余+老郭 | event_ts≤data_source_ts≤ingestion_ts≤as_of_ts, 任一缺失/违例 fail-closed | 过: 0 违例入决策
- `M1-B05` PM book vs Pinnacle 时序对齐 | Integration+Sim | 小梁+老彭 | \|PM.ds_ts - Pinnacle.ds_ts\| p95<5s | 过: p95<5s
- `M1-B06` WSS schema 漂移检测 | Integration | 小冯+老高 | field 缺失/多余/类型变 → fail-closed+告警 <60s | 过: mock 漂移 100% 触发
- `M1-B07` 跨洋链路质量监控 | Integration | 老陈+小郑 | RTT p50/p99+心跳缺失+reconnect 入 metrics, 1min 粒度 | 过: 72h 连续 4 曲线

### 1.C 信号生成 (源: BC-02)

- `M1-C01` P0-01 PinnacleNoVig 5 条件 AND | Unit+Sim | 小程+小梁 | LiveSection match+mid diff+liquidity+TTL+vig 上限, 任一不满足不发 | 过: 25 case (5×5) 全过
- `M1-C02` LiveSection 5 enum 分类 | Unit | 小田+小宋 | PREGAME/Q1/Q2/H_TIME/Q3/Q4/OT, 50 fixture | 过: accuracy 100%
- `M1-C03` signal tick p99 <500us | Benchmark | 老姜 | 1M tick, p99<500us p999<2ms | 过: p99<500us
- `M1-C04` feature_snapshot_id 唯一 (R-20 PIT) | Unit+Integration | 小余+老唐 | 1M emit, set size==1M | 过: 0 碰撞
- `M1-C05` signal→intent 时序透传 | Integration | 小程+老唐 | (event_ts, ds_ts, as_of_ts) 完整透传 | 过: 100%
- `M1-C06` 0 lookahead bias | Unit+Sim (R-12) | 小蒋+老周 | signal 在 t 仅看 ds_ts≤t 数据 | 过: 0 violation

### 1.D 订单执行 paper (源: BC-04+BC-05, 永久 §8 ⑧)

- `M1-D01` paper 端到端 <50ms | Sim | 老姜+小宋 | WSS recv→VirtualFill emit p99<50ms | 过: p99<50ms
- `M1-D02` paper/risk WAL 物理隔离 (R-11) | CI+Integration | 老王+老高 | 路径/fd/inode 不同, 1h 跑量 0 cross-write | 过: 0 cross
- `M1-D03` paper 不写真账本 (R-11) | CI grep | 老高+老吴 | paper engine 调用图无 position/pnl_ledger/nonce_ledger 写 | 过: 0 命中
- `M1-D04` 4 ts 违例 → INVALID_INTENT.TS_* | Unit | 小宋 | 4 种违例 (event>ds, ds>ingest, ingest>as_of, 任一==0) 各自 RejectCode | 过: 4/4
- `M1-D05` VirtualFill 可解释 | Unit+Sim | 小袁+小蒋 | 给 PM book+intent, (filled_size, avg_price, slippage) 公式重算 | 过: 偏差 <1bp

### 1.E 复盘能力 (源: GM M-03, BC-08)

- `M1-E01` audit_id 完整链路复盘 | Integration+Manual | 老唐+老雷 | 任一 audit_id 拉出 signal_ctx+PM book+Pinnacle quote+RM verdict+signer+matcher, 查询 <10s | 过: 6 段齐 + <10s
- `M1-E02` snapshot_id ↔ audit_id 双向 join | Unit+Integration | 小余+老唐 | SELECT JOIN 任一方向 0 孤儿 | 过: 100%
- `M1-E03` audit chain hash 全过 | Sim | 老唐 | 72h 跑后 verify 0 mismatch | 过: 0
- `M1-E04` 复盘视图导出 (CSV/JSON) | Manual | 老雷+小程 | 给 [t1,t2] 导出 fills+rejects+signals 双格式 | 过: 4 周抽样 100% 成功

### 1.F 系统健康 (源: GM M-07, BC-09)

- `M1-F01` 在线率 ≥99.0% (paper 期) | 长跑 | 小郑+老吴 | 7 天滑动 uptime/total≥0.99 | 过: ≥99.0%
- `M1-F02` WAL fsync p99 <1ms | Benchmark | 老王+老姜 | 1M write p99<1ms, group commit batch 64 或 1ms | 过: p99<1ms
- `M1-F03` vCPU 7 核 affinity | Manual+Sim | 老周+老吴 | 7 核线程 affinity 与 v0.6 一致 | 过: 7/7
- `M1-F04` 0 个 R-12 违例 | Sim | 老周+老姜 | WSS event loop 0 同步 REST/0 阻塞 IO/0 锁>100us | 过: 3/3 全 0
- `M1-F05` 决策延迟全栈观测 | Integration | 小郑+老姜 | 4 阶段 (ingestion/signal/RM/signer) p50/p99/p999 进 Prometheus | 过: 12 指标在线
- `M1-F06` 72h 无崩溃 (硬条件) | 长跑 | 小宫+老吴 | 72h 0 segfault/0 OOM/0 死锁/0 panic | 过: 4/4 全 0

### 1.G 紧急操作 (源: GM M-05, BC-12)

- `M1-G01` GM 一键 halt 二次确认 | Manual | 老雷 | click→确认→halt 生效 t<1s, state==HALTED | 过: ≤1s
- `M1-G02` 三签解锁 (老韩+老唐+GM) | Manual | 老雷+老韩+老唐 | 3 签 →RUNNING, <3 签保 HALTED | 过: bool 正确
- `M1-G03` 系统 crash → 自动 SAFE_MODE | Chaos | 老吴+老韩 | kill -9 后 watchdog 重启, state==SAFE_MODE | 过: 100%
- `M1-G04` signer 异常 → 自动 SAFE/HALT | Chaos | 老孙+老韩 | signer timeout/panic/错误码 自动 HALT 或 SAFE | 过: 100%

### 1.H 当日 PnL 可见 (源: GM M-01)

- `M1-H01` 今日 paper PnL 一个数字 | Manual | 老雷+老胡 | 看板首屏 <1 屏可见 $X (绿/红) | 过: 1 屏内
- `M1-H02` 7/30 天/Sprint/季度 趋势 | Manual | 老雷 | 4 时间维度趋势图 | 过: 4/4 在线
- `M1-H03` PnL 数据准确 (与 audit chain 一致) | Integration | 老唐+小董 | 看板 PnL == SUM(virtual_fill.pnl) | 过: diff==0

**M1 合计 38 条** (≥30 ✓)

---

## 2. M4.5 (T+18 月, 2027-05) 验收标准 — paper 7 hard gate

**M4.5 业务目标:** paper trading 连续 2 周通过 7 hard gate, 解锁实盘.

### 2.A 7 hard gate (源: 小董 W4 framework, GM M-04)

- `M4.5-G1` PnL t-test p<0.05 | Sim+统计 | 小董+小梁 | 2 周 PnL vs 0 one-sample t-test | 过: p<0.05
- `M4.5-G2` Sharpe 95% bootstrap CI 下限 >0.5 | Sim+统计 | 小董+小梁 | 1000 次 bootstrap Sharpe 分布 | 过: CI_lower>0.5
- `M4.5-G3` 风控失效次数 ==0 | 长跑 | 老韩+老唐 | 2 周 RM 绕过/hash 篡改/红线触发任一 >0 → 重置 | 过: 0/0/0
- `M4.5-G4` 在线率 ≥99.5% | 长跑 | 小郑+老吴 | 2 周 uptime/total≥0.995 | 过: ≥99.5%
- `M4.5-G5` 最大 DD ≤8% | Sim | 小梁+小董 | 2 周 paper equity max DD≤8% (北极星 15% paper 收紧) | 过: ≤8%
- `M4.5-G6` 笔数 ≥50 | Count | 小董 | 2 周 fills ≥50 | 过: ≥50
- `M4.5-G7` paper > random baseline | Sim+统计 | 小董+小梁 | vs random sign (Kelly/2) baseline one-sided t-test | 过: p<0.05

### 2.B 附加 acceptance

- `M4.5-A01` BC-07 结算对账偏差告警 | Integration | 小余+老唐 | fill 实际结算 vs 系统记录偏差 >0.01 → alert <60s | 过: mock 100% 告警
- `M4.5-A02` BC-11 ML 数据钩子 (32 feature) | Unit+Integration | 小邓+小余 | 每 fill 关联 32 feature+TrainingLabel join 0 孤儿 | 过: 100%
- `M4.5-A03` BC-12 strategy decay 三签解锁 | Manual | 老雷+小梁+老韩 | decay 检测 → 自动暂停 + 三签恢复 | 过: 单/双签时保持暂停
- `M4.5-A04` G1-G7 任一 fail → 重置 | Manual | 老胡+老雷 | 任一 gate fail → 状态机回起点, 2 周计时重启 | 过: 窗口=0
- `M4.5-A05` live mode build pass (不部署) | CI | 老吴+老孙 | cmake --target live 编译, 无 stub | 过: build pass
- `M4.5-A06` live signer 单测 100% | Unit | 老孙+老沈 | libsecp256k1+SecureBuffer+EIP-712 cov 100%, PM testnet vectors 100% | 过: 100%/100%
- `M4.5-A07` alpha decay 检测在线 | Integration | 小董+小梁 | 7/30 天滑动 Sharpe+胜率+edge 入 Grafana, 跌破自动 alert | 过: mock decay 100% 触发
- `M4.5-A08` maker/taker 拆分 PnL (GM M-06) | Integration | 小董+老胡 | 看板 signal/sport/market/maker-taker 4 维拆分 | 过: 4 维在线
- `M4.5-A09` 投资人导出 (GM M-10) | Manual | 老雷+老钱 | 一键导出季度 PnL+Sharpe+DD+笔数+净收益 PDF/CSV | 过: 2/2
- `M4.5-A10` 灾难恢复演练 (RTO<1h, RPO<1min) | Chaos | 老吴+老韩 | 全节点故障注入, RTO<1h RPO<1min | 过: 双指标达标

**M4.5 合计 17 条** (7 gate + 10 附加 ✓)

---

## 3. M5 (T+24 月, 2027-11) 验收标准 — 实盘首笔成交

**M5 业务目标:** Moneyline 实盘首笔成交, 0 风控失效.
**前置硬条件:** M4.5 必须通过 (paper 7 gate 连续 2 周不重置).

### 3.A 实盘上链

- `M5-A01` Polygon RPC 接入 (5 vendor) | Integration | 老叶+老陈 | Infura/Alchemy/QuickNode/Ankr/公开 5 个+健康检查+failover<10s | 过: 5/5 + <10s
- `M5-A02` live signer C++ (libsecp256k1+SecureBuffer+EIP-712) | Unit+Integration+Manual | 老孙+老沈+老雷 | 已知向量 100%+testnet 100%+mainnet 受控试单 1 笔 | 过: 3/3
- `M5-A03` 私钥永不入日志/git/落盘加密 | Security audit | 老沈+老黄 | (1) log grep 0; (2) git history 0; (3) 落盘 Sygnum/Taurus 加密 | 过: 3/3
- `M5-A04` 实盘首笔 <$100 验证 | Manual | 老雷+老韩 | size 上限 $100 + 成交后 RM 暂停 24h | 过: ≤$100 + 24h 暂停
- `M5-A05` live audit chain Polygon Merkle anchor | Integration | 老叶+老唐 | 每日 Merkle root 锚定 Polygon, tx hash 可查 | 过: 7 天 7 tx
- `M5-A06` gas/nonce 管理 | Integration+Chaos | 老叶+老孙 | nonce 单调 0 冲突, gas 超限自动暂停 | 过: 0+100%
- `M5-A07` 链上 replay/fork 防护 | Sim | 老叶+老沈 | EIP-712 domain separator 含 chainId, fork/replay 100% 拒 | 过: 100%
- `M5-A08` 实盘 RM 阈值收紧 | Manual | 老韩+老雷 | live Kelly/单笔/日累计 size 上限 比 paper 严, 3 阈值参数化 | 过: 3 阈值存在

### 3.B 实盘运维

- `M5-A09` 7×24 自动运行 (GM M-09) | 长跑 | 小宫+老吴 | 实盘 30 天 0 人工干预 (除告警响应) | 过: 干预==0
- `M5-A10` 异常自动 SAFE_MODE | Chaos | 老吴+老韩 | 6 类异常 (网络/RPC 全断/signer panic/价格异常/nonce 冲突/RM 触发) 100% 自动 SAFE | 过: 6/6
- `M5-A11` 重大决策 escalate 操盘手 | Manual | 老雷+老胡 | 6 类重大事件 escalate 邮件+电话 <60s | 过: 6/6
- `M5-A12` 实盘 PnL 链上对账 | Integration | 老叶+老唐 | 每日 PnL vs 链上 USDC 余额变化, 偏差 <$0.01 | 过: 7 天 0 偏差
- `M5-A13` 实盘最大 DD ≤15% (北极星) | 长跑 | 小梁+老韩 | 90 天 max DD≤15% | 过: ≤15%
- `M5-A14` 实盘 Sharpe ≥1.5 (90 天 OOS) | 长跑+统计 | 小梁+小董 | 90 天 daily Sharpe≥1.5 | 过: ≥1.5
- `M5-A15` 平台 ToS / 速率 / 反操纵合规 | Integration | 老黄+老陈 | 0 违反速率/0 反操纵触发/0 reselling | 过: 3/3 全 0

**M5 合计 15 条** (≥15 ✓)

---

## 4. 跨 stage 不可降级 acceptance (永久红线)

来源: `CLAUDE.md` §8 + GM must-NOT-have.

- `永久-01` 任何 RM 绕过 → 立即回滚 + post-mortem | §8 ① | 老高+老唐 | CI grep+WAL audit 任一发现 → 自动回滚 + 24h post-mortem
- `永久-02` 私钥明文落盘/出现日志 → 权限暂停 | §8 ② | 老沈 | log grep 0 命中, fd 检查无明文
- `永久-03` 回测/实盘数据处理不一致 → 策略禁上线 | §8 ③ | 小蒋+老周 | engine binary diff 检测, 处理函数共享 SSOT
- `永久-04` WSS event loop 同步 REST/阻塞 IO/锁>100us → P0 | §8 ⑦ (R-12) | 老周+老姜 | Sim 100us 阈值, 任一违例 P0
- `永久-05` paper 污染真账本 → P0 | §8 ⑧ (R-11) | 老高+老吴 | CI grep paper 调用 position/pnl_ledger/nonce_ledger, 0 命中
- `永久-06` 4 时间戳契约任一缺失 → P0 | §8 ⑨ (R-20) | 小余+老郭 | 任一 ts 缺失 fail-closed
- `永久-07` 数据源文档 4 维扫描 | §8 ⑩ (R-33) | 小米+老郭 | 官方 portal/SDK 源码/实测 RTT/同行 SSOT 全签字
- `永久-08` HR 注册前置 | §7 ⑦ | 小林+老雷 | persona file 新建前必有 registry 工号+入职日+单元+状态+HR 签字
- `永久-09` GM 派 wave 前 4 题自检 | §7 ⑧ | 老雷自检+小林监督 | 4 题任一 yes → 重设计
- `永久-10` 数据 schema 静默变更 → 责任人事故 | §8 ④ | 小余+老高 | schema change 必经 PR+下游通知
- `永久-11` Polymarket/Goalserve ToS 违反 → 立即回滚 | §8 ⑥ | 老黄+老陈 | 速率/反操纵/reselling 任一触发 24h 回滚

**永久 11 条** (≥10 ✓)

---

## 5. 测试方法映射

| 测试类型 | 主导 | 典型 acceptance |
|---|---|---|
| Unit test | 小宋 + 各 owner | M1-A04 / M1-C02 / M1-C04 / M1-D04 |
| Integration test | 小宋 + 老唐 | M1-A03 / M1-B01..B07 / M1-E01..E04 |
| Sim test (老周 R-12 4 场景) | 老周 + 小宋 | M1-A06 / M1-C06 / M1-D01 / M4.5-G1..G2 |
| Chaos test | 老吴 + 老韩 | M1-B03 / M1-G03 / M1-G04 / M4.5-A10 / M5-A10 |
| Manual UAT | 老雷 / 小宫 / 老胡 | M1-G01 / M1-G02 / M1-H01 / M4.5-A03 / M5-A02 |
| Benchmark | 老姜 | M1-A07 / M1-C03 / M1-F02 |
| 长跑监控 | 小郑 + 老吴 | M1-F01 / M1-F06 / M4.5-G3..G4 / M5-A09 |
| 统计检验 | 小董 + 小梁 | M4.5-G1 / G2 / G5 / G6 / G7 / M5-A14 |
| Security audit | 老沈 + 老黄 | M5-A03 / 永久-02 |

---

## 6. GM must-have 覆盖矩阵

| GM must-have | M1 acceptance | M4.5 acceptance | M5 acceptance |
|---|---|---|---|
| M-01 看 PnL | M1-H01..H03 | M4.5-A09 (导出) | M5-A12 (链上对账) |
| M-02 风控不失效 | M1-A01..A08 | M4.5-G3 | M5-A03 / A08 / 永久-01 |
| M-03 复盘任何决策 | M1-E01..E04 | M4.5-A02 (ML 钩子) | M5-A05 (链上 anchor) |
| M-04 M4.5 解锁状态 | (M1 阶段不可见, 等 M4.5) | M4.5-G1..G7 全部 | M4.5 必通过 (前置) |
| M-05 紧急 halt | M1-G01..G04 | (继承 M1) | M5-A10 / A11 |
| M-06 策略赚什么钱 | (M1 不强求) | M4.5-A08 (拆分 PnL) | M5-A14 (实盘 Sharpe) |
| M-07 系统健康度 | M1-F01..F06 | M4.5-G4 | M5-A09 / A13 |
| M-08 换信号配方 | (P1, M1 不强求) | M4.5-A03 (三签解锁) | M5-A08 (实盘收紧) |
| M-09 操盘手不在岗 | M1-G03 / G04 (自动 SAFE_MODE) | (paper 期 7×24) | M5-A09 / A10 / A11 |
| M-10 给投资人看 | (P2, M1 不强求) | M4.5-A09 (导出) | M5-A14 (业绩) |

**结论:** 10/10 GM must-have 全覆盖.

---

## 7. UAT 执行人清单

### 7.1 Operator-led UAT

- **执行人:** 小宫 (dogfood-tester) + 老胡 (PM)
- **覆盖:** M1-G01..G04 (紧急操作), M1-H01..H03 (PnL 看板), M1-E04 (复盘导出), M1-F06 (72h 长跑), M4.5-A04 / A09, M5-A04 / A09

### 7.2 Analyst-led UAT

- **执行人:** 小程 + 小蒋 + 小邓 + 小董
- **覆盖:** M1-C01..C06 (信号), M1-E01..E03 (复盘链路), M4.5-G1 / G2 / G5..G7 (统计 gate), M4.5-A02 / A07 / A08, M5-A14

### 7.3 GM-led UAT

- **执行人:** 老雷 + 老钱 (CPO)
- **覆盖:** M1-A02 / G02 (三签), M1-H01..H03 (PnL 看板), M4.5-A03 / A04 / A09 (解锁三签 / 导出), M5-A02 / A04 / A08 / A11 (实盘首笔 / escalation)

### 7.4 风控-led UAT

- **执行人:** 老韩 + 老唐 + 老沈
- **覆盖:** M1-A01..A08 全部, M1-D02..D04, M4.5-G3 / A01 / A06, M5-A03 / A05 / A06 / A08 / 永久-01 / 02

### 7.5 自动化

- **执行人:** 小宋 (test-replay-engineer)
- **覆盖:** 全部 Unit + Integration + Sim + Chaos test, CI 持续运行, 任一退化 < 1h 内告警

### 7.6 Security-led UAT

- **执行人:** 老沈 + 老黄 + 老叶
- **覆盖:** M5-A03 / A05 / A06 / A07 / A15, 永久-02 / 11

---

## 8. 与并行 wave 输入的对齐 (待落盘后做)

老钱 BC v1 与小杜 UC v2 W22 并行交付, **本文成稿时未落盘**. 落盘后小颖将做:

1. **BC ↔ acceptance 反向覆盖矩阵** — 每条 BC 至少 1 条 acceptance 锚定
2. **UC ↔ acceptance 正向覆盖矩阵** — 每个 user journey step 至少 1 条 acceptance
3. **gap 反馈** — 若有 acceptance 找不到 BC / UC 对应, 反馈 @老钱 / @小杜 补需求
4. **冲突识别** — BC / UC / acceptance 三方不一致, 升级 @老雷 (CLAUDE.md §6 决策机制)

---

## 9. 风险与未决事项 (open question, 等 GM 拍)

### 9.1 紧急 halt 后 open position 处置

GM M-05 原文: "真 halt 后, 已 open position 怎么处理 (自动 close? 等到期? 操盘手手动?) — **这是产品决策**, 待 CPO 拍."

**小颖建议:** M1 paper 阶段不存在真 open position, 此问题在 M5 实盘前必须由老钱 + 老韩 + 老雷三方决议. acceptance 占位:

- M5-A16 (待补) open position 处置策略落地 + 单测 + UAT

### 9.2 M1 "首笔成交" 定义

CLAUDE.md §2 写 "第一笔成交". 老钱 mvp-scope §1 锁定 "T+24 周 paper 跑通".

**小颖理解:** M1 = paper 跑通 (含 VirtualFill 第一笔), 真钱首笔留 M5. 若 GM 意见是 "M1 必须真钱首笔", 本文 M1 章节需大改, 加 signer / Polygon RPC / 三签流程进 M1.

**等老雷拍板**.

### 9.3 跨平台扩展拒绝面 acceptance

GM N-04 "只做 Polymarket / 体育 / Polygon". 这是 must-NOT-have, 是否需要 acceptance ?

**小颖建议:** 不写"扩展提案被拒"为 acceptance (太负面), 但在 PR review checklist 加一条 "本 PR 是否引入跨平台 / 跨资产 / 跨链路径? 若是, 走 §6 升级路径".

---

## 10. 自检 (小颖)

W4 Wave 22 输出物自查:

- [x] M1 acceptance ≥ 30 (实际 38)
- [x] M4.5 acceptance ≥ 17 (实际 17, 7 gate + 10 附加)
- [x] M5 acceptance ≥ 15 (实际 15)
- [x] 永久红线 ≥ 10 (实际 11)
- [x] 覆盖矩阵 (10/10 GM must-have)
- [x] UAT 人员清单 (6 类执行人)
- [x] 每条 acceptance testable + measurable (无主观)
- [x] 不写代码 / 不写 endpoint
- [x] 不读 RESEARCH/ 80+ 报告 / 不读 Polymarket / Goalserve 文档
- [x] 只读 CLAUDE.md + GM must-have + (并行 wave 输入待落盘)
- [x] ≤ 700 行

---

**最后更新:** 2026-05-28 by 小颖 (requirements-analyst)
**下次 review:** 老钱 BC v1 + 小杜 UC v2 落盘后做 cross-ref (Wave 22 收口)
**升级路径:** 任何冲突 / 缺口 → @老雷 (GM, 48h 响应)
