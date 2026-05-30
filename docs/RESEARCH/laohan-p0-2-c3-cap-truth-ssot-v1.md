# P0-2 c3 — Cap 真值 SSOT + 验收门禁

- **owner:** 老韩 (风控工程师, RM 主权 + cap 真值 owner)
- **last_review:** 2026-05-30
- **status:** 决策已拍 (RM 主权直接定数); 待 GM 落地 c3 代码变更
- **scope:** 只读分析 + 决策文档。本文不改生产代码。
- **前置:** P0-2 c2 已落地 (RiskConfig 3 cap 字段已是 MicroPUSD 强类型, 字节级零变, 已联签)。

---

## 0. 治理 — R-4 红线 + §8.1 第 3 条 (粘原文, 禁转述)

c3 是 cap 单位语义/真值变更 + 默认值变更 + 字段消费点单位修正, 触以下两条, 逐字粘:

> **§8 红线 (CLAUDE.md):** 「数据 schema 静默变更（不通知下游） → 责任人承担事故」

> **§8.1 第 3 条 (CLAUDE.md, 2026-05-30 立):** 「**ABI/字段单位变更必触发下游审计（补 R-4 配套）：** 任何字段重命名/单位变更（如 `size_usdc → size_pUSD_micro`）必须 audit 全部比较点/消费点的单位一致性，否则会「静默架空」依赖该字段的红线（caps/exposure/bankroll）。这类变更走 R-4（schema 静默变更红线）。」

**c3 合规要求:** cap 字段官方单位钉死后, 必须 audit 全部消费点 (本文 §1 表) 单位一致, 任一消费点不一致 = 静默架空 caps 红线 = R-4 事故。本文即为该 audit + SSOT。

---

## 1. cap 真值 SSOT (RM 主权钉死)

### 1.1 官方单位 = micro pUSD (c2 类型已确立, c3 确认并钉死)

`RiskConfig.{per_order_cap_usdc, market_exposure_cap_usdc, per_outcome_cap_usdc}` 三字段:

- **官方单位 = micro pUSD** (1 pUSD = 1'000'000 micro)。`MicroPUSD.v` = 原始 micro 整数。
- 依据: RM 全部 cap 比较点均以 `size_pUSD_micro` (micro) 同型比 — `risk_gateway.cpp:432/452/465`, 均 `MicroPUSD::from_micro(size) > cfg_.xxx_cap`。RM 侧单位已自洽 (micro vs micro), c2 后字节级零变。**RM 是 cap 真值的唯一权威消费者, 故 cap 官方单位随 RM = micro。**

### 1.2 全消费点 audit 表 (§8.1#3 强制)

| # | 消费点 | 文件:行 | 当前单位处理 | c3 后 | 正确性 |
|---|---|---|---|---|---|
| 1 | RM PER_ORDER | risk_gateway.cpp:432 | `from_micro(size_micro) > cap` | 不变 | ✅ micro vs micro 自洽 |
| 2 | RM CONDITION | risk_gateway.cpp:452 | `from_micro(cur+size) > cap` | 不变 | ✅ |
| 3 | RM PER_OUTCOME | risk_gateway.cpp:465 | `from_micro(cur+size) > cap` | 不变 | ✅ |
| 4 | sizing Cap1 | sizing_calculator.cpp:177 | `(double)cap.v` ← micro raw 当 whole pUSD 比 | **改 `cap.to_pusd()`** | ❌→✅ |
| 5 | sizing Cap2 | sizing_calculator.cpp:189 | `(double)cap.v` 同上 | **改 `cap.to_pusd()`** | ❌→✅ |
| 6 | sizing Cap3 | sizing_calculator.cpp:200 | `(double)cap.v` 同上 | **改 `cap.to_pusd()`** | ❌→✅ |
| 7 | paper→sizing 灌 cap | paper_loop.cpp:416-421 | `from_micro((int64)whole_pUSD)` ← .v=10 (=1e-5 pUSD) | **改 `from_pusd(whole)`** = .v=10e6 | ❌→✅ |
| 8 | paper→RM 灌 cap | paper_daemon.cpp:207-212 | `from_micro(whole×1e6)` = .v=10e6 | 不变 (已对) | ✅ |
| 9 | RiskConfig 默认 | risk_gateway.hpp:304-306 | 10'000/50'000/25'000 micro = 0.01/0.05/0.025 pUSD | **校准 (见 §2)** | ❌→✅ |
| 10 | consistency 测试 size | test_sizing_rm_consistency.cpp:184 | `(int64)suggested_notional` ← 缺 ×1e6 | **改 ×1e6** | ❌→✅ |

