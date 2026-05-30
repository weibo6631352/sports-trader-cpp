# PaperDaemon 重构 — R-11 隔离审计 v1

owner: 老韩 (B 风控合规部主管, RM/R-11 主权)
last_review: 2026-05-30
scope: 只读审计, 不碰代码主干 / CMake
审计对象:
  - `src/stcpp/debug_api/debug_server_main.cpp` (现状, 1000 行, paper 栈 line 725-803 / 880-894 / 961-996)
  - GM 拟重构: 抽 `class PaperDaemon { Build() / Shutdown() }` + 新 headless `paper_runtime` binary
关联红线: CLAUDE.md §8 R-11 (paper 禁写 position / pnl_ledger / nonce_ledger → P0)
关联: ADR-011 (paper/live 共享 binary + transport 分离), R-7 (build-time mode 锁), R-12, R-20
依据代码:
  - `include/stcpp/risk/rm_debug_snapshot.hpp` (attach/detach 全局单例 hook §6)
  - `src/stcpp/risk/risk_gateway.cpp:211-219, 658` (g_rm_debug_snapshot atomic + evaluate load)
  - `include/stcpp/execution/execution_mode.hpp:41-79` (kCompiledMode + ExecutionContext::Init 双调 abort)
  - `tests/integration/r11_paper_pollution_test.cpp` (T1-T5)
  - `deploy/paper-runtime/stcpp-paper.service:36` (Environment=PAPER_MODE=1)

---

## 0. 现状隔离机制 (审计基线)

debug_server_main.cpp 当前 paper 栈靠 5 道物理隔离立住 R-11:

1. **独立 PositionLedger 实例** — `paper_position_ledger = make_unique<PositionLedger>()` (line 735), 与 live 账本路径**物理不同对象**, paper fill 只 apply 到这个实例。
2. **全局 hook 注册/注销** — `attach_rm_debug_snapshot(paper_rm_snap.get())` (line 737) / 退出 `detach_rm_debug_snapshot()` (line 976)。这是**进程级单例 atomic 指针** (risk_gateway.cpp:211), 不是成员变量。
3. **NullAuditEmitter** — M1 不落 WAL (line 741-745), 不写 risk_audit/position/shadow_audit。
4. **paper 专用 RiskGateway** — 独立 cap (line 746-755), 与 live RM 隔离。
5. **PaperLoop 内部** — PaperSigner (mock 不上链) + VirtualMatcher (mode_tag==0) + PaperAudit WAL kind。

注意:R-7 的 build-time 锁 (`kCompiledMode == Paper`, execution_mode.hpp) 是这 5 道之上的**最外层保险**。debug_server binary 由 CMake `STCPP_EXEC_MODE=paper` 编译, R-11 的根本保证不在运行期对象, 在 build-time。

---

## 1. 搬迁中最容易被破坏的不变量

把上述 5 道从 `main()` 局部变量搬进 `PaperDaemon::Build()/Shutdown()` 成员, **风险全部集中在"生命周期 + 时序"这一层, 不在逻辑层**。逐条:

### INV-1 (最高危): detach 必须严格早于 paper_rm_snap 析构

现状靠 `main()` **局部变量逆序析构 + 显式 detach 在前** 的双保险:
```
line 737  attach_rm_debug_snapshot(paper_rm_snap.get());   // 注册裸指针
...
line 976  detach_rm_debug_snapshot();                       // 注销 (置 nullptr)
line 1000 }  // ← 此后 paper_rm_snap unique_ptr 析构 (栈逆序)
```
detach 在 line 976, 对象析构在 line 1000 函数返回时, **detach 先于析构** — 正确。

**搬进 class 后的陷阱:** unique_ptr 成员的析构由**成员声明顺序逆序**决定, 与 `Shutdown()` 里写不写 detach **无关**。两个独立的失效路径:

- **路径 A (Shutdown 显式 detach):** 只要 `Shutdown()` 里调了 `detach_rm_debug_snapshot()`, 且 Shutdown 在析构函数之前被调用 → 安全。**但若 Shutdown 漏调 / 异常提前 return / 析构函数没兜底调 Shutdown** → 全局 hook 仍指向**已析构的 paper_rm_snap_**, 此后任何 RiskGateway::evaluate() REJECTED 路径 (risk_gateway.cpp:658 load 全局指针) 会 **push_reject 到悬垂指针 → UAF**。这不是 R-11 污染, 是更糟的内存安全事故, 但同样 P0。
- **路径 B (成员析构顺序):** 即便 Shutdown 调了 detach, 如果 `~PaperDaemon()` 不显式 detach 兜底, 而某条 early-return / 构造失败路径**跳过了 Shutdown**, attach 后的全局指针就漏注销。下一个 PaperDaemon 实例 attach 会覆盖 (单例语义, 见 rm_debug_snapshot.hpp:399), 表面无事, 但**进程内残留悬垂窗口**。

