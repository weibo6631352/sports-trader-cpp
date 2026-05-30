---
owner: 老韩 (E-009, risk-engineer, B 主管)
occasion: W5 末 Wave 27 架构 challenge 投票会
model: ADR-009 v2 = Sonnet
constraint: 不替其他主管投, 不替 GM 拍, cpp = 0 守住
last_review: 2026-06-01
---

# 老韩 B 主管投票 — W5 末架构 Challenge (Wave 27)

老板原话: "我们要做的是发现问题和架构优化, 需要协商投票讨论, 不是死板的硬套最初版本, 我们不向不合理的架构妥协."

---

## §1 五议题投票

### 议题 1: paper/live 共享 binary (R-2)

**我投: A (共享 binary)**

风控理由: R-1 RM 唯一入口、R-11 paper 不污染真账本, 靠 build-time ExecutionMode 强切 + PID file + WAL 路径物理隔离, 不靠 binary 分叉。分叉维护两份 RM 代码是更大的风险 — 两份可能漂移导致 paper 策略在不同 RM 版本下测试, 上线时实际行为不一致。W5 actual code 证明 CMake `if(STCPP_PAPER_BUILD)` + 三路 WAL + SingleInstanceLock 已能 enforce R-11，共享 binary 风控可闭合。

---

### 议题 2: WSS 拓扑

**我投: C (2×4)**

风控理由: R-12 WSS event loop 禁阻塞。1×8 单 loop 一旦阻塞全盘订阅停摆, 风控无法收到 market state 更新, fail-open 风险最大。4-5×2 过细, 连接粒度管理复杂且 ingestion_ts 打点分散增加 R-20 4 ts 对齐难度。2×4 在 loop 级故障域隔离和管理复杂度之间取中, RM STALE_DATA 触发面可控, ingestion_ts 来源明确。1×3×3 层级过深, M5 前不需要。

---

### 议题 3: 跨洋部署

**我投: C (M4.5 前单点, M5+ 多点)**

风控理由: 老吴 v0.1 已定 AWS us-east-1 主 + Hetzner Ashburn warm standby, 这是当前最合理的起点。M4.5 前单点强制 RM enforce 唯一性 (无跨节点 nonce 竞争), position_cap / bankroll 状态无需分布式对账。单点前提是 Hetzner warm standby 的 RTO < 5min 必须真实演练过 (否则 fallback 是空头支票)。多点时机: M4.5 paper engine 首判窗口验证稳定后, 再评估是否需要 active-active。

---

### 议题 4: ML 时机

**我投: B (W6 shadow)**

风控理由: STRATEGY_DECAYED 三签解锁 (老钱+老雷+小梁) 是 ML 进生产最重要的风控闸。但 shadow 数据越早开始积累越好 — 小邓 ML roadmap v2 明确 "paper 跑起来后每天产出的虚拟 PnL + 信号触发是金矿, 等 M5 = 浪费 4 个月"。W6 shadow 启动, ML-R1 (不进 RM 决策) + ML-R2 (不进 OrderIntent) + ML-R3 (M4.5 7 gate 不看 ML PnL) 红线一字不改。M2 选项太激进, ML 推理代码 (ONNX C++) 尚未有 latency bench 数据 (D1-6 是 9/18), 贸然提早进 shadow 没有数据基础; M4.5 后 (A 选项) 则浪费了宝贵的数据积累窗口。W6 shadow = 风控 + 进度平衡点。

---

### 议题 5: vCPU pin

**我投: C (M4.5 前 single vCPU)**

风控理由: R-12 WSS event loop 不阻塞是绝对红线。M4.5 前 single vCPU 的好处是: 没有跨核 cache miss、没有 false-sharing、RM evaluate() p99 ~100us + Signal Engine tick p99 ~500us 已在单核上量化, 引入 pin 方案前不应产生新变量。7 vCPU 方案 (A 选项) 是 M4.5+ 才需要的规模, 当前 paper engine 没有并发下单需求, 过度拆分反而增加 event loop 竞争风险。2-3 vCPU (B 选项) 是中间状态, 但 pin 方案复杂度已上来了、性能收益还不明确。M4.5 首判窗口后有了真实 throughput 数据, 再升级 pin 方案。

