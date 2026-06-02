# 设计 — 看板 SSE 推增量(Server-Sent Events)v1

**owner:** 老雷 (GM)
**last_review:** 2026-06-02
**状态:** 设计待评审/拍板(未实施)
**目标:** 把看板从"前端 N 个轮询 + 跨洋逐请求 RTT"改成"一条长连接,服务端推增量",往返次数→0,看板真正实时。**一次设计到位,留足扩展,避免"数据不够再重设计"。**

---

## 0. 为什么是 SSE(而非 HTTP/2 / WebSocket)

- **HTTP/2**:解的是"6 连接不够",但我们已用 `/grid` 批量化(N→1)绕过;瓶颈是 200ms RTT,h2 改不了 RTT。(见 [2026-06-02-http2-backend-eval](../MEETINGS/2026-06-02-http2-backend-eval.md))
- **SSE**:一条长连接,**连上后服务端推送只付单向延迟(~100ms),不再每次更新付一个 RTT 往返**。轮询的"请求→等→响应"循环消失。这是高延迟链路的正解 —— 减往返。
- **WebSocket**:双向、要心跳/重连/帧协议,对【只读观测看板】过重。SSE 单向(服务端→浏览器)恰好够用,且浏览器 `EventSource` 原生支持自动重连 + `Last-Event-ID`,cpp-httplib `set_chunked_content_provider` 原生可写。
- SSE 走普通 HTTP/1.1,**无需 TLS/证书/反代**(这正是 HTTP/2 的成本);将来若上反代+h2,SSE 照样跑。

---

## 1. 前端数据全盘点(确保不漏 —— 这是"数据够不够"的根)

当前轮询(`store.ts initPolling`)消费的全部数据,按"是否always-on、频率、体量"分类:

| # | 数据 | 现轮询 | 体量 | 变化频率 | 归属 |
|---|---|---|---|---|---|
| 1 | **status**(mode/state/wss_connected/signals_active/positions_count/rm_rejects_60s/uptime/as_of) | 2s | ~250B | 秒级 | **SSE 推** |
| 2 | **account**(cash/equity/bankroll/...) | 5s | ~300B | 秒级 | **SSE 推** |
| 3 | **grid**(每 condition 顶档: best_bid/ask/cross_spread/fair/edge_bps/sharp_fair/model_confidence/advisory/event_ts) | 2s | ~50KB 全量 | 秒级(live 簿动) | **SSE 推(delta)** |
| 4 | **scores**(每 live event: home/away/home_score/away_score/period/clock_sec/status/sport) | 5s(节流) | ~小 | 秒~十秒级 | **SSE 推** |
| 5 | **events**(发现的市场结构: event_id/slug/title/sport/live/neg_risk/condition_ids[]) | 5s | ~中 | 分钟级(重发现) | **SSE 推(keyframe)** |
| 6 | **positions**(每 market: outcome/net_qty/avg_px/pnl_realized/pnl_unrealized) | 5s | 小(paper 常空) | 成交时 | **SSE 推** |
| 7 | **pnl_attribution**(per_market net_pnl + 总计) | 15s | 小 | 成交时 | **SSE 推** |
| 8 | **gate**(paper gate 状态) | 15s | 小 | 偶发 | **SSE 推** |
| 9 | **rejects**(近 60s 风控拒单滚动) | 5s | 小~中 | 拒单时 | **SSE 推** |
| 10 | **pnl_timeseries**(净值曲线) | 15s | 中 | 秒级追加 | SSE 推(tail)或保留 REST |
| 11 | **metrics**(Prometheus 文本) | 30s | **大(KB~10KB)** | 秒级 | 保留 REST(Ops 页常驻,大文本) |
| 12 | **features/health**(110 特征健康) | 20s | 中 | 慢 | 保留 REST(Ops 页) |
| 13 | **mapping/status**(condition↔Goalserve) | 10s | 中 | 慢 | 保留 REST(Ops 页) |
| 14 | **book_pair/{cid}** 全档深度阶梯 | 2s(展开行) | 中/盘 | 秒级 | 保留 REST 按需(展开)→ v2 可 SSE focus |
| 15 | **quote/{cid}** 全 QuoteParams | 2s(展开行) | 中/盘 | 秒级 | 保留 REST 按需 → v2 可 SSE focus |
| 16 | **market/{cid}** 全 MarketInfo 元数据 | 60s/展开 | 中/盘 | 极慢 | 保留 REST 按需 |

