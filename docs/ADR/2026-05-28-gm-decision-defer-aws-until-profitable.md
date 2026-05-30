# GM 决议 — AWS 部署延后到 paper 稳定盈利后

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Standing Decision
- **关联:** 用户 2026-05-28 指令、上链 deferred 同逻辑（原 gm-decision-defer-onchain-until-profitable.md 已删; 决策事实: MVP 不上链, M4.5 hard gate 通过后解锁）、M4.5 7 hard gate

---

## 1. 用户原话

> "aws 滞后 这是盈利后的事情"

## 2. 决议

**MVP / paper trading 阶段不部署 AWS。** 本地开发 + 本机跑 paper trading + 历史回测。M4.5 7 hard gate 通过 → 与上链解锁同步 → 才上 AWS。

## 3. 逻辑（与上链 deferred 同向）

```
paper trading 不需要 us-east-1 colocation:
   ├ 不发真单子 → 跨洋延迟优势无意义
   ├ 真实订阅 Polymarket WebSocket 在本地照样能跑 (跨洋 880ms TTFB)
   ├ 历史回测在本地 C++ 编译跑 (DuckDB / Parquet)
   ├ Goalserve 数据通过代理也是本地接入
   └ paper engine 虚拟撮合 + 虚拟 PnL 本地账本

M4.5 7 hard gate 通过 → 解锁实盘 → 才需要:
   ├ us-east-1 colocation (减跨洋延迟到 < 10ms)
   ├ HA standby
   ├ 24/7 monitoring infra
   └ ...其他生产基础设施
```

## 4. 现在月费实账（重新算）

```
当前 MVP 起步月费 (本地开发):
   ├ Goalserve plan (已购):     不增量
   ├ AWS:                       $0  ← 撤回 ~$420/月
   ├ 工具栈 (全 free):           $0
   ├ Polygon RPC:               $0 (deferred)
   ├ Pinnacle 数据:              $0 (Goalserve 已覆盖)
   ├ KMS / Sygnum:              $0 (deferred)
   ├ 律师:                       $0 (30-90 天后触发)
   └ 总:                        ~$0/月增量

(只在 Goalserve plan 已购 + 本地电费/网费, 本质 0 烧钱阶段)
```

## 5. 暂停（不撤销，设计保留作 future activation）

| 模块 | Owner | 状态 |
|---|---|---|
| AWS us-east-1 注册 + 实例开通 | 老吴 | ⏸️ 撤 Wave 16 任务 |
| Hetzner Ashburn warm standby | 老吴 | ⏸️ |
| CloudWatch + S3 cold storage | 老吴 | ⏸️ |
| Self-hosted runner | 老吴 | ⏸️ |
| us-east-1 服务器规格 c6i.xlarge (ADR-001 W-6 批) | 老吴 | ⏸️ 不撤销规格决议，撤实际开通 |

## 6. 保留必须做（与 AWS 部署无关）

| 模块 | 用途 |
|---|---|
| **本地 C++ 编译 + paper engine 跑通** | 用本机 CPU 跑 paper trading（开发笔记本 / 台式机即可）|
| **本地 SSD 历史数据 + audit log** | DuckDB / Parquet + 老王 WAL framework 全本地 |
| **本地 Python venv 离线训练** | 小邓 ML 训练 / 小董 统计 |
| **跨洋实测仍要做**（老陈 v1 数据保留）| 知道未来 us-east-1 能省多少延迟 |
| **架构设计仍要 us-east-1 假设**（老周 v0.4 §13）| 未来切换时 zero-rewrite |

## 7. 解锁 AWS 部署的触发条件

```
M4.5 7 hard gate 连续 2 周全通过:
   ├ G1 PnL > 0
   ├ G2 Sharpe > 1.0
   ├ G3 风控失效 = 0
   ├ G4 在线率 ≥ 99.5%
   ├ G5 max DD ≤ 8%
   ├ G6 n_trades ≥ 50
   └ G7 paper > random baseline

→ 触发 AWS 实际开通（老吴 SOP 启动，4 工作日 lead time）
→ 同时启动上链解锁（ADR `defer-onchain` 同步触发）
→ paper → live 切换（架构 v0.4 §18 ExecutionMode.Live）
```

## 8. 撤回的派单（即时）

- ⏸️ **老吴 AWS 注册 SOP**（Wave 16 #76 子项）— 设计保留，实际不跑
- ⏸️ **老吴 24h cron 跑** bandwidth 测试 — paper 阶段在本地跑就够
- ⏸️ **Self-hosted GitHub runner 部署** — 用 GitHub Actions cloud free tier

## 9. 保留派单

- ✅ 老郭 ADR-003 评审（本地架构层面评审，与 AWS 无关）
- ✅ 老王 WAL framework 骨架代码（本地跑就有用）
- ✅ 小宋 测试 framework 骨架（本地 CI）
- ✅ 小林 招聘 JD（与基础设施无关）
- ✅ 小米 docs 健康度（与基础设施无关）
- ✅ 老胡 Sprint-2 周报 + W2 backlog（含 AWS deferred 调整）

## 10. 红线

- **paper trading 阶段任何 PR 启用 AWS 真实资源（KMS unwrap / EC2 instance / S3 bucket 真实付费）→ P0**
- **AWS 账号注册仍可（注册 = 占名 + 不付费）**，但实例开通延后
- 任何"为了 demo / for fun 上 AWS" → reject

## 11. 与上链 deferred 同步触发

```
M4.5 7 hard gate 全通过
       ↓
解锁 AWS + 解锁上链 + 解锁 Sygnum 商务 (90 天 timeline)
       ↓
       一起切换到生产路径 (架构 v0.4 zero-rewrite 设计已就位)
```

---

**Decided by 老雷, 2026-05-28**

**Superseded:**
- `docs/RESEARCH/laoye-cross-region-deployment-v0.1.md` 实际部署部分 → archive
- 老吴 AWS SOP（未交付即撤）→ 设计保留作 future activation

**Active 不变:**
- 老周 v0.4 §13 部署假设（us-east-1 主 + Hetzner Ashburn 备）保留设计
- ADR-001 W-6（c6i.xlarge 规格）保留决议
