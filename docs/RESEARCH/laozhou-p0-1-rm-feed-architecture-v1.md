# P0-1 RM 喂数架构 spec — 数据流 + 线程模型

> owner: 老周 (系统工程部主管 + 架构主权)
> last_review: 2026-05-30
> 配套: 老韩 RM 侧喂数契约 (RM 需要什么) / 本文出数据从哪来、怎么流、线程安全、R-12/R-11 不破
> 范围: 只读分析 + 架构 spec, 不改代码。落地派 GM/IC。
> 红线 cite (粘原文, §8.1 纪律):
>   - R-12 (CLAUDE.md §8): "WebSocket event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0"
>   - R-11 (CLAUDE.md §8): "Paper mode 污染真账本 (写入 position / pnl_ledger / nonce_ledger) → P0"
>   - R-20 (CLAUDE.md §8): "event_ts ≤ data_source_ts ≤ ingestion_ts ≤ as_of_ts ... 禁本地 now() 替代上游 ts"

---

## 0. 问题陈述 (一句话)

RM (RiskGateway) 的 DD / consec / exposure 安全网逻辑齐全 (`check_position_caps_` 全实现),
但生产**从不调用** `set_daily_pnl` / `set_condition_exposure` / `set_outcome_exposure` / `set_consec_loss`
→ 这四个量在生产恒为初值 (pnl=0 / exposure 空 map / consec=0) → DD/consec/exposure 三道风控红线**纸面化**。
唯一被喂的是 `set_bankroll` (paper_loop.cpp:142 Start 时灌一次)。

**本 spec 的任务**: 把 PaperLoop 已经算出来的 fill / 持仓 / pnl 接到 RM 的这四个 setter 上, 不破 R-12/R-11/R-20。

---

## 1. 三个量的真实数据源 (文件:结构)

关键结论: **三个量的真实数据源已经全部存在于 PaperLoop 内部, 无需新建账本。** 缺的只是「算完 → 喂 RM」这一跳。

### 1.1 exposure (per-condition + per-outcome 名义敞口, micro)

- **真实源**: `risk::PositionLedger` (`src/stcpp/risk/position_ledger.cpp`)
- **现成聚合 API** (无需新写聚合逻辑):
  - `get_per_condition_exposure()` → `unordered_map<condition_id, int64 net_size>` (position_ledger.cpp:136)
    - 内部即 `condition_exposure_` map, `apply_fill` 时 `condition_exposure_[cid] += delta_usdc` 增量维护 (position_ledger.cpp:102)
  - `get_per_outcome_exposure()` → `unordered_map<token_id, int64 net_size>` (position_ledger.cpp:126)
- **单位真相 (关键, §8.1 单位审计)**: `PositionView.size_usdc` 是 **signed micro pUSD**。
  - 来源链: `apply_fill` 把 `VirtualFill.fill_size_usdc` (double, **whole pUSD**) `static_cast<int64>` 存进 `delta_raw` (position_ledger.cpp:54)。
  - **⚠ 单位炸点**: `fill_size_usdc` 是 whole pUSD (paper_loop.cpp:560 `vord.size_usdc = size_pUSD_micro / 1e6`),
    但 RM 的 `condition_exposure_usdc` 比的是 **micro** (risk_gateway.cpp:452 `from_micro(cur + size_pUSD_micro)`)。
    PositionLedger 存的 `size_usdc` 实际是 whole 截断 (因 apply_fill 直接 cast whole double)。
    **这与 RM 期望的 micro 差 1e6 倍** → 见 §4.3「单位归一化」。这是最大落地陷阱。

### 1.2 daily_pnl (当日盈亏, micro 或 whole 待定)

