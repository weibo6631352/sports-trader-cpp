# HR Pulse Check — Sprint-2 W3 部门扫描

- **Owner:** 小林 (hr-talent-manager)
- **Date:** 2026-05-28
- **指令来源:** GM 老雷 — "紧盯各部门情况, 推动高效运转"
- **频率:** 每 wave 末做一次 (W3 首次, 之后 W6/W9 跟)
- **关联:**
  - `docs/HIRING/backlog.md` (Q3 HC v2)
  - `docs/HIRING/sprint2-w2-progress.md` → 本次更新 `sprint2-w3-progress.md`
  - `docs/INCIDENTS/gm-self-mistakes-log.md` (3 错 baseline)
- **价值观对照:** 实盘优先 / 纪律 / 数字说话 / 不耻下问

---

## Part 1: 5 战斗单元 + 顾问团 Pulse Check

| 部门 | Owner | 本季交付 (Sprint-1 + W1-W3) | 工作量 (0-10) | 风险信号 | HR 建议 |
|---|---|---|---|---|---|
| **A 系统工程** | 老周 | v0.1-v0.5 spec + Lifecycle ADR + 14 子 owner 协调 + PaperSigner 卡点 | **9/10 (红)** | 单点 + 评委位 4 个 (HC-01/02 初+深) + W4 PR review 入帐 | 老冀 7/15 入职后**当周**接 PaperSigner mock; HC-01 初面降级让 IC pool 老陈分担 (W2 已埋); Q3 评估 onchain-ops 第 2 人 (HC-01 兜底位) |
| **B 风控合规** | 老韩 | RM v0.1-v0.3.1 + STRATEGY_DECAYED 三签 + 老沈/老唐协调 | **7/10 (黄)** | 三人协调瓶颈 (老沈跨 vendor + 老唐 audit), 老韩自己写 + review + 拍板三角色叠加 | Q3 招 risk-quant-engineer (HC-04) 分担规则引擎实现, 老韩留拍板和 review 角色 |
| **C 量化研究** | 小梁 | 12 信号 spec + Pinnacle 路径 ADR + 4 retro 真发言 + 小程/小蒋/小袁/小肖 4 人协调 | **8/10 (黄+)** | M4.5 gate 9/12 倒推 W6 P0-01 回测必出, 小袁 Mode A++ R-14 自加项已显增量, 团队天花板可见 | **HC-03 小吕提前到 8/1 入职** (原计划 Q3 中); Q3 再评 data-scientist (HC-08) 提前可能性 |
| **D 数据基建** | 小余 | Sprint-1 无独立 v1 spec (自评), W3 ETL 8 必做项主推, 小段 v3 顶住 | **7/10 (黄)** | 小余自己 spec 产出节奏慢于其他 owner, 小段被压实 | HC-05 data-engineer 按 Q3 计划招不提前, 但 W4 起加 1 次 小余 1:1 看是否需要 staff-level 数据架构师 (现 HC-05 是 mid-level) |
| **E 产品保障** | 老胡 | PM 甘特图 + 小颖/小杜/小宋/小苏/小米/小尤/小宫 8 人均衡分发 | **6/10 (绿)** | 均衡, 无单点, 但 W4 起 GM 错频率高需老胡周报硬约束 | OK, 不加人. W4 起老胡周报多加"GM 本周错"一节 (机制已在 INCIDENTS log §后续) |
| **F 顾问团** | 老郭/老高/老叶 | 老郭 ADR-003 闭环, 老高 PR review (W4 起会忙), 老叶 ML/财务 occasional | **5/10 (绿)** | 老高 W4 PR review 量会涨, 老郭多 ADR 排队 | OK, 顾问按需调度. W6 评估老高是否需要从顾问转 full-time |

### 单点风险红榜 (优先级降序)

