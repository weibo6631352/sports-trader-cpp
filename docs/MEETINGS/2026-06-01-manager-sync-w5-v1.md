# W5 主管周同步会议纪要 v1 (2026-06-01)

- **主持:** 老胡 (PM, E-026)
- **出席:**
  - 老雷 (GM, 主管制度发起人)
  - 老钱 (CPO, dial-in)
  - 5 主管: 老周 (A, E-001) / 老韩 (B, E-009) / 小梁 (C, E-018) / 小余 (D, E-022) / 老胡 (E, E-026)
  - 老郭 (F 顾问团协调人, E-016, 架构一票否决权)
  - 小林 (HR, E-046, dial-in 跨单元招聘)
- **时长:** 45min (ADR-005 §4.2 节奏, 接 W5 试点首周末)
- **会议性质:** ADR-005 立的"主管周同步"**第一次正式会议** (W5 试点 → W6 硬约束转换前最后一次软约束运行)
- **关联:**
  - ADR-005 §4.2 主管间协作 (议程模板)
  - W5 Wave 24 commit `3ab5dfb` 312/312 测试 (上一 wave 全公司战果)
  - INCIDENTS log 错 #9 (本次会议核心复盘事项)
  - 6 份就职宣言 (`docs/RESEARCH/<persona>-manager-mandate-v1.md` × 5 主管 + 老郭协调)
- **议程:** 7 段 (§1 ~ §7) + 决议清单 §8 + 升级议题 §9
- **PM 主持声明:** 我集成 5 主管 + 老郭 6 个独立 input, 不替任何人说话; 决议归 GM ack, 架构归老郭 ack, 产品归老钱 ack.

---

## §1 ADR-004 + BUG-W5-001 patch 4 会签 closeout

> **议题状态:** 等 4 签 — 老韩 first review + 老郭 架构 + 老高 PR review + GM ack.
> **占位说明:** 4 主管/协调各自独立交 input, 本节为集成框, 我不替任一人 ack.

### 1.1 patch 概要 (W5 Wave 24 战果, 全员可见)

- **ADR-004 patch (老沈, 82 行):** RiskGateway::evaluate() position_caps 与 liquidity 短路顺序互换 (B 选项落地), ABI 0 改动, 21 RejectCode + 9 sub_reason enum 不动, 5 文件 + 1 regression test
- **BUG-W5-001 patch (老沈, 97 行 P0):** next_audit_id seq 10B → 6B big-endian (shift 0~40 < 48 远离 UB), 修复 -O2 把 UB 优化为 0 导致 audit_id 全零, R-1 invariant 恢复

### 1.2 会签状态 (4 主管 input 集成)

| 签字人 | 角色 | input 状态 | ack 摘要 |
|---|---|---|---|
| 老韩 (B 主管) | first review (RM owner) | **✓ ack** (input file `2026-06-01-input-laohan-B-status.md` §2.1) | ADR-004 patch 顺序按 B 选项落地, ABI 0 改动, enum 不动, R-1 invariant 恢复, 14 RM release pre-existing fail 全部恢复 |
| 老郭 (F 协调) | 架构评审 (R-1 invariant + ABI 影响) | **✓ ack** (input file `2026-06-01-input-laoguo-F-status.md` §2.1/2.2) | ADR-004 patch PASS + BUG-W5-001 P0 PASS, 三方 review 一致 (老韩 B 主管 + 老高 PR + 老郭 F) |
| 老高 (F 顾问) | PR review v1.1 grep (R-7 / R-11 / R-20 / HMAC 4 反模式) | **✓ ack** (commit `3ab5dfb` 含 `laogao-pr-review-v1.1.md` 213 行) | PR review v1.1 全 5 grep + 4 cpp 反模式 enforce, CLAUDE.md §10 enforce 时机老郭 forward GM 拍 |
| GM 老雷 | 业务 ack (P0 紧急 patch 闭环) | 待 6/1 EOD | 3 主管/协调 ack 已到位, 仅待 GM 业务签字 closeout |

### 1.3 主持人备注

- BUG-W5-001 是 GM 老雷直接派老沈的 P0 紧急 patch (ADR-005 §3.2 例外: 紧急 P0 < 2h 响应), **不算越主管**, 制度允许
- 但 W6 起 P0 仍走主管层 (老韩) 优先, 老雷直派仅限 < 2h critical
- 4 会签 closeout 后我把 ADR-004 状态改为 **Closed-Implemented** 并归档

---

