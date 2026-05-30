# 自研 WAL Framework v0.1

- Owner: 老王 (cpp-persistence-engineer)
- Date: 2026-05-28
- 验收人: 老韩 (RM) + 老唐 (audit schema) + 老周 (architecture) + 老郭 (ADR)
- 关联:
  - `docs/ADR/2026-05-28-gm-signoff-adr-001.md` (§3.3 audit WAL + group commit, §3.3.5 双 WAL 物理分离)
  - `docs/ADR/2026-05-28-gm-signoff-paper-trade.md` (R-11 paper 与 live audit 分离)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.2.md` §12 (audit WAL 架构)
  - `docs/RESEARCH/laozhou-architecture-v0.2.md` §7.3 / §7.4 (WAL 最小可证明设计 + 双 WAL 物理分离)
  - `docs/RESEARCH/laotang-audit-schema-v1.md` (audit record schema, 链式 hash, Merkle anchor)
  - `docs/RESEARCH/xiaoshi-data-structures-selection-v1.md` (rigtorp SPSC 8 ns)
  - `docs/RESEARCH/xiaosong-test-replay-framework-v0.1.md` (测试层 replay, 与 WAL replay 区分)
- Wave: GM 老雷 Wave 6 派单
- 状态: v0.1 待会签 (老韩 / 老唐 / 老周 / 老郭)

---

## 1. 设计原则 + 红线

### 1.1 设计原则

| # | 原则 |
|---|---|
| P1 | **DB 仅审计, 运行时不读 DB** (项目铁律). WAL 不是 DB, 是磁盘上 append-only 日志, 启动期 replay 进内存; 运行期只 append. |
| P2 | **一切 critical state 必先 WAL 后内存** (老周 §7.3.1). nonce 递增 / position 变动 / open-order 状态 必须 WAL fsync 后才视为生效. |
| P3 | **同步路径只 append, 不 fsync** (老韩 §12.2). fsync 走背景线程. |
| P4 | **故障域物理隔离**. 三条 WAL 各有独立文件 / 独立 fd / 独立 fsync 线程 / 独立 cpu core. |
| P5 | **失败即拒, 不退化降级**. SPSC 满 = REJECT + SAFE_MODE. fsync 失败 = SAFE_MODE. WAL 损坏 = SAFE_MODE. 不存在 "best-effort". |
| P6 | **格式向前兼容**. record 头 16B 固定, payload schema 由 owner 自管, framework 不解析 payload (除 replay 时算 CRC). |

### 1.2 红线 (不可妥协)

| # | 红线 | 出处 |
|---|---|---|
| R1 | 三条独立 WAL: `risk_audit` / `position` / `paper_audit` | Wave 6 派单 |
| R2 | Group commit: batch = 64 OR 1ms timer, 取先到 | ADR-001 §3.3 + 老韩 §12 |
| R3 | Fail-closed: WAL 写不进去 → REJECT + SAFE_MODE | 老韩 G7 + W-3 |
| R4 | 同步路径 audit append ≤ 6 us p99 | 老韩 §12.4 + ADR-001 |
| R5 | RPO ≤ 1ms (audit) / 0 (position+nonce) / 1ms (paper) | 老韩 §12.5 + 老周 §7.3.1 |
| R6 | RTO ≤ 30s | Wave 6 派单 |
| R7 | 启动默认进 SAFE_MODE, 显式 unlock 才恢复交易 | GM W-3 |
| R8 | paper_* 数据严禁写入 `position` / `risk_audit` | Paper sign-off R-11 |
| R9 | audit_id 单调递增, 乱序 = bug + 进 SAFE_MODE | 老韩 §5.3 v0.2 新 |

---

## 2. Record 格式

### 2.1 framework 拥有的 envelope (16 B header + payload + 4 B CRC)

```
+--------+--------+--------+--------+--------+--------+--------+--------+
| MAGIC (4B) "WAL1"  | VER(1) | RSVD(1) |   LEN_PAYLOAD (u16 LE)        |
+--------+--------+--------+--------+--------+--------+--------+--------+
|                   SEQ (u64 LE, monotonic per-WAL)                     |
+--------+--------+--------+--------+--------+--------+--------+--------+
|                              PAYLOAD                                  |
|                          (LEN_PAYLOAD bytes)                          |
+--------+--------+--------+--------+--------+--------+--------+--------+
|                       CRC32C (u32 LE) of [MAGIC..PAYLOAD]             |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

