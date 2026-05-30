# RM 喂数契约 spec (P0-1 安全网激活)

> **owner:** 老韩 (RM 主权, B 风控合规部)
> **last_review:** 2026-05-30
> **状态:** spec v1 — 只读分析产出, 不改代码。落地由 GM 主干 enforce, 数据流由老周架构定。
> **红线 cite (粘原文, §8.1 纪律 1):**
> - R-1: "任何下单链路绕过 `RiskManager` → 立即回滚 + post-mortem"
> - R-11: "Paper mode 污染真账本(写入 position / pnl_ledger / nonce_ledger) → P0"
> - R-12: "WebSocket event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0"
> - R-20: "所有数据源使用必须标记时间信息(4 时间戳契约) → P0;时间戳优先用数据源自带,禁本地 `now()` 替代上游 ts"

---

## 0. 问题陈述 (我已查实)

RM (`include/stcpp/risk/risk_gateway.hpp` + `src/stcpp/risk/risk_gateway.cpp`) 三条风控红线
**逻辑齐全但生产零喂数 → 安全网是空的**:

| 红线 | RM 内部状态 | 喂数 setter | 生产现状 | 后果 |
|---|---|---|---|---|
| **DD 软/硬熔断** | `daily_pnl_usdc_` (atomic int64, micro) | `set_daily_pnl(int64)` | paper_loop / paper_daemon **从不调** | 恒 0 → `pnl<0` 分支永不进 → DAILY_LOSS_HALT 永不触发 |
| **per-condition / per-outcome exposure cap** | `condition_exposure_usdc` / `token_exposure_usdc` map | `set_condition_exposure` / `set_outcome_exposure` | **从不调** | map 恒空 → `cur=0` → cap 永远比 `0+size` 永不咬 |
| **consec loss halt** | `consec_loss_` (atomic int32) | `set_consec_loss(int32)` | **从不调** | 恒 0 → `cl >= 5` 永不成立 |
| bankroll | `bankroll_usdc_` (atomic int64, micro) | `set_bankroll(int64)` | paper_loop.cpp:142 **已喂** | OK (唯一活的) |

证据:
- `paper_daemon.cpp` 构造 `paper_rm_` (行 215) 后, 全文件 **无任何 `set_daily_pnl/set_condition_exposure/set_outcome_exposure/set_consec_loss` 调用** (grep 实测)。
- `paper_loop.cpp:142` 仅 `rm_.set_bankroll(...)` 一处喂数;Step 8 (`position_ledger_.apply_fill`) 后**未回喂 RM**。
- DD 分支 `risk_gateway.cpp:482` `pnl = daily_pnl_usdc_.load()`;`pnl<0` 是 DD 全部触发前提,恒 0 即死分支。
- exposure 分支 `risk_gateway.cpp:443-465`;map 空 → `cur=0`,`from_micro(0+size) > cap` 仅在单笔自身越 per-order 时才可能,**累积敞口维度完全失效**。

**根因不在 RM 逻辑,在喂数链路缺失。** 本 spec 钉死 RM 侧"要被喂什么、setter 语义、谁在什么时机喂、怎么验"。

---

## 1. 数据可得性勘验 (喂数源已就位)

好消息:喂数所需的数据源 **已全部存在**,无需新建账本。

| RM 需要的量 | 现成数据源 | 接口 |
|---|---|---|
| per-token exposure | `PositionLedger` | `get_per_outcome_exposure() → map<token_id, int64 signed micro>` (position_ledger.hpp:69) |
| per-condition exposure | `PositionLedger` | `get_per_condition_exposure() → map<condition_id, int64 signed micro>` (position_ledger.hpp:73) |
| daily P&L | `PositionLedger` 持仓 + mark, 或 `LedgerFeatures` | `pnl_realized` + `pnl_unrealized` / `pnl_net()` (ledger_snapshot_hub.hpp:104-120) |
| consec loss | 平仓 realized P&L 符号序列 | **M1 paper 只开仓不平仓 → realized 恒 0 → consec 无源 (见 §2.4)** |

`PositionLedger::get_per_*_exposure()` 返回的 map 单位/语义与 RM 的 exposure map **完全一致** (signed int64 micro pUSD)。
这意味着 exposure 喂数是 **直接 map 搬运**,零换算。

---

## 2. RM 需喂量清单 — 逐个钉死 setter 契约

