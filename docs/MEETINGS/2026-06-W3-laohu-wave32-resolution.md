# W6 W3 问题全景 + 解决方案集成 v1

- **owner:** 老胡 (pm-project-manager, E 单元主管)
- **last_review:** 2026-06-W3
- **任务来源:** GM W6 Wave 32 — 主持 W6 W3 全员问题解决方案集成
- **集成范围:** GM 错 #11/#12/#13 + Wave 30 文件抢占 + 老周/老高联合 review
- **上报链:** 老胡整合 → 老郭过目 → GM 老雷拍板
- **约束:** 老胡 E 主管, W6 cpp = 0 严格守; 不替老板拍; review only + 不修代码

---

## §1 W6 W3 问题全景表 (13 项)

| ID | 问题 | 责任人 | 严重度 | 解决方案 | W7 deadline |
|---|---|---|---|---|---|
| GM-11 | 派单 prompt 无 build+ctest 验证硬约束, 10 IC 声称通过 GM 整合出 10 处 hotfix, hotfix 率 62.5% | GM 老雷 | P0 制度 | 派单 prompt 必含 hard 约束 (老高 PR review v1.3 grep 已落, W6 W3 起执行) | 已落 |
| GM-12 | gitignore 通配 `build_*/` 缺失, 误推 931 files | GM 老雷 | P0 已 fix | .gitignore 已补 `build_*/`; ADR-005 commit hygiene KPI 追踪 | 已落 |
| GM-13 | GM 越权代修 10+ 处别人代码, 无协调无上报, 违反 ADR-005 | GM 老雷 | P0 制度 | ADR-005 加 "GM 不越权" 铁律; CI commit author check warning-only (老高 W7 落) | 老高 W7 EOW |
| W30-CONFLICT | Wave 30 老孙 "顺手修" single_instance.cpp (非本人 owner), 小卢 owner 版本最终 wins | 老孙 / 流程 | 流程 P1 | ADR-005 §3.4 文件 ownership lock 子条款 (待老郭主笔); 派单 prompt 列允许修改文件清单 | W7 末 (老郭) |
| C-02/H-03 | 老孙 libsodium 用 brew `find_library` prebuilt, 违反 FetchContent 优先决策; CI Ubuntu 走 mock, 真 Ed25519 未被 CI 覆盖 | 老孙 | P1 | 老孙 W7 按老沈 SOP §6 原意改 FetchContent ExternalProject_Add; mock fallback 仅作 CMAKE_OFFLINE last resort | W7 W4 (7/3) |
| C-04/H-05 | tests/unit/CMakeLists.txt 4 方并发改动 (小卢/老孙/小田/GM 越权), 靠侥幸 "last write wins", 774+ 行维护性差 | 4 IC + GM 流程 | 流程 P1 | ADR-005 §3.4 文件 ownership lock 补丁; 高争用文件串行派单; 小宋 W7 整理重复 define + 注释 | W7 末 |
| C-06/H-07 | `friend class AuditEmitterPool` 是 GM 错 #11 hotfix 加入, 未经老唐正式 ack; 扫描显示 Pool 实际只走 public API, friend 声明可能多余 | 老唐 | P1 | 老唐 W7 正式 ack — 确认 friend 是设计意图 (持锁调 apply_hash_chain) 或提出重构方案; 若多余则移除 | W7 W4 (7/3) |
| H-10 | stcpp_polymarket_wss + stcpp_cli_* 含 ADR-010 §4 已删非 grandfather `-Wno-` flags, ADR-010 §4 W7 EOW deadline 漏网 | 小宋 | P1 | 小宋 W7 EOW 清理 `/src/stcpp/polymarket/wss/CMakeLists.txt` L22-23 和 `/src/stcpp/bin/CMakeLists.txt` L57-59, L77-79 | W7 EOW (7/5) |
| D-01 | `position_ledger.cpp::ToMarketIdArray` 用 `[[maybe_unused]]` 掩盖 dead code; 小蒋 W6 Wave 29 已改用 fill.market_id | 老王 | P1 | 老王 W7 删 `ToMarketIdArray` + 测试中 no-op 调用 | W7 W4 (7/3) |
| D-02 | `audit_chain_verify_test.cpp` L187 `bool first_mismatch_at = -1` 语义 bug (bool 接 index 语义, 非零 int 转 bool = true), GM 错 #11 机械改动遗留 | 老唐 | P1 | 老唐 W7 删 `first_mismatch_at` 变量 + L189 `(void)` suppressor; 与 C-06/H-07 同 wave 处理 | W7 W4 (7/3) |
| D-03 | wal_writer.cpp 3 处 "TODO W4" 长期挂账 (ring / bg fsync / fd / SPSC ring), W4→W5→W6 未清 | 老王 | P2 转 backlog | 老王 W7 将 3 处 TODO 转 Sprint-3 backlog (HC-07), 改注释 "Sprint-3 HC-07 实施"; 不在代码留开放 TODO | W7 W4 (7/3) |
| D-04 | rigtorp_mpmc `GIT_TAG=master` 浮动引用, 每次 configure 可能拉不同版本 | 小石 | P2 | 小石 W8 将 master 改为具体 commit SHA + 注释说明"无官方 tag" | W8 |
| C-08 | build-time switch 5 CMake option + 1 PRIVATE define (STCPP_TEST_BUILD), CI 只测 paper mode, 12 组合矩阵无上限规范 | 老郭 (仲裁) + GM | P1 — ADR 待立 | 老周建议立 ADR-019; 老高建议 ADR-018 §X 补; 老胡倾向老高方案 (5+1 暂不超 8 阈值, 避免 ADR 滥用); **待老板拍板** | W7 (ADR 草案) |
| D-05 | ci-grep-abi-lock CI 注释 "always-pass" 已过时 (文件现已存在, CI 真执行) | 老高 | P2 | 老高 W7 自清更新注释 | W7 |

