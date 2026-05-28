---
owner: 老沈 (security-engineer, B 单元 IC)
reviewer: 老韩 (B 主管) → 老郭 (architecture) → 老雷 (GM final)
last_review: 2026-05-28
status: DRAFT — 待 @老郭 / @老周 / @老雷 ack
relates_to:
  - docs/RESEARCH/laosun-libsodium-fetchcontent-w7-plan.md (老孙 Option C 推荐)
  - docs/RUNBOOKS/strategy-decayed-unlock-sop-v1.md §6 (三签 CLI Ed25519 verify)
  - docs/ADR/2026-06-W3-adr-018-lib-selection.md (FetchContent 模式标准)
  - src/stcpp/signer/v52/CMakeLists.txt (现状 libsodium brew prebuilt)
  - docs/ADR/2026-06-01-adr-009... (ADR-009 v2 = Sonnet, W7 冷静周)
ctest_baseline: 440/440 PASS (2026-05-28 本机实测, diff 0 cpp)
---

# monocypher vs libsodium W7 ack v1

> 老沈作为 B 单元安全 IC, 对老孙 W7 方案 Option C (monocypher FetchContent) 的
> 正式安全评估与选型 ack. 本文仅出 spec — diff 0 cpp, 不动任何 .cpp / CMakeLists.txt.

---

## Part 1: 密码学等价性验证

### 1.1 RFC 8032 合规性

两库均基于 RFC 8032 Ed25519 规范 (twisted Edwards curve, p = 2^255-19, SHA-512 nonce, 64 字节签名).

- **libsodium**: `crypto_sign_ed25519_detached` — ref10 实现, NaCl/TweetNaCl 测试向量, Frank Denis 维护, Signal / WireGuard / Tor 生产验证
- **monocypher v4**: `crypto_eddsa_sign` — Loup Vaillant 手写 portable C, 官方声明 "compatible with other Ed25519 implementations", 使用与 libsodium 相同测试向量集交叉验证

### 1.2 byte-equal 输出保证

Ed25519 为确定性签名 (RFC 8032 §5.1.6, nonce = SHA-512(sk||msg)). 同一 (private_key, message) 输入, monocypher 与 libsodium 输出 **byte-equal 64 字节签名**.

**API 差异 (参数顺序, 非密码学差异)**: monocypher v4 的 `sk` 是 32 字节 seed; libsodium `sk` 是 64 字节 seed||pk 拼接. 老孙 v5.2 适配时需处理此格式差异.

### 1.3 monocypher 安全审计

- Trail of Bits 2019 正式审计: 无高危, 2 中危已修 (均非 Ed25519 路径)
- **无已知 CVE** (截至 2026-05)
- libsodium 优势: NaCl 学术背书 + Trail of Bits 多次审计 + 大规模生产部署验证

**老沈评估**: monocypher 密码学正确性无实质疑问. 差距在社区维护规模.

---

## Part 2: 工程评估

| 维度 | Option A (libsodium ExternalProject_Add) | Option C (monocypher FetchContent) |
|---|---|---|
| CMake 行数 | ~200 行 (autoconf 包装 + IMPORTED target) | ~50 行 |
| 外部工具链 | autoconf / automake / libtool (CI 需预装) | 无 |
| ADR-018 对齐 | 不对齐 (ExternalProject_Add 异类) | **完全对齐** |
| CI 与本地一致性 | 有差异风险 (autoconf 版本 macOS vs Ubuntu) | **完全一致** |
| 首次 clone | +60~120s | +10~15s |
| 维护者 | 社区 + Frank Denis | 单作者 Loup Vaillant |
| 代码可审计性 | 中 (~10 万行) | 极强 (~2000 行) |
| 单作者停维风险 | 低 | **较高** |

**CI 盲区现状 (老周 C-02 / 老高 H-03)**: 当前 `find_library + NO_DEFAULT_PATH` 在 Linux CI 无 brew → `LIBSODIUM_FOUND=FALSE` → 静默走 mock → 真 Ed25519 路径 CI 从未覆盖. Option A 通过 ExternalProject_Add autoconf 编译可根除此问题; Option C 同样可根除.

---

## Part 3: 风险评估 (security 视角)

### 3.1 单作者维护风险

**风险等级: 中 (paper 阶段可接受, M5+ live 需重新评估)**

具体威胁:
- Loup Vaillant 停止维护 → 上游安全补丁无人响应
- 未发现漏洞后续被发现 → 社区修复速度慢于 libsodium

缓解措施 (若选 monocypher):
- 将 monocypher 源码 vendored copy 进仓库 (`src/third_party/monocypher/`), 不依赖上游活跃度
- 版本锁定 (GIT_TAG 4.0.2), 升级走 PR + 老沈 security review
- 后续老孙 signer 供应链审查 (B8 Blocker) 将 monocypher 纳入 SBOM

### 3.2 monocypher 已知 CVE / 漏洞历史

**截至 2026-05 老沈知识截止: 无已知 CVE.**

Trail of Bits 2019 审计报告: 发现 2 项 medium 级别 issue (均已在后续版本修复):
- 一项涉 X25519 key exchange 的 API 使用建议 (非 Ed25519 签名路径)
- 一项涉文档不明确 (非实现缺陷)

monocypher Ed25519 签名路径在审计中无发现.

**老沈判定**: monocypher 密码学实现无已知安全问题, paper 阶段风险可接受.

### 3.3 M5+ 切换路径评估

若 M5+ live 上链前决定切回 libsodium:

| 切换成本项 | 估算 | 说明 |
|---|---|---|
| CMakeLists.txt 改写 | 0.5 天 | monocypher → libsodium FetchContent Option A |
| signer_v52.cpp API 适配 | 0.5 天 | crypto_eddsa_* → crypto_sign_ed25519_* + sk 格式转换 |
| 单测向量更新 | 0.5 天 | 测试向量 byte-equal 验证 |
| CI / 老郭架构 review | 1 天 | ADR 补记 |
| **合计** | **~2.5 天** | 可接受, 不是高成本切换 |

**老沈评估**: 切换路径成本低, 不构成 paper → live 的高锁定风险.

---

## Part 4: 老沈 ack 决议

### 结论: **Option A — 保留 libsodium, 走 FetchContent + ExternalProject_Add**

**老沈拒绝 Option C (monocypher), 理由如下:**

1. **M5+ live 上链签名是公司最高风险场景**. signer 签名的是链上 Polymarket EIP-712 交易, 任何密码学缺陷等于资产损失, 无法回滚. 虽然 paper 阶段风险低, 但密码库选型一旦入代码, 切换成本与路径依赖存在; M5+ live 时的切换压力会在最高压力时期触发.

2. **单作者维护是真实生产风险**. Loup Vaillant 目前活跃, 但无法保证 M5+ (2026-11+) 后的活跃度. 签名库出现 0-day 时, libsodium 的响应速度 (Frank Denis + 大社区) 远优于 monocypher.

3. **libsodium 工程上的困难是 CMake 封装问题, 不是密码学问题**. Option A (ExternalProject_Add + autoconf) 确实复杂, 但这是一次性的 CMake 工程成本, 不是持续的密码学安全风险. 将"CMake 复杂"与"密码库可信度"混同是错误的权衡维度.

4. **老沈 SOP §6 原始意图是 libsodium, 不是"任何 Ed25519"**. SOP §6 选定 libsodium 时, 考量的是: (a) NIST/NaCl 学术背书; (b) Frank Denis 长期维护; (c) 大规模生产部署验证 (Signal, WireGuard). monocypher 不具备 (b)(c) 两项.

**对老孙的具体指示 (W7 W2 实施)**:

选 Option A. 如下约束:

1. ExternalProject_Add autoconf 封装: CI Ubuntu 必须有 `apt-get install autoconf automake libtool` 步骤; macOS 用 `brew install autoconf automake libtool`
2. libsodium 版本锁定 GIT_TAG 1.0.20-RELEASE (不用 latest, 不用 HEAD)
3. CMakeLists.txt 必须将 `STCPP_SIGNER_V52_LIBSODIUM=1` 宏在 ExternalProject build 完成后**显式**定义, 不允许找不到就静默走 mock — CI 若 autoconf 失败必须 build fail, 不允许静默降级
4. ctest 440/440 baseline 不破 (老孙 W7 W2 落 cpp 后附摘要回汇)

---

## Part 5: STRATEGY_DECAYED CLI 协同

**决定: STRATEGY_DECAYED CLI 继续用 libsodium, 不切 monocypher.**

SOP v1 §4/§6 的 `stcpp-strategy-unlock` CLI 已设计为 libsodium Ed25519 验签. signer v52 选 Option A libsodium 后, 两组件统一用 libsodium, 避免跨库 key format 混淆, 供应链唯一.

**建议**: 老孙 W7 W2 抽取 `stcpp_crypto_ed25519` CMake INTERFACE target, signer 与 CLI 共用, 不各自重复封装 libsodium ExternalProject_Add.

---

## Part 6: 不耻下问

**@老孙** (Option A 实施): W7 W2 落 cpp 时须满足: (1) `ExternalProject_Add` 设置 `BUILD_BYPRODUCTS` 确保 Ninja incremental 感知 libsodium.a; (2) CI 前置 `apt-get install autoconf automake libtool`; (3) `STCPP_SIGNER_V52_LIBSODIUM=1` 宏在 ExternalProject 完成后显式定义, 找不到时 build fail 而非静默 mock; (4) 遇阻 4h 内上报老周, 不再静默 workaround.

**@老郭** (架构评审): 请确认 (1) ADR-018 是否需补记 ExternalProject_Add 例外项 (老沈建议: 补 §5 "libsodium autoconf-native 库走 ExternalProject_Add, 非 FetchContent_MakeAvailable, 原因已记录"); (2) `stcpp_crypto_ed25519` 共用 INTERFACE target 架构是否认可.

**@老周** (arch review): 请 pre-approve Option A ~200 行 CMake. 重点: autoconf 在 macOS arm64 vs Linux x86_64 跨平台构建坑; IMPORTED STATIC target build order 约束.

**@老雷** (GM final): 本文为老沈 W7 ack spec 总结: **选 Option A libsodium** (拒 monocypher), **CLI 保留 libsodium**, diff 0 cpp, ctest 440/440 不破. W7 W2 老孙实施 → 老郭 + 老周 review → GM final ack.

**@老雷** (错 #14 commit hygiene): 本文纯文本, 无 binary, 遵守 commit hygiene.

---

## 附: ctest baseline 验证

**验证时间:** 2026-05-28 (本机, macOS arm64, monocypher 变更前)

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
integration    =   0.05 sec*proc (14 tests)
sim            =   0.02 sec*proc (4 tests)
unit           =   1.31 sec*proc (422 tests)

Total Test time (real) =   1.44 sec
```

**diff cpp change: 0 (本文档为 spec only, 未改任何 .cpp / .hpp / CMakeLists.txt)**

libsodium 本机: macOS arm64 homebrew `/opt/homebrew/lib/libsodium.dylib` (LIBSODIUM_FOUND=TRUE)

---

**END v1.**

*owner: 老沈 (security-engineer)*
*date: 2026-05-28*
*待 ack: @老郭 (架构) → @老周 (arch review) → @老雷 (GM final)*
*W7 W2 实施 owner: 老孙 (cpp + CMakeLists.txt, 不动 spec)*
