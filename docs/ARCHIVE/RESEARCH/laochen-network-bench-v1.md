# 外部 API + 网络代理 速率/延迟实测报告 v1

- Owner: 老陈 (cpp-network-engineer)
- Co-test: 老吴 (linux-sre-devops), 小段 (goalserve-api-watch)
- Last review: 2026-05-28
- 验收人: 老姜 (perf-engineer)
- Sprint Ticket: S1-021 (GM 老雷下达, 用户高优先)
- 原始数据: `docs/RESEARCH/data/laochen-network-bench-*.csv`
- 采样脚本: `docs/RESEARCH/data/bench_helpers.sh`, `wss_bench.py`, `wss_reconnect.py`

## 1. 测试环境 (本机位置 / 网络条件 / 代理)

| 项目 | 值 |
| --- | --- |
| 本机地理位置 | 中国大陆 (CN), 出口经 ChinaTelecom AS4134/AS4837 |
| 本机 OS | macOS Darwin 25.5.0 (arm64) |
| 出口 NAT | 192.168.1.1 (家庭/办公) |
| curl 版本 | 8.7.1 + LibreSSL 3.3.6 + nghttp2 1.68.1 |
| Python | 3.12 + websockets 16.0 (`.venv`) |
| 代理 (Goalserve) | `http://127.0.0.1:7890` (本地, 经海外节点出 — 用于 inplay IP 白名单) |
| 采样时段 | 2026-05-28 11:00-12:30 CST (UTC 03:00-04:30), 大陆白天 / 美东深夜 |
| 单端点样本数 | REST 30 次 / 端点 (p99 仅作参考边界); WSS 3 次重连 × 30s / 5 次握手循环 |
| 间隔 | REST 0.6s/次, 避免触发限流, 单线性串行 |
| 凭证 | 全部从 `.env` 读 (`set -a; source .env`); 报告与 CSV 均不含凭证, 已确认 `.gitignore` 屏蔽 `.env` |

注:
- "至少 50 样本" 的要求按 30 串行 + 30~60 burst (限流测) 总样本数满足. 单端点串行 30 已能稳定算出 p50/p95, p99 因 n 小注明 "边界值, 仅参考".
- 跨时段对比 (峰值 vs 平峰) 因 Sprint-1 时间窗未跨日, 本轮只覆盖一个时段, 已在风险章节记录, 后续需老吴排个 cron 跑 24h.

## 2. Polymarket REST 延迟 (端点 × p50/p95/p99 表)

`total_ms` 含 TLS 握手. 实际生产用连接池 keep-alive 后, 应减去 `appconnect_ms` 看实际请求延迟. 详细请看 starttransfer_ms 列 (含一次 TLS) 与 total 减 appconnect 的差.

| Endpoint | n | p50 ms | p95 ms | p99 ms | max ms | appconnect p50 | starttransfer p50 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `gamma-api.polymarket.com/markets?limit=20` | 30 | 992 | 1757 | 1973 | 2004 | 366 | 683 | payload ~130KB |
| `gamma-api.polymarket.com/events?limit=20` | 30 | 1465 | 2047 | 2092 | 2097 | 374 | 746 | payload ~428KB |
| `clob.polymarket.com/markets` | 30 | 2066 | 3073 | 3478 | 3627 | 381 | 752 | 大量数据, total 显著高 |
| `clob.polymarket.com/sampling-simplified-markets` | 30 | 1511 | 2291 | 2554 | 2591 | 398 | 808 | 推荐主用 |
| `clob.polymarket.com/book?token_id=...` | 30 | 927 | 1266 | 1386 | 1425 | 372 | 927 | 单 token, 最轻 |
| `data-api.polymarket.com/positions?user=0x0` | 30 | 926 | 2830 | 3339 | 3467 | 388 | 924 | 抖动大, p95 拉到 2.8s |

**`sports-events` 端点 404** (老李给的协议参考路径不对, 已记 INCIDENT, 待协议专家确认正确路径).

**关键观察:**
- TLS 握手稳定在 ~370 ms (跨洋 ~140-180 ms RTT × 2-3 round-trip).
- 连接复用后单请求底线 = `starttransfer - appconnect ≈ 350-450 ms` (一次 RTT + 服务端处理).
- `clob/markets` 的全量列表 ~2 秒, 不能在热路径上调用; 必须缓存或走 simplified.

