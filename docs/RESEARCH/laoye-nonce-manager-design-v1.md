# Nonce Manager 设计 v1

- Owner: 老叶 (defi-onchain-advisor)
- Date: 2026-05-28
- 验收人: 老孙 (crypto-signing-expert) + 老雷 (CEO)
- 关联: `docs/RESEARCH/laosun-key-management-v5.1.md` (nonce mgr Q9, 原 v2 已删), 关联 ticket S1-005 (signer) / S1-009 (RPC)
- 截止: 2026-06-26 (Sprint-2 启动)
- 上游依赖: 老叶 `laoye-polygon-rpc-selection-v1.md` (Alchemy primary / QuickNode secondary / dRPC fallback)
- 下游消费者: signer (老孙), trader (老周), audit (老唐), monitoring (小郑)

---

## 0. TL;DR (给老孙 + 老雷)

active-standby 双 signer 不能各自管 nonce — 一旦同时 reserve 同一个 nonce, 链上拒一笔 + 重放风险 + 后续 nonce gap 卡死写交易. 本设计:

- **选型: Redis 主存 + SQLite 本地落盘 + 链上 RPC 当真值源** (三层, 不引入 etcd / Consul / Raft)
- **接口 4 个: allocate / commit / rollback / observe**, gRPC over UDS (本机), 与老孙 signer 同盘部署
- **active-standby 语义: 持有 Redis lease 即 primary**, lease 1s TTL, 失去 lease 1 个 ping 周期内停发 allocate
- **SLO: allocate p99 < 1ms (本机 Redis)**, commit p99 < 5ms, observe (RPC 链上对账) p99 < 200ms
- **灾备 4 档: Redis 挂 → SQLite-only 降级 (允许 allocate); SQLite 损坏 → RPC 重建; 双挂 → trader 进 cancel-only; 网络分裂 → fencing token 强制单 primary**
- **链上 nonce reorg / replacement tx**: observe loop 每 1s 调 RPC `eth_getTransactionCount(addr, "pending")` 对账, 偏差强制重置本地 nonce

**核心不变量: 同一个 (wallet, nonce) 全集群只允许有一笔签名出现在 mempool**. 重放保证靠这个不变量 + signer 内部 nonce 校验 (老孙 B5).

---

## 1. 为什么需要 Nonce Manager (业务背景)

Polygon EVM 的 nonce 语义是 per-EOA 单调递增, broadcast 到 mempool 的 raw tx 必须严格按 nonce 顺序被矿工打包, 否则后续 tx 卡 pending.

我们的 signer 因 B7 (老孙 v2) 改为 active-standby 双进程:
- signer-A 活跃, signer-B 热备, 同一份 wallet 私钥两份内存副本
- 故障切换 detection < 100ms, failover < 1s 内 signer-B 接管

风险:
- failover 期间, signer-A 已签 nonce=N (返回 trader 但还没上链), signer-B 接管不知道 N 已被消费, 再签 N → mempool 两笔同 nonce → 一笔被丢弃, gap 卡死
- 网络分裂下 (signer-A 自认 primary, signer-B 也自认 primary), 双方各签 N, N+1, N+2... 后果同上
- trader 上链失败 (gas 太低 / RPC 返回 nonce too low), 本地 nonce 不释放, 后续全部 stuck

Nonce Manager 集中仲裁这个状态机.

---

## 2. 选型 (Redis + SQLite + RPC 三层)

### 2.1 候选对比

| 方案 | allocate 延迟 | 一致性 | 故障半径 | 引入复杂度 | 判定 |
|---|---|---|---|---|---|
| 文件 + flock | 1-5ms | 强 (单进程) | 文件系统 | 低 | 单点, 不抗 signer 跨机 |
| SQLite WAL (单机) | 0.5-2ms | 强 | 单文件 | 低 | 同上, 不抗机器宕机 |
| Redis 单实例 + AOF | 0.1-1ms | 强 (per-key) | 进程 | 低 | 主选, 加 SQLite mirror 补持久 |
| etcd / Consul Raft | 5-30ms | 强 (Raft) | 集群 | 高 | 杀鸡用牛刀; allocate p99 不达标 |
| ZooKeeper | 同 etcd | 强 | 集群 | 高 | 同上 |
| Postgres advisory lock | 2-10ms | 强 | DB | 中 | 引入新组件不划算 |
| **Redis + SQLite mirror + RPC observe (本设计)** | **0.1-1ms** | **强 (lease + fencing)** | **进程+磁盘+网** | **中** | **选** |

