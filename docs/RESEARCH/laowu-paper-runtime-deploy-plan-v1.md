# laowu-paper-runtime-deploy-plan-v1.md
# owner: 老吴 (linux-sre-devops, A-unit, #10)
# last_review: 2026-05-29

## 1. 背景与目标

小颖 M1 audit 指出 paper runtime 的活跃 blocker 之一是 AWS Frankfurt 部署未就位。
本文记录老吴针对 paper runtime 解锁链 ① — 部署侧的工件设计与 Frankfurt 部署计划。

产出工件 (deploy/paper-runtime/):
- `Dockerfile.debug-server` — 多阶段构建
- `stcpp-debug-server.service` — systemd unit
- `docker-compose.yml` — compose 运行方式
- `provision-debug-server.sh` — Frankfurt 实例 bootstrap 脚本
- `docs/RESEARCH/laowu-paper-runtime-deploy-plan-v1.md` — 本文 (说明 + 计划要点)

关联 ADR:
- ADR-038: 观测/调试 API (stcpp_debug_server, 127.0.0.1, SSH 隧道)
- ADR-013 v2: Frankfurt eu-central-1 选址 (Draft, W9 W2 实测 ack)
- ADR-015: vCPU pin (debug server → CPU 1)
- CLAUDE.md R-11: PAPER_MODE=1 强制
- CLAUDE.md R-12: API server 独立线程

---

## 2. Dockerfile 多阶段构建设计

文件: `deploy/paper-runtime/Dockerfile.debug-server`

### 2.1 三个 Stage

```
Stage 1: node-builder   (node:20-slim)
  → COPY frontend/package.json + package-lock.json
  → npm ci --ignore-scripts          (layer cache: 只有 lockfile 变动才重装)
  → COPY frontend/                   (源码后复制)
  → npm run build                    (tsc --noEmit + vite build → dist/)
  产出: /frontend/dist/

Stage 2: cpp-builder    (ubuntu:24.04)
  → gcc-13 + cmake + ninja + libssl-dev
  → cmake -DPAPER_MODE=ON -DSTCPP_EXEC_MODE=paper
  → cmake --build --target stcpp_debug_server  (只编这一个 target)
  产出: /src/build/src/stcpp/debug_api/stcpp_debug_server

Stage 3: runtime        (ubuntu:24.04 minimal)
  → libssl3 + ca-certificates + curl + tzdata (TZ=Europe/Berlin)
  → 非 root 用户 stcpp
  → COPY stcpp_debug_server (from cpp-builder)
  → COPY dist/ (from node-builder)  — 无 Node 运行时
  → HEALTHCHECK curl /healthz
  → CMD: --host 127.0.0.1 --port 8080 --frontend /app/frontend/dist
```

### 2.2 关键设计决策

**运行时纯 C++ (CLAUDE.md §10):**
最终镜像不含 Node.js、npm、构建工具。SolidJS SPA 在 Stage 1 完成编译，输出纯静态文件 (HTML + JS + CSS)，由 cpp-httplib `set_mount_point("/", dist_dir)` 托管。

**Layer cache 优化:**
`package.json` + `package-lock.json` 先于源码 COPY，保证 `npm ci` 只在依赖变动时重跑。C++ 侧先 COPY `CMakeLists.txt` + `include/`，后 COPY `src/`，同理。

**--target stcpp_debug_server:**
CMake 只编译 debug server 这一个可执行文件，跳过 paper_runtime / backtest binary，显著缩短 CI 构建时间。

**ARG STCPP_EXEC_MODE=paper:**
编译期常量注入。docker build 时可传 `--build-arg STCPP_EXEC_MODE=live` 覆盖，无需改代码。

---

## 3. systemd unit 设计

文件: `deploy/paper-runtime/stcpp-debug-server.service`

### 3.1 CLI 绑定

```
ExecStart=/opt/stcpp/bin/stcpp_debug_server \
    --host 127.0.0.1 \
    --port 8080 \
    --frontend /opt/stcpp/frontend/dist
```

