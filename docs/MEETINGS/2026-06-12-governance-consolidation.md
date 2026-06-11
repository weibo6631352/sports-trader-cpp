# 2026-06-12 治理整合 — 模块审计 + 三轮清删

owner: 老雷 (GM) | last_review: 2026-06-12
背景: 老板令「改优化的就优化, 能利用的都利用起来, 实在没用的删了, 模块混乱治理一下」
+「整合治理, 模块是否太散, 职责是否过于分散, 该通用的模块没通用, 比如支持虚拟盘」。

## 审计方法

4 路并行只读侦察: ① 构建图孤儿库 ② 模块内死代码/死旋钮 ③ 闲置资产可利用 ④ 前端死代码。
结论全部入本文; 改动全部 GM 主干落地, 每轮 build+ctest 双绿后 commit。

## 「模块是否太散」的数据回答

- 生产闭包 24 库, 依赖方向清晰 (paper_loop→risk→sizing→pricing), **不散**。
- 真散的是【三层被超越的旧架构并存】: ① signer 三代 (v1 生产 / v52 ed25519 IPC / v62 transformer)
  ② PM client 双抽象 (IPmClient mock+stub, 从未落地) ③ Wave3 出站序列化层 (被 PersistentHttps 超越)。
  → 全删 (轮 3)。
- 「该通用的没通用」已解: **一个引擎双模式** — 同一 paper_loop/daemon 代码, STCPP_EXEC_MODE
  编译期定模式, executor seam (SetExecutor) 运行时换 VirtualExecutor↔LiveExecutorAdapter;
  签名/执行库从「paper 模式独占」改双模式构建 (R-7 隔离改为 seam + R-11 mode_tag 断言 + FATAL guard)。

## 三轮清删 (commit 5b: 1fce7ae3→8c89bae9)

1. **轮 1** (92d96a32 后): kMlBlend 枚举 (ResolveFair 永不产出) / sharp_lag 外推 3 件套 (零读) /
   min_buy_price (被 min_open_fair+near_end 取代)。
2. **轮 2** (d71a62d4): fair/mark 基止损全家桶 8 旋钮 + RelStopShouldHoldWinner + ~200 行
   (hold-to-settlement 反事实判死, 生产恒 0, 实测 500 fill 0 卖出)。
   **★捎带修出 P0 级潜伏 bug**: 旧统一铁律 else 分支把 sel_force_stop 复位 →
   frozen_hard 灾难逃生门结构性不可达。新铁律不覆盖 force_stop, 逃生门恢复。
3. **轮 3** (8c89bae9): signer v52/v62/v53test + IPmClient 抽象层 + net_outbound + 三签 CLI
   (废会签机器) + ed25519/libsodium 整段 — clob_wire L2 HMAC 换 OpenSSL (向量测试验证)。
   ~6 千行, 5 库 target, 1 个 autoconf 外部依赖归零。

## 仓库卫生 (b488ee68 / db827682)

- frontend/node_modules 移出 git (4748 文件, −104 万行索引); .gitignore 补全。
- ml_research/ 36M (已砍大模型的训练样本) + .cache 截图删除。
- 76 个未跟踪工作产物 (RESEARCH/MEETINGS/实验探针) 按归档纪律入 git。

## 测试基线变化

1410 → **1307** all-green (删 ~102 个孤儿测试: v52×15/v62/v53/pm_client/outbound/三签)。
live binary 干净重配置链接 ✅ (单参数切换不受影响)。

## 未完 backlog (能利用的利用起来 — 任务 #12)

按资产审计价值排序: ① FillRateModel::compute_from_clob_book 接 FLB 执行质量门
② CLV positive_rate<0.70 模型失效熔断 ③ WindowMinSeen 反哺 FLB 抄底确信度
④ stats::GateEvaluator (G1-G7 统计门, 全写完零调用) 接 paper→live 晋升评估
⑤ backtest/ 修复 (只回放 book 1/6 输入, 红线#3 破裂 — 修好才能离线验 FLB 参数)。
前端 4 个死文件 (ConfBar/TabNav/EventGrid/GlobalBar) 待删 (低优先)。
