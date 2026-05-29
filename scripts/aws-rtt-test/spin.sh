#!/usr/bin/env bash
# spin.sh — AWS 3-region t3.micro spot spin-up for 24h RTT test
# owner: 老吴 (SRE, A-unit, #10)
# date: 2026-05-29
# usage: bash spin.sh [--dry-run]
# input: Plan §1.2 — eu-central-1 / eu-west-1 / eu-west-2
# AWS pricing (on-demand): eu-central-1 $0.0116/h, eu-west-1 $0.0116/h, eu-west-2 $0.0131/h
# source: https://aws.amazon.com/ec2/pricing/on-demand/ @ 2026-05-29
# spot discount ~70% -> expected total <$0.40 for 24h; on-demand upper bound $1.50

set -euo pipefail

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------
REGIONS=(eu-central-1 eu-west-1 eu-west-2)
INSTANCE_TYPE="t3.micro"
KEY_NAME="${AWS_KEY_NAME:-laowu-sre-key}"
SG_NAME="laowu-rtt-test-sg"
TAG_OWNER="laowu"
TAG_PURPOSE="rtt-test-24h"
UBUNTU_OWNER="099720109477"            # Canonical official AMI owner
UBUNTU_FILTER="ubuntu/images/hvm-ssd/ubuntu-noble-24.04-amd64*"
DRY_RUN="${1:-}"