`--host 127.0.0.1` 对齐 ADR-038 §5 安全默认，不开 0.0.0.0。远程访问走 SSH 隧道：

```bash
ssh -L 8080:127.0.0.1:8080 ubuntu@<frankfurt-ip> -N
```

### 3.2 安全加固

```
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/opt/stcpp/data /var/log/stcpp
PrivateTmp=true
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=true
```

`ProtectSystem=strict` + `ReadWritePaths` 最小化写入范围。非 root 运行 (User=stcpp)。

### 3.3 凭证管理

```
EnvironmentFile=-/etc/stcpp/paper.env
Environment=PAPER_MODE=1   # 硬 override (R-11 red-line)
```

`/etc/stcpp/paper.env` 由 secret manager (AWS SSM Parameter Store) 在实例启动后写入，`chmod 600`，不入 git。`-` 前缀表示文件缺失时 unit 仍能启动（防止首次 provision 顺序问题）。`PAPER_MODE=1` 作为独立 `Environment=` 行，即使 env_file 漏写也强制保护。

### 3.4 重启策略

```
Restart=on-failure
RestartSec=5s
StartLimitIntervalSec=60s
StartLimitBurst=5
```

崩溃后 5s 重启，60s 内最多 5 次，超出后 systemd 停止重试并告警 (需 Prometheus alertmanager 接收)。

---

## 4. docker-compose.yml 设计

文件: `deploy/paper-runtime/docker-compose.yml`

`docker compose` 作为 systemd 模式的替代运行方式，两种方式选一：

- **systemd 模式** (推荐 paper runtime 联动): binary 直跑，cpu affinity 精准控制 (ADR-015 vCPU pin)
- **docker compose 模式** (推荐快速上线验证): 镜像隔离，回滚方便，适合 MVP 初期

compose 关键点:
- `ports: "127.0.0.1:8080:8080"` — 严格绑 loopback
- `env_file: /etc/stcpp/paper.env` + `PAPER_MODE: "1"` 双重保险
- `deploy.resources.limits.cpus: "0.5"` — debug server 让出资源给热路径
- `logging.driver: journald` — 日志统一进 journald → promtail → Loki (Sprint-3 小郑观测栈)

---

## 5. Frankfurt 部署计划

### 5.1 Region 选型理由

依据 ADR-013 v2 (老郭, 2026-05-29):

**eu-central-1 Frankfurt 是 London-Sofia 连线的近似地理中点。**

| 数据源 | IP / 节点 | RTT 估算 | RTT 实测 (W9 W1 待填) |
|---|---|---|---|
| Polymarket CLOB (`clob.polymarket.com`) | Cloudflare anycast → eu-west-2 London (5 源三角确认) | ~15ms | TBD |
| Goalserve inplay (`inplay.goalserve.com`) | 91.206.228.73, Sofia BG, AS58294 CloudWall | ~20ms | TBD |
| Goalserve pregame (`www.goalserve.com`) | 69.64.x.x, Phoenix AZ, Codero AS18501 | ~100ms | TBD |

us-east-1 Virginia 被撤回的原因: PM CLOB ~130ms + inplay ~145ms，均跨大西洋，不可接受 (ADR-013 v2 §1.2)。

**Frankfurt 优于 Dublin (eu-west-1):**
Dublin PM CLOB ~1ms 极优，但 Dublin→Sofia inplay ~30ms vs Frankfurt→Sofia ~20ms，在 inplay decay tau=25s 场景下 Frankfurt 多 10ms 优势。

**Frankfurt 优于 London (eu-west-2):**
London PM CLOB <1ms 最优，但 London→Sofia ~25ms vs Frankfurt→Sofia ~20ms，inplay 优势权重高于 PM CLOB 亚毫秒差异 (老彭 OQ-P02-3 结论)。

### 5.2 实例规格建议