### 2.2 为什么不用 etcd / Raft

老孙 v2 把 signer SLO 定到 allocate < 10ms (其实他写的是建议值, 真实需要看), 我们目标 p99 < 1ms. Raft consensus 一轮 commit 在同机房 5-15ms, 跨机房 30-80ms — 直接吃光 sign request 整个延迟预算 (老蒋 latency budget 给 signer 全链路 50ms p99). 不做.

### 2.3 三层存储职责

```
┌──────────────────────────────────────────────────────────┐
│ Layer 1: Redis (主存)                                     │
│  - 持有 lease (primary 选举)                              │
│  - reserved_max[wallet] = u64 (已分配最大 nonce)          │
│  - inflight[wallet] = sorted set of (nonce, request_id, ts)│
│  - 延迟: 本机 unix socket, p99 0.3ms; 同机房 TCP, p99 0.8ms│
└──────────────────────────────────────────────────────────┘
                           │
                  每次写同步落 SQLite (异步 ok, 200ms 滞后)
                           ▼
┌──────────────────────────────────────────────────────────┐
│ Layer 2: SQLite WAL (本地落盘)                            │
│  - 同表 reserved_max + inflight, 用于 Redis 挂掉时重建    │
│  - 启动时若 Redis 空, 从 SQLite 恢复                       │
│  - WAL mode + synchronous=NORMAL, fsync 间隔 1s          │
└──────────────────────────────────────────────────────────┘
                           │
                  每 1s observe loop 调 RPC 对账
                           ▼
┌──────────────────────────────────────────────────────────┐
│ Layer 3: Polygon RPC (真值源)                             │
│  - eth_getTransactionCount(wallet, "pending")            │
│  - eth_getTransactionCount(wallet, "latest")             │
│  - 若 reserved_max < pending → 链上更激进, 抬高 local     │
│  - 若 reserved_max - latest > 50 → 太多 inflight, 告警    │
└──────────────────────────────────────────────────────────┘
```

### 2.4 部署拓扑

```
┌──────────────────┐         ┌──────────────────┐
│   signer-A       │         │   signer-B       │
│   (active)       │         │   (standby)      │
└────────┬─────────┘         └────────┬─────────┘
         │                            │
         │ gRPC over UDS              │ gRPC over UDS
         │ /var/run/nonce.sock        │ /var/run/nonce.sock
         ▼                            ▼
┌─────────────────────────────────────────────────┐
│   nonce-manager (本机, 同机房双副本)             │
│   ┌──────────────────────────────────────┐       │
│   │ Redis (unix socket, AOF everysec)    │       │
│   │ + SQLite WAL                         │       │
│   │ + observe goroutine                  │       │
│   └──────────────────────────────────────┘       │
│                                                 │
│   nonce-mgr-A (primary)  nonce-mgr-B (replica)   │
└─────────────────────────────────────────────────┘
                  │
                  ▼ 1s 周期
         ┌────────────────┐
         │  rpc-router    │ (老姜 S1-011)
         │  Alchemy + QN  │
         └────────────────┘
```

部署原则:
- nonce-mgr 与 signer 同机 (本机 Redis unix socket), 跨机房不要
- nonce-mgr 自己也 active-standby — 用 Redis SENTINEL 或 keepalived VIP, primary 选举周期 1s
- signer 通过 unix socket 直连本机 nonce-mgr, 不走网络 (减一跳)

---

## 3. 接口规范 (gRPC over UDS, protobuf)

### 3.1 .proto 定义

