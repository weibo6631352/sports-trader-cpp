# PaperDaemon 重构 — 架构决议 (老郭 §A.1 配套落地评审)

- **owner:** 老周 (系统工程部主管 + 架构主权)
- **last_review:** 2026-05-30
- **scope:** GM 主线 main 上的 PaperDaemon 重构 (debug_server_main.cpp 抽类 + headless paper_runtime binary)
- **依据:** 老郭 `docs/RESEARCH/laoguo-arch-deadcode-review-v1.md` §A.1/§A.2;`src/stcpp/debug_api/debug_server_main.cpp`(1000 行);`src/stcpp/debug_api/CMakeLists.txt`;`deploy/paper-runtime/stcpp-paper.service`
- **性质:** 只读评审 + 架构意见。代码由 GM 落地,本文不动任何 .cpp/.hpp/CMake。

---

## 一句话结论

**方向点头,切法有一个 blocker + 两个必须钉死项。**

- **方向 ACK:** 抽 `class PaperDaemon { Build/Run/Shutdown }` + 抽 `market_discovery` 可测库 + 起 headless `paper_runtime` — 这三件全对,和老郭 §A.1、systemd unit 期待一致。
- **BLOCKER B1(库归属):** `PaperDaemon` 类 **不能进 `stcpp_debug_api` STATIC 库**。理由是依赖方向 —— 见 §1。这是架构主权的硬性约束,不是建议。
- **钉死项 1(析构 vs Shutdown):** RAII 成员析构逆序与显式 `Shutdown()` 反序停线程必须**二选一,不可并存**,否则 double-stop / use-after-free。见 §2。
- **钉死项 2(RunMode):** 砍掉 `ObserverOnly`,MVP 只留 `PaperDaemon + Headless` 两档。见 §3。

---

## 1. BLOCKER B1 — PaperDaemon 类的库归属:不进 debug_api 库,放 thin app 编排层

### 1.1 实测依赖事实(这是结论的硬依据,非主观)

读 `src/stcpp/debug_api/CMakeLists.txt`:

- `stcpp_debug_api` STATIC 库当前 link 面 = **仅 `Threads::Threads`**(L116-117)。它编译的是 server.cpp + 13 个 endpoint_*.cpp,纯"只读契约 + JSON 序列化"层,**不 link** orderbook_hub / risk / paper_loop / inplay_feed / pricing / signer / execution。
- 那 11 个重型库(`stcpp_orderbook_snapshot_hub` / `stcpp_risk` / `stcpp_paper_loop` / `stcpp_pricing` / `stcpp_signer_paper` / `stcpp_execution_paper` / `stcpp_data_inplay_feed` …)是 **`stcpp_debug_server` executable** 在 link 的(L157-170),不是库在 link 的。

**也就是说:装配 8 组件这套 wiring 的全部重依赖,今天只活在 executable 的 main.cpp 里,没污染库。**

### 1.2 GM 设计把 PaperDaemon 进 `stcpp_debug_api` 库 = 把这 11 个重依赖灌进库

`PaperDaemon::Build()` 要 `make_unique` OrderBookSnapshotHub / RiskGateway / PaperLoop / InplayFeedThread / LiveWssTransport / FeatureRecorder …。这些 include 一旦进库的 .cpp,`stcpp_debug_api` 的 `target_link_libraries` 必须从 "Threads only" 扩成 "+11 重型库 + OpenSSL"。

后果,逐条:

1. **直接坐实老郭 §A.2 的警告。** §A.2 原文:"debug_api 正在变成什么都连的 hub"。今天它还没连(库只 link Threads),GM 这一刀会让它**真的连上全系统**。我作为架构主权,不接受在 MVP 阶段主动把这个膨胀趋势变成既成事实。
2. **污染所有 link `stcpp_debug_api` 的人。** 任何只想用只读 endpoint / JSON 序列化的 target,会被迫拖进 paper_loop + signer + execution + OpenSSL 的传递依赖。库的"轻"是它现在唯一的结构优点,不能丢。
3. **测试隔离倒退。** 老郭 §A.1 抽类的**核心目的**就是"集成测试不起真 HttpServer 也能验装配"。但若 PaperDaemon 和 endpoint 同库,测 endpoint 的单测 target 会被迫 link 整个 daemon 依赖图,编译时间 + 链接面双输。

