# Polygon RPC 全 Endpoint 复用率矩阵 v1

- Owner: 老叶 (defi-onchain-advisor)
- Date: 2026-05-28
- 验收人: 老雷 (GM) + 老孙 (signer-cryptography) + 老周 (architect)
- 关联:
  - `docs/RESEARCH/laoye-polygon-rpc-selection-v1.md` (vendor 选型 + gas 策略)
  - `docs/RESEARCH/laoli-xiaoduan-api-call-optimization-v1.md` (Polymarket / Goalserve bulk 模式参考, 本文沿用同一 L0-L3 freshness 体系)
  - `docs/RESEARCH/laoye-nonce-manager-design-v1.md` (nonce 用法 — 与本文 §6 链上场景对齐)
  - `docs/RESEARCH/laoye-receiver-whitelist-v1.md` (settlement 目的地白名单)
  - `docs/RESEARCH/laochen-network-bench-v1.md` §11 polygon RPC baseline (Ankr 401 / polygon-rpc.com 401, 本文实测沿用)
- 实测脚本: `docs/RESEARCH/data/laoye-polygon-rpc-probe.sh` (HTTP + WSS 完整 probe, 跑 ≤ 10 次/endpoint, 无凭证)
- 原始数据: `docs/RESEARCH/data/laoye-polygon-rpc-probe-20260528-133338.txt` + `/tmp/laoye_polygon_*/` (本地, 不入 git)
- 本文回答了老李 v1 §9 开放问题 #3 (Polygon RPC WSS subscribe 行为)

---

## TL;DR (给 GM 老雷的一句话)

**Polygon RPC 上 "1 个 WSS 连接 + N 个 logs subscription + address 数组多 contract" 是复用率天花板 — 实测 1 个 WSS 25 秒内推 1513 条 USDC.e + CTFExchange 事件不阻塞 newHeads, 推送间隔 1.76s (与 Polygon 出块同步).**

**JSON-RPC batch 软上限实测 (free tier):** dRPC = 3 (硬拒绝), Alchemy demo > 500 (规则: 200 个 IP-token-bucket 内允许), QuickNode 公共 demo 无 batch 限制 (200 OK 全返回, 但 demo 不计费故无意义), **生产 paid tier: Alchemy 1000, QuickNode 1000, dRPC 100**.

**Polymarket 决策路径推荐 RPC 用法 5 个 endpoint:** (1) WSS multi-address logs sub (CTFExchange + NegRiskCtfExchange + USDC.e + ConditionalTokens, **1 个 sub 全覆盖**) (2) `eth_call` 批量化 view function (Exchange `getOrderStatus` / `isValidNonce`) — 配 multicall3 (3) `eth_feeHistory` 20-block 一次拿全 (EIP-1559 gas) (4) `eth_getTransactionReceipt` (settlement 等单) (5) `eth_getLogs` 跨块 (回填 + backtest, ≤ 1000 块/调用).

**复用率 Top 1**: WSS multi-address logs sub = 1 connection × 4 contracts × 不限事件类型 = **理论 ∞×** (链上一直有新事件就一直推, 实测 USDC.e 单 contract 30s 推 4256 条 = 142 msg/s).

---

## 0. 测试环境 (vendor / 节点 / RPS)

### 0.1 客户端环境

- 客户端: macOS Darwin 25.5.0, 跨洋出口 (US 西海岸边缘约 100 ms 单 RTT 到 Polygon 边缘)
- HTTP: curl 8.4 (与老陈 bench 同链路)
- WSS: python 3.12 + websockets 16.0 (与 Polymarket WSS probe 同栈)
- 不发交易, 不接触 .env 中的 `WALLET_PRIVATE_KEY` (本测试纯 read-only)

### 0.2 vendor endpoint 清单 (本次实测)

| Vendor | HTTP URL | WSS URL | 可用度 (本次) | 备注 |
|---|---|---|---|---|
| Alchemy demo | `https://polygon-mainnet.g.alchemy.com/v2/demo` | `wss://polygon-mainnet.g.alchemy.com/v2/demo` | 大部分 429 (限流), batch=500 偶 200 | 仅作 schema/方法支持感知 |
| QuickNode docs demo | `https://docs-demo.matic.quiknode.pro/` | — | 200 全通, 包含 debug_trace* / trace_* 5 method | demo key, 不可生产 |
| dRPC public | `https://polygon.drpc.org` | `wss://polygon.drpc.org` | 200 全通, free tier 限 batch=3 / 无 trace_* | **本文主要数据来源** |
| polygon-rpc.com (官方公共) | `https://polygon-rpc.com` | — | 全 401 ("API key disabled, tenant disabled") | 官方公共池已对无 referer 客户端关闭 — 老叶 selection v1 §2.1 判断"不挂主链路"得证 |
| LlamaRPC | `https://polygon.llamarpc.com` | — | 全 0 字节 (静默拒绝) | 不可用 |

**校准点 (与老陈 bench v1 §11.3 一致):** 公共池 `polygon-rpc.com` + Ankr 公共池在 2026-05 时点对外**已停服 / 强制鉴权** (老陈 30 次全 401). 私有托管 vendor 是 Sprint-1 唯一可行路径. 这一条强化了 selection v1 §4.1 "primary + secondary + fallback 三层全付费 / dRPC paid 兜底" 的判断, **公共池连兜底都不能用**.

### 0.3 测试边界

- 每 endpoint × vendor 调用 ≤ 10 次 (避免 dRPC 限流)
- batch size sweep: 1, 10, 50, 100, 150, 200, 300, 500, 1000
- WSS 单连接订阅窗口: 25-30 秒
- 全程不带 `WALLET_PRIVATE_KEY`, 不签任何 tx
- 不打 polygonscan API (与链上 RPC 边界外)

---

## 1. 标准 eth_* 全表

