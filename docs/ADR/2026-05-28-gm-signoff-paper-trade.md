# GM Sign-off — Paper Trading 模式红线

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Approved
- **关联:** 用户 2026-05-28 指令、`docs/RESEARCH/laogao-code-conventions-v1.md` §2.6、`docs/OKR/2026-Q2-Q3-startup-season.md` M4.5

---

## 1. Paper Trading 是公司红线一部分

用户指令：**虚拟盘稳定盈利才上实盘**。GM 已加 M4.5 gate（2 周 paper PnL > 0, Sharpe > 1.0, 风控失效 = 0）。

## 2. 三条决议（回老高 §2.6）

| 决议点 | GM 决议 | 备注 |
|---|---|---|
| D-PT1: paper 模式是否走完整 RM? | **走，全 RiskGateway / 全 audit / 全 signer**（signer disabled 模式不上链）| 绝不允许 `#ifdef PAPER_TRADE` 跳 RM，违反 R-1 |
| D-PT2: build-time vs run-time 切换? | **build-time + run-time 二选一启动；同进程不允许动态切换** | 防红线，单进程只能一个 mode 跑全程 |
| D-PT3: paper audit vs live audit 合表? | **分两个独立 WAL**（`paper_audit` vs `risk_audit`） | 与 ADR-001 audit/position WAL 分离同理 |

## 3. 红线增项 R-11（老高 v1.1 转 ACTIVE）

**R-11 paper-mode 不得污染真账本** — `paper_*` 数据严禁写入 `position` / `pnl_ledger` / `nonce_ledger`；严禁出现在 `risk_audit` 真审计流（走独立 `paper_audit`）；UI / Grafana 必须显式区分。

违者 = P0 事故。

## 4. ExecutionMode 三态（老高 §2.2 建议）

```
ExecutionMode { Live, Paper, Shadow }
```

- **Live**：真实下单、真实签名、真实上链
- **Paper**：真实数据 + 真实 RM + 模拟撮合（小袁 microstructure model）+ signer disabled → 虚拟 fill → 虚拟 PnL（独立账本）
- **Shadow**：真实数据 + 真实 RM + 真实路由 dry-run + 模拟撮合并跑 → 用于实盘前最后一道全链路压测

## 5. 派单

- **老周 v0.3** → §16 加 `ExecutionMode` 枚举 + L5 `exec/router` 三态分发
- **老韩 v0.3** → RM 接受 mode 参数（不影响 evaluate 逻辑）
- **小蒋** → Paper Trading 引擎 v0.1 已按本红线写（Wave 6 在跑）
- **老高 v1.1** → 规范升 ACTIVE 含 R-11
- **小米** → docs/INDEX 加本 ADR + 通知全员阅读

---

**Approved by 老雷, 2026-05-28**
