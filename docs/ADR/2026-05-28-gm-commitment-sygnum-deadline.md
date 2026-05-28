# GM 书面承诺 — Sygnum 接入截止日

- **Owner:** 老雷 (GM)
- **Date:** 2026-05-28
- **Status:** **SUPERSEDED 2026-05-28** by [`gm-policy-jurisdictional-deferral.md`](2026-05-28-gm-policy-jurisdictional-deferral.md)
- **Superseded reason:** 用户 2026-05-28 决议 "不要在地域合规性纠缠了"，未来迁合规地区一次性处理
- **关联:**
  - `docs/RESEARCH/laoshen-multi-vendor-kms-v1.md` Top 1 风险
  - `docs/RESEARCH/laohuang-shamir-jurisdiction-signoff-v1.md` §8.2 (N4 非美主权约束)
  - `docs/RESEARCH/laosun-key-management-v3.md` §N4

---

## 背景

老沈在跨 vendor KMS v1 中指出：v1 阶段推荐的三大美云母公司（AWS / GCP / Azure）均受美国法律约束（CLOUD Act / FISA 702 / 国安信函），不满足老黄合规红线 §8.2 "真·非美总部"硬约束。

老沈条件接受 v1，要求 GM 书面承诺 **Sprint-4 启动前接入 Sygnum**（瑞士 FINMA 银行 HSM），否则老黄不会 final sign-off，老沈也不会签字。

## GM 决议

**老雷书面承诺：**

> **2027-02-26（Sprint-4 启动日）前必须完成 Sygnum 接入并通过老沈 + 老黄联签验收。**
>
> **未达成 → Polymarket 业务一票否决暂停。** 这条不可撤销、不可延期、不可走 GM 单签绕过。

## 路线

| 时间节点 | 责任人 | 交付 |
|---|---|---|
| 2026-06-11 前 | 老黄 + 老雷 | 完成 Sygnum / Taurus 初步联系，要求合同模板 |
| 2026-07-15 前 | 老黄 + 外部律师 | 合同 review + 价格谈判 |
| 2026-08-15 前 | 老黄 | 合同签字，启动 KYC |
| 2026-12-01 前 | 老沈 + 老孙 | 集成测试通过（Sygnum API + 我方 signer 联调）|
| 2027-02-26 前 | 老沈 + 老黄 | 联签验收，Sprint-4 启动条件满足 |

## 红线

- 任一节点延迟 > 4 周 → 自动升级到老雷 + 老黄 + 老钱三方紧急会议
- Sygnum 谈判失败 → 立即启动 Taurus 备选（瑞士第二选项）；两家都失败 → MVP 后业务暂停决策

---

**Committed by 老雷, 2026-05-28**
