# Sprint-03 Backlog

- **Owner**: 老胡 (pm-project-manager)
- **周期**: Sprint-3 日期待老胡 Planning 确认 (目标 Sprint Planning 6/29)
- **状态**: WAL-B01/B02/B03 已完整录入 — 其余 tickets 待老胡 Sprint Planning 补充
- **last_review**: 2026-05-28 老王 (WAL-B01/B02/B03 W7 Wave 33 补全)

---

## WAL group commit + fsync 真实实现 (老王)

来源: `wal_writer.cpp` 三处 TODO W4 (已转 Tracked 注释 W7 Wave 33)
前提: ADR-017 小石 SPSC ring framework 先就绪 (Sprint-3 W9 目标)

| Ticket | 内容 | 依赖 | 目标 Week | Owner |
|---|---|---|---|---|
| WAL-B01 | `WalWriter::Open()` 真实实现 | ADR-017 小石 SPSC framework; 老姜 vCPU pin S2-011 6/22 数据 | Sprint-3 W9 | 老王 + 小石联调 |
| WAL-B02 | `WalWriter::Append()` 真实实现 | WAL-B01; 老陈 serialize_into 接口锁定; 老唐 BLAKE3 audit chain | Sprint-3 W10 | 老王 |
| WAL-B03 | `WalWriter::~WalWriter()` 真实实现 | WAL-B01 jthread; WAL-B02 ring | Sprint-3 W11 | 老王 |

---

### WAL-B01: `WalWriter::Open()` 真实实现

**目标 Week**: Sprint-3 W9
**Owner**: 老王 + 小石联调
**依赖**:
- ADR-017 小石 SPSC framework (rigtorp SPSCQueue 或等价实现) 接口锁定
- 老姜 vCPU pin S2-011 6/22 latency budget 数据 (bg_cpu_core 最终值)

**输入**:
- `WalConfig`: `kind` (WalKind), `path_prefix` (string), `capacity` (ring_capacity, 2 的幂), `bg_cpu_core` (optional), `fsync_mode`

**输出** (成功路径):
- segment file descriptor 打开 (`O_WRONLY | O_CREAT | O_APPEND`, 可选 `O_DIRECT`)
- rigtorp SPSC ring 初始化 (capacity = cfg.ring_capacity)
- bg `std::jthread` 启动 (group commit loop: batch 64 OR 1ms flush)
- CPU affinity pin (M5+ 前 OS scheduler 即可; cfg.bg_cpu_core == -1 跳过 pin)
- R-11: 路径前缀硬校验已在 skeleton 实现, 真实 Open 保持不动

**失败路径**:
- segment fd open 失败 → `WalError::Io` (不 abort, 由调用方处理)
- ring 分配失败 → `WalError::Io`

**Acceptance**:
- 5 WalKind (Position / RiskAudit / PaperAudit / ShadowAudit / NonceLedger) 各 1 Open 跑通
- R-11 路径前缀校验 abort 路径单测保留 (已有)
- R-20 4 ts PIT chain 不动 (已在 Append 实现)
- R-11 WalKind 物理隔离: 各 kind 写入不同 path prefix, Open 不跨混

---

### WAL-B02: `WalWriter::Append()` 真实实现

**目标 Week**: Sprint-3 W10
**Owner**: 老王
**依赖**:
- WAL-B01 已 merge (fd + ring + jthread 就绪)
- 老陈 `serialize_into(record, buf)` 接口锁定 (序列化输出 byte span)
- 老唐 BLAKE3 audit chain 接口 (可选: W10 前到位则接入, 否则 CRC32C 先上)

**输入**: `WalRecord` concept instance (任意满足 concept 的 record 类型)

**输出** (成功路径):
- `serialize_into(record, frame_buf)` 序列化 payload
- 构造 `WalRecordHeader` (已有 `FillHeaderFromRecord`, 补填 `len_payload`)
- CRC32C 尾部 4B 计算并追加 (header + payload)
- `ring.try_push(frame)` 推入 SPSC ring
- bg jthread group commit: batch 64 条 OR 1ms 触发一次 `fdatasync`
- `FsyncMode::PerRecord` (position WAL): 推入后同步等待 bg fsync condvar 信号再返回

