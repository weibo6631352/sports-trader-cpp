<!--
  sports-trader-cpp PR 模板 (老高 v1.1, W5 Wave 24).
  规范来源: docs/RESEARCH/laogao-code-conventions-v1.md + laogao-pr-review-v1.1.md
  改本模板需走 PR + 老高/老郭/老雷 三方任一签字.

  填写约定:
  - 7 节描述全部必填, 缺一被 reject.
  - Checklist 用 [x] 勾选, 未勾即视为未做.
  - 红线 §9 涉及 (R-1..R-33) 必须显式选 Yes/No.
  - ADR-005: 5 战斗单元 IC task PR 必带 "spec by <主管>".
-->

## 0. 派单链 (ADR-005, CI grep enforce)
<!--
  CLAUDE.md §7.8 第 5 题硬 enforce: 5 战斗单元 IC 任务必经主管.
  本节一行声明派单来源, CI 会 grep PR description.
  允许的 spec 源:
    - spec by 老周  (A 系统工程部 主管)
    - spec by 老韩  (B 风控合规部 主管)
    - spec by 小梁  (C 量化研究部 主管)
    - spec by 小余  (D 数据基础设施部 主管)
    - spec by 老胡  (E 产品业务保障部 主管)
    - spec by 老郭  (F 协调人, 顾问团 forward)
    - spec by 老雷  (GM, 例外: 顾问团 / 紧急 P0 / 主管本人 / 跨单元统筹)
    - spec by 老钱  (CPO, 平级 GM)
    - self-spec (F 顾问)  ← 顾问团 self 派 (老高 / 老何 / 老郭 / 老徐 / 小白 / 老钱)
  绕主管直接派 IC = ADR-005 越权, PR 拒绝.

  例外补丁: dependabot / 文档型 PR / .github/ 元 PR 允许 self-spec.
-->

spec by:
<!-- 例: spec by 老郭 (F 协调人 W5-F-01 forward 老高 PR review v1.1 任务) -->

走主管层 ack: <!-- yes / no / N/A (顾问 self-spec / GM 例外) -->


## 1. What
<!-- 一句话: 改了什么. 不要复述 diff, 说意图. -->


## 2. Why
<!--
  链 ADR / RESEARCH / Sprint ticket, 不允许 "老板让我做的".
  例:
    - 关联 ticket: S1-014
    - 关联 ADR: docs/ADR/2026-05-28-arch-and-rm-v0.1-review.md §2.2
    - 关联 RESEARCH: docs/RESEARCH/laozhou-architecture-v0.1.md §6
-->


## 3. 影响模块
<!--
  按层标 + 具体路径. 跨层 = 老郭前置审 (PR 大小 > 3 层走架构通道).
  例:
    - L4 risk/gateway (修改 evaluate 签名)
    - L1 infra/error (新增 Status code)
-->

- [ ] L1 infra/
- [ ] L2 data/
- [ ] L3 strategy/
- [ ] L4 risk/
- [ ] L5 exec/
- [ ] tests/
- [ ] tools/
- [ ] CMake / CI / 部署
- [ ] docs/

涉及路径:


## 4. 风控 / 安全 / 红线相关?
<!--
  改到下列任一 = Yes, 必须老韩 + 老郭双签 + 抄老雷:
  - risk/ 任意
  - exec/signer / exec/router / exec/clob / exec/nonce
  - 配置中风控阈值 / 私钥 / KMS
  - 任何触碰 §9 红线 R-1..R-10 的代码 (即使是新增防御)

  虚拟跑盘 (paper trade) PR: 必须明示是否仍走完整 RM 路径
  (默认: 走, 见 docs/RESEARCH/laogao-code-conventions-v1.md §9 R-1 / R-4).
-->

- [ ] 风控相关 (Yes / No):
- [ ] 安全相关 (Yes / No):
- [ ] 虚拟跑盘 / paper trade 模式相关 (Yes / No):
- 如 Yes, 已通知 reviewer:


## 5. 测试 / 验证
<!--
  跑了什么, 贴关键输出. 没跑 = 不准提.
-->

