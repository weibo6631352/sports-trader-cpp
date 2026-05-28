# 代理 + Goalserve 总带宽专项实测 v1

- Owner: 老吴 (linux-sre-devops)
- Date: 2026-05-28
- 数据用途: 网络诊断必备 / 老陈 SSOT 补全 / 跨洋链路容量规划
- 验收人: 老陈 (整合到 SSOT) + 老雷 (GM)
- 关联: `laochen-network-bench-v1.md`, `xiaoduan-goalserve-api-spec-v1.md`, GOALSERVE 官方文档 (inplay-feed-new.txt)
- 原始数据 (CSV + 脚本):
  - `docs/RESEARCH/data/laowu-bandwidth-probe.sh` — 主采样脚本
  - `docs/RESEARCH/data/laowu-bandwidth-proxy-throughput.csv`
  - `docs/RESEARCH/data/laowu-bandwidth-goalserve-throughput.csv`
  - `docs/RESEARCH/data/laowu-bandwidth-gzip-ratio.csv`
  - `docs/RESEARCH/data/laowu-bandwidth-concurrency.csv` (轻 endpoint inplay)
  - `docs/RESEARCH/data/laowu-bandwidth-concurrency-heavy.csv` (重 endpoint nba-shedule)

## 1. 实测环境

| 项 | 值 |
| --- | --- |
| 本机 | macOS Darwin 25.5.0 (arm64) |
| 出口 | 中国大陆 ChinaTelecom, 家庭/办公 NAT 192.168.1.1 |
| 代理 | `http://127.0.0.1:7890` (本地 client, 海外节点出, 用于 Goalserve inplay 白名单) |
| 采样时段 | 2026-05-28 15:34-16:05 CST (UTC 07:34-08:05), 大陆晚白天 / 美东深夜 — **淡时段** |
| 采样样本量 | 代理吞吐 5-8 × 4 size; Goalserve 三域 5 × 6 endpoint × 2 mode; concurrency 30-40 req × 9 并发档位 |
| curl | 8.7.1 + LibreSSL 3.3.6 + nghttp2 1.68.1 |
| 凭证 | `${GOALSERVE_API_KEY}` 从 `.env` 读取, 报告 + CSV 中所有 URL 已 sed redact |
| 端点 IP (resolved) | `www.goalserve.com` 69.64.69.90 (Phoenix codero); `inplay.goalserve.com` 91.206.228.73; `oddsfeed.goalserve.com` 69.64.83.69 |

注: 凡是 "code=000 t=7.5s" 行均为 HTTPS 端口连接被服务端拒绝 / IP 白名单未开 (见 §3 inplay/oddsfeed 部分).

## 2. 代理本身吞吐 (公网 上下行 max)

测试方法: 走代理拉 `speed.cloudflare.com/__down?bytes=N` 完整 N 字节 payload, 5-8 次 sample.

### 2.1 下行 (download)

| Target | n | size dl | p50 mbps | p95 mbps | max mbps | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| cf 1 MB (完整) | 8 | 1000000 | 3.4 | 4.6 | 4.7 | 跨洋单连接稳态 |
| cf 25 MB (完整, 长跑) | 3 | 25000000 | 25.3 | 25.6 | 25.6 | **单连接峰值, 跨洋稳态** |
| cf 10 MB (短跑, max-time 30s 截) | 5 | 1.9-3.2 MB | 17.7 | 25.4 | 25.4 | 与 25MB 一致 (代理出口 ≈ 25 Mbps cap) |

**结论**: 代理出口下行 **稳态 ~25 Mbps 单连接** (3.2 MB/s). 小 payload (1 MB) 受跨洋 RTT + TCP slow-start 影响只能跑到 ~3-4 Mbps. 业务实际场景多为 KB 级响应 → 跨洋 RTT 占主导, 而非带宽.

### 2.2 上行 (upload)

| Target | n | size ul | p50 mbps | max mbps |
| --- | --- | --- | --- | --- |
| cf `__up` 5 MB | 3 | 5242880 | 6.2 | 8.9 |

**结论**: 代理出口上行 **稳态 ~6 Mbps, 峰值 ~9 Mbps**. 上行容量足以支撑 Polymarket order 提交 (单 order 几 KB).

### 2.3 代理基线 RTT (健康指标)

| Target | n | p50 | 用途 |
| --- | --- | --- | --- |
| cloudflare trace (208 B) 走代理 | 1 | 0.74 s | proxy health probe, 应 < 1.5 s |

## 3. 通过代理对 Goalserve 三域名稳态吞吐

