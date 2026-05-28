# Polygon RPC 选型 + Gas 监控方案 v1

- Owner: 老叶 (onchain-defi-advisor)
- Last review: 2026-05-28
- Last measured: 2026-05-28 13:33 UTC (data: `laoye-polygon-rpc-probe-20260528-133338.txt`, vendor 选型沿用同一 probe)
- 验收人: 老孙 (security-cryptography-engineer)
- 关联 ticket: S1-009
- 关联交付物: 老吴 S1-010 (跨洋部署), 老孙 S1-005 (HSM/KMS), 老彭 S1-012 (sharp money)

---

## 0. TL;DR

- **主链路:** Alchemy Growth 套餐 (Polygon mainnet, US-East region) — 主写 + 主读
- **备链路:** QuickNode Build 套餐 (Polygon mainnet, US-East) — failover + WSS 备份
- **降级链路:** dRPC (Polygon, multi-region anycast) — 公共池兜底
- **gas 价格源:** Polygon Gas Station v2 (`https://gasstation.polygon.technology/v2`) 1s 轮询 + Alchemy `eth_maxPriorityFeePerGas` 交叉验证
- **暂不自建节点** (Sprint-1 阶段 ROI 不成立, 详见 §2.4); Sprint-3 重审

---

## 1. 业务约束 (为什么 RPC 选型很关键)

| 维度 | 约束 | 来源 |
|---|---|---|
| 读延迟 | order placement 走链下 CLOB, 链上读主要是 `balanceOf` / `allowance` / `nonce` / `proxy wallet` 状态 | 老李 S1-002 |
| 写延迟 | settlement / approve / deposit / withdraw — 单笔可容忍 5-15s, 但 nonce 不能卡 | 老周 S1-001 |
| 调用量 | MVP 阶段 < 50 写交易/天, 但读 > 100k req/天 (balance/allowance polling + receipt 轮询) | 老钱 S1-008 (MVP scope) |
| WSS | newHeads + logs (USDC.e Transfer, NegRiskCtfExchange events) 需要稳定订阅, 断线重连必须无声 | 老周 |
| 跨洋 | 主节点候选美东 (老吴 S1-010), 距 Polygon 验证人主集群 < 50ms | 本文 §6 |
| 资金安全 | RPC 不能拿到私钥 (老孙 HSM 本地签名), 但 RPC 能审查/延迟我们的交易 | 老孙 S1-005 |

---

## 2. RPC 拓扑选项对比

### 2.1 公共 RPC (polygon-rpc.com / chainlist 池)

- **优势:** 0 成本, 0 鉴权
- **劣势:** 限流不可预测 (10-25 RPS, 高峰被降级), archive 不保证, WSS 经常踢, 节点版本碎片化 (eth_call 结果偶有不一致)
- **判定:** 仅作为 chainlist fallback, 不挂主链路, 不挂监控

### 2.2 私有托管 RPC (Alchemy / QuickNode / Infura / Blast / Tenderly / dRPC)

主选方向. 详见 §3.

### 2.3 自建 Bor + Heimdall (full / archive 节点)

- **硬件:** Bor full 节点目前 chain size ~3.5TB (2026Q2 估计 4TB+), NVMe + 32GB RAM + 8 vCPU; archive 节点 12TB+
- **运维成本:** Heimdall + Bor 双进程, snapshot sync 12-24h, 升级窗口需 fork 协调
- **Hetzner AX52 / AX102 月费 60-130 EUR, 但 NVMe 单盘 4TB 上限, archive 需要 RAID0 多盘**
- **判定:** Sprint-1 不做. ROI 阈值 (老钱 MVP scope): 当 RPC 月费 > 800 USD 或写交易量 > 500/day 时重审

### 2.4 Erigon (Polygon 模式)

- **优势:** 同步快 (12-18h), 存储省 (full ~1.8TB, archive ~5TB), 多线程友好
- **劣势:** Polygon 适配相对滞后, 重组期偶有问题, 社区支持比 Bor 少
- **判定:** 自建路线时优先 Erigon over Bor, 但仍是 Sprint-3 议题

---

## 3. 主流服务商对比 (2026-05 询价 + 公开文档)

### 3.1 价格 / 限流

