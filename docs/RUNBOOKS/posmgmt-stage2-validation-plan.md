# 持仓管理 Stage 2 — paper 验证计划 (执行层/规模层乘子)

> owner: 老雷 (GM) · last_review: 2026-06-09 · 性质: 验证 runbook
> 对象: 2026-06-09 落地的 §4.1 执行层 + 规模层乘子 (全部默认 OFF, 需 paper 背书才开)
> 设计依据: [`laolei-position-management-synthesis-v1.md`](../RESEARCH/laolei-position-management-synthesis-v1.md) §4.1
> 前置: paper server 已起 (`/home/ec2-user/start_paper.sh`, `--enable-fills`); 真钱不涉 (R-11 paper)

---

## 0. 一句话

5 组旋钮全部**默认关 = 现状基线**。验证 = **逐组开 + 标系数 + 比指标**, 全经 env 配置 (无需重编译)。
**纪律: 一次只开一组, 与 all-off 基线 A/B 对比; 任何组净负或换手率爆增 → 关掉。**

---

## 1. 旋钮清单 (env → PaperLoopConfig)

| env 变量 | 字段 | 默认(关) | 作用 | 组 |
|---|---|---|---|---|
| `STCPP_DEADBAND_FEE_K` | deadband_fee_k | 0 | p(1−p) 死区缩放: 费贵处(p≈0.5)放宽死区抑制 churn | A |
| `STCPP_EXEC_MARGIN` | exec_margin_enabled | 0 | 动态 exec_margin 总开关 (逆选保护) | B |
| `STCPP_EXEC_MARGIN_K_TOX` | exec_margin_k_tox | 0 | 毒性强度 ×\|OFI\|/depth | B |
| `STCPP_EXEC_MARGIN_K_VOL` | exec_margin_k_vol | 0 | 波动强度 ×RealizedVol²×τ | B |
| `STCPP_EXEC_MARGIN_CAP` | exec_margin_cap | 0.05 | exec_margin 上限 (prob) | B |
| `STCPP_TOX_GATE` | tox_gate_enabled | 0 | 毒性冻结加仓硬档总开关 | C |
| `STCPP_TOX_OFI_DEPTH_THR` | tox_gate_ofi_depth_thr | 0 | \|OFI\|/depth ≥ 此 → 冻结新增加仓 | C |
| `STCPP_TOX_BID_ABSENCE_THR` | tox_gate_bid_absence_thr | 1.0 | BidAbsence frac ≥ 此 → 冻结 | C |
| `STCPP_CORR_MULT` | corr_mult_enabled | 0 | 相关性折扣 Kelly 乘子总开关 | D |
| `STCPP_CORR_TAPER_START` | corr_taper_start | 0.50 | ρ 加权占用 ≥ 此才 taper | D |
| `STCPP_CORR_FLOOR` | corr_floor | 0.30 | 乘子下限 | D |
| `STCPP_CORR_RHO_DEFAULT` | corr_rho_default | 0.70 | ρ 占位 (per-type 表待校准) | D |
| `STCPP_CORR_EVENT_CAP_PUSD` | corr_event_cap_pusd | 10000 | event cap (**须 == RM event_exposure_cap**) | D |

启动时若任一组开, daemon 打印 `[paper_daemon] Stage2 验证旋钮: ...` 一行回显, 核对配置生效。

---

## 2. 推荐起步系数 (各组首次 A/B)

> 全是保守起点, 非最优; 验证就是为了标定。先验证「方向对不对」(指标改善方向), 再细调幅度。

- **组 A 死区**: `STCPP_DEADBAND_FEE_K=2.0` (死区 ≥ 2×往返费; p≈0.5 盘明显放宽, 极价盘几乎不动)。
- **组 B exec_margin**: `STCPP_EXEC_MARGIN=1 STCPP_EXEC_MARGIN_K_TOX=0.3 STCPP_EXEC_MARGIN_K_VOL=0`(先只开毒性项, vol 项后单测; k_tox 量纲 = prob/(OFI/depth 比值), 0.3 起步看 exec_margin 是否进 [0.005,0.02] 合理区, 看 required_margin 分布)。
- **组 C 毒性冻结**: `STCPP_TOX_GATE=1 STCPP_TOX_BID_ABSENCE_THR=0.5`(先只用 BidAbsence 半窗无 bid 判据; ofi_depth_thr 留 0 关, 因其量纲需先观测 |OFI|/depth 分布再定)。
- **组 D 相关性**: `STCPP_CORR_MULT=1`(taper/floor/rho 用默认; **先确认 event cap 与 RM 一致**)。ρ 用 0.70 占位 — 这组只验证「同赛事多盘时 target 是否被合理 taper」, per-type ρ 表是后续离线校准事。

