# GM 架构 Challenge 投票拍板决议 (Wave 27)

- **日期:** 2026-06-01 (W5 末)
- **拍板人:** 老雷 (GM, 老板 verbatim 授权 "投票后你拍板决定")
- **基于:** Wave 27 6/6 投票 (老周/老韩/小余/小梁/老胡/老郭) + 22 architecture smell
- **不升全体争议会** (老板授权 GM 直接拍)

---

## 5 ADR 拍板决议

### ADR-011 (议题 1 paper/live binary): **A 共享 binary + C 内部 transport 分离**

- **投票**: A 4 (老韩/小余/小梁/老胡) / C 2 (老周/老郭)
- **GM 决议**: 主决议 **A 共享 binary** (维持 R-2 红线), 实施细节采纳 **C transport 内部分离**
  - 同一 binary, CMake build-time mode switch (现状 R-7 已实施)
  - 内部 RM + Signal core 共享 (统计可解释性, M4.5 G7 paired t-test, 小梁 retro A-15)
  - signer/matcher transport 层物理隔离 (老周/老郭 防 #ifdef 技术债)
- **理由**: R-2 红线是公司"回测 vs 实盘相同逻辑"金融纪律 (CLAUDE.md §8), 共享 binary 维护 binary 同源性; transport 分离是工程细节, 不破 R-2
- **W6 owner**: 老周 (架构) 落 v0.7 §8 启动期 11 步 + transport 分离 spec

### ADR-012 (议题 2 WSS 拓扑): **E 相位方案 (paper 1×8 不动, M4.5 后升 4-5×2)**

- **投票**: A 1 / B 2 / C 2 / E 1 (极度分歧)
- **GM 决议**: 采纳 **老郭 E 方案**
  - paper 阶段 (现在 ~ M4.5): **1 conn × 8 sub 不动** (老胡 PM 视角"小冯 1044 行已成事实, 8 ASK 未 ack 不叠加变量")
  - M4.5 后到 M5+ 切换: **升 4-5 conn × 2 sub** (老周/小余 多 conn 故障域)
- **理由**:
  - paper 阶段最高优先级是 SPSC 5 queue 落地 (老周 P0 + 老郭强调), 不是 WSS 拓扑
  - 4-5 conn 设计需要跨洋 conn overhead 真实数据 (老周承认 v0.6 C 无实测)
  - 老韩/小梁 2×4 折中方案 paper 阶段没必要做
- **W6 owner**: 小冯 维持 1×8, 不动. M2 后小冯 + 老李准备 4-5 conn upgrade design

### ADR-013 (议题 3 跨洋部署): **C M4.5 前单点 us-east-1, M5+ 多点评估** — 6/6 共识

- **投票**: C 6/6 全共识 ✓
- **GM 决议**: 直接 ack **C**
  - W6 ~ M4.5: us-east-1 单点 (老胡 PM 视角 + 老吴 SRE + HC-10 无 JD)
  - M5+ live 后 bankroll > $20K (小梁): 评估 multi-region active/active
  - active/active 真正阻塞是 nonce 分布式协调 (老郭/老韩 一致), 非 SRE 资源
- **W6 owner**: 老吴 维持 us-east-1, 不动. M5+ 前老叶 + 老韩 nonce 分布式 spec

### ADR-014 (议题 4 ML 时机): **C M2 (8/6) 后 shadow, M4.5 后 active**

- **投票**: A 2 (小梁/老胡) / B 2 (老韩/小余) / C 2 (老周/老郭) — 三分裂
- **GM 决议**: 采纳 **C 中间方案** (与 小邓 v1 阶段 1 LightGBM baseline 配套)
  - W6 ~ M2 (6/7-8/6): paper 数据进 paper_mldata.wal + 老彭 de-vig 校准 (ADR-008)
  - M2 后 (8/6 ~ M4.5): de-vig 锁定 (小梁 P1 smell #1 触发 24h 内 ack), shadow inference 启动
  - M4.5 (2027-05) 后: 评估 ML active 进生产 (老钱 + 老韩 + 老雷 三方决议, ML-R6 解锁)
- **理由**:
  - 小梁 P1 smell #1: "de-vig 算法 W6-M4.5 漂移, 用漂移 fair_value 做 training label = noise" — 必须 de-vig 锁定后 (M2) 才 shadow
  - 老韩 B 视角: shadow 数据越早越好 — M2 比 M4.5 提前 9 个月足够
  - 老胡 A 视角: ML gate 绑定 M4.5 — shadow 不影响 active gate (ML-R3 不阻塞 if-else)
  - 三方主张兼顾, C 折中合理
- **W6 owner**: 小邓 W6 paper 数据收集 (hook 已就位 W4), M2 后接 shadow inference (小邓 + 小蒋)

### ADR-015 (议题 5 vCPU pin): **C 阶段性 (paper 现状 + WAL bg 4 线程, M5+ 前升 7 vCPU)**

- **投票**: C 4 (老周/老韩/小梁/老郭) / A 1 (小余) / B 1 (老胡)
- **GM 决议**: 采纳 **C 阶段性**, 同时**接受小余 A 论据** (WAL bg 4 线程已用核)
  - paper 阶段 (~ M5+ live 前): main thread (decision) + 4 WAL bg fsync 线程 (老王 v0.2 现状)
  - 不强 pin 7 vCPU (paper p99 3.9us 余 200x+, 跨洋 RTT 200ms 是绝对瓶颈)
  - M5+ live 前: 老姜真实压测 (S2-011 6/22) 后升 7 vCPU pin
- **理由**:
  - 老周/老韩/小梁/老郭 一致: paper 阶段 single thread main 主路径 (3.9us 实测)
  - 小余关键论据: "WAL bg 4 线程已用 core 6/7" — 这是事实, 不是单 thread 实际是 5 thread
  - 老胡 PM: 等 6/22 压测数据 — 与"M5+ 前老姜 bench"一致
  - 真实工程: paper 现状 (main + 4 WAL bg = 5 thread) ≠ 严格 single thread, 也 ≠ 强 pin 7 vCPU
- **W6 owner**: 老姜 W6 起出 latency bench 上下界 + S2-011 压测 6/22, M5+ 前 7 vCPU pin spec

---

## 22 Smell 集成到 W6 backlog (GM ack)

### P0 (W6 第 1 周必修)
- **Smell 老韩#3**: Position WAL 缺位升 **P0** (老韩 vs 老周 P1, GM 接受老韩) — 崩溃重启 circuit breaker 状态归零 + M4.5 gate 数据不完整. **W6 owner: 老王 (WAL framework) + 老蒋 (position 写入)**

### P1 (W6 必修)
- **Smell 老周#A**: AuditEmitter 隐性单点 (5 上游压 1 emitter, BLAKE3 切真时回归全受影响) — W6 老唐 加 emitter pool / 抗扰
- **Smell 老周#B**: WAL append-only 无 compaction/rotation/Parquet export → ML 撞墙 — **HC-06 ml-data-engineer 入职后 owner** (8/15)
- **Smell 老周#C**: live_pm_client 14 接口无 ABI lock 文档 — W6 老李 + 老孙 handshake 文档
- **Smell 老韩#1**: AUDIT_WAL_BACKPRESSURE 细化时机 + STRATEGY_DECAYED MONITORING 独立 — W6 老韩 + 老唐
- **Smell 老韩#2**: BLAKE3 stub 严禁进 live build (M1 acceptance gate 前置) — W6 老唐 + 老高 PR review v1.2
- **Smell 老韩#4**: STRATEGY_DECAYED 三签解锁 SOP + CLI 空白 — **M2 前必补**, 老韩 + 老唐 + 老吴
- **Smell 小余#DS-01**: 缺第 5 WAL `IngestRaw` (原始帧落盘防永久丢) — W6 小冯 + 老王
- **Smell 小余#DS-02**: Goalserve 单 source vendor lock-in — W7+ 老段 + 老彭 evaluate alternative source
- **Smell 小余#DS-03**: R-20 第 5 ts `label_ts` (ML schema) — W6 小邓 + 老唐 schema 协商
- **Smell 小余#DS-04**: WAL → Parquet ETL 触发机制未定 — HC-06 owner (8/15 后)
- **Smell 小余#DS-05**: gzip ratio anomaly CI 缺失 — W6 小段 + 老高 PR review v1.2
- **Smell 小梁#P1**: multiplicative de-vig 对称假设 totals 盘口可能偏差, Shin 升级触发指标 — 老彭 W6 EOW 实证数据 24h 内 ack
- **Smell 小梁#P2**: **G2 CI 下界 0.3 (小梁口头) vs 0.5 (小董代码) 正式会签入档** — **W6 第 1 周必交** (老板视角的 ground truth issue)
- **Smell 小梁#P3**: 12 信号规划过多, MVP ≤ 3 个 — CPO 老钱 W6 ack 调整 backlog
- **Smell 小梁#P4**: R-2 可在 backtest audit chain BLAKE3 放松, Kelly hash 严守 — W6 老唐 + 老高 PR review v1.2

### P2 (W7+ 优化)
- **Smell 老韩#1 (split)**: 21 RejectCode 升 25/30 评估 — Sprint-3 老韩 + 老唐
- **Smell 老胡#1**: **M1 → M4.5 12 周无 buffer, 建议 M4.5 右移 2 周** — **6/25 M1 评审同步评估** (GM ack)
- **Smell 老胡#2**: 协商会 0 次就转 W6 硬约束 — 6/02 第 1 次协商会必出 ack 记录 baseline
- **Smell 老胡#3**: 5 议题均跨主管, 协商 framework 未验证就全量派 50+ ticket — W6 起小批量验证
- **Smell 老胡#5**: ML 时机与小颖 acceptance v1 121 条绑定关系未明确 — W6 小颖 update acceptance v2 引用 ADR-014
- **Smell 老胡#6**: 议题 1 + 2 耦合 — GM 拍板已 weigh (ADR-011 A 共享 binary + ADR-012 paper 内部 1×8, 两者一致)

---

## W6 backlog update (老胡 PM 必交)

按 GM 拍板 + 22 smell, W6 派单调整:

- W6-A-binary-split: **删** (ADR-011 A 共享 binary, 不分叉)
- W6-A-wss-topology: **改成 W6 维持 1×8 not change** (ADR-012 E 相位)
- W6-A-crossregion: **删** (ADR-013 C 单点不动)
- W6-C-ml-shadow: **改成 M2 后启动** (ADR-014 C, W6 准备 paper data 收集)
- W6-A-vcpu-bench: **改成 W6 起老姜 bench** (ADR-015 C, M5+ 前 7 vCPU pin)
- **W6-A-position-wal: 新增 P0** (Smell 老韩#3 升 P0)
- W6-C-g2-ci-lock: **新增 P0** (Smell 小梁#P2 G2 CI 下界正式入档)

---

## 老胡 M4.5 右移 2 周提案

老胡 #1 smell 触发: M4.5 右移 2 周 (2027-05 → 2027-06 中) 建议:
- **GM 视角**: 老胡的提案有数据支撑 (M1→M4.5 仅 12 周, 5 件大事无 buffer, ADR-011/012/014 都增工程量)
- **但**: 老板战略层"paper 跑稳定盈利才上实盘"是质量 gate, 不是日历 gate
- **决议**: M4.5 不强右移 — paper 跑出 7 hard gate 通过才算, 时间长短不强约束. M1 (2026-11) 维持
- **6/25 M1 评审复评**: 用真实进度数据再判, 不预判

---

## GM 拍板时间线

- 2026-06-01: GM 拍板 5 ADR (本文档)
- 2026-06-02 EOD: 24h 申辩窗口 (老郭 W5 改进, 任何主管对 GM 拍板有异议入 ADR §"申辩记录")
- 2026-06-03 起 W6: 按新 ADR 执行
- 2026-06-12 (W6 EOW): 老胡周报跟进 5 ADR 执行 + 22 smell 处理率
- 2026-06-25: M1 评审同步 (Smell 老胡#1 复评 M4.5 时机)

---

**完成: GM 拍板 5 ADR + 22 smell 集成 W6 backlog + M4.5 不强右移 (维持质量 gate)**

**老板原话回应:** 投票完成 + GM 拍板 + 不向不合理架构妥协. v0.6 部分撤回 (议题 1 transport 分离 / 议题 5 vCPU 阶段性), 维持高质量产品方向. 申辩窗口 24h 留给主管复议.