1. **老周 9/10 红** — A 系统工程单点, HC-01 兜底必须 7/15 到岗, 否则 W6 老周爆
2. **小梁 8/10 黄+** — C 量化研究 M4.5 gate 倒推, 小吕提前 8/1 是 P0 建议
3. **老韩 7/10 黄** — 三角色叠加, Q3 HC-04 必招
4. **小余 7/10 黄** — 自评慢 + 小段压实, 需 1:1 诊断
5. **老胡 6/10 绿** — 良性, GM 错周报机制加强即可

---

## Part 2: Q3 HC 提前评估

| HC | 原计划 | 提前建议 | 理由 | 评估 |
|---|---|---|---|---|
| HC-03 小吕 quant-engineer | Q3 (9 月) | **8/1 提前 1 个月** | M4.5 gate 9/12 倒推 W6 回测必出, 小梁 8/10 工作量, 提前 1 个月对齐 P0-01 deadline | **批准, JD W4 启动起草** |
| HC-04 risk-quant | Q3 | 不变 (9 月) | 老韩 7/10 还能扛, Sprint-3 中段招来得及 | 维持 |
| HC-05 data-engineer | Q3 | 不变 (9 月) | 小余 1:1 后再评 staff vs mid 级别, JD 类型可能变 | 维持 + 待 1:1 输出 |
| HC-06 devops-infra | Q3 | **延后到 Q4 / AWS 解锁后** | AWS 跨洋部署未解锁前 devops 没有真活, 招来空转 | 延后 |
| HC-07 小方 market-data QA | Q3 P2 | 不变 (9 月) | 数据边缘 case 量未到爆炸级 | 维持 |

**Q3 招聘节奏调整:** 原 5 人 → 实际 4 人 (HC-06 延后), 其中 HC-03 提前到 8 月. Q3 net = HC-03/04/05/07.

---

## Part 3: HC-01 老冀 + HC-02 小秦 招聘 6/30 进度

### JD 状态

| HC | JD 文件 | 渠道发布 | 评委确认 | 候选人池 |
|---|---|---|---|---|
| HC-01 老冀 | `jd-onchain-ops-laoji-v1.md` 已发布草案 | **6/1 deadline 待老周 5/30 ack** | 待老周 / 老韩 **5/29 deadline** | 0 (未发布) |
| HC-02 小秦 | `jd-strategy-execution-xiaoqin-v1.md` 已发布草案 | **6/1 deadline 待老周 5/30 ack** | 待老周 / 小梁 **5/29 deadline** | 0 (未发布) |

### 5/29 deadline 评委齐没齐 — **未确认, 5/29 当日小林追**

- 老周: HC-01 初+深, HC-02 初+深 → 4 个评委位, **追确认 by 5/29 EOD**
- 老韩: HC-01 深度面 → **追确认 by 5/29 EOD**
- 小梁: HC-02 深度面 → **追确认 by 5/29 EOD**
- 小林+老雷: 文化面 → 已默认 OK

### 紧急升级路径 (若 5/29 评委没齐)

1. **L1 (5/29 18:00):** 小林直接 ping owner, 给 24h 窗口
2. **L2 (5/30 18:00):** 小林 → 老雷, GM 直接拍 owner
3. **L3 (6/1 00:00):** 老周初面降级 → IC pool 老陈做技术筛 (W2 已埋路径), 老周只做深度
4. **L4 (6/4 候选人池 < 5):** 内推奖金 2 万 RMB → 临时上调 3 万, 全员发动

### 6/30 入职 vs 7/15 兜底

- **6/30 base case:** 候选人 immediate available, 28 天 SOP 压缩到 21 天可达
- **7/15 兜底:** 接受候选人 notice period 30 天, signed offer + 7/15 实到岗 — **这是 GM 已批的兜底**
- **风险:** 若 W3 (6/4) 候选人池 < 5/岗, 6/30 base case 失守, 直接走 7/15 兜底

---

## Part 4: 文化健康度