### 1.3 决议:新增 thin app 编排层,PaperDaemon 进 app 层,不进 debug_api 库

```
新增 target: stcpp_paper_app  (STATIC, 新目录 src/stcpp/app/)
  ├── market_discovery.cpp      (§4: gamma 解析, 独立可测)
  └── paper_daemon.cpp          (class PaperDaemon)
  link: stcpp_debug_api + 那 11 个重型库 + OpenSSL
        (= 把今天 stcpp_debug_server executable 的 link 列表整体搬到 app 库)

executable stcpp_debug_server  → main(RunMode::PaperDaemon) → link stcpp_paper_app
executable paper_runtime       → main(RunMode::Headless)    → link stcpp_paper_app
```

**依赖方向钉死:** `app → debug_api`(单向)。debug_api 库**永远不 link app,不 link 那 11 个重型库**,保持它"只读契约层"的轻量身份。app 层是"什么都连"的那一层 —— 让膨胀发生在一个**明确命名为编排层**的地方,而不是渗进契约库。

- header `paper_daemon.hpp`:GM 提案放 `include/stcpp/debug_api/`。**改放 `include/stcpp/app/paper_daemon.hpp`**,namespace 用 `stcpp::app`,**不用 `stcpp::debug_api`**。命名就是边界,daemon 是 app 不是 debug。
- `market_discovery.hpp/.cpp`:GM 提案进 `stcpp_debug_api`。**改进 `stcpp_paper_app` 库**,namespace `stcpp::app`(或 `stcpp::discovery`)。它纯 gamma JSON 解析,零 hub 依赖,本可独立成最小库 `stcpp_market_discovery`;若 GM 嫌 target 碎,合进 `stcpp_paper_app` 也接受 —— **唯一红线是别进 debug_api**。

> 注:GM 不想新建 `src/stcpp/app/` 目录、想全压在 debug_api 目录下省事,可以,**目录无所谓,target 边界是硬的**。即:文件可以物理放 `src/stcpp/debug_api/` 下,但 CMake 里**必须是独立的 `stcpp_paper_app` target**,且 `stcpp_debug_api` 库的 source 列表里**绝不出现 paper_daemon.cpp / market_discovery.cpp**。我看的是 link 图,不是文件夹。

---

## 2. 钉死项 1 — Build/Run/Shutdown 三段式 vs RAII:三段式对,但析构必须设成 no-op 守卫

### 2.1 三段式 vs 纯 RAII 的裁决:三段式胜

本项目启动期是"11 步顺序装配 + 跨线程 happens-before"(老周 W6 v0.7 §8),不是简单 RAII 能表达的,因为:

- **顺序耦合是显式的、有数据依赖的。** 现 main 的真实约束(读 L805-894):
  - PaperLoop 必须在 WSS `AsyncConnect` **之后** Start(L883-894 注释明写"在 WSS io_thread 启动后再启动 PaperLoop,确保 hub 已就位");
  - LiveMetricsHooks 需 **两段注入** —— Step2 先注入空 hooks,Step4 WSS transport 构建后 re-inject `wss_transport` 指针(L833-836);
  - `attach_rm_debug_snapshot(paper_rm_snap.get())` 是**全局 hook**(L737),必须在 `paper_rm_snap` 析构**前** detach(L976)。
- 这种"构造完成 ≠ 可运行"的两阶段语义,**RAII 单段构造表达不了**。强行 RAII 会把"起线程"塞进构造函数 → 构造期抛异常时半起的线程 join 不掉 → 启动异常安全直接破功。

**所以 `Build()`(只装配、不起线程)+ `Run()`(起线程、阻塞)+ `Shutdown()`(反序停)三段式是对的。Build 异常 → 没有任何线程在跑,析构干净;Run 才引入线程。**

