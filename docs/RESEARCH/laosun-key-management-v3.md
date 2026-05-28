# 私钥管理方案 v3 (Shamir + BIP39 + 跨 vendor 整改)

- Owner: 老孙 (crypto-signing-expert)
- Co-review: 老沈 (security-engineer) — 等其 v2 跨 vendor 出, 与本 v3 §N4 联签
- Compliance sign-off: 老黄 (compliance-legal) — v1 已 Conditional Accept (`laohuang-shamir-jurisdiction-signoff-v1.md`), 等 v3 final sign-off
- GM 验收: 老雷 (已批老黄方案)
- Date: 2026-05-28
- 状态: v3 提交, 等老黄 + 老沈 final sign-off
- 关联:
  - 前版: `docs/RESEARCH/laosun-key-management-v2.md` (保留, 不删)
  - 前版: `docs/RESEARCH/laosun-key-management-v1.md` (保留, 不删)
  - 合规整改源: `docs/RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md` (老黄 5 项硬整改 §9 + SOP-K1~K5 §10)
  - co-review: `docs/RESEARCH/laoshen-key-management-coreview-v1.md` (B1~B8 出处)
  - 红线参考: `docs/RESEARCH/laohuang-compliance-redline-v1.md` (R1~R12)

> v3 自包含, 但 §1~§7 沿用 v2 主体不重复. 本 v3 主要修改集中在 §8 (跨境 Shamir + BIP39) + §N4 (跨 vendor KMS) + SOP-K1~K5 引用. 其余 v2 章节不变.

---

## 0. v2 → v3 变更摘要 (5 项老黄整改 + N4 跨 vendor)

| # | 章节 | v2 状态 | v3 修复 | 整改源 |
|---|---|---|---|---|
| 1 | §8.3 Shamir 持有人 + 司法管辖区分布 | 占位符 + 老沈/老孙/老黄持片 | 完全替换为 5 地点真实方案 (HK/SG/CH/CN/JP-or-KR) + 老黄拒持片 + 老周/老吴入列 | 老黄 §9.1 |
| 2 | §8.2 地理硬约束 | 软描述 "至少 1 份异地" | 加 3-non-PRC / 0-US / 0-OFAC / 0-UK 硬约束 | 老黄 §9.2 |
| 3 | §8.4 演练矩阵 | "半年一次" 单频率 | 季度 tabletop / 半年 partial / 年度 full 三级 | 老黄 §9.3 |
| 4 | §8.6 BIP39 passphrase 二次加密 | v2 §5.2 仅 1 句 | 完整新章: 老雷+老黄 2-of-2 + SOP-K5 销毁权 | 老黄 §9.5 + §4 |
| 5 | §N4 跨 vendor KMS | v2 §5.5 "建议 GCP/Azure" | 升级硬约束: ≥ 2 vendor + ≥ 1 非美 vendor 总部, 与老沈 v2 (Wave 5 并行) 对齐 | 老黄 §8.2~§8.3 |
| 6 | Top 1 风险 (亚太双点失效) | v2 未单列 | 新增预防性轮换条款 (SOP-K1 扩展) | 老黄 §0.4 + §7.3 |
| 7 | SOP-K1~K5 | v2 无 | 引用老黄 §10.1~§10.5 全套 | 老黄 §10 |

**不变项**: B1~B8 修复 (v2 §2)、IPC schema (v2 §3)、HA 设计 (v2 §4)、IAM (v2 §6)、验收 checklist (v2 §7)、附录 (v2 附录 A/B/C). 这些老沈 v2 review 通过, v3 沿用.

---

## 1~7. 沿用 v2 主体 (不重复, 见 `laosun-key-management-v2.md`)

- §1 v1→v2 变更摘要 → v2 不变
- §2 B1~B8 修复方案 → v2 不变
- §3 IPC schema (v2 重写) → v2 不变
- §4 signer active-standby HA → v2 不变
- §5 Shamir 跨境合规 (老沈对齐) → **v3 §8 重写, 取代 v2 §5**
- §6 AWS IAM JSON spec (老吴 owns) → v2 不变, **但 §N4 升级跨 vendor 要求与老吴新协调**
- §7 验收 checklist → v2 不变, **追加 v3 整改 checklist 见 §9**

---

## 8. 跨境 Shamir 分布 v3 (5 项老黄整改全修)

### 8.1 设计原则 (沿用 v2 + 老黄 §1.2 法律性质判定)

