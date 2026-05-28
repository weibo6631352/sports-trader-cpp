# 防多开限制 spec v1 — single-instance enforcement (PID + flock)

- **owner:** 老周 (cpp-chief-architect, A 系统工程部主管)
- **last_review:** 2026-05-28
- **status:** draft, 待 GM ack (§7 三问) → 派 W5-A-09 IC (小卢, Sonnet) 实施
- **scope:** `stcpp_paper` / `stcpp_live` / 未来 `stcpp_backtest` binary 强制单实例
- **trigger:** 用户 verbatim 2026-05-28 "程序要有防止多开的限制"
- **关联红线:** R-1 (RM 不可绕过 → 多开 = 2 个 RM = 风控逃逸物理通道), R-7 (ExecutionMode build-time), R-11 (paper 不污染真账本 → 2 个 paper 同时写 paper_audit 也破坏可重放性), R-12 (WSS 非阻塞 → lock 仅启动期, 不进 hot path)
- **关联 ADR:** ADR-005 §2.2 (主管不写代码, 本 spec only), ADR-009 (IC 默认 Sonnet)

---

## §0 问题陈述

paper engine binary `stcpp_paper` (W4 已 stub, W5 接 WSS) 与未来 `stcpp_live` 必须**强制单实例**. 多开后果按严重度:

| 后果 | 严重度 | 触发场景 |
|---|---|---|
| WAL 文件锁冲突 (4 wal kind 同 path 写) | P0 — 数据损坏 | 任意双开 |
| Polymarket WSS 同 wallet 重复订阅 → 账号 ban 风险 | P0 — vendor ToS | live 双开 |
| Polygon nonce 冲突 (同 wallet 2 进程发 tx → 1 失败 1 上链, replay 混乱) | P0 — 资金损失 | live 双开 |
| paper_audit / position 双写 → R-11 物理隔离失效, 回测不可重放 | P0 — 策略不可上线 | paper 双开 |
| Goalserve rate-limit 双倍消耗 → 全公司额度被烧 | P1 | 任意双开 |

结论: **单实例必须是 main() 第一行硬约束, 失败立即 exit, 不允许任何后续逻辑跑.**

---

## §1 选型评估 (4 candidate)

| 方案 | 平台 | 优 | 缺 | 推荐? |
|---|---|---|---|---|
| **PID file + `flock(LOCK_EX\|LOCK_NB)`** | POSIX (macOS + Linux 一致) | 标准 / 简单 / kernel 保证 kill -9 自动释放 / RAII 友好 / 跨 boot 持久路径 | PID file 留尾需读旧 PID 提示 (但不影响 flock 正确性) | **首选 (本 spec 采用)** |
| Unix domain socket `bind` | POSIX | 进程退出 socket 自动释放 | abstract namespace Linux only / filesystem socket 跨 boot 留尾仍需清理 / 比 flock 复杂无收益 | 备选, 不采用 |
| SysV `shmget` key | POSIX | — | 需手动清理 / kill -9 不释放 / 跨平台行为差 | 否决 |
| systemd unit `Type=simple` 单实例 | Linux only | 部署期天然 | 开发 macOS 不支持 / 不解决 dev / CI 多开 / MVP 阶段没 systemd | M5+ 部署 Linux 时**叠加**, 不替代本 spec |

**flock vs fcntl POSIX lock:** 选 `flock(2)` 而非 `fcntl(F_SETLK)` 因为:
- `flock` 锁绑 open file description, fd close 自动释放; `fcntl` 锁绑 pid, 任一 fd close 即释放 → 子进程 fork 后行为反直觉
- macOS + Linux 行为一致 (`flock` 在 macOS 是真 BSD flock, 不是 fcntl emulation, 本地 dev 不踩坑)
- 本场景不需 byte-range, flock 表达力够

---

## §2 设计 (PID file 路径 + flock 协议)

### 2.1 PID file 路径 (按 R-7 ExecutionMode 物理隔离)

```
paper mode:    <BASE>/stcpp/paper.pid
live mode:     <BASE>/stcpp/live.pid
backtest mode: <BASE>/stcpp/backtest.pid   (M4.5 后)
```

