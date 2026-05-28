# 代码规范 v1

- Owner: 老高 (code-quality-reviewer)
- Last review: 2026-05-28
- 验收人: 老周 (cpp-chief-architect) + 老郭 (chief-architecture-reviewer)
- 状态: DRAFT (待 v0.2 与老何 footgun v1 cross-check 后转 ACTIVE)
- 适用范围: `src/`, `include/stcpp/`, `tests/`, `tools/` 全部 C++ 代码; 不覆盖 `docs/` (走小米 `CONVENTIONS-naming.md`).
- 关联依赖:
  - `docs/RESEARCH/laozhou-architecture-v0.1.md` (5 层 + RiskGateway 边界)
  - `docs/RESEARCH/laojiang-latency-budget-v1.md` (热路径预算, 决定性能反模式)
  - `docs/RESEARCH/xiaoshi-data-structures-selection-v1.md` (lock-free 原语)
  - `docs/RESEARCH/laoshen-threat-model-v1.md` (安全编码红线)
  - `docs/RESEARCH/laohe-cpp-footgun-checklist-v1.md` (footgun, 老何, 本文不重写)
  - `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (D1 C++20 / D2 RiskGateway link 阻断 / D3 fail-fast)
  - `docs/CONVENTIONS-naming.md` (文档命名, 与本文代码命名互不冲突)

> 公司宪法: **默认无注释, 只写 WHY**. 代码自解释是第一选择, 注释是不得不留的 WHY.
> 本文不重写老何 footgun (lifetime / iterator / move-after-use 等), 那是 PR review 第二张表; 本文是第一张表, 覆盖**风格 + 红线 + 流程**.

---

## 0. 出场口径

老高出场只看 4 件事:

1. **命名 / 风格** 是否齐整 (一眼能扫出违规).
2. **WHY 注释** 是否到位, WHAT 注释 是否清零.
3. **红线 §9** 是否触碰 (触碰即一票否决, 不商量).
4. **PR 模板 §7 + CI 门禁 §8** 是否过.

性能反模式 §5 我点名, 但具体调优让 @老姜 上.
C++ idiom 深度让 @老何 上 (lifetime / move / coroutine 那一摊).
安全 audit 让 @老沈 上 (我只挡红线显性违规, 不做 threat model).

---

## 1. 命名

### 1.1 总原则

| 维度 | 规则 |
|---|---|
| 字符集 | ASCII 全集, 不允许任何非 ASCII 字符 (含中文 / emoji / 全角符号) |
| 大小写 | 见 §1.2 表 |
| 词分隔 | 按 case 风格, 不混用 |
| 缩写 | 业务术语 (CLOB / WSS / RM / EIP712) 大写不缩写; 通用缩写 (id/url/ip) 小写 |
| 单元 | 涉及物理单位的变量名**必须**带后缀 (见 §1.3) |
| persona | 公司内部 persona 名 (老周/老韩/小石/...) **禁止**进入代码任何位置 (见 §1.4) |

### 1.2 实体命名表 (硬性)

| 实体 | 风格 | 例子 |
|---|---|---|
| 类 / 结构体 / 枚举类型 | `UpperCamelCase` | `OrderBookL2`, `RiskGateway`, `OrderIntent` |
| 函数 / 方法 | `lower_snake_case` | `evaluate_intent`, `apply_book_delta`, `try_send` |
| 局部变量 / 成员变量 | `lower_snake_case` | `audit_id`, `last_seen_ms` |
| 成员变量 (private/protected) | `lower_snake_case_` (尾下划线) | `book_`, `inventory_`, `audit_writer_` |
| 全局变量 | **禁止**, 只允许 `constexpr` 常量 | — |
| 编译期常量 | `kUpperCamelCase` 或 `UPPER_SNAKE_CASE` | `kMaxOrderQty`, `WSS_HEARTBEAT_TIMEOUT_MS` |
| 枚举值 (enum class) | `UpperCamelCase` | `Decision::Approved`, `HaltReason::StaleWss` |
| 命名空间 | `lower_snake_case` 单层, 不超过 3 层 | `stcpp::risk`, `stcpp::data::book` |
| 模板参数 | `UpperCamelCase`, 单字母仅 `T`/`U`/`E` 通用 | `template <class Event>` |
| 宏 (尽量不用) | `STCPP_UPPER_SNAKE_CASE` | `STCPP_LIKELY`, `STCPP_NORETURN` |
| 文件 (.hpp/.cpp) | `lower_snake_case.{hpp,cpp}` | `risk_gateway.hpp`, `order_book_l2.cpp` |
| 测试文件 | `<被测>_test.cpp` 或 `<被测>_bench.cpp` | `risk_gateway_test.cpp` |
| 公共头位置 | `include/stcpp/<layer>/<module>.hpp` | `include/stcpp/risk/gateway.hpp` |
| CMake target | `stcpp_<layer>_<module>` | `stcpp_risk_gateway` |
| 私有头 | `src/<layer>/<module>/internal.hpp` (不进 include/) | — |

**禁用风格:**

- 匈牙利前缀 (`int iCount`, `pStr`) — 禁.
- C 风格 typedef (`typedef struct Foo Foo;`) — 用 `struct Foo {};` 直接.
- 全大写类名 (`class XML_PARSER`) — 禁.
- 单字母变量 (除 `i`, `j` 循环索引和数学公式 `a, b, c`) — 禁.

### 1.3 物理单位后缀 (硬性, 一票否决)

涉及单位的标量变量 / 函数返回值, 名字结尾**必须**带单位. 类型用 `int64_t` / `double` / `uint32_t` 不保护语义, 名字是唯一防线.

| 维度 | 后缀 | 类型建议 | 例 |
|---|---|---|---|
| 时间 | `_ns`, `_us`, `_ms`, `_s` | `int64_t` | `evaluate_p99_us`, `stale_threshold_ms`, `wal_fsync_us` |
| 价格 (Polymarket [0,1]) | `_prob` | `double` | `fair_value_prob`, `best_bid_prob` |
| 价格 (基点) | `_bps` | `int32_t` | `spread_bps`, `slippage_bps` |
| 数量 (份 / 张) | `_qty` | `int64_t` (整数份) | `order_qty`, `fill_qty` |
| 金额 (USDC, 最小单位 6 decimals) | `_usdc6` | `int64_t` | `notional_usdc6`, `daily_pnl_usdc6` |
| 金额 (浮点显示用, 非账本) | `_usd` | `double` | `display_pnl_usd` (只在 UI/log) |
| 比例 / 百分比 | `_ratio` (0..1) 或 `_pct` (0..100) | `double` | `kelly_fraction_ratio`, `cpu_load_pct` |
| 大小 / 字节 | `_bytes`, `_kb`, `_mb` | `size_t` | `ring_capacity_bytes`, `wal_segment_mb` |
| 计数 / 频率 | `_count`, `_per_s` | `int64_t` / `double` | `audit_count`, `intents_per_s` |
| 链上 gas | `_gwei` | `int64_t` | `max_gas_gwei` |
| Nonce / 序号 | `_nonce`, `_seq` | `uint64_t` | `tx_nonce`, `book_seq` |
| 链上区块 | `_block` | `uint64_t` | `fill_block` |

**例子 (禁 vs 允许):**

| 禁 | 允许 |
|---|---|
| `int timeout = 30000;` | `int32_t timeout_ms = 30000;` |
| `double price = 0.42;` | `double price_prob = 0.42;` (或 `bid_prob`) |
| `int64_t pnl = ...;` | `int64_t pnl_usdc6 = ...;` |
| `double spread = 0.005;` | `int32_t spread_bps = 50;` (Polymarket 整数化推荐) |

**违规处理**: PR 一律打回, 老高在 review 里点 "rename `xxx → xxx_ms`". 不商量.

**例外**: 单元在类型名里已经显性表达 (`Duration`, `Probability` strong-typedef), 可省后缀, 但需用 `[[nodiscard]]` strong-typedef 且不允许隐式转 raw. v1 不强制上 strong-typedef, 留 Sprint-3 评估.

### 1.4 Persona 不进代码 (硬性, 一票否决)

公司内部 persona 名 (老周 / 老韩 / 小石 / 老郭 / 老高 / 老何 / 老雷 / 小米 / ...) **禁止**出现在:

- 代码 (任何 `.hpp` / `.cpp` / `.cmake` / CI 配置).
- commit message 的代码 diff 标识符里.
- 错误信息 / log 内容 (用模块名 / 角色名替代).

**为什么**: persona 是组织协作工具, 不是技术 artifact. 班底会换 (新人接手 / 离场), 代码里写"老韩 RiskManager"会变成考古信号. 用角色名 (`RiskOwner`, `risk_engineer`) 都不写; 用**模块名 / 类名 / 文件名**说话.

**允许例外**:

- `CODEOWNERS` 文件可以用 GitHub username (不是 persona 拼音).
- ADR / RESEARCH **文档** 内允许 persona 名 (那是给人看的协作记录, 不是代码).
- 注释里 `// see ADR-001` 允许; `// ask 老韩` 禁止.