- **MAGIC** `"WAL1"` (0x57414C31): record 起始锚, replay 时可用作 resync.
- **VER** = 1 (本设计). VER 不兼容 → replay 拒绝.
- **RSVD** 留 1B 给未来 flag (例如 segment-boundary marker).
- **LEN_PAYLOAD** u16 ≤ 65535. 单条 record 不允许 > 64 KiB; 超出 = caller bug (老唐 audit 单条上限按 schema 推算 ≤ 4 KiB, 富余 16x).
- **SEQ** framework 维护的单调递增序号 (per-WAL, 不跨 WAL). audit_id (ULID 16B) 由 caller 在 payload 内携带 (老唐 schema §2.1), framework 不重复造.
- **PAYLOAD** = caller 序列化好的字节流. framework 不解析. 老唐 schema / 老韩 RM record / paper sim record 各自定义.
- **CRC32C** 算法用 Intel SSE4.2 `_mm_crc32_u64` (Apple Silicon `__crc32cd`), 算上 MAGIC 起到 PAYLOAD 末尾, 不算自己.

### 2.2 audit_id / prev_hash 的归属

- `audit_id` (ULID 16B), `prev_hash` (BLAKE3 32B), `current_hash` (BLAKE3 32B): **由老唐 schema 在 payload 内自己管**, framework 不动.
- framework 仅保证 SEQ 单调 + record 物理完整 (CRC + LEN).
- CI 静态扫描 (老韩 §5.3 v0.2 + 本设计 §11): 启动期 replay 时校验 audit_id ULID 时间戳部分严格递增; SEQ 严格 +1; 任一失败 → SAFE_MODE.

### 2.3 timestamp_ns

- payload 内由 caller 携带 (老唐 schema `evaluated_at_ns`, `ingested_at_ns`).
- framework 仅在 record 进 ring 前打一个**可选** `wal_ingested_at_ns` (caller 不需要时关闭), 用于诊断写入延迟. 不参与 CRC 之外的完整性校验.

---

## 3. 写入路径 (同步 ≤ 6 us)

### 3.1 同步部分 (caller 热路径, RM core 5 / Strategy core 4)

```
caller (RM/Strategy/Paper):
  payload = serialize(record)           # caller 时间, 不算 framework
  seq     = wal->next_seq()              # atomic fetch_add, ~3 ns
  frame   = build_frame(seq, payload)    # memcpy + CRC32C, ~1-2 us (依 payload 大小)
  ok      = wal->ring.try_push(frame)    # rigtorp SPSC, ~8 ns (小石 v1)
  if (!ok) {
    metric: wal_backpressure++
    return REJECTED(<WAL>_BACKPRESSURE)  # fail-closed
  }
  return OK
```

**同步预算分解 (audit, 老韩 §12.4 对齐):**

| 步骤 | p99 |
|---|---|
| ULID gen (caller) | 5 us (含在老韩 evaluate 总预算) |
| build_frame (CRC32C + memcpy ≤ 256B) | ~2 us |
| ring.try_push | < 1 us (小石 SPSC 0.5 us) |
| **framework 同步部分** | **< 3 us** (R4 6us 内, 留 3us 余量) |

### 3.2 异步部分 (背景 fsync 线程, bg core 7)

```
loop on bg core 7:
  pull_batch():
    deadline = now() + 1ms
    while ring.pop(rec):
      buf.append(rec)
      if buf.size() >= 64: break
      if now() >= deadline: break
  if buf.empty():
    park_until(ring.has_data() OR 1ms)
    continue

  write(fd, buf, total_bytes)   # ~5 us
  fdatasync(fd)                  # 100us..1ms SSD typical
  for rec in buf:
    high_watermark = max(high_watermark, rec.seq)
  metric: wal_fsync_latency.observe(...)
  metric: wal_high_watermark.set(high_watermark)
```

- `write` 用 `pwrite` + 显式 offset (避免 `lseek` race), 或单线程下用 `write` + append-only fd (`O_APPEND`). 选 `O_APPEND` + `write` (更省一个 syscall, 单线程持有 fd 无 race).
- `fdatasync` 而非 `fsync` (不需要 metadata 同步, inode 大小 ext4/xfs 会随 data block 一起落盘; APFS macOS 仅开发盘, 生产 Linux).
- batch=64 OR 1ms 取**先到**. 这是 ADR-001 §3.3 钉死.

### 3.3 fail-closed 信号通路

```
ring.try_push fail (SPSC 满):
  caller 同步返回 REJECTED(<WAL>_BACKPRESSURE)
  metric: wal_backpressure_total++ (label=wal_name)
  if backpressure 5min 内 > 10 次:
    SAFE_MODE flag = true (atomic, RM 读到立即拒所有开仓)

fdatasync 失败 (ENOSPC / EIO):
  bg 线程不重试 (避免吃 CPU)
  set SAFE_MODE flag = true
  metric: wal_fsync_failed_total++ (label=wal_name, errno)
  log critical + 触发 alert (老郑 P0)
  bg 线程继续 drain ring (后续 record 仍可入 ring, 但 fsync 不会成功 → 全部 backpressure)
```

