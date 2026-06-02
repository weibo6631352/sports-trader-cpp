# 会议纪要 — CLOB 订单簿 WSS 断开不重连

**owner:** 老雷 (GM)
**last_review:** 2026-06-02
**召集:** 老板(发现 clob WSS 断了)→ 指示"开会讨论, 不要直接做"
**参与:** 老王(网络/WSS)/ 老郭(架构)/ 老李(Polymarket 协议)。**风控不参与**([[no-risk-review-for-frontend]])。
**结论:** 真因极可能是**缺客户端心跳 → 服务端 idle 超时踢连**;且**断开后无重连**(回调只打印)。修法三方共识: **复用 PolymarketCLOBSubscriber 的心跳+重连编排**(已实现), transport 仍用 LiveWssTransport, 配套堵 4 个 P0 缺口。**待老板拍板后再实施。**

---

## 1. 现象(生产实测)
paper_server(伦敦 EC2)CLOB 订单簿 WSS 连上正常收 book ~10 分钟后:
```
WSS CONNECTED, 订阅 392 tokens → books_published=340
周期重发现: 市场集变化 → 198 market 重订 (WSS 全量重订)
WSS DISCONNECTED: recv_loop_ended     ← 服务端关了连接
周期重发现: 199 market 重订            ← 之后永不重连
```
`wss_connected.clob` 此后恒 false 到手动重启 → 订单簿全空, 交易/看板瞎。

## 2. 真因(三方收敛)
- **首因(老李, 协议权威): 缺客户端心跳 → idle 超时。** Polymarket CLOB market channel **服务端不主动发 ping**;客户端须 ~10-15s 无消息时主动发 ping, 30s 无任何 frame 服务端单方关连接。体育盘口行情稀疏, 10 分钟内极易静默 >30s → 被踢。现象("正常收 ~10min 后服务端关")完全吻合 idle 超时特征。
- **直接放大(老王/老郭): 断开后无重连。** paper_daemon 用简单版 `LiveWssTransport`, 其 `SetOnDisconnected` 回调(daemon.cpp:695)**只 fprintf, 不重连**。
- **次因(待实测): "全量重订"用旧格式 `{"type":"Market","assets_ids":[...]}` 在同连接二次发送, 语义(追加 vs 替换)官方无明文保证, 可能触发服务端资源重置/关连接。新格式 `{"assets_ids":[...],"operation":"subscribe"}` 才明确。

**定位手段(实施前先做):** ① 在 recv 循环打出 WSS Close frame 的 close code(1000 idle / 1008-1009 too big / -1 TCP 断)+ reason;② 查 `heartbeat_pings_sent_total` 是否为 0(为 0 = 心跳定时器根本没跑 = 断因坐实)。

## 3. 修法(三方共识)
**选型: 弃简单版的"裸 transport 无编排", 复用 `PolymarketCLOBSubscriber` 的心跳+重连编排层**(已实现 ScheduleReconnect/退避/CheckMarketHeartbeat), **transport 仍用 LiveWssTransport**(TLS/帧/白名单/R-12 都干净, 不重写网络栈)。不给简单版重抄第三套。

**重连后三步序(老李, 严格):** 重发 subscribe → 等每 token 的 book snapshot 到 → 才接受 price_change diff(重连后首 snapshot p50 ~2.2s, 期间 diff 丢弃)。WSS 订阅会自动推全量 snapshot, REST `/books` re-seed 是 gap 兜底(老王建议保留兜底, 老李指出非必需)。

## 4. 必堵的 P0 架构缺口(比重连本身更危险 — 老郭)
1. **自 join 死锁:** `on_disconnected_` 跑在 io_thread_ 内, 直接 `AsyncConnect`(内含 `io_thread_.join()`)= 自 join 死锁。**迁移前必须确认 subscriber 的 ScheduleReconnect 是投递到独立线程/定时器, 不在 io_thread_ 内同步重连**, 否则换实现照样死锁。
2. **心跳定时器必须真在跑:** 确认/接好客户端 ping(~10s)+ 30s 无 frame 主动 Close→重连(回调管显式断, 看门狗管"半死连接"静默死, 缺一不可)。
3. **重连订阅源 = 当前 token 集单一真相, 非固化 config:** subscriber 默认重发 `cfg_.initial_market_token_ids`(连接时固化), 而 daemon 真相是被 RediscoverOnce 改写的 `all_token_ids_`。直接迁移 → 重连后订阅开机死盘集、丢掉新 live 盘。须接 RCU 快照。
4. **data race + republish 唯一入口:** `all_token_ids_` 被映射刷新线程写 / io_thread 读, 无锁 → RCU(`atomic<shared_ptr<const vector>>`)原子换(同 catalog 已用模式);"全量重订"与重连两条 republish 路径须串行到映射刷新线程的**唯一入口**, 禁双路径打架。配合 hub book 打 staleness ts, 重订后旧 token 自然过期。

## 5. R-12 合规
看门狗 jthread 只读 atomic ts + 调 Close()(信号), 不碰 on_text_frame_ 热路径、不持锁 >100us。合规。

## 6. 待老板拍板
- 方案(复用 subscriber 编排 + 4 P0 缺口)认不认可?
- 实施前先做定位(close code + heartbeat metric)确认真因, 还是直接按"补心跳+重连"做?
- 临时缓解: 要不要先加个**最小重连看门狗**(独立 jthread 轮询 IsConnected→退避 AsyncConnect+重订)止血, 完整迁移 subscriber 编排作为正式方案随后跟? (止血 < 半天, 正式方案工作量大)