**检测**: CI 跑 grep 扫源代码与配置, 命中 persona 拼音 (`laozhou|laohan|laogao|...`) 直接 fail.

### 1.5 通用反模式速查

| 反例 | 改成 | 原因 |
|---|---|---|
| `class Manager` | `class RiskGateway` / `class NonceLedger` | Manager 是噪音词 |
| `void process(...)` | `void evaluate_intent(...)` / `void apply_fill(...)` | process 没说处理什么 |
| `auto data = ...;` | `auto book_snapshot = ...;` | data 是噪音词 |
| `std::vector<int> v;` | `std::vector<int> prices_prob_bps;` | 单字母仅循环索引 |
| `bool flag = ...;` | `bool is_halted = ...;` / `bool has_open_order = ...;` | bool 必须 is_/has_/can_/should_ 前缀 |
| `int32_t timeout = 30000;` | `int32_t timeout_ms = 30000;` | 缺单位 |
| `MarketHaltSwitch laohan_switch;` | `MarketHaltSwitch market_halt_switch;` | persona 不进代码 |

---

## 2. 注释 (公司宪法: 默认无注释, 只写 WHY)

### 2.1 注释三档

| 档 | 必须写 | 默认不写 | 禁止写 |
|---|---|---|---|
| **WHY** | 非平凡不变量 / workaround / 跨工种约定 / 引用 ADR | — | — |
| **WHAT** | — | 代码自解释 | 直接重复代码的注释 |
| **HOW** | — | 代码即 HOW | 解释 "怎么实现" 而不是 "为什么这样" |

### 2.2 必须注释的 6 种场景

1. **非平凡不变量** — 类 / 函数前置条件或全局不变量, 读代码看不出.
   ```cpp
   // INVARIANT: nonce_ 单调递增且持久化先于使用 (S1-005 / ADR-001 §3.3).
   //   崩溃恢复时 max(WAL, on-chain) 重建, 不允许复用.
   class NonceLedger { ... };
   ```

2. **workaround** — 绕开第三方库 / OS / 硬件 bug.
   ```cpp
   // WORKAROUND: rigtorp SPSC 在 Apple Silicon (M2) 上 false-sharing
   //   阈值是 128 字节 (而非 x86 64), 必须显式 padding 到 128.
   //   见 xiaoshi-data-structures-selection-v1.md §1.2.
   struct alignas(128) Slot { ... };
   ```

3. **跨工种约定** — 接口由别的 owner 决定, 实现方说明为什么是这个口径.
   ```cpp
   // CONTRACT (老韩 ADR-001 §3.3): evaluate() 必须同步返回 audit_id;
   //   WAL fsync 异步, 但 SPSC append OK 即视为持久.
   //   不要把 fsync 改成同步以"更安全" -- 会爆 200us 预算.
   Decision evaluate(OrderIntent const& intent);
   ```