**设计原则:**
- **always-on + 高频 + 全局**(1-10)→ 进 SSE 流。这是让看板"活"起来的核心,也是消灭轮询风暴的关键。
- **按需(只看一个盘口时)或大体量或 Ops 页低频**(11-16)→ 保留 REST(展开行 REST 已有优先级插队,即时)。
- **v2 扩展点**:per-condition 全档(14/15)做成 SSE "focus 订阅"(见 §4),则展开的盘口也推、零轮询。**v1 不实现,但协议信封预留,加它不动协议。**

---

## 2. SSE 端点与连接

```
GET /api/v1/stream            (text/event-stream, 长连接)
  ?since=<seq>                 (可选; 浏览器重连自动带 Last-Event-ID 头, 二选一)
```
响应头:
```
Content-Type: text/event-stream; charset=utf-8
Cache-Control: no-cache, no-store
Connection: keep-alive
X-Accel-Buffering: no          # 防 nginx/反代缓冲(将来上反代时关键)
```
连接建立后,先发 2KB `:` padding 注释(防中间节点缓冲不 flush),再发 `hello` + 全量 `snapshot`,之后按 tick 推 `delta` + 周期 `heartbeat`。

---

## 3. 协议信封(★ 不返工的关键:通用 + 版本化 + 命名通道)

每条 SSE 消息:
```
id: <seq>
event: <channel>
data: <json-envelope>
\n
```
- `event:`(通道名)∈ `hello | status | account | grid | scores | events | positions | pnl | gate | rejects | heartbeat | bye`(+ v2: `book | quote`)。
- `id:`(= envelope.seq)→ 浏览器自动在重连时回传 `Last-Event-ID`。

**data JSON 信封(所有通道统一外壳,内层 data 各通道自定义):**
```json
{
  "v": 1,                       // 协议版本(加字段不升, 破坏性变更才升)
  "seq": 12345,                 // 单调序号(= SSE id); 见下"seq 语义"
  "as_of_ts": 1780331890286695207,  // ★仅 transport 快照时刻(服务端本地 now, 合法);
                                //   保活/连接 staleness 判据用。【绝非数据新鲜度】
  "mode": "snapshot",           // "snapshot"(全量关键帧) | "delta"(changed/removed) | "append"(追加, 如 timeseries)
  "data": { ... }               // 通道专属载荷(见 §3.2)
}
```
**★ as_of_ts 语义钉死(老郭评审 / R-20):** 信封 `as_of_ts` = 服务端**发帧时刻**(本地 now,仅判"连接还活着/帧有多旧"),**不是数据新鲜度**。**数据新鲜度一律用各通道 data 内的 `event_ts`/`data_source_ts`(上游 ts)**。前端 staleness 着色(盘口延迟红黄绿)**必须读 grid.data 里的 `event_ts`,绝不读信封 as_of_ts** —— 否则"连接活着"会被误显成"数据新鲜",违 R-20。(另:`endpoint_grid.cpp` 现用 `now_epoch_ns()` 当顶层 as_of_ts 是同类语义,合法,因前端摘要延迟读的是每盘 event_ts。)

**★ seq 语义(老郭评审):** seq 为 **per-server 单调递增**(非 per-connection),仅用于前端**去重 / 乱序 / 漏帧 gap 检测**,**不承诺跨重连连续**,服务端**不据 `Last-Event-ID`/`?since` 做任何 delta 回放**(一律重发全量 snapshot)。`?since` 参数取消(去歧义);hello 帧带 `"replay":"none"` 明示。

**★ mode 三态(老郭评审):** 二元 snapshot/delta 不够 —— `rejects` 是滚动窗(全量替换)、`pnl_timeseries`(Phase 2)是追加。故 `mode` 取 `snapshot|delta|append`,且**每通道的 delta 语义在 hello.channels 里逐个声明**(见 §3.1),前端按声明处理,新通道加 append 语义不动信封。