| 用途 | 推荐规格 | 理由 |
|---|---|---|
| **MVP paper runtime** | `t3.medium` (2 vCPU / 4GB RAM) | ADR-013 v2 老钱 CPO 预算 ~$60-150/月; paper runtime 无高并发要求 |
| **pre-live 压测** | `c5.xlarge` (4 vCPU / 8GB RAM) | live runtime 需要隔离 vCPU + 更大网络带宽 |
| **live runtime (M5+)** | `c5.2xlarge` 或 `m5.xlarge` + Placement Group | ADR-015 vCPU pin + 增强网络 (25Gbps) |

MVP 阶段: `t3.medium` + `eu-central-1` + Ubuntu 24.04 LTS。

**AMI:** `ami-0faab6bdbac9486fb` (Ubuntu 24.04 LTS, eu-central-1, 2024-04 以后版本)。实际 deploy 时用 `aws ec2 describe-images` 查最新 canonical AMI。

**EBS:** 30GB gp3 (系统 + 日志 + data)，加密开启 (KMS default key)。

### 5.3 跨洋 RTT 预期

```
Frankfurt (eu-central-1)
    → clob.polymarket.com (Cloudflare → PM London):   ~15ms  (P50 估算)
    → inplay.goalserve.com (Sofia BG, CloudWall):     ~20ms  (P50 估算)
    → www.goalserve.com (Phoenix AZ, Codero):         ~100ms (P50 估算, pregame 30s 刷新可接受)

参考: us-east-1 同类数字 ~130ms / ~145ms / ~5ms
      Frankfurt 对关键链路 (PM + inplay) 改善 110-125ms
```

注: 以上均为估算，W9 W1 老吴 spin up 3 region t3.micro 24h 实测后用真实数字替换 (输出 `laowu-w9-rtt-3region-<date>.md`)。

### 5.4 Base Image

```
ubuntu:24.04
```

原因:
- LTS 支持至 2029，稳定
- gcc-13 (apt) 原生支持 C++20，与 CLAUDE.md §10 语言规范对齐
- prometheus-node-exporter 在 apt 仓库 (无需 snap/PPA)
- Docker 官方镜像 (distroless 暂不用: strace/curl 运维工具需要 shell)

### 5.5 CI 出镜像流程 (Draft)

```
trigger: push to main (或 release tag)

Step 1: GitHub Actions / GitLab CI
  - job: build-debug-server-image
    runs-on: ubuntu-24.04
    steps:
      - checkout
      - docker buildx build
          --platform linux/amd64
          -f deploy/paper-runtime/Dockerfile.debug-server
          --build-arg BUILD_TYPE=Release
          --build-arg STCPP_EXEC_MODE=paper
          -t stcpp-debug-server:${GIT_SHA}
          -t stcpp-debug-server:latest
          --push
          .

Step 2: Registry
  推送到: ECR (eu-central-1) 或 GHCR
  tag 策略: :latest + :${GIT_SHA_SHORT}

Step 3: Frankfurt 实例拉镜像 (手动 or CD)
  ssh ubuntu@<frankfurt-ip>
  cd /opt/stcpp/src
  docker compose -f deploy/paper-runtime/docker-compose.yml pull
  docker compose -f deploy/paper-runtime/docker-compose.yml up -d
  # 或 systemd 模式: 重新 cmake build + install + systemctl restart
```

MVP 阶段 CI 走 ADR-032 (local-first) + ADR-033 (CI 自动化反馈) 已有的 matrix。Frankfurt 实例 CD 暂手动 (SSH 进去 `git pull + cmake + install`)，Sprint-4 再引入 CD pipeline。

---

## 6. 健康检查设计

### 6.1 Liveness — GET /healthz

```json
{"ok":true,"threads":{"ingest_reactor":"alive","signal_engine":"alive","risk_manager":"alive","paper_signer":"alive","api_server":"alive"},"uptime_sec":N,"as_of_ts":epoch_ns}
```

`"ok":true` 为 liveness 判断条件。Dockerfile HEALTHCHECK + systemd watchdog 均用此 endpoint。

失败条件:
- HTTP 非 200 → binary 崩溃或 httplib event loop 卡住
- `"ok":false` → 内部看门狗检测到线程异常 (W10+ 接真实 watchdog atomic)