---

## §2 W6 W3 并行责任人 input 集成

本 wave 32 与以下 input 并行 (老胡整合, 不复述原文):

| input | owner | 状态 | 关键结论 |
|---|---|---|---|
| Wave 30 架构 + 兼容性 review v1 | 老周 | 已落地 `docs/MEETINGS/2026-06-W3-laozhou-wave30-arch-review.md` | P0=0, P1=8 (架构层), Wave 30 有条件 ACCEPT push |
| Wave 30 代码质量 review v1 | 老高 | 已落地 `docs/MEETINGS/2026-06-W3-laogao-wave30-quality-review.md` | P0=0, P1=5 (质量层), P2=9, Wave 30 有条件 ACCEPT push |
| 老孙 libsodium FetchContent 方案 | 老孙 | W6 W3 本 wave — **待老孙在 W7 出修复 PR** (C-02/H-03 同步) | 派回 W7 W4 deadline |
| 老唐 friend class 正式 ack | 老唐 | W6 W3 本 wave — **待老唐 W7 书面 ack 或重构方案** | 派回 W7 W4 deadline |
| 小宋 -Wno- 清理 timeline | 小宋 | W6 W3 本 wave — **待小宋 ack + 执行 W7 EOW** | 派回 W7 EOW deadline |
| 老王 ToMarketIdArray + TODO 拆解 | 老王 | W6 W3 本 wave — **待老王 W7 删 dead code + 转 backlog** | 派回 W7 W4 deadline |
| ADR-005 file ownership lock + ADR-018/019 仲裁 | 老郭 | W6 W3 本 wave — **待老郭主笔 ADR patch, W7 末出草案** | 仲裁分歧见 §5 |

---

## §3 W7 W3 "冷静周" backlog v1

**原则:** 不派新 wave 30+ 功能任务; 只做 P1 retro 修复 + ADR 补丁 + KPI audit.

