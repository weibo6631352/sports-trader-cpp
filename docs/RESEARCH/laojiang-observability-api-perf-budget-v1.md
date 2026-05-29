# 观测/调试 API — 性能预算 + 红线 (v1)

- Owner: 老姜 (#39, A 系统工程部, 性能 owner)
- Last review: 2026-05-29
- 视角: 仅性能 (热路径零拖累 + 读路径预算 + 回归门禁 + profiling)。不写代码不埋点。
- 依据: 老周 obs-api-arch-v1 §1/§3 (SPSC + double-buffer), 小郑 obs-endpoints-v1, `spsc_queue.hpp` (rigtorp 5ns p50/8ns p99 2-core pinned), `queue_capacities.hpp`, `.github/workflows/perf-regression.yml`, ADR-017/ADR-015, R-12。

---

## 1. 热路径零拖累预算 (R-12 不可妥协)

热路径 (vCPU0/1/2/3) 对观测的唯一动作 = 向独立观测 SPSC ring `try_push` 一帧。预算:

| 项 | 上限 | 依据 |
|---|---|---|
| 单帧 `try_push` (POD ≤ 256B) | **p50 ≤ 20ns, p99 ≤ 80ns** | rigtorp 实测 5/8ns + POD copy + drop_count relaxed CAS。远低于 R-12 100us |
| ring 满 → drop | **p99 ≤ 15ns** (return false + relaxed fetch_add) | wait-free, 绝不 spin/阻塞交易 |
| double-buffer snapshot 发布 (热路径侧) | **写 back buffer + atomic 翻 active_idx, ≤ 30ns** | 单 store-release, 读侧零干扰 |

红线:
- **观测帧必须 POD + 值拷贝**, 禁热路径侧序列化/string/堆分配/锁 (违 = P0)。
- **snapshot/double-buffer 读对热路径影响必须为 0**: gateway 只 `memory_order_acquire` 读 front buffer, 与热路径写 back buffer 无共享 cache line (alignas 隔离, 同 `drop_count_` 模式)。读永不阻塞写。
- **ring 满 drop 观测帧, 绝不背压交易**: MarketDataBus/Signal/Fill 风格的 drop-newest + counter。观测 ring 独立于 5 业务 ring (不复用 RiskQueue, 避免观测争用交易 capacity)。
- wait-free 验证: `bench_spsc_latency.cpp` 已覆盖 push/pop, 观测 ring 复用同 bench 出真实数 (rigtorp 替换 mock 后 re-baseline)。

---

## 2. 观测读路径预算 (gateway 线程, vCPU6, 非热路径)

| 通道 | 预算 | 说明 |
|---|---|---|
| REST handler 序列化 (status/gate/positions) | **p99 ≤ 5ms** | UI poll 1~60s, 小苏 100ms 够, 5ms 留足余量 |
| snapshot 拷贝 (RmDebugSnapshot 等 POD) | **≤ 50us** | front buffer memcpy, 无锁 |
| audit replay / trace 重建 (慢查询) | **≤ 2s** | 隔离 gateway 线程, 与热路径零共享锁 (老周 §1) |
| WSS push 序列化 + write (单帧) | **gateway 侧 p99 ≤ 1ms**; 跨洋链路端到端受 RTT 主导, 不计入本预算 | 跨洋 RTT (~150-250ms) 是物理下界, server 端只控自身 ≤1ms |

跨洋链路红线: WSS server 端 throttle 强制 (market 10Hz/position 1Hz, 老周 §1), **带宽紧 → 推送量受控优先于实时性**; 详细 payload 不走常驻推送 (见 §3)。

---

## 3. "信息尽可能详细" vs 性能权衡

老板要详细, 但详细 ≠ 常驻高频序列化。分层:

- **常驻推送 (WSS, 高频)**: 只推**摘要帧** (L1 盘口 / state 迁移 / reject 摘要 / 计数)。禁 full orderbook / 全链 trace 常驻推送 (序列化成本 O(depth)/O(span), 跨洋带宽炸)。
- **按需拉取 (REST, 低频)**: full orderbook (`/api/v1/market/{id}/book?depth=`)、全链 trace (`/trace/{intent_id}`)、log 检索 — 调试时一次性拉, 序列化成本不进常驻路径。trace 从 audit WAL + trace ring **离线重建** (小郑 §2), 不在热路径留 span。
- **预算**: 按需 detail handler p99 ≤ 50ms (full book depth≤50 / trace span≤16)。**详细信息成本由请求方承担, 永不摊到热路径或常驻推送。**

---

## 4. 回归门禁 (防观测 API 悄悄拖慢热路径)

复用现有 `perf-regression.yml` (10% p99 拦 / 5% warn / lower guard 100ns / e2e upper 50ms), 扩 3 项:

1. **新增 `bench_observability_push`**: 测热路径 `try_push` 观测帧的 p50/p99, 纳入 8→9 bench targets。门禁 p99 ≤ 80ns (§1), 退化 > 10% 拦 PR。
2. **e2e bench 双跑对比**: 观测 ring **开启 vs 关闭** 两组 e2e_latency, delta p99 必须 ≤ **2%** (观测 overhead 上限)。> 2% = 观测拖累热路径, 拦 PR。
3. **静态门禁 (R-12 enforce)**: CI 检查 gateway/obs handler 不得 link 热路径锁符号; 热路径 TU 不得出现观测侧 JSON/string/堆分配调用 (老周 §7 验收项)。

baseline: main push 后 `update-baseline` job 更新 `tests/perf/baselines/main.json` (rolling), 含 observability_push 项。waiver 机制: 已知合理退化走 PR label + 老姜签字, 不静默放行。

---

## 5. profiling / overhead 量化监控

- **micro**: `bench_observability_push` + `bench_spsc_latency` (CI 每 PR), Apple Silicon 本地用 `xctrace` 取 cycle/cache-miss; Linux 部署用 `perf stat` 看 ring push 的 cache-miss / branch-miss。
- **macro (生产)**: 小郑已规划 `stcpp_event_loop_latency_seconds{loop}` histogram (p99/p999) + `stcpp_audit_wal_ring_fill_ratio`。**新增 `stcpp_obs_ring_fill_ratio` + `stcpp_obs_drop_total{ring}`**: drop 率突增 = gateway 消费跟不上, 是观测自身瓶颈 (非交易问题), 但 fill_ratio 持续高 = 背压预警。
- **归因法**: 观测 ring drop > 0 不影响交易 (设计如此), 但若 e2e p99 与 obs_ring_fill_ratio 正相关 → 说明 false sharing / cache 争用泄漏到热路径, 触发 §4.2 双跑复查。
- 监控目标: 观测 overhead 占 e2e budget ≤ 2% 常态, 跨 sprint 趋势进老胡周报。
