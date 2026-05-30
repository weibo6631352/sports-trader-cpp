# P0-2 c2b — bankroll / daily_loss_halt 强类型化 + DD 熔断零变 spec

- **owner:** 老韩 (风控工程师, RM 主权 + cap 真值 owner)
- **last_review:** 2026-05-30
- **status:** 决策已拍 (RM 主权直接定数 + DD 块改法钉死); 待 GM 落地 c2b 代码变更
- **scope:** 只读分析 + 决策文档。本文不改生产代码。
- **前置:** P0-2 c2/c3 已落地 (3 cap 字段已 MicroPUSD 强类型, 默认值已校准, 已 push)。c2b 是 RiskConfig **最后两个**未强类型金额字段 `bankroll_usdc` / `daily_loss_halt_usdc` 转 MicroPUSD。
- **触及 RM 主权域:** DD 熔断逻辑 (软/硬阈值) + INSUFFICIENT_BANKROLL 比较点 — 故须 RM owner spec, 不可由 IC 自决。

---

## 0. 治理 — R-4 红线 + §8.1 第 3 条 (粘原文, 禁转述)

c2b 是字段单位语义钉死 + 默认值变更 + 字段消费点单位审计, 触以下两条, 逐字粘:

> **§8 红线 (CLAUDE.md):** 「数据 schema 静默变更（不通知下游） → 责任人承担事故」

> **§8.1 第 3 条 (CLAUDE.md, 2026-05-30 立):** 「**ABI/字段单位变更必触发下游审计（补 R-4 配套）：** 任何字段重命名/单位变更（如 `size_usdc → size_pUSD_micro`）必须 audit 全部比较点/消费点的单位一致性，否则会「静默架空」依赖该字段的红线（caps/exposure/bankroll）。这类变更走 R-4（schema 静默变更红线）。」

> **§8.1 第 1 条:** 「红线/R-XX 引用必粘原文，禁转述。」— 本文所有阈值改法粘当前代码原文 (§3) 再给改法, 不凭记忆转述。

**c2b 合规要求:** bankroll/daily_loss 字段官方单位钉死后, 必须 audit 全部消费点 (本文 §2 表) 单位一致。bankroll 是 INSUFFICIENT_BANKROLL 红线 + DD 熔断分母的双重载体 — 任一消费点单位失配 = 静默架空 DD 熔断红线 = R-4 事故。本文即为该 audit + SSOT + DD 零变保证。

---

## 1. 当前现状 (名实不符现场, 与 cap 同病)

`RiskConfig.bankroll_usdc` / `daily_loss_halt_usdc` 字段名带 `_usdc`, 但 RM 实际拿 `size_pUSD_micro` (micro) / `daily_pnl_usdc_` (micro) **直接同型比** — 故**实际语义已是 micro**, 字段名是历史遗留谎言。

| 项 | 字段值 | 字段名暗示 | RM 实际当 | 后果 |
|---|---|---|---|---|
| `bankroll_usdc` 默认 | `100'000` | 100k pUSD | micro = **0.1 pUSD** | 默认荒谬 (同 cap 0.01pUSD bug) |
| `daily_loss_halt_usdc` 默认 | `5'000` | 5k pUSD | micro = **0.005 pUSD** | 默认硬熔断阈荒谬 |

**为何线上没炸:** 运行期所有真实路径都用 `whole × 1e6` 灌真 micro 覆盖默认值 —
- `paper_loop.cpp:142` `set_bankroll(cfg_.bankroll_usdc × 1'000'000.0)` (whole→micro)
- `paper_daemon.cpp:212` `paper_rm_cfg.bankroll_usdc = paper_loop.bankroll_usdc × kMicroPerPusd` (whole→micro, 直写 cfg 字段, ctor:152 原样灌 atomic)

默认值仅当"忘配 config"兜底时才暴露, 故 bug 潜伏。**c2b 把字段单位钉死 micro + 默认值校准成真 micro 真值, 消除潜伏。**

---

## 2. 全消费点 audit 表 (§8.1#3 强制)