```protobuf
syntax = "proto3";
package nonce_mgr.v1;

service NonceManager {
    // 核心 4 接口
    rpc Allocate(AllocateRequest) returns (AllocateResponse);
    rpc Commit(CommitRequest)     returns (CommitResponse);
    rpc Rollback(RollbackRequest) returns (RollbackResponse);
    rpc Observe(ObserveRequest)   returns (ObserveResponse);

    // 主备控制 (signer 不直接调, 由 nonce-mgr 内部 fail-over 协议用)
    rpc Lease(LeaseRequest)       returns (LeaseResponse);
    rpc Health(HealthRequest)     returns (HealthResponse);
}

message AllocateRequest {
    bytes wallet = 1;          // 20 bytes EOA
    string request_id = 2;     // signer 端 SignRequest.request_id 透传
    uint64 ts_ns = 3;          // signer 时钟 (NTP-synced)
    bytes signer_token = 4;    // signer 启动时拿到的 fencing token (见 §4)
}

message AllocateResponse {
    uint64 nonce = 1;          // 分配的 nonce
    uint64 lease_id = 2;       // 内部 lease id, commit/rollback 时回传
    uint64 expires_at_ns = 3;  // 这个 nonce 必须在此前 commit 或 rollback (默认 60s)
    Status status = 4;
}

message CommitRequest {
    bytes wallet = 1;
    uint64 nonce = 2;
    uint64 lease_id = 3;
    bytes tx_hash = 4;         // trader 拿到的链上 tx hash (broadcast 之后)
    string request_id = 5;
}

message CommitResponse {
    Status status = 1;
    uint64 confirmed_at_ns = 2;
}

message RollbackRequest {
    bytes wallet = 1;
    uint64 nonce = 2;
    uint64 lease_id = 3;
    string reason = 4;         // "broadcast_failed" | "rpc_reject" | "user_cancel" | ...
    string request_id = 5;
}

message RollbackResponse {
    Status status = 1;
    bool nonce_gap_created = 2;  // 若 nonce < reserved_max, 产生 gap, trader 需补占位 tx
}

message ObserveRequest {
    bytes wallet = 1;
}

message ObserveResponse {
    uint64 onchain_pending_nonce = 1;   // RPC pending
    uint64 onchain_latest_nonce  = 2;   // RPC latest (confirmed)
    uint64 local_reserved_max    = 3;   // 本地分配过的最大值
    repeated InflightTx inflight = 4;
    uint64 observed_at_ns = 5;
}

message InflightTx {
    uint64 nonce = 1;
    bytes tx_hash = 2;
    uint64 reserved_at_ns = 3;
    uint64 broadcast_at_ns = 4;
    string request_id = 5;
}

enum Status {
    OK = 0;
    NOT_PRIMARY = 1;            // 调用方应切到另一个 nonce-mgr
    LEASE_EXPIRED = 2;
    NONCE_ALREADY_COMMITTED = 3;
    NONCE_ALREADY_ROLLED_BACK = 4;
    FENCING_TOKEN_REJECTED = 5; // signer 切了 standby, 不再有权 allocate
    RPC_UNAVAILABLE = 6;
    INTERNAL_ERROR = 99;
}
```

### 3.2 Allocate 语义

signer 收到 SignRequest 后, 在做 EIP-712 hash 前先调:

```rust
let resp = nonce_mgr.allocate(wallet=order.maker_or_signer, request_id, ts_ns, signer_token).await?;
match resp.status {
    OK => {
        // 把 resp.nonce 写进 typed_data.nonce 字段, 签名
        let signature = sign(typed_data_with_nonce(resp.nonce), wallet_key);
        // trader 拿 signature broadcast, 拿到 tx_hash 后回:
        nonce_mgr.commit(wallet, resp.nonce, resp.lease_id, tx_hash, request_id).await?;
    }
    NOT_PRIMARY => panic!("signer 应该已经 fail-over"),
    FENCING_TOKEN_REJECTED => abort!("signer 已被踢出 primary, 应停止 sign"),
    _ => Err(...),
}
```

Allocate 内部:
1. 验 fencing_token (Redis 当前 epoch == signer_token 携带的 epoch)
2. `INCR reserved_max:{wallet}` (Redis 原子)
3. 写 `inflight:{wallet}` (ZADD nonce, ts)
4. 异步同步到 SQLite (best-effort, 不阻塞返回)
5. 返回 nonce + lease_id

### 3.3 Commit 语义

