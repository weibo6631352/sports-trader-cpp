# C++ Footgun 清单 — PR Review 用 v1

- Owner: 老何 (modern-cpp-advisor)
- Last review: 2026-05-28
- 验收人: 老郭 (architecture-reviewer) + 老周 (cpp-chief-architect)
- 适用范围: sports-trader-cpp 全代码库 PR review
- 关联文档: `docs/RESEARCH/laozhou-architecture-v0.1.md`, `docs/RESEARCH/laohe-cpp-version-selection-v1.md`
- 编译标准: C++20, `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`

---

## 0. 如何使用本清单

- PR 作者: 提交前**自检**.
- Reviewer: 按本清单**逐条扫**, 命中即 block.
- 每条规则结构: **规则 → 反例 → 正例 → 检测手段**.
- 标 [block] 是合并阻塞项, 标 [warn] 是建议改进.

老雷一句话: **"PR 没过这清单, 别 @ 我."**

---

## 1. Lifetime / Dangling Reference

C++ 工程师的头号死法. 在低延迟项目里, 80% 的 use-after-free 不是逻辑错, 是这一类.

### 1.1 [block] 函数返回临时对象的引用 / 指针

**反例**:
```cpp
const std::string& get_name() { return std::string("foo"); }   // dangling
std::string_view get_view()  { return std::string("foo"); }    // dangling
```

**正例**:
```cpp
std::string get_name() { return std::string("foo"); }          // RVO
std::string_view get_view(const std::string& s [[clang::lifetimebound]]) { return s; }
```

**检测**: `-Wreturn-stack-address`, `-Wdangling-gsl` (Clang 16+), ASan.

### 1.2 [block] `std::string_view` 接 `std::string` 返回值

**反例**:
```cpp
std::string make_id();
std::string_view id = make_id();   // dangling: 临时 string 析构后 view 悬空
```

**正例**:
```cpp
const std::string id = make_id();
std::string_view  v  = id;          // OK, id 生命周期覆盖 v
```

**踩坑高发处**:
- JSON parse 返回 string → 立即转 string_view 存到结构体. **必须存 string, 不能存 view**.
- 函数参数用 string_view, 调用方传 `std::string{} + "suffix"`, 临时 string 析构后 view 悬空 (函数体内还活, 但若 view 被存进容器就死).

**检测**: `-Wdangling`, Clang lifetimebound annotation, sanitize.

### 1.3 [block] Lambda capture by reference 跨线程 / 跨 await

**反例**:
```cpp
void schedule() {
  int local = 42;
  threadpool.post([&local] { use(local); });   // local 已析构
}

asio::awaitable<void> coro() {
  std::string buf = ...;
  co_await async_op([&buf] { use(buf); });    // 看似 OK, 但若 lambda 跨 await 存活则危险
}
```

**正例**:
```cpp
void schedule() {
  int local = 42;
  threadpool.post([local] { use(local); });    // by value
}

void schedule_shared() {
  auto data = std::make_shared<Data>();        // 共享所有权 (非热路径)
  threadpool.post([data] { use(*data); });
}
```

**规则**: **跨线程 / 跨异步边界的 lambda 一律 by value 或 by shared ownership**. 同步立即调用 (`std::for_each`) 才允许 by ref.

**检测**: 人审 + clang-tidy `bugprone-dangling-handle`.

### 1.4 [block] `auto&` / `decltype(auto)` 接代理对象

**反例**:
```cpp
auto& bit = bitset[3];   // std::bitset::reference 是代理对象, 不是引用
bit = true;              // OK 但易误读
std::vector<bool> v{true};
auto& b = v[0];          // ERROR: vector<bool>::reference 是 proxy 不能绑 auto&
```

**正例**:
```cpp
auto bit = v[0];         // 明确拷贝代理
// 或干脆别用 vector<bool>, 用 std::vector<uint8_t>
```

### 1.5 [block] 容器迭代器失效