`<BASE>` 候选 (待 GM ack §7):
- `/tmp/stcpp/` — dev + CI 友好, 不需 root, **本 spec 倾向**
- `/var/run/stcpp/` — 生产标准, 需 root + tmpfiles.d, M5+ 实盘切换

**强约束:** paper / live / backtest 三 path 必须**物理隔离**, 任一 mode 的 lock 不影响另一 mode (R-7 + R-11 物理化). path 由 build-time `STCPP_EXEC_MODE_*` 决定, 不允许 runtime 覆盖.

### 2.2 启动流程

```
main() 开头第一行 (在任何 WSS / WAL / RM / signer 启动之前):
  1. 取 PID file path = ResolvePath(ExecutionMode::<build-mode>)
  2. mkdir -p <BASE>/stcpp/  (0755, idempotent)
  3. fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0644)
     - O_CLOEXEC 防止 fork+exec 时 fd 泄漏到子进程 (老沈 review 点)
  4. rc = flock(fd, LOCK_EX | LOCK_NB)
     - 失败 (EWOULDBLOCK):
       - 读 PID file 内容 → 解析旧 PID + start_ts + mode + commit
       - stderr 打印: "[single-instance] FATAL: <mode> already running pid=<P> since=<ts> commit=<C>"
       - close(fd) → exit(EXIT_FAILURE = 4)
     - 成功:
       - ftruncate(fd, 0)
       - 写入 "<PID>\n<start_ts_ns>\n<exec_mode>\n<build_commit_hash>\n"
       - fsync(fd)
       - 持锁直到 main 退出
  5. 注册 SIGTERM / SIGINT handler: 优雅退出时 unlink(path) + close(fd)
     (kill -9 不走 handler, kernel 自动释放 flock; PID file 留尾下次启动覆盖, 不影响正确性)
```

### 2.3 PID file 格式 (调试可读)

```
<PID>
<start_ts_ns>          # CLOCK_REALTIME, 用 infra::wal::pit::NowRealtimeNs()
<exec_mode>            # "paper" / "live" / "backtest"
<build_commit_hash>    # CMake -DSTCPP_BUILD_COMMIT=<git rev-parse HEAD> 注入
```

**注意:** 内容仅供运维肉眼读, **不作为 lock 协议依据**. lock 真伪以 flock kernel 状态为准, PID 文本可过期可乱.

### 2.4 异常路径矩阵

| 场景 | 行为 |
|---|---|
| 正常退出 (return / exit) | RAII 析构 close fd → flock 释放; SIGTERM handler 删 PID file |
| SIGTERM / SIGINT | handler 删 PID file + close fd → flock 释放 → exit |
| `kill -9` (SIGKILL) | kernel close fd → flock 自动释放; PID file 留尾, 下次启动 flock 抢成功后覆盖, 正常 |
| panic / SIGSEGV | kernel 同上, PID file 留尾, 正常 |
| 同一 binary 第二次启动 | flock 失败 → 读 PID file 打印旧 PID → exit(4); 不删旧 PID file (避免 race) |
| paper + live 同时启动 (不同 binary, 不同 path) | 各自 lock, 互不影响 (R-7 物理隔离) ✓ |
| build-time mode 与 path 不匹配 | 不可能 — path 由 build-time define 推出, 编译期保证 |

---

## §3 集成点

| # | 落点 | 内容 | 责任人 |
|---|---|---|---|
| 1 | `src/stcpp/infra/process/single_instance.hpp` + `.cpp` | `class SingleInstanceLock` RAII; `acquire()` 失败抛 `SingleInstanceLockFailure` 含旧 PID; 析构 release | W5-A-09 IC |
| 2 | `src/stcpp/infra/process/CMakeLists.txt` | `stcpp_process_lock` static lib; 依赖仅 `stcpp_execution` (拿 ExecutionMode enum); R-7 build-time define 透传 | W5-A-09 IC |
| 3 | `src/stcpp/bin/paper.cpp` main() **第一行** (在 R-7 guard + ExecutionContext::Init 之前) | `SingleInstanceLock lock{ExecutionMode::Paper};` ; 失败 catch 后 exit(4) | W5-A-09 IC |
| 4 | `src/stcpp/bin/live.cpp` (M5+ stub) | 同 3, `ExecutionMode::Live` | M5+ |
| 5 | `tests/unit/test_single_instance.cpp` (≤ 200 行) | 见 §3.1 测试矩阵 | W5-A-09 IC |