**硬要求 (GM 写代码时):**
- `attach` 必须在 `Build()` 内, 紧跟 `paper_rm_snap_` 构造之后。
- `detach` 必须由 `Shutdown()` 调用, **且** `~PaperDaemon()` 析构函数**必须兜底再调一次 detach (幂等, detach 置 nullptr 可重复调)**, 防 Shutdown 漏调。
- `paper_rm_snap_` 成员声明顺序必须保证它**比 paper_rm_ (RiskGateway) 晚析构** —— RiskGateway 可能在析构期还跑评估? 不会 (单线程析构), 但 PaperLoop 线程必须先 join。见 INV-3。

### INV-2: PaperLoop 线程必须在 detach 之前 join 停止

现状: line 969 `paper_loop->Stop()` (内含 jthread join) → line 976 `detach`。顺序正确:**先停产生 reject 的线程, 再注销 reject 的接收端**。

搬进 class 后, 若 Shutdown 把 detach 写在 `paper_loop_->Stop()` **之前**, 会出现:PaperLoop 线程仍在 tick → 调 RM evaluate → REJECTED → push_reject, 但全局 hook 已 nullptr (这种情况 evaluate 里 `if (snap)` 判空跳过, 不崩, 但**丢 reject 观测**)。更糟的反序是 detach 在 Stop 之后但 paper_rm_snap_ 已析构 —— 即 INV-1 路径 A。

**硬要求:** Shutdown 内严格顺序 = `paper_loop_->Stop()` (join) → `detach_rm_debug_snapshot()` → 成员逆序析构。三者顺序不可换。

### INV-3: 成员声明顺序 = 安全析构顺序

C++ 成员**按声明顺序构造, 逆序析构**。安全的声明顺序 (从先构造到后构造):
```
paper_position_ledger_   // 1 最先 (被 PaperLoop 引用)
paper_rm_snap_           // 2 (被 attach + 被 PaperLoop + 被 RealStateProvider 引用)
paper_audit_emitter_     // 3
paper_rm_                // 4 (引用 emitter)
paper_fv_model_          // 5
paper_loop_              // 6 最后构造 → 最先析构 ✓ (它引用 1/2/4/5)
```
逆序析构 → paper_loop_ 先析构 (其 jthread 已在 Shutdown join, 析构安全) → ... → paper_rm_snap_ 后析构。**只要 detach 在 Shutdown 已执行, paper_rm_snap_ 析构时全局 hook 已 nullptr, 安全。**

**陷阱:** 若有人把 `paper_loop_` 声明在 `paper_rm_snap_` **之前**, 逆序析构会让 paper_rm_snap_ 先死、paper_loop_ 后死 —— 但 paper_loop_ 持 `paper_rm_snap_.get()` 裸指针, 析构期若仍有动作 → 悬垂。**声明顺序必须让被引用者先声明 (后析构)。**

### INV-4: NullAuditEmitter 是局部 class, 搬迁后必须仍保证 paper 不落真 WAL

现状 line 741 `NullAuditEmitter` 是 `main()` 内局部定义的 class。搬进 PaperDaemon 后应提为文件作用域 / 私有嵌套类。**逻辑不变, 但要确认搬迁后 paper_rm_ 注入的仍是 NullAuditEmitter, 不会被误接到 live WalWriter**。M2 接真 PaperAudit WAL 时, 必须是 `WalKind::PaperAudit` 专用文件, 不碰 risk_audit/position/shadow (r11 test T1 守这条)。

### INV-5: token_map / hub 等共享对象的所有权不能被 paper 与 live 混用

现状 hub_owned / ledger_hub_owned / quote_hub_owned 是 paper 与观测共享的。搬进 PaperDaemon 后, 这些是 paper 私有还是 daemon 共享要划清。**R-11 的红线是 PositionLedger, 不是 hub** —— hub 是只读快照发布, 不是真账本, 共享 OK。但 `paper_position_ledger_` 绝不可与任何 live 路径的 PositionLedger 同实例。

---

## 2. headless paper_runtime 会不会意外走 live 账本路径

**结论: 只要守住 build-time mode 锁, headless 不会走 live 账本。但有 3 个新增暴露面。**