### 2.1 daily_pnl (DD 熔断)

| 维度 | 契约 |
|---|---|
| **setter** | `set_daily_pnl(std::int64_t usdc) noexcept` (现有,签名不动) |
| **单位** | **micro pUSD** (1e-6 pUSD)。与 `bankroll_usdc_` / `size_pUSD_micro` 同型,直接比较。**严禁喂 whole pUSD** (会把 -5% 阈值放大 1e6 倍 → DD 永不触发,等同没喂)。 |
| **语义** | **日内累计盈亏 = 已实现 + 未实现**。RM 的 `pnl<0` 判负即损失。RM 内部 `loss = -pnl` 与 `soft/hard_threshold = pct × bankroll` 比。喂"日内总 P&L 的 net 值",**不是**单笔、不是 gross(必须扣 fee:DD 红线管真实净亏)。来源 = `Σ LedgerFeatures.pnl_net()` over 当日所有 condition,× 1e6 转 micro。 |
| **符号** | 盈利为正、亏损为负。RM 只在 `pnl<0` 动作。喂方负责符号正确 (PositionLedger pnl_net 已是 signed)。 |
| **覆盖语义** | **全量快照覆盖** (store,非累加)。每次喂"当前日内 P&L 全量",RM atomic store 覆盖。不是 delta。 |

**为什么是 realized+unrealized 而非仅 realized:** DD 熔断是"今天亏到底线就停",未实现浮亏同样吃保证金/反映真实风险敞口。仅 realized 会让浮亏巨大但未平仓时 DD 装睡。GM §9 裁决 -3%软/-5%硬 锚的是 bankroll 真实缩水,含浮动。

### 2.2 condition_exposure (per-condition cap)

| 维度 | 契约 |
|---|---|
| **setter** | `set_condition_exposure(std::string const& condition_id, std::int64_t usdc) noexcept` (现有) |
| **单位** | micro pUSD,signed (正=多仓名义,负=空仓名义)。与 `cfg_.market_exposure_cap_usdc` 同型。 |
| **语义** | 每 condition 的**当前净名义敞口** = `Σ token 持仓 size_usdc` under 该 condition。直接来自 `PositionLedger::get_per_condition_exposure()`。 |
| **覆盖语义** | **全量覆盖** (per-key store)。喂 `condition_id → cur_exposure`,RM `map[cid] = v`。新 fill 后该 condition 的全量重喂。 |
| **cap 比较口径注意** | RM 在 `check_position_caps_` 比 `from_micro(cur + it.size_pUSD_micro) > cap`,即"已有敞口 + 本单"。喂的 `cur` 必须是**不含本待评估单**的已落地敞口 (本单是 intent,尚未 apply_fill)。PositionLedger 只记已成交 → 天然满足。 |

**⚠️ signed exposure 与 cap 的语义边界 (移交老周/小袁确认):** 当前 RM 用 `cur + size` 与正 cap 比。若 `cur` 为负 (净空仓) 而新单 BUY,`cur+size` 可能仍 < cap 甚至为负 → 放行,语义上"减仓不受 cap 限"合理。但**双向建仓的总名义敞口** (|多| + |空|) 当前 RM 不覆盖。M1 paper 只 BUY YES 单边 → 不暴露此 gap。**M2 双边做市前,exposure 是否应取 abs 名义而非 net,需 cap 语义二次评审 (老韩 + 小袁)。** 本 spec 锁定 M1 = net 直搬。

### 2.3 outcome_exposure (per-token cap)

| 维度 | 契约 |
|---|---|
| **setter** | `set_outcome_exposure(std::string const& token_id, std::int64_t usdc) noexcept` (现有) |
| **单位/语义/覆盖** | 同 2.2,但 key = token_id,源 = `PositionLedger::get_per_outcome_exposure()`。RM 比 `cfg_.per_outcome_cap_usdc`。 |
| **触发前提** | RM 仅在 `!token_id.empty() && per_outcome_cap_usdc.v > 0` 才查 (risk_gateway.cpp:460)。喂方保证 token_id 非空 (paper_loop 已填)。 |

### 2.4 consec_loss (连亏熔断) — **M1 无源,M2 激活**

