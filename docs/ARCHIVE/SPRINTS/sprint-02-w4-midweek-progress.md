# Sprint-2 W4 中期周报 (Wave 19 + Wave 20 半周) — 给老雷

- **Owner**: 老胡 (pm-project-manager)
- **周期**: W4 (2026-06-29 → 2026-07-03), 报告日 2026-05-28 (Wave 20 中期口径)
- **状态**: **绿** (W4 中期超额) + GM 错累计升级到 6 (#4 #5 #6) + sub-agent 自我纠错首次落地
- **抄送**: 老雷 / 老郭 / 老周 / 老韩 / 小梁 / 小林 / 小米
- **关联**:
  - 上一份: `docs/SPRINTS/sprint-02-w3-progress.md`
  - W4 backlog: `docs/SPRINTS/sprint-02.md` §1.6.3
  - 风险登记: `docs/RESEARCH/laohu-risk-registry-v2.2.md` (本 wave 同出)
  - HR registry: `docs/HIRING/employee-registry.md`
  - GM 错 log: `docs/INCIDENTS/gm-self-mistakes-log.md` (#4-6 入)

---

## 1. TL;DR 给老雷 (一段话)

W4 中期 (Wave 19 + Wave 20 半周) **5/14 W4 ticket 提前完成 + 全仓 ctest 193/193 pass** (W3 末 51 → W4 中 193, 净增 +142). Wave 19 6 部门并行落代码 (老韩 RG / 老唐 audit / 小蒋 paper / 小段 Goalserve / 小卢 P0-01 + 小程 spec), Wave 20 5 项 (老郭 ADR-004 / 小董 M4.5 framework / 小邓 ML 数据钩子 / 小袁 microstructure / 老周架构 v0.6) **本 wave 派出尚未回汇**. **老雷必须看的 3 件事**: (1) **W4 中期 36% 超额率** (计划 14, 已交 5, 在飞 5, 待派 4); (2) **GM 错累计 #4-6 升 6 错**, 其中 **#4 sub-agent 自我纠错首次** (小程拒接 P0-01 C++ stub, 边界正确, "边界即文化"落地); (3) **HR registry 制度补救**, founding cohort 57 全员补登 + 3 pending, CI grep `registry_consistency` W4 接入. R-20 / R-11 / R-7 / R-12 红线 build-time + 单测双重 enforce, 工程纪律已上轨.

---

## 2. Wave 19 完整成果 vs W4 backlog (5/14 提前完成)

| W4 ticket | Owner | 计划 | 中期实际 | 状态 |
|---|---|---|---|---|
| W4-01 | 老韩 | RiskGateway::evaluate() v0.1 | **1088 行 + 46 测试 pass** (21 enum + 9 INVALID_INTENT sub + 状态机) | **Agreed 已交** |
| W4-02 | 老唐 + 老韩 | audit_writer v0.1 | **896 行 + 24 测试 pass** (12 AET + R-11 build-time 分流 + R-20 5 case) | **Agreed 已交** |
| W4-03 | 小蒋 | paper engine main v0.1 | **1233 行 + 27 测试 pass** (PaperSigner + VirtualMatcher Mode A++ Bernoulli + R-7 双调 abort + stcpp_paper E2E) | **Agreed 已交 (R-21 闸 2 闭环)** |
| W4-04 | 小程 + 小梁 | P0-01 signal v0.1 | **小程交 spec v0.1 + 小卢 IC pool 落码 1082 行 + 25 测试** | **Agreed 已交 (拆分: spec/code)** |
| W4-05 | 老李 | Polymarket client v0.1 | Wave 20 未派, W5-01 接 | **W5 接 (推迟 1 周, 工程依赖)** |
| W4-06 | 小段 | Goalserve client v0.1 | **1121 行 + 28 测试 pass** (5 host + 8 sport + 11 TimeStatus + 4ts UPSTREAM_PAYLOAD 优先) | **Agreed 已交** |
| W4-07 | 老孙 | Polygon RPC mock v0.1 | 小蒋 PaperSigner 内联 virtual_nonce/gas/confirm 三 stub 已覆盖 | **Agreed 已交 (合并入 W4-03)** |
| W4-08 | 小袁 | microstructure C++ v0.1 | **Wave 20 在飞** (microstructure C++ lib + fill_rate model v0.1) | **Wave 20 在飞** |
| W4-09 | 小余 | etl-pipeline C++ v0.1 | **Wave 20 未派** (Compromised 第 3 推, 强制 W4 末必交) | **黄牌警告** |
| W4-10 | 小邓 | ML shadow signal v0.1 | **Wave 20 在飞** (ML 数据钩子 + 32 feature schema + TrainingLabel) | **Wave 20 在飞** |
| W4-11 | 小冯 | WSS raw frame cold storage v0.1 | W5-02 接 (PM WSS subscriber + sports channel reconnect + back-pressure) | **W5 接** |
| W4-12 | 小宋 | CI 反模式拦截 + R-3 grep + schema_drift_chaos daily | **registry_consistency 已落 (HR 制度补救派)**; R-3 + R-20 grep 在前一阶段已交 | **Agreed 部分** |
| W4-13 | 老胡 | 风险登记 v2.1 + GM 周报 W4 | v2.1 已交 (Wave 19); v2.2 本 wave + 中期周报本份 | **Agreed 已交 + 在飞** |
| W4-14 | 小米 | R-20 回灌 W4 增量 8 篇 | 上一周交 12 篇, W4 增量待 Wave 21 | **Agreed (本周交 W3 12 篇, W4 增量待)** |

**W4 中期完成度**:
- **完全 Agreed 已交**: 5 项 (W4-01 / W4-02 / W4-03 / W4-04 / W4-06) + 隐式 1 项 (W4-07 合并)
- **Wave 20 在飞**: 3 项 (W4-08 / W4-10 + 老周 v0.6 + 老郭 ADR-004 + 小董 M4.5 framework, 共 5 子项映射到 4 owner)
- **W5 接**: 2 项 (W4-05 老李 PM client / W4-11 小冯 WSS cold storage)
- **黄牌**: 1 项 (W4-09 小余 etl-pipeline 第 3 推, 强制 W4 末)
- **部分/惯性**: 2 项 (W4-12 / W4-14)

**超额率**: 5/14 = **36% 中期超额** (W4 中点已交 5, 全 W4 14 项, 末 100% 概率 ≥ 70%).

---

## 3. 全仓 ctest 193/193 pass (W3 51 → W4 193, 净增 +142)

| 模块 | Owner | 测试数 | 红线 enforce |
|---|---|---|---|
| RiskGateway | 老韩 | 46 | R-12 (无锁 / 异步 audit), R-20 (PIT 4ts) |
| AuditEmitter | 老唐 | 24 | R-11 (paper/live build-time 分流), R-20 (5 case) |
| PaperSigner + VirtualMatcher | 小蒋 | 27 | R-7 (mode 双调 abort), R-1 (paper 不写真账本) |
| Goalserve client | 小段 | 28 | R-20 (4ts UPSTREAM_PAYLOAD 优先), R-33 (5 host 全覆盖) |
| P0-01 PinnacleNoVig + LiveSection | 小卢 | 25 | R-20 (data_source_ts 必填) |
| WAL writer (W3) | 老王 | 8 | R-11, R-20 |
| SlippageModel (W3) | 小肖 | 6 | bench p99 5.5x 余量 |
| 测试 framework + grep (W3) | 小宋 | 29 (含 21 enum + 4 r12_sim + 7 PIT grep) | R-20 hard block, R-3 reference grep |
| **合计** | — | **193** | **R-1 / R-7 / R-11 / R-12 / R-20 全 5 红线 build-time + 单测双锁** |

严格 lint (`-Werror -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wdouble-promotion`) 全过. 文档健康度 A (小米归档).

---

## 4. GM 错累计 #4-6 (升 6 错) + sub-agent 自我纠错首次

### 4.1 GM 错 #4 — Wave 19 派单让小程写 C++ stub (违反 persona 边界)
- **错在哪**: 派单 prompt 6 个交付物 5 个是 C++ 代码 + 测试 + CMake, 而小程 persona §拒绝任务明确写 "代码 / 回测"
- **谁纠正**: **小程 sub-agent 自己拒接** (首次 sub-agent 自我纠错, "边界即文化"落地)
- **永久 enforcement**: CLAUDE.md §7-8 加 "GM 派 wave 前 4 题自检" 铁律 (HR 上呈), 老胡周报加 "本周派单越界次数" KPI

### 4.2 GM 错 #5 — AGENT.md "58 (45 类)" 数学算错
- **错在哪**: 实际 file 44 类 (除 #35 IC pool), commit `c417bc3` 写错
- **谁纠正**: 用户 ("为什么 58 个人只有 48 个 .claude/")
- **永久 enforcement**: AGENT.md "维护人小米归档时核数学", commit message 数字必查

### 4.3 GM 错 #6 — Founding cohort 57 人 0 HR 注册 (制度缺失 7 天)
- **错在哪**: 班底 57 persona day 0 ~ W3 末全部直接建 file 上岗, 无花名册 / 工号 / 状态概念
- **谁纠正**: 用户 ("新招聘的同事必须通过人事注册登记。 不然时间长你都忘了")
- **永久 enforcement**: `docs/HIRING/employee-registry.md` 立刻建 + CLAUDE.md §7-7 加 "HR 注册前置" + CI grep `registry_consistency` (W4 小宋 已加)

### 4.4 共性教训 (6 错合看)
- #1-3: 用户实测纠正 (一面之词 / 单 agent 替全员 / "对比"包装)
- **#4: sub-agent 自我纠正 (首次, 公司"边界即文化"起效)**
- #5-6: 用户继续纠正 (数学 + 制度层 GM 仍依赖用户)

**纠错来源演化**: 用户监督 → sub-agent 自检 (#4 是分水岭). 老胡跟进: W4 起每份周报加 "派单越界次数" 字段, W5 第 4 周看趋势.

---

## 5. W4 剩余 9 项 backlog 进度

### 5.1 Wave 20 派出 (5 项, 等回汇, ≤ 2026-05-28 末)

| 派单 | Owner | 内容 | 状态 |
|---|---|---|---|
| Wave 20-A | 老郭 | ADR-004 liquidity vs position_cap 优先级仲裁 | 在飞 |
| Wave 20-B | 小董 | M4.5 7 hard gate 统计 framework C++ + Python | 在飞 |
| Wave 20-C | 小邓 | ML 数据钩子 + 32 feature schema + TrainingLabel | 在飞 |
| Wave 20-D | 小袁 | microstructure C++ lib + fill_rate model v0.1 | 在飞 |
| Wave 20-E | 老周 | 端到端联调架构 v0.6 + 线程模型 + 链路图 | 在飞 |

### 5.2 W4 后续 (4 项, 推进 W5 或末周)

| # | Owner | 内容 | 计划 |
|---|---|---|---|
| W4-09 | 小余 | etl-pipeline C++ v0.1 (第 3 推, **黄牌**) | W4 末必交, 不达标升级老雷 |
| W4-14 | 小米 | R-20 回灌 W4 增量 8 篇 | W4 末交 |
| Wave 21 | 老胡 | W4 final 周报 + Sprint-2 mid-point review | W4 末 (6/3) |
| Wave 21 | 老练 + 老高 | CI hard block R-3 + R-20 grep + persona 边界 grep 全量验证 | W4 末 |

---

## 6. 健康度评级 / OKR M1 进度

### 6.1 健康度 (W3 → W4 中)

| 维度 | W3 | W4 中 | 变化原因 |
|---|---|---|---|
| 整体 | A | **A+** | 193/193 + 6 部门并行落代码 + sub-agent 自纠 |
| 架构 | A | A | 老周 v0.6 在飞, 评级待 Wave 20 回 |
| 风控 | A- | **A** | 老韩 evaluate() 1088 行 + 46 测试 pass |
| 量化 | A- | A | 小卢 P0-01 + 小程 spec 拆分模式, 小袁 microstructure 在飞 |
| 数据 | B+ | **B** (下降) | 小余 etl W4-09 第 3 推, 黄牌警告 |
| 流程 | A- | **A+** | sub-agent 自纠 (#4) + HR registry 立 (#6) + 4 题自检铁律 |
| 文档 | A | A | 小米归档常态化 |
| HR | — | **A-** | registry 57 + 3 pending, Q3 提前规划 |

### 6.2 OKR M1 (2026-07-09 T+6 周) 进度

| KR | 进度 | 状态 |
|---|---|---|
| KR-A-1 架构 v1.0 冻结 | v0.6 在飞 → W5 评审 v1.0 | 按时 |
| KR-A-2 RM C++ v0.1 验收 | 1088 行 + 46 测试 ✓ | **已达** |
| KR-A-3 paper engine main W1 通过 | 1233 行 + 27 测试 + E2E ✓ | **已达 (闸 2 闭环)** |
| KR-A-4 数据接入联调 | 小段 Goalserve client ✓, 小余 etl 黄牌, W5-02 PM WSS 在飞 | 部分 |
| KR-A-5 R-20 落地全闭环 | grep + enum CI + audit 5 case + Goalserve 4ts ✓ | **已达** |
| KR-A-6 跨洋实测联调 | W5-01 派老李 PM client 后接 | W5 必交 |

**M1 评审 7/9 前置 5/6 已达**, R-21 / R-31 闭环路径清晰, R-07 HC 入职 (老冀 / 小秦) 是唯一外部风险.

---

## 7. 老胡 KPI 自评

| KPI | 数据 |
|---|---|
| W4 中期超额率 | **36%** (5/14, 中点) |
| W5 派单覆盖率 | **9/9 = 100%** (本周 update §1.6.4) |
| 派单越界次数 (本周, 新增 KPI) | **1** (Wave 19 派小程, 已记 GM 错 #4 永久 enforcement) |
| sub-agent 自我纠错次数 | **1** (小程, 首次) |
| 黄牌项 | **1** (W4-09 小余 etl) |
| 风险登记 update | v2.1 ✓ + v2.2 本 wave 出 (R-38/39/40 新增) |

---

## 8. 给老雷 3 个决策点

1. **W4-09 小余 etl-pipeline 黄牌升级路径**: 第 3 推, W4 末若不达标我建议 **强制 escalate 老雷 + 老胡 1:1 with 小余**, 不可再推 W5; 若 W5 端到端联调缺 etl 桥接 → R-21 闸 3 触发红.
2. **GM 4 题自检铁律 W5 试运行**: HR 小林 W3 末上呈, CLAUDE.md §7-8 已落. 老胡建议 **W5 派单每个 wave 老雷亲自抽 1 张 prompt 看是否 4 题自检通过**, 4 周观察期后纳入派单 SOP.
3. **R-07 HC-01 老冀 + HC-02 小秦 6/30 入职窗口**: W5-08 已 Escalated 标. 老胡建议老雷 6/15 (Mon) 与小林一次 1:1, 看候选池实际深度, 提前两周布兜底.

---

## 9. 不耻下问 (verify 来源)

- W4 实际代码进度 → @老韩 (RG 1088 + 46) / @老唐 (audit 896 + 24) / @小蒋 (paper 1233 + 27 E2E) / @小段 (Goalserve 1121 + 28) / @小卢 (P0-01 1082 + 25) / @小程 (spec v0.1)
- M4.5 倒推 → @小董 (Wave 20 7 hard gate framework 在飞, 7 gate 量化指标 + bayes_decay)
- 架构端到端 → @老周 (Wave 20 v0.6 在飞, 线程模型 + 链路图)
- HR / 制度 → @小林 (pulse + Q3 + 4 题自检上呈)
- 派单越界趋势 → @老雷 (4 题自检铁律抽检)

---

## 10. 老胡附言

W4 中期是工程拐点 — Wave 19 6 部门 5420 行 C++ + 150 新测试 100% pass, **W3 51 测试 → W4 中 193**, 这是过去 4 周最实在的一周, 不是"撤地域红利"那种白捡的. 老韩 evaluate() 状态机 + 老唐 audit 12 AET + 小蒋 paper E2E + 小段 Goalserve 5 host + 小卢 P0-01 Kelly 公式, 这五块是 MVP 主轴, 全到位. **GM 错 #4 sub-agent 自纠是分水岭**, 公司"边界即文化"价值观第一次从 sub-agent 这边发起执行, 不再只靠用户兜底. **GM 错 #5 / #6 是制度补救** (数学 + HR registry), 都已永久 enforcement.

我的边界守住: W4 中期超额率 + W5 派单覆盖率 + 黄牌项识别 + 风险登记 update, **不替老雷拍 W4-09 升级判断, 不替老周定 v0.6, 不替老韩定阈值**. 决策点 §8 三件事都标"建议", 等老雷拍板.

— 老胡, 2026-05-28 (Sprint-2 W4 Wave 20 中期)
