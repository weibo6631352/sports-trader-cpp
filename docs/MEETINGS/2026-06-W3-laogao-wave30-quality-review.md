# Wave 30 代码质量 Review — 老高 (F 顾问团)

owner: 老高 (#17, code-quality-reviewer)
last_review: 2026-05-28
联合 review: 老周 (cpp-chief-architect, 并行独立, 不重复领域)
任务来源: GM W6 Wave 31 派单 — 代码质量 + 兼容性 hack 审查
约束: review only, 0 代码改动, 0 ADR 改动 (GM 错 #13 教训)

ctest 基准: 440/440 PASS (cmake --build build && ctest -j1, 本地 macOS arm64)
diff stat: 0 added, 0 deleted (我未改任何代码)

---

## Part 1: 兼容性 hack 扫描 (10 项)

### H-01: `STCPP_TEST_BUILD` build-time switch (小卢 Wave 30)

**位置:**
- `/src/stcpp/infra/process/CMakeLists.txt` L49-64
- `/src/stcpp/infra/process/single_instance.cpp` L209-225
- `/tests/unit/CMakeLists.txt` L470-494

**评级: ACCEPT (与 ADR-010 兼容)**

理由: `stcpp_process_lock_test` 是独立 static lib target, 仅编入测试 binary, 不污染生产 binary (`stcpp_process_lock`). `STCPP_TEST_BUILD` macro 仅在 `stcpp_process_lock_test` target 的 `PRIVATE` compile definitions 中注入, 生产代码路径 (`#else constexpr std::string_view base = STCPP_PID_DIR`) 在 `stcpp_process_lock` 中完全不变. 符合 ADR-010 §2.2 "test grading 允许 test-only target" 原则.

cascade 风险评估: 目前仅 `single_instance.cpp` 使用 `STCPP_TEST_BUILD`. 其他模块 (WAL / position_ledger / signer) 均有独立的 mock writer / 测试构造函数路径, 不需要类似 _test variant. 无 cascade 必要.

**一个轻度问题 (P2):** `tests/unit/CMakeLists.txt` L488 同时声明了 `STCPP_PID_DIR="/tmp/stcpp_test"` 和 `STCPP_BUILD_COMMIT="test-build"` 作为 target compile definitions, 与 `stcpp_process_lock_test` 库中已有相同 definitions 重复注入. 不会编译错 (PRIVATE macro 覆盖无害), 但冗余. 派回小卢 W7 清理.

---

### H-02: `STCPP_TEST_PID_DIR` 环境变量 runtime override

**位置:**
- `/src/stcpp/infra/process/single_instance.cpp` L215-221
- `/tests/unit/test_single_instance.cpp` T2/T3/T7 使用 `::setenv("STCPP_TEST_PID_DIR", ...)`

**评级: ACCEPT (不破坏 R-7)**

R-7 保护的是生产 binary, 约束 "PID file path 由 build-time 决定不允许 runtime 覆盖". `STCPP_TEST_PID_DIR` env override 仅在 `#ifdef STCPP_TEST_BUILD` 分支内生效, 该宏只注入 `stcpp_process_lock_test` target. 生产 binary 链 `stcpp_process_lock` (无此宏), `getenv("STCPP_TEST_PID_DIR")` 调用路径对生产 binary 根本不编译进去. R-7 完好.

与 W5-A-09 原设计 `STCPP_PID_DIR` build-time define 无矛盾: 两者分属不同 target, 生产走 build-time, 测试走 test target + optional env override. 设计干净.

**一个轻度问题 (P2):** 测试中 `setenv` 调用在 fork 前设置, fork 后 child 继承. T7 race test 中 `setenv` 后未配对 `unsetenv` 的时机有点晚 (T7 末尾才清), 若测试异常中断可能 leak env 影响同进程后续测试. 派回小卢 W7 用 RAII env guard 包装.

---

### H-03: 老孙 libsodium 接入方式

**位置:** `/src/stcpp/signer/v52/CMakeLists.txt`

**评级: WORKAROUND (W7 重构, 不是 REJECT)**

实际实现是 `find_library + find_path` brew prebuilt 路径 (L36-58), 不是 FetchContent/FetchContent_Populate/system find_package. 没有引入 vcpkg 依赖 (W6 W2 GM 错 #11 已撤).

与 老唐 BLAKE3 FetchContent 模式不一致: BLAKE3 走 `FetchContent_MakeAvailable` (可复现), 老孙走 brew prebuilt 硬路径 (macOS-only, CI 若在 Ubuntu 会退到 mock 路径). `signer_v52.cpp` 注释 L14 仍写 "libsodium: FetchContent (老沈 W6 Wave 29 SOP §6)" 与实际实现不符 — 文档与代码脱节, 是轻度工程债.

老沈 SOP §6 原意是 "不走 vcpkg unofficial-sodium, 改 FetchContent source 编译". 老孙当前实现是 "brew prebuilt, M5+ 补 FetchContent ExternalProject_Add". 这在 W6 deadline 约束下可接受, 但 CI 重现性存疑.

**具体问题 (P1):** 当 CI 环境无 brew (Ubuntu runner) 时, `LIBSODIUM_FOUND=FALSE`, signer 测试走 deterministic mock 路径通过, 但这意味着 CI 实际上没有测过真 Ed25519 签名路径. 这是静默的测试覆盖盲点. 派回老孙 W7, 按 老沈 SOP §6 原意实现 FetchContent libsodium source 编译.

---

### H-04: `STCPP_SIGNER_V52_LIBSODIUM=1` flag 与 mock 路径

**位置:** `/src/stcpp/signer/v52/signer_v52.cpp` L35 / L107-128 / L246-267

**评级: ACCEPT (设计合理), 有 1 处 P1 风险**

`#ifdef STCPP_SIGNER_V52_LIBSODIUM` 的真/mock 分支结构清晰. mock 路径填 `0xA5U ^ i` 固定字节 + "V52P" tag, 有足够区分性, paper engine 使用者能通过签名内容识别是 mock. mock 不上链 (paper mode 不真发链上交易), 不存在被 paper engine "误用进生产" 的问题 — 生产路径 (live mode) 直接 return InternalError (L182-186), R-7 硬保护.

**P1 问题:** `signer_v52.cpp` 注释头部 L14 写 "libsodium: FetchContent", 但实际 CMakeLists 是 brew `find_library`. 注释撒谎会误导后来 owner. 派回老孙修注释 (review only, 不改代码).

`#ifdef` 嵌套深度为 2 层 (mode check + sodium check), 在可接受范围内, 不需要 ADR-019 干预.

---

### H-05: `tests/unit/CMakeLists.txt` 多 sub-agent 改重叠

**位置:** `/tests/unit/CMakeLists.txt` (773 行)

**总行数:** 773 行 (W6 W2 末). W6 W3 Wave 30 再加 test_signer_v52 + test_parquet_writer_stub, 预计 830+ 行.

**评级: P1 (工程债, W7 清理)**

现存问题清单:

1. `test_single_instance` (L470-494) 和 `test_position_ledger` (L505-533) 两个 target 都声明了 `STCPP_PID_DIR="/tmp/stcpp_test"` 和 `STCPP_BUILD_COMMIT="test-build"`, 与 `stcpp_process_lock_test` 库中 PRIVATE definitions 重复. 无功能错误但冗余.

2. `test_position_ledger` 链 `stcpp_process_lock_test` (L511) 是正确的. 但 `test_single_instance` L488 注入 `STCPP_PID_DIR="/tmp/stcpp_test"` 时, 该 define 会覆盖库级别的 `/tmp/stcpp_test` — 两者值相同, 实际无害, 但维护者不清楚谁为主.

3. if/endif 配对: 扫描 `if(STCPP_EXEC_MODE STREQUAL "paper")` 出现 3 次 (test_paper_pm_client L402, test_ml_hook L431 condition extends, test_signer_v52 L745). 配对正确, 但嵌套 if 总共 4 层, 在单文件中不算过分.

4. grandfather list 一致性: 所有 test target 都用 4 个 ADR-010 §2.2 批准的 grandfather. 一致. 但 `test_pm_client_abi` L385 多了 `-Wno-invalid-offsetof` 而无注释说明原因 — 其他有 offsetof 的 target (test_wal_writer, test_audit_emitter, test_blake3_audit) 都有注释 "TU #include .cpp 模板实例化", 该 target 无此注释. 轻度一致性问题.

**派回小宋 W7 整理 tests/unit/CMakeLists.txt:**
- 删重复 STCPP_PID_DIR / STCPP_BUILD_COMMIT definitions
- 补 test_pm_client_abi -Wno-invalid-offsetof 注释
- 考虑按 wave 分拆子文件 (800+ 行维护性差)

---

### H-06: 小田 `parquet_writer` stub 零依赖验证

**位置:** `/src/stcpp/data/parquet_writer.cpp`, `/include/stcpp/data/parquet_writer.hpp`

**评级: ACCEPT (符合 ML-R2 + ADR-010)**

确认: 无 Arrow / parquet-cpp FetchContent 依赖. CMakeLists 中 `stcpp_data_parquet` 未被 `/src/stcpp/data/CMakeLists.txt` 声明 (仅在 build/ 产出 `libstcpp_data_parquet.a` 是因为还有其他 CMakeLists 入口). 但 `/include/stcpp/data/parquet_writer.hpp` L42-45 的 Arrow 注释是前向声明注释 (已注释掉), 不引入真依赖. 符合 W6 stub 承诺.

stub testability: 10 个测试用例覆盖 T1-T10, 验证 ABI 接口 + R-20 ts chain check + FlushResult enum. 测试不测 Parquet 文件写入 (正确, stub 设计使然). testable 合格.

与 小邓 ML hook `FeatureSnapshot` ABI 一致性: `parquet_writer.hpp` L150-176 `from_snapshots()` 映射 `ml::FeatureSnapshot` 32 个 features 到 `feat[32]`, index 4 = `Goalserve_devig_p_yes_fair` (ADR-008 对应). T8 测试验证 `rec.feat[4] == 0.55f`. ABI 一致.

**P2 问题:** `stcpp_data_parquet` 的 CMakeLists target 定义缺失 — `/src/stcpp/data/CMakeLists.txt` 没有 `add_library(stcpp_data_parquet ...)` 声明. `libstcpp_data_parquet.a` 在 build/ 存在说明有其他路径 (可能是顶层 CMakeLists 或 tests CMakeLists 引入). 这个 target 的归属模糊, 需要小田 W7 在 `/src/stcpp/data/CMakeLists.txt` 补正式 target 声明.

---

### H-07: 老唐 `AuditEmitter` friend class 分析

**位置:** `/include/stcpp/observability/audit_emitter.hpp` L121

**评级: WORKAROUND (可接受, 但 P1 设计改进)**

`friend class AuditEmitterPool` 使 Pool 能访问 `AuditEmitter` 的 `build_record / write / ts_chain_ok / apply_hash_chain` 等 private 方法. 这是 GM 错 #11 hotfix 加的.

必要性分析: Pool 的 emit 流程 (L314-480) 需要在持锁状态下调 `inject_chain_state` (public 方法, L111-114), 然后调 emitter 的 emit_* 方法 (均为 public). 看 audit_emitter.cpp L314 起的 pool emit 实现, Pool 实际上通过 `inject_chain_state` (public) + `emitter->emit_decision(in)` (public) 路径执行, 不直接访问 private 成员.

那么 `friend class AuditEmitterPool` 究竟用来访问什么? 扫描 audit_emitter.cpp Pool 实现, 未发现 Pool 直接调 `build_record` / `write` / `apply_hash_chain` 等 private 方法. friend 声明可能是 "防御性加入" 或未来预留, 但当前代码中实际无需要.

**结论:** 如果 Pool 当前只走 public API (inject_chain_state + emit_*), friend 声明是多余的, 增加不必要的封装漏洞. 应移除 friend 声明, 改为完全通过 public API 调用. 派回老唐 W7 验证并清理. 如果确实需要 friend 访问 (例如 Pool 绕过 seq_counter_ 独立算 seq), 需补注释说明理由.

---

### H-08: GM 错 #11 W6 W2 hotfix 代码质量 review

**commit:** a8afebe (feat(w6-wave29): 8 IC 跨 5 单元并行 — W6 W2 + GM 错 #11 hotfix)

GM 改动涉及 10 处, 覆盖 5 个文件:

**质量评估:**

1. `BLAKE3 -Wno-unused-function` 加到 `stcpp_blake3` target: 正确做法 (第三方 C 源码). 质量合格.

2. `BLAKE3_USE_NEON=0` 宏注入: 解决 arm64 NEON 链接失败. 临时 workaround, 正确. 质量合格.

3. `friend class AuditEmitterPool` 加到 `AuditEmitter`: 如 H-07 分析, 可能多余. 质量存疑 (见 H-07).

4. `std::min size_t vs uint64_t` 两处 `static_cast<size_t>`: 是正确 C++ 做法. 质量合格.

5. 小冯 3 处 `[[nodiscard]] dropping → (void) cast`: 是机械修复, 正确. 质量合格.

6. `test_audit_emitter` 加 link `stcpp_observability_audit`: 这是真实漏链修复. 质量合格.

7. 小段 `audit_id` 方法/字段同名 → `get_audit_id()`: 命名修复正确. 质量合格.

8. `EXPECT_NEAR int→double cast`: 机械 cast. 质量合格 (见 H-08-NOTE 下).

**H-08-NOTE:** `test_position_ledger.cpp` L229 的改动:
```
EXPECT_NEAR(static_cast<double>(r2.record.entry_avg_price_micro), static_cast<double>(expected_avg), 2.0)
```
`entry_avg_price_micro` 是 `int64_t`, `expected_avg` 是 `int64_t` 字面量. cast 到 `double` 会在大值时损失精度 (int64 超过 2^53 时). `entry_avg_price_micro` 范围是价格 micro-units, 实测值 600000 左右, 远在 double 精度范围内, 当前无 bug. 但这是隐性假设, 没有注释. P2 问题, 不是 bug.

**总体质量判断:** GM 10 处改动没有引入新 bug (ctest 440/440 验证). 但 friend 声明 (H-07) 和注释 (H-03/H-04) 遗留工程债.

**关键问题:** GM 本地没运行 cmake build + ctest 就 push. 本次 440/440 是我事后验证. GM 应在 W7 建立"push 前本地 build+ctest 强制流程" (见 Part 5).

---

### H-09: build switch 滥用风险 — ADR-019 候选

**当前累计 build switch:**

| switch | 位置 | 类型 | 状态 |
|---|---|---|---|
| `STCPP_EXEC_MODE` | 顶层 CMakeLists.txt L17 | CMake CACHE STRING | 核心, 合理 |
| `STCPP_BUILD_CLI` | bin/CMakeLists.txt L46 | CMake option, 默认 OFF | 合理 (guard) |
| `STCPP_BUILD_SIGNER_V52` | signer/v52/CMakeLists.txt L22 | CMake option, 默认 ON | P1 见下 |
| `STCPP_TEST_BUILD` | process/CMakeLists.txt PRIVATE | compile def, test lib only | 合理 |

`STCPP_BUILD_SIGNER_V52` 默认 ON 且与 `STCPP_EXEC_MODE` 有隐式关联 (signer lib 只在 paper mode 有意义). 如果 live mode build 时 `STCPP_BUILD_SIGNER_V52=ON` 但 `STCPP_EXEC_MODE=live`, signer v52 库体仍会 configure (虽然 L82 的 if 块不创建 paper lib, 但 libsodium 查找 + INTERFACE target 仍运行). 这个逻辑虽然不会出错, 但表达不够干净.

**ADR-019 判断:** 当前 4 个 build switch 未超过 5 个阈值. 暂不需要立独立 ADR. 建议在 ADR-018 §X 补 build switch 命名规范条款:
- 命名: `STCPP_BUILD_*` (CMake option), `STCPP_TEST_*` (test-only compile def)
- 每新增 switch 必须在 switch 定义处注释: 用途 / 默认值 / 与其他 switch 的交互
- 上限触发点: 6+ switches 时立 ADR-019

---

### H-10: lint 一致性扫描

**新加 lib 的 -Werror 覆盖验证:**

顶层 CMakeLists.txt L28 设置 `-Wall -Wextra -Werror` 作为全局 compile options.

| lib | -Werror 继承 | 额外 -Wno- | 是否 ADR-010 批准 |
|---|---|---|---|
| `stcpp_process_lock` | 是 (无 -Wno-) | 无 | 合格 |
| `stcpp_process_lock_test` | 是 | 无 | 合格 |
| `stcpp_signer_v52_paper` | 是 | 无 | 合格 |
| `stcpp_observability_audit` | 是 | 通过 stcpp_blake3 PRIVATE 隔离 | 合格 |
| `stcpp_blake3` | 是, 但 PRIVATE: -Wno-cast-align -Wno-conversion -Wno-sign-conversion -Wno-shadow -Wno-old-style-cast -Wno-unused-function -Wno-unused-but-set-variable | 第三方 C 源码 grandfather | 合格 (与 ADR-010 §2.2 第三方条款一致) |
| `stcpp_data_parquet` | (target 声明缺失, 见 H-06) | 未知 | 待确认 |

**P1 问题 (非 Wave 30 新增, 但此次需上报):**

`stcpp_polymarket_wss` 和 `stcpp_cli_three_sig` / `stcpp_cli_strategy_unlock` 均有 `-Wno-sign-conversion -Wno-conversion -Wno-shadow` — 这些是 ADR-010 §4 小宋 W6 W2 清理已删除的非 grandfather flags, 在这两个 lib 中未清理. 具体:
- `/src/stcpp/polymarket/wss/CMakeLists.txt` L22-23
- `/src/stcpp/bin/CMakeLists.txt` L57-59, L77-79

这意味着 ADR-010 §4 W7 EOW deadline 清理工作没有覆盖到这两个 lib. 派回小宋 (ADR-010 执行人) W7 清理.

---

## Part 2: dead code / 工程债 (5 项)

### D-01: `position_ledger.cpp::ToMarketIdArray` — `[[maybe_unused]]` 掩盖 dead code

**位置:** `/src/stcpp/infra/wal/position_ledger.cpp` L58
**测试引用:** `test_position_ledger.cpp` L254 `static_cast<void>(ToMarketIdArray("paper_market_0"))` 仅为触发编译覆盖

这是工程债的典型形态: 函数已无任何业务调用者 (小蒋 W6 Wave 29 改用 `fill.market_id` 后), 但用 `[[maybe_unused]]` 保留, 并在测试中加 no-op 调用维持覆盖. 正确做法是删除该函数. `[[maybe_unused]]` 是为了 silence -Wunused-function warning, 不是为了保留 utility.

**评级: P1 工程债**
**派回:** 老王 W7 删 `ToMarketIdArray` + 测试中的 no-op 调用

---

### D-02: `audit_chain_verify_test.cpp` L187 — `bool first_mismatch_at = -1` 真实 bug

**位置:** `/tests/integration/audit_chain_verify_test.cpp` L187

```cpp
bool first_mismatch_at = -1;
```

`bool` 赋值 `-1` 在 C++ 中是实现定义行为但实际结果是 `true` (非零整数转 bool = true). 更严重的是: 变量名叫 `first_mismatch_at` 意在存储 index (整数语义), 却声明为 `bool`. L189 有 `(void)first_mismatch_at` 掩盖 unused 警告. 实际有效的 mismatch index 由 `mismatch_idx` (L190, `size_t`) 记录. 所以 `first_mismatch_at` 是彻底的 dead variable — GM 错 #11 改 int→bool 时留下的残留.

**评级: P1 (测试代码真实 bug, 虽不影响 440/440, 但语义错误)**
**派回:** 老唐 W7 删 `first_mismatch_at` 变量和 L189 `(void)` suppressor

---

### D-03: `wal_writer.cpp` W4 TODO 长期挂账

**位置:** `/src/stcpp/infra/wal/wal_writer.cpp` L11, L57, L83

3 处 "TODO W4" — ring / bg fsync / fd / SPSC ring. 当前 WAL 是同步实现, Sprint-3+ 计划切 SPSC ring. 这些 TODO 已跨 W4→W5→W6, 进入长期挂账状态.

**评级: P2 工程债** (CLAUDE.md §8 红线: 无 TODO 长期挂账)
**建议:** 老王 W7 将这 3 处 TODO 转为 JIRA/Sprint backlog item, 或改为注释 "Sprint-3 HC-07 (老王 wal-framework) 实施", 不在代码里留开放 TODO.

---

### D-04: `rigtorp_mpmc` GIT_TAG = master (非固定版本)

**位置:** `/src/stcpp/infra/spsc/CMakeLists.txt` L34

`GIT_TAG master` 是浮动引用, 每次 cmake configure 可能拉到不同版本. rigtorp/SPSCQueue 用 `v1.1` 固定 tag, 但 rigtorp/MPMCQueue 没有 tag, 只有 master. 这是已知限制 (作者未出 release), 但应在注释中说明 "无官方 tag, 固定到具体 commit SHA 更安全".

**评级: P2 工程债**
**派回:** 小石 W7 将 master 改为具体 commit SHA 并在注释说明

---

### D-05: `tests/ci_grep/abi_lock.py` — 老李 ABI lock 14 接口是否 CI 真校验

**位置:** `.github/workflows/pr.yml` L309-316, `/tests/ci_grep/abi_lock.py`

`abi_lock.py` 文件存在 (已落地 Wave 30). CI job `ci-grep-abi-lock` L309-316 检查 `tests/ci_grep/abi_lock.py` 文件是否存在并执行. 脚本已实现 (Wave 30 老高自己的任务). 但 L313 的触发条件是 `if [ -f tests/ci_grep/abi_lock.py ]`, 文件现已存在, 意味着 CI 会真正运行脚本.

**验证:** 脚本通过 `git diff --name-only` 探测 PR 变更, 本地非 PR 环境时静默 pass (L 约 60+ 的豁免逻辑). 逻辑正确, 不会误报.

14 接口 hash 的 static_assert 验证: 在 `tests/unit/test_pm_client_abi.cpp` 中有 29 个 static_assert (sizeof + offsetof + enum count). 这是编译期校验, CI build 时强制执行. 不仅仅是 PR description 引用. 结论: ABI lock 在 CI 是真校验.

**P2 问题:** `ci-grep-abi-lock` job 说明文字 L278 "abi_lock.py 由老高 W6 W2 落地前 always-pass" 已过时 — 文件现在存在, 条件语句已激活. 建议老高自己更新 CI job 注释. (但我 review only, 不改)

---

## Part 3: GM 错 #13 教训巩固

### 为何老高 W6 W2 整合时没发现 GM 越权 hotfix?

W6 W2 整合会议 (2026-06-01) 时, 老高产出的是 `docs/MEETINGS/2026-06-01-input-laogao-code-quality.md`, 那是 review 输入文档, 不是最终 PR commit 的代码审查. GM 越权 hotfix 发生在 Wave 29 commit `a8afebe` 中, 包含在同一 commit 里提交 (8 IC 交付 + GM hotfix 混在一起). 由于 commit 是一个整体, 老高没有在 commit 前做 diff 层面的按作者过滤审查.

**教训:** PR review 流程需要 commit-author-level 过滤. GM 的 commit 应该标记或分离, 不与 IC 交付混在同一 commit 中.

### PR v1.4 是否需要 commit author 限制?

当前 PR v1.3 (`docs/RESEARCH/laogao-pr-review-v1.3.md`) 已有 diff 层面 grep check, 但没有 author 过滤.

**建议 (上报 GM, 不自行修改):**

选项 A (轻量): 在 PR template 中加 checkbox "GM 改动: 是/否, 若是 则列出文件". 人工声明.
选项 B (技术): CI 中 `git log --author="weibo wang" --name-only HEAD~1..HEAD` 检查 GM 是否改了 src/ include/ 代码. 若有则 warning (不 block, 因为 GM 有 exception 权). 

我建议 选项 B 作为 warning-only check, 不 block build. 具体实施派给老高自己 W7.

### 派回 owner 重做 vs GM 代修 成本比较

GM 代修优点: 快 (hotfix 当时解了 build 阻塞). 缺点: 代码质量不可控 (GM 没本地验证), 越权设立坏先例, 后续 review 成本更高 (我现在花时间 review GM 代码质量).

派回 owner 优点: owner 了解上下文, 修复更准确. 缺点: 有时间成本 (owner 可能当时不在线).

**建议 (PR review 视角):** 建立 "24h hotfix 派单" 规则 — build 阻塞时允许 GM 打 tag 说明问题, 但代码修复必须在 24h 内由 owner 提交. GM 可以写 workaround 注释 / 提 issue, 不直接提 PR.

---

## Part 4: 问题分级

### P0 (代码质量红线, 必须 W7 优先修)

无 P0 级代码质量红线违反. 440/440 ctest 全过. 无 ADR 红线违反.

(注: H-07 的 `friend class AuditEmitterPool` 在功能上可能多余, 但不是安全红线. 保留 P1.)

### P1 (兼容性 hack / 必须 W7 前修)

| ID | 问题 | 责任人 | 截止 |
|---|---|---|---|
| H-03-P1 | 老孙 libsodium brew prebuilt: CI Ubuntu 走 mock 路径, 真 Ed25519 未被 CI 测试 | 老孙 | W7 |
| H-07-P1 | `friend class AuditEmitterPool` 可能多余, 漏洞封装 | 老唐 | W7 |
| H-10-P1 | `stcpp_polymarket_wss` + `stcpp_cli_*` 含 ADR-010 §4 已删的非 grandfather -Wno- flags | 小宋 | W7 EOW |
| D-01-P1 | `ToMarketIdArray` dead code + `[[maybe_unused]]` 掩盖 | 老王 | W7 |
| D-02-P1 | `audit_chain_verify_test.cpp` L187 `bool first_mismatch_at = -1` 语义 bug | 老唐 | W7 |

### P2 (W8+ 优化, 不 block W7)

| ID | 问题 | 责任人 | 截止 |
|---|---|---|---|
| H-01-P2 | `test_single_instance` CMakeLists 重复 STCPP_PID_DIR define | 小卢 | W8 |
| H-02-P2 | T7 test 中 setenv 未配对 RAII guard | 小卢 | W8 |
| H-04-P2 | signer_v52.cpp 注释写 FetchContent 但实际 brew prebuilt | 老孙 | W7 清理注释 |
| H-05-P2 | tests/unit/CMakeLists.txt 800+ 行, test_pm_client_abi 缺 -Wno-invalid-offsetof 注释 | 小宋 | W8 |
| H-06-P2 | stcpp_data_parquet target 声明缺失于 src/stcpp/data/CMakeLists.txt | 小田 | W7 |
| H-09-P2 | STCPP_BUILD_SIGNER_V52 默认 ON 与 STCPP_EXEC_MODE 隐式耦合, ADR-018 §X 补条款 | 老高 | W7 |
| D-03-P2 | wal_writer.cpp 3 处 W4 TODO 长期挂账 | 老王 | W7 转 backlog |
| D-04-P2 | rigtorp_mpmc GIT_TAG=master 浮动引用 | 小石 | W8 |
| D-05-P2 | ci-grep-abi-lock CI 注释已过时 | 老高 | W7 自修 |

---

## Part 5: 给 GM + 老板的拍板建议

### 建议 1: W6 Wave 30 是否可 commit + push

**建议: 有条件 ACCEPT**

当前 440/440 测试全过, 无 P0 质量红线违反. Wave 30 新增代码 (signer v52 / parquet stub / single_instance FdGuard / test_single_instance T8-T9) 质量合格.

条件: P1 问题 (H-03 libsodium CI 盲点 / H-07 friend class / H-10 -Wno- 残留 / D-01 dead code / D-02 bool bug) 在 W7 排进 Sprint backlog, 不作为 block 条件. Wave 30 可 push.

**异议项:** 老孙 libsodium CMakeLists 注释与实现不符 (H-04-P2) 应在 push 前由老孙同步修正注释, 这是零成本改动.

### 建议 2: ADR-019 build switch 规范

**建议: 不需要立独立 ADR-019, 在 ADR-018 §X 补条款**

理由: 当前 4 个 switch 在合理范围. ADR-019 立项成本 > 收益. 在 ADR-018 (lib selection, 待审) 中加 §build-switch 一节即可. 触发 ADR-019 的条件: 第 6 个 switch 出现时.

### 建议 3: ADR-010 §4 -Wno- 一致性 W7 deadline 守住

**建议: 追加小宋 W7 EOW deadline, 具体包括 stcpp_polymarket_wss 和 stcpp_cli_* 两个漏网 target**

H-10 已列出具体文件和行号. 这是 ADR-010 §4 明确承诺的 W7 EOW 清理范围, 不应推迟.

### 建议 4: W7 "冷静周" — 我支持

**建议: 支持 W7 设为 audit + 重构周, 不派新功能 wave**

理由: P1 问题 5 项, P2 问题 9 项, 加上 3 处 TODO 挂账, 总工程债已到需要集中还债的程度. W7 不新增功能 wave, 专注 P1 清理 + ADR-010 §4 deadline. 我对 GM 的建议: W7 冷静周是正确的工程决策.

### 建议 5: GM 错 #13 后续 enforcement

**建议: CI 加 warning-only commit author check (选项 B)**

具体: `.github/workflows/pr.yml` 新增 job `check-gm-src-commit`:
```bash
if git log --author="weibo wang" --name-only \
     --diff-filter=AM origin/$BASE...HEAD \
   | grep -qE '^(src|include)/'; then
  echo "::warning::GM committed src/include changes. Please verify with owner."
fi
```
warning-only, 不 block. 下一次 GM 需要 hotfix 时, PR reviewer 会看到 warning 并做显式 ack. 派老高 W7 落地 (我自己的任务, review only 本次不改).

---

## 附: ctest 验证摘要

```
cmake --build build  # ninja: no work to do.
ctest -j1            # 440/440 PASS, 100%
  unit:         422 tests
  integration:   14 tests
  sim:            4 tests
  Total time:   1.35 sec (real)
```

本次 review diff stat: **0 files changed, 0 insertions(+), 0 deletions(-)**

老高 review only, 不改代码, 不改 ADR. 严守 GM 错 #13 边界.

---

**Wave 30 quality review v1 完成汇报:**

10 兼容性 hack 扫描 + 5 工程债/dead code + 3 级问题分类 + GM 错 #13 教训 + 5 拍板建议 + diff 0 change (review only)

P0: 0 项 | P1: 5 项 | P2: 9 项

上报 GM (老雷) + 老板. 联合 review 老周看架构层, 我看质量层, 不重复.
