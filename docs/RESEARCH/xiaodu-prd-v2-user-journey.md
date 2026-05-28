# PRD v2 — 4 user role + user journey (用户视角重写)

> **Owner:** 小杜 (PM) | **Last review:** 2026-05-28
> **触发:** GM 错 #7 — 用户原话 "更重要的是你需要什么, 而不是看他有什么。接口可以他来定, 需求可是一定要你提的。"
> **输入:** `CLAUDE.md` + `AGENT.md` (仅 role) + GM `must-haves.md` (M-01..10) + 老钱 CPO 业务能力 (W4 并行)
> **作用:** 从用户视角写 PRD, 推翻 v1, user journey 为 source of truth
> **约束:** 不读代码 / 官方文档 / 现有报告 / 不写技术语言; 每 use case 以"用户想做什么"开头

---

## 0. v2 与 v1 核心区别

v1 是供给侧梳理 (endpoint / 模块 / API), v2 是需求侧 (用户想知道 / 想做 / 看到). 对不上 → **以 v2 为准, 工程师改后端**.

---

## Part 1: 4 个 user role

### Role 1: Operator (操盘手)

**身份:** 公司雇佣专业操盘手 (M1 老雷兼任, M4.5 后扩 1-2 人轮班).

**典型一天:** 早 8:00 接班看过夜状态 → 白天 tab 常开但不盯屏 → 每 1-2h 扫一眼 → 晚 20:00 交班 / 自动夜班 (M2+).

**核心目标:** 系统稳定 + 异常时第一响应. 操盘手是"决断者"不是"操作员", 系统自动跑, 操盘手只在异常时介入.

**不做:** 不写策略 / 不调代码 / 不决 Kelly / RM 阈值 (GM + Risk Owner 联签) / 不写 PRD.

**关心 (按优先级):** RM state → 实时 fill 流 → WSS 跨洋链路 → 当日 paper PnL → 异常 alert.

**不关心:** signal alpha 贡献 (Analyst) / ML feature importance (Analyst) / 投资人 PDF (GM).

### Role 2: Quant Analyst (量化分析师)

**身份:** 量化研究部 (M1 1 人兼任, M4.5 后 3-4 人).

**典型一天:** Sprint 末看 paper + 跑回测 / 周中写 signal spec + 跑 ML 训练 (离线 notebook 导 ONNX) / 月度做 alpha decay 检测. 不盯实时.

**核心目标:** 策略迭代 + alpha 持续产出 + backtest vs paper 一致性 (R-2 红线) + ML 训练数据准备.

**不做:** 不操盘 / 不上线 live (需 CPO + GM ack) / 不调代码 / 不应急响应实时 alert.

**关心 (按优先级):** 历史 paper data (signal × sport × market type 切片) → backtest vs paper 偏差 → ML feature 可用性 → alpha decay 趋势 → 新策略统计指标.

**不关心:** WSS 是否断线 / halt 按钮 / 实时 reject 决策 (回放看不实时盯).

### Role 3: GM (老雷 / 老板代理人)

**身份:** 总经理 + 老板代理人. 战略 + 投资人 + 紧急止损最终拍板.

**典型一天:** 早 8:30 30s 扫公司 / 白天协调评审 / 周末投资人沟通 / 大事件拍板.

**核心目标:** 公司战略 (北极星 T+36 月 $5M PnL, Sharpe ≥ 1.5) / 紧急止损权 (M-05) / 7 hard gate (M-04) / 投资人交代 (M-10).

**不做:** 不写代码 / 不操盘 / 不调参数 (除非联签).

**关心 (按优先级):** 今日 PnL (一个数字) → 7/30 day / 季度趋势 → 7 hard gate 进度 → RM 失效告警 → halt 按钮 → 季度 PDF.

**不关心:** 单笔 fill 复盘 (除非异常) / WSS 心跳细节 / feature importance.

### Role 4: Trader (M5 后人工干预)

**身份:** M5 后扩招的"人工补充". M5 前不存在.

**典型一天 (M5+):** 大部分不上场 / 大额 position 平仓 / 异常 settlement 对账 / 跨平台手动套利 (M5+ 扩展).

**核心目标:** 系统外人工补充 / 大额事件人在回路 / 对账与异常处理.

**不做:** 不操作实时决策 (Operator) / 不写策略 (Analyst) / 不调参数 (GM 联签).

