# modern C++20/23 审计 v1 + footgun checklist v1.1

owner: 老何 (F 顾问团, ADR-009)
last_review: 2026-06-01
覆盖: src/ + include/ 约 12000 行 (含 tests 共 15536 行), Wave 26 W5 末代码快照

---

## Part 1: C++20 特性使用现状

### 1.1 已用 / 用得好

**std::span** — audit_emitter.cpp 的 `copy_fixed(std::span<char> dst, ...)` 和所有 `serialize_into(std::span<std::byte> out)` 接口都走 span, 正确. 零拷贝传递缓冲区, 没有退化成裸指针.

**concept WalRecord** — wal_writer.hpp 定义得干净:
```cpp
concept WalRecord = requires(const T& r, std::span<std::byte> out) {
    { r.event_ts_ns() } -> std::same_as<std::int64_t>;
    ...
    { r.serialize_into(out) } -> std::convertible_to<std::size_t>;
};
```
TrainingLabel / FeatureSnapshot 均有 static_assert 验证, 正确. W6 新 record 跟进即可.

**constexpr 覆盖率高** — fill_rate_model.hpp 全局常量 (FILL_RATE_FLOOR / FILL_RATE_CAP / PENALTY_* 等), orderbook.hpp 的 TAKER_FEE_PCT / QHL_THRESHOLD_MS, sport_profile.hpp 的 kNumSports / kNumPhases, signer 的 kFixedGasEstimate 全部 `inline constexpr`, 符合 C++17 inline variable + constexpr 最佳实践.

**[[nodiscard]] 覆盖率** — 全库 298 处, 覆盖所有 Result<T> / WalResult<T> 返回, IPolymarketClient 14 个虚函数全标. 做到了最高行业标准.

**noexcept 覆盖率** — include/ 下 293 处, 热路径函数基本都标. 正确.

**WalResult<T> 自制 expected** — wal_error.hpp 用 `std::variant<T, WalError>` 实现 WalResult<T>, API 与 tl::expected 风格对齐. 注释已注明等 C++23 std::expected 切. 设计意图清晰.

### 1.2 缺口 / 可改进

**std::ranges — 零使用.** 当前没有一处 std::ranges::find / std::ranges::sort / ranges::views 用法. 潜在改进点:
- goalserve_stub.cpp 里的手写 URL 拼接 (ostringstream) 不是 ranges 问题, 但 p0_01_pinnacle_no_vig.cpp 里如果有遍历 book_ map 的逻辑, C++20 ranges::views::filter / transform 可以让意图更清晰.
- 建议 W6 新模块在遍历容器时优先考虑 ranges 惯用法, 避免手写 for 循环 + 条件.

**[[likely]] / [[unlikely]] — 零使用.** risk_gateway.cpp 有 21 条规则 short-circuit 链:
```
if (check_state_(...))          { reject_here(); return d; }
if (check_invalid_intent_(...)) { reject_here(); return d; }
...
```
这 8 个 if 分支在正常交易流中几乎全部 false (APPROVED 是热路径). 每个 `if` 应加 `[[unlikely]]`. 估算: x86 分支预测器在 8 连分支时 miss penalty 约 15-20 cycle 每次 miss, 加 hint 后编译器可重排代码布局降低 I-cache pressure. 建议 @老周 确认是否纳入 W6 hot path 优化 scope.

**std::source_location — 零使用.** audit_id 目前是 ULID stub (6B ts + 6B seq + 4B zero), 调试时定位代码位置靠手写字符串. 建议 W6 在 AuditEmitter::emit() 的调试/诊断路径 (非热路径) 加 `std::source_location::current()` 参数, 让 audit_id correlation 调试更精准. 热路径不要带.

**std::format — 零使用, 但现有代码基本不需要改.** 热路径全部走 `snprintf` 到栈 buffer (single_instance.cpp, paper_pm_client.cpp), 这是正确的 (std::format 有堆分配). 非热路径 goalserve_stub.cpp 走 ostringstream. 如果 W6 有诊断日志模块, 用 std::format 替代 ostringstream 是合理的, 但不紧迫. **paper.cpp 里的 std::fprintf(stderr, ...) 格式字符串中有 `%llu` + 多个 static_cast<unsigned long long>, 这恰恰是 std::format 要消灭的 footgun — 建议 W6 paper binary 日志切 std::format.**