- [ ] `clang-format` 通过 (零 diff)
- [ ] `clang-tidy` 通过 (零 warn)
- [ ] unit test 通过 (gtest / Catch2)
- [ ] integration test 通过 (如适用)
- [ ] replay test 通过 (如适用, 数据回放)
- [ ] ASAN / UBSAN 通过
- [ ] TSAN 通过 (concurrency code 必跑)
- [ ] benchmark 跑过 (热路径改动必跑), 与基线对比:
- [ ] 单测覆盖率本 PR diff > 70% (gcovr 输出):


## 6. 回滚 / 风险
<!--
  改坏了怎么办?
  - feature flag? (推荐, 红线模块强制)
  - 单 revert commit 可回?
  - 数据 / schema 迁移有逆向?
  - 是否会影响 paper-trade / sim 模式?
-->


---

## 7. PR 自检 Checklist (按 docs/RESEARCH/laogao-code-conventions-v1.md)

### 命名 / 风格 (§1)
- [ ] 类 `UpperCamelCase`, 函数 `lower_snake_case`, 私有成员 `name_`
- [ ] 涉及单位的标量带后缀 (`_ms` / `_us` / `_bps` / `_qty` / `_usdc6` / `_prob` / ...)
- [ ] **persona 名未进代码** (老周 / 老韩 / 老高 / ... 全无)
- [ ] 无匈牙利前缀 / 无单字母变量 (循环索引除外)

### 注释 (§2, 默认无注释)
- [ ] 无 WHAT 注释, 无被代码已表达的废话
- [ ] 新增 TODO 都带 (owner, sprint, ticket) — 否则删
- [ ] 公共头 (`include/stcpp/`) 新增 API 有 Doxygen `@brief` (只写 WHY)
- [ ] 无 commented-out 代码

### 头文件 / 模块边界 (§3)
- [ ] include 顺序: own → C → C++ std → 三方 → project
- [ ] 用 `#pragma once`, 无 include guard
- [ ] 头里无 `using namespace`
- [ ] 模块边界遵守 §3.2 (L5 不见 risk_internal, L3 不见 exec, ...)
- [ ] 无非法跨层 include (R-5)

### 错误处理 (§4)
- [ ] 用 `Result<T, Status>`, 无 throw, 无 raw `assert()`
- [ ] noexcept 边界正确 (`infra/net` 适配层吃异常)
- [ ] `STCPP_INVARIANT` / `STCPP_EXPECTS` 用对场景

### 性能反模式 (§5)
- [ ] 热路径无 `std::shared_ptr`
- [ ] 任何路径无 `std::endl`
- [ ] 无 `dynamic_cast` (RTTI 已禁)
- [ ] 无 `std::regex`
- [ ] 热路径无隐式分配 (`push_back` 未 reserve / 字符串拼 / `new` / ...)
- [ ] 必要时点名 @老姜 review

### 安全编码 (§6)
- [ ] 私钥 / secret 不进 log / 不进常规容器 / `secure_memzero` 用对
- [ ] 外部输入入口走 schema + range + len 校验
- [ ] HTTPS / WSS 全走 `infra/net::TlsSession` + cert pin
- [ ] 涉及加密 / 随机走 `infra/crypto` (不自行 EVP)
- [ ] 必要时点名 @老沈 review

### CI / 红线 (§8 / §9)
- [ ] 编译 `-Wall -Wextra -Werror` 零警告
- [ ] 无 `#ifdef SKIP_RM` / `DISABLE_AUDIT` / `MOCK_SIGNER` (R-7)
- [ ] 改 schema 已通知下游并升版本 (R-3)
- [ ] 改公共 API 已同步 ADR / RESEARCH (通知 @小米)
- [ ] **红线 §9 R-1..R-10 自检通过**

### 文档同步 (§7.3 最后一条)
- [ ] 同步了 `CLAUDE.md` / `AGENT.md` / `docs/INDEX.md` (如有影响)
- [ ] 改了公共 API 同步了 `docs/RESEARCH/` 对应文档版本
- [ ] 抄送 @小米 (doc-curator) 入档

---

## 8. Reviewer 分配 (按 §7.4)
<!--
  按改动范围填. 红线模块必须双签 + 抄老雷.
-->

- 必 review:
- 选 review:
- 抄送 (红线 / 跨单元):

---

## 9. 老高门 (style + 红线门, 不可跳)

- [ ] 已请 @老高 (code-quality-reviewer) review
- [ ] 老高指出的问题已逐条处理 (不允许 "下次注意")

### 9.1 v1.2 新增自检 (Wave 28)

