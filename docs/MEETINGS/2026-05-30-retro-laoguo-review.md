# 2026-05-30 复盘评审 — 老郭独立第二意见 + 债务复盘

- **owner:** 老郭 (架构评审 + 独立第二意见 + 否决权)
- **last_review:** 2026-05-30
- **会议:** 2026-05-30 本会话复盘 (P0-2 单位闭环 / P0-6 fee / P0-1 exposure / 伦敦部署)
- **角色:** 独立第二意见 + 债务复盘维度。只读分析, 不改代码。
- **关联:** ADR-041 L1 (RiskConfig int64→MicroPUSD) / arch-debt-audit / m1-route-review

> 一句话定调: **本会话把单位债从「裸奔」收到「类型护栏 + CI 守护」, 这是真功夫。但「已治理」的边界只画到了 RiskConfig, 没画到 PositionLedger —— 而 PositionLedger 才是单位失配的真正源头。安全网的喂数管道也只接了 1/3。所以现在系统的真实状态是「单位收口一半 + 风控接通一半」, 不是「单位已治理 + 风控已活」。任何对外口径若说成后者, 就是 c2 给过的同一种「假已治理」错觉, 只是换了文件。**

---

## ① 三钳制落实情况 (逐条核, 带证据)

我本会话定的三钳制, 配 ADR-041 L1。逐条核实落地, 不凭记忆。

### 钳制 c4 — 禁单边回写 (exposure 必须 sizing/RM 同源, 不许只喂 RM 一侧)
- **落实: 部分。** P0-1 step1 只接了 exposure 喂 RM (`FeedRiskGateway` 全量覆盖喂 RM), **但 sizing 一侧 `current_*_exposure_usdc` 仍硬编码 0** (老韩在 c2 评审已挖出, paper_loop:407-408)。
- **判断: 这恰恰是我 c4 要禁的「单边回写」, 现在就是单边状态** —— RM 看到真 exposure, sizing 看到 0。当前不咬, 因为 M1 只买不平 + 第一笔成交前 RM `cur` 也是 0, 累加退化成单笔。
- **风险定级: P1, 非 P0。** 但必须明确标注: **第 2 笔成交起, RM 用真 exposure 累加判 cap, sizing 用 0 判 → 双轨偶发拒, 且 sizing 完全无感** (它以为还有满额度)。M2 接平仓回路前若多笔并发成交进来, 这个 seam 会咬。**c4 没真闭合, 只是被「单笔退化」掩盖。** 我接受 step1 这样落, 但 debt 表 P0-1 的「step1 已接通」措辞要补一句「sizing 侧 exposure 仍 0, c4 单边未闭」。

### 钳制 c2-c3 禁合并 commit (类型护栏 / 同源根治 各自独立可回滚)
- **落实: 合格。** 核实 commit 链: c2 (6b3664a 类型) / c3 (553c9f7 同源) / c2b (c846b91 bankroll) / c5 (40c0d45 兜底+CI) **四个独立 commit, 时间戳分散 (18:00 / 18:29 / 21:16 / 20:54), 各自带回归绿**。没有出现「一个大 commit 糊一堆」。这条钳制守住了。
- **价值兑现: 真有用。** P0-6 fee bug 正是 c3 改测试时间戳的副产物炸出来的 (BOOK_TS_STALE 长期假绿)。如果 c2-c3 糊在一个 commit 里, 这个测试质量洞会被淹没。**分 commit 让「c3 顺手炸出 fee gap」这件事看得见 —— 这是钳制设计的正反馈, 记一功。**

### 钳制 c5 — CI grep 必随 (单位本体闭环必须带防复发守护)
- **落实: 合格, 且当前真绿。** 我亲自跑了 `unit_contract_check.py` → PASS, 扫 3 文件零违规。pre-push.sh:273 + pr.yml:492 双挂载实锤 (不是写了不挂)。
- **设计认可度: 认可, 且老高的精炼比我原始钳制-3 更好。** 我原始想法是「粗禁所有 cap×1e6」, 老高改成 **F1(sizing 禁 cap 裸 `.v`) / F2(禁 `static_cast<double>(cap)` tripwire) / F3(RM+sizing 禁 cap×1e6, 但白名单豁免 paper_daemon `from_pusd`)**。这个精炼对: 粗禁会误杀 paper_daemon 合法的 whole→micro 边界转换, 逼人加 `unit-contract-ok` 豁免标记污染代码。**F1 是真核心防线** —— 类型系统挡得住 `static_cast<double>(cap)` 但挡不住 `cap.v`(int64 隐式可比可乘) 这个逃生口, grep 正好补这一格。