bankroll = `bankroll_usdc_` (atomic, runtime 镜像); daily_loss_halt = `cfg_.daily_loss_halt_usdc` (ctor 起不可变)。

| # | 消费点 | 文件:行 | 当前单位处理 | c2b 后 | 正确性 |
|---|---|---|---|---|---|
| 1 | RM INSUFFICIENT_BANKROLL | risk_gateway.cpp:472-473 | `size_pUSD_micro > br` (br=atomic int64 micro) | 不变 | ✅ micro vs micro 自洽 |
| 2 | RM DD 硬阈 (绝对值分支) | risk_gateway.cpp:489-490 | `daily_loss_halt_usdc`(micro) 当 hard_threshold(micro) | 取 `.v` | ✅ micro vs micro (loss=−pnl micro) |
| 3 | RM DD 硬阈 (pct 分支) | risk_gateway.cpp:492 | `br(micro) × hard_pct(double)` → int64 micro | 不变 (br 仍 atomic int64) | ✅ |
| 4 | RM DD 软阈 | risk_gateway.cpp:496-497 | `br(micro) × soft_pct` → int64 micro | 不变 | ✅ |
| 5 | RM DD 升级 HALTED (evaluate 主路径) | risk_gateway.cpp:694-701 | 同 #2/#3 复用 | 同步改 (取 `.v`) | ✅ |
| 6 | ctor 灌 atomic | risk_gateway.cpp:152 | `bankroll_usdc_.store(cfg.bankroll_usdc)` int64←int64 | `.store(cfg.bankroll_usdc.v)` | ✅ |
| 7 | set_bankroll setter | risk_gateway.hpp:353 | `bankroll_usdc_.store(usdc)` int64 入参 | **保持 int64 raw micro** (见 §4 决策) | ✅ 调用方零改 |
| 8 | paper_loop 灌 bankroll | paper_loop.cpp:142 | `set_bankroll(whole × 1e6)` int64 micro | 不变 | ✅ 已 micro |
| 9 | paper_daemon 灌 bankroll | paper_daemon.cpp:212 | `cfg.bankroll_usdc = whole × kMicroPerPusd` int64 | **改 `MicroPUSD::from_pusd(whole)`** | ❌→✅ 写 .v=whole×1e6 同值 |
| 10 | RiskConfig 默认值 | risk_gateway.hpp:308/315 | `100'000` / `5'000` int64 (当 micro=0.1/0.005 pUSD) | **校准 (见 §3)** | ❌→✅ |
| 11 | 测试喂 bankroll/daily_loss | 见 §6 表 | 裸值/×1e6 混用 | **统一 (见 §6)** | ❌→✅ |

**核心病灶:** 与 cap 同型 —— 字段名 `_usdc` 谎报 whole, 实际全程 micro。c2b 钉死字段=`MicroPUSD`(micro), 默认值校准, daemon 灌值改 `from_pusd` 统一写法, 测试统一喂 micro。

---

## 3. RiskConfig 默认值校准 (RM 主权拍数)

### 3.1 锚定 (与 c3 同源)

c3 SSOT 已锚 **bankroll 语义意图 = 100'000 pUSD** (`laohan-p0-2-c3-cap-truth-ssot-v1.md` §2.2; cap 1%/2%/5% 即按此锚拍)。c2b 把这个"意图"落成真 micro 默认值, 与 c3 cap 默认同账户规模自洽 (cap 1e9/2e9/5e9 micro = 1k/2k/5k pUSD, 占 bankroll 1%/2%/5%)。

### 3.2 两默认值 micro 真值

