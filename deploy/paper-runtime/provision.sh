#!/usr/bin/env bash
# provision.sh — Frankfurt eu-central-1 t3.medium production instance bootstrap
# owner: 老吴 (linux-sre-devops, A-unit, #10)
# date: 2026-05-29
# usage: bash provision.sh [--dry-run] [--skip-build]
#
# 前置条件:
#   - Ubuntu 24.04 LTS 裸机 (AWS AMI ami-0***, eu-central-1)
#   - 以 ubuntu (sudo) 用户 SSH 进去执行
#   - /etc/stcpp/paper.env 由 secret manager 预写 (本脚本不写凭证)
#
# ADR-029 cite: 本脚本在 worktree 内提交 → push → PR, 不由 GM 手动执行.
# ADR-032 cite: 本地 pre-push 先跑, 不等远端 CI.
# CLAUDE.md R-11 red-line: PAPER_MODE=1 在 systemd unit 强制, provision 不绕.

set -euo pipefail

DRY_RUN="${1:-}"
SKIP_BUILD="${2:-}"

log() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] [provision] $*"; }
die() { echo "[ERROR] $*" >&2; exit 1; }

dry() {
  if [[ "${DRY_RUN}" == "--dry-run" ]]; then
    log "DRY-RUN: $*"; return 0
  fi
  "$@"
}

[[ "${DRY_RUN}" == "--dry-run" ]] && log "*** DRY-RUN mode — no system changes ***"

# ---------------------------------------------------------------------------
# §1 OS baseline
# ---------------------------------------------------------------------------
log "=== §1 OS update + packages ==="
dry apt-get update -qq
dry apt-get upgrade -y -qq
dry apt-get install -y --no-install-recommends \
    build-essential gcc-13 g++-13 cmake ninja-build git \
    libssl-dev zlib1g-dev ca-certificates curl \
    netcat-openbsd websocat \
    chrony \
    ufw \
    jq \
    prometheus-node-exporter

# NTP (chrony, AWS time sync)
dry systemctl enable --now chrony
dry chronyc makestep 1 3 || true

# Timezone: Europe/Berlin (Frankfurt)
dry timedatectl set-timezone Europe/Berlin

# ---------------------------------------------------------------------------
# §2 Firewall (ufw)
# ---------------------------------------------------------------------------
log "=== §2 ufw firewall ==="
# Allow only: SSH (22), Prometheus node-exporter scrape (9100), debug REST (9090)
# All outbound: open (PM CLOB / Goalserve / chain RPC)
dry ufw --force reset
dry ufw default deny incoming
dry ufw default allow outgoing
dry ufw allow 22/tcp    comment "SSH"
dry ufw allow 9100/tcp  comment "Prometheus node-exporter"
dry ufw allow 9090/tcp  comment "stcpp debug REST"
dry ufw --force enable
dry ufw status numbered

# ---------------------------------------------------------------------------
# §3 stcpp user + dirs
# ---------------------------------------------------------------------------
log "=== §3 stcpp user + dirs ==="
dry useradd -r -s /bin/false -d /opt/stcpp stcpp 2>/dev/null || log "user stcpp already exists"
dry mkdir -p /opt/stcpp/bin /opt/stcpp/data /var/log/stcpp /etc/stcpp
dry chown -R stcpp:stcpp /opt/stcpp /var/log/stcpp

# Config dir: root-owned, 600 on secret files
dry chown root:root /etc/stcpp
dry chmod 750 /etc/stcpp

log "NOTICE: /etc/stcpp/paper.env MUST be written by secret manager before starting service."
log "        Format: KEY=value (see .env.example for schema, no defaults with secrets)."
log "        chmod 600 /etc/stcpp/paper.env after writing."

