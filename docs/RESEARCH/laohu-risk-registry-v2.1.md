# 风险登记 v2.1

- Owner: 老胡 (pm-project-manager) | 验收人: 老雷
- Last review: 2026-05-28 (Sprint-2 W3 Wave 19)
- 关联: `docs/SPRINTS/sprint-02-w3-progress.md` / `docs/RESEARCH/laohu-risk-registry-v2.md` / `docs/INCIDENTS/gm-self-mistakes-log.md`

---

## 0. TL;DR (v2 → v2.1)

- **关闭 0 / 降级 2 / 升级 0 / 新增 3 (R-35 / R-36 / R-37)**
- **降级**: R-28 schema_drift_chaos / R-31 R-20 PIT 违例 (落代码后)
- **新增 (W3 实际遇到, 非预判)**: R-35 CI 平台差异 / R-36 Anthropic 529 拥塞 / R-37 OAuth scope 阻塞
- **Top 5 不变**: R-02 / R-06 / R-07 / R-09 / R-31 (R-31 仍 Top 5, 降级路径已启动)

---

## 1. R-21 ~ R-34 W3 update

| 编号 | v2 → v2.1 | 状态 | 变更原因 |
|---|---|---|---|
| R-21 paper engine 联调时间不够 | 12 → 12 | 缓解中 (闸 2 推 W4) | skeleton 落地 + main 推 W4-03; W5 闸 3 联调真跑通 |
| R-22 跨域 listening 扩域 | 12 → 12 | 监控 | 不变 |
| R-23 Goalserve push 7s 延迟 | 9 → 9 | 监控 | 不变, alpha 数据出后重审 |
| R-24 vCPU0 压测不达标 | 12 → 12 | 缓解中 | W5-03 压测验, 不达标走老钱 §5.2 |
| R-25 5¢ 后 P0-01 频次 | 9 → 9 | 缓解中 | 小蒋 backtest v2 报告 7/30 |
| R-26 Sygnum onboarding | 撤销归档 | — | 不变 |
| R-27 SecureBuffer 反汇编 | 5 → 5 | 降级缓解 | 不变 |
| R-28 schema_drift_chaos 漏抓 | 9 → **6** | **降级** | 小宋 W3 测试 framework C++ 落代码 + 21 enum coverage CI + nightly workflow |
| R-29 endpoint v3 时间紧 | 关闭 | — | 不变 |
| R-30 美国 entity | 撤销归档 | — | 不变 |
| R-31 R-20 PIT 违例 | 15 → **12** | **降级中 H** | 12 篇回灌 + 7 项 grep 升级 + 21 enum CI + clang-tidy R-20 规则; W5 端到端验证后再降到 9 |
| R-32 paper 联调时间窗提前 | 9 → 9 | 缓解中 | 闸 2 推 W4 不影响 9/12 首判 |
| R-33 数据源 SSOT 流程红线 | 6 → 6 | 缓解中 | 老陈 SSOT v1 + 老郭 review 流程已立 |
| R-34 (补登) GM 错流程 | — → 6 | 缓解中 | GM 错 log 永久维护 (老雷 + 小米归档); W3 #3 触发补登 |

---

## 2. 新增风险 R-35 / R-36 / R-37 (W3 实测)

### R-35 CI 平台差异 (8 = 2×4, 缓解中)

**实测**: GM install 时 gtest FetchContent 在公司 `-Werror` 下报错, 用 `target_compile_options` 单独豁免 fix (commit d9e9be1).

**为什么是风险**: M1 节点 (7/9) AWS us-east-1 跑 CI 从 macOS → Linux, clang/gcc/libc++/libstdc++ 组合下 -Werror 行为不一致; HC 老冀/小秦入职再踩坑.

**缓解**: (1) 老练 W4 GitHub Actions 跑 ubuntu-22.04 + macos-latest 双矩阵 CI; (2) 老高 W4 clang-tidy `-Wno-error=*` 白名单文档; (3) W5 M1 跨洋 us-east-1 容器跑完整 CI.