| 厂商 | 套餐 | 月费 USD | CU/月 | 折合 RPS (粗算) | archive | WSS | 多区域 |
|---|---|---|---|---|---|---|---|
| Alchemy | Free | 0 | 300M CU | ~25 RPS | 是 | 是 | US/EU/AP |
| Alchemy | Growth | 49 | 1.5B CU | ~120 RPS | 是 | 是 | US/EU/AP |
| Alchemy | Scale | 289 | 12B CU | ~900 RPS | 是 | 是 | US/EU/AP |
| QuickNode | Free | 0 | 10M req | ~10 RPS | 否 | 是 (限连接) | US-East default |
| QuickNode | Build | 49 | 80M req | ~30 RPS | 加购 +99 | 是 | 多区域 |
| QuickNode | Accelerate | 249 | 450M req | ~170 RPS | 加购 | 是 | 多区域 |
| Infura | Core | 0 | 6M CU/天 | ~70 RPS | 加购 | 是 | US/EU |
| Infura | Developer | 50 | 15M CU/天 | ~175 RPS | 加购 | 是 | US/EU |
| Blast (Bware) | Free | 0 | 1M req/天 | ~12 RPS | 否 | 是 | EU-heavy |
| Blast Build | 50 | 100M req/月 | ~38 RPS | 是 | 是 | EU/US |
| Tenderly | Free | 0 | 25M Units | ~10 RPS | 是 (gateway) | 是 | US/EU |
| Tenderly | Dev | 50 | 75M Units | ~30 RPS | 是 | 是 | US/EU |
| dRPC | Free | 0 | 公共池 | ~30 RPS soft | 部分 | 是 | anycast (实测就近) |
| dRPC | Growth | 49 | 30M CU | ~50 RPS | 是 | 是 | anycast |

> CU 折算口径不统一 (Alchemy CU vs Infura CU vs Tenderly Units 单价不同). 上表 RPS 为 "稳态混合负载" 粗估, 用于横向感知, 不作 SLA.

### 3.2 延迟 (US-East 客户端实测 p50/p99, 单位 ms; 数值为同行公开 benchmark + dRPC 自家面板, Sprint-1 内由老吴 S1-010 复测)

| 厂商 | p50 (eth_call) | p99 | WSS 推送抖动 |
|---|---|---|---|
| Alchemy | 35 | 110 | < 200ms |
| QuickNode | 40 | 130 | < 250ms |
| Infura | 55 | 180 | < 400ms |
| Blast | 70 | 250 | 不稳 |
| Tenderly | 60 | 200 | 中等 |
| dRPC | 50 | 220 (跨池跳跃) | 中等 |

### 3.3 archive / trace / debug

- 必须需要: `eth_getTransactionReceipt`, `eth_getLogs` 跨日范围
- 加分项: `debug_traceTransaction` (settlement 失败复盘), `trace_replayTransaction`
- Alchemy / Tenderly / QuickNode(加购) 三家 trace 完整; Infura 限制较多; Blast/dRPC 视区域

### 3.4 WSS 稳定性 (按踢线率 + 重连成功率)

- Alchemy: 稳, subscription 持续 > 24h 常态 OK
- QuickNode: 中, 偶尔每 6-12h 自动 rotate, 客户端要会无声重连
- Infura: 历史口碑差, 但 2025 改造后改善
- Blast / dRPC: WSS 不建议作主

### 3.5 治理 / 隐私 / 抗审查

- Alchemy / Infura: 美国公司, 受 OFAC/制裁地址过滤 (Polymarket 美国合规问题与我们 Polygon 写交易关系不直接, 但要警惕)
- QuickNode: 美国, 同上
- Tenderly: 捷克, 相对中立
- dRPC: 去中心化路由, 节点池多元, 但单点质量不可控

---

## 4. 推荐方案

### 4.1 主备拓扑

```
        +-------------------------+
        |  trade-engine (US-East) |
        +-----------+-------------+
                    |
        +-----------v-------------+
        |  rpc-router (本地代理)   |  <-- 老叶定接口, 老姜实现 (S1-011)
        +--+----------+--------+--+
           |          |        |
        primary    secondary  fallback
        Alchemy    QuickNode  dRPC
        (写+读)    (热备读+   (公共池)
                   WSS 备份)
```

