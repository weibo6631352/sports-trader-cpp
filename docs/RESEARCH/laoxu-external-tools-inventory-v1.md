# 外部 MCP + 工具能力盘点 v1

- Owner: 老徐 (ai-ops-collaboration)
- Last review: 2026-05-28
- 验收人: 老雷 (GM)
- 协作: 老陈 (network-ops), 老姜 (perf), 小苏 (frontend), 老王/小田 (data), 老程/小蒋/小董 (quant)
- 探测环境: macOS Darwin 25.5.0, Claude Code 2.1.153, Homebrew 5.1.12, Python 3.12, Node 18+
- 班底规模: 48 个 agent (.claude/agents/01-48)
- MCP 当前状态: **零启用** (`claude mcp list` -> No MCP servers configured)

---

## 0. TL;DR (老雷看这一段就够)

**已埋没的能力 (最严重 3 项):**
1. **MCP 整个体系零启用** — Claude Code 跑了 29 次启动, 一个 MCP server 都没接. chrome-devtools / playwright / filesystem 这些跟我们 dogfood + frontend + e2e 直接对口的, 白白闲置.
2. **Playwright 已经装在 `/opt/homebrew/bin/playwright`** — 但没人调用. 小苏 (#12 frontend) / 小宫 (#48 dogfood) 该用而没用.
3. **本机量化栈空白** — Python 3.12 在, 但 numpy/pandas/polars/statsmodels 一个都没装. 小程 (#19) / 小蒋 (#20) / 小董 (#23) 预研无米下锅.

**最高 ROI 的 3 个 MCP server / 工具 (建议本 Sprint 立即上):**

| 排名 | 名称 | 类型 | 给谁用 | ROI | 上线成本 |
|---|---|---|---|---|---|
| 1 | **chrome-devtools MCP** (`@modelcontextprotocol/server-puppeteer` 或 `chrome-devtools-mcp`) | MCP | 小苏 #12, 小宫 #48, 老姜 #39 (perf trace) | 极高 — Polymarket UI 行为对照 / 抓 CDP / e2e | 1 行 `claude mcp add` |
| 2 | **filesystem MCP** (`@modelcontextprotocol/server-filesystem`) | MCP | 全员 (尤其老郑 #40 doc-curator) | 高 — 跨 agent 共享 docs/ 状态, 减少重复 Read | 1 行配置 |
| 3 | **DuckDB + Polars (本地)** | 工具 | 老王 #24, 小田, 老程 #19, 小蒋 #20 | 极高 — 回测 + parquet 切片, 已在 ADR/S1-024 路线图 | `brew install duckdb && pip install polars pyarrow` |

**缺什么 (建议老雷批准采购/自建):**
- **polymarket-mcp** (自建, 包 gamma + clob + data REST/WSS) — 让 agent 直接问 "BTC-Yankees 当前盘口" 不用每次 curl
- **goalserve-mcp** (自建, 包 inplay + livescore + pregame) — 给 #37 老高 (goalserve-api-watch) 当工具
- **sentry MCP** (公开, 给 #11 观测) — 但需要先有 Sentry 项目, 走后置
- **postgres MCP** (公开, 给 #24 老王 dwh) — 等 DWH 落地后再上

---

## 1. Claude Code 原生能力 (现成可用, 不用花钱)

### 1.1 内置 Tool 一览 (Sonnet/Opus 共享)

| Tool | 用途 | 谁该用 | 我们用得怎样 |
|---|---|---|---|
| **Bash** | 跑命令, run_in_background, timeout | 全员 | 用得最多, OK |
| **Read** | 读文件 (含 PDF/Image/Notebook), 支持 offset/limit | 全员 | OK |
| **Edit** | 精确字符串替换, replace_all | 全员 | OK |
| **Write** | 写新文件 / 覆写 | 全员 | OK, 但 CLAUDE.md 禁止主动写 .md (老雷规矩) |
| **Glob** | 文件名 pattern 匹配 | 老周 #16, #17 code-review | 偶用 |
| **Grep** | ripgrep 内容搜索 | 全员 | OK |
| **WebFetch** | 抓 URL, 转 markdown 喂 LLM | 老朱 #34, 老高 #37, 老叶 #32 | **严重不足** — 跨洋 + 我们 API watch 该用而没用 |
| **WebSearch** | 联网搜 (Brave/Google) | 老朱 #34, 老蓝 #44 | 偶用 |
| **Task / TaskCreate** | 派 sub-agent 干活 | 老雷 (GM) / 老黎 #45 / 小阮 #26 | **未用透** — 应该是 agent 协作主通道 |
| **Agent** | 列出可调 sub-agent | 老徐 (我) | 元命令 |
| **Skill** | 调内置 skill (见 1.4) | 全员 | 几乎未用 |
| **Monitor** | 监听 background 进程 stdout 事件 | 老姜 #39 (bench), 老韩 #28 (replay) | 未用 |
| **NotebookEdit** | 改 .ipynb cell | 老程 #19, 小蒋 #20 (回测) | 等本地装 jupyter 后用 |
| **SendMessage** | 跨 agent 异步消息 | 全员 | 未用 — 我建议跟 Task 配合做 RACI 异步分支 |

### 1.2 Sub-agent 召唤机制

- `.claude/agents/{01..48}-{name}.md` — 我们已建 48 个
- Front-matter `name` / `description` / `tools` — `tools` 字段决定 sub-agent 能用哪些 tool, **缩 tool 集 = 缩注意力 + 防越权**
- 召唤方式: 父 agent 用 **Task** tool 派工, 子 agent 在隔离 context 跑, **返回最终 message 给父 agent**, 父 agent 看不到子 agent 写的 .md 文件 (这条系统提示反复强调, 我们 docs 落盘要靠父 agent 复述路径)

我们的实际打法:
- GM 老雷 -> TaskCreate -> 各 owner (我/老姜/老陈/老王 etc.)
- 各 owner -> Task -> IC pool (#35 / #41 / #42 / #43)
- doc-curator #40 老郑 — 唯一被授权"跨 agent 看 docs/"的人 (建议)

### 1.3 run_in_background / Monitor / Bash 长任务

- `Bash(run_in_background: true)` — 起 long-running (bench / replay / log tail / 服务起停)
- `Monitor` — 监听 background 进程, 每条 stdout 当通知触发回调 (适合 "等条件满足"循环)
- **禁止 sleep poll** — 系统会 block 长 sleep, 必须用 Monitor + `until ... do sleep 2; done`

谁该用:
- 老姜 #39 — 跑 perf bench 用 `run_in_background`
- 老韩 #28 — 重放回放 / fuzz 长任务
- 老陈 #03 — tcpdump 抓包 60s 跨洋链路

### 1.4 可用 Skill 清单 (内置, 不用 npm install)

| Skill | 用途 | 我们的场景 |
|---|---|---|
| `claude-api` | 介绍 Claude API + Agent SDK | 老蓝 #44 ai-llm-advisor 必读 |
| `code-review` | code-review 标准流程 | 老周 #16 + 老彭 #17 |
| `security-review` | 安全审计 | 老雪 #27 + 老桂 #06 (签名) |
| `review` | meta-review (review-of-reviews) | 老周 #16 |
| `verify` | 验证类任务标准流程 | 老岳 #38 audit |
| `init` | 项目初始化 | 已过期 |
| `loop` | for-loop 任务模板 | 批量 |
| `schedule` | 定时任务 | 老仓 #10 sre |
| `run` | 跑命令最佳实践 | 全员 |
| `update-config` | 改配置类任务 | 老仓 #10 |
| `keybindings-help` | 帮用户记快捷键 | 边角 |
| `simplify` | 化简代码 | 老岳 #14 modern-cpp-advisor |
| `fewer-permission-prompts` | 减少授权弹窗 | 老仓 #10 |

**老雷建议:** 把 skill 调用作为 IC pool 的"标准动作", agent prompt 写入 "code-review 走 `code-review` skill; security 走 `security-review` skill".

### 1.5 Hooks / settings.json / 权限模式

- `~/.claude/settings.json` (全局) — 我们的 `defaultMode: auto`, `skipDangerousModePermissionPrompt: true` (老雷允许大胆)
- `.claude/settings.json` (项目级) — 目前**空**, 没用
- `.claude/settings.local.json` — local override, 不入 git
- **Hooks** — PreToolUse / PostToolUse / Stop / SubagentStop / Notification — 我们一个都没配
  - 建议加: PreToolUse(Bash) 拦 `rm -rf /` 之类危险命令, PostToolUse(Edit) 自动 clang-format, SubagentStop 给老雷发汇报

---

## 2. MCP 服务清单 + 推荐启用

### 2.1 现状

**`claude mcp list` 输出: 零.** 我们一个都没接.

### 2.2 公开 MCP server (modelcontextprotocol/servers + 社区)

| MCP server | 包名 | 用途 | 跟我们项目相关度 | 推荐? |
|---|---|---|---|---|
| **filesystem** | `@modelcontextprotocol/server-filesystem` | 受限路径读写 | 高 (跨 agent 共享 docs/) | ✅ 强烈推荐 |
| **github** | `@modelcontextprotocol/server-github` | repo / issue / PR | 中 (我们目前未上 GitHub) | ⏸ 等上 GitHub 后接 |
| **postgres** | `@modelcontextprotocol/server-postgres` | SQL 只读查询 | 高 (#24 老王 DWH 后) | ⏸ Sprint-3 |
| **puppeteer** | `@modelcontextprotocol/server-puppeteer` | headless Chrome 自动化 | 高 (Polymarket UI dogfood) | ✅ 推荐 |
| **chrome-devtools (社区)** | `chrome-devtools-mcp` | CDP 直连, 抓 console/network/perf trace | **极高** (Polymarket 是 React SPA, network panel 看 WSS) | ✅✅ 首推 |
| **playwright (微软官)** | `@playwright/mcp` | 浏览器自动化 (优于 puppeteer) | 高 | ✅ 推荐 (替代 puppeteer) |
| **brave-search** | `@modelcontextprotocol/server-brave-search` | 搜索 | 中 (WebSearch 已够) | ⏸ |
| **fetch** | `@modelcontextprotocol/server-fetch` | HTTP fetch | 低 (WebFetch 已有) | ⏸ |
| **slack** | `@modelcontextprotocol/server-slack` | Slack 消息 | 低 (我们不用 Slack) | ❌ |
| **sentry** | `sentry-mcp` (社区) | error tracking 查询 | 中 (#11 观测) | ⏸ 等 Sentry 项目落地 |
| **memory** | `@modelcontextprotocol/server-memory` | 跨会话 knowledge graph | 高 (RACI / ADR 记忆) | ✅ 备选, 跟 doc-curator 重叠 |
| **time** | `@modelcontextprotocol/server-time` | 时区换算 | 低 | ❌ |
| **sequential-thinking** | `@modelcontextprotocol/server-sequential-thinking` | 多步推理脚手架 | 中 | 可选 |

### 2.3 接入方式 (示例)

```bash
# 一次性 (推荐这个先做)
claude mcp add chrome-devtools npx chrome-devtools-mcp@latest
claude mcp add playwright npx '@playwright/mcp@latest'
claude mcp add filesystem npx '@modelcontextprotocol/server-filesystem' /Users/wangweibo/code/sports-trader-cpp
```

或写到 `.claude/settings.json` (项目级, 入 git, 全队共享):

```json
{
  "mcpServers": {
    "chrome-devtools": {
      "command": "npx",
      "args": ["chrome-devtools-mcp@latest"]
    },
    "playwright": {
      "command": "npx",
      "args": ["@playwright/mcp@latest"]
    },
    "filesystem": {
      "command": "npx",
      "args": [
        "@modelcontextprotocol/server-filesystem",
        "/Users/wangweibo/code/sports-trader-cpp"
      ]
    }
  }
}
```

### 2.4 ROI 估计 (按 Sprint-1 落地价值)

| MCP | 节省时间/Sprint | 减少错误率 | 解锁能力 |
|---|---|---|---|
| chrome-devtools | ~8 h (小苏+小宫 手测 Polymarket UI) | -- | 抓 WSS 帧, 看实际 message 节奏 (老陈很需要) |
| playwright | ~5 h (e2e) | 自动化 regression | 端到端测试 |
| filesystem | ~3 h (跨 agent docs 同步) | 减少 read 重复 | doc-curator 提效 |

---

## 3. 本地调试工具 (探测结果)

### 3.1 已装 (✅)

| 工具 | 路径 | 谁用 | 备注 |
|---|---|---|---|
| `lldb` | `/usr/bin/lldb` | 全员 C++ debug | macOS 原生, 替代 gdb |
| `dtrace` | `/usr/sbin/dtrace` | 老姜 #39 perf | macOS 原生 (需 sudo + SIP 关) |
| `tcpdump` | `/usr/sbin/tcpdump` | 老陈 #03 抓包 | macOS 原生 |
| `curl` | `/usr/bin/curl` | 全员 API 调试 | 基础 |
| `jq` | `/usr/bin/jq` | 老王 #22, 老朱 #34 | JSON 处理 |
| `dtruss` (隐含) | macOS strace 替代 | 老仓 #10 sre | 需 sudo |

### 3.2 缺 (❌, 建议补)

| 工具 | brew/方式 | 谁要 | 优先级 |
|---|---|---|---|
| `valgrind` | macOS 不官方支持; Linux `brew install valgrind` | 老姜 #39 内存检查 | Linux 部署时必装 |
| `gdb` | `brew install gdb` (macOS 要 codesign) | 喜欢 gdb 的 IC | 低 (lldb 够) |
| `perf` | Linux only (`apt install linux-tools-common`) | 老姜 #39 prod perf | **生产 Linux 必装** |
| `rr` | Linux only (`apt install rr`) | 老韩 #28 record-replay | 高 (复现态难 bug) |
| `mitmproxy` | `brew install mitmproxy` | 老陈 #03 中间人调试 Polymarket | **建议立即装** |
| `wireshark` | `brew install --cask wireshark` | 老陈 #03 抓包 GUI | 高 |
| `bpftrace` | Linux only | 老韩 + 老姜 | Linux 部署后 |
| `Instruments` | Xcode 自带 | macOS 性能 profile | 已有 |

**Sanitizers (编译期, 不是命令行工具):**
- ASAN/UBSAN/TSAN — 通过 `-fsanitize=address,undefined,thread`, 在 CMake 加 build type
- 老周 #16 + 老岳 #38 应该把 sanitizer build 列为 CI 默认 (老岳建 audit checklist)

### 3.3 调用范式 (通过 Bash tool)

```bash
# lldb 跑 batch 命令 (非交互)
lldb -o "run" -o "bt" -o "quit" ./build/sports_trader

# tcpdump 抓 60s Polymarket WSS, 后台跑
tcpdump -i any -w /tmp/poly.pcap 'host clob.polymarket.com' -G 60 -W 1
# 用 run_in_background: true

# dtrace 跟踪系统调用 (macOS)
sudo dtrace -n 'syscall:::entry /execname=="sports_trader"/ { @[probefunc] = count(); }'
```

---

## 4. 浏览器 / 前端调试

### 4.1 已装

- **Playwright** — `/opt/homebrew/bin/playwright` ✅ (没人用!)
- **Google Chrome** — `/Applications/Google Chrome.app` ✅

### 4.2 关键能力

| 能力 | 工具 | 用例 |
|---|---|---|
| **CDP (Chrome DevTools Protocol)** | Chrome `--remote-debugging-port=9222` | 老陈 / 老姜抓 WSS 帧 + perf trace |
| **headless 自动化** | Playwright (已装) | 小苏 UI 测试, 小宫 e2e |
| **MCP chrome-devtools** | `chrome-devtools-mcp` (待装) | LLM 直接看 console/network |

### 4.3 推荐: chrome-devtools MCP

为什么不是 Puppeteer? 因为 `chrome-devtools-mcp` 是社区版直连 CDP, **能让 Claude 直接读 console.log / network 帧 / performance trace**, 比手敲 Puppeteer script 快 10 倍.

### 4.4 起手式 (小苏 #12 + 小宫 #48 接收)

```bash
# 1. 起一个带 CDP 的 Chrome
/Applications/Google\ Chrome.app/Contents/MacOS/Google\ Chrome \
  --remote-debugging-port=9222 \
  --user-data-dir=/tmp/chrome-dogfood &

# 2. Playwright 连上去跑 e2e
npx playwright codegen polymarket.com  # 录制
```

---

## 5. 数据工具

### 5.1 现状

| 工具 | 已装? | 路径 |
|---|---|---|
| `jq` | ✅ | `/usr/bin/jq` |
| `yq` | ❌ | `brew install yq` |
| `xq` | ❌ | `pip install yq` |
| `duckdb` | ❌ | `brew install duckdb` |
| `csvkit` | ❌ | `pip install csvkit` |
| Python | ✅ | 3.12 |
| numpy/pandas/polars | ❌ | 一个都没装! |

### 5.2 建议立即装 (一行)

```bash
brew install duckdb yq
pip3 install polars pyarrow pandas numpy statsmodels scipy lightgbm csvkit jupyter
```

谁要:
- **老王 #24** (data-warehouse) — duckdb + parquet 是核心
- **小田 #22** (data-etl) — parquet I/O
- **老程 #19** (signal research) — polars + statsmodels + lightgbm
- **小蒋 #20** (backtest) — polars + jupyter
- **小董 #23** (data-stats) — statsmodels + scipy

### 5.3 DuckDB 在我们项目的杀手锏

- 直接查 parquet (不入库): `duckdb -c "SELECT * FROM 'data/trades_*.parquet' WHERE symbol='LAL-BOS'"`
- 嵌入 C++ (libduckdb) — 老王评估
- Polymarket gamma `markets.csv` 直接 SQL — 已有 `docs/RESEARCH/data/laochen-network-bench-polymarket-gamma-markets.csv`

---

## 6. 网络 / API 工具

| 工具 | 已装? | 装法 | 谁用 |
|---|---|---|---|
| `curl` | ✅ | -- | 全员 |
| `httpie` | ❌ | `brew install httpie` | 老陈 #03 (好读) |
| `websocat` | ❌ | `brew install websocat` | **老陈必装** (WSS 命令行) |
| `grpcurl` | ❌ | `brew install grpcurl` | 暂不用 (我们无 gRPC) |
| `hey` | ❌ | `brew install hey` | 老姜 #39 |
| `wrk` | ❌ | `brew install wrk` | 老姜 #39 (HTTP 负载) |
| `k6` | ❌ | `brew install k6` | 老姜 #39 (scriptable load) |
| `mitmproxy` | ❌ | `brew install mitmproxy` | 老陈 #03 调试 Polymarket REST/WSS |
| `tcpdump` | ✅ | -- | 老陈 #03 |
| `nc` (netcat) | ✅ | (隐含) | 边角 |

**Sprint-1 立即装 (老陈优先):** `brew install websocat mitmproxy httpie hey`

WSS 命令行示例 (老陈给老叶 #07 polymarket-protocol-expert 用):
```bash
websocat 'wss://ws-subscriptions-clob.polymarket.com/ws/market' \
  --ping-interval 30 \
  -E
# 接 -E 自动 echo, 看 server push 节奏
```

---

## 7. 量化 / 数据科学栈 (非 C++ 预研用)

### 7.1 现状 (探测): **全空**

Python 3.12 在, 但 `import numpy` ModuleNotFoundError.

### 7.2 建议虚拟环境 (不污染系统)

```bash
cd /Users/wangweibo/code/sports-trader-cpp
python3 -m venv .venv-research
source .venv-research/bin/activate
pip install numpy pandas polars pyarrow statsmodels scipy lightgbm jupyter ipykernel matplotlib seaborn
```

加 `.venv-research/` 到 `.gitignore`.

### 7.3 用例 (按 agent)

| Agent | 工具组合 | 任务 |
|---|---|---|
| 老程 #19 signal-research | polars + statsmodels + lightgbm + jupyter | 信号挖掘 |
| 小蒋 #20 backtest | polars + pyarrow + jupyter | 回测引擎原型 (C++ 实现前的探路) |
| 小董 #23 data-stats | scipy.stats + statsmodels | 统计显著性 / 假设检验 |
| 老木 #31 ml-engineer | lightgbm + sklearn (后续 pytorch) | 模型训练 (offline) |
| 老柳 #21 microstructure | polars + matplotlib | 微观结构画图 |

### 7.4 边界 (CPO 老郭/老周已确认)

- **生产决策路径必须 C++**, Python 仅限**离线预研 + 回测原型 + 数据探索**
- 任何 Python 产出最终要被 #16 老周 review, 转 C++ 落地

---

## 8. 观测性栈

### 8.1 现状: **全空**

`prometheus / grafana / loki / tempo / otelcol / bpftrace` 一个都没装.

### 8.2 推荐 (老乌 #11 observability-engineer 立项)

| 组件 | 用途 | macOS dev / Linux prod |
|---|---|---|
| **Prometheus** | metrics 拉取 + tsdb | `brew install prometheus` / `apt install` |
| **Grafana** | 看板 | `brew install grafana` / docker |
| **Loki** | 日志聚合 | docker |
| **Tempo** | trace 存储 | docker |
| **OpenTelemetry Collector** | 统一采集 | `brew install opentelemetry-collector` |
| **eBPF / bpftrace** | 内核态低开销 trace | Linux only |

**Sprint-1 不立即做** (老雷已批 perf budget 但观测分阶段). 建议 Sprint-2 启动.

### 8.3 跟 C++ 集成 (老乌 + 老姜)

- `prometheus-cpp` (header-only client) — 暴露 `/metrics` endpoint
- OpenTelemetry C++ SDK — trace export
- 自研 metrics ring buffer + 独立 flush 线程 (老姜路线) — 决策内环 < 500us 不能被 metrics 拖

---

## 9. 缺什么 + 自建 MCP 候选

### 9.1 给老雷的采购/自建清单

#### 9.1.1 必须自建 (没人做过, 我们独家)

| MCP server | 谁建 | 功能 | 价值 |
|---|---|---|---|
| **polymarket-mcp** | 老叶 #07 + 老木 #31 (C++ 封装) | gamma/clob/data REST + WSS 抽象成 MCP tool | agent 不用 curl, 直接 `mcp__polymarket__get_market(token_id)` |
| **goalserve-mcp** | 老高 #37 | inplay/livescore/pregame XML 转 MCP | 老高的本职工作 |
| **ledger-mcp** | 老彭 #38 audit + 老桂 #06 | 查本地 trade ledger / 签名记录 | 审计回溯 |

#### 9.1.2 公开 MCP, 等条件成熟接

| MCP | 等什么 |
|---|---|
| postgres-mcp | DWH 落地 (Sprint-3) |
| github-mcp | 我们还没上 GitHub repo |
| sentry-mcp | 等 #11 观测项目 |
| memory-mcp | 跟 doc-curator 重叠, 先观察 |

#### 9.1.3 工具补齐 (立即)

```bash
# Sprint-1 一次性补齐 (~5 分钟)
brew install duckdb yq websocat mitmproxy httpie hey wrk
pip3 install polars pyarrow pandas numpy statsmodels scipy lightgbm jupyter csvkit
```

### 9.2 自建 MCP 优先级

- **P0**: polymarket-mcp (Sprint-2 启动, 老叶 owner)
- **P1**: goalserve-mcp (Sprint-2, 老高)
- **P2**: ledger-mcp (Sprint-4, 老彭)

---

## 10. 给各 agent 的推荐工具组合 (按 persona)

| Agent | 必装工具 | 必启 MCP | 必读 Skill |
|---|---|---|---|
| **#01 老周 chief-architect** | -- | filesystem, memory | code-review, review |
| **#02 老韩 hot-path** | lldb, Instruments, perf(Linux) | -- | simplify |
| **#03 老陈 network** | tcpdump, websocat, mitmproxy, wireshark | chrome-devtools | -- |
| **#04 老史 serialization** | lldb | -- | simplify |
| **#05 老孟 persistence** | duckdb, lldb | postgres (后) | -- |
| **#06 老桂 crypto-signing** | openssl, lldb | -- | security-review |
| **#07 老叶 polymarket-proto** | websocat, curl, jq | polymarket (自建) | -- |
| **#08 老段 sports-market** | -- | goalserve (自建) | -- |
| **#09 老韩 risk** | duckdb, polars | -- | -- |
| **#10 老仓 linux-sre** | tcpdump, dtrace, lldb | -- | schedule, update-config |
| **#11 老乌 observability** | prometheus, grafana, otelcol, bpftrace | -- | -- |
| **#12 小苏 frontend** | playwright | chrome-devtools, playwright | -- |
| **#16 老周 chief-arch-reviewer** | -- | memory | code-review, review |
| **#17 老彭 code-quality** | clang-tidy, clang-format | -- | code-review |
| **#19 老程 signal-research** | python+polars+statsmodels+lightgbm+jupyter | -- | -- |
| **#20 小蒋 backtest** | polars, jupyter, duckdb | -- | -- |
| **#22 小田 data-etl** | duckdb, polars, pyarrow, jq, csvkit | postgres (后) | -- |
| **#23 小董 data-stats** | scipy, statsmodels, jupyter | -- | -- |
| **#24 老王 data-warehouse** | duckdb, polars, pyarrow | postgres (后) | -- |
| **#27 老雪 security** | -- | -- | security-review |
| **#28 老韩 test-replay** | lldb, rr(Linux) | -- | -- |
| **#34 老朱 api-watch** | curl, websocat | fetch | -- |
| **#37 老高 goalserve-watch** | curl, websocat, xq | goalserve (自建) | -- |
| **#38 老岳 audit** | clang-tidy, sanitizers | -- | verify |
| **#39 老姜 perf** | hey, wrk, k6, perf, dtrace, Instruments | -- | -- |
| **#40 老郑 doc-curator** | -- | filesystem, memory | -- |
| **#44 老蓝 ai-llm-advisor** | -- | -- | claude-api |
| **#48 小宫 dogfood** | playwright | chrome-devtools, playwright | -- |

---

## 11. 行动项 (S1-024 落地)

| # | 动作 | Owner | Deadline | 状态 |
|---|---|---|---|---|
| A1 | `brew install duckdb websocat mitmproxy httpie hey wrk yq` | 老仓 #10 (代全员) | 2026-05-29 | TODO |
| A2 | `pip install polars pyarrow statsmodels lightgbm jupyter` (venv) | 老程 #19 | 2026-05-29 | TODO |
| A3 | `claude mcp add chrome-devtools` + `playwright` + `filesystem` | 老徐 (我) | 2026-05-29 | TODO |
| A4 | `.claude/settings.json` 项目级 MCP 配置, 入 git | 老徐 | 2026-05-30 | TODO |
| A5 | polymarket-mcp 自建立项 ADR 草稿 | 老叶 #07 | 2026-06-03 | TODO |
| A6 | goalserve-mcp 自建立项 ADR | 老高 #37 | 2026-06-03 | TODO |
| A7 | 各 agent prompt 加 "推荐工具组合" 段 | 老徐 + 老郑 #40 | 2026-06-05 | TODO |

---

## 12. 验收标准

- [x] Claude Code 原生 tool / sub-agent / skill / hook 一览全
- [x] 公开 MCP 清单 + 接入示例
- [x] 本地工具 which/version 探测全做完
- [x] 浏览器 / 数据 / 网络 / 量化 / 观测 五大栈分别盘点
- [x] 缺什么清单 + 自建 MCP 候选
- [x] 每个 agent 推荐工具组合表
- [ ] 老雷 review + 批 A1-A7 行动项

---

## 附: 探测原始数据 (2026-05-28)

```
Claude Code 版本: 2.1.153
MCP servers 配置数: 0
全局 settings: ~/.claude/settings.json (defaultMode=auto, effortLevel=xhigh)
项目 settings: .claude/settings.json (不存在)
agent 数: 48
已装关键工具: lldb, dtrace, tcpdump, curl, jq, node, npm, playwright, Google Chrome
未装关键工具: gdb, perf, valgrind, rr, mitmproxy, wireshark, websocat, grpcurl, hey, wrk, k6, duckdb, yq, csvkit, httpie, bpftrace, prometheus, grafana, jupyter
Python 模块: 全空 (numpy/pandas/polars 都没装)
```
