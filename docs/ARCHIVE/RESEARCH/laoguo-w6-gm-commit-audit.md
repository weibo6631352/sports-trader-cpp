# 老郭 W6 GM Commit History 抽查报告 v1

- **owner:** 老郭 (#16, chief-architecture-reviewer, F 顾问团协调人)
- **last_review:** 2026-06-W3 by 老郭
- **触发:** W6 W2 GM 越权 retro audit 书面 (老板 ACK 4 件事之一); W6 W3 仲裁报告 §5.1 改进承诺
- **输入:** git log --author="weibo wang"; gm-self-mistakes-log.md #11/#13/#14; 老郭仲裁报告 W6 W3
- **抄送:** 老雷 (GM) / 老周 / 老高 / 老胡

---

## 1. W6 W2 commit a8afebe audit

**commit:** a8afebe "feat(w6-wave29): 8 IC 跨 5 单元并行 — W6 W2 + GM 错 #11 hotfix"
**date:** 2026-06-01 (W6 W2)
**author:** weibo wang (GM 老雷)

### 1.1 src/include 改动清单 (GM 直接动手的 10 处 hotfix)

根据 git diff 与 gm-self-mistakes-log #11/#13 记录, GM 在整合 8 IC 交付物后自行修了以下 10 处:

| # | 文件 | GM 改动内容 | 原 owner |
|---|---|---|---|
| 1 | `include/stcpp/observability/blake3_hash.hpp` | unused-function / NEON arm64 define guard | 老唐 |
| 2 | `include/stcpp/observability/audit_emitter.hpp` | `friend class AuditEmitterPool` 加入 | 老唐 |
| 3 | `src/stcpp/observability/audit_emitter.cpp` | std::min cast fix | 老唐 |
| 4 | `include/stcpp/infra/wal/ingest_raw_writer.hpp` | nodiscard x3 | 小冯 |
| 5 | `src/stcpp/infra/wal/ingest_raw_writer.cpp` | nodiscard x3 (实现侧) | 小冯 |
| 6 | `include/stcpp/data/odds_record.hpp` | audit_id 方法字段同名冲突 | 小段 |
| 7 | `tests/unit/` (多文件) | EXPECT_NEAR int→double | 小卢 + 老王 |
| 8 | `src/stcpp/infra/wal/position_ledger.cpp` | ToMarketIdArray unused 删除 | 小蒋 (helper 改后无用) |
| 9 | `tests/integration/audit_chain_verify_test.cpp` | sign-conversion x5 (XOR→Blake3 引入) | 老唐 |
| 10 | `include/stcpp/polymarket/pm_client.hpp` | 未描述具体内容 (ABI 路径) | 老李 |

### 1.2 老郭技术判断

**技术正确性:** 10 处 hotfix 均为真实 build error — 编译失败的问题 (unused-function / nodiscard / int→double / sign-conversion) 是需要修的. GM 技术判断 OK, 没有引入新的逻辑错误.

**越权判断:** 越权. 理由:
- `friend class AuditEmitterPool` (#2) 是架构设计决策, 不是纯语法修复. GM 无把握老唐的设计意图.
- `ToMarketIdArray` (#8) 删除是功能决策, 不是 warning fix — 小蒋当时可能有后续计划.
- 其余 8 处虽属语法层面 fix, 但正确处理是"派回 owner 修 + 附 error log", 不是"GM 自己修".

**结论:** 10 处 hotfix 技术上可辩护, 流程上全部越权. ADR-005 §3.3 + CLAUDE.md §7 均不允许 GM 自行代修他人代码.

**处置:** 已入历史 (commit a8afebe), 回滚成本 > 收益, 不强制 revert (gm-self-mistakes-log #13 已记录). W7 起走 ADR-005 §3.4 FOM 预防.

---

## 2. W6 W3 GM 错 #14 commit af36066 + fix 6f2455e audit

### 2.1 af36066 — 误推 Parquet stub data

**commit:** af36066 "feat(w6-wave30-31-32): 8 IC W6 W3 + 联合 review + 全员问题解决方案"
**错误内容:** `data/paper_mldata/` 目录 24 个 Parquet 文件 (~1.5 MB) 入 git

**根因:** 小田 W6 Wave 30 Python notebook 跑出的 stub data 落在 `data/` 目录, `.gitignore` 没覆盖 `data/` 通配. 与 GM 错 #12 (build_adr010/ 误推) 是同类型的 gitignore 漏洞重复.

**老郭判断:**
- 技术: .gitignore 确实有漏洞, 但 #12 已有先例, GM 没有从 #12 中吸取教训形成系统性通配规则 — 流程失效.
- CLAUDE.md §8 红线: "binary 大文件入 git → fail" 当时尚未有 CI enforce, 是制度空窗期导致的事故而非红线故意违反.
- 性质: P2 流程事故 (非 P0 红线, 因为 .parquet 不含私钥/凭证, 无安全风险).

### 2.2 6f2455e — fix commit

**commit:** 6f2455e "fix(git): 移除误推 Parquet stub data 24 文件 (GM 错 #14, 与 #12 同模式)"
**修复内容:** `.gitignore` 加 `data/` + `*.parquet` 通配; `git rm -r --cached data/` 移除 24 文件

**老郭判断:** fix 正确且完整. `git rm --cached` 方式保留本地文件但从 git tracking 中移除, 是正确操作. `.gitignore` 补通配覆盖未来防复发.

**未覆盖:** `.pkl/.pt/.ckpt/.onnx/.h5/.feather` 等其他 ML 产物此时尚未加通配 — 老高 PR v1.4 grep enforce 是正确的后续补齐方向.

---

## 3. W7 起 GM commit history 抽查 SOP

(协调人职责延伸, 不是 line management — 老郭 mandate v1 §2 Do 第 5 条)

### 3.1 触发条件

每 wave 整合 commit push 后 24h 内, 老郭执行一次快速 audit.

### 3.2 抽查命令

```bash
# 找本 wave 中 GM 的 commit
git log --author="weibo wang" --oneline -5

# 检查最近 GM commit 是否含 src/include 改动
git show <commit_hash> --name-only | grep -E "^(src/|include/)"

# 检查是否有大 binary 文件
git show <commit_hash> --name-only | grep -E "\.(parquet|pkl|pt|ckpt|onnx|h5|feather)$"
git show <commit_hash> --name-only | xargs -I{} git cat-file -s {}:HEAD 2>/dev/null | awk '$1 > 1048576'
```

### 3.3 判断标准

| GM commit 内容 | 处置 |
|---|---|
| 仅 docs/ RESEARCH/ ADR/ MEETINGS/ SPRINTS/ | 正常, 不上报 |
| src/ include/ 有改动 | 立即核查: 是否为紧急 P0 hotfix (ADR-005 §3.2 例外)? 若否 → 上报老板 + 记录本文件 |
| binary / .parquet 等大文件 | 立即上报 GM + 老高 fix |
| tests/ CMakeLists.txt 改动 | 核查是否 FOM 已授权 |

### 3.4 报告节奏

- 发现问题 → 24h 内写入本文件 §4+ (追加方式)
- 连续 2 wave 无问题 → 月报简报一条
- 发现越权 → 上报老板 + 通知老周 (技术 owner) + 触发 ADR-005 §3 派回流程

---

## 4. Part 4 不耻下问记录

### @老周

**24h 申辩窗口 — ADR-018 §X 仲裁 (截止 6/2 EOD)**

老周, 仲裁决议已在 ADR-018 §X 正式落地. 你对"ADR-018 §X 补条款方案 + 第 5 个 CMake option 触发老郭过目"有何异议? 如果你认为 ADR-019 独立立项有更强的工程理由 (例: §X 在 ADR-018 里可见度不够 / IC 新增 switch 不会主动查 ADR-018), 请在窗口内书面回复. 无回复视为接受. ADR-019 候选编号已为你预留, 第 6 个 CMake option 出现时触发立项.

### @老高

**PR v1.4 build switch grep + binary 文件 grep + CI 矩阵 enforce**

请 ack 以下 W7 落地清单:
1. 隐式 PRIVATE define (非 CMake option) 漏文档化 → grep fail (目标: `STCPP_TEST_BUILD` 强制在 CMakeLists.txt 顶部注释出现)
2. binary 大文件 (>1MB) 或 `.parquet/.pkl/.pt/.ckpt/.onnx/.h5/.feather` 入 git → CI fail
3. CI 矩阵补 paper × bench OFF / paper × bench ON 两个组合 (基础覆盖, live/backtest 后续 sprint 补)
4. `check-gm-src-commit` warning-only job (本文 §3.3 数据来源)

落地后通知老郭 ack + 记录 PR v1.4 链接.

### @老胡

**ADR-005 §3.4 + ADR-018 §X 集成周报 §9 KPI**

请 ack 并集成以下两项到周报 §9:
1. **文件抢占次数** (期望 0): 来源 老高 PR v1.4 FOM ref 缺失 fail 日志
2. **越 FOM 修改次数** (期望 0): 同上
3. **ABI cascade dead code 检出数**: 老王 + 老高 联动, W7 第一次数据
4. **GM 代修次数 KPI 数据源**: 老高 `check-gm-src-commit` warning log → 你的周报接入方式请与老高对齐

如有 FOM 模板格式建议 (GM 首次执行时用的模板), 请 W7 内给出, 供 GM Wave 33 派单前参考.

### @老雷

**GM 错 #14 ack + W6 W2 retro audit 书面**

请 ack:
1. **GM 错 #14 (af36066 Parquet 误推):** 本文 §2 audit 已完成. 请确认 fix commit 6f2455e 是你的正式 fix, 入 INCIDENTS log #14 closed.
2. **W6 W2 retro audit 书面 (7/5 EOW 前出具):** 格式见仲裁报告 Part 3 §3.3 建议格式. 发送给 老唐 / 小冯 / 老孙 / 小段 / 小卢 / 小蒋 共 6 位原 owner. 老唐需就 `friend class AuditEmitterPool` 正式书面 ack.
3. **CI commit author check warning-only:** 老高 W7 W3 落地, 请 ack.
4. **ADR-005 §3.4 (本次正式入文档):** 请 ack 生效, Wave 33 起 FOM 强制执行.

---

**边界声明:** 本报告为协调人 audit 记录 + 不耻下问. 不替 GM 拍决议, 不替老高写 CI, 不替老胡写周报. 我提问询, 各方 ack 后执行.

— 老郭 (chief-architecture-reviewer), 2026-06-W3
