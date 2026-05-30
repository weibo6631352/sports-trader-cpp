# 2026-05-30 复盘评审 — 架构 / 数据流 / 流程图维度（老周）

- **owner:** 老周（系统工程部主管 + 架构主权）
- **last_review:** 2026-05-30
- **会议:** 2026-05-30 会话复盘（GM 写代码核心 + 全员辅助评审，§10.2 模式）
- **范围:** 本会话 11 commit（P0-2 单位契约 / P0-6 fee canonical / P0-1 exposure 红线接通 + 伦敦 EC2 移植）
- **立场:** 只读分析 + 写评审，不改代码。下面隐患的修复派 IC，本文只给架构裁决 + 边界。

---

## 0. 执行摘要（GM 先读这段）

**① 架构全景一句话**
paper 决策链路现已是「单 loop_thread 串行编排 + WSS io_thread 只读供料」的双线程闭环：`debug_api 契约库`定 ABI、`paper_app`编排 8 步 tick、`risk/sizing/signer/data` 是被编排的纯功能模块；本会话把最后一根断链（exposure 回喂 RM）接上，RiskManager 从「纸面有 cap 但零喂数」变成「真敞口咬 cap」。

**② 决策热路径数据流图** — 见 §2.1（含线程归属 + R-12 边界）。

**③ 最大架构隐患**
`PositionLedger` 存 **whole pUSD**，而全系统其余所有金额口径是 **micro pUSD**。这是一个**未声明的单位孤岛**。本会话两处补丁建立在它之上但方向相反：`FeedRiskGateway()` 把它 `×1e6` 当 whole（正确假设），`PublishLedgerSnapshot()` 把它 `÷1e6` 当 micro（错误假设）。**同一个字段在同一文件里被两个函数当成两种单位** —— PnL 快照因此被低估 1e6 倍。这不是新 bug，是本会话单位治理**没收口到 PositionLedger** 留下的活雷。

**④ 单位 pattern 根因 + 根治建议**
根因：**类型护栏止步于 RM/sizing 边界，没下沉到 PositionLedger 和 VirtualFill**。`MicroPUSD` 钉死了 RiskConfig 的 4 个 cap，但 `PositionLedger::size_usdc`、`condition_exposure_`、`VirtualFill::fill_size_usdc` 仍是裸 `int64`/`double` + 含糊的 `_usdc` 后缀。根治：走 **R-4（schema 变更红线）**，把 PositionLedger 内部存储 + VirtualFill 金额字段统一迁 `MicroPUSD`，删掉 `FeedRiskGateway` 的 `×1e6` 和 `PublishLedgerSnapshot` 的 `÷1e6` 这两个对消/对冲的人工 scale。详见 §4。

**⑤ 红线守法**
- **R-11（paper 不污染真账本）：守住。** `FeedRiskGateway` 只写 RM 内存 exposure map（paper 专用 RM 实例），不碰 position/pnl/nonce 真账本；mode_tag==0 + PaperAudit 链路不变。
- **R-12（event loop 禁阻塞）：守住，但新增了一条要盯的边。** `FeedRiskGateway` 在 loop_thread 内调用，且 `set_*_exposure` 持 `s_->mu` 短锁 —— 它在 paper loop_thread 上跑，**不在 WSS io_thread 上**，所以不触 R-12。但要立规矩：这条回喂边永远不许搬到 io_thread。详见 §5。

---

## 1. 当前系统架构全景（模块边界 + 数据流向）