| 字段 | 意图 (pUSD) | micro 真值 (`.v`) | 写法 | RM 占比校验 |
|---|---|---|---|---|
| `bankroll_usdc` | 100'000 pUSD | **`100'000'000'000`** (1e11) | `MicroPUSD{100'000'000'000}` 或 `domain::from_pusd(100'000.0)` | 锚, cap 占 1%/2%/5% 自洽 |
| `daily_loss_halt_usdc` | 5'000 pUSD | **`5'000'000'000`** (5e9) | `MicroPUSD{5'000'000'000}` | 5% bankroll = `hard_pct(0.05)×100k` 恰等, 绝对值分支与 pct 分支默认同值 (一致性自检 ✅) |

**daily_loss_halt 默认与 hard_pct 一致性 (RM 主权特别校验):** 默认 `daily_loss_halt_usdc=5'000 pUSD` 与 `daily_loss_hard_pct=0.05 × bankroll(100k)=5'000 pUSD` **数值恰好相等**。即默认 config 下"绝对值分支"(>0 覆盖) 与"pct 分支"产出同一硬熔断阈 5k pUSD。这是有意的: 默认行为不因 halt 字段 >0 而偏离 -5% 语义。**保留此等式 = 默认 config 行为零变。**

### 3.3 为何默认给真值而非 0

- 给 `0` → INSUFFICIENT_BANKROLL 比较 `size > 0` 恒真 → 任何 intent 全拒 (fail-closed 过头, 忘配 config 时静默全拒难 debug)。
- `daily_loss_halt_usdc=0` 是**合法且有意义**的值 (注释: ">0 才覆盖 hard_pct"), 故 daily_loss 默认**不能**简单给 0 否则丢"默认绝对硬熔断"语义 → 给 5e9 micro。
- 留旧荒谬 0.1/0.005 pUSD → fail-open 风格假象 (paper 用 ×1e6 覆盖看不出, 实盘忘配则瞬间触发 bankroll 拒 + 硬熔断, 难定位)。
- 给"100k/5k pUSD 真值" = 保守可用兜底, 配错也只是按 100k 账户的 1%/-5% 行事, 不爆仓不全拒。

---

## 4. atomic / setter 决策 (RM 主权一句话)

**决策: `bankroll_usdc_` 保持 `std::atomic<std::int64_t>`(裸 micro); `set_bankroll(std::int64_t)` 签名不变。** (老姜立场: 不要 `atomic<MicroPUSD>` — 采纳)

**理由 (RM 主权):**
1. `atomic<MicroPUSD>` 风险: MicroPUSD 虽 8 字节 standard-layout (ABI lock 已 static_assert), `atomic<MicroPUSD>` 理论可 lock-free, 但**热路径 RM 用 atomic 是为无锁 read; 包装自定义 struct 徒增 `is_trivially_copyable` / lock-free 平台不确定性**, 收益为零 (语义保护在编译边界, 不在 atomic 内)。
2. **调用方破坏面最小:** `set_bankroll(int64 micro)` 保持 → paper_loop:142 / paper_daemon(经字段) / consistency:119 全部已传 micro int64, **零改**。改 `MicroPUSD` 入参会强迫所有调用方包 `from_micro`/`from_pusd`, 破坏面无谓放大。
3. **ctor 边界转换 (消费点 6):** 字段 `cfg.bankroll_usdc` 现是 `MicroPUSD` → ctor 灌 atomic 改 `bankroll_usdc_.store(cfg.bankroll_usdc.v)` (取 `.v` 拿裸 micro)。这是**唯一**新增的 `.v` 解包点, 干净。
4. **类型安全落点:** 强类型保护落在 **RiskConfig 字段层** (配置构造期, 编译期可见单位), runtime atomic 层保持裸 micro 高速。这与 cap 的处理对称 (cap 字段 MicroPUSD, 比较点 `from_micro(size)>cap` runtime)。

---

## 5. DD 块精确改法 (零变保证) — RM 主权钉死

### 5.1 当前原文 (risk_gateway.cpp:486-497, 粘原文禁转述)

```cpp
        // 硬阈值: daily_loss_halt_usdc 旧字段绝对值 (>0 时覆盖 hard_pct)
        std::int64_t hard_threshold = 0;
        if (cfg_.daily_loss_halt_usdc > 0) {
            hard_threshold = cfg_.daily_loss_halt_usdc;
        } else {
            hard_threshold = static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_hard_pct);
        }
        // 软阈值: daily_loss_soft_pct × bankroll
        std::int64_t const soft_threshold =
            static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_soft_pct);