- **真实源**: 也在 `PositionLedger` 的持仓快照 + mark price (PaperLoop 每 fill 算 pnl)。
- **现成数据**: PaperLoop `PublishLedgerSnapshot` 已算 `pnl_realized` / `pnl_unrealized` / `pnl_gross` / `pnl_fee` (paper_loop.cpp:662-669), 发布进 `LedgerSnapshotHub` 的 `LedgerFeatures`。
- **M1 现状**: 买入阶段 `pnl_realized = 0` (paper_loop.cpp:666 注释: realized 在平仓产生)。M1 只买不平 → daily_pnl 主要是 unrealized + fee。
- **聚合方式**: daily_pnl = Σ(per-condition LedgerFeatures.pnl_net()) over all keys。
  `LedgerFeatures.pnl_net() = pnl_gross - pnl_fee` (ledger_snapshot_hub.hpp:120)。
  → 但 `LedgerSnapshotHub` 无「遍历全 key 求和」API (只有 per-key `Read`)。需要喂数侧自己持有 key 列表 (= token_map_ 的 condition 集) 逐 key Read 求和, 或在 PaperLoop 内维护一个 running pnl 累加器。**推荐后者** (见 §4.2)。

### 1.3 consec_loss (连续亏损笔数)

- **真实源**: fill 结果序列 (paper_loop.cpp:571 `matcher_.Match(vord)` 返回的 `VirtualFill`)。
- **M1 问题**: consec_loss 语义是「连续亏损**已了结**笔数」。M1 只买不平 → 单笔 fill 当下无 realized PnL → **M1 阶段 consec 恒 0 是正确的**, 不是 bug。
- **正确定义**: consec_loss 应在**平仓了结**时判定该笔 round-trip 的 realized PnL 正负, 连续负则 +1, 一笔正则清零。M1 无平仓 → 无了结 → consec 留 0。
- **落地策略**: M1 consec 喂数**留 stub** (恒喂 0, 显式注释「M1 只买不平, consec 待 M2 平仓回路」), 不强行用 unrealized 浮亏当 consec (那会错杀)。M2 接平仓 (DRAIN / is_close) 时再补真 consec 累加器。

---

## 2. 数据流路径 (源 → 聚合 → RM setter; 谁调, 在哪)

### 2.1 现状数据流 (已存在, 截止到 LedgerSnapshotHub)

```
WSS io_thread ──Publish──> OrderBookSnapshotHub ──Read──┐
                                                         │ (PaperLoop loop_thread_, 单线程)
                          ┌──────────────────────────────┘
                          ▼
   TickOne: FairValue → Sizing → RM.evaluate ─APPROVED─> PaperSigner.Sign
                          │                                      │
                          │                                      ▼
                          │                            VirtualMatcher.Match → VirtualFill
                          │                                      │
                          │                          ┌───────────┴───────────┐
                          │                          ▼                       ▼
                          │            PositionLedger.apply_fill   PublishLedgerSnapshot
                          │            (R-1 唯一写入路径)          → LedgerSnapshotHub.Publish
                          │                                              (debug_api 只读消费)
                          └── [缺] RM 的 set_*exposure/pnl/consec 从未被调用 ◄── 本 spec 补这一跳
```

### 2.2 补全后数据流 (新增「喂 RM」一跳)

**插入点: `TickOne` 内 Step 8b 之后** (paper_loop.cpp:588 `PublishLedgerSnapshot` 之后),
新增 **Step 8c: FeedRiskGateway()**, 在**同一 loop_thread_、同一 tick、fill 落账之后立即喂 RM**:

```
PositionLedger.apply_fill (Step 8)
        │
        ▼
PublishLedgerSnapshot (Step 8b)   ← LedgerSnapshotHub (debug_api 消费, 不变)
        │
        ▼
FeedRiskGateway (Step 8c, 新增)    ← 本 spec
   ├─ exposure: position_ledger_.get_per_condition_exposure() → 遍历 → rm_.set_condition_exposure(cid, micro)
   │            position_ledger_.get_per_outcome_exposure()   → 遍历 → rm_.set_outcome_exposure(tok, micro)
   ├─ daily_pnl: 累加器 daily_pnl_micro_ → rm_.set_daily_pnl(micro)
   └─ consec:    M1 stub (恒 0); M2 接平仓累加器
```

**谁调 setter**: PaperLoop 的 `loop_thread_` (= 唯一 evaluate 调用方, 也是 PositionLedger 唯一 writer)。
**在哪调**: `TickOne` 尾部, fill 成功落账之后。不开独立喂数线程 (见 §3 理由)。