4. **ADR 引用** — 决策来源, 防止后来人觉得"这写得真奇怪" 然后改回去.
   ```cpp
   // RATIONALE: shared_ptr 在热路径禁用 (ADR-001 D6 / 架构 v0.1 §5 D6).
   //   策略层用 arena + unique_ptr.
   ```

5. **性能 hack** — 看似多余的代码其实在 squeeze cycles.
   ```cpp
   // PERF: branchless check (老姜 latency-budget §1 阶段 7 < 60us).
   //   不要改成 if-else, 实测 p99 退步 18us.
   uint64_t mask = -static_cast<uint64_t>(is_halted);
   ...
   ```

6. **红线提醒** — §9 红线相关代码, 留显式锚点.
   ```cpp
   // REDLINE: 任何调用 signer_.sign() 之前必须先 RiskGateway::evaluate()
   //   且 decision == Approved. CI 静态扫会卡这一条 (ADR-001 §2.2).
   ```

### 2.3 禁止注释的 5 种

1. **WHAT 已被代码表达**:
   ```cpp
   // BAD: 噪音
   i++;  // increment i
   audit_count_++;  // 增加 audit 计数
   ```

2. **TODO 长期挂账 (无主无期)**:
   ```cpp
   // BAD: 没人接, 没期限, 永远不会做
   // TODO: 优化这里
   ```
   **规则**: TODO 必须带 owner + 期限 + 关联 ticket, 否则不允许 merge.
   ```cpp
   // TODO(老李, Sprint-2, S2-014): 切到 simdjson on-demand, 当前 DOM 模式吃 5us.
   ```
   长期 (跨 2 个 Sprint) 仍未关的 TODO, 老高在月末扫一遍, owner 不回复 = 老胡介入.

3. **被改时不会同步的废话**:
   ```cpp
   // BAD: 维护负担
   // returns the price as a double in range [0, 1]
   double get_price();
   ```
   名字 `price_prob`(§1.3) 已经说了; 不要写注释.

4. **persona 名 / 私人吐槽**:
   ```cpp
   // BAD
   // 老韩说要这样, 我也不知道为啥
   // 这段是 Xiao Shi 写的, 别动
   ```

5. **commented-out 代码**:
   ```cpp
   // BAD
   // void old_function() { ... }
   ```
   git 是版本控制, 不是注释草稿本. 删干净, 要找 `git log`.

### 2.4 Doxygen

| 范围 | 启用? | 强制? |
|---|---|---|
| 公共头 `include/stcpp/**/*.hpp` | 启用 | 每个 public class / 函数必填 `/** brief + 关键参数语义 */`, 只写 WHY/CONTRACT, 不写 WHAT |
| 内部 `src/**/*.{hpp,cpp}` | 不启用 | 不强求, 走 §2.2 WHY 注释即可 |
| 模板 / Concept | 启用 | concept 必须有 `/** brief */` 解释约束意图 |
| 测试 | 不启用 | — |

**Doxygen 命令子集 (只用这几个, 别花式)**:
- `@brief` 一行说明 (必须).
- `@param` 仅当参数语义不能从名字看出时才写.
- `@return` 仅当返回值含错误码 / 多模态时才写.
- `@pre` / `@post` 前置 / 后置条件 (非平凡时必写).
- `@throws` **禁用** — 我们走 `Result<T,E>`, 不抛异常 (§4).

**反例 (Doxygen 也会 noise)**:
```cpp
/// @brief Get the price        <-- 废
/// @return double price        <-- 废
double get_price();
```

**正例**:
```cpp
/**
 * @brief 估值器: 给定 FeatureSnapshot, 输出 Moneyline 主队公允概率.
 * @pre  snapshot.heartbeat_ok == true (上游必须先验过 heartbeat).
 * @post 返回值在 [0.01, 0.99]; 越界拒绝出价 (策略层兜底, 不在此函数).
 *
 * RATIONALE: 用 logit 空间累积特征, 防止极端值溢出
 *   (见 xiaomi-microstructure-vN.md §3.2, 待小袁 v1).
 */
double fair_prob(FeatureSnapshot const& snapshot) noexcept;
```

---

## 3. 头文件 / 包含

### 3.1 文件结构

每个 `.hpp` 模板:

```cpp
// SPDX-License-Identifier: <TBD by 老黄> 或 项目自有许可
#pragma once

// 1) own corresponding header (.cpp 才有, .hpp 跳过)
// 2) C system headers
// 3) C++ standard headers
// 4) third-party headers (按字母序)
// 5) project headers (按 layer 顺序: infra → data → strategy → risk → exec)

#include <cstdint>
#include <span>
#include <string_view>

#include <absl/container/flat_hash_map.h>
#include <rigtorp/SPSCQueue.h>

#include "stcpp/infra/error/result.hpp"
#include "stcpp/data/book/order_book_l2.hpp"

namespace stcpp::risk {

class RiskGateway {
  // ...
};

}  // namespace stcpp::risk
```

`.cpp` 模板:

```cpp
#include "stcpp/risk/gateway.hpp"   // 对应头文件**第一个**, 暴露自包含问题

#include <chrono>
#include <utility>

#include <absl/strings/str_format.h>

#include "stcpp/infra/log/logger.hpp"

namespace stcpp::risk {
// ...
}
```

**规则**:

1. `.cpp` 第一个 include **必须**是对应的 `.hpp` (自包含验证).
2. 用 `#pragma once`, 不用 include guard (统一性 + 少出错).
3. 头文件分组之间空一行, 组内按字母序.
4. 第三方头用 `<>`, 项目头用 `""` (clang-format 也按这个区分).
5. 不允许 `using namespace std;` (任何地方); 头文件不允许 `using` 任何东西 (只许在函数体内).
6. 不允许全局 inline / extern 变量 (除 constexpr).

### 3.2 模块边界 (架构 v0.1 §2.4 强约束)

依赖方向严格按 5 层 (L5 → L4 → L3 → L2 → L1), 反向 = 编译期 / link 期失败.

**Include 红线 (§9 §9.5):**