| 维度 | 契约 |
|---|---|
| **setter (现有)** | `set_consec_loss(std::int32_t n) noexcept` — RM 不自己计,**喂方喂"当前连续亏损计数"全量值**。 |
| **语义** | n = 连续 N 笔**已平仓 round-trip 为亏损**的计数。RM 比 `cl >= cfg_.consec_loss_halt_count` (默认 5)。一笔盈利平仓 → 喂方重置 n=0 再喂。 |
| **M1 现实** | **paper_loop 当前只开仓不平仓** (paper_loop.cpp:666 注释 "M1 买入阶段 realized = 0")。无平仓 → 无 round-trip realized → consec **本质无数据可喂**。 |
| **M1 决议** | consec 喂数 **M1 不强制激活** (无源不造假)。RM 侧 `set_consec_loss` 保持可用,生产暂不调 → `consec_loss_=0` → CONSEC 分支静默 (符合"无平仓即无连亏"语义,**不是 bug,是无源**)。**验收口径降级为单测验证 (§5),生产激活随 M2 平仓链路。** |
| **M2 契约** | 平仓 fill 产生 realized P&L 后,喂方在 PositionLedger/pnl 层维护连亏计数器 (亏损→+1,盈利→归 0),每次平仓后 `set_consec_loss(counter)`。计数逻辑归喂方 (PositionLedger 或上层),**不进 RM** — RM 只做阈值判定,保持 RM 无状态累积、纯 enforce。 |

**老韩立场:RM 不接"每笔 win/loss 让 RM 自己累计"。** 理由:① RM 是 enforce 层不是会计层,累计状态放 RM 会复杂化 atomic 语义 + 难测;② 连亏定义 (round-trip? 单 fill? 跨日重置?) 属业务,业务漂移不该改 RM。喂方算好 n,RM 只比阈值。

### 2.5 bankroll (已活,记录在案)

`set_bankroll(int64 micro)` paper_loop.cpp:142 已喂,× 1e6 转 micro 正确。**本条无需改动,仅纳入清单完整性。** 注意 bankroll 是 DD/exposure cap 的分母锚 (DD 阈值 = pct × bankroll),bankroll 喂错会连带 DD 失效 → 列为喂数链路的前置依赖。

---

## 3. 喂数时机 + 频率

### 3.1 推荐方案:fill 后回喂 + daily reset 钩子

```
PaperLoop::TickOne  Step 8  position_ledger_.apply_fill(...)   ← 已存在
                          ↓  [新增喂数 hook, 紧随其后]
                    FeedRiskManagerFromLedger(rm_, position_ledger_, ledger_hub_)
```

| 喂哪个 | 时机 | 频率 | 覆盖方式 |
|---|---|---|---|
| condition/outcome exposure | **每笔 fill 成功后** (apply_fill 之后立即) | 每 fill | 取 `get_per_*_exposure()` 全量,逐 key `set_*_exposure` 覆盖。仅喂本 fill 受影响的 condition/token 即可 (增量 key, 全量 value),省遍历。 |
| daily_pnl | **每笔 fill 后** + **每个 quote/ledger tick 周期快照** | 每 fill + 周期 (unrealized 随 mark 漂移,需周期重算) | 全量 store。**注意:unrealized 随 mark price 变,即使无新 fill 也会变 → 必须周期喂,不能只 fill 后喂**,否则浮亏不更新 DD 装睡。 |
| consec_loss | M2: 每笔**平仓** fill 后 | 每平仓 | 全量 store |

**关键点 — daily_pnl 的双触发:**
- exposure 只在持仓变化时变 → fill 后喂足够。
- daily_pnl 的 unrealized 分量随 mark price 持续漂移 → **必须有周期快照喂数** (建议挂在 PaperLoop 的 tick 周期,与 LedgerSnapshot 发布同频),否则一笔大浮亏开仓后市场继续走坏,DD 永远看的是 fill 时刻的旧 P&L。

### 3.2 daily_pnl 的 reset (日界)

| 维度 | 契约 |
|---|---|
| **谁 reset** | 喂方 (会计层) 维护"当日起始 bankroll"和"当日 P&L 基线",日界翻篇时把累计 realized 归零、重算 daily_pnl。 |
| **RM 侧** | RM **不感知日界**,RM 只在每次 `set_daily_pnl` 收当前日内值。日界 reset = 喂方在新交易日第一次喂时 `set_daily_pnl(0)` 起算。 |
| **新增需求** | **RM 侧无需新增 reset setter** — `set_daily_pnl(0)` 即 reset。日界判定 (UTC? 交易日历? Polymarket 结算日?) 归喂方/数据层,属业务,RM 不碰。**日界定义需老周 + 老胡确认** (跨洋部署,UTC vs 本地 vs Polymarket 结算窗口)。 |
| **建议** | 增量 P&L 跨日归零是 DD 的隐含语义 ("daily" loss)。若 reset 漏做 → 累计 P&L 跨日累加 → 某天小亏被昨日盈利掩盖或昨日亏损叠加误触发。**reset 正确性进 §5 验收。** |

