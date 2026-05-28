# 风险登记 v2.3

- Owner: 老胡 | 验收: 老雷 | Last review: 2026-06-01 (Sprint-2 W5 末 Wave 25, 6/01 主管周同步 v1)
- 关联:
  - `docs/RESEARCH/laohu-risk-registry-v2.2.md` (v2.2 baseline, W4 中期 Wave 20)
  - `docs/MEETINGS/2026-06-01-manager-sync-w5-v1.md` (6/01 主管周同步 v1 决议 D-04/05/06)
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #9 (R-41 来源)
  - `docs/META/weekly-report-template-v2.md` (R-41 配套, 周报模板升 v2)
  - `docs/ADR/2026-05-28-department-manager-mandate.md` ADR-005 (R-41/42/43 体系)

---

## 0. TL;DR (v2.2 → v2.3)

- **关闭 0 / 降级 0 (R-21 下周 e2e p99 验证后再降) / 升级 0 / 新增 1 (R-41 跨 wave 引用过时信息, GM 错 #9 触发)**
- **Top 5 不变**: R-02 / R-06 / R-07 / R-09 / R-31; R-38 候补维持
- **R-41 严重度评估:** 12 (4×3) 与 R-31 / R-38 同级, 但归类 **GM 流程红线** (与 R-34 同源 GM 错 #N 累计), 不直接进 Top 5 (Top 5 是业务 + 工程红线)
- **周报模板正式升 v2** (`docs/META/weekly-report-template-v2.md`), 加 §4 SSOT 版本演进段 (R-41 永久 enforcement 配套), 老胡 owner, W5 五周报首次套用

---

## 1. v2.2 → v2.3 风险 update

| 编号 | v2.2 → v2.3 | 状态 | 变更原因 |
|---|---|---|---|
| **R-41 (新)** | — → **12 = 4×3** | **新增** | GM 错 #9 触发: 跨 wave 引用过时 SSOT (Pinnacle 凭空决议), 漏读小段 v3 推翻 v2.1 → 永久 enforcement 4 条 (见 §2) |
| R-21 paper engine 联调 | 9 → 9 | 不变 (W5 e2e p99 跑通后下周再评) | W5 Wave 24 312/312 测试过, e2e p99 3.9us 远低 50ms 目标; W6 一硬约束转换后再评是否降 6 |
| R-31 R-20 PIT 违例 | 12 → 12 | 不变 | W5 Wave 24 全模块 4ts UPSTREAM_PAYLOAD 优先 enforce, integration test 4 case audit_chain_verify ✓; W6 一硬约束后再评 |
| R-34 GM 错流程 | 9 → 9 | 不变 (R-41 是 R-34 的子类, 同源不重复升) | 错 #9 通过新立 R-41 单独跟踪, R-34 整体 9/3 维持; W6 一 GM 5 题自检 enforce 后再评 |
| R-38 W5 e2e WAL fsync | 12 → 12 | 监控 | W5 Wave 24 e2e p99 = 3.9us 远低预期, wal_fsync_p99 实测 < 50ms (未触发 §R-38 触发条件); W6 一连续跑 7 天后再评 |
| R-39 sub-agent 自我纠错误伤 | 8 → 8 | 监控 | W5 Wave 24 老沈 BUG-W5-001 派单数字纠错 = sub-agent 实证纠正 (健康); 老徐 escalate-flow v0.2 已落 (commit 3ab5dfb 363 行); W6 一 sub-agent 拒接次数监控启动 |
| R-40 HR registry drift | 6 → 6 | 缓解中 | W5 Wave 24 全主管就职宣言 6/6 ✓ + registry persona 数对齐 ✓; W6 一 6/06 5 新 JD 起草后 CI grep 再 run |
| 其他 (R-22 ~ R-37 不变) | 不变 | — | v2.2 baseline 持续 |

---

## 2. 新增风险 R-41

### R-41 跨 wave 引用过时信息, 漏读 owner v(N+1) 推翻 vN 结论 (12 = 4×3, 永久 enforcement)

**来源:** GM 错 #9 (`docs/INCIDENTS/gm-self-mistakes-log.md`), 2026-05-28 W5 末 Wave 24 push 后用户问"下阶段计划", GM 列了"Pinnacle CSV 路径 A 官方 vs C 老彭手工"作为待拍决议. 用户问"我们不是有 Goalserve 吗"立刻发现 GM 用的是过时信息. 小段 W3 末 v3 已**反转结论** ("Goalserve 单源就够 fair value 锚源"), GM 引用的是 v2.1 早期信息, **跨 wave 没读最新版**.

**为什么是风险:**

1. **跨 wave / 跨 sprint 重大决议高频出现:** ADR 评审 / Sprint 规划 / "下阶段计划" / 主管周同步, 都需要引用 SSOT, 但 SSOT 是动态演进的 (小段 v1 → v2 → v2.1 → v3)
2. **数据源 owner 演进经常推翻早期结论:** Pinnacle 路径整体被小段 v3 推翻是首个高调案例, 未来老李 polymarket-client / 老彭 betting-industry / 小邓 ML data 等都可能出 v(N+1) 推翻 vN
3. **GM / 主管 / CPO 凭记忆引用 SSOT 是常态, 不查最新版是常态 — 这是制度缺陷不是个人失误**
4. **无 enforcement 机制时, 类似错每月可能发生 1-2 次, 累计到 Sprint 末 retro 才被发现 — 损失 wave 级派单成本 + decision 信任度**

**永久 enforcement 4 条** (GM 错 #9 教训, 全员遵守):

1. **GM 做"下阶段计划"前查 `docs/RESEARCH/` 各 owner 最新 vN 版本号** (而非按记忆引用), 跨 wave / 跨 sprint 重大决议必查 — GM 5 题自检 4 题升 5 题, **是否再升 6 题** (E-02 议题待 6/06 EOW 评估)
2. **数据源 owner (小段 / 老李 / 老彭 / 小邓 等) 每出 v(N+1) 必须在 Sprint progress 周报里点名"推翻了 vN 的 X 结论"** — 让 GM 不漏读; **owner 不点名 = owner 责任** (CI grep 反模式由小宋 W6 末加)
3. **老胡 PM 周报加 "本周 SSOT 版本演进" 段** — **本节正式落地 `docs/META/weekly-report-template-v2.md` §4**, W5 五周报首次套用 (Sprint-2 W5 progress)
4. **老胡 W5 周报修正 Pinnacle 决议** — 小段 v3 已 closeout, 改为小梁 de-vig 算法选型 (ADR-008 撤回 Pinnacle 立 de-vig, 6/01 主管周同步 D-04 决议)

**触发:**
- 单 wave 内 GM / 主管 / CPO 引用 ≥ 1 个 vN 实际已被 v(N+1) 推翻 → R-41 实际触发, P 升 (5 = 5×1)
- 单 Sprint 内累计 ≥ 3 次类似事件 → P 升至 H (15 = 5×3), 全体会议必上桌评估制度是否再升级

**缓解:**
1. 周报模板 v2 §4 强制段 (W5 五首次套用)
2. owner v(N+1) 出版必带 "推翻 vN" 注释 (周报 + ADR review + 主管周同步引用三处必带)
3. CI grep 反模式 (小宋 W6 末加): `docs/RESEARCH/` 引用如果版本号低于该 owner 最新版本号 → review-block (W7 复评是否启用)
4. GM 5 题自检评估升 6 题 (E-02 议题, 6/06 EOW)

**Owner:** 老胡 (PM, R-41 跟踪 + 周报模板 v2 owner) + 老雷 (GM, 5 题自检) + 小米 (E-040, owner 点名 CI grep 配合) + 全 owner (出 v(N+1) 必点名)

---

## 3. Top 5 (v2.3, 不变)

| 排名 | 编号 | 描述 | I × P | 变动 |
|---|---|---|---|---|
| Top 1 | R-02 | RiskGateway 绕过 | 20 (5×4) | 不变, W5 Wave 24 312/312 测试过 + integration r11_paper_pollution 5 case ✓ |
| Top 2 | R-06 | seconds_delay 吃 PnL | 16 (4×4) | 不变 |
| Top 3 | R-07 | HC-01/02 招聘失败 | 16 (4×4) | 6/30 deadline 临近, W5-08 Escalated 准备; **6/06 EOW HC-04/05/06/07/08 5 新 JD 起草并发** |
| Top 4 | R-09 | Sharpe > 1 不达 | 15 (5×3) | 不变 |
| Top 5 | R-31 | R-20 PIT 违例 | 12 (3×4) | 不变 (W5 e2e p99 3.9us ✓, W6 一硬约束转换后再评) |
| 候补 | R-38 | e2e WAL fsync 集中爆发 | 12 (4×3) | 监控 (W5 实测 < 50ms, W6 一 7 天后再评) |
| 候补 | **R-41** | **跨 wave 引用过时信息** | **12 (4×3)** | **新增 (GM 错 #9 触发, 永久 enforcement 4 条 + 周报模板 v2)** |

**R-41 与 R-31 / R-38 同级但归类不同:**
- R-31 / R-38 = **工程红线** (R-20 4ts / e2e WAL fsync), 进 Top 5 候补
- R-41 = **GM 流程红线** (与 R-34 同源 GM 错 #N 累计), 独立跟踪, 不进 Top 5 但永久 enforcement

R-39 (8) / R-40 (6) / R-34 (9) 评分均低于 Top 5 阈值.

---

## 4. ADR-005 配套风险 (R-41/42/43, 主管制度 3 风险跟进)

> 6/01 主管周同步 v1 W5 试点末 → W6 硬约束转换前最后一次软约束运行, 3 风险首次实测.

### 4.1 R-41 主管装睡 (ADR-005 R-41)

> **注: ADR-005 R-41 = "主管装睡", 与本登记 R-41 = "跨 wave 引用过时" 是不同 R-41, 命名重号 — 6/06 EOW 与小米协调重编**

**ADR-005 R-41 (主管装睡)** 触发条件: 接 GM 派单后实际不拆给 IC, 自己默默做.

**W5 实测:**
- W5 Wave 24 6 IC + 4 follow-up 任务**主管派单覆盖率 100%** (目标 ≥ 70%, 超目标 30 pp)
- 0 主管装睡, 6 主管全部就职宣言 W4 EOW 提前交付
- W6 一硬约束转换后第 1 周再实测

### 4.2 R-42 IC 等主管 (ADR-005 R-42)

**触发条件:** 主管不在线 / 拆任务慢, IC blocker > 24h.

**W5 实测:**
- 0 IC blocker > 24h
- 主管 SLA 24h ack 6/01 EOD 截止收 (会议纪要 §4 占位等回填)
- W6 一硬约束后启动

### 4.3 R-43 IC 越主管直接找 GM (ADR-005 R-43)

**触发条件:** IC 跳过主管直接找 GM 派单或求 ack.

**W5 实测:**
- 0 IC 越主管 (W5 试点软约束未硬 block)
- W6 一 (2026-07-13) 起 GM 5 题自检第 5 题 enforce, IC 越主管找 GM **拒接**
- E-05 议题 (会议纪要 §9): W6 硬约束后若 IC 仍越主管 → 是否触发 IC 流失 (R-43 升级条件), W6 首日监控

---

## 5. 周报模板 v2 正式落地 (D-06 决议)

### 5.1 v2 vs v1 增量

| 段 | v1 | v2 (新) | 触发 |
|---|---|---|---|
| §4 本周 SSOT 版本演进 | 无 | **新增** (vN → v(N+1) 推翻清单 + 跨 wave 决议追溯 + watch list + Pinnacle 撤回追踪) | R-41 + GM 错 #9 永久 enforcement 第 3 条 |
| §5 主管派单 KPI | 无 (v1 仅 GM 周报双轨 OKR) | **新增** (主管派单覆盖率 + 主管 SLA + GM SLA + 协商会次数 + escalate 次数) | ADR-005 §8 W5 试点 W6 硬约束追踪 |
| §8 上周 GM 错 | 有 | 维持 | CLAUDE.md §3 公开失败铁律 |
| §9 主管月度轮值 GM 助理 status | 无 | **新增** (可选段, 当月有轮值时填) | ADR-005 §4.3 |

### 5.2 v2 首次套用 deadline

- **W5 五周报** (Sprint-2 W5 progress, 2026-08-01 暂定): 首次套用 v2 模板
- **§4 SSOT 版本演进首条:** 小段 v2.1 → v3 推翻 "Pinnacle 必要", 6/01 主管周同步 ADR-008 撤回 Pinnacle 路径, 立 Goalserve fair value de-vig 算法选型 (小梁 W6 起建模)
- **§5 主管 KPI 首批数字:** 主管派单覆盖率 100% / 主管 SLA TBD / GM SLA TBD / 协商会 0 (6/02 启动) / escalate 1 (老沈 BUG-W5-001 ADR-005 §3.2 例外允许)

### 5.3 W7 复评 (Sprint-3 W3 末)

- 复评 v2 模板有效性 (4 周后, GM / CPO / 5 主管反馈)
- 是否升 v3 (例: §4 SSOT 版本演进段是否还需细化, §5 KPI 是否加项)
- R-41 触发频次实测 (W6/W7 累计 ≥ 3 次类似事件 → 制度再升级)

---

## 6. 老胡附言

R-41 是 GM 错 #9 直接落地, 与 R-34 共同标记 "GM 流程红线" 体系. 周报模板 v2 §4 SSOT 版本演进段是制度级 enforcement, 不是文档润色 — GM / 主管 / CPO **每周必读 §4 1 次**, 否则下次"凭空决议"还会发生.

**不耻下问:**
- R-41 第 3 条 enforcement (周报 §4) @ 全 owner: v(N+1) 出版**必须在 §4 点名推翻 vN 的 X 结论**, owner 不点名 = owner 责任
- R-41 触发 watch @ 小宋 (E-028, integration test owner): W6 末 CI grep 反模式 (`docs/RESEARCH/` 引用版本号低于最新版本号 → review-block) 是否启用
- R-41 与 ADR-005 R-41 命名重号 @ 小米 (E-040, doc-curator): 6/06 EOW 协调重编, 建议本登记 R-41 不动 (跨 wave 引用过时), ADR-005 R-41 改 ADR-005-R-X (X 待定)

**边界声明 (主管 KPI 自评 60%, 60% 来自不替阈值 / 架构 / 优先级):**
- 我整合, 不替 GM 拍 5 题升 6 题
- 我整合, 不替老郭拍 R-41 与 ADR-005 R-41 重号怎么改
- 我整合, 不替老韩 / 老周 / 小梁 / 小余 / 老郭定本周 SSOT 版本演进 watch list 优先级

— 老胡, 2026-06-01 (Sprint-2 W5 末 Wave 25, 6/01 主管周同步 v1)
