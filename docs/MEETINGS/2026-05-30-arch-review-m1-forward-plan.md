# 架构评审 + M1 后续计划会

- **日期:** 2026-05-30
- **主持:** 老雷 (GM)
- **议题:** PaperDaemon 重构 + A0→A2 第一笔成交 + 红线审计 一大轮后, 评审架构 + 定 M1 前向计划
- **出席 (评审供料):** 老周 (架构) / 老韩 (风控) / 小梁 (量化) / 老胡 (PM)
- **owner:** 老雷  **last_review:** 2026-05-30

---

## 1. 本轮已交付 (均 push main)

1. **PaperDaemon 重构** (老郭 §A.1 / 老周 B1): `stcpp::app::PaperDaemon` (stcpp_paper_app 库) + market_discovery + EventMatcher; 两 binary `paper_server`(HTTP)/`paper_runtime`(headless); debug_api 保持 Threads-only 契约库。
2. **A0→A2 主线**: EventMatcher 映射桥 (condition↔Goalserve event, orientation 防张冠李戴, fail-closed) + PaperLoop 接真比分 + 刷新线程 + **A2 解封第一笔 paper 成交** (daemon 端到端测试通过)。
3. **系统性单位 bug** (size 改 micro 后 caps/bankroll/book_depth 失配): paper 侧 ×1e6 hotfix 已修, 根因待方案 B。
4. **红线审计** (老郭+老韩): R-33 降级 + §8.1 红线治理纪律已落 CLAUDE.md。
5. **删废弃方向** (撤Rust/单binary/不上链) + debug_server→paper_server 改名。

---

## 2. 四 lead 评审摘要

