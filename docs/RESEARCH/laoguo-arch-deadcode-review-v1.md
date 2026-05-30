# 架构健康 + 旧垃圾代码评审 v1 (供 GM 料)

- owner: 老郭 (首席架构评审 + 顾问团协调人)
- last_review: 2026-05-30
- 性质: 纯供料 (§10.2) — 只读分析, 不碰代码主干。GM 拍板, 本文只列不删。
- 评审对象: main 代码树 @ commit ebbbf1d (worktree base)
- 方法: 全树只读扫 — 261 个 .cpp/.hpp (62974 行), find/grep + 逐文件 Read。
- 真实树根: `src/stcpp/<module>/` + `include/stcpp/<module>/` (非 `apps/` / `bin/paper.cpp`)。

---

## A. 架构健康评审

### A.1 `debug_server_main.cpp` (962 行) 是否该拆 — 结论: 该拆 main 编排层, 但拆法有讲究

实测 962 行确认。它确实身兼数职, 一个 `main()` 里顺序做了:
1. CLI 解析 (`--paper / --inplay / --wss / --gamma-filter` 等 flag 组合);
2. gamma 市场发现 (同步阻塞 boot);
3. 装配 paper 依赖栈 (FairValueEstimator / PositionLedger / RmDebugSnapshot / QuoteSnapshotHub / LedgerSnapshotHub / PaperLoop);
4. 起 InplayFeedThread (小段 in-play feed 线程);
5. 起 LiveWss / live_book_publisher 线程;
6. 起 HttpServer (DebugServer, 阻塞 main 线程跑到退出)。

**GM "统一到 debug_server 作 paper daemon" 的决定 — 评审: 方向接受, 但当前实现是"装配逻辑全压在 main 函数体"的反模式。**
对比 `src/stcpp/bin/paper.cpp` (133 行的 W4 stub loop, 已被 GM 选定废弃), 把 daemon 收敛到一个 binary 是对的 (省进程、省 IPC、paper 循环和观测 API 同源读状态)。问题不在"合并"决定本身, 而在合并后 **962 行的 boot/wiring 代码没有从 `main()` 里抽出来**。

**架构代价 (3 条):**

1. **装配逻辑不可测、不可复用。** 现在"如何把 8 个组件接成一个 paper daemon"这套 wiring 写死在 `main()` 函数体里, 没有一个 `PaperDaemon` / `AppContext` 类承载。后果: 集成测试无法在不起真 HttpServer 的前提下验证装配正确性; 将来要起第二个 entrypoint (e.g. headless paper without HTTP) 只能复制粘贴。
   **建议: 抽 `class PaperDaemon { Build(cfg); Run(); Shutdown(); }`**, `main()` 退化为 ~30 行 (parse args → build → run)。962 行里真正属于 main 的不到 5%, 其余应进 daemon 类 + 各自的 builder。

2. **进程角色二相性靠裸 bool flag 隐式组合, 没有 RunMode 枚举钉死合法集。** `--paper / --inplay / --wss` 四个独立 flag = 16 种组合, 但合法的只有少数几个 (observer-only / paper-daemon / paper+live-feed)。"起了 inplay feed 但没起 paper" 这类半成品状态没人拦。而 R-11 (paper 不污染真账本) 的边界恰恰依赖角色清晰。
   **建议: `enum class RunMode`, 启动时一次性校验组合合法性 + 日志打印当前 mode。**

3. **gamma discovery 同步阻塞 boot, 且市场列表 boot 后静态。** 跨洋高延迟下 boot 阻塞数百 ms~秒级; 盘中新开市场不被发现。MVP 可接受, 但要在 ADR 记一笔"已知限制: 市场发现是 boot 一次性, 非动态"。

### A.2 模块依赖方向 — 实测结论: 比预想干净, 无致命反向依赖

实测跨模块 `#include "stcpp/..."` 边 (84 条 include 全扫):
- `debug_api → {data, risk, sizing, paper, pricing, polymarket}` (观测/编排层依赖业务层, 合理)
- `paper → {pricing, microstructure, risk, strategy, data, infra}` (合理)
- `ml → {data/feature_store_contract, ml/*}` (高层依赖低层, 合理)
- `strategy → {pricing, data}` (合理)