### 3.1 控制通道
- **hello**(连上即发一次):
  ```json
  {"v":1,"server":"7c5eb97","stream_id":"a1b2c3","tick_ms":1000,"keyframe_ms":30000,
   "replay":"none","compress":[],
   "channels":[
     {"name":"status","delta":"snapshot"},   {"name":"account","delta":"snapshot"},
     {"name":"grid","delta":"delta"},         {"name":"scores","delta":"delta"},
     {"name":"events","delta":"snapshot"},    {"name":"positions","delta":"snapshot"},
     {"name":"pnl","delta":"snapshot"},       {"name":"gate","delta":"snapshot"},
     {"name":"rejects","delta":"full"}        // 滚动窗: 每次全量小数组替换
   ]}
  ```
  前端据此知道:协议版本、**stream_id**(★ v2 focus 副 POST 回传用,纯加性预留,Phase 2 不动 hello)、推送节奏、**每通道的 delta 语义**(snapshot/delta/append/full)、是否回放(`none`)、压缩能力(`compress` 预留,v1 空=不压)。**新通道/新字段前端不认识就忽略,向后兼容。**
- **heartbeat**(每 ~10s,即使无数据也发):`{"v":1,"seq":N,"as_of_ts":...,"kind":"delta","data":{}}` —— 保活 + 前端据 as_of 判断 staleness(超时即标"连接可能断")。
- **bye**(服务端优雅关闭/重启前):`{"reason":"shutdown"}` → 前端立即重连或回退轮询。

### 3.2 数据通道载荷(全量 snapshot 形态;delta 为同形子集)

- **status.data** = 现 `/status` 全字段(state/mode/wss_connected{sports_api,clob,user_channel}/signals_active_count/positions_count/rm_rejects_last_60s/uptime_sec/data_source)。
- **account.data** = 现 `/api/v1/account` 全字段。
- **grid.data**:
  ```json
  // snapshot: 全量
  {"markets":[{"condition_id":"0x..","book_found":true,"best_bid":0.81,"best_ask":0.82,
               "cross_spread":-0.01,"event_ts":N,"ingestion_ts":N,
               "quote_found":true,"fair":0.83,"market_mid":0.815,"edge_bps":120.5,
               "sharp_fair":0.84,"model_confidence":0.6,"advisory":true}, ...]}
  // delta: 只含变化的盘口 + 移除的
  {"changed":[{condition_id, ...同上字段...}], "removed":["0x..."]}
  ```
  (即现 `/api/v1/grid` 的 markets[],复用同一 provider 循环;delta 由服务端对比上次发送的 per-cid 摘要算出。)
- **scores.data**:`{"scores":[{event_id,home,away,home_score,away_score,period,clock_sec,status,sport}, ...]}`(delta:changed/removed 同理,按 event_id)。
- **events.data** = 现 `/api/v1/events` 的 events[](结构变化才发,kind=snapshot;通常分钟级)。
- **positions.data** = 现 `/api/v1/positions` 的 positions[]。
- **pnl.data** = 现 `/api/v1/pnl/attribution`(per_market + 总计)+ 可选 timeseries tail。
- **gate.data** = 现 `/api/v1/gate/paper`。
- **rejects.data** = 现 `/api/v1/risk/rejects`(滚动窗;每次发全量小数组即可,不必 delta)。

### 3.3 snapshot / delta 节奏
- 连上:每通道发 1 个 `snapshot`(全量关键帧)。
- 之后每 `tick_ms`(建议 1000ms):各通道发 `delta`(只发变化)。grid 在 paper 薄盘下每 tick 变化的盘口很少 → delta 极小。
- 每 `keyframe_ms`(建议 30000ms):重发一次全量 `snapshot`(自愈:防 delta 累积漂移/前端漏帧)。
- **前端漏帧/重连**:浏览器 `EventSource` 自动重连并带 `Last-Event-ID`。服务端策略从简:**重连一律重发全量 snapshot**(不重放历史 delta),seq 继续递增。鲁棒、无状态回放包袱。

