# ADR-010 候选 — 测试代码分级 lint 标准

- **owner:** 老高 (#17, code-quality-reviewer, F 顾问团)
- **last_review:** 2026-06-01
- **status:** 候选 (W6 老高 spec, 等老郭 first review → GM ack)
- **触发:** Wave 26 决议 #7 (老郭 §5.3 challenge, 2026-06-01 代码 review 大会)
- **关联:**
  - `docs/MEETINGS/2026-06-01-code-review-summit-v1.md` §5.3 + 决议 #7
  - `docs/RESEARCH/laogao-pr-review-v1.2.md` §3 测试代码分级
  - `.clang-tidy` v1.2 备注 (F)
  - `tests/unit/CMakeLists.txt` + `tests/CMakeLists.txt` (实施目标)

---

## §1 背景与动机

Wave 26 (2026-06-01 代码 review 大会) 老郭 ground-truth 发现:

- `tests/unit/CMakeLists.txt`: 全部 14 个 test target 均携带
  `-Wno-double-promotion -Wno-sign-conversion -Wno-conversion -Wno-shadow
   -Wno-old-style-cast -Wno-cast-align -Wno-character-conversion -Wno-invalid-offsetof`
- `tests/CMakeLists.txt`: 类似大块抑制
- 生产代码 `src/ + include/`: 零 `-Wno-*` 抑制 (老李 pm_client.hpp benchmark ✓)

**问题:** 测试代码质量低生产代码一档是**隐式约定**, 无显式文档记录。
这导致:
1. 新加测试时习惯性抄全量 `-Wno-*` 而不思考是否需要
2. `-Wno-sign-conversion` 掩盖测试代码真实类型安全问题
3. 无法分辨哪些抑制是"gtest 框架引起的合理妥协"vs"测试写法问题"

**本 ADR 目标:** 显式化分级标准, 建立 grandfather list, 防止扩散, 逐步收紧。

---

## §2 决策

### 2.1 生产代码 (src/ + include/): Strict

- 零 `-Wno-*` 抑制
- 例外: 无 (任何抑制须走 PR + 老高 + 老郭 三方签字, 入档 ADR-010 §5 例外记录)
- clang-tidy: 本仓 `.clang-tidy` 全套 check
- Benchmark: 老李 `pm_client.hpp` (零 -Wno-) 为生产代码标准

### 2.2 测试代码 (tests/): Relaxed (but typed)

**允许 (grandfather list, 原因已验证):**

| -Wno- 标志 | 触发来源 | 允许原因 |
|---|---|---|
| `-Wno-double-promotion` | gtest `ASSERT_NEAR` / `EXPECT_DOUBLE_EQ` 浮点宏 | gtest 框架自身触发, 非测试写法问题 |
| `-Wno-cast-align` | gtest mock 对象转型 | gtest 内部实现触发 |
| `-Wno-old-style-cast` | gtest `ASSERT_*` / `EXPECT_*` 宏展开 | gtest 宏展开触发, C 风格转型不可避免 |
| `-Wno-invalid-offsetof` | TU 内 `#include *.cpp` 做模板实例化 | 已知 pattern, 仅在 WAL/audit 模板测试中 |

**禁止新增 (类型安全必须, 测试代码自己保证):**

| -Wno- 标志 | 禁止理由 |
|---|---|
| `-Wno-sign-conversion` | 测试代码的 sign 错误就是真 bug. 测试 helper 应用正确类型 |
| `-Wno-shadow` | 测试代码变量遮蔽降低可读性. 改名即可 |
| `-Wno-conversion` | 隐式 narrowing 在测试中与在生产中同等危险 |
| `-Wno-character-conversion` | char → int 转换问题, 测试代码应显式 cast |

**注:** 现有 `tests/unit/CMakeLists.txt` 中已有的 `-Wno-sign-conversion` / `-Wno-shadow` / `-Wno-conversion` 进入 §4 清理队列 (不立即删除, 给 W6-W8 修复期)。

### 2.3 clang-tidy 分级

- 生产代码: 本仓 `.clang-tidy` 全套 (含 v1.2 新增 bugprone-misplaced-widening-cast 等)
- 测试代码: 允许关闭 `modernize-use-trailing-return-type` (gtest TEST_F 宏不兼容)
- 测试代码: `bugprone-* cert-* cppcoreguidelines-owning-memory` 同等 enforce (类型安全不让步)

---

## §3 grandfather list (现有存量, 已验证无害, W6 起冻结不扩散)

以下为 Wave 26 老郭 ground-truth 扫描发现的现有抑制点。本 ADR 批准这些存量，但**冻结**: 任何新增 `-Wno-*` 超出此列表必须走 ADR-010 §5 例外申请流程。

### 3.1 tests/unit/CMakeLists.txt (全 14 target 共享)

```cmake
# ADR-010 grandfather list (v1.2 2026-06-01)
-Wno-double-promotion    # gtest 浮点宏 — 允许
-Wno-cast-align          # gtest mock 转型 — 允许
-Wno-old-style-cast      # gtest 宏展开 — 允许
-Wno-invalid-offsetof    # WAL/audit TU include .cpp 模板实例化 — 允许 (仅 test_wal_writer, test_audit_emitter)
```

**待清理 (W7 前目标删除):**
```cmake
-Wno-sign-conversion     # 测试代码类型安全问题, 非 gtest 框架引起 — 待清
-Wno-shadow              # 测试变量遮蔽 — 待清
-Wno-conversion          # 同上 — 待清
-Wno-character-conversion # 同上 — 待清
```

### 3.2 src/stcpp/polymarket/wss/CMakeLists.txt

老郭 §5.4 发现: 4 行 `-Wno-*` 抑制 (来源待 老高 W6 ack 是 nlohmann::json 还是 boost.beast)。
本 ADR 冻结, 待 W6 老高确认来源后:
- 若是三方库引起 → 允许, 文档注明来源
- 若是代码写法引起 → 修复并删除

---

## §4 清理队列 (W6-W8 分批)

| 目标文件 | 待删 -Wno- | 负责人 | 目标 wave |
|---|---|---|---|
| `tests/unit/CMakeLists.txt` | `-Wno-sign-conversion -Wno-shadow -Wno-conversion -Wno-character-conversion` | 小宋 (test infra) + 老高 review | W7 |
| `src/stcpp/microstructure/CMakeLists.txt` | `-Wno-double-promotion` (确认是三方库引起则保留) | 小袁 + 老高 | W6 末 |
| `src/stcpp/ml/CMakeLists.txt` | `-Wno-old-style-cast` (确认是 LightGBM C API 则保留) | 小邓 + 老高 | W6 末 |
| `src/stcpp/polymarket/wss/CMakeLists.txt` | 4 行待确认 (boost.beast vs nlohmann::json) | 小冯 + 老高 | W6 末确认, W7 处理 |

---

## §5 例外申请流程 (新增 -Wno-* 超出 grandfather list)

1. PR author 在 PR description 说明: 哪个 `-Wno-`、哪个文件、为何不能在测试代码修复
2. 在 `tests/ci_grep/` 目录下无对应 CI 检查的 `-Wno-` 直接走 ADR-010 例外路径
3. 老高 review → 老郭 24h 仲裁 → GM ack
4. 批准后更新本文 §3 grandfather list, 注明日期 + 来源 + owner

---

## §6 PR review v1.2 enforce 规则

老高 PR review v1.2 对测试代码的 gate:

- **新增 `-Wno-sign-conversion`** 在 `tests/` 目录 → 直接 reject (不走例外)
- **新增 `-Wno-shadow`** 在 `tests/` 目录 → 直接 reject
- **新增任何 `-Wno-*`** 超出 §3 grandfather list → 触发 §5 例外申请
- **生产代码任何 `-Wno-*`** → 直接 reject (零容忍)
- clang-tidy: tests/ 下 `bugprone-*` + `cert-*` 警告不允许关闭

---

## §7 待 GM ack 事项

- **时间线:** W6 第 1 周内老郭 first review → GM ack
- **实施前置:** 本 ADR 仅定义标准, 不修改 CMakeLists.txt (清理走各 owner PR + 老高 review)
- **与 ADR-005 关系:** ADR-010 变更走 PR, PR 必须符合 ADR-005 派单链

---

## §8 签字位

- [ ] 老高 (#17) owner spec — v1.2 落地配套
- [ ] 老郭 (#16, F 协调人) first review
- [ ] GM 老雷 ack → 生效 (W6 第 1 周)
- [ ] 小宋 (#36, test infra) 确认 W7 清理队列可接
- [ ] 老周 (#01, 系统工程部) 确认 WSS CMakeLists 清理时间线

---

— 老高 (E-017), 2026-06-01 (W6 Wave 28, self-spec F 顾问)
