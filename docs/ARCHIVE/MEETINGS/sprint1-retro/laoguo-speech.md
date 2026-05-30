# Sprint-1 Retro — 老郭发言 (架构评审)

- 发言人: 老郭 (chief-architecture-reviewer)
- Date: 2026-05-28
- 约束: 听取义务 + 双向收口 (GM 跨域听取政策 + 双向回应)
- 必读已扫: ADR-001 / 老周 v0.3 / 老韩 v0.2 / 老王 WAL v0.1 / 老孙 v4 / 老沈 KMS v1 / 老叶 RPC matrix / 老周生命周期 / 老何 C++20 + 8 个 ADR 全过

---

## 0. 三句话总结 (给老雷)

1. **ADR-001 两份 v0.2/v0.3 整改清单 13 项全部落地**, 我升 **Accepted (final)**, 不留 Conditional 尾巴.
2. **老孙 v4 C++ 重写 + 老沈 v2 跨 vendor**, 架构层面**认可**, 但留 1 条硬约束 + 1 条新增 Conditional (SecureBuffer 反汇编 audit Q21 须 Sprint-3 内自动化, 否则降为长期 P1 风险登记).
3. **R-12 / 听取义务 / 长期监控 三 ADR 全部整合到 v0.4**, 我同意, 但 **§17 Wave 10 endpoint 表 + §15.6 vCPU 映射 + W-6 部署变更必须在 v0.4 三件同步落地**, 不允许只搬 §17 不补 §11/§15.

---

## 1. ADR-001 整改验收 — 我升 Accepted

### 1.1 老周 v0.3 对 C-Z1..C-Z7 7 项

| # | ADR-001 整改要求 | v0.3 落位 | 我的核对 |
|---|---|---|---|
| C-Z1 | §11 注明 us-east-1 同区前提 | v0.2 已落 (§13 部署前提), v0.3 §11.3 新预算表延续 | PASS |
| C-Z2 | §12 OQ-5 KMS 同步 CLOSED, 引用老孙 | v0.2 已 CLOSED, 老孙 v4 §5.1 实测延迟 < 250us 同步路径无忧 | PASS |
| C-Z3 | c6i.large 容量与 §8 对齐 (W-6 升级 xlarge) | W-6 GM 批 xlarge, 老吴 v0.2 接力; v0.3 §15.6 4 vCPU 映射对齐 xlarge | PASS |
| C-Z4 | §7.3 自研 WAL 最小可证明设计 | **老王 v0.1 接管**, 三 WAL 物理隔离 + group commit + replay 协议齐 | PASS (派单转给老王是对的, 老周不抢) |
| C-Z5 | §6.2 跨进程 SHM ring 选型 | 小石 S1-011 跟进, 留 Sprint-2; v0.3 §6 通信原语只覆盖进程内 | **延 Sprint-2 通过** (ADR-001 §4.1 已允许这项延期) |
| C-Z6 | §9.3 fail-fast 4 条边界条件 | v0.2 §9.3 已补 (abort 前 flush / 白名单 / 速率限制 / SAFE_MODE 重启) | PASS |
| C-Z7 | RiskGateway link 阻断 + CI + runtime trip-wire | v0.3 §17.8 R-12 enforcement 已扩 PR review checklist; 老练 S1-024 CI 接力 | PASS (架构层面到位, 实施在 Sprint-2) |

**老周 v0.3 升 Accepted (架构层面), C-Z5 单条延 Sprint-2.**

### 1.2 老韩 v0.2 对 C-H1..C-H6 6 项

