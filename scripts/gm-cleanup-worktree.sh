#!/bin/bash
# scripts/gm-cleanup-worktree.sh — GM 一键清理 merged 的 worktree
#
# 老板 2026-05-29 verbatim "worktree和分支是如何自动清理的" 触发
# 用法: scripts/gm-cleanup-worktree.sh [agent-id]
#       agent-id 不传 → 自动扫所有 worktree, 对应 branch 已 merged 的清理

set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

if [ "${1:-}" = "" ]; then
    # 自动扫
    git worktree list --porcelain | grep '^worktree' | awk '{print $2}' | grep "worktree-agent-" | while read wt_path; do
        agent_id="${wt_path##*/agent-}"
        branch="worktree-agent-$agent_id"
        # 检查 branch 是否已 merged 进 main
        if git branch --merged main --remote | grep -q "$branch"; then
            echo "Cleanup merged worktree: $agent_id"
            git worktree remove -f -f "$wt_path" 2>&1 | tail -1 || true
            git branch -D "$branch" 2>&1 | tail -1 || true
            gh api -X DELETE "repos/$(gh repo view --json nameWithOwner -q .nameWithOwner)/git/refs/heads/$branch" 2>&1 | tail -1 || true
        else
            echo "SKIP (not merged): $branch"
        fi
    done
else
    agent_id="$1"
    git worktree remove -f -f ".claude/worktrees/agent-$agent_id" 2>&1 | tail -1 || true
    git branch -D "worktree-agent-$agent_id" 2>&1 | tail -1 || true
    gh api -X DELETE "repos/$(gh repo view --json nameWithOwner -q .nameWithOwner)/git/refs/heads/worktree-agent-$agent_id" 2>&1 | tail -1 || true
fi

echo "✅ Cleanup done"