### 2.3 为什么不在 fill 回调 / 不开独立喂数线程

- **不开独立线程**: exposure/pnl 的写者 (PaperLoop loop_thread_) 与 RM evaluate 的调用者是**同一个线程**。开独立喂数线程反而引入跨线程同步 + setter 与 evaluate 的读写竞态。同线程串行喂 = 零竞态、零额外锁开销。
- **不在 fill 回调里**: 当前架构 fill 是 TickOne 内同步返回 (VirtualMatcher.Match 同步), 没有异步回调机制。直接在 TickOne 尾部喂即「fill 回调」语义。

---

## 3. 线程模型 (R-12 红线: 喂数绝不阻塞 WSS event loop)

### 3.1 三条线程的关系

| 线程 | 角色 | 与喂数的关系 |
|---|---|---|
| WSS `io_thread_` | 接 CLOB book → `OrderBookSnapshotHub.Publish` (release store) | **完全不碰喂数**。喂数发生在 loop_thread_, 物理隔离。 |
| PaperLoop `loop_thread_` (jthread) | TickAll → TickOne → evaluate → fill → **喂 RM** | 喂数全在此线程, 单线程串行。 |
| debug_api 观测线程 | `LedgerSnapshotHub.Read` (acquire load) 只读 | 只读快照, 不碰 RM setter。 |

**R-12 保证 (粘原文: "WSS event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0")**:
- 喂数发生在 `loop_thread_`, **不在 WSS io_thread_**。io_thread_ 与 RM setter 之间零调用边。
- loop_thread_ 读 book 走 `hub_.Read()` (原子 acquire + value copy, < 1us, paper_loop.cpp:212), 不阻塞 io_thread_ 的 Publish (各写各的 double-buffer)。
- **结论: 喂数对 WSS event loop 零影响, R-12 天然满足** (喂数根本不在那条线程上)。

### 3.2 RM setter 的并发安全

RM 内部已自带并发安全, 喂数侧无需额外加锁:
- `set_daily_pnl` / `set_consec_loss` / `set_bankroll`: **atomic store** (risk_gateway.hpp:355-357), 无锁。
- `set_condition_exposure` / `set_outcome_exposure`: 走 `s_->mu` mutex (risk_gateway.cpp:166-175), **与 `check_position_caps_` 读 exposure 同一把锁** (risk_gateway.cpp:437)。

**锁持有时长审计 (R-12 "锁 > 100us")**:
- setter 临界区 = 一次 `unordered_map::operator[]` 赋值, < 1us。
- `check_position_caps_` 临界区 = 几次 map find + 整数比较, < 1us。
- 二者都在 **loop_thread_ 单线程内串行调用** (evaluate 在 TickOne 中段, 喂 RM 在 TickOne 尾部, 不重叠)。**实际无跨线程锁争用**。
- 唯一的潜在跨线程读者: debug_api 若读 RM 内部 exposure (当前 debug_api 走 LedgerSnapshotHub 不直读 RM s_->mu, 故无争用)。
- **结论: s_->mu 持有时长 << 100us, 且这把锁不在 WSS io_thread_ 上, R-12 满足。**

### 3.3 喂数侧绝不持锁久 (架构纪律)

喂数侧在调 setter **之前**完成所有聚合 (get_per_condition_exposure 返回的是 snapshot copy, PositionLedger 内部 shared_lock 已释放, position_ledger.cpp:108)。
即: **聚合 (持 PositionLedger 锁) 与喂 RM (持 RM 锁) 两段锁不嵌套**。先取快照 (PL 锁释放) → 再逐 key 喂 RM (RM 锁)。无锁嵌套 = 无死锁可能。

---

## 4. 增量 vs 全量 + 一致性

### 4.1 exposure: 全量快照覆盖 (推荐) > 增量

