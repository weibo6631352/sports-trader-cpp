---
name: laowu-w10-w2-rtt-3region-results-v1
description: Frankfurt/Dublin/London 24h RTT 实测结果 — ADR-013 v2 final 决策输入
owner: 老吴 (linux-sre-devops, A-unit, #10)
last_review: 2026-05-29
status: PENDING — 等 24h 实测数据 (W10 W2 执行)
---

# 3-Region RTT 实测结果 v1

- **Owner:** 老吴 (linux-sre-devops, A-unit, #10)
- **Date:** 2026-05-29 (模板立; 实测数据 W10 W2 填入)
- **Status:** PENDING — AWS CLI 已就绪后 W10 W2 实跑 spin.sh → 24h → measure.sh --pull --analyze
- **触发:** ADR-013 v2 §5.1 (老郭, 2026-05-29 Draft)
- **实测脚本:**
  - `scripts/aws-rtt-test/spin.sh`
  - `scripts/aws-rtt-test/measure.sh`
  - `scripts/aws-rtt-test/teardown.sh`
- **决策输出:** 填入后 @老郭 review → ADR-013 v2 Draft → Accepted; @老雷 GM ack → 购买

---

## §1 实测执行状态

### W10 W2 执行前置检查

| 前置 | 状态 | 说明 |
|---|---|---|
| AWS CLI 安装 | PENDING | 本机无 AWS CLI — W10 W2 实跑时安装 (`brew install awscli`) |
| AWS credentials | PENDING | `aws configure` 或 `~/.aws/credentials` 配置 |
| SSH key 创建 | PENDING | `aws ec2 create-key-pair --key-name laowu-sre-key --region eu-central-1` |
| 预算 ack | ACK | GM 代理 ack ≤ $200/月, 实测 24h ≤ $1.50 在授权范围内 |
| 脚本 dry-run | DONE | `bash scripts/aws-rtt-test/spin.sh --dry-run` 在本地验证通过 |

### 实测执行步骤 (W10 W2)

```bash
# 1. 安装 AWS CLI (macOS)
brew install awscli

# 2. 配置 credentials (IAM user: laowu-sre, 权限: ec2:*, minimal)
aws configure
# AWS Access Key ID: <from secret manager>
# AWS Secret Access Key: <from secret manager>
# Default region: eu-central-1
# Default output format: json

# 3. 创建 SSH key pair (一次性)
aws ec2 create-key-pair \
  --region eu-central-1 \
  --key-name laowu-sre-key \
  --query 'KeyMaterial' --output text \
  > ~/.ssh/laowu-sre-key.pem
chmod 400 ~/.ssh/laowu-sre-key.pem
# 同一 key name 在 eu-west-1 / eu-west-2 也创建:
aws ec2 import-key-pair --region eu-west-1 \
  --key-name laowu-sre-key \
  --public-key-material fileb://<(ssh-keygen -y -f ~/.ssh/laowu-sre-key.pem)
aws ec2 import-key-pair --region eu-west-2 \
  --key-name laowu-sre-key \
  --public-key-material fileb://<(ssh-keygen -y -f ~/.ssh/laowu-sre-key.pem)

# 4. Spin 3 instances
bash scripts/aws-rtt-test/spin.sh
# 输出 instance IDs 到 /tmp/laowu-rtt-test-instance-ids.txt.ips

# 5. 等待 5min user-data 完成，检查 chain 状态
bash scripts/aws-rtt-test/measure.sh --status

# 6. 24h 后 pull + analyze
bash scripts/aws-rtt-test/measure.sh --pull --analyze \
  2>&1 | tee /tmp/laowu-rtt-analyze-$(date +%Y%m%d).txt

# 7. Teardown
bash scripts/aws-rtt-test/teardown.sh

# 8. 把 analyze 结果填入下面 §2 表格
```

---

## §2 实测结果表 (24h 后填入)

### Chain 1: PM CLOB TCP+TLS time_connect (ms)

| Region | P50 | P95 | P99 | mean | n | ADR-013 v2 估算 |
|---|---|---|---|---|---|---|
| eu-central-1 (Frankfurt) | TBD | TBD | TBD | TBD | TBD | ~15ms |
| eu-west-1 (Dublin) | TBD | TBD | TBD | TBD | TBD | ~1ms |
| eu-west-2 (London) | TBD | TBD | TBD | TBD | TBD | <1ms |

### Chain 2: Goalserve inplay TCP SYN RTT (ms, Sofia BG 91.206.228.73:80)

| Region | P50 | P95 | P99 | mean | n | ADR-013 v2 估算 |
|---|---|---|---|---|---|---|
| eu-central-1 (Frankfurt) | TBD | TBD | TBD | TBD | TBD | ~20ms |
| eu-west-1 (Dublin) | TBD | TBD | TBD | TBD | TBD | ~30ms |
| eu-west-2 (London) | TBD | TBD | TBD | TBD | TBD | ~25ms |

### Chain 3: PM WSS handshake + 1st event elapsed (ms)

| Region | P50 | P95 | P99 | mean | n | endpoint |
|---|---|---|---|---|---|---|
| eu-central-1 (Frankfurt) | TBD | TBD | TBD | TBD | TBD | TBD |
| eu-west-1 (Dublin) | TBD | TBD | TBD | TBD | TBD | TBD |
| eu-west-2 (London) | TBD | TBD | TBD | TBD | TBD | TBD |

### Chain 4: Goalserve pregame HTTP time_connect (ms, Phoenix AZ 69.64.69.90:80)

| Region | P50 | P95 | P99 | mean | n | ADR-013 v2 估算 |
|---|---|---|---|---|---|---|
| eu-central-1 (Frankfurt) | TBD | TBD | TBD | TBD | TBD | ~100ms |
| eu-west-1 (Dublin) | TBD | TBD | TBD | TBD | TBD | ~100ms |
| eu-west-2 (London) | TBD | TBD | TBD | TBD | TBD | ~100ms |

---

## §3 决策判断 (数据填入后由老郭完成)

ADR-013 v2 §4.2 决策触发条件:

**条件 A — Frankfurt confirmed (ADR-013 v2 → Accepted):**
- [ ] Frankfurt Chain 2 P95 ≤ 25ms
- [ ] Frankfurt Chain 2 P95 ≤ Dublin Chain 2 P95 (inplay 优势保留)
- [ ] Frankfurt Chain 1 P95 ≤ 25ms (PM CLOB 可接受)

**条件 B — Refute Frankfurt:**
- [ ] Frankfurt Chain 1 P95 > 25ms (估算严重偏低)
- [ ] Frankfurt Chain 2 P95 > 30ms AND Dublin Chain 2 ≤ 25ms (inplay 优势消失)

**条件 C — 重大架构重评:**
- [ ] 任意 region Chain 1 P95 > 50ms (Polymarket origin 非 eu-west-2?)
- [ ] 任意 region Chain 2 P95 > 50ms (Sofia 节点迁移?)

**@老郭 决策结论:** TBD (数据填入后)

**@老雷 GM ack:** TBD → 购买 Frankfurt production instance (t3.medium, ~$45/月)

---

## §4 预算确认

| 项目 | 预算 | 状态 |
|---|---|---|
| 实测 24h spin (t3.micro × 3) | ≤ $1.50 | GM 代理 ack |
| production t3.medium Frankfurt | ~$45/月 | GM 代理 ack (≤ $200/月) |
| obs 节点 (小郑 未来) | TBD | 待评估 |

**GM 代理 ack 原话 (ADR-013 v2 §8):** "预算 ≤ $200/月 我代理 ack"

---

**老吴 (SRE, A 单元), 2026-05-29**