**核心病灶 (双错对消):** sizing 侧 cap 是 micro raw 当 whole pUSD 读 (消费点 4/5/6), 而 paper 灌进去的又是 `from_micro(10)`=.v=10 (消费点 7, 错把 whole 当 micro)。10 micro 当 10 pUSD 读 → 数值碰巧对上 paper demo 的 10 pUSD 意图, 但两侧各错 1e6 倍互相抵消。c3 把两侧同时摆正 (消费点 4-7 全改) 才能真正同源。**只改一边 (小袁 + 我点名) 会把生产 notional 压垮或放飞** — 见 §4 算例。

---

## 2. RiskConfig 生产默认值校准 (RM 主权拍数)

### 2.1 现默认荒谬

`per_order_cap{10'000}` micro = **0.01 pUSD** — 单笔上限一分钱, 任何真实 Kelly notional 必被钳成 0 或拒。market 50'000 micro=0.05 pUSD, outcome 25'000 micro=0.025 pUSD 同样荒谬。历史遗留, c2 注释已标 "归 c3 校准"。

### 2.2 校准依据 (bankroll 比例锚定)

RiskConfig 默认是 **fail-safe 兜底默认** (生产实跑由 paper_daemon / 实盘 config 显式覆盖)。默认值哲学: **保守但非荒谬** — 以 RiskConfig 默认 `bankroll_usdc = 100'000` (micro? 否 — 见注) 为锚, 取行业惯例单笔风险比例。