| # | ADR-001 整改要求 | v0.2 落位 | 我的核对 |
|---|---|---|---|
| C-H1 | STALE 阈值 2s/10s + 5s/15s + 10s/30s, ≤ D-06 30s | §3.7 + §11 专章 已落, GM sign-off W-3 锁死 | PASS (hard block 项清零) |
| C-H2 | audit fsync 改 group commit + WAL | §5.1 + §12 专章, 与老王 WAL v0.1 对接接口确定 | PASS (设计完毕, 实现 Sprint-2 派老王) |
| C-H3 | EDGE_CI_NEGATIVE 单列 enum 拒因 | §3.2 + §3.10 已加 | PASS |
| C-H4 | sqlite PRAGMA NORMAL + WAL mode + 60s checkpoint + 崩溃语义 | §3.8 扩写, nonce manager 兜底语义清晰 | PASS |
| C-H5 | §4.3 状态转移副作用表 (Kelly / DEFERRED / 取消未成交) | 新增 §4.3 表, SAFE_MODE 行已对齐 GM W-3 | PASS |
| C-H6 | RM 事件循环 + fill_in 优先 | §8.1 改写, BATCH_FILL=32 + 1ms tick + 优先级硬约束 | PASS, 但加一条 review 意见, 见 §1.3 |

**老韩 v0.2 升 Accepted (架构层面), R-1 (Kelly slippage) + R-2 (RTT 实测) + R-3 (RPO ≤ 1ms) 三条残留风险登记, 不阻塞.**

### 1.3 收口 (双向回应)

- **Agreed**: ADR-001 13 项整改全过, 升 **Accepted (final)**, 关闭 Conditional 标签.
- **附条件**: C-Z5 (SHM ring) + C-H2 实现 (audit WAL) 延 Sprint-2, 老周老韩各自不必再答辩, 落给老王 + 小石 即可.
- **新增 R-1 (Kelly slippage v0.3)** 由小肖在 Sprint-2 出, 老韩 v0.3 接力, 不在本 ADR 评审范围.
- **不同意?** 请老周老韩**当场反驳**, 我不接受"先这样吧回头再说"的延期 (GM 双向收口政策).

---

## 2. 老孙 v4 C++ + 老沈 v2 跨 vendor — 架构层面认可

### 2.1 我看 v4 的关键点

**认可项 (Accept)**:

1. **8 Blocker C++ 化方案对齐 v2 Rust 设计**, 算法层 0 变更, 仅实现栈替换 — 这是正解, 不是降级.
2. **签名延迟 p99 < 250us** 与 老姜 latency budget 外环 < 200ms 不冲突, 占比 < 0.5%, 不在 critical path.
3. **libsecp256k1 + OpenSSL 3.x + libsodium + libfido2 + boost::asio** 全是经审计 C 库, 攻击面**反而比 Rust SDK transitive dep 小** (无 aws-sdk-rust 上百 transitive crate).
4. **SecureBuffer<T> 三层防优化** (`[[gnu::optnone]]` + `volatile T*` + asm barrier) 设计正确, 与老何 footgun checklist §3.2 对齐.
5. **跨境 Shamir / passphrase / 跨 vendor / Sygnum 不变** — GM 2027-02-26 承诺不受影响.

**架构层面新增 1 条 Conditional (Q21 残留风险)**:

- v4 §9 Q21 自己承认: SecureBuffer 反汇编 audit 目前**手工季度**, 没法 CI 卡. 编译器升级 / LTO flag 变 / `[[gnu::optnone]]` 语义演化任一变化 → memset 被优化掉但下个季度 audit 周期内无人发现 → 私钥可能驻留内存数月.
- **我的硬约束**: Q21 必须在 **Sprint-3 内**交付自动化反汇编 lint 工具 (LLVM IR pass 或 objdump regex). 不交付 = 升级为 P1 长期风险登记, 老何 + 老沈每月手工 audit 写报告.
- **不允许靠"ASAN/UBSAN 兜底"开 Q21** — ASAN 检测的是越界/UAF, 检测不出"memset 被优化掉".

**架构层面 1 条意见 (老高 review 必看)**:

- v4 §4.1 `SecureBuffer` 析构期调 `munlock` + `munmap`. C++ 析构链异常安全:**任何 throw 跨 mlock 边界 = key 残留**. 老高 PR review 必须查"析构链 noexcept" + "throw 不跨过 SecureBuffer 持有边界". 这是 C++ 比 Rust 弱的根本短板, 不能靠 review 兜, 必须 clang-tidy `bugprone-exception-escape` 强制项.