### 3.1 测试矩阵 (小宋 review 点)

| 用例 | 期望 |
|---|---|
| 单进程 acquire → release → 再 acquire | 都成功 |
| 双进程 (fork + child acquire, parent acquire 同 path) | parent 拿到 lock, child acquire 抛 `SingleInstanceLockFailure` (或反之, 取决于 fork 时序; 测试用 pipe sync) |
| kill -9 child → parent 再 acquire 同 path | 成功 (kernel 自动释放 flock 验证) |
| paper path acquire 不阻塞 live path acquire | 两 lock 同时持有 (R-7 物理隔离验证) |
| PID file 内容格式 | 4 行, PID 匹配 getpid(), mode 字段匹配 build-time |
| SIGTERM 后 PID file 被 unlink | filesystem 验证文件不存在 |
| 不可写目录 (chmod 000) → acquire | 抛异常, 不 silent fail |

测试 fixture 走 `tmpfs` 路径 (CI 友好), 不污染 `/tmp/stcpp/`. fixture 设计派给小宋 (#56 test-engineer), 不在本 spec 内.

---

## §4 R-1/7/11/12 enforce 对照

- **R-1 (RM 不可绕过):** 单实例保证全系统**只有 1 个 RM 实例**在跑, 杜绝 "两个 RM 各管一半订单" 物理通道
- **R-7 (ExecutionMode build-time):** PID file path 由 `STCPP_EXEC_MODE_*` 编译期决定, 不读 env / 不读 runtime 配置, 不允许 paper / live mix
- **R-11 (paper 不污染真账本):** paper.pid 与 live.pid 路径不同名, M5+ 实盘上线后 paper 进程**不可能**误锁住 live, 物理隔离背书
- **R-12 (WSS 非阻塞):** SingleInstanceLock 是 main() 启动期**一次性**操作 (acquire 一次, 持锁至进程结束), **不进 WSS event loop**, 不在 hot path 调用 flock, 0 us 增量延迟

---

## §5 W5-A-09 派单 (待 GM ack §7 后执行)

**派给:** 小卢 (IC pool, E-035-XX) — 待 HR 小林确认具体工号
**Model:** **Sonnet** (ADR-009 落地, IC 默认 Sonnet, 本任务复杂度匹配)
**预计输出:** ≤ 600 行 (hpp + cpp + test + CMake)
**deadline:** W5 末 EOD

派单 prompt 骨架 (老胡周报留底):

```
Agent(
    subagent_type="senior-cpp-ic-pool",
    model="sonnet",                              # ADR-009 enforce
    prompt="按老周 spec laozhou-single-instance-spec-v1.md 实施:
            1. src/stcpp/infra/process/single_instance.{hpp,cpp}
            2. src/stcpp/infra/process/CMakeLists.txt
            3. 集成进 src/stcpp/bin/paper.cpp main() 第一行
            4. tests/unit/test_single_instance.cpp (覆盖 §3.1 7 个用例)
            约束: 
            - 不改 ExecutionMode / ExecutionContext 现有签名
            - O_CLOEXEC 必须 (老沈 fd 泄漏 review 点)
            - flock 不用 fcntl (本 spec §1 已论证)
            - test 用 tmpfs 路径, 不污染 /tmp/stcpp/
            完成提交 PR 走老高 review."
)
```

---

## §6 平台兼容性

| 平台 | 支持 | 备注 |
|---|---|---|
| macOS (开发) | ✓ | flock 真 BSD 实现, 与 Linux 行为一致 |
| Linux (CI + M5+ 生产) | ✓ | 标准 POSIX flock |
| Windows | ✗ | 公司不部署 Windows, 不考虑; 编译期 `#error` 拦截 |

M5+ 生产 Linux 叠加项 (不在本 spec, 留 future ADR):
- `tmpfiles.d` 配置 `/var/run/stcpp/` 创建 + 权限
- systemd unit `Type=simple` + `Restart=on-failure` 作为**第二道**保险 (defense in depth)

---

## §7 待 GM 拍板 (3 问)

**Q1 — PID file 路径:** `/tmp/stcpp/` (dev + CI 友好, 不需 root, **我倾向**) vs `/var/run/stcpp/` (生产标准, 需 root)?
- 我建议: MVP 阶段全部 `/tmp/stcpp/`, M5+ 实盘上线时 ADR 切换到 `/var/run/stcpp/`
- 影响面: build-time define `STCPP_PID_DIR=/tmp/stcpp` (CMake 默认), 切换只需重编不改代码

**Q2 — W5-A-09 派单时机:** 6/01 EOD (主管周同步后立刻派) vs W6 (避免 W5 末并发 push 乱)?
- 我建议: **6/01 EOD**, 用户原话紧急, 不拖. W5 末 IC 已在收尾期, 本任务 ≤ 600 行独立模块, 不冲突.

**Q3 — ADR-009 Sonnet KPI 首期数据:** 本派单是公司**首次** ADR-009 model 分级落地, 是否进老胡周报 §6 Model KPI?
- 我建议: **是**. 老胡周报第一期记录 IC 工号 + model + cpp lines + 完成时间 + review 结论, 后续做横向对比.

---

## §8 跨域咨询记录 (不耻下问)

| 顾问 | 视角 | 询问点 | 待回 |
|---|---|---|---|
| 老吴 (#16 SRE) | systemd / Linux tmpfiles 标准 | `/var/run/` vs `/tmp/` 在 M5+ 生产是否需 tmpfiles.d? Linux 6.x 是否还支持 BSD flock 语义? | 待回 |
| 老沈 (#15 security) | fd 泄漏 | `O_CLOEXEC` 是否足够? fork() 后 child fd 自动 close 行为是否覆盖所有 exec*? exec 链路有无遗漏? | 待回 |
| 小宋 (#56 test) | 双进程 fixture | gtest 内 fork + pipe sync 模拟双进程 race, 是否有现成 pattern? CI 跨平台 (macOS Apple Silicon + Linux x86_64) flock 行为差异? | 待回 |

---

## §9 验收标准 (W5 末)

- [ ] `src/stcpp/infra/process/single_instance.{hpp,cpp}` ≤ 200 行 + CMake
- [ ] `paper.cpp` main() 第一行 SingleInstanceLock, 失败 exit(4)
- [ ] `tests/unit/test_single_instance.cpp` §3.1 全 7 用例通过 (macOS + Linux CI 双绿)
- [ ] 手测: 同时启 2 个 `stcpp_paper` → 第 2 个秒退 + 报错含旧 PID
- [ ] 手测: kill -9 第 1 个 → 第 2 个启动成功
- [ ] 手测: `stcpp_paper` 与 (将来的 `stcpp_live` stub) 同时跑互不影响
- [ ] 老高 PR review pass
- [ ] 老胡周报 §6 首期 Model KPI 数据录入

---

## §10 不在本 spec (out of scope)

- 跨机器单实例 (M5+ 多节点部署时考虑, 走 etcd / consul lease)
- 同一 binary 不同 wallet 配置的多实例隔离 (M5+ 多账号策略时再设计)
- container 内单实例 (M5+ docker 部署时按 namespace 隔离, 本 spec 仍适用)
- 启动期之外的健康检测 (走 observability 栈, 小郑 #11 立项)

---

**完成汇报:** 防多开 spec v1 + 4 candidate 评估 (PID+flock 首选) + 路径/协议/异常矩阵设计 + 集成点 5 (infra/process 新模块 + paper.cpp main 第一行 + test 矩阵 7 用例) + 待 W5-A-09 派单 (小卢 + Sonnet, 待 GM ack §7 三问)