**决策: 用全量快照覆盖, 不用增量。** 理由:
- PositionLedger 本身已用增量维护 (`condition_exposure_[cid] += delta`)。喂 RM 时**再做一层增量 = 双重增量, 易漂移**。
- `get_per_condition_exposure()` 返回的是**当前真值全量 map** (PL 内部增量的结果)。喂数侧拿全量逐 key `set_condition_exposure(cid, val)` **覆盖** RM 侧的值 → RM exposure 永远 = PL 真值, 不会因丢一笔喂数而永久漂移 (自愈)。
- setter 语义本就是 `map[key] = v` 覆盖 (risk_gateway.cpp:168), 天然适配全量覆盖。
- 全量代价: 每 fill 后遍历 O(持仓 key 数)。M1 持仓 key 数 = 活跃 condition 数 (几十~几百), 每 tick 至多 1 fill, 可接受。**若未来 key 数大 (>512)**, 优化为「只喂本次 fill 影响的 cid + token」增量 (见 §4.4 live 替换点)。

**一致性**: 全量覆盖 = RM exposure 是 PL 的「最终一致」镜像, 滞后至多一个 tick。因喂数与 evaluate 同线程串行, **下一个 tick 的 evaluate 一定看到上一个 fill 喂进的 exposure** → 对 cap 红线是 fail-safe 的 (敞口先入账再 evaluate 下一单)。

### 4.2 daily_pnl: running 累加器 (loop_thread_ 私有) → 全量 set

- 在 PaperLoop 内新增 `double daily_pnl_pusd_` (loop_thread_ 私有, 单线程无需 atomic)。
- 每 fill 后, 重新从 `position_ledger_.get_all_positions()` + 当前 mark 算总 pnl (M1 只买不平, 主要是 fee + unrealized), `set_daily_pnl(total_micro)` 全量覆盖。
- **或** 复用 PublishLedgerSnapshot 已算的 pnl 累加 (避免重复算)。推荐: 在 PublishLedgerSnapshot 内顺手累加全局 pnl 后喂。

### 4.3 单位归一化 (落地最大陷阱, 必须显式处理)

- RM 期望: `set_condition_exposure` 的 value 是 **micro pUSD** (因 check 里 `from_micro(cur + size_pUSD_micro)`)。
- PositionLedger 给的: `get_per_condition_exposure()` value 来自 `apply_fill` 的 `static_cast<int64>(fill_size_usdc)` = **whole pUSD 截断** (position_ledger.cpp:54)。
- **失配 1e6 倍。** 喂数侧必须 `set_condition_exposure(cid, val_whole * 1'000'000)` 转 micro。
- **架构风险标注**: 这与 §8.1「`size` 改 micro 后 caps/bankroll/book_depth 单位失配把 caps 红线静默架空」是同一类炸点。若喂数侧漏转 micro → RM 看到的 exposure 比真值小 1e6 倍 → **per_condition cap 永不触发 = exposure 红线静默架空**。**落地必须有单测断言**: fill N 笔后, `rm` 内部 condition_exposure == Σ(size_pUSD_micro)。建议彻底解法: PositionLedger 改存 micro (与 size_pUSD_micro 同单位), 一次性消歧 (走 R-4 schema 变更流程, 派老韩 review)。

### 4.4 daily_pnl 日界 reset

- 谁触发: **不在 loop_thread_ 自己判日界** (会引入 wall-clock 判断散落热路径)。由 **PaperDaemon app 层**持一个日界 watcher (非热路径线程), 跨日时调 `paper_rm_->set_daily_pnl(0)` + 通知 PaperLoop 重置累加器。
- M1 简化: daemon 单次 session 不跨日, **日界 reset 留 app 层 hook stub**, M1 可不实装 (paper session 短)。spec 标清: 日界 reset owner = app 层, 不是 RM, 不是 loop_thread_ 热路径。

---

## 5. R-11 隔离确认 (paper 喂数走 paper 栈, 绝不碰真账本)

**粘原文 (R-11)**: "Paper mode 污染真账本 (写入 position / pnl_ledger / nonce_ledger) → P0"