```
                          sports-trader-cpp · paper 决策链路（本会话后）
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │  数据源层（跨洋）                                                                  │
  │   Polymarket gamma REST ──┐         Polymarket CLOB WSS ──┐    Goalserve inplay ──┐│
  └───────────────────────────┼──────────────────────────────┼───────────────────────┼┘
                              │ (启动期一次拉 token_map)       │ (持续 book 更新)        │ (比分/时钟)
                              ▼                               ▼                        ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │  data / 供料层  ── stcpp::data / polymarket::clob_wss                              │
  │   ┌──────────────────────┐   ┌────────────────────────────┐  ┌──────────────────┐│
  │   │ token_map (cond→tok) │   │ OrderBookSnapshotHub        │  │ ScoreSnapshotStore││
  │   │ (gamma → 启动期)      │   │ (WSS io_thread 写 / 原子)   │  │ (Goalserve → A1)  ││
  │   └──────────────────────┘   └────────────────────────────┘  └──────────────────┘│
  └────────────────────────────────────┬──────────────────────────────────────────────┘
                                        │  hub_.Read(token)  ← 原子只读, 无锁 (R-12)
                                        ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │  paper_app 编排层  ── stcpp::paper / stcpp::app   [loop_thread_, 单线程串行]       │
  │                                                                                   │
  │   PaperDaemon (Build/Run/Shutdown) → PaperLoop::RunLoop → TickAll → TickOne(8步)  │
  │                                                                                   │
  │   编排 8 步 ── 调下面的纯功能模块, 自己不含金融逻辑:                                │
  │    1.FairValue  2.de-vig  3.Sizing  4.QuotePublish                                 │
  │    [gate: advisory / has_real_fair / devig_ok]                                    │
  │    5.OrderIntent 6.RM.evaluate 7.PaperSigner+VirtualMatcher                        │
  │    8.PositionLedger.apply_fill → PublishLedgerSnapshot → FeedRiskGateway ★本会话   │
  └───┬──────────────┬───────────────┬──────────────┬──────────────┬──────────────────┘
      │              │               │              │              │
      ▼              ▼               ▼              ▼              ▼
  ┌────────┐   ┌──────────┐   ┌────────────┐  ┌──────────┐  ┌────────────────┐
  │pricing │   │ sizing   │   │ risk       │  │ signer   │  │ execution      │
  │FairVal │   │SizingCalc│   │RiskGateway │  │PaperSign │  │VirtualMatcher  │
  │de-vig  │   │(Kelly+cap│   │(10 reject) │  │(EIP-712  │  │(Bernoulli fill)│
  │        │   │ .to_pusd)│   │ MicroPUSD  │  │ paper)   │  │                │
  └────────┘   └──────────┘   └─────┬──────┘  └──────────┘  └───────┬────────┘
                                    │ ▲                              │
                  set_*_exposure ×1e6│ │ evaluate(intent)            │ VirtualFill
                                    │ │                              ▼
                              ┌─────┴─┴──────────────────────────────────────┐
                              │ risk::PositionLedger  ⚠ 存 whole pUSD 孤岛     │
                              │  size_usdc / condition_exposure_ = whole pUSD │
                              └───────────────────────────────────────────────┘
                                    │
                                    ▼ Publish (噪声/观测旁路, 不在决策回路)
                  ┌────────────────────────────────────────────────────┐
                  │ Snapshot Hubs (debug_api 旁路):                     │
                  │  QuoteSnapshotHub / LedgerSnapshotHub / RmDebugSnap │
                  │  → REST /api/v1/{quote,ledger,risk/rejects}         │
                  └────────────────────────────────────────────────────┘

  契约库边界（debug_api / domain）: OrderIntent v0.6 ABI / MicroPUSD / VirtualFill / QuoteFeatures
    —— 这层是「冻结契约」, 跨模块 include, 改它走单一 owner 串行 (§10.1 G-FREEZE-W)。
```

**模块边界裁决（架构主权）：**

| 层 | 库 | 职责 | 不许做 |
|---|---|---|---|
| **契约库** | `domain/` + `risk/*.hpp`（OrderIntent/MicroPUSD/VirtualFill/Quote/Ledger Features） | 定 ABI + 单位类型 | 含业务逻辑 |
| **编排层** | `paper_app`（PaperDaemon/PaperLoop） | 串 8 步 tick + gate + 线程归属 | 含金融算法（fair/kelly/cap 数值都在功能模块） |
| **功能模块** | pricing / sizing / risk / signer / execution | 纯函数式被调 | 自己起线程 / 跨模块直连 |
| **供料层** | data / clob_wss / Goalserve | 原子只读快照 | 阻塞 loop |
| **观测旁路** | Snapshot Hubs + REST | 单向 Publish 出 | 反向喂决策回路 |

这个分层是健康的。本会话没破坏它。唯一的越界风险点是 PositionLedger 的单位孤岛（§3/§4）。

---

## 2. 关键数据流图（流程图）

### 2.1 决策热路径（含线程归属 + R-12 边界）