- **Shamir 3-of-5 离线**, 不是"温备份" (温备份是 §6 KMS 路径)
- 分片本身物理或数字介质均可, **不进任何在线系统**
- 分片只在"主密钥不可访问 + 需重建" 时启动恢复, 平时纯冷备
- 老黄 §1.2 核心结论: **Shamir 单一分片不等于密钥, 法律辩护空间 > 单点 cold storage**. 这是 v3 选 Shamir 而不选单点冷钱包的根本理由.

### 8.2 地理硬约束 (老黄 §9.2, v3 硬整改)

替换 v2 软描述, 改为以下**强制**约束 (任一违反 → 老黄 sign-off 撤回):

```
v3 强制地理约束:
1. 至少 3 份分片在非中国大陆主权范围 (即排除大陆 + 香港 + 澳门)
2. 至少 1 份在瑞士 / 欧盟法律保护体系下 (FINMA / MiCA, 优先瑞士)
3. 0 份在美国 (R4 一票否决, 任何介质形式均拒)
4. 0 份在 OFAC 国 (R5 一票否决)
5. 0 份在英国 (R3 红线一致性, 与避英国 IP 同步)
6. 介质多样性: 至少 2 类介质 (金属种子板 + 数字 air-gap), 避免单一介质工艺缺陷共同失效
```

**老黄 §2.3 关键判定 (沿用)**: 香港分片 + 中国大陆分片 = **1 + 0.5 司法管辖区独立性** (因香港主权属 PRC, 全国性紧急/国安场景下两地同被纳入同一权力意志). 故"3 份非 PRC 主权" 是恢复门槛 3-of-5 的硬约束.

### 8.3 5 地点真实方案 (老黄 §5.1 + §9.1, v3 完全替换 v2 §5.1)

| # | 地理位置 | 持片人 | 居住地 | 国籍 | 介质 | 物理位置 | 法律保护强度 |
|---|---|---|---|---|---|---|---|
| 1 | **香港** | 老雷 (GM) | 中国大陆 (港陆往返) | 非美籍 | 金属种子板 (Cryptosteel / Billfodl) + BIP39 passphrase 二次加密 | 汇丰 HK 或 DBS HK 个人保管箱 (老雷名义) | 中等 — 普通法 + 银行保管箱合同 |
| 2 | **新加坡** | 老雷 或 老周 (名义持有人, 经法律 review) | 中国大陆 | 非美籍 | 金属种子板 + passphrase | DBS SG 个人保管箱 | 中等 — MAS + 银行保管箱合同 |
| 3 | **瑞士 (苏黎世 / 楚格州)** | 商业托管商 (Sygnum 或 Taurus, 经老黄 §3.3 6 项扫描 sign-off) | 瑞士 | N/A (商业实体) | 数字 air-gap + 商业 escrow + passphrase | Sygnum / Taurus 托管设施 | **最强** — 瑞士银行保密 + FINMA 框架 |
| 4 | **中国大陆 (老雷常驻城市)** | 老周 (架构师) | 中国大陆 | 非美籍 | 金属种子板 + passphrase | 个人银行保险箱 (**绝不放家中**) | **最弱** — 仅在地灾备意义 |
| 5 | **日本东京 或 韩国首尔** | 老吴 (SRE, 待长期外派可行性确认) | 拟外派日本/韩国 | 非美籍 | 加密 USB (数字 air-gap, 双副本) + Yubikey FIDO2 二次解锁 + passphrase | 当地银行保管箱 或 公司外派点保管 | 中等 — 资金决済法 / FSC 框架 |

**老黄 §3.2 排除清单 (v3 锁定)**:
- 老沈 (security): **不持片** — 安全官需保持"审查者"超脱身份
- 老孙 (crypto-signing, 我自己): **不持片** — signer 设计者与 Shamir 恢复链利益冲突隔离 (老黄 §3.2 第 5 条核心理由)
- 老黄 (compliance): **不持片** — 角色冲突 + 法律风险叠加 (老黄 §4.1 4 条理由, 不让步). 改持 BIP39 passphrase 第二要素 (§8.6)
- 老韩 (RiskManager): **不持片** — 已与签名审批 (v2 §7) 重叠, 不再扩面

**关键 trade-off 说明** (老黄 §5.2 + 老孙复述):
- 真正主权独立分片数 = 新加坡 1 + 瑞士 1 + 日本/韩国 1 = **3 份**, 刚好 = 恢复门槛
- **抗境外单点失效裕度 = 0** (亚太双点失效场景, 见 §8.5 Top 1 风险)
- 不上 4-of-7 因为持片人数量扩大 = 合谋风险扩大 + 协调成本扩大; MVP 阶段接受 0 裕度并用 SOP-K1 预防性轮换补