**关心 (按优先级):** 大额 fill 待 review → 结算异常 → 对账偏差 (gas / fee / slippage 不通的) → 跨平台机会 (M5+).

**不关心:** 实时 RM 决策 / ML 训练数据 / 投资人 PDF.

---

## Part 2: 每 role 的 user journey (12+ use case)

### UC-OP-01: 早上接班看公司状态

**Role:** Operator
**触发:** 用户主动 (每天 8:00 接班)
**前置条件:** 用户登录大盘页面

**步骤:**
1. 用户想知道**过夜系统是否正常**
2. 用户打开浏览器, 进入大盘 L0 页面
3. 系统应在 3 秒内展示: 当前 RM state (绿/黄/红) + 过夜异常计数 + 当前 paper PnL + WSS 连接状态 (全绿/部分断/全断)
4. 用户看完, 如果全绿 → 接班完成, 浏览器 tab 留着, 该干嘛干嘛; 如果有黄/红 → 进 L1 详情看具体哪个单元异常

**Happy path:** 全绿, 用户 5 秒钟看完闭眼. 没有需要立刻处理的事.

**异常 path:**
- 大盘加载超时 (跨洋链路抖) → 用户疑虑"是 UI 挂了还是后端挂了", 系统应明确告知"前端数据拉取超时, 不代表系统挂了" + 重试按钮
- 大盘加载成功但数据是 30 分钟前的 stale → 系统应大字号标红"数据 stale, 上次更新 8:00:00", 不能让用户误以为是实时数据

**Acceptance:**
- [ ] 大盘 L0 首屏在 3 秒内可见关键 4 项 (RM state / 异常计数 / paper PnL / WSS 状态)
- [ ] 数据 stale > 60s 必须显眼提示 (不能静默展示旧数)
- [ ] 用户不需要 ssh / 不需要 grep log / 不需要查代码

---

### UC-OP-02: RM HALTED 收到通知 → 决断是否人工介入

**Role:** Operator
**触发:** 系统 push (RM 进入 HALTED 状态)
**前置条件:** Operator 浏览器 tab 在后台 / 手机邮件可达 / 系统 alert 通道畅通

**步骤:**
1. 用户想知道**为什么 RM HALTED 了 + 现在该不该干预**
2. 系统先 push (浏览器声音 + 邮件) 通知用户
3. 用户切到浏览器, 大盘 L0 顶部应有红色 banner "RM HALTED at 14:32:18, 原因: <一句话>"
4. 用户点 banner 进入 L1 风控详情, 系统应展示: 触发 HALTED 的具体规则 / 当时的 position / 当时的 PnL / 已 open 订单列表 / 推荐动作 (等系统自恢复 / 人工 ack / 手动 close)
5. 用户决断: 如果是已知的规则触发 (e.g. 单日亏损上限) → ack 等明天; 如果是未知异常 → 进 L2 audit chain 查链路, 必要时叫 Engineer

**Happy path:** 用户 30 秒内看到 banner → 1 分钟内进 L1 看清原因 → 5 分钟内决断.

**异常 path:**
- 系统 HALTED 但 push 没发出来 (alert 通道挂了) → 用户下次刷新大盘才发现, 已经晚了 → **alert 通道必须有独立健康检查** + 通道挂时大盘显眼提示 "alert 通道异常, 可能漏报"
- HALTED 原因 unknown (规则没匹配) → 系统应展示 "HALTED 触发但原因匹配失败, 已记录, 请叫 Engineer"

**Acceptance:**
- [ ] RM 进入 HALTED 后 30s 内用户收到 push (浏览器 + 邮件 双通道)
- [ ] 大盘 L0 顶部红色 banner 显示 HALTED 状态 + 一句话原因
- [ ] L1 风控详情可见: 触发规则 / 当时 position / 已 open 订单 / 推荐动作
- [ ] 用户不需要看代码 / 不需要 grep log 即可决断

---

### UC-OP-03: WSS 跨洋链路断了 → 等重连 vs 强重启

**Role:** Operator
**触发:** 系统 push (WSS 连接状态变化)
**前置条件:** WSS 至少有一条断线