### 2.1 根本保证仍是 build-time, 不是 RunMode 分支

R-11 / R-7 的根在 `kCompiledMode` (execution_mode.hpp:45)。paper_runtime 必须由 CMake `STCPP_EXEC_MODE=paper` 编译 (ADR-011: 共享 binary + build-time mode switch)。**少了 HttpServer 这层完全不影响 R-11** —— HttpServer 是只读观测端, 从不写账本。删掉它不会把任何写路径接到 live 账本。

### 2.2 真正的暴露面 (3 个)

- **暴露面 1 — RunMode::Headless 分支误共用 live 装配代码:** 如果 PaperDaemon::Build() 被设计成"既能 paper 又能 live"的通用装配器, 用 RunMode 参数分叉, 那 headless 分支若错误 fallthrough 到 live PositionLedger 构造 → P0。**要求: PaperDaemon 名字即契约, 它只装 paper 栈。headless/debug 的区别只能是"要不要起 HttpServer + ML recorder", 绝不能是"用哪个 ledger"。ledger 永远是 `make_unique<PositionLedger>()` 私有实例。**
- **暴露面 2 — paper_runtime 若被误编成 live mode:** CMake 若给 paper_runtime target 漏传 / 传错 STCPP_EXEC_MODE → kCompiledMode 兜底是 paper (execution_mode.hpp:51, 安全), 但 STCPP_EXEC_MODE_STR 可能不一致。见 §3 硬 gate。
- **暴露面 3 — detach 在 headless 下漏调:** debug_server 有完整 Shutdown 路径 (line 961-996)。headless 若用更简的退出路径 (SIGTERM 直接 exit / systemd kill), 可能跳过 Shutdown → detach 漏调。**进程退出全局 hook 自然消失, 不构成跨进程污染**, 但若 headless 在同进程内重启 daemon (reload) 而不重新 attach/detach → 悬垂。**要求: headless 主循环 SIGTERM/SIGINT → 必走 PaperDaemon::Shutdown() (含 detach), 不允许裸 _exit。**

### 2.3 headless 与 debug_server 的 R-11 边界必须逐字一致

唯一被允许不同的是**观测发布层** (有没有 HttpServer)。**写路径 (PositionLedger / Signer / Matcher / AuditEmitter) 必须由同一份 PaperDaemon::Build() 产出, 不允许 headless 走另一套装配。** 这是 ADR-011 "RM + Signal core 共享" 的延伸 —— paper 写栈也必须单一来源, 防分叉。

---

## 3. paper_runtime 启动期硬 gate (要求 GM 实施)

systemd unit 用 `Environment=PAPER_MODE=1` 强制 override (stcpp-paper.service:36)。但当前代码**没有任何地方读 PAPER_MODE** (grep 确认: src/include 内无 `getenv("PAPER_MODE")`)。这是个缺口 —— env 设了但没人校验, 等于没设。

**老韩要求的硬 gate (paper_runtime `main()` 第一件事, 在 Build() 之前):**

```
HARD GATE (paper_runtime 启动, 任一不满足 → fprintf + std::abort, 禁继续):

G1  build-time 自证: static_assert(stcpp::execution::kCompiledMode ==
        stcpp::execution::ExecutionMode::Paper,
        "paper_runtime 必须 STCPP_EXEC_MODE=paper 编译");
    —— 编译期挡死, live-编译的 binary 根本生不出 paper_runtime。

G2  STCPP_EXEC_MODE_STR 一致性: 运行期断言
        std::strcmp(STCPP_EXEC_MODE_STR, "paper") == 0;
    防 CMake 给 target 传了 macro 但漏传 STR (两套定义不同步)。

G3  PAPER_MODE env 硬校验: const char* pm = getenv("PAPER_MODE");
        要求 pm != nullptr && strcmp(pm,"1")==0。
        缺失或 != "1" → abort, 日志打 "R-11 GATE: PAPER_MODE 未设/非1, 拒绝启动"。
    —— 这把 systemd 的"强制 override"从摆设变成真 enforce。
        即使有人手动跑 binary 忘了 PAPER_MODE=1, 也起不来。

G4  ExecutionContext::Init(ExecutionMode::Paper) 单次调用;
        execution_mode.hpp:58 双调 abort 已守。Init 用 kCompiledMode 喂,
        不允许用 env / 命令行参数喂 mode (env 只用于 G3 校验, 不用于选 mode)。

G5  (建议, 非阻塞) 启动日志打印三方一致行:
        "R-11 GATE PASS: kCompiledMode=paper STCPP_EXEC_MODE_STR=paper PAPER_MODE=1"
        供 ops + audit 抓取。R-11 拒绝/放行都要可追溯 (CLAUDE.md §7.6)。
```