- **路由策略**
  - 写交易 (eth_sendRawTransaction): primary, 失败立即 secondary 同 nonce 重发 (老孙签名层支持 idempotent)
  - 读 (call/getLogs/getBalance): primary 默认, 5xx / timeout >2s 切 secondary; 10s 内 3 次连续失败切 fallback
  - WSS (newHeads / logs): 主用 Alchemy, 心跳 30s 失败 → QuickNode 备 WSS 顶上, 双订阅时按 blockNumber 去重
  - 健康探测: 每 10s `eth_blockNumber` + 与 secondary 对比, 落后 > 3 block 视为降级

### 4.2 配额预算 (MVP 阶段)

- 读 polling: balance/allowance 5s 一次 × 5 contract = 86k req/天 ≈ 2.6M req/月
- WSS: newHeads (~30k blocks/day Polygon) + filtered logs, 不计入 RPS
- 写: < 50 tx/day × (gas estimate + sendRaw + receipt poll 3x) ≈ 250 req/day
- **结论:** Alchemy Growth (49 USD) + QuickNode Build (49 USD) = **98 USD/月**, 在老钱 MVP 预算内 (< 500 USD/月 infra)

### 4.3 凭证 schema (待 .env 增补)

需老孙审过后入 `.env.example`:

```
POLYGON_RPC_PRIMARY_HTTPS=https://polygon-mainnet.g.alchemy.com/v2/<key>
POLYGON_RPC_PRIMARY_WSS=wss://polygon-mainnet.g.alchemy.com/v2/<key>
POLYGON_RPC_SECONDARY_HTTPS=https://<sub>.polygon-mainnet.quiknode.pro/<key>/
POLYGON_RPC_SECONDARY_WSS=wss://<sub>.polygon-mainnet.quiknode.pro/<key>/
POLYGON_RPC_FALLBACK_HTTPS=https://polygon.drpc.org
POLYGON_CHAIN_ID=137
```

---

## 5. Gas 监控方案

### 5.1 数据源 (三源交叉)

1. **Polygon Gas Station v2** — `GET https://gasstation.polygon.technology/v2`
   - 返回 `safeLow / standard / fast` 的 `maxPriorityFee` + `maxFee` (Gwei)
   - 轮询 1s, 公共免费, 抖动可接受
2. **Alchemy `eth_maxPriorityFeePerGas`** — RPC 派生, 与 (1) 交叉验证, 偏离 > 50% 报警
3. **本地 EIP-1559 估算** — 取最近 N=20 block 的 `baseFeePerGas`, 预测下一 block (Polygon 2s 块, 容忍度高)

### 5.2 EIP-1559 策略 (Polygon 已激活 EIP-1559, baseFee 销毁)

- `maxPriorityFeePerGas`: 取 Gas Station `fast.maxPriorityFee` × 1.1 (10% buffer)
- `maxFeePerGas`: `2 × pending baseFee + maxPriorityFeePerGas` (经典公式, 容忍 2 个 block 的 baseFee 翻倍)
- **下限保护:** Polygon 治理要求 priority >= 25 gwei (2024 后), 任何低于此值的 estimate 直接拉到 25
- **上限熔断:** 当 standard.maxFee > 500 gwei, 暂停所有非紧急写交易, 报警老雷

### 5.3 告警阈值

| 信号 | 阈值 | 动作 |
|---|---|---|
| Gas Station 不可达 > 30s | — | warn, 切 RPC 估算 |
| `fast.maxPriorityFee` > 100 gwei | 持续 60s | warn |
| `standard.maxFee` > 500 gwei | 任意 | crit + 暂停写 |
| baseFee 单 block 翻倍 | — | info |
| 我们的 tx pending > 90s | — | warn, 准备 replacement tx (同 nonce, gas × 1.2) |
| pending > 300s | — | crit, 老孙介入 nonce 修复 |
| RPC primary/secondary blockNumber 偏差 > 3 | — | warn, 路由降级 |

### 5.4 Metrics (导出给小郑 Prometheus, S1-018)

- `polygon_gas_price_gwei{tier="safelow|standard|fast",field="priority|max"}`
- `polygon_base_fee_gwei`
- `polygon_block_number{source="primary|secondary|fallback"}`
- `polygon_rpc_latency_ms{source,method}` (histogram)
- `polygon_rpc_errors_total{source,code}`
- `polygon_pending_tx_count` / `polygon_pending_tx_age_seconds`
- `polygon_wss_reconnects_total{source}`