**关键澄清 (纠正一个常见担忧): 不存在 `data <-> ml` 循环依赖, 也不存在 pricing/risk 反向依赖 ml。**
实测 `grep 'include "stcpp/ml/"' src/stcpp/pricing src/stcpp/risk src/stcpp/data` = **0 命中**。`model_feature_spec.hpp` 只 include `data/feature_store_contract.hpp` 和 `ml/fair_value_model.hpp` (都是合法的高→低方向), 不 include orderbook。R-12 关联的反向依赖风险在当前树**不成立**, 这一项 GM 可以放心。

**唯一结构性观察 (非环, 是潜在耦合):** `debug_api/real_state_provider.hpp` (710 行) + `state_provider.hpp` (518 行) 把"观测只读契约"和"从 7 个 hub/ledger 拼 snapshot"的逻辑揉在一起, 且 include 面很宽 (score_store / orderbook_hub / wss / ledger_hub / risk_gateway / quote_hub)。这是 debug_api 成为"全系统状态读取中枢"的体现, 目前合理, 但它和 §A.1 的 962 行 main 是同一个膨胀趋势的两个面: **debug_api 正在变成"什么都连"的 hub**。记 backlog, MVP 不动。

### A.3 重复造轮子 — 确认 4 个 de-vig 实现, 收敛建议

| # | 位置 | 算法 | 状态 |
|---|---|---|---|
| 1 | `pricing/fair_value_estimator.hpp` `devig_binary(yes_mid,no_mid)` | 二元 YES/NO de-vig, 有单边退化 + nullopt 守卫 | **SSOT (paper 热路径在用)** |
| 2 | `strategy/p0_01_goalserve_devig.cpp` `compute_multiplicative_devig(span<BookmakerOdds>)` | 跨 8-9 家 multiplicative 均值 (ADR-008) | **正当 (语义不同: 多家聚合)** |
| 3 | `strategy/p0_01_pinnacle_no_vig.cpp` `no_vig(...)` | 单源 Pinnacle no-vig (W4 旧锚源) | **疑似被 #2 取代** (ADR-008 注明 "替换 W4 Pinnacle 锚源") |
| 4 | `ml/model_feature_spec.hpp` `detail::devig_one(odds_yes,odds_no)` | 单家 de-vig, 仅算 ML feature `g_bm_devig_p_yes` | **重复, 但隔离在 detail 命名空间, 仅本 header 内部调用** |

判断: #1 和 #2 **语义不同** (单市场二元 vs 跨多家聚合), 不算重复, 各有其位。真正重复的是 **#3 (单源 no-vig, 已被多源 #2 取代) 和 #4 (单家 de-vig helper)**。
- #4 风险低 — 它在 `detail` 内, 只服务 ML 特征提取, 不进交易决策路径, 数学上和 #2 的逐家 `p_yes_raw/overround` 一致。可不动, 或将来抽 `pricing::devig_one_raw` 共用。
- #3 是真正的清理候选 (见 B 表)。

**铁律#3 (回测/实盘一致) 关注点:** 四处 de-vig 都用 multiplicative (`p_raw / overround`), 数学族一致, 没发现"实盘一条路、回测另一条路"的分裂。这一点合规。但**多实现长期共存的风险是漂移** — 建议 ADR 记一条"de-vig 数学族锁定 multiplicative, 新增实现必须复用既有纯函数, 不许再手搓"。

### A.4 其它 (非阻塞)
- `signer/` v52/v62 双版本共存: v52 标 deprecated (backtest 历史回测用), v62 是当前。这是有意的 ABI 兼容保留, 非垃圾。
- `polymarket/live/live_pm_client.cpp` 14 接口全 stub (R-7, M5+ deferred): 是 build-time mode 物理隔离的占位, 不是死代码。

---

## B. 旧垃圾代码清单 (只列不删, GM 拍板)

风险分级: **低** = 0 业务引用可直接删; **中** = 有引用/测试需先迁移; **高** = 涉热路径/红线需评审。
注: 全树**没有** `.bak` 文件, **没有** `include/stcpp/risk/Untitled` (git 未跟踪且工作区已不存在)。

