# 老郭 — 红线 + R-XX 约束全盘审计 v1

- **Owner:** 老郭 (chief-architecture-reviewer + 顾问团协调 + 红线协调)
- **Last review:** 2026-05-30 07:26 UTC
- **请决人:** 老雷 (GM)
- **性质:** 只读审计 + 仲裁建议, 结论待 GM 拍板落 ADR
- **触发:** GM M1 主线 A0→A2 撞两次「约束咬人」(ML-R2 字面执行 / 单位契约失配), 要求全盘体检红线是否臃肿/过时/埋雷
- **关联:**
  - `CLAUDE.md` §8 红线 + §10.1 worktree 纪律
  - `docs/MEETINGS/2026-05-30-m1-route-review.md` (D3 advisory 裁定 — 本审计核心判例)
  - `docs/ADR/2026-05-28-gm-redline-data-source-timestamping.md` (R-20)
  - `docs/ADR/2026-05-28-gm-redline-websocket-non-blocking.md` (R-12)
  - `docs/ADR/2026-05-28-gm-signoff-paper-trade.md` (R-11 定义)
  - `docs/ADR/2026-06-01-adr-014-ml-shadow-timing.md` (ML-R1~R6 定义)
  - `docs/ADR/2026-05-28-r07-r08-liquidity-vs-position-cap-priority.md` (RM reject enum R-N — 第三套命名空间)
  - `include/stcpp/risk/risk_gateway.hpp` / `src/stcpp/risk/risk_gateway.cpp` (单位失配实证)

---

## 0. TL;DR — 一句话 + Top 3

**整体健康度: 真红线本身不臃肿、地基稳固; 但红线体系有一个结构性致命缺陷 —— `R-NN` 编号被三套互不相干的命名空间并用 (§8 红线 / 老胡 risk-registry / RM reject enum), 同号异义已造成误传导事故 (ML-R2)。咬人风险不在「红线太多」, 而在「编号歧义 + ABI 演进后派生约束没跟上」。**

**最该优先调的 Top 3:**

1. **【P0 治理】红线 R-NN 命名空间三套同号异义 → 拆命名空间。** §8 红线编号 R-12 = WSS 非阻塞, 而同一份代码库里 RM reject enum R-12 = `EDGE_NEGATED_BY_SLIPPAGE`、老胡 risk-registry R-12 又是另一回事。ML-R2 误传导 (离线训练 → 被传成"paper 恒 advisory")就是这套歧义的直接产物。建议: §8 红线统一加 `RL-` 前缀 (RedLine), RM enum 用 `RJ-` (Reject), risk-registry 用 `RR-`, 永久消歧。

2. **【P0 失配】OrderIntent 单位契约 ABI 演进后 RM caps 未跟上 → 真红线被静默架空。** `size_pUSD_micro` (1e-6 scaled) vs `per_order_cap_usdc=10'000` (whole unit) 在 `risk_gateway.cpp:432` 直接比较 → 任何正常单恒超 cap 恒拒。这不是"红线过时", 是**红线的实现被单位 drift 悄悄废掉了** —— 比过时更危险, 因为红线"看起来还在"。需立即修 + 立"ABI 改字段必须 audit 所有比较点单位"的派生纪律。

3. **【P1 防再犯】"派生约束被误当红线"无识别机制 → 立红线分级 + 解释权归属。** ML-R2 咬人的根因是: 风控把一条 ML 约束的"过度解读"当硬红线字面执行, D3 才由 GM 拍板澄清。建议每条红线显式标注 ① 真红线 / ② 派生约束 ③ 解释权人 (谁有权澄清边界), 避免下一个"字面执行"再阻塞正当目标。

---

## 1. 审计方法 + 关键背景判定

### 1.1 三套 R-NN 命名空间 (本审计第一发现)

grep 全库 `R-\d` 命中 1300+ 处, 但分属**三套互不相干的编号体系**, 这是 GM 直觉"红线是否臃乱"的真正答案 —— 不是数量臃, 是**命名歧义乱**:

| 命名空间 | 来源 | R-N 含义示例 | 性质 |
|---|---|---|---|
| **A. §8 红线编号** | CLAUDE.md §8 + GM 红线 ADR | R-11=paper 污染 / R-12=WSS 非阻塞 / R-20=时间戳 / R-33=四维扫描 | 真红线 (一票否决) |
| **B. RM reject enum** | ADR-004 / risk_gateway | R-1=NaN/Inf / R-6/7/8=caps / R-12=EDGE_NEGATED_BY_SLIPPAGE / R-15/16/17=liquidity | 工程枚举 (拒单码) |
| **C. 老胡 risk-registry** | laohu-risk-registry-v2.4 | R-02=绕过 / R-06=延迟吃PnL / R-42=build verification | 项目管理风险项 (I×P 打分) |

**同号异义实证:** "R-12" 在 A 是"WSS 事件循环非阻塞 P0 红线", 在 B 是"边际被滑点吃光的拒单码", 两者毫无关系却同号。"R-7" 在 A/§8 语境指 RM cap 体系 (per_order_cap), 在 ADR-011 又被写成"build-time paper/live binary 锁", 在 B 是 `INSUFFICIENT_BANKROLL`。**一个 sub-agent 读到 "R-7 红线" 完全可能理解错。**

> 这正是 ML-R2 咬人的同构根因: 编号/语义在不同文档间漂移, 字面执行者抓到的是错误那一版。

### 1.2 核心判例: D3 advisory 裁定 (ML-R2 误传导解剖)

`docs/MEETINGS/2026-05-30-m1-route-review.md` §3.2 + §4 D3 已是教科书级判例, 本审计直接引为「派生约束误当红线」的定义性案例:

- **ML-R2 权威定义** (ADR-014): "仅离线训练 (W6+ ONNX export)" —— 讲的是**模型训练在离线**。
- **被传导成的版本** (M1 route review §3.2 老韩): "paper 恒 advisory, advisory gate 绝不拆" —— 讲的是 **paper 永不产 fill**。
- **两者根本不是一回事。** "advisory" 的真实出处是 ML de-vig calibration gate (校准未锁前不路由 live), 与 paper engine 是否产虚拟 fill 无关。中间传导链把"不路由 live"放大成"不产 paper 成交", 直接让 MVP「第一笔成交」永不可能。
- **D3 GM 裁定:** advisory = 不自动路由 live ≠ 不产 paper 成交。paper fill ≠ live order, 不构成红线。撤回过度解读。

**这是"派生约束被误当红线"的标准范式**, 后文每条都用这把尺子量。

---

## 2. §8 真红线逐条审计

> 分类: ① 真红线 (金融/安全/法律地基) ② 派生约束 (工程选择, 可调) ③ 过时