trader 上链成功 (`eth_sendRawTransaction` 返回 tx_hash, **不一定确认入块**) 后立刻调 commit. 不等确认.

理由: signer 不能等链上确认 (10-30s) 才放下一个 nonce, 会卡死流水. commit 只表示"已脱手到 mempool, 不会重签".

Commit 内部:
1. 校验 lease_id 仍有效 (lease 没过期, 没被 rollback)
2. 把 inflight 那条 entry 加 tx_hash 标记
3. 异步落 SQLite
4. observe loop 之后看链上能否对上 tx_hash → confirmed

### 3.4 Rollback 语义

trader broadcast 失败时调:
- `eth_sendRawTransaction` 返回 nonce too low / nonce too high / gas too low
- 网络 timeout (但要 dedup, broadcast 可能其实成功只是响应丢了 — 见 §3.5)
- 用户主动取消 (其实没有这种路径, sport-trader 全自动)

Rollback 内部:
1. 校验 lease_id
2. 从 inflight 删除
3. **关键: 是否产生 gap?**
   - 若 rollback 的 nonce == reserved_max → 不产生 gap, reserved_max 减 1
   - 若 rollback 的 nonce < reserved_max → 产生 gap, 标记 `gap:{wallet}:{nonce}`, trader 必须发占位 tx 填补 (后面说)
4. 异步落 SQLite

### 3.5 broadcast idempotency (重要边角)

trader 调 `eth_sendRawTransaction` 时网络 timeout, 不知道链上到底收了没. 如果直接 rollback 就可能产生 nonce 冲突 (链上其实接受了, 本地却释放给下一笔). 处理:

1. trader 不立刻 rollback, 先调 `nonce_mgr.observe(wallet)` 看链上 pending nonce 是否覆盖到这一笔
2. 若覆盖 → commit (链上接受了, 只是响应丢)
3. 若 1s 后仍未覆盖 → 重试 broadcast (idempotent, 同 raw tx)
4. 重试 3 次仍失败 → rollback

详细见 §6.3 异常处理.

### 3.6 Observe 语义

trader / signer / monitoring 都可调. 用于:
- 故障切换时, signer-B 拿到 observe.local_reserved_max 后才开始 allocate
- monitoring 每 5s 调一次, 给小郑 metrics
- 链上 reorg 时 (RPC 显示 pending < reserved_max - 5 持续 30s), 触发自动重置

---

## 4. Active-Standby 语义 (Fencing Token)

### 4.1 Lease + Epoch 机制

```
nonce-mgr-A: 启动时 SET epoch:current = INCR epoch:counter (NX 或 lua)
              拿到 epoch=42 → 这是当前唯一权威
              signer-A 启动时, nonce-mgr 给 signer-A 发 signer_token = HMAC(secret, "signer-A:epoch=42")
              signer-A 在 Allocate 请求中带 signer_token

nonce-mgr-A 心跳: 每 200ms 调 Redis SETEX lease:primary 1 "nonce-mgr-A:epoch=42"
                  (TTL 1s, 过期则自动被 nonce-mgr-B 抢)

nonce-mgr-B: 启动时 watch lease:primary 是否过期
              过期 → SETNX lease:primary "nonce-mgr-B:epoch=43"
              抢到 → INCR epoch:counter (现在 epoch=43)
              old signer_token (epoch=42) 全部失效

signer-A 收到 FENCING_TOKEN_REJECTED → abort (因为 epoch 已升)
trader 切到 signer-B, signer-B 启动时拿到 epoch=43 的新 signer_token
```

### 4.2 切换时序 (worst case)

```
t=0    signer-A 正在 allocate nonce=100 (request_id=X)
t=10ms signer-A 进程崩溃 (assume mid-sign)
t=100ms trader UDS ping 超时, trader 切到 signer-B
t=200ms nonce-mgr-A lease 过期 (无心跳 200ms)
t=400ms nonce-mgr-B 抢到 lease, epoch 升级 43
t=410ms signer-B 启动 (或一直热备), 拿新 signer_token (epoch=43)
t=420ms signer-B observe(wallet) → local_reserved_max=100 (因为 signer-A 已成功 allocate 但没 commit)
t=425ms signer-B 决策: nonce=100 标记 stale (lease_id 已 expired, 默认 60s 超时但这里立即标)
        signer-B 下次 allocate 拿 nonce=101 (跳过 100)
        nonce=100 进 stale list, observe loop 5s 后看 RPC 是否上链, 否则产生 gap
t=500ms trader 收到 signer-B 的 ack, 业务恢复 (失败一笔 nonce=100 的 request_id=X)
```