---

## 4. 三 WAL 隔离

### 4.1 物理分离矩阵

| 维度 | `risk_audit` (老韩 ownership) | `position` (老周 ownership) | `paper_audit` (小蒋 ownership) |
|---|---|---|---|
| 路径 | `/var/lib/stcpp/audit/risk_audit-*.wal` | `/var/lib/stcpp/exec/position-*.wal` | `/var/lib/stcpp/paper/paper_audit-*.wal` |
| 文件权限 | `0640 stcpp:stcpp-audit` | `0640 stcpp:stcpp-exec` | `0640 stcpp:stcpp-paper` |
| fd 持有者 | 1 个 writer 线程 (core 7) | 1 个 writer 线程 (core 7, 但不同 fd) | 1 个 writer 线程 (core 7) |
| ring 容量 | 65,536 records (4 MiB @ 64B/rec) | 4,096 records (position 中频) | 16,384 records (paper 量比 live 小但带模拟撮合) |
| batch 策略 | 64 OR 1ms | **每条 fsync** (nonce/position critical) + open-order group 64 OR 5ms | 64 OR 1ms |
| RPO 目标 | ≤ 1ms (最后 fsync 到崩溃) | **0** (单条 fsync, 不允许丢) | ≤ 1ms |
| RTO 目标 | ≤ 30s replay | ≤ 30s replay | ≤ 30s replay |
| segment 大小 | 256 MiB | 64 MiB | 64 MiB |
| 保留期 | 本地 7 天 + S3 7 年 (合规) | 本地 7 天 + S3 90 天 | 本地 30 天 + S3 90 天 |

**关键: position WAL 走"每条 fsync"模式**, 不是 group commit. framework 提供两种 mode:

```cpp
enum class FsyncMode {
  GroupCommit,    // batch=64 OR 1ms, audit / paper 用
  PerRecord,      // 每条立即 fsync, nonce/position critical 用
};
```

PerRecord 模式下同步路径**会**等 fsync 完成 (因为 nonce 递增前必须落盘, 这是公司红线). 调用方 (NonceManager / PositionLedger) 自己接受同步 fsync 的 100us..1ms 延迟, 不在 RM 6us 预算内.

### 4.2 不共享 fd / lock

- 三个 WAL instance 各自构造 `WalWriter<RecordT>`, 各自 `open()` fd, 各自 SPSC ring, 各自 bg 线程 (但都 pin 到 core 7, 通过 epoll-style park 避免抢 CPU).
- 不存在共享锁. SAFE_MODE flag 是全局 atomic, 但只写不读 (RM 读), 不构成 contention.

### 4.3 故障域隔离

| 场景 | 影响 |
|---|---|
| `risk_audit` 文件损坏 | RM 进 SAFE_MODE; `position` 仍可继续 (理论上, 但启动期 SAFE_MODE 不让交易, 不会进到 position 写); 监管事件 |
| `position` 文件损坏 | nonce / position 不可恢复, 紧急人工介入. 这是最严重场景. |
| `paper_audit` 文件损坏 | 仅影响 paper 模式, live 不受影响. paper 不算事故, 重置 paper 账本即可. |

---

## 5. Replay 协议

### 5.1 启动流程 (老周 §7.3.5 对齐)

```
T+0.0  systemd 拉起 stcpp 进程
T+0.0  SAFE_MODE = true (red 默认, GM W-3 红线)
T+0.1  open position.wal -> replay (PerRecord mode, 严格)
T+0.5  open risk_audit.wal -> replay (GroupCommit mode, 宽松, 允许 tail truncation)
T+1.0  open paper_audit.wal -> replay (仅 paper 模式启用时)
T+2.0  metric: wal_replay_complete = true
       wait operator unlock OR auto-unlock 条件满足 (老周 §14)
T+N    SAFE_MODE = false (operator unlock)
```

### 5.2 单 WAL replay 算法