# ---------------------------------------------------------------------------
# §4 Build + install binary (skip if --skip-build)
# ---------------------------------------------------------------------------
if [[ "${SKIP_BUILD}" != "--skip-build" ]]; then
  log "=== §4 cmake build ==="
  REPO_DIR="${REPO_DIR:-/opt/stcpp/src}"
  [[ -d "${REPO_DIR}" ]] || die "Source not found at ${REPO_DIR}. Clone repo first:\n  git clone <repo> ${REPO_DIR}"

  dry cmake -B "${REPO_DIR}/build" \
        -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DPAPER_MODE=ON \
        -DCMAKE_C_COMPILER=gcc-13 \
        -DCMAKE_CXX_COMPILER=g++-13 \
        -S "${REPO_DIR}"

  dry cmake --build "${REPO_DIR}/build" --parallel

  dry install -m 755 \
    "${REPO_DIR}/build/src/stcpp/paper_runtime" \
    /opt/stcpp/bin/paper_runtime

  dry chown stcpp:stcpp /opt/stcpp/bin/paper_runtime
  log "Binary installed: /opt/stcpp/bin/paper_runtime"
else
  log "=== §4 SKIP build (--skip-build) — binary must be pre-installed at /opt/stcpp/bin/paper_runtime ==="
fi

# ---------------------------------------------------------------------------
# §5 systemd unit install
# ---------------------------------------------------------------------------
log "=== §5 systemd unit ==="
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

dry install -m 644 \
  "${SCRIPT_DIR}/stcpp-paper.service" \
  /etc/systemd/system/stcpp-paper.service

dry systemctl daemon-reload
dry systemctl enable stcpp-paper

log "systemd unit installed. Start with: systemctl start stcpp-paper"
log "Watch logs with: journalctl -u stcpp-paper -f"

# ---------------------------------------------------------------------------
# §6 Prometheus node-exporter
# ---------------------------------------------------------------------------
log "=== §6 node-exporter ==="
# Ubuntu 24.04 apt includes prometheus-node-exporter
# Default listens :9100 — already allowed in ufw §2
dry systemctl enable --now prometheus-node-exporter
log "node-exporter status: $(systemctl is-active prometheus-node-exporter 2>/dev/null || echo 'not running (dry-run)')"

# ---------------------------------------------------------------------------
# §7 Log rotation (logrotate for /var/log/stcpp)
# ---------------------------------------------------------------------------
log "=== §7 logrotate ==="
dry tee /etc/logrotate.d/stcpp > /dev/null << 'LOGROTATECFG'
/var/log/stcpp/*.log {
    daily
    rotate 14
    compress
    delaycompress
    missingok
    notifempty
    sharedscripts
    postrotate
        systemctl kill -s HUP stcpp-paper 2>/dev/null || true
    endscript
}
LOGROTATECFG

# ---------------------------------------------------------------------------
# §8 Post-provision checklist output
# ---------------------------------------------------------------------------
log "=== provision.sh DONE ==="
cat << 'CHECKLIST'

Post-provision checklist (manual steps):
  1. Write /etc/stcpp/paper.env via secret manager (POLYMARKET_API_KEY, WALLET_PRIVATE_KEY, GOALSERVE_API_KEY, etc.)
     chmod 600 /etc/stcpp/paper.env
  2. Write /etc/stcpp/paper.toml (app config — no secrets, safe to git)
  3. systemctl start stcpp-paper
  4. systemctl status stcpp-paper
  5. journalctl -u stcpp-paper -f     (watch startup logs)
  6. curl http://localhost:9090/health  (debug REST health)
  7. curl http://localhost:9100/metrics (Prometheus node-exporter)
  8. Add Frankfurt instance IP to Prometheus scrape targets in obs node prometheus.yml

Verify PAPER_MODE enforcement:
  journalctl -u stcpp-paper | grep PAPER_MODE

ADR-013 v2 context:
  - This instance is the Frankfurt eu-central-1 production server.
  - ADR-013 v2 status: Draft -> Accepted pending W10 W2 RTT measurement ack.
  - If ADR-013 v2 is refuted (London/Dublin better), terminate this instance and re-run
    provision.sh targeting eu-west-2 or eu-west-1.

CHECKLIST