### 3.3 增量 vs 全量 — 统一裁决

- **exposure:全量覆盖** (per-key store, RM map[k]=v)。理由:PositionLedger 已是 SSOT,RM 镜像它,全量覆盖天然幂等、无累加漂移风险。
- **daily_pnl:全量覆盖** (atomic store)。
- **consec_loss:全量覆盖** (喂方算好的当前计数)。
- **绝不用 delta/累加喂数** — 累加易因丢喂/重喂偏移,全量覆盖是幂等的 (§4)。

---

## 4. 幂等 / 线程安全契约

### 4.1 setter 的硬约束

| 约束 | 契约 |
|---|---|
| **noexcept** | 全部喂数 setter 已 `noexcept` (现有签名)。必须维持 — 喂数失败绝不抛 (fail-safe: 抛了就没安全网了)。 |
| **不阻塞 (R-12)** | 喂数 setter 内部:`set_daily_pnl/set_consec_loss/set_bankroll` 是 atomic store (无锁,纳秒级)。`set_*_exposure` 持 `s_->mu` (std::mutex) 锁 map。**R-12 红线: "锁 > 100us → P0"**。exposure setter 锁临界区只是一次 map insert/assign,远 < 100us,合规。但**喂数调用方绝不能在 WSS event loop 线程同步调这些 setter** (见 §6 线程边界)。 |
| **幂等** | 全量覆盖 → 同一值重复喂结果不变,天然幂等。漏喂一次 = 状态停在上一快照 (保守,不会误放行),下次喂数自愈。**重喂安全,漏喂自愈,无累加 → 幂等三性满足。** |

### 4.2 setter ↔ evaluate 并发语义

RM 当前实现:
- `daily_pnl_usdc_` / `consec_loss_` / `bankroll_usdc_`:`std::atomic`,setter store / evaluate load。**已线程安全** (acquire/release)。
- exposure map:`s_->mu` (mutex) 保护。`set_*_exposure` 与 `check_position_caps_` 都持同一 mutex → **已线程安全**。

| 场景 | 语义 |
|---|---|
| **喂数线程 ≠ evaluate 线程** | 允许。atomic + mutex 已覆盖。evaluate 读到的是某个一致的喂数快照 (可能略旧,符合 eventual 语义)。 |
| **喂数与 evaluate 在同一线程 (paper_loop)** | M1 现实:paper_loop 单线程串行 tick → fill 后喂数 → 下一 tick evaluate。**无并发,最简单最安全。** 这是 M1 推荐:喂数与 evaluate 同线程串行,RM 看到的永远是上一 fill 后的最新快照。 |
| **exposure map 多 key 喂数的原子性** | 当前每个 `set_*_exposure` 单独加锁 → 多 key 喂数**非原子** (喂到一半 evaluate 进来会看到部分更新)。M1 单线程串行无此问题。**M2 多线程时若需"一批 exposure 原子可见",需新增批量 setter (见 §4.3),否则 cap 判定可能跨 condition 不一致。** |

### 4.3 RM 侧改动建议 (新增/改 setter)

| 改动 | 必要性 | 理由 |
|---|---|---|
| **现有 setter 全部够用 (M1)** | M1 零改动 | exposure/pnl/consec/bankroll setter 签名、单位、语义均满足 M1 喂数。**P0-1 激活不需要改 RM,只需要喂数链路接上。** |
| (建议, M2) `set_exposure_batch(map const& cond, map const& tok) noexcept` | M2 多线程 | 一次加锁原子覆盖整个 exposure 视图,消 §4.2 多 key 非原子 gap。M1 不需要。 |
| (建议, 可选) `set_daily_pnl` 增 assert/clamp | nice-to-have | 防喂方误喂 whole pUSD (放大 1e6)。可在喂数 hook 侧做单位断言,不必改 RM。 |
| **不新增** consec 累计入口 | 明确拒绝 | §2.4 立场:RM 不接每笔 win/loss 累计。 |
| **不新增** daily reset 入口 | 明确拒绝 | §3.2:`set_daily_pnl(0)` 即 reset,RM 不感知日界。 |

