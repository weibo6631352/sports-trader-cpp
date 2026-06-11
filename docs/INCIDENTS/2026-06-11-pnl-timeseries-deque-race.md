# INCIDENT: paper_server 反复 segfault — /pnl/timeseries 并发读 deque 数据竞态

owner: 老雷 (GM) | last_review: 2026-06-11 | 状态: 已根治 (f496a48d)

## 时间线
- 2026-06-10 14:53 UTC: segfault (paper_server, ip 0x461afb) — 当时归因不明
- 2026-06-10 晚: 「7080 不监听但 daemon 活着」事件 — 当时疑 HTTP 服务脆弱
- 2026-06-11 04:16 UTC: segfault at 0 (ip 0x41f68c), coredump 10.7M 捕获

## 根因 (coredump gdb bt 实证)
bt#0 = PaperDaemon::Build() lambda#4 (pnl_timeseries) → RealStateProvider::pnl_timeseries
→ httplib worker 线程。PortfolioMetrics::equity_snapshot() (HTTP 线程, 前端净值曲线轮询)
**无锁遍历 std::deque**, 同时 paper loop 线程 RecordEquity() push_back/pop_front →
deque 块表重分配 → 悬空指针 → SIGSEGV。注释自称「单 writer/读时拷贝」安全 — 错: 读时拷贝
不原子, deque 无 SWMR 保证。运行越久 deque 越大 (cap 100k) 重分配越频繁 → 跑数小时后炸,
与三次事故时间分布吻合。

## 修复
PortfolioMetrics 全方法挂 std::mutex (RecordEquity/current_drawdown/report/sample_count/
max_drawdown/equity_snapshot)。paper loop ~1Hz tick 非热路径, 锁 μs 级; R-12 红线 (WSS loop
锁>100us=P0) 不适用此处。mutex 删拷贝 → test MakeCurve 改 unique_ptr。

## 教训
1. 「单 writer + 读时拷贝」≠ 线程安全 — 拷贝过程本身要同步; deque/vector 跨线程裸读 = 定时炸弹
2. coredumpctl 是利器: systemd-coredump 默认开, 一条 gdb bt 直接定位 — 以后先查 core 再猜
3. 观测端点 (前端轮询) 与决策线程共享的每一个容器都要过线程安全审计 — 派 backlog: 全 debug_api
   provider lambda 审计 (fills_ring_ 有 mutex ✓, ledger/quote hub SWMR ✓, 其余逐个查)

## 附录: 同日第二起 P0 — Restore 误插 RediscoverOnce (持久化上线日两课)

- 07:42 UTC segfault + 净 PnL 假飙 +$7,414,611 (老板发现)。core thread-apply-all-bt 抓现行:
  Thread31 RestoreLedgerSnapshot(sscanf) × Thread1 SaveLedgerSnapshot 并发。
- 根因: 接线脚本 replace(...,1) 把 Restore 插进 RediscoverOnce (300s 周期/映射线程) 而非 Build
  装配段 → 运行中反复重放快照 = 持仓/累计叠加 (+$7.4M) + 并发竞态崩溃。
- 修 (191398a2): Restore 移回 Build (Start 前单线程, 唯一合法调用点, 注释立规); 污染快照清除。
- 教训: ① 脚本化 replace 接线必须人工核对落点函数 (两处同名调用点 = 高危) ② 新状态机加「唯一
  调用点」注释立规 ③ 监控失职: 崩溃 15min 后老板先发现 → 已挂 60s 存活监控 (本地, 报警不自动拉起
  — 老板 2026-06-11 确认「不要自动拉起, 挂了就查根源」)
