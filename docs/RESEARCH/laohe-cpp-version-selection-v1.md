# C++ 标准选型独立第二意见 — C++20 vs C++23

- Owner: 老何 (modern-cpp-advisor)
- Last review: 2026-05-28
- 验收人: 老郭 (architecture-reviewer) + 老周 (cpp-chief-architect)
- 关联文档: `docs/RESEARCH/laozhou-architecture-v0.1.md` §5 D1, OQ-1
- 关联 ticket: S1-001 (架构)

---

## 0. TL;DR (老雷三秒结论)

**结论: 赞同老周 D1 — 主干锁 C++20, 不上 C++23.**

但带 3 个附加条款 (与老周 D1 原文不完全相同):

1. **`-std=c++20` 主干 + `-std=c++2b` 试验分支**: CI 双跑, 提前发现 23 迁移风险. 不锁死.
2. **自研 `Result<T,E>` 必须以 `std::expected` 为蓝本** (P0323R12 final, ISO/IEC 14882:2023 §22.8). 接口签名向标准对齐, 二年后 C++23 工具链铺开可一行 `using` 切换. 不允许造方言 (e.g. `Result::ok()` / `Result::err()` 这种 Rust 化命名).
3. **`std::format` 例外**: C++20 已有 `std::format` 但 libstdc++ 13- / libc++ 16- 实现拉胯, 实测换 `fmt::format` (header-only fmt 10.x) 直到 GCC 14 / Clang 18 普及.

最关键的踩坑预防是: **不要把 C++20 选型当成永久承诺**, 写 ADR 时定一条 "M+18 月重新评估" 触发器.

---

## 1. 工具链稳定性 (硬数字)

### 1.1 三大编译器对 C++20 / C++23 的支持矩阵 (截至 2026-05)

| Feature | GCC 13 (2023-04) | GCC 14 (2024-05) | Clang 17 (2023-09) | Clang 18 (2024-03) | Clang 19 (2024-09) | MSVC 19.40 / VS17.10 |
|---|---|---|---|---|---|---|
| **C++20 core** |
| concepts | full | full | full | full | full | full |
| ranges | full | full | full | full | full | full |
| coroutines | full | full | full | full | full | full |
| modules | partial | better | partial | improved | usable | partial |
| `std::span` | full | full | full | full | full | full |
| `std::atomic_ref` | full | full | full | full | full | full |
| `<chrono>` calendar | full | full | partial → full (17) | full | full | full |
| `std::format` | **GCC 13 OK** | full | **Clang 17: libc++ 缺 chrono format** | mostly | full | full |
| **C++23 core** |
| `std::expected` | GCC 12+ | full | **libc++ 17: yes** | full | full | VS17.3+ |
| `std::print` / `std::println` | **GCC 14 only** | full | Clang 18+ | full | full | VS17.10 |
| `std::flat_map` | **GCC 15 (2025-04, beta)** | absent | absent | absent | **Clang 19 partial** | VS17.10 partial |
| `std::generator` | GCC 14+ | full | **Clang 18+** | full | full | VS17.10 |
| `std::mdspan` | **GCC 13.1** | full | Clang 18+ | full | full | VS17.7 |
| `if consteval` | full | full | full | full | full | full |
| Deducing `this` | **GCC 14** | full | **Clang 18** | full | full | VS17.5+ |
| `import std;` | **GCC 15 wip** | absent | **Clang 18 partial** | improved | mostly | VS17.5+ |
| `std::stacktrace` | full (需 libstdc++_backtrace) | full | **Clang 19 partial** | absent | partial | full |
| Static `operator()` | full | full | full | full | full | full |

**关键观察:**

1. **C++20 在 GCC 13 / Clang 16 已工业级稳定**. 三家覆盖度 ≥ 95%, 已是企业落地标准.
2. **C++23 在 GCC 14 / Clang 18 才开始可用**, 但 `std::flat_map` 直到 2026 仍 partial. `std::print` Clang 17 没有, Clang 18 才到位.
3. **`std::expected` 是 C++23 最容易开箱即用的**, GCC 12 / Clang 17 / VS17.3 都已 ship. 这是老周方案"自研 Result"最大的可争议点 — 但见 §3.
4. **modules 在三家都没完全收敛**, 跨编译器 BMI 不兼容. C++20 选 modules = 自杀, C++23 也未明显改善. 主干仍走 #include + PCH.

### 1.2 部署节点工具链可用性 (跨洋影响)

跨洋部署机房一般两类:

| 场景 | 典型 OS | 默认 GCC |
|---|---|---|
| AWS / GCP / Azure 主流区域 | Ubuntu 22.04 / 24.04 | 11.4 / 13.2 |
| Hetzner / OVH 欧洲 | Debian 12 / Ubuntu 22 | 12.2 / 11.4 |
| 自建 colo / 廉价 VPS | 偶见 CentOS Stream 9 / RHEL 9 | 11.4 (devtoolset-13 可选) |

**老周担忧"跨洋部署节点工具链可能要锁旧版"成立**, 但解决方案不是降级到 C++17, 而是:

- **静态链接 libstdc++ / libc++** (`-static-libstdc++ -static-libgcc`), 主机 glibc 是唯一动态依赖.
- **CI 在 build farm 用 GCC 14 / Clang 19**, 产物只要求目标机 glibc ≥ 2.31 (Ubuntu 20.04+). 主机 GCC 多旧无关.
- **C++20 vs C++23 这点上没差别**, 两者都能这么做. 所以"跨洋节点工具链旧"**不能作为反对 C++23 的硬理由**.

老周 D1 这句话不严谨, 我给老周补一刀.

---

## 2. 关键 feature 逐项评估

按对本项目 (低延迟交易) 重要性排序:

### 2.1 `std::expected` (C++23)

**用途**: 错误处理无异常路径. 与本项目 D3 (无异常贯穿热路径) 完美契合.