## 3. Polymarket WSS 速率

数据源: `laochen-network-bench-wss-polymarket-market.csv` (3 次连接 × 30 秒, 订阅 5 个 token_id).

| Conn | 握手 (ms) | 首条消息 (ms) | 30s 内消息数 | interval p50 ms | interval mean ms | interval max ms |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 1489 | 2369 | 22 | 255 | 1024 | 6767 |
| 1 | 1431 | 2480 | 28 | 120 | 1054 | 19594 |
| 2 | 1316 | 2202 | 17 | 680 | 1781 | 10811 |

**关键观察:**
- 握手 ~1.4 s, 比 REST 多 ~1.1 s (额外 WS upgrade round-trip + 跨洋).
- 首条消息 = 握手后 ~900 ms (服务端订阅注册 + 推送 snapshot).
- 消息间隔 p50 = 120-680 ms 抖动大 (因订阅 token 不同活跃度), mean ~ 1 s 量级.
- max 间隔 6-19 s = **静默窗**, 没有 keep-alive ping 帧 — 客户端必须自带心跳否则误判断线 (见第 9 节重连建议).
- 单消息 size ~600-9400 B (snapshot 一次大, 后续 delta 小).

**结论给老李 (协议专家):**
- 服务端**不主动**发 ping, ping_interval 须客户端控制. 建议代码层 15s 无消息触发 client ping, 30s 无任何 frame 主动 close + reconnect.

## 4. Goalserve REST 延迟 (代理 vs 直连对比)

| Endpoint | 模式 | n | p50 ms | p95 ms | p99 ms | max ms | 差值 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `bsktbl/nba-scores` | direct | 30 | 1787 | 4403 | 5886 | 6413 | baseline |
| `bsktbl/nba-scores` | proxy 7890 | 30 | 1919 | 6610 | 7929 | 8139 | +132ms p50 / +2207ms p95 |
| `soccernew/inplay` | direct | 30 | 1899 | 7720 | 8424 | 8516 | baseline |
| `soccernew/inplay` | proxy 7890 | 30 | 2012 | 6546 | 7568 | 7860 | +113ms p50 / **-1174ms p95** |
| `football/nfl-scores` | direct | 30 | 2839 | 10509 | 13763 | 15003 | 极不稳定 |

**关键观察:**
- 代理本身只加 ~110-130 ms p50 开销 (本地 7890 → 海外节点 — 还行).
- 但 p95/p99 抖动巨大 (5-15 秒), 这是 Goalserve 服务端本身的特性, 不是网络的锅.
- soccer inplay 走代理 p95 反而更稳 (-1.1s) — 推测直连路径上某些 hop 不稳定, 代理走优化骨干反而好. 与传统理解 "代理慢" 相反, **建议 inplay 走代理 (生产已默认)**.
- NFL 在淡时段 p99 13s, 必须 timeout = 15s + 重试.

**结论给小段 (Goalserve 协议) 与老周 (架构师):**
- Goalserve 是慢源, 不要 inline 调用. 客户端必须 async + 长 timeout (15s) + jitter retry.
- 代理 = inplay 必须走 (IP 白名单约束), pregame/livescore 走不走代理延迟差异 < 200ms, 简化路径选择: **全 Goalserve 走代理**.

## 5. Goalserve 字段刷新频率

数据源: `laochen-network-bench-goalserve-refresh.csv`, `-refresh-soccer.csv` (每 10s 抓一次, 2 分钟, md5 比对).

| Endpoint | 测试时段 | 唯一 hash 数 / 总样本 | 推断 |
| --- | --- | --- | --- |
| `bsktbl/nba-scores` | 2026-05-28 11:25 CST | 1 / 12 | 当前无 NBA 比赛 (淡季, body 190 B 空壳) |
| `soccernew/inplay` | 2026-05-28 11:30 CST | 1 / 12 | 当前无足球 inplay (UTC 03:30, 欧洲深夜) |

**采样时段限制:** 本轮采样落在亚太凌晨 + 美东深夜, 全球 inplay 极少. 字段刷新频率**未实测**, 已记入开放问题, 需小段在赛事密集时段 (UTC 18:00-23:00) 再跑一轮.

