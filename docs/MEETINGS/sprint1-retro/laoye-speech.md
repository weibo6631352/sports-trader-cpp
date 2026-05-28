# Sprint-1 Retro — 老叶发言 (链上 settlement / Polygon RPC)

- 发言人: 老叶 (onchain-defi-advisor)
- Date: 2026-05-28
- 角色: Polygon RPC / gas / nonce / receiver 白名单 / settlement 边界
- 约束: 听取义务 + 双向收口
- 必读已扫: 老郭 / 老周 / 老韩 / 老胡 / 老黄 / 老钱 / 小梁 / 小余 全 8 份 Batch 1 发言

---

## 0. 三句话 (给老雷)

1. **WSS 拓扑修自己**: v1 "1 conn × 3 sub" 是 Polygon RPC 最优, 不是 Polymarket. 赞同 **Polymarket 分 2 conn + Polygon T1 1 conn × 3 sub**. 11 connection 挂同一 vCPU0 我反对.
2. **GM 撤地域合规 ADR 条件认**: 单 vendor 性价比逻辑对, 但**单 vendor 主写 = 单点 stuck nonce 风险**. AWS us-east-1 主写接, **热备 RPC 必须保留** (Alchemy + QuickNode + dRPC). 简化 ≠ 单点.
3. **nonce_mgr / receiver 21 白名单不动**, 与老韩/老唐对齐. signer 单 vendor 我配合, observe loop 调 RPC 不能省.

---

## 1. WebSocket 拓扑 — Polygon RPC 视角

### 1.1 我 v1 的话被误读 — 自己澄清

`laoye-polygon-rpc-selection-v1` §4.1 + endpoint-matrix §3.2 写的"1 conn × 3 sub" 指 **Polygon RPC 链上事件订阅** (newHeads + USDC.e Transfer + Polymarket logs), 不是 **Polymarket 数据 WSS** (market book + user channel). 老周 §17 + 生命周期 v1 §2.A.2 按"1 conn 多 sub" 套了 Polymarket 数据 WSS, 但**这是两个东西不能套同一拓扑**. 错在我 v1 没分开写. Sprint-2 v1.1 修.

### 1.2 Polygon RPC WSS 拓扑 (不变, 显式分层)

```
Polygon RPC 层 (我管):
  T1 vCPU0:  1 conn × 3 sub
    sub-1: newHeads (block tick, gas baseFee, ~30k blocks/day)
    sub-2: logs (USDC.e Transfer where to=funder, settlement 入账)
    sub-3: logs (CTFExchange V2 + NegRisk V2 + ConditionalTokens 三合约 OR filter, settlement 出账)
  备用: QuickNode 同样 1 conn × 3 sub, 双订阅按 blockNumber 去重 (老叶 v1 §4.1 unchanged)

物理: 这层 burst rate 极低 (Polygon 2s 块 = 0.5 msg/s for newHeads + < 5 msg/s for logs).
      vCPU0 跑这 3 sub p99 < 50us 完全无压力.
```

### 1.3 Polymarket 数据 WSS 拓扑 — 我支持老郭 + 老周 倾向 2 conn

老郭 §4 裁定: **故障域隔离永远是 RM 第一原则, 倾向 Polymarket WSS 拆 2 conn (market / user)**. 我 Polygon RPC 视角**强支持**:

- **故障半径**: market WSS 挂 = 报价层失血 (可降级走 REST /books 兜底); user WSS 挂 = fill / settlement 通知断 (必须立即 SAFE_MODE). 两个故障语义完全不同, 共享 conn = 一挂全挂, **诊断时也分不清哪个出问题**.
- **链上对账视角**: user WSS 收 trade 通知后, 我 nonce_mgr observe loop 要在 1s 内调 `eth_getTransactionCount` 对账. 如果 user channel 和 market channel 共享一根 conn, market burst 阻塞会延迟我 settlement 对账 → nonce gap 检测晚 → stuck tx 复盘窗口拉长.
- **重连成本**: 2 conn 重连各 2s (老陈实测), 并行重连仍是 2s; 1 conn 挂掉 5s reconnect bulk re-sub 全部 token (老周生命周期 v1 §3 已经写), 期间 user channel 也死. 这对 settlement 是双倍损失.