**c5 的一个盲区(必须说): grep 只守 3 个 enforced 文件 (sizing/RM/paper_loop)。它不守 PositionLedger, 不守 paper_daemon 消费侧, 不守任何新文件。** 这意味着 —— 见 ② —— 单位失配的真正源头 PositionLedger 根本不在 CI grep 射程内。c5 守的是「已知三现场不复发」, 不是「单位语义全库一致」。**别把 c5 PASS 当成「单位债已根治」的证书, 它只是「这三个文件不再退化」的证书。**

### 三钳制总评
- c4 单边未闭 (被单笔退化掩盖, P1 标注待补) / c2-c3 禁合并守住 (且炸出 fee gap 是正反馈) / c5 合格且当前真绿但射程只覆盖 3 文件。
- **没有被违反, 但 c4 是「形式接了实质半拉」, 必须在 debt 表诚实标注, 不能让 P0-1「step1 已接通」读起来像 c4 闭合。**

---

## ② 单位 pattern 根治建议 — 我的独立判断: 走 R-4, 根治点是 PositionLedger, 不是再补 RiskConfig

**本会话单位失配撞了至少 3 次 (caps whole-vs-micro / exposure whole-vs-micro / PublishLedgerSnapshot PnL `/1e6`)。GM 问我: PositionLedger 存 whole 与 RM 比 micro 的双轨, 是不是该走 R-4 一次性根治, 否则还会撞第 4 次?**

**我的回答: 会撞第 4 次, 而且第 4 次已经在 main 里躺着了 —— 就是 PublishLedgerSnapshot 的 PnL bug 本身。这不是「预言」, 是「已发生但还没咬」。** 给三条独立判断:

**判断 1: 三次撞墙不是三个独立 bug, 是同一个根 (`size_usdc` 字段名实不符 + 单位边界没收口) 的三次发作。**
- 核实 `position_ledger.cpp:54` —— `delta_raw = static_cast<int64_t>(fill.fill_size_usdc)`, fill_size_usdc 是 whole pUSD, 直接 cast int64 存进 `pv.size_usdc`。**字段名 `size_usdc` 带 `_usdc`, 存的却是 whole pUSD 整数, 跟 RiskConfig `per_order_cap_usdc` 名叫 usdc 值是 micro 是**完全同一种病** —— 名实不符, 单位活在注释里靠人记。c2 治了 RiskConfig 这一处, PositionLedger 这一处原封不动。
- 三次发作: ① RM 比 micro / sizing 当 pUSD (P0-2, 已 hotfix); ② PositionLedger whole → RM micro 漏 ×1e6 (P0-1, 已加门禁); ③ PublishLedgerSnapshot 把 PositionLedger 的 whole 当 micro `/1e6` (新挖, **未修, 在 main**)。**全是 PositionLedger `size_usdc` 这个字段在不同消费点被当不同单位。**

**判断 2: PositionLedger 存 whole 整数 int64 不止是「单位双轨」, 还藏一个比单位更脏的精度坑。**
- `static_cast<int64_t>(fill.fill_size_usdc)` —— **fill < 1 pUSD 会被截成 0**。M1 demo notional 小 (兜底 1 pUSD, sizing 受 10 pUSD cap), 一旦实盘出现 0.x pUSD 的部分成交或碎单, 仓位直接吞 0。这不是「会不会撞第 4 次」的问题, 这是「记账会静默丢仓位」的问题, 比单位失配更难发现 (单位失配差 1e6 一眼看出, 丢 0.x pUSD 仓位要对账才发现)。
- **whole 整数 pUSD 做仓位单位, 本身就是错的设计选择**, 不是「单位换算没对齐」的小事。

