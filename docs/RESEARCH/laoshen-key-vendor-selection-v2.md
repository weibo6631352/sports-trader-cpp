# KMS Vendor 选型 v2 (性价比 + 延迟)

- Owner: 老沈 (security-engineer)
- Date: 2026-05-28
- Status: Active (v1 `laoshen-multi-vendor-kms-v1.md` 全文 Deprecated)
- Superseded reason: GM 2026-05-28 撤地域合规纠缠, 选型回归性价比 + 延迟; "≥ 1 非美总部" 硬约束撤销
- 关联: `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`, `docs/RESEARCH/laoshen-threat-model-v2.md` (M-K6 KMS unwrap)
- 验收人: 老雷 (GM) + 老孙 (crypto-signing-expert)

> v1 撤销内容: "vendor 总部国别评分", "≥ 1 非美总部硬约束", "Sygnum / Taurus Sprint-4 接入承诺", "瑞士 / 苏黎世 region 偏好", "美国主权一票否决讨论", "R4 / R5 合规对齐章节", "OFAC 二级制裁规避". v1 §1.1 / §6 / §7.3 / §9.1 R1 / R6 全段废弃.

---

## 1. 选型原则 (v2)

| 维度 | 权重 | 说明 |
|---|---|---|
| **延迟** (启动期 unwrap p99) | 高 | trader 主部署区域到 KMS endpoint 的物理就近 |
| **价格** (CMK 月费 + 调用费) | 中 | 启动期单次 + 季度轮换, 总开销月级别 |
| **SLA** | 高 | ≥ 99.99% (单 region) |
| **HSM 等级** | 高 | FIPS 140-2 L3 (CMK key material 在 HSM, 不可导出) |
| **同 region failover (多 AZ / 多 region)** | 高 | 单 vendor 内多 region 互备 |
| **实现独立性 (HSM 厂商)** | 低 | v1 强约束撤销, 仅作加分项 |
| **总部国别** | **不评估** | GM 撤地域合规 |

**单 vendor 可接受**, 但要求 **同 vendor 至少 2 个 region** 互备.

---

## 2. 候选评估

我们 trader 主部署: **AWS us-east-1 / Ashburn warm standby** (老吴 v0.1 部署方案不变).

| Vendor / Region | 延迟 | CMK 月费 | 调用费 | SLA | HSM | 综合 |
|---|---|---|---|---|---|---|
| **AWS KMS us-east-1** | < 5ms | $1/CMK | $0.03/10k | 99.999% | CloudHSM L3 | **首选** |
| AWS KMS us-west-2 | 60ms | $1/CMK | $0.03/10k | 99.999% | 同上 | **备 region** |
| GCP KMS us-east4 | 10~15ms | $1/月 HSM | $0.03/10k | 99.999% | Cloud HSM L3 | 备 vendor (可选) |
| Azure KV eastus | 10~15ms | $1/key | $0.03/10k | 99.99% | Thales nShield L3 | 备 vendor (可选) |
| HCP Vault 自建 | < 5ms | $50/月 EC2 | 0 | 自维 | 软件 (无 FIPS) | 不作主 |

**排除**: Sygnum / Taurus / Fireblocks (GM 撤地域合规); YubiHSM 2 保留作离线兜底.

---

## 3. 推荐组合 (单 vendor + 同 region failover)

### 3.1 三层架构

| 角色 | 选项 | 用途 | 触发 |
|---|---|---|---|
| **主 KMS** | AWS KMS us-east-1 (multi-AZ CMK) | signer 启动期 unwrap age key | 默认 |
| **备 KMS (同 vendor 不同 region)** | AWS KMS us-west-2 (CMK 持同一份 age key 的 wrap 副本) | 主 region 整体不可用时 fallback | us-east-1 KMS unwrap timeout ≥ 5s 或 5xx |
| **离线兜底** | YubiHSM 2 (老沈 + 老雷 各持 1 片, 2-of-2 物理 split) | 全 AWS 灾难 / account 冻 | 主 + 备连续 3 次失败 |

**关键约束**:
- 主 + 备 region 的密文是**同一份 age key 被两个 region 的 CMK 分别 wrap 产生的 2 份独立密文**
- 两份密文打包进 signer 启动配置, 离线可见
- 切换不影响 age key 一致性 — unwrap 后内存比对 hash, 全一致才 mlock, 不一致 abort

### 3.2 切换协议 (启动期)

signer 按顺序尝试 us-east-1 → us-west-2 的 `kms:Decrypt`, 单次 timeout 5s. 任一成功即 unwrap age key, 内存 hash 一致才 mlock. 双 region 全失败 → YubiHSM 离线 SOP (老雷 + 老沈 物理在场).

---

## 4. 延迟 + RTO