## §2 3 candidate ADR 评审 (老郭主持)

> **主持人交棒:** §2 我把麦给老郭. 老郭是 F 协调人, 架构一票否决权所有候选 ADR 必先经他评审才上 GM.
> **input 集成:** 老郭 6/1 会前交 `docs/MEETINGS/2026-06-01-input-laoguo-F-status.md` §3, 3 ADR 评审齐全 (24h 申辩窗口已开, 暂无反对入档). 本节为老郭 input 摘要 + 引用.

### 2.1 ADR-006 候选: HTTP client cpp-httplib vs cpr

- **背景:** Polymarket REST client v0.1 用 cpp-httplib vs cpr
- **老郭三方共识 (老周 + 老李 + 老郭):** **倾向 cpp-httplib**
  - header-only 编译加速 ~30% (paper E2E 跑通是 W5 首要)
  - 无 libcurl 系统依赖 (跨洋部署环境一致性更强)
  - paper mode 起步够用 (异步 + 连接池 paper 阶段 not critical)
  - 升级路径: live mode (M5+) 若需要更激进连接池可 W6+ 重新评估 cpr
- **老郭 evaluate:** PASS, 三方共识扎实, 数字论据齐 (编译 -30% + 系统依赖 -1 项)
- **建议老雷拍:** ADR-006 正式立, 老周 W6 落 cpp-httplib FetchContent
- **GM ack deadline:** 6/1 EOD

### 2.2 ADR-007 候选: VirtualMatcher 切 Mode A 灰度

- **背景:** 现 VirtualMatcher 用固定 Bernoulli clamp(0.50, 0.65), 升级 Mode A 灰度 (queue position model)?
- **老郭三方共识 (小袁 + 老韩 + 老郭):** **倾向 W5 末 paper E2E 跑通后再切, W6 末实施**
  - paper E2E 必须先验证整链路 (signal → RM → matcher → audit), 再换 sampler 避免变量混
  - 小袁 v0.1 §A.3 灰度建议已含 "paper E2E 跑通后" 前置条件
  - 老韩 RM 视角 — fill_rate 5 因子模型对 position_cap / liquidity 触发频率有连带影响, 需 paper 基线 baseline 对比
- **老郭 evaluate:** PASS, 三方共识扎实, 实施时机条件明确
- **建议老雷拍:** ADR-007 正式立, 实施时机 W6 末 (paper E2E 跑通 + 小袁 灰度计划 v0.2 出)
- **GM ack deadline:** 6/1 EOD

### 2.3 ADR-008 候选: ~~Pinnacle 路径~~ → Goalserve fair value de-vig 算法选型

> **重大变更:** 原 ADR-008 候选议题"Pinnacle 路径 A 官方 vs C 老彭手工"被 GM 错 #9 触发**正式撤回**. ADR-008 改为 "Goalserve fair value de-vig 算法选型".

- **撤回理由:** 小段 v3 (`xiaoduan-goalserve-official-doc-v3.md` §7) 四维扫描后反转结论 — Goalserve 8-9 家 retail bookmaker 单源够 fair value 锚源, inplay JSON 含 bet365 value_eu, `getodds?cat=<sport>_10` 含 8-9 家 bookmaker
- **老郭四选项评审 (老郭 input §3.3 完整表):**
  - 选项 A 多 book 中位数 (~10 行) — 简单但 retail margin 5-12% 偏差大 → ❌
  - 选项 B 多 book 加权均值 — 比 A 精细但校准需历史数据, paper 起步用不上 → ❌
  - 选项 C Shin de-vig — 理论最优但 ~150 行黑盒, M4.5 audit 难 → ⏸ 升级版
  - **选项 D multiplicative de-vig (~30 行) — paper 够 + audit 易 → ✓ 三方共识**
- **老郭三方共识 (小梁 + 老韩 + 老郭):** **选 D**
  - 小梁建模视角 — D 是简化 Shin, 数学性质保留 (单调 + 凸性), 校准 1 个参数
  - 老韩风控 audit 视角 — ~30 行人脑 review 可读, 入 audit log 不黑盒
  - 老高 PR review v1.1 grep 视角 — 30 行体量适合 CI grep 红线 (Shin 150 行难 grep)
  - 升级路径: M4.5+ 真有精度需求时升级 C Shin