```
  ════════════ WSS io_thread（异步, R-12 治下: 禁同步 REST / 阻塞 IO / 锁>100us）════════════
     Polymarket CLOB WSS frame
            │
            ▼
     OrderBookSnapshotHub.Publish(token, features)   ← 原子写 (noexcept, 无阻塞)
            │  [跨线程交接点 = 原子快照, 唯一合法的 io→loop 边界]
  ┄┄┄┄┄┄┄┄┄┄│┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄
  ════════════ paper loop_thread（jthread, 串行, R-12 不治但禁污染 io_thread）════════════
            │
            ▼
   [1] hub_.Read(token0)  ──原子只读, 无锁──► OrderBookFeatures (best_bid/ask/microprice + 4ts)
            │  hub_.Read(token1) 取对边 → no_token_mid (de-vig 用)
            ▼
   [2] FairValueEstimator.estimate(game_row, book_row)
            │   game_row 默认 stub(NotStarted); 若 ScoreSnapshotStore.Get 命中且 fresh
            │   → time_status=InPlay, score/clock 填入 → has_real_fair=true
            ▼   de-vig: devig_binary(yes_mid, no_token_mid) → p_market_devig (devig_ok)
        p_fair = has_real_fair ? blend(prior, devig, conf) : p_market_devig
            ▼
   [3] SizingCalculator.compute(sizing_cfg, sz_in)   ← cap 用 .to_pusd() 降量纲
            │   Kelly f* → cap 链(5级取min) → suggested_notional (whole pUSD)
            ▼
   [4] QuoteSnapshotHub.Publish(quote)  ──► 观测旁路 (REST /api/v1/quote)
            │
            ▼   ┌─ gate A: advisory=true        → return (不产 intent)  [P0-4]
        ◇ gates├─ gate B: !has_real_fair        → return (stub fair)   [P0-3]
            │   ├─ gate C: !devig_ok             → return (无市场锚)
            │   └─ gate D: notional<=0 / !valid  → return (CI fail-closed)
            ▼ (全过)
   [5] 构造 OrderIntent v0.6   ← size_pUSD_micro = notional × 1e6 (whole→micro)
            │                     book_depth_l1_usdc × 1e6 (whole→micro)
            │                     4ts 链校验 (event≤ds≤ingest≤as_of)
            ▼
   [6] RiskGateway.evaluate(intent)   ◄────── exposure map (micro) ──┐
            │   10 reject short-circuit (ADR-004 顺序)               │
            │   caps: size vs per_order(micro) / cur+size vs cond(micro)
            ├─ REJECTED → push_reject(ring) [RM 唯一负责, P0-1 去重] → return
            ▼ APPROVED
   [7] PaperSigner.Sign(req) → SignResponse (EIP-712 paper, audit_wal=PaperAudit) [R-11]
            │
            ▼
       VirtualMatcher.Match(vord) → VirtualFill (mode_tag==0 [R-11], Bernoulli fill)
            │   fill.fill_size_usdc = whole pUSD
            ▼
   [8] PositionLedger.apply_fill(cond, tok, Yes, fill)
            │   存 size_usdc = (int64)fill_size_usdc  ⚠ whole pUSD (无 ×1e6)
            ├──► PublishLedgerSnapshot  ──► LedgerSnapshotHub (REST /api/v1/ledger)
            │       ⚠ net_qty = size_usdc / 1e6  ← 错把 whole 当 micro → PnL ÷1e6
            │
            └──► ★ FeedRiskGateway()  [本会话新增闭环]
                    per_condition_exposure (whole) ×1e6 → set_condition_exposure(micro)
                    per_outcome_exposure   (whole) ×1e6 → set_outcome_exposure(micro)
                         └──────────────► 回喂 [6] 的 RM exposure map ──┘ (下一 tick 咬 cap)
```

**线程归属裁决：**
- **io_thread 只做一件事**：把 WSS frame 落成 `OrderBookSnapshotHub` 的原子快照。这是 R-12 治下区域，绝不允许在此做 RM/sizing/fill 任何决策动作。
- **loop_thread 串行做全部 8 步**。`FeedRiskGateway` 在 loop_thread 内、`apply_fill` 之后调用 —— **单写者**，所以「全量覆盖喂 RM」自洽（PL 增量维护 → RM 全量 = PL 真值，自愈）。这点设计正确。
- **唯一跨线程边界 = `hub_.Read()` 的原子快照**。本会话没有新增第二条跨线程边界，干净。

### 2.2 单位流（pUSD ↔ micro 全景）

