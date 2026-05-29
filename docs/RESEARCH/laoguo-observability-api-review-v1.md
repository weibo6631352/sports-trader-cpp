# 后端观测 API — 架构评审 + 顾问团关切汇总 v1

- Owner: 老郭 (F 顾问团协调人, 架构否决权)
- Date: 2026-05-29 UTC
- 视角: 架构评审 + 顾问协调 (不做设计本身; 设计归老周)
- 评审对象: `laozhou-w8-debug-rest-api-spec-v1.md` (12 endpoint), 对齐 ADR-037 / R-11 / R-12 / R-20
- 顾问输入: 小白 `xiaobai-observability-api-security-v1.md` (security, 已产出) + 老何 (现代 C++ footgun) + 老徐 (观测工具栈)

---

## 1. 架构红线把关 (不可妥协)

- **R-12 — 观测面绝不进交易 event loop.** 老周 spec §2.3/§5.2 已对; 加硬约束: API 与 hot path 唯一耦合点 = `atomic snapshot 指针 / SPSC 读端`, 任何 endpoint 不得 inline 调用 `RM::evaluate()` / `emit()` / push 端。新增红线 **G-OBS-1: 观测读路径零锁 + 零 syscall stall**, CI grep 拦 API handler 内出现 hot-path mutex。
- **R-11 — paper/live 物理不混.** response `mode` 字段强制非空 (小白 §3); **API 层禁止 join paper_audit × risk_audit**。`/positions` `/audit/recent` `/metrics` 三个聚合口最危险, 必须按 mode 路由数据源, 不得合表。
- **只读不可变成隐藏控制面 (我加的硬条款).** `/drain` `/resume` 是控制面, 不是观测面。必须 **物理分离**: 观测口 (GET, loopback) 与控制口 (POST, 鉴权 + audit emit) 不同 path 前缀甚至不同端口。控制操作必须留 audit (R-6 可追溯), 否则一个"调试 API"就成了无审计的 halt 后门。

## 2. 顾问团关切汇总 (aggregate)

- **老何 (现代 C++ footgun):** RCU `atomic<Snapshot*>` 读端有 **UAF / dangling 风险** — reader 持裸指针期间 writer free 旧 snapshot。要求 `shared_ptr` 原子 load 或 hazard pointer / epoch 回收, 不许手撸裸指针 swap。`tail_copy(N)` 返回 `vector` 而非 `span` (OQ-8 选 vector, 避免悬垂 view)。JSON 出站统一 allowlist 序列化, 防字段反射意外带出 secret。
- **老徐 (观测工具栈):** `/metrics` Prometheus text 与 9 个 JSON debug 口 **指标语义必须同源** (同一 snapshot), 否则看板与 curl 对不上。建议 metric 前缀 `stcpp_` 固化为命名契约; label 走白名单 (小白 I-04: label 别泄策略参数 / 持仓)。Grafana 集成本季不做 (前端直接解析 text), 但 metric 名一旦对外即契约, 见 §3。
- **小白 (security, 已产出):** 黑名单→allowlist (默认拒); 默认 bind `127.0.0.1`; signer 进程永不开 HTTP 口; trace 脱敏开关 (OQ-10 待老黄+小梁)。老郭背书全部采纳, 列为上线前置。

## 3. 不可逆决策预警 (现在必须定对)

一旦对外, 改起来贵的三项:

1. **4-ts 字段命名 + epoch_ns int64** (R-20) — 全 endpoint 已统一, **锁死, 不许个别口偷用 ISO 字符串**。
2. **vendor-agnostic 字段语义 (对齐 ADR-037)** — `fair_value` / `token_id` / `outcome` 必须是 **归一化后** 字段, 严禁透出 Goalserve / Polymarket 原始 vendor 字段名。换源 = 换 adapter, 观测 schema 不变。当前 spec 合规, 但需 CI grep 守 (禁 `goalserve_` / `pm_` 前缀进 response)。
3. **Prometheus metric 名 + label key** — 对外即契约, 改名破坏告警规则。`stcpp_*` 命名 + label 白名单一次定对。

## 4. 实施分期建议

- **MVP (W9-W10, 满足 PnL 看板 + 调试 trace):** `/healthz` `/version` `/status` `/positions` `/signals/active|history` `/risk/rejects` `/metrics` + 控制口 `/drain` `/resume`。覆盖老板 "curl 功能检查" + M1-H PnL 看板 acceptance。loopback only, 无 auth (debug-first 合理)。
- **完整版 (M5+ live 前):** `/orderbook/{token_id}` `/audit/recent` 全量 trace + bearer token + 控制口鉴权 + signer 隔离审计 + trace 脱敏开关 + reverse proxy。
- 不可逆三项 (§3) 必须 **MVP 阶段就定对**, 不能等完整版。

## 5. 评审 gate — 走不走 ADR

**走 ADR (建议立 ADR-038)。** 理由: (a) 跨 ≥ 3 单元 (A 工程 / B 风控控制口 / D 数据 4-ts / E 前端 + F 安全); (b) 引入对外 schema 契约 (不可逆); (c) 新增控制面需红线背书。老周 spec v1 质量高、ADR 引用齐, 可直接作为 ADR-038 草案主体。

**放行条件 (gate, 全满足才 merge):**
1. 观测/控制面物理分离 + 控制口 audit emit (§1)。
2. RCU 改 `shared_ptr` 原子 / hazard pointer (老何, §2)。
3. allowlist 序列化 + CI vendor 字段 grep (§3.2) + secret grep (小白 §1)。
4. `mode` 字段强制 + 禁合表 CI 守 (R-11)。
5. metric 命名契约固化 (老徐, §3.3)。

**勘误 (移交 GM/小米):** 任务 prompt 称 "ADR-037 + drive directive (R-12/R-11/R-20)" — 实测 ADR-037 (`2026-05-29-data-model-strategy-vendor-agnostic.md`) 是 **数据/模型战略**, 三条红线在各自独立 ADR (R-12 `gm-redline-websocket-non-blocking`, R-20 `gm-redline-data-source-timestamping`, R-11 `adr-011-paper-live-binary`)。本评审已按真实 SSOT 锚定。

---
**最后更新:** 2026-05-29 by 老郭