### 2.2 但这里有个会咬人的陷阱:Shutdown 反序停 + 析构逆序停 = 双重停

GM 设计 `Build()` 里一堆 `unique_ptr` 成员。C++ 成员析构是**声明逆序**,自动发生。而 `Shutdown()` 是**手写显式反序停**。两者并存就出 bug:

- 现 main 的显式停顺序(L957-996):`server.stop()` → `ml_recorder->Stop()` → `paper_loop->Stop()` → `detach_rm_debug_snapshot()` → `inplay_feed->Stop()` → `live_transport->Close()`。
- 若这些是 PaperDaemon 的 unique_ptr 成员,`~PaperDaemon()` 还会按**声明逆序**再析构一遍。`PaperLoop` 这类内部 jthread 的对象,`Stop()` join 过一次,析构再调一次 → 取决于实现可能 double-join / 状态机 assert。

**钉死规则(GM 落地必须遵守):**

1. **`Shutdown()` 幂等 + 析构兜底。** `Shutdown()` 内置 `if (already_shut_) return;` 守卫,且 `~PaperDaemon()` **只调 `Shutdown()` 一次**(不再手写逐个 reset)。即:停的逻辑只有一份,在 Shutdown 里;析构是它的幂等兜底(防 Run 后没显式 Shutdown 就析构的路径)。
2. **停线程的对象顺序 = 数据流逆序,手写在 Shutdown 里钉死,不靠成员声明序碰运气。** 正确反序(对照现 main,这是已验证过的关闭序,照搬):
   ```
   1. HttpServer.stop()        — 先断外部读入口,停止新请求读快照
   2. FeatureRecorder.Stop()   — 停 ML 采集 (它读 quote_hub, 必须先于 quote_hub 析构)
   3. PaperLoop.Stop()         — 停交易循环 (写 ledger_hub/quote_hub, 先于这俩析构)
   4. detach_rm_debug_snapshot() — 注销全局 hook (必须先于 paper_rm_snap 析构)  ★最易漏
   5. InplayFeedThread.Stop()  — 停 score feed (先于 score_store 析构)
   6. LiveWssTransport.Close()  — 停 WSS io+send thread
   ```
3. **`detach_rm_debug_snapshot()` 是全局单例 hook,不是成员 RAII 能管的。** 它在 Shutdown 第 4 步显式调用,且必须保证 `paper_rm_snap` 成员的**声明位置**让它析构发生在 detach 之后。建议:`paper_rm_snap` 声明为 daemon 的**前几个成员**(早声明 = 晚析构),给 detach 留时序余量。这是全文最隐蔽的 use-after-free 点,GM 务必在落地时单独验一遍。
4. **`std::atomic<bool> g_stop` + signal handler 不进类。** 现 L81-85 的 `g_stop` 是文件级 atomic,signal handler 只能用 free function + 全局/atomic。这部分**留在 main.cpp,不进 PaperDaemon 类**。类提供 `RequestStop()`,main 的 handler set 一个 daemon 外的 atomic,main loop 检测到后调 `daemon.Shutdown()`。Run() 内部阻塞在自己的 wait 上,由 RequestStop 唤醒。别想把 POSIX signal 塞进类成员,会很难看且不可移植。

### 2.3 异常安全的验收口径

- `Build()` 抛异常:必须保证此刻**零线程在跑**(因为 Build 不起线程)。InplayFeedThread 现在是 `Build` 阶段就 `Start()`(L818-819)—— GM 落地时**要把它的 Start 挪到 Run()**,否则 Build 抛异常时 InplayFeedThread 已在跑但没人 join。这是从现 main 抽类时一个**语义迁移点**,不是照搬:现 main 是边构造边 Start,抽成 Build/Run 后所有 `.Start()/.AsyncConnect()` 必须**全部归到 Run()**,Build 只 `make_unique` 不触发任何线程/IO。

---

## 3. 钉死项 2 — RunMode 砍到两档:PaperDaemon + Headless,删 ObserverOnly

### 3.1 ObserverOnly 是过度设计,现在不引入