### 2.2 老沈 v2 跨 vendor KMS

- AWS (us-east-1, 过渡) + Sygnum (瑞士, 2027-02-26 终态) + GCP (新加坡, 副 wrap) 三方架构清晰, 与 老黄 jurisdiction sign-off §8.2 一致.
- 老沈条件接受 GM Sygnum 2027-02-26 截止承诺 (`gm-commitment-sygnum-deadline.md`) — 我**架构层面认可**: 任何延期 > 4 周自动升级 GM + 老黄 + 老钱三方会议, 这是合规闸门, 不接受弱化.

### 2.3 收口

- **Agreed (架构层面)**: 老孙 v4 + 老沈 v2 通过, 老沈 + 老高 + 老黄 + 老雷 final sign-off 走自己流程.
- **Conditional (老郭新加)**: Q21 反汇编 audit 自动化 = Sprint-3 内交付, 否则降级 P1.
- **不签字情况**: 若 Q21 在 Sprint-3 末未交付且老何 + 老沈不愿月度手工 audit, 我**保留把 SecureBuffer 风险登记到风险注册表 R-13** 的权利, 由风险委员会 (老胡 + 老郭 + 老韩 + 老沈) 决议是否暂停 live 交易.

---

## 3. R-12 + 听取义务 + 长期监控 三 ADR 整合到 v0.4 — 我同意

### 3.1 R-12 (`gm-redline-websocket-non-blocking.md`)

- 已被 老周 v0.3 §17 完整落地: 12 线程在 4 vCPU 拓扑, vCPU0 R-12 红线区, 静态扫描 + 运行时 trip-wire + PR review checklist 三层.
- **我架构层面认可** (我自己 ADR-001 §2.2 RiskGateway 三层防御立的原则, R-12 是同款方法论扩展到 WSS event loop).
- **一条新增意见**: v0.3 §17.1.1 12 线程在 4 vCPU 上, **vCPU3 跑 8 个线程** (T4-T11), 我担心**内部 starvation** (T8/T9 fsync 与 T6 Goalserve inplay poller 都是 IO bound, 但 fsync 在 RM 同步路径间接背压上). 老周 OQ-R12-3 已识别 (bg_work_ring 4096 容量是否够), 但只覆盖了 ring 容量, 没覆盖 **线程调度优先级**. 我加: **T8/T9 (audit/exec fsync) nice 值优于 T5/T6/T7 (周期轮询)**, 防止 inplay 拉 50ms 解析吃 CPU 把 fsync 推迟到下一个 1ms timer.

### 3.2 听取义务 (`gm-policy-cross-domain-listening.md`)

- 我**架构层面同意**. ADR-001 §5 "跨文档一致性检查" 已经在做这件事 (老韩 §3.7 误用 D-06 = 没读老吴 v0.1 跨洋假设变更), 现在把它升级为 GM 政策硬约束, 是对的.
- **我自己接力的事**: ADR 模板加 "听取确认清单" 章节, 每个被点名的相关人员必须在 ADR 里有发言记录 (含赞同/反对/补充). 这是 §4 GM 政策 §6 派单给我的, 我接 — 本 sprint1-retro 后**立即更新 ADR 模板**, Sprint-2 起所有新 ADR 必带"听取确认清单".
- **双向回应**: 不签字也要明示 ("我没意见" 是赞同, "我没看过" 是不合格).

### 3.3 长期监控 (`gm-policy-api-monitoring-longterm.md`)

- 不是架构议题, 是产品/数据收集节奏问题. **我架构层面默认 Accept**, 不展开. 唯一架构相关一条: 长期 sweep 数据必须**归档进 docs/RESEARCH/api-health-YYYY-MM.md**, 不污染 v1/v2 endpoint matrix 主文档 (老李 v2 + 小段 v2 + 老叶 v1 三份是真值源).

### 3.4 v0.4 整合要求 (硬约束)

