#!/usr/bin/env bash
# provision-paper-server.sh — Frankfurt eu-central-1 bootstrap for stcpp_paper_server
# owner: 老吴 (linux-sre-devops, A-unit, #10)
# last_review: 2026-05-29
#
# usage: sudo bash provision-paper-server.sh [--dry-run] [--docker] [--skip-build]
#
#   --dry-run      打印命令不执行 (安全预览)
#   --docker       使用 docker compose 模式 (default: systemd + binary 直跑)
#   --skip-build   跳过 cmake/docker build (binary 已预装时使用)
#
# 前置条件:
#   - Ubuntu 24.04 LTS (AWS AMI eu-central-1, ami-0faab6bdbac9486fb 或同期版本)
#   - ubuntu (sudo) 用户 SSH 进入
#   - /etc/stcpp/paper.env 由 secret manager 预写 (本脚本不写任何 secret)
#   - 仓库已 clone 到 /opt/stcpp/src (或传 REPO_DIR 环境变量覆盖)
#
# 对齐:
#   ADR-013 v2: Frankfurt eu-central-1
#   ADR-038 §5: 127.0.0.1 绑定, SSH 隧道访问
#   CLAUDE.md R-11: PAPER_MODE=1 强制
#   CLAUDE.md §8: 私钥不落盘, secret manager 管理

set -euo pipefail

# ---------------------------------------------------------------------------
# 参数解析
# ---------------------------------------------------------------------------
DRY_RUN=false
USE_DOCKER=false
SKIP_BUILD=false

for arg in "$@"; do
    case "$arg" in
        --dry-run)     DRY_RUN=true ;;
        --docker)      USE_DOCKER=true ;;
        --skip-build)  SKIP_BUILD=true ;;
        --help|-h)
            grep '^#' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "[ERROR] 未知参数: $arg (try --help)" >&2
            exit 1
            ;;
    esac
done

REPO_DIR="${REPO_DIR:-/opt/stcpp/src}"

log()  { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] [provision-paper-server] $*"; }
die()  { echo "[ERROR] $*" >&2; exit 1; }
dry()  {
    if $DRY_RUN; then
        log "DRY-RUN: $*"
    else
        "$@"
    fi
}

$DRY_RUN && log "*** DRY-RUN mode — no system changes ***"

# ---------------------------------------------------------------------------
# §1 OS 基线: 依赖 + 时间同步 + 防火墙
# ---------------------------------------------------------------------------
log "=== §1 OS update + packages ==="
dry apt-get update -qq
dry apt-get upgrade -y -qq

if $USE_DOCKER; then
    dry apt-get install -y --no-install-recommends \
        ca-certificates curl gnupg lsb-release \
        chrony ufw jq prometheus-node-exporter
else
    dry apt-get install -y --no-install-recommends \
        build-essential gcc-13 g++-13 cmake ninja-build git \
        libssl-dev zlib1g-dev ca-certificates curl \
        node.js npm \
        chrony ufw jq prometheus-node-exporter
fi

# NTP: chrony (AWS 内部 169.254.169.123 time server)
dry systemctl enable --now chrony
dry chronyc makestep 1 3 || true

# Timezone: Europe/Berlin (Frankfurt UTC+1/+2)
dry timedatectl set-timezone Europe/Berlin

# ---------------------------------------------------------------------------
# §2 防火墙 (ufw) — SSH 隧道架构, 不开 8080 公网
# ---------------------------------------------------------------------------
log "=== §2 ufw firewall ==="
# 开放:
#   22/tcp   — SSH (管理 + 隧道入口)
#   9100/tcp — Prometheus node-exporter (仅内网 Prometheus scrape)
# 不开放:
#   8080     — stcpp_paper_server (只绑 127.0.0.1, 走 SSH 隧道, ADR-038 §5)
dry ufw --force reset
dry ufw default deny incoming
dry ufw default allow outgoing
dry ufw allow 22/tcp   comment "SSH + SSH tunnel"
dry ufw allow 9100/tcp comment "Prometheus node-exporter (scrape from obs node)"
dry ufw --force enable
dry ufw status numbered

# ---------------------------------------------------------------------------
# §3 stcpp 用户 + 目录
# ---------------------------------------------------------------------------
log "=== §3 stcpp user + dirs ==="
dry useradd -r -s /bin/false -d /opt/stcpp stcpp 2>/dev/null || log "user stcpp already exists"

