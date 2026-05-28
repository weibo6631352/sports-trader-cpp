# 美国主体 Polymarket Prop Trading 合规可行性 v1

- Owner: 老黄 (compliance-legal)
- Date: 2026-05-28
- Status: Research (非决策, GM 决策待外部律师 confirm)
- 验收人: 老雷 (GM)
- 关联: 用户 2026-05-28 指令 + `docs/ADR/2026-05-28-gm-policy-jurisdictional-deferral.md`
- 免责: 老黄不是注册律师, 本文档为信息汇总 + 风险识别, **不构成法律意见**

---

## 1. 核心问题 + GM 关切

GM 老雷原话 (2026-05-28): "我们到时候公司和服务器搬迁到美国 polymarket 总部附近, 应该是地域合规吧"

**4 层假设, 需逐层验证**:
1. 服务器搬美国 = 延迟优化 (技术层 OK, 老吴 us-east-1 已确认)
2. **服务器在美国 ≠ entity 必须在美国** (两件事, GM 直觉混淆)
3. 美国 entity → 受 SEC/CFTC/FinCEN/IRS 联邦法管辖 (关键风险面)
4. 美国 entity 能否 onboard Polymarket → 取决于 Polymarket ToS + KYC 实操, 不只 CFTC 和解原文

GM "搬美国就合规"直觉需要打挑战, 本调研重点.

---

## 2. Polymarket 2022 CFTC 和解事实

**公开事实 (CFTC PR 8478-22, 2022-01-03)**:
- 被告: Blockratize, Inc. (d/b/a Polymarket.com), Delaware 注册
- $1.4M civil monetary penalty + cease and desist
- 罪名: 未注册的 binary options DCM 操作 (CEA §5h, CFTC Reg 37)
- 命令: wind down 所有"不合规"event contracts, 仅留 CFTC 批准类型

**和解解读 (需律师 confirm)**:
- 禁令主语: **平台** Polymarket, 限制其 "向美国 person 提供未注册合约"
- 关键: 限制平台 offering 行为, **不是直接限制 US entity 的"交易行为"**
- 但平台为合规自行屏蔽美国 IP + 拒美国 entity onboard (平台合规选择, 非 CFTC 命令)

**对我们 (prop trader, 自营, 非用户服务方) 意义** (置信度: 中):
- CFTC 和解主语是平台, 不直接针对交易方
- **能否 onboard 取决于 Polymarket 当前 ToS / KYC 实操**, 不是 CFTC 原文
- [需律师 confirm] CEA 下美国 entity 作 taker 是否构成"参与未注册期货市场" — retail 保护条款是否延及机构

---

## 3. Polymarket 当前主体 + ToS

**主体结构 (公开 + 部分 confirm)**:
- **Blockratize, Inc.** — Delaware 美国主体, 2022 和解被告
- **Polymarket Ltd.** — [需律师 confirm 注册地, 推测 Panama / BVI / Cayman 离岸主体接服务美国境外用户]
- 用户 ToS 当前由离岸 Polymarket Ltd. 签, 美国 IP 屏蔽
- 2024-2025 ToS 有修订, 以官网当前版为准 [@老李 在 polymarket-api-spec 中实时抓]

**ToS 关键条款 (公开版本 2026-01 知识)**:
- §2 Eligibility: 18+, **不得是 US person** (Regulation S 定义), 不在 OFAC 国
- §3 KYC: 大额 / 提现触发, 走 Polymarket KYC vendor
- §15 Choice of Law: 离岸主体法 (e.g. Panama)

**对 entity onboard 路径**:
- 美国 entity (Delaware Inc. / Wyoming LLC) 注册地址在美国 → **大概率被 §2 拦下**
- 判定 US person 不只 IP, 含 entity 注册地 + 控制人国籍
- [需律师 confirm] Polymarket KYC 是否接受 US-formed entity — **最大未知数**

---

## 4. 2024-2026 监管演变 (Trump CFTC stance)

| 日期 | 事件 |
|---|---|
| 2022-01 | CFTC v. Blockratize $1.4M 和解 |
| 2023 | Polymarket 持续屏蔽美国 IP, 离岸用户增长 |
| 2024-10 | Kalshi v. CFTC (DC Circuit) 判决可上市 election event contracts |
| 2024-11 | Trump 当选, 行业期待监管松绑 |
| 2024-11 | Polymarket NYC 总部 FBI 突击搜查创始人, 调查美国用户 evasion |
| 2025-01 | Trump 上任, 提名 Brian Quintenz (亲 crypto) 任 CFTC Chair [需 confirm 就任时间] |
| 2025-? | CFTC 立场可能松绑, 但和解条款未撤 [需律师 confirm 最新 enforcement] |
| 2025-? | Polymarket 是否重新对美开放 [需 confirm, 截至训练知识未确认] |

**对我们意义** (置信度: 低, 监管流动性大):
- Trump CFTC = 利好 trend ≠ 法律约束自动解除
- 即使 CFTC 不 enforcement, Polymarket Ltd. 不会单方面冒险接美国 entity
- 要等 Polymarket 官方公布 "institutional 重新开放" 才有通道
- [需律师 confirm] CFTC 是否修订 Reg 38/40 允许 event contracts as DCM
- [需 confirm] Polymarket 是否在 acquire CFTC-registered DCM (传闻收购 QCX/QCEX 等)