| # | 文件 | 为什么是垃圾 | 引用 (grep) | 删除风险 |
|---|---|---|---|---|
| 1 | `src/stcpp/bin/paper.cpp` (133 行 stub loop) | GM 已选定废弃 (统一到 debug_server)。W4 Wave 19 stub: `RunStubLoop()` 跑 1 条假 intent 打心跳后退出, condition_id 写死 `0xstub000...` | CMake `src/stcpp/bin/` 仍建 target; 无业务代码 include | **中** — 删文件 + 删 bin/CMakeLists 对应 target。注意 `bin/cli/` 下还有 three_signature / strategy_unlock_cli 两个真 CLI, 别误删整个 bin 目录 |
| 2 | `src/stcpp/strategy/p0_01_pinnacle_no_vig.{cpp,hpp}` (228+185 行) | ADR-008 自述 "替换 W4 Pinnacle 锚源 → Goalserve 多家"。单源 no-vig 已被 `p0_01_goalserve_devig` 取代 | 需确认是否仅剩单测 `test_p0_01_signal.cpp` 引用 | **中** — 若只剩测试引用即可连测试一起退役; 若仍被装配进 paper 则先迁移。删前 grep 确认 |
| 3 | `src/stcpp/data/goalserve_stub.cpp` (243 行) | W4 header-only stub, 不打真 HTTP, 返 mock fixture。注释自述 "W5 替换为 cpp-httplib" | 是 `goalserve_client` 的当前唯一实现体 | **高** — 这是 MVP 当前**实际在用**的 Goalserve 数据源 (真 HTTP 未接)。不是垃圾, 是"未完成的真功能"。误删 = paper 无数据源。**列入 B.1 不列垃圾** |
| 4 | `ml/model_feature_spec.hpp` `detail::devig_one` | 第 4 个 de-vig 实现 (见 A.3) | 仅本 header 内部 ML 特征提取调用 | **低** — 可保留 (隔离良好) 或将来抽公共纯函数。不急 |
| 5 | `src/stcpp/ml/hook.cpp` (202 行, R-7 老钩子 MLDataHook v0.1) | W4 Wave 20 旧钩子。注释多处 "v0.1 unordered_map, W6 切 SwissTable+LRU 防 OOM 一直没做" | `ml/hook.hpp` 被 `test_ml_hook.cpp` (382 行) 引用; 是否进 paper 装配需确认 | **中** — 有测试覆盖, 不能直接删。若新 feature_recorder 路径已取代它进 paper, 则 hook 退为纯测试件可退役。需 ml owner 小邓确认 hook vs feature_snapshot/parquet_writer 路径关系 |
| 6 | `src/stcpp/ml/fair_value_model.cpp` (32 行) | ADR-037 ONNX 适配点, body 是 "W11 前返 nullptr, 调用方回落 StubFairValueModel" | 接口契约占位 | **低风险但别删** — 是 ONNX 切入点的占位, 删了 W11 接模型要重建。属"未完成真功能"非垃圾 |
| 7 | `src/stcpp/data/parquet_writer.cpp` (134 行) | W6 stub, `flush_to_file()` 返 `StubNotImplemented` 不写盘; CSV 冒充 parquet | `test_parquet_writer_stub.cpp` 引用 | **中** — 与红线#3 (回测/实盘一致数据) 相关: 当前特征落盘是 stub。不是垃圾, 是缺口。owner 小田/小冯关注 |
| 8 | `src/stcpp/infra/wal/wal_writer.cpp` (123 行) | W3 skeleton: ring/bg-fsync/fd 全 stub, `Append` 不真写磁盘 (多处 `TODO W4`) | WAL framework 基座, 被 position_ledger 等用 | **高** — 这是 WAL 持久化的地基, 当前"不真写盘"。R-11/R-20 审计链依赖它。绝非垃圾, 是**关键未完成项**, 单独拎出来给老韩/老王看 |
| 9 | `signer/v52/signer_v52.{cpp,hpp}` (324+258 行) | 标 deprecated, 当前生产用 v62 | `test_signer_v52.cpp` (830 行) + backtest 历史回测引用 | **低 (但建议留)** — 有意保留的历史 ABI。除非确认 backtest 不再需要 v5.3 历史签名复现, 否则别动 |
| 10 | gamma discovery boot 逻辑 (在 `debug_server_main.cpp` 内) | 非独立文件, 但 §A.1.3 所述 boot 一次性 + 静态市场列表是技术债 | main 内联 | **中** — 随 §A.1 PaperDaemon 重构一并处理 |

