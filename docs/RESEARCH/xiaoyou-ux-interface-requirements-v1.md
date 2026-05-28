# Operator UX 接口需求 + 心流断点 v1 (W4 Wave 21)

- Owner: 小尤 (ux-experience-evaluator)
- Last review: 2026-05-28
- 受 ticket: W4-Wave21 (老雷 GM 派)
- 验收人: 老胡 (pm) + 老雷 (GM)
- 互补文档 (本文不重复, 仅引用):
  - `docs/RESEARCH/xiaosu-ui-wireframe-v0.1.md` (前端: 布局 / panel / 4 色 / 二次确认 / handover / PromQL / chrome-devtools MCP)
  - `docs/RESEARCH/xiaoyou-ux-framework-v1.md` (我 v1: 7 维评分卡 / P0~P4 / persona × day)
  - `docs/RESEARCH/xiaogong-dogfood-playbook-v1.md` (剧本 / 11 边缘场景 / `stcpp-ctl`)
  - `docs/RESEARCH/laohan-riskmanager-design-v0.3.1.md` (RM 5 态)
  - `docs/RESEARCH/laotang-audit-schema-v1.1.md` (audit 字段)
  - `docs/RESEARCH/xiaojiang-paper-trading-engine-v0.2-cpp.md` (paper VirtualFill 链)
  - `docs/RESEARCH/xiaodong-stats-validation-framework-v1.md` (M4.5 hard gate)
  - `docs/RESEARCH/xiaocheng-signal-catalog-v1.md` (signal 字段)

---

## 0. TL;DR (老雷 + 老胡看这一段)

- **本文与小苏 v1 不重叠.** 小苏答的是"屏上画什么 / 怎么画"; 我答的是"用户在什么时刻需要哪一类信息 / 怎么感知 / 别打扰他".
- **核心交付:**
  1. **信息架构 3 层** (P0 顶部不滚动 / P1 主面板 / P2 深钻) — 不是布局, 是"30s 扫一眼 / 5min 看 30s / 必要时点进去" 的注意力梯度
  2. **6 个心流断点** — 什么时机该把 operator 从安静拉出来; **happy path 必须是静音的**
  3. **6 个 affordance + 防误触** — 危险操作的 UI 摩擦力 (RM 解锁 / kill / mode 切换 / Kelly 调参)
  4. **时间感 4 档 + WSS 心跳契约** — operator 对"实时 / 最近 / 今天 / 历史"的语义期待, 没心跳就是"不知道死活"
  5. **异常状态 UI 感受** — 红框 / paper / live watermark / 数据陈旧灰显
  6. **6 个 UX hard ask 给后端 owner** — 不是前端能造的元数据 (reason string / transition_reason / 审计链 id / 改进建议 / confidence + 降级原因 / WSS 心跳)
- **最重要的一句**: operator 不是开发者, 他不读 enum, 他读"人话". 任何 reason 字段如果还要前端做 i18n 字典 → 那是后端偷懒, 不是前端职责.

---

## 1. operator 不是开发者 (一句话设计基线)

operator = M5 实盘后 24/7 操盘手. 与开发者 4 处差异: (a) 被动盯屏 8~12h vs 主动集中; (b) 对术语容忍低 (要"人话") vs 高 (能查 enum); (c) 反应要求 P0 < 5s vs debug 慢可; (d) 怕"错按 / 漏报 / 看错 / 累" vs bug 没修. **结论: UI 不是平铺, 是按注意力梯度组织 — §2 的根本理由.**

---

## 2. Part 1: 信息架构 3 层 (info-arch, 注意力梯度)

operator 一天的注意力不是平的, 是有节奏的. 信息架构必须匹配这个节奏.

### 2.1 P0 顶部 (不滚动, 30s 一扫)

**operator 行为**: 每 30s 扫一眼 (相当于呼吸节奏). 不读, 只用余光扫.

**心理诉求**: "现在没事吧?" 一秒得到答案.

**必看 5 项** (按视觉优先级, 左到右 / 上到下):

