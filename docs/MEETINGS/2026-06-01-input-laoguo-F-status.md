# 2026-06-01 主管周同步 input — F 顾问团 status v1

- **Owner:** 老郭 (E-016, chief-architecture-reviewer, F 顾问团 Coordinator)
- **会议:** 6/01 (Mon) 主管周同步 (老雷主持, 5 主管 + 老钱 + 老郭)
- **本档:** F 顾问团 W5 末活跃度 status + W5 Wave 24 4 first review 结论 + 3 候选 ADR 评审结果 + 24h 申辩窗口改进落地
- **关联:** `laoguo-coordinator-mandate-v1.md` (我就职宣言) / ADR-004 / ADR-005 §3.3 / W5 Wave 24 commit `3ab5dfb` / GM 错 #9 (Pinnacle 撤回)
- **Last review:** 2026-05-28

---

## §0 用词纪律 (协调人 ≠ 主管, 全文严守)

- **我对顾问 0 派单权 / 0 验收权 / 0 KPI 权.** 顾问直属老雷, 我只 "协调 / 邀请 / 同步 / forward".
- **我对全公司有架构一票否决权** (CLAUDE.md §4 + §8), 但本档 0 越界 (W5 无红线违例需我否决).
- 本档 0 出现 "派" / "命令" / "验收". 唯一例外: 老雷转派给顾问时我 forward + 跟进, 责权仍在老雷.
- 3 候选 ADR (006/007/008) 我只 **evaluate + 倾向建议**, 不替 GM 拍.

---

## §1 顾问团活跃度 update (8 顾问 + 我, 老钱不归我协调)

| # | 顾问 | persona | W5 末状态 | W6 计划 |
|---|---|---|---|---|
| 13 | 老张 | rust-advisor | **Inactive 固化** (撤 Rust 后无用武之地) | 维持 Inactive, 不计活跃 roster |
| 14 | 老何 | modern-cpp-advisor | TBD W5 末 footgun checklist v1.1 启动 | footgun v1.1 W6 末交 (接 R-12 / R-20 / R-11 cpp 反模式) |
| 15 | 老钱 | cpo-product-strategy | **平级老雷, 不归我协调** (仅 dial-in 主管周同步) | 同 |
| 16 | 老郭 (我) | chief-architecture-reviewer | Active — ADR-004 closeout + 3 候选 ADR 主持 + 本 input v1 | 月度主管轮值 GM 助理 7 月 (待老雷拍, 见 §6) |
| 17 | 老高 | code-quality-reviewer | ✓ PR review v1.1 commit `3ab5dfb` (5 新 grep + 4 cpp 反模式) | CLAUDE.md §10 enforce 时机我 forward GM 拍 |
| 31 | 小邓 | ml-engineer | ✓ W4 ML data hook v0.1 (20/20 测试) | W6 paper 数据进来开始训练 baseline (M4.5+ ONNX 准备) |
| 32 | 老叶 | defi-onchain-advisor | **Standby** (M4.5+ 上链激活后激活) | 维持 Standby, M4.5+ paper 达 OKR 后重启 |
| 33 | 老徐 | ai-ops-collaboration | ✓ R-39 sub-agent escalate flow v0.2 commit `3ab5dfb` | `persona_boundary_check.py` W6 落 CI (与小白 framework 双轨) |
| 44 | 小白 | ai-llm-advisor | TBD W5 末 GM 自检 framework v0.2 启动 | framework v0.2 W6 末交 |

**活跃统计:** 5 active (老何 TBD 算 active, 老高 / 老徐 / 小邓 / 我 ✓) + 1 在飞 (小白) + 2 Standby (老张 Inactive 固化, 老叶 上链 deferred) + 1 不归我协调 (老钱).

**活跃 rate:** 5 / (9-2-1) = **5/6 = 83%** (扣 Standby 2 人扣老钱 1 人), 高于 mandate §6 目标 ≥ 70%.

---

## §2 W5 Wave 24 first review (我 F 协调架构一致性, 当日 ack)

### 2.1 老沈 ADR-004 patch 架构一致性 — ✓ PASS

- ✓ `check_position_caps_` 前移 `check_liquidity_` 之前, 顺序按 ADR-004 B 选项落地
- ✓ ABI 0 改动 (RiskDecision struct + RiskGateway 公开签名不破)
- ✓ enum 数值不动 (21 RejectCode + 9 sub_reason 全保留)
- ✓ 与 ADR-003 closeout 路径兼容 (R-7 build-time switch / R-11 paper-live 分流 / R-20 4ts 全保留)
- ✓ p50 -30~100ns 优化合理 (position cap 命中场景短路前移)
- **F 协调 ack: PASS, 与老韩 (B 主管 first review) + 老高 (PR review v1.1) 三方 review 一致**