```

### 5.2 c2b 改法 (字节级零变)

字段 `cfg_.daily_loss_halt_usdc` 现是 `MicroPUSD` → 仅在**两处**解包取 `.v`, 其余逐字不动:

```cpp
        std::int64_t hard_threshold = 0;
        if (cfg_.daily_loss_halt_usdc.v > 0) {              // c2b: .v
            hard_threshold = cfg_.daily_loss_halt_usdc.v;   // c2b: .v
        } else {
            hard_threshold = static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_hard_pct);
        }
        std::int64_t const soft_threshold =
            static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_soft_pct);
```

**零变论证:** `daily_loss_halt_usdc.v` 拿的是与旧裸 int64 **同一 micro 整数** (默认值 5e9 与旧 5'000 不同, 但旧 5'000 本就是荒谬 bug; 真实路径 w76 测试喂 50'000 裸值也是 bug, 见 §6 修正)。`br` (atomic int64 micro) / `hard_pct` / `soft_pct` (double) **全不变**, 故熔断阈值**计算式字节级零变** —— c2b 只是把 `daily_loss_halt_usdc` 从"裸 int64"换成"MicroPUSD 取 .v", 同一整数同一运算。

### 5.3 evaluate 主路径 DD 升级块 (risk_gateway.cpp:697-701, 消费点 5) 同步改

```cpp
                std::int64_t hard_threshold =
                    (cfg_.daily_loss_halt_usdc.v > 0)        // c2b: .v
                        ? cfg_.daily_loss_halt_usdc.v        // c2b: .v
                        : static_cast<std::int64_t>(static_cast<double>(br) * cfg_.daily_loss_hard_pct);
```

**两处 DD 块 (check_position_caps_ 与 evaluate) 的 hard_threshold 逻辑必须同步改, 保持一致** (否则 reject 判定与 HALTED 升级判定阈值不一致 = RM 逻辑裂缝)。

### 5.4 老郭 A2 关注的 double 出口 (DD% / 利用率) — RM 立场

老郭 A2 关注"比值 double 出口"。**c2b DD 块内不产出 DD%/利用率 double** —— 当前实现是 `loss(micro int64) >= threshold(micro int64)` **纯整数比**, 不除不产 ratio。MicroPUSD 已定义 `operator/(MicroPUSD,MicroPUSD)->double` 作为唯一合法比值出口 (micro_pusd.hpp:96), **但 c2b 不引入它** —— DD 判定保持整数比 (无浮点误差, 阈值边界确定)。

**RM 主权决策: DD 判定保持整数域比较, 不改 ratio 出口。** 若未来 monitor/debug_snapshot 要显示 "当前 DD%", 那是**展示层** ratio (`MicroPUSD{loss}/bankroll_micro`), 与熔断判定解耦, 不在 c2b scope。c2b 守住"熔断判定 = 整数 micro 比, 零浮点漂移"。

---

## 6. 测试迁移 (守住"熔断阈值零变" + 消除潜伏 bug)

### 6.1 现状: 测试喂值混乱 (名实不符的直接证据)

| 测试:行 | 字段/调用 | 当前喂值 | 当 micro 实际语义 | 是否咬当前断言 |
|---|---|---|---|---|
| test_risk_gateway_wave3:69 | `cfg.bankroll_usdc` | `100'000` 裸 | 0.1 pUSD | 否 (size 小不触 bankroll; halt=0 走 pct, pnl/br 同裸比例对消) |
| test_risk_gateway_wave3:70 | `cfg.daily_loss_halt_usdc` | `0` | 0 (禁绝对值, 走 pct) | 否 (有意禁用) |
| test_position_ledger_w76:189/198 | bankroll | `1'000'000` 裸 + set ×1 | 1 pUSD | 否 (只测 state 机不触 DD/bankroll 数值) |
| test_position_ledger_w76:190/247 | daily_loss_halt | `50'000` 裸 | 0.05 pUSD | 否 (state 测试不触 DD) |
| test_rm_debug_snapshot:104/117 | bankroll | `1'000'000'000` + set 同 | 1000 pUSD | 已 micro ✅ |
| test_paper_loop:132 | `rm_cfg.bankroll_usdc` | `1'000'000'000` | 1000 pUSD (注释明确 micro) | 已 micro ✅ |
| test_sizing_rm_consistency:119 | `set_bankroll` | `bankroll × 1'000'000` | 真 micro ✅ | 是 (consistency 核心) |

