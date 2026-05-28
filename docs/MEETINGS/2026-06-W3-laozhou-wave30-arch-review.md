# Wave 30 联合架构 + 兼容性 Review

- **owner:** 老周 (cpp-chief-architect, A 主管)
- **联合 reviewer:** 老高 (代码质量, 老周架构面 + 老高质量面联合)
- **date:** 2026-06-W3 (2026-05-28 参考日)
- **last_review:** 2026-06-W3 by 老周
- **scope:** W6 Wave 30 8 IC 并行交付 — 兼容性代码扫描 + 架构破坏审查
- **baseline:** ctest 440/440 PASS (review only, diff 0 add 0 del 0 change)
- **上报链:** P0 → 老雷(GM) + 老板; P1 → GM + 各 owner; P2 → owner 自排

---

## 关键背景

GM 错 #11 + #12 + #13 连续爆发, W6 W2 GM 越权代修 10+ 处别人代码, W6 W3 Wave 30
8 IC 并行时 tests/unit/CMakeLists.txt 被 4 方同时改动 (小卢 / 老孙 / 小田 / GM 越权). 本次 review
严守 GM 错 #13 教训: 老周 review only, 不修代码, 不自己 hotfix, 发现问题上报 GM + 老板.

---

## Part 1: 兼容性代码扫描 (8 项)

### C-01 小卢 STCPP_TEST_BUILD variant
**文件:** `src/stcpp/infra/process/CMakeLists.txt` (L44-64)
**描述:** 生产 target `stcpp_process_lock` (STCPP_PID_DIR 编译期 build-time invariant) + 测试专用 target `stcpp_process_lock_test` (STCPP_TEST_BUILD=1, path_for() 走 per-process /tmp/stcpp_test_<PID>).

**架构师判断: ACCEPT (条件)**

理由:
- `STCPP_TEST_BUILD` 是编译期宏, 不是运行期 hack. 生产 binary 链接 `stcpp_process_lock` (无此宏), R-7 build-time invariant 完整守住. 测试 binary 链接 `stcpp_process_lock_test`, 是独立编译单元.
- `STCPP_TEST_PID_DIR` 是 runtime env override, 但仅在 `#ifdef STCPP_TEST_BUILD` 块内生效. 生产代码 zero exposure.
- ctest -j 并发进程 PID file 隔离的问题是真实存在的 (T7 跨进程抢占测试必须用统一 dir; T1-T6 必须隔离). 这个设计解决了真实问题.
- single_instance.cpp 中 path_for() 内部的 #ifdef 结构干净, 两个代码路径完全分离.

**条件:**
- C-01-a (P2): `stcpp_process_lock_test` 是第二个编译 `single_instance.cpp` 的目标, 如果未来 single_instance.cpp 变化, 两 target 必须同步. 建议在 CMakeLists.txt 注释中强调"任何修改 single_instance.cpp 必须同时 review stcpp_process_lock_test 的 defines 是否仍正确".
- C-01-b (P2): 主 binary `stcpp_paper` 链 `stcpp_process_lock` (非 test 变体) 已在 `src/stcpp/bin/CMakeLists.txt` 确认. 正确.

### C-02 老孙 libsodium 接入策略
**文件:** `src/stcpp/signer/v52/CMakeLists.txt`
**描述:** 老孙放弃 FetchContent git clone, 改用 `find_library` + `find_path` 找 homebrew prebuilt (/opt/homebrew/lib, /usr/local/lib, /usr/lib). 找不到时走 deterministic mock 路径, 测试仍然通过.

**架构师判断: P1 — 兼容性 hack, W7 必修**

问题如下:

**P1-01 违反 "FetchContent 优先" 决策原则 (GM W6 W2 拍板):**
老沈 SOP §6 明确"不走 vcpkg unofficial-sodium", 老孙解读为 "brew prebuilt = source 同等效果" — 这是曲解. GM 拍板是 "FetchContent 优先, 与 BLAKE3/rigtorp 一致", 意思是用 FetchContent 拉官方 libsodium source 编译, 不是系统库检测. CMakeLists.txt 注释本身也承认"M5+ 若 CI 无 brew: 补 FetchContent ExternalProject_Add". 这说明老孙自己知道当前策略 CI 不可移植, 但选择了 defer.

