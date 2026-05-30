# PaperDaemon 重构测试计划 v1

- **owner:** 小宋 (test + replay, E 产品业务保障部)
- **last_review:** 2026-05-30
- **适用重构:** `debug_server_main.cpp` 装配逻辑 → `class PaperDaemon { Build()/Run()/Shutdown() }`
  进 `stcpp_debug_api` 静态库；新增 headless `paper_runtime` binary；引入
  `enum class RunMode { PaperDaemon, Headless, ObserverOnly }`
- **对应 GM 计划:** `stcpp_debug_api` 库可测 + `paper_runtime` headless binary
- **原则:** 只读分析 + 出计划供料，GM 写代码与测试；本文档不动主干任何 .cpp / CMakeLists.txt

---

## §1 回归门禁——合并前必须全绿的现有测试集

重构属"行为不变"搬迁：装配逻辑挪位，PaperLoop / RiskGateway / PositionLedger 等组件本身不改。
以下 ctest target 在 GM 每次提交后必须 100% 通过，任一红灯视为行为变更，阻塞合并。

### 1.1 必须全绿的 CTest Target（精确名称）

| 优先级 | Target 名 | 守护内容 | 失败意味着 |
|---|---|---|---|
| P0 | `test_paper_loop` | PaperLoop T01–T14，R-11/R-12/R-20 完整行为 | PaperLoop 本身被破坏 |
| P0 | `stcpp_test_integration_paper_e2e` | 10笔 E2E <50ms p99、4 WAL 物理隔离 | 端到端链路断裂 |
| P0 | `stcpp_test_integration_r11_pollution` | 50笔 E2E 全程 paper_audit 唯一、三路 WAL 空 | R-11 污染红线 |
| P0 | `stcpp_test_integration_audit_chain` | BLAKE3 audit chain 完整性 | audit 审计链断裂 |
| P1 | `test_risk_gateway` / `test_risk_gateway_wave3` | RM 全路径 + P0 整改 | RM 行为变化 |
| P1 | `test_rm_debug_snapshot` | lock-free ring 不翻倍写 (P0-1) | reject 去重失效 |
| P1 | `test_paper_signer` | PaperSigner Mode + WAL kind | signer 污染 |
| P1 | `test_virtual_matcher` | VirtualFill.mode_tag==0，audit_wal_kind=PaperAudit | R-11 matcher 侧 |
| P1 | `test_ledger_snapshot_hub` | double-buffer R-11/R-12/R-20 | positions/pnl 快照 |
| P1 | `test_quote_snapshot_hub` | double-buffer R-12/R-20 | quote 快照 |
| P1 | `test_fair_value_estimator` | FairValueEstimator 数值正确性 | 定价错误 |
| P1 | `stcpp_test_integration_transformer` | OrderIntent→SignV52Request 3 path | signer 变换链 |
| P2 | `test_paper_pm_client` | PaperPolymarketClient 14 接口 | mock PM client |
| P2 | `test_signer_v52` / `test_signer_v53_pit` / `test_signer_v62` | signer 全版本 | signer 链 |
| P2 | `test_fill_rate_model_v2` | VirtualMatcher Mode A | 成交模拟 |

**注：** `paper_gate/test_harness_self_check.sh` 也必须继续 5/5 PASS（运行
`bash tests/integration/paper_gate/test_harness_self_check.sh`）。

### 1.2 回归运行指令

```bash
# P0 快速门禁 (合并前必跑)
ctest --test-dir build -L "paper|r11|e2e|audit" -R "(test_paper_loop|stcpp_test_integration_paper_e2e|stcpp_test_integration_r11_pollution|stcpp_test_integration_audit_chain)" --output-on-failure

# P1 完整回归 (PR CI)
ctest --test-dir build -L "unit;paper" --output-on-failure
ctest --test-dir build -L "integration" --output-on-failure
```

---

## §2 新可测面 market_discovery——抽成纯函数后首次可单测

重构将六个 gamma 解析静态函数从 `debug_server_main.cpp` 的匿名 namespace 迁出，进入
`stcpp_debug_api` 静态库的 `market_discovery.hpp/.cpp`（建议 GM 暴露接口），链入测试后
**第一次可以用 gtest 直接覆盖**。

新 gtest target 建议命名：`test_market_discovery`
链接：`stcpp_debug_api`（或仅链接 `market_discovery` 单独小 target）

### 2.1 `ExtractClobTokenIds` — 双编码