```
  pUSD (whole, double)                          micro pUSD (int64)
  ───────────────────                           ──────────────────
  PaperLoopConfig.*_cap_usdc ──from_pusd()────► RiskConfig.*cap (MicroPUSD)  [c2/c3, paper_daemon]
  cfg.bankroll_usdc          ──×1e6──────────► RM.set_bankroll(micro)        [paper_loop:142]
                                                       │
  SizingCalculator:                                    │
   cfg.cap.to_pusd() ◄──────────────────────────── RiskConfig cap (MicroPUSD)  [sizing:179/191/202]
   notional_kelly (whole) ── 比 cap.to_pusd() (whole) ─ 同量纲 ✓
   suggested_notional (whole)
        │
        ▼ ×1e6
  OrderIntent.size_pUSD_micro (micro int64) ──────► RM check_position_caps_:
        │                                            from_micro(size) vs cap (MicroPUSD) ✓
        │                                            from_micro(cur+size) vs cond cap ✓
        ▼
  VirtualFill.fill_size_usdc (whole double)  ⚠ 名 _usdc 实 whole, 非 micro
        │
        ▼ (int64)cast, 无 scale
  PositionLedger.size_usdc (whole int64)     ⚠⚠ 单位孤岛: 存 whole, 字段名 _usdc 误导
  PositionLedger.condition_exposure_ (whole) ⚠⚠
        │                                  │
        │ ×1e6 (FeedRiskGateway, 正确)      │ ÷1e6 (PublishLedgerSnapshot, 错误)
        ▼                                  ▼
  RM exposure map (micro) ✓          LedgerSnapshot.net_qty (PnL ÷1e6 ✗)
```

**本会话单位治理的成绩**：cap 这条线（PaperLoopConfig → RiskConfig → sizing/RM）已经**全量类型化 + 同源 + CI grep 守护**，这是真进步。**漏的是 fill→ledger→回喂这条线**，它还在裸 double/int64 + 人工 scale 上漂。

---

## 3. 架构健康度复盘

### 3.1 更稳了（本会话的真收益）

| 维度 | 改动 | 为什么稳 |
|---|---|---|
| **类型护栏** | RiskConfig 4 cap + bankroll/DD → `MicroPUSD` 强类型 | 编译期钉死 micro 语义；`MicroPUSD×MicroPUSD` 不存在 → pUSD² 误用直接编译错。挡住了「字段名带 micro 但裸 int64」这类静默漏乘。 |
| **同源真值** | sizing/RM/paper_daemon cap 全走单一 `RiskConfig` 字段，sizing `.to_pusd()`、RM `from_micro` 同源比 | 终结 c2 过渡态的「whole 灌 micro 字段 + 双错对消」。cap 不再有第二真值。 |
| **CI 守护** | `unit_contract_check.py` grep 守 sizing/RM 禁裸 `.v`、禁对 cap `×1e6`，白名单 paper_loop/paper_daemon 边界转换 | 类型系统挡主路径，grep 挡「读 `.v` 当 whole 比」的旁门。双层。符合 §8.1 红线治理「ABI/单位变更必触发下游审计」。 |
| **红线接通** | exposure 回喂闭环（P0-1） | RM 的 per_condition/per_outcome cap 从「纸面齐全零喂数」变「真敞口咬」。风控从纸面化变实战化。 |
| **gate 前移** | advisory / has_real_fair / devig_ok 在 paper_loop 主动 gate，不再依赖 RM 兜底（P0-3/P0-4） | 「宁可空不可假」。RM 不再做 advisory 防线，职责清晰。 |

### 3.2 仍是隐患（按严重度排序）

**H1（高）— PositionLedger 单位孤岛 + PnL ÷1e6 bug（确认）**
`PositionLedger::apply_fill`（`position_ledger.cpp:54`）`static_cast<int64>(fill.fill_size_usdc)` 存的是 **whole pUSD**（fill_size_usdc 是 whole double）。于是：
- `FeedRiskGateway`（paper_loop.cpp:716/720）`whole × 1e6` → micro，**假设正确**。
- `PublishLedgerSnapshot`（paper_loop.cpp:659）`net_qty = size_usdc / 1e6`，**假设它是 micro → 错**。net_qty 被低估 1e6，`pnl_unrealized = (mark - avg) × net_qty` 跟着 ÷1e6。
**同一字段在同一文件被当两种单位**，这正是 §8.1 红线治理纪律警告的「语义在文档/代码间漂移没人守边界」的活样本。PnL 现在只进观测旁路（REST 展示），不进决策，所以**不是 P0**；但它是 daily_pnl/DD 熔断未来接入的拦路雷 —— FeedRiskGateway 注释自己也承认「daily_pnl 撞 PublishLedgerSnapshot 预存 PnL 单位 bug，待修」。