**反例**:
```cpp
std::vector<int> v{1,2,3};
for (auto it = v.begin(); it != v.end(); ++it) {
  if (*it == 2) v.push_back(99);   // push_back 可能 realloc, it 失效
}

std::unordered_map<int, T> m;
auto& ref = m[key];
m[other_key] = ...;                // rehash 触发, ref 失效
```

**正例**:
```cpp
v.reserve(v.size() + 1);           // 预分配, 不会 realloc
for (...) { ... }

// map 引用必须重读
auto& ref = m[key];
use(ref);
ref_no_longer_valid_after_insert();
```

**规则**:
- `vector::push_back` 可能失效全部迭代器.
- `unordered_map::insert` 可能 rehash 失效全部.
- `map::erase` 只失效被删的, 其他不受影响 (这是少有的好行为).

### 1.6 [block] 智能指针误用

**反例**:
```cpp
std::shared_ptr<T> p1(raw);
std::shared_ptr<T> p2(raw);   // 双重 free, 两个独立 control block

void take(std::unique_ptr<T>);
take(std::unique_ptr<T>(raw));
take(std::unique_ptr<T>(raw));  // double free
```

**正例**:
```cpp
auto p = std::make_shared<T>();          // 唯一构造, control block 与 obj 同 alloc
auto u = std::make_unique<T>();
take(std::move(u));
```

**规则**: **永远 make_shared / make_unique**, 不要 `shared_ptr<T>(new T())`.

---

## 2. Concurrency UB (Data Race / Memory Order / False Sharing)

热路径的隐形杀手. 多核延迟优化一不小心就被这里吃掉.

### 2.1 [block] data race = UB, 不是性能问题

**反例**:
```cpp
int counter = 0;
// thread A: counter++;
// thread B: counter++;
```

**正例**:
```cpp
std::atomic<int> counter{0};
counter.fetch_add(1, std::memory_order_relaxed);
```

**规则**: 任何被多线程访问且至少一处写的变量, **必须 atomic 或锁保护**. 即使你"知道"它对齐 + 单字写入是原子的, 标准上仍是 UB, 编译器可能优化掉.

**检测**: `-fsanitize=thread` (TSan).

### 2.2 [block] memory order 滥用 / 误用

**反例**:
```cpp
// 错: 单纯 relaxed 不能做同步
std::atomic<bool> ready{false};
int data;

// thread A:
data = 42;
ready.store(true, std::memory_order_relaxed);    // BUG: data 写可能没被 thread B 看见

// thread B:
while (!ready.load(std::memory_order_relaxed)) {}
use(data);                                        // UB
```

**正例**:
```cpp
// thread A:
data = 42;
ready.store(true, std::memory_order_release);

// thread B:
while (!ready.load(std::memory_order_acquire)) {}
use(data);   // OK
```

**规则速查**:
| 模式 | 用哪个 |
|---|---|
| 单纯计数 (e.g. metrics counter) | `relaxed` |
| 单写者发布数据 / 单读者消费 | store: `release`, load: `acquire` |
| 多写者多读者 + 序保证 | `seq_cst` (默认, 慢) |
| 防止编译器重排 (compiler fence) | `std::atomic_signal_fence(memory_order_acq_rel)` |

**老姜补充**: ARM (Apple Silicon, AWS Graviton) 上 acquire/release 比 x86 贵, 设计时考虑. seq_cst 跨核延迟 ~40-80ns, relaxed 5ns.

### 2.3 [block] false sharing — cache line ping-pong

**反例**:
```cpp
struct Counters {
  std::atomic<uint64_t> a;   // core 0 写
  std::atomic<uint64_t> b;   // core 1 写
};                            // a 和 b 同 64B cache line, 互相 invalidate
```

**正例**:
```cpp
struct alignas(64) PaddedAtomic {
  std::atomic<uint64_t> v;
  char _pad[64 - sizeof(std::atomic<uint64_t>)];
};

struct Counters {
  PaddedAtomic a;
  PaddedAtomic b;
};
```