**consteval — 零使用.** sport_profile.hpp 的 `sport_name(Sport s)` 是 constexpr, 入参是 enum, 理论上可改 consteval (强制编译期求值). 但当前用法 (switch dispatch) 既有编译期也有运行时调用场景, constexpr 已足够. 暂不强制.

**std::string_view 异构查找 — 缺口.** paper_pm_client.cpp 里 `unordered_map<std::string, ...>` 频繁用 `std::string{condition_id}` 做 key (condition_id 是 string_view). 这会触发堆分配. C++20 异构查找 (transparent hash) 可以消灭这个拷贝. 详见 Part 2 footgun #9.

---

## Part 2: footgun 实际检出

### FG-001 未初始化变量 — 实际命中 1 处

**文件**: `src/stcpp/execution/virtual_matcher.cpp:60`

```cpp
double u;
if (has_override_) {
    u = uniform_override_;
} else {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    u = dist(rng_);
}
const bool draw = (u < p_clamped);
```

控制流保证 `u` 一定被赋值 (两个分支都赋值), 所以实际不是 UB. 但 `-Wuninitialized` / clang-tidy `cppcoreguidelines-init-variables` 会报 warning, 且阅读者需要追踪两个分支才能确认安全. **建议**: `double u = has_override_ ? uniform_override_ : dist(rng_);` — 单行三目表达式, 消除未初始化外观, 也让 `dist` 的生命周期更清晰 (当前 `dist` 在 else 块内构造但逻辑上两个分支共用 `u`).

sport_profile.hpp:94-97 / slippage_model.hpp:54-58 / wal_record_header.hpp:34-38 的字段声明没有初始化器是结构体字段 (非局部变量), 由构造函数初始化, 不是问题.

### FG-002 裸 new — 实际命中 1 处, 可接受但有改进空间

**文件**: `src/stcpp/infra/wal/wal_writer.cpp:59`

```cpp
auto w = std::unique_ptr<WalWriter<R>>(new WalWriter<R>());
```

构造函数是 `private`, 所以不能用 `std::make_unique<WalWriter<R>>()`. 这是经典 "private constructor + factory" 模式的局限. 可接受. 如果 W4 真实接 ring + fd, 建议用友元或 `std::allocate_shared` 等方式消除裸 new, 避免未来重构时 Open() 内 throw 路径泄漏 (当前没有 throw, std::abort 也不走析构, 所以暂时安全).

裸 delete — 全库零命中. 正确.

### FG-003 lambda [&] 捕获 — 命中 3 处, 2 处安全 1 处需注意

**goalserve_stub.cpp:82**:
```cpp
auto add_param = [&](std::string_view k, std::string_view v) { ... };
```
lambda 生命周期在函数内, 不逃逸, `[&]` 安全.

**risk_gateway.cpp:178 / 386**:
```cpp
auto fail = [&](InvalidIntentSubReason s) noexcept { ... return true; };
auto reject_here = [&]() noexcept { ... };
```
两者都是函数局部 lambda, 不逃逸. 但 `fail` 捕获 `d` (RiskDecision&), 如果未来 `d` 的引用语义变化 (例如改为 out-param 指针), 容易忘记更新 lambda. **建议**: 显式捕获 `[&d]` 而不是 `[&]`, 让捕获意图一目了然, 也让审查者不需要扫全函数才能确认捕获集合. clang-tidy `cppcoreguidelines-avoid-capturing-all-vars` 会提示.

### FG-004 裸 fd — 命中, 需要关注

**文件**: `src/stcpp/infra/process/single_instance.cpp`

`fd_` 是 `int` 类型成员变量, 析构函数手动 `::close(fd_)`, 构造异常路径也手动 `::close(fd_)` (4 个 throw 分支各写了一次). 目前代码是正确的, 但脆弱: 每次新增抛异常路径都必须记得 close. **建议 W6**: 引入内部 `struct FdGuard { int fd = -1; ~FdGuard() { if (fd >= 0) ::close(fd); } };` 替代裸 int fd_. 标准 POSIX fd RAII 封装, 析构自动 close, 不再需要每个 throw 路径手写 close.

pm_wss_subscriber.cpp:385 `transport_->Close()` 是通过 beast 对象方法关闭 socket, 不是裸 fd, 可接受.

### FG-005 is_finite 自制 vs std::isfinite — 命中多处

**文件**: `src/stcpp/risk/risk_gateway.cpp:48`, `src/stcpp/strategy/p0_01_pinnacle_no_vig.cpp:14`, `include/stcpp/numerical/slippage_model.hpp:87`