### 8.4 演练矩阵 (老黄 §9.3, v3 替换 v2 "半年一次")

替换 v2 单一频率, 改为三级演练 cadence:

| 类型 | 频率 | 范围 | 是否动真分片 | Owner | Audit |
|---|---|---|---|---|---|
| **Tabletop drill (桌面推演)** | 季度 (每 90 天) | 3 持片人模拟到场 + SOP 步骤 walkthrough + 突发场景 (SOP-K1~K5) 演练 | **否** | 老孙主持, 老雷+老黄+持片人参与 | 老唐 (audit) 录像 + memo |
| **Partial drill (实际取分片验证)** | 半年 (每 180 天) | 实际取 1 份分片 (轮换持片人), 到 air-gap 工作站验证可读 + passphrase XOR 验证, **不重建主密钥** | **是 (1 份)** | 老孙 + 老沈 + 1 个持片人 | 老唐 录像 + 老黄合规 memo |
| **Full drill (完整重组)** | 年度 (每 365 天) | 召集 3 持片人到中立地 (瑞士或新加坡), air-gap 工作站重建主密钥, 验证签名能力. 重组后 30 天内启动季度 master key 轮换 (与正常轮换合并, 不浪费一次轮换额度) | **是 (3 份 + passphrase 2-of-2)** | 老孙主持 + 老沈监督 + 老雷书面授权 + 老黄合规授权 | 老唐 全程录像归档 + 老黄出年度合规报告 |

**演练记录归档**: 全部归档老唐 (audit-expert), 老黄 review 每次演练是否触发合规事件 (例如某持片人异常 / 介质损坏 / passphrase 持有人异常). 演练失败任一步骤 → 立即触发 SOP-K1 预防性轮换.

### 8.5 Top 1 风险缓解 — 亚太双点失效 (老黄 §0.4 + §7.3, v3 新增 SOP)

**风险描述**: §8.3 方案下分片 1 (香港) + 分片 4 (中国大陆) 同时失效 (例如全国性紧急状态 / 国安调查), 海外仅余分片 2 (SG) + 3 (CH) + 5 (JP/KR) = **3 份, 恰够门槛, 0 裕度**. 任何第二个海外点同时失效 (例如 SG 持片人意外 + JP/KR 持片人意外) → 不可恢复.

**v3 缓解措施 (SOP-K1 扩展)**:

```
SOP-K1.5 (亚太预防性轮换, v3 新增):

触发条件 (任一):
1. 中国大陆/香港监管政策出现重大不利变化 (老黄监管 alert 标 L2/L3)
2. 中港地缘紧张升级 (老黄合规 alert 触发)
3. 分片 1 或 4 持片人收到任何形式约谈 (即使非分片相关)
4. 老雷或老周长期 (>30 天) 无法 access 分片

行动 (T+0 ~ T+14 天):
1. 老雷召集老沈 + 老孙 + 老黄, 评估"是否预防性转移亚太分片"
2. 决议通过 → 在瑞士或新加坡召集非涉事持片人, air-gap 工作站启动主密钥预防性轮换
3. 新 5 份分片重新分发, 但**亚太分布比例下调**:
   - 选项 A: 分片 4 (中国大陆) 转移到 瑞士 (Taurus 第二柜) → 1 PRC + 1 HK + 1 SG + 2 CH + 1 JP/KR
   - 选项 B: 分片 1 (香港) 转移到 日本/韩国 第二点 → 1 PRC + 0 HK + 1 SG + 1 CH + 2 JP/KR
4. 旧分片即使被强制交出也无价值 (主密钥已轮换)
5. 老唐 audit 记录, 老黄出"预防性轮换合规 memo"

预防性轮换不等于"被触发后才轮换" — 关键是政策风向变化时主动转移, 不等到持片人实际被约谈.
```

**裕度提升路径 (Sprint-N 后, 非 MVP)**:
- 长期方案: 升级 4-of-7, 在原 5 地点基础上加 阿联酋 DIFC + 第二瑞士点; 这需要老黄 §3.3 重新筛 2 个新商业托管商 + 老沈跨 vendor 重新评估. 暂列残留问题 Q15.

### 8.6 BIP39 passphrase 二次加密 (老黄 §9.5 + §4, v3 新增章节)

#### 8.6.1 机制 (老黄 §4.2 + §9.5)

