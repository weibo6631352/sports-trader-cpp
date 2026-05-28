# 老黄 Sprint-1 Retro 发言 (合规法务)

- Persona: 老黄 (compliance-legal)
- Date: 2026-05-28
- 上下文: Sprint-1 Retro, 听取义务 + 双向收口
- 行长上限: ≤ 200 行
- 状态: 真实发言, 不灌水, 不糊弄

---

## 0. 30 秒立场 (给老雷 + 全员)

1. **Sygnum 6/11 截止: 我能干, 但不一票答应**. 老黄 + 老雷双签的"初步联系", 我 6/11 之前能给出 contact-made + 合同模板入手, 不保证报价回收. 见 §1.
2. **Goalserve odds 商务: 我接 owner, 但要分线**. 我管合规 + KYC + 合同, 销售 follow-up 我和老胡分, 老胡当通信枢纽. timeline 见 §2.
3. **BIP39 passphrase 2-of-2: 我准备好接收, 但 §4.3 6 条硬条件先满足, 老雷书面同意我才接**. 见 §3.
4. **R4 美国元素: 老孙 v4 AWS us-east-1 我不签, 老沈 v1 GCP SG 主 我条件接受**. Sygnum 是出路, 这是 §4 + 我前文 Shamir 文档 §8.2 一致立场.

---

## 1. Sygnum 6/11 初步联系 — 我能扛, 但不是一个人扛

### 1.1 我承诺能在 6/11 前交付的

- [ ] **Contact made**: Sygnum + Taurus 销售各一封 cold email + 一次电话 (我手动发, 不走自动化)
- [ ] **合同模板入手**: 拿到两家的 enterprise custody MSA 草案 (PDF / docx)
- [ ] **KYC 资料清单**: Sygnum / Taurus 各自的 onboarding 文档 + KYC 表 (主体身份 / UBO / source of funds)
- [ ] **价格 RFP**: 给两家发 RFP, 列出我们的 wrap+escrow use case, 等报价回收 (6/11 之前能拿到的算我赚)

### 1.2 我承诺不到的

- [ ] **报价 final**: Sygnum 报价回收周期一般 2-4 周, 6/11 拿不到 final
- [ ] **法律 sign-off**: 外部律师 review 在 7/15 deadline (ADR 已写), 我 6/11 不签合同
- [ ] **谈定 onboard 路径**: KYC 实质审查要等老雷主体身份 + UBO 文件备齐

### 1.3 我需要 6/11 之前从老雷拿到的

| # | 需要的 | Owner | 不到位的后果 |
|---|---|---|---|
| 1 | 主体身份决定: 自然人 / HK Co / BVI / 开曼 (我 Shamir §8.1 写没决定) | 老雷 | Sygnum KYC 卡死, 拿不到合同模板 |
| 2 | UBO 同意书 (老雷自身配合 KYC) | 老雷 | 同上 |
| 3 | 业务描述 (Polymarket 不写, 写"量化交易 + 加密资产托管"通用描述) | 老雷 + 我起草 | 触合规审查关键词, 一次预审被拒走流程 ≥ 6 周 |
| 4 | 老胡协助 follow-up 通信 (我 7x24 alert 占位, 销售 ping-pong 我接不住) | 老胡 | 6/11 跟进掉链子 |

### 1.4 风险声明 (我提前说)

- **Sygnum / Taurus 任何一家在 KYC 阶段问我们 Polymarket 关键词触发预审拒绝 → 6/11 截止失败, 走 ADR §红线升级**
- **6/11 我交付的 contact-made + 合同模板入手 = 中间态, 不构成 GM 承诺 2027-02-26 final 满足**
- ADR 已写 "任一节点延迟 > 4 周 → 自动升级三方紧急会议", 我接受这条

**结论: 6/11 截止我能扛 §1.1 四项. §1.2 三项不进我承诺. 老雷 §1.3 配合 4 项不到位 = 我不背锅.**

---

## 2. Goalserve odds 商务 — 接 owner, timeline 如下

### 2.1 我的分工 (vs 老胡)

| 内容 | 我 | 老胡 |
|---|---|---|
| 合规 review (Goalserve ToS / 数据使用边界) | ✓ | 协助看 |
| 价格谈判 (写 RFP, 给底价范围) | △ 起草, 老胡过 | ✓ 实际谈 |
| 销售 follow-up 通信 (跟进 / 催单 / 邮件 ping-pong) | × | ✓ |
| 合同 review (law / clause) | ✓ | × |
| KYC + 公司主体对接 | ✓ | × |
| 内部 PRD 翻译 (商务方案 → 工程项) | × | ✓ |

### 2.2 Timeline (我承诺这条)