> **注 (bankroll 单位坑, 归 c2b/P0-1, 本文仅标前提):** `RiskConfig.bankroll_usdc` 仍是裸 `std::int64_t = 100'000`, RM `check_position_caps_` line 473 用 `size_pUSD_micro > br` 直接比 — 即把 br 当 **micro** 用 (100'000 micro = 0.1 pUSD bankroll, 同样荒谬)。bankroll 强类型化 + 真值归 c2b/P0-1, 不在 c3 scope。**c3 默认值校准以「bankroll 语义意图 = 100'000 pUSD」为锚拍比例**, bankroll 字段本身的单位修正另立。

**单笔风险比例 (锚 bankroll = 100k pUSD 意图):**

| cap | 比例 | whole pUSD | **micro (默认值)** | 依据 |
|---|---|---|---|---|
| per_order_cap | 1% bankroll | 1'000 | **`1'000'000'000`** (1e9) | 单笔 ≤ 1% 账户, 远严于 sizing Cap4 的 10% 硬顶, 双层保守 |
| per_outcome_cap | 2% bankroll | 2'000 | **`2'000'000'000`** (2e9) | 单 token 累计敞口 ≤ 2% |
| market_exposure_cap | 5% bankroll | 5'000 | **`5'000'000'000`** (5e9) | 单 condition 累计敞口 ≤ 5% (含多 token), 须 ≥ per_outcome |
| (锚) bankroll_usdc | — | 100'000 | (归 c2b) | 默认账户规模意图 |

**序约束 (RM 不变量):** `per_order_cap ≤ per_outcome_cap ≤ market_exposure_cap`。1e9 ≤ 2e9 ≤ 5e9 ✅。单笔不能超单 token 累计, 单 token 不能超单 condition 累计 — 否则下层 cap 永不触发 = 死代码 cap。

**为何默认就给"真值"而非 0/极小:** RiskConfig 默认是兜底, 若给 0 → 任何 intent 全拒 (fail-closed 过头, paper/实盘忘配 config 时静默全拒, 难 debug); 若留 0.01 pUSD → fail-open 风格的荒谬 (sizing 当 whole 读 10 micro=10pUSD 假性放行)。给「1%/2%/5% bankroll」= 保守可用兜底, 配错 config 也只损失到 1% 单笔, 不爆仓。

### 2.3 这三个数 = RM 主权终值

```
per_order_cap_usdc       = MicroPUSD{1'000'000'000}   // 1,000 pUSD = 1% of 100k bankroll
per_outcome_cap_usdc     = MicroPUSD{2'000'000'000}   // 2,000 pUSD = 2%
market_exposure_cap_usdc = MicroPUSD{5'000'000'000}   // 5,000 pUSD = 5%
```

---

## 3. paper demo 真值数据流 (单一源 → sizing/RM 各转)

### 3.1 单一源 = PaperLoopConfig 三 cap, 单位 = **whole pUSD** (人读友好, 不变)

`PaperLoopConfig.{per_order_cap_usdc=10, market_exposure_cap_usdc=50, per_outcome_cap_usdc=25}` (paper_loop.hpp:117-119) — 保持 **whole pUSD** 语义。理由: paper demo config 给人写/人读, whole pUSD 直观; 单一源就在这里, 下游两条支路各自转。

### 3.2 统一数据流 (c3 后)

```
              PaperLoopConfig (whole pUSD: 10 / 50 / 25)   ← 单一真值源
                       │
        ┌──────────────┴───────────────┐
        ▼ (sizing 支路)                  ▼ (RM 支路)
 paper_loop.cpp:416-421            paper_daemon.cpp:207-212
 sizing_cfg.cap = from_pusd(10)    paper_rm_cfg.cap = from_pusd(10)
   = MicroPUSD{10'000'000}           = MicroPUSD{10'000'000}
        │  (c3 改: from_micro→from_pusd)   │ (已 ×1e6, 改 from_pusd 统一写法)
        ▼                                 ▼
 sizing Cap1/2/3 用 cap.to_pusd()   RM 用 from_micro(size) > cap
   = 10.0 (whole) 比 whole notional   = micro vs micro
        │                                 │
        └────────► 两端同源 MicroPUSD{10e6} ◄────┘
              sizing 输出 whole notional ≤ 10 pUSD
              → paper_loop.cpp:494 ×1e6 = size_micro ≤ 10e6
              → RM cap 10e6, size ≤ cap → 零 size reject ✅
```

**一句话:** PaperLoopConfig 存 whole pUSD (10/50/25), sizing 与 RM 两条支路**都用 `from_pusd()` 转成同一 MicroPUSD (10e6/50e6/25e6)**; sizing 比较前 `.to_pusd()` 回 whole 比 whole notional, RM 直接 micro 比 micro size。同源同值, 不再双错对消。

**关键改动:** paper_loop.cpp:416-421 当前的 `from_micro((int64)10.0)` = .v=10 (错) → 改 `from_pusd(10.0)` = .v=10e6 (对, 与 RM 支路同值)。

---

## 4. sizing 数据流 + 改 .to_pusd() 是否压垮生产 notional

### 4.1 sizing 5-cap 链单位 (c3 后全 whole pUSD 域)

sizing 内部全程 **whole pUSD** double 运算 (notional_kelly = kelly_frac × bankroll_usdc[whole])。cap1/2/3 改 `.to_pusd()` 后, 三个 cap_limit 从「micro raw 当 whole」变「真 whole pUSD」, 与 notional 同域可比。Cap4 (BANKROLL_FRACTION = bankroll × 0.10) 本就 whole, 不变。

### 4.2 exposure 输入对齐 (前提声明, 回写归 c4/P0-1)

sizing 输入 `current_token_exposure_usdc` / `current_condition_exposure_usdc` = **whole pUSD** (与 cap2/cap3 改 `.to_pusd()` 后同域, 减法 headroom 合法)。

**c3 前提 (硬约束):** paper_loop.cpp:407-408 这两个输入**保持 0.0 + TODO**。理由: 真实 exposure 回写 (RM micro exposure → sizing whole 输入) 是 **c4 随 P0-1** 的活, c3 不碰。c3 阶段 exposure=0 → headroom2 = cap2 全额, headroom3 = cap3 全额, sizing 不因 exposure 维度误钳。**c4 回写时必须 `RM_exposure_micro.to_pusd()` 转 whole 再喂 sizing** — 否则又一个单位失配现场。本文标死此前提, c4 接手。

### 4.3 会不会压垮生产 notional — 算例 (paper demo, bankroll=1000 pUSD)

bankroll = 1000 pUSD, λ=0.25 quarter Kelly, Cap4 = bankroll×10% = 100 pUSD, PaperLoopConfig cap = 10/50/25 pUSD。

**改前 (现状, sizing 当 whole 读 micro raw .v=10):**
- cap1_limit = (double).v = 10 → "碰巧"= 10 pUSD (因为 from_micro(10) 灌的就是 10)。
- 即现状双错对消, sizing 实际按 10 pUSD per_order 钳。notional 被钳到 ≤ 10。

**改后 (c3, from_pusd(10)=.v=10e6, sizing .to_pusd()=10.0):**
- cap1_limit = cap.to_pusd() = 10e6/1e6 = **10.0 pUSD** — 与改前数值**完全一致**。
- ✅ **paper demo notional 零变** — 因为 paper 这条链改前靠双错对消碰对了 10, 改后靠两边都对得到 10, 终值相同。**不压垮也不放飞。**

**生产路径 (RiskConfig 默认, 改后):**
- 假设实盘 bankroll = 100k pUSD, per_order 默认 1000 pUSD (§2.3)。
- Kelly notional 典型: edge 2%, c=0.5 → f*_full = 0.02/0.5 = 0.04, ×λ0.25 = 0.01, ×bankroll 100k = 1000 pUSD。
- cap 链: cap1=1000, cap4=100k×0.1=10k → 取 min = 1000 pUSD。合理单笔, 不压垮。
- **若 c3 漏改 sizing .to_pusd() (只改默认值):** cap1_limit = (double)1e9 = 10亿 pUSD → cap1 永不触发, sizing 只剩 Cap4 (10% bankroll=10k) 兜底 → 单笔可达 10k 而非预期 1000, **放飞 10 倍**。**这正是「只钉死单边动会把生产 notional 压垮/放飞」的实证** — 故 c3 必须默认值 + sizing .to_pusd() + paper from_pusd 三处同改, 缺一即失配。

**结论:** c3 完整三改 → paper notional 零变, 生产 notional 落在 Kelly×cap 设计区间 (单笔 1% bankroll), 不压垮。只改单边 → 1e6 倍量级错位, 压垮 (cap→0) 或放飞 (cap→∞)。

---

## 5. 验收门禁 (可执行口径)

### 5.1 consistency 测试修正 (test_sizing_rm_consistency.cpp)

**改 1 — size_micro 补 ×1e6 (line 184, 双处 line 333/375 同查):**
```cpp
// 改前 (缺 ×1e6, whole 当 micro 灌, 与 cap 各错 1e6 对消假放行):
auto const size_micro = static_cast<std::int64_t>(out.suggested_notional);
// 改后 (suggested_notional 是 whole pUSD, ×1e6 转 micro 喂 RM):
auto const size_micro = static_cast<std::int64_t>(out.suggested_notional * 1'000'000.0);
```

**改 2 — make_rm 的 cfg cap 用真值 micro (line 94-98 区, 显式设, 不靠默认):**
```cpp
cfg.per_order_cap_usdc       = domain::MicroPUSD::from_pusd(1'000.0);  // 1k pUSD
cfg.per_outcome_cap_usdc     = domain::MicroPUSD::from_pusd(2'000.0);
cfg.market_exposure_cap_usdc = domain::MicroPUSD::from_pusd(5'000.0);
cfg.bankroll_usdc            = bankroll_usdc;  // 现传 100'000; 见 §2.2 注 bankroll 单位前提
```
> 注: sizing 用的 `cfg` (SizingCalculator::compute 的入参) 与 RM 用的 `cfg` 必须**同一个对象同源** (测试现在就是 `make_rm` 返回的 cfg 同时喂两端), c3 后 sizing 读 `.to_pusd()`、RM 读 `from_micro(size)` 比同一 cap, 这是 SSOT 验证的核心。

### 5.2 验收断言 (三道, 全绿才算 c3 守住)

**断言 A — 零 sizing 维度 reject (现有 Test1 强化):**
```
EXPECT_EQ(sizing_dim_rejects, 0)
```
语义: sizing 显正仓位 (suggested_notional>0) → ×1e6 喂 RM → RM 不因 PER_ORDER/PER_OUTCOME/CONDITION/BANKROLL 任一拒。改 1+改 2 后, sizing cap (whole) 与 RM cap (micro) 同源 (差精确 1e6), sizing 已自钳到 ≤ cap_whole → ×1e6 必 ≤ RM cap_micro → 零 reject。**若仍有 reject = 两端 cap 不同源, c3 失败。**

**断言 B — 反向验证: 故意越 cap 必拒 (新增, 防"恒不拒"假绿):**
```cpp
// 构造 suggested_notional 略超 per_order_cap 的 intent, ×1e6 喂 RM, 必须 EXCEED_PER_ORDER_CAP
auto over = make_minimal_intent(
    domain::MicroPUSD::from_pusd(1'000.01).v,  // 1000.01 pUSD > 1000 cap
    ci, price, slip, "over_cap");
EXPECT_TRUE(gw->evaluate(over).is_rejected());
EXPECT_EQ(gw->evaluate(over).reject, risk::RejectCode::EXCEED_PER_ORDER_CAP);
```
语义: 断言 A 证"该放行的放行", 断言 B 证"该拒的拒"。**只有 A 会被「cap=∞ 恒不拒」假绿骗过 (这正是当前 bug 的伪装); B 钉死 cap 真在生效**, 边界 1 micro 都不放过。

**断言 C — 边界等值 (cap 真值精确性):**
```cpp
// size 恰 = cap → 不拒 (≤ 语义); cap + 1 micro → 拒
auto eq  = make_minimal_intent(domain::MicroPUSD::from_pusd(1'000.0).v, ...);   // == cap
auto p1  = make_minimal_intent(domain::MicroPUSD::from_pusd(1'000.0).v + 1, ...); // cap+1micro
EXPECT_FALSE(gw->evaluate(eq).is_rejected());   // RM 用 > 比, == 不拒
EXPECT_TRUE (gw->evaluate(p1).is_rejected());
```
语义: 锚定 cap 真值在 micro 粒度精确, 防未来再有 1e6 漂移。

### 5.3 门禁口径汇总 (CI 必须全绿)

| 门 | 检查 | 通过判据 |
|---|---|---|
| G1 | consistency Test1 (N=10000, 改1+改2) | sizing_dim_rejects == 0 |
| G2 | 反向 over-cap (断言B) | EXCEED_PER_ORDER_CAP 命中 |
| G3 | 边界等值 (断言C) | == 放行, +1micro 拒 |
| G4 | paper notional 回归 | paper demo (bankroll 1000, cap 10/50/25) suggested_notional 与 c3 前逐 tick 一致 (零变, 见 §4.3) |
| G5 | grep audit | `grep -n '.v' sizing_calculator.cpp` cap 行清零 (无 `cfg.*cap*.v` 当 whole 读); paper_loop 无 `from_micro.*cap_usdc` |

---

## 6. c3 可否直接派 IC/GM 落地

**可以, 边界清晰、改动机械、验收可执行。** 改动清单 (5 文件, 全在本文 §1 audit 表):

1. `risk_gateway.hpp:304-306` — 3 默认值改 1e9/2e9/5e9 (§2.3)
2. `sizing_calculator.cpp:177/189/200` — `.v` → `.to_pusd()` (cap1/2/3)
3. `paper_loop.cpp:416-421` — `from_micro((int64)x)` → `from_pusd(x)`
4. `paper_daemon.cpp:207-212` — 可选: `from_micro(x×1e6)` 统一写成 `from_pusd(x)` (等值, 仅一致性)
5. `test_sizing_rm_consistency.cpp` — size_micro ×1e6 + make_rm cap 真值 + 断言 B/C (§5)

**派单约束 (给 GM/IC):**
- 三处单位改 (默认值/sizing/paper) **必须同 commit**, 禁拆 — 任一单独上线即 1e6 倍失配 (§4.3 实证)。R-4 红线。
- 落地后 `paper_loop.cpp:407-408` exposure 输入**保持 0.0 + TODO 不动** (c4/P0-1 scope, §4.2 前提)。
- `RiskConfig.bankroll_usdc` 强类型化**不在 c3** (c2b/P0-1), c3 仅以「bankroll 意图=100k pUSD」为锚拍 cap 比例。
- 这是 RM 主权决策, cap 三默认值 (1e9/2e9/5e9 micro) 与验收断言 = 我 (老韩) 终值, 实施侧不得改数, 改数回 RM 复审。

**RM 联签前提:** GM/IC 落地后跑 G1-G5 全绿 + diff 仅触本文 §1 audit 表内消费点, 我复签。任何额外 cap 比较点出现 (新消费点) 必须回 §1 表补 audit, 否则静默架空 = R-4。

---

## 执行摘要

1. **3 个 cap 生产默认值 (micro):** `per_order = 1'000'000'000` (1000 pUSD=1% bankroll) / `per_outcome = 2'000'000'000` (2000=2%) / `market_exposure = 5'000'000'000` (5000=5%)。序 1e9≤2e9≤5e9 满足 RM 不变量。锚 bankroll 意图 100k pUSD。现默认 10'000 micro=0.01pUSD 荒谬, 废。

2. **paper 数据流一句话:** PaperLoopConfig 存 whole pUSD (10/50/25) 为单一真值源, sizing 与 RM 两支路都用 `from_pusd()` 转同一 MicroPUSD (10e6/50e6/25e6), sizing 比前 `.to_pusd()` 回 whole 比 whole notional、RM 直接 micro 比 micro size — 同源同值, 终结双错对消。

3. **改 .to_pusd() 压不压垮:** 不压垮。paper demo notional **零变** (改前靠双错对消碰对 10 pUSD, 改后靠两边都对仍得 10)。生产路径单笔落在 Kelly×cap 设计区 (1% bankroll≈1000 pUSD)。**但只改单边会爆:** 漏改 sizing 只改默认值 → cap1=10亿 pUSD 永不触发 → 单笔放飞到 Cap4 的 10% bankroll (10x); 漏改默认值只改 sizing → cap=0.01pUSD → notional 全钳 0 (压垮)。故三改必须同 commit。

4. **consistency 测试验收口径:** ① size_micro 补 ×1e6 (line 184/333/375); ② make_rm cap 用 `from_pusd` 真值; ③ 三断言 — A 零 sizing reject (该放行的放行)、**B 反向 over-cap 必拒 EXCEED_PER_ORDER_CAP (防"cap=∞ 恒不拒"假绿, 这是当前 bug 伪装)**、C 边界等值 (== 放行 / +1micro 拒, 锚 micro 粒度精确)。G1-G5 全绿 + grep audit cap 行无 `.v` 当 whole。

5. **可直接派 IC/GM 落地:** 可以。5 文件机械改 (默认值/sizing .to_pusd/paper from_pusd/daemon 一致性/测试), 边界清晰、验收可执行。硬约束: 三处单位改同 commit 禁拆 (R-4); exposure 输入保持 0+TODO (c4 scope); bankroll 强类型化不在 c3 (c2b); cap 三数 = RM 主权终值, 实施不得改数。落地全绿 + diff 仅触 §1 audit 表 → 我复签。