```
master_seed_protected = master_seed XOR BIP39_passphrase_24word

Shamir 3-of-5 分片的是 master_seed_protected, 不是 master_seed 本身.

恢复流程:
1. 收集 3-of-5 分片 → 重建 master_seed_protected
2. 收集 2-of-2 passphrase (老雷 + 老黄) → 还原 passphrase
3. master_seed_protected XOR passphrase → master_seed
4. master_seed 派生出 secp256k1 私钥 (BIP32 path m/44'/60'/0'/0/0)

任一环节缺失:
- 仅有 3 分片 无 passphrase: 只能拿到 protected seed, 无法还原 → 资金无法访问
- 仅有 passphrase 无分片: 单独 passphrase 不构成任何资产
- 仅有 2 分片 + 2 passphrase: 不够 Shamir 门槛 → 仍不可恢复
```

#### 8.6.2 passphrase 持有人 (2-of-2, 老黄 §4.2 接受)

| 持有人 | 介质 | 物理位置 | 备份 |
|---|---|---|---|
| **老雷 (GM)** | 脑记 + 1 份金属板 | 个人保险柜 (与分片 1 物理不同位置, 推荐: 香港但不同银行) | 老雷遗嘱执行人 (家属/律师) escrow 加密副本, 见 SOP-K2 |
| **老黄 (compliance)** | 脑记 + 1 份金属板 | 香港银行保险箱 (与老雷不同银行) | 无 escrow — 老黄失能时 SOP-K4 紧急轮换 |

**关键约束 (老黄 §4.3 接受条件, v3 全部承诺)**:
- [x] passphrase 生成必须**双人离线见证** (老黄 + 老雷, air-gap 笔记本)
- [x] passphrase 不进任何数字介质 (无 KMS / 无云 / 无 password manager)
- [x] passphrase 与 5 分片**永不在同一物理位置**
- [x] passphrase 与 master key 同 cadence **季度轮换** (新 master_seed 生成时, 新 passphrase 同时生成)
- [x] 老黄保留"销毁我手中 passphrase 副本"的合规一票否决权 (SOP-K5, §10.5)
- [x] 项目内部 charter 文件明记: 老黄持有 passphrase **不构成对项目资金的法律所有权**, 不影响合规审查超脱身份

#### 8.6.3 SOP-K5 "核武器级兜底" (老黄 §10.5)

**触发条件 (老黄主动行使, v3 明确写入 SOP)**:
- 收到 Polymarket / 监管机构正式 notice 且红线触发 (R4 / R5 等)
- 项目主体被刑事调查 / 重大民事诉讼且资金有被冻结风险
- 老雷 + 老黄共同判定"资金保全 < 项目合规存活"

**动作**:
1. 老黄销毁手中 passphrase 副本 (金属板物理销毁 — 砸毁/熔毁, 脑记部分尽力遗忘但不可强制保证)
2. 通知老雷, 老雷选择是否同步销毁 (老雷可保留 1 份, 等待事件平息后用 SOP-K4 紧急轮换)
3. 若 2-of-2 passphrase 全部销毁 → master key 无法重建 → 主钱包资金**永久链上锁死** (不可访问)
4. 老唐 audit 记录销毁全过程 (录像), 老黄 24h 内出"SOP-K5 触发事件 memo" 给老雷

**意义**: 极端情况下 (例如执法机关试图强制重建密钥), passphrase 销毁是**最后退路** — 项目宁可资金永久锁死, 也不让密钥被强制重建到非合规用途. 此条权力**专属老黄**, 老雷无权撤销 (合规一票否决的最终极形式).

**触发频率预期**: 极低 (0~1 次项目生命周期). 但必须存在, 因这是 R4/R5 一票否决的最终物理保证.

#### 8.6.4 passphrase 轮换流程

每 90 天 (与 master key 季度轮换同步):

1. **T-7 天**: 老雷 + 老黄 商定轮换窗口 (双方均在香港或新加坡可见面)
2. **T+0 天 (轮换日)**:
   - 老雷 + 老黄 到 air-gap 工作站 (推荐瑞士或新加坡中立地)
   - 双人见证生成新 BIP39 passphrase_v2 (用硬件 RNG, 不用软件 PRNG)
   - 双方各自脑记 + 制作 1 份金属板
   - 与本季度 master key 轮换合并 (老孙 v2 §5.2 紧急轮换流程, 但此处是计划性轮换)
3. **T+1 天**: 旧 passphrase_v1 销毁 (双方各自销毁手中金属板, 老唐录像)
4. **T+7 天**: 老黄出"轮换合规 memo", 老沈 review

### 8.7 跨境运输 SOP (老黄 §6, v3 引用)

