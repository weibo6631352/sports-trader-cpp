# Meeting γ — 工程节奏 + 47 agent 协作机制

> 出席: PM / 产品经理 / AI Ops × 2 / SRE / 安全 / 现代 C++ 顾问 / 代码质量评审 / 测试回放 / 合规 / API Watch
> 主持: 主 agent (CPO 缺席, 决议事后递交 CPO 签字)
> 日期: 2026-05-28
> 决议状态: **本轮通过, M1 内可微调; 跨 milestone 修订需 CPO + 架构师 + PM 三方签字**

---

## G. Milestone + 风险登记

### G.1 三个 milestone

agent 团队工作速度参考: 47 agent 并发, 每"周"≈ 真人 5 工作日的产出, CPO + 架构师每日例审, P0 路径任意改动需 24h 内 review.

| Milestone | 时长 | 目标 | 关键交付 | 验收 criteria |
|---|---|---|---|---|
| **M1 端到端 helloworld** | 4 周 | 跑通 "gamma poll → 决策 → paper 下单 → audit 写盘" 最小闭环 | (1) gamma `/events?live=true` 2s 轮询 + nested markets 落 in-memory store<br>(2) Polymarket market WS 单 condition 订阅 + book/price_change 解析<br>(3) `OrderExecutor` paper 路径 (不真签名, 写本地 ledger)<br>(4) `RiskManager` 最小门禁 (bankroll cap + max position size)<br>(5) audit_events SQLite 落盘 + 基础 REST `/runtime` `/positions`<br>(6) Docker 镜像 (macOS arm64 + Linux x86_64) | a. paper 模式连续跑 48h 无 leak / crash<br>b. WS 断线 5min 内自动重连 + sequence_gap REST 兜底走通<br>c. `/runtime` < 2s 返回 (CLAUDE.md §17.9 上限)<br>d. 单 condition paper buy → audit_event 端到端可溯源 |
| **M2 单 sport 准实盘** | 6 周 | 选 **MLB** (Goalserve inplay 最稳, Python 版已实盘验证) 跑准实盘, 资金上限 $50 | (1) Goalserve inplay-baseball.gz HTTP GZIP feed + livescore 双源 reconcile<br>(2) `quant_signal.estimate_signal` 真概率层 (goalserve devig + math_prob max 融合)<br>(3) Kelly sizing + Bankroll resolver<br>(4) 链上签名路径 (EIP-712) + USDC allowance 检查<br>(5) reconcile worker (positions + orders + balance 三轴)<br>(6) Prometheus metrics + Grafana dashboard<br>(7) 操盘 UI (React) 最小集: candidates / positions / live-events | a. 准实盘连续跑 14 天, 资金从 $50 始, 月化回撤 ≤ 20% (无盈利硬指标, M2 验证链路而非 alpha)<br>b. 0 次因系统 bug 导致的错单 / 重单 / 残留 BUY (CLAUDE.md §3)<br>c. 95p 决策延迟 (WS 收到 → 下单提交) ≤ 50ms<br>d. P0 路径 0 次 DB 读 (audit-only)<br>e. CPO 签字"准实盘体验过关" |
| **M3 全 sport 准实盘** | 8 周 | 8 sport (soccer / basket / tennis / volleyball / amfootball / hockey / baseball / esports) + livescore-only 9 项, 资金 $500 | (1) 全 sport demand-driven 轮询调度<br>(2) 分节/分盘/分局 prop 市场支持 (NRFI / NBA 1H / 足球半场)<br>(3) 直播源赔率 vs Polymarket 价差信号 (CLAUDE.md §15 edge 信号)<br>(4) Awaiting-settlement 窗口管理<br>(5) Orphan/redeemable 仓位 reconcile<br>(6) Chaos test (代理断线 / WS 漂移 / DB 满)<br>(7) Runbook + 操盘手册 | a. 8 sport 任意时段并发跑无 crash<br>b. 资金 $500 跑 30 天, 月化 Sharpe > 0 (信号有效性, 不要求绝对盈利)<br>c. 直播源覆盖率 ≥ 95% (有 tracked market 的赛事)<br>d. supply chain audit 通过 (vcpkg manifest pinned + cargo-audit clean)<br>e. CPO + 合规 + 安全 三方签字"可放 $1000+" |