### 公开失败文化落地度 — **正在落地**

- GM 错 3 个: #1 草率 ack / #2 单 PM 编造 retro / #3 老项目参考红线
- Agent 公开承认 bug: 老李 HMAC 4 bug, 小袁 Mode A++ 自加 R-14
- **机制有效**: INCIDENTS log + retro 必读 + 新人 onboarding 必读, 闭环存在

### GM 自检频率建议 (HR 监督角度)

**现状:** Sprint-末 1 次 retro 自检, 但 W3 一周内**3 次错**全靠用户在场纠正才挡住.

**HR 建议 (上呈老雷):**

| 频率层 | 现状 | 建议 |
|---|---|---|
| 每派单 | 无自检 | **派单前 30s 自问 3 题**: ① 我有没有一面之词背书? ② 我有没有让 1 个 agent 替全员说话? ③ 我有没有让 agent 看老项目 / 撤销方案? |
| 每 wave (M-F) | 无 | **每 wave 末 5 分钟自评**, 写到 `INCIDENTS/gm-self-mistakes-log.md` (即使无错也写 "本 wave 无新错") |
| 每 Sprint | 1 次 retro | 维持 |

**理由:** 用户在场是当前唯一刹车, 不可持续. Wave 级自评把刹车前移, 减少用户消耗.

### Agent 协作健康信号

- 4 retro 真发言机制 (Phase 1 → 16 份真实 agent 文件) — **健康**, 错 #2 学到了
- ADR 三签 + 跨域听取 — **健康**, 错 #1 学到了
- CI grep 反模式 + 派单 prompt 红线 — **健康**, 错 #3 已硬约束

---

## Part 5: HR 自己 W4 派单 (自派)

| # | 任务 | Deadline | 产出 |
|---|---|---|---|
| HR-W4-01 | 老冀 + 小秦 招聘进度 weekly report | 每周五 EOD | `sprint2-w4-progress.md` |
| HR-W4-02 | 5/29 评委确认追单 (老周/老韩/小梁) | 5/29 18:00 | Slack ping + 状态更新 |
| HR-W4-03 | HC-03 小吕 quant-engineer JD v1 起草 | 6/5 | `jd-quant-engineer-xiaolyu-v1.md` |
| HR-W4-04 | 小余 1:1 (诊断 staff vs mid 级别 HC-05) | 6/3 | 1:1 记录 + HC-05 JD 方向 |
| HR-W4-05 | 上呈 GM 自检频率建议 (Part 4) → 老雷 ack | 5/30 | 老雷 ack 或退回 |
| HR-W4-06 | 团队 Q2 KPI 月评 (5 月) + 老胡甘特图联动 | 6/2 | KPI 月评表 |
| HR-W4-07 | 老周 1:1 (9/10 工作量预警, 7/15 老冀到岗前过渡方案) | 6/1 | 1:1 记录 + 临时分担方案 |

---

## 完成汇报

1. **HR pulse 5 部门评分:** A 9/10 红 (老周单点) / B 7/10 黄 (老韩三角色) / C 8/10 黄+ (小梁 M4.5 倒推) / D 7/10 黄 (小余慢) / E 6/10 绿 (老胡均衡) / F 5/10 绿 (顾问按需)
2. **Q3 提前招聘建议:** HC-03 小吕 quant-engineer **提前到 8/1** (M4.5 gate 倒推); HC-06 devops-infra **延后到 Q4** (AWS 解锁前空转); 其余维持
3. **HC-01/02 候选人池状态:** **0 候选人, JD 待发布, 5/29 评委确认是 critical path**, 5/30 老周 ack JD → 6/1 发布 → 6/4 简历筛选 ≥ 8 份是 base case; 否则 7/15 兜底
4. **GM 公开错频率建议:** 现 1 次/Sprint → 建议**1 次/wave + 派单前 30s 自检 3 题**, 上呈老雷 by 5/30 ack