```
open file segment list (sorted by seq range)
last_good_seq = 0
last_good_offset = 0
for each segment in order:
  seek to 0
  while not EOF:
    pos0 = tell()
    header = read(16B)
    if EOF: break
    if header.MAGIC != "WAL1": SAFE_MODE + alert "WAL corruption mid-segment" + abort
    if header.VER != 1: SAFE_MODE + alert "WAL version mismatch" + abort
    payload = read(header.LEN_PAYLOAD)
    crc = read(4B)
    if crc != crc32c(header || payload):
      # tail truncation 或损坏
      if at_last_segment AND remaining_bytes_in_file < 1MB:
        # 容忍 tail truncation (kill -9 / 断电 场景), 截断到 last_good_offset
        truncate(file, last_good_offset)
        log warn "WAL tail truncated at offset {}"
        break
      else:
        # mid-file 损坏, 拒绝启动, 等运维介入
        SAFE_MODE + alert "WAL mid-corruption at offset {}" + halt
    if header.SEQ != last_good_seq + 1 AND last_good_seq != 0:
      SAFE_MODE + alert "WAL seq gap: expected {} got {}" + halt
    callback.apply(payload)   # caller (RM / PositionLedger / PaperEngine) 重建内存状态
    last_good_seq = header.SEQ
    last_good_offset = tell()

high_watermark = last_good_seq
next_seq_to_assign = last_good_seq + 1
```

### 5.3 与 SAFE_MODE 联动 (R7 红线)

- 启动期 replay **任何**异常 (mid-corruption / seq gap / version mismatch / fsync 失败 历史) → 拒启动 + 等运维.
- replay 干净通过 → 进 SAFE_MODE (默认), 等运维 `stcpp-ctl unlock` 显式放行.
- 自动 unlock 仅在测试环境允许 (env `STCPP_AUTO_UNLOCK=1`), 生产环境硬关.

### 5.4 与小宋 test-replay-framework 的关系 (澄清)

**两者不是同一个 replay**:

| 维度 | WAL replay (本设计) | 小宋 test-replay-framework |
|---|---|---|
| 输入 | 生产 WAL 文件 (binary frame, 老唐 schema payload) | msgpack fixture (`replay_v0.1.msgpack-schema`) |
| 目的 | 进程崩溃后恢复内存状态 | 测试期复现历史事件流, 验确定性 |
| 时机 | 进程启动期 (一次性) | 测试期 (反复跑) |
| 格式 | 老王 WAL frame + 老唐 audit payload | 小余 EventRecorder msgpack |

**不共享格式**. 但小宋 R4 ("replay 一遍产同款 audit_id 序列") 暗示**生产 WAL 可被解码成 audit 事件供小宋的 GoldenLogAssertion 比对**. 我提供解码工具 `stcpp-wal-cat <file.wal> --format=jsonl` (§9.3), 小宋 fixture 走自己的 EventRecorder, 不强耦合.

---

## 6. 5 种 crash 场景恢复

| # | 场景 | WAL 状态 | framework 行为 | 业务影响 |
|---|---|---|---|---|
| 1 | `kill -9` | ring 内未 flush 部分丢失; 已 fsync 部分完整 | replay 到 last_good_seq, 进 SAFE_MODE | audit RPO ≤ 1ms (~0-64 条 lost ring); position 0 (PerRecord fsync 保证) |
| 2 | 断电 / 主机宕 | 同 #1 + 可能 page cache 中已 `write` 未 `fdatasync` 的也丢 | 同 #1, 文件系统 journal (ext4 data=ordered) 兜底 metadata 不损坏 | 同 #1 |
| 3 | 磁盘满 (ENOSPC) | `write` 或 `fdatasync` 返回 ENOSPC | bg 线程 set SAFE_MODE, ring 继续接收但全部 backpressure REJECT; 人工清磁盘 + 重启 | audit 同步路径 REJECT 所有新 evaluate; position 同 |
| 4 | `fdatasync` 失败 (EIO 硬盘故障) | record 写到 page cache, 未确认落盘 | bg 线程 set SAFE_MODE; 重启后 replay 会发现 CRC OK 但物理可能不在盘上 (取决于 EIO 触发时机) | **最危险**: 磁盘硬故障需老吴 deploy 介入换盘; 触发 §6 cold standby 切换 |
| 5 | WAL 文件损坏 (mid-corruption) | replay 时 CRC 不匹配且非 tail | 拒启动, 等运维 `stcpp-ctl wal-recover` 手动决策 (截断? 从备份恢复?) | RM / PositionLedger 状态不可信, 强人工介入 |

### 6.1 tail truncation 政策

- 仅**最后一个 segment 的末尾 1 MiB 内**允许自动截断 (CRC 不匹配 + 文件末尾不远).
- 截断后留**审计痕迹**: 写一行 `audit/risk_audit-*.wal.truncation.log`, 包含 truncated_at_offset / lost_record_estimate / crash_signature, 老唐 §6 reconcile 会扫这个日志.

### 6.2 fsync 失败后的 cold standby (老吴 cross-region)