| 来源层 | 允许 include | 禁止 include |
|---|---|---|
| L5 exec/ | L4 RiskGateway public + L1 | L4 `risk/limits` / `risk/audit` / `risk/halt` 任何内部头 (link 隔断 + CI grep) |
| L4 risk/ | L1 + 只读 L2 feature view | L3 strategy / L5 exec (任何方向都不许) |
| L3 strategy/ | L2 FeatureStore + L1 | L4 内部 / L5 任何 |
| L2 data/ | L1 | L3 / L4 / L5 任何 |
| L1 infra/ | C++ std + 三方 | 项目任何上层 |

**CMake 实施 (老吴 + 老周):**

```cmake
add_library(stcpp_risk_gateway STATIC ...)
target_link_libraries(stcpp_risk_gateway
  PUBLIC  stcpp_risk_intent_only  # 只暴露 OrderIntent 类型, 给 L3
  PRIVATE stcpp_risk_internal     # limits/audit/halt, 不传染
          stcpp_infra)

add_library(stcpp_exec STATIC ...)
target_link_libraries(stcpp_exec
  PRIVATE stcpp_risk_gateway      # L5 只见 RiskGateway, 不见 risk_internal
          stcpp_infra)
```

`tests/` 允许 link `stcpp_risk_internal` 做 white-box test, 但 production binary target 链不到.

### 3.3 Forward declaration

**用 fwd decl 的场景:**

- 头文件里**只用指针 / 引用** 一个类型 → fwd decl (削编译时间).
- 模板参数 (T 只是 type, 不需要全定义).
- 循环依赖避免 (两个类互引指针).

**不用 fwd decl 的场景:**

- 头里需要 sizeof / 完整成员 / 模板特化 / inline 函数体 → include 全头.
- 标准库类型 (`std::string`, `std::vector`) → 直接 include `<string>` / `<vector>`, **不要**自己 fwd decl std (UB).
- enum / enum class → 用 `enum class Foo : int;` opaque decl 即可, 不需要完整 enum.

**反例 (UB)**:
```cpp
namespace std { template <class T> class vector; }  // BAD: undefined behavior
```

**正例**:
```cpp
namespace stcpp::data { class FeatureSnapshot; }  // OK: 自己的类型 fwd 没问题

namespace stcpp::strategy {
class MoneylinePricer {
 public:
  void on_snapshot(data::FeatureSnapshot const& snap);  // 只用引用, fwd 够了
};
}
```

### 3.4 包含粒度

- 公共头 (`include/stcpp/`) 尽量瘦, 把实现细节挪到 `.cpp` 或私有 `internal.hpp`.
- 模板实现放 `.tpp` (`#include` 在 `.hpp` 末尾) 或 `.hpp` 内, 不放 `.cpp`.
- pimpl 模式只在**需要 ABI 稳定 / 跨大模块** 场景用 (我们大多模块不需要, 别滥用).

---

## 4. 错误处理 (Result 锁)

### 4.1 架构锁 (ADR-001 D3): 不抛异常, 用 `Result<T, Status>`

**任何热路径函数不允许抛异常.**

- 函数签名: `[[nodiscard]] Result<T, Status> f(...) noexcept;`
- 三方库可能 throw 的入口 (`infra/net/*`, `infra/serde/json`) 在适配层 `try/catch` 转 `Status`, 异常**不许跨层**.
- STL 抛异常的接口尽量避开: `vector::at()` → `operator[]` + bound check, `string` 构造前 reserve, 不用 `std::stoi` (用 `std::from_chars`).

### 4.2 `Result<T, Status>` 用法

```cpp
// infra/error/result.hpp (自研, 老周 D3 + ADR §2.1)
template <class T>
class [[nodiscard]] Result {
 public:
  static Result ok(T value);
  static Result err(Status status);

  bool is_ok() const noexcept;
  T&         value() &;           // precondition: is_ok()
  Status const& status() const&;  // precondition: !is_ok()
  // monadic: and_then / or_else / map (可选, Sprint-2 加)
 private:
  // tagged union, sizeof <= 16 + sizeof(T), trivially_copyable when T is
};
```

**调用方两种模式 (二选一, 不混):**

A. **显式 if-return** (推荐, 热路径 / 风控路径):
```cpp
auto r = book_.apply_delta(msg);
if (!r.is_ok()) {
  log_warn("book apply failed: {}", r.status().code);
  return Result<Decision, Status>::err(r.status());
}
```

B. **宏 `STCPP_TRY`** (语法糖, 非热路径; v1 实现):
```cpp
auto book_seq = STCPP_TRY(book_.apply_delta(msg));   // 失败立即 propagate
```
宏展开成 `if-return`, 不影响热路径. 但**不允许**在 noexcept 边界上跨用 (会被 lint 拦).

### 4.3 `Status` 字段

ADR-001 §2.1 锁:
- `sizeof(Status) <= 16 byte`.
- trivially copyable POD.
- 内嵌 `error_code` enum + `audit_id` (与风控 audit 关联).

```cpp
struct Status {
  enum class Code : uint16_t {
    Ok = 0,
    InvalidArgument,
    PreconditionFailed,
    StaleData,
    BookSeqGap,
    RiskRejected,
    SignerUnavailable,
    NetworkTimeout,
    InternalError,
    // ...
  };
  Code     code;
  uint16_t source_module;  // L1/L2/L3/... + 模块编号
  uint64_t audit_id;       // 0 = 无, 非 0 = 关联 RiskAudit
  // 共 12 byte, 余 4 byte 留扩展
};
```

### 4.4 assert / invariant

| 类型 | 用法 | 编译期 / 运行期 |
|---|---|---|
| `static_assert(...)` | 编译期不变量 (类型大小 / concept 满足) | 编译期 |
| `STCPP_DEBUG_ASSERT(expr)` | 调试期不变量, Release 编译展开为空 | 仅 Debug |
| `STCPP_INVARIANT(expr)` | Release **也保留**, 违反则 abort + core dump (走 ADR-001 §2.3 fail-fast 路径) | Debug + Release |
| `STCPP_EXPECTS(expr)` | 函数前置条件; 违反 = caller bug, abort | Debug + Release |

