# laowu-paper-runtime-binary-deploy-alignment-v1.md
# owner: 老吴 (linux-sre-devops, A-unit, #10)
# last_review: 2026-05-30 (T1-T5 落地完成 — 三份配置文件全部对齐新 headless binary)

## 背景

GM 老雷在主线 main 上重构 PaperDaemon,预计新增独立 headless `paper_runtime` binary
(RunMode::Headless,无 HttpServer)。本文审计现有部署制品与新 binary 的对齐缺口,
给出改动建议供 GM 落地。只读 + 文档,不动代码/CMake/unit 文件。

关联文件:
- `deploy/paper-runtime/stcpp-paper.service`
- `deploy/paper-runtime/provision.sh`
- `deploy/paper-runtime/Dockerfile`
- `src/stcpp/debug_api/CMakeLists.txt` (当前事实 paper 常驻进程)
- `src/stcpp/bin/CMakeLists.txt` (paper.cpp stub 已删)
- `src/stcpp/paper/CMakeLists.txt` (stcpp_paper_loop 静态库)

---

## 重要前提:当前架构事实

读完代码后先澄清一个关键架构事实,这直接影响后续所有判断。

**当前 paper 常驻进程不是独立的 `paper_runtime`——是 `stcpp_debug_server`。**

`src/stcpp/bin/CMakeLists.txt` 注释明确写道:
> "paper daemon 统一到 debug_server (src/stcpp/debug_api/debug_server_main.cpp):
> 该进程已装配 gamma 发现 + PaperLoop + InplayFeedThread + LiveWss + HttpServer +
> FeatureRecorder,是事实上的 paper 常驻进程 (老板 2026-05-30 定: 统一 debug_server)。"

`debug_server_main.cpp` 的 CLI 是:
```
stcpp_debug_server [--port N] [--host ADDR] [--verbose] [--no-record-ml] [--ml-path PATH]
```
无 `--config TOML`。PaperLoop 在 Step 4b 以 `std::jthread` 启动,与 HttpServer 共进程。

因此:
- `stcpp-paper.service` 的 `ExecStart=/opt/stcpp/bin/paper_runtime --config /etc/stcpp/paper.toml`
  指向一个**目前不存在的 binary**。原始 `bin/paper.cpp` stub 已于 2026-05-30 被老郭评审 B#1 确认删除。
- `provision.sh §4` 的 `install "${REPO_DIR}/build/src/stcpp/paper_runtime" /opt/stcpp/bin/paper_runtime`
  也指向一个**不存在的 build output**。

GM 的重构目标是新建真正的 `paper_runtime` headless binary(复用 PaperDaemon / PaperLoop,
无 HttpServer),即与 `stcpp_debug_server` 分离成两个进程。以下各节基于此目标审计。

---

## §1 CLI 契约对齐

### 现状差距

| 制品 | 期望 | 当前实际 | 差距 |
|---|---|---|---|
| `stcpp-paper.service` ExecStart | `paper_runtime --config /etc/stcpp/paper.toml` | binary 不存在 | P0 |
| `Dockerfile` CMD | `["--config", "/app/config/paper.toml"]` | binary 不存在 | P0 |
| `provision.sh §4` install | 从 build output 安装 `paper_runtime` | build output 不存在 | P0 |
| `debug_server_main.cpp` CLI | 裸 flag (`--port/--host/--verbose`) | 无 `--config` | 架构差距 |

### 选项 A:paper_runtime 先支持裸 flag,unit 暂改 ExecStart 用 flag(推荐)

unit 改为:
```
ExecStart=/opt/stcpp/bin/paper_runtime \
    --ml-path /opt/stcpp/data/ml_capture/quotes.jsonl \
    --verbose
```
或最简无 flag:
```
ExecStart=/opt/stcpp/bin/paper_runtime
```
所有配置项通过 `EnvironmentFile=/etc/stcpp/paper.env` 注入。

**优点:**
- 与 `stcpp_debug_server` CLI 风格一致(已跑通),无新依赖引入
- paper_runtime 若复用 `debug_server_main.cpp` 大部分代码,CLI 解析可直接复用或精简
- MVP 内无需引入 TOML parser(toml++/cpptoml/tinytoml 均需 FetchContent 或手写)
- provision.sh 的 `§4 --config /etc/stcpp/paper.toml` 检查也无需保留,删掉即可
- post-provision checklist 里 "Write /etc/stcpp/paper.toml" 步骤可去掉