**H2（中）— 双轨存储：PositionLedger 存 whole，RM 存 micro**
两套真值靠 `FeedRiskGateway` 的 `×1e6` 人工桥接。桥一旦漏（重构/换调用点/加第二消费者）→ exposure 红线静默架空，和 P0-2 cap bug 同型复发。当前靠一条注释 + 一个单测（test_paper_loop P0-1 单位门）守，**护栏强度低于类型系统**。

**H3（中）— sizing exposure 输入恒喂 0**
`sz_in.current_token_exposure_usdc = 0.0` / `current_condition_exposure_usdc = 0.0`（paper_loop.cpp:409-410）写死 0。意味着 sizing 的 Cap2/Cap3（per_outcome/condition headroom）**永远用满额 headroom**，从不随已建仓收缩。RM 侧 exposure 已通过 FeedRiskGateway 接通会兜底拒，但 **sizing 和 RM 在 exposure 维度不再同源** —— sizing 会持续建议越累积 cap 的 size，靠 RM 拒，产生「建议-拒绝」空转。这是 P0-1 只做了 step1（喂 RM）没做 step2（喂 sizing）的债。

**H4（低）— VirtualFill.fill_size_usdc 命名/单位含糊**
契约层字段叫 `_usdc` 实为 whole pUSD double，是孤岛的上游源头。命名债，但因为它是冻结契约（execution），改它要走 §10.1 单一 owner 串行。

---

## 4. 反复出现的单位失配 pattern — 根因 + 根治

### 4.1 本会话至少 3 次撞单位

| # | 现场 | 表现 |
|---|---|---|
| 1 | c2–c5 caps | RiskConfig cap 字段名带 `_usdc` 实为 micro，sizing 当 whole 比、RM 当 micro 比，差 1e6 → 恒拒 |
| 2 | P0-1 exposure | PositionLedger 存 whole，RM 比 micro → 必须 `×1e6` 桥接，漏乘则红线架空 |
| 3 | PnL bug | size_usdc(whole) 被 `÷1e6` 当 micro → PnL ÷1e6（H1） |

### 4.2 架构根因（一句话）

**金额在系统里有三种物理单位（whole pUSD double / micro pUSD int64 / 名义 `_usdc` 含糊后缀），而类型护栏只覆盖了其中一段（RiskConfig cap），剩下的边界全靠人工 `×1e6` / `÷1e6` 和注释守。** 每多一个跨这三种单位的边界点，就多一次漏乘风险。本会话修的是「症状现场」（cap 这段类型化了），没拔「病灶」（whole 和 micro 两种表示在 fill/ledger 段共存）。

### 4.3 根治建议（架构主权裁决）

**走 R-4（schema 静默变更红线 = ABI/字段单位变更必审下游）**，分两步，派系统工程部 IC（小肖 owner，老韩 RM 契约 cosign，老郭 ADR）：

**根治 step A — PositionLedger 内部统一 micro：**
- `PositionLedger::size_usdc` / `condition_exposure_` 改存 **micro**（迁 `MicroPUSD` 或至少明确 micro int64 + 重命名 `*_micro`）。
- `apply_fill` 入口 `fill.fill_size_usdc(whole) → from_pusd() → micro` 一次转换，之后内部全 micro。
- 删 `FeedRiskGateway` 的 `× kMicroPerPusd`（两处）→ 直接喂 micro，**和 RM 同单位、零 scale**。
- 修 `PublishLedgerSnapshot` 的 `÷1e6` → 改成从 micro `.to_pusd()` 出 whole 给展示（H1 PnL bug 顺带根治）。

**根治 step B — VirtualFill 金额字段类型化（中期，走冻结契约 owner 串行）：**
- `VirtualFill::fill_size_usdc` → 评估迁 `MicroPUSD` 或重命名澄清单位。这是 §10.1 G-FREEZE 文件，**禁并行改**，排队走单一 owner。

