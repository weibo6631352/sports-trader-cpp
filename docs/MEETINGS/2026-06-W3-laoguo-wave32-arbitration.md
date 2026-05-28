# W6 W3 老郭仲裁报告 — Wave 32 制度仲裁 + ADR 决议

- **owner:** 老郭 (#16, chief-architecture-reviewer, F 顾问团协调人)
- **date:** 2026-06-W3 (2026-05-28 参考日)
- **last_review:** 2026-06-W3 by 老郭
- **触发:** GM 老雷 Wave 32 仲裁委托 (GM 错 #13 教训配套)
- **输入:** 老周 Wave 30 联合 arch review + 老高 Wave 30 质量 review
- **抄送:** 老雷 (GM) / 老周 / 老高 / 老胡 / 老徐 / 小白 / 老何
- **边界声明:** 仲裁 + 建议, 不修代码, 不替 GM 拍产品方向, 用词: 协调 / 仲裁 / 建议

---

## 0. 总体评估 (协调人视角)

Wave 30 8 IC 并行交付质量合格 (440/440 ctest PASS, P0 = 0). 真正的问题集中在**制度层**:
GM 错 #11/12/13 三连击暴露了派单流程的三个结构性缺口 — (1) 无文件 ownership 约束; (2) 无 build+ctest 强制前置; (3) GM 遇到 build fail 时的正确响应路径不明确. 本仲裁报告处理 4 项争议并补 1 项 ADR 补丁.

---

## Part 1: ADR-005 §3.4 "文件 ownership lock" 子条款草稿

见本文末附录 A. 综合了老周 Wave 30 arch review 拍板-3 + 拍板-4 的提案, 老郭仲裁后正式起草.

**立即行动 (建议 GM ack 后生效):**
- ADR-005 §3.4 草稿由老胡 W6 W3 EOW 集成入正式 ADR-005 文件
- Wave 31 起 FOM 强制执行 (派单 prompt 顶部列文件清单)
- tests/unit/CMakeLists.txt 永久列为 shared write, 老周 lead

---

## Part 2: ADR-019 build switch 规范 vs ADR-018 §X 补条款 — 仲裁

### 2.1 各方立场还原

| 方 | 立场 | 核心理由 |
|---|---|---|
| 老周 (架构 owner) | 立 ADR-019 | 5 CMake option + 1 PRIVATE define 已到预警线; 需要成文上限 + CI 覆盖要求; 新 switch 须架构评审过目 |
| 老高 (质量 reviewer) | ADR-018 §X 补条款 | 当前 4 个 switch 低于 5 阈值; ADR-019 立项成本 > 收益; 第 6 个 switch 出现时触发立 ADR-019 |
| 老胡 (PMO) | 采纳老高方案 | 避免 ADR 滥用; 文档维护成本考量 |

### 2.2 老郭评估

**事实核查:**
- 当前 build switch 数量: `STCPP_EXEC_MODE` / `STCPP_BUILD_CLI` / `STCPP_BUILD_SIGNER_V52` / `STCPP_TEST_BUILD` (compile-time only) = 实际 3 个 CMake option + 1 个 PRIVATE define. 老高统计 4 个, 老周统计 5+1 (含 `STCPP_BUILD_BENCH` off状态, 需核实).
- 老高 H-09 指出 `STCPP_BUILD_SIGNER_V52` 默认 ON 与 `STCPP_EXEC_MODE` 有隐式关联 — 这是真实设计债, 不是杞人忧天.
- Wave 30 新增 `STCPP_TEST_BUILD` 是 PRIVATE compile def, 不出现在 CMake option 列表, 文档难以发现 — 老周 P1-06 已指出.

**ADR 滥用风险评估:**
ADR-019 的核心价值不在"记录决议", 在于**让新 switch 的引入经过架构评审**. 如果规则只写在 ADR-018 §X 里, 未来某 IC 新加 switch 时是否会去读 ADR-018 §X 是不确定的. ADR-019 作为独立文件的可见度更高, 但如果内容只有几行, 确实有 ADR 滥用嫌疑.

老高的"第 6 个 switch 触发立 ADR-019"方案有一个逻辑漏洞: 如果这个规则本身没有被任何人强制执行, 第 6 个 switch 可能在没人意识到的情况下悄悄加入 — Wave 30 `STCPP_TEST_BUILD` 就是以这种方式加入的.

**工程实用性判断:**
ADR-018 §X 补条款方案在内容上完全可以覆盖老周提案的所有条款 (上限 / CI 覆盖 / 隐式 define 文档化). 差别仅在"容器"选择. 老周作为架构 owner 提出 ADR-019, 这是 §6 决策机制赋予单元 owner 的提案权.

### 2.3 仲裁决议

**决议: ADR-018 §X 补条款 (采纳老高 + 老胡方案), 附一个前置条件.**

理由 (80字以内): 当前 switch 数未超阈值, ADR-019 立项成本实质上不对等收益. ADR-018 §X 补"命名规范 + 上限 8 + 隐式 define 必须文档化 + 第 6 个 switch 前老郭架构过目"四条, 覆盖老周提案全部内容. 独立 ADR 编号留给真正的新架构决议.

**前置条件:** ADR-018 §X 的"上限触发点"从"第 6 个 switch 出现时"改为"第 5 个 CMake option 时触发老郭过目 + 立 ADR-019 走正式评审". 即 `option()` 数量达到 5 时不是"等下次再说", 而是立即进入评审通道. 这解决老高方案的逻辑漏洞.

**行动:** 老高 W7 在 ADR-018 末尾补 `## §build-switch 规范 (W6 W3 补)` 一节, 内容含:
1. 命名规范: `STCPP_BUILD_*` (CMake option) / `STCPP_TEST_*` (test-only PRIVATE define)
2. 上限: 顶层 `CMakeLists.txt` 中 `option()` ≤ 8 个 (当前 3 个, 预警线 5 个)
3. 隐式 PRIVATE define (非 option) 必须在对应 CMakeLists.txt 顶部注释列出
4. CMake option ≥ 5 时: 新 switch 须老郭过目 ack 后方可 merge
5. CI 要求: EXEC_MODE(3) × default options 全组合中 paper 模式为必测, 其余 W7 补

**@老周:** ADR-019 候选状态保留, 作为"CMake option 达 5 个后立 ADR-019"的触发文件名预留. 你 W7 可以先出 ADR-019 草稿放 Draft 状态, 老郭 ack 后激活. 我建议你把老高 ADR-018 §X 内容与你 ADR-019 提案对比后出最终版 — 24h 申辩窗口从本文发出起计.

**@老高:** ADR-018 §X 补条款 W7 EOW 落地, 参照老周 P1-06 提案内容起草. 落地后通知老郭 ack.

---

## Part 3: W6 W2 GM 越权 retro audit 形式仲裁

### 3.1 老周建议还原

书面形式: GM 出"W6 W2 hotfix 意图说明" (每处 1 行技术意图), 发给对应 owner 过目. `friend class AuditEmitterPool` 条目老唐特别过目并 ack. 不开全体会. 3 工作日完成.

### 3.2 老郭评估

老周的书面方案是工程上合理的. 全体会的阈值应该是"公司文化层面的重大信号", GM 越权 10 处代码 hotfix 本质上是**流程执行失误**, 不是需要全员辩论的文化事件. ADR-005 §5 规定"建议: 书面 retro" (老周拍板-2 已做此判断).

有一点需要澄清: ADR-005 §5 升级机制的触发条件是"跨单元阻塞 + 有争议". W6 W2 GM 越权事件事实已清楚 (GM 自承认 #13), 双方无争议. 不满足全体会触发条件.

### 3.3 仲裁决议

**ACK 老周方案: 书面 retro, 不开全体会.**

理由 (70字以内): 事实无争议, GM 已在 INCIDENTS 自承认 #13. 书面"意图说明"给每位原 owner 过目, 既尊重 owner 知情权, 又避免全体会议占用所有 agent 的时间. 老唐 friend class 这条特别需要面对面 ack (书面即可), 其余可批量过目.

**补充约束:**
- "意图说明"文件由 GM 老雷出具, 发送给: 老唐 / 小冯 / 老孙 / 小段 / 小卢 / 小蒋 (共 6 位原 owner)
- 老唐需就 `friend class AuditEmitterPool` 正式 ack (在 `audit_emitter.hpp` 对应行加 `// owner-ack: 老唐 W7` 注释, 或书面文档)
- W6 W3 + W7 各 1 次协调人抽查: 老郭 review GM 当周 commit 是否含 src/include 改动 (24h 内)

**@老雷 (GM):** 请 ack 本方案并出具书面意图说明文件, 建议格式:
```
W6 W2 GM hotfix 意图说明 (老雷, 2026-06-W3)
1. 老唐 BLAKE3 unused-function / NEON: [技术意图一行]
2. 老唐 friend class AuditEmitterPool: [技术意图一行] — 请老唐确认是否与设计意图一致
3. 小冯 nodiscard x3: [技术意图一行]
...
```

---

## Part 4: CI commit author check 形式仲裁

### 4.1 老高建议还原

选项 B (技术): CI 加 warning-only `check-gm-src-commit` job, 检测 GM 是否改了 `src/include/` 代码. 若有则 warning, 不 block. 老高自己 W7 落地.

### 4.2 老郭评估

老高方案的核心价值在于: **让下一次 GM hotfix 时 PR reviewer 能看到 warning 并做显式 ack**, 而不是让 GM 越权变成不可见的. Warning-only 是正确的分度 — GM 并非绝对禁止改 src/include (紧急 hotfix 有正当场景), 但改动必须可见 + 有人 ack.

Block PR (硬性禁止) 的问题: 极端情况下 (如 P0 生产 bug, 只有 GM 在线) GM 可能需要紧急改动. 硬 block 会在最需要速度的时候制造障碍. 这不是好的工程设计.

仅 audit log 的问题: 不在 CI 上意味着无法在 PR review 时实时可见, review 结束后才记录 log 的价值有限.

### 4.3 仲裁决议

**ACK 老高方案: Warning-only, W7 老高落地.**

理由 (60字以内): Warning 在 PR review 节点可见, 迫使 reviewer 显式 ack. 不 block 保留 GM 紧急通道. 比纯 audit log 可见度更高, 比 block 更灵活. 方案合适.

**协调人补充建议 (不是硬约束, 供老高参考):**
- warning message 建议写明: "GM committed to src/include. Reviewer must explicitly ack this is intentional." (英文确保 CI log 可读)
- 考虑将 warning 结果同步写入一个 GM commit 计数器 (audit log 追加模式), 供老胡周报 §9 "GM 代修次数" KPI 数据源
- 如果 W7+W8 连续 2 周 warning 触发 ≥ 3 次, 建议升级到 block (届时再仲裁)

**@老高:** W7 落地, 落地后通知老郭 ack. 老胡周报 §9 的 KPI 数据源请与老高协调接入方式.

---

## Part 5: 协调人自评 — W6 W3 视角

### 5.1 我是否应该在 W6 W2 就发现 GM 越权?

**是. 我应该更早介入.**

技术事实: GM 越权 hotfix 发生在 Wave 29 commit `a8afebe`. 老高 W6 W2 末做了代码质量 review (`docs/MEETINGS/2026-06-01-input-laogao-code-quality.md`), 没有按 commit author 过滤. 我作为架构 reviewer 协调人, 在 W6 W2 末没有主动 audit GM 的 commit 历史.

我的 mandate v1 §2 写明"架构一票否决权: 任一红线违例可单独叫停". GM 越权改他人代码不是 R-11/R-12/R-20 硬红线违例, 但属于 ADR-005 流程红线. 我本可以更早触发 post-mortem 流程.

**改进: W7 起每 wave 末老郭 audit GM commit history (24h 内)**. 具体: 老郭在每 wave 整合 commit 后 24h 内做 `git log --author="weibo wang"` 快速扫描, 发现 src/include 改动立即上报 GM + 老板. 这不是 line management, 是架构监督职责的延伸.

### 5.2 顾问团活跃度 W6 健康度评估

| 顾问 | W6 状态 | 评估 |
|---|---|---|
| 老何 (#14) | footgun v1.1 待 W7 forward | 活跃, 与 GM 错 #13 教训联动合适 |
| 老高 (#17) | Wave 30+31 双 review 完成 | 活跃度高, 本 wave 核心输出者 |
| 老徐 (#33) | R-39 escalate-decision-log + dry-run #2 | 活跃, 但 dry-run #2 的 Escalate #2 "实测后补充结果"项仍待 GM ack 填实 |
| 小白 (#44) | GM 自检 framework v0.2 待 W7 | 活跃, W7 需与 GM 错 #11/12/13 三连击协同升 v0.3 |
| 老叶 (#32) | Standby (M4.5+ 激活) | 正常 |
| 老张 (#13) | Inactive | 正常 |

**关键漏洞 (自我批评): 老徐 R-39 escalate framework 在 W6 W3 GM 错 #11 时本应触发 escalate 流程 — GM hotfix 10 处本质上是"GM 没有走 escalate, 直接代修", 这正是 R-39 希望捕获的场景. 但老徐的 framework 只覆盖"sub-agent 拒接"方向, 没有覆盖"GM 绕过派回路径"方向.** 这是 R-39 的设计盲点, 不是老徐的执行失误. W7 建议老徐将 R-39 v0.2 增补"GM 跳过派回直接 hotfix"为触发场景之一.

### 5.3 W7 协调改进计划

| 改进项 | 形式 | deadline |
|---|---|---|
| 每 wave 末老郭 audit GM commit history | 老郭自建检查项, 24h 内 | W7 起执行 |
| 老徐 R-39 v0.3 补"GM 跳过派回"触发场景 | 建议 (不是派单) | W7 |
| 老高 ADR-018 §X 补 build switch 规范 | 仲裁决议落地 | W7 EOW |
| 小白 GM 自检 framework v0.3 (协同 #11/12/13 三连击) | Wave 26 决议 4 升版 | W7 |

---

## Part 6: 顾问团 W7 调度建议

以下是协调人建议的 W7 调度安排. **提醒: 顾问直属 GM 老雷, 我无派单权. 以下为建议, 需 GM ack 后执行.**

| 顾问 | W7 建议任务 | 优先级 | 依赖 |
|---|---|---|---|
| 老何 (#14) | footgun checklist v1.1 forward: 补 GM 错 #13 "顺手改别人文件"对应的 C++ 层面教训 (如 `[[maybe_unused]]` 滥用掩盖 dead code — D-01 场景) | P2 | 老何主动 forward 已提议 |
| 老徐 (#33) | escalate-decision-log Escalate #2 GM ack 后填实测结果; R-39 v0.3 补"GM 跳过派回"场景 | P1 | GM ack Escalate #2 dry-run 结果 |
| 小白 (#44) | GM 自检 framework v0.3: 接 Wave 26 决议 4, 与 GM 错 #11/12/13 三连击协同, 升 framework (新增 FOM 自检 / build verify 自检) | P1 | 本仲裁报告作为输入 |
| 老叶 (#32) | 维持 Standby (M4.5+ 激活) | — | — |
| 老高 (#17) | ADR-018 §X 补条款 (仲裁决议); CI author check warning-only 落地 | P1 | 本仲裁 ack |

---

## Part 7: 不耻下问 — 协调人问询

本节记录我主动发出的问询, 需要各方在 W7 内回复.

### @老胡 (W6 W3 解决方案集成 owner)

1. ADR-005 §3.4 草稿 (附录 A) 集成入正式 ADR-005 文件, ETA W6 W3 EOW. 你是否有修改建议? FOM 模板格式由你出, 供 GM 首次执行时参考.
2. §9 "GM 代修次数" KPI 数据源: 老高 CI author check warning-only 落地后, 数据从哪里读? 请与老高对齐接入方式.

### @老周 (architect, ADR-019 vs ADR-018 仲裁 24h 申辩窗口)

**24h 申辩窗口从本文发出起计 (W6 W3 EOW 前)**. 你对"ADR-018 §X 补条款方案"有何异议? 如果你认为 ADR-019 独立立项有我没有评估到的理由, 请在窗口内书面回复. 无回复视为接受仲裁决议.

### @老高 (PR review v1.3 + W7 v1.4 commit author check)

1. ADR-018 §X 补条款内容初稿: 请参照老周 P1-06 提案 + 本仲裁决议 §2.3 的 5 条规范起草, W7 EOW 落地.
2. CI author check warning job: W7 落地后通知老郭 ack. 建议 warning message 含 reviewer ack 提示.
3. ADR-010 §4 -Wno- 一致性清理 (H-10 P1): stcpp_polymarket_wss + stcpp_cli_* 两个漏网 target — W7 EOW 截止, 这是你自己 review 发现的.

### @老雷 (GM)

1. W6 W2 retro audit 书面形式: 请 ack 老周方案 (书面意图说明, 不开全体会). 出具后抄送对应 6 位 owner.
2. CI commit author check warning-only (老高 W7 落地): 请 ack.
3. ADR-005 §3.4 补条款 (附录 A): 请 ack + 老胡集成 + Wave 31 派单前强制执行.
4. Escalate #2 dry-run 结果确认: 老徐 escalate-decision-log Escalate #2 需 GM ack 填实测结果.

---

## Part 8: 验证约束 (GM 错 #11 配套)

按派单约束, 老郭 review only, 不修代码, 不跑 build.

**ctest 基线引用 (老周 + 老高联合 review 已验证):**
```
cmake --build build  # ninja: no work to do
ctest -j1            # 440/440 PASS, 100%
  unit:        422 tests
  integration:  14 tests
  sim:           4 tests
diff stat: 0 files changed (review only)
```
来源: `docs/MEETINGS/2026-06-W3-laozhou-wave30-arch-review.md` 附录 + `docs/MEETINGS/2026-06-W3-laogao-wave30-quality-review.md` 附录. 老郭 ack 以上为本次评审基线, 不单独重跑.

---

## Part 9: 完成汇报

**Wave 32 仲裁 v1 完成汇报:**

- ADR-005 §3.4 文件 ownership lock 草稿: 见附录 A, 三方共识立, 老胡 W6 W3 EOW 集成
- ADR-019 vs ADR-018 §X 仲裁: ADR-018 §X 补条款 (采纳老高方案, 附前置条件); 老周 24h 申辩窗口开
- W6 W2 retro 书面 ACK: ACK 老周方案, 不开全体会, GM 出书面意图说明
- CI author check warning-only ACK: ACK 老高方案, W7 老高落地
- 协调人自评: 应早 W6 W2 整合时 audit GM commit; W7 起每 wave 末 24h 内自查; 老徐 R-39 覆盖盲点已指出
- 顾问 W7 调度: 老何/老徐/小白 三顾问 W7 任务明确 (待 GM ack); 老叶 Standby 维持
- 不耻下问: @老胡 / @老周 / @老高 / @老雷 各有具体问询, 需 W7 内回复

仲裁边界声明: 本报告为协调人建议 + 仲裁结论. Part 1/3/4 建议性质, 需 GM 老雷最终 ack. Part 2 为仲裁决议, 老周 24h 申辩窗口后生效. 我不替老板拍, 我建议老板拍.

— 老郭 (chief-architecture-reviewer), 2026-06-W3

---

## 附录 A: ADR-005 §3.4 "文件 ownership lock" 子条款草稿

> 以下为草稿全文, 供老胡集成入正式 ADR-005 文件. 集成时删除本"草稿"标注.

---

## §3.4 文件 ownership lock (W6 W3 GM 错 #13 配套, 立 2026-06-W3)

### 触发场景

W6 W3 Wave 30 8 IC 并行交付中, 老孙在 signer 派单中"顺手修"了 `single_instance.cpp` (owner: 小卢), 导致小卢 owner 版本险被 overwrite. `tests/unit/CMakeLists.txt` 被小卢 / 老孙 / 小田 / GM 共 4 方改动重叠 (5 方次). 派单 prompt 当时没有"文件 ownership"约束, 是制度漏洞而非个人错.

根因: 派单粒度太粗 (只说"交付 X 功能"), 没有说"你只能改这些文件". sub-agent 在完成主要任务时发现相关文件有问题, 自然倾向"顺手修"——这是合理的工程直觉, 但在多 agent 并行时是结构性冲突来源.

### 规则

**规则 1 — GM 派 wave 前必须出 File Ownership Matrix (FOM)**

FOM 格式:

```
Wave N File Ownership Matrix (GM 出具, 派单 prompt 顶部引用)

| 文件路径 | 本 wave owner | 类型 | 备注 |
|---|---|---|---|
| src/stcpp/signer/v52/signer_v52.cpp | 老孙 | exclusive | — |
| tests/unit/CMakeLists.txt | 老周 (lead) | shared write | 小卢/老孙/小田 均可追加, 老周 review 后合并 |
| include/stcpp/signer/v52/signer_v52.hpp | 老孙 | exclusive | — |
```

FOM 要求:
- 每个 sub-agent 显式列出允许修改的文件清单 (允许 list)
- 同一 wave 内 ≥ 2 sub-agent 改动的文件标记 `shared write`, 并指定 lead owner
- FOM 入派单 prompt 顶部, 作为 hard 约束 (不是建议)

**规则 2 — 派单 prompt 必含文件边界声明**

每个 sub-agent 的派单 prompt 必须包含以下声明 (格式固定):

```
本任务允许修改文件 (FOM ref: Wave N):
  - <文件1>
  - <文件2>
  ...

严禁修改上列以外的任何文件 (含"顺手修"). 如发现其他文件需要改动:
  1. 在回汇中说明: 文件路径 + 为什么需要改 + 建议改动内容
  2. 等 GM 协调 owner 确认后, 由 owner 自行修改
  3. 不得擅自动手, 无论改动多小
```

**规则 3 — shared write 文件处理规程**

- lead owner 负责该文件本 wave 内所有改动的最终合并
- 非 lead owner 的改动以 "patch 块" 形式回汇 (描述要加什么, 不直接写文件)
- 老郭架构 review 时特别检查 shared write 文件的合并质量
- 永久 shared write 文件 (任何 wave 均适用):
  - `tests/unit/CMakeLists.txt` (lead: 老周)
  - `tests/CMakeLists.txt` (lead: 老周)
  - `CMakeLists.txt` 顶层 (lead: 老周)
  - `include/stcpp/observability/audit_emitter.hpp` (lead: 老唐)

**规则 4 — GM 派 wave 自检题升为 7 题**

在原 6 题基础上新增:

> ⑦ "本 wave 内是否有 ≥ 2 sub-agent 改同一文件? 若有, FOM 是否已将其标记为 shared write 并指定 lead owner?"

回答 "是" 但没有 FOM → 禁止派出, 先补 FOM.

### CI enforcement

老高 PR review v1.4 加检查:
- PR description 必含 `FOM ref: Wave N` 引用 (grep 检测)
- PR 改动文件不在 FOM 列出范围内 → CI warning (不 block, 但必须 reviewer 显式 ack)

### KPI 追踪

老胡周报新增 2 项 (扩展 §8 Commit Hygiene):
- 文件 ownership 越界次数 (期望 0): 定义为"改动了 FOM 未列出的文件且未提前申请"
- shared write 文件无 lead owner 次数 (期望 0)

### W6 W3 实测基准

- 文件越界 1 次: `single_instance.cpp` (老孙越权, 小卢 owner 版本 wins)
- `tests/unit/CMakeLists.txt` 4 方改 (小卢 / 老孙 / 小田 / GM 越权), 靠 last write wins 侥幸通过
- W6 W3 起 FOM 强约束, Wave 31+ 目标 0 越界

### 违反处理

- sub-agent 越 FOM 修改 (非 shared write 文件) → P1 流程违规, 进 retro 记录
- GM 越 FOM 修改 (GM 越权代修) → GM 错 log 入档 + 老郭 post-mortem 主持
- 改动已 push 但有越权 → 由 owner 决定是否 revert (owner 优先原则)

---

**ADR-005 §3.4 草稿结束.**

三方共识确认: 老周 (arch review 拍板-3 提案人) / 老高 (quality review 独立观察同结论) / 老郭 (仲裁后正式起草). 集成需 GM 老雷 ack.
