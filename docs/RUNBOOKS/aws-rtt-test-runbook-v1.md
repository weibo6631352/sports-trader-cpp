# AWS 3-Region RTT Test Runbook v1

- **owner:** 老吴 (SRE, A-unit, #10)
- **last_review:** 2026-05-29
- **status:** Ready — pending GM (老雷) budget ack before spin
- **trigger:** Wave 80, ADR-013 v2 Draft前置实测
- **ADR cite:** ADR-013 v2 Draft (`docs/ADR/2026-06-W4-adr-013-v2-deployment-location.md`)
- **plan cite:** `docs/RESEARCH/laowu-w9-w1-aws-region-rtt-test-plan-v1.md`
- **scripts:** `scripts/aws-rtt-test/spin.sh`, `measure.sh`, `teardown.sh`

---

## §1 目的

ADR-013 v2 推荐 eu-central-1 Frankfurt 为主部署节点, 但 RTT 估算基于理论值:
- Frankfurt→London (PM CLOB) ~15ms (估算)
- Frankfurt→Sofia (Goalserve inplay) ~20ms (估算)

本实测校准这两个数字. 实测数据是 ADR-013 v2 Draft → Accepted 的唯一前置条件.
实测成本 < $0.40 (spot), on-demand 上限 < $1.50.
source: https://aws.amazon.com/ec2/pricing/on-demand/ @ 2026-05-29

---

## §2 前置条件

| 前置 | 说明 | 状态 |
|---|---|---|
| AWS CLI configured | `aws sts get-caller-identity` 返回 account ID | 需确认 |
| SSH key pair | `laowu-sre-key` 在 3 个 region 已创建, 私钥在 `~/.ssh/laowu-sre-key.pem` | 需确认 |
| `gh` CLI | `gh auth status` 通过 (PR 创建用) | 需确认 |
| GM (老雷) budget ack | 实测成本 <$2 — plan §3.1 列明 | **P0 ack 前不执行** |
| 老李确认 WSS endpoint | `wss://sports-api.polymarket.com/ws` 鉴权状态 (measure.sh chain 3 依赖) | 询问中 |

---

## §3 启动 (T+0)

```bash
cd /path/to/sports-trader-cpp

# 1. 环境变量 (如需覆盖默认值)
export AWS_KEY_NAME="laowu-sre-key"
export AWS_SSH_KEY="~/.ssh/laowu-sre-key.pem"

# 2. 预演 (推荐先跑, 无 AWS 操作)
bash scripts/aws-rtt-test/spin.sh --dry-run

# 3. 真正 spin (GM ack 后)
bash scripts/aws-rtt-test/spin.sh

# 输出示例:
# [2026-05-29T...Z] [eu-central-1] Launched instance: i-0abc123...
# [2026-05-29T...Z] [eu-west-1]    Launched instance: i-0def456...
# [2026-05-29T...Z] [eu-west-2]    Launched instance: i-0ghi789...
# Instance ID file: /tmp/laowu-rtt-test-instance-ids.txt
```

spin.sh 内部执行:
1. 查询 3 region 各自最新 Ubuntu 24.04 LTS AMI
2. 创建 security group (SSH ingress 仅限 SRE IP, egress 全开)
3. 启动 t3.micro spot instance + user-data (自动安装 netcat/curl/websocat, 启动 4 条测量 loop)
4. 等待 running 状态, 输出 public IP

---

## §4 监控 (T+5min ~ T+24h)

```bash
# T+5min: 确认 user-data 执行完毕, 4 条链路 PID 存活
bash scripts/aws-rtt-test/measure.sh --status

# 示例正常输出:
# [eu-central-1] PID 1234: running
# [eu-central-1] pm_clob_rtt: 5 rows
# [eu-central-1] gs_inplay_tcp_rtt: 5 rows
# ...

# T+12h: 中期 check (可选)
bash scripts/aws-rtt-test/measure.sh --status

# 异常处理: 某 chain PID dead -> 登录 instance 手动重启 (见 §7)
```

4 条测量链路说明:

| Chain | 频率 | 24h 样本量 | 目标端点 | CSV |
|---|---|---|---|---|
| 1 PM CLOB RTT | 60s | 1440 | clob.polymarket.com (CF anycast) | `pm_clob_rtt.csv` |
| 2 GS inplay TCP SYN | 60s | 1440 | 91.206.228.73:80 (Sofia BG) | `gs_inplay_tcp_rtt.csv` |
| 3 PM WSS + 1st event | 300s | 288 | wss://sports-api.polymarket.com/ws | `pm_wss_first_event.csv` |
| 4 GS pregame HTTP | 300s | 288 | www.goalserve.com (Phoenix AZ) | `gs_pregame_rtt.csv` |

---

## §5 数据收集 (T+24h)

```bash
# Step 1: pull 12 CSVs (4 chains * 3 regions)
bash scripts/aws-rtt-test/measure.sh --pull
# CSVs land in /tmp/laowu-rtt-results/<region>/

# Step 2: analyze — P50/P95/P99 per chain per region
bash scripts/aws-rtt-test/measure.sh --analyze
# 输出直接可填入 laowu-w9-rtt-3region-results-v1.md 表格

# Step 3: teardown (立即终止, 停止计费)
bash scripts/aws-rtt-test/teardown.sh
```

---

## §6 分析与 ADR-013 v2 决策

### §6.1 填表

将 measure.sh --analyze 输出填入:
`docs/RESEARCH/laowu-w9-rtt-3region-results-v1.md`

格式参考 (按 ADR-013 v2 §3 矩阵):

```
| Region        | Chain 1 PM CLOB P95 | Chain 2 GS inplay P95 | Chain 3 WSS P50 | Chain 4 GS pregame P95 |
|---|---|---|---|---|
| eu-central-1  | __ms                | __ms                   | __ms            | __ms                   |
| eu-west-1     | __ms                | __ms                   | __ms            | __ms                   |
| eu-west-2     | __ms                | __ms                   | __ms            | __ms                   |
```

### §6.2 决策触发 (填完后 @老郭)

按 ADR-013 v2 §5.2 / plan §4.2:

**条件 A — 实测支持 Frankfurt:**
- Frankfurt chain1 P95 <= 25ms AND
- Frankfurt chain2 P95 <= 25ms AND
- Frankfurt chain2 P95 <= Dublin chain2 P95

动作: @老郭 confirm → ADR-013 v2 status Draft→Accepted → @老雷 GM ack → 购买 Frankfurt production instance

**条件 B — 实测 refute Frankfurt:**
- Frankfurt chain1 P95 > 25ms OR
- (Frankfurt chain2 P95 > 30ms AND Dublin chain2 P95 <= 25ms)

动作: @老郭 重评 London/Dublin, W9 W2 前不购买任何 production instance

**条件 C — 需紧急架构重评:**
- 任意 region chain1 P95 > 50ms 或 chain2 P95 > 50ms

动作: @老郭 紧急架构评审 (P0), @小段 Goalserve Sofia 节点 IP audit, ADR-013 v2 延期

### §6.3 WSS 告警阈值

| Metric | 期望 | 条件 C 触发 |
|---|---|---|
| Chain 3 WSS P50 (Frankfurt) | <= 30ms | > 100ms |
| Chain 3 1st event P50 | <= 100ms | > 500ms |

---

## §7 异常处理

### Chain PID died (user-data 完成后某 loop 挂了)

```bash
# SSH 进实例
ssh -i ~/.ssh/laowu-sre-key.pem ubuntu@<PUBLIC_IP>

# 查看哪条 chain 死了
cat /data/pids
ps aux | grep chain

# 重启死掉的 chain (以 chain2 为例)
nohup /usr/local/bin/chain2_gs_inplay.sh >> /data/gs_inplay_tcp_rtt.csv 2>/data/chain2.log &
echo "$!" >> /data/pids
```

### websocat not found

```bash
# 手动安装 (在 instance 上)
WEBSOCAT_VER="1.13.0"
sudo curl -fsSL \
  "https://github.com/vi/websocat/releases/download/v${WEBSOCAT_VER}/websocat.x86_64-unknown-linux-musl" \
  -o /usr/local/bin/websocat
sudo chmod +x /usr/local/bin/websocat
```

### WSS endpoint 需鉴权 (chain 3 报错)

chain3 脚本内含 fallback: 若 `wss://sports-api.polymarket.com/ws` 失败自动切
`wss://ws-subscriptions-clob.polymarket.com` (无鉴权 orderbook WS).
观察 /data/pm_wss_first_event.csv 第 4 列 (primary/fallback) 确认.

若两者都失败: @老李 提供 test token 或替换 endpoint.

### Spot instance interrupted

t3.micro spot 被抢占时 instance 会在 2min 内 terminate.
检查: `aws ec2 describe-instances --region <r> --instance-ids <id> --query 'Reservations[0].Instances[0].StateReason.Message'`

若中途抢占: 已有 CSV 数据不丢失 (已落盘), 但样本不足 1440.
处理: 在同 region 重新 spin 一个 on-demand instance 补跑剩余时段.

### scp 连接超时

检查 SG 是否 SSH 白名单包含当前出口 IP:
```bash
current_ip=$(curl -s https://checkip.amazonaws.com/)
aws ec2 describe-security-groups --region eu-central-1 \
  --filters "Name=group-name,Values=laowu-rtt-test-sg" \
  --query 'SecurityGroups[0].IpPermissions'
```
若当前 IP 不在白名单: 联系 @老周 或用跳板机.

---

## §8 Teardown 确认

teardown.sh 执行后确认:

```bash
# 确认 3 region 均已 terminated
for region in eu-central-1 eu-west-1 eu-west-2; do
  echo "=== ${region} ==="
  aws ec2 describe-instances \
    --region "${region}" \
    --filters "Name=tag:owner,Values=laowu" "Name=tag:purpose,Values=rtt-test-24h" \
    --query 'Reservations[*].Instances[*].[InstanceId,State.Name]' \
    --output table
done
```

所有 instance State = terminated -> 计费停止 -> 总费用 < $0.40 (spot 预期) 或 < $1.50 (on-demand 上限).

---

## §9 后续动作 (ADR-029 流程)

完成本 runbook 后, 执行 ADR-029 §3.1 完整 8 步:

```bash
# Step 1: pwd verify
pwd   # 预期: .../sports-trader-cpp/.claude/worktrees/agent-<id>

# Step 2-3: commit
git add -A
git commit -m "feat(sre): aws-rtt-test scripts + runbook (老吴 Wave 80)"

# Step 4-5: fetch + merge
git fetch origin
git merge origin/main --no-edit

# Step 6: push
git push origin worktree-agent-a5b3d6c87d6927f4c

# Step 7: gh pr create (见 PR body 模板)
# Step 8: 回汇 GM commit hash + PR URL + ctest 状态
```

---

## §10 联络矩阵

| 事项 | 联络人 | 优先级 |
|---|---|---|
| ADR-013 v2 实测数据 review + final 推荐 | @老郭 (F 顾问团, ADR-013 v2 owner) | P0 |
| GM budget ack (实测 <$2, production <$90/月) | @老雷 (GM) | P0 ack 前不执行 |
| WSS endpoint 鉴权状态 | @老李 (A 单元, PM API spec owner) | P1 spin 前确认 |
| Goalserve Sofia IP 稳定性 audit | @小段 (D 单元, Goalserve 专精) | P1 全程 |
| paper runtime W11 Frankfurt 排期 | @老周 (A 单元主管) | P1 |

---

**老吴 (SRE, A-unit, #10), 2026-05-29**