**缺点:**
- 未来参数多时 ExecStart 行会变长;但 systemd 支持 `\` 续行,可读性可接受
- 日后要加 TOML 时需补一次 scope

### 选项 B:现在引入 TOML config

**优点:**
- unit 不用随参数增加而修改
- 运维可直接改文件而非 unit/env

**缺点:**
- 需引入 TOML parser 库(FetchContent 或 vendored);CMake 变更会触发 debug_api 模块重构
- paper_runtime 作为新 binary,启动前需要 `/etc/stcpp/paper.toml` 存在,provision.sh
  和 secret manager 流程都需扩展
- MVP 阶段配置项少(tick_interval_ms / bankroll / ml_path / port 等),TOML 的收益
  要等参数多了才显现
- scope 膨胀:headless binary 本体 + TOML parser + provision 配置生成,三件事同时做

### 老吴推荐: 选项 A

理由:MVP 攻坚期 GM 亲自写代码,scope 管控优先。paper_runtime 的配置项在
PaperLoopConfig 里已有结构体默认值(tick_interval_ms=500、bankroll=1000.0 等),
通过 env var override 即可满足运维需要,不需要文件。TOML 可以作为 M2/Sprint-4 的
专项 tech-debt 清偿,单独做。

---

## §2 Binary 安装路径 / CMake install 规则

### CMake 侧:新增 install 目标

当前 `src/stcpp/debug_api/CMakeLists.txt` 有 `add_executable(stcpp_debug_server ...)`,
但无 `install()` 规则。paper_runtime 新 target 建议在对应 CMakeLists.txt 末尾加:

```cmake
# 安装规则 — provision.sh §4 用 cmake --install 替代手动 install(1)
install(TARGETS stcpp_paper_runtime
    RUNTIME DESTINATION bin   # cmake --prefix /opt/stcpp → /opt/stcpp/bin/paper_runtime
)
```

这样 provision.sh 可改用:
```bash
cmake --install "${REPO_DIR}/build" --prefix /opt/stcpp --component paper_runtime
```
比现在的手动 `install -m 755 ... /opt/stcpp/bin/paper_runtime` 更规范,也方便 CI 打包。

### provision.sh §4 需改动(GM 落地,建议如下)

当前第 104-108 行是:
```bash
dry install -m 755 \
  "${REPO_DIR}/build/src/stcpp/paper_runtime" \
  /opt/stcpp/bin/paper_runtime

dry chown stcpp:stcpp /opt/stcpp/bin/paper_runtime
```

改为(路径对齐新 target 的 build output;或改用 cmake --install):
```bash
# 选项 1:直接指定 build output 路径(对应 CMakeLists.txt 中 add_executable 的实际位置)
dry install -m 755 \
  "${REPO_DIR}/build/src/stcpp/paper/<paper_runtime_binary_name>" \
  /opt/stcpp/bin/paper_runtime

# 选项 2:cmake --install(推荐,路径由 CMake install() 规则管理,无需硬编码)
dry cmake --install "${REPO_DIR}/build" --prefix /opt/stcpp

dry chown stcpp:stcpp /opt/stcpp/bin/paper_runtime
```

build target 名称(`stcpp_paper_runtime` 或 GM 自定义)需在 CMakeLists.txt 明确,
provision.sh 才能对齐。GM 落地时请确认 target 名及 output 路径后更新 provision.sh。

### Dockerfile 侧

`Dockerfile` Stage 2 的 COPY 行:
```dockerfile
COPY --from=builder --chown=stcpp:stcpp /src/build/src/stcpp/paper_runtime /app/bin/paper_runtime
```
路径 `/src/build/src/stcpp/paper_runtime` 假设 binary 在 `src/stcpp/paper/` 子目录下。
GM 落地时需确认实际 build output 路径后更新此行。

---

## §3 systemd 健康检查 / Restart / SIGHUP / LimitNOFILE

### headless 无 HTTP:健康检查无法用 curl /health

当前 `stcpp-paper.service` 无 HEALTHCHECK 配置(那是 Docker 的概念),
systemd 的健康由 `Type=simple + Restart=on-failure` 保障:进程退出即重启。
这对 headless binary 是合理的——**只要进程活着,systemd 就认为它 healthy**。

post-provision checklist 第 6 步 `curl http://localhost:9090/health` 是针对
`stcpp_debug_server` 的,对 headless `paper_runtime` 无效。需删除或改为
journal-based 存活验证:
```bash
systemctl is-active stcpp-paper
journalctl -u stcpp-paper --since "1min ago" | tail -5
```

### Restart=on-failure 合理性

`Restart=on-failure` + `RestartSec=5s` + `StartLimitBurst=5/60s` 对 headless paper
daemon 合理。paper_runtime 是长跑服务,崩溃后应自愈。5 次 / 60s 的 burst 限制防止
配置错误导致无限重启。**维持不变。**

### SIGHUP reload