**根治 step C — CI grep 扩域：**
- `unit_contract_check.py` 当前只守 sizing/RM/paper_loop/paper_daemon 四文件。step A 后把 `position_ledger.cpp` 纳入 ENFORCED_FILES，禁裸金额 `×1e6`/`÷1e6`（边界转换只许在 from_pusd/to_pusd）。

**验收口径（数字说话）：** 根治后全链路金额跨边界的人工 `×1e6`/`÷1e6` 计数应从当前 5 处（paper_loop bankroll/depth/size/notional + FeedRiskGateway×2 + Publish÷1）降到 **仅 OrderIntent 构造点 1 处**（whole→size_pUSD_micro 这是 ABI 契约要求的，留），其余全部由 `MicroPUSD.from_pusd/to_pusd` 封装。

**H3（sizing 喂 0）单独派**：把 PositionLedger 的 per_outcome/condition exposure（root step A 后是 micro）`.to_pusd()` 喂回 `sz_in.current_*_exposure_usdc`，让 sizing 和 RM 在 exposure 维度同源。消除「建议-拒绝」空转。

---

## 5. R-11 / R-12 红线评审（本会话新增 FeedRiskGateway）

**原文（§8 红线，粘原文不转述，遵 §8.1 纪律 #1）：**
> - Paper mode 污染真账本（写入 position / pnl_ledger / nonce_ledger） → P0（ADR R-11）
> - WebSocket event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0（ADR R-12）

### R-11 — 守住 ✓
`FeedRiskGateway` 只调 `rm_.set_condition_exposure` / `set_outcome_exposure`，写的是 **paper 专用 RM 实例的内存 exposure map**（unordered_map），不写 position_ledger 真账本文件、不写 pnl_ledger、不碰 nonce。链路其余 R-11 锚点不变：`VirtualFill.mode_tag==0`（assert 在 paper_loop.cpp:574）、`PaperSigner.audit_wal_kind=PaperAudit`。**无污染面。**

### R-12 — 守住，但立一条永久边界规矩 ✓⚠
- **关键事实：FeedRiskGateway 跑在 loop_thread，不在 WSS io_thread。** R-12 治的是 WSS event loop。loop_thread 不在 R-12 治下，所以 `set_*_exposure` 持的 `s_->mu` 短锁不触红线。
- **但**：`set_*_exposure` 内部 `std::lock_guard<std::mutex>`，且 FeedRiskGateway 在每次 fill 后全量遍历 per_condition/per_outcome map。当前 token 量级（336）下锁持有极短，无忧。**架构规矩（写入 ADR）：这条 exposure 回喂边永远绑定 loop_thread，绝不允许任何重构把它搬到 io_thread / WSS 回调里** —— 一旦搬，`s_->mu` 就成了 WSS event loop 上的锁，直接撞 R-12「锁>100us」P0。
- **附带建议**：全量遍历喂 RM 是 O(positions) per fill。MVP 量级无所谓，但 outright 全盘口铺开后 positions 上千时，建议改增量喂（只喂本次 fill 影响的 cond/token）。非本会话债，记 backlog。

---

## 6. 给 GM 的派单建议（架构边界，不替主管派）

1. **H1 PnL bug + H2 双轨**：合并成「PositionLedger micro 化」一个 task（根治 §4.3 step A + C），派系统工程部，小肖 owner / 老韩 RM 契约 cosign / 老郭 ADR（R-4）。这是把本会话单位治理**收口**，优先级最高。
2. **H3 sizing 喂 0**：step A 落地后顺手做，同一 owner。
3. **H4 VirtualFill 字段**：中期，走 §10.1 冻结契约单一 owner 串行，**不与 H1 并行改同文件**。
4. **R-12 边界规矩 + 增量喂 backlog**：入 ADR，老郭归档。

**架构主权结论：** 本会话方向正确（类型护栏 + 红线接通是对的），但单位治理**做了一半就接了 exposure 红线**，导致 FeedRiskGateway/PublishLedgerSnapshot 建在未收口的 whole/micro 双轨上，自相矛盾（一个 ×1e6 一个 ÷1e6）。**下一步必须先收口 PositionLedger 单位，再谈 daily_pnl/DD 接入。** 否则每接一个 PnL 消费者，就复刻一次 P0-2。

— 老周，2026-05-30
