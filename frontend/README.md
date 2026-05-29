# sports-trader-cpp 观测/PnL 看板 v1

**owner:** 小苏 (E 产品业务保障部)  
**last_review:** 2026-05-29  
**关联:** ADR-038 (观测 API), ADR-037 (本地优先)

---

## 技术选型

纯静态 HTML + ES Module JavaScript, 零构建工具, 零 npm 依赖.  
本地用 Python 标准库 `http.server` 或任意静态文件服务器跑起来即可.

理由: ADR-037 本地优先 + 跨洋链路带宽紧, 不引入 node_modules / bundle step.

---

## 本地启动

### 方式 A — Python (推荐, 无需额外安装)

```bash
cd frontend/
python3 serve.py          # 默认 127.0.0.1:3000
python3 serve.py 4000     # 自定义端口
```

浏览器打开: http://127.0.0.1:3000/

### 方式 B — Python 内置 (无 CORS header)

```bash
cd frontend/
python3 -m http.server 3000 --bind 127.0.0.1
```

注意: 方式 B 没有自定义 CORS header, 如果 debug_api 端口不同会跨域报错.  
建议使用方式 A.

### 方式 C — npx serve (如有 Node)

```bash
cd frontend/
npx serve -l 3000 --no-port-switching
```

---

## 连接 debug_api

默认连 `http://127.0.0.1:8080` (对应 `src/stcpp/debug_api/` 后端).

如需修改: 看板右上角点 **设置** 按钮, 修改 API Base URL 后保存.  
设置持久化在 `localStorage`, 刷新后保留.

---

## Stub 模式 (后端未启动时)

URL 加 `?stub=1` 参数使用本地 mock 数据, 全面板可渲染:

```
http://127.0.0.1:3000/?stub=1
```

stub 数据源: `src/stub.js`.  
金额字段均为 JSON number (非 string), 对齐老高提示.

---

## 面板说明

| Tab | 面板 | API Endpoint | 轮询 |
|-----|------|-------------|------|
| PnL/持仓 | 持仓表格 | GET /api/v1/positions | 3s |
| PnL/持仓 | 净 PnL 曲线 | GET /api/v1/pnl/timeseries | 10s |
| PnL/持仓 | 盈亏归因瀑布 | GET /api/v1/pnl/attribution | 10s |
| 风控/拒单 | RM 拒单流 | GET /api/v1/risk/rejects | 5s |
| PAPER-GATE | GM-PAPER-G 门禁仪表 | GET /api/v1/gate/paper | 30s |
| 订单簿 | 全深度订单簿 | GET /api/v1/book/{id} | 2s |
| Metrics | Prometheus 原始文本 | GET /metrics | 15s |
| 状态栏 (顶部常驻) | 系统状态 + WSS + 线程心跳 | GET /healthz + /status | 5s |

---

## 空数据处理

- API 返回 404 → 显示 "未接入"
- API 返回 `has_data: false` → 显示 "未接入"
- 网络不可达 → 显示 "未接入" (不崩溃)
- 后端 stub 数据接口未实现时前端无报错

---

## 4 时间戳字段

对齐 ADR-038 R-20 契约 `event_ts / data_source_ts / ingestion_ts / as_of_ts`.  
字段类型为 epoch_ns int64 (JSON number).  
JS 中 Number 精度上限 2^53 ≈ 9×10^15, epoch_ns ≈ 1.7×10^18 超限, 精度损失约 1024ns.  
显示层用 `Date` 转 ms 渲染, 1024ns 误差可接受.

---

## 验证方式

```bash
# 1. 启动 stub 模式验证前端渲染不报错
cd frontend/
python3 serve.py &
open "http://127.0.0.1:3000/?stub=1"

# 2. 在浏览器 DevTools Console 检查: 无 JS 错误, 无 TypeError
# 3. 逐个切换 Tab 确认各面板正常渲染 (stub 数据填充)

# 4. 如后端已启动, 去掉 ?stub=1 验证真实 API 连通性:
open "http://127.0.0.1:3000/"

# 5. curl 验证现有 stub endpoint
curl -s http://127.0.0.1:8080/healthz | jq .
curl -s http://127.0.0.1:8080/status  | jq .
```

---

## 文件结构

```
frontend/
├── index.html          # 单页入口, ES Module 引入
├── serve.py            # 本地静态文件服务器 (Python 标准库)
├── README.md           # 本文件
└── src/
    ├── api.js          # ADR-038 API client + 工具函数
    ├── stub.js         # 本地 mock 数据 (schema 与 API 对齐)
    ├── panels.js       # 各面板 HTML 渲染函数
    ├── app.js          # 主入口: 轮询 + DOM 管理 + tab 导航
    └── style.css       # 深色量化终端风格样式
```