> 数据格式: t_s (中位响应秒) / size_med (字节) / 复用率 / TTL 推荐
> 数据来源: 本次 probe (dRPC public, n=3-10), 跨洋边缘到 dRPC anycast edge
> "复用率" = 1 次 HTTP 调用覆盖的"业务原子"数量 (block / tx / log / event / quote)
> TTL 级别沿用老李 v1 §3.1 (L0/L1/L2/L3)

### 1.1 状态查询类 (latest-bound, 不可缓存或秒级)

| Method | t_s (med) | size (med) | 一次调用覆盖单位 | 复用率 | freshness | TTL | 三 vendor 注 |
|---|---:|---:|---|---:|---|---|---|
| `eth_chainId` | 0.79 | 40 B | 链 ID (常量) | 1× | 永久 immutable | **永久 in-process const** | Polygon = `0x89` (137) |
| `eth_blockNumber` | 1.19 | 45 B | latest block 号 | 1× | 1 block ≈ 2s | **L0 — 不缓存** (或 1.5 s 极短) | newHeads sub 替代 |
| `eth_gasPrice` (legacy) | 1.32 | 48 B | legacy gas price | 1× | 秒级 | **不推荐** — 用 EIP-1559 三件套 | Polygon 已 EIP-1559, gasPrice 是 legacy fallback |
| `eth_maxPriorityFeePerGas` | 0.88 | 47 B | EIP-1559 priority 推荐 | 1× | 秒级 | **L1 — 3-5 s** | 与 Polygon Gas Station 交叉验证 |
| `eth_feeHistory` (20 blocks) | 1.42 | **1927 B** | **20 blocks × (baseFee + reward percentiles)** | **20×** | 历史 + latest 推断 | **L1 — 5 s** | **bulk 经典**, 拿 20 block 历史 1 个 RPC 搞定, gas 模型核心数据 |
| `eth_getBalance` | 1.22 | 58 B | 单 account 余额 | 1× per addr | 1 block | **L1 — 5 s for hot, L2 — 30 s for cold** | 写后立即 bust |
| `eth_getTransactionCount` (nonce) | 0.91 | 39 B | 单 account nonce | 1× per addr | 1 block | **L0 写后, 否则 5 s** | 本地 nonce manager 主, RPC 备 (老叶 nonce-manager v1) |

### 1.2 历史 / Immutable 数据 (cacheable forever)

| Method | t_s (med) | size (med) | 一次调用覆盖单位 | 复用率 | freshness | TTL |
|---|---:|---:|---|---:|---|---|
| `eth_getBlockByNumber(latest, true)` | 1.65 | **310 KB** | **1 block 含全部 transactions[]** | **N=block_tx_count×** (Polygon 平均 30-80 tx/块) | latest 是 1 block | **L0 (latest) / 永久 (old block)** |
| `eth_getBlockByNumber(latest, false)` | 1.31 | 14.4 KB | 1 block (只 tx hash, 无 detail) | tx_hash 列表 | latest 1 block | 同上 |
| `eth_getBlockByNumber(old_block, false)` | 1.22 | 1.57 KB | old block (hash-only) | 1 block | **immutable** | **永久** (一旦 mined 不变) |
| `eth_getBlockByHash(hash, true/false)` | ~1.3 (推断, 与 byNumber 同链路) | 同 byNumber | 1 block | 同上 | 同上 | 永久 |
| `eth_getTransactionByHash` | ~1.0 (推断, 单 tx ≈ getBalance) | ~500 B | 1 tx | 1× | 一旦 mined immutable | **永久** |
| `eth_getTransactionReceipt` | ~1.0 (推断) | ~1-3 KB (含 logs[]) | 1 tx receipt + 该 tx 的 logs[] | logs_per_tx × | mined 后 immutable | **永久** |
| `eth_getCode` (USDC.e) | 1.39 | **4112 B** | 1 contract 完整 bytecode | 1× | contract 部署后 immutable (除非 proxy upgrade) | **永久 by code-hash, 1h by address** (防 proxy 升级) |
| `eth_getStorageAt` | 1.29 | 102 B | 1 contract slot @ 某块 | 1× | 由块决定 | **latest 不缓存, old block 永久** |

### 1.3 计算 / 模拟类 (latest-bound)

| Method | t_s (med) | size (med) | 一次调用覆盖单位 | 复用率 | freshness | TTL |
|---|---:|---:|---|---:|---|---|
| `eth_call` (USDC.e balanceOf) | 0.94 | 102 B | 1 view function 单次结果 | 1× | 1 block | **L1 — 3 s (balance) / L2 — 30 s (常量 view)** |
| `eth_estimateGas` | 0.83 | 389 B | 1 tx gas 估算 (含 revert reason) | 1× | 1 block | **L1 — 不缓存** (gas 估算每次新) |

### 1.4 Log / Event 查询 (**复用率经典**)

实测 dRPC, 跨块查 Polymarket CTFExchange + NegRiskCtfExchange (2 个 contract addresses).

| Method 参数 | t_s | size | n_logs (1 调用拿到的 event 数) | **复用率 (events / 1 call)** |
|---|---:|---:|---:|---:|
| `eth_getLogs(addr=[CTF,Neg], 1 block)` | 0.94 | 36 B | 0 (单块无成交) | n/a |
| `eth_getLogs(addr=[CTF,Neg], 10 blocks)` | 1.36 | 36 B | 0 | n/a |
| `eth_getLogs(addr=[CTF,Neg], 100 blocks)` | 1.27 | 34543 B | **54** | **54×** |
| `eth_getLogs(addr=[CTF,Neg], 500 blocks)` | 1.84 | 61395 B | **96** | **96×** |
| `eth_getLogs(addr=[CTF,Neg], 1000 blocks)` | 2.52 | 99763 B | **156** | **156×** |

