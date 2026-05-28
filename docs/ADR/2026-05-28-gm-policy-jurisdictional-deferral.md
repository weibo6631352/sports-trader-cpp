# GM 决议 — 撤销地域 / 法律 / 监管合规纠缠（公司主体未来迁合规地区）

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** Standing Policy
- **关联:** 用户 2026-05-28 指令、`docs/RESEARCH/laohuang-compliance-redline-v1.md`、`docs/RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md`、`docs/ADR/2026-05-28-gm-commitment-sygnum-deadline.md` (Superseded)

---

## 1. 用户原话

> "我们不要考虑法律安全了，地区规则等，我们到时候会到合规的地区，不要在地域合规性纠缠了。"

## 2. 决议范围 — 撤销

**撤销以下纠缠：**
- ❌ R4 "美国元素全部 block" 红线 → **暂时撤销**（未来迁地址后由当地律师重定义）
- ❌ Sygnum / Taurus 2027-02-26 接入承诺 → **Superseded**（不再追求"瑞士非美 vendor"）
- ❌ 老黄合规红线 §2 "司法管辖区限制" → **简化**（保留平台层 ToS 即可）
- ❌ 老黄合规红线 §3 "跨境 海关 / 数据出境" → **撤销**
- ❌ 老孙 Shamir 跨境 5 地点（中国大陆 / 香港 / 新加坡 / 瑞士 / 日韩）→ **简化**为操作便利优先（地理灵活）
- ❌ 老沈跨 vendor KMS "≥ 1 非美总部" 硬约束 → **撤销**（性价比 + 延迟优先）
- ❌ 监管动向（CFTC / SEC / 中国监管）跟踪 → **降级**到月度旁观（不进决策路径）
- ❌ 持片人地理分散 + 跨境运输 SOP → **撤销**

## 3. 决议范围 — 保留

**仍然必须做（与地域无关的）：**

| 类型 | 保留项 | 强度 |
|---|---|---|
| 平台 ToS | Polymarket ToS 不踩（速率 / 反操纵 / API 使用条款）| 红线 |
| 平台 ToS | Goalserve ToS / 商业 vendor 合同 | 红线 |
| 数据使用 | Goalserve / Polymarket 数据条款（reselling / 公开 / 内部） | 红线 |
| 反操纵 | 不自买自卖、不刷量、不 spoofing | 红线 |
| 资金 KYC | Polymarket 平台已做的 KYC（我们以 individual / entity 身份 onboard）| 平台层 |
| 私钥安全 | mlock / MADV_DONTDUMP / 零拷贝 / SecureBuffer / 审批阈值 / HA | 红线 |
| Vendor 合同 | AWS / GCP / Alchemy / QuickNode 等 vendor 合同 review | 商务 |
| 内部审计 | audit log 完整性、可追溯（老唐 BLAKE3 schema）| 红线 |
| 数据安全 | TLS / cert pin / IPC 鉴权 / 防泄漏 | 红线 |

## 4. 简化后立场

**地理 / vendor 选择回归性价比 + 延迟优先：**
- KMS 主：**AWS us-east-1**（与 Polymarket / Goalserve / Polygon edge 同区，延迟最低）
- KMS 备：另选 region（AWS us-west / GCP us / 内部 Vault），不强求跨 vendor
- Shamir 分片（如果保留）：5 份分公司内部高管 + 银行保管箱即可，地理灵活
- 部署节点：**us-east-1 主 + Ashburn warm standby**（老吴 v0.1 不变）

**未来迁地址（用户 2026-05-28 后续澄清）：**

> "我们到时候公司和服务器搬迁到美国 Polymarket 总部附近，应该是地域合规吧"

- **服务器 colo：美东 us-east-1（与 Polymarket / Goalserve / Polygon edge 同区）→ 已定，老吴跨洋部署 v0.1**
- **公司主体未来很可能也在美国**（Polymarket 总部附近）— **未确认合规可行性**
- **合规可行性待实证**：Polymarket 2022 与 CFTC 和解被禁向美国用户，**美国实体做 Polymarket prop trading 当前 stance 未知**（不是面向用户提供服务）
- 触发节点：盈利稳定 / 资金到位 → 老黄做"美国主体可行性"专项调研 → 老雷决定
- 当前阶段：以"未决"姿态做（不为美国主体提前调整，但也不主动 block 美国 vendor）
- 届时 90 天内一次性合规审计 + 调整

## 5. 派单（撤回 + 简化）

| Owner | 动作 | 截止 |
|---|---|---|
| 老黄 | 合规红线 v2 简化（保留平台 ToS / 数据使用 / 反操纵 / 合同 review，删 jurisdictions / 跨境 / 监管跟踪）| 6/4 |
| 老黄 | Shamir 跨境 sign-off v1 → **Deprecated**（保留作 future 参考）| 即日 |
| 老孙 | Signer v5 简化（撤跨 vendor 8 候选 + 撤 Shamir 5 地点跨境 + 简化为 AWS us-east-1 主 + 内部 Shamir）| 6/4 |
| 老沈 | 跨 vendor KMS v1 → **Deprecated**（保留作 future 参考）| 即日 |
| 老沈 | 安全 v2 简化（保留 STRIDE / 内存防护 / 内控审计，删跨 vendor 硬约束）| 6/4 |
| 老雷 | Sygnum / Taurus 接触暂停 | 即日 |
| 小米 | docs/INDEX 标 deprecated 文档 | 6/1 |

## 6. 节省效果

- 老孙 v4 → v5 简化：少 2-3 个 vendor 集成 + 少 5 地点 Shamir 部署 → **节省 ~3 周工程量**
- 老黄红线 v2：保留 50% 内容，删 50% jurisdictional → **每月 review 时间从 4h → 1h**
- 老沈 v2：单 vendor + region failover，安全审计聚焦内控 → **专注度提升**
- 整体：**Sprint-2 + 起步季节省 ~4 周时长**，专注于业务核心

## 7. 红线（本决议本身的红线）

- 撤销不等于"不做" — 平台 ToS / 反操纵 / 私钥安全 / 数据使用条款仍是 P0
- 未来迁主体时必须**重新审核所有撤销项**（不是永久撤销）
- 当前阶段任何"为美国 / 中国监管特别开发"的代码 → Reject（既然不纠缠就不留路）

---

**Decided by 老雷, 2026-05-28**

**Superseded:**
- `docs/ADR/2026-05-28-gm-commitment-sygnum-deadline.md`（Sygnum 2027-02-26 承诺 → 撤销）
- `docs/RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md`（跨境 Shamir → Deprecated）
- `docs/RESEARCH/laoshen-multi-vendor-kms-v1.md` "≥ 1 非美总部" 硬约束 → Deprecated（vendor 选型回归性价比）