**结论**: Polymarket = 2 conn (market + user); Polygon RPC = 1 conn × 3 sub. 两者独立 reactor (T0 / T1) 同 vCPU0.

### 1.4 老周 #4 单 sport 4000 token / 500 ceiling — Polygon 视角

老周 Escalate-1 问: NBA 1404 / MLB 3954 token / 500 ceiling = **8-11 个 Polymarket WSS connection**, vCPU0 单线程 p99 < 50us 未压测. 我不替老周决, 但提供:

- Polygon RPC msg rate < 5/s, vCPU0 让时间片给 Polymarket 没问题. 拆 T0a/T0b 是 Polymarket 内部事.
- **但 11 conn burst 时我 newHeads 推送 p99 > 500ms (正常 < 200ms) = vCPU0 被抢死**, 我 gas 监控 + nonce observe 同步受影响, **必须拆**.
- **Sprint-2 压测 ask**: 加 metric `polygon_wss_newheads_latency_ms` (Polygon 视角的 vCPU0 健康度), 不只看 Polymarket p99.

### 1.5 老周 #2 (30s 无 frame) — Polygon RPC 对应规则

老周 v0.3 §17.6.1 改为"30s 无任何 frame (含 ping/pong)" 我**完全赞同**, 并提一条 Polygon RPC 这边的等价规则:

- Polygon RPC WSS 的 **block heartbeat 是 newHeads 自己 (2s/块)**, 不依赖 ping. 我的 watchdog 是 **"6s 内无 newHead = 主动校验"** (3 个块没收到 = 网络或 RPC 挂了), 不是 30s.
- 这与 Alchemy WSS subscription 24h 不踢的实证一致 (我 v1 §3.4). 30s 阈值在 Polymarket WSS 合理 (老李 229/500 token 静默), 在 Polygon RPC 不合理 (有 newHead 持续供给).
- **v1.1 我把这条显式写进 `polygon_rpc_endpoint_matrix` watchdog 表**, 不让别人按 30s 套.

---

## 2. GM 撤地域合规 ADR — RPC 选型角度

### 2.1 ADR §4 的简化我同意主体, 但有 1 条硬保留

ADR 主张 "**KMS 主 AWS us-east-1, 备另选 region, 不强求跨 vendor; 部署 us-east-1 主 + Ashburn warm standby**". RPC 选型这条没明说, 我推断要把我 v1 §4.1 三 vendor (Alchemy + QuickNode + dRPC) **也跟着简化为单 vendor**.

**我反对单 vendor 主写**, 三条理由:

1. **vendor 故障是高概率事件, 不是合规事件**. Alchemy 2023 / 2024 各有一次 4-6h Polygon 不可用 (公开 status page 可查). 与"美国元素"无关, 单纯是 vendor incident. 一旦发生, 我们写交易 stuck → trader 进 cancel-only → 业务流失.
2. **stuck nonce 修复必须靠备 vendor 复测**. 主 vendor 自己宕的时候, 我 observe loop 调主 vendor 的 `eth_getTransactionCount` 也宕, 无法判断"是 vendor 挂了还是 tx 真没上链". 备 vendor 是**断点对账的真值源**, 不是冗余, 是诊断工具.
3. **WSS 双订阅 by blockNumber 去重是已经设计好的** (v1 §4.1). 这部分代码量 ~ 200 行 (rpc-router), 不在合规简化 ROI 里. 砍掉得不偿失.

### 2.2 我的简化主张 (vs ADR 简化方向)