| # | 任务 | Owner | 截止 | 类型 | 关联 ID |
|---|---|---|---|---|---|
| 1 | libsodium FetchContent ExternalProject_Add 重做 (CI 可重现) | 老孙 | 7/3 (W7 W4) | P1 retro | C-02/H-03 |
| 2 | friend class AuditEmitterPool 正式 ack 或重构方案出文档 | 老唐 | 7/3 (W7 W4) | P1 retro | C-06/H-07 |
| 3 | first_mismatch_at dead variable 删除 | 老唐 | 7/3 (W7 W4) | P1 retro | D-02 |
| 4 | ToMarketIdArray dead code + 测试 no-op 删除 | 老王 | 7/3 (W7 W4) | P1 retro | D-01 |
| 5 | wal_writer 3 处 TODO W4 → Sprint-3 HC-07 backlog item | 老王 | 7/3 (W7 W4) | P2 转 backlog | D-03 |
| 6 | stcpp_polymarket_wss + stcpp_cli_* -Wno- 漏网 flags 清理 | 小宋 | 7/5 EOW (W7 W5) | P1 retro | H-10 |
| 7 | ci-grep-abi-lock 注释更新 + CI commit author warning-only check 落地 | 老高 | 7/3 (W7 W4) | P2 + #13 enforce | D-05 + GM-13 |
| 8 | ADR-005 §3.4 file ownership lock 子条款草案 + ADR-018 §X (或 ADR-019 视老板拍板) | 老郭 | 7/4 (W7 W5) | 流程 P1 | C-04 + C-08 + W30-CONFLICT |
| 9 | GM W6 W2 retro audit 书面 — 10 处 hotfix 意图说明 (每处一行), 发给对应 owner 过目 | GM 老雷 | 7/5 EOW (W7 W5) | 文化 P1 | GM-13 |

**未进 W7 冷静周 (W8+ 处理):**
- 小石 rigtorp_mpmc GIT_TAG → commit SHA (W8, D-04)
- 小卢 RAII env guard + CMakeLists 重复 define 清理 (W8, H-01/H-02)
- 老周 架构依赖图 v0.7 更新 (M1 前, P2-03)
- 小田 stcpp_data_parquet target 声明补齐 (W7, H-06-P2)

---

## §4 KPI 数据 (W6 W2 基线 → W7 目标)