- 单机 fdatasync 持续失败 (5 分钟内 > 3 次 EIO) → 触发跨区切换 (老吴 v0.1).
- 切换前: 当前主机 SAFE_MODE 锁死, 不接受新 evaluate.
- 切换后: cold standby 用**异步 S3 副本**的 WAL 重放, 接受**最近 1 分钟数据丢失** (S3 异步上传 lag); 这是公司接受的 RPO trade-off.

---

## 7. 存储 + 副本

### 7.1 本地 SSD (hot path)

- 本地 NVMe SSD (老吴 c6i.xlarge 自带), `/var/lib/stcpp/{audit,exec,paper}/`.
- 文件系统: ext4 `data=ordered, noatime, discard` (老吴 deploy 配置).
- Disk full alert: 磁盘 80% 老郑告警, 90% framework 主动转 SAFE_MODE.

### 7.2 异步 S3 副本 (合规 + 备份, 不在 hot path)

```
独立进程 stcpp-wal-archiver (老吴 systemd timer 每 5 分钟):
  for each closed segment in {audit,exec,paper}:
    compute sha256
    write {segment}.sha256
    if audit segment: compute Merkle root + 写 {segment}.merkle (老唐 §4.2)
    gzip segment -> segment.gz (压缩比 audit ~3x, position ~2x)
    aws s3 cp segment.gz s3://stcpp-wal-archive/{wal_name}/yyyy/mm/dd/segment.gz
    aws s3 cp {segment}.sha256 s3://...
    aws s3 cp {segment}.merkle s3://...
    if upload OK 且 local age > 7 天 (audit) / 90 天 (paper) / 永不本地删 (position):
      delete local segment
```

- **双写不在 hot path**: framework 同步/异步路径都不碰 S3, 由独立 archiver 进程负责.
- **合规 7 年保留** (老唐 schema §5): S3 lifecycle 配置 `Glacier Deep Archive` 90 天后, 7 年后允许删除 (老黄 compliance).

### 7.3 segment 轮转

- 触发条件: 当前 segment 大小 ≥ 配置上限 (audit 256 MiB / position 64 MiB / paper 64 MiB) **或** 整点切.
- 轮转算法 (单线程, bg 中执行):

```
bg 线程 (fsync 完一批之后):
  if active_segment.size >= MAX_SIZE OR hour_changed:
    fdatasync(active_fd)
    close(active_fd)
    rename(active_segment, active_segment + ".closed")
    open new segment (active_seq + 1)
    fdatasync(parent_dir_fd)  # 确保目录条目落盘
```

- rotation 期间 ring 仍可接收 (caller 不感知). 切换 fd 是 bg 线程内单线程动作, 无 race.

---

## 8. 性能 budget

| 路径 | budget | 测量方法 |
|---|---|---|
| `wal->append(record)` 同步 p99 | **≤ 6 us** (含 CRC + ring push, 不含 caller 序列化) | 老姜 micro-bench, S1-011 |
| 背景 fsync 单批 p99 | ≤ 1 ms (64 records or 1ms timer 取先到) | 老姜 micro-bench |
| 背景 fsync 单批 p50 | ≤ 200 us | NVMe SSD 典型 |
| replay 1M 条 audit (含 CRC 校验) | ≤ 30 s | 启动 RTO 红线; 顺读 ~30 MB/s 即可 |
| replay 1M 条 position | ≤ 10 s | position 比 audit 小 + 单条独立 |
| segment rotation 阻塞 caller | **0** (bg 线程做, caller 不等) | — |
| S3 upload (异步, 不在 hot path) | best-effort, lag ≤ 5 min | 老吴 monitoring |

### 8.1 同步预算分解 (audit 路径, R4 6us)

| 步骤 | 平均 | p99 |
|---|---|---|
| ULID gen (caller, 不算 framework) | 200 ns | 5 us |
| record build (memcpy + CRC32C 256B) | 500 ns | 2 us |
| `wal->next_seq()` atomic | 5 ns | 50 ns |
| `ring.try_push` (rigtorp SPSC) | 8 ns | 500 ns |
| metric inc (atomic) | 5 ns | 50 ns |
| **framework 同步合计 (不含 caller)** | **< 1 us** | **< 3 us** |
| **含 caller ULID + serialize** | **< 1 us** | **< 6 us** |

---

## 9. C++ API 草案

### 9.1 核心接口 (header-only template, infra/wal/wal_writer.h)