**规则**:
- `assert()` (C 标准) **禁用** — Release 关掉太危险, 用我们自己的 `STCPP_DEBUG_ASSERT`.
- 不变量违反走 abort, 不走异常 / 不走 Result (那不是错, 是 bug).
- `STCPP_INVARIANT` abort 前必须调 ADR-001 §2.3 的 best-effort flush (signal handler 内).

### 4.5 errno / status code

- POSIX syscall 返回 -1 时**必须**立刻读 `errno`, 转 `Status`. 不允许在中间穿插任何调用 (会污染 errno).
- 不允许在业务层直接看 `errno`, 一律走 `Status`.
- 不允许把 `errno` 数字直接 log (无可移植性), 用 `std::error_category` 转描述.

---

## 5. 性能反模式 (老姜配合, 细节去问)

老高在 PR 里点这几条**显性**反模式, 命中即 review comment. 深度调优 (cache miss / branch predict / NUMA) 让 @老姜 上.

### 5.1 [block] 热路径禁 `std::shared_ptr`

**热路径定义**: §11 性能预算覆盖的代码 (signal → order intent → RM → exec), 含 §1 阶段 1-10.

**为什么**: 原子引用计数在多核 cache line ping-pong, 实测吃掉 5-30 us.

**正例**: arena allocator + `unique_ptr` + 移动; 跨线程用 SPSC 转移所有权; 共享只读用 `RcuPtr<Snapshot>` (小石 §1).

### 5.2 [block] 任何路径禁 `std::endl`

**为什么**: `endl = '\n' + flush`, 每次都 fsync 流, 吞吐损 10x+.

**正例**: 用 `'\n'`, 刷新走显式 `os.flush()` 或 logger 后端 (异步 ring sink).

### 5.3 [block] 热路径禁 dynamic_cast / RTTI

**为什么**: `dynamic_cast` 走 RTTI 链, 多继承下 O(n), 不可预测.

**ADR 锁**: 编译用 `-fno-rtti` (老何 footgun §0 + ADR-001 D1). dynamic_cast 编译失败.

**替代**: tagged union (`std::variant<...>`) + `std::visit`, 或自研 enum + switch.

### 5.4 [block] 不必要的虚函数 (热路径)

**判定**: 同一函数在热路径每秒调用 > 1k 次, 且不存在跨实现切换 (例如 mock test), 则不应虚.

**为什么**: vtable 跳转破坏 inlining, branch predict 失败, 实测损 2-15 us.

**替代**: CRTP (Curiously Recurring Template Pattern) 或 `if constexpr` 分发.

允许虚的场景: 测试 mock 注入 / 启动期一次性多态 / 非热路径 (例: `ConfigStore::on_reload`).

### 5.5 [block] `std::regex` 任何路径

**为什么**: GCC libstdc++ 实现慢 100x, 不可预测; 而且不该出现在交易系统里.

**替代**: hand-rolled parser / `std::string_view::starts_with` / `re2` (三方, 启动期 / 离线工具可用, 不入热路径).

### 5.6 [block] 隐式分配 (热路径)

热路径任何函数禁止:

- `std::vector::push_back` 而不 reserve.
- `std::string` 拼接 (`a + b`) — 用 `absl::StrAppend` 或 `std::format_to` 预分配.
- `std::function` 类型擦除 (有 small-buffer 优化但仍有间接) — 用 lambda + template 或函数指针.
- `new` / `malloc` (任何形式) — 走 arena / object pool.
- `std::unordered_map::operator[]` 在 hot read 路径 (插入分配) — 用 `find` + 显式 insert.

### 5.7 [warn] `std::optional<T&>` (C++26 才正)

C++20 不支持 `optional<T&>`, 用 `T*` (raw pointer, 不持有所有权) + nullable contract 在文档说明.

### 5.8 [warn] 不必要的拷贝

- 函数参数大对象用 `T const&` 或 `std::span<T>`; 小 POD 直传值.
- range-for: `for (auto x : v)` 拷贝; `for (auto const& x : v)` 不拷贝.
- 不要 `return std::move(local)` (抑制 RVO, 老何 footgun 有, 这里点一下).

### 5.9 [warn] `std::list` / `std::map` / `std::deque`

L2 cache 不友好, 几乎从不该用. 用 `std::vector` / `absl::flat_hash_map` / `boost::circular_buffer`.

例外: `std::deque` 用于偶发 push_back 的非热路径队列 OK.

---

## 6. 安全编码 (老沈 + 老孙 配合)

老高这里只挡**显性**违规. 完整威胁见 `laoshen-threat-model-v1.md`, 私钥架构见 `laosun-key-management-v1.md`.

### 6.1 [block / 红线 §9.2] 私钥相关代码

**任何私钥相关代码 (signer 模块 + KMS adapter)**:

1. **禁 log 任何私钥字段** — 包括 raw bytes / hex / base64 / partial (前 4 字节也禁).
   - log 时只允许打 `key_id` (KMS handle) 或 `pubkey_hash`.
2. **禁 `memcpy(dst, key, len)` 留 trace** — 用 `secure_memzero(buf, len)` 在使用后立即清零; `memcpy` 临时缓冲必须 `mlock` + 使用后 `memzero`.
3. **禁私钥进 `std::string` / `std::vector<uint8_t>`** (常规容器会 realloc 留 copy) — 用固定大小 `std::array<uint8_t, 32>` + `mlock` + 析构 `memzero`.
4. **禁私钥跨进程 / 跨线程不加密传递** — TB-C 边界, signer 子进程独立 (老沈 threat-model §2 TB-B/C).
5. **禁 GDB / core dump 包含私钥页** — `madvise(MADV_DONTDUMP)` + `prctl(PR_SET_DUMPABLE, 0)` 在 signer 进程.
6. **禁私钥做参数命名** — 不要 `void f(uint8_t* private_key)`, 用 `KeyHandle`(opaque) 包装 + 只在 signer 内部解封.