`ExecReload=/bin/kill -HUP $MAINPID` 在 headless binary 中只有在代码实际捕获
SIGHUP 并做 reload 时才有意义。`stcpp_debug_server` 的 logrotate.d 配置用了
`systemctl kill -s HUP stcpp-paper`,如果 paper_runtime 不捕获 SIGHUP,
logrotate 的 postrotate 会静默失败(SIGHUP 发出去但进程忽略)。

**建议 GM 在 paper_runtime main 中注册 SIGHUP handler**,哪怕只是 `std::signal(SIGHUP, SIG_IGN)`
或重新打开 log fd,保证 logrotate postrotate 不静默失败。

### LimitNOFILE=65536

headless paper_runtime 的 fd 需求:
- WSS 到 Polymarket CLOB: 1 个 TCP socket
- Goalserve inplay HTTP poll: 最多 3 个 (soccer/basketball/tennis 各一)
- FeatureRecorder JSONL: 1 个 fd
- PositionLedger WAL: 少量 fd

合计远低于 65536。**但 65536 是无害的上限,维持不变**——后续升 live mode 如需更多
并发连接,不用再改 unit。

### LimitNOFILE 注释需更新

unit 当前注释:`文件描述符 (WSS + CLOB + Goalserve 链路 + debug REST + prometheus /metrics)`

headless 无 `debug REST + prometheus /metrics`,注释需更新为:
```
# 文件描述符 (WSS + CLOB + Goalserve 链路 + FeatureRecorder JSONL + PositionLedger WAL)
```

### 可观测性:headless 无 /metrics 端点

`stcpp-paper.service` 注释提到 "prometheus /metrics",但 headless paper_runtime 无
HttpServer,因此没有 `/metrics` Prometheus 端点。MVP 阶段可观测性路径:

**MVP 够用:stderr → journal**

journal 已通过 `StandardOutput=journal` + `StandardError=journal` + `SyslogIdentifier=stcpp-paper`
接入。`debug_server_main.cpp` 里 PaperLoop 在 Stop() 时打印:
```
ticks=%llu approved=%llu fills=%llu
```
headless paper_runtime 也应在关键节点(每 N tick、每次 approved/fill/reject)
向 stderr 输出结构化行,journald 收集后可用 `journalctl -u stcpp-paper` 监控。

**Sprint-3 升级路径:小郑观测栈**

promtail → Loki → Grafana 已规划(CLAUDE.md §12.5)。journal 是 promtail 的
标准采集源,无需 paper_runtime 内置 /metrics。
Prometheus blackbox_exporter probe headless 进程的替代方案:
- 用 `node_exporter` textfile collector:paper_runtime 定期写 `/var/lib/node_exporter/textfile/*.prom`
- 或依赖 `stcpp-debug-server.service` 的 `/metrics` 端点(两进程同机时)

**结论:MVP 阶段 stderr → journal 够用,不需要在 headless paper_runtime 里内置 HTTP。**

---

## §4 Frankfurt 部署面 (ADR-013)

### paper_runtime 与 debug_server 同机两进程

**结论:同机两进程,互补不替代。**

两个进程的职责分工:
| 进程 | binary | 职责 | 持续运行条件 |
|---|---|---|---|
| `stcpp-paper` | `paper_runtime` | headless PaperLoop + FeatureRecorder + WAL;**无 HTTP** | 纯 daemon,低资源,全天候 |
| `stcpp-debug-server` | `stcpp_debug_server` | HttpServer (127.0.0.1:8080) + 看板 API;paper 联动时读 hub/ledger_hub/quote_hub | 可按需停启,debug/观测用 |

两进程通过共享内存内的 snapshot hub 联动:
- `OrderBookSnapshotHub` / `LedgerSnapshotHub` / `QuoteSnapshotHub` 目前是进程内对象,
  两进程同机时**不能直接共享**——需要 IPC(Unix domain socket / shared memory)。
  这是架构层面的设计决策,需老周 / GM 拍板。

**MVP 最简方案(老吴建议):**
两进程暂时独立运行,各自订阅 Polymarket CLOB WSS(两条独立 TCP 连接)。
paper_runtime 专注 PaperLoop + ML 数据采集,不对外暴露 HTTP。
debug_server 独立运行,查看看板用 SSH 隧道访问 127.0.0.1:8080。
IPC 联动作为 Sprint-4 task(小郑 + 老周)。

### CPUAffinity 资源分配

当前 unit:
- `stcpp-paper.service`: `CPUAffinity=0`(CPU 0)
- `stcpp-debug-server.service`: `CPUAffinity=1`(CPU 1)

t3.medium 2 vCPU,两进程各 pin 一个 core,**刚好分满**。