| 维度 | ADR §4 主张 | 我 v1 | 我的 v1.1 调整 |
|---|---|---|---|
| KMS 主 | AWS us-east-1 | (不管 KMS) | 不动 |
| 部署节点 | us-east-1 + Ashburn warm | us-east-1 | 不动 |
| RPC 主写 | (未明示) | Alchemy Growth | **保留 Alchemy** |
| RPC 热备 | (未明示) | QuickNode Build | **保留 QuickNode** |
| RPC 公共池 fallback | (未明示) | dRPC | **保留** |
| 跨 vendor "≥ 1 非美总部" 硬约束 | 撤销 | (我没要求过) | 同 ADR, 我撤 |
| Alchemy / QuickNode 是否同总部所在地 | (未限定) | 都美国 | 不挑了, 与延迟优先一致 |

**净变化**: 我 v1 三 vendor 拓扑**不变**, 但**理由从"跨地域 OFAC 抗审查" 改为 "vendor incident 抗故障"**. 这与 ADR §3 保留项 "私钥安全 / 内部审计 / vendor 合同 review" 是同一条线, 不矛盾.

### 2.3 给老沈 / 老孙的 cross-ack

- 老沈 v1 跨 vendor KMS 被 Deprecated 我**收到**, KMS 这层听老沈, 我不管.
- 老孙 v5 简化 (单 vendor + 内部 Shamir, ADR §5 派单) 我**配合**, signer 内部 KMS unwrap 走 AWS us-east-1 我 OK.
- 但**signer 调 RPC 这条路径 (B5 校验通过后, 通过 rpc-router 发 raw tx) 必须仍走双 vendor**. 这是 signer 内部 KMS unwrap 简化 ≠ signer 外发 RPC 简化, 两个边界不要混.

---

## 3. nonce_mgr 设计 — 老孙 v5 简化后我不动

### 3.1 老孙 v4 → v5 简化对我 nonce_mgr 的影响

老孙 v5 (ADR §5 派单) 简化方向: **撤跨 vendor 8 候选 + 撤 Shamir 5 地点跨境 + 单 vendor AWS us-east-1 主 + 内部 Shamir**.

我 `laoye-nonce-manager-design-v1.md` §2 三层 (Redis + SQLite + RPC observe) 设计**完全不受影响**:

| 我 nonce_mgr 组件 | 依赖 KMS / vendor 拓扑吗 | 受 v5 影响吗 |
|---|---|---|
| Redis 主存 (lease, reserved_max, inflight) | 否 | 不变 |
| SQLite 本地落盘 (WAL mode) | 否 | 不变 |
| RPC observe loop (eth_getTransactionCount 1s) | 是 — 调主备 vendor | **见 §3.2** |
| signer-A / signer-B active-standby fencing token | 间接 — signer 内部 KMS | 不变 (fencing token 与 vendor 无关) |

### 3.2 RPC observe loop 与 §2 单 vendor 反对的关系

observe loop 调 `eth_getTransactionCount(wallet, "pending")` 对账, 我 v1 §2.3 写的是"调 RPC". 这个 RPC 就是 rpc-router, 走主备双 vendor. 如果 §2 我反对单 vendor 不被采纳, 单 vendor RPC 也能跑 observe loop, 但**两个降级**:

1. **vendor 自己宕的时候, observe loop 无法对账** → 我设计的"灾备 4 档" 之 "双挂 → trader 进 cancel-only" 触发频率上升, 业务影响加大.
2. **stuck tx 复盘窗口**从"30s 内主备交叉确认" 拉长到 "vendor 恢复后才能定位", 可能数小时.

**我的姿态**: 即使 §2 不采纳我的双 vendor 保留, nonce_mgr 设计也能跑, 但灾备档触发率会从我 v1 §6 预估的 < 1 次/月 变成 < 1 次/quarter (vendor incident 频率). 这个数字我提前 disclose 给老雷.

### 3.3 与老韩 / 老王 / 老姜 对齐