总切换 < 1s, request_id=X 那笔订单失败 (trader 看到 retry-or-fail), 但后续 trader stream 不卡.

### 4.3 nonce 100 怎么处理 (stale 的难点)

signer-A 是 mid-sign 崩的, 可能:
- 没签出来 → nonce 100 没人用, 应回收
- 签出来但没返回 trader → trader 不知道, 不会 broadcast, nonce 100 没上链, 应回收
- 签出来已返回 trader, trader 也 broadcast 了, 但 nonce-mgr-A 没收到 commit → nonce 100 已上链, 应 reconcile

observe loop 处理:
```
每 5s, 对 inflight 中 (broadcast_at_ns < 0 即未 commit) 的 nonce:
  - 拿 wallet 当前 pending_nonce_chain
  - 若 nonce < pending_nonce_chain → 链上已接收, 找 tx_hash 补 commit (调 RPC eth_getBlockByNumber 反查)
  - 若 nonce >= pending_nonce_chain && elapsed > 30s → 视为 stale, 自动 rollback
                                                     + gap 检测 + 占位 tx 派发 (§6.4)
```

### 4.4 fencing token 攻击面

老沈 (security) 关心的点: signer_token 若泄漏 → 攻击者可冒充 signer 调 allocate. 缓解:
- signer_token 是 nonce-mgr secret 派生的 HMAC, 不出 signer 内存 (与 wallet key 同等保护, 走 B6 mlock + zeroize)
- nonce-mgr 与 signer 同机 (UDS), 不经网络
- epoch 升级 = 旧 token 立即失效

不算高危, 因为 signer 进程被攻陷的话 wallet key 也丢了, nonce_mgr 不是最薄弱环节. 老沈意见请单独 review.

---

## 5. SLO (服务等级目标)

| 接口 | p50 | p99 | p99.9 | 备注 |
|---|---|---|---|---|
| Allocate | 0.3ms | 1ms | 3ms | 本机 Redis unix socket |
| Commit | 0.5ms | 5ms | 20ms | 含 SQLite 异步落盘 |
| Rollback | 0.5ms | 5ms | 20ms | 同上 |
| Observe (cached) | 0.2ms | 0.5ms | 1ms | 5s 内不调 RPC, 返回缓存 |
| Observe (force=true) | 30ms | 200ms | 500ms | 调 RPC, 跨网 |
| Lease 切换 (failover) | — | 1s | 3s | 含 detection + epoch 升级 |

### 5.1 不达标如何降级

- Allocate p99 > 10ms 持续 30s → 告警, trader 进 cancel-only (cancel 不消耗 nonce, 不依赖 allocate)
- Allocate 失败率 > 1% 持续 60s → 告警 + 自动 fail-over 到 standby nonce-mgr
- Observe 失败 (RPC 全挂) > 5min → 告警, 进保守模式: 不允许 rollback (避免误判产生 gap)

### 5.2 与 latency-budget (老蒋) 对齐

老蒋 latency budget signer 全链路 50ms p99 (跨洋路径以外), nonce_mgr.allocate < 1ms 占预算 2%, 不是瓶颈.

---

## 6. 灾难恢复

### 6.1 Redis 进程崩溃

**症状**: signer 调 allocate timeout / connection refused.

**自动处理**:
1. nonce-mgr 检测到 Redis 不可达 (libredis-rs healthcheck 每 100ms)
2. 切到 SQLite-only 模式 (用 SQLite 当 reserved_max 主存)
3. 仍然能 allocate / commit / rollback, 但每次都 fsync SQLite (慢 5-20x)
4. allocate p99 升到 5ms 但可用, 不阻塞 trader
5. 后台尝试 Redis 重启, 重启成功后从 SQLite 重建 Redis 状态, 切回 Redis-first
6. 告警老吴 + 小郑 (P1, 5min 内响应)