---

## 6. 区域亲和性 (与老吴 S1-010 对齐)

- Polygon Bor 验证人地理: 主集中在 US (Ankr/Figment), EU (Chorus One), AP 少
- Alchemy / QuickNode 默认 edge 在 us-east-1 / us-east-2
- 主节点选址若在美东 (老吴 推荐 §见 S1-010), RPC 边缘 RTT 可压到 5-15ms
- 若主节点选欧洲 (Hetzner), 应切到 EU edge (Alchemy 支持 region pin via API key 配置), 但 Polygon 验证人多在美, EU edge → 美国验证人仍要绕一跳
- **结论:** RPC 区域跟主节点走, 由老吴定主节点位置后, 由老叶请 Alchemy/QuickNode region pin

---

## 7. 接力点

### 7.1 → 老吴 (S1-010 跨洋部署)

- 主节点选址后, 老叶向 Alchemy/QuickNode 申请同区域 API key
- 老吴提供出口 IP 段给 RPC 厂商做 IP allowlist (Alchemy / QuickNode 都支持)
- 老吴在主节点装 `rpc-router` 本地代理 (老姜实现), 避免每个进程各自连 RPC

### 7.2 → 老孙 (S1-005 私钥管理)

- 老叶不接触私钥. 签名后的 raw tx 通过 rpc-router 提交
- 老孙的 nonce manager 要订阅 rpc-router 的 `polygon_pending_tx_age_seconds` 决定是否 replacement
- gas estimate 由老叶模块出, 老孙签名时复核 (max gas cap 校验)

### 7.3 → 小郑 (S1-018 Prometheus)

- 老叶提供 metrics endpoint (HTTP /metrics), 小郑配 Grafana dashboard "Polygon Gas + RPC Health"
- 告警走 Alertmanager → 老雷飞书

### 7.4 → 老姜 (S1-011 lock-free)

- rpc-router 设计需要的并发模型: 多 reader 共享 secondary connection, 写交易串行化 (nonce 顺序)
- 老叶提供接口契约 (`rpc::Client`), 老姜定无锁队列实现

### 7.5 → 老彭 (S1-012)

- gas 突涨与 sharp money 入场可能同时, 老彭可订阅 `polygon_base_fee_gwei` 作信号

---

## 8. 风险登记

| Risk | 概率 | 影响 | 缓解 |
|---|---|---|---|
| Alchemy 美国合规事件影响我们账户 | 低 | 高 | 双厂商 + dRPC 兜底; 切换演练 月 1 次 |
| Gas Station 公共 API 限流 | 中 | 中 | 1s 间隔 + RPC 派生交叉; 失败容忍 |
| WSS 静默断线 (TCP keepalive 不踢但消息停) | 中 | 高 | 双 WSS + blockNumber heartbeat 主动校验 |
| Polygon 硬分叉升级期 RPC 抖动 | 周期性 | 中 | 升级窗口前 24h 全量演练, 提升 fallback 权重 |
| 自建节点 ROI 转正后未及时切 | 低 | 低 | Sprint-3 评审定 |

---

## 9. 验收清单 (老孙 review)

- [ ] 三源 RPC 都能从美东主节点 200 OK (待老吴主节点 ready)
- [ ] WSS 24h 持续 newHeads 推送 < 3 次断线
- [ ] gas station 1s 轮询 SLA > 99%
- [ ] failover 演练: 主断, 2s 内 secondary 接管
- [ ] metrics 导出且 Grafana 面板就绪 (与小郑 S1-018 对齐)
- [ ] 凭证 schema 入 .env.example (老沈 S1-016 review)

---

## 附录 A: 不选自建节点的量化理由 (Sprint-1)

- 写交易 50/day, 读 100k/day → Alchemy Growth + QuickNode Build = 98 USD/月
- 自建 Bor full + Heimdall: Hetzner AX102 (130 EUR) × 2 (主备) + 流量 = ~300 EUR/月, 加运维半人月 (老吴 不愿背, 老雷不批人头)
- **ROI 翻转条件:** RPC 月费 > 800 USD (≈ 16k tx/月 或 5M+ read/月)
- 提前自建会消耗 Sprint-1 的稀缺工程容量, 与老钱 MVP scope 拒绝清单冲突