CI 静态扫: grep 源码任何 `private_key` / `priv_key` / `secret_key` 字符串作为变量名 + log 调用同一函数体内出现, 直接 fail.

### 6.2 [block] 输入验证边界

所有**外部输入**(Polymarket WSS / REST, Goalserve, Polygon RPC, 配置文件, IPC 消息) 进入系统**第一个**函数必须做:

| 校验项 | 工具 |
|---|---|
| Schema 校验 (字段存在 / 类型对) | simdjson on-demand + 自研 schema 表 |
| 数值范围 (price ∈ [0,1], qty > 0, ...) | `STCPP_EXPECTS` 或 `Result::err(InvalidArgument)` |
| 字符串长度上限 | `view.size() <= kMaxXxxLen`, 越界 reject |
| 整数溢出 | `__builtin_add_overflow` / `boost::safe_int` |
| 数组索引 | `at()` 或显式 bound check |
| 时间戳合理 (不来自未来 / 不来自 1970) | `now_ms - ts_ms in [-5s, +5s]` |
| 重放攻击 (idempotency_key) | 老韩 §3.8 LRU + TTL |

**红线**: 任何外部输入直接进 hot-path 数据结构 (book / position) 而未经 normalize 层校验 = §9.5 红线.

### 6.3 [block] TLS / cert pin

- 所有 egress HTTPS / WSS **必须**走 `infra/net::TlsSession`, 不允许任何业务模块自己创 socket.
- `TlsSession` 强制 cert pinning (老沈 M-N1), 起始 PIN 列表硬编码二进制, 不走配置 (防篡改).
- TLS 版本 ≥ 1.2, 优先 1.3; 不允许任何 SSLv*.

### 6.4 [block] 加密 / 随机

- 任何 "加密 / 签名 / nonce 生成" **必须**走 `infra/crypto::*` (调老孙的库), 不允许业务层自己 `EVP_*` / `secp256k1_*`.
- 随机数: 加密用途用 `crypto::secure_random()` (内部 `getrandom`), 不允许 `std::rand` / `srand`.
- 业务用途的随机 (例如 jitter): `std::mt19937` + 显式 seed (可重放).

### 6.5 [block] 敏感字符串处理

- `std::string` 对密钥 / token 不安全 (SSO 会留副本). 用 `SecureBuffer` 类 (待 老孙 v1.x 实现).
- 不允许 `printf` / `iostream` 输出敏感字段 (会进 stdout/stderr 默认 buffer).
- `.env` / 配置文件中的 secret 必须以 `${SECRET_REF:xxx}` 形式间接, 不允许明文.

---

## 7. PR 模板 + Reviewer 分配

### 7.1 PR 模板位置

`.github/PULL_REQUEST_TEMPLATE.md` (本规范配套创建).

### 7.2 PR 描述结构 (强制)

每个 PR 描述必须有以下 6 节, 不全 = 老高 review 拒绝, 不进 CI.

1. **What** — 一句话说改了什么 (功能 / fix / 重构).
2. **Why** — 链接到 ADR / RESEARCH / Sprint ticket, 不允许"老板让我做的".
3. **影响模块** — 列出涉及的层 (L1/L2/L3/L4/L5) + 具体模块路径.
4. **风控相关? (Yes/No)** — 如果碰 `risk/` / `exec/signer` / `exec/router` 任意一个文件 = Yes, **必须**老韩 + 老郭双签.
5. **测试** — 跑了哪些 test (unit / integration / replay / sanitizer / bench), 贴关键输出.
6. **回滚预案** — feature flag? revert commit? 数据迁移有逆向?

### 7.3 自检 checklist (强制, 在 PR body 内勾)

- [ ] 命名遵守 §1 (单位后缀 / 无 persona / 无匈牙利)
- [ ] 注释遵守 §2 (无 WHAT, 无僵尸 TODO, WHY 必到)
- [ ] include 顺序遵守 §3.1, 模块边界遵守 §3.2
- [ ] 错误处理用 `Result<T,Status>`, 无 throw, 无 raw `assert()`
- [ ] 热路径无 shared_ptr / endl / dynamic_cast / regex / new
- [ ] 私钥 / secret 相关变更已通知 @老沈
- [ ] 跑过 `clang-format` + `clang-tidy` + ASan + UBSan, 无新增警告
- [ ] 单测覆盖率本 PR diff > 70%
- [ ] 没引入 `#ifdef SKIP_RM` / `// FIXME 不影响功能` 之类的 dirty hack
- [ ] 文档同步: 改了公共 API 已同步 ADR / RESEARCH (通知 @小米)

### 7.4 Reviewer 分配矩阵

| 改动范围 | 必 review | 选 review |
|---|---|---|
| `infra/` | 老周 + 1 个 IC (小卢) | 老姜 (性能相关) |
| `data/` 任意 | 小余 + 老李 (Polymarket) 或 小董 (Goalserve) | 小石 (lock-free) |
| `data/feature` | 小梁 + 小余 | 小程 (信号) |
| `strategy/` | 小梁 + 老彭 (赌业知识) | 小程 / 小蒋 / 小袁 看具体子模块 |
| `risk/` 任意 | **老韩 + 老郭** (双签, 红线) | 老沈 (安全) / 老唐 (审计) |
| `exec/signer` | **老孙 + 老沈** (双签, 红线) | 老韩 |
| `exec/router` / `exec/clob` | **老韩 + 老李** (双签) | 老郭 |
| `exec/nonce` | 老孙 + 老叶 | 老韩 |
| `exec/fill` / `exec/recon` | 小肖 / 老彭 | 老韩 (账本) |
| CMake / CI / 部署 | 老吴 | 老郭 |
| 配置 schema | 老陈 (ConfigStore) + 影响模块 owner | — |
| **任何** PR | **老高** 走 §1-§3 风格门 + §9 红线扫 | — |

**单独红线 PR (改 RiskGateway public ABI / signer 接口 / nonce ledger)**: 必须 **老雷知情** + 双签全到. 老高在 review 里 @ 老雷.

### 7.5 PR 大小