**判断 3 (裁定): 我支持走 R-4 (schema 静默变更红线) 流程, 一次性把 PositionLedger 改存 micro (或直接上 MicroPUSD 强类型), 而不是再给 RiskConfig 补丁。理由:**
1. **根在 ledger 不在 RM。** RM 比 micro 是对的 (它是金额比较, micro 是正确精度)。错的是 PositionLedger 用 whole 整数。补 RM 侧是补叶子, 改 ledger 是砍根。
2. **R-4 是对的载体。** `size_usdc(whole int64) → size_pUSD_micro(MicroPUSD)` 是字段单位+命名变更, 正是 §8.1 第 3 条「ABI/字段单位变更必触发下游审计」要管的事。走 R-4 强制 audit 全部消费点: `FeedRiskGateway`(×1e6 可删) / `PublishLedgerSnapshot`(`/1e6` 的 bug 自然消失) / `get_per_*_exposure` / `get_all_positions`。**改名会触发编译错, 强制把所有当 whole 用的地方暴露出来** —— 这正是我在 debt 表 §4 流程红线写的「改名触发编译错强制审计, 改值不改名是静默的」。现在该把这条用在 PositionLedger 上。
3. **不走 R-4 的代价: c5 的 CI grep 永远守不到这里。** grep 射程是 3 个文件, PositionLedger 不在内。只要 ledger 还是 whole, 任何新消费点 (M2 平仓回路 / DD 喂数 / ML 训练数据落盘) 都可能再把 whole 当 micro, 而 grep 一声不吭。**根不砍, c5 就只能一个文件一个文件追着加 enforced, 这是打地鼠不是治理。**

**建议落地 (派单建议, 我不实施):**
- **owner: 老韩 (RM/ledger 单位 SSOT 主权) 定 spec + 老周 (架构/下游审计) + GM 落地。走 R-4 流程, 引用红线粘原文 (§8.1 第 1+3 条)。**
- **排期: 我定为 P0, 但排在 P0-1 exposure(已接) 之后、DD 喂数之前。** 因为 DD 喂数依赖 unrealized PnL, 而 unrealized PnL 的源就是 PublishLedgerSnapshot 这个 bug。**ledger 单位不根治, DD 喂数接上去就是接了个错 1e6 的 PnL 进 RM 熔断 —— 那比不接还危险 (熔断阈值在错误量纲上判, 要么永不触发要么乱触发)。** 这是硬序: ledger 单位 R-4 → 修 unrealized PnL → 才能接 DD。
- **不接受的方案: 在 PublishLedgerSnapshot 单点补 `×1e6` 把 `/1e6` 抵消。** 那是第 4 块补丁, 制造第 4 个「这里记得换那里忘了换」的现场。**否决单点补丁, 必须改 ledger 存储单位。**

---

## ③ deferred 项怎么防「假已治理」错觉

**GM 问: P0-1 只接了 exposure (1/3 红线), DD/consec deferred。「安全网部分接通」会不会给「风控已活」的错觉 (类似 c2 给「已治理」错觉)? 怎么标清「DD 仍纸面」?**

**会, 而且这是本会话最危险的认知风险, 比任何单个 bug 都危险。** c2 给过一次「MicroPUSD 是零接入孤儿但看着已治理」的错觉, 我当时挖出来升 P0。现在 P0-1 有结构完全一样的错觉风险, 只是换了维度。直说三点:

**第一: 「三条红线」现在的真实状态是 1 接 + 2 纸面, 必须按红线逐条标，禁止用「P0-1 step1 已接通」这种整体措辞。**
- exposure: **接了** (有喂数 + 有单位门禁测试)。
- DD (daily_pnl): **纸面 + 还卡在 PublishLedgerSnapshot PnL bug 后面** (源是错的 unrealized, ledger 单位不修接不了)。
- consec_loss: **纸面但当前 0 是正确的** (M1 只买不平, 无平仓结果序列, 恒 0 非 bug)。**注意: consec 的「纸面」和 DD 的「纸面」性质不同** —— consec 是「逻辑对, 但 M1 业务上不产生信号」, DD 是「逻辑对, 但喂数管道断且源头有 bug」。**这俩不能混为一谈标成「2 条 deferred」, 那会让人以为 DD 也像 consec 一样「不接是合理的」。DD 不接是有 bug 堵着, consec 不接是业务上还没到。**