### 老周 (架构): 能撑到 MVP, 最大债是单位制散落 29 文件
- **分层健康**: debug_api 库 link 仅 Threads (契约库未膨胀), app 单向依赖, paper_loop 零引用 app/。「匹配在上层(EventMatcher)、消费在下层(PaperLoop)」是正交切分, 对。
- **瑕疵 (技术债, 不阻塞)**: EventScore POD 跨层 include (放错家, 应下沉 data/); real_state_provider 710 行已到拆分上限, 再加 endpoint 必拆。
- **方案 B 收窄边界**: `_micro` 散布 **29 文件**, paper_loop 一处 6 个手抖 ×1e6, 任一漏乘=静默资金量级错 (踩铁律#3)。**但不做全量强类型 (29 文件全改=过度工程)**, 收窄为: ① `MicroPUSD` strong typedef (单头/explicit/零开销) ② 只锁三边界 (RM 入口 / SignReq / PositionLedger) ③ 内部 double pUSD 计算保留。
- **一句话**: 唯一能让 MVP 实盘静默烧钱的债是单位制, 方案 B 必须在接 live RM 前落地。

### 老韩 (风控): 真防线 3 层, 纸面红线 3 条 + live 没接 RM
- **方案 B = RM v0.7 头号** (他主, Sprint-2 上半): struct MicroPUSD + constexpr 运算符 + `_pusd` UDL; 顺序「typedef 隐式转换桥全绿 → 逐边界拆桥 → 删桥抓漏」; ABI 安全 (单 int64 布局零变)。
- **3 条纸面红线接线** (他门禁, 老沈接, B 后): exposure→consec_loss→DD; **B 落地前冻结一切金额接线** (免裸 int64 上叠回灌埋新 bug)。
- **mode_tag magic** (R-11, 中优, 搭接线同批): 现 0 既是 paper 又是 struct 默认零值, fail-open 风险; 改 default=非法哨兵 + paper 显式 magic。
- **MVP 实盘前 6 硬门**: ① live 下单过 RM (当前仅 paper 验过) ② 1-3 全落 ③ live 真 bankroll/pnl 回灌 ④ nonce_ledger live 隔离实测 ⑤ kill-switch ⑥ 拒单 audit 落盘。
- **一句话**: 真防线 = per-order cap + mode_tag fail-closed 隔离 + advisory 收口; **纸面的 = DD/consec_loss/exposure (零喂数据) + live 没接 RM**, 实盘前必补这 4 样。

### 小梁 (量化): 能成交但几乎不成交, time_frac 是关键
- **根因**: paper_loop.cpp:369 `prior_confidence(time_frac=0.0)` 硬编码 → conf 压在 0.15 → 先验对 fair 拉力仅 15% → 2:0 领先 edge 被 CI (n_eff=30 margin≈0.09) 吃成负。
- **调优三步 (依赖序)**: ① **接真 time_frac** (`EventScore.clock_sec / total_game_seconds()`, 后者已在 fair_value_estimator.hpp:393 备好全 sport 映射) — 关键前置, 非线性 (time_frac=0.7 时 prior 贡献 0.465 vs 0.15, edge 翻三倍) ② n_effective 30→**150~200** (非 500, 那是开门非校准; CI margin→0.035-0.040) ③ kBasePriorConfidence 0.15→0.25 (alpha/beta 不动, 无回测不拍脑袋)。
- **「正 alpha」空白 (排序)**: Goalserve 时延 decay (入球后 30-90s market 已 reprice, >60s edge 衰减完, 需测 edge_half_life) > de-vig 精度 (宽盘口 microprice 噪声 ±200-400bps) > 信号校准 (无历史真实胜率校准)。
- **盘口**: 先 soccer Moneyline (alpha 0.30, 连续时钟, edge 最可测), basket 噪比高、baseball/tennis 无时钟排后。
- **一句话**: 接真 time_frac → n_eff 150-200 → 实测时延 decay, 三步做完才有数字支撑「正期望成交」, 在此前任何成交是机械触发。

### 老胡 (PM): 4 验收项现状 + Goalserve 是即时 blocker
| 验收项 | 完成度 | 说明 |
|---|---|---|
| 单盘口跑通 | 75% | A0→A2 通, 差真数据回灌 (Goalserve 白名单掉) |
| 第一笔成交 | 60% | 代码链路证明 (daemon e2e); 生产参数太保守→真数据下几乎不触发 |
| 零风控失效 | 40% | 真红线健康; 纸面红线 (DD/consec/exposure) + 单位失配 |
| 72h 无崩溃 | 15% | 短测过, 长跑/重连/内存未验 |
- **关键路径 (串行)**: Goalserve 恢复 → 量化参数调 → 方案 B → 真成交。**最大 blocker = Goalserve IP 白名单 (GM 处理中)**。
- **6-25 M1 demo**: 真实 Goalserve 下单 Moneyline 产 ≥1 笔过 RM 成交 + 落账 + edge 分布可观测。

---

## 3. GM 拍板 (老雷)

**收敛**: 4 lead 一致 **方案 B (单位契约) = 头号架构/风控债**; **time_frac 真接入 = 量化解锁关键**; **Goalserve 白名单 = 即时 blocker (GM 处理)**。前向分两条并行轨:

### 轨 A — 让它「真成交 / 正期望」(量化主线, M1 demo 所系)
- **A1 (GM 即可做, 不依赖 Goalserve)**: time_frac 真接入 — paper_loop 填 `clock_sec/total_game_seconds()` 进 game_row。小改动, 小梁 #1 前置。
- **A2 (Goalserve 恢复后)**: 量化参数调 (小梁主) — n_eff 150-200 + conf 0.25, 用真实 Goalserve feed 跑 fill 分布验证 (每场预期成交 2-8 次)。
- **A3**: 实测 Goalserve 时延 decay 曲线 (小梁 + 数据组), 定 edge_half_life。
- 盘口锁 **soccer Moneyline 单盘口** (老钱 scope 红线 + 小梁)。

### 轨 B — 「实盘不烧钱」(单位安全 + 实盘前提, 部分进 M1)
- **B1 (头号, 进 M1)**: 方案 B 单位契约 — **老周定 MicroPUSD typedef + 三边界契约, 老韩 review 风控语义, IC 实施, 单 ADR**。收窄边界 (非 29 文件全改)。
- **B2 (M1 至少接 exposure)**: 纸面红线接线 — exposure→consec_loss→DD, 老韩门禁 (B1 后), 老沈接线, 全程走 MicroPUSD。
- **B3 (搭 B2)**: mode_tag magic (老韩+老沈)。
- **B4 (M1 后)**: live 下单路径接 RM (铁律#3 复用 B1 边界转换); 完整 72h 长跑; Frankfurt 部署。

### 并行 (不卡 demo)
- R-NN 命名空间拆分 (RL-/RJ-/RR-, 小米)。
- EventScore POD 下沉 data/ (老周, 技术债)。

### M1 (6-25) 达标线
真实 Goalserve 数据下, **单 soccer Moneyline, 产 ≥1 笔过 RM 的 paper 成交 (非死代码触发, RM 真拦过 ≥1 单) + 落 paper 账本 (R-11 已证隔离) + edge 分布直方图/分位可观测 + 单位口径一致 (方案 B)**。72h 长跑做「启动跑通」, 完整 72h 留 M1 后。

### 即时下一步 (GM 主线)
**time_frac 真接入 (轨 A1)** — 不依赖 Goalserve, 小改动, 解锁正期望前置。Goalserve 恢复前先把这步落了。同时 **B1 方案 B 派老周出 typedef + 三边界契约 spec**。

---

## 4. backlog 登记

| 项 | owner | 轨 | 时机 |
|---|---|---|---|
| time_frac 真接入 | GM | A1 | 即时 |
| 量化参数调 (n_eff/conf) | 小梁 | A2 | Goalserve 恢复后 |
| Goalserve 时延 decay 实测 | 小梁 + 数据组 | A3 | Goalserve 恢复后 |
| 方案 B 单位契约 (MicroPUSD) | 老周(定)+老韩(review)+IC | B1 | M1 头号 |
| 纸面红线接线 (exposure/consec/DD) | 老韩(门禁)+老沈 | B2 | B1 后, M1 至少 exposure |
| mode_tag magic | 老韩+老沈 | B3 | 搭 B2 |
| live 接 RM | (待派) | B4 | M1 后 |
| 72h 长跑 + Frankfurt 部署 | 老吴 | B4 | M1 后 |
| R-NN 命名空间拆分 | 小米 | 并行 | M1 后 |
| EventScore POD 下沉 | 老周 | 并行 | 技术债 |