GM 提的三档:`PaperDaemon`(全量带 HTTP)/ `Headless`(无 HttpServer)/ `ObserverOnly`(不起 PaperLoop/ML 仅观测)。

- 前两档**有真实 consumer**:`PaperDaemon` = 现 `stcpp_debug_server`(带看板 HTTP);`Headless` = systemd `paper_runtime`(无 HTTP,纯跑 paper)。这两个 binary 真实存在、systemd unit 真实期待,**留**。
- `ObserverOnly`(不起 PaperLoop/ML 仅观测)**当前没有任何 consumer**。没有 binary 用它,没有 systemd unit 要它,没有测试需要它。引入它要付:① 新 flag 解析;② Build/Run 里到处 `if (mode != ObserverOnly)` 分支;③ 三档组合合法性校验矩阵;④ 配套测试。**为一个无人调用的模式付这些成本 = 教科书级过度设计**,违反"数字说话 + 实盘优先"。
- 真要"只观测不交易",今天已有 `--no-record-ml`(L597)关 ML;关 PaperLoop 同理一个 flag 就够,**不需要上升为 RunMode 枚举档**。RunMode 表达的是"装哪些组件 / 起不起 HTTP"这种**结构性差异**,不是"开关某个组件"的运行时差异。

**决议:`enum class RunMode { PaperDaemon, Headless }` 两档。** ObserverOnly 记 backlog,等真有 consumer(e.g. 纯行情录制 binary)再加 —— 那时它该不该是 RunMode 还是独立 binary 另说。

### 3.2 两档的差异点必须收敛到一处

两档唯一结构差异 = **起不起 HttpServer + record_ml 默认值**。落地要求:`Build()` 接收 `RunMode`,内部**只有一处** `if (mode == PaperDaemon)` 决定是否 `make_unique<HttpServer>` + 是否默认开 ML。其余 7 组件两档完全一致。别让 mode 分支散落到 Build/Run/Shutdown 各处 —— 散落就是下一个 1000 行 main 的种子。组合合法性"校验 + 日志"GM 提的对,保留,但两档的校验矩阵就两行,别搞复杂状态机。

---

## 4. market_discovery 抽取 — ACK,补两条约束

GM 把 ~400 行 gamma 解析(`DiscoverSportsEvents` / `DiscoverSportsMarketsFlat` / `ExtractClobTokenIds` / `ExtractNextObject` / `ExtractMarketsArray` / `NormalizeSportsMarketType` / `DiscoveredEvent`/`DiscoveredMarket` 结构)抽成独立可测单元 —— **完全同意**,这是全重构里 ROI 最高的一刀:纯函数、无线程、无 IO 依赖(除 popen curl),独立可测,且老郭 §A.1.3 / backlog 第 10 项点名要处理的"boot 一次性 + 静态市场列表技术债"就靠这个抽出来后才好治。

补两条:

1. **`popen("curl ...")` 的 IO 边界要拆出去,留可测的纯解析核。** 现 `DiscoverSportsEvents` 把"curl 拉 JSON"和"解析 JSON"焊在一个函数里(L391-494),抽出来后**单测没法跑**(单测不该打真 gamma API)。要求:拆成 `FetchEventsJson() -> string`(IO,popen,不测或 mock)+ `ParseEvents(string json) -> vector<DiscoveredEvent>`(纯函数,喂 fixture JSON 单测)。可测的是 Parse,不是 Fetch。这才兑现老郭"独立可测"的承诺。
2. **`popen` + 拼 URL 的注入面记 backlog。** 现在 URL 是写死常量,无注入风险;但抽成库后若将来 `max_events` / tag 变可配,别让外部输入拼进 `popen` 的 shell 命令。MVP 不动,标注释即可。(curl via popen 本身非热路径、启动一次,R-12 不踩,这点现 main 注释 L105 已说清,保留。)

---

## 5. 组件 owner 引用生命周期 — 落地检查清单

