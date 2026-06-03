# ML 校准接线 + 无赔率 edge 现实 — 夜间攻坚记录 (v1)

> Owner: 老雷 (GM) | last_review: 2026-06-03
> 背景: 老板两问 ——「怎么让模型置信度>0、校准过?」+「没赔率这东西还能用吗?我没有其他信息」。
> 老板交代「你看着弄、搬到漂亮合理、没做完不停」后,夜间自主完成本记录所列工作。

---

## 1. 老板问题一:怎么让模型置信度>0 + 校准过?

### 发现的真 bug
`OnnxFairValueModel::predict()` **从不设 `confidence`/`calibrated`** → 走结构体默认 0/false。
原设计注释「W11+ isotonic/conformal 填真值」**从没实现**。后果:哪怕 auto-train 训出好模型,
前端 `modelReady() = calibrated && conf>0` **永远 false → 永远显示「未训练·占位」**。光训练救不了。

### 修复 (commit 5870c3e8) — 训练侧产校准元数据, C++ 加载侧读
- **train_fair_value.py**: 训练后在 **holdout(时序末 20%,防泄漏)** 上评估 → 写 `<onnx>.meta.json`:
  - `confidence` = 判别力 `2·(AUC−0.5)`(AUC 0.5 无技能→conf 0→前端正确保持占位;0.65→0.30;1.0→1.0)
  - `ci_halfwidth` = **ECE 校准误差**(分箱 `|mean(pred)−mean(y)|` = "概率平均偏多少",概率可信带)
    - 不用 `|y−p|` 残差分位:二值标签下那是对【结果】的区间(恒~0.5 宽,无意义)
  - `calib_method` = conformal,附 auc/brier/n_holdout
  - 最终模型仍**全量训练**(部署),holdout 仅估指标
- **OnnxFairValueModel**: 构造时 `LoadMeta` 读 sidecar(极简无依赖 JSON 标量解析)→ predict 填
  confidence/calibrated/calib_method + 非零宽 CI。**fail-safe**:无 meta → conf=0/未校准(降级同 stub,
  前端保持占位)。**纯观测,不改 prob 输出 / 不 gate 交易**(已核实 paper_loop 只把它拷进 QuoteFeatures)。
- **AutoTrain**: 先搬 sidecar 再搬 .onnx(.onnx 作提交点,避免 RefreshModel watcher 在 meta 就位前
  热加载的 race)。
- 测试: ON04 有 sidecar→点亮 / ON05 无 sidecar→降级; T16 cricket 护栏; selftest fixture 带 meta。

### 何时会真亮
auto-train 训出 AUC>0.5 的模型 → sidecar conf>0 → watcher 热加载 → 前端那行不再调暗、显置信区间、
置信/edge/Kelly 出现。**真实体育 AUC 大概 0.55-0.65 → confidence 0.1-0.3**(synth 验证 AUC0.65→conf0.30)。

---

## 2. 老板问题二:没赔率,还能用吗?(诚实结论)

**能转,但 edge 很薄。** 公允值优先级(fair_resolve.hpp):derivative > **sharp 锚(要赔率)** >
**score-prior blend(不要赔率,只要比分+时钟)** > ML blend > 市场 de-vig 兜底。

没赔率时回落 **score_prior_blend**:`sigmoid(α·比分差 + β·时钟)` 与市场价加权(置信随时钟升)。
**完全不需赔率,只要 Goalserve 实时比分**(刚加的 cricket/esports/tennis livescore 正好喂)。

**但 edge 现实(必须诚实):**
- 实时比分是**公开**的,Polymarket 群众也定价了 → 光"领先=高胜率"赢不了市场
- 真 edge 在**比分反应速度**(进球瞬间比分流比 PM 挂单快 → 事件延迟套利;同
  [[shorthorizon-locking-feasibility]] 结论:只有事件延迟套利活着)
- bet365 sharp 赔率值钱在庄家模型比群众强,但覆盖有限(一直在搞的覆盖率问题)