**规则**: 任何多核共享的 atomic 必须 cache-line align (x86_64 64B, Apple Silicon M-series 128B). C++17 起有 `std::hardware_destructive_interference_size`, 但 GCC 13- 实现是 64 hardcode, 不靠谱 — **自己 hardcode 128 (M-series 安全) 或 64 (x86 紧凑)**, 不要用 std 常量.

**小石确认**: SPSC ring 已强制 padding (S1-011 §1.2).

### 2.4 [block] `volatile` 不是并发原语

**反例**:
```cpp
volatile bool ready;   // BUG: volatile 不防 race, 不防重排
```

**正例**:
```cpp
std::atomic<bool> ready;   // 唯一正确选择
```

**规则**: `volatile` 只用于 MMIO / signal handler, 与多线程**完全无关**.

### 2.5 [block] 锁的常见死法

**反例**:
```cpp
std::mutex m;
{
  std::lock_guard lg(m);
  callback();           // callback 内部又锁 m → deadlock
}

// 错: 双锁顺序不一致
thread A: lock(a); lock(b);
thread B: lock(b); lock(a);   // deadlock
```

**正例**:
```cpp
// 1. 不在锁内调 callback
{ std::lock_guard lg(m); copy = state; }
callback(copy);

// 2. 双锁用 std::lock 或固定顺序
std::lock(a, b);
std::lock_guard la(a, std::adopt_lock);
std::lock_guard lb(b, std::adopt_lock);
```

**规则**: 热路径**不许出现 mutex**. 全走 SPSC / MPSC / RCU (老周 D7).

---

## 3. STL 滥用

### 3.1 [block] `std::shared_ptr` 在热路径

**反例**:
```cpp
class HotPath {
  std::shared_ptr<Book> book_;   // 热路径成员
  void on_tick() {
    auto local = book_;          // 原子 inc, 多核 ping-pong
  }
};
```

**正例**:
```cpp
class HotPath {
  Book* book_;                   // 不持有, 由 owner 保证生命周期
  // 或:
  RcuPtr<Book> book_;            // 老周 D7 RCU snapshot
};
```

**规则**: `shared_ptr` 的原子 refcount 在多核 ~20-50ns/inc, false sharing 严重. 热路径用:
- raw pointer + 显式所有权 (推荐)
- `unique_ptr` + 移动 (推荐)
- RCU snapshot (跨线程发布 immutable 数据)
- `shared_ptr` **仅限非热路径管理代码**.

### 3.2 [block] `std::vector` 不 reserve

**反例**:
```cpp
std::vector<Order> orders;
for (auto& intent : intents) {
  orders.push_back(make_order(intent));   // 多次 realloc
}
```

**正例**:
```cpp
std::vector<Order> orders;
orders.reserve(intents.size());
for (auto& intent : intents) {
  orders.emplace_back(make_order(intent));
}
```

**规则**: 任何已知大小或可估上界的 vector 必须 reserve. 热路径里**禁止 push_back 触发 realloc**.

### 3.3 [warn] `std::map` 当 cache 用

**反例**:
```cpp
std::map<MarketId, Book*> books;    // 红黑树, cache miss 严重
```

**正例**:
```cpp
absl::flat_hash_map<MarketId, Book*> books;    // 小石推荐
// 或: 已知 id 连续 → 数组直接索引
std::array<Book*, MAX_MARKETS> books;
```

**规则**: 除非要按 key 顺序遍历, 否则不用 `std::map`. 用 abseil flat_hash_map / boost::container::flat_map / 数组.

### 3.4 [block] `std::unordered_map` 默认参数热路径用

**反例**:
```cpp
std::unordered_map<int, T> m;     // chained hashtable, 每 slot 一个 list, cache miss + alloc/free
```

**正例**:
```cpp
absl::flat_hash_map<int, T> m;    // open addressing, 单数组
```

