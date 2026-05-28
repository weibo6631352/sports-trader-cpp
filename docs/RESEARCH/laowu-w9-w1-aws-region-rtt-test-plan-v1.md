# AWS 3 Region RTT 实测 Plan v1

- **Owner:** 老吴 (linux-sre-devops, #10, A 单元 SRE)
- **Date:** 2026-05-29
- **Last review:** 2026-05-29
- **Status:** Draft — 待老雷 GM ack 预算后执行
- **触发:** Wave 49 P0 — ADR-013 v2 final 前置实测 (老郭 2026-05-29 评审草案)
- **输入文档:**
  - `docs/ADR/2026-06-W4-adr-013-v2-deployment-location.md` (老郭 Draft)
  - `docs/RESEARCH/laowu-w8-polymarket-origin-verification-v1.md` (老吴 W8 W2)
  - `docs/RESEARCH/xiaoduan-w8-goalserve-inplay-node-verification-v1.md` (小段 W8 W2)
- **AWS pricing 来源:**
  - https://aws.amazon.com/ec2/pricing/on-demand/ @ 2026-05-29
  - https://aws.amazon.com/about-aws/global-infrastructure/ @ 2026-05-29

---

## §1 实测方案

### §1.1 目标

W9 W1 spin 3 个 AWS t3.micro (spot) 临时 instance, 每 region 跑 24h 连续 RTT 测量, 实测校准 ADR-013 v2 §3 估算数字. 实测数据是 ADR-013 v2 status Draft → Accepted 的唯一前置条件.

### §1.2 Instance 配置

| Region | AZ | Instance | OS | 用途 |
|---|---|---|---|---|
| eu-central-1 (Frankfurt) | eu-central-1a | t3.micro spot | Ubuntu 24.04 LTS | 24h RTT 测量 |
| eu-west-1 (Dublin) | eu-west-1a | t3.micro spot | Ubuntu 24.04 LTS | 24h RTT 测量 |
| eu-west-2 (London) | eu-west-2a | t3.micro spot | Ubuntu 24.04 LTS | 24h RTT 测量 |

**Spot instance 原因:** 24h 一次性实测, 非长跑服务. eu-central-1 t3.micro spot 约 $0.003-0.004/h (on-demand $0.0116/h 的 70% 折扣). 成本可控, 实测后立即 terminate.

**Security group:** ingress 仅 SSH (22/tcp, 来自 SRE 跳板 IP), egress 全开 (用于 outbound RTT 测量).

### §1.3 4 条测量链路

每 region 跑以下 4 条链路, 24h 连续, 结果落 CSV:

**链路 1 — PM CLOB TCP+TLS RTT (每 60s 一次)**

```bash
#!/bin/bash
# pm_clob_rtt.sh — 每 60s 采样一次, 输出 epoch,time_connect_ms,time_total_ms
while true; do
  ts=$(date +%s)
  result=$(curl -s -o /dev/null \
    --noproxy '*' \
    --connect-timeout 5 \
    --max-time 10 \
    -w "%{time_connect},%{time_total}" \
    "https://clob.polymarket.com/")
  echo "${ts},${result}" >> /data/pm_clob_rtt.csv
  sleep 60
done
```

备注: CF anycast 下 time_connect 反映 TCP+TLS to CF 最近 PoP, time_total 反映完整请求. 关键是两者 P50/P95, 用于判断 CF PoP 与 origin 的组合延迟.

**链路 2 — Goalserve inplay TCP SYN RTT (每 60s 一次)**

```bash
#!/bin/bash
# gs_inplay_rtt.sh — TCP SYN to 91.206.228.73:80 (Sofia BG, AS58294 CloudWall)
# ICMP ping blocked, 改用 nc TCP SYN
INPLAY_IP="91.206.228.73"
while true; do
  ts=$(date +%s)
  t_start=$(date +%s%N)
  nc -zw3 "${INPLAY_IP}" 80 2>/dev/null
  t_end=$(date +%s%N)
  rtt_ms=$(( (t_end - t_start) / 1000000 ))
  echo "${ts},${rtt_ms}" >> /data/gs_inplay_tcp_rtt.csv
  sleep 60
done
```

**链路 3 — Polymarket WSS handshake + 第 1 event 延迟 (每 300s 一次)**

```bash
#!/bin/bash
# pm_wss_e2e.sh — WSS connect to sports-api/ws, 记录 handshake_ms + first_event_ms
# 使用 websocat (brew install websocat / apt install websocat)
while true; do
  ts=$(date +%s)
  t_hs_start=$(date +%s%N)
  # 连接 WSS, 收第 1 条消息后退出
  first_event=$(timeout 10 websocat \
    "wss://sports-api.polymarket.com/ws" \
    --no-close \
    -n1 2>/tmp/ws_err.txt)
  t_first=$(date +%s%N)
  hs_ms=$(( (t_first - t_hs_start) / 1000000 ))
  echo "${ts},${hs_ms},$(echo "${first_event}" | wc -c)" >> /data/pm_wss_first_event.csv
  sleep 300
done
```

备注: WSS endpoint 以 `laoli-polymarket-endpoint-matrix-v3.md` 为准. 若 sports-api/ws 需鉴权 token, 改用无鉴权 orderbook WS (记录在老李 v3 端点矩阵). 具体 endpoint 启动前向老李确认.

**链路 4 — Goalserve pregame HTTP RTT (每 300s 一次)**

```bash
#!/bin/bash
# gs_pregame_rtt.sh — HTTP GET www.goalserve.com (Phoenix AZ, AS18501 Codero)
# 30s 周期增量拉取, 非延迟敏感, 采样 5min/次 足够
PREGAME_IP="69.64.69.90"
while true; do
  ts=$(date +%s)
  result=$(curl -s -o /dev/null \
    --noproxy '*' \
    --connect-timeout 5 \
    --max-time 15 \
    --resolve "www.goalserve.com:80:${PREGAME_IP}" \
    -w "%{time_connect},%{time_total}" \
    "http://www.goalserve.com/")
  echo "${ts},${result}" >> /data/gs_pregame_rtt.csv
  sleep 300
done
```

### §1.4 数据收集与输出

- 4 个 CSV 文件落 `/data/` (实例本地), 24h 后 `scp` 拉回
- 输出格式: `epoch_ts, rtt_ms` (链路 1/2/4) 或 `epoch_ts, handshake_ms, payload_bytes` (链路 3)
- 统计: 使用 `awk` / `datamash` 算 P50 / P95 / P99 + std
- 24h 样本量: 链路 1/2 各 1440 点; 链路 3/4 各 288 点 — 统计显著
- 结果落 `docs/RESEARCH/laowu-w9-rtt-3region-results-v1.md`

### §1.5 Spin-up 脚本 (AWS CLI)

```bash
#!/bin/bash
# spin_3_instances.sh
# 前置: aws cli configured, ~/.aws/credentials OK
# AMI: Ubuntu 24.04 LTS (各 region 不同 AMI-ID, 启动前 aws ec2 describe-images 查)

REGIONS=("eu-central-1" "eu-west-1" "eu-west-2")
INSTANCE_TYPE="t3.micro"
KEY_NAME="laowu-sre-key"  # 提前创建, 或用已有 key
SG_NAME="laowu-rtt-test-sg"

for REGION in "${REGIONS[@]}"; do
  echo "Spinning up ${INSTANCE_TYPE} in ${REGION}..."
  aws ec2 run-instances \
    --region "${REGION}" \
    --image-id "$(aws ec2 describe-images \
      --region "${REGION}" \
      --owners 099720109477 \
      --filters "Name=name,Values=ubuntu/images/hvm-ssd/ubuntu-noble-24.04-amd64*" \
      --query 'sort_by(Images, &CreationDate)[-1].ImageId' \
      --output text)" \
    --instance-type "${INSTANCE_TYPE}" \
    --key-name "${KEY_NAME}" \
    --instance-market-options "MarketType=spot" \
    --tag-specifications "ResourceType=instance,Tags=[{Key=Name,Value=laowu-rtt-test-${REGION}},{Key=owner,Value=laowu},{Key=purpose,Value=rtt-test-24h}]" \
    --count 1 \
    --output json | jq -r '.Instances[0].InstanceId'
done
```

terminate 脚本 (24h 后执行):
```bash
# terminate_test_instances.sh
aws ec2 describe-instances \
  --filter "Name=tag:owner,Values=laowu" "Name=tag:purpose,Values=rtt-test-24h" \
  --query 'Reservations[*].Instances[*].[InstanceId,Placement.AvailabilityZone]' \
  --output text | while read iid az; do
    region="${az:0:-1}"  # 去掉最后一个字符 (AZ suffix)
    aws ec2 terminate-instances --region "${region}" --instance-ids "${iid}"
done
```

---

## §2 AWS 官网 pricing 数据 (@ 2026-05-29)

**来源: https://aws.amazon.com/ec2/pricing/on-demand/ @ 2026-05-29**

| Region | t3.micro On-Demand | t3.medium On-Demand | c5.large On-Demand |
|---|---|---|---|
| eu-central-1 (Frankfurt) | $0.0116/h | $0.0464/h | $0.096/h |
| eu-west-1 (Dublin) | $0.0116/h | $0.0464/h | $0.085/h |
| eu-west-2 (London) | $0.0131/h | $0.0525/h | $0.096/h |

注: t3.micro spot 通常折扣 60-80%, 实际约 $0.003-0.005/h. 上表为 on-demand 价格, 用于上限估算.

**来源: https://aws.amazon.com/about-aws/global-infrastructure/ @ 2026-05-29**

| Region | 启动年份 | AZ 数量 | 备注 |
|---|---|---|---|
| eu-central-1 (Frankfurt) | 2014 | 3 (a/b/c) | AWS 欧洲最大 region, 基础设施最成熟 |
| eu-west-1 (Dublin) | 2007 | 3 (a/b/c) | AWS 欧洲首个 region |
| eu-west-2 (London) | 2016 | 3 (a/b/c) | 距 Polymarket origin 最近 |

---

## §3 预算估算

### §3.1 实测阶段 (W9 W1, 24h)

| 项目 | 数量 | 单价 | 小计 |
|---|---|---|---|
| t3.micro spot × eu-central-1 | 24h | ~$0.004/h | $0.096 |
| t3.micro spot × eu-west-1 | 24h | ~$0.004/h | $0.096 |
| t3.micro spot × eu-west-2 | 24h | ~$0.005/h (London 稍贵) | $0.120 |
| EBS gp3 10GB × 3 instance | 24h | ~$0.0027/h × 3 | $0.065 |
| 数据传输 (出站) ~100MB × 3 | 一次 | $0.09/GB | $0.027 |
| **合计 (实测)** | | | **~$0.40** |

保守上限按 on-demand 价格计算:
- t3.micro on-demand × 3 region × 24h × $0.013/h = **$0.94**
- 加 EBS + 数据传输: **≤ $1.50 实测上限**

**GM 代理 ack 范围:** 实测成本 < $2, 直接 ack, 无需 GM 审批.

### §3.2 Production (W9 W2+ 确认 Frankfurt 后)

基于 ADR-013 v2 推荐 eu-central-1 Frankfurt:

| 场景 | Instance | 月费 (on-demand) | 备注 |
|---|---|---|---|
| paper runtime (W11, Sprint-3) | t3.medium × 1 | $0.0464 × 24 × 30 = **$33.4/月** | paper engine, 无高 CPU 需求 |
| live runtime (M4.5+) 最低配 | t3.medium × 1 | $33.4/月 | 单 AZ, MVP 阶段 |
| live runtime (M4.5+) 推荐配 | c5.large × 1 | $0.096 × 24 × 30 = **$69.1/月** | 更好单核性能, 热路径友好 |
| live runtime 含 EBS + 数据传输 | — | +$10-20/月 | 100GB gp3 + 出站流量 |
| **paper runtime 月总成本** | | **~$45/月** | t3.medium + EBS + 流量 |
| **live runtime 月总成本** | | **$60-90/月** | c5.large + EBS + 流量 |

**GM 代理 ack 范围 (已 ack ≤ $200/月):**
- paper runtime ~$45/月: 直接 ack
- live runtime ~$60-90/月: 直接 ack
- 总月度预算 ≤ $90/月, 远低于 $200 上限. 我直接 deploy, 不需要报告.

**注意:** 上述均为单 region 单 instance 估算. M5+ multi-region (ADR-013 v2 §6 联动) 另行评估.

---

## §4 实测 metric 期望与判断标准

### §4.1 预期 RTT 矩阵 (老郭 ADR-013 v2 §3 估算, 待实测校准)

| 部署 | PM CLOB (eu-west-2 London) | Goalserve inplay (Sofia BG) | Goalserve pregame (Phoenix AZ) | 综合 |
|---|---|---|---|---|
| eu-central-1 (Frankfurt) | ~15ms (估算) | ~20ms (估算) | ~100ms (估算) | 最优? |
| eu-west-1 (Dublin) | ~1ms (老板 verbatim) | ~30ms (估算) | ~100ms (估算) | 优 |
| eu-west-2 (London) | <1ms (同 region) | ~25ms (估算) | ~100ms (估算) | 优 |

### §4.2 决策触发条件

**条件 A — 实测支持 Frankfurt (ADR-013 v2 维持):**

- Frankfurt PM CLOB P95 ≤ 25ms AND
- Frankfurt inplay P95 ≤ 25ms AND
- Frankfurt inplay P95 ≤ Dublin inplay P95 (证明 Frankfurt inplay 优势)

结论: ADR-013 v2 status → Accepted, 购买 eu-central-1 Frankfurt production instance.

**条件 B — 实测 refute Frankfurt, London/Dublin 更优:**

- Frankfurt PM CLOB 实测 > 25ms (估算严重偏低) OR
- Frankfurt inplay 实测 > 30ms (且 Dublin inplay ≤ 25ms, inplay 优势消失)

结论: 老郭重评, London (C) 或 Dublin (B) 替代. W9 W2 前不购买任何 production instance.

**条件 C — 需要重大架构重评:**

- 任意 region 的 PM CLOB P95 > 50ms (暗示 Polymarket origin 非 eu-west-2)
- 或 inplay 在任意欧洲 region P95 > 50ms (暗示 Sofia BG 节点已迁移)

结论: @老郭 紧急架构评审, @小段 Goalserve Sofia 节点 IP audit, ADR-013 v2 延期.

### §4.3 WSS handshake metric

| Metric | 期望 | 告警阈值 |
|---|---|---|
| WSS handshake P50 (Frankfurt) | ≤ 30ms | > 100ms 触发条件 C |
| first event 延迟 P50 (Frankfurt) | ≤ 100ms | > 500ms 触发条件 C |

---

## §5 Timeline

| 时间 | 执行人 | 任务 | 输出 |
|---|---|---|---|
| W9 W1 Mon AM | 老吴 | spin 3 region t3.micro spot, 配置 security group, 部署 4 条测量脚本 | 3 instance running, 脚本跑起来 |
| W9 W1 Mon PM ~ Tue PM | 自动 | 24h 连续 RTT 测量 (1440 点 / 链路 1/2, 288 点 / 链路 3/4) | 4 CSV × 3 region = 12 文件 |
| W9 W1 Tue PM | 老吴 | scp 拉取 12 CSV, awk 统计 P50/P95/P99, 填入结果表 | `laowu-w9-rtt-3region-results-v1.md` |
| W9 W1 Wed AM | 老吴 + 老郭 | 老吴 review 实测数字, 对比本文 §4 预期. @老郭 confirm/refute ADR-013 v2 推荐 | 老郭 ack or 重评 |
| W9 W1 Wed PM | 老吴 | terminate 3 test instance | AWS 费用停止计费 |
| W9 W1 Thu | 老吴 | 若老郭 ack Frankfurt: 购买 eu-central-1 production instance (t3.medium 或 c5.large) | production instance running |
| W9 W1 Fri | 老吴 | production instance 基础配置: OS hardening, ssh key, NTP sync, monitoring agent | instance ready for paper runtime |
| W9 W2 Mon | 老吴 + 老雷 | ADR-013 v2 final: 老郭修订 status → Accepted, 老雷 GM ack | ADR-013 v2 Accepted |
| W9 W2 | 老吴 + 小蒋 | spec Frankfurt paper runtime 部署细节 (联动 ADR-013 v2 §6) | paper runtime deploy spec |
| W11 | 老吴 | paper runtime on Frankfurt | Sprint-3 milestone |

---

## §6 不耻下问

| 接收方 | 问题 / 任务 | 截止 | 优先级 |
|---|---|---|---|
| **@老郭 (F 顾问团, ADR-013 v2 owner)** | W9 W1 Wed 实测数字回来后: review 实测 vs §4 预期差异, 给出 ADR-013 v2 final 推荐 (Frankfurt confirm 或重评). 老吴直接执行你的结论. | W9 W1 Wed EOD | P0 |
| **@老雷 (GM)** | GM 代理预算 ack 确认: 实测成本 ≤ $2 直接执行; production 单 instance $60-90/月 已在 GM 代理 ack 范围 (≤ $200/月). 本文 spec 数字供老雷 review. | W9 W1 Mon (实测启动前) | P0 ack |
| **@老周 (A 单元主管)** | 确认 paper runtime W11 启动地 = eu-central-1 Frankfurt (ADR-013 v2 联动). 老吴 W9 W1 任务已排入本 plan, 请老周知晓 + 纳入 A 单元 Sprint-3 排期. | W9 W1 Mon | P1 |
| **@小段 (D 单元, Goalserve 专精)** | Goalserve Sofia 节点 IP 稳定性 audit: 确认 91.206.228.73 (inplay) / 91.206.228.78 (live) 在 W9 W1 实测期间无 IP 漂移. 若 dig 结果变化请立即 @老吴 告警. | W9 W1 全程 | P1 |
| **@老李 (A 单元, Polymarket API spec owner)** | WSS endpoint 确认: `wss://sports-api.polymarket.com/ws` 是否需要鉴权 token? 若需要, 提供测试用 token 或 fallback endpoint (无鉴权 orderbook WS). 老吴 §1.3 链路 3 依赖. | W9 W1 Mon AM (启动前) | P1 |

---

**老吴 (SRE, A 单元), 2026-05-29**