**步骤:**
1. 用户想知道**跨洋链路是哪个断了 + 多久了 + 是临时抖还是真挂**
2. 系统 push 通知 (黄色, 不上升到红色除非全断)
3. 用户进大盘 L0, 顶部黄色 banner "WSS 部分断线 (Goalserve inplay 已断 3min)"
4. 用户点 banner 进 L1 网络详情, 系统展示: 每条 WSS 的状态 (绿/黄/红) + 断线时间戳 + 最近成功心跳时间 + 自动重连尝试次数 / 间隔
5. 用户决断: 如果断线 < 5min 且系统在尝试重连 → 等; 如果断线 > 5min 或重连失败 > 3 次 → 强重启该 WSS (按钮在 L1)

**Happy path:** WSS 5min 内自动重连成功, 用户只 ack 一下就完事.

**异常 path:**
- WSS 显示"已重连"但实际数据未恢复 (心跳通了但 message stale) → 系统应单独有"数据流活跃度"指标, 而不仅看 socket 状态
- 用户强重启后仍断 → 系统应提示 "请检查公司侧网络 / 联系跨洋链路服务商 / 或联系 Engineer"

**Acceptance:**
- [ ] WSS 任一条断线 30s 内 push 通知
- [ ] L1 网络详情可见每条 WSS 的 (状态 / 断线时长 / 最近心跳 / 重连尝试)
- [ ] 强重启按钮在 L1 可见, 二次确认后执行
- [ ] 用户能区分"socket 通"和"数据流活" — 两个独立指标

---

### UC-OP-04: 单笔大额 fill 异常 → drill-down 复盘

**Role:** Operator
**触发:** 系统 push (单笔 fill 金额超阈值)
**前置条件:** 系统配置了大额阈值 (e.g. $500 / 单笔)

**步骤:**
1. 用户想知道**这笔大额成交是谁触发的 + 当时的依据是什么**
2. 系统 push (黄色 alert) "大额 fill: $850 at 15:42:10"
3. 用户进大盘, 大额 fill 在 L1 fill 流列表里被高亮
4. 用户点这条 fill, 进 L2 单笔追踪页面, 系统应展示完整链路:
   - 触发的 signal (哪个 condition?)
   - signal 当时看的市场 PIT snapshot (Polymarket book + Pinnacle quote)
   - RM 决策 (allow + 当时的 limit 还剩多少)
   - signer (paper / live)
   - matcher (filled / partial / canceled)
5. 用户判断: 如果链路合理 → ack 关掉; 如果链路异常 (e.g. signal 看的 quote 已过期 30s) → 标记为可疑事件, 升级到 Analyst

**Happy path:** 用户 2 分钟内看完链路, 90% 情况下链路合理, ack 关掉.

**异常 path:**
- 链路某一环 hash 校验失败 → 系统应大字号标红 "审计链不完整, 此事件不可信", 升级到 P0
- PIT snapshot 缺失 (signal 当时没存) → 系统应明确 "signal PIT 未留痕, 无法复盘", 标记为 ML 训练数据废弃

**Acceptance:**
- [ ] 大额 fill 触发后 30s 内 push
- [ ] L2 单笔追踪页面在 5s 内加载完整链路 (signal → snapshot → RM → signer → matcher → settle)
- [ ] audit chain hash 校验失败时大字号显眼提示
- [ ] PIT snapshot 缺失时明确提示, 不能静默

---

### UC-AN-01: 月度 signal 贡献分析 → alpha decay 检测

**Role:** Quant Analyst
**触发:** 用户主动 (月初做月度复盘)
**前置条件:** 至少 30 天的 paper 数据

**步骤:**
1. 用户想知道**P0-01 信号上个月的 PnL 是多少 + 按 sport / market type 拆分后哪个赚哪个亏**
2. 用户进 Analyst L1 信号分析页面, 选时间窗 (last 30 days)
3. 系统应展示: signal 总 PnL + 按 sport (NBA / NFL / MLB / ...) 拆分 + 按 market type (Moneyline / Totals / Spreads / ...) 拆分 + 按 maker vs taker 拆分 + 与上月对比 (alpha 涨/跌 / 持平)
4. 用户判断: 如果某 sport × market type 组合 PnL 显著下降 → 该 cell 是 alpha decay 信号 → 写新 signal spec / 调参; 如果整体 PnL 持平 → 信号还有效

**Happy path:** 月度报表 30 秒内加载, 用户 10 分钟看完做出决断.