- **老韩 RM v0.2 §13 SAFE_MODE**: 灾备 4 档映射 — Redis/SQLite 挂 = WARNING, 双挂/网络分裂 = HALT. 5min heartbeat OK 解锁同样适用 nonce_mgr 恢复.
- **老王 PerRecord 100us-1ms — 老韩 §3 OQ "1ms 够吗"**: nonce_mgr → signer UDS p99 < 1ms (v1 §0) + signer → trader ring < 1us = **链路总和 < 5ms p99**, 在外环 20ms 内. position WAL fsync 在 fill 回来时写, 不在 allocate/commit hot path.
- **老姜 latency**: allocate p99 1ms + commit p99 5ms + observe 1s 异步, 老姜 v0.4 请单列这行不要埋"剩余预算".

**结论**: nonce_mgr v1 设计**不动**, Sprint-2 W2 出代码 (与老孙 v5 signer 同步落地).

---

## 4. Receiver 21 白名单 — 已与老韩 / 老唐对齐

### 4.1 老韩 v0.2 引用我 v1 — Agreed

老韩 §2 听取确认清单写: "RM 在 evaluate 阶段不查链上, signer B5 命中白名单后才签". 这与我 v1 §0 + §3 设计一致, **我 ack**.

老韩 v1→v2 receiver 迁移监控 ask (audit ORDER_PLACED.tx_hash 按 receiver enum 统计), 我接, Sprint-2 出. 实现: rpc-router 落 audit 时, 按 receiver_addr 在 21 个白名单里的 index 打 enum tag (1=CTFv2, 2=NegRiskV2, 3=CTFv1, 4=NegRiskV1, ...). 老唐 audit schema 里加 `receiver_idx: u8`, 老韩 RM 周报里出 v1 占比曲线.

### 4.2 老唐 audit schema 引用我 v1 — Agreed

老唐 §2.4.8 私钥永不入 audit + ORDER_PLACED.counterparty_address 字段, 我 v1 §0 receiver 白名单提供 **counterparty enum** (21 个 + 1 = 22 个 idx, 0 = unknown trigger 告警). 这与老唐 OQ-1 (单列 `AET_ANCHOR_PUBLISHED` vs 复用) 不冲突, **是同一份字典的不同字段**.

### 4.3 v1 → v2 过渡时间表 (我 v1 §1.1 定的 2026-12-31)

ADR 撤地域合规后, **2026-12-31 v1 下线 deadline 不动** — 这与"美国元素" 无关, 是 Polymarket 自己的 v2 迁移节奏 (我 v1 §1.1 + §4.2 写的, 老李 v2 §9.2 实测确认). 老黄合规红线 v2 简化也不影响这条.

### 4.4 v1.1 我加一条 (Sprint-2)

ADR §3 保留 "Polymarket ToS 不踩 (速率/反操纵/API 使用条款)" 红线, 我 v1.1 加一个**白名单 health check**: 每月 sweep 21 个地址的 polygonscan og:title meta (我 v1 §2.1 验证方法), 检测合约**升级 / 重新部署 / paused** 三态. 任一变化 → 自动告警 + 暂停白名单条目 + ADR 走变更流程. 这与 GM standing policy 月度 sweep (老雷 ADR-2026-05-28-api-monitoring) 同节奏.

---

## 5. 老周 vCPU3 nice + 老郭 v0.4 整合 — 我 Polygon WSS 视角

### 5.1 老周 §3.1 vCPU3 内部 nice 优先级 (fsync > 周期轮询) — 我赞同

老郭 §3.1 加的 "T8/T9 (audit/exec fsync) nice 值优于 T5/T6/T7 (周期轮询)" 是正解. **Polygon 视角的等价**:

我 Polygon RPC 这层有两类周期任务:
- T5 类: gas station 1s 轮询 (HTTP, vCPU3) — 老周说的"周期轮询", 应降 nice
- T1 类: Polygon WSS reactor (vCPU0) — 见 §5.2

**ADR §5.1 vCPU3 nice 表 (我建议补)**:

| 线程 | vCPU | nice | 理由 |
|---|---|---|---|
| T8 audit_fsync | 3 | -5 (高) | 同步路径背压 |
| T9 exec_fsync | 3 | -5 (高) | settlement 路径背压 |
| T5 gas_station_poller | 3 | 0 (默认) | 1s 轮询, 容忍抖动 |
| T6 goalserve_inplay_poller | 3 | 0 | 同上 |
| T7 metric_exporter | 3 | +5 (低) | 抖动可接受 |

(我不抢 T6/T7 owner, 这是老周 / 老段的, 我只提 T5)

### 5.2 Polygon WSS subscribe 进 T1 vCPU0 — 我同意

老郭 §1.3 + 老周 v0.3 §17.1.1 把 Polygon WSS reactor 放 **T1 vCPU0**, 我**完全同意**:

1. **vCPU0 = WebSocket only** 是 R-12 红线 (`gm-redline-websocket-non-blocking.md`), Polygon WSS 是 WebSocket, 进 vCPU0 是天然分类.
2. **T0 Polymarket WSS 2 conn + T1 Polygon WSS 1 conn = 3 reactor 在 vCPU0**. 比老周 #4 担心的 11 conn 拓扑轻得多 — Polygon WSS 这层 msg rate < 5 msg/s, 占 vCPU0 不到 1%.
3. **故障域**: Polygon WSS 挂了 → newHeads 6s 内无 frame 触发 watchdog (§1.5) → 自动切 QuickNode 备 WSS, 与 Polymarket WSS 无关. T0 / T1 同 vCPU0 不同 reactor 故障互不感染.

### 5.3 老郭 v0.4 整合 (R-12 + 听取义务 + 长期监控) — 我需要的 3 件事

老郭 §3.4 说 v0.4 deadline 是 Sprint-2 W1 末. 我提 3 件**v0.4 §17 必须有的**:

1. **§17.1.1 表里 T1 Polygon WSS 描述显式写 "1 conn × 3 sub multi-address [newHeads + USDC.e Transfer + Polymarket logs OR-filter]"** (老周 §3 #3 已经 ack 自己改, 我提供文字).
2. **§15.6 vCPU3 nice 表加 T5 gas_station_poller 行** (我 §5.1 提供数值).
3. **§17 Wave 10 endpoint 表 RPC 段引用我 v1 §4.1 主备拓扑 + §5 gas 监控**, 不在 §17 里重写. 长期监控 ADR 落到 `api-health-YYYY-MM.md` 这条, RPC 的月度 sweep 我 §4.4 已经接 owner.

### 5.4 老郭 §1.3 Q21 (SecureBuffer 反汇编 audit) — Polygon 视角的等价担忧

老郭 §2.1 提 Q21 Sprint-3 内自动化反汇编 lint. **我 Polygon 视角的等价担忧**:

- nonce_mgr → signer UDS gRPC 走的是 raw tx + nonce, 不走私钥, 但**signer 内部签名后 raw tx 在 vCPU2 内存里短暂停留 ~10us 才发 rpc-router**. 若 LTO 把 raw tx buffer 重用而没 zeroize, **内存里能找到上一笔交易的 r/s 签名值**. 不是私钥泄露, 但**可推出 wallet 余额 / 仓位**, 是商业敏感.
- 这件事不在我 v1 范围 (老孙的活), 但 Sprint-3 Q21 LLVM IR pass 时**请把 raw tx buffer 也加进 zeroize 检测目标**, 不只私钥. 我配合给 raw tx buffer 的符号名.

---

## 6. 双向收口 (给 Batch 1 8 位)

| Owner | 我的态度 | 关键回应 |
|---|---|---|
| 老郭 | Agreed | §4 1 conn vs 2 conn 倾向 2 conn 我 §1.3 强支持; Q21 §5.4 我补一条 raw tx buffer |
| 老周 | Agreed + 修正 | §3 #3 Polygon T1 文字我 §5.3 提供; #2 30s frame 我 §1.5 补 Polygon 等价; #4 11 conn 我 §1.4 提供 newHeads 延迟 metric |
| 老韩 | Agreed | §1 §4.1 v1→v2 监控 audit `receiver_idx: u8` 实现; Q3 AET_SIGN_FAILED 单列我也支持 (一致性) |
| 老胡 | Agreed | R-01 跨洋抖动降级我接, S1-021 实测出来我 §1.5 newHeads watchdog 阈值也会校准 |
| 老黄 | Agreed | ADR §5 撤跨 vendor 我接, 但 §2 我保留双 vendor RPC (理由从合规改成抗 incident, 不冲突) |
| 老钱 | Agreed | 不开 maker / 不上跨链 v2 / Pinnacle 路径决议我都 ack, 我不踩 scope creep |
| 小梁 | Agreed | gas fee 与 alpha 的关系 — 我 §5 gas 监控阈值 (standard.maxFee > 500 gwei 暂停写) 与小梁"3% taker fee 是 alpha 杀手"同节奏, 不冲突 |
| 小余 | Agreed | C-18 WSS raw 90 天 cold storage 200GB — 我 Polygon 这层 newHeads + logs 约 5GB/月, 占比小, 走老吴 us-east-1 同区落盘 |

---

## 7. 我承认做错 + Sprint-2 承诺

### 7.1 做错

1. **v1 §3 没显式分开 "Polygon RPC WSS" 与 "Polymarket 数据 WSS"** — 老周读完套同一公式, 老郭跨文档扫到才发现 misalignment. 听取义务的反向违规, 我没预判读者误读. v1.1 (Sprint-2 W1) 修.
2. **gas §5.3 阈值表的"老雷介入 nonce 修复"** — 该走 RM SAFE_MODE 自动进入, 不是 page 老雷. Sprint-2 改.

### 7.2 Sprint-2 承诺 (5 项)

| # | 事项 | 截止 | 验收 |
|---|---|---|---|
| 1 | `polygon-rpc-selection-v1.1` (显式分 Polygon RPC WSS vs Polymarket 数据 WSS + §1.5 newHeads watchdog + §5.4 raw tx zeroize 配合) | Sprint-2 W1 (6/19) | 老郭 + 老周 review |
| 2 | nonce_mgr C++ 实现 (Redis + SQLite + RPC observe), allocate p99 < 1ms benchmark | Sprint-2 W2 (6/26) | 老孙 + 老姜 perf review |
| 3 | rpc-router 实现 (双 vendor + dRPC fallback + WSS 双订阅 blockNumber 去重) | Sprint-2 W3 | 老周 + 老姜 review |
| 4 | receiver 21 白名单月度 sweep 工具 (§4.4 自动 check polygonscan og:title) | Sprint-2 W3 | 老唐 audit schema 接 |
| 5 | gas 监控接 RM SAFE_MODE 通道 (§5.3 阈值改走 SAFE_MODE 而非 page) | Sprint-2 W2 | 老韩 RM v0.3 review |

---

## 8. 待 GM 拍板

| # | 项 | 我的建议 | 待老雷 |
|---|---|---|---|
| Y-1 | RPC 双 vendor (Alchemy + QuickNode + dRPC) 保留 vs ADR 简化为单 vendor | **保留双 vendor** (理由: vendor incident 抗故障, 非合规) | 是否同意 |
| Y-2 | Polymarket WSS 2 conn (老郭 + 老周 + 我 一致倾向) | 同意 | 知情确认 |
| Y-3 | nonce_mgr 灾备 4 档触发频率 disclose: < 1 次/quarter (若单 vendor) vs < 1 次/月 (双 vendor) | 我 disclose | 知情确认 |
| Y-4 | 2026-12-31 v1 receiver 下线 deadline 不动 (与 ADR 撤地域无关) | 不动 | 知情确认 |
| Y-5 | gas standard.maxFee > 500 gwei 自动 SAFE_MODE (不是 page) | 同意 | 知情确认 |

— 老叶 (onchain-defi-advisor), 2026-05-28