| # | 红线条目 | 分类 | still-valid? | 咬人风险 | 建议 |
|---|---|---|---|---|---|
| 1 | 下单链路绕过 RiskManager → P0 | ① 真红线 | ✅ 成立, 金融地基 | 低。但注意 paper 也走全 RM (ADR paper-trade D-PT1), 措辞已正确 | **保留不动** |
| 2 | 私钥明文落盘/入日志 → 权限暂停 | ① 真红线 | ✅ 成立, 安全地基 | 低。MEMORY"永不删私钥"配套 | **保留不动** |
| 3 | 回测与实盘不同数据逻辑 → 不许上线 | ① 真红线 | ✅ 成立, PIT 一致性地基 | 低-中。需与 R-20/ADR-011 共享 binary 交叉守 | **保留**, 措辞补"含单位/scaling 一致" (见 §4 失配教训) |
| 4 | 数据 schema 静默变更 → 责任人担责 | ① 真红线 | ✅ 成立 | 中。**反讽: 单位契约 drift (§4) 本身就是一次未被 R-4 抓到的静默 schema 变更** | **保留 + 收紧**: schema 红线显式涵盖"字段单位/scaling 语义变更" |
| 5 | 跳过架构评审上重大变更 → 回滚 | ① 真红线 (流程) | ✅ 成立 | 低 | **保留不动** |
| 6 | 违反 Polymarket/Goalserve/vendor ToS → 回滚 | ① 真红线 (法律) | ✅ 成立, 法律地基 | 低。D3 已确认 paper fill 不下 CLOB = 不碰 ToS | **保留不动** |
| 7 | WSS event loop 同步 REST/阻塞 IO/锁>100us → P0 (R-12) | ① 真红线 | ✅ 成立, 延迟地基 (跨洋链路核心) | **中**: "100us/50us" 阈值是**派生数值**, 在跨洋高延迟实测下可能需重标 (老吴 B 长跑反哺)。红线精神(不阻塞)是①, 具体微秒阈值是② | **红线保留, 数值阈值降级为可调派生约束** (实测校准, 不动红线本体) |
| 8 | Paper mode 污染真账本 → P0 (R-11) | ① 真红线 | ✅ 成立 | **中-高 (历史咬人源)**: R-11 本身是真红线, 但 D3 之前的"advisory 恒真"误读正是挂靠在"paper 要隔离"的过度保守上。R-11 守的是"账本不串", 不是"paper 不产 fill" | **保留红线 + 加澄清注**: "隔离 = 账本物理分离, **不等于** paper 不许产虚拟成交" (固化 D3) |
| 9 | 数据源用必须标时间 (4ts 契约) → P0 (R-20) | ① 真红线 | ✅ 成立, PIT 地基 | **中**: 红线对; 但 M1 A1 实测中 game_row 四戳"现借 book ts"是已知临时违规 (route review §3.1), 接真数据后必须切 `EventScore.ts`。**R-20 字面执行会卡住 A1 联调** —— 已有 fail-closed 缓解 | **保留红线**, 但补"联调期退化标识 (INFERRED_*) 是合规的, 非违例", 避免字面执行卡联调 |
| 10 | 数据源文档必须四维扫描 → P0 (R-33) | ② 派生约束 (误标红线) | ⚠️ 流程质量约束, 非金融/安全/法律地基 | **低但定性错**: R-33 是"文档撰写流程质量门", 性质是工程纪律, **不该和"绕过 RM/私钥落盘"同列一票否决**。它咬不了人, 但占着红线位拉低红线严肃性 | **降级为派生约束/SOP**, 从 §8 一票否决移到文档治理 SOP (ADR-028) |

### 2.1 §8 红线小结

- **真红线 9 条里 8 条扎实** (1-9), 不臃肿, 是金融/安全/法律/PIT 地基, **保留**。
- **R-33 (四维扫描) 是唯一一条"被误升级为红线的派生约束"** —— 它是文档质量 SOP, 不是一票否决级地基。**建议降级。**
- **R-11/R-20 是两条"红线对、但字面执行会咬人"** —— 不是退役, 是要补"边界澄清注"防止下一次 ML-R2 式过度解读。
- 地域/法律/监管层已 GM 决议暂缓 (jurisdictional-deferral ADR), §8 未列, 无需动。

---

## 3. ML-R 红线逐条审计 (ADR-014)

| # | 约束 | 分类 | still-valid? | 咬人风险 | 建议 |
|---|---|---|---|---|---|
| ML-R1 | ML 不进生产 (active M4.5 后) | ① 真红线 (纪律地基) | ✅ | 低 | 保留 |
| **ML-R2** | **仅离线训练 (ONNX export)** | ① 真红线 | ✅ 定义本身没问题 | **高 (已咬人)**: 定义=离线训练, 但被传导成"paper 恒 advisory/不产 fill"。**问题不在 ML-R2 本体, 在它被错误引用** | **保留定义 + 立"引用纠偏"**: 任何文档引用 ML-R2 必须粘贴原文"仅离线训练", 禁止二次转述。D3 入 ADR 固化 |
| ML-R3 | 不阻塞 if-else 路径 (shadow 旁路) | ① 真红线 | ✅ | 低 | 保留 |
| ML-R4 | 不替代风控 (RM enum 静态) | ① 真红线 | ✅ | 低 | 保留 |
| ML-R5 | 4ts R-20 全程透传 | ① 真红线 (R-20 派生) | ✅ | 低 | 保留 |
| ML-R6 | shadow→active 解锁需三方决议 | ① 真红线 (治理) | ✅ | 低 | 保留 |