**异常 path:**
- 数据缺失 (某周 paper 没跑) → 系统应明确"数据窗 X 至 Y 缺失, 报表不完整", 不能静默忽略
- 样本量过小 (某 cell < 30 笔) → 系统应标灰 + 提示"样本不足, 结论不可信"

**Acceptance:**
- [ ] 月度信号报表 30s 内加载
- [ ] 至少 4 维拆分 (sport / market type / signal condition / maker-taker)
- [ ] 数据缺失 / 样本不足时明确标识, 不静默
- [ ] 与上月对比的 delta 一目了然 (颜色 + 数字)

---

### UC-AN-02: paper 数据 → ML 训练数据导出

**Role:** Quant Analyst
**触发:** 用户主动 (新 ML 模型训练周期)
**前置条件:** 至少 60 天的 paper 数据

**步骤:**
1. 用户想要**把 paper 期间所有决策点 + 当时的 feature snapshot + 后续结果 拉成训练集**
2. 用户进 Analyst L1 数据导出页面, 选时间窗 + 选 feature 子集 (e.g. PM mid / Pinnacle line / book imbalance / 时间到比赛开赛分钟数 / 比分 / ...)
3. 系统应在合理时间内 (e.g. < 5 分钟) 生成训练集 (Parquet / CSV), 用户下载到本地 notebook 跑 ML
4. 用户在 notebook 里训练, 训练完导 ONNX, 交给 Engineer 接入生产

**Happy path:** 用户选完时间窗 → 系统打包 → 用户下载 → 在 notebook 里跑, 整个过程 < 30 分钟.

**异常 path:**
- 数据量太大 (e.g. 几十 GB) → 系统应支持分片下载 + 断点续传
- feature 字段缺 (e.g. signal 当时没存某 feature) → 系统应明确告知"feature X 在窗 Y 不可用", 不能给用户一个 NaN 列让用户自己猜
- PIT 与 settle 时间对不上 (settle 在导出窗外) → 系统应允许"半截样本"(只 feature, label TBD) 或纯过滤掉, 由用户选

**Acceptance:**
- [ ] 数据导出 30 天窗 < 5 分钟
- [ ] 支持 Parquet / CSV 格式
- [ ] feature 缺失明确标识, 不静默 NaN
- [ ] 导出包含完整 schema 描述 (字段名 / 类型 / 单位 / PIT 时间戳)

---

### UC-AN-03: backtest 跑历史 → 与 paper 实测对比 (R-2 红线)

**Role:** Quant Analyst
**触发:** 用户主动 (Sprint 末做一致性验证)
**前置条件:** backtest engine 可用 + 有对应窗的 paper 数据

**步骤:**
1. 用户想知道**同一个时间窗, backtest 跑出来的 PnL 与 paper 实测的 PnL 是否一致 (差异 < 阈值)**
2. 用户进 Analyst L1 一致性验证页面, 选时间窗 + 选 signal config
3. 系统应跑 backtest (后台异步, 进度条) 然后展示对比表: backtest PnL / paper PnL / 差异 / 差异占比 / 按 trade 逐笔对比
4. 用户判断: 如果差异 < 1% → 一致性 OK; 如果差异 > 5% → R-2 红线触发, 不允许该策略上 live, 召唤 Engineer 查代码

**Happy path:** 一致性 OK, 用户 ack 后该策略进入下一阶段 (从 paper → live readiness).

**异常 path:**
- backtest 跑失败 (数据缺 / 引擎 bug) → 系统应明确"backtest fail at X, 原因 Y", 不能给一个 "??? PnL = 0" 让用户误以为是 OK
- 差异显著 → 系统应自动定位差异最大的几笔 trade, 让用户 drill-down 看具体哪笔

**Acceptance:**
- [ ] backtest 后台异步跑, 进度条可见
- [ ] 对比表逐笔可 drill-down
- [ ] 差异 > 阈值时显眼红标 (R-2 红线)
- [ ] 用户不需要看代码即可定位差异来源

---

### UC-GM-01: 早上 30s 扫公司

**Role:** GM
**触发:** 用户主动 (每天早上 8:30)
**前置条件:** 用户登陆 GM 大盘