**规则**: `std::unordered_map` 是工业上**最被诟病的标准容器**, 设计早, 性能差. 用 abseil 或 robin_hood::unordered_map.

### 3.5 [block] `std::string` 短串场景

**反例**:
```cpp
std::string ticker = "BTC";   // 主流实现 SSO 是 ~15-22 字节, OK
std::string id = polymarket_market_id;   // ~64B uuid, heap alloc
```

**规则**:
- 短串 (< 16B) 用 `std::string` SSO OK.
- 已知上界 ≤ 23B → `fixed_string<23>` (自研或 boost).
- 64B uuid 高频 → 转 `std::array<uint8_t, 16>` (parsed) 而非 string.

### 3.6 [warn] `std::endl` 永远不用

```cpp
std::cout << "x" << std::endl;   // flush, 慢
std::cout << "x\n";              // 不 flush
```

热路径根本不该 cout (走 ring buffer logger), 但万一漏: `\n` not `endl`.

### 3.7 [warn] `std::regex` 慢

`std::regex` 实测比 PCRE2 / re2 慢 10-100x. 任何 regex 用法要么换 re2, 要么换 hardcoded parser.

---

## 4. 异常 vs Result 取舍 + 边界

### 4.1 [block] 项目内一律 `stcpp::Result<T,E>`, 禁抛异常

**反例**:
```cpp
Status parse(const json& j) {
  if (!j.contains("price")) throw std::runtime_error("missing price");   // BUG
}
```

**正例**:
```cpp
stcpp::Result<Order, ParseErr> parse(const json& j) {
  if (!j.contains("price")) return stcpp::Err{ParseErr::MissingPrice};
  return Order{...};
}
```

**规则**: 编译选项 `-fno-exceptions`, 任何 throw 会直接 abort. 配合静态检查.

### 4.2 [block] 三方库异常必须在适配层吃掉

**反例**:
```cpp
// in strategy/pricing.cpp
auto j = nlohmann::json::parse(s);    // nlohmann 可能 throw
```

**正例**:
```cpp
// in infra/serde/json_adapter.cpp
stcpp::Result<simdjson::dom::element, ParseErr> safe_parse(std::string_view s) {
  // simdjson 不抛, 返回 error code
  ...
}
```

**规则**:
- 摄入边界 (infra/net, infra/serde) 必须把三方异常**翻译成 Result**.
- 业务层 (strategy, risk, exec) 看不见任何 try/catch.
- 若必须用会抛的三方库, 在适配层加 `try { } catch (const std::exception& e) { return Err{...}; }`.

### 4.3 [block] `Result` 不 check 直接 `.value()`

**反例**:
```cpp
auto r = parse(j);
use(r.value());    // r 可能是 unexpected, 调 .value() 会 abort (无异常时)
```

**正例**:
```cpp
auto r = parse(j);
if (!r) { handle_err(r.error()); return; }
use(*r);

// 或 monadic:
parse(j)
  .and_then([](auto&& o) { return validate(o); })
  .transform([](auto&& o) { return enrich(o); });
```

**规则**: `.value()` 在 `-fno-exceptions` 下会 terminate, 等于断言. **任何 `.value()` 调用都要写注释解释为什么这里一定 ok**.

### 4.4 [warn] `Result` 链过深可读性

Monadic chain 超过 5 层难读, 改回早返回:
```cpp
auto a = step1(); if (!a) return a.error();
auto b = step2(*a); if (!b) return b.error();
...
```

可写 `STCPP_TRY(name, expr)` 宏减重复 (类似 Rust `?`).

---

## 5. 模板 Footgun

### 5.1 [block] 不用 concepts 用 SFINAE

**反例**:
```cpp
template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
void foo(T x) {...}
```

**正例**:
```cpp
template<std::integral T>
void foo(T x) {...}

// 或:
void foo(std::integral auto x) {...}
```

**规则**: C++20 起所有新模板用 concepts. SFINAE 仅限维护旧代码.