老周 v0.4 必须**同步**落以下三件, 不允许只搬一项:

1. **§17 Wave 10 endpoint 表**填实 (老李 + 小段 + 老叶 三方供给, 现在 v0.3 §17.5 是骨架)
2. **§11/§15 性能预算与 vCPU 映射对齐 c6i.xlarge (W-6)** — 老吴 v0.2 升级 xlarge 后, 4 vCPU 物理映射要写死在 §15.6
3. **§17 测试验收 (小宋) 跑过 4 场景** (慢响应 / single-flight / 重连 bulk / R-12 静态扫描), 任一未过 = v0.4 不发

### 3.5 收口

- **Agreed**: R-12 + 听取义务 + 长期监控 三 ADR 整合到老周 v0.4.
- **Compromised**: v0.4 deadline = Sprint-2 第 1 周末 (不是 Sprint-1 末, 因为要等 Wave 10 endpoint 表). 我接受这条延期, 但前提是 v0.3 当前版可启动 Sprint-2 开发 (RM + WAL + signer 三条线不受 v0.4 阻塞).
- **新增 Conditional**: vCPU3 内部线程 nice 优先级 (§3.1 第三段) 必须在 v0.4 §15.6 落地.

---

## 4. 跨文档一致性二次扫 (我自己的硬功课)

我承诺 ADR-001 §8 附言里说过的: 下次评审基于整改清单**逐条核对**. 现在我做了:

| 文档对 | 一致性 | 问题 |
|---|---|---|
| 老周 v0.3 §15.6 vCPU0 only WebSocket vs 老韩 §8.1 RM 单线程 event loop | OK | RM 在 vCPU2, WSS 在 vCPU0, 不抢核 |
| 老韩 §12 audit WAL group commit vs 老王 v0.1 §3 同步预算 ≤ 6us | OK | 同一份预算, 数值对齐 |
| 老周 v0.3 §17.3.1 RM evaluate inline vCPU2 200us vs 老姜内环 50us | OK (我 ADR-001 §5.1 已确认两层预算兼容) | — |
| 老孙 v4 §5 签名 p99 < 250us vs 老姜 latency budget 外环 < 200ms | OK | signer 占比 < 0.5% |
| 老叶 RPC matrix WSS 1 conn × 3 sub vs 老周 v0.3 §17 vCPU0 跑 2 个 reactor | **微调建议** | 老叶建议 Polygon 也是 1 conn 多 sub, 老周 v0.3 §17.1.1 T1 wss_polygon_reactor 单连接是对的, **但 §17.6.3 写到 "T0 是 Polymarket 单 connection multi-channel, T1 是 Polygon 单 connection" 这条没明示 channel-level isolation 怎么做**. 老周 OQ-R12-7 已识别, 留 Sprint-2 老陈 + 老周 解决 |
| 老周生命周期 v1 §2.A.2 (1 conn multiplex + 1 user channel separate) vs v0.3 §17 (T0 only Polymarket reactor) | **微冲突** | 生命周期 v1 说"市场 + user channel **分两个 WebSocket connection**", v0.3 §17.1.1 只写 T0 一个 Polymarket reactor. v0.4 必须明示是 1 conn 还是 2 conn, 老周 决 |

**裁定**:
- 老周 v0.4 §17 必须写清 Polymarket WSS 是 1 conn (market + user channel multiplex) 还是 2 conn (market / user 分离). 老韩 RM 偏好 2 conn (故障隔离, 见生命周期 v1 §2.A.2), 我倾向 2 conn — **故障域隔离永远是 RM 第一原则**, 这与我 ADR-001 §3.3.5 双 WAL 隔离同款理由.

---

## 5. 我的派单 (老郭自己接的活)

