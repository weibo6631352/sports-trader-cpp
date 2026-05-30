# 代码质量审计 v1 — 老高 W5 末 Wave 26 输入

- **Owner:** 老高 (#17, F 顾问团, code-quality-gate)
- **Last Review:** 2026-06-01
- **会议:** W5 末 Wave 26 代码 review 大会
- **ADR:** ADR-009 (老高 = Sonnet 4.6)
- **范围:** main tree 实跑 grep + 6 模块 spot check + 3 测试 sample

---

## Part 1: PR review v1.1 grep 实跑结果

所有 grep 在 `src/` + `include/` 执行，测试目录单独处理。

| grep 规则 | 命中行数 | 误报 | 真违例 | 说明 |
|---|---|---|---|---|
| `data_source_ts.*now()` | 2 | 2 | 0 | 仅 comment 行 (goalserve_record.hpp + wss_event.hpp 文档注释)，无赋值违例 |
| `event_ts.*=.*ingestion_ts` | 0 | 0 | 0 | 干净 |
| `lock_guard\|blocking_read\|sync_http` in `src/stcpp/polymarket/wss/` | 0 | 0 | 0 | WSS 目录干净 |
| `sports-tail-trader` | 0 | 0 | 0 | 项目名无漂移 |
| `/ws/user\|paramType\|sigType.*=.*1` | 5 | 5 | 0 | 全为 comment/文档，`signature_type{1}` 是正确 enforce，非违例 |
| `rstrip(b"=")` | 0 | 0 | 0 | 无 Python-style 代码混入 |

**结论：6 项 grep 均 0 真违例，符合预期。**

备注：`data_source_ts.*now()` 命中的 2 行是 R-20 契约注释（`event_ts ≤ data_source_ts ≤ ... ≤ now()`），grep 模式可收窄为 `data_source_ts\s*=\s*.*now\(\)` 以降低噪音，建议 v1.2 更新。

---

## Part 2: 6 模块 spot check

### 2.1 RiskGateway (老韩 spec + 老沈 BUG-W5-001 patch)

`include/stcpp/risk/risk_gateway.hpp` + `src/stcpp/risk/risk_gateway.cpp`

**整体评价：生产级，21 reject short-circuit 顺序与 ADR-004 一致。**

具体发现：

1. **BUG-W5-001 修法合规**：原 `seq >> ((9-i)*8)` 在 `uint64_t` 上 shift=72 是 UB，已改为 6 byte big-endian (shift max=40)，`out[12..15]` 占位 0 等 M5 老孙 ULID rand 接入。括号显式加，防优先级误读。修法技术上正确，ULID spec (Crockford) 要求 10 byte randomness，当前 stub 为 6 byte seq + 4 byte zero，已在注释标明 "M5 接老孙 ULID generator"，stub 阶段可接受。

2. **注释编号歧义（P2）**：`hpp` 第 232 行注释写 `// 5 市场`，而 `evaluate()` 中市场检查位于位置 5（含 state=1-3，invalid_intent=4，dup=5，stale=6），实际执行顺序中市场是第 7 步。注释沿用了 ADR-004 前的旧编号，未随 liquidity/caps 调换而更新。文件顶部和 `.cpp` 描述正确，但 `hpp` private section 的行内注释有两处编号标 "5" 和 "6" 重复，与 SSOT（ADR-004 + .cpp 注释块）不一致。

3. **ABI 稳定**：`RiskDecision` 字段顺序（decision/reject/sub_reason/audit_id/decision_ts/slippage_bps/expected_fill_rate）无 pragma pack，struct 布局靠编译器默认对齐，目前为 POD-like，尚无跨进程共享场景，可接受。

4. **RiskConfig 无 `bankroll_usdc` 动态路径**：ctor 将 `cfg.bankroll_usdc` 写入 `bankroll_usdc_` atomic，但 `cfg_` 副本也留有 `bankroll_usdc`。`set_bankroll()` 只更新 atomic，不更新 `cfg_`，两者可能漂移。当前测试绕过此路径，无 bug，但未来维护者可能误读。

### 2.2 IPolymarketClient (老李 + PaperPolymarketClient)

`include/stcpp/polymarket/pm_client.hpp`

**整体评价：14 接口契约清晰，R-7/R-11/R-20/HMAC 红线均有文档标注，无 -Wno- 抑制。**

具体发现：

1. **零 -Wno- 抑制**：pm_client.hpp 为纯 header（接口 + struct），生产代码无 -Wno- 抑制，符合预期。

2. **HMAC 4 反模式（P2 命名）**：`signature_type{1}` 已在 `SignedOrder` struct 默认值中强制，注释引用 "HMAC bug #3"。grep 结果显示 paper 实现也有 sigType=2 防御注释（`paper 也防 caller 误传 sigType=2`），enforce 路径存在。

3. **`kPMErrorKindCount = 10` 值正确**（Ok=0 + 9 失败，共 10）。命名可考虑 `kPMErrorKindTotal` 以区分"错误种数"与"最大 enum 值+1"，但当前无歧义。

4. **`DataSourceTsSource` 命名（P2）**：`InferredFromDsTs` 略模糊，含义是"从同 batch 内 ds_ts 推断"，建议 v1.2 审核时追问老李是否有更清晰选项（如 `InferredFromBatch`）。

### 2.3 AuditEmitter + AuditRecord (老唐)

`include/stcpp/observability/audit_emitter.hpp` + `audit_record.hpp`

**整体评价：架构设计扎实，BLAKE3 stub 有明确 Sprint-3 升级路径，API 稳定性合格。**

具体发现：

1. **kAuditEventTypeCount 语义歧义（P1）**：`AuditEventType` enum 从 `Unknown=0` 到 `ReconDrift=12` 共 **13 个值**，但 `kAuditEventTypeCount = 12`。注释写"12 AET"，说明设计意图是排除 Unknown。名称应改为 `kAuditEventTypeCount_ExclUnknown` 或文档明确 "不含 Unknown"，否则遍历代码用 `kAuditEventTypeCount` 会少一次迭代。此为命名不精确，不影响运行（Unknown 不应出现在正常路径），但 P1 级别修复。

2. **ORDER_BOOKED 缺失（P1）**：老李 v1 提出的 audit v1.2 需求：订单上 book 后（Booked 状态）应有审计事件，当前 AET 12 项无 `OrderBooked`，audit trail 存在 gap（下单→成交中间无痕迹）。建议 W6 小唐与老李对齐，bump audit schema v1.2。

3. **XOR stub 安全性**：`stub_chain = prev XOR payload_hash` — XOR 无加密强度，但当前为 stub，Sprint-3 切 BLAKE3 时只需替换 `stub_chain` 函数，API 接口（`apply_hash_chain`）不破坏。升级路径 API 稳定，P2 跟踪即可。

4. **`emit_recon_drift` 不拒 PIT 违规**：注释说明"recon_drift 本身可能因 PIT 失败触发，不拒 ts 不一致"，逻辑合理（诊断路径不能递归失败），但框架 `Append` 仍会拒，需确认最终兜底行为（W5 接 RM 慢路径时补测试）。

### 2.4 P0-01 PinnacleNoVigSignal + LiveSectionClassifier (小卢/小程)

`include/stcpp/strategy/p0_01_pinnacle_no_vig.hpp` + `src/stcpp/strategy/p0_01_pinnacle_no_vig.cpp`

**整体评价：5 条件 AND short-circuit 顺序合理，no-vig 公式数值精度合格。**

具体发现：

1. **no-vig 公式精度**：`p_yes_fair = p_yes_raw / overround`，`overround = 1/decimal_yes + 1/decimal_no`，multiplicative normalization 标准实现，无明显数值精度问题。`MIN_DECIMAL_ODDS = 1.0 + 1e-9` 防零除，`is_finite` 自实现（未用 `std::isfinite`）用 NaN 比较 + 范围检查，等效但可读性略低（P2）。

2. **触发条件 3 语义依赖（P2）**：条件 3 检查 `delta_kickoff_ns < SIX_HOURS_NS || g.live`。当比赛已经结束（`kickoff_ts < as_of_ts`），`delta_kickoff_ns` 为负，仍满足 `< SIX_HOURS_NS`，条件 3 通过。实际上由条件 5（`LiveSection ∈ {Live, Soon}`）兜底拦截 Closed 状态，逻辑上正确但防御不自洽，条件 3 本身应改为 `delta_kickoff_ns ∈ (-kickoff_ns, SIX_HOURS_NS)` 或加 `delta_kickoff_ns >= 0` 约束，不依赖后序条件。

3. **R-20 4 ts 透传**：`validate_context_` 检查 4 ts >0 + 单调不等式 + market_id/feature_snapshot_id 非空，符合 R-20 要求。

4. **`round_to_bps` 自实现四舍五入**：未用 `std::lround`，用 `+0.5/-0.5` 方式，对正值正确，负 edge 时也有覆盖（`raw >= 0.0 ? +0.5 : -0.5`），无 bug。

### 2.5 WalWriter + WalKind (老王)

`include/stcpp/infra/wal/wal_writer.hpp` + `wal_kind.hpp` + `src/stcpp/infra/wal/wal_writer.cpp`

**整体评价：4 WalKind 路径分流正确，R-11 abort 路径测试可触达，skeleton 标注清晰。**

具体发现：

1. **4 WAL kind 路径分流**：`kPathRoots` 4 条目与 `WalKind` enum 1:1，`PathPrefixOk` 校验 + `AbortOnPathMismatch` [[noreturn]] 路径完整。`[[noreturn, maybe_unused]]` 双 attribute 语法正确（GCC/Clang 均支持）。

2. **WalRecord concept 满足**：`AuditRecord` 有 `event_ts_ns()/data_source_ts_ns()/ingestion_ts_ns()/as_of_ts_ns()/audit_id()/serialize_into()/max_serialized_size()` 7 个方法，concept static_assert 已在 audit_record.hpp 末尾验证编译期。

3. **PIT chain assert**：`Append()` 内 `pit::AssertChain(h)` 必调，返回 `WalError::PitViolation` 而非 throw，符合 noexcept 要求。

4. **TODO W4 标注（P1，已知）**：ring / bg fsync / fd 仍为 skeleton。当前 `high_watermark_` 无条件推进，单测通过但不代表真实 fsync 语义。`TODO W4` 已有明确 owner (@小石 SPSC, @老练 fsync)，需 W6 前确认 backlog 进 Sprint。

5. **`h.len_payload` 写 0**：`Append()` 中 `h.len_payload = 0`，skeleton 合理，但 W4 真实 serialize_into 后需填真值，属于 TODO。

### 2.6 SingleInstanceLock (小卢 W5-A-09, ADR-009 Sonnet 首例)

`include/stcpp/infra/process/single_instance.hpp` + `src/stcpp/infra/process/single_instance.cpp`

**整体评价：production grade，POSIX RAII + flock 选型正确，7 测试覆盖全矩阵。**

具体发现：

1. **flock vs fcntl 选型**：`flock(2)` 在 Linux + macOS BSD 语义一致（绑 open file description，close 自动释放）；`fcntl(F_SETLK)` 绑进程，fork 后子进程继承，语义更复杂。选 flock 对本场景（防双开）更合适，注释已说明理由。

2. **O_CLOEXEC 正确**：`open()` 时传 `O_CLOEXEC`，防 fork+exec 后 fd 泄漏到子进程（老沈 review 点已落实）。

3. **SIGTERM async-signal-safe**：`SigtermHandlerImpl` 只调 `::unlink`、`::close`、`::_exit`，三者均在 POSIX.1-2017 async-signal-safe 函数列表中。`g_pid_path_for_handler` 为 static char[]，`g_lock_fd_for_handler` 为 static int——非 atomic，但 handler 只从单一 signal context 写入，主线程不并发写（handler 安装后 g_pid_path 不再改），可接受（P2 级别：如果将来多次调 InstallSigtermHandler 需加 volatile 或说明）。

4. **g_lock_fd_for_handler 永不被赋值（P1，功能缺陷）**：`InstallSigtermHandler` 内只拷贝了 `pid_path`，`g_lock_fd_for_handler` 始终为 -1（初始值）。`SigtermHandlerImpl` 中 `if (g_lock_fd_for_handler >= 0)` 分支永远不执行——handler 不 close fd。这意味着 SIGTERM 后 fd 靠进程退出（`_exit(0)`）自动关闭，最终效果正确（fd close → flock 释放），但 handler 显式 close 的设计意图落空，且 spec header 注释（"SIGTERM handler 另行 unlink PID file"）未提 fd close，造成行为与代码注释不一致。建议修复：`InstallSigtermHandler` 增加 `int lock_fd` 参数，或删除 handler 中无效的 close 分支，并更新注释。

5. **`MkdirP` 仅支持 2 层**：注释 "PID dir 只有 2 层"，如果未来 PID dir 改为更深路径（如 `/var/run/stcpp/paper`）会静默失败，依赖 mkdir 返回错误抛异常。当前 MVP 路径 `/tmp/stcpp` 安全，P2 记录。

---

## Part 3: 测试代码质量

### 3.1 -Wno-* 全局抑制评估

`tests/unit/CMakeLists.txt` 中所有 14 个 test target 均携带：

```
-Wno-double-promotion -Wno-sign-conversion -Wno-conversion
-Wno-shadow -Wno-old-style-cast -Wno-cast-align
```

其中 `test_wal_writer` 和 `test_audit_emitter` 额外有 `-Wno-invalid-offsetof`（因 TU 内 `#include *.cpp` 做模板实例化，offsetof 在非 standard-layout 类上触发警告）。

**评估：**

- `Wno-sign-conversion` 在测试代码中可能掩盖真实 sign-conversion bug。测试 helper 如 `make_ok_intent()` 中 `now - 500 * NS_PER_MS` 是 `int64_t - int64_t`，安全；但如果测试代码写了 `size_t i` 与 `int` 比较，警告会被抑制而不报。
- 当前 319 case 全过，无回归，说明现有测试未命中该 bug 类。
- `Wno-shadow` 抑制了 test helper 内可能的变量遮蔽，降低测试代码可读性保障。

**结论：** 测试代码质量比生产代码低一档，属于当前阶段务实妥协（gtest 自身触发 conversion 警告），但不应无限期维持。

### 3.2 分级 PR review 建议

建议 W6 起分两档：
- **生产代码**：维持 `-Werror -Wall -Wshadow -Wsign-conversion`（现状）
- **测试代码**：保留 `Wno-double-promotion`（gtest 浮点宏触发）+ `Wno-cast-align`（gtest mock 转型），但**取消** `Wno-sign-conversion` 和 `Wno-shadow`

这样测试代码仍能检出类型安全问题，只豁免真正由 gtest 框架自身触发的警告。

### 3.3 3 测试 spot check

**test_risk_gateway.cpp（40+ case）**
- `make_ok_intent()` 构造合法 OrderIntent，4 ts 单调，book_snapshot fresh，覆盖 R-20 前置。
- ADR-004 regression：test 包含 position_caps 先于 liquidity 的 short-circuit 用例，符合 ADR-004 patch。
- `AuditId.NonZero_O2`：APPROVED 路径 audit_id 非空验证，覆盖 R-1 invariant。
- 发现：`InMemoryEmitter` 定义在 `test_risk_gateway.cpp` 内，与 `observability::AuditEmitter` 同名但不同命名空间（`risk::AuditEmitter`），可接受但命名易混淆（P2）。

**test_paper_pm_client.cpp（28 case）**
- `MakeValidTs()` 构造 4 ts，`MakeValidOrder()` 强制 `signature_type=1`，覆盖 HMAC bug #3。
- R-7 mode tag：test 在 `STCPP_EXEC_MODE=paper` 下编译，`if(STCPP_EXEC_MODE STREQUAL "paper")` CMake 守门正确。
- R-11 `audit_wal_kind == PaperAudit` 验证存在。

**test_single_instance.cpp（7 case）**
- T2 双进程 fork + pipe 同步无竞态。
- T3 kill -9 模拟（SIGKILL + waitpid）后 flock 自动释放，覆盖 spec §2.4。
- T7 8 进程并发 race，仅 1 成功，flock LOCK_NB 语义验证完整。
- T6 SIGTERM：子进程 raise(SIGTERM) → handler unlink → parent 验证文件不存在。严谨。
- T5 PID 文件内容格式 4 行解析用 `::atoll`（NOLINT 已注）。

**测试质量总体合格，T7 race test 是 production-grade 并发验证。**

---

## Part 4: 严重发现 + 建议

### P0（上线阻塞）

**无。**

319 case 全过，6 个 v1.1 grep 0 真违例，R-20/R-11/R-12/R-1 红线均有代码级 enforce 且有对应测试覆盖。

### P1（W6 必修）

| ID | 模块 | 描述 | Owner 建议 |
|---|---|---|---|
| P1-01 | audit_record.hpp | `kAuditEventTypeCount = 12` 但 enum 含 13 个值（含 Unknown=0），命名模糊，遍历代码依赖此常量会缺 1 次 | 老唐 修复命名或加 `static_assert` 说明 |
| P1-02 | audit_record.hpp | `ORDER_BOOKED` AET 缺失，订单上 book 无 audit 痕迹（老李 v1 audit v1.2 需求） | 老唐 + 老李 对齐，audit schema bump v1.2 |
| P1-03 | single_instance.cpp | `g_lock_fd_for_handler` 永不赋值，SIGTERM handler 中 close(fd) 分支死代码；功能上靠 `_exit` 兜底，但代码意图与实现不一致 | 小卢 修复：`InstallSigtermHandler` 加 `lock_fd` 参数，或删除无效分支 + 更新注释 |
| P1-04 | tests/unit CMakeLists.txt | 全量 `-Wno-sign-conversion -Wno-shadow` 抑制，测试代码类型安全保障低一档，存在漏检风险 | 老高 W6 PR review v1.2 分级守门（见 Part 5） |
| P1-05 | wal_writer.cpp | ring / bg fsync / fd TODO W4 仍为 skeleton，`high_watermark_` 无条件推进，未进 Sprint-2 backlog | 老王 确认 W6 Sprint-2 backlog 条目，明确 @小石 SPSC + @老练 fsync 时间线 |

### P2（W7+ 优化）

| ID | 模块 | 描述 |
|---|---|---|
| P2-01 | risk_gateway.hpp | private section 行内注释有两处编号重复（"5 市场" 与 ADR-004 后顺序不符），建议统一用 step-1..step-9 |
| P2-02 | pm_client.hpp | `DataSourceTsSource::InferredFromDsTs` 命名略模糊 |
| P2-03 | p0_01_pinnacle_no_vig.cpp | 触发条件 3 语义依赖条件 5 兜底（delta < 0 时也通过），建议条件 3 自洽：加 `g.kickoff_ts_ns > 0` 检查或注明依赖 |
| P2-04 | p0_01_pinnacle_no_vig.cpp | `is_finite` 自实现，可改 `std::isfinite`（C++20 constexpr，无运行时开销）提升可读性 |
| P2-05 | single_instance.cpp | `g_lock_fd_for_handler` 非 volatile/atomic，多次 `InstallSigtermHandler` 调用需说明不可重入 |
| P2-06 | audit_emitter.cpp | XOR stub hash chain Sprint-3 升 BLAKE3 时 API 不破坏（`apply_hash_chain` 接口稳），但 XOR 无加密强度，线下 audit 可伪造，需在文档明确 Sprint-3 deadline |
| P2-07 | tests | `InMemoryEmitter`（test_risk_gateway.cpp 内） 与 `observability::AuditEmitter` 同名不同 ns，命名建议改 `MockAuditEmitter` |
| P2-08 | risk_gateway.hpp | `RiskConfig.bankroll_usdc` 与 `bankroll_usdc_` atomic 可漂移，建议 ctor 注释说明"cfg_.bankroll_usdc 为初始快照，运行期以 atomic 为准" |

---

## Part 5: 老高 PR review v1.2 升级建议

### 5.1 测试质量 sub-gate（W6 起）

在 `tests/unit/CMakeLists.txt` 的 `-Wno-*` 列表中：

- **保留**（gtest 框架自身触发）：`-Wno-double-promotion`、`-Wno-cast-align`、`-Wno-old-style-cast`、`-Wno-invalid-offsetof`
- **取消抑制**（测试代码应自己保证）：`-Wno-sign-conversion`、`-Wno-shadow`、`-Wno-conversion`

PR checklist 新增：测试代码不允许新增 `-Wno-sign-conversion` / `-Wno-shadow` 抑制。

### 5.2 ADR-009 model grep（W6 起）

`.github/workflows/pr.yml` 新增 step：

```bash
# grep opus exception tag
grep -rn "model: opus" .claude/ docs/ | grep -v "例外:" && \
  echo "ERROR: model: opus without 例外: label" && exit 1 || true
```

PR template 加 checklist 项：IC persona 派单是否标注 model=Sonnet。

### 5.3 GM 6 题自检 grep（W6 起）

PR description 模板加 section：

```
## GM 6 题自检
- [ ] CLAUDE.md §7 HR 注册前置规则已读（本 PR 含新 agent？）
- [ ] 派单 prompt 未越过 persona 拒绝任务边界
- [ ] 未使用一面之词背书
- [ ] 文档变更是否通知 @小米 doc-curator
```

### 5.4 grep v1.2 规则更新

以下 2 条 v1.1 规则精度优化：

- `data_source_ts.*now()` → 改为 `data_source_ts\s*=\s*.*now\(\)` 只匹配赋值语句
- 新增：`kAuditEventTypeCount` 使用处 grep，确认无越界遍历（`i < kAuditEventTypeCount` 与实际 enum 值对应）

---

## Part 6: ADR-009 Sonnet 派单首例评估

**评估对象：** 小卢 E-035-01，SingleInstanceLock，W5-A-09

| 指标 | 值 | 评价 |
|---|---|---|
| 代码行数 | 929 行（hpp + cpp 合计） | 合理 |
| 测试数 | 7 用例（T1-T7） | 覆盖完整 |
| -Wno- 抑制 | 0（仅 gtest 标准抑制）| 达生产标准 |
| P0 红线违例 | 0 | 合格 |
| 回归 | 0（319 全过） | 合格 |

**强项：**
- POSIX RAII 设计规范，flock 选型理由清晰
- fork + pipe 并发测试 (T7) 覆盖 race condition，非常规 IC 不具备此深度
- macOS BSD flock + Linux 兼容性均通过
- SIGTERM async-signal-safe 约束明确

**弱项（已归 P1-03）：**
- `g_lock_fd_for_handler` 未被 `InstallSigtermHandler` 赋值，handler 中 close 分支为死代码

**结论：** Sonnet 4.6 派单可行性证明成立。929 行 production-grade C++ + 7 fork/signal/race 测试，仅 1 处 P1 功能缺陷，0 P0，符合 ADR-009 Sonnet IC 能力预期。

**老胡周报 §6 Model KPI 数据点：**
- E-035-01 / Sonnet 4.6 / 929 行 / 7 测试 / 319 全过 / 0 -Wno- 抑制（生产代码）/ 1 P1（g_lock_fd_for_handler 死代码）/ 0 P0 / 0 回归

---

## 总结

W5 末代码库质量整体健康：

- **P0 清零**，所有红线（R-1/R-7/R-11/R-12/R-20）均有代码级 enforce + 测试覆盖
- **5 P1 发现**，均非阻塞上线，但需 W6 修复：命名歧义（P1-01）、ORDER_BOOKED 缺失（P1-02）、SIGTERM fd 死代码（P1-03）、测试 -Wno 分级（P1-04）、WAL skeleton backlog 确认（P1-05）
- **ADR-009 Sonnet 派单首例合格**，小卢 SingleInstanceLock 达生产级水准
- **v1.2 PR review 升级方向明确**：测试质量分级 sub-gate + model grep + GM 自检模板

派单建议：
- P1-01/02: @老唐 audit schema v1.2 bump（与老李对齐）
- P1-03: @小卢 fix `InstallSigtermHandler` fd 参数
- P1-04: @老高 W6 PR v1.2 CMake 分级守门落地
- P1-05: @老王 W6 Sprint-2 backlog 确认 SPSC ring 时间线

---

**汇报完成**："代码质量审计 v1 + grep 实跑 / 6 模块 spot check / 3 测试 sample / P0/1/2 分类 / PR v1.2 升级"
