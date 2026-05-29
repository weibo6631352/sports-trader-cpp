# ADR-041: 前端技术栈 — SolidJS + TypeScript + Vite (推翻"纯静态不引 bundle")

- **ID:** ADR-041
- **owner:** 老郭 (架构评审 + 顾问团协调) — 出 ADR 记录; **决策人: GM 老雷 (老板拍板)**
- **last_review:** 2026-05-29
- **status:** **Accepted**
- **类别:** 前端工程栈决策 — 推翻先前「前端纯静态 HTML/ES module, 不引 node_modules/bundle step」约定
- **影响边界:** 仅前端独立静态应用;**不影响后端 / 热路径 C++,不影响「生产无 Python / 无 Rust」政策**

---

## §0 决策摘要 (TL;DR)

1. **前端采用 SolidJS + TypeScript + Vite。** 开发 / 构建用 Node + Vite,**不用 Python (去 `serve.py`)**。产物为静态文件,静态部署。
2. **推翻先前「纯静态 HTML/ES module + 不引 bundle」约定。** 该约定见于前端 v1 spec (`xiaosu-w10-w1-frontend-ui-v1-spec.md`,原拟 React/原生 fetch/零依赖) 与 v3 评审落地的 `frontend/serve.py` + 原生 ES module。理由是「本地优先 + 跨洋带宽」。**老板 2026-05-29 决议推翻** —— 原生三件套太 low,上框架。
3. **缓解原「跨洋带宽」顾虑** (见 §3): SolidJS 运行时极小 + Vite 构建产物精简 + 一次构建静态服务 + `node_modules` 进 `.gitignore` 不入仓。
4. **Rust/WASM 作为未来备选记录** (§4)。老板「Rust 也比 Python 强」系选型偏好表达,本次明确选 **TS 栈**,Rust/WASM 不在本轮。
5. **已落地:** 前端目录已迁 Vite + TSX (`index.html` → `/src/index.tsx`,`package.json` / `tsconfig.json` 就位),`serve.py` 标记待移除。本 ADR 为正式化记录。

---

## §1 老板 verbatim (决策依据)

> **老板 (2026-05-29):** 前端上框架 (原生三件套太 low),选定 **SolidJS + TypeScript + Vite**,不用 Python (去 `serve.py`)。
>
> **老板 (选型偏好补充):** 「Rust 也比 Python 强」。

**解读 (老郭):** 第一句是本次明确决策 — TS 栈。第二句是选型价值取向表达 (相对 Python 的偏好),本轮落地为 TypeScript;Rust/WASM 路线作为未来备选记录在 §4,不在本轮实施。

**决策机制:** 依 CLAUDE.md §6「产品方向: 老钱 + 老雷联决」+ 本项为老板直接拍板,GM 老雷为决策人,老郭出 ADR 记录 + 架构边界守护。

---

## §2 为何新立 ADR-041 而非改 ADR-037 (老郭口径澄清)

GM 派单口径称「ADR-037 原定前端纯静态」。**经核 (老郭),此引用有偏差:**

- **ADR-037** 实际是 `2026-05-29-data-model-strategy-vendor-agnostic.md` —「数据/模型战略 — 信息源中立 + 自估赔率 + AI 量化模型」,**与前端栈无关**。
- **前端「纯静态 / 不引 bundle」约定**实际散落于:① 前端 v1 spec (小苏, 原拟 React + 原生 fetch + 「零依赖原则」);② ADR-029 (前端模块归属:独立 package.json / Vite 构建链 / 静态部署);③ v3 评审落地的 `frontend/serve.py` + 原生 ES module 实现。**无单独 ADR 锁定此约定。**

**裁定:** 新立 **ADR-041** 锁定前端栈决策,比硬改主题不符的 ADR-037 更干净、更可追溯。ADR-037 内容不动。本 ADR 同时收编 / 取代散落约定中与「纯静态不引 bundle」相关的部分。

> **注:** ADR-029 中「独立 `package.json` / Vite 构建链 / 静态部署 (nginx)」本就预留了构建链空间,与本决策方向一致 —— ADR-041 是把框架从 (原拟) React/原生 收敛到 SolidJS,并明确去 Python serve。

---

## §3 缓解原「跨洋带宽 + 本地优先」顾虑

原约定的核心顾虑是跨洋链路带宽紧 + 本地优先 (curl 即调)。本决策针对性缓解:

| 顾虑 | 缓解措施 |
|---|---|
| 运行时体积 (跨洋首屏) | **SolidJS 运行时极小** (无虚拟 DOM,核心 runtime 量级显著小于 React),编译期细粒度响应,产物小 |
| 构建产物体积 | **Vite 构建 + tree-shaking + 代码分割**,产物精简;生产走一次构建后的静态资源 |
| 带宽 / 本地优先 | **一次构建,静态服务** (本地或就近节点静态托管),运行期无构建依赖,无 CDN 强依赖;观测面板仍 `127.0.0.1` 本地优先,curl 调后端 API 不受影响 |
| 仓库膨胀 | **`node_modules/` + `frontend/dist/` 进 `.gitignore` 不入仓** (已落地, `.gitignore` 已含两行) |
| 开发链 Python 残留 | **去 `serve.py`** (Vite dev server 取代),前端栈与「生产无 Python」政策更彻底对齐 |