详见老黄 sign-off §6, 关键点:
- 所有跨境运输 = 持片人本人手提 + 不托运 + 不申报 (除非主动询问, 实事求是回答"个人加密兴趣的助记词金属板, 无金融价值")
- **绝对禁止快递/邮寄**
- 二次加密 passphrase 的合规红利: 分片金属板单独无法被识别为"密钥", 仅是 24 个英文单词 (老黄 §6.2 第 3 条)
- 数字传输 (分片 3 瑞士 onboarding + 分片 5 USB 制备): 必须 PGP + 商业托管商专用加密通道, VPN 出口 (非中国大陆 IP)

### 8.8 持片人入选条款 (SOP-K3, 老黄 §10.3)

任何分片持有人 (老雷/老周/老吴) 在接受角色前, 必须签署以下条款 (**老黄起草 + 外部律师 review**):

```
1. 我自愿持有 sports-trader-cpp 项目 Shamir 分片之一, 并承担保管义务
2. 我承诺不向任何无关第三方透露分片存在 / 内容 / 位置
3. 我承诺若遇监管/司法约谈, 按 SOP-K1 立即通知老雷 + 老黄
4. 我承诺若我决定退出项目, 至少 90 天前通知, 完成分片回收
5. 我承诺指定 1 名"分片访问遗嘱执行人" (家属或律师), 失能时由其协助回收
6. 我了解持片不构成对项目资金的法律所有权
7. 我接受合规一票否决权 (老黄) 可在紧急情况下要求我销毁手中分片
```

Sygnum / Taurus 商业托管商不签持片人条款 (本身就是商业 escrow 合同), 但合同必须经老黄 §3.3 6 项扫描 + 外部律所 review.

---

## N4. 跨 vendor KMS 方案 (老黄 §8.2~§8.3, v3 升级)

### N4.1 v2 → v3 变更

**v2 §5.5 状态**:
- 主 KMS: AWS us-east-1 单 vendor
- 副 KMS: "建议跨 vendor (GCP / Azure)" — 软描述, 未硬约束

**v3 硬约束**:
- **拒绝 AWS us-east-1 单 vendor** (老黄 §8.3 明确否决)
- 必须跨 vendor (≥ 2 vendor 同时持 age key wrap 副本)
- 必须 ≥ 1 个非美 vendor 总部 (Sygnum / Taurus 瑞士 KMS, 或 GCP 新加坡 region 配合非美管辖)
- 与老沈 v2 (Wave 5 并行) **联签 sign-off** — 此章节最终方案以老沈 v2 输出为准, 本文档仅锁定边界

### N4.2 候选 vendor 矩阵

| Vendor | 总部 | KMS region 选项 | R4 触点 | 老黄判定 | v3 优先级 |
|---|---|---|---|---|---|
| **AWS KMS** | US | us-east-1 / ap-northeast-1 (东京) / ap-southeast-1 (新加坡) / eu-central-2 (苏黎世) | 间接 (CMK 受美国管辖) | 单一不接受, 多 region 仍受美国管辖 | 主选项 1 (受限) |
| **GCP KMS** | US | asia-southeast1 (新加坡) / asia-northeast1 (东京) / europe-west6 (苏黎世) | 间接 (同 AWS) | 单一不接受, 与 AWS 双美互补意义有限 | 主选项 2 |
| **Azure Key Vault** | US | switzerlandnorth (苏黎世) / japaneast (东京) / southeastasia (新加坡) | 间接 (同上, 但 Azure 政企合规框架略不同) | 可作第二副本 | 备选项 |
| **Sygnum Custody (瑞士)** | 瑞士 | 瑞士本地 | **无 R4 触点** (瑞士法人, 不受美国 KMS 政策直接影响) | **强推荐**, 满足"≥ 1 非美 vendor 总部"硬约束 | 主选项 3 |
| **Taurus Protect (瑞士)** | 瑞士 | 瑞士本地 | 同上 | **强推荐**, 与 Sygnum 互为备选 | 主选项 4 |
| **HashiCorp Vault (自托管)** | US (HashiCorp 公司) | 自部署任何 region | 自部署可避美国管辖 (但运维成本高) | 可作长期升级路径 (V2.0) | 备选, Sprint-N |

### N4.3 v3 推荐组合 (3 vendor, 2-of-3 unwrap)

**主 wrap**: AWS KMS (us-east-1) — 与 trader 同 region, 性能最优 (delivery latency 主路径)
**副 wrap 1**: **Sygnum 或 Taurus (瑞士)** — 满足非美 vendor 总部硬约束, 法律隔离美国管辖
**副 wrap 2**: GCP KMS (asia-southeast1, 新加坡) — 跨美国 vendor 多元化, 亚太 region