| Case ID | 输入 | 期望结果 | 边界说明 |
|---|---|---|---|
| MD-01 | `"clobTokenIds":["tok-aaa","tok-bbb"]` | tok0="tok-aaa", tok1="tok-bbb", return true | 原生 array，正常 |
| MD-02 | `"clobTokenIds": "[\"tok-aaa\",\"tok-bbb\"]"` | tok0="tok-aaa", tok1="tok-bbb", return true | JSON-encoded string，gamma /events 真实编码 |
| MD-03 | `"clobTokenIds":["only-one"]` | return false | 仅 1 token，不足 2 |
| MD-04 | `"clobTokenIds":[]` | return false | 空数组 |
| MD-05 | `"clobTokenIds": "[]"` | return false | JSON-encoded 空数组 |
| MD-06 | `"clobTokenIds": "[\"tok-with\\\"quote\",\"tok-b\"]"` | tok0 含引号字符，return true | 嵌套引号转义 |
| MD-07 | 字段缺失（无 `"clobTokenIds":` 键） | return false | 缺字段 |
| MD-08 | `"clobTokenIds":["tok-a","tok-b","tok-c"]` | tok0="tok-a", tok1="tok-b", return true | 超过 2 token，取前两个 |

### 2.2 `ExtractNextObject` — 平衡括号

| Case ID | 输入片段 (在 `pos=0` 开始扫) | 期望 {start,end} | 边界说明 |
|---|---|---|---|
| MD-10 | `{"a":1}` | {0,6} | 最简单对象 |
| MD-11 | `{"a":{"b":2}}` | {0,12} | 嵌套对象 |
| MD-12 | `{"a":"has }"}` | {0,12} | 字符串内的 `}` 不计 |
| MD-13 | `{"a":"\\\""}` | {0, 11} | 字符串内转义引号 |
| MD-14 | `]` (以 `]` 开头) | {npos,npos} | 遇 `]` 表示数组已结束 |
| MD-15 | `` (空串) | {npos,npos} | 空输入 |
| MD-16 | `{"unclosed"` | {npos,npos} | 未闭合括号（depth 不归零） |

### 2.3 `NormalizeSportsMarketType`

| Case ID | 输入 raw | 期望输出 | 说明 |
|---|---|---|---|
| MD-20 | `"Moneyline"` | `"moneyline"` | 大写首字母 |
| MD-21 | `"MONEYLINE"` | `"moneyline"` | 全大写 |
| MD-22 | `"moneyline"` | `"moneyline"` | 已小写 |
| MD-23 | `"Spread"` | `"spread"` | spread 盘口 |
| MD-24 | `"Total Points"` | `"totals"` | "total" 子串 |
| MD-25 | `"Over/Under"` | `"totals"` | "over" 子串 |
| MD-26 | `"Outright"` | `"outright"` | 直接 |
| MD-27 | `"Futures"` | `"outright"` | "futures" 子串 |
| MD-28 | `"Prop Bet"` | `"prop"` | "prop" 子串 |
| MD-29 | `"Series"` | `"series"` | series 赛制 |
| MD-30 | `""` | `"unknown"` | 空串 fallback |
| MD-31 | `"xyzunsupported"` | `"xyzunsupported"` | 无匹配 → 原样返回 |

### 2.4 `DiscoverSportsEvents` — fixture JSON 喂

`DiscoverSportsEvents` 内部用 `popen(curl...)` 拉 live 数据，生产代码不宜在单测中发网络请求。
建议 GM 设计接受注入 JSON 字符串的重载，或将解析部分拆成
`ParseEventsResponse(const std::string& json, int max_events)` 纯函数。

| Case ID | Fixture 描述 | 期望结果 |
|---|---|---|
| MD-40 | 2 个 event，每个有 2 个 market，`clobTokenIds` 用 native array | result.size()==2，每个 ev.markets.size()==2，token_map 4 条 |
| MD-41 | 1 个 event，市场 `conditionId` 缺失 | 该 market 被跳过，ev.markets.empty() → event 也跳过 |
| MD-42 | 1 个 event，`markets:[]` 空数组 | ev.markets.empty() → event 被跳过，result 为空 |
| MD-43 | `sport` 字段为 `"null"` 字符串，`title` 含 "NBA" | 体育关键词匹配，event 被纳入 |
| MD-44 | `sport` 为空、`title` 无任何体育关键词 | event 被过滤，result 为空 |
| MD-45 | 3 个 event 但 `max_events=2` | result.size()==2，截断正确 |
| MD-46 | JSON-encoded `clobTokenIds` 格式 | 解析后 token_map 正确填充 |
| MD-47 | 嵌套引号 `"question":"NBA \"Finals\""` | ExtractJsonStr 不截断，question 字段完整 |

