# Sprint-2 W8 W1 周报 (给老雷)

- **Owner:** 老胡 (pm-project-manager, E-026)
- **周期:** 2026-06-22 (Mon) → 2026-06-26 (Fri), Sprint-2 W8 W1
- **报告日:** 2026-05-28 (Wave 36)
- **状态:** 2 红 P0 (ADR 撤回率 + worktree 数据丢失) / 余 4 项绿
- **抄送:** 老雷 / 老郭 / 老韩 / 老周 / 小梁 / 小余
- **关联:**
  - 上一份: `docs/SPRINTS/sprint-02-w7-w3-progress.md`
  - 风险登记: `docs/RESEARCH/laohu-risk-registry-v2.4.md`
  - GM 错 log: `docs/INCIDENTS/gm-self-mistakes-log.md` (错 #17-21 本 wave 入)
  - 里程碑 W7 基线: `docs/SPRINTS/milestone-progress-w7.md`
  - Wave 34 commit: `c48a731`
  - Wave 35 commit: `4394693` (merge: `2560966`)

---

## §0 TL;DR 给老雷 (一段话)

W8 W1 (Wave 34/35) 实施 worktree 首战: 老孙 libsodium ed25519 落码, 老高 PR v1.5 落码, ctest 441/441 baseline 不破. **老雷必须看的 3 件事**: (1) **2 红 P0** — ADR 撤回率 3 次 (020 → 022 → 023, GM 1 天 3 ADR 误判老板原意) + worktree 数据丢失 1 次 (老高 Wave 34 6 项产出 GM force-remove 漏 cp, GM 错 #21, 老高 Wave 35 全部重做); (2) **1 黄** — Ninja $(MAKE) hotfix (ADR-005 §3.2 P0 例外, 合规标注); (3) **4 绿** — GM 越权 src/include 0 次 / worktree 使用率 100% / 文件抢占 0 / ctest 441/441. W8 W2 起立 2 个新 KPI (ADR 撤回率 < 1/sprint + worktree 数据丢失次数 0) 并入周报 §13.

---

## §1 W8 W1 完成 (git log 实测)

### git log 实测数据

| commit | 内容 | +行 | -行 | ctest |
|---|---|---|---|---|
| c48a731 | Wave 34: 老孙 libsodium + ADR-022/023 撤回 ADR-020 + GM 错 #17-20 | +1511 | -246 | 441/441 |
| 4394693 | Wave 35: 老高 PR v1.5 + 2 新 grep (GM 错 #21 补救重做) | +571 | -17 | 441/441 |
| 2560966 | merge: 老高 PR v1.5 Wave 35 → main | — | — | — |

**W8 W1 累计代码净增 (Wave 34/35, cpp 改动): +288 行 / -246 行 (老孙 ed25519.cpp + signer_v52 重构)**

> Wave 34 总 +1511/-246 行: cpp/hpp 净增 +288/-246 (老孙 crypto), 余为文档/ADR/CI. Wave 35 +571/-17 全在 .github/ + tests/ci_grep/ + docs/, cpp 净增 0. 符合 W8 W1 "libsodium 落地 + CI 强化"定位.

---

### 5 IC 交付汇总 (Wave 34/35)

| 单元 | IC | 交付摘要 | cpp 改动 | ctest 增量 |
|---|---|---|---|---|
| A 老周 | 老孙 | libsodium ExternalProject_Add v0.2: ed25519.hpp + SecureBuffer<N> + ed25519.cpp + CMakeLists.txt; signer_v52 撤 vcpkg brew 双路径 → 统一 stcpp_crypto_ed25519; GM 1 行 Ninja hotfix (ADR-005 §3.2 P0 例外) | +288/-246 cpp+hpp | 0 (ADR-023 W8 W2 老孙补) |
| E 老胡 | 小宋 | W3-W7 retro 审查 32 盲点 + 4 IC bug 候选 (RM-01/02 OR 断言 / AUDIT-01 弱断言 / SIGNER-01 future-ts); ADR-023 撤 Tester 后信息可选参考 | 0 cpp | 0 |
| A 老周 | 老高 | PR review v1.5 (Wave 35 GM 错 #21 补救重做): ic_no_self_test.py + worktree_check.py + abi_lock.py v1.5 + pr.yml v1.5 (17 grep job) + PR template v1.5 §9.4 | 0 cpp | 0 |
| F 老雷 | 老徐 | Escalate #2 dry-run: persona_boundary_check.py FAIL 验证 + Escalate #2/3/4 retro 全填 | 0 cpp | 0 |
| E 老胡 | 老胡 (本) | W7 W3 周报 + 里程碑文档 + KPI v3 + 模板 v3 | 0 cpp | 0 |

**W8 W1 ctest:** 441/441 (波次 34/35 均不破基线, ADR-023 撤 Tester 后 W8 W2 老孙补测试)

---

## §2 W8 W1 阻塞

### 已解: 老高 Wave 34 产出丢失 (GM 错 #21)

- **现象:** GM force-remove 老高 worktree 漏 cp 6 项产出 (ic_no_self_test.py / worktree_check.py / abi_lock.py v1.5 / pr.yml v1.5 / PR template v1.5 / laogao-pr-review-v1.5.md)
- **处置:** 老高 Wave 35 重做全部 6 项, worktree 内 git commit 后 GM merge → main; Wave 35 commit 4394693 验证
- **根因:** GM 错 #20 (未要求 worktree 内 commit) + GM 错 #21 (force-remove 前未 cp)
- **状态:** 闭环 (Wave 35 全部重做 + ADR-024 W8 W2 老郭立 8 步标准流程)

### 监控: ADR-023 后 老孙 ctest 缺口

- **现象:** ADR-023 撤 Tester 层, 老孙 Wave 34 按 ADR-020 旧版本未加 test; ADR-023 最终版 IC 自测唯一裁判, 老孙 W8 W2 需补 ctest
- **ETA:** 老孙 W8 W2 (SIGNER-01 修 + ctest 补)
- **风险:** W8 W2 前 ctest 441/441 baseline 不增, 技术债留存 1 周

### 监控: 小彭 OQ-P02-3 inplay odds 单源 ack

- **来源:** 小卢 P0-02 cpp 前置依赖 OQ-P02-3 ack
- **ETA:** 老彭 W8 W2 ack
- **风险:** 若 W8 W2 未 ack, 小卢 P0-02 cpp 推 W8 W3

---

## §3 风险登记 update (v2.4 维持, 2 新险 watch)

详细版本: `docs/RESEARCH/laohu-risk-registry-v2.4.md`

| 变更 | 内容 |
|---|---|
| R-45 (watch) | ADR 误判率偏高 — W8 W1 3 次撤回, W8 W2 起立 ADR 撤回率 KPI; 老郭 ADR-024 配套流程 |
| R-46 (watch) | worktree 数据丢失 — W8 W1 1 次 (GM 错 #21); ADR-024 8 步标准流程 W8 W2 立; 期望 W8 W2 起 0 |
| R-42 (降级维持) | sub-agent build verification — W8 W1 100% worktree 实证, R-42 仍观察; W8-W10 数据稳定后降 |

**Top 5 v2.4 (W8 W1 末不变):**

| 排名 | 编号 | 描述 | I×P | 动态 |
|---|---|---|---|---|
| 1 | R-02 | RiskGateway 绕过 | 20 (5×4) | 不变 |
| 2 | R-06 | seconds_delay 吃 PnL | 16 (4×4) | 不变 |
| 3 | R-07 | HC-01/02 招聘失败 | 16 (4×4) | HC-08 8/15 入职确认; HC-01/02 仍待定 |
| 4 | R-09 | Sharpe > 1 不达 | 15 (5×3) | 不变 |
| 5 | R-42 | sub-agent build verification | 15 (5×3) | W8 W1 100% worktree 实证, 持续观察 |

---

## §4 本周 SSOT 版本演进

### §4.1 vN → v(N+1) 推翻清单

| owner | 单元 | SSOT 文档 | vN | v(N+1) | 推翻结论 |
|---|---|---|---|---|---|
| 老孙 | A | ed25519/signer_v52 实现 | brew find_library 双路径 | **FetchContent ExternalProject_Add 单路径** | 撤 vcpkg/brew 混用; stcpp_crypto_ed25519 INTERFACE target 统一 |
| 老高 | A | laogao-pr-review | v1.4 | **v1.5** | +2 grep (ic_no_self_test + worktree_check); ADR-023 反转 ic_no_self_test 语义; pr.yml 15 → 17 job |
| ADR-020 | GM | IC ≠ Tester 分离 | 立 (W7 末) | **撤回** (W8 W1) | GM 误判老板"自己当裁判"原意; 实际 = 双层裁判不合理 |
| ADR-022 | GM | IC 自测 + Tester review | 立 (W8 W1 数小时) | **撤回** (W8 W1 当天) | 老板 "Tester review 不需要"; 当天撤 |
| ADR-023 | GM | IC 自测唯一裁判 (最终) | 立 (W8 W1 末) | **维持** | 回 W3-W7 模式; Tester 角色归正 (小宋/HC-08 owner integration, 非 review unit) |

### §4.2 ADR 总数 W7 → W8 W1

- W7 末: 21 (ADR-001 → ADR-021)
- W8 W1 末: **24** (+ADR-022 撤 / +ADR-023 IC 自测最终 / +ADR-024 老郭 W8 W2 立, 已预立编号)
- 净有效 ADR: 22 (ADR-020 撤回不计; ADR-022 撤回不计)

### §4.3 W8 watch list

- ADR-024 老郭 W8 W2 立 (worktree 8 步标准流程 + KPI 配套)
- 老孙 W8 W2 SIGNER-01 修 + ctest 补 (ADR-023 配套)
- 小宋 retro 32 盲点: 4 IC bug 候选 (RM-01/02 + AUDIT-01 + SIGNER-01) 派回原 IC 自决

---

## §5 主管派单 KPI

| KPI | W7 实测 | W8 W1 实测 | 状态 |
|---|---|---|---|
| 主管派单覆盖率 | 100% | 100% (Wave 34/35 全 5 IC 均主管或 GM 派) | 绿 |
| Sonnet 派单率 (ADR-009 v2) | 100% | 100% | 绿 |
| ctest 通过率 | 441/441 = 100% | 441/441 = 100% | 绿 |
| 主管帽 cpp 行数 | 0 | 0 (老周/老韩/小梁/小余/老胡 全 0) | 绿 |
| GM 越权代修 src/include | 0 | 0 (老孙 worktree 写 ed25519 是 sub-agent; main signer_v52 是 IC 错 #19, 非 GM 越权) | 绿 |

---

## §6 W8 W1 KPI 红绿灯 (6 项)

| KPI | W7 末基线 | W8 W1 实测 | 状态 |
|---|---|---|---|
| GM hotfix 率 | 0% | 1 次 (Ninja $(MAKE) → make, ADR-005 §3.2 P0 例外) | 黄 (例外标注合规) |
| GM 越权 commit src/include | 0 | 0 | 绿 |
| worktree 使用率 | 0% | **100%** (Wave 34/35 全 worktree) | 绿 |
| 文件抢占次数 | 0 | 0 (worktree 物理隔离实证) | 绿 |
| ADR 撤回率 | n/a | **3 次 (ADR-020 → ADR-022 → ADR-023)** | **红 P0** |
| worktree 数据丢失次数 | n/a | **1 次 (老高 6 项, GM 错 #21)** | **红 P0** |

---

## §7 Build Verification + Hotfix KPI

| KPI | W6 W2 基线 | W7 实测 | W8 W1 实测 | W8 W2 期望 |
|---|---|---|---|---|
| GM hotfix 率 | 62.5% | 0% | **1 次** (ADR-005 §3.2 例外) | 0% |
| GM 越权代修 src/include | 10+ 次 | 0 次 | **0 次** | 0 |
| 派单 prompt build+ctest 约束覆盖率 | 0% | 100% | **100%** | 100% |

**W8 W1 hotfix 标注:** Ninja $(MAKE) → make 1 行修改, ADR-005 §3.2 P0 例外流程走通 (Ninja 兼容性不属跨界代修), 合规.

---

## §8 Commit Hygiene KPI

| KPI | W7 实测 | W8 W1 实测 | W8 W2 期望 |
|---|---|---|---|
| 误推临时 build dir 次数 | 0 | **0** | 0 |
| 误推其他大文件次数 | 0 | **0** | 0 |
| GM 错号累计 | 16 (错 #15/#16 入) | **21** (错 #17/#18/#19/#20/#21 入) | 降趋势 |

**W8 W1 GM 错 #17-21 简述:**

| 编号 | 内容 | 影响 |
|---|---|---|
| #17 | ADR-020 误判老板"自己当裁判"原意 → IC 不写测试是错读 | ADR-020 撤回; 老板二次校正 |
| #18 | ADR-022 又误判 → Tester review 也不需要, 1 天 3 ADR | ADR-022 当天撤; 老板三次校正 |
| #19 | ADR-021 worktree 隔离不完全 → Edit tool 跨 path 写 main tree | 老孙 Wave 34 main tree 被改 (非 worktree) |
| #20 | 派单 prompt 未要求 worktree 内 git commit | GM 需 cp 而非 git merge; 老高产出丢失前置 |
| #21 | Force-remove 老高 worktree 漏 cp 6 项产出 | 老高 Wave 35 全部重做 |

---

## §9 GM 越权代修次数

| 周期 | GM 代修 src/include 次数 | 状态 |
|---|---|---|
| W6 W2 | 10+ 处 | P0 红灯 |
| W6 W3 | 2 处 | 改善 |
| W7 | 0 | 绿 |
| W8 W1 | **0** | 绿 ✓ |
| W8 W2 期望 | 0 | 目标 |

---

## §10 ADR 撤回率 KPI (新, W8 W1 P0 红灯触发)

> W8 W1 老板 verbatim 三次校正, GM 立了 3 ADR (020/022/023), 撤 2 (020/022). ADR 滥用 / 误判老板原意确认.

| KPI | W8 W1 实测 | W8 W2 目标 | 执行机制 |
|---|---|---|---|
| ADR 撤回次数 | **3 次 (020/022 撤, 023 最终版)** | 0 | ADR 立项前 verbatim 复述老板原话 + 列 2-3 候选方案 + 等二次 ack |
| ADR 撤回率目标 | — | < 1/sprint | 老胡周报 §10 永久跟踪 (GM 错 #18 配套) |
| 1 天多 ADR 限制 | 1 天 3 ADR (W8 W1 超标) | ≤ 1 ADR/天 (非紧急) | GM 自查 4 题检查表 (CLAUDE.md §7 规则 8) |

---

## §11 worktree KPI (ADR-021 §7)

| KPI | W7 基线 | W8 W1 实测 | W8 W2 目标 |
|---|---|---|---|
| worktree 使用率 | 0% (Wave 33 GM 错 #15 单次容忍) | **100%** (Wave 34/35 全 worktree) | 100% |
| 文件抢占次数 | 1 (老唐 ninja cache 脏, 自然恢复) | **0** (worktree 隔离实证) | 0 |
| P0 例外次数 | 0 | 0 | ≤1/week |

---

## §12 worktree 数据丢失 KPI (新, W8 W1 P0 红灯触发)

> W8 W1 GM force-remove 老高 worktree 漏 cp 6 项产出 (GM 错 #21), 老高 Wave 35 重做.

| KPI | W8 W1 实测 | W8 W2 目标 | 执行机制 |
|---|---|---|---|
| worktree 数据丢失次数 | **1 次 (老高 6 项)** | 0 | ADR-024 8 步标准流程 (老郭 W8 W2 立); git log audit (sub-agent commit → worktree branch → merge main) |
| 重做浪费时间 | 1 IC wave (老高 Wave 35) | 0 | GM remove worktree 前必 verify: `git log worktree-branch --oneline` 确认全 commit |

---

## §13 ADR 撤回率 (§10 并项, GM 错 #18 永久 enforcement)

见 §10. 本节作为 §13 永久 tracking slot, 每周五周报必填.

| 周期 | 撤回次数 | 状态 |
|---|---|---|
| W7 末 | 0 | — |
| W8 W1 | 3 (ADR-020 / ADR-022 / ADR-023 三连) | **红 P0** |
| W8 W2 目标 | 0 | — |

---

## §14 里程碑进度 update (W8 W1 末)

| 里程碑 | W7 末基线 | W8 W1 末 | 差异 | 关键变化 |
|---|---|---|---|---|
| M1 MVP (2026-11) | 63% | **64%** | +1pp | 老孙 libsodium 落 + signer_v52 重构; PnL 看板仍 0/8 |
| M2 (2027-08-06) | 40% | **41%** | +1pp | libsodium 链 = M2 倒推前置之一; paper runtime 仍未启 |
| M4.5 (2027-05) | 10% | **10%** | 0pp | paper runtime 仍 0%; Sprint-3 W11 才启动 |
| M5 (2027-11) | 5% | **8%** | +3pp | libsodium ed25519 = signer M5+ live 首笔前置; secp256k1 仍未探 |

**时间消耗 28% (W8 W1 末), M1 工作 64%, 领先 36pp** (vs W7 末 38pp, 小幅下降因 ADR 撤回 + 老高重做浪费时间约 0.3pp).

### M1 关键路径 (W8 W1 → M1 达标)

1. **P0-02 cpp** — 小卢 W8 W2 (OQ-P02-3 老彭 ack 后); M1-C01 前置
2. **WAL-B01/B02/B03** — 老王 Sprint-3 W9/W10/W11; M1-F02 + paper runtime
3. **paper runtime 真启动** — Sprint-3 W11; M1-D 全段
4. **PnL 看板 M1-H** — Sprint-3 启动 + Sprint-4; 0/8 最大欠账

---

## §15 W8 W2 backlog (本 Wave 36 已知)

| IC | 单元 | 任务 | 优先级 | 依赖 |
|---|---|---|---|---|
| 老孙 | A | SignerV52 ctest 补 + SIGNER-01 修 (worktree+commit) | P0 | ADR-023 最终版; ctest baseline 回补 |
| 老郭 | F | ADR-024 worktree 8 步标准流程立 (worktree+commit) | P0 | GM 错 #21 配套; KPI §12 执行机制依赖 |
| 老胡 | E | W8 W1 周报 (本文, worktree+commit) | P1 | — |
| 老韩 | B | RM-01/02 OR 断言自审 (worktree+commit) | P1 | 小宋 retro 4 bug 候选; IC 自决 |
| 老唐 | A | AUDIT-01 弱断言自审 + first_mismatch (worktree+commit) | P1 | 小宋 retro 4 bug 候选; IC 自决 |
| 老彭 | C | OQ-P02-3 inplay odds 单源 ack (worktree+commit) | P0 | 小卢 P0-02 cpp 前置; 不 ack 则 P0-02 推 W8 W3 |

---

## §16 W8 W3+ backlog 预排

| IC | 单元 | 任务 | 前置 |
|---|---|---|---|
| 小卢 | A | P0-02 cpp (Wave N-A) | OQ-P02-3 老彭 W8 W2 ack |
| 小袁 + 小邓 | C+D | ADR-010 §4 存量 -Wno- 清理 (老高 v1.4 dry-run 发现) | W8 W2 排期确认 |
| 老姜 | A | ADR-010 §5 perf -Wno- 例外申请 | 小袁+小邓 清理进度 |
| 小宋 | E | integration 扩 (W3-W7 retro 32 盲点 IC 可选参考) | ADR-023 最终版; 小宋 owner integration |
| 老高 | A | v1.6 worktree_commit_check + gm_merge_audit (worktree+commit) | ADR-024 立后; W8 W3 |

---

## §17 Sprint-3 Sprint Planning 前置 (6/29 周一, 老胡主持)

| Sprint-3 Week | 目标 | 关键 ticket | Owner |
|---|---|---|---|
| **W9** (6/29-7/03) | WAL-B01 + Sprint Planning | WAL-B01 WalWriter::Open(); Sprint Planning 6/29 | 老王 + 小石 |
| **W10** (7/06-7/10) | WAL-B02 + 端到端联调起步 | WAL-B02 WalWriter::Append(); SPSC ring 接通 | 老王 + 小蒋 + 老李 + 小冯 |
| **W11** (7/13-7/17) | WAL-B03 + paper runtime 真启动 | WAL-B03 + paper runtime 端到端首跑; M2 gate 数据起步 | 老王 + 全链路联调 |

**6/29 Sprint Planning 议程 (老胡主持):**
1. Sprint-2 retro 快回顾 (老高 CI KPI + 老胡 PM KPI + 2 红 P0 ADR 撤回/数据丢失复盘)
2. Sprint-3 WAL-B01/B02/B03 spec 逐条确认 (老王 + 老周)
3. libsodium 集成 W8 → Sprint-3 衔接 (老孙 + 老韩)
4. HC-08 onboarding 计划融入 Sprint-3 节奏 (老胡 + 小林)
5. M2 gate 数据收集计划 (小梁 + 小蒋 + 老彭)
6. ADR-023 IC 自测节奏确认 + ADR-024 worktree 流程 (老郭 + 老胡)

**Sprint-3 关键路径 (W8 W1 末更新):**
- WAL-B01/B02/B03 串行依赖 (B01 → B02 → B03), 老王唯一 owner, 不可并行
- 老孙 libsodium ctest 补 W8 W2 完成, Sprint-3 W9 前无遗留
- HC-08 8/15 入职 onboarding 计划 老胡 + 小林 W8 W4 前出
- 老郭 ADR-024 W8 W2 落, worktree 流程 Sprint-3 全程强 enforce

---

## §18 不耻下问

| 问 | 被问方 | 内容 | ETA |
|---|---|---|---|
| ADR-024 8 步流程 + KPI §12 配合 | @老郭 | worktree remove 前 git log 校验 SOP; ADR-024 W8 W2 立并推 PR | W8 W2 |
| W8 W3 v1.6 worktree_commit_check.py + gm_merge_audit.py | @老高 | ADR-024 落后实现 CI 层校验; W8 W3 派单 | W8 W3 |
| GM 错 #17-21 retro 书面 7/5 EOW | @老雷 | 5 错 retro 书面 + ADR 误判意图说明 + worktree 数据丢失 SOP | 7/5 EOW |
| W8 W2-W4 各单元 backlog | @5 主管 | W8 W2 backlog 收集, 老胡整合排期 | W8 W2 EOW |
| HC-08 8/15 onboarding plan | @小林 | JD 草稿 + 入职前置 checklist; ADR-023 职责写入 JD | W8 W4 前 |

---

## §19 完成汇报

W8 W1 交付: Wave 34 (c48a731) + Wave 35 (4394693, merge 2560966) 全清. ctest 441/441 维持. worktree 使用率 100% (首战成功). GM 错 #17-21 全公开 INCIDENTS.

**6 KPI 红绿灯:**
- 红 P0 x2: ADR 撤回率 3 次 (W8 W1 严重超标) + worktree 数据丢失 1 次 (老高 Wave 35 重做)
- 黄 x1: GM hotfix 率 1 次 (ADR-005 §3.2 例外合规)
- 绿 x3: GM 越权代修 0 + worktree 使用率 100% + 文件抢占 0

**里程碑:** M1 64% / M2 41% / M4.5 10% / M5 8% (时间 28%, 领先 36pp)

**W8 W2 backlog:** 6 任务 (老孙 ctest + 老郭 ADR-024 + 老胡周报 + 老韩 RM + 老唐 AUDIT + 老彭 OQ-P02-3)

**git log 实测交叉验证:** Wave 34 c48a731 (+1511/-246) + Wave 35 4394693 (+571/-17) / ctest 441/441 两次验证 / 老胡 0 cpp 改动 (PM 帽维持) / worktree branch: worktree-agent-ae8ecf73694a70b8e

— 老胡, 2026-05-28 (Sprint-2 W8 W1 末, Wave 36)