**第二: 防错觉的硬手段 —— RM 自己要能报「我这条线没被喂数」, 不能靠人记文档。**
- 现在 DD/consec 的「纸面」状态只活在 debt 表注释里。**一旦文档和代码漂移 (§8.1 的老病), 就会有人看到「RM 有 DD 熔断逻辑」就以为 DD 活着。** 这跟 ML-R2 转述走样、跟 MicroPUSD 孤儿是同一类「语义在文档间漂移没人守边界」。
- **我的建议 (架构层, 非阻塞但 M1 前应做): 给 RM 加 feed-liveness 自检 —— 每条红线记「上次被喂数的 ts」, 启动时 / 周期性 emit「DD: never fed / consec: never fed / exposure: last fed Xs ago」到 audit log。** 这样「哪条线是空的」是系统自己说的事实, 不是文档承诺。**一条红线如果从没被喂过数, 它在生产里就等于不存在 —— RM 自己必须知道并喊出来。** 这比任何文档标注都防错觉。

**第三: MVP 验收口径「零风控失效」必须改写, 否则会假绿。**
- 当前 MVP 定义「零风控失效」。如果 DD/consec 纸面、exposure 单边 (sizing 侧 0), 那么「零风控失效」在 M1 paper 里**必然为真 —— 因为根本没有活的风控会失效**。这是个假绿: 没失效不是因为风控好, 是因为风控没接通到能失效。
- **建议把验收拆成两问: (a) 已接通的红线 (exposure) 在受控越界测试下是否真拒? (b) 未接通的红线 (DD/consec) 是否被 RM feed-liveness 显式标记为 not-fed?** 两问都绿才算「风控状态诚实」。**只验 (a) 会把「没接」误当「没失效」。**

**一句话给 GM: 不要说「风控安全网部分接通」, 要说「3 条红线: exposure 接通可咬 / DD 纸面且被 PnL bug 堵 / consec 纸面但 M1 业务无源。RM 当前无法自报哪条没喂数, 这是 M1 前要补的。」** 措辞精确到逐条, 错觉就没地方长。

---

## ④ MVP 诚实差距评估

MVP 定义 (CLAUDE.md §2): **「Moneyline 单盘口实盘跑通, 第一笔成交, 零风控失效, 72h 无崩溃」**。注意是**实盘 (live)**, 不是 paper。本会话的成果是 paper 侧。诚实差距:

**已经有的 (真功夫, 不打折):**
- paper daemon 接真 Goalserve fair value → 第一笔 paper 成交 (A0-A2)。
- 单位债从裸奔收到类型护栏 + CI 守护 (c1-c5)。
- exposure 红线接通可咬 (P0-1 step1, 有单位门禁测试)。
- fee canonical p 对齐 (P0-6, sizing↔RM 同源)。
- 伦敦 EC2 实连 Polymarket + gcc 移植 (链路打通)。

**「看着接了实际没活」的隐患 (按危险度排):**
1. **【最危险】live 下单链路整条 stub (P0-4, 14 接口全空)。** MVP 字面要求是**实盘第一笔成交**, 现在连接 RM 的真下单路径一行没有。**当前所有「第一笔成交」都是 paper VirtualFill。距离 MVP 字面定义还差一整条 live execution 链。** 这不是隐患, 这是 MVP 的主体还没开工。必须诚实: **我们完成的是「M1 第一笔 paper 成交」, 不是「MVP 第一笔实盘成交」。两者差一条 live 下单链 + 真实风控喂数全活 + 私钥签名实战。**
2. **风控安全网半空 (见 ③)。** 「零风控失效」当前是假绿 (没活的风控会失效)。
3. **PositionLedger PnL bug 在 main (见 ②)。** unrealized PnL 错 1e6, M1 只买不平没咬, 实盘一旦平仓立即咬, 且 DD 喂数堵在这。
4. **paper fill 分布不代表实盘但正被当 ML 训练数据存 (debt P1-6)。** fill_rate/slippage 硬编码常数。**这条我单独点出: 它现在不阻塞 MVP, 但它在持续生产「带系统偏差的训练数据」**, 跑得越久, 用这批数据训出来的模型偏差越深。**这是「时间会放大」的债, 不是「放着不咬」的债。** 建议在 ML 数据上打「synthetic_fill_distribution」标记, 防将来误当真实分布。
5. **动态市场发现缺失 + popen("curl") (P0-5)。** 跨洋链路 + 无超时 + ToS 速率不可控, 实盘前必换 C++ client。