**反对意见**:
- **现代 C++ 顾问**: M1 4 周太激进, C++ 项目从 0 到能跑 paper 链路, 仅 build 系统 (CMake / vcpkg / clang-tidy) + 基础抽象 (asio + Boost.Beast WS + simdjson) 就要 2 周. 反驳: 47 agent 并发, C++ 高频 + 网络协议 + 数据序列化 三个 agent 并行能压到 4 周.
- **PM**: M2 资金 $50 太小, 无法验证 Kelly 在真盘下表现. 反驳: M2 是链路验证 milestone, 不是 alpha 验证; 资金验证留给 M3 + post-M3.
- **测试工程师**: 没有显式"回归测试通过率" criteria, 风险大. 决议: 按 CLAUDE.md §11 默认不写测试, 但 M1/M2/M3 强制要求**回放 sim 通过** (见 H.6).

**落地动作**:
- M1 启动 day-0: 主 agent + 架构师 + 现代 C++ 顾问 拍 CMake / vcpkg / 目录骨架 PR
- 每 milestone 末: CPO + PM + 架构师 三方 sign-off, 否则不进下一阶段
- milestone 内每周 PM 出 burndown

### G.2 Top 8 风险登记

| ID | 风险 | 类别 | 概率 | 影响 | mitigation | owner |
|---|---|---|---|---|---|---|
| R1 | C++ 项目第一次实盘出资金事故 (错单 / 重单 / 私钥泄漏) | technical + 安全 | 中 | 致命 | M1/M2 全程 paper, M2 准实盘资金 $50 上限硬卡, RiskManager 单元化测 + 双签 (人 + 自动) | 风控工程师 + 安全 |
| R2 | 47 agent 协作变形式主义, 决策慢于 Python 项目 | people | 高 | 高 | H.1 RACI 矩阵 + H.2 召唤 trigger 收敛参与人; 默认沉默, 按需召唤 | AI Ops × 2 |
| R3 | C++ 内存 / 线程 bug (UB / data race) 比 Python leak 更难定位 | technical | 中 | 高 | M1 起强制 ASan + TSan + UBSan CI; 现代 C++ 顾问 review 任何 raw pointer / manual sync | 现代 C++ 顾问 + 代码质量 |
| R4 | Polymarket 协议变更 (API 改版 / WS schema 变动) | dependency | 中 | 中 | API Watch agent 每周扫 Polymarket changelog + GitHub issue; Polymarket 协议专家维护 protocol-version 兼容层 | API Watch + Polymarket 协议专家 |
| R5 | Goalserve 数据源不稳 (inplay 断流 / 格式漂移) | dependency | 高 | 中 | Goalserve 调研专家 每周扫 full_package_feed.txt 漂移; livescore + inplay 双源 reconcile; 失败 fallback Polymarket-only 信号 | Goalserve 调研 + API Watch |
| R6 | 中国 → 代理 → 美国 链路抖动导致决策延迟超 50ms | technical | 高 | 中 | 多代理热备 (3+ 出口 IP); 性能监控 95p 延迟 alert; SRE 维护代理健康度面板 | SRE + 网络协议 |
| R7 | 合规 / KYC 风险 (Polymarket 大陆受限, 资金路径) | business | 中 | 致命 | 合规 agent M1 出"红线清单"; 资金路径只走加密钱包, 不接银行; 法务 sign-off 后才放 $1000+ | 合规 + 安全 |
| R8 | supply chain 投毒 (vcpkg / cargo 依赖被恶意篡改) | 安全 | 低 | 高 | vcpkg manifest pinned commit; cargo-audit + cargo-vet CI; 第三方库每月安全扫描 | 安全 + SRE |

**反对意见**:
- **合规**: R7 影响应是"致命+法律责任", mitigation 列得太轻. 决议: 升级 R7 mitigation, M2 末 必须有法律意见书.
- **PM**: 缺一个"agent 输出质量风险" (agent 写出 plausible 但错的代码). 决议: 合并入 R2, 通过 H.1 RACI 多签 + 代码质量评审强制 review 兜底.

---

## H. 47 agent 协作机制

### H.1 决策权矩阵 (RACI 简化版)

R = Responsible (拍板), A = Approver (签字), C = Consulted (征求意见), I = Informed (知会)