- 单 PR diff > 1000 行 = 老高建议拆分 (除非是新模块首次落地或大规模 rename).
- 单 PR 触及 > 3 层 = 老郭出场前置审 (跨层架构问题, 别走小 PR 通道).
- 紧急 hotfix: 例外, 但**必须**带 incident 编号 + post-fix PR 24h 内补全规范.

---

## 8. CI 强制门禁

老吴 + 老郭 落地, 老高代为列单. 以下 9 关任何一关挂 = block-merge, **不允许 waiver**.

| # | 关卡 | 工具 | 失败动作 |
|---|---|---|---|
| 1 | 编译 (Debug + Release + Asan + Tsan + Ubsan) | clang 16+, gcc 13+, `-Wall -Wextra -Werror -Wshadow -Wconversion -Wpedantic` | 任意 warn 阻塞 |
| 2 | 格式 | `clang-format` (`.clang-format` 跟随 §1) | diff 非空阻塞 |
| 3 | 静态 lint | `clang-tidy` (自定义检查集, 见 §8.1) | 任意 error / warning 阻塞 (warn 也阻) |
| 4 | 静态分析 | `cppcheck`, `include-what-you-use`, `cpplint` (轻量) | error 阻塞 |
| 5 | 单测 | gtest / Catch2, 覆盖率 > 70% (gcovr) | < 70% 阻塞; 本 PR diff < 70% 阻塞 |
| 6 | Sanitizer 跑全套 unit + integration | ASAN, UBSAN, TSAN, MSAN(可选) | 任何 issue 阻塞 |
| 7 | 性能回归 (bench 关键 path) | 老姜 §4 benchmark suite | 阈值见 老姜 v1 §4.1, 越界阻塞 |
| 8 | 红线扫描 | 自研 grep (见 §9 红线 + §1.4 persona) | 命中阻塞 |
| 9 | 文档同步检查 | 自研 (改公共 API 须同步 docs/, 改 ADR 须 cross-link) | 不同步阻塞 |

### 8.1 clang-tidy 必启检查 (子集, 老何 footgun 落地)

```
bugprone-*
cert-*
clang-analyzer-*
concurrency-*
cppcoreguidelines-pro-bounds-array-to-pointer-decay  (禁)
cppcoreguidelines-pro-type-cstyle-cast               (禁)
cppcoreguidelines-pro-type-reinterpret-cast          (禁, 例外见下)
cppcoreguidelines-special-member-functions
modernize-use-nullptr
modernize-use-override
performance-*
readability-identifier-naming                          (走本规范 §1)
hicpp-signed-bitwise                                   (禁有符号位运算)
misc-no-recursion                                      (热路径无递归)
```

例外 (允许 reinterpret_cast): `infra/serde` zero-copy + 网络帧 layout. 文件级 `// NOLINTBEGIN(cppcoreguidelines-...)` + WHY 注释.

### 8.2 编译警告白名单 (零)

不允许 `#pragma warning push/pop` 屏蔽自家代码警告. 三方头屏蔽允许 (`-isystem` 自动 silence).

### 8.3 sanitizer 用例覆盖

| Sanitizer | 跑什么 | 频率 |
|---|---|---|
| ASAN | unit + integration | 每 PR |
| UBSAN | unit + integration | 每 PR |
| TSAN | concurrency-sensitive (lock-free / SPSC / RCU) 子集 | 每 PR |
| MSAN | (可选, clang only) infra/ + serde/ | nightly |
| LSAN | (含在 ASAN) | 每 PR |

### 8.4 #ifdef 限制

- `#ifdef NDEBUG` 允许 (区分 Debug/Release).
- `#ifdef STCPP_TEST` 允许 (启用 test friend 等).
- `#ifdef SKIP_RM` / `#ifdef DISABLE_AUDIT` / `#ifdef MOCK_SIGNER` (在 production target) = **§9 红线**, CI 直接 reject.
- 平台分支 (`__APPLE__` / `__linux__`) 允许, 但必须有 fallback 实现.
- 编译期 feature flag (`#ifdef STCPP_ENABLE_X`) 走 CMake `target_compile_definitions`, 不走源码 `#define`.

---

## 9. 红线 (一票否决, 直接 Reject, 不商量)

老高在 PR review 看到下列任意一条 = **直接关 PR + 通知 owner + 抄送老雷**. 不允许"我下次注意" 式补救; 必须拆 PR 重来.

| # | 红线 | 触发判定 | 文档锚点 |
|---|---|---|---|
| **R-1** | **绕过 `RiskGateway`** — `exec/signer` 任意 sign() 调用前未见同函数 `RiskGateway::evaluate()` Approved 分支 | CI grep (§8 关卡 8) + 人审 | ADR-001 §2.2, CLAUDE §8, 架构 v0.1 §2.4 |
| **R-2** | **私钥落盘 / 进日志** — 任何形式 log / file / metric / stdout 出现私钥字节 (含 partial) | grep + 人审 + 老沈 spot check | CLAUDE §8, threat-model §5.1 §6.1 |
| **R-3** | **数据 schema 静默变更** — 改了 `data/normalize` 字段 / `Event POD` 布局 / 配置 schema 而未通知下游 + 未升版本 | 自动 diff schema + PR body 自检未勾"通知下游" | CLAUDE §8, 架构 v0.1 §1.2 |
| **R-4** | **跳过 audit log** — RM evaluate 或 signer sign 路径出现"快速路径"未写 audit (例如"测试模式""紧急 bypass") | 人审 + ADR-001 §3.3 audit WAL 必经 | CLAUDE §7 §8, ADR-001 §3.3 |
| **R-5** | **跨层非法 include** — §3.2 表禁止边的 include 出现 | clang include-what-you-use + grep + 架构层 link 阻断 | 架构 v0.1 §4, 本文 §3.2 |
| **R-6** | **persona 名进代码** — §1.4 任何形式 | CI grep | 本文 §1.4 |
| **R-7** | **#ifdef SKIP_RM / DISABLE_AUDIT / MOCK_SIGNER 在 production target** | CI grep + CMake target 隔离 | 本文 §8.4 |
| **R-8** | **抛异常跨模块** — 热路径函数 throw 或允许异常逃出 noexcept 边界 | clang-tidy + 编译选项 `-fno-exceptions` | ADR-001 D1, 本文 §4 |
| **R-9** | **僵尸 TODO** — TODO 无 owner / 无期限 / 跨 2 个 Sprint 未关 | CI grep + 月末扫 | 本文 §2.3 |
| **R-10** | **回测 / 实盘逻辑分叉** — feature pipeline 出现"if (is_backtest) ..." 分支 | grep + 小余 review | CLAUDE §8 D-04, 架构 v0.1 §2.2 |

