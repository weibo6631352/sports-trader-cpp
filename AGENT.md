# Agent Roster v0 — sports-trader-cpp

35 agent 班底, 分 7 类. 详细职责 + 协作规约由首次会议输出 v1.

## 核心实施 C++ (12)
1. C++ 首席架构师
2. C++ 高频系统工程师
3. C++ 网络协议工程师
4. C++ 数据/序列化工程师
5. C++ 持久化工程师
6. 加密签名专家
7. Polymarket 协议专家
8. 体育市场专家
9. 风控工程师
10. Linux SRE / DevOps
11. 可观测性工程师
12. 前端工程师

## 顾问 (5)
13. Rust 顾问
14. 现代 C++ 顾问 (C++20/23)
15. CPO (首席产品)
16. 首席架构评审
17. 代码质量评审

## 金融 / 量化研究 (4)
18. 金融专家 (资本市场 / 衍生品 / VaR)
19. 量化研究 - 信号 / α
20. 量化研究 - Backtest
21. 量化研究 - Microstructure

## 数据分析 (3)
22. 数据 - ETL / 清洗
23. 数据 - Stats / Bayesian
24. 数据 - 数据仓库

## 业务保障 (5)
25. 需求分析师
26. PM 项目经理
27. 安全工程师
28. 测试 / 回放工程师
29. 合规 / 法务顾问

## 跨域 (5)
30. 博彩行业专家
31. 机器学习工程师
32. 链上 / DeFi 顾问
33. AI Ops / Agent 协作工程师
34. 外部接口调研 / API Watch (通用 — Polymarket + 依赖库 + 竞品)

## 主力 IC 池 + 战术产品
35. 高级 C++ 开发工程师 (Senior IC Pool × 10)
36. 产品经理 (Product Manager — PRD / 用户故事 / 验收 criteria)

## 直播源专精 (1)
37. **Goalserve 接口调研专家** — full_package_feed.txt 全量探索 / 新 sport+league
    覆盖跟踪 / pregame/inplay/livescore 格式深度监控 / 没接入的 endpoint 主动 PoC

总数: 37 categories + 10 Senior IC = ~47 个 agent 在役

## 保障增补 (v0.1 追加)
38. **审计专家** (Audit Expert) — 业务行为审计 / 决策可复盘 / 财务一致性 / 操作可追溯
    - 跟 #29 合规法务 区别: 合规是 legal/regulatory; 审计是 "系统行为是否符合声明"
    - 跟 #28 测试 区别: 测试验证"功能对错"; 审计验证"事后可解释"
    - 关键产出: audit_events 表设计 / 操盘动作链路追溯 / 资金账目对账规则

39. **性能专家** (Performance Engineer) — profiling / benchmarking / latency budget / SIMD / cache 优化
    - 跟 #2 高频系统 区别: 高频系统是 P0 实现; 性能专家是跨模块 profile + 调优 + 性能回归门禁
    - 跟 #11 可观测性 区别: 可观测性提供 metric; 性能专家用 metric 找瓶颈 + 复现 + 修复
    - 关键产出: latency budget 表 (每模块允许多少 ms) / perf 回归 CI / flame graph 工具链

总数: 39 categories + 10 Senior IC = ~49 个 agent 在役