> **老郭评审意见:** 跨洋带宽顾虑在「一次构建 + 静态部署 + SolidJS 极小 runtime」组合下成立 —— 带宽成本在构建产物大小,而非框架本身的开发期工具链 (工具链只在开发/构建机跑,不上跨洋链路)。原「不引 bundle」约定其实混淆了「开发期工具链体积」与「运行期产物体积」;真正该控的是后者,Vite 恰恰是控产物体积的工具。**顾虑缓解成立,批准。**

---

## §4 架构边界守护 (老郭, 否决权范围确认)

本决策**不触任何架构红线**,边界确认如下:

1. **不影响后端 / 热路径 C++。** 前端是独立静态应用,`ui/`/`frontend/` 不入 hot path 构建,CMake 不感知 (沿用 ADR-029 隔离)。热路径全 C++20 政策不变。
2. **不破「生产无 Python」政策 (CLAUDE.md §10 语言纪律)。** 恰恰相反 —— 去 `serve.py` 让前端栈与该政策**更**对齐。Node/Vite 是前端独立工具链,非「Python 进生产」,非热路径常驻服务。
3. **不破「禁止 Rust」政策。** 本轮选 TS,不引 Rust;§4.1 的 Rust/WASM 仅作未来备选记录,未来若启用须另立 ADR。
4. **后端 JSON schema 契约不变** (ADR-040 per-token / ADR-038 4 时间戳 + vendor-agnostic 仍是前端消费契约)。前端 TS 类型仅与 JSON schema 一致,不引入 C++ ABI 约束;schema 变更走 ADR + 通知小苏 (最小惊喜)。

### §4.1 未来备选记录: Rust/WASM (不在本轮)

老板「Rust 也比 Python 强」的偏好表达,记录 Rust/WASM 前端路线为**未来备选**:若 SolidJS 路线遇到性能/复杂度瓶颈,可评估 Rust → WASM (如 Leptos/Yew)。**启用须另立 ADR + 老郭评审。** 本轮明确不实施。

---

## §5 落地态与分工

| 项 | 状态 | owner |
|---|---|---|
| `frontend/` 迁 Vite + TSX (`index.html` → `/src/index.tsx`, `package.json` / `tsconfig.json`) | ✅ 已落地 | 小苏 (前端) |
| SolidJS 组件实现 (单屏盯盘终端 v5) | 进行中 | 小苏 |
| `frontend/serve.py` 移除 (Vite dev server 取代) | 待清理 (本 ADR 标记 deprecated) | 小苏 |
| `node_modules/` + `dist/` 进 `.gitignore` | ✅ 已落地 (`.gitignore` 含两行) | 小苏 |
| 前端 v1 spec / ADR-029 「零依赖/原生」措辞同步更新指向 ADR-041 | 待办 | 小苏 + 小米 (doc curator) |
| UX 纪律 (DEMO 标记 / advisory 角标 / 不闪烁) 沿用 v3 评审 §4 | 不变 | 小尤 |

---

## §6 决议要点 (回报 GM)

1. **前端栈定: SolidJS + TypeScript + Vite,去 Python serve。** 推翻「纯静态不引 bundle」约定 (该约定散落于前端 v1 spec / ADR-029 / serve.py,无独立 ADR,本 ADR 收编)。
2. **新立 ADR-041 而非改 ADR-037** — ADR-037 实为数据/模型战略,与前端栈无关 (GM 派单引用偏差,本 ADR §2 澄清)。
3. **跨洋带宽顾虑缓解成立** (SolidJS 极小 runtime + Vite 精简产物 + 一次构建静态服务 + node_modules 不入仓)。
4. **架构边界全部守住** (不影响后端/热路径 C++、不破生产无 Python/无 Rust 政策)。Rust/WASM 列未来备选,本轮不实施。
5. **决策人 GM 老雷 (老板拍板);老郭出 ADR + 边界守护。** 已落地, 本 ADR 正式化。

---

## §7 关联文档

| 文档 | 关联 |
|---|---|
| `docs/RESEARCH/xiaosu-w10-w1-frontend-ui-v1-spec.md` | 前端 v1 spec (原「零依赖/原生」措辞, 被本 ADR 收编更新) |
| `docs/MEETINGS/2026-05-29-frontend-dashboard-v3-design-review.md` | v3 评审 (serve.py + 原生 ES module 落地态) |
| `docs/ADR/2026-05-29-data-model-strategy-vendor-agnostic.md` (ADR-037) | GM 派单误引为前端栈, §2 澄清 — 实为数据/模型战略, 内容不动 |
| `docs/ADR/2026-05-29-adr-040-market-structure-per-token.md` (ADR-040) | 后端 JSON schema 契约 (前端消费对象) |
| ADR-029 (前端模块归属) | 独立 package.json / Vite 构建链 / 静态部署 — 与本决策方向一致 |
| `frontend/` | 落地态 (Vite + TSX) |

---

**最后更新:** 2026-05-29 by 老郭 (#F, 架构评审 + GM 派单 — 前端栈推翻正式化 ADR-041)