**Unwrap 门槛**:
- 平时 signer 启动: 仅用 AWS 主 unwrap (1-of-3, 性能优先)
- AWS 锁定 (US 法院冻结 / 账户风控): 走 Sygnum 副 unwrap (1-of-3, 跨 vendor failover)
- 极端场景 (AWS + Sygnum 双锁): GCP 副 unwrap (1-of-3, 终极兜底)
- **不上 2-of-3 多签 unwrap** (老沈 v2 review 后定; 多签 unwrap 增加签名链路延迟 + 复杂度, 单签 + vendor switch 更实用)

### N4.4 跨 vendor 同步流程

1. **age key 生成**: 老沈 + 老雷 双签生成 age key (air-gap, 与 master_seed 同 air-gap)
2. **同时 wrap 三次**: 用 AWS CMK / Sygnum CMK / GCP CMK 各 wrap 一次, 输出 3 个 wrapped blob
3. **三 blob 分别存** (与 age 加密的密钥文件同位置, 不再分发分片):
   - AWS wrapped blob: 与 signer binary 同 region 部署 (us-east-1 EBS)
   - Sygnum wrapped blob: 瑞士 Sygnum 设施
   - GCP wrapped blob: GCP asia-southeast1 bucket (KMS encrypted)
4. **季度轮换**: age key 季度轮换时, 三 vendor 同步重新 wrap (单次轮换 = 3 vendor 操作 + 3 blob 更新)
5. **CMK 监控**: 每 vendor CMK 任何变更 (key policy / grant) 告警 (CloudTrail + GCP audit log + Sygnum portal)

### N4.5 与老沈 v2 联签 (老黄 §8.3 要求)

**老孙 v3 锁定边界**:
- ≥ 2 vendor (硬)
- ≥ 1 非美 vendor 总部 (硬, Sygnum/Taurus)
- AWS 单 vendor 拒绝 (硬)
- 跨 region 不够 (AWS 全 region 都受美国管辖, 老黄 §8.2)

**老沈 v2 待交付 (Wave 5 并行)**:
- 具体 vendor 组合 (Sygnum vs Taurus, GCP vs Azure)
- Unwrap failover SLO + 自动化方案 (是否自动 vendor switch, 还是人工触发)
- 3 vendor 同步 wrap 的运维流程 (老吴 owns 实施)
- CMK key policy / IAM 细节 (与 v2 §6 老吴 AWS IAM 协调)

**联签 sign-off 流程**:
1. 老沈 v2 出 → 老孙 cross-check 与本 v3 §N4 边界一致 → 老黄 R4 复核 → 老雷 final
2. 任一边界违反 (例如老沈 v2 选回单 vendor) → 老孙退回 + 老黄一票否决

### N4.6 IAM 协调 (与 v2 §6 老吴对接)

v2 §6 老吴 IAM JSON spec 仅覆盖 AWS, v3 升级要求老吴**跨 vendor IAM 三套**:
- AWS IAM (v2 §6 原方案, 不变)
- GCP IAM (signer service account, 仅 `cloudkms.cryptoKeyVersions.useToDecrypt` 单权限对单 CryptoKey)
- Sygnum API key (Sygnum portal, view + decrypt 权限, MFA 强制)

**老吴新 deadline**: v2 §6 原 deadline 2026-06-26 (Sprint-2 启动前), v3 加跨 vendor 三套 IAM, **deadline 不延** — 老吴需在同窗口出三套 IAM. 若不可行, 老雷 + 老沈 + 老吴 三方协商裁决.

---

## 9. v3 整改验收 checklist (5 整改 + N4 升级)

### 9.1 老黄 5 项硬整改 (§9 sign-off 条件)

- [x] §8.3 完全替换为 5 地点真实方案 (HK / SG / CH / CN / JP-or-KR)
- [x] §8.3 老沈/老孙/老黄 全部退出持片名单, 改老雷/老周/老吴 + 瑞士商业托管 1 份
- [x] §8.2 加硬约束 (3-non-PRC / 0-US / 0-OFAC / 0-UK)
- [x] §8.4 演练改为季度/半年/年度 三级
- [x] §8.6 新增 BIP39 passphrase 二次加密 (老雷+老黄 2-of-2)
- [x] §8.6.3 SOP-K5 销毁权写入 (老黄一票否决兜底)
- [x] §8.5 新增 Top 1 风险缓解 (SOP-K1.5 亚太预防性轮换)
- [x] §N4 跨 vendor KMS 升级 (拒绝 AWS 单 vendor, ≥ 2 vendor + ≥ 1 非美总部)