- **老郭 evaluate:** PASS, audit / 工程 / 建模 三视角对齐
- **建议老雷拍:** ADR-008 正式立, 小梁 W6 落代码 (~30 行 + 小程 spec v0.2 + 老彭历史校准支撑)
- **owner:** 小梁 (建模) + 老彭 (行业 prior 顾问) + 小段 (数据源)
- **GM ack deadline:** 6/1 EOD

### 2.4 主持人备注

- 3 ADR 候选评审本身归老郭 ([F input 完整全文]), 决议归 GM
- 老郭 W5 起 ADR-005 §3.3 协调改进落地: **任何 ADR 仲裁结论前留 24h 申辩窗口 + 申辩入档** (透明可追溯, W4 ADR-004 老韩偏快教训)
- ADR-008 撤回是本次会议**最大单一决议** — 直接 cascade 影响 §3 (GM 错 #9 复盘) + §4 (Pinnacle 决议正式撤回) + 周报 §4 SSOT 版本演进段 (R-41 永久 enforcement 首次落地实例)

---

## §3 GM 错 #9 公开复盘 (老雷)

> **议题归属:** GM 老雷自主复盘 (CLAUDE.md §3 核心价值观 "公开失败" 铁律 GM 自己示范).
> **占位说明:** 本节 GM 自己 ack 4 条永久 enforcement, 我不替老雷写, 仅集成框 + 主持节奏.

### 3.1 错 #9 摘要 (GM 已入 incidents log)

- **场景:** 2026-05-28 W5 末 Wave 24 push 后用户问"下阶段计划", GM 列了"Pinnacle CSV 路径 A 官方 vs C 老彭手工"作为待拍决议
- **错在哪:** 小段 W3 末 v3 已**反转结论** ("Goalserve 单源就够"), GM 引用的是 v2.1 早期 Pinnacle 缺失 + UK Racing 没 Pinnacle 的纠结, **跨 wave 没读最新版**
- **是用户直接纠正的:** "我们不是有 goalserver 吗"
- **真正的下阶段 open 决议是:** Goalserve 8-9 家 retail bookmaker fair value de-vig 算法 (归小梁), 不是 Pinnacle 路径 A/B/C (后者是 GM 凭空发明的决议)

### 3.2 GM 公开 ack 永久 enforcement 4 条 (老雷自己念, 全员见证)

1. **GM 做"下阶段计划"前查 `docs/RESEARCH/` 各 owner 最新 vN 版本号** (而非按记忆引用), 跨 wave / 跨 sprint 重大决议必查
2. **数据源 owner (小段 / 老李 / 老彭) 每次出 v(N+1) 必须在 Sprint progress 周报里点名"推翻了 vN 的 X 结论"** — 让 GM 不漏读
3. **老胡 (PM) 周报加 "本周 SSOT 版本演进" 段** (vN → vN+1 推翻清单) — 本节为本次会议正式落地, 周报模板升 v2 (`docs/META/weekly-report-template-v2.md` 我 owner, 见 §8 决议)
4. **老胡 W5 周报修正 Pinnacle 决议** — 小段 v3 已 closeout, 改为小梁 de-vig 算法选型 (与 §2.3 ADR-008 同步)

### 3.3 主持人备注

- GM 错 #9 与 GM 错 #8 (越级派单) **并发触发** — 共同把"主管制度 + SSOT 版本治理"两块短板暴露
- W6 起 GM 5 题自检 4 题升 5 题已落 (ADR-005), 但 GM 错 #9 揭示**第 6 题需要立**: "我引用的 SSOT 是各 owner 最新 vN 吗?" — 提请老雷会后评估是否升 6 题

---

## §4 W5 主管层试点 KPI 实测

> **议题归属:** 我作为 PM 主管负责出数字, 不替任何人加色 (KPI 数字 = 数据说话, 不是 "我感觉").

### 4.1 实测数字 (W5 W4 EOW → W5 末 6/01 截止)

| KPI | 目标 (ADR-005) | W5 实测 | 状态 |
|---|---|---|---|
| **主管派单覆盖率** | ≥ 70% (W5 试点软约束, W6 起 100% 硬约束) | **100%** (W5 Wave 24 6 IC 任务 + 4 follow-up 任务**全部** spec by 主管) | **超目标** |
| **主管 SLA (24h ack)** | > 90% (ADR-005 §4.1) | **TBD** (4 会签 §1 待集成, 我 6/1 EOD 收 4 主管 + 老郭 ack 后回填) | 待回填 |
| **GM SLA (24h 拍板)** | > 95% (ADR-005 §6 决策机制) | **TBD** (GM 老雷 ack 计数 6/1 EOD 截止收, 含 §2 3 ADR + §3 4 enforcement) | 待回填 |
| **协商会次数** | 每周 1 次 (老胡主持每周二 30min, 我就职宣言 §4.2 E-W5-M01) | **0** (W5 试点首周未启动, **6/02 (W5 二) 第 1 次启动**) | W6 起常态化 |
| **IC 越主管直接找 GM 次数** | = 0 (W6 起硬约束) | **0 检测到** (W5 Wave 24 全部派单走主管) | 达标 |
| **GM 越主管直接派 IC 次数** | ≤ 例外场景 4 条 (ADR-005 §3.2) | **1 次** (BUG-W5-001 老沈 P0 紧急 < 2h, **属于 §3.2 例外**) | 制度允许 |
| **主管帽 W5 cpp 行数** | = 0 (mandate §2.2 §6 不亲力亲为) | **5 主管全 = 0** (老周 input §1 / 老韩 input §1 / 小梁 input §1 / 小余 input §1 + 老胡 PM 帽下产出 100% 文档) | **达标** (制度立后首周全员守住) |
| **1:1 启动率** | weekly 1:1 全单元覆盖 | 小余 4/5 (大幅改善 W4 0/5), 其余主管见各 input | W6 全单元 100% 覆盖 |

### 4.2 主持人备注

- W5 试点首周**主管派单覆盖率 100% 超目标 30 个百分点** — 6 主管就职宣言 (W4 EOW 6/06 deadline) 实际**全部提前 W4 EOW 当天 (2026-05-28) 交付**, 制度执行力强于预期
- 但**协商会 0 次**是软肋 — ADR-005 §4.1 跨单元接口对接走主管协商, W5 没开过, 6 主管接口需求 (各自就职宣言 §5) 全部 24h ack 期望未真实跑过 → **6/02 (W5 二) 必须开第 1 次, 否则 W6 硬约束转换无 baseline**
- GM SLA + 主管 SLA 数字 6/1 EOD 截止收, 6/02 (W5 二) 协商会前 24h 我补齐 (周报 §5 主管派单 KPI 段)

---

## §5 各主管 W5 末 status (5 主管 + 1 协调 独立 input)

> **议题归属:** 5 主管 + 老郭各自独立交 input file (`docs/MEETINGS/2026-06-01-input-*.md`), 我集成 ≤ 50 字摘要 + 引用全文路径. 不替任一人说话.

### 5.1 老周 (A 系统工程部主管) status

- **input file:** `docs/MEETINGS/2026-06-01-input-laozhou-A-status.md`
- **摘要:** A 单元 W5 cpp 净增 **2094 行 + 36 unit tests** (老李 PolymarketClient 1050 行 + 小冯 PM WSS 1044 行, commit `3ab5dfb`, 全 312/312 ctest); 主管帽 W5 cpp lines = **0** (mandate 红线守住); ADR-006 cpp-httplib + ADR-007 W5 末切 Mode A 三方共识入老郭 input §3; 跨主管 5 ASK 待 6/1 EOD 回填; HC-04/05 JD 起草进行中.

### 5.2 老韩 (B 风控合规部主管) status

- **input file:** `docs/MEETINGS/2026-06-01-input-laohan-B-status.md`
- **摘要:** B 单元 W5 老沈 cpp 净增 **179 行** (ADR-004 patch 82 + BUG-W5-001 P0 patch 97), 14 RM release 模式 pre-existing fail 全部恢复; 老唐 W5-B-02 BLAKE3 + W5-B-03 R-7 switch 联调 TBD (我不替老唐承诺); 主管帽 W5 cpp lines = **0** (mandate 红线守住); HC-04 risk-quant Q3 评委 ack 进度待 §7 上桌.

### 5.3 小梁 (C 量化研究部主管) status

- **input file:** `docs/MEETINGS/2026-06-01-input-xiaoliang-C-status.md`
- **摘要:** C 单元 W5 5 ticket: 1 已交 (C-05 小袁 microstructure v0.1) + 3 W5 末收口 (C-01 P0-01 review / C-02 Mode A 灰度 W6 头切 / C-04 PIT CI 起草) + 1 撤回重派 (**C-03 Pinnacle CSV 撤回 → ADR-008 立 Goalserve de-vig 算法选型 D 选项, 小梁 W6 起建模**); 单元载荷小蒋红 / 小袁黄 / 小程黄 / 老彭绿 (W5 主线撤回短期低载, W6 接 de-vig 校准升档); HC-03 小吕 8/1 入职预期不变.

### 5.4 小余 (D 数据基础设施部主管) status

- **input file:** `docs/MEETINGS/2026-06-01-input-xiaoyu-D-status.md`
- **摘要:** D 单元 W5 cpp 净增 **1044 行 + 8 unit tests** (小冯 PM WSS, A/D 跨主管联签); D-W5-01 Pinnacle ingestion **撤回 (GM 错 #9)** → 老彭 W6 起做 Goalserve 历史校准; 主管帽 W5 cpp lines = **0** (mandate 红线守住); 1:1 启动 W4 0/5 → W5 4/5 (大幅改善); 主管 KPI 5.5 → 7; 小田归属仲裁 ASK-A-4 / ASK-D-1 同议题 6/2 EOD 升老雷.

### 5.5 老胡 (E 产品业务保障部主管, 本人) status

- **W5 末交:** 6 主管就职宣言全部到位 (E 单元 8 IC 派单 8/8 覆盖率 100%, 含小宫 D1 干跑稿激活); 小宋 integration test framework v0.1 **1132 行 + 14 测试** ✓ (commit `3ab5dfb`, ASK-A-5 / ASK-B-3 fixture 已 ack)
- **W5 主管帽统筹动作:** master gantt v1 + risk registry v2.2 → v2.3 + **本会议主持** (主管周同步 v1) + 周报模板 v2 SSOT 版本演进段落地 (`docs/META/weekly-report-template-v2.md`) + sprint-02 §1.6.5.bis 更新
- **HC-08 qa-integration-engineer 提请:** 小宋 9/10 红, M3 W14 integration test 启动前 2 天 buffer, 6/1 主管周同步上桌, 6/06 EOW JD 与老周联签起草
- **W5 实测主管帽 KPI 自评:** 18/30 (60%) → W7 复评目标 24/30 (80%)

### 5.6 老郭 (F 顾问团协调人) status

- **input file:** `docs/MEETINGS/2026-06-01-input-laoguo-F-status.md`
- **摘要:** F 顾问活跃 rate **5/6 = 83%** (>mandate 70% 目标); ADR-004 patch + BUG-W5-001 + 老高 PR review v1.1 + 老徐 escalate-flow v0.2 **4 first review 全 PASS** (commit `3ab5dfb`); 3 ADR 候选评审完成 + 24h 申辩窗口已开暂无反对入档 (见 §2.1/2.2/2.3); CLAUDE.md §10 enforce 升级 forward GM 6/1 当面拍; 月度主管轮值 GM 助理 7 月 (老郭, 待老雷拍 §7 E-04 议题).

### 5.7 主持人备注

- 6 status 各自独立 input 文件已交付 (5 主管 + 老郭), **不再有"占位句"** — 全部为本人 input 摘要 + 引用路径, 我**集成不替写**
- W5 末 6 主管/协调**全部交付**, ADR-005 制度立后**第一周即达 100% 主管制运行 + 6 份 input 全部到位**
- 4 主管帽 W5 cpp lines 全部 = **0** (老周 / 老韩 / 小梁 / 小余 / 我 5 主管 mandate 红线全守住), 主管不亲力亲为铁律首周硬约束跑通

---

## §6 跨主管接口 ASK 进度 (24h ack 表)

> **议题归属:** 5 主管就职宣言 §5 跨单元接口需求扫表, 我集成 6/01 当日 ack 状态.
> **占位说明:** 每条 ASK 由发起主管负责催 ack, 我只统计是否 24h 内 ack 到位.

| # | 发起主管 | 协商对象 | 接口 | 期望 ack | 6/01 实测 ack | 状态 |
|---|---|---|---|---|---|---|
| ASK-A-1 | 老周 (A) | 老韩 (B) | RiskGateway::evaluate() ABI stable (RiskDecision struct + reject_code enum 21) | 6/1 EOD | TBD | 待 6/1 EOD 回填 |
| ASK-A-2 | 老周 (A) | 小梁 (C) | SignalOutput struct schema (signal_id / intent / confidence / 4ts / feature_snapshot_id) | 6/1 EOD | TBD | 待回填 |
| ASK-A-3 | 老周 (A) | 小余 (D) | Goalserve client struct + 4ts 字段位置 | 6/1 EOD | TBD | 待回填 |
| ASK-A-4 | 老周 (A) | 小余 (D) | 小田 (E-008) 归属仲裁 (兼 A + D) | 6/2 EOD | TBD | 6/2 升老雷 |
| ASK-A-5 | 老周 (A) | 老胡 (E) | integration test fixture (paper_audit.wal verify + hash chain + 4ts 单调) | 6/1 EOD | **ack** (小宋 W5-E-03 1132 行已交, 见 commit 3ab5dfb) | **完成** |
| ASK-B-1 | 老韩 (B) | 老周 (A) | v0.6 §17.1.1 PREGAME_FAR 15000ms 取齐 + RM 接 SlippageModel/WAL/PaperSigner header | 6/1 EOD | TBD | 待回填 |
| ASK-B-2 | 老韩 (B) | 小梁 (C) | signal output 接 RM RiskIntent schema | 6/1 EOD | TBD | 待回填 |
| ASK-B-3 | 老韩 (B) | 老胡 (E) | integration test fixture (paper E2E 60s + audit chain verify) | 6/1 EOD | **ack** (同 ASK-A-5) | **完成** |
| ASK-C-1 | 小梁 (C) | 老周 (A) | signal_iface schema (老李 polymarket-client → SignalEngine) | 6/1 EOD | TBD | 待回填 |
| ASK-C-2 | 小梁 (C) | 老韩 (B) | RM SIGNAL_CI_TOO_LOW reject_code | 6/1 EOD | TBD | 待回填 |
| ASK-C-3 | 小梁 (C) | 小余 (D) | **ADR-008 撤回 Pinnacle → Goalserve fair value 数据源 hand-off** (小段 v3 → 小梁 de-vig 建模) | 6/2 EOD | **C+D 双向 ack** (小梁 input §1 W5-C-03 撤回 + 小余 input §1 D-W5-01 撤回 + 老彭 W6 起做 Goalserve 历史校准) | **完成** |
| ASK-D-1 | 小余 (D) | 老周 (A) | 小田归属 (与 ASK-A-4 同) | 6/2 EOD | A+D 双向确认升级 (老周 input §5 / 小余 input §5 同议题升老雷) | 6/2 升老雷 |
| ASK-D-2 | 小余 (D) | 小梁 (C) | feature schema 32 (小邓 ML hook v0.1) | 6/2 EOD | TBD | 待回填 |
| ASK-E-1 | 老胡 (E) | 全 4 主管 | 协商会 W5 二 6/02 第 1 次开 | 6/2 EOD | **commit** (4 主管会上 ack 出席) | 待 6/02 验证 |
| ASK-E-2 | 老胡 (E) | 老钱 (CPO) | PRD v2 (小杜) cross-ref 业务能力 v1 | 6/1 EOD | TBD (老钱 dial-in 直答) | 待回填 |
| ASK-F-1 | 老郭 (F) | 老周 (A) | R-12 vCPU 分配 0-6 终评 | 6/1 EOD | TBD | 待回填 |
| ASK-F-2 | 老郭 (F) | 老高 (F) | PR review v1.1 R-20+R-11+HMAC 4 反模式 grep | 7/8 | **ack** (W5 Wave 24 commit 含 `laogao-pr-review-v1.1.md` 213 行) | **完成** |

### 6.1 主持人备注

- 17 ASK, **4 已 ack 完成** (ASK-A-5/B-3 小宋 integration ✓ + ASK-F-2 老高 PR review v1.1 ✓ + **ASK-C-3 Pinnacle 撤回 C+D 双向 ack ✓**), **13 待 6/1 EOD 回填**
- **2 项 (ASK-A-4 + ASK-D-1 小田归属)** 是同一议题双向发起, **必须 6/2 EOD 升老雷拍板** (不能拖到 W6 硬约束转换)
- **ASK-C-3 (Pinnacle → Goalserve hand-off)** 6/1 会上四方共识 (小段 v3 / 小梁 C input / 小余 D input / 老郭 ADR-008 evaluate), 撤回 + 立 de-vig 选项 D 同步完成, 小梁 W6 起建模, 老彭 W6 起历史校准
- W5 试点末 ASK 完成率 4/17 = **24%**, **13 项 6/1 EOD 必须真实 ack 出数字** — 这是 W6 硬约束转换前唯一可量化主管 SLA 数据点

---

## §7 W6 启动决议

> **议题归属:** ADR-005 §8 推行时间线 W5 → W6 硬约束转换关键决议, GM 拍板, 我集成日期.

### 7.1 决议

1. **主管层 W5 试点 → W6 正式 (硬约束)** — 自 2026-07-13 (W6 一) 起, GM 5 题自检第 5 题 (越主管直接派 IC) fail 拒派单, IC 越主管找 GM 拒接, 例外按 ADR-005 §3.2
2. **月度主管轮值 GM 助理 6 月启动** — 老周 (6 月) → 老韩 (7 月) → 小梁 (8 月) → 小余 (9 月) → 老胡 (10 月), 每月主管 dial-in 老雷学 GM 视角
3. **HR 招聘 HC-04/05/06/07/08 JD W4 EOW (6/06) 起草**
   - HC-04 risk-quant (老韩, Q3 9 月入职)
   - HC-04 (老周自加重号) wal-storage-engineer (老周, Q3 8/15 入职)
   - HC-05 observability-2 (老周, Q3 8/15 入职)
   - HC-06 ml-data-engineer (小余, Q3 入职)
   - HC-07 market-data QA (HR pulse 原议, Q3 9 月)
   - HC-08 qa-integration-engineer (老胡 + 老周联签, Q3 9/1 入职, M3 W14 启动前 2 天 buffer)
   - **W5 一 (6/1) HR 小林对齐编号** (老周 HC-04 与老韩 HC-04 撞号, 需重编)
4. **W6 派单 50+ ticket (老胡 master backlog 维护)** — W5 末 W6 backlog v1 我交, 周报 §6 下周 backlog 段公示

### 7.2 主持人备注

- W6 硬约束转换是 ADR-005 制度立后**首次真正考验**, W5 试点 100% 覆盖率超目标但**协商会 0 次 + 14 跨主管 ASK 待 6/1 EOD 回填** = 软肋未跑通
- 6/02 (W5 二) 协商会**必须开 + 必须出会议纪要**, 否则 W6 一硬约束无 baseline
- HC-08 入职 9/1 vs M3 启动 9/3 = **2 天 buffer**, 任何 HR 延误链路: JD 6/06 起草 → 6/15 发布 → 7/1 候选 ≥ 5 → 8/1 终面 → 8/15 offer → 9/1 入职 → **6/06 JD 起草延误 = M3 启动延** → **W5 一 (6/1) HR 小林必接**

---

## §8 决议清单

> **本次会议拍板的所有决议 + ADR 编号 + owner**, GM 老雷会后 24h 内 ack 入 `docs/ADR/`.

| # | 决议 | ADR 编号 | Owner | Ack 节奏 |
|---|---|---|---|---|
| D-01 | ADR-004 patch + BUG-W5-001 patch 4 会签 closeout (老韩 / 老郭 / 老高 / GM) | ADR-004 状态改 Closed-Implemented | 老韩 + 老郭 | 6/1 EOD 4 签 |
| D-02 | ADR-006 候选 HTTP client cpp-httplib (老郭倾向, 待 GM ack) | ADR-006 (新) | 老郭 + GM | 6/1 EOD |
| D-03 | ADR-007 候选 VirtualMatcher 切 Mode A 灰度 — W5 末再切 (老郭倾向) | ADR-007 (新) | 老郭 + 小梁 + 老韩 | 6/1 EOD |
| D-04 | **ADR-008 撤回 Pinnacle 路径 (小段 v3 推翻 v2.1) → 立 Goalserve fair value de-vig 算法选型** | ADR-008 (新方向, 立) | 小梁 (建模) + 老彭 (顾问) + 小段 (数据源) | W6 起建模, W7 末 v0.1 spec |
| D-05 | **GM 错 #9 永久 enforcement 4 条 ack** — GM 查 vN + owner 点名推翻 + 周报 SSOT 段 + Pinnacle 撤回 | INCIDENT log 已入, 周报模板 v2 落地 | 老雷 (GM 自承) + 老胡 (周报模板 owner) | 即时 ack |
| D-06 | **周报模板升 v2** (加 §4 本周 SSOT 版本演进段) — `docs/META/weekly-report-template-v2.md` | (无 ADR, 制度落地文档) | 老胡 (PM, 本人) | W5 五 (8/1) 周报首次套用 v2 |
| D-07 | **主管层 W5 试点 → W6 硬约束转换** — 2026-07-13 (W6 一) 起, 5 题自检第 5 题 enforce | ADR-005 §8 推行时间线 | 老雷 (GM) + 5 主管 | 即时 ack |
| D-08 | 月度主管轮值 GM 助理 6 月启动 (老周) | ADR-005 §4.3 | 老周 (6 月) | 即时 ack |
| D-09 | HC-04 重编号 + 5 JD W4 EOW (6/06) 起草 | (HR 制度) | 小林 (HR) + 5 主管联签 | 6/06 EOW JD 草稿 |
| D-10 | 6/02 (W5 二) 第 1 次需求-工程协商会启动 (老胡主持 30min, 4 主管出席) | ADR-005 §6 决策机制 | 老胡 (PM) + 4 主管 | 6/02 14:00 |
| D-11 | 14 跨主管 ASK 6/1 EOD 回填 ack 状态 | (会议纪要追溯) | 各 ASK 发起主管 | 6/1 EOD |
| D-12 | ASK-A-4 + ASK-D-1 小田归属 6/2 EOD 升老雷拍板 | (跨主管协商不下) | 老周 + 小余 → 老雷 | 6/2 EOD |

---

## §9 待 GM 拍板 / 升级议题

> **协商不下需要 GM 后续 24h 决议的事项**, 我主持时 escalate, 老雷会后单独 ack.

| # | 议题 | 升级理由 | GM 拍板 deadline |
|---|---|---|---|
| E-01 | 小田 (E-008) 兼 A + D 归属仲裁 | 老周 (A) 与小余 (D) 协商不下, ASK-A-4 + ASK-D-1 同议题 | 6/2 EOD |
| E-02 | GM 自检 4 题升 5 题 → 是否再升 6 题 (含"SSOT vN 最新版查询") | GM 错 #9 暴露 5 题不够覆盖 SSOT 版本治理 | 6/06 EOW 评估 |
| E-03 | HC-08 qa-integration-engineer 与 HR pulse HC-07 market-data QA 编号冲突 | 老胡提请的 HC-08 与小林 HC-07 是两个不同岗, 编号待对齐 | 6/1 EOD (W5 一 HR 小林对齐) |
| E-04 | ADR-008 撤回 Pinnacle 后 — 老彭 (E-030) Pinnacle CSV 数据源选型工作量 5/10 → 0, 是否调岗 | 老彭 W3-W4 主线撤掉, W5+ 接 Goalserve de-vig 顾问角色, 工作量可能 < 3/10 | 小梁 (C 主管) + 小林 (HR) 月末评估 |
| E-05 | W6 硬约束转换后 — 若 IC 仍越主管直接找 GM, GM 拒接 → 是否会触发 IC 流失 | ADR-005 R-43 风险 (IC 越主管), W6 一首次硬约束触发 | W6 一首日 (6/01 + 14 = 6/15 模拟? 实际 W6 是 2026-07-13) |

---

## §10 会议结束 + 下次会议

- **会议结束时间:** 2026-06-01 (W5 一) 10:45 (45min 议程)
- **下次主管周同步:** 2026-06-08 (W6 一, **硬约束首次运行**), 议程模板 §1 ~ §7 沿用本 v1
- **6/02 (W5 二) 14:00 需求-工程协商会** (老胡主持, 4 主管出席, 30min): 14 跨主管 ASK 真实跑一轮
- **6/06 (W4 EOW) JD 起草:** HC-04/05/06/07/08 5 份 JD 草稿 (小林 owner, 5 主管联签)
- **会议纪要 owner:** 老胡 (PM), 6/01 EOD 入 `docs/MEETINGS/`, 小米 (E-040) 归档
- **GM ack deadline:** 2026-06-02 EOD (会后 24h, ADR-005 §6 决策机制)

---

## §11 主持人复盘 (老胡自评)

- **集成 6 个独立 input 不替任一人说话:** 5 主管 + 老郭各自 status 占位等本人 6/1 EOD 当面更新, 我集成框 + 不加色
- **ADR 评审麦给老郭:** §2 3 ADR 候选我不替老郭 ack, 他主持评审 GM 拍板
- **战略议题不替老雷拍:** §3 GM 错 #9 复盘 + §7 W6 启动决议归 GM, 我只主持议程节奏
- **产品方向不替老钱拍:** §6 ASK-E-2 PRD v2 cross-ref 老钱 dial-in 直答, 我不替 CPO 说话
- **GM 错 #2 防范成功:** 单 PM 不替全员说话, 6 主管/协调真发言 (本次纪要为占位等本人填写, 非我编造)

---

**主持:** 老胡 (PM, E-026)
**纪要 owner:** 老胡 (PM)
**归档:** 小米 (E-040, doc-curator)
**GM ack:** 待老雷 6/02 EOD 入 `docs/ADR/` 或会议 closeout 标记
**最后更新:** 2026-06-01 by 老胡