dry mkdir -p \
    /opt/stcpp/bin \
    /opt/stcpp/frontend/dist \
    /opt/stcpp/data \
    /var/log/stcpp \
    /etc/stcpp

dry chown -R stcpp:stcpp /opt/stcpp /var/log/stcpp
# /etc/stcpp: root-owned, 750 (stcpp 组可读, 全局不可读)
dry chown root:stcpp /etc/stcpp
dry chmod 750 /etc/stcpp

log "NOTICE: /etc/stcpp/paper.env 必须由 secret manager 写入后再启动 service."
log "        chmod 600 /etc/stcpp/paper.env"

# ---------------------------------------------------------------------------
# §4 Docker 安装 (--docker 模式)
# ---------------------------------------------------------------------------
if $USE_DOCKER; then
    log "=== §4 Docker install ==="
    if command -v docker &>/dev/null; then
        log "Docker 已安装: $(docker --version)"
    else
        # 官方 apt 仓库安装
        dry install -m 0755 -d /etc/apt/keyrings
        dry curl -fsSL https://download.docker.com/linux/ubuntu/gpg \
            | gpg --dearmor -o /etc/apt/keyrings/docker.gpg
        dry chmod a+r /etc/apt/keyrings/docker.gpg
        echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] \
https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable" \
            | dry tee /etc/apt/sources.list.d/docker.list > /dev/null
        dry apt-get update -qq
        dry apt-get install -y docker-ce docker-ce-cli containerd.io docker-compose-plugin
        dry systemctl enable --now docker
        log "Docker 安装完成: $(docker --version 2>/dev/null || echo '(dry-run)')"
    fi
fi

# ---------------------------------------------------------------------------
# §5 构建 + 安装 binary (非 --docker 模式, 且非 --skip-build)
# ---------------------------------------------------------------------------
if ! $USE_DOCKER && ! $SKIP_BUILD; then
    log "=== §5 cmake build (systemd 模式) ==="
    [[ -d "${REPO_DIR}" ]] || die "仓库未找到: ${REPO_DIR}. 先执行:\n  git clone <repo> ${REPO_DIR}"

    # §5.1 frontend build (npm run build → /opt/stcpp/frontend/dist)
    log "--- §5.1 frontend npm build ---"
    FRONTEND_DIR="${REPO_DIR}/frontend"
    [[ -d "${FRONTEND_DIR}" ]] || die "frontend/ 目录不存在: ${FRONTEND_DIR}"
    pushd "${FRONTEND_DIR}" > /dev/null
    dry npm ci --ignore-scripts
    dry npm run build
    popd > /dev/null
    # 复制 dist/ 到 /opt/stcpp/frontend/dist
    dry rsync -a --delete "${FRONTEND_DIR}/dist/" /opt/stcpp/frontend/dist/
    dry chown -R stcpp:stcpp /opt/stcpp/frontend/dist
    log "Frontend dist 已复制: /opt/stcpp/frontend/dist"

    # §5.2 C++ cmake build
    log "--- §5.2 cmake build ---"
    dry cmake -B "${REPO_DIR}/build-debug-server" \
          -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DPAPER_MODE=ON \
          -DSTCPP_EXEC_MODE=paper \
          -DCMAKE_C_COMPILER=gcc-13 \
          -DCMAKE_CXX_COMPILER=g++-13 \
          -S "${REPO_DIR}"

    dry cmake --build "${REPO_DIR}/build-debug-server" \
          --target stcpp_paper_server \
          --parallel

    dry install -m 755 \
        "${REPO_DIR}/build-debug-server/src/stcpp/debug_api/stcpp_paper_server" \
        /opt/stcpp/bin/paper_server
    dry chown stcpp:stcpp /opt/stcpp/bin/paper_server
    log "Binary 已安装: /opt/stcpp/bin/paper_server"

elif ! $USE_DOCKER && $SKIP_BUILD; then
    log "=== §5 SKIP build — binary + frontend/dist 必须已预装 ==="
    log "    binary:  /opt/stcpp/bin/paper_server"
    log "    dist:    /opt/stcpp/frontend/dist/"
fi