| # | 信息 | 形态 | 颜色规则 | 后端字段 |
|---|---|---|---|---|
| P0-1 | ExecutionMode badge | 文字 badge `PAPER` / `LIVE` | PAPER 灰, LIVE 红 (警戒) | `/ops/system_mode` (常量) |
| P0-2 | RM state badge | 文字 + 颜色 | RUNNING 绿 / WARNING 黄 / HALTED 红 / SAFE_MODE 橙 / DRAIN 灰 | `/ops/rm_state` (event push) |
| P0-3 | 今日 PnL | 数字 + 箭头 | + 绿 / - 红 / 0 灰 | `stcpp_l4_daily_pnl_usdc` |
| P0-4 | position 总数 + exposure_pct | 数字 + 进度条 | < 1% 绿 / 1~2% 黄 / > 2% 红 (vs 红线) | `stcpp_l4_market_exposure_ratio` |
| P0-5 | 上一笔 fill 时间 (now - last_fill_ts) | 相对时间 "2m ago" | < 10min 绿 / > 10min 灰 | `stcpp_l5_last_fill_ts` |

**关键设计点**:

- **5 项摆在最顶部, 永不滚动** — 即使用户滚到 audit 历史 1 小时前, 这 5 项还在屏幕顶部
- **不是 panel, 是 status bar** — 高度 ≤ 48px, 不占主面板栅格
- **30s 内得不到这 5 项 = UI 失败** (P0-2 RM state push 延迟必须 < 1s, 见 §6 hard ask 1)
- **"上一笔 fill"为什么是 P0**: operator 最常问的问题 — "我系统是不是死了". 没 fill 不一定异常 (可能没机会), 但没 fill **超过 10min 一定要告知**, 不然 operator 不知道是没单还是断了

### 2.2 P1 主面板 (主屏中部, 5min 看 30s)

**operator 行为**: 每 5min 把视线从 P0 status bar 下移到主面板, 看 30s, 关心趋势.

**心理诉求**: "今天大盘怎么样?" / "信号哪里来的多?" / "哪个 gate 落后?"

**5 个 panel** (与小苏 v1 D1 主屏 12 panel 互补, 这里说"为什么 operator 想看", 不是布局):

| Panel | 为什么 operator 需要 | 不放在 P0 的理由 | 后端字段 (见 §8) |
|---|---|---|---|
| PnL 时序曲线 (1h / 24h / 7d 切换) | 趋势比即时数字重要; 趋势告诉他"是不是进入异常区" | P0 已有"今日 PnL"数字, 趋势是 5min 关心 | `stcpp_l4_pnl_timeseries` |
| 7 hard gate 进度条 (M4.5 解锁状态) | M4.5 卡点是 paper → live 的唯一闸门, operator 需要知道"还差几关" | 不是即时态, 隔日演变 | `/ops/m45_gates` (小董 §6 hard ask) |
| 信号 trigger 频率热力图 (小时 × sport) | operator 心里"今天信号多不多"; 帮他判断是否是机会日 | 累计指标, 非即时 | `stcpp_l3_signal_fire_total` by hour × sport |
| LiveSection 分布饼图 (Live / Soon / Delayed / Closed / Future) | operator 需要知道"现在有多少市场在跑" — 周末 vs 平时差异大 | 市场结构, 非紧急 | `/ops/section_distribution` |
| RM reject 流瀑布 (21 enum 频率柱, 异常 sub_reason 高亮) | "今天为什么没下单" 的根因图; reject_rate 是 operator 的"情绪指示器" | 累计, 异常 sub_reason 才进 P0 toast | `stcpp_l4_reject_total{reject_code,sub_reason}` |

**关键设计点**:

- **不要 12 panel 全平铺** — 小苏 v1 D1 是 12 panel, 但我建议 P1 主面板 (status bar 之下) 优先这 5 个, 其他放 P2 钻取 tab
- **panel 之间不要相互依赖才能理解** — 每个 panel 自洽, 不要 "看 A panel 再算 B 才懂 C"
- **趋势 > 即时** — P1 全部是 timeseries / 累计, 不是即时单点

### 2.3 P2 深钻 (必要时 click in, 5~30s 调查)

**operator 行为**: P0 或 P1 看到异常, click 进去查根因.

**心理诉求**: "为什么 reject?" / "这笔 fill 经过哪些 audit?" / "这个市场 spread 是不是出问题?"

**4 个深钻视图**:

| View | 触发入口 | 期望加载延迟 | 后端必须配合 (见 §6 hard ask) |
|---|---|---|---|
| 单笔 fill 详情 (audit_id 全链路) | click P1 audit tail row | < 500ms | 审计链回溯 (小蒋 hard ask 3) |
| audit hash chain 验证 UI | 顶部菜单 "verify audit" | < 2s (按 1h 时间范围) | 老唐 audit schema 已支持 |
| 单 market 微观结构 (book depth + spread + qhl + slippage history) | click P1 任何 market_id | < 1s | 小梁 microstructure 数据 |
| ML feature snapshot (32 feature 可视化) | click 某 signal_id | < 1s | 小邓 ML 推理时落 snapshot |

**关键设计点**:

- **P2 视图允许加载延迟** — operator 已经在调查状态, 1s 内出即可, 不用 < 100ms
- **必须有"返回 P0/P1 主屏"按钮** — 不让 operator 迷路在深钻视图里
- **P2 视图不应该把 operator 困住** — 即使在 P2 视图里, P0 status bar 仍永远可见 (顶部固定)

---

## 3. Part 2: 心流断点 (6 个该打断的时机)

**核心 UX 原则**: **安静的 happy path**. 99% 工作时间 UI 不应该主动打扰 operator. 只在该打断时打断, 别的时候让他静音工作.

### 3.1 6 个心流断点矩阵

| # | 触发 | 严重度 | 通知通道 | UI 表现 | 期望响应时间 | 自动动作 |
|---|---|---|---|---|---|---|
| HB-1 | **RM HALTED** | P0 灾难 | 浏览器 notification + 声音 + 红色全屏 banner + Slack-crit | 全屏红色边框 + 中央 modal 强制 ack | < 5s | RM 已停, audit 已记 |
| HB-2 | **RM SAFE_MODE 进入** | P0 重大 | 浏览器 notification + 橙色 banner + Slack-crit | 顶部橙色常驻 banner + 二次确认才能 dismiss | < 30s | 风控降档 |
| HB-3 | **单笔 PnL > $500 异常波动** | P1 注意 | 黄色 toast (10s 持续) | 顶部右上角 toast, 不强制 ack | < 2min | 仅记录 |
| HB-4 | **WSS disconnect > 60s** | P1 注意 | 灰色 banner "重连中..." | 顶部灰 banner + status bar P0-2 转黄 | < 1min | BackoffPolicy 自动重连 |
| HB-5 | **7 hard gate 单个 fail** | P1 周报关注 | 邮件 daily digest | 灰色徽章 (P1 主面板 7 gate 进度条该格变红) | 隔日看 | 无 |
| HB-6 | **平淡运行** | 无打断 | 无 | 无 | 无 | 无 |

### 3.2 心流断点设计要点

- **HB-1 灾难级**: 浏览器 notification 提前申请权限; 声音用 piano 单音不用警报器; 全屏 12px 红边框, 切 tab 也能从 favicon 红点感知
- **HB-2 重大级**: SAFE_MODE 是"降档跑", 不是停; 用橙不用红; 二次 dismiss 强制 click "I understand"; status bar P0-2 持续橙直到 RUNNING
- **HB-3 注意级**: $500 阈值可配 (operator prefs); 10s 自消失, 不强制 ack
- **HB-4 注意级**: 文案"重连中..."不写 enum; 60s 阈值防狼来了; 依赖 §7 UX-A6 心跳
- **HB-5 周报级**: 不发即时通知, P1 灰徽章 + daily digest; gate fail 是累计指标, 不打扰当下
- **HB-6 静音**: 99% 时间不打扰是核心承诺; P0 绿 + P1 缓更新 + 无 toast/modal/sound

### 3.3 反例 (我们不做)

不弹"每次 fill" toast / 不弹"系统正常"心跳 / 不滚动播 audit log / 不强制 P2 以下 modal ack / 不用红色提醒"非异常但需注意" — 红色只留给 P0 灾难, 否则狼来了.

---

## 4. Part 3: 操作 affordance + 防误触

**核心 UX 原则**: 危险操作必须有摩擦力. operator 凌晨 3 点眯着眼时, "摩擦力"是救命的.

### 4.1 6 个操作 affordance