**P1-02 跨平台可移植性缺口:**
- macOS arm64 (本地开发, /opt/homebrew): 当前能找到 libsodium, 测试有真 Ed25519
- Linux x86_64 CI / 生产节点: 没有 brew, /usr/local/lib 取决于 apt 是否安装 libsodium-dev; NO_DEFAULT_PATH 限制了 /usr/lib/x86_64-linux-gnu 等标准 Linux 路径
- 结果: CI 大概率走 mock 路径, 本地走真 Ed25519 — **本地 CI 行为不一致**, 违反"同一代码库本地和 CI 行为一致"原则

**P1-03 mock fallback 掩盖真实问题:**
找不到 libsodium 时自动降级为 deterministic mock 并让测试通过 — 这是典型兼容性 hack. T1-T7 在无 libsodium 环境下通过的, 实际是在测试 mock 而非真实 Ed25519. 未来 live 路径接入时可能暴露真 sodium API 使用方式错误.

**派回原 owner:** 老孙 W7 必修: 按 GM 原决策改为 FetchContent ExternalProject_Add libsodium (官方 source 编译), 与 BLAKE3 / rigtorp 模式一致. mock fallback 作为 CI 无网络时的 last resort 可保留, 但不能作为主策略.

### C-03 老孙 single_instance.cpp "顺手修" (已被 overwrite)
**状态:** 已被小卢 owner 版本覆盖. git log commit a8afebe 包含整合后版本, 小卢 owner 版本 wins.
**架构师判断: 教训事项, 非现存代码问题**

当前 single_instance.cpp 内容干净 (老高 P1-03 修 g_lock_fd_for_handler dead code + 老何 FdGuard RAII 均已体现). 但流程问题需要记录:
- 老孙没有 single_instance.cpp 的 owner 权限, 不应在 signer 派单中顺手改 process 模块
- GM 错 #13 之前派单 prompt 没明确"严禁动非 owner 文件" — 制度漏洞
- 教训: 派单 prompt 必须显式列出"本次允许修改的文件列表", 列表外文件如需改动必须先向 owner 申请

### C-04 tests/unit/CMakeLists.txt 4 方改重叠
**文件:** `tests/unit/CMakeLists.txt` (774 行)
**涉及方:** 小卢 (test_single_instance / test_position_ledger) + 老孙 (test_signer_v52) + 小田 (parquet_writer 相关) + GM 越权 (已 revert)
**架构师判断: 当前 ACCEPT, 流程 P1**

读取结果分析:
- if/endif 配对: 检查所有条件块 — STCPP_EXEC_MODE "paper" 对应 3 个 if/endif (test_paper_pm_client, test_ml_hook, test_signer_v52), STCPP_BUILD_CLI 1 个 if/endif. 全部配对正确, 无悬空 if.
- Dead code: `test_strategy_unlock` 在 STCPP_BUILD_CLI=OFF (default) 时不编译 — 这是已知 guard, 非 dead code. 其余 target 全部无条件编译.
- 当前 774 行文件功能完整, 无 dead target.

**P1-04 (流程): 4 方并发改同一文件是流程红线:**
tests/unit/CMakeLists.txt 是高争用文件 (每个新测试 target 都改). Wave 30 中 4 方并发改此文件靠"last write wins"解决, 这是侥幸. 必须在派单前明确文件 ownership 矩阵, 告知所有 wave 内 IC.

### C-05 小田 parquet_writer stub ABI 锁
**文件:** `src/stcpp/data/parquet_writer.cpp` + `include/stcpp/data/parquet_writer.hpp`
**架构师判断: ACCEPT (stub 设计合理), P2 (ABI 兼容性待确认)**

当前 stub 接受 `FeatureSnapshot + TrainingLabel` 和 `MultiBookOddsRecord`, 做 R-20 ts chain check, flush_to_file() 返回 StubNotImplemented.

**设计正面:**
- flush_to_file() 的 stub 行为 (clear buffer, 不写磁盘, 返回 StubNotImplemented) 是正确的 stub 模式
- R-20 ts chain check 在 stub 中已执行 (ts_violations_ 计数)
- W7+ HC-06 入职接 Apache Arrow 时只需替换 flush_to_file() 实现, .hpp ABI 不变 — 设计意图正确