### 5.2 [block] concepts 写错语义

**反例**:
```cpp
template<class T>
concept Hashable = requires(T t) { std::hash<T>{}(t); };   // 没 check 返回类型 size_t

template<class T>
concept Order = requires(T t) {
  t.price();          // 没 check 返回类型 / const
  t.size();
};
```

**正例**:
```cpp
template<class T>
concept Hashable = requires(T t) {
  { std::hash<T>{}(t) } -> std::convertible_to<size_t>;
};

template<class T>
concept Order = requires(const T t) {
  { t.price() } -> std::convertible_to<Price>;
  { t.size()  } -> std::convertible_to<Size>;
};
```

**规则**:
- concept 内 `{ expr } -> type` 形式必查返回类型.
- 用 `const T` 而非 `T` 测试 const-correctness.

### 5.3 [warn] 过度模板化 (everything is a template)

**反例**:
```cpp
template<class TBook, class TSignal, class TRiskMgr, class TExec>
class Strategy { ... };   // 5 个模板参数 = 维护噩梦
```

**正例**:
```cpp
class Strategy {           // 用接口 / 函数指针 / std::function 注入
  IBook&    book_;
  ISignal&  signal_;
  ...
};
```

**规则**: 模板用于:
1. 容器 / 算法 (库).
2. 静态多态 (热路径 dispatch, 避免虚函数).
3. 编译期常量 (NTTP, constexpr).

业务编排逻辑用接口 / 多态 / 函数对象, **不要把每个依赖都模板化**.

### 5.4 [block] 模板代码全堆 header

**反例**: `book.hpp` 500 行模板实现, 任何 include 都重新编译.

**正例**:
- 私有实现下沉到 `book_impl.hpp` (detail namespace).
- 显式实例化常用类型: `extern template class Book<DefaultPolicy>;` 在 .cpp 里 instantiate.
- 用 type erasure 把模板边界藏到接口背后.

**规则**: 编译时间是开发体验生命线. PR 让全项目编译变慢 > 5%, 评审打回.

### 5.5 [block] CRTP 没必要时不用

CRTP 是上世纪的静态多态把戏, 现在用 `static operator()` (C++23) / concepts / deducing `this` 替代. 主干不引入新 CRTP 设计.

---

## 6. Coroutine 陷阱

### 6.1 [block] coroutine frame heap-allocated

每个 coroutine 默认在 heap 上分配 frame (state + locals). 热路径用 = 直接死.

**规则**:
- 热路径完全不用 coroutine. 老姜 §1 budget 不留余地.
- 非热路径 (REST 编排) 可用, 走 asio::awaitable.
- 启用 HALO (Heap Allocation eLision Optimization) 需 `-O2` + inline 链完整, 不可依赖.

### 6.2 [block] coroutine reference dangling

**反例**:
```cpp
asio::awaitable<int> compute(const Big& b) {
  co_await something();
  return b.x;     // b 引用可能已悬空, 调用方传临时 / 局部已析构
}

// 调用:
co_await compute(Big{...});   // 临时 Big 析构后 await 恢复 → UB
```

**正例**:
```cpp
asio::awaitable<int> compute(Big b) {    // by value, frame 持有
  co_await something();
  co_return b.x;
}
```

**规则**: **coroutine 参数禁用引用** (除非生命周期严格保证 outlive coroutine). by value 是默认选项, 让 frame 拷贝拥有.

### 6.3 [block] coroutine executor / lifetime

```cpp
asio::awaitable<void> task(asio::io_context& io) { ... }
// task 启动后, io_context 不能先析构.
// awaitable handle / promise / continuation 的生命周期由 executor 管理.
```

**规则**:
- 启动 coroutine 用 `co_spawn(executor, ..., asio::detached)` 必须确保 executor outlive coroutine.
- 不要 `asio::detached` 然后 capture 局部. 用 `bind_executor` 或共享所有权.
- 关停顺序: 停 executor → join 所有 coroutine → 析构.