**SLA**: 降级 RTO < 500ms (自动切换), 修复 RTO < 30min (老吴介入).

### 6.2 SQLite 文件损坏

**症状**: 启动时 PRAGMA integrity_check 失败 / journal corrupt.

**自动处理**:
1. nonce-mgr 启动时检测到 SQLite 损坏
2. 不能 trust 本地状态, 调 RPC `eth_getTransactionCount(wallet, "pending")` 拿真值
3. 设 reserved_max = onchain_pending_nonce + safety_margin (比如 +5, 防止 stale 在 mempool)
4. inflight 全部当作 unknown, 进入 reconciliation 模式: trader 端的 pending 订单全部触发 observe + replay
5. 告警 P0, 老吴 + 老孙 + 老雷 立即响应

**SLA**: 自动重建 < 10s (RPC 探测 + reset). 期间 trader 进 cancel-only.

### 6.3 RPC 全挂 (Alchemy + QuickNode + dRPC 三层都不通)

**症状**: observe loop 连续 30s RPC 失败.

**自动处理**:
1. nonce-mgr 进保守模式:
   - 仍允许 allocate (基于本地 reserved_max)
   - **拒绝 rollback** (避免误判, 因为不知道链上状态)
   - 不主动 cleanup stale inflight
2. trader 业务正常 (allocate 仍可用), 但风控侧应该看 RPC 全挂告警进 cancel-only
3. RPC 恢复后, observe loop 全量 reconcile inflight

**SLA**: 取决于 RPC 厂商. 老叶 RPC 选型 v1 §4.1 写了 trader 监测 RPC primary/secondary 偏差 > 3 block 自动降级, fallback dRPC 是终极兜底. 三家全挂概率 < 0.1%/year.

### 6.4 网络分裂 (split brain)

**症状**: nonce-mgr-A 和 nonce-mgr-B 各自认为是 primary, 同时给 signer-A / signer-B 发 signer_token.

**预防**:
- Redis SETEX lease NX 是原子, 只有一个 nonce-mgr 能持有 lease
- 但如果两个 nonce-mgr 用各自的 Redis (不共享) → 必须共享 (sentinel / cluster)
- 部署强制: 两 nonce-mgr 必须用同一 Redis 实例 (主+replica), 不允许 split

**检测**:
- 每个 nonce-mgr 启动时记 epoch, allocate 时强制带 epoch
- signer 收到 NOT_PRIMARY 或 FENCING_TOKEN_REJECTED 一律 abort sign + 告警

**应急**:
- Redis 分区时, 持有 quorum 的那边继续, 另一边自动退化 standby
- 没 quorum (双 Redis 互相看不见) → 两边都 standby, 业务停 (cancel-only). 不允许 split 时双方都 active.

### 6.5 nonce gap (链上需要顺序)

**症状**: nonce 100 已 reserved 但失败回滚, nonce 101 已上链, nonce 102 等待 → 链上要先看到 100 才能打包 101+, 后面全卡.

**处理**:
1. rollback 时若 nonce < reserved_max, 标记 gap
2. nonce-mgr 自动调 signer 签一笔占位 tx (转 0 wei 给 self, gas = 30 gwei priority + 2x base, nonce=100)
3. 占位 tx 通过 trader broadcast (不走 trader 业务路径, 走 nonce-mgr 直连 RPC)
4. 占位 tx 上链 → 后续 nonce 可继续

**等价的 alternative**: 直接调 RPC 用同 nonce 发一笔高 gas 的 self-transfer 替换原 stuck tx ("clear" 操作). Polygon 上廉价 (< 0.001 USD).

**SLA**: gap 检测 < 5s, 占位 tx 上链 < 30s (含 1 block 确认).

### 6.6 chain reorg

Polygon 偶发 1-3 block reorg (Heimdall 终局性 < 2s, 实际罕见). 处理:
- observe loop 比较 `pending` 和 `latest` 的偏差, 持续追踪
- 若 reserved_max < pending → 链上更激进 (可能我们的 standby 也广播了重复 tx), 抬高 reserved_max
- inflight 中 tx_hash 不在最新链状态 → 标记 reorg-suspected, 30s 内再观察, 仍不在则 rollback 自动重发

