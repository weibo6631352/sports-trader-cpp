# 立项 — 看板 SSE Phase 2(focus 订阅 + Ops 通道)

**owner:** 老雷 (GM)
**last_review:** 2026-06-02
**状态:** 立项(设计 + 计划,待实施)
**前置:** Phase 1 已上线([laolei-sse-push-design-v1](laolei-sse-push-design-v1.md))—— 9 always-on 通道走 SSE,REST/SSE 序列化已单一数据源(`endpoint_payloads.hpp`)。
**评审:** 网络(老王)+ 架构(老郭)轻评审一轮;**风控不参与**(只读观测,[[no-risk-review-for-frontend]])。

---

## 1. 目标 / 为什么

Phase 1 后,看板唯一还在轮询的是:**展开盘口的全档 book/quote**(`refreshExpandedDetail` 每 2s REST)+ **Ops 页**(metrics/features/mapping)。Phase 2 把这些也纳入 SSE,实现**整看板零轮询**(除回退)。

- **价值大头 = focus 订阅**:你展开/选中的盘口,全档深度 + Quote 量化详情**由服务端推**,不再 2s REST 轮询。深度阶梯/edge 跟顶档一样秒级跳,且省掉跨洋往返。
- **价值小头 = Ops 通道**:metrics/features/mapping 上 SSE(Ops 页低频、按需,收益有限,排后)。

**北极星:稳态前端→服务端主动请求 = 0(仅一条 SSE 长连 + 偶发 focus POST)。**

---

## 2. 范围

| 子项 | 内容 | 优先级 |
|---|---|---|
| **2a. focus 订阅** | 展开/选中盘口的全档 `book` + `quote` 通过 SSE 推(新增 book/quote 两通道 + focus 副信道) | **P0(本立项核心)** |
| 2b. Ops 通道 | metrics / features_health / mapping_status 上 SSE(on-change) | P2(可后置) |
| 2c. pnl timeseries | 净值曲线走 SSE append 通道 | P3(可不做,REST 15s 够) |

本立项**实施 2a**;2b/2c 设计预留、按需再做。

---

## 3. focus 订阅设计(唯一有难度处,想透)

### 3.1 协议(信封不变,纯加 2 通道 + 1 副信道)
- SSE 单向(服务端→浏览器),"我在看哪些盘口"用**副 POST** 告知:
  ```
  POST /api/v1/stream/focus
  body: {"stream_id":"<hello 下发的>", "conditions":["0x..","0x.."]}
  → 200 {"ok":true,"n":2}   /  404 {"ok":false} (stream_id 未知/已断)
  ```
- 服务端据此在**该连接**的流里增发:
  - `book` 通道:`data` = 全档 `BinaryMarketBookView`(= REST `/book_pair/{cid}`,含 condition_id);
  - `quote` 通道:`data` = 全 `QuoteParams`(= REST `/quote/{cid}`,含 market_id)。
  - 每 focused cid 一帧(data 自带 cid);on-change 推(深度/报价变才发)。
- hello.channels 增加 `{"name":"book","delta":"snapshot"},{"name":"quote","delta":"snapshot"}`(前端老代码忽略新通道 = 向后兼容)。

### 3.2 服务端架构(focus 跨线程通信 —— 关键)
- SSE provider 跑在**每连接独立线程**(Phase 1 既有);POST /focus 在**另一线程**到达 → 需要把 focus 集合从 POST 线程传到 provider 线程。
- **共享注册表**(新):`FocusRegistry` = `mutex + unordered_map<stream_id, shared_ptr<FocusState>>`。
  - `FocusState` = `std::atomic<std::shared_ptr<const std::vector<std::string>>>`(C++20 atomic shared_ptr,**provider 每 tick lock-free 读,POST copy-on-write 整体换**;读侧零锁,符合 R-12 精神)。
  - SSE provider 连上:生成唯一 `stream_id`(per-server atomic 计数器,非 now())→ 注册表插入 FocusState;断开(loop 退出 / resource_releaser):移除条目。
  - POST /focus:查 stream_id → 原子换新 conditions 向量;未知 stream_id → 404。
- provider 每 tick:读本连接 FocusState 的 conditions(封顶,如 ≤ 32 防滥用)→ 对每个 cid 算 book/quote payload,on-change 推。
- **R-12**:全程只读 const StateProvider 快照;FocusState 读 lock-free;POST 仅写注册表(非热路径)。不碰 RM/signer/WSS。

### 3.3 序列化单一数据源(延续 Phase 1,不返工)
- 新增 `payload::book_pair(sp,cid)` + `payload::quote(sp,cid)` builder。
- **同步把 REST `endpoint_book_pair.cpp` / `endpoint_quote.cpp` 改调这俩 builder**(单一数据源,REST/SSE 不漂移)—— 与 Phase 1 八端点同样处理。
- focus 推送复用同一 builder。零重复。

### 3.4 前端(`store.ts`)
- hello 收到 → 存 `stream_id`。
- `detailInterest` 变化(展开/折叠/选中)→ **POST /api/v1/stream/focus**(替代 `refreshExpandedDetail` 的 2s 轮询)。focus 即时性:POST 后下一 tick(≤1s)即推;展开瞬间仍可保留一次 REST 即时拉(优先级插队)兜底首屏。
- `book`/`quote` SSE 通道 → 写 `conditionCache[cid].book/.quote` + rebuildGroups。
- **回退**:SSE 死 → 退回 `refreshExpandedDetail` REST 轮询(Phase 1 回退框架已在,focus 仅多注册一类)。

---

## 4. 里程碑 / 计划

| # | 任务 | 产出 |
|---|---|---|
| M1 | `payload::book_pair` + `payload::quote` builder;REST 两端点改调(de-dup) | 单一数据源,编译+curl 验证 REST 不变 |
| M2 | `FocusRegistry` + `POST /api/v1/stream/focus` + provider 增 book/quote 通道;stream_id 改唯一计数器 | 后端 SSE 推 focus 盘口 |
| M3 | 前端:hello 存 stream_id;detailInterest→POST focus;book/quote 通道入 store;回退保留 | 展开盘口零轮询 |
| M4 | 联调 + 上线验证 | 展开盘口深度/报价 SSE 秒级推;`refreshExpandedDetail` 轮询消失;断连回退 OK |
| (后置) | 2b Ops 通道 / 2c timeseries | 按需 |

---

## 5. 验收

- 展开任意盘口:全档深度 + Quote 详情**经 SSE 推**,秒级更新;network 里**无 `/book_pair`、`/quote` 周期轮询**(仅展开瞬间一次兜底 REST,可选)。
- 稳态前端→服务端主动请求 ≈ 0(1 条 SSE + focus 变更时偶发 POST)。
- focus 上限(≤32)防滥用;连接断开 focus 自动清理无泄漏。
- SSE 断 → 自动回退 REST,功能不降级。
- REST `/book_pair`、`/quote` 输出与改前一致(de-dup 不破契约)。

---

## 6. 风险 / 注意

- **focus 跨线程**:atomic shared_ptr COW,读 lock-free;务必在 provider 退出时从注册表摘除(防 stream_id 泄漏 / 悬挂)。
- **连接复用**:浏览器重连换新 stream_id(hello 重发)→ 前端须在 hello 后**重新 POST focus**(重连后重订)。设计已含(hello→存 id→若有 detailInterest 立即 POST)。
- **focus POST 与 SSE 不同连接**:走普通 POST(短请求),不占 SSE 线程;并发闸管它。
- 不引入双向协议复杂度(不上 WebSocket)——副 POST 足够,符合"最小机制"。