**诚实结论: 距 MVP 字面定义 (实盘第一笔成交) 还差「主体」—— live 下单链 (P0-4) + 风控喂数全活 (P0-1 剩 2/3 + ledger 单位根治) + 真实 fill 分布。本会话完成的是「M1 paper 第一笔成交」这个中间里程碑, 是通往 MVP 的必经台阶, 但不要把台阶说成终点。** M1 (6-25) 达标 ≠ MVP 达标, 这两个目标在文档里不能混。

---

## ⑤ 有无架构否决点 / 必须叫停的

**本会话我事后看, 没有需要一票否决回滚的东西。** 改动方向都对 (单位收口 / fee 对齐 / exposure 接通 / 保守默认), 实现也守了钳制。不和稀泥地说: **本会话的工程质量是合格的, 崩点不在「做错了什么」, 在「哪些还没做但容易被当成做完了」。**

**但有两个必须紧急修正 (非否决, 是「不修会变成否决点」):**

**修正 1 (P0, 序在 DD 喂数之前): PublishLedgerSnapshot PnL bug + PositionLedger 单位根治 (见 ②)。**
- 当前不咬 (M1 只买不平), 但它是 main 里的活 bug, 且是 DD 喂数的硬前置。**任何人若在 ledger 单位根治前去接 DD 喂数, 我事前否决 —— 那是把错 1e6 的 PnL 喂进熔断阈值, 比不接更危险。** 这是我本会话唯一的事前否决声明: **DD 喂数 blocked on PositionLedger R-4 单位根治。**

**修正 2 (认知层, 立即): MVP 口径与 M1 口径分离 (见 ③④)。**
- 「零风控失效」「第一笔成交」这两个词在 MVP 定义和 M1 达标线里指向不同东西 (live vs paper / 风控全活 vs exposure 单接)。**不修正口径不会让系统崩, 但会让决策层误判进度 → 在「以为 MVP 快到了」的错觉下做排期 → 这是组织级风险。** 这条不是架构否决, 是要求 debt 表 + 周报口径统一到「逐条诚实」, 由小米守文档边界 (§8.1)。

**给 c4 的事前声明: M2 接平仓回路 / 多笔并发成交前, sizing 侧 exposure 必须从 RM 真实值取 (不许再硬编码 0)。** 在那之前 c4 是单边状态, 单笔退化掩盖着。这条进 debt 表 P0-1 备注, 不是现在的否决点, 是 M2 的前置门。

---

## 附: 给 debt 表的具体修订建议 (派小米/GM 落)

1. **P0-1 措辞**: 「step1 exposure 已接通」后补「(c4 单边: sizing 侧 exposure 仍 0, 第 2 笔成交起双轨, M2 前须从 RM 取真值)」。
2. **新增 P0 项**: 「PositionLedger `size_usdc` 存 whole int64 → 改 micro/MicroPUSD (R-4)。根治单位失配源头 + 消 PublishLedgerSnapshot `/1e6` PnL bug + 消 sub-1-pUSD 截 0 精度坑。DD 喂数硬前置。owner 老韩 spec + 老周下游审计 + GM。」—— 把现在挂在 P0-1 注释里的「PublishLedgerSnapshot PnL bug backlog」升级独立 P0, 因为它是 DD 的 blocker 不只是一个 bug。
3. **DD/consec 标注分离**: DD = 「纸面 + 被 PnL bug 堵」; consec = 「纸面但 M1 业务无源 (恒 0 正确)」。禁合并标「2 条 deferred」。
4. **新增架构 backlog (P1)**: RM feed-liveness 自检 (每红线记 last-fed ts + 启动 emit not-fed)。防「文档说有逻辑就以为活着」的错觉。
5. **新增 ML 数据债标注 (P1-6 配套)**: paper fill 当训练数据须打 synthetic 标记, 防将来误当真实分布。

---

**老郭签字立场: 本会话工程合格, 三钳制守住, c5 CI grep 设计认可。但「单位已治理」的边界画错了文件 (画在 RiskConfig, 真源在 PositionLedger), 「风控已活」是 1/3 不是全部。两个口径错觉是本会话比任何 bug 都需要纠正的东西。R-4 根治 PositionLedger 单位 = 我的核心建议, 且为 DD 喂数硬前置, 单点补丁我否决。**