---

## 5. 美国 entity 注册州对比 (DE / WY / NJ / NY)

| 维度 | Delaware | Wyoming | New Jersey | New York |
|---|---|---|---|---|
| Franchise tax | $300-400/yr LLC | **$60/yr LLC** | ~$500/yr | ~$200/yr |
| 隐私 (member 公开?) | 不公开 | **强不公开** | 公开 | 公开 |
| Crypto-friendly | 中性 | **强 (Wyoming DAO LLC / SPDI)** | 中性 | **极严 (BitLicense)** |
| State income tax | 仅 DE 源收入 | **0%** | 6.37-10.75% | 6.5-7.25% + NYC 加 |
| Polymarket 同州? | 是 (Blockratize) | 否 | 否 (但 GM 指 NYC 隔江) | 是 (办公) |
| Prop trading 门槛 | 无 | 无 | 无 | BitLicense 风险 |

**初步建议 (置信度: 中)**:
- **Wyoming LLC** 综合最优: 0% state tax + 隐私 + crypto-friendly + 低成本
- **Delaware Inc.** 适合未来融资 (VC 默认), 当前 prop trading 不需要
- **New York 排除**: BitLicense + NYDFS MTL 风险高, USDC 进出可能触发
- **New Jersey 中性**, 无明显优势

**关键: 注册州 ≠ 联邦法**. SEC/CFTC 联邦, 选州只影响 state tax + state license, 联邦义务相同.

---

## 6. 美国 entity 合规义务清单

| 义务 | 触发判断 | 置信度 |
|---|---|---|
| CFTC DCM/SEF 注册 | **不触发** (我们 taker, 非场所) | 高 |
| CFTC CPO/CTA/FCM | **不触发** (自营, 不替他人管钱) | 高 |
| SEC RIA / RA | **不触发** (自营 + AUM < $150M) | 高 |
| FinCEN MSB | **可能触发** (USDC 大额进出 = money transmission?) | 中 [律师 confirm] |
| BSA / AML | 跟随 MSB 判定 | 中 |
| FBAR (FinCEN 114) | **可能触发** (Polymarket 离岸账户 USDC > $10k?) | 中 [律师 confirm] |
| FATCA (Form 8938) | 跟随 FBAR | 中 |
| IRS Form 1099 / Schedule D | 触发 (自我申报) | 高 |
| IRS crypto (Form 8949) | **必触发** (Polymarket = crypto disposition) | 高 |
| Wyoming MTL | [律师 confirm WY 对 prop trading USDC 持有要求] | 低 |

**最大未知 (P0 律师 confirm)**:
1. Polymarket 交易 IRS 分类: options / 赌博 / crypto disposition? 影响税率 + 申报表
2. FBAR 触发: Polymarket 余额是否构成 "foreign financial account"
3. MSB 触发: USDC 大额进出是否 "money transmission"

---

## 7. Polymarket affiliate / MM 程序

(截至 2026-01 知识, @老李 verify polymarket-endpoint-matrix)

- Polymarket CLOB 有 maker rebate (sports_fees_v2 含 25% maker reward = 此项)
- 程序对所有 maker 开放, **无 institutional 专属程序公开**
- [需 confirm] 是否有正式 MM Program (2024 有讨论, 未确认上线)
- 与 Polymarket 法务对接: business@polymarket.com / institutional inquiry, 但 US entity 当前不太可能被 onboard [需 confirm]

**意义**: Maker rewards 25% 是协议层 distribution, 不分国籍, 但要能 onboard 才拿得到. 美国 entity onboard 不了 → 拿不到. **离岸 entity 隐藏优势之一**.

---

## 8. 风险评估 + 与离岸主体对比

| 维度 | 美国 entity (Wyoming LLC) | 离岸 entity (BVI / Cayman / Panama) |
|---|---|---|
| Polymarket onboard | **极不确定** (当前不接受) | **可行** (主流用户结构) |
| 设立成本 | $500-2000 + 年费 $60-400 | $2k-5k + 年费 $1.5k-3k |
| Bank (法币端) | 美国 bank 容易 | 美国 bank 难, 需离岸 (Sygnum/瑞士) |
| 税务 | US person 全球纳税, LLC pass-through | 离岸 0% corp tax, 股东仍可能美国纳税 |
| 合规年成本 | $5k-30k | $10k-50k |
| Polymarket ToS 友好 | **不友好** | 友好 |
| 服务器位置约束 | 美国 | 任意 (仍可放美国) |
| 退出 / 清盘 | 美国法 | 离岸法 |

**关键观察**: 服务器在美国 ≠ entity 必须在美国. 老吴跨洋部署 v0.1 已支持 us-east-1 + 离岸 entity, **当前最优组合**, 不需要 entity 也搬美国.