### 6.4 [block] `std::generator` 用法

C++23, GCC 14+. 主干不用. 即使用了:
- `co_yield` 返回引用要确保产生器 frame 还在.
- 不可跨线程消费.

---

## 7. 头文件 vs 模块

### 7.1 [block] 不用 modules

详见 `laohe-cpp-version-selection-v1.md` §2.3.

**理由**: GCC / Clang / MSVC BMI 不兼容, 三方库无 module 接口, CMake `FILE_SET CXX_MODULES` 不稳定.

**规则**:
- 主干 #include + PCH (`target_precompile_headers`).
- 公共头放 `include/stcpp/...`, 私有放 `src/.../detail/`.
- 重头文件用 forward declaration 减依赖.
- M+24 月重新评估 modules.

### 7.2 [block] 头文件循环依赖

**检测**: `include-what-you-use` (IWYU), `cppdep` 工具, Bazel build 强制无环 (本项目走 CMake, 靠 CI script).

### 7.3 [warn] 头文件膨胀

- `#include <algorithm>` 在 header 拉一堆模板, 改 .cpp 里 include.
- `using namespace std;` 在 header 禁止 (污染所有 includer).
- 公共头 < 100 行, 模板实现下沉到 detail.

---

## 8. 编译选项铁律

### 8.1 [block] 全套 warning + Werror

```cmake
add_compile_options(
  -Wall -Wextra -Wpedantic -Werror
  -Wshadow                          # 屏蔽外层变量
  -Wconversion -Wsign-conversion    # 隐式收窄
  -Wnon-virtual-dtor                # 基类无 virtual dtor 删派生
  -Wold-style-cast                  # 禁 C cast
  -Wcast-align -Wcast-qual
  -Woverloaded-virtual              # 重载 vs 覆盖混淆
  -Wnull-dereference
  -Wdouble-promotion                # float → double 隐式
  -Wformat=2 -Wformat-security
  -Wimplicit-fallthrough
  -Wmissing-declarations
  -Wzero-as-null-pointer-constant
  -Wsuggest-override
  -Wno-unused-parameter             # 可选: 项目里参数太多
)
```

**规则**: CI 必须开 `-Werror`. 局部抑制必须 `[[maybe_unused]]` / `// NOLINT(name)` + 一行注释解释.

### 8.2 [block] Sanitizer 必跑 (Debug + CI)

```cmake
# Debug build
add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
add_link_options(-fsanitize=address,undefined)

# 单独 TSan build (与 ASan 不兼容)
add_compile_options(-fsanitize=thread)
add_link_options(-fsanitize=thread)
```

**规则**:
- CI 每次跑 ASan + UBSan + TSan (三条独立流水).
- nightly 跑 MSan (memory sanitizer, 检 uninit reads, 要 libc++ 重编).
- 性能测试单独用 release build, 不带 san.

### 8.3 [block] `-O2` 不是 `-O3`

**理由**:
- `-O3` 启用 aggressive 向量化 / inline 阈值放宽, **代码大小**膨胀, icache miss 反而拖慢热路径.
- HFT 业界惯例: `-O2 -march=native -ffast-math` (后者慎用, IEEE754 语义改变).
- `-O3` 仅在 profile 确认收益时局部用 (`__attribute__((optimize("O3")))`).

**规则**:
- Release: `-O2 -g -DNDEBUG -march=native` (或 `-march=skylake-avx512` 显式锁).
- `-ffast-math` **禁用**, 浮点语义破坏会引入难定位 bug.
- 关键 hot 函数加 `[[gnu::hot]]`, 冷函数 `[[gnu::cold]]`.

### 8.4 [block] `-fno-exceptions -fno-rtti`

如选型文档 §7 所述. 老周 D3 物理保障. 不接受任何 exception spec.

### 8.5 [warn] LTO (Link Time Optimization)

```cmake
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
```