**P2-01 ABI 兼容性 (待确认):**
- `FeatureSnapshotRecord::from_snapshots()` 在 stub 中调用, 但 `FeatureSnapshot` 结构是小邓 ML hook 32 feature 字段锁定的. 需要确认 HC-06 入职时接 Apache Arrow 写 parquet schema 时, Arrow schema 字段顺序与 FeatureSnapshot 字段顺序对齐 (BLAKE3 hash chain 字段位置, 4 ts 位置)
- `kBookmakerIds[0..7]` ABI 在 parquet_writer.cpp 中硬编码为注释 (8 家顺序), 但实际 slot 填充走 `rec.slots[i]` index. ADR-008 8-9 家 bookmaker ABI 需要小田和小段联确认 slot index 与 goalserve_schema.hpp 中 `kNumBookmakers` / bookmaker 排列一致

### C-06 老唐 BLAKE3 AuditEmitterPool friend class
**文件:** `include/stcpp/observability/audit_emitter.hpp` L120-121
**描述:** `AuditEmitter` 类声明 `friend class AuditEmitterPool`. 背景: GM 错 #11 hotfix 中加入, 理由是 "AuditEmitterPool 需访问 build_record / write / ts_chain_ok / apply_hash_chain".

**架构师判断: P1 — friend 设计存疑, 应 review 是否可重构**

friend class 是 C++ 中已知的封装破坏手段. 本 case 的判断:

**支持保留 friend 的理由:**
- `AuditEmitterPool` 与 `AuditEmitter` 是同一模块 (observability/audit_emitter.hpp) 内的配套类, 不是跨模块访问
- pool 需要在持 chain_mutex_ 时调用 `apply_hash_chain`, 这要求 pool 能访问 emitter 内部方法. 如果把 apply_hash_chain 改为 public, 则外部任意代码都可以破坏 hash chain 序列 — 这比 friend 更危险
- 整个 pool 设计目的就是"代理 5 个 emitter 的 chain 更新", 紧耦合是设计选择, 不是偷懒