### B.1 stub/TODO 但属"未完成真功能"非垃圾 (不要删, 进 backlog 跟踪)
这些 grep 命中 "stub/TODO/v0.1" 但**是 MVP 范围内的真功能未完成**, 删了会塌:
- `data/goalserve_stub.cpp` — 当前唯一 Goalserve 数据源 (真 HTTP 待接)
- `infra/wal/wal_writer.cpp` — WAL 不真写盘 (R-11/R-20 审计地基)
- `data/parquet_writer.cpp` — 特征落盘 stub (回测一致性相关)
- `ml/fair_value_model.cpp` — ONNX 切入点占位
- `polymarket/live/live_pm_client.cpp` — R-7 物理隔离 live 占位
- `signer` live/backtest 分支 stub — M5+ secp256k1 待切
- `observability/audit_record.hpp` BLAKE3 — W4 XOR stub, Sprint-3 切真 BLAKE3 (代码已有 BLAKE3_REAL/STUB 双路 + CI grep 拦截, 机制健康)

全树 grep 命中: `stub` 117 处、`v0.1` 大量、`TODO/deprecated` 若干。**绝大多数是有意占位 + 注明 owner/wave 的良性 TODO**, 不是无主烂尾。真正"无引用 dead code" 在本树**很少** —— 这个代码库的卫生比典型 MVP 好, 主要债是"未完成"而非"已死"。

---

## C. 最该先做的 1 项

**先抽 `PaperDaemon` / `AppContext` 类, 把 962 行 `debug_server_main.cpp` 的装配逻辑从 `main()` 函数体里搬出来 + 引入 `RunMode` 枚举。**

理由:
1. **这是 GM 当前主线 (统一 debug_server 作 paper daemon) 的直接配套** — GM 既然定了合并, 就该把合并产物结构化, 否则每加一个组件 main 就再胖一圈, 很快不可维护;
2. **解锁可测性** — 装配进类后, 集成测试能在不起 HttpServer 的前提下验 wiring; 当前 962 行 main 根本测不了;
3. **RunMode 枚举顺手收紧 R-11 边界** — 把 paper/observer/live-feed 合法组合钉死, 比四个裸 bool 安全;
4. **改动是"搬移 + 封装", 风险可控**, 适合 GM 一两轮收口, 不动业务逻辑。

第二顺位 (零风险纯减负, 顺手做): 删 `bin/paper.cpp` stub + 对应 CMake target (B#1)。
第三顺位 (需 owner 协同, 排 sprint): 退役 `p0_01_pinnacle_no_vig` (B#2, 确认无装配引用后) + 厘清 `ml/hook.cpp` vs feature_recorder 路径 (B#5, 小邓)。

**注意我推翻了一个常见误判:** 没有 `data<->ml` 循环依赖, 没有 pricing/risk 反向依赖 ml, 也没有 `Untitled` 垃圾文件。这些在真实树里都不成立, 不要花时间去修不存在的问题。

---

## D. 评审边界声明

- 本文只看 system 架构 / 依赖方向 / 死代码。idiom 层归现代 C++ 顾问; 产品取舍归 CPO 老钱。
- R-11 (paper 污染真账本) / R-12 (event loop 锁) 最终裁定权在红线 owner (老周/老韩/老郭任一)。
  §A.1 的 daemon 重构 + §A.2 debug_api 状态中枢化, 建议 GM 写之前找老周 (架构主权) + 老韩 (R-11) 点头。
- B 表凡标"高"风险项 (goalserve_stub / wal_writer) **本质是未完成真功能不是垃圾**, 误删会塌系统, GM 清理时优先看 B.1。