- 收益: 跨翻译单元 inline / DCE, 二进制小 5-15%, 性能提升 5-10%.
- 代价: 链接慢 2-5x.
- 建议: 仅 Release build 开, Debug / CI 关.

### 8.6 [block] 不依赖 UB 优化

```cpp
int x = INT_MAX + 1;            // signed overflow UB
if (p->x) { ... if (!p) {...} } // 编译器可能删 if(!p) 因为前面已 deref
```

**规则**: clang-tidy + UBSan 必跑, 任何 UB warning 一律 fix.

---

## 9. ABI 稳定性

### 9.1 [warn] 跨编译器 ABI 差异

- libstdc++ vs libc++ 的 `std::string` ABI 不同 (SSO 大小, COW 历史).
- GCC dual ABI (`_GLIBCXX_USE_CXX11_ABI`) 历史包袱, 链接旧库注意.
- **本项目对策**: 静态链接 libstdc++, 部署节点只依赖 glibc.

### 9.2 [block] 公共头不暴露 STL container

公共接口 (动态库 / 进程间 IPC) 避免 STL container 出现在签名里 ABI 漂移风险高.

**反例**:
```cpp
// public ABI
extern "C" struct PublicApi { std::vector<int> ids; };  // STL 在 C ABI = 灾难
```

**正例**: 用 POD struct + 长度字段, 走 SHM / IPC.

### 9.3 [block] inline namespace / 版本宏

公共头不要轻易加 inline namespace, 改一次破二进制兼容. 库版本走 `#if STCPP_VERSION >= ...`.

### 9.4 [warn] 编译器版本 lock

CI 锁 GCC 13 + Clang 17/18 + (实验) GCC 14. 不允许 PR 静默引入更新编译器需求.

---

## 10. 性能反模式

### 10.1 [block] 虚函数滥用

**反例**:
```cpp
class Pricer { virtual Price compute(...) = 0; };   // 热路径每 tick 调
```

**评估**:
- 单次虚调用 ~3-5ns (vtable indirect call, 现代 CPU branch predict OK).
- 但**抑制 inline**, 算上 cache miss / 分支预测错误, 实测 20-50ns 损耗.
- 热路径每秒 10k+ tick = 0.2-0.5ms/s 额外开销.

**规则**:
- 热路径用模板静态多态 / `if constexpr` / variant + visit / function pointer table.
- 非热路径 (策略注册 / 配置加载) 用虚函数无所谓.

### 10.2 [block] `std::shared_ptr` 热路径 (重申)

详见 §3.1.

### 10.3 [block] 隐式拷贝

**反例**:
```cpp
void process(std::vector<Order> orders);   // by value, 大对象拷贝
class Strategy {
  std::vector<Order> orders_;
  std::vector<Order> get() const { return orders_; }   // 返回 by value
};
```

**正例**:
```cpp
void process(std::span<const Order> orders);    // view, 零拷贝
const std::vector<Order>& get() const { return orders_; }

// 返回值 RVO OK:
std::vector<Order> build() { return result; }   // NRVO, 零拷贝
```

**规则**: 大对象用 `span` / `string_view` / 引用. 返回值优先 RVO (返回局部变量), 不要 `std::move(local)` (抑制 NRVO).

### 10.4 [block] alloc 在热路径

任何 heap alloc 在热路径 = 死刑. 包括:
- `new` / `make_shared` / `make_unique`.
- `std::function` (small buffer optimization 因实现而异).
- `std::vector::push_back` 触发 realloc.
- `std::string` 长串 (> SSO).
- coroutine frame (除非 HALO).

**对策**:
- Arena allocator (老周 D6).
- Object pool.
- Stack buffer + span.
- 预分配 + reserve.

### 10.5 [warn] 不当使用 `std::function`

`std::function` 有 type erasure 开销 + 可能 heap alloc (大于 SBO). 热路径用 `function_ref` (自研或 tl::function_ref) / 模板回调.

