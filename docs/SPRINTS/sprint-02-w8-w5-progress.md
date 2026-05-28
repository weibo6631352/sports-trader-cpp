# Sprint-2 W8 W5 周报 (给老雷)

- **Owner:** 老胡 (pm-project-manager, E-026)
- **周期:** 2026-06-29 (Mon) → 2026-07-03 (Fri), Sprint-2 W8 W5
- **报告日:** 2026-05-29 (Wave 45)
- **状态:** 3 红 P0 / 1 黄 / 3 绿
- **抄送:** 老雷 / 老郭 / 老韩 / 老周 / 小梁 / 小余 / 老钱
- **关联:**
  - 上一份: `docs/SPRINTS/sprint-02-w8-w1-progress.md`
  - 风险登记: `docs/RESEARCH/laohu-risk-registry-v2.4.md`
  - GM 错 log: `docs/INCIDENTS/gm-self-mistakes-log.md` (错 #22/23 本期入)
  - Wave 45 (本 wave) commit: 本文

---

## §0 TL;DR 给老雷 (一段话)

W8 W4-W5 (Wave 37-45) 完成: 老李/小段 数据结构 SSOT 落地 (Wave 40), 老周 ABI gap audit + 后端 REST API spec (Wave 40/42), 老胡 Wave 41 复盘会准备 + ADR-027 + Stage-Gate framework (Wave 41/43/44/45). **老雷必须看的 3 件事:** (1) **3 红 P0** — ADR 撤回率 5+ 次 (ADR-020/022/025/026 四撤) + 跨 worktree 写 main 重犯 5 次 (老孙/老彭/老韩/老胡/老李) + GM 错累计 23 (#22 ABI 漏 ~4 周 + #23 派单 prompt 无 WebFetch); (2) **里程碑 M1 64% 维持, M5 +3pp** — libsodium ed25519 + signer_v52 落地, 但 ABI gap 待修; (3) **W9 四大议题** — ABI 修复优先 + REST skeleton + 选址实测 + 数据结构 IC 招聘. Sprint-3 Sprint Planning W9 W1 老胡主持.

---

## §1 W8 W4-W5 完成清单 (commit hash 实证)

| Wave | commit | 内容 | 单元 | IC |
|---|---|---|---|---|
| Wave 37 | f9b8942, f32967a | 老吴 Polymarket origin 实测 + ADR-013 HOLD; 小段 Goalserve inplay Sofia BG 实测 | A/D | 老吴/小段 |
| Wave 37 | 4811584, eaefae4 | 老沈 RM Wave 36 redo 单一断言 + 6 P0; 老韩拒 STALE >= 修 | B | 老沈/老韩 |
| Wave 40 | 1052b14, a6a6641 | 老李 Polymarket 数据结构 SSOT v1; 小段 Goalserve 数据结构 SSOT v1 | A/D | 老李/小段 |
| Wave 40/41 | cbf2697, 44409a7 | 老周 ABI gap audit v1 (OrderIntent/Side enum); 老胡 Wave 41 P0 复盘会准备 | A/E | 老周/老胡 |
| Wave 42 | 9e7e4b4 | 老周 后端 debug REST API spec v1 (12 endpoint + cpp-httplib 选型) | A | 老周 |
| Wave 43/44 | f6d1f56, 214783f | 老胡 Stage-Gate 功能检查 framework + GM 授权范围请示 (老板 2026-05-29 verbatim) | E | 老胡 |
| Wave 44 | 2026-06-W4-adr-027 (ADR 文件) | ADR-027 核心数据结构 SSOT enforce (老郭主审, W8 W5 立) | F | 老郭 |

**W8 W4-W5 说明事项:**

- Wave 39 PositionManager 第 2 轮 — 老板撤回, 6 commit 全撤, doc 已 reset. 本期不计入完成清单.
- ADR-025/026 — 已撤回 (未保留有效 ADR 编号), 与 W8 W1 ADR-020/022 同模式.
- 王经理 #45 onboarding — Wave 43 正式启动, 本期跑中. Sprint-3 联决 ETA W9 W1.
- ADR-013 v2 选址 (老郭/老吴) — 本期并行; W9 W1-2 老吴 spin 3 AWS region 实测.

---

## §2 W8 W4-W5 KPI 红绿灯

| KPI | W8 W1 基线 | W8 W5 实测 | 变化 | 状态 |
|---|---|---|---|---|
| ctest 通过数 | 441 | 458 (+17) | 老孙 SignerV52 +9 + 老唐 AUDIT-01 +1 + 老沈 RM redo +7 | 绿 |
| ADR 立项总数 | 22 | 27 (+5: 023/024/025-撤/026-撤/027) | ADR-025/026 撤回 (Wave 39 配套) | 黄 |
| ADR 撤回率 (累计) | 3 次 | 5+ 次 (ADR-020/022/025/026 四撤) | Wave 39 撤回 2 新增 | 红 P0 |
| GM 错累计 | 21 | 23 (+#22 ABI gap + #23 WebFetch 缺) | #22 ABI 漏洞 ~4 周; #23 派单 prompt 无 WebFetch | 红 P0 |
| worktree 数据丢失 | 1 次 | 1 次 (不变) | W8 W2+ 未新增 | 绿 |
| worktree 使用率 | 100% | 100% | 全 worktree 执行 | 绿 |
| 跨 worktree 写 main | 0 次 | 5 次 (老孙/老彭/老韩/老胡/老李 各 1) | ADR-024 §3.1 pwd verify 已立; 派单 prompt 模板未 100% enforce | 红 P0 |
| Opus 使用率 | 0% | 0% | 全 Sonnet (ADR-009 v2) | 绿 |
| 主管 cpp 行数 | 0 | 0 | 5 主管 0 cpp 改动 | 绿 |

**3 红 P0 说明:**

1. **ADR 撤回率** — W8 W1 起已立为 KPI (< 1/sprint 目标). 本期新增 ADR-025/026 两撤, 累计 4 次撤回 (020/022/025/026). Wave 39 全撤触发; 根因: ADR 立项前 verbatim 复述老板原话未执行. ADR-027 (老郭主审) 本期落地为修正实例.
2. **GM 错累计 23** — #22 OrderIntent ABI 漏洞 (token_id/outcome/Side::Sell 缺失 ~4 周); #23 派单 prompt 没有 WebFetch 官网 verify 步骤. 两错均触发流程整改 (ADR-027 §4 + §5).
3. **跨 worktree 写 main 5 次** — W8 W3-W4 5 个 sub-agent 绕过 worktree 直接写 main tree. ADR-024 §3.1 已立规则但派单 prompt 模板未 100% enforce. 整改见 §6.

---

## §3 里程碑进度 update (W8 W5 末)

| 里程碑 | W7 末基线 | W8 W1 末 | W8 W5 末 | 变化 | 关键变化 |
|---|---|---|---|---|---|
| M1 MVP (2026-11) | 63% | 64% | 64% | +0pp | ABI gap 待修抵消 libsodium 落地增量; PnL 看板仍 0/8 |
| M2 Sharpe (2027-08) | 40% | 41% | 41% | +0pp | P0-02 cpp 老彭 ack 完成; inplay alpha v2 lower bound 估 1.5-2.5% |
| M4.5 paper gate (2027-05) | 10% | 10% | 10% | +0pp | paper runtime 仍未启 (Sprint-3 W11 节点) |
| M5 live (2027-11) | 5% | 8% | 8% | +0pp | ed25519 落地已记入; 选址 W9 W1-2 实测中 |

**时间消耗 29% (W8 W5 末), M1 工作 64%, 领先 35pp** (W8 W1 36pp → W8 W5 35pp, 小幅下降因 ABI 修复延迟 + Wave 39 全撤浪费).

### M1 关键路径 (W8 W5 → M1 达标)

1. **OrderIntent ABI 修复** — 老韩 v0.5 (W9 W1) + 老孙 SignerV52 align (W9 W2) + 老唐 audit v1.3 (W9 W3). 前置 ADR-027 通过 (已落 W8 W5).
2. **WAL-B01/B02/B03** — 老王 Sprint-3 W9/W10/W11 (M1-F02 paper runtime 前置).
3. **paper runtime 真启动** — Sprint-3 W11 (M1-D 全段, M4.5 gate 首步).
4. **PnL 看板 M1-H** — Sprint-3 启动 + Sprint-4 (0/8, 最大欠账; 小苏 + 小宫 W9 启动).

---

## §4 W9 (Sprint-3 启动) backlog — 等 GM 老雷 + 王经理 W8 W5 联决

| 议题 | 内容 | 预期决策方 | ETA |
|---|---|---|---|
| A: ABI 修复优先 | OrderIntent v0.5 (老韩) + SignerV52 align (老孙) + audit v1.3 (老唐) vs PositionManager 实施排序 | 老雷 + 老周 + 老韩 | W9 W1 |
| B: 后端 REST skeleton | 老周 + 小卢 W9 W1 开始 cpp-httplib skeleton (12 endpoint, laozhou-w8-debug-rest-api-spec-v1.md SSOT) | 老周 + 老雷 | W9 W1 |
| C: ADR-013 v2 选址实测 | 老吴 W9 W1 spin 3 AWS region (US-East / EU-Frankfurt / SG), 实测 RTT + 带宽; 结论 W9 W2 | 老郭 (主审) + 老雷 (拍板) | W9 W2 |
| D: 数据结构 IC 招聘 | 小林 HR W9 W1 启动 JD, 8/1 入职目标 (ADR-027 §4 FOM 第 4 名 approve 来源) | 小林 + 老雷 | W9 W1 JD 发布 |

---

## §5 风险 registry (P0/P1/P2 监控, W8 W5 末)

| 风险 ID | 描述 | 级别 | mitigation | Owner | 状态 |
|---|---|---|---|---|---|
| R-001 | OrderIntent ABI 修复延后 → M1 M4.5 paper runtime W11 节点存在 push 风险 | P0 | ADR-027 已立; W9 W1 老韩 v0.5 启动; M4.5 风险 W10 末评估 | 老胡 (跟进) + 老韩 (实施) | 活跃 |
| R-002 | 数据结构 IC 8/1 入职 — FOM 4 人 approve 缺第 4 名 (ADR-027 §4) | P1 | 小林 W9 W1 JD 发布; 面试周期 4 周估算; 8/1 入职可行但紧 | 小林 + 老雷 | 活跃 |
| R-003 | 老李/小段 SSOT 没 WebFetch 官网 verify (GM 错 #23) | P2 | 数据结构 IC 入职后 redo 四维扫描; 现有 SSOT 标记 unverified | 老胡 (标记) + 老李/小段 (redo) | Watch |
| R-004 | ADR-013 v2 选址未决 → 服务器购买 hold → M5 live 节点受压 | P1 | 老吴 W9 W1 spin 实测; 老郭 W9 W2 评审拍板 | 老郭 + 老吴 | 活跃 |
| R-005 | paper runtime W11 启动 — M4.5 硬节点; WAL-B01/B02/B03 串行依赖老王 | P0 | WAL-B01 WAL-B02 WAL-B03 Sprint-3 W9/W10/W11; 老王唯一 owner | 老王 (实施) + 老胡 (跟进) | 活跃 |
| R-006 | GM 错累计 23 — 派单 prompt 流程缺口持续; ADR-024 + ADR-027 已立未 100% enforce | P0 | 派单 prompt 模板硬 enforce §6 整改; 老高 W9 W4 4 grep deploy | 老胡 (跟进) + 老高 (CI) | 整改中 |
| R-007 | 顾问团激活不足 — 老板 verbatim "顾问们也别闲着"; F 单元 9 人仅老郭/老高/老彭活跃 | P2 | 老郭 协调人 W9 W1 激活意见箱; 老胡周报 §7 跟进 | 老郭 + 老胡 | Watch |

---

## §6 跨 worktree 写 main 重犯 5 次 — 升级 enforcement

**事件:** W8 W3-W4 5 个 sub-agent 跨 worktree 写 main tree:

| sub-agent | 写入内容 | 触发原因 |
|---|---|---|
| 老孙 | signer_v52 相关文件 | 派单 prompt 未含 pwd verify 步骤 |
| 老彭 | OQ-P02-3 ack 文档 | 同上 |
| 老韩 | RM audit spec 文档 | 同上 |
| 老胡 | W8 W1 周报 (Wave 36) | 同上 |
| 老李 | Polymarket SSOT 文档 | 同上 |

**根因:** ADR-024 §3.1 pwd verify 规则已立, 但派单 prompt 模板未 100% 包含强制检查步骤. 每次派单仍依赖 sub-agent 自觉.

**整改 W8 W5 (已执行):**

1. 本 wave (Wave 45) 派单 prompt 含 "pwd 输出 + Edit 前 ls verify path" 强 enforce 语句.
2. 老高 W9 W4 部署 abi_lock + worktree_commit_check + gm_merge_audit + ssot_check 4 grep (ADR-027 §4 CI 配套).
3. 周报 §6 永久跟踪跨 worktree 写 main 次数 (目标 0, 连续 3 sprint 0 后降为 Watch).

**W9 期望:** 跨 worktree 写 main = 0.

---

## §7 不耻下问

| 问 | 被问方 | 内容 | ETA |
|---|---|---|---|
| Sprint-3 排期联决 | @王经理 #45 | onboarding 中; ABI 修复 vs PositionManager 排序 联决 | W9 W1 |
| W8 W5 联决 (ABI/选址/招聘) | @老雷 GM | 议题 ABCD 四项 (§4) W9 W1 Sprint Planning 前拍板 | W8 W5 前 |
| 盈利 KR 数字拍板 | @老钱 CPO | STG-002 G5 验收量化 KR (PnL ≥ ? / Sharpe ≥ ?) | Sprint-3 Planning W9 W1 |
| ADR-013 v2 评审 + ADR-027 主审 W9 W4 | @老郭 | 选址实测结论评审 W9 W2; ADR-027 CI grep 老高 W9 W4 deploy 主审 | W9 W2 / W9 W4 |
| 数据结构 IC + HC-08 onboarding | @小林 HR | JD 发布 W9 W1; onboarding checklist (ADR-027 §4 FOM 第 4 名来源) | W9 W1 JD |
| W9 周会排期 | @5 主管 | Sprint-3 Planning 6/29 议程确认 (老胡主持); 各单元 W9 backlog | W9 W1 |
| 顾问团激活 | @老郭 (协调人) | F 单元 9 人激活意见箱; 老张/老何/老钱/老叶/老徐/小白/小邓 W9 W1 收口 | W9 W1 |

---

## §8 Opus 使用 KPI (ADR-009 v2, §6 监控)

| Sprint | Opus 使用次数 | 目标 | 状态 |
|---|---|---|---|
| Sprint-2 (W1-W8) | 0 次 | < 5% / sprint | 绿 |

全员 Sonnet 4.6 派单, W8 W4-W5 无 Opus 例外. ADR-009 v2 执行稳定.

---

## §9 W9 Sprint-3 Planning 议程 (老胡主持, 6/29 周一)

1. Sprint-2 retro 快回顾 (ADR 撤回 P0 + 跨 worktree 写 main P0 + GM 错 23 复盘)
2. Wave 39 PositionManager 撤回 + Wave 41 ABI gap 复盘 5min
3. Sprint-3 ABI 修复排期确认 (老韩 v0.5 + 老孙 align + 老唐 v1.3, W9-W10)
4. WAL-B01/B02/B03 spec 确认 (老王 + 老周)
5. 后端 REST skeleton W9 启动 (老周 + 小卢)
6. ADR-013 v2 选址实测启动 (老吴 W9 W1)
7. 数据结构 IC 招聘启动 (小林 W9 W1 JD)
8. Stage-Gate STG-001 老雷拍板 §3.1 授权范围 + STG-002 老钱 PnL KR 拍板
9. 顾问团激活 (老郭 W9 W1)

---

## §10 ADR 撤回率 (永久 tracking, GM 错 #18 enforcement)

| 周期 | 撤回次数 | 撤回 ADR | 状态 |
|---|---|---|---|
| W7 末 | 0 | — | — |
| W8 W1 | 3 次 | ADR-020 / ADR-022 (净有效 ADR-023) | 红 P0 |
| W8 W4-W5 | +2 次 | ADR-025 / ADR-026 (Wave 39 配套) | 红 P0 |
| 累计 | 5+ 次 | 020/022/025/026 撤, 023/024/027 有效 | 红 P0 — 严重超标 |
| W9 目标 | 0 | ADR 立项前 verbatim + 2-3 候选方案 + 二次 ack | 目标 |

---

## §11 核心数据结构 ABI audit (永久 tracking, GM 错 #22 enforcement)

| struct | cite: Polymarket SSOT | cite: Goalserve SSOT | FOM 4 人 approve | ADR-027 状态 |
|---|---|---|---|---|
| OrderIntent | 老李 spec v1 §88-91 (引用) | 小段 SSOT v1 (引用) | 老李 + 小段 + 老周 (3/4, 缺数据结构 IC) | ABI 修复 W9 待完成 |
| SignedOrder | 老李 spec v1 (引用) | N/A | 3/4 (同上) | 待修 |
| Side enum | 老李 handshake v1 §84 (引用) | N/A | 3/4 | 待修 |

FOM 第 4 名 = 数据结构 IC, 8/1 入职目标. W9 ABI 修复完成后补全 4 人 approve.

---

## §12 完成汇报 (必带)

pwd 验证: `/Users/wangweibo/code/sports-trader-cpp/.claude/worktrees/agent-aedcf21c56349c26f` (本 wave 全程 worktree, 未写 main tree).

W8 W5 周报交付:

- 7 KPI 红绿灯: **3 红 P0** (ADR 撤回率 5+ + GM 错 23 + 跨 worktree 写 main 5 次) / **1 黄** (ADR 立项总数 +5 含 2 撤) / **3 绿** (ctest +17 / worktree 使用率 100% / Opus 0)
- 里程碑: M1 64% (持平) / M2 41% (持平) / M4.5 10% (持平) / M5 8% (持平). 时间 29%, 领先 35pp.
- W9 议题 ABCD: ABI 修复 / REST skeleton / ADR-013 实测 / 数据结构 IC 招聘
- 7 风险 registry (R-001/005 P0 活跃, R-002/004 P1 活跃, R-003/006/007 整改中/Watch)
- 跨 worktree 写 main 5 次重犯升级 enforcement: 派单 prompt 模板 100% 强 enforce + 老高 W9 W4 4 grep deploy
- Stage-Gate framework 落 sprint-03-backlog.md (老板 2026-05-29 verbatim 触发)

worktree commit: 见本 wave git log (Wave 45).

— 老胡, 2026-05-29 (Sprint-2 W8 W5 末, Wave 45)
