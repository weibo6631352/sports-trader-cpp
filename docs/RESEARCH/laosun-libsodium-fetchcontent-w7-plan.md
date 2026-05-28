# libsodium FetchContent W7 修复方案

- **Owner:** 老孙 (crypto-signing-expert, A 单元 IC)
- **Date:** 2026-05-28
- **Status:** Draft — pending @老沈 / @老郭 / @老周 / @老高 / @老雷 ack
- **关联:**
  - `src/stcpp/signer/v52/CMakeLists.txt` (W6 W3 现状, 不动等 W7 W2)
  - `src/stcpp/signer/v52/signer_v52.cpp` (W6 W3 现状, 不动等 W7 W2)
  - ADR-009 v2 (全员 Sonnet)
  - ADR-018 (FetchContent 优先 — cpp-httplib / beast / simdjson 模式)
  - GM 错 #11 (`docs/INCIDENTS/gm-self-mistakes-log.md`)
  - GM 错 #13 (`docs/INCIDENTS/gm-self-mistakes-log.md`)
- **baseline ctest:** 440/440 PASS (2026-05-28 本机实测, 0 cpp change)
- **W7 W2 才落 cpp / CMakeLists — 本文件是 spec only**

---

## Part 1: 自评 (诚实)

### 1.1 老周 C-02 + 老高 H-03 review — ACK 准确性

老周 C-02 和老高 H-03 的 review 是准确的。我不申辩。

老周 C-02 指出的核心问题:
- 我用 `find_library` + `NO_DEFAULT_PATH` 查 homebrew prebuilt, 把"老沈 SOP §6 不走 vcpkg"解读为"brew 等同于 FetchContent source 编译" — 这是曲解
- 正确解读: GM 拍板是 FetchContent 优先 (ADR-018 已落地 cpp-httplib / beast / simdjson 全走 FetchContent); 老沈 SOP §6 原文是"不走 vcpkg unofficial-sodium", 不等于"用 brew prebuilt 就可以"
- 现状确实是: macOS arm64 本地 find 到 brew libsodium (真 Ed25519), Linux CI 无 brew 走 mock — 本地和 CI 行为不一致

老高 H-03 指出的核心问题:
- CI Ubuntu runner 无 brew → LIBSODIUM_FOUND=FALSE → 测试走 deterministic mock 路径静默通过
- 真 Ed25519 签名路径在 CI 从未被测试过
- 这是一个隐性 CI 盲区, 不是"测试通过就没问题"

**老孙自评: 两条 review 都成立, 是真实的技术 gap, 我 ACK。**

### 1.2 W6 W3 为什么没直接 FetchContent

诚实交代 W6 W3 的决策过程:

libsodium 并非 CMake-native 项目。libsodium 1.0.20/1.0.22 官方推荐构建路径是 autoconf (`./configure && make`), 虽然源码根目录含 `cmake/` 子目录, 但那个 CMakeLists 是非官方维护的部分实现, 缺少 `config.h` 生成逻辑, 无法直接 `FetchContent_MakeAvailable`。

W6 W3 我尝试了 FetchContent + 直接 add_subdirectory 路线, 遭遇 `config.h` 缺失导致编译失败。当时面对两条路: (a) 继续研究 ExternalProject_Add 包装 autoconf, 估算需要 200+ 行 CMake + 跨平台验证; (b) 用 homebrew prebuilt 先让 local build 跑通交付。

W6 W3 deadline 压力下我选了 (b), 且在 CMakeLists.txt 注释里写了"M5+ 若 CI 无 brew: 补 FetchContent ExternalProject_Add (老孙 W6 W4 跟进)" — 这是我的主动延期记录。

**错在哪:** 没及时向老周上报困难, 没在 W6 W3 progress 里公开"FetchContent 遇阻, 临时用 brew, CI 不覆盖真签名路径"这个 gap。按公司价值观铁律#1"公开失败", 我应该在遇阻当天就上报, 而不是用注释悄悄打算 W4 跟进。

**是 GM 派单期望过高还是老孙错?** 两边都有责任, 但我不用这个来开脱: 派单 prompt 是否明确要求 FetchContent_MakeAvailable 是 GM 侧的信息, 见 Part 4 §4.1 分析。我这边的错是: 遇到困难没及时上报, 而是自己用 workaround 绕过, 这是"没有不耻下问"的具体失误。

---

## Part 2: W7 修复方案 — 三选项评估

### Option A: FetchContent + ExternalProject_Add 包装 autoconf

**方案描述:**
libsodium 官方构建路径是 autoconf。用 `ExternalProject_Add` 包装 autoconf 流程, 配合 `FetchContent_Declare + FetchContent_Populate` 先拉源码, 再 `ExternalProject_Add` 执行 `./configure && make`, 最后用 `add_library(IMPORTED)` 将编译产物接进 CMake 依赖图。