**质疑点:**
- `build_record` / `write` / `ts_chain_ok` 是否真的需要 private? 还是可以改为 protected 或提供更窄的 friend 接口?
- GM 加 friend 的方式 (错 #11 hotfix) 没有经过老唐 review. 老唐 W6 W3 是否已正式 ack 这个 friend 设计?

**P1-05: 老唐需正式 ack:**
- 老唐必须在 W7 对此 friend 声明出具正式 ack (确认是设计意图, 而非 GM 误加). 如果老唐认为 friend 不合理, 提出重构方案 (如: 提取 `IAuditEmitterInternal` 接口).
- 当前功能正常, 不阻 commit. P1 级: W7 老唐 ack 或重构方案落地.

### C-07 跨 Wave ABI cascade — VirtualFill sizeof=120
**文件:** `include/stcpp/execution/virtual_matcher.hpp` L98-99
**描述:** W6 W2 小蒋加 market_id (array<char,32>) + outcome (uint8_t), sizeof=120 static_assert 锁定.

**架构师判断: ACCEPT — W6 W3 各 IC ABI 一致性确认**

扫描结果:
- tests/unit/CMakeLists.txt 中 test_virtual_matcher / test_ml_hook / paper_e2e_smoke 均链 stcpp_execution_paper, 链路正确
- test_position_ledger (老王) 链 stcpp_infra_wal + stcpp_process_lock_test, 不直接用 VirtualFill — 老王已切 W6 W2 新版 WAL
- test_signer_v52 (老孙) 不直接依赖 VirtualFill, 依赖链 stcpp_signer_v52_paper -> stcpp_infra_wal, 正确
- test_ingest_raw (小冯) 链 stcpp_ingest_raw_writer, 不依赖 VirtualFill

**风险点 (P2-02): ML hook FeatureSnapshot 与 VirtualFill 的字段对齐**
- test_ml_hook 链了 stcpp_ml + stcpp_execution_paper. FeatureSnapshot 32 feature 中部分字段源自 VirtualFill (market_id, outcome, fill_ts_ns 等). W6 W2 VirtualFill 加字段后, 小邓 ML feature column index 4/5 是否已更新? test_p0_01_goalserve_devig 注释说"feature_snapshot_id ABI 不动 (小邓 ML column index 4/5 锁死)" — 需要小邓确认 W6 W2 VirtualFill 变化没有 shift column index.
- 当前 440/440 PASS, ABI static_assert 都通过, 无立即问题. P2 级追踪.

### C-08 Build-time switch 复杂度 (整体系统性问题)
**涉及开关:**
- `STCPP_EXEC_MODE` (live/paper/backtest) — 顶层, 三值, 核心
- `STCPP_BUILD_BENCH` (OFF) — 性能测试
- `STCPP_BUILD_CLI` (OFF) — 老沈三签 CLI
- `STCPP_BUILD_SIGNER_V52` (ON by default) — 老孙 signer v52
- `STCPP_TEST_BUILD` (编译期 define, 非 CMake option) — 小卢测试变体
- 内部 defines: `BLAKE3_REAL`, `STCPP_EXEC_MODE_paper=1`, `STCPP_SIGNER_V52_LIBSODIUM`

**架构师判断: P1 — build 系统开关数量已到预警线, 需要 ADR-019 规范**

当前 5 个 CMake option + 1 个 PRIVATE define (STCPP_TEST_BUILD) 的组合矩阵:
- EXEC_MODE(3) × BUILD_CLI(2) × BUILD_SIGNER_V52(2) = 12 组合, CI 只测 paper mode
- STCPP_TEST_BUILD 是编译期宏, 不出现在 CMake option 列表, 文档中难以发现

**P1-06 (ADR-019 候选):** 需要明文规定: build-time switch 总数上限, 每个 switch 的 CI 覆盖要求, "编译期 define" (STCPP_TEST_BUILD 这类) 必须在 CMakeLists.txt 顶部注释中列出. 详见 Part 5 拍板建议.

---

## Part 2: 架构破坏审查 (6 项)

### A-01 v0.6/v0.7 架构与 Wave 30 actual code 一致性
**判断: 基本一致, 局部漂移 P1**

一致的部分:
- 5 模块分层 (infra/execution/risk/strategy/observability) 依赖方向正确, 无循环
- R-11 4 WAL 物理隔离: paper.pid 与 live.pid 通过 STCPP_PID_DIR / STCPP_TEST_BUILD 隔离, PositionLedger WAL prefix 通过 STCPP_POSITION_WAL_PREFIX 分离, ingest_raw_writer WAL prefix 通过 STCPP_INGEST_RAW_WAL_PREFIX 分离
- R-12 event loop: 当前 tests/sim/r12_sim 存在, 4 ts 链路完整
- R-20 4 ts: 所有新增模块 (signer_v52 / ingest_raw_writer / parquet_writer) 均有 ts chain check

局部漂移:
- Wave 30 新增了 `stcpp_signer_v52_paper` / `stcpp_signer_v52_sodium` / `stcpp_process_lock_test` 3 个子 lib. 这些在 v0.6 架构图中没有对应条目. 属于实现细节级扩展, 不改变模块层次, 但模块依赖图需要更新.
- `stcpp_data_parquet` (parquet_writer.cpp) 依赖 `ml::FeatureSnapshot` + `data::goalserve::MultiBookOddsRecord` — 引入了 data 模块对 ml 命名空间的依赖. 如果 ml 模块未来有更大变动, data 模块受影响. 这是一个跨层依赖, 需要老郭架构评审时关注.

### A-02 ADR-005 派单 3 层流程破坏
**判断: 流程被 GM 错 #13 实质性违反, 制度需要加固**

ADR-005 规定: GM → 主管 → IC 三层. Wave 30 中:
- Wave 30 8 IC 由 GM 直接派 (绕主管) 是已知情况, 属于"老板原话允许 GM 并行派 IC 处于起步期" — 暂时接受
- GM 越权 hotfix (错 #13) 是 ADR-005 "build fail / 冲突 → GM 暂停整合 + 上报老板"的违反
- 文件 ownership 没有在派单 prompt 中声明 — ADR-005 缺失"文件 ownership lock"子条款

**P1-07:** ADR-005 需要补"文件 ownership lock"子条款. 详见 Part 5.

### A-03 ADR-008 multiplicative de-vig cascade 健康度
**判断: ACCEPT — de-vig 链路完整**

- ADR-008 de-vig 公式落在 strategy 模块 (test_p0_01_goalserve_devig 覆盖 T1-T5)
- 8 家 bookmaker ABI 锁在 goalserve_schema.hpp + odds_record.hpp (小段 W6 W3)
- test_goalserve_schema 的 T5 "8-9 家 bookmaker ABI 锁 (ADR-008)" 已覆盖
- 级联路径: Goalserve → MultiBookOddsRecord → de-vig signal → VirtualFill → PositionLedger WAL 全部有测试覆盖

### A-04 ADR-014 BLAKE3 + ADR-017 SPSC + ADR-018 lib 选型 cascade
**判断: ADR-014 + ADR-017 健康; ADR-018 W7 待接入**

- BLAKE3: stcpp_blake3 (FetchContent) + stcpp_observability_audit 链路正确. CI grep "BLAKE3_STUB" 禁止出现, "BLAKE3_REAL=1" 已注入. 440/440 中 test_blake3_audit 5 test 全过.
- ADR-017 SPSC: stcpp_infra_spsc 存在, test_spsc_queue T1-T7 全过. IngestRaw 2-tier 设计 R-12 合规.
- ADR-018: cpp-httplib / beast / simdjson 尚未接入 (计划 W7). 当前 placeholder 结构已留 (BoostBeastTransport hook). 不是 Wave 30 问题, 不影响当前 440/440.

### A-05 R-7 / R-11 / R-12 / R-20 红线在冲突解决过程中的完整性
**判断: R-7 / R-11 / R-20 完整; R-12 本 wave 未被触碰**

- R-7 (build-time mode): stcpp_paper 主 binary 链 stcpp_process_lock (非 test 变体). test binary 链 stcpp_process_lock_test. stcpp_signer_v52_paper 只在 STCPP_EXEC_MODE="paper" 时 add_library. 正确.
- R-11 (4 WAL 物理隔离): PositionLedger test 用 STCPP_POSITION_WAL_PREFIX="/var/lib/stcpp/paper/position". ingest_raw test 用 STCPP_INGEST_RAW_WAL_PREFIX="/var/lib/stcpp/ingest/paper". 物理隔离 macro 正确.
- R-20 (4 ts): signer_v52.cpp AssertChainTs 严格守住. parquet_writer.cpp ts_chain_ok() 调用正确. 未发现 local now() 替代上游 ts 的情况.
- R-12 (event loop 禁阻塞): Wave 30 未涉及 WSS event loop 代码. stcpp_infra_spsc try_push back-pressure 语义正确. 当前无违反.

**P1-08 (待观察): R-7 cross-contamination 风险**
stcpp_process_lock (生产) 和 stcpp_process_lock_test (测试) 编译同一个 single_instance.cpp. 如果 CMake 缓存在不同 mode 之间没有完全清除, 存在 test variant .o 混入生产 link 的理论风险. 实际 build 系统中两个 target 是独立 STATIC lib, link 时由 CMake 选择, 不会混淆. 但 CI 流程应明确 `-DSTCPP_EXEC_MODE=paper` fresh build 时只链生产 target.

### A-06 模块依赖图变更 — 新增 3 个子 lib
**新增 target:**
1. `stcpp_signer_v52_paper` (老孙) — signer/v52/ 子模块
2. `stcpp_signer_v52_sodium` (INTERFACE wrapper, 老孙) — libsodium 抽象
3. `stcpp_process_lock_test` (小卢) — process/infra 测试变体

**判断: 结构可接受, P2 文档更新**

新增 lib 都在已有模块边界内 (signer/ 和 infra/process/), 没有引入新的跨层依赖. 但架构图 (老周 v0.6 / v0.7) 没有这些 target. 老周本人需要在下次架构图更新时补入.

**P2-03:** 架构依赖图需要在 M1 前更新, 体现 stcpp_signer_v52_* / stcpp_process_lock_test 的位置和依赖关系.

---

## Part 3: 问题分级清单

### P0 — 无 (当前 440/440 全过, 无架构红线违反)

Wave 30 当前代码没有 P0 级架构破坏. GM 错 #13 越权代码已通过"小卢 owner 优先"和 GM revert 解决.

### P1 — 兼容性 hack / W7 必修 (6 项)

| ID | 问题 | 责任人 | 严重度 | 修复路径 | 全体会议? |
|---|---|---|---|---|---|
| P1-01 | 老孙 libsodium: brew prebuilt 策略违反 FetchContent 优先原则, CI 不可移植 | 老孙 (#06) | 中 | W7: 改 FetchContent ExternalProject_Add libsodium; mock fallback 作 last resort 保留 | 否, owner 自修 |
| P1-02 | 老孙 libsodium: NO_DEFAULT_PATH 遗漏 Linux x86_64 标准路径 (/usr/lib/x86_64-linux-gnu), CI mock != 本地真实 Ed25519 | 老孙 (#06) | 中 | P1-01 修后自动解决 | 否 |
| P1-03 | 老孙 libsodium: mock fallback 让无 sodium 环境测试静默通过, 掩盖真实 Ed25519 API 验证缺口 | 老孙 (#06) | 中 | P1-01 修后: CI 必须用真 libsodium; mock 只作明确 CMAKE_OFFLINE 场景 guard | 否 |
| P1-04 | tests/unit/CMakeLists.txt 4 方并发改动无 ownership 声明, 靠侥幸"last write wins" | GM + 老胡 (流程) | 高 (流程) | ADR-005 补"文件 ownership lock"子条款; 派 wave 前 GM 出文件 ownership 矩阵 | 否, ADR 补丁 |
| P1-05 | 老唐 `friend class AuditEmitterPool` 是 GM 错 #11 hotfix 添加, 未经老唐正式 ack | 老唐 (#38) | 中 | W7: 老唐正式 ack 或提出替代重构方案 (提取 IAuditEmitterInternal 接口) | 否, owner ack |
| P1-06 | Build-time switch 数量达预警线 (5 CMake option + STCPP_TEST_BUILD 编译期 define), 无上限规范 | 老周 (提案) + 老郭 (评审) | 中 | ADR-019: build-time switch 上限 + CI 覆盖要求. 详见 Part 5 | 否, ADR |
| P1-07 | ADR-005 缺"文件 ownership lock"子条款, Wave 30 文件抢占是制度漏洞而非个人错 | GM + 老胡 | 高 (流程) | ADR-005 补丁: 派 wave 前 file ownership matrix 强制出具 | 是 (W7 全体) |
| P1-08 | R-7 cross-contamination 理论风险: CI fresh build 没有显式验证主 binary 不链 test variant | 老周 (架构) + 小宋 (CI) | 低 | CI nm 扫描: 验证 stcpp_paper binary 不含 STCPP_TEST_BUILD 符号 | 否 |

### P2 — 可接受但有改进空间 (3 项, W8+)

| ID | 问题 | 责任人 | 建议 |
|---|---|---|---|
| P2-01 | parquet_writer ABI: HC-06 入职时 Arrow schema 字段顺序与 FeatureSnapshot column index 对齐待确认 | 小田 (#24) + 小邓 (#31) | HC-06 入职前由小田 + 小邓联合出 Arrow schema spec, 与 goalserve_schema.hpp slot 顺序比对 |
| P2-02 | VirtualFill W6 W2 加字段后, 小邓 ML feature column index 4/5 是否受影响待正式确认 | 小邓 (#31) | 小邓出"feature column index 不变"正式确认文档, 或更新 column index 定义 |
| P2-03 | 架构依赖图 v0.6/v0.7 未体现 Wave 30 新增 3 sub-lib | 老周 (自有) | M1 前更新架构图, 体现 stcpp_signer_v52_* 和 stcpp_process_lock_test |
| P2-04 | C-01-a: stcpp_process_lock / stcpp_process_lock_test 两 target 改动同步性缺文档提醒 | 小卢 | CMakeLists.txt 加注释提醒联动修改要求 |

---

## Part 4: GM 错 #13 教训巩固 (架构主权视角)

### 4.1 GM W6 W2 代修 10+ 处 — 技术是否合理?

逐项评估 (从可读代码角度):

**技术合理但越权的 (7 处):**
- test_audit_emitter 加 `stcpp_observability_audit` link: 技术正确 (blake3.h include 路径依赖), 但应由老唐自修
- BLAKE3 `BLAKE3_NO_NEON=0` + `-Wno-unused-function`: 技术正确 (Apple Silicon arm64 编译 NEON 需关闭), 但老唐当初遗漏这个平台细节
- `BLAKE3_USE_NEON=0` compile definition: 技术正确, 同上
- stcpp_infra_wal `ingest_raw_writer.cpp` nodiscard 3 处: 技术修复正确, 但小冯应自修
- position_ledger.cpp `ToMarketIdArray` unused: 正确删除 (小蒋 W6 改后 helper 没用), 但应派回小蒋或小卢
- `audit_chain_verify_test` sign-conversion: 正确修, 但属老唐 review 范围

**技术存疑的 (3 处):**
- `friend class AuditEmitterPool` (P1-05): 功能上解决了编译问题, 但设计意图需老唐 ack. GM 在没有理解老唐 pool 整体设计的情况下加 friend — 这是 GM 错 #13 说的"无法把握对方意图".
- signer_v52 CMakeLists.txt OFF guard: GM 理解的"加个 OFF guard 就能绕过 FetchContent 失败"是技术可行的, 但实际老孙的设计意图是 brew prebuilt, 不是 OFF guard. GM 改的和老孙的策略不同, 造成最终版本是老孙的 brew 策略而非 GM 的 OFF guard.
- test_position_ledger EXPECT_NEAR int→double cast: 技术正确, 但属于测试代码范畴, 小卢 / 老王应自修.

**结论:** GM W6 W2 的 10 处 hotfix 大多数技术方向是对的, 但 (1) 没有理解对方设计意图就动手, (2) 剥夺了原 owner 的修复机会和学习机会, (3) "技术对"不等于"可以越权". 即使技术合理, GM 越权的流程错误对团队文化的损害大于技术收益.

### 4.2 文件抢占 prevention SOP 建议

老周架构师视角的建议 (不替 GM 落地, 只出方案):

**建议 SOP: 派 wave 前 File Ownership Matrix (FOM)**

1. GM 在派单 prompt 批次出之前, 列出本 wave 所有预期修改的文件, 每个文件标注本 wave owner
2. 高争用文件 (历史上被多方改过) 红色标注, 要求 wave 内串行处理或明确 merge 策略
3. 高争用文件清单 (当前已知): `tests/unit/CMakeLists.txt` / `tests/CMakeLists.txt` / `include/stcpp/observability/audit_emitter.hpp` / `src/stcpp/infra/wal/CMakeLists.txt`
4. 派单 prompt 中明确: "本次允许修改文件: [列表]. 列表外文件如需修改, 先向 owner 申请, owner ack 后 GM 知情"

**为什么这有效:** 不是依赖 agent 自觉, 而是把文件边界转为 prompt 约束 — 与"persona 拒绝任务"机制同层次.

### 4.3 老孙"顺手修"派单 prompt 约束建议

老孙 signer 派单 prompt 应加入以下约束 (供 GM 参考格式):

```
本次派单允许修改的文件范围:
  - src/stcpp/signer/v52/signer_v52.cpp
  - src/stcpp/signer/v52/CMakeLists.txt
  - include/stcpp/signer/v52/signer_v52.hpp
  - tests/unit/test_signer_v52.cpp
  - tests/unit/CMakeLists.txt (仅添加 test_signer_v52 target 段)

严禁修改 (owner 不是老孙):
  - src/stcpp/infra/process/single_instance.cpp (owner: 小卢)
  - src/stcpp/infra/process/single_instance.hpp (owner: 小卢)
  - 任何其他模块的 .cpp / .hpp

如果在实现过程中发现其他文件需要修改, 先在回汇中说明原因, 等 GM 协调 owner 确认后再动手.
```

---

## Part 5: 给 GM + 老板的拍板建议 (5 项)

### 拍板-1: Wave 30 是否可 commit + push

**建议: 可 commit + push, 附条件**

条件:
1. P1-01/02/03 (老孙 libsodium) 老孙出正式修复计划 (W7 时间表), 不阻本次 push
2. P1-05 (老唐 friend) 老唐正式 ack, 形式: 在 audit_emitter.hpp 对应行加 owner-review 注释, W7 完成
3. 现状 440/440 PASS, 无 P0 架构破坏

**不建议阻 push 的理由:** Wave 30 代码整体质量合格, P1 项均为可追踪改进, 阻 push 对团队士气影响大于技术风险. 以 P1 追踪票 + W7 时间表替代"卡着不 push".

### 拍板-2: W6 W2 GM 越权 10 处 retro audit 是否要做

**建议: 是, 但精简化 (不是全体会, 是书面 retro)**

理由:
- 技术层面: 10 处大多数方向正确, 不需要 revert (revert 成本 > 收益)
- 流程层面: 每处越权的"GM 为什么这么改"需要有记录, 供原 owner 了解设计意图
- 建议: 老雷出一份"W6 W2 GM hotfix 意图说明" (每处一行说明技术意图), 发给对应 owner 过目. 不开会. 3 工作日完成.
- friend class AuditEmitterPool 这条特别要让老唐过目并 ack.

### 拍板-3: ADR-005 是否加"文件 ownership lock"子条款

**建议: 是, 必须加**

**拟加条款 (供 GM 参考):**

```
ADR-005 补丁 §3.4 文件 ownership lock (Wave 30 后新增)

派 wave 前 GM 必须:
1. 列出本 wave 预期修改文件清单 (File Ownership Matrix)
2. 每个文件标注本 wave 唯一 owner sub-agent
3. 高争用文件 (定义: 同一 sprint 被 2+ sub-agent 历史修改) 必须串行处理
4. 派单 prompt 中显式声明"严禁修改清单外文件"
5. 如需临时扩大修改范围, sub-agent 必须在回汇中说明, 等 GM 协调 owner 确认

违反: 不协调擅自修改他人 owner 文件 = P1 流程违规, 进 retro 记录.
```

### 拍板-4: ADR-019 — build-time switch 上限

**建议: 立 ADR-019, 优先级 P1**

**ADR-019 核心内容 (老周提案, 老郭评审):**

问题: 当前 5 CMake option + 1 PRIVATE define (STCPP_TEST_BUILD) 已构成复杂矩阵. 无成文上限.

提案:
- CMake option 上限: 顶层 CMakeLists.txt 中 `option()` 声明不超过 8 个 (当前 5 个, 留 3 个扩展空间)
- PRIVATE 编译期 define (不出现在 option 列表的) 必须在模块 CMakeLists.txt 顶部注释中列出
- 新增 build-time switch 必须经架构评审 (老郭) 过目后才能落地
- CI 必须覆盖所有 on/off 组合中的高危组合 (至少: paper/live/backtest × default options)
- STCPP_TEST_BUILD 类型的"隐式"编译期 define 必须改为: 在对应 CMakeLists.txt 中显式 documented

### 拍板-5: W7 W3 是否设"冷静周"

**建议: 是, 但重新定义"冷静周"内容**

老周建议 W7 W3 的工作重点:
1. 老孙 P1-01/02/03 修复 + 本地 ctest 验证 (当周交付)
2. 老唐 P1-05 ack 文档 (1 个工作日)
3. ADR-005 §3.4 补丁落地 + 老胡出 Wave 31 前 FOM 模板
4. ADR-019 草案 (老周提案 + 老郭一轮评审)
5. 暂停新功能 wave, 只做以上 4 项 + P1 追踪

**不建议的做法:** 全面 freeze 停止所有工作. Wave 30 P1 项可以由原 owner 局部修复, 不影响其他 IC 正常工作.

---

## 附录: ctest 基线确认

```
review 执行方式: read only (0 代码改动)
build 命令:     cmake --build build
结果:           ninja: no work to do (0 rebuild — 无代码变更)
ctest 命令:     ctest --test-dir build -j 1 --output-on-failure
结果:           100% tests passed, 0 tests failed out of 440
Label 分布:     unit=422 / integration=14 / sim=4
diff stat:      0 files changed, 0 insertions, 0 deletions
```

**老周 (cpp-chief-architect) 签字:**
Wave 30 联合 review v1 完成. 8 兼容性扫描 + 6 架构审查 + P0(0) P1(8) P2(4) 三级问题分类 + GM 错 #13 教训巩固 + 5 拍板建议. Diff 0 change (review only).

上报: 老雷 (GM) + 老板. P1-04/P1-07 (文件抢占流程) 建议 W7 全体会讨论. P0=0, Wave 30 可 push.