**结论: 裸值测试 (wave3/w76) 侥幸不咬 = 潜伏 bug, c2b 必须连同修正**, 否则字段改 MicroPUSD 后这些裸值会变成 `MicroPUSD{100'000}` = 0.1 pUSD, 编译过但语义仍荒谬 (只是从"裸 int64 荒谬"变"MicroPUSD 荒谬", 没真修)。

### 6.2 测试迁移规则 (RM 主权)

**原则: 所有测试喂 bankroll/daily_loss 一律用 `MicroPUSD::from_pusd(whole)` 或显式 micro, 语义化, 消除裸值。**

| 测试:行 | 改法 | 守住什么 |
|---|---|---|
| wave3:69 | `cfg.bankroll_usdc = MicroPUSD::from_pusd(100'000.0)` (.v=1e11); **必须同步加 `gw.set_bankroll(100'000'000'000LL)`** | wave3 当前**没 set_bankroll**! ctor 灌 1e11 后 pct 路径 `br×0.05`=5e9 micro, 测试若用真实 loss 需重算。**见 §6.3 wave3 专项** |
| wave3:70 | `cfg.daily_loss_halt_usdc = domain::MicroPUSD{0}` | 保持禁用绝对值走 pct, 语义零变 |
| w76:189/243 | `MicroPUSD::from_pusd(1'000'000.0)` + set 对齐 | state 测试, 不触 DD, 改后仍不咬 (安全) |
| w76:190/247 | `MicroPUSD::from_pusd(50'000.0)` (.v=5e10) | 同上, state 测试不触 DD |
| rm_debug_snapshot:104 | `MicroPUSD::from_micro(1'000'000'000)` (已 micro, 保值) | 保持 1000 pUSD |
| rm_debug_snapshot:105 | `MicroPUSD{0}` | 保持禁用 |
| paper_loop:132 | `MicroPUSD::from_micro(1'000'000'000)` (保值) | 保持 |
| consistency:107 | `cfg.bankroll_usdc = MicroPUSD::from_pusd(bankroll_usdc_whole)` ; :119 `set_bankroll` 保持 `whole × 1e6` | consistency 核心: cfg 字段与 set 值同 micro |

### 6.3 wave3 DD 测试专项 (RM 主权重点核对 — 熔断阈值零变的命门)

wave3:69 `bankroll=100'000` 裸 + **无 set_bankroll** → ctor 灌 atomic `br=100'000` (旧裸=micro 0.1pUSD)。其 DD 测试 (line 153/174/201 期望 DAILY_LOSS_HALT) 必然用了**与这个荒谬 br 成比例的 daily_pnl**, 所以 pct 路径 `loss >= br × 0.05` **在荒谬 micro 域内自洽** (双方都 ×1e-6 比例不变), 侥幸过。

**c2b 改后**: cfg 字段改 `from_pusd(100'000)` → ctor 灌 `br=1e11` micro。**若 wave3 的 set_daily_pnl 喂值不同步 ×1e6, 阈值就破 (loss 远小于 5e9 threshold → 不触发 → 测试红)。**

**强制要求 (派单时必传 GM):**
- wave3 测试**全部 set_daily_pnl 喂值同步乘 1e6 (whole pUSD → micro)**, 与 bankroll 同域。
- 改后逐条核对: 软阈 `1e11×0.03=3e9` micro (3k pUSD), 硬阈 `1e11×0.05=5e9` micro (5k pUSD)。测试 daily_pnl 设 `-3'500 pUSD` 即 `-3'500'000'000` micro → 落软区拒新开/放平仓; 设 `-5'500 pUSD` → 落硬区 HALTED。
- **验收门: 改前 ctest 三条 DD 测试 (line 153/174/201) 绿, 改后仍绿, 且阈值边界值 (3k/5k pUSD) 与改前等价语义。** 不允许"为了让测试过而调 pnl 喂值凑" —— 必须按"软=3% 硬=5% of 100k bankroll"重算, 阈值零变是数学等式不是凑数。