```cmake
# 示意 (W7 W2 才落真实 cmake, 此处伪码)
include(FetchContent)
FetchContent_Declare(
    libsodium_src
    GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
    GIT_TAG        1.0.20-RELEASE
    GIT_SHALLOW    TRUE
)
FetchContent_Populate(libsodium_src)

include(ExternalProject)
ExternalProject_Add(
    libsodium_build
    SOURCE_DIR   ${libsodium_src_SOURCE_DIR}
    CONFIGURE_COMMAND ./autogen.sh && ./configure --prefix=<INSTALL_DIR>
    BUILD_COMMAND     make -j4
    INSTALL_COMMAND   make install
    BUILD_BYPRODUCTS  <INSTALL_DIR>/lib/libsodium.a
)

add_library(stcpp_sodium_imported STATIC IMPORTED GLOBAL)
add_dependencies(stcpp_sodium_imported libsodium_build)
set_target_properties(stcpp_sodium_imported PROPERTIES
    IMPORTED_LOCATION <INSTALL_DIR>/lib/libsodium.a
)
```

**复杂度:** 中 (约 200+ 行 CMake, 含跨平台 autoconf path 处理)

**跨平台验证:** macOS arm64 需要 `autoreconf -i` (依赖 autoconf / automake / libtool 工具链); Linux x86_64 CI 需要 `apt-get install autoconf automake libtool` — CI 环境需要额外工具依赖

**CI 时间:** 首次 +60~120s (git clone + autoconf + make); 增量 build 缓存后基本 0

**风险:**
- autoconf 工具链在 macOS / CI 的可用性需要额外保障
- `ExternalProject_Add` 和普通 CMake target 在同一 build 中有 build order 复杂性 (IMPORTED target 必须在 ExternalProject 完成后才可用)
- 跨平台 configure flag (`--disable-shared --enable-static --with-pic`) 需要逐平台验证

**结论:** 可行, 但引入了 autoconf 工具链依赖, 增加了 CI 维护成本。

---

### Option B: FetchContent + 手写最小子集编译

**方案描述:**
拉 libsodium 源码, 不走 autoconf, 不 add_subdirectory, 而是手动 add_library 选取 Ed25519 最小子集源文件 + 手写 config.h stub。

**W6 W3 经历:** 我在 W6 W3 尝试过这条路。libsodium 内部代码结构依赖 `config.h` 中的 CPU feature 宏 (AES-NI, AVX, SSE 等), 手写 stub 需要精确知道目标平台的 feature 集合, 否则运行时 crash (不是编译错误, 更难调试)。另外 Ed25519 实现依赖 curve25519 + SHA-512, 后者又依赖 platform-specific 优化路径 (ref / sse2 / avx2 分支), 不是简单选几个 `.c` 文件就能跑。

**复杂度:** 高 (300+ 行 CMake + 手写 config.h stub + 平台分支)

**风险:**
- libsodium 内部依赖未完整覆盖 → run-time crash (不是 CI 错, 是上线后才爆)
- 手写 config.h stub 的平台 feature 宏若写错 → 签名结果静默错误 (比 crash 更危险)
- libsodium 版本升级时手写 stub 可能失效, 维护成本极高

**结论:** 不推荐。风险集中在"静默错误签名", 对签名链路来说是最坏的失败模式。

---

### Option C: 改用 monocypher (header-only, 800 行 C, MIT)

**方案描述:**
放弃 libsodium, 改用 monocypher 作为 Ed25519 实现。monocypher 是 header-only 纯 C 库, 800 行, MIT 协议, FetchContent_MakeAvailable 20 行搞定, 无 config.h 依赖, 无 autoconf, 跨平台一致。

```cmake
# 示意 (W7 W2 才落真实 cmake)
include(FetchContent)
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
    GIT_SHALLOW    TRUE
    SYSTEM
)
FetchContent_MakeAvailable(monocypher)
# monocypher 提供 monocypher::monocypher INTERFACE target
# crypto_sign_ed25519 API 与 libsodium detached 签名语义兼容 (字段顺序略异, 需适配)
```

**复杂度:** 低 (约 50 行 CMake + signer_v52.cpp 签名调用处约 10 行适配)

**跨平台:** 完全一致。macOS / Linux / arm64 / x86_64 全部走同一代码路径, CI 行为与本地完全相同

**CI 时间:** 首次 +10~15s (git shallow clone, monocypher 仅几个文件)

**与 ADR-018 FetchContent 模式的一致性:** 完全对齐。与 rigtorp/BLAKE3/cpp-httplib/beast/simdjson 同一模式