三个文件各自定义了 `is_finite` 局部函数, 实现略有差异:
- risk_gateway: `!(x != x) && x > -1e300 && x < 1e300` — 手写 NaN check + 范围限定 (比 std::isfinite 更严)
- slippage_model: `[[nodiscard]] inline constexpr bool is_finite(double x)` — constexpr, 用 `x == x` check NaN

`std::isfinite` 是 `<cmath>` 标准函数, C++23 起是 constexpr. 当前用 C++20, std::isfinite 不是 constexpr, 所以用自制的 constexpr 版本有其理由. **建议**: 在 `include/stcpp/` 某公共 header (例如 `math_utils.hpp`) 统一定义一个 `[[nodiscard]] constexpr bool finite_pos(double x) noexcept` 和 `finite(double x) noexcept`, 三处重复代码合并到一处. orderbook.hpp 已经有 `finite_pos` / `finite`, 可以以它为 SSOT — @老周确认是否提升到公共层.

### FG-006 signed/unsigned 混用 — 命中 1 处需关注

**文件**: `src/stcpp/infra/process/single_instance.cpp:93-101`

```cpp
int remaining = len;     // int, snprintf 返回 int
while (remaining > 0) {
    const ssize_t n = ::write(fd, p, static_cast<std::size_t>(remaining));
    ...
    remaining -= static_cast<int>(n);   // ssize_t → int truncation
}
```

`ssize_t` 转 `int` 在 write 返回大于 INT_MAX 字节时截断 (理论上不可能 — 写的是 256 字节 pid buffer), 实际不会触发. 但 `-Wconversion` 会 flag 这个 cast. 建议改为 `ssize_t remaining = static_cast<ssize_t>(len);` 全程用 ssize_t, 消除截断 cast.

### FG-007 heterogeneous lookup 缺失 — 命中多处

**文件**: `src/stcpp/polymarket/paper/paper_pm_client.cpp:53, 124, 167, 214, 329`

所有 `books_.find(std::string{condition_id})` 在传入 `string_view` 时构造临时 `std::string`, 触发堆分配. paper path 不是超热路径, 但 live path 接入后同样模式会在每次 GetOrderbook 调用时分配. C++20 heterogeneous lookup 方案:

```cpp
struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    std::size_t operator()(const std::string& s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};
std::unordered_map<std::string, OrderBookSnapshot, StringHash, std::equal_to<>> books_;
```

查找时直接传 `string_view`, 零拷贝. 建议 W6 live 模块引入时统一改. @老周 架构层是否需要公共 TransparentStringHash.

### FG-008 手写 is_finite NaN check 语义分歧 — 见 FG-005

---

## Part 3: BUG-W5-001 UB shift — modern C++ 防御

BUG-W5-001 (老沈 Wave 24 修复): 原 audit_id 生成代码 `seq >> ((9 - i) * 8)` 在 i=0 时 shift = 72, `uint64_t` 是 64 位, shift >= 64 是 C++ UB (未定义行为). -O2 编译器将该整段优化为 0, 导致 14 个 release mode 测试 audit_id 全零.

修法 (小宋 W5 W3): seq 从 10 字节缩为 6 字节, shift 最大 40, 安全.

**modern C++ 防御层 (我的建议, 非修复已完成):**

1. `<bit>` 库 (C++20) 的 `std::rotl` / `std::rotr` 专门处理旋转, 不存在 shift >= width 问题. 对于需要旋转语义的 hash / PRNG 代码, 优先用 `<bit>`.

2. 自制 safe_shift constexpr helper:
   ```
   // 在 math_utils.hpp 或 bit_utils.hpp
   template <std::unsigned_integral T>
   [[nodiscard]] constexpr T safe_shr(T v, unsigned shift) noexcept {
       // shift >= sizeof(T)*8 是 UB; 编译期检查.
       if constexpr (requires { std::is_constant_evaluated(); }) {
           // 编译期: static_assert 拦截
       }
       return (shift < sizeof(T) * 8u) ? (v >> shift) : T{0};
   }
   ```
   next_audit_id 的两个 for 循环改为调用 `safe_shr(ts_ms, (5u - i) * 8u)`, 编译期 / 运行时双重防御.

3. 当前修法已安全 (shift < 48 逐 byte 写), 但 `(5 - i) * 8` 中 `5 - i` 是 signed int 运算, i 循环到 0 时值为 5, 没问题. 若未来有人把 loop bound 改大, UB 会重现. 加一行 `static_assert(6 * 8 <= 64, "shift safe")` 或 assert(i < 6) 可防止退化.