告警: Prometheus 通过 blackbox_exporter probe `/healthz` → alertmanager → PagerDuty (Sprint-3 小郑接入)。

### 6.2 Readiness — GET /status data_source 字段

```json
{"state":"RUNNING","mode":"paper","wss_connected":{...},"data_source":"demo","as_of_ts":epoch_ns}
```

`data_source` 枚举:
- `"demo"` — DemoStateProvider (默认, 全面板可渲染, MVP 阶段 standalone)
- `"mixed"` — RealStateProvider (book/rejects 接真实 hub, 其余 demo)
- `"stub-empty"` — StubStateProvider (最小空值, 面板空但结构合法)

Readiness 判断规则:
- paper runtime standalone: `data_source == "demo"` 即就绪
- paper runtime 联动 real hub: `data_source == "mixed"` 且 `wss_connected.sports_api == true`

外部 probe 脚本示例:
```bash
DATA_SOURCE=$(curl -sf http://localhost:8080/status | jq -r '.data_source')
[[ "$DATA_SOURCE" != "stub-empty" ]] && echo "READY" || echo "NOT READY"
```

---

## 7. provision-debug-server.sh 关键步骤

脚本: `deploy/paper-runtime/provision-debug-server.sh`

```
§1 OS 基线: apt upgrade + chrony NTP + ufw
§2 防火墙: deny incoming default; allow 22 (SSH) + 9100 (node-exporter); 8080 不开公网
§3 stcpp user + /opt/stcpp /var/log/stcpp /etc/stcpp 目录
§4 Docker 安装 (--docker 模式选项)
§5 cmake build (systemd 模式):
    §5.1 npm ci + npm run build → /opt/stcpp/frontend/dist
    §5.2 cmake -DPAPER_MODE=ON -DSTCPP_EXEC_MODE=paper → stcpp_debug_server
§6 systemd unit install / docker compose up
§7 prometheus-node-exporter
§8 logrotate
§9 post-provision checklist 打印
```

`--dry-run` 模式打印所有命令不执行，用于变更评审。

---

## 8. 秘密管理原则

CLAUDE.md §8 red-line: 私钥明文禁落盘 / 禁出现在日志。

```
AWS SSM Parameter Store
  → 实例启动后 (user-data 或 CI CD 脚本) 写 /etc/stcpp/paper.env
  → chmod 600 /etc/stcpp/paper.env
  → chown root:stcpp /etc/stcpp/paper.env

systemd 读取: EnvironmentFile=-/etc/stcpp/paper.env
              (- 前缀: 文件缺失不 fatal, 等 secret manager 写入后再 start)

升级路径 (Sprint-4+): systemd-creds (systemd v250+) 替代 EnvironmentFile,
                       secret 加密存储, 仅 service 启动时解密注入
```

`/etc/stcpp/paper.env` 不入 git (`.gitignore` 已排除 `*.env`)。仓库内只有 `.env.example` (无 secret 占位)。

---

## 9. 待办 / 后续

| 项目 | 负责人 | 截止 | 状态 |
|---|---|---|---|
| W9 W1: Frankfurt / Dublin / London t3.micro 24h RTT 实测 | 老吴 | W9 W1 EOW | 待 ADR-013 v2 ack 后执行 |
| W9 W2: ADR-013 v2 ack → 购买 Frankfurt t3.medium | 老雷 GM | W9 W2 | 待实测数据 |
| provision-debug-server.sh 实机跑通 | 老吴 | W10 W1 | 待节点就位 |
| CI 镜像流水线 (ECR push + SSH deploy) | 老吴 + 小郑 | Sprint-4 | 待 Sprint 规划 |
| Prometheus blackbox_exporter 接 /healthz /status 告警 | 小郑 (A 单元) | Sprint-3 | 待观测栈 ADR |
| SSH 隧道管理工具 (autossh / ~/.ssh/config ProxyJump) | 老吴 | W10 | TBD |
| docker compose healthcheck readiness 外部 probe 脚本 | 老吴 | W10 | 本文 §6.2 已给命令 |

---

**老吴 (linux-sre-devops, A-unit, #10), 2026-05-29**