**触发**: 新平台 CI 报错且短期不可解 → escalate 老周 + 老郭.

**Owner**: 老练 + 老高 + 老周

### R-36 Anthropic 529 拥塞 (6 = 3×2, 监控)

**实测**: W2-EXTRA-09 派单 (后被 GM 自纠为错 #3) 全部 task 0 token 失败因 Anthropic 529. **客观拦下错 #3 污染, 但这是运气不是机制**.

**为什么是风险**: 派单密集时段 (Wave 末) 529 概率升高; 0 token ≠ 派单成功, manager 可能误以为已 ack.

**缓解**: (1) 老胡 W4 派单 prompt 加入"完成回报必须含具体 commit/file 路径", 0 token 失败无产出不假 ack; (2) 小宋 W4 写 task_id retry script (≤ 3 次, 60s/180s/600s 指数退避); (3) 老雷 / 老胡 每 Wave 末抽检派单回报清单.

**触发**: 连续 ≥ 5 次 529 → P1 alert, 暂停大规模派单 30 分钟.

**Owner**: 老胡 + 小宋 + 老雷

### R-37 OAuth scope 阻塞 (6 = 2×3, 缓解中)

**实测**: GM install 本机时某项目 OAuth scope 不足无法直接 grant, GM 绕过 (install commit log).

**为什么是风险**: HC 老冀 / 小秦 6/30 入职 onboarding 重现; 跨 vendor (GitHub / Slack / 1Password / AWS) OAuth scope 不统一; 老沈 KMS onboarding 流程未含 OAuth.

**缓解**: (1) Sprint-3 W1 小林 + 老沈 联合写 onboarding playbook §OAuth scope (每 vendor scope 清单 + 申请路径); (2) Sprint-3 W1 老吴/老沈 评估 IdP (Okta/Google SSO) 统一 OAuth — 看 ROI; (3) W4 GM install playbook 落档 `scripts/install.md`.

**触发**: 新人首日 OAuth grant > 1 小时未完成 → P2 alert.

**Owner**: 小林 + 老沈 + 老吴

---

## 3. Top 5 (v2.1, 不变)

| 排名 | 编号 | 描述 | I × P | 变动 |
|---|---|---|---|---|
| Top 1 | R-02 | RiskGateway 绕过 | 20 (5×4) | 不变, W4 evaluate() 落代码后 W5 联调验证 |
| Top 2 | R-06 | seconds_delay 吃 PnL | 16 (4×4) | 不变 |
| Top 3 | R-07 | HC-01/02 招聘失败 | 16 (4×4) | 6/30 deadline 临近 |
| Top 4 | R-09 | Sharpe > 1 不达 | 15 (5×3) | 不变 |
| Top 5 | R-31 | R-20 PIT 违例 | 12 (3×4) | **降级** (v2 15 → v2.1 12), 仍 Top 5 但降级路径已启 |

R-35/36/37 评分均低于 R-31, 不进 Top 5.

---

## 4. 老胡附言

v2.1 是 W3 一周实战 update, **3 件新风险全是 W3 实际踩到** (非预判) — 风险登记基于事实非想象. R-31 从 15 降到 12 是关键, 小米 12 篇回灌 + 小宋 7 项 grep + 21 enum CI + clang-tidy R-20 规则, **代码层防线已立**, W5 端到端联调跑通后再降到 9. R-35/36/37 评分都不高 (8/6/6) 但都有 W3 实证: R-35 GM 已 fix; R-36 反而救了 #3 错 (不能依赖运气); R-37 onboarding 风险预判 (老冀/小秦入职前必解).

不耻下问 — R-35 @老练 + @老高 (W4 落), R-36 @小宋 + @老雷 (retry script + 抽检), R-37 @小林 (Sprint-3 W1 onboarding playbook).

— 老胡, 2026-05-28 (Sprint-2 W3 末 Wave 19)