| # | 操作 | 风险 | UI 设计 | 防误触机制 |
|---|---|---|---|---|
| AF-1 | 紧急 kill (halt RM) | P0 | 红色显眼按钮 (右上角, 不放在主操作区中心) | 二次确认 modal + 输入 "HALT" 字符串 + 3s 倒计时 |
| AF-2 | RM 解锁 (HALTED → RUNNING) | P0 | 隐藏入口 (从顶部菜单进, 不放主屏) | 三签强制 (STRATEGY_DECAYED, 老韩 cpp 已 enforce) + reason 必填 |
| AF-3 | 切换 paper/live mode | P0 | UI **只能显示**, 不能切换 | 真实切换需重 deploy (build-time 锁, 老周架构层 enforce) |
| AF-4 | 暂停某 signal (P0-01 等) | P1 | 蓝色按钮 + signal_id 选择器 | 一次确认 modal + reason 必填 |
| AF-5 | 调 Kelly fraction | P1 | 滑动条 + 实时 preview "Kelly 0.25 → 0.20 (-20%)" | ACK 按钮 + 显示影响范围 (哪些 signal 受影响) |
| AF-6 | 查询历史 (audit / fill) | 无 | 自由搜索 + filter | 无 (查询不会写入, 安全) |

### 4.2 affordance 设计要点

- **AF-1 kill**: 文案 `EMERGENCY HALT` (不写 "Stop"); 输入 "HALT" 字符串而非 click (强制注意力); 3s 倒计时 + Yubikey (对齐小苏 §3.2); 全红视觉
- **AF-2 RM 解锁 (最危险)**: 主屏只有 HALT 没 UNHALT, 解锁从顶部菜单进 (访问深度 +1); 三签 STRATEGY_DECAYED 后端 enforce, UI 显示 "等待第 2/3 签..."; 解锁后 next state 默认 DRAIN 不 RUNNING (再加摩擦)
- **AF-3 paper/live**: UI 不能切换 (P0 红线 ADR R-11); 只显当前 mode + "如需切换重 deploy 找老吴"; 任何"切换 mode" button/link/API 都不该出现
- **AF-4 暂停 signal**: 选择器带 confidence + 最近 1h fire 次数; 暂停时长 1h/4h/24h/手动恢复 — 不允许永久 (防遗忘)
- **AF-5 Kelly**: 滑动条 0.1~0.5, 步长 0.05, 不允许直接输数字 (防 "2.5" 误打成 "0.25"); 实时 preview 基于过去 7 天回测显示 "今日预计 PnL 变化"
- **AF-6 查询**: 无摩擦但要快 (P2 < 1s); 默认范围 1h 上限 7d (避免 30d 压后端)

### 4.3 affordance 排版纪律

危险按钮永远右上角 (视线最后扫到); 安全按钮放主区中心; 危险与安全**绝对不能并排**; 危险颜色红(HALT)/橙(SAFE_MODE) 不与 P1 toast 冲突.

---

## 5. Part 4: 时间感 (operator 对"实时"的语义期待)

operator 的"实时"和工程师不一样. 工程师说"5s 轮询差不多", operator 听到的是"系统 5s 落后, 我是不是看的是 5s 前的数据".

### 5.1 4 档时间语义

| 语义 | operator 心理期待 | 技术实现 | UI 标识 |
|---|---|---|---|
| **实时 (real-time)** | "现在就是这样" | < 1s push (WSS), 不能轮询 | 无标识 (默认) |
| **最近 (recent)** | "刚刚发生的" | < 1min 内, 可轮询 | 数据下方 "2s ago" / "30s ago" |
| **今天 (today)** | "今天累计" | < 24h | 时间范围 "today" |
| **历史 (history)** | "任意时间, 慢可以" | 任意, < 10s 查询返回 | 时间范围 picker |

### 5.2 时间感设计要点

- **"实时" 必须 push** — 用 5s 轮询 ≠ 实时. 见 §6 hard ask 1 (RM state 变化 push) + hard ask 6 (WSS 心跳)
- **"最近" 必须显示相对时间** — operator 看 "2s ago" 比看绝对时间 "14:23:01" 更有"新鲜感"
- **"今天" 必须明确时区** — ET 还是 UTC 还是 CN? 默认 ET (与小苏 v1 §6.2 一致, Polymarket 服务器时区), 但顶部明确显示
- **"历史" 查询必须有 progress indicator** — > 1s 的查询要显示 "loading 1/3 ..." (operator 不知道是不是死了)

### 5.3 WSS 心跳契约 (硬要求)