### 10.6 [block] cache-unfriendly 数据布局

**反例 (AoS, Array of Structs)**:
```cpp
struct Tick { Price p; Size s; Timestamp t; OrderId id; ... };
std::vector<Tick> ticks;   // 处理 price 字段时全字段都加载
```

**正例 (SoA, Struct of Arrays)**:
```cpp
struct Ticks {
  std::vector<Price>     prices;
  std::vector<Size>      sizes;
  std::vector<Timestamp> timestamps;
};
// 仅访问 price 时 cache miss 大幅减少
```

小石 §1 OrderBook 已选 SoA, 这是同理.

### 10.7 [warn] 分支预测不友好

- 热路径里少用 `if (rare_case)`, 用 `[[likely]] / [[unlikely]]` 标注 (C++20).
- 测试用 `__builtin_expect` 旧法, C++20 起 attribute 是正道.

### 10.8 [block] 内存对齐缺失

```cpp
struct Counters {
  std::atomic<uint64_t> a;   // 默认对齐 8B, false sharing
};

struct alignas(64) AlignedCounters { ... };   // 强制 cache line
```

详见 §2.3.

---

## 11. PR Review 速查表 (打印贴墙)

| # | 规则 | 速查 |
|---|---|---|
| 1 | 生命周期 | string_view 不接 string 返回值, lambda 跨线程 by value |
| 2 | 并发 | 任何共享变量 atomic, padding 64/128B, 热路径无 mutex |
| 3 | STL | shared_ptr 不进热路径, vector reserve, 用 abseil 替 unordered_map |
| 4 | 错误 | 用 stcpp::Result, 三方异常在 adapter 层吃掉 |
| 5 | 模板 | C++20 concepts, 不用 SFINAE, 不过度模板化 |
| 6 | 协程 | 热路径禁, 参数 by value, executor 长寿命 |
| 7 | 头文件 | 不用 modules, 私有下沉 detail, 公共头 < 100 行 |
| 8 | 编译 | Werror + ASan + UBSan + TSan, -O2 不 -O3, -fno-exceptions -fno-rtti |
| 9 | ABI | 静态链接 libstdc++, 公共 ABI 不暴露 STL |
| 10 | 性能 | 热路径无虚函数 / shared_ptr / alloc, SoA 优于 AoS |

---

## 12. 求证 / 求助

- **性能反模式数字**: §10.1 虚函数 20-50ns 损耗, §3.1 shared_ptr 20-50ns/inc → 待 @老姜 godbolt + perf 实测背书.
- **数据结构选择**: §3.3 / §3.4 替代容器, 已与 @小石 v1 文档对齐.
- **simdjson 边界**: §4.2 是否真完全不抛 → 待 @老李 (S1-002) 接入时确认 simdjson API 行为.
- **coroutine HALO 实测**: §6.1 GCC 13 / Clang 17 在何条件下能 elide frame alloc → 后续 benchmark.
- **modules 状态**: §7.1 每季度 revisit GCC / Clang 进展.

---

## 13. 维护机制

- 本清单每季度 review 一次 (老何主持).
- 任何 PR review 发现新 footgun → 补入对应章节, 走 PR.
- 严重 footgun 入 INCIDENT post-mortem, 反向同步本清单.
- 老郭对本清单条目有最终拍板权.

---

## 14. 评审请求

**@老郭**: 评 §11 速查表是否纳入 PR template, §4 异常边界是否需要严格 lint 工具支持.
**@老周**: 评 §3.1 / §10 是否与你 D6 内存政策完全一致, 是否需要进 ADR.
**@老姜**: 评 §10 性能数字是否需 godbolt + perf 实测背书.
**@小石**: 评 §3.3 / §3.4 容器选择是否与你 S1-011 §0 完全对齐.
**@老李**: 评 §4.2 simdjson 异常行为是否准确.

拍板后, 本清单加入 PR template 必填 checklist (老胡 / 小米协助).