确认喂数全程在 paper 隔离栈 (paper_daemon.cpp:185-227 已物理隔离):
- `paper_rm_` = 独立 RiskGateway 实例 (paper_daemon.cpp:215), NullAuditEmitter 不落真 WAL。
- `paper_position_ledger_` = 独立 PositionLedger 实例 (paper_daemon.cpp:190, 注释 [R-11] 物理隔离)。
- `ledger_hub_` = 独立 LedgerSnapshotHub。
- `apply_fill` 运行期硬 gate: `fill.mode_tag != 0 → return` (position_ledger.cpp:38, 注释「R-11 运行期实际守卫」)。
- 喂数源 = `paper_position_ledger_` (paper 隔离), 喂数目标 = `paper_rm_` (paper 隔离)。
- **结论: 喂数链路完全在 paper 隔离栈内, 不触及任何 live position/pnl_ledger/nonce_ledger。R-11 满足。**
- **落地纪律**: FeedRiskGateway 只能持 PaperLoop 已注入的 `rm_` 引用 (= paper_rm_) 与 `position_ledger_` 引用 (= paper_position_ledger_)。**禁止**喂数代码内新建任何 RM / Ledger 句柄或写 WAL。

---

## 6. paper 先行 + live 对称 (M5+ 替换点)

喂数架构 **paper / live 对称**, live 时只换数据源, 数据流 + 线程模型 + setter 接口不变:

| 维度 | paper (M1, 本 spec) | live (M5+) | 替换点 |
|---|---|---|---|
| exposure 源 | `paper_position_ledger_` (VirtualFill 驱动) | live PositionLedger (真实 CLOB fill / data API 回报驱动) | PositionLedger 实例换, `get_per_*_exposure()` 接口不变 |
| daily_pnl 源 | unrealized + fee (只买不平) | 真实 realized (含平仓) + mark-to-market | pnl 累加器换真实结算源 |
| consec 源 | M1 stub (恒 0) | 真实平仓 round-trip 盈亏序列 | 接 DRAIN/is_close 平仓回路累加器 |
| 喂数线程 | loop_thread_ | live 决策线程 (同构: 单 writer 喂 RM) | **不变** (对称) |
| RM setter | 同一套 set_* | 同一套 set_* | **不变** (接口对称) |
| 单位 | micro pUSD | micro pUSD | **不变** (§4.3 归一后) |
| 隔离 | R-11 paper 栈 | live 栈 (RM 是真防线) | 实例换, 隔离纪律对称 |

**核心对称点**: 「fill 落账 → 全量聚合 exposure/pnl → set RM」这套**逻辑 paper/live 完全一致**, 只是数据源实例不同。
建议落地时把 `FeedRiskGateway()` 抽成**不依赖 paper 具体类型的纯逻辑** (入参: PositionLedger& + RiskGateway& + pnl), live 直接复用。

---

## 7. 执行摘要 (老周拍板)

### ① 三个量真实数据源 (文件:结构)
- **exposure**: `risk::PositionLedger` (position_ledger.cpp), 现成 `get_per_condition_exposure()` / `get_per_outcome_exposure()` 返回全量 micro(需 ×1e6 归一) map。无需新建账本。
- **daily_pnl**: PaperLoop `PublishLedgerSnapshot` 已算 (pnl_gross/fee/unrealized, paper_loop.cpp:662-669) → 新增 loop_thread_ 私有累加器全量喂。
- **consec_loss**: fill 序列, 但 **M1 只买不平 → 恒 0 是正确的**, 留 stub, M2 接平仓回路。

### ② 数据流路径 (谁调 setter, 在哪)
PaperLoop `loop_thread_` 在 `TickOne` 尾部 (Step 8b PublishLedgerSnapshot 之后) 新增 **Step 8c FeedRiskGateway()**: 取 PositionLedger 全量快照 → 逐 key `set_condition_exposure`/`set_outcome_exposure` (覆盖) + 累加器 `set_daily_pnl`。**同线程串行, 不开独立喂数线程, 不在 fill 异步回调**。

