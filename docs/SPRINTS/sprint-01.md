# Sprint-01 Backlog

- **Owner：** 老胡（pm-project-manager）
- **周期：** 2026-06-01 → 2026-06-12（2 周）
- **Sprint Goal：** 各部门完成基础调研 + 设计文档 v0.1；API 联通可行性验证
- **Last review：** 2026-05-28
- **Planning 会议：** 2026-06-01 9:00
- **Retro 会议：** 2026-06-12 16:00

---

## 交付物（必须 sprint 末可验收）

| Ticket | 责任人 | 部门 | 交付物 | 验收人 |
|---|---|---|---|---|
| S1-001 | 老周 | A | 系统架构 v0.1（5 层 + 模块边界 + 依赖图） | 老郭 |
| S1-002 | 老李 | A | Polymarket CLOB API 实测报告 + 协议规范 v1 | 老周 |
| S1-003 | 小余+小段 | D | Goalserve API 实测报告 + 字段清单 | 老胡 |
| S1-004 | 老韩 | B | RiskManager 设计文档 v0.1 | 小梁 + 老郭 |
| S1-005 | 老孙 | A | 私钥管理方案（HSM/KMS 选型） | 老沈 + 老雷 |
| S1-006 | 老黄 | B | 合规红线清单 + 全员签收 | 老雷 |
| S1-007 | 小梁 | C | Polymarket 体育市场结构研究报告 | 老钱 |
| S1-008 | 老钱 | F | MVP scope 拒绝清单 | 老雷 |
| S1-009 | 老叶 | F | Polygon RPC 选型 + gas 监控方案 | 老孙 |
| S1-010 | 老吴 | A | 跨洋部署方案 v0.1 | 老周 |
| S1-011 | 老姜+小石 | A | 延迟预算拆解 + lock-free 选型 | 老周 |
| S1-012 | 老彭 | C | 体育博彩行业市场分析（sharp money + line movement） | 小梁 |
| S1-013 | 小林 | E | Q2 P0 岗位 JD（老冀+小秦）发布 | 老雷 |
| S1-014 | 老胡 | E | 全局甘特图 + 风险登记 | 老雷 |
| S1-015 | 小米 | E | docs/ 体系搭建 + 漂移监控 | 老雷 |
| S1-016 | 老沈 | B | 安全威胁模型 v1 | 老孙 |
| S1-017 | 小程 | C | 信号假设清单（≥ 10 候选） | 小梁 |
| S1-018 | 小郑 | A | Prometheus + Grafana 基础设施 v0.1 | 老吴 |
| S1-019 | 老徐 | F | 班底协同规范 v1 | 小林 |
| S1-020 | 小白 | F | LLM 辅助开发规范 | 老雷 |
| S1-021 | 老陈+老吴+小段 | A+D | 外部 API + 网络代理 速率/延迟实测报告 | 老姜 |
| S1-022 | 小尤 | E | UX 体感评估框架 v1（指标 + 评分卡） | 老胡 |
| S1-023 | 小宫 | E | Dogfood 剧本 v1（NBA/NFL/MLB 周末跑通脚本） | 老胡 |
| S1-024 | 老徐 | F | 外部 MCP + 工具能力盘点（Chrome DevTools / lldb / MCP 服务等） | 老雷 |
| S1-025 | 老吴 | A | 安装 chrome-devtools MCP + Python 量化栈 + .claude/settings.json 入 git | 老徐 + 老雷 |
| S1-026 | 老陈+老吴+小段 | A+D | 带宽专项实测 v2 (在 S1-021 v1 基础上, GM 强调带宽与延迟并重) | 老姜 + 老雷 |

---

## 风险登记

| Risk | 概率 | 影响 | 缓解 | Owner |
|---|---|---|---|---|
| Goalserve API 限流影响联调 | 中 | 高 | 调研代理 + 节流；GOALSERVE_PROXY 已配 | 小段 |
| Polymarket API 鉴权失败 | 低 | 高 | 凭证已验证；备用 sandbox | 老李 |
| 跨洋链路稳定性未知 | 高 | 中 | Sprint-1 内做带宽 + 延迟实测 | 老吴 |
| HC-01 / HC-02 招聘候选人池薄 | 中 | 中 | 多渠道（LinkedIn + 内推 + 行业论坛）| 小林 |

---

## Daily Standup

- 时间：每日 10:00（15min）
- 主持：老胡
- 模板：昨完成 / 今做 / blocker
- Blocker 4h 内 owner 响应，否则升级老雷
