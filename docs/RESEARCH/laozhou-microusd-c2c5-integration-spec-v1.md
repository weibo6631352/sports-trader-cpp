# MicroPUSD 单位契约集成 spec — c2-c5 (根治 P0-2 单位失配)

- **owner:** 老周 (系统工程部主管 + 架构主权)
- **last_review:** 2026-05-30
- **status:** 落地 spec (IC 可直接实施); 老韩 (RM 主权) review cap 真值 + exposure 喂数 + 兜底语义后开工
- **前置:** c1 = MicroPUSD 强类型已落地 (commit d28def8); P0-2 第一刀 = clamp 遮羞布拆 + paper 侧 pUSD 单一真值源 (commit 9daad2f)
- **上游:** `docs/MEETINGS/2026-05-30-arch-debt-audit.md` §P0-2 (含老韩 c2-c5 清单)
- **配套 c1 spec:** `docs/RESEARCH/laozhou-microusd-unit-contract-spec-v1.md`
- **关联红线:** R-4 (schema 静默变更) / §8.1 第 3 条 (ABI/单位变更触发下游审计) / 铁律#3 (回测=实盘同逻辑) / 红线 §8「下单链路绕过 RiskManager」
- **关联 ADR:** 落 ADR-041 (本 spec; ADR-040 = c1 typedef, 见 c1 spec §5)

---

## 0. TL;DR (派单方看这段)

**根因复述 (粘原文, 禁转述 — §8.1 第 1 条):**