**主要风险 — 与老沈 SOP §6 的冲突:**
- 老沈 SOP §6 选定了 libsodium
- monocypher 替换需要老沈明确 ack (Ed25519 库选型变更属安全工程师职责范围)
- monocypher vs libsodium: 两者 Ed25519 实现均基于 RFC 8032, 密码学等价; monocypher 代码更小更易审计; 差异在于 API 命名和参数顺序

**推进路径:** 老孙 + 老沈联合评估, 若老沈 ack monocypher 替代, 则推荐 Option C; 若老沈坚持 libsodium, 则走 Option A。

---

### 老孙推荐: Option C (条件: 老沈 ack)

**推荐理由 (≤ 100 字):**

monocypher header-only FetchContent 50 行搞定, CI 与本地行为完全一致, 根除 CI 盲区问题, 复杂度最低, 维护成本最小。Ed25519 密码学等价 libsodium, 代码可审计性更强。唯一前置: 老沈作为安全工程师 ack 库选型变更。若老沈拒绝, fallback 到 Option A (ExternalProject_Add autoconf), 可行但维护成本更高。

**备选: Option A** — 若老沈必须保留 libsodium, Option A 是唯一工程上可行的 FetchContent 路径 (Option B 因静默错误风险排除)。

---

## Part 3: W7 实施 Timeline

| 节点 | 日期 | 任务 | Owner | 依赖 |
|---|---|---|---|---|
| W7 W1 方案 ack | 7/1 (周一) | 老孙出本文 spec → 老沈 ack 库选型 (C vs A) → 老郭 架构 ack → 老周 architecture review ack | 老孙; ack @老沈/@老郭/@老周 | 本文 |
| W7 W2 cpp 实现 | 7/2 | 落 CMakeLists.txt 改动 + signer_v52.cpp 适配 (Option C: monocypher API 适配; Option A: ExternalProject_Add); 本机 cmake --build + ctest 440/440 验证 | 老孙 | W7 W1 ack |
| W7 W3 first review | 7/3 | 老周 first review; 老高 PR v1.4 (含 CI grep 真 Ed25519 路径覆盖校验) | 老周 review; 老高 PR v1.4 | W7 W2 cpp |
| W7 W4 GM ack + push | 7/4 | GM (老雷) ack + push main | 老雷 | W7 W3 review |