### ③ 线程模型 (R-12 不阻塞怎么保证)
喂数全在 **loop_thread_, 物理不在 WSS io_thread_** → R-12 ("WSS event loop 锁>100us=P0") 天然满足 (喂数根本不在那条线程)。RM setter: pnl/consec 是 atomic 无锁; exposure 走 s_->mu, 临界区 <1us 且与 evaluate 同线程串行无争用。聚合先取快照 (PL 锁释放) 再喂 (RM 锁), **两锁不嵌套, 无死锁**。

### ④ 增量 vs 全量
**exposure 用全量快照覆盖** (PL 已增量, 喂 RM 再增量会双重漂移; 全量覆盖 = RM 永远 = PL 真值, 自愈)。daily_pnl 全量累加器覆盖。日界 reset owner = **app 层 daemon watcher**, 非热路径, M1 留 hook。

### ⑤ 与老韩 RM 契约的接口边界
- **老周侧 (本 spec) 负责**: 数据从 PositionLedger 聚合 + 单位归一 (×1e6 转 micro) + 调用时机 (TickOne 尾部) + 线程归属 (loop_thread_)。
- **老韩侧 (RM 契约) 负责**: setter 签名 (set_condition_exposure(cid, micro) / set_outcome_exposure(token, micro) / set_daily_pnl(micro) / set_consec_loss(n)) + setter 内部并发安全 (已 atomic/mutex 齐) + check_position_caps_ 消费语义 + **单位真值 (确认 setter 收 micro)**。
- **交接面**: 四个 setter 的**单位 = micro pUSD** 必须老韩书面确认 (§4.3 是 1e6 失配高危区)。consec M1=0 stub 需老韩签字认可 (确认 M1 不平仓 consec 恒 0 不违风控意图)。

### ⑥ 可直接派 IC/GM 落地否 + 最大架构风险
- **可落地**: 是。改动面极小且物理隔离 (单文件 `paper_loop.cpp` 加一个 `FeedRiskGateway()` 私有方法 + TickOne 尾部一次调用 + 一个 pnl 累加器字段)。不碰 WSS、不碰 live、不碰 RM 内部、不开新线程、不动契约文件。**适合 GM 主干直接写** (CLAUDE.md §10.2 模式), 或派 1 个 IC (小肖, paper_loop owner)。
- **最大架构风险 (P0 级, 必须门禁)**: **单位失配 1e6 倍 (§4.3)**。PositionLedger 存 whole, RM 比 micro。喂数侧漏 ×1e6 → exposure 红线**静默架空** (与 §8.1 size→micro 翻车同型)。
  - **强制门禁**: 落地必带单测 — fill N 笔 size_pUSD_micro 已知 → 断言 `paper_rm_` 内部 condition_exposure == Σ(micro), 且第 (cap/单笔+1) 笔触发 EXCEED_CONDITION_EXPOSURE。无此测试不许 merge。
  - **彻底解法 (建议, 走 R-4)**: PositionLedger 改存 micro 与 size_pUSD_micro 同源消歧, 派老韩 review (ABI/单位变更触发下游审计, §8.1 第 3 条)。
- **次要风险**: M2 平仓回路接入时, consec/realized pnl 的 round-trip 结算逻辑需补 (本 spec 已标 stub 边界, 不阻塞 M1)。

---

## 8. 落地 checklist (供 GM/IC)

1. PaperLoop 新增私有 `void FeedRiskGateway()`: 取 `position_ledger_.get_per_condition_exposure()` / `get_per_outcome_exposure()`, **逐 key `rm_.set_*_exposure(key, val * 1'000'000)`** (whole→micro)。
2. PaperLoop 新增 loop_thread_ 私有 `daily_pnl_*` 累加器, PublishLedgerSnapshot 内顺手累加, FeedRiskGateway 内 `rm_.set_daily_pnl(micro)`。
3. consec: `rm_.set_consec_loss(0)` 显式 stub + 注释「M1 只买不平」(或干脆不调, 留默认 0 + 注释)。
4. TickOne Step 8b 之后调一次 `FeedRiskGateway()`。
5. 单测: 单位断言 + cap 触发断言 (§7 ⑥ 门禁)。
6. 老韩书面确认: 四 setter 单位 = micro + consec M1=0 认可。
7. (可选, 推荐) app 层日界 reset hook stub。
