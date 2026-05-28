# 风险登记 v2.2

- Owner: 老胡 | 验收: 老雷 | Last review: 2026-05-28 (Sprint-2 W4 中期 Wave 20)
- 关联: `docs/SPRINTS/sprint-02-w4-midweek-progress.md` / `docs/RESEARCH/laohu-risk-registry-v2.1.md` / `docs/INCIDENTS/gm-self-mistakes-log.md`
---

## 0. TL;DR (v2.1 → v2.2)

- **关闭 0 / 降级 1 (R-21: 12 → 9, W4 闸 2 闭环) / 升级 1 (R-34: 6 → 9, GM 错 #4#5#6) / 新增 3 (R-38/39/40)**
- **Top 5 不变**: R-02 / R-06 / R-07 / R-09 / R-31; R-38 候补 (W5-04bis e2e 实测后定)

---

## 1. v2.1 → v2.2 风险 update

| 编号 | v2.1 → v2.2 | 状态 | 变更原因 |
|---|---|---|---|
| R-21 paper engine 联调 | 12 → **9** | **降级** | W4-03 小蒋 paper main 1233 行 + 27 测试 + stcpp_paper E2E ✓; W5-05bis 60s smoke; 9/12 首判前置达成 |
| R-22 / R-23 / R-24 / R-25 / R-27 / R-28 / R-32 / R-33 / R-35 / R-36 / R-37 | 不变 | 缓解中/监控 | 详见 v2.1, W4 未触发变更 |
| R-31 R-20 PIT 违例 | 12 → 12 | 降级中 H 维持 | grep + 21 enum + audit 5 case + Goalserve 4ts UPSTREAM_PAYLOAD ✓; W5 e2e 跑通后再降 9 |
| R-34 GM 错流程 | 6 → **9** | **升级** | W4 累计 #4 #5 #6, sub-agent 自纠 #4 是分水岭但 #5 #6 仍用户兜底 → P 升 |

---

## 2. 新增风险 R-38 / R-39 / R-40

### R-38 W5 端到端联调 WAL fsync 集中爆发 (12 = 4×3, 缓解中)

**来源**: 老周架构 v0.6 (Wave 20 在飞) 识别. W5-04bis e2e smoke test 把 paper engine + Goalserve client + PM client + WSS + SPSC + audit WAL 全链路启动 60s, 4 路 WAL (risk/paper/shadow/audit) **同时 fsync** 在跨洋 us-east-1 EBS gp3 上可能触发 fsync 集中阻塞.

**为什么是风险**: W4 单模块测试不暴露 — RiskGateway 46 测试 / AuditEmitter 24 测试都是单元测试, WAL writer 单独跑 fsync p99 也 OK. **多路 WAL 并发 + 跨洋磁盘 latency** 是 e2e 首发场景, R-12 红线 "WSS event loop 锁 > 100us = P0" 可能在 e2e 跑出.

**缓解**:
1. W5-04bis e2e smoke 加 WAL fsync p99 监控 (Prometheus `wal_fsync_p99` 指标 — W5-05bis 小郑 已纳入 12 指标)
2. 老王 W5 准备 fsync_group_commit batch size 调优 patch 备用
3. 老周 v0.6 §WAL 章节加 "多路 WAL fsync 并发预算" 表
4. 若 e2e p99 > 50ms (单 fsync) → R-12 触发风险升级

**触发**: e2e smoke 60s 内 WAL fsync p99 > 50ms 或 paper_audit_write_lag 超 200ms → 升级到 P1, 老周 + 老王 + 老韩 4h 内召集

**Owner**: 老周 + 老王 + 老韩 + 小蒋

### R-39 sub-agent 自我纠错可能误伤 (8 = 2×4, 监控 — 派老徐 escalate 流程)

**来源**: GM 错 #4 — 小程拒接 P0-01 C++ stub 派单, **拒接是对的** (违反 persona 边界). 但**未来情境**: 若 GM 派单 prompt 实际合理 (例如临时 IC 补位 / Sprint 紧急需求 / Owner 借调), sub-agent 仍按 persona §拒绝任务 机械拒接 → 误伤生产力.

**为什么是风险**:
- 边界 = 静态文件, 业务 = 动态需求, 长期必有冲突
- 当前无机制区分 "GM 越界派单 (#4 类)" vs "GM 紧急合理派单"
- sub-agent 拒接后 GM 是 ack 还是 escalate? 当前默认 ack (重派他人), 但若全员都拒接同一类任务 → 死锁

**缓解** (派老徐 v0.2 ai-ops 设计 escalate 流程):
1. **派老徐 Sprint-2 W5 出 `docs/RESEARCH/laoxu-ai-ops-escalate-v0.2.md`** — sub-agent 拒接 → owner 二次确认 → GM 仲裁 → 4 题自检 → 重派 or 确认越界 全流程
2. CLAUDE.md §7-8 4 题自检铁律 + escalate 流程双轨, persona 边界 ≠ 不可妥协
3. 老胡周报新增 "本周 sub-agent 拒接次数 + 合理性占比" KPI (W4 Wave 19 = 1 次, 100% 合理)

**触发**: 单 Sprint 内 sub-agent 拒接次数 > 5 且合理性占比 < 70% → P2 alert, 派老徐 review escalate 流程

**Owner**: 老徐 + 老胡 + 老雷

### R-40 HR registry 与 .claude/agents/ 不一致 (6 = 2×3, 缓解中)

**来源**: GM 错 #6 — founding cohort 57 人 0 HR 注册 7 天. 制度补救后 `docs/HIRING/employee-registry.md` 立, 但**registry / AGENT.md / `.claude/agents/*.md` 三处数据源**, 长期一致性维护是新风险.

**为什么是风险**:
- 离职 / 转岗 / 状态变更 (Active / Standby / Deprecated) 5 工作日同步规则刚立, 缺工具
- AGENT.md 5 单元表 + registry persona 表 + `.claude/agents/` file 数 三者必须永远一致
- 历史已踩 GM 错 #5 (AGENT.md 数学算错), 三方源 drift 风险叠加

**缓解**:
1. W4-12 小宋 `tests/ci_grep/registry_consistency.py` 已落 (GM 错 #6 永久 enforcement): registry persona 数 = AGENT.md 数 = `.claude/agents/*.md` 数 (file #35 算 10), 任一不一致 fail
2. 小米 W5 归档时三方源 cross-check 入文档健康度评级
3. 小林 HR 每月第 1 周 pulse 时核 registry (Sprint-2 起常态化)

**触发**: CI grep `registry_consistency` fail 或 小林月度 pulse 发现 drift → P3 alert, 4 题自检铁律 + 老胡 24h 内同步

**Owner**: 小宋 + 小米 + 小林

---

## 3. Top 5 (v2.2, 不变)

| 排名 | 编号 | 描述 | I × P | 变动 |
|---|---|---|---|---|
| Top 1 | R-02 | RiskGateway 绕过 | 20 (5×4) | 不变, W4 evaluate() 落码 ✓, W5 e2e 端到端验证 |
| Top 2 | R-06 | seconds_delay 吃 PnL | 16 (4×4) | 不变 |
| Top 3 | R-07 | HC-01/02 招聘失败 | 16 (4×4) | 6/30 deadline 临近 (W5-08 Escalated 准备) |
| Top 4 | R-09 | Sharpe > 1 不达 | 15 (5×3) | 不变 |
| Top 5 | R-31 | R-20 PIT 违例 | 12 (3×4) | 不变 (W5 e2e 联调跑通后再降 9) |
| **候补** | **R-38** | **e2e WAL fsync 集中爆发** | **12 (4×3)** | **新增, 与 R-31 平分 Top 5, 等 W5-04bis e2e 实测后定优先级** |

R-39 (8) / R-40 (6) / R-34 升 (9) 评分均低于 Top 5 阈值.

---

## 4. 老胡附言

R-38 老周 v0.6 push 加 (我识别不到), R-39 是 #4 自纠的反面, R-40 是 #6 补救的拖尾. R-21 降 9 = paper E2E 跑通真战果; R-34 升 9 = 6 错诚实 (#5 #6 仍用户兜底). 不耻下问: R-38 @老周 + @老王 + @小蒋; R-39 @老徐 (W5 escalate v0.2 必交) + @老雷; R-40 @小宋 ✓ + @小林 + @小米. 边界: 我整合, 不替阈值 / 架构 / 优先级.

— 老胡, 2026-05-28 (Sprint-2 W4 中期 Wave 20)
