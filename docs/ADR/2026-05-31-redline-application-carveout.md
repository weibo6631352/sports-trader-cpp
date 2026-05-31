# ADR 2026-05-31 — 红线复评:核心红线全留,补「加性/非重大」与「人签门位置」两 carve-out

- **owner:** 老雷 (GM)
- **last_review:** 2026-05-31
- **状态:** 已决
- **触发:** GM 红线复评(Phase 4 接线被会签/审计仪式过度阻塞)

## 背景

Phase 4 实盘接线中,首席架构师老郭 R-4 审计把若干**加性、已通知、默认 disarmed**的工程改动(ExecReport 补 4ts、LiveExecutorAdapter plumbing)划为「需老周+老韩会签」,与「真金白银开闸自动交易」同档对待,无谓阻塞工程。

GM 复评 §8 全部 9 条核心红线:**全部合理,护真实灾难性风险(无界亏损 / 私钥被盗 / 回测失真 / 账本污染 / 热路径瘫痪 / ToS / 数据血缘),一条不删。**

问题不在红线条文,在**应用漂移** —— 正是 §8.1 立条时点名的「约束咬人 / 语义在文档间漂移」病:
- §8 #4 触发词是「**静默**变更」;加性+已通知的字段新增不静默。
- §8 #5 触发词是「**重大**变更」;写默认 disarmed、不开闸不花钱的 plumbing 代码不是重大变更。
- 需人类会签的金融风险事件是「**开闸真实自动交易**」,不是「写还没开闸的代码」。

## 决议

**不修改、不删除任何 §8 核心红线。** 补两条应用边界(纳入 §8.1):

### C-1:加性/已通知/非重大变更 carve-out

**纯加性变更**(struct 末尾新增字段,无重命名 / 单位 / 语义变更)+ **已在 PR/commit 通知下游** + **非重大** → **不触发 R-4 全审计 / 不需架构评审会签**,走普通 PR review。

- 依据:§8 #4 触发词是「静默」、#5 是「重大」;加性已通知非重大三者皆不沾。G-FREEZE-W「只增不改名」本就是低仪式路径。
- **R-4 全审计仅针对:重命名 / 单位变更 / 语义变更**(原始风险:静默架空 caps/exposure/bankroll,如 `size_usdc→size_pUSD_micro`)。
- 误把加性 plumbing 当 R-4/重大套会签 = §8.1 自身要消除的「约束咬人」。

### C-2:人签门画在「真金白银动作」,不画在「写未开闸代码」

需要人类会签(老韩 RM 主权 + 小白安全)的是**实际开闸真实自动交易**:`LiveOrderGate.Arm()` 在 live binary 中对真钱放行。

**不需要会签**(走普通 PR review)的是:编写**默认 disarmed** 的 live plumbing —— submitter / gate / executor / LiveExecutorAdapter / ExecReport 4ts / live 账本 plumbing。这些代码不开闸、不花钱、默认 fail-closed。

- 依据:风险在「钱动」不在「码写」。LiveOrderGate 默认 `armed_=false`,plumbing 写好也不会触达 CLOB。
- 把「代码接线」和「真钱开闸」混成一步 = 无谓阻塞工程,违 §3 铁律「实盘优先」的工程效率。

## 影响(Phase 4 解阻)

C-1/C-2 后,以下从「会签线外」转为「GM 可实施(普通 PR review)」:
- ExecReport 补 4ts(加性)
- LiveExecutorAdapter(新 plumbing,默认 disarmed)
- live 账本 plumbing(`infra::wal::PositionLedger` 同型 FillEvent 化,加性重载)

**仍需人签(不变):** `LiveOrderGate.Arm()` 真实开闸自动交易 —— 老韩 RM 签字 + 小白安全门 + GM 拍板。这是 §8 #1(下单经 RM,已由 LiveOrderGate 落地)+ §6 风控红线的正当金融门,**留**。

## 不变量(复评确认仍有效)

- §8 全部 9 条核心红线:留,verbatim。
- R-11 paper/live 账本隔离:留(账本可信)。
- R-20 4ts:留;fill 事件携带 intent 上游 4ts + fill_ts(回执实测时刻,非 now() 替代上游)= 合规。
- 回测=实盘逻辑:留(已由 CI 口径统一满足)。