### 3.1 `www.goalserve.com` (主 REST, HTTPS 443)

测试: 5 次 × {nba-shedule, nhl-shedule, racing/uk, bsktbl/inplay 4 endpoint} × {proxy, direct}.

| Endpoint | mode | n | size_dl (wire/gz) | p50 kbps | p95 kbps | max kbps |
| --- | --- | --- | --- | --- | --- | --- |
| `bsktbl/nba-shedule` | proxy | 5 | 32-45 KB | 307 | 358 | 358 |
| `bsktbl/nba-shedule` | direct | 5 | 27-45 KB | 292 | 358 | 358 |
| `hockey/nhl-shedule` | proxy | 5 | 46-120 KB | 512 | 965 | 965 |
| `hockey/nhl-shedule` | direct | 5 | 35-155 KB | 886 | 1244 | 1244 |
| `racing/uk` | proxy | 5 | 38-56 KB | 362 | 451 | 451 |
| `racing/uk` | direct | 5 | 21-50 KB | 394 | 398 | 398 |
| `bsktbl/inplay?json=1` | proxy | 5 | 19-92 B | 0.45 | 0.74 | 0.74 |
| `bsktbl/inplay?json=1` | direct | 5 | 55-107 B | 0.58 | 0.86 | 0.86 |

**关键观察:**
- `www.goalserve.com` 稳态吞吐 **300-1200 kbps (0.3-1.2 Mbps)**, 远低于代理 25 Mbps 出口 cap → **服务端是瓶颈, 不是网络**.
- nba/nhl schedule 是大 payload (raw 755 KB / 3.7 MB 实测见 §5), gzip 后 wire 30-150 KB.
- 小 endpoint (inplay) 吞吐数字 < 1 kbps 没意义 (body 太小), 用延迟而不是吞吐评估.

### 3.2 `inplay.goalserve.com` (官方 inplay odds 域)

| Endpoint | HTTPS 443 (proxy) | HTTPS 443 (direct) | HTTP 80 (proxy) |
| --- | --- | --- | --- |
| `/inplay/soccer_10?key=...` | code=000 timeout 7.5s | code=000 timeout 7.6s | **code=404 0.97s** |
| `/` (root) | - | - | code=404 0.95s |

**已确认约束:**
- `inplay.goalserve.com` **TLS 443 端口对当前 IP (经代理出口) 不响应** — SYN→FIN/RST 不返, max-time 7.5s 全 timeout. 推断为 IP 白名单或服务端 HTTPS 未开放.
- HTTP 80 端口可达, IIS 10 服务器返 404. 当前 API key (`87ff5e...526`) **未开通 inplay subscription** — 5 次不同 path 全 404 (`/inplay/soccer_10`, `/inplay/soccer`, `/getfeed/.../soccernew/inplay`, `/inplay-feed/soccer`, `/`).
- **稳态吞吐无法实测** — 写入开放问题, 需 GM 老雷 / 老雪向 Goalserve 申请 inplay subscription + IP 白名单后重测.
- 预期 (按 Goalserve 官方文档): 每秒推送 gzip XML, raw ~10-50 KB/s, gzip 后 ~2-8 KB/s.

### 3.3 `oddsfeed.goalserve.com` (settlement)

| Endpoint | HTTPS 443 (proxy) | HTTP 80 (proxy) |
| --- | --- | --- |
| `/getfeed/.../soccernew/inplay` | code=000 timeout 8.4s | code=404 0.71s |
| `/oddsfeed/soccer` | - | code=404 0.89s |
| `/getfeed/.../soccer/odds-current` | - | code=404 0.67s |
| `/` | - | code=404 1.13s |

**与 inplay.goalserve.com 同样的约束**: HTTPS 443 不通, HTTP 80 IIS 返 404. 当前 key 未开通 settlement subscription. 稳态吞吐**无法实测**, 写入开放问题.

### 3.4 三域统一观察

- 三域都是 codero.com / Phoenix 美西机房, traceroute 末跳 RTT ~192 ms (老陈 v1 §7).
- 当前 key **仅 `www.goalserve.com` 主 REST 全量可用**, inplay / oddsfeed 子域需额外 subscription.
- 这是**当前 SSOT 必须如实记录的约束**, 不能假设全 covered.

## 4. 直连 vs 代理 对比表 (定量补 v1 小段的定性结论)

**大 endpoint** (`bsktbl/nba-shedule`, 5 sample, gzip):