**失败路径**:
- ring 满 → `WalError::Backpressure` (老韩 v0.3 #18 `AUDIT_WAL_BACKPRESSURE` metric)
- PIT 违反 → `WalError::PitViolation` (已有, 保持不动)
- fdatasync 失败 → `failed_` 置 true, 后续 Append 全返 `WalError::FsyncFailed`

**Acceptance**:
- 1000 笔连续 Append p99 < 5us (老姜 latency budget; FsyncMode::GroupCommit 路径)
- ring 满时正确返回 `Backpressure` 不 block (R-12: bg thread 不阻塞热路径)
- `FsyncMode::PerRecord` 路径: 每笔等 fdatasync 完成 (position WAL RPO=0)
- ctest WAL Append 集成测试 pass

---

### WAL-B03: `WalWriter::~WalWriter()` 真实实现

**目标 Week**: Sprint-3 W11
**Owner**: 老王
**依赖**:
- WAL-B01 jthread 就绪 (request_stop + join 路径)
- WAL-B02 ring + fdatasync condvar 就绪 (drain 路径依赖)

**输入**: dtor 触发 (正常退出 / SIGTERM handler 联动)

**输出** (dtor 成功路径):
1. `jthread.request_stop()` 通知 bg thread 停止接受新任务
2. drain ring: 消费所有剩余帧到 fd (不丢数据)
3. 最终 `fdatasync(fd)` 确保磁盘持久化
4. `close(fd)`
5. `jthread` join (已由 `std::jthread` 析构自动 join, 确认语义)

**SIGTERM 联动**:
- 与小卢 `SingleInstanceLock` SIGTERM handler 协调: signal 触发后
  调用 `WalWriter::~WalWriter()` drain path, 不强杀 jthread

**Acceptance**:
- 正常退出: ring 内剩余全部落盘, 0 数据丢失 (WAL replay 验证)
- SIGTERM 路径: 与小卢 SingleInstanceLock 联调测试 pass
- dtor 后 `failed_` 为 true, 任何后续 Append 调用返回 `WalError::FsyncFailed` (防御)
- ctest WAL dtor + drain 单测 pass

---

## 验收条件 (三条共用)

- ctest 全量 pass (含 WAL group commit 集成测试, Sprint-3 实施时补)
- WAL-B01/B02/B03 串行依赖: B01 PR merge 后才开 B02, B02 merge 后才开 B03
- 老高 PR review + 老韩 WAL backpressure 行为确认
- 老周 first review (每 ticket PR)

---

*其余 Sprint-3 tickets 待老胡 6/29 Planning 会议补充*

---

## Stage-Gate 功能检查 framework (老板 2026-05-29 verbatim 触发)

**来源:** `docs/MEETINGS/2026-05-29-laohu-stage-gate-framework-and-gm-escalation.md`

**老板 verbatim:** "我们每个阶段都要进行功能检查, polymarket专家、金融专家、量化交易专家、ai专家agent都要来检查 ... 直到我们前后端能真正上线盈利, 并且产品经理和用户体验专家他们都对此满意."

| Ticket | 内容 | 依赖 | 目标 Week | Owner |
|---|---|---|---|---|
| STG-001 | GM 老雷 拍板 §3.1 Q1-Q4 4 项授权范围 | 老板 verbatim 入约束 | W8 W5 | 老雷 |
| STG-002 | CPO 老钱 + 老雷 联决盈利量化 KR (§5.1) | STG-001 | Sprint-3 Planning W9 W1 | 老钱 + 老雷 |
| STG-003 | ADR-027 老郭 主审通过 (核心数据结构 SSOT enforce) | 复盘会 §6 | W8 W5 | 老郭 |
| STG-004 | Sprint-3 Planning 加 "Stage-Gate framework 落地" 议题 | STG-001/002/003 | W9 W1 | 老胡 |
| STG-005 | 4 专家 (老李/老叶/小梁/小邓+老何) 进入 Stage-Gate 审查角色预告 | STG-001 | W9 W1 | 老胡 (通过周报 §11) |
| STG-006 | G1 stage-gate 触发日预排 (M1 达 90% 时) | STG-004 | M1 进度跟踪 | 老胡 |

**Stage-Gate 触发条件 (5 个 stage):**

| Stage | 触发条件 | 目标日期 |
|---|---|---|
| G1 (M1 MVP) | 进度 ≥ 90% (34/38 acceptance) | T+6 月 (2026-11-30) |
| G2 (M2 Sharpe) | OOS Sharpe ≥ 1.0, 500 场样本 ≥ 80% | T+8 月 (2026-12) |
| G3 (M4.5 paper 7 gate) | paper runtime ≥ 14 天 + 7 gate 全过 | Sprint-3 W11+ |
| G4 (M5 live 首笔) | G3 通过 + 风控 final ack | T+24 周 |
| G5 (M6 盈利达标) | PnL ≥ KR 阈值 (待 STG-002 拍板) | T+36 月 |

---

## Risk Registry — Sprint-3 风险条目

| Risk ID | 描述 | 影响 | 概率 | mitigation | Owner |
|---|---|---|---|---|---|
| R-STAGE-GATE-001 | GM 老雷 W8 W5 未拍板 §3.1 授权范围 → Stage-Gate framework 启动 hold → Sprint-3 Planning 议题 STG-004 推后 | M | M | 老胡 W8 W5 前再次提醒 GM, 升级路径走 CLAUDE.md §6 (P0 2h 内介入) | 老胡 |
| R-STAGE-GATE-002 | 盈利量化 KR 未拍板 → G5 验收无 measurable 标准 → 老板"上线盈利"无 measurable | H | M | STG-002 强 enforce Sprint-3 Planning 前完成 | 老钱 + 老雷 |
| R-ABI-022 | OrderIntent ABI 漏 token_id (GM 错 #22) → Sprint-3 W9-W10 修复, M4.5 W11 paper runtime 推后风险 P1 | H | M | ADR-027 4 项 enforce + W10 末 M4.5 风险评估 | 老胡 (评估) + 老韩 (实施) |
| R-STAGE-GATE-003 | 4 专家 (老李/老叶/小梁/小邓+老何) 跨单元协调失败 → Stage-Gate 会议无人到 | M | L | STG-005 W9 W1 预告 + 周报 §11 跟进 | 老胡 |