**步骤:**
1. 用户想知道**昨天赚了还是亏了 + 系统过夜有没有出大事**
2. 用户打开浏览器, 进入 GM L0 大盘
3. 系统应在 2 秒内展示一屏 5 个核心数字:
   - 今日 paper PnL (大字号, 绿/红)
   - 7 day PnL 曲线 (mini chart)
   - RM state (绿/黄/红)
   - 异常计数 (过夜 ack pending 数)
   - 7 hard gate 进度 (e.g. "3/7 解锁")
4. 用户决断: 全绿 → 关掉浏览器去开会; 有黄红 → 点进 L1 看详情

**Happy path:** 30 秒看完一屏, 一天的"公司在跑"信心建立, 关 tab.

**异常 path:**
- 数据加载慢 (跨洋链路) → 系统应展示 skeleton 占位, 数字逐步填充, 不能整屏白屏 5 秒
- 数字与昨日的 close 不一致 → 系统应明确"截至 X 时间, 数据可能未完全 reconcile"

**Acceptance:**
- [ ] L0 GM 大盘 5 个核心数字 2s 内可见
- [ ] 字号大到不戴老花镜也能看清
- [ ] 数字时效性显眼标识 (X 秒前更新)
- [ ] 用户不需要任何操作即可完成"扫描"

---

### UC-GM-02: 7 hard gate 进度

**Role:** GM
**触发:** 用户主动 (周末做战略复盘)
**前置条件:** 用户登陆 GM L1 战略页面

**步骤:**
1. 用户想知道**M4.5 离解锁还差多远, 哪个 gate 卡住了**
2. 用户进 GM L1 战略页面, 7 hard gate 列表
3. 系统应展示每个 gate 的: 名称 / 当前状态 (未达成 / 已达成) / 当前数值 / 目标数值 / 卡住天数 / 责任 Owner
4. 用户判断: 哪个 gate 卡最久 → 找该 Owner 问"卡在哪, 需要什么资源" → 战略会上拍板

**Happy path:** 用户 5 分钟内看清 7 个 gate 状态, 排出"本周最紧的 1 个 gate".

**异常 path:**
- 某 gate 数值"已达成"但 Owner 还没确认 → 系统应明确"达成但未 ack, 等 Owner 签字" — 不能自动跳"已解锁"
- gate 标准变更 (e.g. 阈值从 X 调到 Y) → 系统应留 audit log, GM 能看到"谁在何时改过 gate 标准"

**Acceptance:**
- [ ] 7 gate 列表一屏可见
- [ ] 每个 gate 的 (状态 / 数值 / 目标 / 卡住天数 / Owner) 清晰
- [ ] gate 标准变更留 audit
- [ ] 达成但未 ack 的 gate 明确标识

---

### UC-GM-03: 紧急 halt

**Role:** GM
**触发:** 用户主动 (突发事件, e.g. Polymarket 平台异常 / 数据源大规模污染 / 监管要求)
**前置条件:** 用户登陆 GM L0 大盘

**步骤:**
1. 用户想**立刻让系统停所有新订单 + 保留已 open position (等 GM 后续决定)**
2. 用户点 L0 大盘右上角"紧急 halt"按钮 (红色)
3. 系统弹窗二次确认 "确认 HALT? 已 open position 不会自动 close, 请稍后联系 Operator 决断"
4. 用户输入"HALT" 字样 + 点确认
5. 系统在 5s 内: RM 进入 HALTED + 全员 push 通知 + audit log 留痕 (GM at HH:MM 触发)
6. 用户看到大盘顶部红色 banner "已 HALTED by GM at HH:MM" → 关掉 tab 去处理外部事件

**Happy path:** 用户 30 秒内完成"看到事件 → 决断 halt → 确认 halt → 系统已 halt".

**异常 path:**
- 系统未在 5s 内进 HALTED → 系统应明确"halt 命令已发, 等待 RM 响应 (X 秒)", 不能让用户怀疑"按了没生效"
- 已 open position 平仓策略 (CPO 待拍) — v2 暂定: 不自动 close, 等 Operator 人工; M5 后可加"自动 close at market" 选项

**Acceptance:**
- [ ] halt 按钮在 L0 显眼可见
- [ ] 二次确认强制输入 "HALT" 字样 (防误触)
- [ ] 系统 5s 内进入 HALTED
- [ ] 全员 push 通知 + audit log 留痕
- [ ] 已 open position 处理策略明确 (v2: 等人工; M5+: 可配置)