---

## 7. 与老孙 signer IPC 协议对接

### 7.1 signer 启动流程 (新增 nonce-mgr 握手)

老孙 v2 §3 IPC schema 没有 nonce_mgr 字段, 这里补:

```
signer 启动:
  1. systemd ExecStartPre 验签 (B4)
  2. KMS unwrap age key, 加载 wallet (B2/B6)
  3. mlock + MADV_DONTDUMP + PR_SET_DUMPABLE=0 (B6)
  4. **NEW**: connect 本机 nonce-mgr UDS, 调 Health() 拿当前 epoch
     拿到 signer_token = HMAC(nonce_mgr_secret, role || epoch)
     若 nonce-mgr 不可达 → abort (不允许无 nonce 仲裁就启动)
  5. listen UDS for trader
  6. 接受 SignRequest 时, allocate nonce 之后再 sign
```

### 7.2 SignRequest 流程 (老孙 v2 §3.2 step 12 nonce 校验) 重写

老孙 v2 写了:
```
12. nonce 校验:
    reserved = nonce_mgr.reserve_nonce(wallet_addr)?
    若 expected_onchain_nonce != reserved → reject (trader 与链上不同步)
```

我这里的改进:
```
step 12 改成:
  12a. let alloc = nonce_mgr.Allocate(wallet_addr, request_id, ts_ns, signer_token).await?;
  12b. 把 typed_data.nonce 字段替换为 alloc.nonce (signer 内部重写, 不信 trader 传的)
  12c. 若 expected_onchain_nonce (trader 传的预期) != alloc.nonce → 警告 + Slack 知会
       但不 reject (trader 可能基于过期 observe 算的 nonce)
  12d. signer 内部以 alloc.nonce 为准, 用它 sign
  12e. SignResponse.used_nonce = alloc.nonce, alloc.lease_id 也透传
       (trader 端 broadcast 后调 nonce_mgr.Commit, 不经 signer)
```

### 7.3 trader 调 commit/rollback 直连 nonce-mgr (不经 signer)

trader 也 connect nonce-mgr UDS, 调 commit/rollback. 这样 signer 只管签名, 不负责 broadcast 后续状态.

```
trader:
  1. sign_resp = signer.Sign(req).await?;
  2. tx_hash = rpc.send_raw_transaction(sign_resp.r||s||v + tx_body).await;
  3. 若 tx_hash 成功 → nonce_mgr.Commit(wallet, sign_resp.used_nonce, sign_resp.lease_id, tx_hash).await;
  4. 若 broadcast 失败 → nonce_mgr.Rollback(wallet, ..., reason).await;
  5. 若 broadcast 超时不确定 → nonce_mgr.Observe(force=true) 看 pending 是否包含
```

trader 拿到 lease_id 才能调 commit/rollback, 防止跨 request 误操作.

### 7.4 老孙 v2 §2.7 `NonceManager` trait 对齐

老孙 v2 写的 Rust trait:
```rust
trait NonceManager {
    fn reserve_nonce(&self, wallet: Address) -> Result<u64>;
    fn confirm_used(&self, wallet: Address, nonce: u64) -> Result<()>;
    fn current_chain_nonce(&self, wallet: Address) -> Result<u64>;
}
```

映射到我这边 gRPC 接口:
- `reserve_nonce` → `Allocate` (返回 nonce + lease_id, 老孙 trait 接口要扩展 lease_id 出参)
- `confirm_used` → `Commit` (要扩展 tx_hash 入参)
- `current_chain_nonce` → `Observe.onchain_latest_nonce`

**请老孙在 v3 trait 加上 lease_id / tx_hash 参数** — 否则我们没法做 idempotency 和 stale 检测.

### 7.5 release_nonce (老孙 v2 §2.7 提到)

老孙 v2 写"trader 上链失败 → trader 显式 `release_nonce`". 这里 release_nonce 即 Rollback. trait 缺这个方法, 请补:

```rust
fn release_nonce(&self, wallet: Address, nonce: u64, lease_id: u64, reason: &str) -> Result<()>;
```

