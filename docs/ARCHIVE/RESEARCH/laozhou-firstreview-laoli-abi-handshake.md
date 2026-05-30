# 老周 First Review — 老李 ABI Handshake v1

- Owner: 老周 (cpp-chief-architect, A 主管)
- Reviewer 角色: Smell #C 提出方 + 架构主权 (L0-L3 改动等级最终裁量)
- Review 对象: `docs/RESEARCH/laoli-laoSun-handshake-v1.md` + `tests/unit/test_pm_client_abi.cpp`
- Review 日期: 2026-06-W3 (W6 Wave 30)
- 申辩窗口: 24h (W5 末老郭立, 老李可在 24h 内提出 counter)
- 关联:
  - `include/stcpp/polymarket/pm_client.hpp` — ABI LOCK header
  - `tests/ci_grep/abi_lock.py` — CI enforce
  - `docs/MEETINGS/2026-06-01-vote-laozhou-arch-challenge.md` Smell #C

---

## 0. 结论

**ACK — 老李 handshake v1 架构层面通过 first review。**

14 接口 ABI hash、29 个 static_assert 覆盖面、L0-L3 改动等级划分均符合架构主权要求。以下章节记录具体确认事项与 2 条修改建议。

---

## 1. 14 接口 ABI Hash 准确性确认

确认方法: `test_pm_client_abi.cpp` T1 表与 `pm_client.hpp` 实际接口签名人工交叉比对。

| 确认项 | 状态 | 备注 |
|---|---|---|
| 14 条 hash 与 handshake §2 表 1:1 对应 | **确认** | T1 `kAbiTable` 长度 static assert 14 |
| F-14 `SubscribeSportsWss` 使用 C-style 函数指针 (禁 std::function) | **确认** | R-12 热路径堆分配限制正确编码 |
| F-02 `SubmitOrder` 参数 `const SignedOrder&` 而非值传递 | **确认** | 大 struct 引用传递 OK |
| F-07 / F-09 / F-11 / F-12 void 参数组 | **确认** | `CancelAll(void)` 对应 hash `5225e5f0b9d3c970` |
| F-13 `GetPricesHistory` 3 参数 (sv + 2×int64) | **确认** | 参数顺序在 hash 字符串中体现 |

**保留项 (非阻塞):** ABI hash 是文档自洽验证 (sha256 由老李本机计算, 未在 CI 实算 hash)。T1 测试验证的是"hash 字符串格式 + 表条目数", 不验证 hash 值本身与签名字符串的数学关系。这是已知设计决策 — 老李在 §7 写明"ABI hash 变更 PR 必须同时更新 T1 期望值", 流程合规。无需补 hash 计算 CI (M5 前可选, 不阻本期)。

---

## 2. 29 static_assert 完整性评估

实际清点 `pm_client.hpp` 中 static_assert 数量 (老李宣称 29):

| 分组 | assert 内容 | 数量 |
|---|---|---|
| DataSourceTsSource 4 值 | 值 0-3 不变 | 4 |
| TimestampQuad sizeof + 5 offsetof | sizeof 40B + 5 字段 offset | 6 |
| OrderStatus 7 值 + count | 7 值 + kOrderStatusCount==7 | 8 |
| PMErrorKind 10 值 + count | 10 值 + kPMErrorKindCount==10 | 11 |
| **合计** | — | **29** |

**确认: 29 个 static_assert 数量吻合。**

**架构层补充意见 (非阻塞, 老李 ack 后自行评估):**

`WalKind::PaperAudit` 默认值在 T2 `OrderAck.audit_wal_kind` 运行时测试中验证, 但未有 static_assert 覆盖 WalKind 枚举值本身。该 enum 定义在 `stcpp/infra/wal/wal_kind.hpp` (老王 domain), 跨 domain 加 static_assert 有架构耦合风险。老周建议: **留给老王在 WAL framework 自己的 static_assert 里覆盖** (R-11 语义在 WAL domain)。不要在 pm_client.hpp 中加跨域 static_assert。

---

## 3. L0-L3 改动等级合理性 (架构主权确认)