---

## §3 PaperDaemon 装配测试——不起真 HttpServer / 不连真 WSS

### 3.1 核心问题

`debug_server_main.cpp` 目前把 8 组件（OrderBookSnapshotHub / ScoreSnapshotStore /
LedgerSnapshotHub / QuoteSnapshotHub / PositionLedger / RiskGateway / PaperLoop /
FeatureRecorder）全部在 `main()` 内硬接线。重构后 `PaperDaemon::Build()` 负责
装配，`Run()` 阻塞等待信号，`Shutdown()` join 所有线程。

**测 `Build()` 的核心难点**：现有代码 `Build()` 内部直接调 `DiscoverSportsEvents()`（popen curl），
并在发现结果非空时构造 `LiveWssTransport + AsyncConnect()`。两者都需要外网，单测禁止。

### 3.2 建议 GM 留的 seam（可测钩子）

GM 在实现 `Build()` 时，应支持以下注入点：

**Seam-1：token_map 预注入**
```
PaperDaemon::Builder& with_token_map(MarketTokenMap tm);
```
当 `with_token_map` 被调用时，跳过 gamma 发现（不 popen curl），直接用注入的
token_map 完成剩余装配。测试通过此接口控制发现结果。

**Seam-2：WSS transport factory**
```
PaperDaemon::Builder& with_wss_factory(std::function<std::unique_ptr<IWssTransport>()>);
```
单测注入 NullWssTransport（空实现，AsyncConnect 立即返回不起线程），避免真 WSS 连接。

**Seam-3：RunMode 校验在 Build() 返回前完成**
Build() 返回 `BuildResult { ok, error_msg }` 而非直接 throw，便于测试 assert error_msg。

### 3.3 装配测试用例

新 gtest target 建议命名：`test_paper_daemon_build`
链接：`stcpp_debug_api`（重构后包含 PaperDaemon）+ `stcpp_paper_loop` + `stcpp_risk`
      + `stcpp_ledger_snapshot_hub` + `stcpp_quote_snapshot_hub`
标签：`"unit;paper-daemon;build;r-11;runmode"`

| Case ID | 测试场景 | 验证断言 |
|---|---|---|
| PD-01 | `with_token_map({"cond-1": {"tok-yes","tok-no"}})` + NullWssTransport + `RunMode::PaperDaemon` | `Build()` 返回 ok=true；`daemon.paper_loop()` 非 null；`daemon.ledger_hub()` 非 null |
| PD-02 | PD-01 基础上：读 `daemon.paper_loop()` 的 token_map → 与注入一致 | token_map["cond-1"] = {"tok-yes","tok-no"} |
| PD-03 | R-11 隔离验证：`daemon.paper_position_ledger()` 与 `daemon.live_position_ledger()`（若有）不是同一对象 | 指针不相等 |
| PD-04 | R-11 隔离验证：`daemon.ledger_hub()` 为 paper 专用；`Build()` 后不向其写 live WAL | `ledger_hub->Read("cond-1")` 初始为 empty（未被 live 路径污染） |
| PD-05 | `RunMode::Headless` + 注入 token_map | `Build()` ok；`daemon.http_server()` 为 null（headless 无 HTTP）；`daemon.paper_loop()` 非 null |
| PD-06 | `RunMode::ObserverOnly` + 注入 token_map | `Build()` ok；`daemon.paper_loop()` 为 null（观察者不跑 paper loop）；`daemon.wss_transport()` 非 null |
| PD-07 | 非法组合：`RunMode::Headless` + `enable_http = true`（显式开 HTTP） | `Build()` 返回 ok=false，`error_msg` 含 "Headless" 或 "incompatible" |
| PD-08 | 非法组合：`RunMode::ObserverOnly` + `enable_paper_loop = true`（强制开 paper loop） | `Build()` 返回 ok=false |
| PD-09 | 空 token_map（`with_token_map({})`）+ `RunMode::PaperDaemon` | `Build()` ok（空 token_map 合法，运行时 hub 为空）；paper_loop 存在但 tick 时 `hub_reads_empty` 增长 |
| PD-10 | `Shutdown()` 在 `Build()` 成功后立即调用（未 `Run()`） | 不崩溃，所有线程 join 完成 |