---

## 4. v2 扩展点(★ 现在不做,但协议已容纳,加它不返工)

**per-condition 全档 focus 订阅**(让展开的盘口也推、彻底零轮询):
- 副信道告知服务端"我在看哪些盘口":`POST /api/v1/stream/focus {stream_id, conditions:[cid,...]}`(SSE 单向,订阅意图走副 POST)。
- 服务端在该连接的流里增发 `book` / `quote` 通道(全档 BinaryMarketBookView / QuoteParams),只针对 focus 的 cid。
- **因为信封是"通用 + 命名通道",加 `book`/`quote` 两个通道 = 纯增量,协议不升版、前端老代码忽略新通道不报错。** 这就是 v1 设计要做对的地方。
- v1 阶段:展开行继续走 REST(已有优先级插队,即时),不阻塞。

---

## 5. 服务端架构(cpp-httplib,R-12 合规)

- 新文件 `endpoint_stream.cpp` + `register_stream(svr, hs)`,`svr.Get("/api/v1/stream", ...)`。
- 用 `res.set_chunked_content_provider("text/event-stream", provider)`。provider 闭包:
  ```
  循环每 tick: 读 StateProvider 各 const 快照 → 算 delta → 写 SSE 帧到 DataSink;
  sink.write() 返回 false(客户端断开)→ 退出循环、释放线程;
  周期 keyframe; 周期 heartbeat。
  ```
- **线程模型**:cpp-httplib 是 thread-per-connection 阻塞模型 → 每个 SSE 连接占 1 个线程,整条连接生命周期内驻留。操盘手 1-2 人 → 1-2 线程,可忽略。**必须设上限**(`max_sse_clients=8`)。**超上限不要裸 503**(浏览器 EventSource 会 3s 快速重连风暴)→ 写一帧 `retry: 30000` 再关连接,让浏览器 30s 退避重连(老王评审)。
- **★ 死连接 / flush(老王评审,必改):**
  1. `svr.set_write_timeout(3, 0)` —— 跨洋 TCP window 填满时 `sink.write()` 会阻塞而非立即返 false;无 write timeout 则**连接早死、线程还挂在 write**,吃满 8 线程上限。设 3s write timeout,超时即返 false → provider 退出释放线程。
  2. `svr.set_tcp_nodelay(true)` —— 否则 padding/每帧可能被 Nagle 合并缓冲,逐帧 flush 与防缓冲 padding 失效。开 TCP_NODELAY 后初始 padding 可从 2KB 缩到 **256B**。
  3. tick 不用裸 `sleep_for(1s)`,用**带超时的 condvar wait** —— 服务端 shutdown 时能立即退出 provider,不必等满一个 tick。
- **R-12 合规**:provider 在 debug_api 线程跑,只读 const StateProvider 快照(atomic/double-buffer),**绝不触热路径、不持热路径锁、不调 RM/signer/WSS**。与现有端点同一安全模型。tick 的 sleep 在本连接线程内,不阻塞他人。
- **delta 状态**:每连接本地保存"上次发送的 per-cid grid 摘要 / per-event score"(线程局部,~350+17 条),与上次对比出 changed/removed。内存可忽略。
- **as_of_ts**:每帧带服务端快照 epoch_ns(R-20)。

---

## 6. 前端集成(`store.ts` / `api.ts`)

- `api.ts`:`new EventSource(`${base}/api/v1/stream`)`,`addEventListener(channel, ...)` 逐通道更新 store。
- snapshot → 整段替换对应 state;delta → 合并(grid 按 cid merge,removed 删)。
- **替换** initPolling 里 status/account/grid/scores/events/positions/pnl/gate/rejects 这 9 个轮询 → 全部由 SSE 喂。
- **保留 REST**:book_pair/quote/market(展开,优先级插队)、metrics/features/mapping(Ops 页)、可选 timeseries。并发闸(6)仍管这些按需 REST。
- **★ 回退机制(鲁棒性,必须有)**:`EventSource.onerror` 连续失败 / 连不上 N 秒 → **自动回退到现有轮询模式**(轮询代码不删,作为 fallback)。某些企业代理会缓冲/掐 text/event-stream;回退保证永不比现在差。**回退的轮询与 SSE 读同一后端 StateProvider 快照,前端两条通路写同一份 store、不得各自加工**(老郭评审:防双源分叉)。
- **staleness**:超过 ~3×tick 没收到 heartbeat → 顶栏标"连接可能中断 / 重连中"。

