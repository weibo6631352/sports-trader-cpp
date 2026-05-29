# ADR-039: 开发工作流 SOP (制度 SSOT) + 自动化

- **ID:** ADR-039
- **Date:** 2026-05-29
- **Owner:** 老雷 (GM); 老吴 (CI/自动化) + 老高 (质量门) + 老郭 (架构门) + 小米 (doc) 维护
- **Status:** ✅ **Validated / 强制规范** — 2026-05-29 首批 8 路并发 batch 实跑验证通过 (集成 PR #38, 双评审 approve, 见 §12)。**任何人(IC / dispatcher / GM)开发必须按 §9 办事,偏离 = 流程事故入 INCIDENTS。**
- **触发 (老板 verbatim 2026-05-29):** "完善一下制度吧, 定规范, 能自动化的尽量自动化, 大家工作流确实很随意。" + "我们不会自动评审吗" + "pr 完成是不是需要关闭?" + "把状态弄对, 以后不要再遗漏了"
- **背景:** 制度文档不缺 (PR 模板 v1.7 + 一堆 ADR), 缺的是**统一入口 + 自动强制**。本 ADR 是工作流**唯一 SSOT 索引**, 并固化本会话暴露的随意点。

---

## 1. 本会话暴露的"随意"(反面教材, 禁止重演)

| 随意行为 | 谁 | 正确做法 |
|---|---|---|
| sub-agent "不 push, 主工作区改" | GM 早期派单 | **一切代码改动必经 worktree → PR**, 无"主工作区直接改" |
| `git push --no-verify` 绕过 gate | 老高 (Wave3) | **禁止 --no-verify**(除非 documented 紧急 + 逐项说明每个被跳检查为何安全) |
| 只跑自己新增测试就报"完成" | 老沈 (Wave3) | 报完成必跑**全量 ctest**, 贴通过数(见 [wave3 lessons](../INCIDENTS/2026-05-29-wave3-precommit-lessons.md)) |
| PR 堆积无人评审 / merge | GM | 每个 PR 必达终态(merged/closed), 不 dangling(§3) |
| #26 被遗忘 | GM | PR 周度清扫(§6) |
| 评审全靠手动临时派 | GM | 评审自动化(§4)+ SOP 化(§5) |

---

## 2. PR 生命周期 (强制, 无例外)

```
worktree(ADR-029, isolation=worktree)
  → 分支开发
    → push (pre-push gate 自动跑: build + 全量 ctest + clang-format + grep, ADR-032)
      → gh pr create (用 PR 模板, Definition of Done 勾全)
        → 自动评审 (claude-review.yml, §4) + reviewer 评审 (§5)
          → 评审通过 → merge (--squash --delete-branch) ← GitHub 自动关闭 PR
            → 终态: MERGED (或 CLOSED 若废弃)
```

**铁律:**
- ❌ 不直接 commit/push main(branch protection enforce, §4)
- ❌ 不 `--no-verify` 绕 gate
- ❌ 不"主工作区改不 push"
- ✅ "完成 PR" = **merge**(自动关闭), 不是手动 close; 手动 close 仅用于**废弃**该改动

## 3. 无 dangling PR (老板"把状态弄对")

- 每个 PR 必须走到终态: **MERGED**(完成)或 **CLOSED**(废弃), 不允许长期 OPEN 无动作
- 评审打回的 PR: owner 限时整改 → 重新评审 → merge; 或明确废弃 → close
- **周度 PR 清扫**: 老胡周报 §6 列所有 OPEN PR + 停留天数 + 阻塞点 + owner; > 7 天无动作的升级

## 4. 自动化清单 (能自动的全自动)

| 自动化 | 文件/机制 | 作用 |
|---|---|---|
| **pre-push gate** | `.git/hooks/pre-push` (ADR-032) | push 前自动 build+全量 ctest+clang-format+5 grep checks, 不过不让 push |
| **PR CI** | `.github/workflows/pr.yml` / `pr-linux.yml` | PR 自动 build+test(Linux) |
| **性能回归门** | `perf-regression.yml` | 热路径 p99 回归 > 10% 拦 PR; 观测 API 开关双跑 delta ≤ 2%(ADR-038) |
| **🆕 自动代码评审** | `claude-review.yml` (本 ADR 新增) | 每个 PR 自动 Claude 评审(Sonnet 第一道), 贴 review comment, [BLOCKING] 标重大问题 |
| **🆕 CODEOWNERS** | `.github/CODEOWNERS` (本 ADR 新增) | 红线路径(risk/signer)自动请求 owner 评审 |
| **nightly / 通知** | `nightly.yml` / `notify-on-main-failure.yml` | 夜跑 + main 失败告警 |
| **自动清理** | `cleanup-stale-branches.yml` / `cleanup-old-runs.yml` | 陈旧分支/run 自动清 |
| **green 自动关 fail PR** | `close-ci-fail-on-green.yml` | CI 转绿自动关 fail 标记 |
| **周报** | `weekly-report-cron.yml` | 老胡周报(含 §6 PR 清扫 + idle + Opus 监控) |
| **branch protection** | GitHub repo 设置(§7 待老板开) | main 禁直接 push, 必 PR + 必过 CI |

**ANTHROPIC_API_KEY**: `claude-review.yml` 需此 repo secret 才生效, 老板在 Settings → Secrets and variables → Actions 添加(未配时不阻塞, 评审降级到 §5 agent 评审)。

## 5. 评审 SOP (两道, 自动 + agent)

1. **第一道 — 自动 (claude-review.yml)**: 每个 PR 自动 Claude(Sonnet)评审, 覆盖正确性/性能/安全/红线/提交质量, 贴 comment。便宜(~$0.01-0.05/PR)、即时、零遗漏。
2. **第二道 — reviewer agent (代码 PR 必走)**:
   - **质量门**: 老高(code-quality-reviewer)— 命名/API/红线/提交质量
   - **架构/ABI 门**: 老郭(chief-architecture-reviewer)— ABI 变更/跨层/不可逆 schema
   - **红线路径加签**: risk/ → 老韩(RM 主权); signer/crypto → 老沈+老孙; 数据 schema → 小余
   - 文档型 PR: 第一道 + GM/小米 即可, 免第二道
3. **通过即 merge**, GM/主管不再临时起意才评审 —— 评审是流水线固定环节。

## 6. Definition of Done (报"完成"前, PR 模板强制勾)

见 `.github/pull_request_template.md`(老高 v1.7)+ [wave3 lessons 8 条清单](../INCIDENTS/2026-05-29-wave3-precommit-lessons.md)。核心三条:
1. 跑**全量 ctest**(不只自己的), 贴通过数
2. 改契约 → grep 所有下游消费方一起改
3. 本地 pre-push gate 全过, 未 --no-verify

## 7. 待老板/admin 一次性开启 (GitHub 设置)

- [ ] 加 `ANTHROPIC_API_KEY` secret(激活 claude-review.yml 自动评审)
- [ ] main branch protection: require PR + require status checks(pr-linux / claude-review)+ 禁 force push + 禁直接 push
- [ ] 开 auto-merge(PR 满足条件自动合, `gh pr merge --auto`)

> GM 已尝试用 gh 设 branch protection; 若权限不足见 §8 输出, 需老板在 repo Settings 手动开。

## 8. 落地

- [x] ADR-039 立(本文件, 工作流 SSOT 索引)
- [x] `.github/workflows/claude-review.yml` 自动评审
- [x] `.github/CODEOWNERS` 红线路径自动请评审
- [ ] 老板开 §7 三项 GitHub 设置
- [ ] 老胡周报加 §6 PR 周度清扫段
- [ ] 老吴 评估本地 pre-commit hook(把 gate 再前移一步, 缩短反馈环)

## 9. 采纳工作流 — 本地集成 + 评审前置 + 直推 main (老板 2026-05-29 定)

> 推翻早期"每 worktree 各自 push+PR"模型(它导致 rebase 竞速 + #31/#32/#36 污染)。老板原话: "worktree 从本地 main 为基准; 做完只本地提交; 所有 worktree 做完后清理、逐个合并解冲突; 完成后推送远端。" + "直推 main, 评审前置"。

### 9.0 第一原则: origin/main 是唯一真相
本地 `main` 是会过期的缓存。**开批前必须 `git fetch` + 本地 main 快进到 `origin/main`(快进失败=分叉, 先解决)**, 才能作 worktree base。

### 9.1 流程(本地集成优先, 批量 fan-in)

| # | 环节 | 谁 | 动作 |
|---|---|---|---|
| 0 | **开批前同步** | dispatcher | `git fetch origin && git checkout main && git pull --ff-only`; 校验 `HEAD==origin/main`(唯一同步点) |
| 1 | **开 worktree** | dispatcher | 从【本地 main】建一批: `git worktree add -b feat/X <path> main` |
| 2 | **并行开发** | IC(各 worktree) | 开发 → `add` → **本地 `commit`**。**不 push、不开 PR、绝不 git 手术**(reset/rebase/force/切分支) |
| 3 | **评审前置** | reviewer agent | 每个 worktree 产出由 老高(质量)/老郭(ABI)/红线 owner 审过 + 全量 ctest 绿 → 该 worktree 才算 done |
| 4a | **逐个集成** | dispatcher | 全部 done 后, 把各 worktree 分支【逐个 merge 进本地 main, 逐个解冲突】(一人连贯解, e.g. CMakeLists append 取并集) |
| 4b | **集成验证** | dispatcher | 集成后本地 `cmake --build && ctest` 全量绿 |
| 4c | **清理** | dispatcher | `git worktree remove` 清理所有 worktree |
| 5 | **直推** | dispatcher | `git push origin main`(一次, 无 PR; pre-push gate 把关) |

### 9.2 同步时机(精炼)
- **只在环节 0 同步一次** 本地 main = origin/main。
- 批次中途无人 push → origin/main 不动 → **无 rebase 竞速**(根除 #31/#32/#36 那类乱象)。
- 结尾直推一次。

### 9.3 为什么直推不走 PR(老板定)
- 评审【前置】在环节 3(reviewer agent 审 worktree 产出), 不靠 PR 后置。
- 单机编排: 所有写者是本地 worktree, 集成由 dispatcher 一人连贯做 → PR 的并发协调价值低, 反引入竞速。
- `claude-review.yml` / `CODEOWNERS` 保留给**偶发外部 PR**; 内部批次走本流程。

### 9.4 铁律(本会话事故固化)
1. **agent 只在自己 worktree 本地 commit, 不 push/PR, 绝不 git 手术**(#30 共享树污染根因 = 派单让 agent 做 git 手术)。
2. **集成 + 解冲突由 dispatcher 一人连贯做**(评审前置已保证各产出合格, 集成只解 sibling 间冲突)。
3. **dispatcher 每步校验** `HEAD==origin/main` + 工作树洁癖(checkout/branch 前清 uncommitted 残留)。
4. **直推 main 前必过本地 pre-push gate**(build+全量 ctest);本地 `build/` 文件增删后先 `cmake -B build` reconfigure 防 stale。

## 10. 已知漏洞 + 待办(老板"看看有没有遗漏/不合理")

| 漏洞 | 影响 | 建议 | owner |
|---|---|---|---|
| **CMakeLists.txt 单点 append** | 每个并发 PR 同处加 test target → 必冲突 | CMake 测试**自动发现**(`file(GLOB)`/self-register), 根除冲突 | 老吴+老高 |
| **branch protection 私有 repo 不可用** | 无法自动 enforce 必 PR/必最新/禁直推(需 GitHub Pro) | 老板定: 升 Pro / 靠 CI+纪律(§9) | 老板 |
| **作者漏做提交前 rebase 解冲突** | GM 替解掩盖缺口 | 入 Definition of Done + CI require-up-to-date | 老高(模板)+全员 |
| **并发 background agent 过多 + git 手术** | 多 agent 同写共享区/做手术 → 污染(本次 #30) | 同区文件同时 1 写者; §9 禁手术 | GM 派单纪律 |
| **本地 build/ 缓存陈旧** | 文件增删后 incremental build 失败 → 卡 push | reconfigure(`cmake -B build`)或 CI 干净环境构建 | 老吴 |

## 11. #30 共享树污染事故(post-mortem 摘要)

- **现象**: 小冯 #30 整改 agent 被派去在共享树做 `reset --hard`/rebase 手术 → 污染本地 repo + #30 分支"领先10落后2"; GM 本地 HEAD 反复错位; stale build 卡 push。
- **未扩散**: **origin/main 全程干净完整**(所有 PR + 全部 test target 在), 仅 GM 本地受污染, 已 `reset --hard origin/main` 修复。
- **小冯的"删除"是对的**: 删 `kAdapterBookDepth=20` 改用 SSOT `kBookDepthLevels=5`(老高评审要求), 非乱删冲突。
- **根因链**: 派单让 agent 做 git 手术 + dispatcher 残留未清 + 未每步校验 HEAD + 作者未提交前 rebase 解冲突 + stale build。
- **整改**: §9 全环节 + 同步时机入制度; #30 作废, 待小冯在隔离 worktree、从 origin/main、普通流程干净重做。

## 12. 验证记录 (老板"成功后更新文档证明,形成规范")

**首批实跑验证 — 2026-05-29 (集成 PR #38, 已 merge origin/main `77af3d2`):**

- **规模**: 8 路 worktree 并发(小冯 orderbook / 小邓 AI模型 / 小肖 定价 / 小苏 前端 / 小蒋 回测 / 小宋 chaos-replay / 小宫 dogfood / 小颖 验收),覆盖 D/ML/A/前端/C/测试/E 各部门。
- **§9 流程逐环兑现**:
  - 开批前同步 main==origin/main ✓
  - 8 worktree 均从同一 snapshot 起,**全部只本地 commit,零 push/PR/git 手术** ✓(每个 agent 回执确认)
  - **本地 fan-in**: 7 个 git 自动合并,**1 个真冲突**(小蒋 root+tests CMakeLists vs 小邓/小肖)→ 一人 union 连贯解决 ✓
  - **merge 成功即清理 worktree**(新增铁律 §9.4-5,见下)✓ 全部清理,无堆积
  - **pre-push gate 三次拦截并修复**(工作流自证有效): clang-format(整批 56 文件漏跑)×2 轮 + **ADR-010 §2.1 生产 -Wno 红线**(小蒋 backtest 库,去掉后零警告)
  - **集成 PR #38 → 双评审 approve**(老高质量 8/8 PASS 零红线 / 老郭架构无否决无不可逆)→ merge
  - 全程 **全量 ctest 绿**: 671 unit + 22 integration + 19 replay + 4 sim
- **结论**: 同一 snapshot + 本地 fan-in 比"每 worktree 各自 push+PR rebase 竞速"显著更干净(7/8 自动合并)。工作流可推广到后续所有批次。

**§9.5 新增铁律(老板 2026-05-29):merge 成功 → 立即 `git worktree remove + 删分支`**,不堆积、不遗忘(本批已贯彻)。

## 13. 本批 follow-up(评审提出,后续 wave 跟进,非阻塞)

| # | 项 | owner | 截止 |
|---|---|---|---|
| F1 | **ADR-040: fair-value 层边界收敛** — 小邓 `ml::FairValueModel`(ONNX 向)vs 小肖 `pricing::IFairValueModel`(可解释向)两抽象需收敛(老郭建议: pricing 门面 + ml 引擎),否则 W11 接 ONNX 返工 | 小梁 牵头, 老郭评审 | W11 接 ONNX 前 |
| F2 | **CMake 测试自动发现**(`file(GLOB)`/self-register)根除 CMakeLists 单点 append 冲突(§10)| 老吴 + 老高 | 紧跟本批(下一 solo) |
| F3 | feature_store 契约级 enum(TimeStatus 等)提升 vendor-neutral 命名 | 小田/小冯 | 后续 |
| F4 | backtest `event_replayer` sport/phase 硬编码 default 去除 | 小蒋 | 后续 |
| F5 | 金额字段补 `_usdc` 后缀(backtest types)+ 文档措辞修正(pricing/orderbook 头注释)| 小蒋/小肖/小冯 | 后续 |

---

**最后更新:** 2026-05-29 by 老雷 (GM) — 完善制度+自动化 + 全环节同步时机(§9)+ #30 事故 + **§12 首批验证通过, 升强制规范** + §13 follow-up