# ---------------------------------------------------------------------------
# §6 systemd unit 安装 (systemd 模式)
# ---------------------------------------------------------------------------
if ! $USE_DOCKER; then
    log "=== §6 systemd unit install ==="
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

    dry install -m 644 \
        "${SCRIPT_DIR}/stcpp-paper-server.service" \
        /etc/systemd/system/stcpp-paper-server.service

    dry systemctl daemon-reload
    dry systemctl enable stcpp-paper-server
    log "systemd unit 已安装."
    log "启动: systemctl start stcpp-paper-server"

else
    # Docker 模式: 不装 systemd unit, compose 自管
    log "=== §6 docker compose pull/build ==="
    COMPOSE_FILE="${REPO_DIR}/deploy/paper-runtime/docker-compose.yml"
    [[ -f "${COMPOSE_FILE}" ]] || die "docker-compose.yml 未找到: ${COMPOSE_FILE}"
    if ! $SKIP_BUILD; then
        dry docker compose -f "${COMPOSE_FILE}" build
    fi
    log "启动: docker compose -f ${COMPOSE_FILE} up -d"
fi

# ---------------------------------------------------------------------------
# §7 Prometheus node-exporter
# ---------------------------------------------------------------------------
log "=== §7 node-exporter ==="
dry systemctl enable --now prometheus-node-exporter
log "node-exporter: $(systemctl is-active prometheus-node-exporter 2>/dev/null || echo '(dry-run)')"

# ---------------------------------------------------------------------------
# §8 logrotate (systemd 模式落文件日志时使用)
# ---------------------------------------------------------------------------
if ! $USE_DOCKER; then
    log "=== §8 logrotate ==="
    dry tee /etc/logrotate.d/stcpp-paper-server > /dev/null << 'LOGROTATECFG'
/var/log/stcpp/*.log {
    daily
    rotate 14
    compress
    delaycompress
    missingok
    notifempty
    sharedscripts
    postrotate
        systemctl kill -s HUP stcpp-paper-server 2>/dev/null || true
    endscript
}
LOGROTATECFG
fi

# ---------------------------------------------------------------------------
# §9 Post-provision checklist
# ---------------------------------------------------------------------------
log "=== provision-paper-server.sh DONE ==="

cat << 'CHECKLIST'

Post-provision checklist (手动步骤, 按顺序执行):

  [必须] 1. 写入凭证 (secret manager):
            /etc/stcpp/paper.env — POLYMARKET_API_KEY / WALLET_PRIVATE_KEY / GOALSERVE_API_KEY 等
            chmod 600 /etc/stcpp/paper.env
            chown root:stcpp /etc/stcpp/paper.env

  [必须] 2. 写入 /etc/stcpp/paper.toml (应用配置, 无 secret, 可 git 存):
            参见 deploy/paper-runtime/.env.example

  [systemd] 3. systemctl start stcpp-paper-server
  [docker]  3. docker compose -f deploy/paper-runtime/docker-compose.yml up -d

  4. 验证 liveness (本机):
     curl -sf http://localhost:8080/healthz | jq '.ok'
     # 期望: true

  5. 验证 readiness (data_source):
     curl -sf http://localhost:8080/status | jq '.data_source'
     # demo 模式: "demo"
     # real 模式: "mixed"
     # 空: "stub-empty" (表示 provider 未接真实数据)

  6. 远程访问 (SSH 隧道, ADR-038 §5):
     本地: ssh -L 8080:127.0.0.1:8080 ubuntu@<frankfurt-ip> -N &
     浏览器: http://localhost:8080/
     API:    curl http://localhost:8080/healthz

  7. Prometheus scrape 配置 (obs node prometheus.yml):
     - job_name: frankfurt-node
       static_configs:
         - targets: ['<frankfurt-ip>:9100']
     # 注: 9100 已在 ufw 开放; 8080 不对外 (隧道访问)

  8. 验证 PAPER_MODE 强制:
     [systemd] journalctl -u stcpp-paper-server | grep -i paper
     [docker]  docker logs stcpp-paper-server | grep -i paper

RTT 预期 (ADR-013 v2 §3):
  clob.polymarket.com (Cloudflare → eu-west-2 London):  ~15ms (Frankfurt→London)
  inplay.goalserve.com (Sofia BG, AS58294 CloudWall):   ~20ms (Frankfurt→Sofia)
  www.goalserve.com (Phoenix AZ, Codero):               ~100ms (欧洲→美国)
  pregame 30s 刷新周期 → 100ms 可接受

CHECKLIST
