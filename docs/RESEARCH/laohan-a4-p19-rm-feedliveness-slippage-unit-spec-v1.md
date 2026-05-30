# laohan-a4-p19-rm-feedliveness-slippage-unit-spec — v1

> owner: 老韩 (risk-engineer, 风控合规部主管, RM 主权 owner)
> last_review: 2026-05-30
> 形态: RM 主权 spec 供 GM 主干实现 + 老韩 review。本文档只读分析产出, 不改代码主干。
> 关联:
> - retro synthesis A2 副产 P1-9 (docs/MEETINGS/2026-05-30-retro-synthesis.md:70)
> - retro synthesis A4 feed-liveness (docs/MEETINGS/2026-05-30-retro-synthesis.md:35 §3)
> - A1 单位根治 (MicroPUSD / to_pusd 既有 API, include/stcpp/domain/micro_pusd.hpp)
> - 单位契约 CI grep (tests/ci_grep/unit_contract_check.py, 老高 #17)

---

## 0. 优先级结论 (先做哪个)

**P1-9 优先于 A4, 立即做。** 论证 (MVP「第一笔成交」目标驱动):

- P1-9 是**正确性 P0 级实盘阻断**: 当前 `check_liquidity_` 把 `size_pUSD_micro` (micro, 1e-6 pUSD) 直接当 whole pUSD 喂进 SlippageModel 的 `order_size_usdc`。实盘任何真实 size (例 500 pUSD = `5e8` micro) 对几千 pUSD 的 book 会算出 ρ = 5e8 / 5e3 = 1e5 ≫ RHO_MAX(3.0) → **必触 `EXCEED_BOOK_DEPTH`**。结果: 所有正常单被 RM liquidity gate 全量误拒, **第一笔成交永远不可能发生**。这是 MVP 验收的直接拦路石。
- A4 (feed-liveness 自检) 是**防御纵深 / 可观测性增强**, 解决「红线纸面化」(上游忘喂 → RM 拿默认值假装已活静默放行)。它降低未来事故概率, 但不阻断当前 MVP「第一笔成交」路径。属于「重要但不紧急」。
- 二者**无代码耦合**: P1-9 改 `check_liquidity_` 一行 + test 重校; A4 加 RiskGateway 私有成员 + 诊断方法。可串行, P1-9 先落主干、绿、push, 再做 A4。
- 工作量诚实评估: **P1-9 的真实成本不在那一行改动, 而在 test 重校** (见 §1.3, 6 个 case 需重校, 其中 R17/R18/R13 还要抬 cap 否则被 ADR-004 caps 抢先命中)。A4 工作量集中在设计「不改 ABI-locked struct」的私有成员方案 (见 §2)。

---

# 议题 1 — P1-9: RM slippage gate 单位失配 (P0, MVP 阻断)

## 1.1 确认: 这是 bug, 不是有意为之

证据链 (读代码, 非转述):

1. **SlippageModel 把两入参直接相除, 必须同口径。**
   `include/stcpp/numerical/slippage_model.hpp:161`:
   ```cpp
   double const rho = in.order_size_usdc / in.book_depth_l1_usdc;
   ```
   ρ (order/depth 比) 是无量纲量, `order_size_usdc` 与 `book_depth_l1_usdc` **必须同量纲**才有意义。header 注释 (slippage_model.hpp:54,56) 两者都标 `usdc` (whole 口径), 且 lib 内部从不做 /1e6 — 它是单位无关的纯数值库, 喂什么口径算什么。

2. **book_depth_l1_usdc 是 whole pUSD。**
   `OrderIntent.book_depth_l1_usdc` (risk_gateway.hpp:141) 是 `double`, 无 micro 语义, 全库 test 都按 whole 填 (例 5'000 = 5000 pUSD)。SlippageInput 直接透传 (risk_gateway.cpp:528)。

3. **size_pUSD_micro 是 micro (1e-6 pUSD)。**
   risk_gateway.hpp:133-136 注释明示 `size_usdc rename → size_pUSD_micro (micro = 1e-6 语义)`。

4. **构造点把 micro 当 whole 喂。**
   `src/stcpp/risk/risk_gateway.cpp:526`:
   ```cpp
   .order_size_usdc = static_cast<double>(it.size_pUSD_micro),   // ← BUG: micro 当 whole, 漏 /1e6
   ```
   ρ 分子是 micro、分母是 whole → ρ 被放大 1e6 倍。

**结论: bug。** 与 micro_pusd.hpp:6-9 头注记录的同源病根一致 ——「字段名带 micro 但裸 int64 无类型护栏 → 跨边界漏乘/漏除 ×1e6」。v0.6 rename 时 `check_liquidity_` 是漏网点 (cap 比较点在 c2/c3 已转 MicroPUSD 修过, 唯独这条 SlippageInput 构造路径漏了)。被 test 用极小 size (1'000 micro = 0.001 pUSD) + book_depth 同样小值掩盖, 数值上 ρ 偶然落在合法/拒绝区, 没人察觉。A2 修 bench 跑真实热路径才暴露。

## 1.2 正确修法 (精确改动点)

**文件**: `src/stcpp/risk/risk_gateway.cpp`, `check_liquidity_()` (约 525-526 行)。

**改动前**:
```cpp
numerical::SlippageInput in{
    .order_size_usdc = static_cast<double>(it.size_pUSD_micro),
```

**改动后**:
```cpp
numerical::SlippageInput in{
    // P1-9: size_pUSD_micro 是 micro(1e-6 pUSD); SlippageModel 的 order_size_usdc 与
    //   book_depth_l1_usdc 同为 whole pUSD 口径 (ρ=order/depth 需同量纲)。
    //   走 MicroPUSD::from_micro(...).to_pusd() = micro→whole 唯一合法转换通道 (A1 既有 API)。
    //   cite: docs/RESEARCH/laohan-a4-p19-...-spec-v1.md §1; micro_pusd.hpp:37 to_pusd()
    .order_size_usdc = domain::MicroPUSD::from_micro(it.size_pUSD_micro).to_pusd(),
```

要点:
- `domain::MicroPUSD` 已 include (risk_gateway.hpp:73 `#include "stcpp/domain/micro_pusd.hpp"`), `risk` 命名空间内可直接写 `domain::MicroPUSD`。无需新 include。
- `from_micro` constexpr 零成本; `to_pusd` 单次 int64→double + /1e6, 热路径可忽略 (一次浮点除)。
- **禁止**写成 `static_cast<double>(it.size_pUSD_micro) / 1e6` 的手写散乘除 — A1 钳-1 已定 to_pusd() 为唯一合法转换入口 (micro_pusd.hpp:114-120 同理), 散写会被 §1.4 新 grep 规则拦。

## 1.3 下游 test 冲击审计 (穷举, 修这个 bug 的主要工作量)

修正后语义从「ρ = (raw micro 计数) / depth」回到「ρ = (真实 whole pUSD size) / depth」。下表对每个走 `check_liquidity_` 的 test 给出: 当前 BUG 行为 → 修正后行为 → 是否破 → 重校值。

数值由 `/tmp/slip_check.cpp` 复算 SlippageModel Linear (BETA=0.3, RHO_MAX=3.0, FILL_RATE_FLOOR=0.50, KAPPA=1.0, T_HALFLIFE=30000ms), 与 lib 公式逐行对齐。

### 受影响 test 清单 (tests/unit/test_risk_gateway.cpp)

| Test | size_micro | depth(whole) | price | dt | BUG: ρ → 判定 | FIX: ρ → 判定 | 破? |
|---|---|---|---|---|---|---|---|
| R16 LOW_FILL_RATE (393) | 1'000 (默认) | 1'100 | 0.5 | **40s stale** | ρ=0.909 fill=0.0249 → **LOW_FILL** | ρ≈2e-6 fill=**0.2636** → **LOW_FILL** | **不破** (stale 主导, fill 0.264<0.5) |
| R17 EXCESSIVE_SLIPPAGE (402) | 1'000 | 400 | 0.5 | 200ms | ρ=2.5 fill=0.188 → LOW_FILL | ρ≈2.5e-6 fill=0.993 slip=0 → **Ok** | **破** (期望 LOW_FILL_RATE) |
| R17b EXCESSIVE_pure (410) | 1'000 | 400 | 0.5 | 200ms | ρ=2.5 → LOW_FILL (符合 OR 断言) | ρ≈2.5e-6 → **Ok** | **破** (期望 EXCESSIVE∥LOW_FILL) |
| R18 EXCEED_BOOK_DEPTH (419) | 1'000 (默认) | 250 | 0.5 | 200ms | ρ=4.0 → **EXCEED_BOOK_DEPTH** | ρ≈4e-6 → **Ok** | **破** (期望 EXCEED_BOOK_DEPTH) |
| R13 EDGE_NEGATED_BY_SLIPPAGE (362) | 1'000 (默认) | 800 | 0.5 | 200ms | ρ=1.25 fill=0.546 slip=150bps → 进 check_signal, edge_bps=50<150 → **EDGE_NEGATED** | ρ≈1.25e-6 slip=**0**bps → edge_bps=50>0 → **Ok** | **破** (期望 EDGE_NEGATED_BY_SLIPPAGE) |
| ADR-004 priority (447) | 20'000 | 5'000 | 0.5 | 200ms | size>cap → EXCEED_PER_ORDER_CAP (caps 先于 liquidity, 命中前不到 liquidity) | 同左 (caps 不受本 bug 影响) | **不破** |
| R07 EXCEED_PER_ORDER_CAP (315) | 20'000 | 100'000 | 0.5 | 200ms | caps 先命中 | 同左 | **不破** |
| 其余 Approved 路径 (R01-R06, R08-R12, R14-R15, R19-R20 等用默认 size=1'000 depth=5'000) | 1'000 | 5'000 | 0.5 | 200ms | ρ=0.2 fill≈0.937 → Ok (liquidity 放行, 命中各自目标 reject) | ρ≈2e-7 fill≈0.993 → Ok (更宽松, 仍放行) | **不破** |

### 受影响 test 清单 (tests/unit/test_risk_gateway_wave3.cpp)

| Test | 现状 | 修正后 | 破? |
|---|---|---|---|
| D1-D4 (DD/fee 系列, 默认 size=1'000 depth=5'000, 见 wave3:97-98) | liquidity 放行 → 命中各自 DD/fee 目标 | ρ 更小, liquidity 仍放行 | **不破** |
| D4 系列 fee 边界 (depth=100'000 时 line 353; 默认时其余) | ρ 极小 liquidity 放行 | 同左, 更宽松 | **不破** |

**wave3 全部不破** — 它没有故意触发 liquidity reject 的 case, 全靠 liquidity 放行后命中 DD/fee/edge 目标; 修正只让 ρ 更小、liquidity 更宽松, 放行行为不变。

### 不受影响 (走别的路径, 非 RM SlippageInput 构造)

- `tests/unit/test_virtual_matcher.cpp` — 走 `VirtualOrder.size_usdc` (double, 独立路径), 不经 RM。
- `tests/unit/test_position_ledger_w76.cpp` — VirtualMatcher 路径。
- `tests/integration/test_fixture.hpp:322` — `vo.size_usdc = (double)intent.size_pUSD_micro` 是 VirtualOrder 转换 (独立 bug 嫌疑, 但**不在本 spec 范围** — 那是 fixture 喂 VirtualMatcher, 不是 RM。**单列出来提示老周/GM**: fixture 这行同样是 micro 当 whole, 若 integration test 依赖 VirtualMatcher 真实 fill 数值需另查; 本 spec 只动 RM)。
- `tests/perf/bench_risk_gateway.cpp` (A2 已修) — Approved 路径 size=1'000 depth=5'000 修正后仍 Approved (line 72-73); reject 路径 size=50'000 depth=10 走 caps 先命中 (line 194,209), 不破。

### 4 个真破 test 的重校值

#### R18 EXCEED_BOOK_DEPTH (test_risk_gateway.cpp:419-424)
要重建 ρ>3: 真实 whole size / depth 250 > 3 → size > 750 whole pUSD = `750'000'000` micro。但 SetUp 的 `per_order_cap = 10'000 micro` (0.01 pUSD) 会让 caps 先命中 EXCEED_PER_ORDER_CAP (ADR-004 caps 在 liquidity 前)。**必须本地抬 cap**。重校:
```cpp
TEST_F(RiskGatewayTest, R18_EXCEED_BOOK_DEPTH) {
    // P1-9: size 现按真实 whole pUSD 喂 SlippageModel。要 ρ>3 (depth=250),
    //   需 size>750 pUSD = 800'000'000 micro; 同步抬 per_order_cap 否则 caps 先命中。
    RiskConfig c = cfg_;
    c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(2'000.0);      // ≥ 800 pUSD
    c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(3'000.0);
    c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(5'000.0);
    auto le = std::make_shared<InMemoryEmitter>();
    RiskGateway local(c, le); local.set_state(RmState::RUNNING);
    local.set_market_state(kMockConditionId, MarketState::PREGAME);
    local.set_market_freshness_ms(kMockConditionId, 100);
    local.set_market_active(kMockConditionId, true);
    auto it = make_ok_intent("sig_xdepth");
    it.size_pUSD_micro = 800'000'000;  // 800 pUSD whole; ρ=800/250=3.2>3
    it.book_depth_l1_usdc = 250;
    auto d = local.evaluate(it);
    expect_rejected(d, RejectCode::EXCEED_BOOK_DEPTH);
}
```
验证: ρ=800/250=3.2>3.0 → EXCEED_BOOK_DEPTH ✓ (`/tmp/slip2.cpp` size800 depth250 → EXCEED_BOOK_DEPTH)。
> 注: 该 test 原用 RiskGatewayTest fixture (有 inject_market helper)。若改用 local cfg, 需手动注入 market_state/freshness/active (如上), 或更简: 仅在 fixture 上临时调 cap 不可行 (cfg_ 已构造进 rm_)。**推荐**给 fixture 加一个 `make_rm_with_caps(...)` helper 或本 test 走 local gateway。GM 落地时择一, 老韩 review。

#### R17 EXCESSIVE_SLIPPAGE (test_risk_gateway.cpp:402-408)
当前期望 `LOW_FILL_RATE` (注释名叫 EXCESSIVE 但实际断言 LOW_FILL)。修正后要保持触发 LOW_FILL_RATE: ρ 需落在 fill<0.5 区。depth=400, 要 fill<0.5 → ρ 较大 (例 ρ=2.75 fill=0.158)。size = 2.75×400 = 1'100 whole = `1'100'000'000` micro, 同样要抬 cap。重校:
```cpp
TEST_F(RiskGatewayTest, R17_EXCESSIVE_SLIPPAGE) {
    RiskConfig c = cfg_;
    c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(2'000.0);
    c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(3'000.0);
    c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(5'000.0);
    auto le = std::make_shared<InMemoryEmitter>();
    RiskGateway local(c, le); local.set_state(RmState::RUNNING);
    local.set_market_state(kMockConditionId, MarketState::PREGAME);
    local.set_market_freshness_ms(kMockConditionId, 100);
    local.set_market_active(kMockConditionId, true);
    auto it = make_ok_intent("sig_xslip");
    it.book_depth_l1_usdc = 400;
    it.size_pUSD_micro = 1'100'000'000;  // 1100 pUSD; ρ=2.75, fill=0.158<0.5
    auto d = local.evaluate(it);
    EXPECT_EQ(d.reject, RejectCode::LOW_FILL_RATE);  // 语义不变: fill<floor 先于 slip 检
}
```
验证: ρ=2.75 fill=0.158<0.5 → LOW_FILL_RATE ✓ (`/tmp/slip_check.cpp` R17 recreate size=1100wh)。

#### R17b EXCESSIVE_pure (test_risk_gateway.cpp:410-417)
断言是 `EXCESSIVE_SLIPPAGE || LOW_FILL_RATE` 的 OR。复用 R17 同参即可满足 (LOW_FILL_RATE 满足 OR)。**但** 若要真正测出纯 `EXCESSIVE_SLIPPAGE` (fill≥0.5 且 slip>200bps), Linear 公式下 price=0.5/tick=0.01 时单档 slip ≤ 100bps 永远 <200, 不可能。要纯 EXCESSIVE 必须用**低 price** (tick 占比放大): 例 depth=400, price=0.05, size=85 whole → ρ=0.21 fill=0.932 slip=213bps → EXCESSIVE_SLIPPAGE。重校 (保留 OR 断言, 但给真能命中 EXCESSIVE 的参数, 让测试名副其实):
```cpp
TEST_F(RiskGatewayTest, R17b_EXCESSIVE_SLIPPAGE_pure) {
    RiskConfig c = cfg_;
    c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(500.0);  // ≥85 pUSD
    c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000.0);
    c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(2'000.0);
    auto le = std::make_shared<InMemoryEmitter>();
    RiskGateway local(c, le); local.set_state(RmState::RUNNING);
    local.set_market_state(kMockConditionId, MarketState::PREGAME);
    local.set_market_freshness_ms(kMockConditionId, 100);
    local.set_market_active(kMockConditionId, true);
    auto it = make_ok_intent("sig_xslip_b");
    it.price = 0.05;                     // 低 price 放大 tick 占比 → slip>200bps
    it.book_depth_l1_usdc = 400;
    it.size_pUSD_micro = 85'000'000;     // 85 pUSD; ρ=0.21 fill=0.932 slip=213bps
    auto d = local.evaluate(it);
    EXPECT_EQ(d.reject, RejectCode::EXCESSIVE_SLIPPAGE)
        << "R17b: fill>=0.5 但 slip>200bps, 应纯 EXCESSIVE_SLIPPAGE";
}
```
验证: ρ=0.21 fill=0.932≥0.5 slip=213bps>200 → EXCESSIVE_SLIPPAGE ✓ (`/tmp/slip2.cpp` FOUND size=85 depth=400 price=0.05)。
> 收益: 修正后这个 test 终于能**真正**覆盖 EXCESSIVE_SLIPPAGE 纯命中分支 (旧版靠 OR 兜底, 实际从没测到纯 EXCESSIVE)。建议 GM 采纳此强化版。

#### R13 EDGE_NEGATED_BY_SLIPPAGE (test_risk_gateway.cpp:362-368)
此 case 走 `check_signal_`: liquidity 先填 `d.slippage_bps`, 再 check_signal 比 `edge_bps < slippage_bps`。当前 BUG: ρ=1.25 → slip=150bps, edge_ci=0.005→edge_bps=50<150 → EDGE_NEGATED。修正后 ρ→极小 slip=0, 50>0 → 放行。要保持触发: 需真实 size 让 slip>50bps 且 fill≥0.5 (否则被 LOW_FILL 抢先, reject 码会变)。depth=800, 要 slip 在 50~ 区且 fill≥0.5: ρ 落单档 (≤1) slip=100×ρ (price=0.5/tick=0.01)。要 slip>50 → ρ>0.5。取 ρ=0.6: fill=1-π-s_stale, s_stale(200ms)≈0.0066, π(0.6)=0.165 → fill≈0.828≥0.5, slip=60bps>50。size=0.6×800=480 whole = `480'000'000` micro, 抬 cap。重校:
```cpp
TEST_F(RiskGatewayTest, R13_EDGE_NEGATED_BY_SLIPPAGE) {
    RiskConfig c = cfg_;
    c.per_order_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(1'000.0);
    c.per_outcome_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(2'000.0);
    c.market_exposure_cap_usdc = stcpp::domain::MicroPUSD::from_pusd(3'000.0);
    auto le = std::make_shared<InMemoryEmitter>();
    RiskGateway local(c, le); local.set_state(RmState::RUNNING);
    local.set_market_state(kMockConditionId, MarketState::PREGAME);
    local.set_market_freshness_ms(kMockConditionId, 100);
    local.set_market_active(kMockConditionId, true);
    auto it = make_ok_intent("sig_thin2");
    it.book_depth_l1_usdc = 800;
    it.size_pUSD_micro = 480'000'000;       // 480 pUSD; ρ=0.6 fill≈0.83 slip≈60bps
    local.set_edge_ci_lower("sig_thin2", 0.005);  // edge_bps=50 < slip 60 → EDGE_NEGATED
    auto d2 = local.evaluate(it);
    EXPECT_EQ(d2.reject, RejectCode::EDGE_NEGATED_BY_SLIPPAGE);
}
```
验证: ρ=0.6 → slip≈60bps, fill≈0.83≥floor (不被 LOW_FILL 抢), edge_bps=50<60 → EDGE_NEGATED ✓。
> ⚠️ GM 落地时跑实测确认 slip 落 60bps (我的手算 banker-round 可能 ±1bps); 若边界太紧, 取 ρ=0.7 (slip≈70bps) 留余量, 同步 size=560'000'000。

### 重校工作量小结
- **4 个 test 需改** (R17 / R17b / R18 / R13), 每个需: ① size_micro 改成真实 whole×1e6; ② 本地抬 cap (否则 ADR-004 caps 抢先) → 需 local RiskGateway + 手动 inject market。
- **建议 GM 给 fixture 加 helper** `RiskGateway make_local_rm(RiskConfig)` + 复用 inject_market, 减少 4 处重复样板。老韩 review helper 签名。
- **2 个 test 不破但语义变** (R16 仍 LOW_FILL 但因 stale 而非 size; 可加注释说明), 0 改动。
- 其余 test (R01-R12, R14-R15, R19-R20, wave3 全部, bench, integration) **不破**。

## 1.4 单位护栏 (CI grep 新规则, 防回归)

现有 `tests/ci_grep/unit_contract_check.py` (老高 #17) 已有 F1/F2/F3 守 cap 字段单位, 但**不覆盖 size_pUSD_micro 当 whole 喂 double 的反模式**。建议加 **F4**:

- **目标文件**: `risk_gateway.cpp` (已在 ENFORCED_FILES["rm"])。
- **禁止模式**: `order_size_usdc` 被裸 `static_cast<double>(...size_pUSD_micro...)` 或 `(double)...size_pUSD_micro` 赋值 (不走 `.to_pusd()`)。
- **建议正则** (Python `re`, 单行扫, 注释/`unit-contract-ok` 豁免, 与 F1-F3 同框架):
  ```python
  # F4: risk_gateway.cpp 里 size_pUSD_micro 裸转 double 当 whole pUSD (不走 .to_pusd())
  _F4_SIZE_RAW_DOUBLE = re.compile(
      r'order_size_usdc\s*=\s*(?:static_cast<double>|\(double\))\s*\(\s*[\w.\->]*size_pUSD_micro'
  )
  # 触发条件: 命中 _F4 且 同行不含 'to_pusd' → FAIL
  ```
  实现要点: 若一行同时含 `to_pusd` 则放行 (正确写法 `MicroPUSD::from_micro(it.size_pUSD_micro).to_pusd()` 内含 `.to_pusd()`, 不会误杀)。
- **报错文案** (照 F1-F3 风格):
  ```
  F4 (FAIL): risk_gateway.cpp:NN order_size_usdc 裸 (double)size_pUSD_micro
    size_pUSD_micro 是 micro(1e-6); SlippageModel.order_size_usdc 是 whole pUSD。
    必须 domain::MicroPUSD::from_micro(...).to_pusd() (micro→whole 唯一通道)。
    裸转 = micro 当 whole 喂 ρ=order/depth, 差 1e6 → liquidity gate 全量误拒 (P1-9 复发)。
    cite: laohan-a4-p19-...-spec-v1.md §1.4
  ```
- **建议**: 同时把守护从「只盯 size_pUSD_micro→order_size_usdc」泛化为「任何 SlippageInput 字段裸吃 micro」更稳, 但 MVP 阶段 F4 单点足够 (YAGNI, 不过度设计)。老高 #17 落地 grep, 老韩 review 正则。

## 1.5 红线引用 (粘原文, CLAUDE.md §8.1 纪律)

- **铁律#3 (CLAUDE.md §3 核心价值观)**, 原文:
  > 「3. **数字说话（Data-Driven）** — 任何提案带预期量化指标，"我感觉"不是发言。」
  > (注: 本 spec 所有 ρ/fill/slip 数值均 `/tmp/slip_check.cpp` 复算, 非「我感觉」。)
- **红线 #3 单位口径 (CLAUDE.md §8.1 条3 R-4 配套)**, 原文:
  > 「**ABI/字段单位变更必触发下游审计（补 R-4 配套）：** 任何字段重命名/单位变更（如 `size_usdc → size_pUSD_micro`）必须 audit 全部比较点/消费点的单位一致性，否则会「静默架空」依赖该字段的红线（caps/exposure/bankroll）。这类变更走 R-4（schema 静默变更红线）。」
  > P1-9 正是 `size_usdc → size_pUSD_micro` rename 时**漏 audit 的消费点** (check_liquidity_), 教训印证此条。
- **§8 红线 R-4 (schema 静默变更)**, 原文:
  > 「数据 schema 静默变更（不通知下游） → 责任人承担事故」
- **拒单码定义 (include/stcpp/risk/reject_enum.hpp, 粘原值)**:
  > `EXCEED_BOOK_DEPTH = 17` (ρ > RHO_MAX); `LOW_FILL_RATE = 15` (expected_fill_rate < FILL_RATE_FLOOR); `EXCESSIVE_SLIPPAGE = 16` (slip_abs > excessive_slippage_bps); `EDGE_NEGATED_BY_SLIPPAGE = 12`。
  > lib 端 (slippage_model.hpp:36-41): `ExceedBookDepth=2` (ρ>RHO_MAX), `FillRateBelowFloor=3` (<FILL_RATE_FLOOR)。

## 1.6 验收标准 (P1-9)

1. `check_liquidity_` 改为 `domain::MicroPUSD::from_micro(it.size_pUSD_micro).to_pusd()`, 干净 build。
2. 4 个重校 test (R17/R17b/R18/R13) 绿; 其余 RM test (含 wave3) 全绿; bench 编译通且 Approved 路径仍 Approved。
3. CI grep F4 规则加入, 对当前主干 PASS (因已改 to_pusd), 对故意回插 raw cast 的临时改动 FAIL (人工验一次)。
4. **实盘 sanity (MVP 关键)**: 给一个真实量级 intent (size=500 pUSD=`5e8` micro, depth=5000 whole) 跑 evaluate, **不再** 命中 EXCEED_BOOK_DEPTH (ρ=500/5000=0.1, 合法)。这是「第一笔成交」能发生的直接前置。
5. audit record 里 `slippage_bps` / `expected_fill_rate` 落真实数值 (非 1e6 放大后的退化值)。

---

# 议题 2 — A4: RM feed-liveness 自检 + 风控状态逐条标

## 2.0 采纳原文 (粘, retro synthesis §3)

docs/MEETINGS/2026-05-30-retro-synthesis.md:35:
> 「**架构建议(采纳): RM 加 feed-liveness 自检** —— 每红线记 last-fed ts，启动 emit "DD: never fed"。一条红线从没被喂过 = 生产里不存在，RM 必须自己喊出来。**owner: 老韩 + GM。**」

核心病: 上游忘喂某红线 → RM 拿默认值 (0 / 空 map) **假装已活静默放行** = 「风控纸面化」。

## 2.1 RM setter 盘点 + 分类 (读 risk_gateway.hpp:344-372)

| Setter | 喂什么 | 默认值 | 类别 | 忘喂后果 | liveness 必跟? |
|---|---|---|---|---|---|
| `set_bankroll` | bankroll (micro) | `bankroll_usdc_{0}` | **每 tick 活数据** | bankroll=0 → INSUFFICIENT_BANKROLL 把**所有单拒死** (fail-closed, 偏保守) | **必跟** |
| `set_daily_pnl` | 当日盈亏 (micro) | `0` | **每 tick 活数据** | pnl=0 → DD 永不触发, **亏损时仍放行** ⚠️危险放行 | **必跟 (高危)** |
| `set_consec_loss` | 连亏计数 | `0` | 每 tick 活数据 | =0 → CONSEC_LOSS 永不触发 ⚠️危险放行 | **必跟 (高危)** |
| `set_market_exposure` / `set_condition_exposure` | per-condition 敞口 | 空 map → 0 | 每 tick 活数据 | 缺 → 敞口当 0, **cap 形同虚设** ⚠️危险放行 | **必跟 (高危)** |
| `set_outcome_exposure` | per-token 敞口 | 空 map → 0 | 每 tick 活数据 | 同上 ⚠️ | **必跟 (高危)** |
| `set_market_freshness_ms` | 行情新鲜度 | 空 map → 0 | 每 tick 活数据 | =0 → 永远「新鲜」, **stale gate 失效** ⚠️危险放行 | **必跟 (高危)** |
| `set_token_book_freshness_ms` | token book 新鲜度 | 空 map → 0 | 每 tick 活数据 | 同上 ⚠️ | **必跟 (高危)** |
| `set_recon_freshness_ms` | 对账新鲜度 | `0` | 周期活数据 | =0 → recon 永远新鲜, **对账 gate 失效** ⚠️危险放行 | **必跟 (高危)** |
| `set_edge_ci_lower` | 信号 CI 下界 | 空 map | 按需 (per-signal) | 缺该 signal → check_signal 跳过 (放行该信号 slippage/fee 检查) ⚠️ | **必跟 (中危)** |
| `set_strategy_ev_ratio` | 策略 EV 比 | 空 map | 按需 (per-strategy) | 缺 → STRATEGY_DECAYED 不触发 (中性, 衰减检测失效) | **必跟 (中危)** |
| `set_market_state` | 5 档市场态 | 空 map → fail-safe 保守档 `{200,800}` | 每 tick 活数据 | 缺 → 默认最严档 (偏保守, 不危险但可能误拒) | **跟 (诊断用)** |
| `set_market_active` | 市场 active | 空 map → ? (需读 .cpp 确认默认) | 每 tick 活数据 | 若默认 active=false → 全拒 (保守); 若默认 true → MARKET_NOT_ACTIVE 失效 ⚠️ | **必跟 (高危, 取决默认)** |
| `set_state` | RM 状态机 | `SAFE_MODE` (启动默认) | 控制面 | 启动 SAFE_MODE 全拒新开仓 (安全默认, 不算纸面化) | 不跟 (状态机自有可观测) |

**危险放行 (默认值=「假装通过」) 红线** = 最该被 feed-liveness 喊出来的: `daily_pnl` / `consec_loss` / `*_exposure` / `*_freshness` / `recon_freshness` / `market_active`。这些默认值让红线**静默失效且偏放行**, 是「纸面化」核心。

> 注: `bankroll`/`market_state` 默认值偏**保守** (拒死/最严档), 忘喂会过度拒单 (干扰 MVP「第一笔成交」) 但不烧钱; 也该被自检喊出来 (运营会很快发现「怎么全拒」)。

## 2.2 feed-liveness 机制设计 (热路径安全)

**约束 (CLAUDE.md §8 红线 R-12)**, 原文:
> 「WebSocket event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0（ADR R-12）」

→ 自检本身**不能进热路径** (evaluate ≤100us 预算)。设计成: **喂时记 ts (O(1) atomic, 无锁) + 诊断/启动期低频读**。

### 数据结构 (RiskGateway 私有成员, 不进 ABI-locked struct)

给「全局标量类红线」(bankroll/daily_pnl/consec_loss/recon_freshness) 加固定下标的 atomic 数组:
```cpp
// risk_gateway.hpp private 区 (不改任何 ABI-locked struct 布局; 见 §2.4)
enum class FeedKey : std::uint8_t {
    Bankroll = 0, DailyPnl, ConsecLoss, ReconFreshness,
    COUNT
};
std::array<std::atomic<std::int64_t>, static_cast<size_t>(FeedKey::COUNT)>
    last_fed_ns_{};  // 0 = 从未喂过; 喂时 store(now_realtime_ns(), relaxed)
```
对应 setter 末尾加一行 (O(1), relaxed store, 无锁, 热路径零感):
```cpp
void set_bankroll(std::int64_t usdc) noexcept {
    bankroll_usdc_.store(usdc);
    last_fed_ns_[(size_t)FeedKey::Bankroll].store(now_realtime_ns(), std::memory_order_relaxed);
}
```

**map 类红线** (per-condition/per-token/per-signal/per-strategy exposure & freshness): liveness 不按 key 逐条记 (太重且语义是「这个 condition 喂没喂」, 不是「红线喂没喂」)。改记**「该红线 map 是否至少被喂过一次」**: map 类各加一个 atomic 标量 `*_ever_fed_ns_` (首次 set 时 CAS 0→now, 后续不动)。MVP 够用: 区分「这条红线生产里压根不存在 (从没喂)」vs「在用」。逐 key 粒度留 M2+ (YAGNI)。

### 自检输出 (启动期 + 低频, 非热路径)

```cpp
struct FeedLivenessRow { std::string_view key; std::int64_t last_fed_ns; bool ever_fed; };
[[nodiscard]] std::vector<FeedLivenessRow> feed_liveness_report() const noexcept;  // 诊断, 非热路径
```
- 用途1 (启动自检): paper/live daemon 起来后、进 evaluate 循环前调一次, 对 `ever_fed==false` 的红线 emit warning log `"RM feed-liveness: <key> NEVER FED — 该红线生产里不存在"`。对应采纳原文「启动 emit "DD: never fed"」。
- 用途2 (周期巡检): daemon 每 N 秒 (例 5s) 调一次, 对「曾喂过但 now - last_fed > 阈值 (例活数据 >10s)」的红线 emit `"<key> STALE FEED — 上次喂 <ago>s 前"`。检测「喂过但断流」。
- **绝不在 evaluate 热路径调** report (它分配 vector + 遍历)。evaluate 只做 O(1) store, 零额外成本。

### 阈值 (老韩 RM 主权定, 金融专家如需可调)
- bankroll/daily_pnl/exposure/freshness: 活数据, 巡检 staleness 阈值 **10s** (MVP; 实盘按 tick 率收紧)。
- recon_freshness: 周期数据, 阈值 **60s** (与 stale gate recon 30s halt 同量级, 留 buffer)。
- edge_ci/strategy_ev: 按需数据, 不做 staleness 巡检 (只查 ever_fed)。

## 2.3 「逐条标」: evaluate 决策时标注红线数据新鲜度 (最小可行)

**不改 AuditRecord ABI** 的最小方案 (避免 §2.4 ABI 摩擦):

evaluate 时, 对「本次决策实际依据的红线」, 在 audit 的**现有自由文本/sub_reason 之外**不新增结构化字段。改为:
- **MVP 方案 (推荐)**: evaluate **不**逐条标进每条 audit (会膨胀热路径 + 撑爆 AuditRecord ABI)。改为**周期诊断快照** —— `feed_liveness_report()` 的输出由 daemon 周期 dump 到独立诊断日志 (非 WAL audit), 运营/复盘时对照「决策时刻附近这些红线是否新鲜」。这满足「防假已活」的可观测性诉求, 且零 ABI 改动、零热路径成本。
- **逐条标进 audit (不推荐 MVP)**: 若坚持每条 decision audit 带「依据红线是否新鲜」位图, 需给 AuditRecord 加字段 → 触发 ABI lock Rule 1/4 + SSOT cite (见 §2.4), 成本高且对 MVP「第一笔成交」无直接价值。**判定: 过度设计, M2+ 再议。**

→ **结论**: 「逐条标」MVP 降级为「周期 feed-liveness 诊断快照 dump」。决策级逐条标延到 M2 (届时若有真实复盘需求再加 AuditRecord 字段, 走正式 ABI 变更)。这符合「不要过度设计, MVP 够用」。

## 2.4 ABI 注意 (优先零 ABI 改动)

**ABI-locked 文件** (risk_gateway.hpp 在 abi_lock.py ABI_LOCKED_FILES + core_data_structure_ssot_check)。

本 A4 方案**刻意零 ABI 改动**:
- `last_fed_ns_` 数组 / `*_ever_fed_ns_` 标量 = **RiskGateway 私有成员**, 不进 `RiskConfig` / `AuditRecord` / `OrderIntent` 任何被 memcpy/WAL 序列化的 struct。RiskGateway 类本身非序列化对象 (pImpl + atomic 成员), 加私有成员**不触发 ABI lock Rule 1/4**, 不需 SSOT cite。
- `FeedKey` enum / `FeedLivenessRow` / `feed_liveness_report()` 都是 RM 内部诊断类型, 不跨 ABI 边界。
- 唯一需确认: abi_lock.py 是否对 risk_gateway.hpp 做**整文件 hash** (那样加私有成员也会触发)。**派给 GM 落地前先验**: 若是整文件 hash, 加私有成员需更新 ABI lock baseline (Rule 1 级, 低风险, 因布局未变 sizeof(序列化 struct) 不变); 若只 hash 序列化 struct 布局, 则零触发。**老韩判定: 即便触发也是最低 Rule 1 级 (无序列化布局变更), 远低于改 AuditRecord 字段。**

**如必须改 ABI (逐条标进 audit, 已判定 M2+)**: 给 AuditRecord 加 `uint16_t feed_liveness_bitmap` (各红线新鲜位) → **Rule 4 级** (AuditRecord 是 WAL 序列化 struct, 老唐 WAL schema 联动 + SSOT cite + WAL version bump)。MVP 不做。

## 2.5 红线引用 (粘原文)

- **CLAUDE.md §8 红线 R-12**, 原文:
  > 「WebSocket event loop 任何同步 REST / 阻塞 IO / 锁 > 100us → P0（ADR R-12）」
  > (A4 自检零锁/零阻塞进热路径, 仅 O(1) relaxed atomic store; report 非热路径。)
- **CLAUDE.md §8 红线 R-1 (隐含, 风控拒单可追溯)** + §7 协作规范条6:
  > 「**可追溯：** 风控拒单 / 关键决策 / API 变更 — 全部留 audit log」
  > (feed-liveness 诊断日志补强「红线是否真在工作」的可追溯性。)
- **采纳原文** (retro synthesis:35) 已粘于 §2.0。

## 2.6 验收标准 (A4)

1. `last_fed_ns_` (4 标量红线) + `*_ever_fed_ns_` (map 类红线) 私有成员加入, 对应 setter 末尾加 O(1) relaxed store, **零 ABI 序列化布局变更** (sizeof(RiskConfig/AuditRecord/OrderIntent) 不变, abi_lock.py 验过)。
2. `feed_liveness_report()` 返回各红线 (ever_fed, last_fed_ns); 单测覆盖「从未喂 → ever_fed=false」「喂后 → ever_fed=true + last_fed_ns>0」。
3. paper daemon 启动自检接通: 起循环前调一次, 对 ever_fed=false 红线 emit warning (验「DD: never fed」语义)。
4. **热路径零回归**: bench_risk_gateway evaluate p99 不因 A4 升 (relaxed store 可忽略); BM_RmFeed 不退化。
5. evaluate 热路径**不**调 report (代码审 + grep 确认无 report() 出现在 evaluate 调用链)。

---

## 3. 交接 / 派单边界

- **P1-9 + A4 实现**: GM 主干落地 (CLAUDE.md §10.2 工作模式), 老韩 review。
- **CI grep F4**: 老高 #17 落地正则, 老韩 review。
- **fixture helper (make_local_rm)**: GM 落地, 老韩 review 签名 (减少 4 test 重校样板)。
- **test_fixture.hpp:322 VirtualOrder size 转换嫌疑** (§1.3): 派老周/小宋查 VirtualMatcher 路径是否同病, **不在本 spec 范围**, 单列提示。
- **abi_lock.py 对 risk_gateway.hpp 是否整文件 hash**: GM 落 A4 前 1 分钟验 (§2.4)。

## 4. 一句话总结

P1-9 先做 (一行改 to_pusd + 4 test 重校抬 cap), 它是「第一笔成交」的直接拦路石; A4 紧随 (零 ABI 私有 atomic + 启动自检 + 周期诊断 dump, 逐条标 audit 降级 M2), 把「红线纸面化」喊出来。两者数值与冲击面已穷举 (复算见 /tmp/slip_check.cpp, 与 SlippageModel Linear 公式逐行对齐)。