---

### UC-GM-04: 季度投资人 PDF

**Role:** GM
**触发:** 用户主动 (季度末)
**前置条件:** 至少 90 天的 paper / live 数据

**步骤:**
1. 用户想**一键导出季度业绩报表给投资人**
2. 用户进 GM L1 报表页面, 选季度 (e.g. 2026 Q2)
3. 系统应展示草稿: 季度 PnL / Sharpe / 最大回撤 / 笔数 / 净收益 / 按月 PnL 曲线 / 按 sport 分布
4. 用户可调整文案 (e.g. 加段战略说明), 然后导出 PDF / CSV
5. 用户下载, 发给投资人

**Happy path:** 用户 10 分钟内完成"选季度 → 看草稿 → 调文案 → 导出 → 发出".

**异常 path:**
- 数据不完整 (某月缺数据) → 系统应明确"X 月数据不完整, 报表会标 N/A"
- 第三方审计需求 → 系统应能导出"原始 audit chain + hash" 供第三方验证 (M-10 要求"业绩可独立第三方审计")

**Acceptance:**
- [ ] 季度报表 PDF 5 分钟内生成
- [ ] 包含: PnL / Sharpe / DD / 笔数 / 月度曲线 / sport 分布
- [ ] 文案可编辑
- [ ] 第三方审计模式可导原始 audit chain

---

### UC-TR-01: 大额 position 手动平仓 (M5+)

**Role:** Trader (M5 后)
**触发:** 系统 push (单笔 position 金额超阈值, 系统不敢自动平)
**前置条件:** M5+ 阶段 + Trader 角色已上线

**步骤:**
1. 用户想知道**这笔大额 position 现在的状态 + 市场价 + 平仓预估损益**
2. 系统 push (黄色 alert) "大额 position $5000 需人工 review"
3. 用户进 Trader L1 大额列表, 选这笔
4. 系统应展示: position 详情 (size / entry price / 当前 mid / unrealized PnL) + 平仓建议 (market vs limit vs 等到期)
5. 用户决断: 立刻 market close / 挂 limit / 等结算

**Happy path:** 用户 5 分钟内决断 + 下单, 系统执行 + 留 audit.

**异常 path:**
- 市场流动性差, market close 滑点大 → 系统应预估滑点, 警告"预估滑点 > X%, 是否仍执行"
- 平仓订单本身被 RM reject (异常) → 系统应明确"平仓单被 RM reject, 原因 X", 不能静默失败

**Acceptance:**
- [ ] 大额 position push 通知
- [ ] L1 大额列表清晰可见 position 详情 + 平仓建议
- [ ] 平仓订单留 audit (Trader at HH:MM)
- [ ] 滑点预估警告

---

### UC-TR-02: 异常 settlement 对账 (BC-07)

**Role:** Trader (M5 后)
**触发:** 系统 push (Polymarket 结算与本地 ledger 偏差超阈值)
**前置条件:** 至少 1 笔已结算

**步骤:**
1. 用户想知道**Polymarket 结算给了多少 + 本地 ledger 期望多少 + 差额来源 (gas / fee / slippage / 错误)**
2. 系统 push (红色 alert) "settlement 偏差 $X, 笔 ID = Y"
3. 用户进 Trader L1 对账页面, 选这笔
4. 系统应展示: 期望 PnL / 实际 PnL / 差额 / 差额拆解 (gas / fee / 已知 slippage / 未知)
5. 用户判断: 如果是已知费用 → ack 关掉; 如果"未知"占比大 → 升级到 Engineer 查代码

**Happy path:** 用户 10 分钟内对账完, 95% 情况下能拆完差额.

**异常 path:**
- 差额完全不可解释 → 系统应允许"标记为可疑事件 + 升级 P0", 不能让用户"算了就算了"
- 对账数据源 (Polymarket data API) 拉取失败 → 系统应明确"对账数据源不可用, 暂缓对账"

**Acceptance:**
- [ ] 结算偏差超阈值后 push
- [ ] L1 对账页面差额拆解清晰
- [ ] 不可解释差额可升级 P0
- [ ] 对账数据源失败明确提示

---

## Part 3: 信息架构 (L0 / L1 / L2)

### L0 — 大盘 (P0 必看, 一屏 5-7 项)

**目标:** 用户 30 秒扫完, 决定是否进 L1.