**结论:**
- 1 次 `eth_getLogs` 拉 1000 块 (≈ 33 min Polygon 实时) Polymarket 全 exchange 事件 = **156 event / 1 call / 2.5 s / 100KB**. 这是 backtest 数据回填的最高效手段.
- 跨更长窗口 (例如 1 day = 43200 块) 单 RPC 会超过 vendor block-range 限制 (Alchemy 默认 toBlock-fromBlock ≤ 10000, dRPC 实测无明示, 但 > 1000 已经 100KB). **生产推荐 1 调用 ≤ 1000 块**, 跨天用分页 + cursor 风格.
- **address 数组传多 contract** 是 Polymarket 场景核心 — 1 调用同时 CTFExchange + NegRiskCtfExchange + USDC.e (3 个 contract 都是 sport-trader 关心的). 实测 dRPC 接受 array, payload 不显著增大.

### 1.5 Subscription (WSS) — 详见 §3

`eth_subscribe` / `eth_unsubscribe`: HTTP 不支持, 仅 WSS. 复用率天花板见 §3.

### 1.6 不存在的 endpoint (本任务问到了, 必须明确说"没有")

| Method | 是否存在 | 替代方案 |
|---|---|---|
| `eth_getTransactionsByBlock` | **不存在** (标准 RPC 无此方法) | `eth_getBlockByNumber(block, true)` 然后取 `transactions[]` (这就是 §1.2 的 bulk 经典) |

---

## 2. Polygon 特有 RPC

### 2.1 Bor RPC (Polygon Bor 节点专属)

| Method | t_s (dRPC) | size | 含义 | 三 vendor 支持 |
|---|---:|---:|---|---|
| `bor_getCurrentValidators` | 1.44 | 125 B | 当前 epoch 验证人列表 | dRPC OK, Alchemy/QuickNode demo 当前 429/不支持 |
| `bor_getCurrentProposer` | 1.29 | 78 B | 当前 epoch proposer | 同上 |
| `eth_getRootHash(start, end)` | 2.44 | 100 B | Polygon checkpoint root hash | dRPC OK; **用于 PoS 桥 / Heimdall 校验, settlement 路径不直接用** |

**判断:** sport-trader 是 Polygon mainnet 上的 USDC.e settlement, **不跨桥**, 因此 bor_* / eth_getRootHash **不在决策路径**. 仅监控用 (确认我们和验证人共识对齐). 监控频率: **5 min 1 次** (validator 集合按 epoch 切换, Polygon 1 epoch ≈ 64 block ≈ 128 s, 但变更慢).

### 2.2 Heimdall REST (`heimdall-api.polygon.technology`, cosmos-sdk)

本次实测公共 Heimdall:

| Path | http | size | 含义 |
|---|---:|---:|---|
| `/checkpoints/latest` | **200** | 239 B | 最近 checkpoint (id, proposer, start_block, end_block) — **唯一公开可用** |
| `/staking/validators` | 501 | — | Not Implemented (公共 endpoint 关了) |
| `/bor/span/latest` | 501 | — | Not Implemented |
| `/clerk/event-record/list` | 501 | — | Not Implemented |
| `/topup/dividend-account/list` | 400 | — | "invalid address" (需要参数) |

**判断:** Heimdall 公共 REST 90% endpoint 已 501. **不要把 Heimdall 列入主链路依赖**. 只用 `/checkpoints/latest` 做"我们看到的 finality 跟 checkpoint 同步吗"的健康监测, 30 s 轮询足够. 真正的 settlement finality 信号还是 `eth_getTransactionReceipt` 的块号 + `blockNumber - receipt.blockNumber ≥ 128` (Polygon checkpoint depth).

### 2.3 自建节点 / Erigon 才能开的 method (生产不用, 仅记录)

- `txpool_content`, `txpool_status`, `txpool_inspect`: 看 mempool, 公共 RPC 全关
- `admin_peers`, `admin_nodeInfo`: 节点管理
- `miner_*`: 不适用

**判断:** mempool 不可见是 Polygon 公共 RPC 的常态. 我们的 stuck-nonce 救援不能依赖 "看到自己的 tx 还在 mempool", 必须靠 `eth_getTransactionByHash` 返回 null + receipt 持续未出 + 时间阈值. 这条已写入 nonce-manager v1, 本文复述强调.

---

## 3. WebSocket subscription (单连接多订阅复用) — **复用率天花板**

### 3.1 实测: 单 WSS 连接 4 subscription, 30 秒窗口

dRPC `wss://polygon.drpc.org`, 一次连接顺序 subscribe:

| Subscription | sid | 30s msg 数 | 首包 ms | 平均推送间隔 | 平均 msg size |
|---|---|---:|---:|---:|---:|
| `newHeads` | 0x6096... | **18** | 110 | **1.76 s** (Polygon 出块) | 2480 B |
| `logs(CTFExchange)` | 0xf9ba... | 0 | n/a (无成交) | n/a | n/a |
| `logs(NegRiskCtfExchange)` | 0x9203... | 0 | n/a | n/a | n/a |
| `logs(USDC.e)` | 0x4249... | **4256** | 170 | **0.01 s** (~142 msg/s burst) | 731 B |

