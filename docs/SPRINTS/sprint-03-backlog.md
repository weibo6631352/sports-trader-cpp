# Sprint-03 Backlog (存根)

- **Owner**: 老胡 (pm-project-manager)
- **周期**: Sprint-3 日期待老胡 Planning 确认
- **状态**: 预存根 — W7 末老胡 Sprint Planning 后更新
- **last_review**: 2026-06-28 老王 (WAL-B01/B02/B03 初始录入)

---

## WAL group commit + fsync 真实实现 (老王)

来源: `docs/RESEARCH/laowang-tomarketidarray-todo-w7-cleanup.md` Part 2.3
前提: ADR-017 小石 SPSC ring framework 先就绪 (Sprint-3 W9 目标)

| Ticket | 内容 | 依赖 | 目标 Week | Owner |
|---|---|---|---|---|
| WAL-B01 | `WalWriter::Open()` 真实实现: segment fd (O_DIRECT), bg jthread 启动, CPU affinity pin (cfg.bg_cpu_core), rigtorp SPSC ring 初始化 | ADR-017 小石 SPSC framework 就绪 | Sprint-3 W9 | 老王 + 小石联调 |
| WAL-B02 | `WalWriter::Append()` 真实实现: serialize_into (老陈接口) + CRC32C 尾部 + ring.try_push + Backpressure 路径 (WalError::Backpressure) + PerRecord fdatasync condvar 等待 | WAL-B01 就绪; 老陈 serialize_into 接口锁定 | Sprint-3 W10 | 老王 |
| WAL-B03 | `WalWriter::~WalWriter()` 真实实现: bg jthread join/request_stop + ring drain 到空 + fdatasync + close(fd) | WAL-B01 jthread 就绪 | Sprint-3 W11 | 老王 |

**验收条件 (三条共用)**:
- ctest 全量 pass (含 WAL group commit 集成测试)
- WAL-B01/B02/B03 串行依赖: B01 PR merge 后才开 B02, B02 merge 后才开 B03
- 老高 PR review + 老韩 WAL backpressure 行为确认

---

*其余 Sprint-3 tickets 待老胡 Planning 会议补充*