ADR-015 paper 阶段决议:不强 pin 7 vCPU,paper p99 3.9us 有 200x 余量,跨洋 RTT 200ms
是绝对瓶颈。两进程各占 1 vCPU 的 pin 策略与 ADR-015 一致。

headless paper_runtime 的线程模型:
- main thread(信号监听 + 启动协调)
- PaperLoop jthread(500ms tick,低频)
- InplayFeedThread(soccer/basketball/tennis,3 个线程)
- FeatureRecorder jthread(5s 低频)
- LiveWssTransport io_thread_ + send_thread_(如果 headless 也连 CLOB WSS)

共 7-8 个线程,但 500ms/5s tick 的 paper_runtime CPU 占用极低,pin CPU 0 单核足够。

---

## §5 一句话结论

**`stcpp-paper.service` 和 `provision.sh` 指向的 `paper_runtime` binary 目前不存在,
是遗留自已删 stub 的空壳;GM 落地新 headless binary 后,部署侧需改 4 处,全部是机械改动,
不影响架构。**

---

## paper_runtime 落地后部署侧 TODO 清单

按优先级排序,GM 完成 binary 后由老吴执行:

### T1: unit 改动(stcpp-paper.service)

| 行 | 当前内容 | 改为 |
|---|---|---|
| ExecStart | `ExecStart=/opt/stcpp/bin/paper_runtime --config /etc/stcpp/paper.toml` | 选 A:`ExecStart=/opt/stcpp/bin/paper_runtime`(或加需要的裸 flag) |
| LimitNOFILE 注释 | `(WSS + CLOB + Goalserve 链路 + debug REST + prometheus /metrics)` | `(WSS + CLOB + Goalserve 链路 + FeatureRecorder JSONL + PositionLedger WAL)` |
| (无) | (无) | 确认 SIGHUP handler 存在后保留 ExecReload;否则删掉 ExecReload 行 |

### T2: provision.sh 改动

| 步骤 | 当前内容 | 改为 |
|---|---|---|
| §4 install 路径 | `${REPO_DIR}/build/src/stcpp/paper_runtime` | 对齐 CMakeLists.txt 实际 build output 路径 |
| §8 post-provision checklist 第 2 步 | `Write /etc/stcpp/paper.toml` | 删除(选 A 无需 TOML 文件) |
| §8 post-provision checklist 第 6 步 | `curl http://localhost:9090/health` | 改为 `systemctl is-active stcpp-paper` + `journalctl -u stcpp-paper --since "1min ago"` |

### T3: Dockerfile 改动

| 行 | 当前内容 | 改为 |
|---|---|---|
| COPY 行 | `/src/build/src/stcpp/paper_runtime` | 对齐实际 build output 路径 |
| HEALTHCHECK CMD | `curl -sf http://localhost:9090/health` | 改为进程存活检查:无 HTTP 端点可用 `exit 0`(依赖 Docker restart policy)或删除 HEALTHCHECK |
| CMD | `["--config", "/app/config/paper.toml"]` | 选 A:去掉 `--config` 参数,或改为裸 flag |

### T4: CMake install 规则(请 GM 落地时添加)

在 paper_runtime 的 CMakeLists.txt 中添加:
```cmake
install(TARGETS stcpp_paper_runtime RUNTIME DESTINATION bin)
```
这样 provision.sh 可选 `cmake --install` 替代手动 install(1),更健壮。

### T5: logrotate 同步(provision.sh §7)

当前 logrotate postrotate:
```
systemctl kill -s HUP stcpp-paper 2>/dev/null || true
```
headless paper_runtime 如不捕获 SIGHUP 则 HUP 静默失败。
两个选项:
- GM 在 main 注册 SIGHUP handler → logrotate 保持现状
- 不注册 SIGHUP → logrotate postrotate 改为 `systemctl restart stcpp-paper` (代价:短暂重启)

---

## 非部署侧参考(派给其他人的问题)

以下是审计时发现的非部署层问题,老吴不越界,仅记录供 GM 参考:

1. **两进程 IPC 设计**:paper_runtime 与 stcpp_debug_server 同机时如何共享
   OrderBookSnapshotHub/LedgerSnapshotHub/QuoteSnapshotHub 快照?
   目前两进程各自连 WSS 是 MVP 可接受的临时方案,但长期需老周 / GM 拍板 IPC 机制。

2. **CMake target 名称**:新 headless binary 的 cmake target 名(`stcpp_paper_runtime`?
   `paper_runtime`?)需 GM 明确,provision.sh 依赖这个名字。

3. **RunMode::Headless 对 TOML 的态度**:若 GM 决策 B(引入 TOML),需拉
   小肖 / 老周确认 PaperLoopConfig 序列化方案。

---

**老吴 (linux-sre-devops, A-unit, #10), 2026-05-30**