| 等级 | 触发条件 | 审批链 | 老周评估 |
|---|---|---|---|
| **L0** | 新增独立接口 (不改现有签名) | 自动通过 | 合理。F-15+ 可随意加, 不破 ABI |
| **L1** | POD struct 末尾追加字段 | 老李单签 | 合理。末尾追加不破现有偏移 |
| **L2** | 改字段类型/顺序/大小; 改接口参数类型 | 老李+老孙+GM 三方签 | **合理, 且充分**。TimestampQuad 改动 = L2 这条特别重要 — R-20 IPC 协议破坏风险高, 三方签约束力足够 |
| **L3** | 删接口; 改接口名; 改返回类型 | 老郭+老韩+GM 三方签 | **合理**。删接口影响老韩 RM 调用链 + 老郭架构完整性, 三方签正确 |

**架构主权确认事项:**
- L2 "老李+老孙+GM" 组合正确: L2 影响 signer IPC 协议 (老孙 domain) 和接口语义 (老李 domain), 不需要老郭进来 (L2 不破架构拓扑)。
- L3 "老郭+老韩+GM" 组合正确: 删接口/改返回类型会影响 RM evaluate() 调用路径 (老韩) 和跨模块接口图 (老郭)。老周不在 L3 审批链 — 这是合理分权 (老周负责接口设计边界, 不参与每次 L3 会签)。
- **一个修改建议 (请老李 24h 内 ack):** handshake v1 §6 里程碑表 M5 一行写 "L0 (接口签名不变, 实现切真)", 但 live_pm_client.cpp 首次真实现 SubmitOrder 时会调用老孙 signer IPC — 若 signer IPC SignRequest schema 变更 (老孙 v5.1 之后某版本) 导致 `SignedOrder.signature` 字段语义变化, 这实质上是 L2。建议在 §5 M5+ 联合 checklist 中补一条: "若 signer IPC schema 有 breaking change → 重走 L2 流程, 不依赖 M5 L0 免审"。不影响本期 v1 有效性。

---

## 4. L2/L3 改动 Architecture Review 流程定稿

**老周决议 (架构评审流程):**

### L2 流程
1. 提案方 (通常老李) 提 PR, PR 描述包含: 变更原因 + ABI hash 新旧对比 + 受影响接口列表。
2. PR 描述必须引用 `laoli-laoSun-handshake-v1.md F-XX L2`。
3. 老李 + 老孙 代码 review + ack comment (24h 窗口)。
4. GM 最终 ack 后 merge。
5. `test_pm_client_abi.cpp` T1 表同步更新, CI `abi_lock.py` 不 fail 为合并前置条件。
6. 无需进老郭月度架构评审 (L2 影响范围局限于接口参数, 不改拓扑)。

### L3 流程
1. 提案方提 RFC 文档 (不直接开 PR), 先投 Slack / 会议征求意见。
2. 老郭 (架构评审) 确认拓扑影响范围 → 列入月度架构评审议题。
3. 老韩确认 RM 调用链不违反 R-12 / R-20。
4. GM ack。
5. 开 PR, 合并前 `test_pm_client_abi.cpp` T1/T3 表同步更新, CI `abi_lock.py` 不 fail。
6. **提前 2 sprint 提案** (handshake v1 §6 约定, 老周确认合理)。

**特别说明:** L3 "提前 2 sprint" 约束是老李自己立的, 老周 ack — 这是保护下游 (老孙 signer, 老韩 RM, 小冯 WSS) 不被突袭的有效机制。

---

## 5. 老周 ACK 声明

老李 ABI handshake v1 满足 Smell #C 的修复要求:
- 14 接口 ABI hash 有文档记录。
- 29 static_assert 在编译期强制 ABI 不漂移。
- L0-L3 四级审批链合理且可执行。
- CI `abi_lock.py` + `test_pm_client_abi.cpp` 双重 enforce。

**老周 first review: PASS。**

后续流程:
- 老郭 PR v1.2 review (本 review 之后, 按老郭 laoguo-coordinator-mandate 节奏)
- GM ack (老郭过后)
- M5 接入前: 老李 + 老孙 联合 review (handshake §8 next_review 约定)

---

**老周 (cpp-chief-architect), 2026-06-W3**