### 9.2 SOP 引用清单 (老黄 §10)

v3 引用并锁定老黄 §10 全套 SOP-K1~K5:
- SOP-K1: 单一持片人被约谈 (§10.1, v3 §8.5 扩展为 K1.5 加预防性轮换)
- SOP-K2: 持片人意外失能 (§10.2)
- SOP-K3: 持片人入选条款 (§10.3, v3 §8.8 引用)
- SOP-K4: passphrase 持有人变更 (§10.4)
- SOP-K5: 合规一票否决 passphrase 销毁权 (§10.5, v3 §8.6.3 引用)

### 9.3 跨阻塞项进展同步 (引用占位, 不替他人写)

| Q | 内容 | Owner | 状态 | v3 引用方式 |
|---|---|---|---|---|
| Q9 | nonce manager 接口 + SLO | 老叶 | 进行中 (Wave 5 已派) | v2 §4.3 + B7, v3 不变 |
| Q10 | Receiver 白名单合约地址 | 老叶 + 老李 | 进行中 (Wave 5 已派) | v2 §3.3, v3 不变 |
| Q11 | Polymarket EIP-712 schema | 老李 | 进行中 (Wave 5 已派) | v2 §3 IPC schema, v3 不变 |
| Q1 | AWS IAM JSON spec | 老吴 | v3 §N4.6 升级为跨 vendor 三套 | v3 §N4.6 督办 |

Q9/Q10/Q11 老孙等他们 Sprint-2 第 1 周交付 → 老孙集成进 signer 实现, 与 v3 跨境合规独立路径, 互不阻塞.

### 9.4 老沈 v3 cross-check checklist

老沈需在其 v2 (Wave 5 并行) 出后 cross-check 本 v3:

- [ ] 跨 vendor KMS 边界一致 (≥ 2 vendor + ≥ 1 非美)
- [ ] 跨 vendor unwrap failover 方案与 v3 §N4.3 单签 vendor switch 兼容
- [ ] CMK key policy 三套 (AWS/GCP/Sygnum) 与 v3 §N4.4 同步流程兼容
- [ ] signer binary 编译期 cert pin (B2) 升级为多 vendor 多 pin (AWS / GCP / Sygnum 各 SPKI hash 数组)
- [ ] 季度 SBOM review (v2 B8) 加 GCP SDK + Sygnum API client crate

### 9.5 老黄 v3 final sign-off 条件 (老黄 §11 + v3 新增)

- [x] v3 §8.3 完全替换为本文件 §8.3 (老黄 §9.1 内容)
- [x] v3 §8.2 加硬约束 (老黄 §9.2)
- [x] v3 §8.4 演练三级 (老黄 §9.3)
- [x] v3 §8.6 新增 passphrase 章 (老黄 §9.5)
- [x] v3 §10 Q8 标记关闭 (沿用 v2)
- [x] v3 §N4 跨 vendor KMS 升级 (老黄 §8.3 要求)
- [ ] 商业托管商 (Sygnum / Taurus) 完成老黄 §3.3 6 项扫描, 进入正式选型 — **待 Sprint-2 老黄主导, 老雷对接律师**
- [ ] 老吴长期外派可行性确认 (分片 5 位置); 若不可行, 备选: 第二家瑞士托管 — **待老吴 + 老雷 HR 确认, T+10 天**
- [ ] 老周入选条款签署 (SOP-K3) — **待 Sprint-2 启动前**
- [ ] 老雷 (GM + 持片人 + passphrase 持有人) 签署本方案 + passphrase 持有人条款
- [ ] 老黄 (compliance) 签署 passphrase 持有人条款 (§8.6.2)
- [ ] 老沈 v2 跨 vendor KMS 与本 v3 §N4 联签 — **Wave 5 并行**

**全部满足后老黄 final sign-off**. 否则不进 Sprint-2 实施.

预计 final sign-off: T+10 天 (老黄 §11 时间窗).

---

## 10. 残留开放问题 (v3 update)

v2 §8 残留 14 个问题 + v3 新增 1 个:

| # | 问题 | 阻塞? | Owner | v3 状态 |
|---|---|---|---|---|
| Q1 | AWS IAM JSON spec | 是 (Sprint-2) | 老吴 | **v3 升级**: 跨 vendor 三套 IAM (AWS + GCP + Sygnum), deadline 不延 2026-06-26 |
| Q2~Q14 | (沿用 v2) | (沿用) | (沿用) | v3 不变 |
| **Q15** (v3 新增) | 4-of-7 升级路径 (长期, 提升 0 裕度 → 1 裕度) | 否 (Sprint-N) | 老孙 + 老黄 | Sprint-N 评估, 加 阿联酋 DIFC + 第二瑞士点 |
| **Q16** (v3 新增) | Sygnum vs Taurus 选型 + 合同 review | 是 (Sprint-2) | 老黄 (合规面) + 老雷 (对接外部律师) | T+10 天 |
| **Q17** (v3 新增) | 老吴长期外派日本/韩国可行性 | 是 (Sprint-2) | 老雷 + 老吴 (HR/签证) | T+10 天 |
| **Q18** (v3 新增) | 老周 SOP-K3 入选条款律师 review | 是 (Sprint-2) | 老黄 + 外部律师 | T+10 天 |

**v3 残留: 18 个** (4 个新增 Q15~Q18, 1 个 Q1 升级). Sprint-2 启动前必须关闭 Q1/Q9/Q10/Q11/Q16/Q17/Q18 七项.

---

## 11. v3 sign-off 路径 + 时间线

```
T+0  (2026-05-28): 老孙 v3 提交本文档
T+1  (2026-05-29): 老黄 review v3 §8/§N4 → 一审 sign-off (条件性)
T+2  (2026-05-30): 老沈 v2 跨 vendor KMS 出 → 老孙 cross-check
T+3  (2026-05-31): 老沈 + 老孙 联签 §N4
T+5  (2026-06-02): 老雷对接外部律师, Sygnum/Taurus 合同初谈
T+7  (2026-06-04): 老吴外派可行性 + 老周入选条款 出
T+10 (2026-06-07): 老黄 final sign-off
T+10 (2026-06-07): 老雷 GM 终审签字
T+10 ~ Sprint-2 启动 (2026-06-26): 实施期 — 商业托管商 onboarding + 持片人入选 + 首次 air-gap 工作站搭建
Sprint-2 启动 (2026-06-26): signer 实现开始, 跨境合规与 signer 实现并行不阻塞
```

---

## 附录 A. 与 v2 章节对应 (沿用 + 修订)

| v2 章节 | v3 状态 |
|---|---|
| §1 v1→v2 变更摘要 | v2 保留, v3 §0 新增 v2→v3 摘要 |
| §2.1~§2.8 B1~B8 修复 | v2 不变 |
| §3 IPC schema | v2 不变 |
| §4 HA 设计 | v2 不变 |
| **§5 Shamir 跨境合规** | **v3 §8 完全重写, 取代 v2 §5** |
| §6 AWS IAM (老吴) | v3 §N4.6 升级跨 vendor 三套 |
| §7 验收 checklist (B1~B8 STRIDE) | v2 不变, v3 §9 追加整改 checklist |
| §8 残留问题 | v3 §10 update (新增 Q15~Q18) |
| 附录 A 核心库选型 | v2 不变, v3 §9.4 老沈 cross-check 加 GCP SDK + Sygnum client |
| 附录 B 参考 | v2 不变 |
| 附录 C STRIDE 对应 | v2 不变 |

---

## 附录 B. v3 与老黄 sign-off §11 验收条件逐条对应

| 老黄 §11 条件 | v3 章节 | 状态 |
|---|---|---|
| §8.3 完全替换为本文件 §9.1 | v3 §8.3 | done |
| §8.2 补充本文件 §9.2 硬约束 | v3 §8.2 | done |
| §8.4 演练改三级 | v3 §8.4 | done |
| 新增 §8.6 passphrase | v3 §8.6 | done |
| §10 Q8 关闭 | v3 §10 (沿用 v2) | done |
| 老沈 §4.3 跨 vendor KMS | v3 §N4 | done (边界锁定, 等老沈 v2 联签) |
| 商业托管商 6 项扫描 | v3 §8.3 引用老黄 §3.3 | 待 Sprint-2 |
| 老吴外派可行性 | v3 Q17 | 待 T+10 |
| 老周入选条款 | v3 §8.8 + Q18 | 待 T+10 |
| 老雷签署 | — | 待 T+10 |
| 老黄签署 passphrase 持有人条款 | v3 §8.6.2 | 待 T+10 |

---

*v3 提交: 2026-05-28*
*Owner: 老孙 (crypto-signing-expert)*
*预计 final sign-off: 2026-06-07 (T+10)*
*Sprint-2 启动: 2026-06-26 (与 signer 实现并行, 跨境合规不阻塞)*
*v1 / v2 保留为 review trail, 不删*