```cpp
namespace stcpp::infra::wal {

enum class FsyncMode : uint8_t {
  GroupCommit,    // batch=64 OR 1ms (audit / paper)
  PerRecord,      // 每条立即 fsync (position / nonce)
};

struct WalConfig {
  std::string         path_prefix;        // e.g. "/var/lib/stcpp/audit/risk_audit"
  size_t              ring_capacity;      // power of 2, e.g. 65536
  size_t              segment_max_bytes;  // e.g. 256 << 20
  std::chrono::nanoseconds rotation_period{std::chrono::hours{1}};
  FsyncMode           fsync_mode;
  uint16_t            batch_size = 64;
  std::chrono::nanoseconds batch_timeout{std::chrono::milliseconds{1}};
  int                 bg_cpu_core = 7;
  // S3 upload 由外部 archiver 处理, 本 framework 不接 S3
};

// Caller-defined payload type. framework 不解析, 只 memcpy + CRC.
template <typename PayloadT>
concept WalPayload = requires(const PayloadT& p, std::span<std::byte> out) {
  { p.serialize_into(out) } -> std::convertible_to<size_t>;  // returns bytes written
  { PayloadT::max_serialized_size() } -> std::convertible_to<size_t>;
};

template <WalPayload PayloadT>
class WalWriter {
 public:
  static absl::StatusOr<std::unique_ptr<WalWriter>> Open(const WalConfig& cfg);

  // 同步 append. p99 ≤ 6us (含 build + CRC + ring push).
  // 返回 ok = 已入 ring (逻辑持久); 返回 BackpressureError = ring 满, 调用方 fail-closed REJECT.
  // PerRecord 模式下会等到 fsync 完成才返回 ok.
  absl::StatusOr<uint64_t /* seq */> Append(const PayloadT& payload);

  // 显式等待至 high_watermark >= seq (用于 graceful shutdown / 测试).
  absl::Status FlushUntil(uint64_t seq, std::chrono::milliseconds timeout);

  // 当前已 fsync 到的最高 seq.
  uint64_t HighWatermark() const noexcept;

  // 是否处于 fail 状态 (fsync 持续失败 / disk full).
  bool IsFailed() const noexcept;

  ~WalWriter();  // graceful: drain ring + final fsync + close

 private:
  // SPSC ring (rigtorp 包装) + bg thread (pin to bg_cpu_core)
  // ...
};

// Replay 入口. 启动期由 RM / PositionLedger / PaperEngine 各自调用.
template <WalPayload PayloadT>
class WalReplayer {
 public:
  static absl::StatusOr<std::unique_ptr<WalReplayer>> Open(const std::string& path_prefix);

  // Visitor 风格. visitor 返回 false 中断.
  // 返回 last_good_seq, 或错误 (mid-corruption / version mismatch).
  absl::StatusOr<uint64_t> Replay(
      std::function<bool(uint64_t seq, std::span<const std::byte> payload)> visitor,
      ReplayOptions opts = {});
};

struct ReplayOptions {
  bool allow_tail_truncation = true;   // 最后 segment 末尾 1MB 内允许截断
  size_t tail_truncation_bytes = 1 << 20;
};

}  // namespace stcpp::infra::wal
```

### 9.2 与 RM (老韩) 集成示例

```cpp
// risk/gateway.cc (老韩 evaluate hot path)
auto seq_or = audit_wal_->Append(audit_record);
if (!seq_or.ok()) {
  return RejectResult{RejectCode::AUDIT_WAL_BACKPRESSURE};
}
// audit_id 已经在 audit_record 的 payload 里 (ULID, caller 生成)
return DecisionResult{audit_record.audit_id, ...};
```

### 9.3 CLI 工具 (老吴 deploy 同步)

| 工具 | 用途 |
|---|---|
| `stcpp-wal-cat <file>` | 解码 WAL frame → jsonl (调试 / 小宋 fixture 转换) |
| `stcpp-wal-verify <file>` | 全文件 CRC + seq 单调 校验 |
| `stcpp-wal-truncate <file> --to-seq=N` | 手动截断到指定 seq (运维介入) |
| `stcpp-wal-stats <dir>` | segment 列表 / 大小 / 时间范围 |

---

## 10. 配置

### 10.1 TOML (老周 §7 对齐)