**公共 (所有 role):** RM state (绿/黄/红) / 当日 paper PnL (大字号) / 异常 alert 计数 (三级聚合) / WSS 状态 (全绿/部分断/全断) / 数据时效 (X 秒前更新).

**Operator 个性化:** 实时 fill 流 mini (last 5) + 当日 reject count.

**Analyst 个性化:** 当日 signal 触发数 + 最近一次 backtest 完成时间.

**GM 个性化:** 7 day PnL 趋势 mini + 7 hard gate 进度 ("3/7 解锁") + halt 按钮 (右上角).

**Trader 个性化 (M5+):** 大额 position 待 review 数 + 对账异常笔数.

### L1 — 单元详情 (P1, 点 L0 进入)

**目标:** 用户深入某单元, 看清"什么情况", 决定是否进 L2 单笔追踪.

**5 个 L1 页面 (按业务单元):**

1. **风控详情 (L1-Risk):** RM state 历史 / 触发规则列表 / 当前限额使用率 / 已 open 订单 / 推荐动作
2. **网络详情 (L1-Network):** 每条 WSS 状态 / 心跳 / 重连 / 数据流活跃度 / 强重启按钮
3. **fill 流 (L1-Fills):** 实时 fill 列表 / 按 sport / market type 过滤 / 大额高亮 / 点击进 L2
4. **信号分析 (L1-Signal, Analyst 主用):** signal × sport × market type 矩阵 / alpha decay 趋势 / 最近触发列表
5. **战略 (L1-Strategy, GM 主用):** 7 hard gate 进度 / 季度 PnL 曲线 / 投资人报表入口

### L2 — 单笔追踪 (P2, 点 L1 单条记录)

**目标:** 用户复盘一笔决策, 看完整链路.

**内容:** 触发 signal (哪 condition 命中) / signal 当时 PIT snapshot (PM book + Pinnacle quote) / RM 决策 (allow/reject + 原因 + 当时 limit) / signer (paper/live + 签成功否) / matcher (filled/partial/canceled) / settle (Win/Loss + 实际 PnL) / audit chain hash (链不完整时大字号标红).

### 跳转关系

```
L0 大盘
  ├── 点 RM state 灯 → L1-Risk
  ├── 点 WSS 状态 → L1-Network
  ├── 点 fill mini → L1-Fills
  ├── 点 signal mini → L1-Signal
  ├── 点 gate 进度 → L1-Strategy
  └── 点紧急 halt → 直接弹窗确认 (不进 L1)

L1-Fills
  └── 点某条 fill → L2 单笔追踪

L1-Risk
  └── 点某条 reject → L2 单笔追踪 (含 reject reason chain)

L1-Signal
  └── 点某 cell (sport × market type) → 该 cell 下的 trade 列表 → 点某 trade → L2
```

---

## Part 4: 关键交互模式

不写技术细节, 写**用户体感**.

### 4.1 实时 push (用户被动接收)

**适用:** RM state 变化 / 大额 fill / WSS 断线 / 紧急事件.

**体感:** tab 后台时 "标题闪烁 + 系统通知 + 声音" 三重 push; 重要事件 (HALTED) 升级邮件 + 短信 (M5+); 30s 内必到.

**期望:** push 含"一句话原因" + 点击跳 L1 + "我已知悉"按钮 (ack).

### 4.2 轮询 (用户停留页面, 数据持续刷新)

**适用:** PnL 曲线 / position list / gate progress / fill 流.

**体感:** 数字自动更新 5-10s 频率, 无闪烁; stale > 60s 显眼提示.

**期望:** 数字变化短暂高亮 (PnL 涨绿闪一下); 用户能手动暂停刷新 (截图用).

### 4.3 on-demand (用户主动查询)

**适用:** L2 单笔追踪 / 历史查询 / backtest / 数据导出 / PDF 生成.

**体感:** 等待 < 5s; 复杂查询异步 + 进度条 + 完成 push; 失败明确告知原因 (不能转圈白屏).

**期望:** < 1s 同步; 1-5s 同步 + skeleton; > 5s 异步 + 进度条 + 完成 push; 查询参数留痕.

---

## Part 5: 与 v1 的差异 (changelog)

v1 (`xiaodu-mvp-prd-v1.md`) 基于"已有模块梳理", v2 推翻重来. 关键差异:

| 维度 | v1 (供给侧) | v2 (需求侧) |
|---|---|---|
| 写作起点 | 列 endpoint / 模块 | 列 user role + use case |
| acceptance | "调 RiskGateway::evaluate" | "用户想知道为什么被拒" |
| 优先级依据 | 模块成熟度 | M-01..10 用户体感 |
| role 视角 | 模糊"用户"统称 | 4 role 各自典型一天 + 关心 / 不关心 |
| 信息架构 | endpoint 列表 | L0 → L1 → L2 决断链 |
| 交互模式 | "WSS subscribe / HTTP poll" | "实时 push / 轮询 / on-demand" |
| 异常处理 | "返回 errno" | "一句话原因 + 推荐动作" |
| 数据时效 | "字段 update_ts" | "stale > 60s 显眼提示" |
| 审计 | "audit chain SHA256" | "链不完整大字号标红" |
| 跨域协作 | struct / topic 术语 | 用户决断 (是否人工介入) |

**方法论变化:** v1 写的是"系统使用说明书", v2 写的是"用户决策手册". v2 与代码对不上 → 以 v2 为准, 工程师改后端 (GM 错 #7).

---

## Part 6: 给小颖 (需求分析师) 的指令

PM 给 user journey + use case 框架. 小颖把每 UC-XX-YY 拆成:

1. acceptance criteria (testable + measurable, 从本文 Acceptance 细化)
2. 测试用例 (正向 / 异常 / 边界)
3. 回归用例 (M1 / M4.5 / M5 各 stage)
4. 可观测指标 (用户体感量化, e.g. "3 秒内可见" → p95 ≤ 3s)

输出: `xiaoying-acceptance-spec-v1.md` (W4 Wave 22 并行).

---

## Part 7: 给小苏 (前端) / 小尤 (UX) 的指令

**关系:** v2 = source of truth (用户要的) / Wave 21 = gap 分析输入 (后端有的). **v2 与 Wave 21 对不上 → 以 v2 为准, 工程师改后端.**

**给小苏:**
- 按 L0/L1/L2 信息架构画 wireframe
- 4 role 各自 L0 大盘个性化 (公共 + role 个性化)
- 交互模式严按 Part 4
- 异常态 (stale / 加载失败 / 数据缺失) 必须显眼提示, 不能静默

**给小尤:**
- 评估每 UC 的"心流"顺畅度 (触发到决断的认知负担)
- 重点关注异常 path 体感 (用户最痛的不是 happy path)
- 评估 4 role "切换"成本 (e.g. Operator 上 GM 大盘是否信息过载)

---

## Part 8: 待 CPO (老钱) 拍的产品决策

以下 v2 留空, 待 CPO 拍:

1. **HALTED 后已 open position 策略** (M-05): 自动 close at market? 等到期? Operator 手动? v2 默认"Operator 手动", M5+ 可能需"可配置"
2. **大额阈值** (UC-OP-04 / UC-TR-01): 单笔 $500 / position $5000 是 v2 猜测值
3. **alert 通道优先级** (UC-OP-02): v2 默认浏览器 + 邮件双通道, M5+ 是否加短信 / Slack / 电话
4. **Trader role 上线时点**: v2 假设 M5 后, 具体 sprint CPO 拍
5. **第三方审计范围** (UC-GM-04): 只导 hash? 原始数据? 涉合规 (老黄主权)

---

## Part 9: 完成汇报

**交付:** 4 role + 12 UC + L0/L1/L2 信息架构 + 3 交互模式 + 10 项 changelog + 下游指令 + 5 项待 CPO 拍.

**方法论改进:** 从"系统能做什么"转向"用户想做什么". GM 错 #7 吃透, 不再回头看供给侧.

**协作期待:**
- 老钱 (CPO): 拍 Part 8 5 项产品决策
- 老雷 (GM): 整合 v2 + Wave 21 gap 分析给改造单
- 小颖: 按 UC-XX-YY 拆 acceptance + 测试用例
- 小苏 / 小尤: 按 L0/L1/L2 落 wireframe + UX 评估
- 老韩 / 老郭: 风控 UC (UC-OP-02 / UC-GM-03) 联签

**最后更新:** 2026-05-28 by 小杜
**下次 review:** W5 Wave 23, CPO 拍 Part 8 后整合