**结论:RM 侧 M1 零改动。P0-1 是纯喂数链路接线工作 (GM 主干写 hook),不是 RM 改造。** 这是好消息 — 安全网逻辑不动,只接电源。

---

## 5. 验收口径 (证明安全网真活了)

每条红线给"喂入 → 触发"的端到端可测口径。验收分两层:**单测 (RM 隔离)** + **集成 (paper 链路真喂)**。

### 5.1 DD 软/硬熔断

| 测试 | 口径 |
|---|---|
| **单测 (现有 fixture 扩)** | 构造 RM,`set_bankroll(100k×1e6)`;`set_daily_pnl(-3001×1e6)` (越 -3% 软) → evaluate 开仓单 (is_close=false) 必 `DAILY_LOSS_HALT`;同 P&L evaluate 平仓单 (is_close=true) 必**放行**。`set_daily_pnl(-5001×1e6)` (越 -5% 硬) → evaluate 任何单 (含 is_close) 必 `DAILY_LOSS_HALT` + 状态迁 `HALTED`。 |
| **集成 (paper)** | paper 链路注入足够浮亏 (mark 下行) → 周期喂数后 daily_pnl 转负越阈 → 下一 tick evaluate 新开仓被拒,`/api/v1/risk/rejects` 出现 DAILY_LOSS_HALT。**关键反例:不喂数时此测试必 fail (恒 0 → 永不触发) → 证明喂数前安全网是空的。** |

### 5.2 exposure cap (condition + outcome)

| 测试 | 口径 |
|---|---|
| **单测** | `set_condition_exposure(cid, 4999×1e6)` (per-condition cap 5000);evaluate size=2×1e6 单 → `4999+2 > 5000` → 必 `EXCEED_CONDITION_EXPOSURE`。同理 `set_outcome_exposure(tok, 1999×1e6)` + size=2 → `EXCEED_PER_OUTCOME_CAP`。 |
| **集成 (paper)** | 连续 fill 累积同 condition 敞口至接近 cap → fill 后回喂 exposure → 再次 evaluate 越 cap 单被拒。**反例:不喂数时 map 恒空 → 累积敞口看不见 → 永远放行 → 证明喂数前 cap 是死的。** |

### 5.3 consec loss halt

| 测试 | 口径 |
|---|---|
| **单测 (M1 验收锚此层)** | `set_consec_loss(5)` (= halt_count) → evaluate 必 `CONSEC_LOSS_HALT`;`set_consec_loss(4)` → 不因 consec 拒。证明 RM 逻辑活、喂数 setter 通。 |
| **集成 (M2)** | M2 平仓链路就位后:构造 5 连亏平仓 round-trip → 喂方 set_consec_loss 累至 5 → evaluate 新开仓被拒。**M1 因无平仓源,集成层验收 defer 到 M2,单测层 M1 必过。** |

### 5.4 喂数链路本身的验收 (防"接了但接错")

| 测试 | 口径 |
|---|---|
| **单位一致性** | 断言喂入 RM 的 exposure == `PositionLedger::get_per_*_exposure()` 原值 (零换算,直搬)。喂入 RM 的 daily_pnl == `Σ pnl_net × 1e6`。**单位失配是 §8.1 纪律 3 点名的"静默架空 cap"风险 — 必测。** |
| **daily reset** | 跨日界喂数后 daily_pnl 归零起算,不跨日累加。 |
| **R-12 不阻塞** | 喂数 hook 在 paper tick 线程,非 WSS event loop 线程 (见 §6)。若未来挪进 WSS loop,锁临界区 benchmark < 100us。 |

---

## 6. 与老周架构的接口边界

| 我管 (老韩, RM 契约) | 老周管 (架构, 数据从哪来怎么流) |
|---|---|
| RM 要被喂哪些量、单位 (micro)、语义 (net/全量覆盖)、setter 签名 | 喂数 hook 装哪个线程、哪个模块拥有它 |
| setter noexcept / 不阻塞 / 幂等契约 | PositionLedger → RM 的数据搬运在哪执行 (paper_loop tick? 独立喂数线程?) |
| exposure=net 直搬、daily_pnl=realized+unrealized、consec=喂方算好 | daily_pnl 周期快照的触发节奏 (与 ledger snapshot 同频?) |
| RM 侧 M1 零改动结论 | 日界定义 (UTC/交易日历/Polymarket 结算窗口) — 业务+数据层 |
| 验收口径 (§5) | consec 计数器归谁 (PositionLedger 扩? 上层会计模块?) 的归属 |