```toml
[wal.risk_audit]
path_prefix          = "/var/lib/stcpp/audit/risk_audit"
ring_capacity        = 65536
segment_max_bytes    = 268435456   # 256 MiB
rotation_period_sec  = 3600
fsync_mode           = "group_commit"
batch_size           = 64
batch_timeout_us     = 1000
bg_cpu_core          = 7
local_retention_days = 7
s3_bucket            = "stcpp-wal-archive"
s3_prefix            = "risk_audit/"
s3_retention_years   = 7
compression          = "gzip"

[wal.position]
path_prefix          = "/var/lib/stcpp/exec/position"
ring_capacity        = 4096
segment_max_bytes    = 67108864    # 64 MiB
rotation_period_sec  = 3600
fsync_mode           = "per_record"
bg_cpu_core          = 7
local_retention_days = 7
s3_bucket            = "stcpp-wal-archive"
s3_prefix            = "position/"
s3_retention_days    = 90
compression          = "gzip"

[wal.paper_audit]
path_prefix          = "/var/lib/stcpp/paper/paper_audit"
ring_capacity        = 16384
segment_max_bytes    = 67108864
rotation_period_sec  = 3600
fsync_mode           = "group_commit"
batch_size           = 64
batch_timeout_us     = 1000
bg_cpu_core          = 7
local_retention_days = 30
s3_bucket            = "stcpp-wal-archive"
s3_prefix            = "paper_audit/"
s3_retention_days    = 90
compression          = "gzip"
```

### 10.2 文件系统准备 (老吴 deploy)

```
mkdir -p /var/lib/stcpp/{audit,exec,paper}
chown stcpp:stcpp-audit /var/lib/stcpp/audit
chown stcpp:stcpp-exec  /var/lib/stcpp/exec
chown stcpp:stcpp-paper /var/lib/stcpp/paper
chmod 0750 /var/lib/stcpp/{audit,exec,paper}
mount -o data=ordered,noatime,discard /dev/nvme1n1 /var/lib/stcpp
```

### 10.3 systemd resource (老吴 deploy)

- `LimitNOFILE=8192` (三个 WAL 每个 2-3 fd, 加上 archiver, 富余)
- `IOWeight=900` (优先 WAL IO)
- `CPUAffinity` framework 自己 pin, systemd 不限制

---

## 11. 测试需求 (小宋接口)

### 11.1 单测 (≥ 90% coverage, 我自己写)

- `wal_writer_append_p99` — 微 bench, 单线程 1M append, p99 < 6us
- `wal_writer_backpressure_on_full` — ring 灌满, append 返回 BackpressureError
- `wal_writer_fsync_failure_safe_mode` — mock fsync 返回 EIO, framework set SAFE_MODE flag
- `wal_writer_rotation_no_block` — 触发 rotation, append 不阻塞 > 100us
- `wal_writer_segment_seq_continuity` — 跨 rotation seq 连续
- `wal_replayer_clean` — 写 1M 条, replay 一致
- `wal_replayer_tail_truncation` — 写一半 record, replay 自动截断 + 留 truncation.log
- `wal_replayer_mid_corruption_halt` — 中间故意改一个 byte, replay 拒绝 + SAFE_MODE
- `wal_replayer_seq_gap_halt` — 跳号, replay 拒绝
- `wal_replayer_version_mismatch_halt` — VER=2 record, replay 拒绝

### 11.2 chaos / 5 种 crash 场景 (小宋接口)

`tests/chaos/wal_*` 路径下, 小宋 framework 驱动:

| case | 注入 | 期望 |
|---|---|---|
| `chaos/wal/kill_9_during_append` | `kill -9` 进程 | 重启后 replay 干净, RPO ≤ 1ms |
| `chaos/wal/power_loss_simulation` | fsync 后立即 SIGKILL + `posix_fadvise(DONTNEED)` | 同上 |
| `chaos/wal/disk_full_during_fsync` | `fallocate` 占满分区 | append 返回 BackpressureError, SAFE_MODE = true |
| `chaos/wal/fsync_eio_injection` | LD_PRELOAD mock `fdatasync` 返 EIO | SAFE_MODE = true, alert P0 |
| `chaos/wal/mid_segment_corruption` | 启动前 `dd` 覆盖中间 1 byte | replay 拒启动, exit code = SAFE_MODE_LOCKED |

### 11.3 压测 (老姜 S1-011 接口)

- `bench/wal/append_throughput` — 单线程 / 多 WAL 并行 append throughput
- `bench/wal/fsync_latency` — fdatasync p50 / p99 在不同 batch size 下
- `bench/wal/replay_throughput` — replay MB/s

---

## 12. 开放问题