**热路径 0 影响** — KMS 仅启动期 unwrap 一次, 热签名走本地 libsecp256k1, p99 ≤ 1ms.

| 场景 | 启动期延迟 | 单 region RTO | 双 region RTO |
|---|---|---|---|
| 正常 | < 50ms | — | — |
| 主 region 故障 | 5.1s (timeout + 切) | 30min~3h | 0.1s 自动 |
| AWS account 冻 | 触发 YubiHSM | 1~24h | **不化解** → YubiHSM |
| 全 AWS 灾难 | YubiHSM 离线 | N/A | 30min~2h |

**v2 vs v1 对比**:

| 维度 | v1 (3 vendor 6 region + Sygnum) | v2 (AWS 双 region) |
|---|---|---|
| 月成本 | ~$7 + Sygnum $5k+ | ~$2 AWS |
| 工程量 | 3 套 SDK + IAM + cert pin | 1 套 |
| 演练项 | 4 | 2 |
| onboarding | 3 vendor KYC + Sygnum 银行 KYC 3~6 月 | 几天 |
| 残留风险 | 美国主权 (撤销) | AWS account 冻 (YubiHSM 兜底) |

**老沈判定**: GM 撤地域合规后, v2 单 vendor 双 region 最合理. 省 ~3 周工程 + 月费降 90%+ + 演练降 50%, 敞口仅 "AWS account 冻", 由 YubiHSM 离线兜底.

---

## 5. 演练 (季度)

| 季度 | 演练 | Owner |
|---|---|---|
| Q1 | us-east-1 主动 down, 验 us-west-2 自动接管 | 老沈 + 老吴 |
| Q2 | 双 region 全 down, YubiHSM 离线 unwrap (老沈 + 老雷 物理) | 老沈 + 老雷 |
| Q3 | 完整 master key 轮换 + 双 region 重新 wrap (与老孙轮换合并) | 老沈 + 老孙 + 老吴 |
| Q4 | 应急 / 取证综合演练 (融合威胁模型 §6.3) | 老沈 + 老雷 |

演练记录归档老唐 (audit-expert), 任一项失败 30 天内修复 + 复测.

---

## 6. v1 撤销条数 (10 条)

1. v1 §1.1 "合规视角 (R4/R5 对齐)" 全段
2. v1 §1.2 "美国法院 cert / 政府密令 (CLOUD Act / FISA 702)" 失败模式
3. v1 §2.1 评分维度 "HQ (总部国别)"
4. v1 §2.2 候选表中 Sygnum / Taurus / Fireblocks / Azure switzerlandnorth / GCP europe-west6
5. v1 §3.1 "GCP 新加坡 / Azure 苏黎世 / AWS 东京" 跨 vendor 组合 (改为 AWS us-east-1 + us-west-2)
6. v1 §5 与 Shamir 5 地点跨境耦合
7. v1 §6 "R4 / R5 合规对齐" 全章
8. v1 §7.3 / §9.1 R1 / R6 "Sygnum Sprint-4 硬截止"
9. v1 §9.1 R2 "美国主权一票否决" 风险
10. v1 §10 "Sygnum / Taurus 商务谈判"

**保留**: v1 §1.2 region 故障 / account 冻 / CMK CVE / TLS / IAM 五类技术风险, v1 §3.2 切换协议 (改 C++ 双 region), v1 §4 延迟框架, v1 §8 演练框架 (4 → 2 项).

---

## 7. 开放问题 + sign-off

| # | 问题 | Owner | Deadline |
|---|---|---|---|
| Q1 | AWS us-east-1 + us-west-2 IAM (CMK + sourceIp + Decrypt only) | 老吴 + 老沈 | Sprint-1 末 |
| Q2 | YubiHSM 2 采购 + 个人化 | 老吴 + 老沈 + 老雷 | Sprint-2 |
| Q3 | 双 region 密文一致性校验 C++ + 单测 | 老孙 | Sprint-2 W1 |
| Q4 | KMS endpoint SPKI pin 抓取 + 编进 binary | 老孙 + 老吴 | Sprint-2 W1 |
| Q5 | break-glass IAM (备账户备 CMK) | 老吴 + 老沈 | Sprint-2 W2 |
| Q6 | KMS Service Quota 申请 (Decrypt 5500/s) | 老吴 | Sprint-1 末 |

**sign-off 条件**: 老孙 v5 引用本文 + 老吴 Sprint-1 末完成双 region IAM/CMK + YubiHSM Sprint-2 内到位 + Q1/Q2 演练录入老唐 audit. 全部满足 → 老沈签字 → 进 Sprint-2.

---

*v2 提交: 2026-05-28*
*Supersedes `laoshen-multi-vendor-kms-v1.md` (全文 Deprecated)*
*下次 review: Sprint-1 末 (与 signer v5 联动)*
