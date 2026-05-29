# Post-mortem + 开发自检清单 — Wave 3 提交前未发现的问题

- **Owner:** 老雷 (GM) 归纳, 老高 (质量) + 老郭 (架构) 复核, 小米 (doc) 维护
- **Last review:** 2026-05-29
- **触发 (老板 verbatim):** "能不能把这些错归纳一下, 给开发人员, 注意尽量不要再犯了, 为什么提交代码前不能意识到这些问题。"
- **范围:** Wave 3 (老沈 RM v0.5 / 老陈 net 出站 / 小袁 VirtualMatcher) 提交时暴露的一批本可提前发现的问题
- **抄送:** 全体 IC + 5 主管 + 老郭

---

## §1 这次到底错了什么 (事实)

3 个 IC 各自报告"完成 + 单测全绿"(老沈 103/103, 老陈 11/11, 小袁 20/20), 但实际:

| # | 问题 | 谁的改动引入 | 本可提前发现吗 |
|---|---|---|---|
| 1 | **5 个集成测试挂** (PaperE2EFixture T1/T4 + R-11 T1/T2/T3) | 老沈给 OrderIntent 加 `timestamp_ms==0 → TS_V2_MISSING` 校验, 但共享 fixture `MakeValidIntent` 没填 timestamp_ms → 所有 happy-path intent 被判 INVALID, 连锁挂 5 个 | ✅ 能 — 跑全量 ctest 立刻暴露 |
| 2 | **14 个文件没过 clang-format** | 全员 | ✅ 能 — 本地 clang-format dry-run |
| 3 | **`-Wno-double-promotion` 缺 ADR-010 豁免注释** | execution/CMakeLists.txt | ✅ 能 — 本地跑 grep check |
| 4 | **enum ABI 破坏变更未提前标评审** (INVALID_BYTES32_FORMAT 14→16) | 老沈 | ✅ 能 — 改 enum 编号就该立刻想到 ABI + 评审 |
| 5 | **死代码** (三元两支相同, v0.4→v0.5 改名遗留) | 历史遗留 | ✅ 能 — review/编译告警 |

**关键: 5 个里没有一个是"难以预料的深层 bug", 全部是提交前一条命令就能发现的。**

---

## §2 为什么提交前没意识到 (根因, 老板的问题)

三个根因叠加:

### 根因 1: 局部验证盲区 — 只跑自己的测试, 不跑全量
- 每个 IC 只跑了**自己新增**的单测 (老沈跑 risk_gateway 的 103 个), 没人跑**全量 ctest** (含 integration)。
- 跨模块破坏的本质: 老沈改的是**契约** (OrderIntent 加校验), 影响的是**共享 fixture** (集成测试用的 MakeValidIntent)。改契约的人看不到下游测试, 下游测试的人没在跑 → 中间地带无人负责。

### 根因 2: 契约变更没追下游消费者
- 给 OrderIntent 加 `timestamp_ms` 校验是**对的** (laohan spec R3.6), 但加了之后**没 grep 谁在构造 OrderIntent** → 漏了 fixture。
- 任何"加字段/加校验/改 enum/改默认值"都是契约变更, 必须同步改所有构造方/消费方。

### 根因 3: 质量门太靠后 (pre-push 才触发)
- build+ctest+clang-format+grep 这套 gate 只在 **pre-push** 跑 (ADR-032)。IC 在"写完→报告完成"和"push"之间隔了很久, 等于一大批活做完才发现问题, 反馈环太长。
- gate 本身是好的 (这次正确拦住了 broken code), 但**应该在本地、提交前就主动跑**, 而不是等 push 被拒。

---

## §3 提交前自检清单 (强制, 报告"完成"前必过)

> 任何 IC 在说"完成 / 单测全绿 / 可 review"之前, 本清单逐项过。**跳项 = 提交质量事故。**

```
□ 1. 跑【全量】ctest, 不只跑自己新增的测试
      cmake --build <build> && ctest --test-dir <build> --output-on-failure
      —— 必须全绿 (含 integration/sim/replay), 不是只看自己那几个

□ 2. 改了契约? (新增/删除字段、改 enum 编号、改默认值、加校验、改函数签名)
      → grep 所有构造方 + 消费方 (含 tests/fixtures), 一起改
      grep -rn "<改的类型/字段/enum名>" include/ src/ tests/

□ 3. clang-format 所有改动文件 (dry-run 无 diff)
      git diff --name-only | grep -E '\.(cpp|hpp)$' | xargs clang-format --dry-run -Werror

□ 4. 新增 -Wno-* ? → 必带 ADR-010 §3.2 豁免注释 (理由 + owner)

□ 5. 改了 ABI/enum 编号/核心数据结构? → 提交前就 @老郭 (架构评审) + @老唐 (WAL migration), 不要事后补

□ 6. 本地跑一遍 pre-push 同款 gate (别等 push 被拒)
      —— build + 全量 ctest + clang-format + python grep checks 全过

□ 7. 顺手清自己改动里的死代码 / 改名遗留

□ 8. 不用 --no-verify 绕过 gate; 万不得已绕过, 必须逐项说明每个被跳的检查为何安全
```

---

## §4 系统性整改 (不靠自觉, 靠机制)

| 措施 | Owner | 状态 |
|---|---|---|
| 本清单纳入 onboarding + 派单 prompt 标准尾注 (IC 完成定义 = 全量 ctest 绿) | 小林 (HR) + 各主管 | 待办 |
| 提供 `scripts/precommit-check.sh` 一键本地跑 pre-push 同款 gate (缩短反馈环) | 老吴 (SRE) + 老高 | 待办 |
| "完成"定义升级: 报告完成必附【全量 ctest 通过数】, 不接受只报自己模块数 | 5 主管 enforce | 即日起 |
| 契约变更 PR 模板加 "已 grep 下游消费者" 勾选项 | 老高 (PR 模板) | 待办 |
| --no-verify 使用纳入周报监控 (老胡 §6) | 老胡 | 即日起 |

---

## §5 一句话给开发

**报"完成"前，先跑全量 ctest，不是只跑你自己写的那几个测试。改契约就 grep 谁在用。这次 5 个挂掉的测试，任何一个人多跑一条 `ctest` 全量命令就能在提交前发现。**

---

*老雷 (GM), 2026-05-29 — Wave 3 提交质量 post-mortem。下次再犯同类问题入 INCIDENTS 个人事故记录。*