**ML-R 小结:** 6 条定义全部成立, **没有一条该退役**。唯一问题集中在 **ML-R2 的传导链路被污染** —— 这是引用纪律问题, 不是约束本身问题。修法是"引用必粘原文 + D3 固化", 不是改红线。

---

## 4. 派生约束/失配实证: 单位契约 (GM 第二个咬人实例)

**这是本审计最危险的发现 —— 不是"红线过时", 是"真红线被单位 drift 静默架空"。**

### 4.1 实证 (代码级)

- `risk_gateway.hpp:135`: `std::int64_t size_pUSD_micro{0}` —— v0.6 ABI break #5, `size_usdc` rename, **micro = 1e-6 scaled**。
- `risk_gateway.hpp:298-301`: `per_order_cap_usdc=10'000` / `market_exposure_cap_usdc=50'000` / `per_outcome_cap_usdc=25'000` / `bankroll_usdc=100'000` —— **全是 whole-unit USDC, 没跟着 micro 化**。
- `risk_gateway.cpp:432`: `if (it.size_pUSD_micro > cfg_.per_order_cap_usdc)` —— **micro (1e-6) 直接 > whole-unit cap**。10000 pUSD 的正常单 = 10,000,000,000 micro > 10,000 cap → **恒超 → 恒拒**。
- `cpp:452/465/473`: market_exposure / per_outcome / bankroll 同样 micro vs whole-unit, **全部失配**。
- `cpp:526`: `.order_size_usdc = static_cast<double>(it.size_pUSD_micro)` —— audit 里把 micro 当 usdc 落账, **审计数字也错了 1e6 倍**。

### 4.2 定性

- 分类: **派生约束 (单位契约) ABI 演进后失配** —— 不在"红线"清单, 但**直接架空了 R-6/7/8 三条 cap 真红线**。
- still-valid: caps 红线本身 valid, **但实现被 drift 废掉**。advisory gate 长期遮住下单路径, A2 解封才现形 (route review 印证)。
- 咬人风险: **已实际咬人 (A2 永久拒单)**。比"红线过时"更隐蔽 —— 红线"看起来还在", 实则失效。
- **根因 = R-4 (schema 静默变更红线) 漏网**: `size_usdc → size_pUSD_micro` 是一次单位语义变更, **没有任何机制强制审计所有下游比较点**。这正是 R-4 该覆盖却没覆盖的盲区。

### 4.3 建议

1. **立即修** (GM 主线 A2 配套): caps/bankroll/book_depth 全部 micro 化, 或在比较点统一换算; audit 落账修 1e6。**此条派给老韩 RM owner 复核 (风控主权), 老郭不替写**。
2. **立派生纪律 (新 ADR)**: "字段单位/scaling ABI 变更 → 必须 grep 全部比较点 + audit 落账点单位一致性" —— 挂到 R-4 schema 红线下作执行细则。
3. **R-4 红线措辞收紧**: schema 静默变更显式涵盖"字段单位/scaling 语义", 不止字段增删。

---

## 5. RM reject enum (B 命名空间) 审计

- ADR-004 把 RM 的 21 条 reject 用 R-1~R-17 编号 (R-6/7/8=caps, R-15/16/17=liquidity, R-12=EDGE_NEGATED_BY_SLIPPAGE)。
- 分类: ② **工程枚举**, 不是红线。但**借用了 R-N 前缀**, 与 §8 真红线撞号。
- still-valid: 枚举本身 valid (ADR-004 短路顺序仲裁正确, 我当时拍的 B 选项成立)。
- 咬人风险: **中 (命名歧义)** —— sub-agent 读"R-12"无法判断是 WSS 红线还是滑点拒单码。
- 建议: **RM enum 改 `RJ-` 前缀 (Reject)**, 与 §8 `RL-` 彻底分开。枚举数值不动, 只改文档/注释引用前缀。

---

## 6. 背景演进对约束的影响 (GM 特别关切)