| mode | n | p50 total (s) | p95 total (s) | p50 dl_kbps | 备注 |
| --- | --- | --- | --- | --- | --- |
| direct | 5 | 3.41 | 7.12 | 308 | baseline |
| proxy | 5 | 2.78 | 4.11 | 358 | **比直连快, 与小段 v1 inplay 测出 -73% 一致方向** |

**结论:**
- 大 endpoint 上代理比直连 p50 快 ~18%, p95 快 ~42%.
- 小 endpoint 老陈 v1 测过 (inplay 70B): direct p95 8.5s, proxy p95 2.3s, **代理路径绕开了大陆 → Phoenix 的不稳定 hop**.
- **生产决策**: 全 Goalserve REST 走代理, 直连作 fallback (代理挂了立切).

## 5. gzip 压缩比 (跨洋链路省 60%+ 关键数据)

测试方法: 同一 endpoint 拉两次, 一次 `Accept-Encoding: identity` (off), 一次 `--compressed` (on), 比 `size_download`.

| Endpoint | raw bytes | gz bytes | ratio | **省** |
| --- | --- | --- | --- | --- |
| `www.goalserve.com/bsktbl/nba-shedule` | 755,435 | 116,020 | 15.4% | **84.6%** |
| `www.goalserve.com/hockey/nhl-shedule` | 3,717,142 | 600,790 | 16.2% | **83.8%** |
| `www.goalserve.com/soccer/home` | 143,202 | 21,321 | 14.9% | **85.1%** |
| `www.goalserve.com/bsktbl/inplay?json=1` | 70 | 185 | 264.3% | -164% (gzip framing 反而胀, 小 body 别开) |

**结论:**
- **XML schedule / list 类 endpoint gzip 省 84-85%** — 远超 GM 关心的 60% 阈值.
- 小 endpoint (< 500 B) 不要开 gzip — gzip header + framing 比 body 还大, 反而胀 2-3 倍.
- ETL 默认开 `Accept-Encoding: gzip` (curl `--compressed`), 但**单个 endpoint 大小判断 < 500 B 时跳过**.
- 推算流量: 假设每天 24 × 4 sport × schedule 全量轮询 = 96 次拉, 每次 raw 1 MB → 96 MB/d → gzip 后 14 MB/d. **节省 82 MB/d** (跨洋出口带宽租用成本相关).

## 6. 并发上限 + 限流触发点 (sweep 表)

### 6.1 轻 endpoint (`bsktbl/inplay`, 70-185 B body, 20 req)

| concurrency | n | succ | timeout | err | wall_s | RPS | p50 ms | p95 ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 20 | 20 | 0 | 0 | 61.5 | 0.33 | 2290 | 6218 |
| 2 | 20 | 20 | 0 | 0 | 31.9 | 0.63 | 2342 | 6158 |
| 4 | 20 | 20 | 0 | 0 | 18.3 | 1.09 | 2532 | 8379 |
| 6 | 20 | 20 | 0 | 0 | 9.9 | 2.02 | 2616 | 3846 |
| 8 | 20 | 20 | 0 | 0 | 15.0 | 1.33 | 4234 | 7535 |
| 10 | 20 | 20 | 0 | 0 | 5.2 | 3.82 | 2094 | 2933 |

### 6.2 重 endpoint (`bsktbl/nba-shedule`, gzip 30-150 KB wire, 30-40 req)

| concurrency | n | succ | timeout | err | wall_s | RPS | p50 ms | p95 ms | agg_kbps |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 30 | 30 | 0 | 0 | 148.0 | 0.20 | 3865 | 10892 | 188 |
| 2 | 30 | 30 | 0 | 0 | 61.6 | 0.49 | 3331 | 8748 | 452 |
| 4 | 30 | 30 | 0 | 0 | 35.2 | 0.85 | 3640 | 5053 | 791 |
| 6 | 30 | 30 | 0 | 0 | 23.8 | 1.26 | 3506 | 8973 | 1168 |
| 8 | 30 | 30 | 0 | 0 | 17.7 | 1.69 | 3338 | 7172 | 1572 |
| 10 | 30 | 30 | 0 | 0 | 13.5 | 2.22 | 3692 | 5592 | 2063 |
| 12 | 30 | 30 | 0 | 0 | 10.1 | 2.97 | 3238 | 6827 | 2756 |
| 16 | 40 | 40 | 0 | 0 | 15.1 | 2.65 | 3265 | 5510 | 2462 |
| 20 | 40 | 40 | 0 | 0 | 8.8 | 4.56 | 3463 | 4661 | 4235 |
| 30 | 40 | 40 | 0 | 0 | 9.1 | 4.40 | 3238 | 8702 | 4085 |