**待办(数据攒够后):** 回测验证"没赔率时哪些盘、哪种打法有正期望"(事件延迟套利 vs 纯比分先验)。
**当前阻塞**:训练数据已全删重采(见 §4),无历史数据回测,需等新数据积累。

---

## 3. cricket score-prior 护栏 (commit 78208e8a)

覆盖率攻坚加 cricket 补充源引入的**真风险**:cricket innings 制 —— 一队先打满(300/5)另一队还没打(0)
→ `score_diff=runs 差`(可达数百)喂 goals-like sigmoid **饱和成"必胜"**(对方尚未追分)= 垃圾 fair →
无赔率时回落 score_prior_blend → 可能垃圾 paper 单。

修:`pricing::score_prior_applicable(sport)` —— cricket→false(prior_conf=0 → 回落市场 de-vig →
覆盖但不交易),其余→true(累计点/进球/盘/maps/runs 逐局可比,score_diff 有界单调)。
esports(maps 0-3 有界)/tennis(既有行为)不动。纯 paper,不涉真钱。

---

## 4. 训练数据全删重采 (老板令)

老板「训练数据全都删了,重新训练」。**先停 daemon**(让 recorder 释放文件句柄,否则运行时删空间不释放)
→ 删全部采集 jsonl(5.9G→4.0K,保留 model 文件)→ 重启全新采集。

**自动训练会自动跑,但不是马上**:每 30min 一轮,但 **标注样本 ≥500 才训练**,标注来自**已结算比赛**。
现在 settlements 从 0 重新积累 → 头几轮跳过(样本不足)→ 等今天直播盘陆续打完+Polymarket 结算
(几个~十几场)凑够 500 → 首次真训练(几小时内,非立刻)。**删了重采是对的**:旧数据是这轮特征修复
(period/盘口深度/时钟/覆盖率)之前采的,特征值错;新数据干净,训出来才准。

---

## 5. 一句话总结
- **问题一**:修了真 bug(校准接线缺失),训练好的模型现在能真正点亮。✅
- **问题二**:没赔率系统照转(比分先验顶上),但 edge 薄,真钱主要在事件延迟套利;回测待数据。
- 顺带:cricket 垃圾 fair 护栏 + 数据重采闭环跑通。
- 全程 1380 测试全绿,5 次提交已推送部署,WSS 0 断连。

---

## §6 全盘口量化接入 (老板「除了 Moneyline 别的量化未接入」→「全都加,缺的都加」, 2026-06-03)

### 现状诊断 (接入前)
| 盘口类型 | 码 | 模型 | 状态 |
|---|---|---|---|
| moneyline | 0 | FairValueEstimator(通用 sigmoid 跨运动) | ✅ |
| spread/totals | 1/2 | derivative(Poisson/Normal)**仅连续时钟运动** | ⚠️ 模型在但 PM live 大头(tennis/esports)不支持 |
| outright/prop/series | 3/4/5 | 无模型 `return` | ❌ 永远「未接入」 |

错位: derivative 只支持 basket/soccer/hockey/amfb/baseball(连续时钟); PM live 大头是
tennis(84)/esports(15)/cricket(12)(盘/maps/innings 制) → 全不支持。只有 moneyline 跨运动。

### 已接入 (本轮, 全测试 + 部署)
1. **网球 totals/spreads** (commit 0ea8039): `tennis_fair_value.hpp` games/sets 制。
   EventScore+game_row += games_home/away (s1..s5 求和)。TOTALS=E[终场总局](当前+剩余整盘×μ9.7);
   SPREADS=当前局差+对称剩余方差。fail-closed 终态/太早。**v1 覆盖 tennis_scores 源(itf/challenger)**。
2. **电竞 totals/spreads** (commit ef6281d): `esports_fair_value.hpp` best-of-N **精确枚举**系列结局
   分布。per-map p=Laplace clamp[0.35,0.65]。BO3 默认。覆盖 esports/home 源。