| # | 问题 | 倾向 | 待会签人 |
|---|---|---|---|
| OQ-1 | rigtorp SPSC 是否能在 PerRecord 模式下回压 caller (等 bg fsync 完)? | 不能, 需要额外 condvar 同步. 倾向: PerRecord 模式 caller 直接同步 write+fsync, 跳过 ring (单线程 nonce/position 写量低, 没必要 ring) | @小石 (lock-free) + @老周 (position WAL owner) |
| OQ-2 | CRC32C 在 ARM (Apple Silicon 开发机) vs x86 (生产) 速度差异是否影响 6us 预算? | 老练 CI 跑 macOS / Linux 对照. 倾向 ARM 与 x86 都 < 1us / 256B. | @老练 (CI) |
| OQ-3 | audit_id ULID 单调性: 多线程 evaluate 时同毫秒生成多个 ULID, monotonic 部分由谁保证? | 老唐 schema 用 ULID monotonic 模式 (同毫秒 +1). framework 仅校验 WAL 内**单调**, 不重新排序. | @老唐 |
| OQ-4 | paper_audit 与 risk_audit 是否需要分别独立的 SAFE_MODE flag? | 倾向**共享**全局 SAFE_MODE: paper WAL 出问题不应拖 live 下水 → 否, paper 失败应只锁 paper, live 继续. 待 GM 拍. | @老韩 + @小蒋 + GM |
| OQ-5 | S3 archiver 单点故障: archiver 挂了, 本地 audit 攒 > 7 天怎么办? | 倾向: 老郑 alert + 本地保留延长到 30 天兜底; archiver 重启自动追传. | @老吴 (deploy) + @老郑 (obs) |
| OQ-6 | macOS 开发机没有 fdatasync, 用 `F_FULLFSYNC` 还是 `fsync`? | 开发机用 `fsync` 即可 (开发机不算合规 RPO 0); 生产 Linux 用 `fdatasync`. CI 跨平台分支 #ifdef. | @老练 |
| OQ-7 | WAL frame VER=1 → VER=2 升级时, 老 WAL 文件如何迁移? | 倾向: VER=1 永久支持 read-only, 新写入 = VER=2, 不做 in-place 升级. 7 年合规期内任何 reader 必须能读 VER=1. | @老唐 + @老黄 (compliance) |
| OQ-8 | ring buffer 是否需要 mmap 文件兜底 (而非纯 RAM), 把 RPO 推到 0? | 老韩 §12.5 明示纯 RAM, RPO ≤ 1ms 接受. mmap 会让 try_push 抖动到 us 级, 破 R4. 倾向**不**做. | @老韩 |

---

## 13. 落地计划

| Sprint | 任务 | Owner | DoD |
|---|---|---|---|
| Sprint-1 W3 | v0.1 设计会签 (本文) | 老王 | 4 签到位 (老韩/老唐/老周/老郭) |
| Sprint-1 W4 | `infra/wal/wal_writer.h` API + skeleton | 老王 | 编译通过 + 头文件 review |
| Sprint-1 W5 | `WalWriter` GroupCommit 实现 + 单测 | 老王 | §11.1 全绿 |
| Sprint-1 W5 | `WalWriter` PerRecord 实现 + 单测 | 老王 | 同上 |
| Sprint-1 W6 | `WalReplayer` 实现 + 单测 | 老王 | §11.1 replay 全绿 |
| Sprint-1 W6 | chaos cases 落地 (小宋协作) | 小宋 + 老王 | §11.2 5 个 case 全绿 |
| Sprint-1 末 | RM v0.2 / Position WAL 切到本 framework | 老韩 + 老周 | RM/Position integration test 通过 |
| Sprint-2 | S3 archiver 独立进程 | 老吴 + 老王 | 5 分钟 lag 内全部 segment 上传 |
| Sprint-2 | `stcpp-wal-*` CLI 工具 | 老王 | 4 个工具齐 |

---

## 14. 风险登记

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| WR-1 | RPO ≤ 1ms 不达 0, 崩溃丢最多 64 条 audit | 中 | 老韩 §12.7 兜底: 失踪 audit 由 sqlite 索引 + 链上 nonce 反向核对 |
| WR-2 | fdatasync 在共享 NVMe 上抖到 > 10ms (邻居进程 IO) | 中 | 独占数据盘 (老吴 deploy); IOWeight=900; metric 监控 p99 |
| WR-3 | macOS 开发机 fsync 语义弱, 测试结果不能代表生产 | 中 | 生产 Linux CI 必跑 (老练) |
| WR-4 | CRC32C 漏检概率 (~2^-32) 在 7 年 PB 级数据下不为 0 | 低 | 老唐 §3.2 已加 BLAKE3 链式 hash 双保险 |
| WR-5 | rigtorp SPSC 在 Apple Silicon 上性能未实测, 可能不达 8ns | 低 | 老练 跑实测 (小石 OQ-4) |
| WR-6 | paper_audit / risk_audit 误配置 → paper 数据进真账本 | **高** | 三 WAL instance 构造时校验 path_prefix 不能跨边界; CI lint; R-11 红线 |
| WR-7 | replay 1M 条 30s 不达 (CRC 校验慢) | 中 | CRC32C 硬件指令; mmap read; 必要时并行多 segment |