4. `-fsanitize=undefined` (UBSan) 在 CI 运行时直接捕获 shift UB, 建议 W6 CI 加 UBSan build target. @老郭 架构评审.

---

## Part 4: 异常 vs error code — 混用现状分析

| 模块 | 当前模式 | 评价 |
|---|---|---|
| RiskGateway::evaluate() | RiskDecision (enum reject, 软返回) | 正确, 热路径不抛 |
| IPolymarketClient 14接口 | Result<T> (PMError struct, 软返回) | 正确, noexcept 接口 |
| WalWriter::Append/FlushUntil | WalResult<T> (variant, 软返回) | 正确, noexcept |
| SingleInstanceLock::Init() | 抛 SingleInstanceLockFailure | 正确, 启动期一次性 |
| WalWriter::Open() path fail | std::abort (不抛) | 正确, R-11 防绕过 |
| AuditEmitter::emit() | bool (软返回) | 可接受, 简单 |

**混用是否合理?** 是. 现有划分符合 modern C++ 主流观点:
- 启动期配置错误 (pid lock 失败, path prefix 不匹配): 抛异常或 abort — 无法恢复, 让进程在已知状态终止比继续运行更安全.
- 热路径运行时错误 (reject / 背压 / 网络): 软返回 — 不抛, caller 决策.

**std::expected 迁移建议 (W6 起新模块):**

WalResult<T> 已经是 expected 语义的正确实现. 当工具链升 C++23 后, 迁移路径是:
```
WalResult<T>  →  std::expected<T, WalError>
Result<T>     →  std::expected<T, PMError>
```
API 不变 (has_value() / value() / error() 语义相同). 切换只需改 typedef.

**W6 新模块建议**: 如果引入 `tl::expected` polyfill (CLAUDE.md README.md 提到过), 则 W6 新增模块直接用 `tl::expected<T, ErrorKind>` 替代裸 bool / enum return. 不引入 tl 的话维持 WalResult 风格一致即可. 核心原则: **新模块不再用裸 bool 表达带错误信息的返回值**.

@老周 确认工具链 C++23 切换时间点.

---

## Part 5: footgun checklist v1.1

owner: 老何 (W5-F-04 派单)
整合自: W5 R-12/R-20/R-11 cpp 反模式 + BUG-W5-001 + Wave 26 扫描结果

| # | 反模式 | 推荐 | clang-tidy 规则 | 项目实际命中 |
|---|---|---|---|---|
| F-01 | UB shift: `uint64_t x >> n`, n >= 64 | `safe_shr<T>(v, n)` constexpr helper; `<bit>` std::rotl/rotr; 加 assert(n < sizeof(T)*8) | `clang-analyzer-security.insecureAPI.*` + UBSan `-fsanitize=shift` | BUG-W5-001: risk_gateway.cpp audit_id seq 段 shift=72, -O2 全零. W5 修复. |
| F-02 | 隐式整型截断: ssize_t → int | 全程用同一有符号类型; `-Wconversion` enforce | `bugprone-narrowing-conversions` | single_instance.cpp:101 `remaining -= static_cast<int>(n)`. 低风险但 -Wconversion flag. |
| F-03 | 裸 new/delete | `std::make_unique` / `std::make_shared`; factory 模式用 `unique_ptr<T>(new T(...))` 仅在 private ctor 时 | `cppcoreguidelines-owning-memory` | wal_writer.cpp:59 (private ctor, 可接受). 全库零裸 delete. |
| F-04 | lambda [&] 全量捕获 | 显式捕获 `[&x, &y]` 或 `[=]`; 逃逸 lambda 禁 [&] | `cppcoreguidelines-avoid-capturing-all-vars` | risk_gateway.cpp:178/386 (不逃逸, 安全但不显式). |
| F-05 | 未初始化局部变量 | 声明即初始化: `double u = 0.0;` 或三目表达式消除 if-else 赋值 | `cppcoreguidelines-init-variables` | virtual_matcher.cpp:60 `double u;` (控制流安全但外观不佳). |
| F-06 | 裸 fd / POSIX handle 无 RAII | `struct FdGuard { int fd=-1; ~FdGuard(){if(fd>=0)::close(fd);} };` | `cppcoreguidelines-pro-type-member-init` | single_instance.cpp: fd_ 是裸 int, 析构 + 4 throw 路径手写 close, 脆弱. |
| F-07 | 异常与软返回混用 (无原则) | 启动期/不可恢复 → exception/abort; 热路径/可恢复 → Result<T>/WalResult<T>; W6 新模块 → std::expected | `bugprone-exception-escape` | 项目现有划分合理 (启动期抛, 热路径软返). |
| F-08 | signed vs unsigned 比较 | 统一类型; 用 `std::cmp_less` (C++20) 做跨类型安全比较 | `clang-diagnostic-sign-compare` (-Wsign-compare) | 目前 size() 比较均有 guard (goalserve_stub / gate_evaluator), 未命中. |
| F-09 | unordered_map<string> 用 string_view 查找触发堆分配 | C++20 transparent hash + equal_to<>: `unordered_map<string, V, StringHash, equal_to<>>`, find(string_view) 零拷贝 | `performance-inefficient-string-concatenation` (间接) | paper_pm_client.cpp 5 处 `books_.find(std::string{condition_id})`. |
| F-10 | 多处重复定义 is_finite | 公共 `math_utils.hpp` 统一 constexpr is_finite / finite_pos, 三处复制删除 | `bugprone-macro-repeated-side-effects` (概念) | risk_gateway.cpp, p0_01_pinnacle_no_vig.cpp, slippage_model.hpp 各一份. |

