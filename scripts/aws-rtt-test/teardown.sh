#!/usr/bin/env bash
# teardown.sh — terminate all 3 RTT test instances across 3 regions
# owner: 老吴 (SRE, A-unit, #10)
# date: 2026-05-29
# usage:
#   bash teardown.sh              # terminate all instances tagged owner=laowu purpose=rtt-test-24h
#   bash teardown.sh --dry-run    # print what would be terminated, no action
#
# IMPORTANT: run only after measure.sh --pull has completed successfully.
# Once terminated, EBS volumes are deleted (DeleteOnTermination=true in spin.sh).

set -euo pipefail

DRY_RUN="${1:-}"
REGIONS=(eu-central-1 eu-west-1 eu-west-2)
TAG_OWNER="laowu"
TAG_PURPOSE="rtt-test-24h"

log() { echo "[$(date -u +%Y-%m-%dT%H:%M:%SZ)] $*"; }

terminate_region() {
  local region="$1"

  # Find instance IDs by tags
  local instance_ids
  instance_ids=$(aws ec2 describe-instances \
    --region "${region}" \
    --filters \
      "Name=tag:owner,Values=${TAG_OWNER}" \
      "Name=tag:purpose,Values=${TAG_PURPOSE}" \
      "Name=instance-state-name,Values=running,stopped,pending" \
    --query 'Reservations[*].Instances[*].InstanceId' \
    --output text 2>/dev/null | tr '\t' ' ' | tr '\n' ' ')

  if [[ -z "${instance_ids// /}" ]]; then
    log "[${region}] No active RTT test instances found."
    return
  fi

  log "[${region}] Found instances: ${instance_ids}"

  if [[ "${DRY_RUN}" == "--dry-run" ]]; then
    log "[${region}] DRY-RUN: would terminate ${instance_ids}"
    return
  fi

  aws ec2 terminate-instances \
    --region "${region}" \
    --instance-ids ${instance_ids} \
    --query 'TerminatingInstances[*].[InstanceId,CurrentState.Name]' \
    --output table

  log "[${region}] Terminate request sent."
}

clean_sg() {
  local region="$1"
  # Only delete SG if no instances reference it
  local sg_id
  sg_id=$(aws ec2 describe-security-groups \
    --region "${region}" \
    --filters "Name=group-name,Values=laowu-rtt-test-sg" \
    --query 'SecurityGroups[0].GroupId' \
    --output text 2>/dev/null || echo "None")

  [[ "${sg_id}" == "None" || -z "${sg_id}" ]] && return

  if [[ "${DRY_RUN}" == "--dry-run" ]]; then
    log "[${region}] DRY-RUN: would delete SG ${sg_id}"
    return
  fi

  # Give terminate a moment before SG deletion
  sleep 5
  aws ec2 delete-security-group \
    --region "${region}" \
    --group-id "${sg_id}" 2>/dev/null \
    && log "[${region}] SG ${sg_id} deleted." \
    || log "[${region}] WARN: SG delete failed (may still have dependencies — retry in 60s)"
}

# ---------------------------------------------------------------------------
# Preflight: confirm CSVs pulled
# ---------------------------------------------------------------------------
PULL_DIR="/tmp/laowu-rtt-results"
csv_count=$(find "${PULL_DIR}" -name "*.csv" 2>/dev/null | wc -l || echo 0)
if [[ "${csv_count}" -lt 12 && "${DRY_RUN}" != "--dry-run" ]]; then
  log "WARNING: only ${csv_count}/12 CSVs found in ${PULL_DIR}"
  log "Expected 4 CSVs * 3 regions = 12 total."
  read -rp "CSVs may not be fully pulled. Continue teardown? [y/N] " confirm
  [[ "${confirm}" =~ ^[Yy]$ ]] || { log "Aborted."; exit 1; }
fi

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
[[ "${DRY_RUN}" == "--dry-run" ]] && log "*** DRY-RUN mode ***"
log "=== teardown.sh START ==="

for region in "${REGIONS[@]}"; do
  terminate_region "${region}"
done

# Wait for instances to reach terminated state before deleting SGs
if [[ "${DRY_RUN}" != "--dry-run" ]]; then
  log "Waiting 30s for termination to propagate before SG cleanup..."
  sleep 30
fi

for region in "${REGIONS[@]}"; do
  clean_sg "${region}"
done

# Clean up local instance-id tracking files
if [[ "${DRY_RUN}" != "--dry-run" ]]; then
  rm -f /tmp/laowu-rtt-test-instance-ids.txt \
         /tmp/laowu-rtt-test-instance-ids.txt.ips
  log "Local instance ID tracking files removed."
fi

log "=== teardown.sh DONE ==="
log "All 3 instances terminated. Billing stopped."
log "CSVs are in: ${PULL_DIR}/"
log "Next: bash measure.sh --analyze  (if not already done)"