| 节点 | 日期 | 动作 | Owner |
|---|---|---|---|
| T+0 | 2026-05-29 (明天) | Goalserve sales cold email + RFP 起草 | 我 |
| T+3 | 2026-06-01 | Sales 回邮件确认收到 | 老胡 follow |
| T+7 | 2026-06-05 | 拿到 odds tier 价格表 + RPS 商务限上限 | 老胡 |
| T+10 | 2026-06-08 | 我合规 review 完 Goalserve odds ToS (R9 数据转售边界确认) | 我 |
| T+14 | 2026-06-11 | 给老雷书面方案 + 价格 (ADR 已写截止) | 我 + 老胡 |

### 2.3 我提前 flag 的 3 个风险

1. **Goalserve odds 价格 tier**: 行业惯例 $200-$2000/月分档, 我们 RPS 1-2 安全档可能在低 tier. 但 sales 可能强推 enterprise tier ($5k+/月) 套, 我 RFP 写死"个人量化, 非 redistribution"防套高.
2. **R9 数据转售边界**: Goalserve odds 接到后, 任何缓存 > 7 天 + 对外分发 = 触红线. 我合同 review 把这条写到内部 SOP, 工程侧 (老叶 / 小段) 同步.
3. **代理产权 `127.0.0.1:7890`**: ADR 已点 老吴 6/4 前确认产权归属. 这不是我管的, 但合规视角下若代理产权属于第三方 = 多 1 个数据中介合规面, 我需要老吴 6/4 给我答案.

---

## 3. BIP39 passphrase 2-of-2 (老黄 + 老雷)

### 3.1 立场: 接受, 但条件不变

我 Shamir 文档 §4 已经讲清: **拒绝持 Shamir 分片, 接受 passphrase 角色**. 这条今天不变.

我的接受 = 老沈 v1 N3 建议升级版, 把 Shamir 从 "3-of-5" 提到 "3-of-5 + 2-of-2 (passphrase)" 双层防御.

### 3.2 准备状态 (我现在的)

- [x] 心理准备: 完成. 我知道我承担什么 (脑记 + 1 张金属板)
- [x] 物理准备: 计划用香港某家银行保险箱 (与老雷分片 1 物理不同位置, 我 Shamir §5.1 写死)
- [ ] 联合 setup: **等老雷 + 老沈 6/4 之前确认 air-gap 笔记本物理准备 + 时间**
- [ ] 律师 charter: **等外部律师起草"老黄持 passphrase 不构成项目资金法律所有权"声明** (Shamir §4.3 第 6 条硬要求)

### 3.3 §4.3 6 条硬条件复述 (老雷 sign 前必须满足)

1. [ ] passphrase 生成必须双人离线见证 (老黄 + 老雷, air-gap 笔记本)
2. [ ] passphrase 不进任何数字介质 (无 KMS / 无云 / 无 password manager)
3. [ ] passphrase 与 5 分片**永不在同一物理位置**
4. [ ] passphrase 轮换 cadence: 季度 (90 天) 同 master key 同步
5. [ ] 紧急事件下我有权单方决定"销毁我手中 passphrase 副本" (合规一票否决兜底, SOP-K5)
6. [ ] 项目内部 charter 文件明记: 老黄持 passphrase 不构成对项目资金的法律所有权

**老雷必须 6 条全签. 6/4 之前书面确认. 缺任意 1 条我撤回 passphrase 角色, 走外部律师 escrow 兜底.**

### 3.4 时间预期

- 6/4: 老雷 6 条书面 sign
- 6/11: 外部律师 charter 起草完
- Sprint-2 启动前 (6/26): air-gap 笔记本 setup + passphrase 生成仪式 (老黄 + 老雷 物理同场)
- Sprint-2 第 1 周: 写入 老孙 v4 §8.6 (老孙已经预留位置)

---

## 4. R4 美国元素 — 老孙 v4 / 老沈 v1 的我的最终态度

### 4.1 老孙 v4 (C++ 重写) 合规态度

老孙 v4 §6 明确写 "跨境 Shamir / passphrase / 跨 vendor / Sygnum / HA **完全不变**, 跨语言无关". 我的判定:

- **合规层面 v4 = v3, 我对 v4 跨语言无新合规反对**
- v4 §8.3 PR review 接力时间表里写 "T+2 老黄 review §6 合规保留项 → final accept (无合规变化, 跨语言)" — 这条我**接受**
- 但 v4 §5.2 启动期 KMS unwrap 的 vendor 顺序 (AWS us-east-1 → ...) 我**不接受** (见 §4.2)

### 4.2 老孙 v4 §5.2 + §6.3 AWS us-east-1 主 wrap — 不签

老孙 v4 §6.3 写:
> 主 wrap: AWS KMS (us-east-1) — 与 trader 同 region, **过渡方案**

**我不签 us-east-1 主**. 三条理由 (Shamir §2.4 已写, 这里复述):
1. AWS + us-east-1 = 双重美国元素 (公司 + region)
2. R4 红线写 "**任何环节带美国元素全部 block**" — "环节" 包括启动期 unwrap 凭据
3. ADR 已签 Sygnum 2027-02-26, 但 Sygnum 接入前的"过渡方案" 不能再走 us-east-1, 那是回到原点