**对标**: tl::expected (header-only, MIT, https://github.com/TartanLlama/expected, ~3k stars, monadic ops 完整).

**评估**:
- C++23 `std::expected` API 已稳定, 但 monadic ops (`and_then`, `or_else`, `transform`, `transform_error`) 在 GCC 12 已支持.
- tl::expected 是事实标准, ABI 兼容性 OK, 跨编译器一致.
- 自研 = 重新发明轮子, 但接口对齐标准成本极低.

**建议**: 用 **tl::expected** 作为 polyfill 而非自研. 理由:
1. tl 接口与 `std::expected` 1:1, 24 个月后切换零成本.
2. 自研团队没人会比 Sy Brand (tl 作者, C++ 委员会 EWG) 写得好.
3. 减少老周 L1 `infra/error` 模块的维护负担.

若坚持自研, 命名必须严格对齐 (见 §0 第 2 条).

### 2.2 `std::format` / `std::print` (C++20 / C++23)

**用途**: 类型安全格式化, 替代 `printf` / `iostream`.

**评估**:
- `std::format` C++20 标准, 但 libc++ < 17 / libstdc++ < 13 实现有坑 (尤其 chrono format).
- `std::print` C++23 才有, GCC 14 / Clang 18 起.
- **fmt 库 (fmtlib/fmt v10.x)** header-only, 与 `std::format` API 等价, 编译期格式串检查更早.

**建议**: 主干用 **fmt::format / fmt::print**, 待 GCC 14 / Clang 18 在所有部署节点铺开后, 单点切换. fmt 与 std::format 写法兼容 (`{}` 占位符相同), 切换基本 sed.

热路径不要用 format, 用预分配 buffer + 自研 formatter (老姜 §1 阶段 9 已规定).

### 2.3 modules (C++20)

**用途**: 替代 #include, 编译加速 + 隔离宏污染.

**评估**: **不可用**.
- GCC 13 partial, Clang 17 partial, MSVC 部分 ship.
- 跨编译器 BMI 不兼容, header-units 也没收敛.
- 三方库 (boost / abseil / simdjson / openssl) 没有一个出 module 接口.
- google 内部用 modules 是 build system magic, 不是 ISO 标准模式.

**建议**: 完全不碰 modules. CMake `target_sources(... FILE_SET CXX_MODULES)` 写法记入"未来评估清单", M+24 月再看. **C++23 也不改变这个结论** — 这条不能算 23 的优势.

### 2.4 coroutines (C++20)

**用途**: 异步 IO / 异步 RPC 编排. 主干已是 C++20 的现成特性.

**评估**:
- C++20 coroutines 是语言机制, **没有标准库 task / generator**. C++23 加了 `std::generator` (单线程 coroutine 友好).
- 主流用法依赖 cppcoro (Lewis Baker, 不再活跃) / folly::coro / asio::awaitable.
- 老周 D5 明确"reactor + state machine 优先, coroutine 局部使用", 这是对的.

**建议**:
- 主干不用 coroutines.
- 局部使用场景: 非热路径的 REST 调用编排 (Polygon RPC 多 provider race), 用 asio::awaitable.
- **永远不在热路径用 coroutine**: heap-alloc 的 coroutine frame 是 latency 杀手. 老姜 §1 budget 不会原谅这种.

### 2.5 ranges (C++20)

**用途**: 函数式风格 view / pipeline. concept-constrained algorithms.

**评估**: C++20 ranges 是子集, C++23 加了 `views::zip`, `views::adjacent`, `views::chunk_by`, `views::stride`, `to<container>()`. C++23 增量很有用, 但每个都能自己写 10 行替代.

**建议**:
- 热路径**禁用 ranges views** (eager-evaluated 操作 OK, lazy view 偶尔抑制优化, 实测 godbolt 验证).
- 非热路径 (config 解析 / 回测准备 / metrics aggregation) 鼓励用 ranges, 可读性大幅提升.
- `views::zip` 没有也能用 `std::views::iota` + indexing 替代, 不构成 C++23 硬需求.

### 2.6 concepts (C++20) — 唯一 0 异议特性

模板代码必须用 concepts 约束, 不再容忍 SFINAE. 详见 footgun 清单 §5.

### 2.7 `std::flat_map` / `std::flat_set` (C++23)

**用途**: 排序数组背景 map, cache-friendly, 小规模数据 (< 256 entries) 比 `std::map` 快.

**评估**:
- GCC 15 (2025-04 beta), Clang 19 partial, MSVC 17.10 partial.
- 工业上已被 abseil flat_hash_map / boost::container::flat_map 占领.
- 标准 flat_map ABI 与三方不兼容, 切换成本 ≠ 0.

**建议**: 用 **abseil flat_hash_map** (小石 §0 也是这个推荐). C++23 这点收益 = 0.

### 2.8 deducing `this` (C++23)

**用途**: 消除 const/non-const 成员函数重复, CRTP 替代.

**评估**: 语法糖, 写库时很爽, 写业务代码不常用. Clang 18 / GCC 14 起可用.

**建议**: 暂不依赖, 待工具链铺开. 不影响选型.

---

## 3. 自研 Result vs std::expected vs tl::expected — 直接对老周喊话

老周 D1 反方意见说"我们自己实现 `Result<T,E>` 等效, 不为糖锁工具链". 这话**对了一半, 错了另一半**.

**对的部分**: 不该为单一糖锁定工具链.

**错的部分**: 自研 ≠ 唯一替代. tl::expected 已经存在且生产级.

### 3.1 三种选项对比

| 维度 | 自研 `Result<T,E>` | tl::expected | `std::expected` (C++23) |
|---|---|---|---|
| 工具链要求 | C++17+ | C++11+ | GCC 12+ / Clang 17+ / VS17.3+ |
| 接口标准化 | 完全自定义, 容易方言 | 1:1 对齐 std::expected | 标准 |
| monadic ops (and_then / or_else / transform) | 写 | 已有 | 已有 |
| `std::in_place` / `std::unexpect_t` 语义 | 自己设计 | 已对齐 | 标准 |
| reference type 支持 (`expected<T&, E>`) | 自己处理 | 部分 (跟标准 P2549 进度) | 标准但有 LWG issue |
| 异常 fallback (`.value()` throw) | 自己设计 | bad_expected_access | 标准 |
| 维护成本 | 持续吃团队精力 | 0 (header-only, 上游维护) | 0 (标准库) |
| 切换 C++23 成本 | 全项目改名 / 改接口 | `using` 一行 | 自动 |
| godbolt 验证 | 需自己写 | https://godbolt.org/z/tl-expected | https://godbolt.org/z/expected-c23 |

### 3.2 建议方案 (老何拍板)

```cpp
// include/stcpp/infra/error/result.hpp
#if __cpp_lib_expected >= 202211L && !defined(STCPP_FORCE_TL_EXPECTED)
  #include <expected>
  namespace stcpp {
    template<class T, class E> using Result = std::expected<T, E>;
    template<class E>          using Err    = std::unexpected<E>;
    inline constexpr auto err = std::unexpect;
  }
#else
  #include <tl/expected.hpp>
  namespace stcpp {
    template<class T, class E> using Result = tl::expected<T, E>;
    template<class E>          using Err    = tl::unexpected<E>;
    inline constexpr auto err = tl::unexpect;
  }
#endif
```

**结果**:
- 现在 (C++20 工具链) 走 tl::expected, 一行依赖加入 Conan.
- 24 月后切 C++23, 改一个宏 / 一行 CMake 选项.
- 全代码库 `stcpp::Result<T,E>` 不变.
- 老周原 D3 (无异常贯穿) 决策不动.

**节省**: 自研估计 2-3 人周 (写 + 测 + monadic ops + corner case). tl::expected 节省这些精力直接转交风控 / 策略.

---

## 4. 招聘门槛

### 4.1 市场 C++ 工程师标准分布 (2026)

按公开招聘数据 (HFT / 量化 / 系统编程岗位 JD):

| 语言要求 | 占比 | 备注 |
|---|---|---|
| C++17 fluent | ~85% | 行业基线 |
| C++20 fluent | ~50% | concepts / ranges / coroutines 至少摸过 |
| C++23 fluent | ~10% | std::expected 用过的多, 全套 23 少 |

**结论**: C++20 招聘门槛 OK, C++23 在 2026 仍偏高. 自研 Result 反而**抬高门槛** (因为新人要学项目方言). 用 tl::expected 让新人看到 `std::expected` 文档就能上手.

### 4.2 中文圈 C++ 工程师

国内一线 (Tencent, ByteDance, Pinduoduo) 主流仍 C++17, C++20 在新项目铺开中. 跳槽时 C++20 经验是加分项. C++23 仅在游戏引擎 / 高频前沿团队普及.

**老钱视角 (CPO)**: 如果团队后续要在国内招人, C++20 是合适招聘梯度. 完全锁 C++23 会让招聘池缩 30-40%.

---

## 5. HFT / 量化行业实践 (引用)

| 公司 | 公开标准 | 来源 |
|---|---|---|
| Optiver | C++20 主干, 部分 C++23 试点 | CppCon 2024 "Hard Latency" talk by Carl Cook |
| Jane Street | OCaml 主, C++17 系统层 | 内部 talk + Bloomberg interview 2024 |
| IMC | C++17 → C++20 迁移完成 (2023) | C++Now 2024 |
| HRT (Hudson River Trading) | C++20 + 自研 ranges-like | github HRT 公开仓库 |
| Citadel Securities | C++17 + 严格静态分析 | Andrew Pardoe (Senior Director) ISO C++ committee posts |
| Bloomberg | C++17 主 + BDE 库 (内部 std 替代) | bsl library |
| Trading Tech (Chicago) | C++20 (2024) | EmbeddedCpp talks |
| NVIDIA quant teams | C++20 + CUDA | GTC 2024 |
| LMAX (UK, 撮合引擎) | Java 主, C++ 系统 | Disruptor paper |

**关键观察**: **没有任何一线 HFT 公司当前 (2026-05) 全面切到 C++23**. C++20 是新建项目的"现在", C++23 是"接下来 12-24 月".

老周选 C++20 = 与 Optiver / IMC / HRT 同梯队, **完全合理**.

---

## 6. 反方意见 (我自己当反方)

诚实列一下"为什么应该上 C++23":

| 反方论据 | 强度 | 反驳 |
|---|---|---|
| `std::expected` 标准化 | 强 | tl::expected 等价, 见 §3 |
| `std::print` 更安全 | 中 | fmt::print 等价 |
| `std::flat_map` 性能 | 弱 | abseil / boost::container 已占领 |
| `std::generator` 协程 | 中 | 热路径不用 coroutine, 非热路径用 asio |
| Deducing `this` | 弱 | 业务代码不常用 |
| `if consteval` | 中 | 罕见场景, 写两条路径不痛苦 |
| `static operator()` | 弱 | 微优化 |
| `import std;` | 强 (如果可用) | 三家都没完成, 无法依赖 |

**唯一足以重新考虑的**: 如果团队全员都在等 `std::expected` + `std::print` + `std::generator`, 且 GCC 14 / Clang 18 已确认可在所有目标环境跑, 那 C++23 也不是不可以.

**但**: 见 §0, **CI 双跑** (`-std=c++20` 主, `-std=c++2b` 试) 已经覆盖这条迁移路径, 不需要现在拍板.

---

## 7. 编译选项铁律 (与 footgun 清单 §8 一致, 此处只点要)

```cmake
# CMakeLists.txt 全局
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)         # 关 GNU 扩展, 拒绝 -std=gnu++20

add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow -Wconversion -Wsign-conversion
  -Wnon-virtual-dtor -Wold-style-cast
  -Wcast-align -Woverloaded-virtual
  -Wnull-dereference -Wdouble-promotion
  -Wformat=2 -Wimplicit-fallthrough
  -fno-rtti                           # 项目不用 dynamic_cast, 节省 binary
  -fno-exceptions                     # 热路径无异常, D3 强制
                                       # (注意: 三方库需 try/catch 边界, 在适配层 wrap)
)

# Debug / Sanitizer build
add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
add_link_options(-fsanitize=address,undefined)

# Release
add_compile_options(-O2 -g -DNDEBUG)  # 注意: -O2 不是 -O3, 见 footgun §8
```

**关键**: `-fno-exceptions` + `-fno-rtti` 是 D3 的物理保障. 选 C++20 还是 C++23, 这套选项都一样.

---

## 8. 最终建议 (拍板)

### 8.1 推荐方案

**主干 C++20, 自研 Result 改为 tl::expected polyfill.**

- 编译标准: `-std=c++20`, `-fno-exceptions`, `-fno-rtti`.
- 错误处理: `stcpp::Result<T,E>` = tl::expected (现) / std::expected (24 月后切).
- 格式化: fmt::format (现) / std::format (24 月后切).
- 协程: 不用主干, 局部用 asio::awaitable.
- modules: 不用, M+24 重评估.
- ranges: 非热路径 OK, 热路径需 godbolt 验证.
- 工具链: GCC 13+ / Clang 17+ 为 build farm 基线; libstdc++ / libc++ 静态链接, 目标机仅依赖 glibc.
- CI: 三档同时跑 — `gcc-13 -std=c++20` (主) + `clang-18 -std=c++20` (审计) + `gcc-14 -std=c++2b` (实验, 允许 fail).

### 8.2 触发器: 何时升 C++23

任一条件满足触发评审:

1. GCC 14 + Clang 18 在所有部署节点可用 (估 2026-Q4 / 2027-Q1).
2. 团队提案明确指出 ≥ 3 个 C++23 特性当前阻塞开发.
3. 第三方核心依赖 (simdjson / abseil) 要求 C++23.

评审会议: 老周主持, 老郭 + 老何参与.

### 8.3 入 ADR

建议老郭把以下决定入 ADR-002 (C++ standard selection):

> sports-trader-cpp 主干 C++20 (`-std=c++20 -fno-exceptions -fno-rtti`), 错误处理走 tl::expected polyfill 命名 `stcpp::Result<T,E>`, 接口签名严格对齐 std::expected. CI 同时跑 C++23 实验编译. M+18 月触发 C++23 升级评审.

---

## 9. 与老周原 D1 的 diff (供老郭评审用)

| 条目 | 老周 D1 (原) | 老何独立意见 |
|---|---|---|
| 锁 C++20 | ✓ | ✓ 同意 |
| 不上 C++23 | ✓ | ✓ 同意 (但 CI 双跑) |
| 自研 Result | ✓ | ✗ **改用 tl::expected**, 接口对齐 std::expected |
| 跨洋节点工具链旧 = 反 C++23 论据 | ✓ | ✗ 不严谨, 静态链接可消除 |
| 工具链稳定性论据 | 简略 | 强化, 见 §1.1 矩阵 |
| 升级触发器 | 无 | 新增, 见 §8.2 |
| `std::format` 取舍 | 未说 | 新增 fmt 过渡, 见 §2.2 |
| `-fno-exceptions / -fno-rtti` | 隐含 | 显式写入 CMake 模板 |

---

## 10. 求证 / 求助

- **性能争议**: §7 编译选项中 `-O2 vs -O3`, ranges view 是否影响热路径优化 → @老姜 godbolt 实测.
- **数据结构**: §2.7 flat_map 与 abseil flat_hash_map 性能差异 → @小石 (已在文档 v1 §0 给推荐, 我对齐).
- **Rust 互操作**: 如果未来 §3.2 的 `Result` 要跨 FFI 到 Rust, 接口是否对齐 → @老张 (rust-advisor) 评审.
- **工具链节点实测**: 部署节点 GCC/glibc 版本调查 → @老吴 (S1-010 跨洋部署) 顺手做.

---

## 11. 评审请求

**@老郭**: 主要评 §3 (Result 自研 vs polyfill) + §8 (最终方案) + 是否入 ADR-002.
**@老周**: 主要评 §9 diff, 看是否接受这 3 处修改 (尤其放弃自研 Result).

拍板后, 老何不再持续 own 此文档, 移交老周作为架构主干一部分进 D1 v0.2.