| 决议类型 | R (拍板) | A (签字, 全员必须 ✓) | C (征求) | I (知会) |
|---|---|---|---|---|
| 架构变更 (新增 service / 改链路骨架) | 首席架构师 | CPO + 现代 C++ 顾问 + Rust 顾问 | 高频系统 + 网络 + 持久化 | 全员 |
| 风控规则变更 (RiskManager 门禁) | 风控工程师 | CPO + 合规 + 首席架构师 | 量化-信号 + 金融专家 | 全员 |
| 私钥 / 签名路径 变更 | 加密签名专家 | 安全 + 合规 + 首席架构师 | 链上/DeFi + Polymarket 协议 | 全员 |
| 数据源接入 (新 sport / 新 endpoint) | Goalserve 调研 OR API Watch | 首席架构师 + 数据-ETL | 体育市场 + 量化-信号 | PM + CPO |
| Polymarket 协议适配 | Polymarket 协议专家 | 首席架构师 + 网络协议 | 加密签名 + 链上/DeFi | 全员 |
| 量化信号 / Kelly / 定价模型 | 量化-信号 | CPO + 金融专家 + 风控 | 量化-Backtest + 量化-Microstructure + 数据-Stats | PM |
| 操盘 UI 变更 | 产品经理 | CPO + UX (经 §5 § PM 走) | 前端工程师 × 2 | 全员 |
| 部署 / 容器 / 监控 | SRE | 安全 + 首席架构师 | 可观测性 + 现代 C++ 顾问 | 全员 |
| 测试 / 回放策略 | 测试/回放 | CPO + 首席架构师 | 量化-Backtest + 性能分析 | 全员 |
| 文档 (架构 / runbook / API spec) | 文档作者 (任一) | 对应模块 R + 代码质量 | — | 全员 |
| **CLAUDE.md / AGENT.md 修订** | CPO | 用户 (人) + 首席架构师 + PM | 全员 | — |

**反对意见**:
- **现代 C++ 顾问**: 架构变更只让 Rust 顾问签 不让金融专家签 不合适, 金融逻辑改动也算架构. 决议: "金融逻辑层的架构变更" 走"风控规则变更"流程, 不双跑.
- **AI Ops**: 签字 agent 必须 24h 内回, 否则视为默认通过. 否则会变 deadlock. 决议: **采纳, 写入 H.2 escalate 规则**.

### H.2 agent 召唤 trigger (默认沉默, 按需召)

**自动 trigger 表** (CI / commit hook 自动召, 不召不许 merge):

| 触发事件 | 自动召谁 review | SLA |
|---|---|---|
| PR 改动 P0 路径 (`OrderExecutor` / `RiskManager` / `OrderGateway` / `MarketTickWorker` / WS handler) | 首席架构师 + 高频系统 + 现代 C++ 顾问 + 风控 | 24h |
| PR 改动加密签名 / 私钥 / 钱包 | 加密签名 + 安全 + 合规 | 24h |
| PR 改 CMakeLists / vcpkg.json / 依赖 | SRE + 安全 + 现代 C++ 顾问 | 48h |
| PR 改 Goalserve / Polymarket 协议适配 | 对应专家 + API Watch | 48h |
| PR 改 audit_events schema | 数据-数仓 + 持久化 + 代码质量 | 48h |
| PR 改 UI | 产品经理 + UX + 前端另一位 | 48h |
| 任何 PR (兜底) | 代码质量评审 (1 名 IC) | 72h |

**手动召唤 trigger** (主 agent / CPO 显式 invoke):

- 实盘事故复盘 → CPO + 风控 + 涉事模块 R + 测试/回放
- 性能瓶颈定位 → 可观测性 + 高频系统 + 性能分析 (从 IC 池抽)
- 大型 refactor 设计阶段 → 首席架构师 + Rust 顾问 + 现代 C++ 顾问 + CPO

**SLA 兜底**: 任一签字方超时未回 → 自动 escalate CPO + PM, 再超 24h → 视为默认通过 + 记录在 audit log, 后续问题责任分摊到超时方.

**反对意见**:
- **安全**: 安全相关 PR 不能"默认通过", 必须显式 ✓ 否则 block. 决议: 采纳, **安全 + 合规 相关 PR SLA 兜底改为 block 而非 pass**.

### H.3 冲突仲裁机制

1. **2-agent 冲突**: 召第 3 个同领域 agent 评审 (e.g. 现代 C++ vs Rust 顾问冲突 → 召首席架构师)
2. **3-agent 评审无法判定**: 升 CPO + PM 联席决议
3. **CPO + PM 仍无法判定**: 升用户 (人) + 给出双方论据 + 推荐方案
4. **绝不允许**: 主 agent 单方面"综合权衡" 后绕过签字方实施 (CLAUDE.md §13 + AGENT.md §3 红线)