---

## 7. 跨洋 / 中间节点注意

- SSE 长连接连上后,推送只付**单向延迟(~100ms)**,无往返 —— 这是相对 2s 轮询(每次 200ms RTT)的实质提升,且服务端可 1s 推、看板秒级跳动。
- 防缓冲:`X-Accel-Buffering: no` + 连接初 2KB padding + 10s heartbeat。
- 将来上反代(h2/TLS):SSE 在 h2 上照跑,且 h2 多路复用顺带解决"SSE 占 1 条连接、按需 REST 还要连接"的并存(锦上添花,非必需)。

---

## 8. 分期与验收

**Phase 1(本设计落地范围):**
- 后端 `/api/v1/stream`:hello + 9 数据通道 snapshot/delta + heartbeat,连接数上限,R-12 合规。
- 前端:EventSource 接入 9 通道 + 轮询回退;展开行/Ops 维持 REST。
- 验收:看板秒级跳动;稳态前端→服务端**主动请求≈0**(仅 1 条 SSE 长连 + 偶发展开 REST);断连自动重连/回退;0 超时;服务器新增负载可忽略(1-2 线程 + 秒级快照读)。

**Phase 2(需要时):**
- focus 订阅 → book/quote 通道,展开盘口也推。
- metrics/features/mapping 上 SSE 通道。

---

## 10. 评审修订(老王 网络 + 老郭 架构,2026-06-02)— 已纳入,实施按此

轻评审一轮(**风控不参与,老板指示**),两位结论均"改了再批",全部为 v1 加字段/配置、不动架构。已纳入上文:

**网络(老王)必改:**
- ✅ `set_write_timeout(3,0)`(防死连接占线程)、`set_tcp_nodelay(true)`(防缓冲)、tick 用 condvar wait(秒退)。(§5)
- ✅ 超连接上限发 `retry:30000` 帧再关,非裸 503。(§5)
- ✅ 取消 `?since` 歧义参数;hello 带 `replay:"none"`、`compress:[]` 预留。(§3 / §3.1)

**架构(老郭)必改(都是 v1 加字段,不破坏 v2):**
- ✅ 信封 `as_of_ts` 钉死为"仅 transport 时刻",数据新鲜度用 data 内 `event_ts`/`data_source_ts`(上游 ts);前端 staleness 必读 event_ts,**不读信封 as_of_ts**(修 R-20 固化风险)。(§3)
- ✅ `seq` 改 per-server 单调,仅作去重/乱序/漏帧检测,不承诺跨重连连续、不回放。(§3)
- ✅ 信封 `kind`→`mode: snapshot|delta|append`,每通道 delta 语义在 hello.channels 逐个声明(rejects=full 滚动窗;timeseries=append)。(§3 / §3.1)
- ✅ hello 加 `stream_id`(v2 focus 副 POST 回传用,纯加性预留)。(§3.1)
- ✅ §6 明确:轮询 fallback 与 SSE **读同一 StateProvider 快照,不得各自加工**(防双源分叉)。

> 注:老郭附议"再过老韩(R-12)一轮"—— 按老板指示**风控不参与**,R-12 只读约束由 GM 自检(SSE provider 只读 const 快照,与现有端点同一安全模型)。

---

## 9. 待拍板项(请老板/评审定)

1. **Phase 1 范围**认不认可(9 通道全进 SSE,detail/Ops 留 REST)?
2. **tick 节奏**:1s 推一次够不够?(更快=更"活"但服务端读更频;1s 对人工盯盘足够)
3. 要不要先过一轮**网络(老王)+架构(老郭)+风控(老韩)评审**再动手?(我倾向过,因为这是新接口契约,扣 R-12 + 线程上限 + 回退)