**痛点**: WSS 没消息 ≠ 断了 (可能是真没事), 但 operator 不知道.

**契约**:
- WSS 必须有 **10s 心跳** (server push, 不是 client ping)
- UI 显示 "online" (绿) 当心跳 < 15s, "online (lagging)" (黄) 当 15s ~ 30s, "offline" (红) 当 > 30s
- 心跳 payload 可以是 `{"type":"heartbeat","ts":1234567890}`, 极小, 不占带宽
- 见 §6 hard ask 6 (后端必须实现)

**没心跳 = operator 不知道是没事还是断了, 这是 UX 失败. 不接受 "没消息就是没事" 的设计.**

---

## 6. Part 5: 异常状态的 UI 感受 (不是 badge, 是整屏感觉)

**核心 UX 原则**: 异常感受必须"整屏可感", 不能藏在小角落.

### 6.1 异常态 3 个整屏感受

**RA-1 RM HALTED — 整屏红色边框**:
- 不是 P0 status bar 的小红 badge, 而是整个浏览器视口外围加 12px 红色边框
- 即使 operator 切到 P2 深钻视图, 边框仍在
- 心理学: operator 的余光会持续感知到红色, 不会忘记"系统在 HALTED"
- 边框可以呼吸 (0.5Hz fade 透明度 60%~100%), 不是闪烁 (闪烁太烦)

**RA-2 paper / live mode — 永久 watermark**:
- paper mode: 灰色 "PAPER" 全屏水印 (透明度 5%, 字号 200px, 居中), 不影响阅读, 但 operator 每次看屏都能感知 "我在 paper 不是 live"
- live mode: 红色 "LIVE" 警戒水印 (透明度 10%, 字号 100px, 右下角斜 30°), 让 operator 永远知道"我现在动的是真钱"
- 这一条比 P0 badge 重要 — paper / live 误判是 P0 红线 (ADR R-11)

**RA-3 数据陈旧 — 单元格灰显示**:
- 任何数据点的 `data_source_ts > 30s` 时, 该单元格背景灰显示 + 文字浅化
- hover 显示 "data stale, last update 35s ago"
- 这不是 "loading" 状态, 是 "data is old, you're looking at history" 的明示
- 与 ADR R-20 4 时间戳契约对应: UI 必须知道 `data_source_ts`, 不能用 `now()` 替代

### 6.2 异常感受设计要点

整屏边框 > 局部 badge (注意力在中央, 边框是余光防漏); watermark 不闪烁 ("提醒存在"非"提醒注意", 闪烁会累); 灰显 > 隐藏 (operator 不知道是没数据还是没显示); 错误态不用 emoji / 卡通 (金融系统, 视觉严肃).

---

## 7. Part 6: 6 个 UX hard ask 给后端 owner (与小苏不重复)

**核心立场**: 这 6 个 ask 不是"前端能自己造的", 是后端必须配合的 UX 元数据. 后端偷懒 → 前端 i18n 字典维护成本爆炸 → UI 体验崩盘.

### 7.1 UX-A1: 老韩 (RM) — RM state 变化 emit event + transition_reason (P0)

后端 emit event 不做前端轮询 (满足 §5 "实时=push"); event payload 含 `transition_reason` string (人话, 非 enum): `"manual_halt"` / `"strategy_decayed_triggered"` / `"goalserve_stale_60s"` / `"drain_complete"`. 前端直接渲染到 toast/banner 不做翻译. 不能让前端做的理由: enum 字典维护成本高 (老韩可能加新 reason 不告知 → P0); reason 上下文在后端. 关联 `laohan-riskmanager-design-v0.3.1.md`.

### 7.2 UX-A2: 老唐 (audit) — audit event 带 human_reason (P0)

audit schema 已有 `reject_code` 21 enum, 但前端不能渲染 enum. event 必须加 `human_reason` string: `STALE_DATA → "数据源 goalserve 滞后 6.2s, 阈值 5s"`; `EDGE_CI_NEGATIVE → "edge CI 下界 -0.5 bps, 低于 0"`; `INSUFFICIENT_BANKROLL → "可用 USDC $234, 需要 $500"`. 前端直接渲染到 P2 audit tail 不做 i18n. 不能让前端做: sub_reason 动态 (age=6.2s 非固定); 前端模板拼接 = 后端 schema 改前端漏改 → "STALE_DATA undefined undefined". 关联 `laotang-audit-schema-v1.1.md` 加字段.