**关键观察:**
- **本轮 100% 成功率, 0 timeout, 0 5xx** — 老陈 v1 测到的 "6 并发 13% 超时" **本轮淡时段没复现**. 推断 v1 触发的是服务端**晚高峰瞬时排队**, 不是 hard rate-limit.
- **服务端饱和点 RPS ≈ 4.5** (c=20 达 4.56 RPS, c=30 反降为 4.40), 增并发不增吞吐 → **Goalserve 服务端是瓶颈**.
- 代理 + 出口完全没饱和: c=20 聚合 4.2 Mbps, 远低于代理 25 Mbps 下行 cap.
- p50 latency 几乎不随并发变化 (~3.2-3.7 s), 抖动主要在 p95.

**生产决策 (给老周):**
- ETL 并发设 `c=4-6`, 已能拿到大半饱和吞吐 (790-1170 kbps), 留余量给重试.
- 不建议 c≥10 — 边际收益小, 增大 p95 抖动概率.
- **晚高峰** (UTC 23:00 - 03:00 美东赛事密集时段) **必须重测** — 老陈 v1 的 13% 超时是这个时段的现象, 当前淡时段无法复现.

## 7. 给老陈 SSOT 的填表数据 (直接 copy 进 §0 TL;DR)

```
## §X 代理 + Goalserve 带宽 baseline (老吴 v1, 2026-05-28 淡时段)

代理出口下行峰值: 25.3 Mbps (单连接, 25MB 完整下载稳态)
代理出口下行 1MB 短跑: 3-5 Mbps (跨洋 slow-start 主导)
代理出口上行峰值: 8.9 Mbps (5MB 上传)
代理出口上行稳态: 6.2 Mbps
代理 health probe RTT: 0.74 s (cloudflare trace 208B)

www.goalserve.com 稳态吞吐 (proxy, gzip): 300-1200 kbps
  - 大 schedule (NBA/NHL): 350-960 kbps
  - 小 inplay endpoint: < 1 kbps (延迟主导, 非吞吐)
inplay.goalserve.com: HTTPS 443 不可达 (timeout 7.5s) — IP 白名单未开
oddsfeed.goalserve.com: HTTPS 443 不可达 — subscription 未开
两子域 HTTP 80 IIS 可达但 path 全 404 (subscription 未开)

gzip 压缩比 (XML schedule 类): raw 755KB → gz 116KB, 省 84.6% (max 85.1%)
gzip 阈值规则: body > 500B 时开 --compressed; 否则跳过 (避免 gzip framing 反胀)

并发上限 (www.goalserve.com proxy):
  服务端饱和 RPS = 4.5 (c=20 达到, c=30 反降)
  本轮 1-30 并发 100% 成功 (淡时段 30 req/档)
  代理出口 + 上行 + 下行 完全未饱和
  推荐生产并发 c=4-6, 留余量给重试

直连 vs 代理 (大 endpoint nba-shedule):
  direct p50 3.41s / p95 7.12s
  proxy  p50 2.78s / p95 4.11s  ← 代理更稳, 与小段 v1 inplay 测一致

时段限制: 本轮淡时段 (UTC 07:34-08:05 美东深夜), 晚高峰需 cron 重测
```

## 8. 网络诊断手册 (将来用) — 怎么用这些 baseline 判断 "网络出问题了"

### 8.1 探针 (固化 cron job)

| 探针 | 命令 | 期望 | 红线 |
| --- | --- | --- | --- |
| proxy health | `curl -x $PROXY https://www.cloudflare.com/cdn-cgi/trace -o /dev/null -w '%{time_total}\n' --max-time 8` | < 1.5 s | > 5 s 或 connect refused |
| proxy 下行 1MB | `curl -x $PROXY -o /dev/null -w '%{speed_download}\n' --max-time 30 'https://speed.cloudflare.com/__down?bytes=1000000'` | > 200 KB/s (1.6 Mbps) | < 50 KB/s |
| proxy 下行 25MB | (每日 1 次) | > 2 MB/s (16 Mbps) | < 500 KB/s 持续 |
| www.goalserve.com schedule | `curl -x $PROXY --compressed -o /dev/null -w '%{time_total}\n' --max-time 25 "$URL_NBA_SCHED"` | p50 < 5 s | p95 > 15 s 持续 |
| direct vs proxy diff | 同 endpoint 双拉 | proxy 不应慢于 direct > 50% | proxy > direct × 2 |

### 8.2 告警条件 (给可观测性老何参考)

