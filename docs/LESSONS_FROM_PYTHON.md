# 从 sports-tail-trader (Python) 继承 vs 拒绝继承

**v0 红线文档** — 所有 agent 决议前必读. 任何 PR / 文档 / 决议引入下面 ❌ 列出的反模式, 自动 reject.

继承自 R33-R39 的失败教训 + 长期债务清算. **新项目不重蹈覆辙**.

---

## ✅ 必须继承 (业务规则 + 硬约束)

### 业务范围
- Polymarket 体育全盘口家族 (Moneyline/Totals/Spreads/分节/分盘/系列赛/prop/outright)
- "不放过任何可盈利市场" — 缺数据补数据, 不静默丢弃
- Goalserve 全直播源覆盖 (full_package_feed.txt 是权威清单)
- "直播源赔率 vs Polymarket 价格差价" 是重要 edge

### 交易安全硬约束
- 单一下单入口 (Python 的 `OrderExecutor` → C++ 对应模块)
- 强制风控门禁 (Python 的 `RiskManager` → C++ 对应模块), 任何下单旁路风控视为 P0 bug
- 买入侧不得保留长期 resting BUY
- Domain 层纯业务规则, 不依赖框架 / 网络 / DB
- 金额/价格用定点小数 (Python `Decimal` → C++ 需选 boost::multiprecision / 自实现 fixed-point)

### 状态真相源
- 优先级: 外部实时事件 > 内存状态 > DB (DB 仅审计)
- 运行时不读 DB 拉运行时数据
- WS 是盘口唯一真相源, REST 仅 sequence_gap 兜底

### §0 部署环境约束
- 中国 → 代理 → 美国 Polymarket, RTT 150-400ms, 带宽小
- 任何非必要网络 IO 都在 ms 级窃取 WS 决策反应速度
- 推 > 拉, 内存 store > 重复 fetch, demand-driven > broadcast, batch endpoint > N 次单调

### 操盘 + 审计统一走后端 API (不旁路)

### Family-agnostic 决策路径
- 所有 family (Moneyline/Totals/Spreads/分节) 走同一份 Kelly + signal 主路径
- 无 family 专属子包

---

## ❌ 拒绝继承 (Python 项目实施债务)

### 内存 / 性能反模式
- **Python 内存模型本身** (dict churn + GC 不还 OS + C ext PyObject 包装) — 这是 R39 PoC 证明 C++ 8 GB/h vs ~0 leak 的核心动因
- **tracemalloc.start(25) 这种"诊断工具反过来是 leak"** — 任何 instrument 必须有 disable 路径 + 性能预算
- **gc.freeze() + threshold 调参** 这种语言层 hack — C++ 不需要
- **httpx h2 stream gate Semaphore(20)** 治标 patch — C++ 应在协议层正确设计
- **ContextRedactionFilter 在主线程跑** 这种 silly performance bug — 必须先想清楚再写
- **discovery LRU 100K capacity** 这种 "万一" 凭感觉配置 — 容量必须有依据数字

### 架构反模式
- **main.py 1600 行 wiring** — 必须拆 module / DI container 化, 启动逻辑独立成 bootstrap layer
- **runtime: Any duck typing** — C++ 类型系统比 Python 严格, 不允许任何 erased type
- **getattr() 兜底** — C++ 没这种东西, 不允许任何 "如果有就用" 的 reflection 旁路
- **routes 注解层漂移** (64 处类型化债务) — C++ 编译期保证类型, 不会发生
- **散落硬编码 magic number** (60s / 90s / 120s 各种凭感觉 timeout) — 所有 constant 集中 config 表, 每个有依据
- **TOML / env 间接层** — 配置走代码默认值, 改阈值重编译 (Python 现在也是, 但 C++ 必须更严格)
- **散落决策逻辑** (operator / recovery / route 各写一份) — 决策只能在 quant_decider, 旁路视为 P0 bug

### 命名 / 同义债务
- **runtime / engine / state 多种叫法** — 同一概念单名
- **paused 字段 4 重语义** (人工 / 自动 / 市场 / 仓位) — 不允许字段 overload, 拆 4 个字段
- **过渡期 fallback 字段 / 别名 / 兼容包装** — 一次切到目标, 不留中间形态 (§8)

### 数据流反模式
- **每 N 秒"为完整性"加的 REST polling** — 必须先答 §17.10 七问 (能否 WS push / demand-driven / cache 命中 / 失败兜底)
- **gamma /events 已嵌套 markets 仍单独再调 /markets/{id}** — payload 共享原则, 任何 reader 走同一 in-memory store
- **httpx 默认 max_connections=100** 这种 "默认就行" 思维 — 所有 pool / queue / cache 容量必须显式 cap + 反压

### 测试 + 文档反模式
- **过程性文档不清理** (R 系列实施清单 / 进度日志长期保留为历史档案) — 任务完成态由代码 + git log 表达, 中间文档主动删
- **"必须补测试"原则反转** (Python §11 改为"默认不写测试") — C++ 必须有测试? Meeting γ 讨论. 但绝不允许 Python 那种"写了不维护"的回归测试僵尸

### 诊断 / 调试反模式 (R33-R39 教训)
- **看错地方反复治标** — 必须先实测排除, 再下结论 (R33-R39 5 次错诊根因)
- **lldb / 工具误用** — 任何实验必须有 baseline 对照 (R34-A 单次 -67MB 误判为"找到了")
- **诊断 endpoint 长期留生产** — 必须有时效性, 删除时机明确 (tracemalloc.start 留半年是反例)

---

## ⚠️ 重新设计 (Python 设计有道理, C++ 应换更好的)

### Python 用 dataclass + Settings, C++ 怎么对应?
- Python `TradingWorkflowConfig` dataclass 默认值 (无 TOML / env) — C++ 用 constexpr struct + 编译期 inject? 还是 toml++ 单文件?
- 由 Meeting α 决议

### Python `EventBus + OutboxEvent + sink + worker`, C++ 怎么对应?
- 是否仍 in-process async queue? 还是 shared memory ring buffer 跨进程 (利用 C++ 真并发)?
- 由 Meeting β 决议

### Python operator API 用 FastAPI, C++ 怎么对应?
- HTTP/REST 是否仍是首选? 还是 gRPC + 前端 WebGRPC?
- 由 Meeting β 决议

### Python 前端用 React (frontend/), C++ 重写时是否换?
- 候选: 继续 React (跨 stack) / Tauri (Rust + Web) / Qt (C++ native) / 纯 CLI
- 由 Meeting γ 决议

---

## 红线生效机制

1. 任何 agent 提议含上述 ❌ 模式 → 代码质量评审 #17 自动 reject + 红线引用
2. 任何 PR 含上述 ❌ 模式 → CI 自动 fail (lint rule)
3. 任何 milestone 验收发现上述 ❌ 模式 → milestone 不通过

**所有 agent 第一次召唤时, 必须 Read 本文档**.