3. **outright/prop/series 市场兜底** (commit 36dcbaa): mkt_type>2 不再 return「未接入」, 改发
   quote fair=市场 de-vig(诚实标 market_devig, edge≈0 不交易)。强制挡 score-prior/sharp/ML
   (否则匹配单场比分当冠军概率=垃圾)。

### 剩余缺口 (阻塞/难)
- **atp/wta 网球 games**: 大头(57场)走 inplay-tennis(JSON, 被去重), inplay 解析器只读
  `info.score`(盘数)不读逐盘局数 → atp/wta totals 仍 fail-closed。需确认 inplay JSON 的逐盘
  字段名(`state`/`ss`?)再加解析。**阻塞: feed 被 daemon 占住难抓样本**。
- **series 真模型**: 需系列赛状态(系列已赢场数, 非单场比分), Goalserve 未 plumb → 暂市场兜底。
- **cricket totals**: innings 制总分(runs)模型, 需 overs/wickets 上下文 → 难, 暂无。

### 结论
moneyline + tennis(itf/ch) + esports + (outright/prop/series 市场兜底) 已接入。**结构性「未接入」
(无模型的市场类型)已消除**; 剩 totals/spreads 的温度性「未接入」(赛前/终态/atp-wta-无games)。

---

## §7 ML 垃圾成交事故 + 修复链 (2026-06-03, auto-train 首次触发后暴露)

### 事故
全盘口量化部署后, paper 突现大量垃圾成交: 多市场 fair=0.9995 (恒定), 在 0.006/0.14/0.54
等便宜价位疯买 → 假 86-99% edge。

### 根因链 (逐层挖出)
1. **auto-train 真的训练了** (model 625B→27KB + sidecar), 但 sidecar 显示 **AUC=1.0 / conf=1.0**。
2. **AUC=1.0 在体育上不可能 = 标签泄漏**: 训练特征含市场价 (microprice/bid/ask), 近结算捕获的行
   市场价已 ≈0/1 → 模型平凡"预测"已定结果 → AUC 1.0 (无 alpha)。
3. **93% NO 类别失衡** (settlements 929 value=0 / 87 value=1; 疑 outright 多 NO + settlement
   误record: value=0 且 end_date 未来 6 天) → 模型退化, 对所有市场预测 ~0.0005。
4. **AUC-based confidence 被泄漏骗** (以为模型完美 conf=1.0) → 过校准门。
5. **ml_fair_blend_weight=1.0 满驱动** → fair = 100% ML = 0.0005 → 选 NO → 买 NO @ fair=0.9995。

### 修复 (本轮, 全部署)
1. **校准门** (commit 7dddb70): ml_blend 仅当 `calibrated && conf>0` (与前端 modelReady 同口径)。
2. **fair-sanity 门** (commit 97b1c3e): `|p_fair − 市场 de-vig| > 0.45` → fail-closed 不交易 (真实
   体育 edge 极少 >0.45; 防 orientation/match/模型饱和垃圾)。
3. **训练泄漏守卫** (commit e3f5a56): AUC≥0.9 → leak_suspect → conf=0/calibrated=false → 不驱动。
   selftest 改含噪二值标签 (AUC~0.57 真实)。
4. **清当前垃圾模型**: 删 server model+sidecar + 重启 → stub fallback (不驱动) → fair=baseline。
   验: 0 垃圾成交 / 0 持仓 / WSS 正常。

### 深层待办 (治本, 需量化决策)
- **标签泄漏**: fair-value 模型应【不含市场价特征】或【排近结算捕获行】, 否则永远学"价→果"无 alpha。
  这是模型变可用的前提 (光 gate 掉只是不产垃圾, 模型仍无用)。
- **类别失衡 + settlement 质量**: 查 SettlementRecorder 是否对 end_date 未来/未真结算的市场误录
  value=0 (假 NO 标签); 训练应滤 moneyline (平衡) 或按类别加权。
- 在此之前: 模型 calibrated=false 不驱动 (前端"未训练·占位"), 系统靠 sharp/score-prior/市场 baseline。