**接口边界一句话:** 我交付"RM 这端的插座规格" (喂什么、什么单位、什么时机覆盖、并发怎么保证);
老周交付"从 PositionLedger 到插座的那根线" (谁在哪个线程把 `get_per_*_exposure()` / `pnl_net` 搬进 setter)。

**线程边界硬约束 (R-12):** 喂数 hook **禁止**装在 WSS event loop 线程 (同步搬运 + 锁会卡 loop)。
M1 推荐装在 paper_loop tick 线程 (fill 后回喂,单线程串行,无并发)。这条是红线,移交老周时必须守住。

---

## 执行摘要

**① RM 需喂的量清单 (单位/语义/setter)**
| 量 | setter (全现有) | 单位 | 语义 | 覆盖 |
|---|---|---|---|---|
| daily_pnl | `set_daily_pnl(int64)` | micro pUSD | 日内 realized+unrealized net | 全量 store |
| condition exposure | `set_condition_exposure(cid,int64)` | micro pUSD signed | 该 condition 净名义敞口 | per-key 全量 |
| outcome exposure | `set_outcome_exposure(tok,int64)` | micro pUSD signed | 该 token 净名义敞口 | per-key 全量 |
| consec_loss | `set_consec_loss(int32)` | 计数 | 当前连亏 round-trip 数 (喂方算) | 全量 store |
| bankroll | `set_bankroll(int64)` | micro pUSD | 资金 (已活, paper_loop:142) | 全量 store |

数据源全部现成:`PositionLedger::get_per_outcome_exposure()` / `get_per_condition_exposure()` (单位与 RM 完全一致,直搬);daily_pnl = `Σ LedgerFeatures.pnl_net() × 1e6`。

**② 喂数时机**
- exposure:每笔 fill 后 (apply_fill 之后立即回喂受影响 key)。
- daily_pnl:每笔 fill 后 **+ 周期快照** (unrealized 随 mark 漂移,只 fill 后喂会装睡)。
- daily reset:`set_daily_pnl(0)` 日界起算,RM 不感知日界 (归喂方)。
- consec:M2 每平仓后 (M1 无平仓源)。

**③ RM 侧改动:M1 零改动。** 所有 setter 签名/单位/语义已满足。P0-1 是喂数链路接线 (GM 主干写 hook),不是 RM 改造。建议 (非必须):M2 加 `set_exposure_batch` 解多 key 原子;明确**拒绝**新增 consec 累计入口与 daily reset 入口 (RM 是 enforce 层不是会计层)。

**④ 线程安全契约:** setter 全 noexcept + 不阻塞;atomic (pnl/consec/bankroll) + mutex (exposure,临界区 << R-12 的 100us)。全量覆盖 → 幂等 (重喂安全/漏喂自愈/无累加)。M1 喂数与 evaluate 同 paper tick 线程串行,无并发。

**⑤ 验收口径:** 每条红线"喂入越阈 → 必触发对应 reject"的单测 (DD 软放平仓/硬迁 HALTED、exposure 双 cap 咬、consec≥5 halt) + paper 集成 (真喂触发,**且不喂时必 fail = 证明喂前安全网空**)。加单位一致性 + daily reset + R-12 不阻塞三项链路验收。consec 集成层 defer M2,单测层 M1 必过。

**⑥ 与老周边界:** 我管 RM 插座规格 (喂什么/单位/时机/并发);老周管从 PositionLedger 到插座的线 (哪个线程搬、daily_pnl 周期节奏、日界定义、consec 计数器归属)。**R-12 硬约束:喂数 hook 禁装 WSS event loop 线程,M1 装 paper tick 线程。**

**待二次评审项 (我已标):**
- exposure net vs abs 名义 (M2 双边做市前,cap 语义,老韩+小袁)。
- 日界定义 (UTC/交易日历/Polymarket 结算窗口,老周+老胡)。
- consec 计数器模块归属 (PositionLedger 扩 or 上层会计,老周)。