**风险评级**:
- 美国 entity: 高风险 (Polymarket onboard 不确定 + FBAR/MSB/IRS 复杂) — **不推荐**
- 离岸 entity + 美国服务器: 中风险 (跨境合规 + bank 挑战) — **GM 决议默认路径**
- 个人主体 + 美国服务器: 当前阶段最实际, 盈利大后升级

**90 天预警 (如 GM 选美国 entity)**:
- 外部 US 律所 (Goodwin/Cooley/Cleary 等 fintech 强所) 出 jurisdictional opinion: 4-8 周, $15-30k
- IRS 加密税顾问 (Chainalysis legal / TaxBit): 2-4 周
- Polymarket 正式 institutional inquiry: reply 不可控

---

## 9. 我老黄当前判断 (置信度: 中)

1. **GM "搬美国就合规" 直觉部分对部分错**: 服务器对, entity 错
2. **美国 entity 最大障碍不是 CFTC 直接禁我们, 是 Polymarket 不接受美国 entity onboard**
3. **离岸 entity + 美国服务器** 是当前最合规 + 实用组合, 与 jurisdictional-deferral ADR 兼容
4. **Trump CFTC 松绑是 trend 不是 fact**: 行业期待 vs 法律约束有时间差
5. **置信度: 中**. 法律事实 (CFTC 和解, ToS) 高置信; 当前 stance + Polymarket 政策低置信, 必须律师 confirm

---

## 10. 必须找外部律师 confirm 的问题清单 (8 条, 预算 $15-30k)

1. **[Polymarket onboard]** Polymarket Ltd. 当前 ToS 是否接受 US-formed entity (WY LLC / DE Inc.) onboard? KYC 是否因 entity 注册州拒绝?
2. **[CFTC 和解适用]** 2022 和解 "prohibition on offering binary options to US persons" 中 "US person" 是否包含 US entity 作为 **交易对手方** (taker), 还是仅限 retail 用户身份?
3. **[FBAR/FATCA]** US person 控制的 LLC 持有 Polymarket 离岸账户 USDC 余额是否触发 FBAR (FinCEN 114) + FATCA (Form 8938)?
4. **[MSB]** Prop trading entity 通过 self-custody wallet 进出 USDC 是否构成 FinCEN MSB activity?
5. **[税法分类]** Polymarket event contracts IRS 视角: (a) Section 1256 contracts? (b) 普通 options? (c) gambling? (d) crypto disposition (Notice 2014-21)? 影响税率 + 表格
6. **[CFTC stance 更新]** 2025-2026 期间 CFTC 是否发 enforcement priority memo / no-action letter 关于预测市场? Polymarket 是否在谈判 register as DCM 或收购 DCM (Kalshi/QCEX 等)?
7. **[离岸 vs 美国 opinion]** BVI LLC vs Wyoming LLC 同样 prop trading 业务, 法律风险 + 税负 + onboard 概率定量对比
8. **[Wyoming 特殊]** WY 是否对持 USDC 的 prop trading LLC 要求 MTL? Special Purpose Depository Institution (SPDI) 是否对我们有意义?

---

## 11. 给 GM 的下一步建议

**短期 (0-30 天)**:
1. **不要** 现在启动美国 entity 注册
2. **维持** jurisdictional-deferral 立场: 服务器 us-east-1 主 + entity "未决"
3. 盈利稳定前: 个人 onboard Polymarket (KYC 已做) + IRS Schedule D 申报

**中期 (30-90 天, 老雷决定启动时)**:
4. 外部 US fintech 律所 RFP, 出 jurisdictional opinion (问题 §10), $15-30k
5. 同步 BVI/Cayman 律所对比 opinion, $5-10k
6. Polymarket 正式 institutional inquiry (generic counsel, 不暴身份)
7. 独立 IRS crypto 税顾问出 memo (问题 §10.5)

**长期 (90+ 天)**:
8. opinion + Polymarket reply 齐, GM 决策注册地
9. entity 注册 + bank + KMS 重布局 (老孙/老沈协同)
10. 全员 ToS / 合规手册重新签收

**老黄个人偏好 (非法律意见)**:
- 倾向 **BVI/Cayman LLC + 美国服务器**, 与 Polymarket 主流用户结构一致 + 与 jurisdictional-deferral 兼容
- 美国 entity 唯一硬优势 banking 便利, 可用 USDC + 离岸 bank 替代
- 最终决策需律师 confirm + GM 拍板

---

## 附录 A: 协作分工

| 范围 | Owner |
|---|---|
| Polymarket ToS 实时抓取 | @老李 |
| Polymarket onchain 合约 / 钱包 | @老叶 |
| 服务器 us-east-1 部署 | @老吴 |
| 离岸/美国 bank 选型 (未来) | @老雷 + 老黄 |
| 外部律所 RFP | @老雷 主导, 老黄起草问题清单 |

## 附录 B: 版本历史

| 版本 | 日期 | 变更 |
|---|---|---|
| v1 | 2026-05-28 | 首版, 应 GM Wave 13 任务 |

**免责声明**: 老黄非注册律师, 本调研为信息汇总 + 风险识别, **不构成法律意见**. entity 注册 / 申报决策须经外部注册律师 confirm.