<!--
  ADR-009 v2 = 全员 Sonnet。Opus 必须例外说明 + GM ack。
  R-20 / R-12 / R-33 / HMAC 反模式由 CI grep 自动检查；本 checkbox 为人脑 pre-check。
-->

- [ ] **ADR-009 v2 Opus 例外自检**: 本 PR 不含 `model: opus`；
      或含 `model: opus` 但已在文件内加 `例外:` 段 + GM ack 链接，
      且理由不含 "管理层身份" / "我觉得 Sonnet 不够好" / "task 复杂"
- [ ] **R-20 / R-12 / R-33 / HMAC 反模式自检**: 本 PR diff 已扫以下场景
      (CI Python grep 会自动检查, 本 checkbox 为提交前 pre-flight):
      - R-20: 无 `data_source_ts = now()`, 无 `event_ts = ingestion_ts`
      - R-12: WSS 路径无 `lock_guard` / `blocking_read` / 同步 HTTP / `fsync`
      - R-33: paper 路径无 `/ws/user`, 无旧 host `ws-subscriptions-clob`
      - HMAC: 无 `rstrip(b"=")` / `paramType` / `sigType=2` / request_path 拼 querystring

### 9.2 v1.3 新增自检 (Wave 30)

<!--
  GM 错 #11 配套: build+ctest 验证条款
  ABI lock: pm_client.hpp / live_pm_client.hpp 改动须引用 handshake 文档
-->

- [ ] **本地 build+ctest 通过**: `cmake --build build && ctest --output-on-failure` 已跑,
      且输出摘要已贴 §5 测试验证节 (写测试不算交付, 跑通才算 — GM 错 #11)
- [ ] **ABI lock 自检** (改 pm_client.hpp / live_pm_client.hpp 时):
      PR description 已含 `ABI ref: docs/RESEARCH/laoli-laoSun-handshake-v1.md F-XX L<等级>`;
      L2/L3 已含 "三方签" 声明

### 9.3 v1.4 新增自检 (Wave 33 — GM 错 #13 #14 配套)

<!--
  GM 错 #13: 越权代修别人代码 → gm_commit_author_check.py
  GM 错 #14: binary 大文件 / ML artifact 入 git → binary_large_file_check.py
  ADR-005 §3.4 FOM → fom_check.py (W7 W4 激活 FAIL)
  老王 W6 Wave 32 ABI cascade → abi_cascade_check.py
  ADR-010 §2.2 -Wno-* → adr010_wno_check.py (与小宋 W7 协作)
-->

- [ ] **ABI 变更自检**: 本 PR 是否含 ABI 变更 (struct 新增字段 / sizeof 变化)?
      - [ ] 不含 ABI 变更
      - [ ] 含 ABI 变更, 已在 PR description 列 "下游 audit 清单:" 章节 (或加 "ABI cascade reviewed: no downstream")
- [ ] **binary / ML artifact 自检**: 本 PR 是否含 > 1MB 文件或 .parquet/.pkl/.pt/.onnx 等?
      - [ ] 不含
      - [ ] 含大文件, 已在 PR description ack 说明原因 (GM 错 #14 配套)
- [ ] **GM commit author 自检** (仅 GM 老雷填): 本 PR 是否含 GM 直接 commit 的 src/ / include/ 文件?
      - [ ] 不含 (正常通道, 跳过)
      - [ ] 含, 已在 PR description 加 "GM 紧急 hotfix 理由: <至少 10 字说明>" (ADR-005 §3.2)
- [ ] **FOM 自检** (改 CMakeLists.txt / pr.yml / .clang-tidy / .gitignore 等关键文件时):
      PR description 含 "FOM ref: <wave>" + "lead owner ack: <name>" (W7 W4 激活前 warning-only)
- [ ] **ADR-010 -Wno-* 自检**: 本 PR 是否新增 `-Wno-*` 抑制?
      - [ ] 不含
      - [ ] 含, 已确认在 ADR-010 §2.2 grandfather 白名单内或已走例外申请流程

<!-- FOM ref 字段 (W7 W4 ADR-005 §3.4 立后填, 改关键基础文件时必填) -->
FOM ref: <!-- 例: W7-F-04 或 N/A (不改 FOM 管控文件) -->

---

<!--
  生成约定: 由 Claude Code 协作完成的 PR 在 commit message 末尾保留:
    Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
-->