---

## §2 风控 Architecture Smell (B 主管自抛)

### Smell-1: 21 RejectCode — 够不够?

W5 actual: 21 enum + 9 sub_reason (INVALID_INTENT 内部细分)。

问题: `INVALID_INTENT` 一个 enum 承载了 9 种不同的输入污染场景 (NaN/Inf、时间戳零值、stale、非法 tick size、4 ts 不等式等), 现在 W5 末还新增了 `TS_ORDER_VIOLATED` 和 `TS_FUTURE` 两个 sub_reason。

我的判断: **21 enum 目前是够的, 但有 2 个升级信号需要观察**:
1. 如果 `AUDIT_WAL_BACKPRESSURE` (enum 18) 在实盘中频繁触发, 说明 WAL 写入是 RM 瓶颈, 需要拆出更细的原因码 (backpressure vs fsync 阻塞 vs 队列满)。
2. `STRATEGY_DECAYED` 三签解锁机制落地后, 解锁中的 MONITORING 状态 (KELLY 0.5x 期间) 需要一个独立的 RejectCode 或 RiskState 来区分 "完全 HALT" vs "降仓运行中", 否则 audit 日志无法精确区分两种状态。

**建议: W6 出 enum 扩展 protocol (bump 版本时机 + 新增 enum 条件), 挂在我的 W5-B-04 INVALID_INTENT 终版表 task 里一并交付, 不单独开 ticket。**

### Smell-2: audit chain BLAKE3 vs XOR stub — 上链时够不够强?

W5 actual: audit_emitter BLAKE3 stub (XOR hash chain), 老唐 W5-B-02 真 BLAKE3 替换还在 TBD。

问题: BLAKE3 stub 是 XOR, 不是密码学安全 hash。当前 paper 阶段只做内部回测验证, XOR 可以接受。但有一个风险:

**stub 如果意外进入 live build** (R-7 build-time switch 还在 W5-B-03 联调中, 尚未端到端验证), paper_audit.wal 用 XOR chain 生成的 "hash" 在 audit replay 时会通过验证但没有防篡改性。这不是 paper 阶段的 P0 — 但 W5-B-03 联调必须在 M4.5 gate 前完成, 否则 live build 路径不可信。

**建议: W5-B-02 (真 BLAKE3) 和 W5-B-03 (R-7 build-time switch 端到端联调) 必须作为 M1 acceptance gate 的前置条件, 不允许带 stub 进 live build。@老唐 @老沈 W6 末 deadline 不能移。**

### Smell-3: position WAL 缺位的 R-11 完整性风险

W5 actual: 4 wal kind 设计 (risk_audit / paper_audit / shadow_audit / position), 但 position WAL 未落代码 (偏离 2, P1)。

问题: 当前 paper engine 的 position 状态在内存中, 没有持久化。如果 paper engine 崩溃重启, position 数据全丢。这意味着:
1. RM 的 bankroll_cap / daily_loss / consec_loss 等状态在重启后归零, 等于 circuit breaker 被 reset。
2. M4.5 gate 的 7 hard gate 依赖历史 paper fill 数据, position WAL 缺位 = gate 输入数据可能不完整。
3. R-11 "paper 不污染真账本" 目前靠 build-time switch + WAL 路径隔离, 但如果 position 状态没持久化, 重启后的 paper engine 无法验证 "上一次运行的 position 边界是否在允许范围内"。

**建议: position WAL (偏离 2) 优先级应升 P0, 不是 P1。我建议 W6 第一周 @老周 接单, 与 v0.7 §8 启动期序列同步设计。**

### Smell-4: 三签解锁的 human-in-the-loop 操作路径未定义