| KPI | W6 W2 基线 | W7 目标 | 说明 |
|---|---|---|---|
| GM hotfix 率 | 62.5% (10/16 IC, Wave 28+29) | < 5% (期望 0) | 派单 prompt hard 约束 W6 W3 起执行 |
| 派单 prompt 含 build+ctest hard 约束 | 0% (W6 W2 前) → 100% (W6 W3 起) | 维持 100% | 老高 PR review v1.3 grep 已落地 |
| 文件抢占次数 | 1 次 (single_instance, Wave 30 W6 W3) | 0 次 | ADR-005 §3.4 补丁 + FOM 模板落地后执行 |
| GM 越权 commit src/include 次数 | 10+ 次 (W6 W2, GM 错 #13) | 0 次 | CI warning-only author check (老高 W7) |
| 工程债存量 (P1+P2 open) | 14 项 (本 wave 32 评估) | ≤ 5 项 (W7 末) | W7 冷静周 9 任务消化 9 项, 留 ≤ 5 项 W8+ |
| ctest pass rate | 440/440 (100%) | 440/440 (100%, W7 修复后不破) | 所有 W7 retro 修复必须 build+ctest 验证通过 |

---

## §5 6 件事三方意见整合 + 老胡建议 (待老板拍板)

老胡整合老周 + 老高两份 review 的拍板建议, 加老胡 PM 视角意见. **此处不替老板拍, 只整合 + 标注分歧.**

### 件 1: Wave 30 commit + push

- **老周建议:** 有条件 ACCEPT — 当前 440/440 PASS, 无 P0. P1 排进 W7 backlog 不阻 push.
- **老高建议:** 有条件 ACCEPT — 同上. 异议项: 老孙 signer_v52.cpp 注释与实现不符 (brew vs FetchContent), 应在 push 前由老孙同步修正注释.
- **老胡建议:** 三方共识 ACCEPT. 老高"注释先修"合理, 成本为零, 建议附带.
- **分歧:** 无分歧, 三方共识.
- **结论供老板拍:** ACCEPT push, 附条件: (a) 老孙修 signer_v52.cpp L14 注释; (b) 5 P1 项记入 W7 backlog.

### 件 2: W6 W2 GM 越权 retro audit 书面形式

- **老周建议:** 是, 精简化书面 retro (不开会). 老雷出"W6 W2 GM hotfix 意图说明", 每处一行, 3 工作日内发给对应 owner.
- **老高建议:** (质量 review 中未单独列, 从 Part 3 推断) 支持 CI author check 的防护思路, 默认同意书面 retro.
- **老胡建议:** 三方共识书面形式. W7 W5 (7/5) 前老雷出文档.
- **分歧:** 无分歧.

### 件 3: ADR-005 补"文件 ownership lock"子条款

- **老周建议:** 是, 必须加. 拟条款: 派 wave 前 GM 出 File Ownership Matrix (FOM), 列每个文件本 wave 唯一 owner; 高争用文件串行处理; 派单 prompt 显式声明"严禁修改清单外文件"; 扩大修改范围须先 GM 协调 owner.
- **老高建议:** 同 (Part 1 C-04 中明确建议派回小宋整理 tests/unit/CMakeLists.txt, 呼应 FOM 机制).
- **老胡建议:** 三方共识. 老郭主笔, W7 末 (7/4) 出草案. 老胡出 FOM 模板 (PM 工具, 不写 cpp).
- **分歧:** 无分歧.

### 件 4: ADR-019 vs ADR-018 §X 补 — build switch 规范

- **老周建议:** 立 ADR-019 (独立). 理由: 5+1 switch 已到预警线, CI 12 组合矩阵无上限, 需要明文规范 + 老郭评审.
- **老高建议:** 不立 ADR-019, 在 ADR-018 §X 补 build switch 命名规范条款. 理由: 当前 4 个 switch 在合理范围 (老高计数 4, 老周计数 5+1). 触发 ADR-019 的条件: 第 6 个独立 CMake option 出现时.
- **老胡建议:** ⚠️ 分歧存在. 老胡倾向老高方案 — 避免 ADR 滥用, 5+1 switch 暂不超 8 阈值, ADR-018 §X 成本更低, 在 Sprint-2 阶段性价比更高. 但最终仲裁由老郭出书面意见, 老板拍板.
- **分歧:** 老周 (立 ADR-019) vs 老高 (ADR-018 §X 补); 老胡倾向老高, 待老郭仲裁.

### 件 5: W7 W3 "冷静周"

- **老周建议:** 是. W7 暂停新功能 wave, 专注 P1 清理 + ADR-010 §4 deadline + ADR-005 补丁 + ADR-019 草案.
- **老高建议:** 是. W7 9 P1/P2 任务可局部修复, 不需全面 freeze.
- **老胡建议:** 三方共识. §3 冷静周 backlog v1 (9 任务) 已列出, 责任人 + 截止已分配.
- **分歧:** 无分歧.

### 件 6: CI commit author check (warning-only)

- **老周建议:** 支持 (Part 4.2 选项 B). 技术: `git log --author="weibo wang" --name-only HEAD~1..HEAD | grep -qE '^(src|include)/'` → `::warning::` 不 block.
- **老高建议:** 同 (Part 5 建议 5 明确, 老高自己 W7 落地).
- **老胡建议:** 三方共识. 派老高 W7 W4 (7/3) 落地.
- **分歧:** 无分歧.

---

## §6 上报老板 6 件事 (整合版)

以下供 GM 老雷拍板. 老胡不替拍.

1. **Wave 30 commit + push:** 三方共识 ACCEPT, 附 (a) 老孙注释先修; (b) 5 P1 进 W7 backlog. 建议老板批准.
2. **W6 W2 GM 越权 retro audit 书面形式:** 三方共识. 建议老板确认由老雷本人出"W6 W2 GM hotfix 意图说明"文档, W7 W5 (7/5) 前发给对应 owner.
3. **ADR-005 补 file ownership lock 子条款:** 三方共识. 建议老板批准老郭主笔, W7 末出草案, 纳入下次派 wave 前必须执行.
4. **ADR-019 vs ADR-018 §X:** ⚠️ 老周建议立 ADR-019; 老高建议 ADR-018 §X 补; 老胡倾向老高方案. 请老板拍板 — 建议指定老郭 W7 出仲裁意见后老板终裁.
5. **W7 W3 冷静周:** 三方共识立. 9 任务已在 §3 排期, 建议老板批准 W7 不派新功能 wave.
6. **CI commit author check warning-only:** 三方共识. 建议老板批准, 老高 W7 W4 落地.

---

## §7 W7 EOW 复评 KPI (验收标准)

W7 W5 (7/5) 老胡出复评周报, 按以下 KPI 验收:

| KPI | 目标 | 验证方式 |
|---|---|---|
| P1 修复率 (5 项: C-02, C-06/D-02, H-10, D-01) | 100% (5/5 修复 + PR merge) | git log + ctest 440/440 |
| 工程债存量 (P1+P2 open) | ≤ 5 项 | 老胡 W7 EOW 盘点 |
| GM hotfix 率 (W7 派单验收) | < 5% | W7 wave 整合结果统计 |
| 文件抢占次数 | 0 次 | git log 按文件扫描 |
| GM 越权 src/include commit | 0 次 | CI author check warning log |
| ctest pass rate | 440/440 (100%) | CI 记录 |
| W7 冷静周任务完成率 | 100% (9/9) | §3 checklist |
| ADR-005 §3.4 草案 + ADR-018 §X (或 ADR-019) 出草案 | 是 | 老郭文档落地 |

---

## §8 验证 hard 约束 (GM 错 #11)

**老胡 E 主管, W6 cpp = 0:**
- 本文件: doc only, 0 .cpp / .hpp / .cmake 改动
- build+ctest 验证: 不适用 (老胡不写 cpp, 本次 diff 0 cpp change)
- git diff stat (本次): 1 file changed (新建 docs/MEETINGS/2026-06-W3-laohu-wave32-resolution.md), 0 cpp insertions

**ctest 基线 (来自老周 + 老高 review 独立验证):**
```
ctest -j1: 440/440 PASS, 100% (unit=422 / integration=14 / sim=4)
build:     ninja: no work to do (0 代码变更, review only)
```

---

## 附: 引用文件路径

- 老周 review: `docs/MEETINGS/2026-06-W3-laozhou-wave30-arch-review.md`
- 老高 review: `docs/MEETINGS/2026-06-W3-laogao-wave30-quality-review.md`
- W6 W2 周报 (基线): `docs/SPRINTS/sprint-02-w6-w2-progress.md`
- ADR-018 lib 选型 (build switch 补丁目标): `docs/ADR/2026-06-W3-adr-018-lib-selection.md`
- ADR-005 (文件 ownership lock 待补): 现有 ADR-005 (见 docs/ADR/)
- GM 错 log: `docs/INCIDENTS/gm-self-mistakes-log.md`

---

**W6 W3 解决方案集成 v1 完成汇报:**

13 问题全景 + W7 冷静周 9 任务 + 三方共识 5 / 分歧 1 (ADR-019 vs ADR-018 §X) / 上报老板 6 件事 + KPI 基线 → 目标 (W7 EOW 验收) + diff 0 cpp

老胡签字: E 主管 + PM, W6 W3 cpp = 0 严格守, 不替老板拍.
上报: 老郭 first review → GM 老雷整合 → 老板 ack.