> `docs/MEETINGS/2026-05-30-arch-debt-audit.md` §P0-2 原文:
> 「同一个 `RiskConfig.per_order_cap_usdc` 被两条路当两种单位: paper_daemon→RM 当 **micro** (10'000'000); paper_loop sizing 用默认 `RiskConfig{}` (10'000) → SizingCalculator 当 **pUSD** (`sizing_calculator.cpp:175`)。差 1000 倍。」
> 「`RiskConfig` 字段名叫 `_usdc` 值是 micro (`risk_gateway.hpp:298`) — 名实不符。」

**当前态 (commit 9daad2f 后) 实测确认:**
- `sizing_calculator.cpp:175` `cap1_limit = static_cast<double>(cfg.per_order_cap_usdc)` —— sizing **把 cap 字段当 whole pUSD**(无 ÷1e6)。
- `risk_gateway.cpp:432` `it.size_pUSD_micro > cfg_.per_order_cap_usdc` —— RM **把同字段当 micro**。
- **两条路对同一字段类型解读差 1e6**,9daad2f 靠「paper 喂 sizing 的是 pUSD cap (10/50/25)、喂 RM 的是 ×1e6 micro cap」让两边各自自洽,但**字段本身的单位语义仍是分裂的** —— 任何复用 RiskConfig 的新调用方(backtest / live / 第二个 daemon)只要不知道这套「双喂」约定就会再踩 1e6。

**推荐方案 (一句话):** **RiskConfig 4 个金额字段改强类型 `MicroPUSD`(方案 A),不是改名 `_micro`(方案 B)** —— 改名只让人「看见」单位,强类型让漏乘/错配「编译不过」,且 c1 typedef 已落地、ABI 零变(sizeof==8),增量成本主要在测试 fixture 字面量迁移,类型安全收益 >> 工期成本。取舍见 §3。

**c2-c5 一句话各是什么:**
- **c2 = RiskConfig 字段名实统一**:4 个 cap 字段 `int64_t → MicroPUSD`,RM 4 比较点 + setter + ctor 改显式 MicroPUSD 用法。**唯一改单位语义的步,老韩签 cap 真值。**
- **c3 = 消 int64 截断 + sizing 单位归一**:SizingCalculator 接口改吃 `MicroPUSD` cap(不再 `static_cast<double>(cfg.per_order_cap_usdc)` 当 whole pUSD),pUSD→micro 转换收敛到单一出口,删 `paper_loop.cpp:413-415` 的 `(int64)` 截断。
- **c4 = exposure 累加路径对齐**:决定 paper 是否回写 RM exposure;若回写,sizing 的 `current_*_exposure` 必须从 RM 真值取,消除「exposure 恒 0」假闭环。**本 spec 推荐 c4 拆成 c4a(喂数管道,与 P0-1 合流)+ c4b(sizing 读真值),c4b 依赖 c4a。**
- **c5 = 最小兜底 clamp 到 cap + 删桥收尾 + CI 守护**:`size<=0` 兜底改 `min(1pUSD, per_order_cap)`;删 c1 隐式桥(若 c2-c4 用桥);上 CI grep 禁裸 `×1'000'000`。

**最大风险:** c2 改 cap 字段单位语义 = R-4 静默变更红线高发区。risk_gateway.hpp 是 ABI-locked 文件(`abi_lock.py` 锁),改 RiskConfig 字段类型属 **L2**(改字段类型),需**老李 + 老孙 + GM 三方签**(handshake §1 表)。若漏审任一构造/消费点,会像上次一样静默架空 cap 红线。

**需谁签:** 老韩 (RM 主权,cap 真值 + exposure 喂数语义 + 兜底) / 老郭 (架构评审,ADR-041) / **L2 三方签 = 老李 + 老孙 + GM**(abi_lock Rule 2,risk_gateway.hpp 改字段类型) / 老高 (CI grep 守护) / 老唐 (audit_record.size_pUSD_micro 是否随动,见 §6 注)。

---

## 1. 全库单位审计清单 (R-4 配套 — §8.1 第 3 条强制)

> **红线原文 (粘,禁转述 — §8.1 第 1 条):** CLAUDE.md §8.1 第 3 条:
> 「ABI/字段单位变更必触发下游审计(补 R-4 配套):任何字段重命名/单位变更(如 `size_usdc → size_pUSD_micro`)必须 audit 全部比较点/消费点的单位一致性,否则会「静默架空」依赖该字段的红线(caps/exposure/bankroll)。这类变更走 R-4(schema 静默变更红线)。」
>
> **R-4 原文 (粘):** CLAUDE.md §8:「数据 schema 静默变更(不通知下游) → 责任人承担事故」。

### 1.1 RiskConfig 4 个 cap 字段 + bankroll/halt — 构造点 × 单位

| # | 位置 | 字段 | 当前单位语义 | c2 后单位 | 改动 |
|---|---|---|---|---|---|
| C-01 | `risk_gateway.hpp:298-301,308` | `per_order/market_exposure/per_outcome_cap_usdc` + `bankroll_usdc` + `daily_loss_halt_usdc` (默认值) | **名 usdc 实 micro**(RM 消费按 micro);默认 10'000/50'000/25'000/100'000/5'000 | `MicroPUSD`;默认值改 `from_micro(...)` 或 `_upusd`(数值不变, 仍是 micro raw) | c2 |
| C-02 | `paper_daemon.cpp:206-212` | `paper_rm_cfg.*` = `cfg_.paper_loop.* × kMicroPerPusd` | 显式 pUSD×1e6→micro 派生 (正确) | `= MicroPUSD::from_pusd(cfg_.paper_loop.*)` | c2/c3 |
| C-03 | `paper_daemon.cpp:234` | `rsp_risk_cfg` | (default RiskConfig, real_state_provider 未用) | 类型随动, 值不变 | c2 |
| C-04 | `paper_loop.cpp:412-415` | `sizing_cfg.* = (int64)(cfg_.paper_loop.*)` | **截断 pUSD→int64 pUSD**(7.7→7);sizing 当 whole pUSD 用 | **删** — sizing 直接吃 `MicroPUSD`(同 RM cap 真值源),见 c3 §4.2 | c3 |
| C-05 | `real_state_provider.hpp:141,574` | `risk_cfg = {}` (参数兼容, 未使用) | 默认 | 类型随动, 无逻辑 | c2 |
| C-06 | `test_risk_gateway.cpp:94-98,663,758` | `cfg_.per_order_cap_usdc = 10'000` 等 | 测试当 micro(配 size 1'000=micro) | `= 10'000_upusd` 或按老韩真值 | c2 测试迁移 |
| C-07 | `test_risk_gateway_wave3.cpp:65` | `RiskConfig{}` | 默认 | 默认随动 | c2 测试 |
| C-08 | `test_sizing_calculator.cpp:36-43` | `make_default_cfg()` 用默认 10'000 等, **当 whole pUSD**(input bankroll=100'000.0 pUSD, exposure=20'000.0 pUSD 对齐 cap=25'000) | sizing cap 改 MicroPUSD → 测试须重定 cap 真值与 input 同尺度 | c3 测试迁移(**最大迁移面**, 见 §5) |
| C-09 | `test_sizing_rm_consistency.cpp:92-94` | `RiskConfig cfg` + size micro | RM/sizing 跨验, 单位双轨正是它要锁的 | 改后此测试是 c2/c3 主回归门(见 §6) | c2/c3 测试 |
| C-10 | `test_position_ledger_w76.cpp:185,241` | `RiskConfig cfg` | 默认 | 随动 | c2 测试 |
| C-11 | `test_rm_debug_snapshot.cpp:99` | `RiskConfig cfg{}` | 默认 + size 10'000'001 micro 触 per_order | 随动, size 字面量改 | c2 测试 |
| C-12 | `test_strategy_unlock.cpp:139` / `test_coverage_metrics.cpp:105` / `test_p1_market_metrics_wss.cpp:96` | `RiskConfig{}` | 默认 | 随动 | c2 测试 |
| C-13 | `test_fixture.hpp:218-222` | `rcfg.* = 10'000` 等 + `it.size_pUSD_micro=100` | 当 micro | `_upusd` / 真值 | c2 测试 |
| C-14 | `bench_risk_gateway.cpp:69-74` / `bench_e2e_latency.cpp:82-86,193-195` | `c.* = 10'000` 等 | 当 micro | `_upusd` | c2 perf |

### 1.2 cap/bankroll 字段消费点 (RM evaluate + sizing) × 单位

| # | 位置 | 消费 | 当前单位假设 | c2/c3 后 |
|---|---|---|---|---|
| K-01 | `risk_gateway.cpp:432` | `it.size_pUSD_micro > cfg_.per_order_cap_usdc` | 两侧 micro(size 是 micro, cap 实 micro) | `it.size_pUSD_micro > cfg_.per_order_cap_usdc`(两侧 MicroPUSD, 直接 `>`) |
| K-02 | `risk_gateway.cpp:452` | `cur + it.size_pUSD_micro > cfg_.market_exposure_cap_usdc` | `cur`=map int64 micro | `MicroPUSD::from_micro(cur) + it.size > cfg_.market_exposure_cap_usdc` |
| K-03 | `risk_gateway.cpp:465` | `cur_tok + it.size > cfg_.per_outcome_cap_usdc` | 同 K-02 | 同 K-02 |
| K-04 | `risk_gateway.cpp:473` | `it.size_pUSD_micro > br`(`br`=bankroll atomic int64 micro) | micro | `it.size > MicroPUSD::from_micro(br)` |
| K-05 | `risk_gateway.cpp:489-497` | `daily_loss_halt_usdc` / `hard_pct × br` / `soft_pct × br` | micro(loss 与 br 同 micro 才能比) | 见 §4.4 DD 块 (老韩定 double 中转 or 纯 micro) |
| K-06 | `risk_gateway.cpp:152` | `bankroll_usdc_.store(cfg.bankroll_usdc)` | 存 micro raw | `.store(cfg.bankroll_usdc.v)` |
| K-07 | `risk_gateway.cpp:472,694` | `bankroll_usdc_.load()` → `br` | atomic int64 micro(不变) | atomic 仍 int64, 比较点包 `from_micro` |
| K-08 | **`sizing_calculator.cpp:175`** | `cap1_limit = static_cast<double>(cfg.per_order_cap_usdc)` | **当 whole pUSD**(bug 现场) | **c3: `cap1_limit = cfg.per_order_cap_usdc.to_pusd()`** — 显式转 pUSD, 单位钉死 |
| K-09 | **`sizing_calculator.cpp:187`** | `cap2_limit = static_cast<double>(cfg.per_outcome_cap_usdc)` | 当 whole pUSD | `cfg.per_outcome_cap_usdc.to_pusd()` |
| K-10 | **`sizing_calculator.cpp:198`** | `cap3_limit = static_cast<double>(cfg.market_exposure_cap_usdc)` | 当 whole pUSD | `cfg.market_exposure_cap_usdc.to_pusd()` |
| K-11 | `sizing_calculator.cpp:168,210` | `in.bankroll_usdc`(SizingInput, **double pUSD**, 运行期值) | pUSD(独立于 cfg.bankroll) | **不动** — SizingInput.bankroll_usdc 是运行期 double pUSD(老韩 §1.3 防 drawdown 漂移),不进契约 |
| K-12 | `sizing_calculator.cpp:188,199` | `in.current_token/condition_exposure_usdc`(double pUSD) | pUSD | c4: 若 paper 回写真值, 这里入参单位须与 cap2/3(to_pusd 后 pUSD)对齐, 仍 double pUSD |

### 1.3 size_pUSD_micro 透传链 (边界② — 不改类型, 仅确认单位一致)

| 位置 | 消费 | 单位 | c2-c5 动作 |
|---|---|---|---|
| `paper_loop.cpp:488` | `intent.size_pUSD_micro = (int64)(notional_usdc × 1e6)` | sizing 出 pUSD → micro | c3: 改 `MicroPUSD::from_pusd(notional_usdc)`(若边界②也上 typedef);否则保留但收敛常量 |
| `paper_loop.cpp:528` | `sign_req.size_pUSD_micro = intent.size_pUSD_micro` | micro 透传 | 不变 |
| `paper_loop.cpp:554` | `vord.size_usdc = (double)sign_req.size_pUSD_micro / 1e6` | micro→pUSD 展示 | 不变(或 to_pusd 若上 typedef) |
| `transformer_v62.hpp:162` | `req.size_pUSD_micro = intent.size_pUSD_micro` | micro 透传 | 不变(取 `.v` 若上 typedef) |
| `audit_emitter.cpp:86` / `audit_record.hpp:171` | `r.size_pUSD_micro = in.size_pUSD_micro` | micro | **不变** — audit 是 int64 micro raw(见 §6 注: c1 spec 误判 audit 无 size 字段, 实有) |
| `rm_debug_snapshot.hpp:228` | `row.size_usdc = (double)intent.size_pUSD_micro / 1e6` | micro→pUSD | 不变 |
| `check_liquidity_` (`risk_gateway.cpp:524-528`) | `order_size_usdc = (double)it.size_pUSD_micro` vs `book_depth_l1_usdc` | **隐藏耦合**: size 是 micro raw double, book_depth paper 侧 ×1e6 成 micro(`paper_loop.cpp:496`) | 见 §4.5 — book_depth 单位切口, 老韩 review |

**审计结论:** RiskConfig 4 cap 字段是单位分裂的唯一发源地。**K-08/K-09/K-10(sizing 把 cap 当 whole pUSD)是 c1 spec 没覆盖的真正第二现场** —— c1 只锁了 RM 入口(边界①),没锁 sizing 对 cap 的解读。c3 必须把这三点纳入,否则名实统一只统一了 RM 一侧,sizing 仍当 pUSD,bug 半关闭。

---

## 2. c2-c5 拆分 (可独立 commit + ABI 影响 + 测试门)

### c2 — RiskConfig 字段名实统一 (改 MicroPUSD)

**做什么:**
- `risk_gateway.hpp:298-301,308` 5 字段 `int64_t → MicroPUSD`(`per_order` / `market_exposure` / `per_outcome` / `bankroll` / `daily_loss_halt`)。默认值改 `_upusd`(micro raw 不变,数值语义零变)。
- RM setter `set_bankroll(MicroPUSD)`(`risk_gateway.hpp:346`),`bankroll_usdc_.store(.v)`(`risk_gateway.cpp:152`)。
- RM 4 比较点 K-01~K-04 改显式 MicroPUSD(`from_micro(cur)` 包 map raw,size 已是 MicroPUSD 若边界②同步;若边界②本 commit 不动,size_pUSD_micro 仍 int64 → 比较点用 `MicroPUSD::from_micro(it.size_pUSD_micro)`)。
- DD 块 K-05 按老韩 §4.4 定 double 中转 or 纯 micro。
- setter 形参 `set_*_exposure(... , MicroPUSD)`(:339-342)。

**ABI 影响:** risk_gateway.hpp 是 `abi_lock.py` `ABI_LOCKED_FILES`。RiskConfig **不在** `ABI_LOCKED_STRUCT_KEYWORDS`(只锁 OrderIntent/Position/SignV52Request/Side/Outcome/PositionKey),但改 locked 文件触发 **Rule 1**(PR 必含 `ABI ref` 行)。改字段类型 = handshake §1 表 **L2**(改字段类型/大小)。MicroPUSD sizeof==8==int64 → RiskConfig 总 sizeof 不变、字段偏移不变,**物理 ABI 零破**;但「字段类型变」按等级表名义仍是 L2 → **Rule 2 需「三方签」**(L2 = 老李+老孙+GM)。**注:RiskConfig 非 wire/IPC struct(不跨进程、不入 WAL),L2 判定偏严,可在 ADR-041 申请老郭裁定降 L1**(POD 末尾不动、仅 typedef 包装、sizeof 不变)。**老周建议:走 L1 + 老郭单签 + 老韩联签,理由 ABI 物理零变 + 非 IPC;若老郭坚持 L2 则三方签。**
- `ABI ref` 行:`ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-NN L1`(F 编号取最近,或 ADR-041 自身锚)。

**可独立 commit 边界:** c2 自成一 commit(RM 域闭环)。若边界②(size_pUSD_micro)同步上 typedef 会牵 signer/audit/transformer 一大片 → **c2 不碰 size_pUSD_micro 类型**,只在 RM 比较点用 `from_micro` 把 int64 size 包成 MicroPUSD 比 cap。边界②留 c1 spec 的边界②工作(独立排期,非 P0-2 必需)。

**测试门:** `test_risk_gateway*` / `test_sizing_rm_consistency`(C-06/07/09/11/13) fixture cap 字面量过 `_upusd`,size 字面量不变(仍 micro int64)。全量 ctest 绿。**数值零变**(默认 10'000 micro = `10'000_upusd`,RM 行为字节级一致)。

### c3 — 消 int64 截断 + sizing cap 单位归一

**做什么:**
- `sizing_calculator.hpp:140` `compute(RiskConfig const&, ...)` 签名不变(仍吃 RiskConfig);**但 cap 读取改 `to_pusd()`**:K-08/09/10 `static_cast<double>(cfg.X)` → `cfg.X.to_pusd()`。这是把「sizing 当 cap 是 whole pUSD」的隐含约定**显式化为 micro→pUSD 转换**,单位钉死。
- 删 `paper_loop.cpp:412-415`:不再构造独立 `sizing_cfg` + `(int64)` 截断。**sizing 与 RM 用同一个 RiskConfig 实例**(paper_daemon 已建 `paper_rm_cfg`,paper_loop 持有同源)。pUSD→micro 转换从此**只剩 paper_daemon ×1e6 一处**(C-02),sizing 内部 `to_pusd()` 转回 pUSD 算,RM 直接 micro 比 —— 全链单一真值源。
- `SizingInput.bankroll_usdc`(K-11) + `current_*_exposure_usdc`(K-12)保持 double pUSD(运行期值,不进 cfg)。

**截断消除证明:** 老韩挖出原文(粘):「`sizing_cfg.per_order_cap_usdc = (int64)cfg_.per_order_cap_usdc` 把 pUSD 小数 cap 截整(10.0/50.0/25.0 无损;7.7→7 偏小)」。c3 后 sizing 读 `MicroPUSD.to_pusd()` = `7'700'000 / 1e6 = 7.7` 无截断,cap 经济含义精确。

**ABI 影响:** sizing_calculator.hpp **不在** ABI_LOCKED_FILES,compute 签名不变 → 无 ABI 门。纯实现层改。

**可独立 commit 边界:** c3 依赖 c2(cap 已是 MicroPUSD 才能 `.to_pusd()`)。c3 自成 commit。

**测试门:** **`test_sizing_calculator.cpp` 是最大迁移面**(C-08)——当前 cap=10'000 当 whole pUSD、input exposure=20'000.0 pUSD 对齐 cap=25'000。c3 后 cap 是 MicroPUSD,`make_default_cfg()` 须把 cap 设成 `25'000_upusd`(=0.025 pUSD)还是 `25'000.0_pusd`(=25000 pUSD)取决于测试意图。**老韩裁定:test_sizing 的 cap 真值改为 demo cap 真值(per_order=10 pUSD → `10.0_pusd`),input exposure/bankroll 同步降到 pUSD 尺度(exposure 0~headroom 内)**。`test_sizing_rm_consistency`(C-09)是跨 RM/sizing 一致性门,c3 后必须用同一 RiskConfig 实例喂两边,验「sizing 不超 cap → RM 不拒」(§6 回归门)。

### c4 — exposure 累加路径对齐 (拆 c4a + c4b)

**问题原文 (粘,老韩挖出):**
> `docs/MEETINGS/2026-05-30-arch-debt-audit.md` §P0-2:「per_outcome/condition 闭环靠的是「exposure 恒 0」不是单位推理:paper 全程不调 RM exposure setter(RM 侧 `cur` 恒 0),sizing 的 `current_*_exposure_usdc` 也硬编码 0(paper_loop:407-408)。累加比较退化成单笔。c2-c5 若让 paper 回写 exposure,sizing 必须从 RM 真实 exposure 取值,否则第 2 笔后 per_outcome/condition 双轨偶发拒且 sizing 无感。」

**实测确认:** `set_condition_exposure`/`set_outcome_exposure`/`set_market_exposure` 在 `src/` 全库**仅 risk_gateway.cpp 内定义,零生产调用**(只在 tests/perf 调)。`paper_loop.cpp:407-408` sizing exposure 硬编码 0。→ per_condition/per_outcome cap **从未真正约束过累加**。

**做什么 (拆两步):**
- **c4a — exposure 喂数管道(与 P0-1 合流):** paper 成交后从 PositionLedger 取 per-token/per-condition 累计敞口,`set_outcome_exposure(token_id, MicroPUSD)` / `set_condition_exposure(condition_id, MicroPUSD)` 回写 RM。**这是 P0-1「RM 无生产喂数」的一部分,owner 老韩定喂数契约 + 老周定数据流向(债务表 §P0-1 已派)。** 单位:PositionLedger 出口 `from_pusd` 或 MicroPUSD(边界③),RM setter 吃 MicroPUSD(c2 已改)。
- **c4b — sizing 读 RM 真值:** `paper_loop.cpp:407-408` 不再硬编码 0,从 RM(或 PositionLedger 同源快照)取 `current_token/condition_exposure`,转 double pUSD 填 `SizingInput.current_*_exposure_usdc`(K-12)。**sizing headroom = cap.to_pusd() − current_exposure.to_pusd()**,与 RM `cur + size > cap`(micro)同源。

**ABI 影响:** setter 形参类型 c2 已改;c4 无新 ABI。c4b 是 paper_loop 实现层。

**可独立 commit 边界:** c4a 依赖 c2(setter 吃 MicroPUSD)+ PositionLedger exposure 出口(边界③,可能需 c1 边界③先落)。c4b 依赖 c4a。**风险高,见 §7 —— 老周建议 c4 与 P0-1 喂数管道统一排期,不在 c2/c3 commit 内,标记为 P0-2 的 P0-1 依赖项。** 若 P0-1 喂数管道未就绪,c4 的「正确」做法是**保持 exposure=0 但在 sizing/RM 双侧用同一个「0」常量 + 显式 TODO(P0-1)**,不引入半吊子单边回写(单边回写比恒 0 更危险:RM 有 exposure、sizing 无感 → 第 2 笔偶发拒)。

**测试门:** c4a 加 exposure 喂数 → RM 第 2 笔累加测试(`test_risk_gateway` 已有 K-02/03 累加用例,补 paper 集成)。c4b 加 sizing headroom 随 exposure 变化测试。**铁律#3 门:同一笔在 paper 与 backtest 走同 exposure 逻辑。**

### c5 — 最小兜底 clamp + 删桥 + CI 守护

**兜底问题原文 (粘,老韩挖出):**
> §P0-2:「最小 1 pUSD 兜底可被 RM 拒(sub-1-pUSD cap 下):paper_loop `size<=0 → 1'000'000` 兜底;若 cap < 1 pUSD(如 per_order=0.1 → RM cap=100000 micro),兜底写 1 pUSD = 1000000 micro `> 100000` → RM 拒。c2-c5 兜底应 clamp 到 `min(1pUSD, per_order_cap)`。」

**做什么:**
- `paper_loop.cpp:489-490` 兜底改:
  ```cpp
  // c5: 兜底不得超 per_order cap (sub-1-pUSD cap 下 1 pUSD 会被 RM 拒)
  if (intent.size_pUSD_micro <= 0) {
      const MicroPUSD floor = std::min(1.0_pusd, cfg_.per_order_cap_usdc);
      intent.size_pUSD_micro = floor;   // 若边界② typedef; 否则 floor.v
  }
  ```
  注:`std::min(MicroPUSD, MicroPUSD)` 走 c1 的 `<=>`(c1 header 已有 `operator<=>` default)。`cfg_.per_order_cap_usdc` = c2 后 MicroPUSD。
- 若 c2-c4 用了 c1 的隐式桥(脚手架),c5 删桥 + 恢复 explicit + 编译错清零(抓漏,c1 spec §3 Step 4 同款)。**若 c2-c4 全程显式用法(`from_micro`/`.v`/`.to_pusd`)未用桥,则 c5 无删桥步。** 老周倾向 **c2-c4 直接显式、不用桥**(改动面已收敛在 RiskConfig + sizing,无需全库桥),c5 只做兜底 + CI。
- **CI grep 守护(老高):** 加规则禁 `risk_gateway.cpp` / `sizing_calculator.cpp` / `paper_loop.cpp` 出现裸 `* 1'000'000` / `static_cast<double>(cfg.*cap*)`(应走 `to_pusd`/`from_pusd`/`from_micro`)。paper_daemon ×1e6 的**唯一合法点**白名单(或改 `from_pusd`)。

**ABI 影响:** 无(paper_loop + sizing 实现层 + CI 脚本)。

**测试门:** sub-1-pUSD cap 兜底测试(cap=0.5 pUSD,size→0 兜底 → 不超 cap → RM 不拒)。CI grep 自测。

---

## 3. 推荐方案:MicroPUSD 强类型 (方案 A) vs 改名 `_micro` (方案 B)

| 维度 | 方案 A — 改 `MicroPUSD` 强类型 | 方案 B — 改名 `per_order_cap_micro` |
|---|---|---|
| **ABI 物理破坏** | 零(sizeof==8==int64,偏移不变) | 零(int64 不变,仅名字) |
| **ABI 等级(名义)** | L2「改字段类型」(可申请降 L1, 见 c2) | L2「改字段名」≈ L3「改名」? handshake §1 表无「字段改名」专档,按「改类型/顺序」类比 L2 |
| **单位错配防护** | **编译期**:漏乘/whole↔micro 错配 = 类型不匹配编译错(`to_pusd`/`from_micro` 强制显式) | **仅命名提示**:`_micro` 让人「看见」,但 `int64` 仍可裸赋裸比,新调用方照样能错配(改名不改防护本质) |
| **K-08/09/10 (sizing 当 whole pUSD) 修复** | `cfg.X.to_pusd()` 编译强制,漏改 = 编译错 | 改名后 sizing 仍 `static_cast<double>(cfg.X_micro)` 当 pUSD,**改名不阻止**该 bug,需人肉审 |
| **测试迁移量** | fixture 字面量 → `_upusd`/`_pusd`(C-06~14, ~10 文件);test_sizing cap 真值重定(C-08 最大) | fixture 字段名批量 sed 改名(机械,但 test_sizing 单位语义仍需手审) |
| **类型安全收益** | 高:MicroPUSD 量纲钉死,exposure 累加/cap 比较/sizing 全程类型守 | 低:命名卫生,无编译期防护,下次 size 改单位仍会静默架空 |
| **c1 复用** | **直接复用已落地 typedef(d28def8),零新增类型成本** | 不用 c1 成果,c1 变孤儿(债务表已批评「零接入孤儿」) |
| **工期** | c2~3 各 1 commit + 测试迁移 ~1 天;c4/c5 依 P0-1 | 略快(sed 改名),但留单位防护债 + c1 孤儿债 |

**老周推荐:方案 A(MicroPUSD 强类型)。** 理由:
1. **根因是「单位语义靠人肉守」**,方案 B 只换了个更醒目的名字,**没改「靠人肉」本质** —— 下一个复用 RiskConfig 的 backtest/live 调用方仍能裸赋错单位。方案 A 把「漏乘 ×1e6 / whole 当 micro」变编译错,这是债务表 §P0-2 要的「不许名实不符躺 main」的彻底解。
2. **c1 已落地 MicroPUSD typedef(sizeof==8,ABI 零变)**,方案 A 是 c1 的「真正接入」,消除「零接入孤儿」债;方案 B 让 c1 白做。
3. **方案 A 顺手关掉 K-08/09/10**(sizing 当 whole pUSD 的第二现场)—— `.to_pusd()` 是编译强制的显式转换,方案 B 的改名管不到这。
4. ABI 物理零破,L2 名义等级可在 ADR-041 申请老郭裁定降 L1(非 IPC、sizeof 不变)。

**保留意见(老韩可否决):** 若老韩认为 demo 期 cap 真值频繁调、MicroPUSD UDL 字面量增加测试编辑摩擦不值,可先 B(改名止血)再 A(下个 sprint 上类型)。但老周不建议两段走(两次动 ABI-locked 文件、两次三方签)。

---

## 4. 关键实现裁定 (派 IC 前老韩 review)

### 4.1 RM atomic 仍 int64 (不引入 atomic<MicroPUSD>)

`bankroll_usdc_` / exposure map value 保持 `std::atomic<int64_t>` / `int64_t`(lock-free 保证)。MicroPUSD 仅在「ctor 入口 .store(.v)」和「比较点 from_micro 包」出现。**不动 atomic 类型 = 热路径零开销 + 无 lock-free 风险**(c1 spec §7 同款裁定)。

### 4.2 sizing 吃 RiskConfig(不改签名)+ to_pusd 读 cap

`compute(RiskConfig const&, SizingInput const&)` 签名**不变**。cap 读取从 `static_cast<double>` 改 `.to_pusd()`。**不把 cap 改成 double pUSD 入参** —— 保持 cap 单一真值在 RiskConfig(MicroPUSD),sizing 在边界 `.to_pusd()` 转,与 RM 同源。这样「pUSD↔micro 转换」全链只有两类显式点:`from_pusd`(paper_daemon 入口)/ `to_pusd`(sizing 读 cap),无第三处。

### 4.3 paper_daemon ×1e6 改 from_pusd

`paper_daemon.cpp:206-212` `cfg_.paper_loop.* × kMicroPerPusd` → `MicroPUSD::from_pusd(cfg_.paper_loop.*)`。这是 pUSD→micro 的**唯一入口**,CI grep 白名单或直接走 from_pusd 消除裸常量。

### 4.4 DD 块单位 (老韩裁定)

`risk_gateway.cpp:482-499` `pnl`/`loss`/`hard_threshold`/`soft_threshold` 与 bankroll 同 micro。`hard_pct × bankroll`:
- **选项 1(纯 micro int):** `hard_threshold = (int64)(br × hard_pct)`(br 是 micro int64,× double pct → int64 micro)。当前实现已是此式(:492),**c2 后 br 仍 atomic int64,无需改**。
- **选项 2(double 中转):** `cfg_.bankroll.to_pusd() × pct → from_pusd()`。
- **老周建议选项 1**(改动最小,当前式正确,只是 `cfg_.daily_loss_halt_usdc` 变 MicroPUSD 后取 `.v` 比 loss int64)。`set_daily_pnl(MicroPUSD)`,内部 store `.v`。老韩确认 pnl 单位 = micro。

### 4.5 book_depth liquidity 切口 (老韩 review)

`check_liquidity_`(`risk_gateway.cpp:524-528`)`order_size_usdc = (double)it.size_pUSD_micro`(micro raw 当 double)vs `book_depth_l1_usdc`(paper 侧 `paper_loop.cpp:496` ×1e6 成 micro)。**这是 size 与 book_depth 的隐藏 micro 耦合**。c1 spec 裁定 book_depth 保持 double pUSD、比较点 `to_pusd()` 桥接。**本 spec 维持 c1 裁定:book_depth 不进契约,但需老韩确认 check_liquidity_ 比较两侧单位**(当前 paper ×1e6 让 book_depth 变 micro 与 size micro 对齐,是「都 micro」自洽;若改 to_pusd 桥接则 size 也 to_pusd)。**本切口归边界 liquidity,非 P0-2 cap 红线核心,老周建议 c2-c5 不动 book_depth,仅在 ADR-041 登记为 follow-up(P1)。**

---

## 5. 测试迁移面评估

| 测试文件 | 迁移类型 | 工作量 | 风险 |
|---|---|---|---|
| `test_risk_gateway.cpp` | cap 字面量 `10'000 → 10'000_upusd`(数值不变);size 字面量不变 | 中(~10 处) | 低(数值零变) |
| `test_risk_gateway_wave3.cpp` | `RiskConfig{}` 默认 + DD set_daily_pnl 字面量 | 低 | 低 |
| `test_sizing_calculator.cpp` | **cap 真值语义重定**(whole pUSD → MicroPUSD;input exposure/bankroll 尺度同步) | **高(C-08, 老韩定真值)** | **中** — 改错尺度 = 测试假绿 |
| `test_sizing_rm_consistency.cpp` | 用同一 RiskConfig 喂 RM+sizing,验一致性(c3 主回归门) | 中 | **关键门, 见 §6** |
| `test_position_ledger_w76.cpp` | `RiskConfig cfg` 默认随动 | 低 | 低 |
| `test_rm_debug_snapshot.cpp` | cap 默认 + size 字面量(10'000'001 micro 不变) | 低 | 低 |
| `test_fixture.hpp` / `test_strategy_unlock` / `test_coverage_metrics` / `test_p1_market_metrics_wss` | `RiskConfig{}` / cap 字面量 `_upusd` | 低 | 低 |
| `bench_risk_gateway.cpp` / `bench_e2e_latency.cpp` | cap 字面量 `_upusd` | 低 | 低 |
| `test_paper_loop.cpp` | RM caps/bankroll micro 注释 + 兜底 sub-cap 新测试(c5) | 中 | 低 |

**总评:** ~12 测试文件触及,绝大多数是「`10'000 → 10'000_upusd` 数值零变」机械迁移(低风险)。**唯一高风险是 `test_sizing_calculator.cpp`**(cap 当 whole pUSD 的语义改 MicroPUSD,真值由老韩定),需老韩逐个签 demo cap 真值。

---

## 6. 回归门 (老韩验收口径 — 证明没静默架空任何 cap 红线)

> **验收原则:** 改完每个 cap 红线(per_order / per_condition / per_outcome / bankroll / DD)**仍在数量级正确的输入下触发拒单**,且 sizing 与 RM 对同一 cap 解读一致。

1. **cap 触发回归(每条 cap 一个用例,RM 侧):** 构造 `size > cap`(micro)→ 必拒对应 RejectCode;`size == cap` → 不拒(边界 `>`);`size = cap + 1 micro` → 拒。覆盖 K-01~K-04。**数值与 c2 前字节级一致**(c2 仅 typedef,行为不变)→ diff ctest 数不增不减即证「非 no-op 且语义不变」。
2. **sizing↔RM 一致性门(c3 核心,`test_sizing_rm_consistency`):** 同一 `RiskConfig` 实例喂 RM + sizing。断言:**sizing 输出 `suggested_notional`(pUSD)× 1e6 ≤ RM `per_order_cap`(micro)** 恒成立 → sizing 不会产出会被 RM per_order 拒的 size。这正是 P0-2 的根本不变式(取代旧 clamp)。c3 后 sizing 读 `cap.to_pusd()`、RM 比 micro,二者同源 → 不变式编译期+测试双保。
3. **exposure 累加门(c4):** 喂 RM 两笔同 token,第 2 笔 `cur + size > per_outcome_cap` → 拒;sizing 第 2 笔 headroom = `cap.to_pusd() − current_exposure` 相应缩小 → suggested_notional 受限。**验「RM 与 sizing 看到同一 exposure」**(c4b 修「sizing 恒 0」假闭环)。
4. **兜底 sub-cap 门(c5):** `per_order_cap = 0.5 pUSD`,sizing 出 0 → 兜底 `min(1pUSD, 0.5pUSD)=0.5pUSD` → RM 不拒(`0.5pUSD micro == cap`,`>` 不触发)。证旧 1 pUSD 硬兜底的 sub-cap 拒单 bug 关闭。
5. **CI grep 门(c5):** `risk_gateway.cpp`/`sizing_calculator.cpp`/`paper_loop.cpp` 无裸 `× 1'000'000` / `static_cast<double>(cfg.*cap*)`(白名单除外)→ 复发即 CI 红。
6. **全量 ctest:** 每 commit 1057+ 全绿(c2 数值零变;c3/c4/c5 新增测试),无 flaky 新增。

**老韩签字项:** ①cap demo 真值(test_sizing)②DD 块单位(§4.4 选项)③c4 exposure 喂数契约(是否 c4 与 P0-1 合流)④book_depth liquidity 切口(§4.5 是否本期动)。

---

## 7. 风险 + 回滚 (哪步最危险 + 分步验证)

| 步 | 危险度 | 风险 | 缓解 |
|---|---|---|---|
| **c2** | **中-高** | RiskConfig 改字段类型 = R-4 静默变更高发区;ABI-locked 文件,漏审构造点 → 静默架空 cap(正是上次踩雷模式) | §1 全审计清单逐点过;c2 数值零变(`10'000_upusd`)→ ctest diff 数不变即证语义守;三方签/老郭裁定降级 |
| **c3** | **中** | sizing cap 从 whole pUSD 改 MicroPUSD.to_pusd(),test_sizing 真值重定改错尺度 = 测试假绿、bug 半关闭 | `test_sizing_rm_consistency` 同源不变式(§6.2)硬验;老韩签 cap 真值 |
| **c4** | **高(本组最危险)** | 单边回写 exposure(RM 有、sizing 无感)比恒 0 更糟:第 2 笔偶发拒、sizing 无感 → 实盘资金量级误判 | **c4 不在 c2/c3 commit;与 P0-1 喂数管道统一排期;未就绪则双侧保持显式 0 + TODO,禁半吊子单边回写** |
| **c5** | **低** | 兜底 clamp / CI grep | sub-cap 单测;CI 白名单评审 |

**最危险一步:c4(exposure 路径)。** 它不是单位问题、是「安全网喂数」问题,与 P0-1 同源。**老周裁定:c4 从 P0-2 spec 中剥离为「c4 依赖 P0-1」,c2/c3/c5 先行根治单位失配(P0-2 本体),c4 随 P0-1 喂数管道落地。** 这样 P0-2 单位 bug 在 c2+c3+c5 三 commit 内彻底关闭,不被 c4 的喂数复杂度拖住。

**回滚:** c2/c3/c5 各独立 commit、每步全绿 → `git revert` 逆序可回任意中间步。**不可回滚项:** c3 若老韩重定了 cap 真值(test_sizing),回滚会退回「sizing 当 whole pUSD」错配态 —— 故 cap 真值校准建议拆 c3 内独立子 commit 便于隔离。c2 是纯 typedef + 数值零变,回滚最安全。

**分步验证序:** c2(数值零变,ctest diff 数守恒)→ c3(一致性不变式门)→ c5(兜底/CI)→〔c4 随 P0-1〕。每步独立 PR + ABI ref 行 + 全绿门。

---

## 8. 交付 checklist (派 IC 依据)

- [ ] **c2** commit:RiskConfig 5 字段 `MicroPUSD` + RM 4 比较点 `from_micro` + setter MicroPUSD + ctor `.store(.v)` + DD 块 §4.4;fixture `_upusd`(数值零变);全绿 ctest diff 数守恒;PR 含 `ABI ref` 行(L1/L2 看老郭裁定)
- [ ] **c3** commit:sizing K-08/09/10 `.to_pusd()` + 删 paper_loop:412-415 `(int64)` 截断 + sizing/RM 共用同 RiskConfig;`test_sizing_calculator` cap 真值(老韩签)+ `test_sizing_rm_consistency` 一致性门绿
- [ ] **c5** commit:兜底 `min(1.0_pusd, per_order_cap)` + CI grep 守护(老高)+ sub-cap 单测
- [ ] **c4**(剥离,随 P0-1):exposure 喂数 c4a + sizing 读真值 c4b;铁律#3 paper=backtest 同逻辑
- [ ] ADR-041 落 `docs/ADR/`,老郭签(含 ABI 等级裁定 L1/L2)+ 老韩签(cap 真值 + DD + exposure + book_depth)
- [ ] 下游通知:老孙(size_pUSD_micro 边界②本期不动,RM cap 改类型不影响 signer)/ 老唐(audit_record.size_pUSD_micro 是 int64 micro raw,c2 不动它,见 §6 注)/ 老高(CI grep)/ 老李+老孙+GM(若 L2 三方签)
- [ ] 老韩 §6 验收四签字项通过

---

## 附:c1 spec 一处勘误 (供 c1 边界② follow-up)

c1 spec §2 边界② 注:「`AuditRecord` 没有 size 字段(已确认 risk_gateway.hpp:265 AuditRecord 无 size),audit 链不受影响」。**实测:`risk_gateway.hpp:265` 的 AuditRecord 确无 size 字段,但 `observability/audit_record.hpp:171` + `audit_emitter.hpp:103` 的 AuditRecord(另一个,obs 域)有 `int64 size_pUSD_micro`**。两个 AuditRecord 同名异 struct。本 c2-c5 **不触 size_pUSD_micro 类型**(边界②留排),obs AuditRecord 的 size 是 int64 micro raw,c2 改 cap 类型不影响它 —— 但 c1 边界② 真要上 size typedef 时须把 obs AuditRecord 一并纳入,c1 spec 该处「audit 链零影响」结论对边界② 不成立。已登记,归 c1 边界② follow-up(非 P0-2)。