| 演进 | 受影响约束 | 是否仍需要 | 判定 |
|---|---|---|---|
| **撤 Rust → 全 C++** | 老孙 signer Rust 设计、老张 crate 选型 | 约束=禁 Rust, 仍有效 (ADR-041 §69 重申) | ✅ 红线有效, 无失配 |
| **单 binary (ADR-011)** | R-2 (paper/live 同 binary)、R-7 (build-time mode 锁) | 仍需要; transport 分离已落地 | ✅ 有效。注意 R-2/R-7 在 ADR-011 语境的编号又与 §8/B 撞号 (命名空间问题) |
| **不上链 (defer onchain)** | signer 部署/nonce/whitelist/gas 全 deferred | 约束=paper 不触链, 仍有效 | ✅ 有效。设计保留作 future activation, 无过时约束 |
| **live-only 重构 (M1 A 方向)** | advisory gate / has_real_fair 双防线 | D3 已澄清: 拆 has_real_fair, advisory 改 live-only | ✅ D3 裁定后无失配, 需固化入 ADR |
| **ABI v0.6 (size 单位)** | RM caps (§4) | **失配 (见 §4)** | ❌ **唯一真失配, 待修** |

**结论: 撤 Rust / 单 binary / 不上链 三大演进都没留下过时红线 —— 设计保留机制 (future activation) 处理得当。唯一演进失配是 ABI v0.6 单位 drift (§4), 与"重构"无关, 是字段改名没审计下游。**

---

## 7. 红线体系整体健康度评级

| 维度 | 评级 | 依据 |
|---|---|---|
| 真红线是否臃肿 | **健康** | §8 9 条真红线 8 条是地基, 只 R-33 该降级。ML-R 6 条全有效。不臃肿 |
| 真红线是否过时 | **健康** | 撤Rust/单binary/不上链均无过时残留, future activation 机制得当 |
| 派生约束被误当红线 | **2 处** | R-33 (文档SOP升红线) + ML-R2 传导污染。均可修 |
| ABI/架构演进后失配 | **1 处严重** | §4 单位契约 drift 静默架空 caps 红线 (已咬人) |
| **命名治理** | **不健康 (结构性)** | R-NN 三套命名空间同号异义, 是 ML-R2 误传导的系统性土壤 |

**一句话: 红线内容地基稳、不臃肿、基本不过时; 但红线的"编号治理"和"引用纪律"是软肋 —— 咬人的两个实例 (ML-R2 / 单位契约) 根都在"语义在文档间漂移没人守边界", 不在红线本身。修体系比修条文重要。**

---

## 8. 仲裁建议 (待 GM 拍板入 ADR)

> 老郭只给仲裁建议, 不动代码; RM caps 单位修复属老韩风控主权, 派给老韩。

1. **【拆命名空间, P0】** §8 红线 → `RL-NN`; RM reject enum → `RJ-NN`; 老胡 risk-registry → `RR-NN`。一次性重编, 小米执行映射表, 全库 grep 替换引用。**消除 ML-R2 式误传导的系统土壤。**
2. **【修单位失配, P0】** §4 caps/bankroll/audit 单位统一 → **派老韩 (RM owner)** 复核修复 + 立"ABI 单位变更必 audit 全比较点"派生纪律 (挂 R-4 下)。
3. **【固化 D3, P0】** ML-R2 定义粘原文"仅离线训练" + D3 "advisory=不路由live≠不产paper fill" 入 ADR, 立"红线引用必粘原文禁转述"纪律。
4. **【R-33 降级, P1】** 四维扫描从 §8 一票否决移到文档治理 SOP (ADR-028)。
5. **【红线分级标注, P1】** 每条红线显式标 ①真红线/②派生约束 + 解释权人 (谁有权澄清边界), 防下一个字面执行咬人。
6. **【R-11/R-20 补边界注, P1】** R-11 补"隔离≠不产fill"; R-20 补"联调期 INFERRED_* 退化合规非违例"。

---

**审计人 老郭, 2026-05-30 07:26 UTC。结论待 GM 拍板, 拍板后由小米落 ADR + 重编命名空间。**