---

## 3. A/B 流程 (一次一组)

1. **跑基线 (all-off)** 一个稳定窗口 (建议 ≥ 一个完整赛日 / ≥ 200 笔成交), 记录指标 §4。
2. **开一组** (设该组 env, 其余不设), 跑同等窗口。
3. **比指标** (§4): 该组是否在不显著恶化 PnL 的前提下改善了它的目标指标。
4. **判定**:
   - 改善 + 无副作用 → 保留, 进下一组。
   - 净负 / 换手率爆增 / maxDD 恶化 → **关掉**, 记录为「当前数据下不开」。
5. **组合**: 单组都过后, 再开「全开」对比基线 (查交互效应)。
6. **真钱开闸**: 任何组要上真钱仍须 maxDD≤15% 实测背书 + 老韩 RM + 小白安全会签 (§8.1, 与 paper 验证分离)。

---

## 4. 看哪些指标 (全在现有观测)

| 指标 | 来源 | 期望方向 |
|---|---|---|
| **换手率** (orders/fills per market·hr) | `/api/v1/stats` (orders_attempted/fills_completed) | 组 A 应↓ (死区抑制 churn); 别看到爆增 |
| **maxDD** | portfolio_metrics | 不恶化 (北极星红线 ≤15%) |
| **滚动 CLV 均值** | `/api/v1/quote` rolling_clv_mean / `clv_report()` | 不变差 (入场质量); 组 B/C 应不伤甚至略升 (逆选保护) |
| **乘子分布** | `/api/v1/quote` lifecycle_mult/clv_mult/**dd_mult/corr_mult** | 组 D: corr_mult 在同赛事多盘时 <1 (生效); 单盘时 =1 |
| **exec_margin/required_margin** | quote required_margin | 组 B: 毒簿/高波动 tick 上 required_margin 应↑ |
| **tox_freezes** | `stats().tox_freezes` | 组 C: 毒簿盘冻结计数 >0, 平静盘 ≈0 |
| **净 PnL / 笔均** | pnl_ledger | 任何组不显著转负 |

---

## 5. start_paper.sh env 片段 (部署节点应用)

> 部署节点 `/home/ec2-user/start_paper.sh` 在 server 上, GM 改 server 状态前确认 (§13)。
> 下面是验证某一组时往启动命令前加 env 的样例 (这里示范组 A 死区):

```bash
# 基线 (all-off): 不设任何 STCPP_* → 现状
./paper_server --enable-fills --port 8081 ...

# 组 A: p(1−p) 死区
STCPP_DEADBAND_FEE_K=2.0 ./paper_server --enable-fills --port 8081 ...

# 组 B: 动态 exec_margin (毒性项)
STCPP_EXEC_MARGIN=1 STCPP_EXEC_MARGIN_K_TOX=0.3 ./paper_server --enable-fills --port 8081 ...

# 组 C: 毒性冻结加仓
STCPP_TOX_GATE=1 STCPP_TOX_BID_ABSENCE_THR=0.5 ./paper_server --enable-fills --port 8081 ...

# 组 D: 相关性折扣 (确认 cap 与 RM event_exposure_cap 一致)
STCPP_CORR_MULT=1 STCPP_CORR_EVENT_CAP_PUSD=10000 ./paper_server --enable-fills --port 8081 ...
```

env 未设 = 该旋钮关 = 逐位等于现状 → 基线安全, 不怕忘关。

---

## 6. 注意 / 已知边界

- **组 D 的 event cap 必须 == RM `event_exposure_cap_usdc`** (默认都 10000 pUSD), 否则 soft 乘子与硬 cap 口径分叉 (spec Q3)。改一个记得改另一个。
- **per-type ρ 静态表未建**: 组 D 现在用单一 rho_default=0.70。真 ρ 表 (ML↔totals 0.45 / ML↔spreads 0.85…) 待 paper 期 realized correlation 离线校准后回填, 是后续单独事 (synthesis §4 Q4)。
- **exec_margin 系数量纲未标**: k_tox/k_vol 的合理量级要先观测 paper 期 |OFI|/depth 与 RealizedVol 分布 (面板) 再定; §2 的起步值是占位。
- 验证全程 **R-11 paper, 不碰真钱**; 这些旋钮改的是 paper 内 sizing/reservation。
