# 工具栈安装报告 v1

- Owner: 老吴 (sre-devops-deployment, #10)
- Date: 2026-05-28
- 验收: 老徐 (ai-ops-collaboration) + 老雷 (GM)
- 依据 ticket: Sprint-1 S1-025
- 依据盘点: `docs/RESEARCH/laoxu-external-tools-inventory-v1.md` (A1-A7)
- 执行环境: macOS Darwin 25.5.0, Homebrew 5.1.12, Node v26.0.0, Python 3.12.13, Claude Code 2.1.153

---

## 0. TL;DR (老雷看这一段就够)

- **MCP server**: 2 / 2 全 Connected (chrome-devtools, filesystem) — 走 `.mcp.json` (项目级, 入 git, 团队共享)
- **Python 量化栈**: 11 / 11 全装齐 (含 lightgbm + libomp 系统依赖修复) — `.venv/` 不入 git, 走 `scripts/activate-quant.sh` 激活
- **CLI 工具**: 9 / 9 全装齐 (duckdb / jq / yq / websocat / httpie / mitmproxy / hey / wrk / k6)
- **失败项**: 0
- **遗留坑**: Node v26 + npx 缓存 ESM resolve bug — 已用全局二进制规避, 不影响业务
- **总耗时**: ~25 分钟 (含 brew 10 分钟 + pip 8 分钟 + MCP 联调 3 分钟)

---

## 1. 装了什么 (含版本号)

### 1.1 MCP server (2 个, `.mcp.json` 项目级, 入 git)

| MCP | 命令 | 状态 | 给谁用 |
|---|---|---|---|
| **chrome-devtools** | `npx chrome-devtools-mcp@latest` | Connected | 小苏 #12, 小宫 #48, 老姜 #39, 老陈 #03 |
| **filesystem** | `/opt/homebrew/bin/mcp-server-filesystem /Users/wangweibo/code/sports-trader-cpp` | Connected | 老郑 #40, 跨 agent 共享 docs/ |

配置文件: `/Users/wangweibo/code/sports-trader-cpp/.mcp.json` (无 token / secret, 已审计).

### 1.2 CLI 工具 (9 个, 通过 brew)

| 工具 | 版本 | 给谁用 |
|---|---|---|
| duckdb | 1.5.3 (Variegata) | 老王 #24, 小田 #22, 小蒋 #20 |
| jq | 1.8.1 | 老朱 #34, 老王 #22, 全员 JSON |
| yq | 4.53.2 | 老仓 #10, YAML 配置 |
| websocat | 1.14.1 | 老陈 #03, 老叶 #07 (WSS 命令行) |
| httpie | 3.2.4_10 | 老陈 #03 (易读 HTTP) |
| mitmproxy / mitmdump / mitmweb | 12.2.3 | 老陈 #03 (中间人调试 Polymarket REST/WSS) |
| hey | 0.1.5 | 老姜 #39 (HTTP 简单压测) |
| wrk | 4.2.0_2 | 老姜 #39 (HTTP 高强度压测) |
| k6 | 2.0.0 | 老姜 #39 (scriptable 负载) |
| **libomp** (额外补) | 22.1.6 | lightgbm 系统依赖 |

### 1.3 Python 量化栈 (11 个核心包 + ~120 个依赖, 装在 `.venv/`)

| 包 | 版本 | 给谁用 |
|---|---|---|
| numpy | 2.4.6 | 全员预研 |
| pandas | 3.0.3 | 老程 #19, 小董 #23 |
| polars | 1.41.1 | 老程 #19, 小蒋 #20, 老王 #24, 小田 #22 |
| pyarrow | 24.0.0 | 小田 #22, 老王 #24 (parquet) |
| duckdb (Py 绑定) | 1.5.3 | 老王 #24, 小田 #22 |
| statsmodels | 0.14.6 | 老程 #19, 小董 #23 |
| scipy (随 statsmodels 装) | 1.17.1 | 小董 #23 |
| lightgbm | 4.6.0 | 老程 #19, 老木 #31 |
| scikit-learn | 1.8.0 | 老木 #31 |
| matplotlib | 3.10.9 | 老柳 #21, 老程 #19 |
| jupyter (metapackage) | 1.1.1 | 小蒋 #20, 老程 #19 |
| ipykernel | 7.2.0 | jupyter notebook 内核 |

### 1.4 配置文件 (项目根)

| 文件 | 用途 | 入 git? |
|---|---|---|
| `.mcp.json` | MCP 项目级配置 (团队共享) | 是 |
| `.gitignore` | 已加 `.venv/`, `.venv-*/`, `__pycache__/`, `*.pyc`, `.ipynb_checkpoints/`, `.claude/settings.local.json` | 是 |
| `scripts/activate-quant.sh` | venv 激活脚本 + 版本探测 | 是 (chmod +x) |
| `CLAUDE.md` | 末尾追加 "## 12. 工具栈" 段, 引用本报告 + 老徐报告 | 是 |
| `.venv/` | Python 虚拟环境 | 否 (gitignore) |
| `.claude/settings.local.json` | 本地敏感配置 (token 等) | 否 (gitignore, 预防性, 当前不存在) |

---

## 2. 没装什么 (原因)

**本 ticket 全部清单装齐, 0 失败.** 

老徐报告里列入"可选 / 等条件成熟"的下面这些**有意不装**:

| 没装 | 原因 |
|---|---|
| playwright MCP (`@playwright/mcp`) | 老徐报告 9.1.2 说"chrome-devtools 已能覆盖 90%, playwright 等 e2e 项目立项再上" — 跟老雷确认前不动 |
| github MCP | 项目还没上 GitHub repo |
| postgres MCP | DWH 没落地 (Sprint-3) |
| sentry MCP | 观测项目 #11 老乌 还没起 |
| memory MCP | 跟 doc-curator #40 老郑职责重叠, 先观察 |
| prometheus / grafana / loki / tempo | Sprint-2 由老乌 #11 立项, 本 ticket 不属于范围 |
| valgrind / perf / rr | Linux only, macOS 装不了 — Linux 部署机器到位后再装 |
| wireshark GUI | 命令行 tcpdump / mitmproxy 已能覆盖, 等老陈 #03 提需求 |
| gdb | lldb 已能覆盖 (macOS 原生), 老周 #16 也确认无需 |
| `scipy` 单独装 | statsmodels 拖进来了 1.17.1, 无需重复 |

---

## 3. 怎么验证可用

### 3.1 MCP

```bash
claude mcp list
# 期望:
#   chrome-devtools: npx chrome-devtools-mcp@latest - ✓ Connected
#   filesystem: /opt/homebrew/bin/mcp-server-filesystem ... - ✓ Connected
```

### 3.2 CLI 工具

```bash
duckdb -version           # v1.5.3
jq --version              # jq-1.8.1
yq --version              # v4.53.2
websocat --version        # 1.14.1
http --version            # 3.2.4
hey                       # Usage: hey [options...] <url>
wrk                       # wrk 4.2.0 ...
k6 version                # k6 v2.0.0 ...
mitmproxy --version       # Mitmproxy: 12.2.3 binary
```

### 3.3 Python 栈

```bash
source /Users/wangweibo/code/sports-trader-cpp/scripts/activate-quant.sh
# 自带打印所有 11 个包版本; 任何一个 "未装" 即失败
```

### 3.4 端到端冒烟

```bash
# polars + duckdb + pyarrow 联调
.venv/bin/python -c "
import polars as pl, duckdb, pyarrow as pa
df = pl.DataFrame({'symbol':['LAL-BOS','NYY-BOS'],'price':[0.55,0.42]})
con = duckdb.connect()
print(con.execute('SELECT * FROM df WHERE price > 0.5').fetchall())
"
# 期望: [('LAL-BOS', 0.55)]
```

### 3.5 安全验证

```bash
# .env / .venv / settings.local.json 必须被 gitignore
git check-ignore -v .venv/bin/python .env .claude/settings.local.json
# 期望: 三行都命中 .gitignore 规则
```

---

## 4. 给各 agent 的快速上手命令

### 4.1 小苏 #12 frontend / 小宫 #48 dogfood

MCP chrome-devtools 已就位. 起 Chrome 带 CDP:

```bash
'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
  --remote-debugging-port=9222 \
  --user-data-dir=/tmp/chrome-dogfood &
# 然后 Claude 直接通过 MCP tool 操作 (无需手敲 puppeteer 脚本)
```

### 4.2 老陈 #03 network-ops / 老叶 #07 polymarket-proto

WSS 命令行抓帧:

```bash
websocat 'wss://ws-subscriptions-clob.polymarket.com/ws/market' --ping-interval 30 -E
```

中间人调试 Polymarket REST:

```bash
mitmweb --listen-port 8080
# 然后客户端走 HTTPS_PROXY=http://localhost:8080
```

### 4.3 老程 #19 / 小蒋 #20 / 小董 #23

```bash
cd /Users/wangweibo/code/sports-trader-cpp
source scripts/activate-quant.sh
jupyter lab --notebook-dir=./docs/RESEARCH/notebooks  # 目录可自建
```

### 4.4 老王 #24 / 小田 #22

DuckDB 直查 parquet:

```bash
duckdb -c "SELECT symbol, COUNT(*) FROM 'data/trades_*.parquet' GROUP BY symbol"
```

Python 侧:

```bash
source scripts/activate-quant.sh
python -c "import polars as pl; print(pl.scan_parquet('data/*.parquet').collect().head())"
```

### 4.5 老姜 #39 perf

```bash
# HTTP 压测三件套, 按场景选
hey -n 1000 -c 50 https://gamma-api.polymarket.com/markets        # 简单
wrk -t4 -c100 -d30s https://gamma-api.polymarket.com/markets      # 高强度
k6 run scripts/load-test.js                                       # 脚本化
```

### 4.6 老郑 #40 doc-curator

filesystem MCP 已就位, 可跨 agent 看 `docs/`. 用法在 Claude Code session 里直接调 `mcp__filesystem__read_file` 之类 tool.

### 4.7 全员

`claude mcp list` 看当前 server 状态. 新装走 `claude mcp add` + 同步到 `.mcp.json`. 含敏感字段走 `.claude/settings.local.json` 不入 git.

---

## 5. 后续 (Sprint-2 计划自建 MCP)

按老徐报告 9.1.1 + 9.2 优先级:

| MCP | Owner | Sprint | 价值 |
|---|---|---|---|
| **polymarket-mcp** (P0) | 老叶 #07 + 老木 #31 | Sprint-2 | gamma/clob/data REST + WSS 抽象成 MCP tool, agent 不用 curl |
| **goalserve-mcp** (P1) | 老高 #37 | Sprint-2 | inplay/livescore/pregame XML 转 MCP |
| **ledger-mcp** (P2) | 老彭 #38 + 老桂 #06 | Sprint-4 | 查本地 trade ledger / 签名记录, 审计回溯 |

观测栈 (prometheus / grafana / loki / tempo) 由老乌 #11 在 Sprint-2 立项, 跟我 (老吴) 联合做 docker-compose + systemd unit 部署.

---

## 6. 遗留风险 / 给老雷的小报告

1. **Node v26 + npx 缓存 ESM bug** — `npx @modelcontextprotocol/server-filesystem` 直跑会因 `diff` 包 ESM resolve 失败. 已用全局 `npm install -g @modelcontextprotocol/server-filesystem` + 全局二进制路径规避. **新机器装务必照本配置抄, 不要直接复制 老徐报告 ch2.3 的 npx 写法**. 待 Node 升级修复后简化.

2. **`.mcp.json` 用绝对路径 `/Users/wangweibo/...`** — 团队成员如果 home 路径不同, 必须改. 待 Sprint-2 我整一个 `scripts/setup-mcp.sh` 用 `${PROJECT_ROOT}` 模板化.

3. **mitmproxy 是 cask 安装 (12.2.3)**, 系统级 Python 3.14 (httpie 拖来的) 跟 venv Python 3.12 共存. 不冲突, 但磁盘多了 75MB. 可接受.

4. **本 ticket 修了一处 `.gitignore`** — 加了 `.venv/`, `.venv-*/`, `__pycache__/`, `*.pyc`, `.ipynb_checkpoints/`, `.claude/settings.local.json`. 已 `git check-ignore` 验证 `.env` / `.venv` / `settings.local.json` 全部命中. **`.env` 仍然在 gitignore 第一行, 没动**.

5. **CLAUDE.md 加了 "## 12. 工具栈" 段** — 引用本报告 + 老徐报告. 老雷如果不喜欢可让老郑 #40 doc-curator 改样式.

---

## 7. 验收 checklist

- [x] A1 — brew 装 9 个 CLI 工具 + libomp 系统依赖
- [x] A2 — Python venv + 11 个核心包
- [x] A3 — `claude mcp add chrome-devtools` + `filesystem` 全 Connected
- [x] A4 — `.mcp.json` 项目级, 入 git, 无 secret (审计过)
- [x] `.gitignore` 补 `.venv/` + Claude 本地配置
- [x] `scripts/activate-quant.sh` 可执行, 跑过冒烟
- [x] `CLAUDE.md` 末尾追加工具栈段
- [x] 本报告落盘 `docs/RESEARCH/laowu-toolstack-install-v1.md`
- [ ] A5 — polymarket-mcp ADR (老叶 #07 owner, 截至 2026-06-03)
- [ ] A6 — goalserve-mcp ADR (老高 #37 owner, 截至 2026-06-03)
- [ ] A7 — 各 agent prompt 加"推荐工具组合" (老徐 + 老郑 #40, 截至 2026-06-05)

A5-A7 不在我 (老吴) 的 RACI, 列在这里供老雷盯进度.

---

**完成. 已交付老徐 + 老雷验收.**