# Egress-only SG: instances only need outbound to measure RTT
# Inbound: SSH (22) from SRE source IP only (default: current public IP)
SRE_IP=$(curl -s --max-time 5 https://checkip.amazonaws.com/ 2>/dev/null || echo "0.0.0.0")
SRE_CIDR="${SRE_IP}/32"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*"; }

dry() {
  if [[ "${DRY_RUN}" == "--dry-run" ]]; then
    log "DRY-RUN: $*"
    return 0
  fi
  "$@"
}

resolve_ami() {
  local region="$1"
  aws ec2 describe-images \
    --region "${region}" \
    --owners "${UBUNTU_OWNER}" \
    --filters "Name=name,Values=${UBUNTU_FILTER}" \
              "Name=state,Values=available" \
              "Name=architecture,Values=x86_64" \
    --query 'sort_by(Images, &CreationDate)[-1].ImageId' \
    --output text
}

ensure_sg() {
  local region="$1"
  # Check if SG already exists
  local sg_id
  sg_id=$(aws ec2 describe-security-groups \
    --region "${region}" \
    --filters "Name=group-name,Values=${SG_NAME}" \
    --query 'SecurityGroups[0].GroupId' \
    --output text 2>/dev/null || echo "None")

  if [[ "${sg_id}" == "None" || -z "${sg_id}" ]]; then
    log "[${region}] Creating security group ${SG_NAME} ..."
    sg_id=$(aws ec2 create-security-group \
      --region "${region}" \
      --group-name "${SG_NAME}" \
      --description "RTT test SG — laowu 2026-05-29" \
      --query 'GroupId' \
      --output text)

    # Inbound: SSH from SRE IP only
    aws ec2 authorize-security-group-ingress \
      --region "${region}" \
      --group-id "${sg_id}" \
      --protocol tcp \
      --port 22 \
      --cidr "${SRE_CIDR}" > /dev/null

    # Egress: already open by default (all traffic allowed)
    log "[${region}] SG created: ${sg_id} (SSH ingress from ${SRE_CIDR})"
  else
    log "[${region}] Existing SG found: ${sg_id}"
  fi

  echo "${sg_id}"
}

# ---------------------------------------------------------------------------
# User-data: installs tools + launches 4 measurement loops on boot
# Outputs CSVs to /data/<chain>.csv; also writes PIDs to /data/pids
# ---------------------------------------------------------------------------
build_user_data() {
  local region="$1"
  cat <<'USERDATA'
#!/usr/bin/env bash
set -euo pipefail
mkdir -p /data

apt-get update -qq
apt-get install -y -qq netcat-openbsd curl jq > /dev/null 2>&1

# websocat binary (static build from GitHub releases)
WEBSOCAT_VER="1.13.0"
curl -fsSL \
  "https://github.com/vi/websocat/releases/download/v${WEBSOCAT_VER}/websocat.x86_64-unknown-linux-musl" \
  -o /usr/local/bin/websocat
chmod +x /usr/local/bin/websocat

# ---------- Chain 1: PM CLOB TCP+TLS RTT (every 60s -> 1440 samples/24h) ----------
cat > /usr/local/bin/chain1_pm_clob.sh << 'EOF'
#!/usr/bin/env bash
while true; do
  ts=$(date +%s)
  result=$(curl -s -o /dev/null \
    --noproxy '*' \
    --connect-timeout 5 \
    --max-time 10 \
    -w "%{time_connect},%{time_total}" \
    "https://clob.polymarket.com/" 2>/dev/null || echo "err,err")
  echo "${ts},${result}"
  sleep 60
done
EOF
chmod +x /usr/local/bin/chain1_pm_clob.sh
nohup /usr/local/bin/chain1_pm_clob.sh >> /data/pm_clob_rtt.csv 2>/data/chain1.log &
echo "$!" >> /data/pids

# ---------- Chain 2: Goalserve inplay TCP SYN (every 60s -> 1440 samples/24h) ----------
# IP: 91.206.228.73 Sofia BG AS58294 CloudWall (小段 2026-05-28 实证)
# ICMP blocked by CloudWall; use nc TCP SYN instead
cat > /usr/local/bin/chain2_gs_inplay.sh << 'EOF'
#!/usr/bin/env bash
INPLAY_IP="91.206.228.73"
while true; do
  ts=$(date +%s)
  t_start=$(date +%s%N)
  nc -zw3 "${INPLAY_IP}" 80 > /dev/null 2>&1 && status=ok || status=err
  t_end=$(date +%s%N)
  rtt_ms=$(( (t_end - t_start) / 1000000 ))
  echo "${ts},${rtt_ms},${status}"
  sleep 60
done
EOF
chmod +x /usr/local/bin/chain2_gs_inplay.sh
nohup /usr/local/bin/chain2_gs_inplay.sh >> /data/gs_inplay_tcp_rtt.csv 2>/data/chain2.log &
echo "$!" >> /data/pids

# ---------- Chain 3: PM WSS handshake + 1st event (every 300s -> 288 samples/24h) ----------
# Endpoint: wss://sports-api.polymarket.com/ws (老李 laoli-polymarket-endpoint-matrix-v3.md)
# fallback: wss://ws-subscriptions-clob.polymarket.com (no-auth orderbook WS)
cat > /usr/local/bin/chain3_pm_wss.sh << 'EOF'
#!/usr/bin/env bash
PRIMARY_WSS="wss://sports-api.polymarket.com/ws"
FALLBACK_WSS="wss://ws-subscriptions-clob.polymarket.com"
while true; do
  ts=$(date +%s)
  t_start=$(date +%s%N)
  first_event=$(timeout 15 websocat "${PRIMARY_WSS}" --no-close -n1 2>/tmp/ws_err.txt \
    || timeout 15 websocat "${FALLBACK_WSS}" --no-close -n1 2>/tmp/ws_err.txt \
    || echo "")
  t_end=$(date +%s%N)
  elapsed_ms=$(( (t_end - t_start) / 1000000 ))
  payload_bytes=$(echo -n "${first_event}" | wc -c)
  endpoint_used=$(grep -q 'sports-api' /tmp/ws_err.txt && echo "primary" || echo "fallback")
  echo "${ts},${elapsed_ms},${payload_bytes},${endpoint_used}"
  sleep 300
done
EOF
chmod +x /usr/local/bin/chain3_pm_wss.sh
nohup /usr/local/bin/chain3_pm_wss.sh >> /data/pm_wss_first_event.csv 2>/data/chain3.log &
echo "$!" >> /data/pids

# ---------- Chain 4: Goalserve pregame HTTP RTT (every 300s -> 288 samples/24h) ----------
# IP: 69.64.69.90 Phoenix AZ AS18501 Codero (小段 2026-05-28 实证)
cat > /usr/local/bin/chain4_gs_pregame.sh << 'EOF'
#!/usr/bin/env bash
PREGAME_IP="69.64.69.90"
while true; do
  ts=$(date +%s)
  result=$(curl -s -o /dev/null \
    --noproxy '*' \
    --connect-timeout 5 \
    --max-time 15 \
    --resolve "www.goalserve.com:80:${PREGAME_IP}" \
    -w "%{time_connect},%{time_total}" \
    "http://www.goalserve.com/" 2>/dev/null || echo "err,err")
  echo "${ts},${result}"
  sleep 300
done
EOF
chmod +x /usr/local/bin/chain4_gs_pregame.sh
nohup /usr/local/bin/chain4_gs_pregame.sh >> /data/gs_pregame_rtt.csv 2>/data/chain4.log &
echo "$!" >> /data/pids

echo "[user-data] all 4 chains launched — PIDs: $(cat /data/pids | tr '\n' ' ')"
USERDATA
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
INSTANCE_IDS_FILE="/tmp/laowu-rtt-test-instance-ids.txt"
> "${INSTANCE_IDS_FILE}"

log "=== spin.sh START — regions: ${REGIONS[*]} ==="
log "SRE source IP: ${SRE_IP}"
[[ "${DRY_RUN}" == "--dry-run" ]] && log "*** DRY-RUN mode — no AWS calls will be made ***"

for REGION in "${REGIONS[@]}"; do
  log "--- Processing region: ${REGION} ---"

  # Resolve latest Ubuntu 24.04 LTS AMI
  if [[ "${DRY_RUN}" == "--dry-run" ]]; then
    AMI_ID="ami-dryrun-${REGION}"
  else
    AMI_ID=$(resolve_ami "${REGION}")
  fi
  log "[${REGION}] AMI: ${AMI_ID}"

  # Ensure security group
  if [[ "${DRY_RUN}" == "--dry-run" ]]; then
    SG_ID="sg-dryrun-${REGION}"
  else
    SG_ID=$(ensure_sg "${REGION}")
  fi
  log "[${REGION}] SG: ${SG_ID}"

  # Build user-data
  USER_DATA=$(build_user_data "${REGION}" | base64)

  # Spot instance run
  INSTANCE_ID=$(dry aws ec2 run-instances \
    --region "${REGION}" \
    --image-id "${AMI_ID}" \
    --instance-type "${INSTANCE_TYPE}" \
    --key-name "${KEY_NAME}" \
    --security-group-ids "${SG_ID}" \
    --instance-market-options "MarketType=spot,SpotOptions={SpotInstanceType=one-time,InstanceInterruptionBehavior=terminate}" \
    --user-data "${USER_DATA}" \
    --block-device-mappings '[{"DeviceName":"/dev/sda1","Ebs":{"VolumeSize":10,"VolumeType":"gp3","DeleteOnTermination":true}}]' \
    --tag-specifications \
      "ResourceType=instance,Tags=[{Key=Name,Value=laowu-rtt-test-${REGION}},{Key=owner,Value=${TAG_OWNER}},{Key=purpose,Value=${TAG_PURPOSE}},{Key=wave,Value=80}]" \
    --count 1 \
    --output json 2>/dev/null | jq -r '.Instances[0].InstanceId' || echo "i-dryrun")

  log "[${REGION}] Launched instance: ${INSTANCE_ID}"
  echo "${REGION} ${INSTANCE_ID}" >> "${INSTANCE_IDS_FILE}"

  # Wait for running state (skip in dry-run)
  if [[ "${DRY_RUN}" != "--dry-run" ]]; then
    log "[${REGION}] Waiting for instance running..."
    aws ec2 wait instance-running \
      --region "${REGION}" \
      --instance-ids "${INSTANCE_ID}" 2>/dev/null || true

    PUBLIC_IP=$(aws ec2 describe-instances \
      --region "${REGION}" \
      --instance-ids "${INSTANCE_ID}" \
      --query 'Reservations[0].Instances[0].PublicIpAddress' \
      --output text)
    log "[${REGION}] Instance running — public IP: ${PUBLIC_IP}"
    echo "${REGION} ${INSTANCE_ID} ${PUBLIC_IP}" >> "${INSTANCE_IDS_FILE}.ips"
  fi
done

log "=== spin.sh DONE ==="
log "Instance ID file: ${INSTANCE_IDS_FILE}"
log "Next step: wait ~5min for user-data to complete, then run measure.sh"
log "Teardown: run teardown.sh after 24h (to stop billing)"
