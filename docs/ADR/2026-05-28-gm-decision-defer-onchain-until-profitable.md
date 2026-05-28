# GM 决议 — 起始阶段不上链，paper trading 证明能赚钱后再考虑

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Standing Decision
- **关联:** 用户 2026-05-28 指令、M4.5 gate（OKR）、ADR jurisdictional-deferral

---

## 1. 用户原话

> "老叶别研究了，起始阶段就先别上链了，确定能赚钱后再考虑吧"

## 2. 决议

**MVP 起始阶段不执行任何真实链上操作。** Paper trading 必须证明持续盈利（M4.5 7 hard gate 全通过）才解锁上链。

## 3. 范围

### 暂停（不撤销，设计保留作 future activation）

| 模块 | Owner | 暂停理由 | 设计文档去向 |
|---|---|---|---|
| Polygon RPC 选型实施（free tier 调研 #69） | 老叶 | 不上链就不需要 RPC | v1 文档归档保留 |
| Signer 实际部署 + KMS unwrap | 老孙 v5 | 不签真单子 | v5 设计保留作 future |
| nonce_mgr 真实 Redis/SQLite/RPC observe | 老叶 nonce-mgr v1 | 用 mock nonce | 设计保留 |
| Receiver 21 白名单实际生效 | 老叶 whitelist v1 | 不上链不触发 | 设计保留 |
| Polygon WebSocket subscription 实际跑 | 老周 v0.3 §17 | 同上 | 架构保留 |
| Gas 监控 + Sygnum 接入 | 老叶 + 老黄 | 全部 deferred | 已 Superseded |

### 保留必须做

| 模块 | 用途 |
|---|---|
| Polymarket REST API + WebSocket | **paper trading 必须真实订阅真实盘口**（成交是虚拟的） |
| Goalserve API | 真实订阅比赛进展 |
| RiskManager | 真实跑（paper 路径走完整 RM，R-11 / R-7 红线）|
| Audit log（BLAKE3 + WAL） | 真实写（paper_audit WAL，与 prod audit 物理隔离）|
| Paper engine 虚拟撮合 | 小袁 microstructure 模型 + 小肖 slippage model |
| 虚拟 PnL 账本 | 决定 M4.5 gate 通过否的关键 |
| **虚拟 nonce / 虚拟 gas / 虚拟 fill 时序** | Paper engine 内 mock，与未来上链时无缝切换 |

### 关键：架构 plug-in 设计

老周 v0.3 + 老韩 RM v0.2 + 老孙 signer v5 都已经走 `ExecutionMode { Live, Paper, Shadow }` 三态设计（GM Sign-off Paper Trade ADR）。**Paper mode 走 PaperSigner 不出链**，与 Live mode 共享 RM / audit / executor 全套，只在 L5 exec/router 出口换一个 signer 实例。

**这意味着：今天的所有架构设计完全兼容"未来上链"** — 不需要重做，只需要把 PaperSigner 换 LiveSigner，把虚拟 nonce 换真实 nonce，把虚拟 fill 换链上 confirm。

## 4. 触发上链的条件（M4.5 7 hard gate）

paper trading 必须连续 2 周累计满足：
- G1 PnL > 0 + t-test p < 0.10
- G2 Sharpe > 1.0 + bootstrap CI 下界 > 0.3
- G3 风控失效 = 0
- G4 在线率 ≥ 99.5%
- G5 max DD ≤ 8%
- G6 n_trades ≥ 50
- G7 paper PnL > random-entry baseline

**+ 老雷与外部律师确认美国 entity 或离岸 entity 选择（老黄 30-90 天 timeline）**

## 5. 经济效果

```
MVP 阶段月费节省:
   ├ RPC vendor 月费:     $98 → $0
   ├ KMS / Sygnum 接入:   $0（已撤销 ADR）
   ├ Polygon gas:         $0（不上链）
   ├ 律师费触发节点延后:  30-90 天后再付
   └ 累计节省:            ≥ $400/月 + $20-40K 一次性律师费延期

资源专注: paper trading 跑稳 + 数据收集 + ML 训练（小邓 Wave 14）
```

## 6. 派单调整（立即执行）

| Owner | 动作 | 时间 |
|---|---|---|
| 老叶 | 停 RPC free tier 调研 #69；v1 vendor 选型文档归档保留 | 即日 |
| 老叶 | nonce_mgr / whitelist 设计保留，**Sprint-2 不实施** | 即日 |
| 老孙 | Signer v5 设计保留，**Sprint-2 不实施 PaperSigner mock 即可** | 即日 |
| 老周 | 架构 v0.4 加 §18 "ExecutionMode.Paper 不上链时的具体 mock 接口" | Sprint-2 W1 |
| 小蒋 | Paper engine v0.2 把"虚拟上链"做成 plug-in mock（virtual_nonce / virtual_gas / virtual_confirm 三个 stub） | Sprint-2 W2 |
| 老唐 | Audit schema BLAKE3 hash chain 仍生效，但 Polygon Merkle anchor 暂停（链上锚定 deferred） | 即日 |
| 老吴 | us-east-1 部署仍要（Polymarket REST/WSS 服务器在那），但不部署 Polygon node | Sprint-2 |
| 小邓 | ML 路线图 v2 数据基础设施照常（这事跟上链无关） | Sprint-2 |

## 7. 红线

- **任何 PR 在 paper mode 触发真实链上交易 → P0** 事故
- **任何虚拟 nonce / 虚拟 gas 字段污染 prod ledger → P0**（R-11 红线已立）
- **paper trading 期 ML shadow signal 也不上链** — ML 仍是离线训练 / shadow 评分（小邓 Wave 14）

## 8. Sprint-1 / Sprint-2 backlog 影响

Sprint-1 已交付的设计文档**不动**（v1/v2/v3/v4/v5 + ADR 全保留作 future activation）。

Sprint-2 backlog（S2-001 ~ S2-028）调整：
- **保留**：架构 v0.4 / RM v0.3 / paper engine 联调 / 测试 framework / UX / dogfood / data infra / ML 训练 pipeline
- **deferred**：signer 实施 / nonce_mgr 实施 / Polygon RPC 接入 / KMS unwrap / gas 监控

详见 `docs/SPRINTS/sprint-02.md`（待小米 + 老胡 6/1 前更新 v0.2）

---

**Decided by 老雷, 2026-05-28**

**Superseded:**
- `docs/RESEARCH/laoye-polygon-rpc-selection-v1.md`（archive，Sprint-N+ reactivate）
- `docs/RESEARCH/laoye-nonce-manager-design-v1.md`（archive）
- `docs/RESEARCH/laoye-receiver-whitelist-v1.md`（archive）
- `docs/RESEARCH/laoye-polygon-rpc-endpoint-matrix-v1.md`（archive）

**Active 不变:**
- `docs/RESEARCH/laosun-key-management-v5-simplified.md`（设计保留，Sprint-2 只实现 PaperSigner mock）
- `docs/RESEARCH/laozhou-architecture-v0.3.md`（架构兼容 paper mode，未来切 live 不重做）