### 3.4 component 接线正确性验证（白盒快照）

PD-01 基础上运行 PaperLoop 若干 tick，验证管道接通：

```
注入 book → hub.Publish(tok-yes, feat) →
PaperLoop tick → quote_hub.Publish("cond-1", qf) →
assert quote_hub.Read("cond-1").has_value()
```

此测试复用 `test_paper_loop.cpp` 的 `MakeSyntheticBook()` 辅助函数逻辑即可。

---

## §4 RunMode 校验测试

新 gtest target（或合并进 `test_paper_daemon_build`）：`test_paper_daemon_runmode`
标签：`"unit;paper-daemon;runmode"`

### 4.1 三档启停组件矩阵

| RunMode | HttpServer | PaperLoop | LiveWssTransport | InplayFeedThread | FeatureRecorder |
|---|---|---|---|---|---|
| PaperDaemon | 启动 | 启动 | 启动 | 启动 | 启动（可选 --no-record-ml） |
| Headless | **不启动** | 启动 | 启动 | 启动 | 启动（可选） |
| ObserverOnly | 启动 | **不启动** | 启动 | 启动 | **不启动** |

### 4.2 启停断言用例

| Case ID | RunMode | 组件 | 断言 |
|---|---|---|---|
| RM-01 | PaperDaemon | HttpServer | `daemon.http_server() != nullptr && http_server.is_running()` |
| RM-02 | PaperDaemon | PaperLoop | `daemon.paper_loop() != nullptr` |
| RM-03 | Headless | HttpServer | `daemon.http_server() == nullptr` |
| RM-04 | Headless | PaperLoop | `daemon.paper_loop() != nullptr`（headless 仍跑 paper loop） |
| RM-05 | ObserverOnly | PaperLoop | `daemon.paper_loop() == nullptr` |
| RM-06 | ObserverOnly | HttpServer | `daemon.http_server() != nullptr`（观察者仍需 API） |
| RM-07 | ObserverOnly | FeatureRecorder | `daemon.ml_recorder() == nullptr`（无 paper loop 即无采集） |
| RM-08 | 非法值（RunMode 整型越界或 4+） | Build() | 返回 ok=false，error_msg 含 "invalid RunMode" |
| RM-09 | PaperDaemon，Shutdown() 后 | is_running() | `false`，所有 thread join 完毕 |
| RM-10 | Headless，Run() 后接收 SIGTERM | 进程退出码 | 0（优雅关停） |

### 4.3 日志断言

非法组合拒绝时，要求 `error_msg`（或 stderr）包含：
- `"RunMode"` 关键字
- 被拒组合的具体说明（"Headless + HttpServer" 或 "ObserverOnly + PaperLoop"）

此断言通过 `EXPECT_THAT(error_msg, ::testing::HasSubstr("RunMode"))` 实现。

---

## §5 paper_runtime Headless 冒烟

类比 `tests/integration/paper_gate/` 的 shell harness，新增：
`tests/integration/paper_runtime_headless/`

### 5.1 Harness 结构

```
tests/integration/paper_runtime_headless/
├── run_headless_smoke.sh         # 主冒烟脚本
├── fixtures/
│   └── synthetic_token_map.json  # 注入用 token_map fixture
└── README.md
```

### 5.2 冒烟步骤（`run_headless_smoke.sh`）

```
Step 1  构建 paper_runtime binary（假设已 cmake --build）
Step 2  启动 paper_runtime --headless --token-map fixtures/synthetic_token_map.json
        --no-record-ml --tick-ms 50 &
        PID=$!
Step 3  sleep 2s（等 3 个 tick）
Step 4  R-11 无 HTTP 验证：
          curl -s --max-time 1 http://127.0.0.1:8080/healthz → 连接拒绝（exit!=0）
          assert $? != 0   # headless 无 HTTP server 监听
Step 5  R-11 不污染断言（进程外）：
          不存在 /var/lib/stcpp/live/position.wal 或 /var/lib/stcpp/risk_audit.wal
          （headless paper 模式只写 paper_audit.wal，不写真账本路径）
Step 6  优雅关停：
          kill -SIGTERM $PID
          wait $PID
          EXIT=$?
          assert $EXIT == 0
Step 7  退出码 0 = 全 PASS；否则打印 FAIL + 具体失败步骤
```

### 5.3 关键 assert 清单