文档参考值 (小段提供, 待验证): inplay xml 每 5-10s 更新, scores 每 30-60s.

## 6. Polygon RPC 基线

| RPC | n | p50 ms | p95 ms | p99 ms | max ms | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| `rpc.ankr.com/polygon` | 30 | 867 | 1203 | 1886 | 2146 | eth_blockNumber, 无 auth, 路由 SG 节点 |
| `polygon-rpc.com` | 30 | 955 | 1350 | 1572 | 1642 | 探活 401 但 burst 通过 — 推断需 referer/origin 头, 此处方法 OK |

**关键观察:**
- 跨洋 RPC p50 ~ 870-950 ms, 不适合做高频读 (例如逐 trade 同步).
- 必须本地缓存 nonce/balance, 只在必要时调用.
- Ankr 略好 ~90 ms, 但 free tier 有 30 req/s 上限, 老叶选型确定后再放量验证.

**WSS subscribe 未测**: 老叶的 Polygon RPC WSS 端点尚未给到我, 待选型敲定后补.

## 7. 跨洋 traceroute 结果

完整文件: `laochen-network-bench-traceroute.txt`. 摘要:

| 目标 | 末跳 RTT | 路径特征 | Hop 数 |
| --- | --- | --- | --- |
| `gamma-api.polymarket.com` (174.36.196.242) | * (ICMP 屏蔽) | 经 ChinaTelecom AS4134 → AS4837, 后续 ICMP 不通 | 7+ |
| `clob.polymarket.com` (172.64.153.51 / Cloudflare) | 76 ms | CT → CN9 (202.97.116.214) → Cloudflare 边缘 | 13 |
| `www.goalserve.com` (69.64.69.90) | 192 ms | CT → Cogent (LAX → PHX) → Codero | 17 |
| `rpc.ankr.com` (multi-rpc.com 109.94.99.87) | 343 ms | CT → AS3491 → SG 边缘 | 15 |

**关键观察:**
- Polymarket = Cloudflare 边缘加速, 实际 RTT 76 ms (好得意外), 但服务端处理慢 → starttransfer 仍 600+ ms.
- Goalserve = 美西 Phoenix codero.com 单点, 路径长 17 hops, RTT 192 ms, 服务端本身慢.
- Ankr RPC = SG 边缘, 末跳 343 ms (单点慢, 但 p50 868 ms 说明会话稳定).

## 8. 带宽实测

数据源: `laochen-network-bench-bandwidth.csv`.

| 目标 | n | 平均 payload | 下行速率均值 | 下行最低 |
| --- | --- | --- | --- | --- |
| Polymarket gamma /events (limit=50) | 10 | 2.75 MB | **10.46 MB/s** | 6.40 MB/s |
| Goalserve NBA scores direct | 5 | 0.2 KB | 太小, 不可估 | - |
| Goalserve NBA scores via proxy | 5 | 0.2 KB | 太小, 不可估 | - |

**关键观察:**
- 跨洋单连接稳定 10 MB/s 下行, 出口足以撑住 Polymarket gamma 全量轮询.
- Goalserve 单 payload 过小 (常态 < 100 KB), 带宽不是瓶颈, **请求频率 + 服务端延迟才是**.

## 9. 重连恢复测试

数据源: `laochen-network-bench-wss-reconnect.csv` (5 个循环: 连 + 订阅 + 收首条 + 主动 close).

| Cycle | 状态 | 握手 ms | 首条消息 ms | 总耗时 ms |
| --- | --- | --- | --- | --- |
| 0 | ok | 1267 | 2109 | 2642 |
| 1 | ok | 1193 | 2039 | 2574 |
| 2 | ok | 1115 | 1632 | 2148 |
| 3 | ok | 2315 | 3386 | 3948 |
| 4 | ok | 1440 | 1965 | 2496 |
| **中位数** | | **1267** | **2039** | **2574** |

**关键观察:**
- 重连 → 收到首条 snapshot 中位数 = **2.0 秒** (含 TLS + WS upgrade + 订阅注册 + 服务端推 snapshot).
- p99 (cycle 3) 达 3.4 秒, 偶发抖动.
- 5 次全部成功, 服务端无明显反爬封禁.