**反对意见**:
- **AI Ops**: 升级链太长, P0 救火等不及. 决议: 救火场景走 CLAUDE.md AGENT.md §3 例外, 先修后评.

### H.4 会议节奏

| 会议 | 周期 | 参与 | 产出 |
|---|---|---|---|
| **CPO 日审** | 每日 | CPO + 主 agent + 当日活跃模块 R | 本日决议 + 明日方向 |
| **架构周会** | 每周一 | 首席架构师 + Rust + 现代 C++ + CPO + 高频系统 | 架构债登记 + 下周改造计划 |
| **风控周会** | 每周三 | 风控 + CPO + 金融专家 + 量化-信号 + 合规 | 门禁参数审查 + 异常单复盘 |
| **API watch 周报** | 每周五 | API Watch + Goalserve 调研 + Polymarket 协议 | 上游变更登记 + adaptation backlog |
| **Milestone 评审** | 触发: M1/M2/M3 完成 | 全员 + 用户 | sign-off / 修正 backlog |
| **事故复盘 (post-mortem)** | 触发: 任何实盘事故 / 资金损失 | CPO + 风控 + 涉事模块全员 + 安全 (如涉) | RCA 报告 + 防止再现 action |
| **月度 retro** | 每月末 | 全员 | 协作改进 + agent 调度优化 |

**反对意见**:
- **PM**: 会议太多, agent token 成本爆. 决议: 周会改为"异步周报" (各 R 写 ≤ 200 字 update, CPO + PM 异步合并), 仅 milestone 评审 + 事故复盘走真会议形式.

### H.5 文档体系 (docs/)

| 文档 | 写 | review | 更新频率 |
|---|---|---|---|
| `architecture.md` 系统架构 | 首席架构师 | CPO + 现代 C++ + Rust | milestone 末 |
| `prd.md` 产品需求 | 产品经理 | CPO + PM | 每轮 |
| `risk-rules.md` 风控规则 | 风控工程师 | CPO + 合规 + 金融 | 每次门禁改动 |
| `api-spec/` REST + WS 契约 | 网络协议 + 后端 IC | API Watch + 前端 | 每次 schema 改 |
| `runbook.md` 运维手册 | SRE | 可观测性 + CPO | 月度 |
| `security.md` 私钥 + 供应链 | 安全 | 加密签名 + 合规 | 季度 |
| `data-sources.md` 直播源 + Polymarket 端点 | Goalserve 调研 + API Watch | 数据-ETL | 月度 |
| `claude.md` / `agent.md` (迁移自 Python) | 用户 + CPO | 全员 (但只用户能改, AGENT.md §1) | 按需 |
| `meeting-*.md` 会议决议 (本文档系列) | 主持人 | 出席方 | 触发式 |

原则: **完成态由代码 + git log 表达, 过程文档完结清理** (CLAUDE.md §12).

### H.6 测试 / 回放策略

**核心决议**: C++ 项目**部分背离** CLAUDE.md §11 "默认不写测试":

| 模块 | 测试要求 | 理由 |
|---|---|---|
| `RiskManager` 门禁逻辑 | **强制单元测** | UB / off-by-one 在 C++ 比 Python 致命; 风控错误 = 资金损失 |
| `OrderExecutor` 签名 + 提交路径 | **强制集成测** (mock Polymarket) | 同上 |
| 加密签名 (EIP-712) | **强制单元测 + KAT** (Known Answer Test) | 签名错 = 资金锁死 |
| 序列化 / 解析 (simdjson / WS frame) | **强制 fuzz test** (libFuzzer) | 上游漂移容易触发解析崩 |
| 并发原语 (lock-free queue / SPSC) | **强制 TSan + 形式化 (Loom-equiv)** | data race 极难调 |
| 业务策略 / Kelly / sizing | **可选, 走回放 sim** | 与 Python 版一致, 实盘观察为准 |
| UI / REST endpoint | **不写测试** (CLAUDE.md §11) | 同 Python 版 |

**回放 sim** (M2 必须可用):
- 录制 1 周真实 Polymarket WS + Goalserve feed (binary capture)
- M2 起 每周回放最近 7 天数据, 对比 决策 + 仓位 + PnL 与历史是否一致
- Chaos 模式: 注入 WS 断线 / sequence_gap / 代理 5s 延迟 / DB 满 / 错误 payload

**反对意见**:
- **现代 C++ 顾问**: 强制测试列表太短, `OrderGateway` 编排逻辑也该单元测. 决议: 采纳, 加入强制列.
- **CPO** (代表): 测试维护成本爆炸. 决议: 上述强制清单仅限 P0 安全相关, 总测试代码 ≤ 业务代码 30%, 超过砍.