**关键设计立场:** mode 的**唯一真相源是 build-time** (G1/G4)。PAPER_MODE env **只是冗余交叉校验 (G3), 绝不允许用 env 在运行期"切换"到 live** —— 那会重新打开 R-7 防的 runtime 切换口子。env 与 build 不一致时**一律 abort, 不是"以谁为准"**, 是"不一致即 bug, 不启动"。

---

## 4. 搬迁后 R-11 不变量 checklist (GM 逐条自检)

GM 写完 PaperDaemon + paper_runtime 后, 逐条对照。每条标 [PASS]/[FAIL], FAIL 任一不许合 main。

```
[ ] C1  paper_position_ledger_ 是 PaperDaemon 私有 make_unique<PositionLedger>()
        实例, 全代码 grep 确认它不与任何 live 路径 PositionLedger 共用对象。
[ ] C2  attach_rm_debug_snapshot() 仅在 Build() 调一次, 紧跟 paper_rm_snap_ 构造后。
[ ] C3  detach_rm_debug_snapshot() 在 Shutdown() 调; 且 ~PaperDaemon() 析构兜底
        再调一次 (幂等)。两处都在, 缺一不可。
[ ] C4  Shutdown() 顺序: paper_loop_->Stop()(join) → detach → 返回。
        detach 严格在 PaperLoop 线程 join 之后、paper_rm_snap_ 析构之前。
[ ] C5  成员声明顺序: paper_rm_snap_ 声明在 paper_loop_ 之前 (→ 逆序析构时
        paper_loop_ 先死, paper_rm_snap_ 后死), 被引用者后析构。
[ ] C6  paper_rm_ 注入的 emitter 是 NullAuditEmitter (M1) / PaperAudit-only
        WalWriter (M2), 绝不接 risk_audit / position / shadow_audit WAL。
[ ] C7  headless 与 debug_server 共用同一份 PaperDaemon::Build() 产出写栈;
        二者唯一差异是观测层 (HttpServer / ML recorder 起不起), 不在写路径。
[ ] C8  RunMode/Headless 分支不触碰 ledger 选择; ledger 永远是 paper 私有实例。
[ ] C9  paper_runtime main() 执行 §3 G1-G4 硬 gate, 任一 FAIL → abort, 不进 Build()。
[ ] C10 headless SIGTERM/SIGINT → 必走 PaperDaemon::Shutdown() (含 detach),
        无裸 _exit / 跳过 Shutdown 的退出路径。
[ ] C11 r11_paper_pollution_test (T1-T5) 全绿; 且新增/沿用一条断言覆盖
        "PaperDaemon Build→Shutdown 一轮后, 全局 g_rm_debug_snapshot 为 nullptr"。
[ ] C12 跑一轮 PaperDaemon 构造→Shutdown→析构, ASan/UBSan 无 UAF
        (验 INV-1 detach 时序, 防悬垂)。
[ ] C13 ExecutionContext::Init 全进程仅一次 (双调 abort 已守); paper_runtime 与
        debug_server 不在同进程同时各 Init 一次。
```

**测试侧补充 (派测试组并行):** 现有 r11_paper_pollution_test T1-T5 守 WAL 隔离与 mode tag, 但**没有覆盖 attach/detach 生命周期时序** (C3/C4/C12)。建议小宋补一条 integration: 构造 PaperDaemon → Build → 制造 reject → Shutdown → 断言 `g_rm_debug_snapshot == nullptr` 且重复 Shutdown 幂等不崩。这条是本次搬迁新增的回归面, 现有测试盖不到。

---

## 5. 一句话结论

**点头, 但带 2 条阻塞性硬 gate (必须先落):**
(1) paper_runtime main() 必须实现 §3 的 G1-G4 硬 gate (尤其 G3 把 PAPER_MODE env 从"systemd 设了没人读"变成真 enforce, 当前代码零校验 = 隐患);
(2) detach 时序必须双保险 (Shutdown 显式调 + 析构兜底, checklist C3/C4), 并补 C11/C12 回归测试 —— 这是搬迁唯一真正新增的 UAF/悬垂风险面。

这两条落地 + checklist C1-C13 全 PASS + r11_paper_pollution_test 全绿, 即可合 main。R-11 的根 (build-time mode 锁) 本身搬迁不动它, 风险全在生命周期层, 上述护栏覆盖。
```