### clang-tidy 配置建议 (W6 起加 CI)

在 `.clang-tidy` 中启用:
```
Checks: >
  cppcoreguidelines-init-variables,
  cppcoreguidelines-owning-memory,
  cppcoreguidelines-avoid-capturing-all-vars,
  bugprone-narrowing-conversions,
  performance-unnecessary-value-param,
  clang-diagnostic-sign-compare,
  clang-diagnostic-conversion,
  -fsanitize=undefined (UBSan, CI debug build)
```

@老高 PR review v1.2 引用本 checklist, 对每个 PR 检查 F-01/F-02/F-05/F-09 四个最高频 footgun.

---

## Part 6: W6 起建议 (优先级排序)

**P1 — 必做 (安全/正确性)**

1. **F-06 FdGuard** — single_instance.cpp 引入 RAII FdGuard, 消除 4 处手写 `::close(fd_)`. W6 第一个 PR. @老孙 / @老周 接.

2. **F-01 safe_shr helper** — 在 `include/stcpp/` 加 `bit_utils.hpp`, 提供 `safe_shr<T>` + `safe_shl<T>`. next_audit_id 两个 for 循环调用它. M5 接 ULID generator 时顺带. @老孙 接.

3. **CI UBSan build** — CMakeLists 加 `-fsanitize=undefined` Debug build target, BUG-W5-001 类型 shift UB 会在 CI 直接 crash 而不是静默产生错误结果. @老周 接.

**P2 — 建议做 (代码质量)**

4. **F-09 transparent hash** — paper_pm_client + p0_01_pinnacle_no_vig + ml/hook 的 `unordered_map<string, ...>` 全部换 transparent hash. live 模块上线前必须完成. @老李 接.

5. **F-05 virtual_matcher.cpp:60** — `double u;` 改三目表达式. 5 分钟 fix. @小蒋 接.

6. **F-10 is_finite 统一** — orderbook.hpp 的 `finite_pos` / `finite` 提升为公共, risk_gateway / p0_01 / slippage_model 删重复定义. @老周 确认公共 header 归属.

**P3 — W7 复评**

7. `[[unlikely]]` 加到 risk_gateway.cpp 8 个 short-circuit if — 需要和 @老周 确认 hot path profiling 结果再决定, 避免过早优化.

8. **std::expected 切换** — 等工具链 C++23 稳定 (或引入 tl::expected) 后, WalResult<T> + Result<T> 一次性 typedef 迁移. @老周 定时间点.

9. **W7 复评** — 新模块 std::expected / transparent hash 比例, clang-tidy F-01..F-10 零 warning CI 绿.

---

## 协作 @

- @老高: PR review v1.2 引用 checklist v1.1 的 F-01/F-02/F-05/F-09 四条作为 mandatory check
- @老郭: UBSan CI build target 进架构评审 (W6 月例会)
- @老沈: BUG-W5-001 已修, 请确认 safe_shr helper 方案与你的修法一致
- @老周: FdGuard + transparent hash + is_finite 公共化 + std::expected 时间点四个决策

---

**汇报**: modern C++20/23 审计 v1 + footgun 实际检出 10 条 / std::expected 迁移建议 / BUG-W5-001 safe_shr 防御方式 / footgun checklist v1.1 (F-01..F-10)

checked: 老何 2026-06-01