W5 actual: STRATEGY_DECAYED enum 已立, 三签解锁 (老钱+老雷+小梁) 机制 ADR-003 §3.3 和老唐 v1.1 已立 `AET_STRATEGY_UNLOCK` payload schema, 但实际操作路径 (谁触发、通过什么界面/工具签字、签字结果怎么注入 audit chain) 尚未定义。

问题: 如果 STRATEGY_DECAYED 在实盘中真触发了, 三个人要怎么操作解锁? 目前没有 tool, 没有 SOP, 没有 CLI 命令。这是一个 "spec 有、实施路径空白" 的风险。

**建议: W6 出解锁操作 SOP v1 (我自己出 spec, 纯文档不是 cpp), 包括: 触发通知链 (PagerDuty/飞书) + 签字工具 (临时用 CLI `stcpp-risk unlock --strategy-id X --approver-role CPO/GM/FINANCIAL`) + audit chain 注入方式。M2 与 BLAKE3 三签实施同步落地。**

---

## §3 B 主管最关心议题排序

1. **议题 1 (paper/live binary)** — 直接影响 R-1 / R-11, 是风控部门的生死线, 必须确认共享 binary + ExecutionMode 物理隔离方案是否真能守住。
2. **Smell-3 (position WAL 缺位)** — 我认为这是当前最被低估的风险, 比五个议题中任何一个都紧急。
3. **议题 4 (ML 时机)** — STRATEGY_DECAYED + 三签解锁是 ML 进生产前的最后一道闸, ML shadow 时机直接决定数据积累窗口和风控验证时间。
4. **议题 2 (WSS 拓扑)** — R-12 event loop 非阻塞是红线, 拓扑选型影响 ingestion_ts 可靠性和 RM STALE_DATA 触发准确率。
5. **议题 5 (vCPU pin)** — 影响 R-12, 但 M4.5 前单核方案风险低, 可以先跑数据再决策。
6. **议题 3 (跨洋部署)** — M4.5 前单点风险可控, Hetzner warm standby 演练优先级更高。

---

## §4 不耻下问 (CLAUDE.md §3 第 4 条铁律)

- **@老周 (A):** position WAL 优先级我认为应升 P0 — 你 W6 第一周能否接单? 与 v0.7 §8 启动期序列同步设计最合适。如果不接, 我需要知道由谁承接, 不能空着。
- **@老唐 (B):** W5-B-02 真 BLAKE3 + W5-B-03 R-7 联调 — 你 W6 末 deadline 确认? 这两个必须在 M1 acceptance gate 前完成, 否则 live build 的 audit chain 没有密码学保障。
- **@老沈 (B):** W5-T8 17 reject 路径 integration fixture — 我 W6 初出 spec, 你 cpp 确认接单? M1-G8 acceptance gate 要验这些路径。
- **@小梁 (C):** STRATEGY_DECAYED 三签中你是 financial 签字人 — M2 解锁工具出来之前, 如果真触发了 Bayesian BLACK, 你能接受临时 CLI 工具签字还是需要更强的操作界面? 先对齐需求再设计工具。
- **@老郭 (F):** 我投 Smell-3 position WAL 应升 P0 — 你架构评审视角如何看? 是否需要进 ADR-010 (W5 偏离 follow-up)?
- **@老雷 (GM):** Smell-4 三签解锁操作路径 (SOP + CLI) — W6 出 spec 你能在 M2 前给 GM 签字路径确认吗? 我不替你承诺, 但我需要你对 SOP 框架有 ack 才能设计工具。

---

**B 主管签字:** 老韩 (E-009)
**投票日期:** 2026-06-01
**约束声明:** 我仅代表 B 风控合规部视角投票, 不替老周 / 小梁 / 小余 / 老胡 / 老郭 / 老雷投票。最终架构决策归 GM 拍板。
**cpp = 0:** 本文档纯文字 spec, 零 cpp 行 (ADR-005 §2.2 mandate 守住)。
