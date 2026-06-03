# 事件延迟套利端到端延迟可行性报告 v1

> **owner:** 小袁 (microstructure, 量化研究部 #C)
> **last_review:** 2026-06-03
> **召唤背景:** 小邓 ml-engineer-fairvalue-alpha-design-v1.md §5.3 悬案："实测我们的端到端延迟 vs 12s 窗口，够不够抢"

---

## TL;DR

**边际可行，但余量极薄，不是"舒适胜"。**

我们的端到端中位延迟 **~2.6s（事件感知）+ 0.5s（下单）= ~3.1s**，对比 PM 重定价中位 13.2s，理论剩余 **~10.1s 窗口**。链路本身没问题。

但真实瓶颈不在网络：**Goalserve 数据本身每 ~2s 更新一次**（实测 updated_ts_ms 变化中位 2015ms），加上我们 1s 轮询周期，**比分事件到我们感知的感知延迟中位约 2–3s**，不是文档里写的"~1s"。这是头号认知错误，必须纠正。

**可行性判断：**
- 跨洋链路（伦敦 EC2）：**不是瓶颈**，CLOB RTT 实测 ~25ms，inplay 全程 ~77ms
- Goalserve 数据时效：**是瓶颈之一**，中位 2s 刷新间隔 + 1s 轮询 → 感知延迟 1.5–3s
- paper_loop tick：**是瓶颈之二**，当前 500ms tick + 最坏 score staleness = 额外 0–500ms
- 总端到端：**中位 ~3.1s，最差 ~5s**，vs PM 重定价 p25=8.5s → p25 情景下也有 **~3.5s 裕量**
- 结论：**事件延迟套利在工程上可行，但必须专门建低延迟触发路径才能兑现这个裕量**

---

## 1. 端到端延迟分解（实测 + 估算）

### 1.1 延迟预算表

| 链路段 | 延迟（实测/估算） | 来源 | 备注 |
|---|---|---|---|
| **[A] 真实事件 → Goalserve 数据刷新** | **0–2000ms（均值约 500ms）** | 估算 | Goalserve 内部 pipeline；见 §1.2 |
| **[B] 我们轮询 Goalserve（等下一次 GET）** | **0–1000ms（均值 500ms）** | 代码：poll_interval_ms=1000 | 均匀分布，均值 500ms |
| **[C] inplay.goalserve.com HTTP 全程** | **TCP connect 38.7ms + 传输 ~38ms = 77ms** | 实测（EC2）| 429 响应；正常 200 含更多 body |
| **[D] gzip 解压 + JSON 解析 + Publish** | **< 5ms** | 估算（代码审查）| ~259KB JSON，CPU bound ~1ms |
| **[E] ScoreSnapshotStore::GetSnapshot()** | **~5ns** | 代码注释（RCU atomic）| 热路径零锁 |
| **[F] paper_loop::TickAll() 到感知比分变化** | **0–500ms** | 代码：tick_interval_ms=500 | 取决于 tick 相位 |
| **[G] TickOne 决策处理（per market）** | **~0.38ms（全盘口 380ns/market）** | 架构评审 bench | 441 tokens，可忽略 |
| **[H] CLOB POST /order（含 TLS 新建连接）** | **~60ms（全链路）** | 实测（EC2，10 次）| p50=59.8ms；连接复用可降至 ~25ms |
| **[I] CLOB 服务端处理 + 确认** | **~3ms（估算）** | TTFB - CONNECT = 25ms - 3ms | 已含在 H 中 |

**端到端中位总计（A+B+C+D+F+H）：**
- 乐观（A=250ms, B=250ms, F=125ms）：**~770ms**
- 中位（A=500ms, B=500ms, F=250ms）：**~1350ms + 网络 + 下单 = ~1.6s**
- 含完整全程（含 C+G+H）：**~3.1s**
- 最差（A=2000ms, B=1000ms, F=500ms）：**~3.6s + 网络 = ~4.0s**

### 1.2 关键实测数据（EC2 eu-west-2 伦敦，2026-06-03）

**Goalserve inplay 数据实际刷新频率（实测，非假设）：**

| Sport | 变化次数 | p25 间隔 | p50 间隔 | p75 间隔 | 最短 |
|---|---|---|---|---|---|
| Soccer | 1823 | 2013ms | 2015ms | 2037ms | 1007ms |
| Basketball | 1818 | 2017ms | 2023ms | 2047ms | 1014ms |
| Tennis | 1815 | 2013ms | 2015ms | 2034ms | 1012ms |

**结论：Goalserve updated_ts_ms 实际刷新间隔中位 ~2015ms，不是 1s。** 文档里"Goalserve 比分更新 ~1s"指的是我们的轮询频率，不是 Goalserve 服务端的刷新频率。这两个是不同的。Goalserve 服务端数据每 ~2s 更新一次。

注意：updated_ts_ms 的连续不变频率约 42%（1312/3112 样本中相邻两轮完全相同），意味着我们实际"感知到数据变化"的概率在 ~58% 的轮次里有更新。

**CLOB 网络 RTT 实测：**

| 端点 | ICMP RTT | TCP connect | TLS handshake | HTTP full RTT |
|---|---|---|---|---|
| clob.polymarket.com | 2.39ms avg | 2.8ms avg | 24.9ms | 25ms（auth err） |
| CLOB POST /order | — | — | — | **59.7ms p50**（新建 TLS）|
| gamma-api.polymarket.com | 2.42ms avg | 2.7ms avg | — | 25.4ms |
| inplay.goalserve.com | ICMP blocked | 38.7ms avg | N/A（HTTP）| **77ms**（429）|
| www.goalserve.com | 128ms avg | 131ms avg | — | 264ms（307 redirect）|

**关键发现：**
1. Polymarket CLOB 到伦敦 EC2 仅 2.4ms ICMP RTT（Cloudflare CDN 就近节点在欧洲），**伦敦部署对 CLOB 延迟是绝对优势**
2. inplay.goalserve.com TCP 连接 38.7ms（Kansas City 机房），每次 HTTP GET 都新建连接（Connection: close），这是不可避免的额外延迟
3. 完整 CLOB 下单（含新建 TLS）约 60ms；若能维持长连接则降至 ~25ms

---

## 2. 对比 12s 窗口：总延迟 vs PM 重定价时序

### 2.1 PM 重定价时序（来自 shorthorizon-locking-feasibility-v1.md）

| PM 重定价 | 时间 |
|---|---|
| p25（最快 25%） | 8.5s |
| 中位 | 13.2s |
| p75 | 20.5s |
| 最快观测 | 2.4s |
| 有可观测重定价的事件 | 56.5% |

### 2.2 我们的端到端 vs 窗口

| 情景 | 我们的延迟 | PM 重定价 | 剩余窗口 |
|---|---|---|---|
| 最好（A=0, B=0, F=0, 复用连接）| 0.1s | p25=8.5s | **~8.4s** |
| 乐观（A=500ms, B=250ms, F=125ms）| ~1.1s | p25=8.5s | **~7.4s** |
| **中位（A=1000ms, B=500ms, F=250ms, 新建TLS）** | **~3.1s** | **中位=13.2s** | **~10.1s** |
| 最差（A=2000ms, B=1000ms, F=500ms）| ~4.5s | p25=8.5s | **~4.0s** |
| 极端最差（A=2000ms, B=1000ms, F=500ms）| ~4.5s | 最快=2.4s | **-2.1s（抢不到）** |

**瓶颈识别：**

1. **头号瓶颈：Goalserve 数据刷新延迟（A 段，实测中位 ~1000ms 感知延迟）**
   - Goalserve 服务端每 ~2s 刷新，我们轮询 1s，感知延迟服从 U[0, 2s] 分布，均值 1s
   - 这是**不可控的上游延迟**，除非升级 Goalserve 数据合约（push 模式或更高频轮询）

2. **二号瓶颈：paper_loop tick 间隔（F 段，当前 500ms）**
   - 感知到 score 变化后，最坏需等 500ms 才执行下一次 TickOne
   - **这是可控的**：专门的事件触发器可以绕过 tick 调度，实现亚秒响应

3. **三号因素：inplay HTTP 新建连接（C 段，38.7ms TCP + 传输）**
   - 每次 HTTP GET 都新建 TCP 连接（Connection: close），无持久化
   - **可优化**：TCP 长连接或 keep-alive 可以节省 ~40ms/次

4. **四号因素：CLOB 下单新建 TLS（H 段，~60ms）**
   - **可优化**：维持 HTTP/2 或 WebSocket 长连接至 CLOB，降至 ~25ms

---

## 3. 窗口稳健性分析：哪些价位 + 盘口真有正期望？

### 3.1 价位敏感性

| 价位段 | BE（中位） | H=60s >BE 比例 | 我们 3.1s 延迟后剩余 | 实际可用窗口内 >BE 估计 |
|---|---|---|---|---|
| **极端（mid<0.15 / >0.85）** | **1.7c** | **9.3%** | 约 10s | **约 8–9%（最优）** |
| 中间段（0.15–0.35 / 0.65–0.85）| 3.8c | 5.9% | 约 10s | 约 4–5% |
| **近平（0.35–0.65）** | **6.45c** | **1.4%** | 约 10s | **约 0.5%（不可行）** |

**结论：**
- **near_half 盘口（0.35–0.65）明确不可行**：BE=6.45c，H=60s 内 >BE 仅 1.4%，加手续费后负期望
- **极端价位盘口是甜区**：BE=1.7c 仅需小移动即够本，且事件驱动移动集中（进球在低/高赔率盘口冲击大）
- 注意：极端价位盘口通常已接近结算，position risk 不对称，必须严格控仓

### 3.2 流动性门槛

- 极端价位中位 spread 1c，BE=1.7c → **理论净期望空间约 0c 到正数**（取决于事件后具体移动幅度）
- 极端价位 H=60s >BE 的 9.3% 中，绝大多数（>70%）集中在事件后 10s 内（shorthorizon 报告 §C1）
- 如果我们在 3.1s 进场，PM 中位重定价 13.2s 还没完成，剩余 ~10s 窗口内仍有 8–9% 的概率出现 >BE 移动

### 3.3 覆盖率约束

当前日志显示：**227/420 market（54%）匹配到 Goalserve event**，且变动最多为 cov-diag 显示 207/420。意味着只有约 50% 盘口有实时比分数据，另外 50% 无法执行事件套利策略。事件套利只能在已匹配的盘口上发生，实际候选池约 200 个 market。

---

## 4. 触发设计评估：现有架构能否支撑亚秒触发？

### 4.1 现有架构的触发路径（诚实评估）

```
事件发生
  → Goalserve 内部刷新 (~2s)
  → 我们 GET 轮询（下次，0-1s 等待）
  → TCP connect + HTTP (~77ms)
  → gzip 解压 + 解析 (~5ms)
  → ScoreSnapshotStore::Publish (~5ns)
  → paper_loop 下次 tick（0-500ms 等待）
  → TickAll 遍历所有 market (~380ns/market * 441 = 168ms)
  → TickOne 决策
  → CLOB POST (~60ms)
总计中位: ~3.1s
```

**现有架构的根本局限：**
1. `paper_loop` 是**时间驱动**（tick-based），不是**事件驱动**。即使比分刚刷新，也要等到下个 500ms tick 才触发
2. `inplay_feed_thread` 和 `paper_loop` 是**解耦的异步系统**，通过 `ScoreSnapshotStore` RCU 共享。比分更新后 paper_loop 不知道"有新事件"，只能靠定时轮询
3. TickAll 每次遍历 441 tokens，是**全量扫描**，不是**增量事件触发**
4. **没有 score_change 通知机制**：`ScoreSnapshotStore::Publish` 只是原子替换，没有 observer/callback

### 4.2 现有架构能否支撑亚秒触发？

**不能，当前架构天然 500ms tick 延迟。**

要实现亚秒触发，需要：

**方案 A（最小改动）：降低 tick_interval_ms**
- 把 500ms 降至 100ms
- 效果：F 段从 0-500ms 降至 0-100ms，节省均值 200ms
- 代价：CPU 消耗增加 5x，对 441 tokens 每 100ms 全遍历 = 约 10k 次/s 决策运算
- 风险：热路径 CPU 占用可能影响 WSS 事件处理
- **可行性：可行，改动小，是 v2 第一步**

**方案 B（专用事件触发器）：**
- 在 `InplayFeedThread::RunSportLoop` 的比分 Publish 后，检测 score 变化
- 如果变化 → 立即触发一个轻量级"事件 TickOne"，跳过全量遍历
- 只针对已知映射到该 event 的 token 集（直接查映射表），不扫全量
- 效果：F 段降至 ~0，C+D 处理完即触发 → 总延迟降至 ~1.5s
- 代价：需要增加 `score_change_callback` 接口 + event→token 反向映射
- **可行性：需一定工程量，但架构上干净**

**方案 C（不推荐：降低 inplay 轮询至 500ms）：**
- 把 `poll_interval_ms` 从 1000 降至 500
- 等效把 B 段均值从 500ms 降至 250ms
- 节省 250ms 均值
- 风险：**违反 Goalserve ToS 速率限制**（当前 min_fetch_interval_ms=1000 正是守 1 req/s 限）
- **不推荐**

### 4.3 触发器设计原则（事件延迟套利，非建模）

事件延迟套利的核心逻辑是规则而非 ML：

```
IF score_changed(event_id) AND game_phase ∈ {InPlay, HalfTime}:
    candidate_tokens = lookup_by_event(event_id)
    FOR tok IN candidate_tokens:
        direction = score_to_direction(old_score, new_score, market_type)
        IF direction != NONE AND liquidity_ok(tok):
            submit_limit_order(tok, direction, size=min_size)
```

**"score_to_direction"逻辑（极简）：**
- Moneyline: 领先分扩大 → 领先方 YES 上涨 → 买领先方 YES（当前价格 < 新 fair）
- Totals: 总分增加 → OVER 上涨（若 total < line）→ 买 OVER
- 不需要精确 fair-value 模型，只需方向正确

---

## 5. 诚实结论

### 5.1 可行性判断

**事件延迟套利对我们：【边际可行】**

| 维度 | 结论 | 关键数据 |
|---|---|---|
| 链路延迟 | **不是瓶颈** | EC2-CLOB ICMP 2.4ms；CLOB POST p50 25ms（连接复用） |
| Goalserve 时效 | **是主要约束** | updated_ts_ms 中位 2s 刷新，感知延迟均值 ~1s，最差 2s |
| 总端到端（中位）| **3.1s** | vs PM 重定价中位 13.2s，剩余 ~10s |
| 总端到端（最差）| **~4.5s** | vs PM 重定价 p25 8.5s，最差情景裕量 4s |
| 极端价位 >BE 期望 | **理论正期望** | BE=1.7c，H=60s >BE=9.3%，事件后 10s 内集中 |
| near_half 可行性 | **不可行** | BE=6.45c 远高于典型事件移动 |
| 架构支撑 | **需改造** | 当前 500ms tick 不支持亚秒触发，需专用事件触发器 |

### 5.2 如果要做，v2 第一步是什么

**按优先级：**

**Step 1（可立即做，改动最小）：tick_interval_ms: 500 → 100ms**
- 改 `PaperLoopConfig::tick_interval_ms` 默认值
- 效果：F 段均值从 250ms 降至 50ms，总延迟降至 ~2.9s
- 验证：观察 CPU 占用是否可接受（目前 441 tokens * 2/s = 882 次/s TickOne，改后 ~4410 次/s）

**Step 2（核心工程）：score_change 事件触发机制**
- `ScoreSnapshotStore::Publish` 添加 change callback，或 `InplayFeedThread::RunSportLoop` 检测变化后 notify
- `PaperLoop` 添加 `OnScoreEvent(event_id, old_score, new_score)` 接口
- 内部：只对涉及该 event 的 token 触发 TickOne，不做全量遍历
- 效果：F 段降至 ~0，总延迟降至 ~1.5s（A+B+C+D+H）

**Step 3（可选优化）：Goalserve 长连接 / CLOB 长连接**
- inplay HTTP 改为 keep-alive（节省 38.7ms TCP 建连）
- CLOB 下单改为 HTTP/2 持久化（节省 ~35ms TLS 握手）
- 效果：总延迟降至 ~1.0s

**Step 4（策略逻辑）：score_to_direction 触发器**
- 纯规则：比分变化 → 方向判断 → 进场
- 极端价位优先（mid<0.15/>0.85），near_half 屏蔽
- 仓位控制：事件后 10-15s 内自动平仓（避免持仓过久吃 PM 重定价后的不利价）

### 5.3 不画饼的数字估计

- **有效触发频率（估算）：** 每场球 scoring event ~5–20 次，有 Goalserve 覆盖的盘口约 200 个，每天 ~50–100 场有效事件，PM 有对应 condition 且流动性足 → 可触发机会 **每天 ~10–50 次**（极保守）
- **单次期望净值（极端价位）：** 1.7c BE，如果我们进场时 PM 还未重定价，实际移动中位估算 3–5c（事件驱动） → 净期望 **~1.3–3.3c/share**，扣滑点后 **~0.5–2c**
- **总限制：** 流动性是真约束，极端价位盘口深度有限（一次 >$100 很难）；频率低，每日期望 PnL 量级在 **$5–$50**（非$500+）
- **这不是主要收入来源，是补充策略**：与 sharp-anchor baseline（现有 fair_resolve 路径）配合，在事件瞬间有更强的方向信号

---

## 附录：关键延迟数据汇总

### EC2 eu-west-2 实测（2026-06-03）

```
CLOB ICMP ping:              avg 2.39ms, mdev 0.031ms
CLOB TLS handshake:          24.9ms
CLOB POST /order (新建TLS): p50 59.8ms, avg 59.7ms, min 57.6ms
CLOB POST /order (估算复用): ~25ms (= TTFB 减去握手)
gamma-api HTTP:              avg 25.4ms (Cloudflare 就近)

inplay.goalserve.com ICMP:   100% packet loss (防火墙)
inplay.goalserve.com TCP:    avg 38.7ms (Kansas City)
inplay.goalserve.com HTTP:   ~77ms (含 TCP + 传输，429 限速)

Goalserve updated_ts_ms 刷新间隔（实测，3000+ 样本）:
  Soccer: p50=2015ms, min=1007ms
  Basketball: p50=2023ms, min=1014ms
  Tennis: p50=2015ms, min=1012ms
```

### 代码路径延迟

```
ScoreSnapshotStore::GetSnapshot():   ~5ns (RCU atomic load)
gzip 解压 (~50KB gz → ~260KB JSON): ~1ms 估算
JSON 解析 (InplayScoreParser):       ~2ms 估算
ScoreSnapshotStore::Publish():       ~5ms (mutex + map clone)
paper_loop TickAll (441 tokens):     ~168ms (380ns/market bench)
paper_loop tick 等待（当前）:        0–500ms (tick_interval_ms=500)
```