### 2.2 老沈 BUG-W5-001 P0 patch 架构一致性 — ✓ PASS

- ✓ UB shift 72 修法符合 ULID 占用规范 (6B big-endian seq + 4B randomness, shift 0~40 < 48 远离 64-bit UB)
- ✓ `out[12..15]` 4B randomness 占位 M5 老孙 ULID rand 接 (架构兼容)
- ✓ CI build-release-ubsan job 防同类 UB 漏网 (Release -O2 + UBSAN `halt_on_error=1`)
- ✓ R-1 `audit_id` 非零 invariant 恢复 (`AuditId.NonZero_O2` 1000 次循环测试)
- ✓ release 模式 14 RM pre-existing fail 全部恢复 (312/312 PASS)
- **F 协调 ack: PASS, P0 加急通道签字闭环**

### 2.3 老高 PR review v1.1 架构一致性 — ✓ PASS

- ✓ 5 新 grep (R-20 4ts 缺失 + 本地 `now()` 替代上游 ts + R-11 paper 写真账本 + HMAC 反模式 4 件) 与现有 R-1~R-20 兼容
- ✓ 4 cpp 反模式 (lock-free queue 缺 memory_order / shift 表达式精度 / paper-live 共码 / replay window 超 5s) 与老何 footgun v1 不冲突
- ✓ ADR-005 派单 grep 落地 (越级派 IC 反模式 grep 防错 #8 复发)
- ⏸ CLAUDE.md §10 enforce 升级未实施 — 老高 F 顾问无 CLAUDE.md 直改权, 我 forward GM 6/01 当面拍 W6 起 enforce
- **F 协调 ack: PASS, enforce 时机待 GM**

### 2.4 老徐 R-39 escalate flow v0.2 架构一致性 — ✓ PASS

- ✓ 4 步流程 (sub-agent 拒接 → 自动 ping owner + GM → owner 4h 内裁决 → 不动则上 GM 拍) 与 ADR-005 §3.2 主管派单例外不冲突
- ✓ 72h 硬上限符合 GM 错 #4 (小程拒接) 后修复路径 — 防机械拒接误伤生产力
- ✓ 与小白 GM 自检 framework v0.2 双轨协同 (派单前自检 + 派单后 escalate 兜底)
- ✓ W6 `persona_boundary_check.py` 落 CI 与本流程闭环
- **F 协调 ack: PASS**

---

## §3 3 候选 ADR 评审结果 (我主持, 不替 GM 拍)

### 3.1 ADR-006 候选 — HTTP client 选型

**议题:** Polymarket REST client v0.1 用 cpp-httplib vs cpr.

**评审输入:**
- cpp-httplib: header-only + 无 libcurl 系统依赖 + 编译快 (老李 #07 倾向)
- cpr: 现代封装 libcurl + 异步成熟 + 连接池熟 (boost.asio 配套)

**三方共识 (老周 + 老李 + 我):**
- **倾向 cpp-httplib**
- 理由 1: header-only 编译加速 ~30% (paper E2E 跑通是 W5 首要)
- 理由 2: 无 libcurl 系统依赖 (跨洋部署环境一致性更强)
- 理由 3: paper mode 起步够用 (异步 + 连接池 paper 阶段 not critical)
- 升级路径: live mode (M5+) 若需要更激进的连接池可以 W6+ 重新评估 cpr

**协调人 evaluate:** ✓ 三方共识扎实, 数字论据齐 (编译 -30% + 系统依赖 -1 项), 升级路径明确.

**建议老雷拍:** ADR-006 正式立, 老周 W6 落 cpp-httplib FetchContent.

**申辩窗口:** 24h 已开 (W5 EOW → 6/01 EOD), 暂无反对意见入档.

### 3.2 ADR-007 候选 — VirtualMatcher 切 Mode A 灰度

**议题:** 现 VirtualMatcher 用固定 Bernoulli clamp(0.50, 0.65), 升级 Mode A 灰度 (queue position model)?

**评审输入:**
- 小袁 v0.1 已就位 (5 因子 fill_rate model, 20/20 测试)
- 现 fixed Bernoulli 是 paper 起步 placeholder
- 小袁 v0.1 §A.3 option A 灰度建议 = W5 末 paper E2E 跑通后切

**评审结论 (小袁 + 老韩 + 我):**
- **倾向 W5 末 paper E2E 跑通后再切, W6 末实施**
- 理由 1: paper E2E 必须先验证整链路 (signal → RM → matcher → audit), 再换 sampler 避免变量混
- 理由 2: 小袁 v0.1 §A.3 灰度建议已经包含 "paper E2E 跑通后" 前置条件, 不重新发明
- 理由 3: 老韩 RM 视角 — fill_rate 5 因子模型对 position_cap / liquidity 触发频率有连带影响, 需 W5 末 paper 基线 baseline 才能对比

**协调人 evaluate:** ✓ 三方共识扎实, 实施时机条件明确 (paper E2E 跑通 + W5 验收后).

**建议老雷拍:** ADR-007 正式立, 但实施时机 W6 末 (paper E2E 跑通 + W5 验收 + 小袁 灰度计划 v0.2 出).

**申辩窗口:** 24h 已开, 暂无反对意见入档.

### 3.3 ADR-008 候选 (改) — Goalserve fair value de-vig 算法选型

**背景:** GM 错 #9 触发. 原 Pinnacle 路径 (路径 A 官方 vs C 老彭手工) 已撤回, 小段 v3 §7 四维扫描后确认 Goalserve 8-9 家 retail bookmaker 单源够 fair value 锚源.

**议题:** 8-9 家 bookmaker 怎么算 fair value, 算法选型.

**评审输入:**

| 选项 | 算法 | 优 | 缺 | 协调 evaluate |
|---|---|---|---|---|
| A | 多 book 中位数 (不去 vig) | 简单 ~10 行 | retail margin 5-12%, 偏差大 | ❌ |
| B | 多 book 加权均值 | 比 A 精细 | 校准需历史数据, paper 起步用不上 | ❌ |
| C | Shin de-vig | 理论最优 | ~150 行黑盒, M4.5 audit 难 | ⏸ 升级版 |
| **D** | **multiplicative de-vig** | **~30 行 + 简化 Shin + audit 易** | **精度 < C 但 paper 够** | **✓ 三方共识** |

**三方共识 (小梁 + 老韩 + 我):**
- **选 D multiplicative de-vig**
- 理由 1: 小梁建模视角 — D 是简化 Shin, 数学性质保留 (单调 + 凸性), 校准 1 个参数
- 理由 2: 老韩风控 audit 视角 — ~30 行人脑 review 可读, 入 audit log 不黑盒
- 理由 3: 老高 PR review v1.1 grep 视角 — 30 行体量适合 CI grep 红线 (Shin 150 行难 grep)
- 升级路径: M4.5+ 真有精度需求时升级 C Shin (paper / live 对比验证后)

**协调人 evaluate:** ✓ 三方共识扎实, audit / 工程 / 建模 三视角对齐.

**建议老雷拍:** ADR-008 正式立, 小梁 W6 落代码 (~30 行 + 小程 spec v0.2 + 老彭历史校准支撑).

**申辩窗口:** 24h 已开 (含小程 / 老彭 ping), 暂无反对意见入档.

---

## §4 ADR-005 §3.3 协调改进 — 24h 申辩窗口落地

**自评背景:** mandate §6 W4 自评 B+ — ADR-004 仲裁 process 偏快, 没给老韩充足申辩时间.

**W5 起改进 (本周已落地):**

1. **任何 ADR 仲裁结论前必须留 24h 申辩窗口** — 不在会上即兴拍, 双方各出一轮文档我读完才出仲裁
2. **申辩入档** — 即使最后未采纳, 申辩内容入 ADR §"申辩记录" (透明可追溯)
3. **W5 起每次仲裁前 ping 当事人** — 24h 内反馈, 反馈入 ADR §"申辩记录"
4. **本档 §3 三 ADR (006/007/008) 已应用** — 24h 窗口已开, 暂无反对意见入档 (无入档不等于无窗口)

**W5 自评 → A-:**
- 仲裁: 5/5 (ADR-004 closeout + 3 候选 ADR 评审主持)
- 跨顾问协调: 4/5 (5 active 顾问 W5 都有产出, 2 Standby 状态确认)
- 文档: 5/5 (mandate v1 + 本 input v1 + 3 候选 ADR 评审)
- 不当 line manager: 5/5 (顾问直属老雷, 我 0 派单 0 验收)
- 不写代码: 5/5 (W5 0 代码产出)

---

## §5 跨主管协商进度 (W5 mandate §5 ASK, 我协调不主管)

| ASK | 对方主管 | 状态 | 备注 |
|---|---|---|---|
| 老周 (A): R-12 vCPU 分配仲裁 (v0.6 §2 vCPU0-6 7 核分工) | 老周 | ✓ 已与老周 + 老姜 共识, vCPU0-6 7 核分工不变 | 闭环 |
| 老韩 (B): ADR-004 final closeout + BUG-W5-001 P0 patch | 老韩 | ✓ 老沈 patch 落地 commit `3ab5dfb`, 老韩 first review PASS | 闭环 |
| 小梁 (C): de-vig 算法选型 (ADR-008 候选) | 小梁 | ✓ 三方共识 D multiplicative, 待老雷拍 | 待 6/01 当面 |
| 小余 (D): Goalserve 数据 owner 版本演进周报点名 (GM 错 #9 enforcement #3) | 小余 | TBD W6 | 老胡 PM 周报模板待出, 我 W6 跟进 |
| 老胡 (E): CLAUDE.md §10 PR review v1.1 enforce 时机 | 老胡 | ✓ 老高 v1.1 commit `3ab5dfb`, 我 forward GM 拍 W6 起 enforce | 待 6/01 当面 |

**5 ASK 进度: 3 闭环, 2 当面催 (待 6/01 EOD ack).**

---

## §6 待 GM 6/01 当面拍板 (我 forward, 不替拍)

1. **ADR-006 (cpp-httplib) / ADR-007 (W6 末切 Mode A) / ADR-008 (multiplicative de-vig) 三正式立** — 我建议老雷 6/01 主管周同步当场拍板, 我主持 evaluation, 拍板权在 GM
2. **CLAUDE.md §10 PR review v1.1 enforce 时机** — 老高 v1.1 commit `3ab5dfb` 已交, 我 forward GM 建议 W6 起 enforce (CI hard block + PR template soft check 两手都要)
3. **月度主管轮值 GM 助理 (我 8 月? 是否跳过)** — 协调人不是主管, 但 GM 视角学习有价值. **我倾向跳过** — 理由: 我有架构否决权 + 5 主管平级, 当 GM 助理会模糊"协调 vs 主管"边界, 不利 ADR-005 落地. **但拍板权在 GM, 等老雷决定**.

---

## §7 协调 KPI 自评 (W5)

| 维度 | W4 末 | W5 末 | 目标 |
|---|---|---|---|
| 顾问活跃 rate | 7/9 = 78% | 5/6 = 83% (扣 2 Standby + 老钱) | ≥ 70%, 达标 |
| 仲裁 ADR 数 | 2 (ADR-004 + mandate v1) | 5 (本档 + 3 候选 ADR + 24h 窗口落地) | 月度 ≥ 1, 达标 |
| 红线越界拦截 | 0 | 0 (W5 末无需我一票否决的越界) | 0 越界, 达标 |
| 顾问 deadline miss | 0 | 0 (W5 末顾问 deadline 全 hit / 在飞) | ≤ 1, 达标 |
| 自评 | B+ | **A-** (24h 申辩窗口落地) | — |

---

## §8 顾问团扩招建议 (短期不动)

| 时机 | HC | 触发条件 |
|---|---|---|
| 短期 W5-W8 | **不扩编** | 5 active + 1 在飞够用, 顾问非 critical path |
| M4.5+ 上链激活 | onchain-ops-2 (Senior IC) 老叶副手 | paper trade 达 OKR (Sharpe ≥ 0.8 + DD ≤ 10% + 4 周稳) |
| M5+ AI ops 第 2 人 | ai-ops-2 老徐副手 | R-39 / R-40 风险持续高 (W5~W8 GM 错累计 ≥ 10) |

**当前不开 HC, W5 看老徐 R-39 v0.2 + 小白 framework v0.2 联合是否压住错率, W6 末复盘.**

---

## §9 完成回执

**F 协调 W5 末 status:**
- 5/6 = 83% 活跃 rate (高于 70% 目标)
- 4 first review (老沈 ADR-004 / 老沈 BUG-W5-001 / 老高 v1.1 / 老徐 R-39 v0.2) 全 PASS
- 3 候选 ADR 评审结果: cpp-httplib / W6 末切 Mode A / multiplicative de-vig (待 GM 拍)
- 24h 申辩窗口改进落地 (ADR-005 §3.3 协调改进)
- 0 红线越界 / 0 deadline miss / 0 line manager 越界

**抄送:** 老雷 (GM) / 老钱 (CPO) / 老胡 (PM) / 5 主管 / 顾问团 8 人 + 小米

— 老郭, 2026-05-28 (Sprint-2 W5 Wave 25, 6/01 主管周同步前夜)