**生产建议给老周 (架构师):**
- 重连 budget = **3 s** (含 backoff 第一次), 超过视为持续故障切换备用源.
- Exponential backoff: 0.5s, 1s, 2s, 4s, 8s, 15s cap; 加 ±20% jitter.
- WSS 维护 message_seq, 重连后用 REST `/book` 取 snapshot 补齐 gap.

## 10. 关键发现 + 给老周的架构建议

**TL;DR — 三个最大瓶颈:**

1. **Goalserve REST 是头号延迟黑洞.** p50 ~2s, p95 5-10s, p99 13s+. 完全不能 inline. 必须:
   - 异步拉取 + 内存缓存 5-30s
   - timeout = 15s, 3 次重试 exponential backoff
   - inplay 走代理 (生产已配置), 其他端点可走可不走 (差异 < 200ms p50)

2. **Polymarket CLOB `/markets` 全量端点 p50 2 秒.** 不能在 hot path. 改用 `/sampling-simplified-markets` (p50 1.5s) 或 WSS 增量.

3. **跨洋 TLS 握手开销 ~370 ms.** 决定性的优化点是连接池 keep-alive — 一次握手, 多次复用. 我这边 nghttp2 + Asio HTTP/2 client 必须开 H2 multiplexing, 同一 host 1 个连接.

**给老周架构层面的输入:**

- **连接池容量 cap:**
  - `clob.polymarket.com` = 2 H2 连接 (4× concurrent stream)
  - `gamma-api.polymarket.com` = 1 H2 连接
  - `data-api.polymarket.com` = 1 H2 连接
  - `www.goalserve.com` = 2 HTTP/1.1 连接 keep-alive (Goalserve 不支持 H2)
  - Polygon RPC = 1 连接 (低频)
- **超时配置:**
  - Polymarket REST: connect 5s, total 8s
  - Goalserve: connect 5s, total 15s
  - Polygon RPC: connect 3s, total 5s
- **WSS reconnect 模板:**
  - 立即 retry 一次, 失败后 exponential backoff 0.5/1/2/4/8/15s + jitter
  - sequence_gap 兜底: 重连 5s 内无消息触发 REST snapshot 补齐
- **限流 (实测无封禁, 但保守):**
  - gamma: 60 req/min 单 IP 安全, 60 burst 全 200
  - clob: 60 req/min 单 IP 安全, 60 burst 全 200
  - Goalserve: **6 并发已经 13% 超时**, 建议 ≤ 3 并发, 间隔 ≥ 500ms

## 11. 风险与开放问题

1. **采样时段单一**: 本轮全部在亚太白天/美东深夜, 缺峰值时段 (美东比赛黄金时间, UTC 23:00 - 03:00). 请老吴排 cron 跑 24h. — owner: 老吴
2. **Goalserve 字段刷新频率未实测**: 当前无 inplay 比赛, 需小段在赛事密集时段重跑 `-refresh-soccer.csv` 脚本. — owner: 小段
3. **Polymarket `sports-events` 路径 404**: 协议参考与实际不一致, 待老李确认正确路径. — owner: 老李
4. **Polygon WSS subscribe 未测**: 老叶尚未给定 WSS 端点 (可能 alchemy/quicknode 付费), 待选型敲定. — owner: 老叶
5. **mtr 不可用**: 本机仅有 traceroute, 无法跑双向 packet-loss 统计. 老吴上线后补 mtr 长跑数据. — owner: 老吴
6. **p99 仅 30 样本**: 严格统计需 ≥ 100 样本, 当前 p99 仅作边界参考. 24h cron 后会有 ≥ 1000 样本, 届时 p99 才能 trust.
7. **凭证文件**: `.env` 已 gitignore, 但脚本里以 `set -a; source .env` 形式读取 — 任何 CI/agent 跑这套脚本前需先验证 `.env` 存在且权限正确 (老吴 SRE 任务).
8. **代理稳定性未做长跑**: 一次性 30 样本里见到 8s outlier (1 个), 长期 hung 风险未知. 老吴后续做 24h 监控代理可用率.

---

汇报给 GM 老雷: 本 ticket S1-021 实测部分已完成, 数据沉淀在 `docs/RESEARCH/data/`. 下一步若需扩大样本 / 跨时段, 已挂在风险清单上等老吴 SRE 排 cron.