现 main 用裸 `unique_ptr` 局部变量 + 大量 `*hub_owned` 引用传递(e.g. PaperLoop 构造吃 `*hub_owned, *paper_rm, *paper_position_ledger, *ledger_hub_owned, *quote_hub_owned`,L774-778;RealStateProvider 吃一堆裸指针 L783-788)。抽成成员后,**成员声明顺序 = 生死顺序**,必须满足"被引用者先声明(后析构),引用者后声明(先析构)"。依现 main 的真实引用图,声明顺序约束:

```
先声明 (后析构) ──────────────────────────────► 后声明 (先析构)
hub / score_store / ledger_hub / quote_hub        ← 被 PaperLoop / RealStateProvider 引用
paper_position_ledger / paper_rm_snap             ← 被 PaperLoop / RealStateProvider 引用;
                                                     paper_rm_snap 还被全局 hook 持裸指针 ★
paper_rm / paper_fv_model                          ← 被 PaperLoop 引用
paper_loop                                         ← 引用上面全部
real_provider                                      ← 引用 hub/snap/score_store/ledger/quote
inplay_feed                                        ← 引用 score_store
live_publisher / live_transport                    ← 引用 hub
ml_recorder                                        ← 引用 quote_hub
HttpServer (仅 PaperDaemon 档)                     ← 持 real_provider 裸指针, 最后声明 = 最先析构
```

落地验收三条:
1. **HttpServer 必须最后声明(最先析构)** —— 它持 `real_provider.get()` 裸指针(L923),server 还活着时 real_provider 不能死。
2. **`paper_rm_snap` 早声明** —— 它被全局 hook 持裸指针(§2.2 第 3 条),必须最晚析构;且 Shutdown 第 4 步 detach 必须在它析构前跑。这是声明序 + Shutdown 序**双重保险**。
3. **别用 `std::move` 把成员搬空后还引用。** 现 main 有 `real_provider->set_events(std::move(event_infos))`(L791)这类 move-in,是局部变量 move 进 provider,没问题;但抽类后注意别把"已 move 走的成员"再传引用。这条在 review GM diff 时逐行扫。

---

## 6. 给 GM 的落地 checklist(我会按这个 review 你的 diff)

- [ ] **B1:** `paper_daemon.cpp` / `market_discovery.cpp` 进**独立 `stcpp_paper_app` target**,`stcpp_debug_api` 库 source 列表**不含**这俩。`stcpp_debug_api` 的 `target_link_libraries` 保持 `Threads` only(别加重型库)。
- [ ] namespace `stcpp::app`(非 `stcpp::debug_api`);header 放 `include/stcpp/app/`。
- [ ] **三段式:** Build 不起任何线程/IO(含把现 L818 InplayFeed 的 Start 挪进 Run);Run 起全部线程;Shutdown 幂等 + 析构只调 Shutdown。
- [ ] **关闭序** 照 §2.2 第 2 条六步,手写在 Shutdown 里;`detach_rm_debug_snapshot` 在 paper_rm_snap 析构前(第 4 步)。
- [ ] **RunMode 两档**(删 ObserverOnly);mode 分支只在 Build 一处。
- [ ] **discovery 拆 Fetch/Parse**,Parse 喂 fixture 可单测。
- [ ] **成员声明序** 按 §5:HttpServer 最后、paper_rm_snap 最早。
- [ ] signal/g_stop 留 main.cpp,类只给 `RequestStop()`。
- [ ] 干净 build + ctest 全绿后再 commit(R-11/R-12 不回归:paper 不写真账本、WSS loop 无阻塞)。

---

## 7. 结论

**点头,带 1 blocker(B1 库归属)+ 2 钉死项(析构/Shutdown 二选一、RunMode 砍两档)。**

B1 是架构主权范围内的硬约束,不是商量:PaperDaemon 进独立 app 层,**绝不进 debug_api 库**,守住老郭 §A.2 警告的"契约库不许变成什么都连的 hub"。其余切法(抽类、三段式、market_discovery 抽取、headless binary)我全部 ACK。GM 按 §6 checklist 落地,diff 出来走我这一道 review,涉及 R-11 隔离的部分同步抄送老韩。

— 老周