---

## 7. 执行摘要 (GM 落地用)

### ① 两默认值 micro 数
- `bankroll_usdc` 默认 = **`100'000'000'000`** (1e11 micro = 100k pUSD)
- `daily_loss_halt_usdc` 默认 = **`5'000'000'000`** (5e9 micro = 5k pUSD); 与 `hard_pct 0.05 × 100k = 5k` 恰等 (有意, 默认行为零变)

### ② atomic / setter 决策 (一句话)
`bankroll_usdc_` 保持 `atomic<int64_t>` 裸 micro, `set_bankroll(int64)` 签名不变 (调用方零改); 唯一新增解包是 ctor `cfg.bankroll_usdc.v` 灌 atomic。

### ③ DD 块改法 (零变保证)
两处 DD 块 (risk_gateway.cpp:489-490 + 697-700) 把 `cfg_.daily_loss_halt_usdc` → `cfg_.daily_loss_halt_usdc.v`, **其余逐字不动**; `br`/`hard_pct`/`soft_pct`/整数比全不变 → 熔断阈值计算式字节级零变。DD 判定保持整数 micro 比, **不引入 ratio double 出口** (老郭 A2: 比值出口归展示层, 与判定解耦, 不在 c2b)。

### ④ 可否直接派 GM 落地 / RM 逻辑风险点
**可直接派 GM 落地**, 但**两个 RM 风险点必须写进派单 prompt 当硬验收门:**

1. **【命门】wave3 DD 测试 daily_pnl 必须同步 ×1e6** (§6.3): bankroll 默认从荒谬 0.1pUSD 校准成真 1e11 micro 后, DD 测试的 pnl 喂值若不同步进 micro 域, 阈值判定全破。必须按"软 3% / 硬 5% of 100k"重算阈值 (3k/5k pUSD), 阈值是数学等式零变, 禁凑数。wave3 当前**无 set_bankroll**, 需补。
2. **【一致性】两处 DD 块 hard_threshold 逻辑同步改** (§5.3): check_position_caps_ 的 reject 判定 与 evaluate 的 HALTED 升级判定共用 hard_threshold 公式, 两处必须同步取 `.v`, 否则 reject 与 HALTED 升级阈值不一致 = RM 逻辑裂缝 (会出现"拒了但没 HALT"或反之的诡异态)。
3. **paper_daemon.cpp:212 灌值改 `from_pusd` 统一写法** (消费点 9): 当前 `whole × kMicroPerPusd` 数值对, 但 c2b 后字段是 MicroPUSD, 须改 `paper_rm_cfg.bankroll_usdc = MicroPUSD::from_pusd(paper_loop.bankroll_usdc)`, 与 cap 的 from_pusd 写法对称。

**无其他 RM 逻辑风险。** bankroll 比较点 (INSUFFICIENT_BANKROLL, :472-473) micro vs micro 本就自洽, c2b 不动它。整个 c2b 的风险全集中在"默认值校准触发 wave3 测试 pnl 需同步"这一处, 守住 §6.3 即安全。

### ⑤ 落地顺序 (建议)
1. 改字段类型 + 默认值 (hpp:308/315) → 2. ctor `.v` (cpp:152) → 3. 两处 DD 块 `.v` (cpp:489-490/697-700) → 4. paper_daemon from_pusd (cpp:212) → 5. 测试迁移 (§6.2 表 + §6.3 wave3 专项) → 6. ctest 全绿 + DD 三条阈值边界等价核对 → 7. commit。
**set_bankroll / paper_loop:142 / consistency:119 零改** (已 micro int64)。