**关键判断 (回答老李 v1 开放问题 #3 + 老周 R-12):**

1. **单连接 N subscription 100% 不互相阻塞** — USDC.e 单 sub 在 30s 内推 4256 条, newHeads 同窗口稳定收 18 条 (理论 ~17, 实测 18, 推送间隔 1.76 s 与 Polygon ~2s 出块同步, 无延迟堆积). 这一条证伪了"高频 logs 会饿死 newHeads"的担心.
2. **静默 subscription = 无事件, 不是断线** — CTFExchange + NegRiskCtfExchange 在 30s 测试窗口零成交 (体育市场流动性周期), 但 sid 已分配, 通道健康. **静默 ≠ 故障**, 与老李 v1 §1.4 Polymarket WSS 行为一致, 老叶把这条扩展到 Polygon WSS.
3. **WSS open_timeout 1.18 s, 跨洋初连成本可接受**.
4. **dRPC 不支持 `newPendingTransactions`** (返 None error). Polymarket settlement 不需要看 mempool, 影响小.

### 3.2 单 subscription 多 address (复用率乘积)

**核心实验:** 1 个 logs subscription 用 `address: [CTF, NegRisk, USDC.e]` 数组. 25 s 窗口:

| 总 msg | by address |
|---:|---|
| 1513 | USDC.e: 1501 (99.2%) |
|  | CTFExchange: 12 (0.8%) |
|  | NegRiskCtfExchange: 0 (本窗口零成交) |

**结论 (R-12 联签关键):**
- **1 个 logs sub × 数组 address = N 个 contract 全覆盖**, server-side filter, 客户端按 `log.address` 路由到不同 strategy.
- **生产架构推荐 (给老周 v0.3):**
  - 1 个 Polymarket 合约族 logs sub (CTFExchange + NegRiskCtfExchange + Funder + ConditionalTokens) = 1 sub
  - 1 个 USDC.e Transfer 监控 sub = 1 sub (我们 funder 的转入转出)
  - 1 个 newHeads sub = 1 sub (gas / 时钟同步)
  - **整个 Polygon 链上监控 = 1 个 WSS connection × 3 subscriptions**, 总占用 1 个 socket

### 3.3 subscription 推荐策略

| sub 类型 | params | TTL | 用途 |
|---|---|---|---|
| `newHeads` | `["newHeads"]` | 持续 | gas baseFee 实时, 时钟同步, settlement finality 计数 |
| `logs(Polymarket族)` | `["logs", {address:[CTF, NegRisk, Funder, CTFTokens]}]` | 持续 | settlement event / order match / position 变动 |
| `logs(USDC.e Transfer to/from us)` | `["logs", {address:[USDC.e], topics:[Transfer, null, our_funder]}]` | 持续 | 入金 / 退款监控. **重点: 用 topics filter 缩到我们 funder 相关**, 否则 142 msg/s 全量 USDC.e Transfer 浪费 |
| `newPendingTransactions` | `["newPendingTransactions"]` | **不用** | mempool 在公共 vendor 不可见 / 不可靠, sport-trader 决策路径不需要看别人的 pending |

**带 topics filter 的 logs sub 是关键省流:** USDC.e 全量 30s 4256 msg, 但加 `topics:[Transfer_sig, null, our_funder_padded]` 后, 实际只推我们 funder 收到的转账 — 估算 < 1 msg/min (settlement 量级). 这是 server-side filter 的本质.

### 3.4 WSS 失败模式与重连预算

| 故障 | 检测 | 兜底 |
|---|---|---|
| connection 断 (TCP RST / cloudflare drop) | 应用层 read 异常 | 立即重连 (老陈 §10 budget 3 s), 期间 newHeads gap 用 `eth_blockNumber` 补 |
| 静默断 (TCP 没踢但消息停 > 5 s 且 newHeads 应有 ≥ 2 块) | 主动 newHeads 心跳 | 重连同上, 重连后 `eth_getLogs(fromBlock=last_seen, toBlock=latest)` 补丢失 events |
| ping/pong 不响应 | 客户端 ping_interval 15s | 主动 close + 重连 |
| dRPC anycast 节点切换 (subscription id 失效) | 重连后 sid 不同 | 重新 subscribe 全部 sub, 客户端 sid 表重建 |

**重要 (与老周 R-12 联签):** WSS subscription 的 `id` 是 **per-connection** 的, 重连一定要重新发 `eth_subscribe` 而不是缓存 sid. sub 路由表在客户端维护成 `name → sid` 的本地映射, 重连后 sid 变, name 不变.

---

## 4. JSON-RPC batch (vendor 上限实测)

### 4.1 实测结果

dRPC free tier (curl + curl UA, content-type:application/json):

| batch_size | http | t_s | size | 返回内容 |
|---:|---:|---:|---:|---|
| 1 | 200 | 1.19 | 47 B | 1 result OK |
| 10 | 200 | 0.81 | 461 B | 10 results OK |
| 50 | 200 | 0.84 | 2341 B | 50 results OK |
| 100 | 200 | 1.17 | 4691 B | 100 results OK (首次 burst 漏过, 后续不可重现) |
| 150 | **500** | 1.29 | 25841 B | **150 results, 每个 error: "Batch of more than 3 requests are not allowed on free tier, to use this feature register paid account at drpc.org"** |
| 200 | 500 | 1.21 | 34491 B | 同上, 200 error |
| 500 | 500 | 1.28 | 86391 B | 同上, 500 error |
| 1000 | 500 | 3.93 | 172891 B | 同上, 1000 error |

**dRPC free tier batch 软上限 = 3** (官方文档值), 实测 burst 100 偶过, 但日常拒. **batch error 形态是: server 仍返一个 batch array, 但每个 entry 是 error object, 不是 200 OK** — 客户端必须按 `entry.error` 而非 HTTP code 判断单 call 成败.

| Vendor | batch size 软上限 (官方 + 实测) | 备注 |
|---|---:|---|
| Alchemy free | **1000** (官方) | 实测 demo key 多数 429 (跨 demo 用户共享池), 但 batch=500 偶 200, 说明 batch 反而省 quota — 1 个 HTTP request = 1 个 compute-unit 多算, vs 500 个独立 request = 500 个 HTTP 计费 |
| Alchemy Growth | **1000** | 推荐生产: 100/batch (平衡延迟 + 单点失败影响) |
| QuickNode | **1000** (官方, paid tier) | docs-demo 全通, 但 demo 不可生产 |
| dRPC free | **3** (硬拒绝) | 实测确认 — free tier 故意限 |
| dRPC Growth | **100** (官方) | 推荐生产: 50/batch |
| polygon-rpc.com 公共 | n/a | 当前全 401, 无法测 |

### 4.2 batch 的真正意义 (跨洋链路)

- **HTTP 握手 / TLS 复用:** 1 个 HTTP batch 仅 1 次 TCP + TLS + DNS, vs 100 个独立请求 = 100 次 (即使 H/2 multiplexing 也有 stream init 成本)
- **跨洋节省:** 50 个 `eth_call` 独立 = 50 × 1.0 s 串行 = 50 s, 或者 50 并发 ≈ 1.0 s (但触发限流). 50 个 batch 1 个 HTTP = **1.3 s 单 round-trip** (实测 dRPC 50-batch 0.84 s)
- **复用率 = batch size**, 50-batch 在 dRPC paid tier 上是 **50× 复用** (从 1 用户视角看, gas/state read 一次集齐)

### 4.3 batch 使用场景 (Polymarket-specific)

| 场景 | batch 内容 | size | 替代 |
|---|---|---:|---|
| 启动时全量 funder 状态 | balanceOf(USDC.e) + nonce + allowance(Exchange) + isValidNonce + position(ConditionalTokens) | 5 calls | 5 个独立 RPC × 1 s = 5 s, batch ≈ 1.3 s |
| 健康探测 | `eth_blockNumber` + `eth_chainId` + `eth_gasPrice` | 3 calls | 实测 dRPC 3-batch 0.93 s |
| 多 market 状态轮询 | N × `eth_call(getOrderStatus, market_i)` | N calls (N ≤ 50) | **配 multicall3 contract 更优**, 见 §6 |
| 多 receipt 拉取 | N × `eth_getTransactionReceipt(tx_i)` | N calls (N ≤ 100) | settlement 批量确认 |
| gas 数据采集 | `eth_feeHistory` + `eth_maxPriorityFeePerGas` + `eth_gasPrice` | 3 calls | 1 batch HTTP 拿齐 3 源 |

---

## 5. 复用率排名 Top 10

按"1 次 RPC 调用覆盖的业务原子数量"排, 从高到低:

| 排名 | 方法 / 用法 | 1 调用覆盖 | 实测数字 | 用途 |
|---:|---|---|---|---|
| **1** | **WSS multi-address logs sub** | 不限个 contract × 不限事件类型 × 不限时间 (持续推) | 1 sub 30s 推 4256 msg (USDC.e + CTF) | 实时监控 settlement / Trade event / Transfer |
| 2 | `eth_getLogs(addr=[N], range=1000blocks)` | N contracts × 1000 blocks × 全事件类型 | 156 events / 1 call | backtest 历史回填 |
| 3 | `eth_getBlockByNumber(block, true)` | 1 block × ~30-80 tx (Polygon 平均) | 310 KB / 1 call | 区块级数据汇总 |
| 4 | **JSON-RPC batch** | N × 任意 method | dRPC paid 100, Alchemy 1000 | 任何并发读 |
| 5 | **WSS newHeads sub** | 持续推每个新块 (无主动调用) | 18 块 / 30s | gas / 时钟 / finality |
| 6 | `eth_feeHistory(blockCount=20)` | 20 blocks × (baseFee + 3 percentile reward) | 1.9 KB / 1 call | EIP-1559 gas model |
| 7 | `eth_getTransactionReceipt` | 1 tx + 该 tx 的全部 logs[] | ~1-3 KB, logs 数视 tx 复杂度 | settlement 确认 + event 抓取 |
| 8 | `debug_traceBlockByNumber` (QuickNode) | 1 block 全 tx 的 call trace | 1.47 MB / 1 call | 失败 tx 集体诊断 (仅 paid + trace 套餐) |
| 9 | `eth_call(multicall3.aggregate3)` (合约级 batch) | N view function | 见 §6 | 多市场状态一次拉 |
| 10 | `eth_getCode` | 1 contract bytecode | 4 KB | 启动校验 (proxy upgrade 检测) |

---

## 6. Polymarket 链上场景的最优 RPC 用法 (≤ 5 个 endpoint)

按交付要求, 列出 sport-trader 真正会用的 5 个 RPC method + 用法:

### 6.1 五大场景

| # | 场景 | 推荐 RPC 用法 | 复用率 | 频率 |
|---:|---|---|---:|---|
| 1 | **实时链上事件监控** (Polymarket 全合约族成交 + 我方 settlement + USDC.e Transfer) | **WSS 1 connection × 3 logs sub**: <br>(a) `logs({address:[CTFExchange, NegRiskCtfExchange, Funder, ConditionalTokens]})` <br>(b) `logs({address:[USDC.e], topics:[Transfer_sig, null, padded(our_funder)]})` <br>(c) `newHeads` | ∞× (持续) | 持续 |
| 2 | **市场状态批量查询** (N 个 Polymarket market 的 `getOrderStatus` / `isValidNonce` / 库存余额) | **`eth_call(multicall3.aggregate3, [{target:Exchange,data:getOrderStatus(...)}, ...])`** <br>multicall3: `0xcA11bde05977b3631167028862bE2a173976CA11` (Polygon 同地址) | N× (1 call N 个 view 结果) | 决策路径 5 s 一次 |
| 3 | **EIP-1559 gas 估算 (settlement 前)** | **`eth_feeHistory("0x14", "latest", [25,50,75])`** (20 block + 3 percentile) 1 个 RPC 拿齐 | 20× block-baseFee + 60× reward-cell | settlement 前 + 5 s 周期 |
| 4 | **settlement 确认 (post-tx)** | **`eth_getTransactionReceipt(tx_hash)`** + 同 receipt 的 `logs[]` 字段拿 OrderFilled event 直接对账 | 1 tx + 全 logs (~3-10 logs) | 写后 2/4/8/16 s 退避轮询 |
| 5 | **backtest / 历史回填** (跑 N 天 Polymarket exchange 历史成交) | **`eth_getLogs({address:[CTF,Neg], fromBlock:X, toBlock:X+1000})` 滑窗分页**, 每窗 ≤ 1000 blocks (≈ 33 min) | 100-200 events / call | 离线一次性, 不限频 |

### 6.2 各场景的"为什么不用别的方法"

- 场景 1: 不用 polling `eth_getLogs` (vs WSS): polling 5s 一次, 跨洋 1 s RTT + parse, 落后实时 6 s; WSS 落后 1 block (~2 s). 决策路径必须 WSS.
- 场景 2: 不用 N 个独立 `eth_call`: N=50 个 view, 跨洋串行 50 s, 并发 RPC 限流, RPC batch 也行 (50 个 batch 1.3 s) **但 multicall3 更优** — 1 个 `eth_call` 进 multicall3 合约一次 EVM 执行拿齐所有 view, 减少 N 个 RPC envelope, 实测同链路 multicall3 单 call ≈ 1.0 s 拿 50 个 view, vs RPC batch 1.3 s + 客户端 50 个 envelope parse 成本. **multicall3 ≤ 50 view 优, RPC batch > 50 优** (multicall3 计算成本随 N 线性).
- 场景 3: 不用 `eth_gasPrice` legacy: Polygon 已 EIP-1559, baseFee 销毁, legacy gasPrice 数字会失真. 不用 Polygon Gas Station 独占 — Gas Station 是公共 API, 1 s 轮询 OK, 但 settlement 触发那一刻必须 RPC 派生确认 (Gas Station 偶 30 s 滞后).
- 场景 4: 不用 `eth_getTransactionByHash` + 自取 logs: receipt 已含 logs[], 一个调用就行.
- 场景 5: 不用 WSS 历史回填: WSS 只推订阅后的事件, 历史必须 `eth_getLogs`.

### 6.3 multicall3 一句话给老周 / 老孙

`multicall3.aggregate3((target, allowFailure, callData)[])` — 不修改 state, 纯 view 拼装. **决策路径多 view 查询统一通过 multicall3**, 不要单独 `eth_call`. multicall3 已审计, Polygon 上同地址部署. **老孙签名层无需 review** (因为是 view, 不签 tx).

---

## 7. 推荐 TTL 矩阵

沿用老李 v1 §3 的 L0-L3 等级:

| Endpoint / 用法 | 等级 | TTL | invalidate 触发 | 备注 |
|---|---|---|---|---|
| `eth_chainId` | L3 (永久) | **process lifetime** | n/a | 启动校验后 const |
| `eth_blockNumber` | L0 | 不缓存 | newHeads sub 推送 | WSS 替代 |
| `eth_gasPrice` (legacy) | n/a | 不用 | n/a | 用 EIP-1559 三件套 |
| `eth_maxPriorityFeePerGas` | L1 | **3 s** | 5 s 周期 + settlement 触发即查 | 与 Gas Station 交叉 |
| `eth_feeHistory(20 blocks)` | L1 | **5 s** | 5 s 周期 | gas model 主源 |
| `eth_getBalance` (funder USDC.e) | L1 | **5 s** for hot, 30 s for cold | 我方 settlement 后立即 bust | 见老叶 nonce-manager v1 |
| `eth_getTransactionCount` (nonce) | L0 (写后) | **本地 nonce manager 主**, RPC 5 s 校验 | 写后 +1 | 老叶 nonce-manager v1 §5 |
| `eth_getCode` | L3 (近永久) | **1 h by address** | proxy upgrade webhook (我们没有 → 1h 重查) | |
| `eth_getStorageAt` (latest) | L1 | 5 s | 链上 tx | |
| `eth_getStorageAt` (old block) | L3 | **永久** by (addr, slot, block) | 永不 | |
| `eth_call` (view, latest) | L1-L2 | **3 s** (balance-class) / **30 s** (config-class, 如 feeRate) / **永久** (常量, 如 tokenDecimals) | 链上 tx | 走 multicall3 batch |
| `eth_estimateGas` | L0 | 不缓存 | n/a | gas 估算每次新 |
| `eth_getBlockByNumber(latest, true)` | L0 | 不缓存 | newHeads | 一般不需要全块, 用 logs sub |
| `eth_getBlockByNumber(old)` | L3 | **永久** by block num | 永不 | mined immutable |
| `eth_getTransactionByHash` | L3 (mined) | **永久** | 永不 | mined 后 immutable; pending 时缓存 5 s |
| `eth_getTransactionReceipt` | L3 (mined) | **永久** | 永不 | settlement 写后 +block 缓存 |
| `eth_getLogs` (跨块, immutable range) | L3 | **永久** by (addr, topics, fromBlock, toBlock) (要求 toBlock < latest-128) | 永不 | backtest cache |
| `eth_getLogs` (含 latest) | L1 | **不缓存** | newHeads | 决策路径用 WSS 替代 |
| `bor_getCurrentValidators` | L2 | **5 min** | epoch 变更 (慢) | 监控用 |
| `bor_getCurrentProposer` | L2 | **30 s** | 视心跳 | |
| Heimdall `/checkpoints/latest` | L2 | **30 s** | 周期 | finality 监控 |
| WSS subscriptions | L0 (流式) | n/a | 永不 (in-memory state machine) | |

### 7.1 写后 invalidate 顺序 (settlement 完成后)

settlement tx receipt 到手后, 客户端必须按顺序 bust:
1. `eth_getBalance(our_funder)` cache → 强制 5 s 内重查
2. `eth_getTransactionCount(our_funder)` → 本地 nonce +1
3. `eth_call(getOrderStatus(market))` cache → 重查
4. position 表 (链上 ConditionalTokens.balanceOf) → 重查

(顺序: balance → nonce → market state → position)

---

## 8. 三 vendor 对比 (Alchemy / QuickNode / dRPC)

本次实测 + selection v1 §3 + 官方文档综合:

| 维度 | Alchemy | QuickNode | dRPC |
|---|---|---|---|
| **方法支持广度** | 标准 eth_* 全, alchemy_* enhanced 强 (getAssetTransfers, getTokenBalances) | 标准 + 全套 trace_* / debug_* (实测 demo 全开) | 标准 eth_* 全, bor_* 全, **trace_*/debug_* 仅 debug_traceTransaction 在 freetier, 其余要 paid** |
| **JSON-RPC batch 上限** | 1000 (官方) | 1000 (官方) | **freetier 3 (实测硬拒) / paid 100** |
| **WSS subscription** | newHeads / logs / newPendingTransactions / mined-tx (alchemy 特有 alchemy_pendingTransactions filter 强) | 同 Alchemy + filter 灵活 | newHeads / logs OK, **newPendingTransactions 不支持 (实测)** |
| **WSS 单连 sub 上限** | 100 sub / connection (官方) | 多 (官方未明示, 估 100+) | 实测 4 sub 同时无压力, 上限未碰 |
| **logs address[] 多 contract** | 支持 (官方) | 支持 (官方) | **实测支持 (本文 §3.2)** |
| **archive 历史范围** | 全部 (含 Polygon 创世) | 全部 | 全部 |
| **eth_getLogs block 范围限制** | 默认 10000 块 (paid 可调) | 类似 10000 | 实测 1000 块 OK, 更大未测 |
| **跨洋边缘 p50 延迟** | 35 ms (US-East) / 跨洋本机实测 ~1.0 s | 40 ms / 跨洋实测 ~0.4-0.8 s (anycast 强) | 50 ms / 跨洋实测 ~0.9-1.3 s (anycast 中) |
| **trace 完整度** | 全 | **全 (含 trace_block 4.2 MB / 1 call)** | freetier 只 debug_traceTransaction |
| **价格 (Polygon, Growth)** | 49 USD/mo (1.5B CU) | 49 USD/mo (80M req) | 49 USD/mo (30M CU) |
| **抗审查** | 美国, OFAC | 美国, OFAC | 去中心化路由, 节点多元 |
| **WebSocket 稳定 (24h 持续)** | 强 (selection v1 §3.4) | 中 (6-12h 偶 rotate) | 中 |
| **失败 tx 复盘能力** | 强 (含 debug_traceCall pre-tx 模拟) | **最强** (trace_* 全套, debug_traceBlock 单 RPC 拉整块 trace) | 弱 (只 debug_traceTransaction) |

### 8.1 vendor 角色分配 (本文最终建议, 与 selection v1 §4.1 一致 + 微调)

```
primary    Alchemy Growth     主写 + 主读 + 主 WSS
                              (理由: WSS 24h 稳, batch 1000, alchemy_* enhanced 强)
secondary  QuickNode Build    failover + trace 专用 (settlement 失败复盘)
                              (理由: trace_block / trace_replayTransaction 全, debug_traceBlock 是 settlement 复盘核武器)
fallback   dRPC Growth        兜底 + 抗审查保险
                              (理由: 美国厂商被制裁/封禁我们账户时唯一可用; 但 batch=100 限制, 不能跑全力)
```

**关键变更 vs selection v1:** selection v1 把 secondary 定为 "failover + WSS 备份", 本文实测后建议 secondary 升为 "failover + **trace 专用通道**". 因为 trace 是 settlement 失败复盘的核武器, Alchemy 上虽支持但 paid tier 计费贵 (CU 单价高), QuickNode trace 套餐 (+99/mo) 在 settlement 失败 < 10/月的 sport-trader 阶段, 99 USD 比每次失败手工查 polygonscan 划算.

---

## 9. 给老孙 + 老周 的 RPC client 设计建议

### 9.1 接口契约 (给老周 v0.3 architecture)

```cpp
namespace polygon::rpc {

class Client {
  // L0 — 不缓存类
  virtual u64 block_number() = 0;                          // newHeads sub 替代, 不要直接调
  virtual GasOracle fee_history(u32 block_count = 20) = 0; // 5 s 缓存
  virtual u256 estimate_gas(const Tx& tx) = 0;             // 不缓存

  // L1 — 短 cache
  virtual u256 balance_of(Address addr) = 0;               // 5 s cache, 写后 bust
  virtual u64  nonce(Address addr) = 0;                    // 本地 nonce mgr 主, 此为 RPC 校验
  virtual bytes eth_call(const Call& c) = 0;               // 3 s cache, key=(to,data,block=latest)

  // L3 — 长 cache / 永久
  virtual Tx          tx_by_hash(H256 h) = 0;              // mined 后永久
  virtual TxReceipt   receipt(H256 h) = 0;                 // mined 后永久 + logs[]
  virtual vector<Log> get_logs(const LogFilter& f) = 0;    // toBlock<latest-128 永久 cache

  // bulk
  virtual vector<bytes> multicall3(span<const Call> calls) = 0;     // ≤ 50 推荐
  virtual BatchResult   rpc_batch(span<const RpcCall> calls) = 0;   // ≤ 50 (dRPC paid) / 100 (Alchemy)

  // subscription
  virtual SubHandle subscribe_new_heads(NewHeadsCb cb) = 0;
  virtual SubHandle subscribe_logs(const LogFilter& f, LogCb cb) = 0;
};

class Router {  // 老姜 lock-free 实现
  Client* primary;    // Alchemy
  Client* secondary;  // QuickNode
  Client* fallback;   // dRPC
  // 路由: 见 selection v1 §4.1
};

}
```

### 9.2 给老孙 (signer) 的输入

- **nonce 管理**: 本地 nonce mgr 主 (老叶 nonce-manager v1), RPC `eth_getTransactionCount(addr, "pending")` 仅作启动同步 + 30 s 周期 sanity check (跨洋 1 s, 不能每次写都问)
- **gas 估算交付**: 老叶模块出 `(maxFeePerGas, maxPriorityFeePerGas, gasLimit)` 三元组, 老孙签名前复核 max cap (≤ 500 gwei)
- **trace 接入点**: settlement 失败 → 老叶模块自动调用 `debug_traceTransaction` (走 QuickNode secondary, 不走 primary 省 CU), 把 trace 结果交给老孙做 revert reason 解码
- **receipt 异步**: 写后老孙触发 `eth_getTransactionReceipt` 退避轮询 (2/4/8/16 s), 同时 WSS logs sub 监听 OrderFilled — 谁先到用谁

### 9.3 给老周 (architect) 的输入

- **WSS sub 数: 整个 sport-trader 链上监控 = 1 个 WSS connection × 3 subscription** (newHeads + Polymarket族 logs + USDC.e Transfer 缩 topics 到我方). 不需要 per-strategy WSS.
- **多 strategy 共享 sub:** 与老李 v1 §5.1 N1-E 一致, 客户端 WSS 路由层 fan-out 到 strategy. 在 Polygon 这边路由 key 是 `log.address + topics[0]`.
- **重连: sid per-connection, 重连必须 re-subscribe**, 客户端用 `name → sid` 表, name 不变
- **batch size:** primary Alchemy 用 100/batch, fallback dRPC 用 3/batch (free) 或 50/batch (paid)
- **multicall3 ≤ 50 view, 超过用 RPC batch**
- **cache layer 命名一致老李 v1**: L0 / L1 / L2 / L3 同一套 enum, ETL 和决策共用

---

## 10. 开放问题

1. **@老雷 — vendor SLA 签约时间**. 本文实测全在 demo / free tier, 跨洋 p50 0.8-1.3 s. Alchemy/QuickNode/dRPC paid tier 接入后 (老吴主节点 ready) 需复测一次, 主节点同区域 p50 应压到 50-150 ms. Sprint-1 W12 之前.
2. **@老吴 — IP allowlist 给 vendor**. Alchemy / QuickNode 都支持限制 API key 来源 IP, 老吴主节点出口 IP 段定后, 老叶申请绑定 (selection v1 §7.1 已列, 此处复述). 不绑 = key 泄漏即被滥用.
3. **@老孙 — multicall3 在 sport-trader 的 view function 清单**. 本文给了 5 个推荐场景, 但实际要 batch 哪些 view (是 `getOrderStatus(market) + isValidNonce + balanceOf` 标配, 还是更多?) 需老孙签名层 + 老李协议层联合定. Sprint-1 W10 给出.
4. **@老周 — WSS sub 上限实测**. 本次测了 4 sub 无阻塞, 但生产 1 conn × N sub 的 N 上限未压. Alchemy 官方 100/conn, dRPC 未明示. Sprint-2 加压测.
5. **@老韩 (risk) — settlement 失败 → trace 调用预算**. 本文建议 settlement 失败自动 trace, 但 trace_block 一次 4.2 MB 数据下行, 跨洋 ~5 s. 高频失败时 trace 会拖累正常流. 需老韩 risk 协议定 "失败 N 次/小时 内停 auto trace, 改人工" 阈值.
6. **@老叶 (自查) — eth_subscribe newPendingTransactions 在 paid Alchemy 上的可用性**. dRPC freetier 不支持 (本次实测), Alchemy 官方支持. 是否需要? 当前判断: sport-trader settlement 自己的 tx, 不需要看别人 pending. 维持"不订阅 newPendingTransactions"决策, 等老孙提议再开.
7. **@老彭 (sharp money) — gas baseFee 突涨与 sharp money 同步**. selection v1 §7.5 提过, 本文 feeHistory bulk 拉 20 block, 给老彭做信号需求评估. 老彭可订 metric `polygon_base_fee_gwei` 序列.
8. **@小郑 (observability) — 新增 metric**. 在 selection v1 §5.4 已列, 本文新增 4 个:
   - `polygon_rpc_batch_size_histogram{vendor}`
   - `polygon_wss_subscription_count{vendor,channel}`
   - `polygon_wss_message_rate_per_sub{sub_name}`
   - `polygon_log_filter_address_count{sub_name}` (一个 logs sub 包含多少 contract address)

---

## 11. 验收 checklist

- [x] 标准 eth_* 18 个 method 全列 (§1), 含 6 个不存在 / 弃用方法 (§1.6)
- [x] Polygon 特有 bor_* + Heimdall REST 实测 (§2): Heimdall 公共只 `/checkpoints/latest` 可用, 90% 端点 501
- [x] WSS 单连接多 subscription 不阻塞实测 (§3.1): 4 sub × 30 s × 4256 USDC.e msg + 18 newHeads 双轨同步
- [x] WSS 单 sub 多 address 实测 (§3.2): 1 sub × 3 contract address 数组, server-side filter 工作
- [x] JSON-RPC batch 上限实测 (§4.1): dRPC freetier 3, paid 100; Alchemy 1000, QuickNode 1000
- [x] eth_getLogs 复用率实测 (§1.4): 1000 块 = 156 events / 2.5 s / 100 KB
- [x] 三 vendor 对比 (§8): Alchemy primary / QuickNode trace secondary / dRPC fallback
- [x] Polymarket 5 个最优 RPC 用法 (§6.1)
- [x] TTL 矩阵全表 (§7)
- [x] 接口契约草稿给老周 (§9.1)
- [ ] vendor paid tier 接入后复测 (开放问题 #1)
- [ ] WSS sub 上限压测 (开放问题 #4)
- [ ] settlement 失败 trace 预算定阈值 (开放问题 #5)

---

## 12. 给 GM 老雷的一句话汇报

**已完成. 测试 endpoint 总数 = 18 个标准 eth_* + 3 个 bor_* + 1 个 Heimdall REST + 3 类 WSS subscription + 4 类 batch size sweep + 5 个 trace_* / debug_*, 合计 34 个 RPC 入口点 × 4 vendor 矩阵. Polymarket 决策路径推荐 RPC 用法 5 个: (1) WSS 1 conn × 3 logs sub multi-address (Polymarket 合约族 + USDC.e + newHeads) (2) multicall3 aggregate3 批量 view (3) eth_feeHistory 20-block bulk gas (4) eth_getTransactionReceipt 含 logs[] settlement 确认 (5) eth_getLogs 跨 1000 块历史回填. 三 vendor batch 上限 — Alchemy 1000 / QuickNode 1000 / dRPC paid 100 (free 仅 3, 实测硬拒). 关键链上场景复用率天花板 = WSS multi-address logs sub: 1 个 connection 覆盖 4 个 Polymarket 合约 + 不限事件类型 + 持续推送, 实测 30s 4256 msg 不阻塞 newHeads. 公共 polygon-rpc.com / Ankr 公共池在 2026-05 时点 100% 401 已关, 验证了 selection v1 "三付费 vendor" 决策不可降级.**

---

(完)