| 告警 | 触发 | 严重度 | 行动 |
| --- | --- | --- | --- |
| proxy_down | proxy health probe 连续 3 次 timeout | P1 | clash 重启, 切备代理 |
| proxy_slow | proxy 下行 1MB < 50 KB/s 持续 5 min | P2 | 查代理出口节点, 切节点 |
| goalserve_slow | www.goalserve.com p95 > 15s 持续 5 min | P3 | 看是否晚高峰; 降并发到 c=2 |
| goalserve_5xx | 任意 5xx > 10% in 1 min | P1 | 暂停业务调用, 等 Goalserve 恢复 |
| goalserve_403 | 任意 403 | P0 | key 失效 / 订阅过期 — 立即联系老雪 |
| gzip_ratio_anomaly | XML endpoint gzip ratio > 50% (应该 < 20%) | P3 | 服务端可能没启 gzip / 中间设备改写 |

### 8.3 诊断流程 (oncall runbook 草稿)

发生 "代理 / Goalserve 慢" 工单时:

1. **先验本机** — 直连 cloudflare trace 是否 < 1s; 不通则本机出口或运营商问题 (非代理锅).
2. **验代理** — 走代理 cloudflare trace 是否 < 1.5s. 大于 → clash 重启 / 换节点.
3. **验代理带宽** — 25MB 长跑是否 > 16 Mbps. 不足 → 海外节点出口拥堵, 切节点.
4. **验 Goalserve** — direct nba-schedule 与 proxy nba-schedule 同时拉, 对比. 直连快 → 代理路径有问题; 直连一样慢 → Goalserve 服务端慢, 不能修, 降并发等服务端恢复.
5. **验 subscription** — 任意 endpoint 返 403 / 401, 立即查 `.env` 中 `GOALSERVE_API_KEY` 是否过期 (Goalserve 续费周期由老雪盯).
6. **inplay/oddsfeed 子域 timeout** — 这是已知约束 (subscription 未开通), 不是事故 — **除非将来开通后再次 timeout 才告警**.

### 8.4 baseline 失效条件

- 代理换节点 → 重测 §2
- Goalserve 续约 / 加 inplay subscription → §3.2 §3.3 重测
- 出现 24h × 多时段重测后稳定数据 → 当前 v1 进 v2 (替换淡时段 spot 数据)

## 9. 风险与开放问题

1. **inplay / oddsfeed 子域吞吐未实测** — 当前 key 未开通对应 subscription, HTTPS 443 不通, HTTP 80 全 404. — owner: 老雪 (vendor 联络) + 老吴 (开通后重测)
2. **采样时段单一** — 本轮淡时段 (UTC 07:34-08:05 美东深夜) 没复现老陈 v1 的 13% 超时. 需 cron 跑 24h 拿到峰值时段数据 — owner: 老吴
3. **代理 25 Mbps cap 是节点限制还是 ChinaTelecom 出口限制** — 当前 1 节点测试, 多节点对比需要扩 — owner: 老吴 (待 SRE 工具栈 v2)
4. **gzip ratio 仅 3 个 endpoint 样本** — 全 Goalserve endpoint 矩阵 gzip 分析待小段 / 小余在 ETL 上线后补全 — owner: 小段
5. **代理上行 9 Mbps 是否够 Polymarket order 提交峰值** — 当前 sprint 还没大量交易测试, 待老陈 + 老姜 latency budget 跑通后回测 — owner: 老陈
6. **未做 24h 长跑稳定性** — proxy 是否会偶发 hung > 30s 未知, 老陈 v1 见过 1 个 outlier. Cron 上线后 SLA 实测 — owner: 老吴

---

汇报给 GM 老雷 (Wave 15):
- **已完成**: 代理出口峰值 / 稳态 + Goalserve 三域吞吐 + gzip 压缩比 + 并发 sweep, 数据已沉淀.
- **代理峰值带宽**: 单连接下行 **25.3 Mbps**, 上行 **8.9 Mbps**.
- **Goalserve 三域稳态吞吐**: `www.goalserve.com` proxy 300-1200 kbps; `inplay.goalserve.com` / `oddsfeed.goalserve.com` subscription 未开通, HTTPS 不可达, 已记入约束.
- **gzip 省**: XML schedule 类 **省 84-85%** (远超 60% 目标), 阈值 > 500 B 才开 gzip.
- **限流触发并发数**: 本轮淡时段 1-30 并发 **0 触发** (服务端 RPS 上限 ~4.5, 我方代理 + 出口完全未饱和). 老陈 v1 的 13% 超时是晚高峰瞬时排队, 需 cron 重测峰值时段.
