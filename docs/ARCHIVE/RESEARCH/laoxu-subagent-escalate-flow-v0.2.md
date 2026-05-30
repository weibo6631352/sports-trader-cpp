# R-39 sub-agent 自我纠错 escalate 流程 v0.2

- **Owner:** 老徐 (#33 ai-ops-collaboration, F 顾问团)
- **First review:** 老郭 (#16 F 协调人)
- **Final ack:** 老雷 (GM)
- **Date:** 2026-05-28 (W5 Wave 24)
- **Status:** Draft v0.2 (待老郭 review → GM ack)
- **触发:** 老胡 risk-registry v2.2 R-39 + 老郭 coordinator-mandate v1 §4 W5-F-02
- **关联文档:**
  - `docs/RESEARCH/laoguo-coordinator-mandate-v1.md` §3.7 + §4 W5-F-02
  - `docs/RESEARCH/laohu-risk-registry-v2.2.md` R-39
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #4 (小程拒接 P0-01)
  - `docs/ADR/2026-05-28-department-manager-mandate.md` ADR-005 §3.2 例外, §3.3 GM 5 题自检
  - CLAUDE.md §7 行为铁律 (4 题自检 → 5 题自检) + §8 红线

---

## 0. 摘要 (TL;DR)

R-39 评分 8 = 2×4 (低 P × 中影响). 风险**不是 sub-agent 拒接本身**, 而是 **拒接后无标准化 escalate 路径**, 长期可能:

1. **沉默死锁** — 全员都拒接同一类合理派单 (e.g. 紧急 hotfix 撞 persona 边界), GM 反复 retry 仍 fail
2. **误伤生产力** — sub-agent 机械按 persona §拒绝任务 拒接, 但实际派单合理 (临时补位 / Sprint 借调 / 边界临时妥协)
3. **GM 自我消化** — 没人 challenge GM 直接 retry, 拒接信号丢失, GM 错 #4 同类错再犯

**v0.2 设计**: 5 类拒接原因分类 → 4 步 escalate (即时分类 → 4h 主管 review → 24h GM 拍板 → 48h 全体争议会), 配套 2 工具 (`persona_boundary_check.py` 前置 + `escalate-decision-log.md` 后置), 4 KPI 监控. 与 ADR-005 §3.2 例外不冲突 (例外本身就是绕路, 不再二次 escalate). 与 GM 错 #4 enforcement (5 题自检) 协同: 自检挡前置, escalate 挡漏网.

**绝对死锁防护**: 任何 sub-agent task 待定 > 72h, 默认 GM 强制拍板"接受 sub-agent 拒接 + 任务 cancel + post-mortem 入 INCIDENTS".

---

## 1. Part 1 — 拒接合理性分类 (5 类)

sub-agent 收到派单后, 若选择"派给别人" / 拒接 / 部分接 / 反提案, 老徐归 5 类:

| # | 类型 | 例 | 合理性 | 处置 |
|---|---|---|---|---|
| **C1** | **边界违反** | 小程拒 C++ stub 代码 (persona §拒绝任务 = "代码 / 回测"). #4 案例. | **100% 合理** | 不 escalate, 修 prompt 重派给正确 persona (e.g. IC pool 小卢) |
| **C2** | **越权派单** | GM 跳过老周直派老李 (违反 ADR-005 §3.1 3 层流程). #8 案例. | **100% 合理** | 不 escalate, 拒接默认 ack, GM 自检 5 题第 5 题 + 重派经主管 |
| **C3** | **资源不足** | 派单要 1000 行 spec 但 IC 工作量预算 ≤ 500 行 / Sprint | **部分合理** | escalate Step 2, 评估妥协方案 (减 scope / 分多个 wave / 借 IC pool) |
| **C4** | **依赖未就绪** | 派单要接 X module 但 X 还在 W6 才交付 | **部分合理** | escalate Step 2, 评估 mock 依赖 / 或延后到 X 交付后再做 |
| **C5** | **理解偏差** | sub-agent 误读 prompt 拒接, 但拒接理由不成立 (e.g. prompt 写 "出 spec 不写代码", sub-agent 误读为"要写代码"拒接) | **不合理 (误伤)** | escalate Step 2, 主管 review 后修 prompt 澄清重派 |

### 1.1 分类决策树 (sub-agent 拒接回汇时必带)

```
sub-agent 拒接回汇必填 4 项:
  1. 拒接理由 (1 句)
  2. 引用 persona file 行号 (e.g. .claude/agents/19-quant-signal-research.md L42-45 "拒绝任务: 代码 / 回测")
  3. 推荐替代 persona (e.g. "派给 IC pool 小卢")
  4. 自评分类 (C1 / C2 / C3 / C4 / C5)
```

GM / 主管收到拒接 → 即时核对自评分类 → C1/C2 静默 ack 重派, C3/C4/C5 走 Step 2.

### 1.2 边界 ≠ 不可妥协 (老胡 R-39 缓解 #2 原文)

persona §拒绝任务 是**静态约束**, 业务需求是**动态变量**. 长期必有冲突. v0.2 流程允许"主管 + GM 协商后**临时**突破边界"(C3/C4 场景), 但**必须 escalate 走流程**, 不允许 GM 单方面强压 sub-agent 接 (那是 GM 错 #4 + #8 复合).

---

## 2. Part 2 — 4 步 escalate 流程

### 2.1 流程图

```
[sub-agent 拒接回汇]
    │
    │ (回汇必带: 理由 + persona 行号 + 替代 persona + 自评分类)
    ▼
[Step 1] 即时分类核对 (派单人 = 主管 or GM)
    │
    ├─ C1 边界违反 → ack 拒接, 修 prompt 重派 (END)
    ├─ C2 越权派单 → ack 拒接, GM 5 题自检 + 经主管重派 (END)
    │
    └─ C3 资源 / C4 依赖 / C5 理解偏差
        │
        ▼
[Step 2] 4h 内主管 review (单元主管, F 顾问团则老郭 review)
    │
    ├─ 主管同意 sub-agent (拒接成立) → 修 prompt / 减 scope / mock 依赖 重派 (END)
    ├─ 主管不同意 sub-agent (派单合理) → 上 Step 3
    │
    └─ 主管 4h 不 ack → 自动升 Step 3 (死锁防护)
        │
        ▼
[Step 3] 24h 内 GM 拍板
    │
    ├─ GM 同意主管 (强制派单) → sub-agent 必接, persona 边界临时突破, 入 ADR
    ├─ GM 同意 sub-agent (主管错估) → 修 prompt 重派 (END)
    │
    └─ GM 24h 不拍板 → 自动升 Step 4 (死锁防护)
        │
        ▼
[Step 4] 48h 内全体争议会 (老雷召集)
    │
    ├─ 走 CLAUDE.md §5 月末全体 Review 节奏 (或紧急临时会)
    ├─ 决议入 ADR, 永久 enforcement
    │
    └─ 72h 仍未决议 → GM 强制"接受拒接 + 任务 cancel + post-mortem" (END)
```

### 2.2 各 Step 责任

| Step | 主责 | 协责 | 输出 |
|---|---|---|---|
| Step 1 | sub-agent (自评分类) + 派单人 (核对) | — | 1 行回执到 `docs/META/escalate-decision-log.md` |
| Step 2 | 单元主管 (战斗单元) / 老郭 (顾问团) | sub-agent 自身 | review 决议 (同意/不同意/妥协方案) |
| Step 3 | GM 老雷 | 主管 + sub-agent | 拍板 (强制/同意拒接), 入 ADR if 突破边界 |
| Step 4 | GM 老雷 召集 | 全主管 + 老郭 + 老胡 | 全体 ADR |

### 2.3 各 Step 决议落库

每次 escalate 走完, 派单人 (GM / 主管) 必须在 `docs/META/escalate-decision-log.md` append 1 行:

```
| 日期 | sub-agent | 派单人 | 拒接分类 | Step 终止 | 决议 | 后续动作 |
|---|---|---|---|---|---|---|
| 2026-05-28 | 小程 #19 | GM 老雷 | C1 (边界违反) | Step 1 | ack 拒接, 重派 IC pool 小卢 | persona §拒绝任务 不动, GM 5 题自检 W6 起 enforce |
```

---

## 3. Part 3 — 与 ADR-005 §3.2 例外的关系

ADR-005 §3.2 列 GM 可直派 IC 的 **4 类例外**:

1. 顾问团成员
2. 紧急 P0 (< 2h 响应)
3. 主管自己
4. 跨多单元统筹

### 3.1 R-39 escalate 对例外**不适用**

例外**本身就是绕路** (3 层流程的合法快捷), 不再走 escalate. 即:

- 顾问团成员 sub-agent 拒接 → GM 直接 ack (老郭 协调, 不走 Step 2 主管 review)
- 紧急 P0 sub-agent 拒接 → GM 1h 内强压 (不走 4h Step 2), 但事后 post-mortem 必入 INCIDENTS
- 主管自己拒接 → GM 直接拍板 (主管自己就是 Step 2 的 reviewer, 不可能自己 review 自己)
- 跨多单元统筹 (老胡 PM / 老郭 评审 / 老高 PR review) → 这类任务本身**不在 5 战斗单元主管职责内**, 走老雷直接 ack

### 3.2 但 4 类例外**仍受 5 题自检约束**

ADR-005 §3.3 GM 5 题自检 (本 R-39 v0.2 同步原文):

1. 一面之词背书?
2. 单 agent 替全员说话?
3. 让 agent 看老项目 / 撤销方案?
4. 派单 prompt 越 persona "拒绝任务" 边界?
5. 是否越主管直接派 IC?

例外场景里**第 5 题豁免** (例外本身就是越主管), 但**第 4 题不豁免** — 即使紧急 P0, GM 仍必须扫 persona §拒绝任务 段. 漏扫 = GM 错 #4 复刻.

---

## 4. Part 4 — 时间窗 + 死锁防护

### 4.1 时间窗汇总

| Step | SLA | 超时处置 |
|---|---|---|
| Step 1 (分类核对) | 即时 (sub-agent 回汇当下) | N/A (回汇 = 必带分类) |
| Step 2 (主管 review) | 4h 内 ack | 自动升 Step 3 |
| Step 3 (GM 拍板) | 24h 内拍板 | 自动升 Step 4 |
| Step 4 (全体会) | 48h 内开会 + 决议 | **绝对死锁防护**: 72h 总时长上限, GM 强制"接受拒接 + 任务 cancel" |
| **任务总滞留** | **72h 硬上限** | post-mortem 入 INCIDENTS, R-39 触发 P2 alert |

### 4.2 死锁防护具体规则

1. **Step 2 主管不在线 (休假 / 跨时区)**: 老胡 PM 代 ack "见到回邮再决", 但 4h SLA 仍计时, 4h 内无主管亲自 ack → 自动升 Step 3
2. **Step 3 GM 不在线**: 老郭 (F 协调人) 临时拍板权 (限本 escalate, 不扩权), 24h SLA 内必决
3. **Step 4 全体会无法召集**: 老胡 PM 转书面投票 (主管 + 顾问团), 48h 截止
4. **任何 step 待 72h 仍无决议**: 默认 GM 强制 cancel 任务 + 入 INCIDENTS R-39 实例 + 下 sprint retro 必讨论

### 4.3 紧急豁免

若 P0 RM HALTED / 安全事件 / 用户原话指令 < 2h 响应, **escalate 流程整体豁免**, GM 直派直命令, 事后 6h 内补 post-mortem.

---

## 5. Part 5 — 与 GM 错 #4 enforcement (5 题自检) 协同

### 5.1 关系: 自检挡前置, escalate 挡漏网

```
[GM 派单前]
    │
    ▼
[5 题自检] (CLAUDE.md §7 第 8 铁律 + ADR-005 §3.3)
    │
    ├─ 任一 fail → 拒派, 重设计 prompt (#4 #8 教训)
    │
    └─ 全 pass → 派单发出
        │
        ▼
    [sub-agent 收到 prompt]
        │
        ├─ 接 → 正常工作
        │
        └─ 拒接 → 进入本 v0.2 escalate 流程 (Step 1 ~ Step 4)
```

### 5.2 双重保护

- **自检失败 + escalate 流程触发**: 说明 5 题自检漏了, post-mortem 必查 GM 自检 framework (小白 v0.2 W5 EOW 待交) 是否覆盖不全
- **自检通过但 escalate 仍触发**: 说明 prompt 设计合理但 sub-agent persona 边界已与业务脱节, post-mortem 必查 persona file 是否需更新 (小米 doc-curator 接手归档)

### 5.3 与小白 GM 自检 framework v0.2 互锁

小白 W5 EOW 交 `docs/RESEARCH/xiaobai-gm-self-audit-framework-v0.2.md`, 内容含派单前 self-audit prompt template + sub-agent 召唤前 pre-flight check. 老徐本 v0.2 escalate 与小白 framework **双轨**: 前置 (自检 framework) + 后置 (escalate), 一前一后挡 GM 错 #4 同类风险.

---

## 6. Part 6 — Tooling 支持

### 6.1 `tests/ci_grep/persona_boundary_check.py` (老徐 W5-W6 落)

**作用**: PR 提交前自动扫派单 prompt vs persona §拒绝任务 段, 防止 GM 错 #4 同类错入 commit.

**输入**:
- prompt 文本 (commit message 或 派单 markdown)
- 目标 persona file path

**逻辑** (伪码, 不写真代码, 仅约束):

```
1. 读 persona file, 抽 ## 拒绝任务 段, 取每行 keyword (e.g. "代码", "回测")
2. 扫 prompt 文本, grep 是否含被拒 keyword
3. 命中 → fail, 输出 "派单 prompt 含被拒 keyword '代码', 该 persona §拒绝任务 行号 L42-45, 派单越界, 拒提交"
4. 不命中 → pass
```

**落地节奏**:
- W5 (本 wave): 老徐设计 spec, append 到本文件 §6.1 末尾 (不立 v0.3)
- W6: 老徐写 Python 脚本 + 小宋 CI hook 接入
- W7+: 进 CI 红线, fail 拒 PR merge

**约束**: 仅 grep keyword, 不做语义判断 (语义判断留给小白 GM 自检 framework). 误报率允许 < 10%, false negative 不允许.

### 6.2 `docs/META/escalate-decision-log.md` (W5 起 append-only)

**作用**: 每次 escalate 走完, 派单人必 append 1 行决议. append-only, 不删不改 (小米 doc-curator 监督).

**Schema**:

```
| 日期 | sub-agent | 派单人 | 拒接分类 (C1-C5) | Step 终止 | 决议 | 后续动作 | 关联 INCIDENT |
```

**首行 (本 wave append)**:

```
| 2026-05-28 | 小程 #19 | GM 老雷 | C1 (边界违反) | Step 1 | ack 拒接, 重派 IC pool 小卢 | persona §拒绝任务 L42-45 不动, GM 5 题自检 W6 起硬约束 | INCIDENT 错 #4 |
```

**维护人**: 老徐 (主) + 小米 (归档) + 老胡 (周报抽样核 KPI).

### 6.3 不立第 3 个工具

W5 仅落 6.1 + 6.2 2 个工具. 若 KPI (Part 7) 显示 escalate 频率过高 (> 5 次 / Sprint), W6 评估再加工具 (e.g. escalate 自动 ping 机器人 / SLA timer dashboard). 当前 over-engineering 风险高于收益.

---

## 7. Part 7 — KPI (老胡 周报必带)

| # | KPI | 期望 | 触发动作 |
|---|---|---|---|
| K1 | sub-agent 拒接次数 / Sprint | < 3 | > 5 → P2 alert, 老徐 + 老胡 review escalate 流程 (R-39 v0.2 §1.3 触发条件复用) |
| K2 | 主管 review SLA (Step 2 4h ack 达成率) | > 90% | < 80% → P3 alert, 主管 1:1 with 老雷, 评估主管工作量是否超载 |
| K3 | GM 拍板 SLA (Step 3 24h 达成率) | > 95% | < 80% → P2 alert, GM 自检 framework review (小白 framework v0.2 复盘) |
| K4 | 全体争议会次数 / Sprint | < 1 | ≥ 2 → P1 alert, 制度根因排查 (Step 1-3 哪环失效) |

### 7.1 KPI 数据源

- K1: `docs/META/escalate-decision-log.md` 行计数 (老胡周报)
- K2/K3: log 里 "Step 终止" + 日期戳推算 SLA
- K4: 全体会次数 = log 里 "Step 4" 标记行数

### 7.2 KPI 与 R-39 触发联动

R-39 老胡 v2.2 原始触发: 单 Sprint 内 sub-agent 拒接次数 > 5 且合理性占比 < 70% → P2 alert. v0.2 细化:

- 合理性占比 = (C1 + C2 行数) / 全部行数 (C1/C2 = 100% 合理, C3/C4 部分合理记 0.5, C5 不合理记 0)
- 占比 < 70% → P2 alert + 派老徐 review (现有缓解链路不变)

---

## 8. Part 8 — 与 R-40 (HR registry 三方源 drift) 的关系

R-39 与 R-40 都是 "sub-agent 制度风险" 大类, 但 **范围独立**:

| 维度 | R-39 (escalate) | R-40 (registry drift) |
|---|---|---|
| 触发源 | sub-agent 拒接 (单次事件) | registry / AGENT / .claude 三方源对齐 (持续状态) |
| 主缓解工具 | `persona_boundary_check.py` + `escalate-decision-log.md` | `registry_consistency.py` (小宋 W4-12 已落) |
| Owner | 老徐 + 老胡 + 老雷 | 小宋 + 小米 + 小林 |
| v0.2 范围 | 处理 | **不处理** (小宋 CI grep 已 cover, 老徐不越界) |

### 8.1 不重叠点

- R-39 是**动态决策流程**, R-40 是**静态数据一致性**
- 工具完全不同, 不复用 (persona_boundary_check 扫 prompt vs persona, registry_consistency 扫三方源 count)
- KPI 也不复用 (R-39 看次数 / SLA, R-40 看 CI fail 率)

### 8.2 共同风险点 (未来 v0.3 可能合并)

若未来出现"sub-agent 拒接因为 registry 误把 persona 标 Deprecated"这种 R-39 + R-40 复合事件, 老徐 + 小宋 v0.3 合并讨论. 当前 v0.2 不处理.

---

## 9. 验收清单 (W5 EOW)

- [x] §1 5 类拒接分类 + 决策树
- [x] §2 4 步 escalate 流程图 + 责任表 + 落库 schema
- [x] §3 与 ADR-005 §3.2 例外关系澄清
- [x] §4 时间窗 + 4 条死锁防护 + 紧急豁免
- [x] §5 与 GM 错 #4 5 题自检协同 + 与小白 framework v0.2 互锁
- [x] §6 tooling 2 (`persona_boundary_check.py` spec + `escalate-decision-log.md` schema)
- [x] §7 4 KPI + 数据源 + R-39 触发联动
- [x] §8 与 R-40 独立 (老徐不越界)
- [ ] 老郭 first review (本 wave 内 ack)
- [ ] GM 老雷 ack (本 wave 内)
- [ ] W6 老徐落 `tests/ci_grep/persona_boundary_check.py` + 小宋 CI hook
- [ ] W6 首次 `docs/META/escalate-decision-log.md` 立 + 补错 #4 历史行
- [ ] W5 起老胡周报加 K1-K4 KPI

---

## 10. 不耻下问 (本 v0.2 已 ping)

- **@老郭** (F 协调人, W5-F-02 spec 出题人): first review 本文件 §1-§9 是否覆盖 R-39 spec 全部. 重点核 §2 4 步流程是否漏 step.
- **@小程 #19** (#4 拒接案例 owner): 自评本 v0.2 §1.1 决策树是否还原她拒接当时的决策路径. 如果有遗漏, append 到 §1 后注释.
- **@老胡 #26** (R-39 risk owner + PM 周报 owner): 核 §7 KPI 4 项是否可落周报, K1-K4 数据源是否可拿. 若 escalate-decision-log.md schema 不够支撑 KPI, 反馈到 §6.2 调整.
- **@小白 #44** (LLM 视角 + GM 自检 framework v0.2 owner): 核 §5 双轨设计 (前置自检 + 后置 escalate) 是否与小白 framework v0.2 兼容, 若 framework 含 sub-agent pre-flight check 也覆盖 persona 边界扫, 与本 §6.1 工具是否重复. 重复则合并到 framework, 不重复保持双轨.

---

## 11. 边界声明 (老徐 self)

- 本 v0.2 **不写代码** — `persona_boundary_check.py` 实现交 W6 老徐 + 小宋. v0.2 只出 spec.
- 本 v0.2 **不替主管 / GM 决策**, 只设计流程 + RACI + 工具.
- 本 v0.2 **不动 ADR-005 §3.2 例外** (例外是 GM 拍板的, 老徐尊重).
- 本 v0.2 **不动 persona §拒绝任务 段**, 只设计扫这段的工具.
- 本 v0.2 **不处理 R-40** (小宋 cover, 老徐不越界).

---

## 12. 完成回执 (按老郭 mandate §4 W5-F-02 格式)

- **任务**: W5-F-02 R-39 sub-agent escalate 流程 v0.2
- **deliverable**: `docs/RESEARCH/laoxu-subagent-escalate-flow-v0.2.md` (本文件, 全程一稿)
- **deadline**: W5 EOW (本 wave 内提交)
- **状态**: **draft 已交**, 待老郭 first review + GM ack
- **后续依赖**:
  - W6 落 `tests/ci_grep/persona_boundary_check.py` (老徐 + 小宋)
  - W6 立 `docs/META/escalate-decision-log.md` (老徐 + 小米)
  - W5 起老胡周报加 K1-K4 (老胡 PM)

**汇报口径**: **R-39 escalate flow v0.2 + 4 步流程 + 时间窗 + 死锁防护 + tooling 2 + KPI 4**

---

**Last updated:** 2026-05-28 by 老徐 (#33)