**合计 10 条红线.**

**对老高自己的红线 (元规则):**
- 老高在 review 中点红线必须**引到本文 §9 对应编号**, 不允许只说"这个不行".
- 老高不挑性能 / 安全 / 架构深度议题 — 那些派给老姜 / 老沈 / 老郭, 老高只挡显性违规.

---

## 10. 新人 PR 第一周指南 (与小林 onboarding 联动)

小林负责 onboarding 流程, 老高负责"新人第一个 PR 不踩雷". 联动如下:

### 10.1 新人 Day-1 必读 (小林安排)

1. `CLAUDE.md` (公司宪法).
2. `docs/RESEARCH/laozhou-architecture-v0.1.md` (5 层 + RiskGateway).
3. `docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md` (D1/D2/D3 锁).
4. **本文** `docs/RESEARCH/laogao-code-conventions-v1.md`.
5. `docs/RESEARCH/laohe-cpp-footgun-checklist-v1.md` (走完后做 self-test).

### 10.2 第一个 PR 限制 (老高把关)

- **限制范围**: 不允许碰 `risk/` / `exec/signer` / `data/feature` / `infra/ipc`. 起步只能做 `tests/` / `tools/` / 非热路径 `infra` 文档与小工具.
- **必须配对**: 第一个 PR 指定一个 mentor (单元 owner 或资深 IC) 做 pre-review, 通过后才开 PR.
- **必须跑通**: CI 9 关全过, 不允许 waiver.
- **限定 size**: < 300 行 diff, 学规范不学写大模块.

### 10.3 第一周节奏

| Day | 内容 | Owner |
|---|---|---|
| 1 | 读完 §10.1 5 份文档 + 环境搭好 | 小林 + 老吴 |
| 2 | clang-format / clang-tidy 跑通 + 跑 hello-world test | 老吴 + IC mentor |
| 3 | 选 backlog 上 "good-first-issue" tag 的 1 个 ticket (老胡维护) | 老胡 + mentor |
| 4-5 | 写 + mentor pre-review | mentor |
| 5 末 | 正式开 PR, 老高 review | 老高 + reviewer 矩阵 §7.4 |

### 10.4 老高 onboard 反馈

- 第一周结束, 老高出"新人 PR 总结" 1 页给小林, 列高频违规 → 小林写入 next-onboard tip.
- 滚动版"新人 PR 反例集"维护在 `docs/HIRING/onboard-pr-pitfalls.md` (小林 owner, 老高供素材).

---

## 附录 A: 与其他规范的关系

| 文档 | 重叠 | 权责切分 |
|---|---|---|
| `docs/CONVENTIONS-naming.md` (小米) | 命名 | 那个管 docs/ 文档名; 本文管代码符号名. **不重叠**. |
| `laohe-cpp-footgun-checklist-v1.md` (老何) | C++ 写法 | 那个管 idiom / lifetime / move; 本文管风格 / 红线 / 流程. **互补, 不重写**. |
| `laojiang-latency-budget-v1.md` (老姜) | 性能 | 那个定预算 / benchmark; 本文 §5 只列**显性**反模式. **互补**. |
| `laoshen-threat-model-v1.md` (老沈) | 安全 | 那个定 threat / 缓解; 本文 §6 只列**显性**违规. **互补**. |
| `laozhou-architecture-v0.1.md` (老周) | 模块边界 | 那个定层与契约; 本文 §3.2 落到 include / link 规则. **互补**. |
| `2026-05-28-arch-and-rm-v0.1-review.md` (老郭) | 红线 | 那个是 ADR 锁; 本文 §9 把锁落地到 PR review. **互补**. |
| `laohan-riskmanager-design-v0.1.md` (老韩) | RM 接口 | 那个定 RM 行为; 本文 §7.4 / §9 R-1 / R-4 落到 PR. **互补**. |

---

## 附录 B: 落地待办 (Sprint-1 末 / Sprint-2 初)

| # | 待办 | Owner | 期限 |
|---|---|---|---|
| L-1 | 写 `.clang-format` (按 §1 表) | 老吴 | Sprint-1 末 |
| L-2 | 写 `.clang-tidy` (按 §8.1) | 老吴 + 老何 | Sprint-1 末 |
| L-3 | 写 CI grep 红线脚本 (§9 R-1/R-2/R-6/R-7/R-9) | 老吴 + 老沈 (R-2) | Sprint-2 初 |
| L-4 | 写 `STCPP_INVARIANT` / `STCPP_EXPECTS` / `STCPP_TRY` 宏 + `Result<T,Status>` | 老周 (D1 落地) | Sprint-1 末 |
| L-5 | 写 `secure_memzero` / `SecureBuffer` / `KeyHandle` | 老孙 | Sprint-2 |
| L-6 | CMake link 阻断 RiskGateway (§3.2 + ADR-001 C-Z7) | 老周 + 老吴 | Sprint-1 末 |
| L-7 | 跑通新人 PR Day-5 流程 (招到一个 IC 跑一次) | 小林 + 老高 | Sprint-2 |
| L-8 | 与老何 footgun v1 cross-check 后, 本文转 ACTIVE | 老高 + 老何 + 老郭 | Sprint-2 初 |

---

## 附录 C: 版本历史

| 版本 | 日期 | 变更 | 作者 |
|---|---|---|---|
| v1 (DRAFT) | 2026-05-28 | 首版, Sprint-1 任务 | 老高 |
