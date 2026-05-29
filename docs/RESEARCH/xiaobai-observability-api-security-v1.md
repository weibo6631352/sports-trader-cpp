# 观测/调试 API 安全边界审查 v1

- Owner: 小白 (ai-llm-advisor, F 顾问 / security 预审)
- Date: 2026-05-29
- 视角: **仅 security 顾问 advise, 不实现** (落地归老沈/老吴/模块 owner)
- 触发: GM "后端 API 信息尽可能详细 (观测/开发/调试用)"
- 关联: CLAUDE.md §8 红线; `laoshen-threat-model-v1.md` (I-01/I-02/S-05); `laotang-audit-schema-v1.md` (R8/OQ-10); ADR R-11
- 备注: 任务点名的 `xiaobai-security-preaudit-gap-list-v1.md` + ADR-037 **当前仓库不存在** (已 grep 确认), 本文以现存 threat-model + audit-schema + 红线为 SSOT 替代锚点。

---

## 0. 核心立场: 详细 ≠ 泄密

观测 API 可以详细 (字段全、trace 深、时间戳全), 但"详细"维度是**业务/状态/时序**, 不是**密钥/凭证字节**。两者正交。下面给硬边界。

## 1. API 绝不可暴露字段 (黑名单 — 映射 §8 红线 + I-01/I-02)

任何 endpoint / response / debug dump / health 页一律剔除:

| 黑名单类 | 资产 | 映射 |
|---|---|---|
| WALLET_PRIVATE_KEY / mnemonic / shamir 分片 | AS-01 | §8 私钥红线, I-01(评分20 最高) |
| 签名材料: 原始 calldata 签名字节 / r,s,v / 待签 digest+key 上下文 | AS-04 | T-04, R8 |
| POLYMARKET API_KEY/SECRET/PASSPHRASE | AS-02 | I-02 |
| GOALSERVE_API_KEY / DB password / 代理凭证 | AS-03/05/15 | I-02 |
| 任意 HMAC / session / signing key 物料 | AS-04 | S-05 |

实现侧 advise: response 序列化层统一过 **allowlist (默认拒)**, 而非 blacklist 漏一个就泄。CI 静态扫 endpoint schema 复用 audit 的 secret scanner (R8 同款)。

## 2. 观测 API 访问控制 (映射 S-05 仿冒 / E-01 攻击面)

"详细 API" = 高价值攻击面。advise:
- **默认 bind `127.0.0.1`**, 不监听 `0.0.0.0`; 远程调试走 SSH 隧道, 不开公网口。
- **只读 token** (短 TTL, 与下单/signer 权限物理隔离); 写操作 API 与观测 API 不同进程/端口。
- signer 进程**永不**暴露任何 HTTP/观测口 (TB-B/TB-C: signer 不出网, 仅 UDS)。观测主机被入侵 ≠ 横向到 signer (堵 E-02)。

## 3. R-11 隔离落到 API 层

- response 必带 `mode: paper|live` 顶层字段, 不可缺省。
- paper 数据查 `paper_audit` 源, live 查 `risk_audit` 源, **API 层禁止 join/合表**。
- 防"详细聚合 endpoint"把 paper PnL 混进 live 报表 → 污染即 R-11 P0。

## 4. audit/trace 脱敏 (映射 R8 + OQ-10)

- 决策 trace 可详细 (rule_id / 触发阈值 / 输入快照 / 4 时间戳 R-20), 但 KEY_ROTATION payload 只回元数据 (wallet address + 审批引用), **永不带私钥字节** (R8)。
- ORDER_DECISION 的 `rule_trace` 对外/合规交付需脱敏开关 (OQ-10 未决, 待老黄+小梁)。内部调试可全量, 外部口默认脱敏。

## 5. 建议→红线映射汇总

| 建议 | 红线/gap |
|---|---|
| §1 黑名单 allowlist | §8 私钥, I-01/I-02, T-04 |
| §2 localhost+只读 token+signer 无口 | S-05, E-01/E-02 |
| §3 mode 字段+禁合表 | R-11 |
| §4 trace 脱敏 | R8, OQ-10, R-20 |

## 6. 移交

落地: 老沈 (访问控制/secret) + 老吴 (部署/bind/token) + audit owner 老唐 (脱敏)。OQ-10 需老黄+小梁定合规脱敏策略。本文 advise only。