### H.7 安全 + 合规底线

**私钥管理硬约束** (安全 agent 红线):
1. 私钥**只存** OS Keychain (macOS) / Linux KMS (sealed by TPM); **绝不**进 .env / 代码 / git
2. 签名操作**只**在 `signer` 独立进程, 主交易进程通过 unix socket 调; 主进程不持密钥
3. 私钥**轮换** 季度强制 + 任何 agent 离职 / 协作变动后立即轮换
4. 钱包资金**上限**: M1=$0 (paper) / M2=$50 / M3=$500 / post-M3 解锁需用户书面确认

**跨境 / KYC 红线** (合规 agent):
1. Polymarket 大陆受限, 资金路径**只走加密钱包**, 不接银行 / 法币 onramp
2. M2 末**法律意见书**到位前不放真实盘 (尽管 paper / 准实盘可继续)
3. agent 输出**不得**包含规避 KYC / 跨境监管的策略建议; 违反则该 agent 输出整段废弃

**Supply chain audit**:
1. vcpkg manifest mode + commit pinned, 升级走 PR + 安全 ✓
2. cargo-audit + cargo-vet CI gating (Rust 部分, 如有)
3. 第三方库每月安全扫描 (Snyk / OSV-Scanner); CVE 高危 48h 内响应
4. CI build 在 hermetic container (无网络) 内跑, 防 build-time 投毒

**反对意见**: 无, 全员通过.

### H.8 CI/CD 流水线

**编译矩阵**:
- `macOS arm64` (开发主力, Clang 19)
- `Linux x86_64` (生产, GCC 14 + Clang 19 双 build)
- `Linux arm64` (备机, GCC 14)

**Stage**:

| Stage | 工具 | 失败处理 | 时长目标 |
|---|---|---|---|
| 1. format | clang-format 19 | block | < 30s |
| 2. 静态分析 | clang-tidy + cppcheck | block on warning level ≥ medium | < 5min |
| 3. 编译 (3 矩阵 并行) | CMake + Ninja | block | < 10min |
| 4. 单元测试 | doctest + ASan/UBSan | block | < 5min |
| 5. 集成测试 | mock Polymarket + mock Goalserve | block | < 10min |
| 6. 性能回归 | benchmark P0 路径延迟 95p (与 baseline 对比, 退化 > 10% block) | block | < 5min |
| 7. fuzz (nightly) | libFuzzer 序列化路径, 8h budget | report only | nightly |
| 8. TSan (nightly) | 并发模块完整跑 | report + alert | nightly |
| 9. 安全扫描 (weekly) | Snyk + OSV-Scanner + Coverity | block on high CVE | weekly |
| 10. 容器打包 + 签名 | Docker buildx + cosign | block | < 5min |
| 11. 部署 (M2+) | gitops (FluxCD), staged rollout | manual approve | — |

**反对意见**:
- **现代 C++ 顾问**: Coverity 商业费用高, 可换 PVS-Studio 或开源 SonarQube. 决议: M1/M2 用 SonarQube, M3 资金充足后评估 Coverity.
- **测试**: 性能回归 baseline 不稳, 容易 false-block. 决议: 95p 延迟用 7 天 rolling baseline, 单次 PR 与 baseline 比, 退化 > 10% 才 block; 同时给"性能 waiver" 流程 (架构师 + 性能分析 双签可放过).

---

## 总结决议

1. **3 milestone**: M1 (4 周 helloworld) → M2 (6 周 MLB 准实盘) → M3 (8 周 全 sport)
2. **8 top risk** 登记, R7 (合规) + R1 (资金事故) 列为致命级, 全程跟踪
3. **决策走 RACI 矩阵**, 默认沉默 + trigger 召唤; 24h SLA, 安全/合规相关 block 而非默认通过
4. **测试策略部分背离 Python**: P0 安全模块强制测, 业务策略走回放 sim
5. **会议改异步周报为主, 仅 milestone + 事故走真会议**, 控 token 成本
6. **私钥独立进程 + 资金阶梯解锁** ($0 → $50 → $500 → 解锁需用户书面)
7. **CI 11 stage**, 性能回归用 rolling baseline + waiver 流程

**最终签字**:
- 本决议需 CPO + 首席架构师 + 用户 三方 ✓ 才入档生效
- 决议入档后, 任何修订走 AGENT.md §3 双协评 + CPO 复核