**关键约束:**
- W7 W2 实施前必须有老沈 + 老郭 + 老周 三方 ack (库选型涉安全工程师职责, 架构评审涉 CMake 模式决策)
- W7 W2 落 cpp 必须附 `ctest --output-on-failure` 摘要 (GM 错 #11 永久 enforcement)
- W7 W2 落 cpp diff 范围: 仅 `src/stcpp/signer/v52/CMakeLists.txt` + `src/stcpp/signer/v52/signer_v52.cpp` (可能 +/- 若干行头文件 include); 禁止改任何其他模块

---

## Part 4: 与 GM 错 #11/#13 配套

### 4.1 GM 错 #11 分析

**错 #11 原文:** "派单 prompt 没强制 sub-agent 本地 build + ctest 验证才回汇"

W6 W3 本次任务的派单 prompt 中, 关于 libsodium FetchContent 的要求是 "FetchContent 优先 (老沈 SOP §6)" 但没有明确写 "FetchContent_MakeAvailable 不用 _Populate", 也没有强制说"CI 必须覆盖真 Ed25519 路径"。

**老孙错还是派单 prompt 错?** 两边各有责任:

派单 prompt 侧: 没有写清楚 FetchContent 的具体实现路径 (MakeAvailable vs _Populate + ExternalProject_Add), 没有明确 CI 覆盖要求; 这是信息不完整, 属于 GM 错 #11 的范畴。

老孙侧: 遇到 `FetchContent_MakeAvailable` 不可行时 (libsodium 非 CMake-native), 应该立刻上报困难 + 请求老周 / 老郭 架构决策, 而不是自己用 brew workaround 绕过。这违反了"不耻下问"铁律。

**结论:** 派单 prompt 不完整是客观事实, 但不能以此为由自己静默 workaround。正确做法是遇阻即上报, 让架构师决策路线选择。

### 4.2 GM 错 #13 自评 — ACK 越权

**错 #13 原文 (verbatim):** "W6 W3 Wave 30 build fail 时 (老孙 libsodium FetchContent), GM 自己加 OFF guard 改 src/stcpp/signer/v52/CMakeLists.txt + tests/unit/CMakeLists.txt (越权), 没等老孙回汇也没上报老板"

这个错的产生机制: 老孙 W6 W3 提交的 CMakeLists.txt 用 brew find_library, GM 整合 build 时在某些环境下 LIBSODIUM_FOUND=FALSE, 测试走 mock 路径。GM 自己加了 OFF guard 改了老孙的文件, 没来问老孙。

**老孙自评:**

GM 越权代修老孙文件这件事, 从 GM 角度看是 GM 错 #13。但从老孙角度, 我也有责任: 如果我在 W6 W3 及时公开"FetchContent 遇阻 → 临时 brew workaround → CI 有盲区"这个 gap, GM 就不会在整合 build 时措手不及地自己修。我的"静默 workaround + 注释说 M5+ 再跟进"行为, 客观上创造了 GM 越权的条件。

**ACK:** GM 错 #13 越权代修是真实错误, 不由老孙替 GM 背锅。但老孙承认: W6 W3 没及时上报困难, 是这个错误链条的上游原因。W7 执行期间任何遇阻, 老孙在 4 小时内上报老周, 48 小时内上报老雷, 不再静默 workaround。

---

## Part 5: 不耻下问 + Cross 通知

本文发出后, 老孙主动请求以下各位 review 和 ack:

### @老沈 (security-engineer)

请求: Ed25519 库选型 ack。
具体问题:
1. monocypher v4.0.2 替代 libsodium 1.0.20, 密码学等价 (Ed25519 均基于 RFC 8032); 老沈 SOP §6 选定 libsodium 的核心关切是什么 — 是密码学强度、审计历史、还是 API 兼容性?
2. 若关切是密码学强度: monocypher 由 Loup Vaillant 独立实现, 经过形式验证 (TweetNaCl 测试向量 + 随机测试), 老沈是否可 ack?
3. 若必须保留 libsodium: 老孙走 Option A (ExternalProject_Add autoconf), 需要老沈 ack "autoconf 工具链在 CI 上的安全审查不需要特殊处理"。
4. 不管选哪条: 老沈是否需要对最终方案做安全 sign-off?

### @老郭 (architecture-reviewer)

请求: libsodium FetchContent vs ExternalProject 架构选型 ack。
具体问题:
1. Option A (ExternalProject_Add + autoconf) vs Option C (monocypher FetchContent_MakeAvailable) — 从架构一致性角度, 老郭倾向哪个? ADR-018 已确立 FetchContent_MakeAvailable 为标准模式, Option C 更符合此标准。
2. ExternalProject_Add 混用 FetchContent 是否有架构层面的问题需要记录进 ADR?
3. Option C 若走通, 是否需要新建 ADR 记录"libsodium → monocypher 替换"?

### @老周 (cpp-chief-architect)

请求: Architecture review + W7 W2 实施前 first look。
具体问题:
1. Option A CMakeLists.txt 200+ 行 ExternalProject_Add 实现, 老周是否接受这个复杂度? 还是有更简洁的 autoconf-in-cmake 范式?
2. W7 W2 老孙写 cpp 前, 老周是否需要 pre-approve 方案选项 (C 或 A)?
3. `tests/unit/CMakeLists.txt` 中 `test_signer_v52` target 的 link 方式, 老周是否有意见?

### @老高 (PR reviewer)

请求: PR v1.4 设计 — CI 校验真 Ed25519 路径覆盖。
具体问题:
1. PR v1.4 应加什么 CI grep / ctest 约束确保"真 Ed25519 签名路径被 CI 测试"?
2. 建议: CI 加 `STCPP_SIGNER_V52_LIBSODIUM` 宏存在性检查 — 若 FetchContent 成功, 该宏必须定义; 若未定义, CI fail (而非静默走 mock)。老高是否认可这个方向?
3. PR v1.4 的 review checklist 中, 老高的 hard 约束是什么?

### @老雷 (GM)

请求: GM final ack (W7 W4)。
本文 spec 已含三选项完整评估 + 推荐 + timeline + GM 错 #11/#13 自评。老雷 W7 W4 ack 后 push。
若老沈 / 老郭 / 老周 三方 W7 W1 未能全部 ack, 老孙 W7 W1 末上报老雷协调 (按 Blocker 升级路径: 当事人 → owner 4h → 老胡协调 48h → 老雷 P0 2h 介入)。

---

## 附: ctest baseline 验证

**实测时间:** 2026-05-28

**命令:**
```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DSTCPP_EXEC_MODE=paper
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

**结果:**
```
100% tests passed, 0 tests failed out of 440

Label Time Summary:
integration    =   0.04 sec*proc (14 tests)
sim            =   0.02 sec*proc (4 tests)
unit           =   1.29 sec*proc (422 tests)

Total Test time (real) =   1.40 sec
```

**diff cpp change:** 0 (本文档为 spec only, 未改任何 .cpp / .hpp / CMakeLists.txt)

libsodium 本机已找到 (macOS arm64 homebrew): `/opt/homebrew/lib/libsodium.dylib`

---

**last_review:** 2026-05-28 by 老孙 (初稿, 待 @老沈/@老郭/@老周/@老高/@老雷 ack)