---

## 8. 验收 checklist (sprint-2 启动前)

### 8.1 功能

- [ ] gRPC .proto 编译通过 (Rust signer + C++ trader 双语言生成)
- [ ] Allocate / Commit / Rollback / Observe 单元测试覆盖
- [ ] fencing token 拒签测试 (旧 epoch 拿来 allocate 必须 FENCING_TOKEN_REJECTED)
- [ ] split brain 测试: 强制双 nonce-mgr 都自认 primary, signer 必须拒绝其中一个的 token

### 8.2 性能

- [ ] allocate p50/p99/p99.9 实测 (本机, 1000 RPS 持续 60s)
- [ ] commit / rollback 同上
- [ ] failover 演练: kill signer-A, 测 detection + failover 总 < 1s

### 8.3 故障注入 (chaos drill)

- [ ] Redis kill -9 → 自动切 SQLite-only, 业务不中断
- [ ] SQLite 文件 dd 写损坏 → 启动期自动从 RPC 重建
- [ ] RPC 三路全断 → 进保守模式, allocate 仍可用, rollback 拒绝
- [ ] 网络分裂 → 仅一边能 allocate

### 8.4 监控 (与小郑 S1-018 对齐)

- [ ] metric: `nonce_mgr_allocate_latency_ms{wallet}` histogram
- [ ] metric: `nonce_mgr_commit_latency_ms`
- [ ] metric: `nonce_mgr_rollback_total{reason}` counter
- [ ] metric: `nonce_mgr_gap_count{wallet}` gauge
- [ ] metric: `nonce_mgr_inflight_count{wallet}` gauge
- [ ] metric: `nonce_mgr_onchain_lag{wallet}` gauge (reserved_max - pending)
- [ ] metric: `nonce_mgr_lease_epoch` gauge
- [ ] metric: `nonce_mgr_redis_up` / `sqlite_up` / `rpc_up` gauge

### 8.5 与老孙对齐 (硬阻塞)

- [ ] 老孙 v2 §2.7 NonceManager trait 升级到 v3, 加 lease_id / tx_hash / release_nonce
- [ ] signer 启动时强制 nonce-mgr 握手 + 拿 signer_token
- [ ] signer 内部以 nonce-mgr 分配的 nonce 为准, 不信 trader 传的
- [ ] signer 内部 nonce-mgr 不可达 → 不开始 listen, 拒绝所有 SignRequest

---

## 9. 选型说明 (给老雷的一句话)

**nonce_mgr 选型: Redis 主存 + SQLite 本地 mirror + Polygon RPC 真值对账, gRPC over UDS, lease + epoch fencing**.

不选 etcd/Raft 因为 allocate p99 SLO 1ms 撑不住. 不选纯文件 lock 因为不抗机器宕. Redis 加 SQLite 双层在性能和持久之间是合理折中, 加 RPC observe 做漂移校正, 三层兜底足够防 split brain + 链上状态偏差.

---

## 10. 残留问题

| # | 问题 | Owner | 截止 |
|---|---|---|---|
| N1 | 多 wallet (主备热-热) nonce 隔离是否一对一 nonce-mgr 实例? | 老叶 + 老韩 | Sprint-2 W1 |
| N2 | EIP-1559 replacement tx 抬 gas 时 nonce 不变, nonce_mgr 是否要支持 "Replace" 接口? | 老叶 + 老孙 | Sprint-2 W2 |
| N3 | Polygon Heimdall fork 期 nonce 是否回滚? observe loop 怎么应对? | 老叶 自查 | Sprint-2 W2 |
| N4 | nonce-mgr 自身 high availability (Redis sentinel 还是 keepalived VIP)? | 老吴 + 老叶 | Sprint-2 W1 |
| N5 | gRPC over UDS 还是 raw msgpack? msgpack 更轻但要自实现 idempotency. 老孙偏好? | 老孙 + 老叶 | Sprint-2 W1 |

---

*v1 提交时间: 2026-05-28*
*老孙验收 deadline: 2026-06-12 (Sprint-1 末)*
*老雷 sign-off: 2026-06-26 (Sprint-2 启动)*
