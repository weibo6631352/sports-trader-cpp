# 风险登记 v2.4

- Owner: 老胡 | 验收: 老雷 | Last review: 2026-05-28 (Sprint-2 W6 W2 末 Wave 30)
- 关联:
  - `docs/RESEARCH/laohu-risk-registry-v2.3.md` (v2.3 baseline, W5 末 Wave 25)
  - `docs/INCIDENTS/gm-self-mistakes-log.md` 错 #11 (R-42 来源) + 错 #12 (R-43 来源)
  - `docs/SPRINTS/sprint-02-w6-w2-progress.md` (W6 W2 周报, R-42/43/44 配套)
  - `docs/META/weekly-report-template-v2.md` → v3 (§7 Build Verification + §8 Commit Hygiene KPI)
  - `docs/ADR/2026-05-28-department-manager-mandate.md` ADR-005

---

## 0. TL;DR (v2.3 → v2.4)

- **新增 3 / 关闭 0 / 降级 0 / 升级 1 (R-42 新入 Top 5)**
- **Top 5 变动**: R-31 (4位) 被 R-42 (新, build verification) 顶出, R-42 首次入 Top 5
- **R-41 (跨 wave 引用过时):** W6 W2 0 新发, 降为观察期 (不降分, 保持 12)
- **老韩 #3 Position WAL P0 阶段缓解:** Wave 28 position_ledger + WAL 全套落码 428 tests ✓, R-38 WAL fsync 阶段缓解, R-44 vcpkg 候补顶入
- **周报模板升 v3:** §7 Build Verification KPI + §8 Commit Hygiene KPI 入模板 (GM 错 #11/#12 配套)

---

## 1. v2.3 → v2.4 风险 update

| 编号 | v2.3 → v2.4 | 状态 | 变更原因 |
|---|---|---|---|
| **R-42 (新)** | — → **15 = 5×3** | **新增, 入 Top 5** | GM 错 #11 触发: Wave 29 8 IC 声称测试过, GM 整合 build 出 10 处 hotfix, GM hotfix 率 62.5% |
| **R-43 (新)** | — → **9 = 3×3** | **新增** | GM 错 #12 触发: build_adr010 临时目录误入 git, .gitignore 通配不全 |
| **R-44 (新)** | — → **9 = 3×3** | **新增, 候补** | 小宋 P1 blocker 触发: vcpkg unofficial-sodium configure fail, vcpkg vs FetchContent 混用无规范 |
| R-41 跨 wave 引用过时 | 12 → 12 | **观察期** (0 新触发) | W6 W2 周报 §4 SSOT 版本演进段执行, W6 W2 无 GM/主管 引用过时 SSOT 事件 |
| R-31 R-20 PIT 违例 | 12 → 12 | **退出 Top 5 (被 R-42 顶)** | W6 W2 428/428 tests ✓, 4ts audit ✓, 降为候补; 仍需 W7 硬约束验证 |
| R-38 e2e WAL fsync | 12 → 12 | 监控 | Wave 28 position_ledger + WAL 落码 + 57 tests ✓; 7 天连续跑前不降 |
| R-39 sub-agent 自我纠错误伤 | 8 → 8 | 改善 | W6 W2 sub-agent 拒接 0 次, escalate 0 次, 比 W5 改善 |

---

## 2. 新增风险 R-42

### R-42 sub-agent build verification 缺失 (15 = 5×3, P0 红灯)

**来源:** GM 错 #11 (`docs/INCIDENTS/gm-self-mistakes-log.md`), 2026-06-01 W6 Wave 29 GM 整合 build 时. Wave 29 8 IC 各自报"测试 X/X 全过", GM 整合 build 出 10 处编译错误.

**影响评估 (5×3 = 15):**
- Impact = 5: 触发时 GM 代替 sub-agent 做工程修复, 直接损失 30min hotfix 时间; 重复发生则整合 build 阶段成为交付瓶颈
- Probability = 3: GM 派单 prompt 不含 build+ctest 验证约束时必然触发; W6 W3 起加 hard 约束后 P 降

**10 处 hotfix 明细:** 老唐 BLAKE3 4 处 / 小冯 nodiscard 3 处 / audit_emitter link / 小段字段同名 / 小卢+老王 EXPECT_NEAR / 小蒋 unused / sign-conversion 5 处

**为什么是系统性风险 (不是个人失误):**
1. 派单 prompt 无 build+ctest 验证强制要求 — sub-agent 无 contract 约束
2. sub-agent 倾向于"单文件验证"而非"全 cmake configure + ctest 跑通"
3. 跨模块 ABI 改动 (老唐 BLAKE3 hpp 暴露 blake3.h) 无下游 target link audit 约束
4. 第三方依赖 (vcpkg / FetchContent 新源) 无 CMake guard 默认 OFF 规范

**永久 enforcement (GM 错 #11 第 1-4 条):**
1. **派单 prompt 必加**: "本地 cmake --build build && ctest --output-on-failure 必须全过才回汇, 写测试不算交付, 跑通才算"
2. "测试 X/X 全过"叙述必须附 `ctest --output-on-failure` 摘要
3. 第三方依赖 (vcpkg / FetchContent 新源) 派单必须留 CMake guard 默认 OFF
4. 跨模块 ABI 改动必须 audit 下游 target 是否 link
5. **老高 PR review v1.3 (Wave 30):** 加 grep "ctest --output-on-failure" 在派单 prompt 文件
6. **老胡周报 §7 Build Verification KPI (v3 模板新段):** 每周追踪 GM hotfix 率

**触发条件:**
- 单 wave 内 GM 整合 build hotfix 次数 ≥ 3 → R-42 触发, 升级老雷
- GM hotfix 率 > 30% 连续 2 wave → P1 全员协商会必上桌

**缓解:**
- W6 W3 起所有派单 prompt 含 build+ctest 验证 hard 约束 (100% 覆盖率)
- 老高 PR review v1.3 grep 检验 (Wave 30 派出)
- 周报 §7 每周追踪 GM hotfix 率 (目标 < 10%)
- 跨模块 ABI 改动标准: 出 PR 时必须跑 `cmake --build build --target all` 全目标

**Owner:** 老胡 (PM, §7 Build Verification KPI 追踪) + 老雷 (GM, 派单 prompt 硬约束执行) + 老高 (PR review v1.3 grep 验证) + 5 主管 (对 IC 执行 build+ctest 要求)

---

## 3. 新增风险 R-43

### R-43 gitignore 通配不全 — 临时目录误入 git (9 = 3×3)

**来源:** GM 错 #12 (`docs/INCIDENTS/gm-self-mistakes-log.md`), Wave 29 GM 用 build_adr010 临时 build dir 验证, 忘了 .gitignore 没覆盖 `build_adr010/`, 误推入 git (885 文件 48795 行删除).

**影响评估 (3×3 = 9):**
- Impact = 3: 误推大量 build artifact 污染 git history, 删除需要专项 fix commit (2 次额外 commit)
- Probability = 3: 临时 build dir 命名不遵循 `build/` 模式时必然触发; 已补 `build_*/` 通配后概率降

**永久 enforcement:**
1. `.gitignore` 已补 `build_*/` 通配 (commit c065791)
2. **临时 build dir 命名规范**: 统一 `build/` 或 `build_<name>/`, 不用自定义前缀
3. **老高 PR review v1.3 (Wave 30):** 加 grep "build_*/" .gitignore 验证条 (配合 R-42)
4. commit 前必须跑 `git status` 检查非预期 staged 文件

**触发条件:**
- 单月误推 build artifact 次数 ≥ 2 → P 升 (4 = 3×4)

**Owner:** 老胡 (PM 跟踪) + 老雷 (GM, git hygiene self-check) + 老高 (PR review v1.3 grep 验证)

---

## 4. 新增风险 R-44

### R-44 vcpkg 与 FetchContent 混用 — 依赖管理二元化 (9 = 3×3)

**来源:** 小宋 W6 W2 上报 P1 blocker — Wave 29 老沈 CLI 依赖 vcpkg unofficial-sodium configure fail. 项目同时存在 vcpkg (系统级 port) 和 FetchContent (per-target inline 拉取) 两种依赖管理路径, 互操作规范未定.

**影响评估 (3×3 = 9):**
- Impact = 3: 新依赖上线时 configure fail 阻塞整 wave 交付; 长期混用导致版本冲突 + CI 不可复现
- Probability = 3: 无规范时 IC 按习惯选路, 混用概率高

**为什么是风险:**
1. vcpkg unofficial-* port 质量参差, configure fail 无稳定修复路径
2. FetchContent 每次 cmake configure 重拉 (网络不稳定 / 跨洋链路 CI 抖动)
3. 混用导致同一 lib (如 libsodium) 可能被 vcpkg 和 FetchContent 各自实例化, ODR 违例风险

**短期缓解 (W6 W3 协商会议程):**
- 老周 + 老韩 + GM 三方 W6 W3 协商: vcpkg vs FetchContent 选用规范 (E-15 升级议题)
- 临时规则: 新依赖必须先查 vcpkg "official" port; unofficial port 必须 CMake guard 默认 OFF + GM 拍板
- FetchContent 仅允许: gtest (已用) / nlohmann-json (ADR-018) / BLAKE3 (已用, 例外)

**触发条件:**
- Wave 内新依赖 vcpkg configure fail 次数 ≥ 1 → R-44 触发, 升 P1
- ODR 违例出现 → 升 P 至 12 (4×3)

**Owner:** 老周 (A, 依赖管理规范) + 老韩 (B, libsodium 依赖 track) + 老胡 (PM 跟踪) + 小宋 (E, CI 验证)

---

## 5. Top 5 (v2.4)

| 排名 | 编号 | 描述 | I × P | 变动 |
|---|---|---|---|---|
| Top 1 | R-02 | RiskGateway 绕过 | 20 (5×4) | 不变 |
| Top 2 | R-06 | seconds_delay 吃 PnL | 16 (4×4) | 不变 |
| Top 3 | R-07 | HC-01/02 招聘失败 | 16 (4×4) | 6/30 deadline 临近 |
| Top 4 | R-09 | Sharpe > 1 不达 | 15 (5×3) | 不变 |
| Top 5 | **R-42 (新入)** | **sub-agent build verification 缺失** | **15 (5×3)** | **GM 错 #11 触发, W6 W3 必降** |
| 候补 | R-31 | R-20 PIT 违例 | 12 (3×4) | 退出 Top 5, 监控 (W6 W2 428 tests ✓) |
| 候补 | R-38 | e2e WAL fsync 集中爆发 | 12 (4×3) | 监控 (Wave 28 WAL 落码 ✓) |
| 候补 | R-41 | 跨 wave 引用过时信息 | 12 (4×3) | W6 W2 0 触发, 观察期 |
| 候补 | R-44 | vcpkg 混用 | 9 (3×3) | 新增 |
| 候补 | R-43 | gitignore 通配不全 | 9 (3×3) | 新增 |

**R-42 与 R-09 同分 (15) 进 Top 5 理由:** R-42 Impact=5 (触发时阻塞整合 build, 代替 IC 做工程是 GM 时间成本最高场景之一); R-09 Impact=5 同. 进 Top 5 因为 R-42 是**当前已实际触发**的系统性风险 (不是预测风险), W6 W3 行动计划明确.

---

## 6. v2.3 遗留跟踪项

### 6.1 R-41 ADR-005 命名重号 (观察中)

- v2.3 §4.1 注: ADR-005 R-41 = "主管装睡" vs 本登记 R-41 = "跨 wave 引用过时" 命名重号
- W6 W2 状态: 未解决. 小米协调重编计划 (6/06 EOW 目标) 未见产出
- W6 W3 老胡跟小米确认: 建议本登记 R-41 保持编号 (跨 wave 引用过时), ADR-005 R-41 改 ADR-005-R-A1 或类似

### 6.2 R-21 paper engine 联调 (降级待定)

- v2.3: W5 e2e p99 3.9us ✓, 待 W6 一硬约束转换后再评
- W6 W2 Wave 29: 小蒋 VirtualFill 联调 + paper_e2e_smoke T1 ✓
- 降级时机: W7 末连续 7 天 e2e p99 < 50ms → 降 9 → 6

### 6.3 R-40 HR registry drift (缓解中)

- W6 W2 状态: 6 主管全部就职 + persona 对齐 ✓; 小林 HR W6 W3 JD 5 新岗起草 → registry 验证
- W6 W3 CI grep registry_consistency 再 run (小宋 Wave 30 配合)

---

## 7. 老胡附言

R-42 是 W6 W2 最重要的新风险 — 不是"GM 写 hotfix"的小事, 而是**派单合约缺失**的系统性风险. 16 个 IC 声称测试过, 10 处实际 build fail, 62.5% 的 hotfix 率意味着"声称测试过"这个回汇约定当前没有任何强制效力. W6 W3 的主要工程纪律任务是把这个比率降到 < 10%.

R-43 和 R-44 是 build 管理周边的两个配套风险, 本次联合处置 (gitignore 补通配 + vcpkg 规范待定), 纳入 Top 5 候补持续监控.

**不耻下问:**
- R-42 派单 prompt hard 约束 @ 老雷 (GM): W6 W3 首日是否确认执行? 我需要 ack
- R-44 vcpkg vs FetchContent 规范 @ 老周 (A 主管) + 老韩 (B 主管): W6 W3 协商会上出结论
- R-43 老高 PR review v1.3 grep @ 老高 (顾问): Wave 30 任务中是否已包含 gitignore 验证条?

**边界声明:**
- 我跟踪 R-42/43/44 分数, 不替老周拍依赖管理规范方案
- 我跟踪协商会 6/09 召开, 不替 5 主管决议议题结论

— 老胡, 2026-05-28 (Sprint-2 W6 W2 末 Wave 30)