### 7.3 UX-A3: 小蒋 (paper) — VirtualFill 带审计链回溯 (P1)

每笔 VirtualFill 带链路 id: `signal_id → decision_audit_id → sign_audit_id → fill_audit_id`. P2 视图 click 一键展开 4 个 id 串联, 每个可跳转 audit 详情. 不能让前端做: 前端拿不到完整链, 需后端 paper engine 主动落 4 个 id; 没链, operator 调查 "为什么这笔 fill" 要 grep 4 次. 关联 `xiaojiang-paper-trading-engine-v0.2-cpp.md` VirtualFill schema.

### 7.4 UX-A4: 小董 (M4.5 gate) — gate fail 带 suggestion (P1)

7 hard gate 任一 fail 时给 `suggestion` string (可执行建议): Sharpe 0.4 < 0.5 → `"建议: 增加样本到 200+; 降低 Kelly 到 0.20; 暂停 confidence<0.6 信号"`; max_dd 18%>15% → `"建议: 检查 P0-01 最近 fill, 触发 sub_reason=DECAY 可能"`. P1 主面板 7 gate 进度条 hover 显示, daily digest 邮件直接渲染. 不能让前端做: suggestion 需后端统计知识 (Sharpe vs sample size); 前端模板 = gate 改前端漏改 → 错误建议. 关联 `xiaodong-stats-validation-framework-v1.md` 7 gate.

### 7.5 UX-A5: 小卢/小程 (signal) — signal 带 confidence + degrade_reason (P1)

每个 signal emit 带 `confidence` (float 0~1, ML 置信度) + `degrade_reason` (string, 可选, 解释"为什么没下单"): `NBA-P0-01 confidence=0.8 没下单 → "edge_bps 4.2 低于阈值 5.0"`; `MLB-P0-03 confidence=0.3 → "confidence 0.3 低于阈值 0.5, signal 抑制"`. UI 信号热力图 hover 显示, audit 显示 "SIGNAL_DEGRADED" event 而非 "skipped". 不能让前端做: 后端决策逻辑前端不知道阈值; "skipped"=黑盒 operator 无法 debug. 关联 `xiaocheng-signal-catalog-v1.md`.

### 7.6 UX-A6: 老周 (架构) — WSS gateway 10s 心跳 (P0)

前端连的 WSS gateway (不是 Polymarket WSS) 主动 push 10s 心跳: `{"type":"heartbeat","server_ts":<unix_ms>}`. 前端用 `now() - server_ts` 显 status bar P0-2 "WSS online (lag 2s)" / "lagging (lag 18s)" / "offline (no hb 35s)". 不能让前端做: client-side ping 不能区分"网络问题"vs"后端死了" — 必须 server push; 没心跳 → §5.3 时间感失败. 关联 `laozhou-architecture-v0.5.md`.

### 7.7 UX hard ask 汇总表

| # | owner | ask | 优先级 | 文档 |
|---|---|---|---|---|
| UX-A1 | 老韩 | RM state event + transition_reason | P0 | `laohan-riskmanager-design-v0.3.1.md` |
| UX-A2 | 老唐 | audit event + human_reason 字段 | P0 | `laotang-audit-schema-v1.1.md` |
| UX-A3 | 小蒋 | VirtualFill 4 个 audit id 链 | P1 | `xiaojiang-paper-trading-engine-v0.2-cpp.md` |
| UX-A4 | 小董 | gate fail + suggestion 字段 | P1 | `xiaodong-stats-validation-framework-v1.md` |
| UX-A5 | 小卢/小程 | signal + confidence + degrade_reason | P1 | `xiaocheng-signal-catalog-v1.md` |
| UX-A6 | 老周 | WSS 10s 心跳 | P0 | `laozhou-architecture-v0.5.md` |

---

## 8. Part 7: 与小苏 v1 的边界 (再三明确)