| # | 事项 | Deadline | 输出 |
|---|---|---|---|
| 1 | ADR 模板加"听取确认清单" 章节 (GM 政策 §6 派单) | 2026-06-04 (Sprint-1 末) | docs/ADR/_TEMPLATE.md |
| 2 | ADR-001 升 Accepted (final), 关闭 Conditional | 本会议后立即 | 本发言 §1.3 收口生效 |
| 3 | ADR-002 老孙 v4 + 老沈 v2 架构层面评审决议 (含 Q21 Conditional) | 2026-06-04 | docs/ADR/2026-06-04-signer-v4-review.md (新增) |
| 4 | ADR-003 老周 v0.4 整合 R-12 + 听取义务 + vCPU3 nice 优先级 评审 | Sprint-2 W1 末 | docs/ADR/2026-XX-XX-arch-v0.4-review.md |
| 5 | 风险登记 R-13 (SecureBuffer 反汇编 audit) 暂挂, Sprint-3 末再裁 | Sprint-3 末 | 老胡 risk registry 同步 |

---

## 6. 给老雷的请决项

| # | 项 | 我的建议 | 待老雷确认 |
|---|---|---|---|
| L-1 | ADR-001 升 Accepted (final), 关闭 Conditional | 同意 | 是否同意 (架构 + RM 两份 v0.2/v0.3 整改 13 项全过) |
| L-2 | 老孙 v4 + 老沈 v2 架构层面认可, Q21 Sprint-3 内自动化 | 同意 | 是否同意 (Q21 不到位降 P1 长期风险) |
| L-3 | 老周 v0.4 deadline 延 Sprint-2 W1 末 (Wave 10 endpoint 表依赖) | 同意 | 是否同意 |
| L-4 | vCPU3 内部 nice 优先级 (fsync > 周期轮询) 写死 v0.4 §15.6 | 同意 | 知情确认 |
| L-5 | Polymarket WSS 1 conn vs 2 conn 由老周 v0.4 拍, 我倾向 2 conn (故障域隔离) | 倾向 2 conn | 是否同意 (老韩偏好同) |
| L-6 | ADR 模板加听取确认清单, Sprint-2 起新 ADR 强制 | 同意 | 知情确认 |

---

## 7. 老郭附言

ADR-001 老周老韩两份**真过了**, 13 项整改全部落地, 我升 Accepted 不留尾巴.

老孙 v4 C++ 重写**比预想的好**: 算法 0 变更, 仅栈替换, 延迟无差异, GM Sygnum 承诺不受影响. Q21 是唯一架构层面新增风险, Sprint-3 内自动化兜得住.

R-12 + 听取义务 + 长期监控 三 ADR 整合**正当其时**: R-12 是我 ADR-001 §2.2 三层防御方法论扩展, 听取义务是我 §5 跨文档一致性升级, 长期监控是产品节奏 — 三件都该入 v0.4.

**特别表扬一点 (对老韩)**: v0.2 §11 STALE 阈值表 + §4.3 状态转移副作用表 + §12 audit WAL 专章, 这三章是 v0.1 → v0.2 的硬增量, 写法清晰, 与 ADR-001 整改清单逐条对齐 — 这是被点名后**当 sprint 内交答卷**的样板, Sprint-2 其他人参照.

**特别批评一点 (对所有人, 包括我自己)**: 我 ADR-001 §8 附言写"跨文档读不全是评审前的硬功课", 但我本次评审仍然漏看了 老周 生命周期 v1 §2.A.2 (1 conn vs 2 conn 与 v0.3 §17 微冲突). 这是听取义务的具体应用 — Sprint-2 起我评审前必带"听取确认清单"自查表, 不许再漏.

下次 (老周 v0.4) 我评审基于本发言整改清单 + 听取确认清单**两份**核对.

---

**会签:**
- 老郭 (主审): 签
- 老雷 (GM, 待 L-1..L-6 确认): ____
- 老周 (v0.3 owner, 同意 v0.4 deadline 与 vCPU3 nice 调整): ____
- 老韩 (RM v0.2 owner, 同意升 Accepted, 同意 1 conn vs 2 conn 倾向 2 conn): ____
- 老孙 (signer v4 owner, 同意 Q21 Conditional Sprint-3 内自动化): ____
- 老沈 (security, 同意架构层面认可, 自己流程 final sign-off): ____