| Assert ID | 验证内容 | 预期 | 检测方式 |
|---|---|---|---|
| HS-01 | paper_runtime 启动不崩溃 | 进程存活 2s 后仍在 | `kill -0 $PID` |
| HS-02 | headless 模式无 HTTP 端口 | curl 8080 连接拒绝 | `curl` 退出码非 0 |
| HS-03 | R-11：不写 live WAL 路径 | live WAL 文件不存在 | `test ! -f` |
| HS-04 | R-11：不写 position.wal | position WAL 不存在 | `test ! -f` |
| HS-05 | SIGTERM 优雅退出 | wait 退出码 == 0 | `wait $PID; echo $?` |
| HS-06 | 启动后至少 1 次 PaperLoop tick | （若 binary 暴露 /metrics 则验；否则靠 HS-01 + 日志 grep） | grep "ticks_total=[^0]" 或进程存活 |

### 5.4 与现有 paper_gate harness 的对比

| 维度 | paper_gate harness | paper_runtime_headless smoke |
|---|---|---|
| 目的 | 30 日 PnL gate 验收 | 单次启动/关停行为正确性 |
| 数据源 | JSON fixture 文件 | synthetic token_map + 内存 book |
| 网络依赖 | 无（file mode） | 无（注入 token_map 跳过 gamma） |
| 运行时长 | <5s | ~5s |
| CI 阶段 | M1 gate 前置 | 每次 PR CI |

---

## §6 最大测试风险点

**重构最大风险：`Build()` 中 gamma 发现与组件装配的 seam 边界设计不清晰。**

具体说：`debug_server_main.cpp` 当前的 `DiscoverSportsEvents()` 调用发生在 `main()` 最
前端，其结果（`token_map`）是所有 8 个组件初始化的输入——PaperLoop / LiveWssTransport /
LiveBookPublisher / RealStateProvider / MarketInfoCatalog 全部依赖它。若
`PaperDaemon::Build()` 没有提供 `with_token_map` 注入接口，所有装配测试（§3）都无法
在不发真实 HTTP 请求的前提下执行，导致整个 §3 测试面无法在 CI 中落地。

**次风险：RunMode 切换是编译期常量还是运行时参数尚未确定。**
当前 `STCPP_EXEC_MODE_STR` 是 build-time 宏（`CMakeLists.txt` 注入）。若
`RunMode::Headless / ObserverOnly` 也走编译期路径，则 §4 和 §5 的三档各自需要三个
独立 binary，CI 构建矩阵复杂度翻 3 倍。若改为运行时 flag，R-7（paper-only build）
语义需要重新界定。**GM 在动代码前需先决定 RunMode 的生命周期（编译期 vs 运行时），
再由小宋相应调整 §4 / §5 的 harness 设计。**

---

## 附录 A：本文档覆盖的测试 target 汇总

| Target 名 | 类型 | 所在目录 | 新增/现有 |
|---|---|---|---|
| `test_paper_loop` (T01–T14) | unit | `tests/unit/` | **现有，守护** |
| `stcpp_test_integration_paper_e2e` | integration | `tests/integration/` | **现有，守护** |
| `stcpp_test_integration_r11_pollution` | integration | `tests/integration/` | **现有，守护** |
| `stcpp_test_integration_audit_chain` | integration | `tests/integration/` | **现有，守护** |
| `paper_gate/test_harness_self_check.sh` | shell harness | `tests/integration/paper_gate/` | **现有，守护** |
| `test_market_discovery` | unit | `tests/unit/` | **新增（§2）** |
| `test_paper_daemon_build` | unit | `tests/unit/` | **新增（§3+§4）** |
| `paper_runtime_headless/run_headless_smoke.sh` | shell smoke | `tests/integration/paper_runtime_headless/` | **新增（§5）** |

## 附录 B：与现有 Gap Report 的关系

本计划中 `test_market_discovery`（§2）直接关闭
`xiaosong-test-gap-report-v1.md` §1 Gap-2（gamma 解析器零覆盖，最高风险之一）。
`test_paper_daemon_build` 中 PD-03/PD-04 覆盖 Gap-1 提及的 has_real_fair 接线正确性前置
（先确保组件接对，再做 has_real_fair=true 路径集成）。

Gap-1（has_real_fair=true 路径从未被测）在本次重构中**暂不新增覆盖**，理由：重构属行为
不变搬迁，paper_loop.cpp 本身不改。Gap-1 对应的新测试（需要 Goalserve game_row 注入）留给
M2 接入真实 InplayFeedThread 时单独出计划。