### 4.3 老沈 v1 (跨 vendor KMS) — 条件接受

老沈 v1 §3.1 推荐:
- 主 = GCP KMS asia-southeast1 (新加坡)
- 备 = Azure Key Vault switzerlandnorth (苏黎世)
- 紧急 = AWS KMS ap-northeast-1 (东京)
- 离线兜底 = YubiHSM 2

**我条件接受这个组合, 不接受 AWS us-east-1**. 条件:

| # | 条件 | Owner | Deadline |
|---|---|---|---|
| 1 | 老孙 v4 §6.3 三 vendor 表改写为 老沈 v1 §3.1 (GCP SG 主 / Azure CH 备 / AWS JP 紧急), **删除 us-east-1** | 老孙 (v4.1 patch) | 2026-06-04 |
| 2 | 老雷 ADR 书面 Sprint-4 启动前 Sygnum 接入 (已签, 2027-02-26) | 老雷 | DONE |
| 3 | 老吴 Sprint-1 末三 vendor onboarding 完成 (KYC 不暴露 Polymarket) | 老吴 | 2026-06-12 |
| 4 | Sygnum onboarding 启动 (§1.1) | 我 + 老雷 | 2026-06-11 |
| 5 | YubiHSM 2 采购 + 老雷 + 老沈 物理 split | 老吴 + 老沈 + 老雷 | Sprint-2 内 |

**4 + 5 是 老沈 v1 §9.3 自己要求的 sign-off 条件, 与我 align**.

### 4.4 我的明确态度 (给老孙)

> 老孙: v4 §6.3 主 wrap = AWS us-east-1, 我**否决**. 改为 GCP asia-southeast1 主, 否则我不签 v4 final. 老沈 v1 §7.2 已经写了具体 diff, 你直接抄.

---

## 5. 听取义务 — 我听到了什么 (Sprint-1 内)

1. **老沈 v1 跨 vendor 论证扎实**: §1.3 最小信任假设 + §5 Shamir vs KMS 区分清晰. 我接受.
2. **老孙 v4 跨语言 (Rust → C++) 合规无影响**: §6 沿用 v3, 我 §4.1 已接.
3. **老雷 Sygnum ADR 写得清楚**: 时间表 + 升级路径 + 一票否决都到位. 我接.
4. **老徐 MCP 盘点**: §9.1.1 polymarket-mcp / goalserve-mcp 自建 — 我合规视角 flag 一句: **MCP 接 polymarket REST/WSS 时, OAuth token / API key 不能进 LLM context**, 这是 R8 私钥明文落盘的延伸. 老徐 + 老叶 Sprint-2 立项时我要 review threat model.
5. **Goalserve odds ADR 三线并进**: 我 owner 线 A, 线 B/C 不归我但 fallback 路径 (Pinnacle no-vig / Polymarket midprice) 没踩合规面, OK.

---

## 6. 双向收口 — 我反向要的 (给老雷 + 全员)

| # | 我要什么 | 找谁 | Deadline |
|---|---|---|---|
| 1 | 老雷 §3.3 6 条 passphrase 条件书面 sign | 老雷 | 2026-06-04 |
| 2 | 老孙 v4 §6.3 改 AWS us-east-1 → GCP asia-southeast1 | 老孙 | 2026-06-04 |
| 3 | 老胡 Goalserve sales follow-up 接手 | 老胡 | 2026-05-30 |
| 4 | 老吴 `127.0.0.1:7890` 代理产权确认 | 老吴 | 2026-06-04 |
| 5 | 全员合规红线 v1 签收 (当前 2/48) | 小米 SOP 推 | 2026-06-11 |
| 6 | 老徐 MCP polymarket/goalserve 自建立项 ADR 我 review threat model | 老徐 + 老叶 | Sprint-2 启动前 |
| 7 | 主体身份决定 (自然人 / HK / BVI / 开曼) | 老雷 | 2026-06-11 (Sygnum 卡这条) |

---

## 7. 结语 (60 秒)

- Sygnum: 我扛 6/11 contact-made + 合同模板, 不扛 final 报价. 老雷配合 §1.3 四项.
- Goalserve odds: 我 + 老胡 分工, 我管合规 + 合同, 老胡管销售通信. 6/11 给老雷书面方案.
- BIP39 passphrase: 接受, 6 条硬条件 6/4 老雷 sign 后启动.
- R4 美国元素: 老孙 v4 改 GCP SG 主, AWS us-east-1 不签. Sygnum 是 final 出路.
- 合规红线签收 2/48: 小米 SOP 已经推, 我不催, 6/11 截止后老雷处置.

**我的 Sprint-1 交付 = 红线 v1 + Shamir sign-off + 跨 vendor 复核. Sprint-2 我承诺: Sygnum onboarding 启动 + passphrase 仪式 + Goalserve odds 商务收口.**

— 老黄 (compliance-legal), 2026-05-28