| 维度 | 小苏 v1 | 小尤 v1 (本文) |
|---|---|---|
| 布局 / panel 编号 | owner (12 panel × 5 dashboard) | 不参与, 只说"按注意力 P0/P1/P2 分层" |
| 颜色规则 | owner (4 色法) | 引用小苏, 不重新定义 |
| 二次确认 modal 设计 | owner (3s 倒计时 + Yubikey) | 强化"为什么需要" + 防误触原理 (§4) |
| handover 协议 | owner (`stcpp-ctl handoff`) | 不参与, 引用小苏 §6.3 |
| PromQL 草稿 | owner (54 panel × 1~3 query) | 不参与, 引用小苏 §8 |
| chrome-devtools MCP 用法 | owner (5 个用例) | 不参与 |
| **信息架构注意力梯度** | — | **owner (§2 P0/P1/P2)** |
| **心流断点 6 时机** | — | **owner (§3 HB-1 ~ HB-6)** |
| **operation affordance 防误触原理** | — | **owner (§4 AF-1 ~ AF-6)** |
| **时间感 4 档语义** | — | **owner (§5)** |
| **异常 UI 感受 (整屏红框 / watermark / 灰显)** | — | **owner (§6 RA-1 ~ RA-3)** |
| **后端 UX 元数据 hard ask** | — | **owner (§7 UX-A1 ~ UX-A6)** |

**冲突时谁说了算**:
- 布局 / 视觉 / 技术契约 → 小苏
- 注意力梯度 / 心流 / affordance / 时间感 / 异常感受 / 后端元数据 → 小尤
- 都不确定时 → 找老胡协调

---

## 9. 开放问题 (待跟进)

| # | 议题 | 待定 | 跟进 |
|---|---|---|---|
| OQ-1 | RM state event push 接口 (WSS 还是 SSE 还是 EventSource)? | 小苏 v1 用 SSE, 我同意 | @老周 + @小苏 |
| OQ-2 | watermark 不影响 chrome-devtools MCP 截图回归? | 需小宫 dogfood 验证 | @小宫 |
| OQ-3 | 整屏红框在 Grafana iframe 嵌入时是否可控? | 小苏 §9 OQ-5 同问 | @小苏 |
| OQ-4 | `human_reason` / `suggestion` / `degrade_reason` 语言? 默认中文还是英文? | 我倾向中文 (operator 国内值班), 老雷决策 | @老雷 |
| OQ-5 | 7 hard gate suggestion 字段是后端硬编码还是配置? | 倾向配置 (gate 变化频繁) | @小董 + @老韩 |
| OQ-6 | WSS 心跳带宽估算 (跨洋 10s × payload ~50 bytes) | ~5 bytes/s, 可忽略 | @老姜 |

---

## 10. 验收 + 下一步

### 10.1 v1 验收 (老胡 + 老雷)

- [ ] §2 P0/P1/P2 3 层与小苏 v1 D1 panel 布局对齐 (status bar + 主面板 + 深钻)
- [ ] §3 6 心流断点与小苏 v1 §4 P0~P4 告警分级对齐
- [ ] §4 6 affordance 与小苏 v1 §3 控件清单对齐 (HALT / DRAIN / RESUME-ACK / 等)
- [ ] §5 时间感 4 档与 ADR R-20 4 时间戳契约对齐
- [ ] §6 异常感受 (red border / watermark / 灰显) 进小苏 v0.2 wireframe
- [ ] §7 6 UX hard ask 各 owner 回话 (P0 ask 7 天内, P1 ask 14 天内)

### 10.2 v2 计划 (Sprint-2 中)

1. **W2-1**: §7 6 UX hard ask 各 owner 回话, 进 PR
2. **W2-2**: 与小宫 dogfood 联合跑 §3 6 心流断点 (mock 触发 + 体感评分)
3. **W2-3**: 与小苏联合跑 §6 异常感受 (red border / watermark) 视觉验证
4. **W2-4**: 跨 agent 沟通体验调研 — 6 UX hard ask 各 owner 配合度 retro

### 10.3 跨人依赖

- @小苏: v0.2 wireframe 落 §2 信息架构 (status bar + 5 panel 主面板 + 4 深钻 view)
- @老韩 / 老唐 / 小蒋 / 小董 / 小卢/小程 / 老周: §7 6 UX hard ask 各 owner 回话
- @小宫: §3 6 心流断点 dogfood 体感评分
- @老胡: §9 OQ-4 中英文语言决策上呈老雷

---

**END v1.** 与小苏 v1 完全互补, 不重叠. 等老胡 + 老雷 review, v2 在 Sprint-2 中期出.
