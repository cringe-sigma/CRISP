# RAMPART 实验执行总规划（iMX 8M Mini + Raspberry Pi 5）

> 中文版同步副本：[experiment-plan-CN.md](experiment-plan-CN.md)
> 本文档汇总论文 [RAMPART-paper-EN.md](RAMPART-paper-EN.md) §3、§6 在两块真实硬件上要做的全部实验：每项实验的 **目的 → 测试集 → 步骤 → 跑量 → 数据分析 → 产出图表**。
> 论文骨架：实验在 **i.MX 8M Mini EVK（主平台，53 个 TACLeBench + 8 EEMBC + 4 DNN）** 和 **Raspberry Pi 5（跨平台，20 子集 + 4 EEMBC + 2 DNN）** 上同步进行；前者覆盖完整工作流，后者作 **方法迁移性验证**。
> 工程仓库映射见 [experiments/README.md](experiments/README.md)。

---

## 0. 平台与共用前置

| 项 | i.MX 8M Mini EVK | Raspberry Pi 5 |
|---|---|---|
| 核 | 4× Cortex-A53 @ 1.8 GHz（顺序） | 4× Cortex-A76 @ 2.4 GHz（乱序） |
| L2 / L3 | 512 KB 共享 16-way | 512 KB 私有 + 1 MB L3 共享 16-way |
| DRAM | LPDDR4 @ 3.0 GHz | LPDDR4X @ 4.27 GHz |
| OS | Linux 6.1 PREEMPT_RT | Linux 6.6 PREEMPT_RT |
| PMU | A53 PMUv3 | A76 PMUv3（事件号不同） |
| γ_PMU | 1.02（A53 errata） | 1.04（A76 errata） |

**共用前置（两块板各做一次）**：

1. 烧写 PREEMPT_RT 内核，关闭 CPU idle / DVFS（governor=performance），关闭散热降频。
2. 关闭硬件预取：A53 写 `CPUACTLR_EL1`；A76 写 `CPUACTLR2_EL1`。
3. 中断、ksoftirq 全部 pin 到 core 0；measurement 跑在 core 1；攻击者跑 core 2/3。
4. 编译：`-O2 -static`，`arm-linux-gnueabihf-gcc`（A53）/`aarch64-linux-gnu-gcc`（A76）。
5. 配置文件：`experiments/config/platform_imx8mm.yaml`、新增 `platform_pi5.yaml`（事件号、L^ρ、cache 几何按 A76 TRM 改）。

**测试集（合计）**：

| 套件 | i.MX | Pi 5 | 备注 |
|---|---|---|---|
| TACLeBench | 53（全集） | 20（分层抽样，seed=42） | 主基准 |
| EEMBC AutoBench | 8 (`a2time, aifftr, aiifft, basefp, bitmnp, cacheb, canrdr, iirflt`) | 4 | 工作负载多样性 face |
| TFLite-Micro DNN | 4 (`person_detect, kws_micro, magic_wand, mnist_cnn`) | 2 (`person_detect, kws_micro`) | 高 α 极端 face |
| 合成任务集 | 200 套 (n∈[10,80]) | 200 套 (n∈[10,40]) | §6.5 调度评估 |

---

## 实验 E1 — Phase 1 静态预筛（§3.1）

**目的**：得到每个 benchmark 的静态 memory-access 上界 $N_i^{\text{mem}}$、$\hat\alpha_i^{\rho,\text{static}}$，作为 Phase 2 的种子和 sanity bound。

**测试集**：53 + 8 + 4 = 65（i.MX）；20 + 4 + 2 = 26（Pi 5）。可在主机交叉编译，不占板。

**步骤**：
1. `extract_loopbounds.py`：解析 `_Pragma("loopbound min X max Y")` → `loopbounds.json`。
2. `arm-…-gcc -O2 -S` 生成汇编，统计每个循环体内 load/store 指令数 $m_l$。
3. `static_estimates.py`：$N_i^{\text{mem}} = \sum_l \max_l \cdot m_l$；用 cache 几何 clip 得到 $N_i^{L2}$。

**跑量**：每 benchmark 一次，CPU 时间 < 1 min。

**分析**：将 $\hat\alpha_i^{\rho,\text{static}}$ 与 E2 测得 $\alpha_i^\rho$ 对比，几何均值倍率为"静态悲观度"。

**产出**：`data/processed/static_estimates.csv` → **Figure 4**（Phase-1 vs Phase-2 散点，两面板）。

---

## 实验 E2 — Phase 2 PMC 采集（§3.2）

**目的**：在压力诱导下采得每个任务的核心 3 维 (α^L2, α^BUS, IPC) + 辅助 3 维 (TLB, BR, CV) PMC 包络；同步获得 `_Mix_` 配置下的 amplification scalar $a_i^{\max}$。

**测试集**：同 E1，每平台全跑。

### E2.1 Stress task 验证

**步骤**：
- 单独运行 `stress_llc / stress_bus / stress_mem`，`perf stat` 测吞吐。
- 验收：单通道吞吐 ≥ 硬件理论极限 85%，否则按 `data-guide.md §5.3` 调整 `mem_size / stride / rw_ratio / SIMD`。

**跑量**：每个 stress 任务 5 次 × 10 秒。

### E2.2 Adversary Search（Algorithm 1）

**目的**：对每个 (τ_i, ρ_k) 用 bounded coordinate descent 找到 θ_k*，使 victim 上的 PMC 速率收敛。

**步骤**（每 (τ_i, ρ_k) 一遍）：
1. 初始 6^|θ| 网格 → 取中心。
2. 第 g 轮：在 θ 邻域取 `coordinate_grid(θ, step_g)`；step_g 在上一轮变化最大的维上减半。
3. 每个候选 θ′ 跑 R=1000 次：victim on core 1，attacker S_k(θ′) on core 2/3/(4)；记录 α′(θ′) = max_r PMC(ρ_k) / min_r cycles。
4. θ ← argmax；若连续两轮 |Δα|/α < ε=0.02 → 停。
5. Γ ≤ 6 → 单 (τ, ρ) 至多 1200 次 kernel 评估。

**跑量**：
- i.MX：65 benchmarks × 3 channels × ~600 evals × 1 s ≈ **32 小时**（实际两轮收敛即可，中位 18–22 h）。
- Pi 5：26 benchmarks × 3 channels × ~600 evals × 0.7 s ≈ **9 小时**。

**分析**：记录每轮 α^(g)；画 4 个代表 benchmark（每类 1 个）的收敛曲线 → **Figure 2**。
后处理：用 finite-difference 估计 Lipschitz $L_\alpha$，验证 Proposition 1（i.MX ≤ 0.42，Pi 5 ≤ 0.51）。

**产出**：`data/processed/adversary_optimal_theta.csv`（每 (bench, channel) 一行 θ*、α、L_α）。

### E2.3 Five-config PMC sweep

**目的**：以 θ_k* 为定值，跑 5 种 stress 配置 × 3 轮事件组 × 1000 次重复。

**配置矩阵**（每 benchmark 5×3=15 组）：

| Config | core1 (victim) | core2 | core3 | core4 |
|---|---|---|---|---|
| Solo | τ_i | idle | idle | idle |
| LLC | τ_i | S_LLC(θ*) | idle | idle |
| BUS | τ_i | S_BUS(θ*) | idle | idle |
| MEM | τ_i | S_MEM(θ*) | idle | idle |
| **Mix** | τ_i | S_LLC(θ*) | S_BUS(θ*) | S_MEM(θ*) |

**三轮事件组**（每轮一次重启 perf）：
- R1：`cycles, L2D_CACHE_REFILL, BUS_ACCESS, INST_RETIRED` （bound）。
- R2：`cycles, L1D_TLB_REFILL, BR_MIS_PRED, L1D_CACHE_REFILL` （taxonomy）。
- R3：`perf record -e L2D_CACHE_REFILL -c 10000` 时间戳采样 → CV。

**跑量**：
- i.MX：65 × 5 × 3 × 1000 ≈ 975 000 次执行，平均 0.3 s/次 → **约 81 小时（连续 3.5 天）**。
- Pi 5：26 × 5 × 3 × 1000 ≈ 390 000 × 0.2 s → **约 22 小时**。

**分析**：
- 取 R=1000 中的 **max** 作 α_i^ρ（论文口径）；同时记录 max/p99.9/mean/std/CV。
- $\alpha_i^{\rho_k} = \max_r \text{PMC}(\rho_k) / \min_r \text{cycles}$（Solo/LLC/BUS/MEM 各一个值）。
- $a_i^{\max} = (\text{cycles}_{\text{Mix}} - C_i^{\text{solo}}) / \sum_k \Delta_i^{\rho_k}$ 的最大单次比值（Mix vs 单通道）。

**产出**：
- `data/raw/r{1,2,3}_{bench}_{config}.csv`
- `data/processed/pmc_aggregated.csv` （per-bench × per-config 包络）
- `data/processed/feature_vectors.csv` （3+3 向量）
- `data/processed/amplification_scalars.csv` （a_i^max 每任务一行）
- → **Figure 3**（3+3 heatmap，两面板）。

### E2.4 Lemma 1a 校验（attacker dominance）

**目的**：证明合成攻击者 S_k(θ*) 在单通道上比真实 co-runner 任务更"凶"。

**步骤**：随机选 53×3 = **159 对 (i.MX) / 20×3 = 60 对 (Pi 5)** (victim, real co-runner) — co-runner 取自 TACLeBench 自身；测 α_real 并对比 α_S。

**跑量**：每对 R=1000，约 5 小时（i.MX）/ 2 小时（Pi 5）。

**分析**：检查 `α_real / α_S < 1`；论文报告：i.MX 最大 0.67，Pi 5 最大 0.81，**0 / 159 与 0 / 60 violation**。

### E2.5 Lemma 2 校验（Mix dominance）

**目的**：验证三任务 co-runner triple 下 $a_i^{\text{real}} \le a_i^{\max}$。

**步骤**：每平台均匀随机抽 **500** 个三任务三元组（`numpy.random.default_rng(42)`）；victim 在 core 1，三个 co-runner 在 core 2/3/4；R=1000，记录最大 amplification ratio。

**跑量**：500 × 1000 × 0.3 s ≈ **42 h（i.MX）/ 28 h（Pi 5）**。

**附加**：对 §6.5 case study 任务集，枚举所有 hyperperiod-pairwise phase offset，重测 worst-phase 下 $a_i^{\text{real}}$。

**分析**：Clopper–Pearson 99% 上界；论文：每平台 9.2×10??，合并 4.6×10??。

---

## 实验 E3 — Bound 计算与 Tightness 评估（§6.3）

**目的**：把 E2 输出代入 Theorem 1 的 $\widehat C_i = \gamma_{\text{PMU}}[C_i^{\text{solo}} + \sum_k \Delta_i^{\rho_k} + A_i]$ 与各 baseline 公式，画 CDF。

**测试集**：与 E2 一致。

**Baseline 列表**：
1. Full-isolation（每通道按最坏攻击者全计入）。
2. Kim'16 [11]（DRAM-tight + additive）。
3. Hassan'18 [18] （DRAM-tight + additive）。
4. Sullivan'24 [8] （LLC-tight + additive）。
5. **RAMPART-additive**（去掉 $A_i$）。
6. **RAMPART-full** （含 $A_i$）。
7. Empirical max（实测下界参考）。

**步骤**：
1. 用 aiT/OTAWA 或 release 的 static $C_i^{\text{solo}}$（论文不重做单核 WCET）。
2. `03_bounding/interference_bound.py` 计算 7 条 bound × 65/26 任务。
3. Bootstrap 10 000 次重采样得中位数 95% CI；与 RAMPART 做配对 Wilcoxon signed-rank。

**分析**：
- 中位 $\widehat C_i / C_i^{\text{solo}}$ 与 CI（论文 Table 3）。
- p-value 对 Sullivan'24：i.MX `<10??`，Pi 5 `<10??`。

**产出**：**Figure 5（CDF，两面板）**、**Table 3**。

### E3.1 PolyRhythm 攻击器对比（§6.3.1）

**测试集**：每平台 10 个分层子集（每类 1–2 个）。

**步骤**：用 PolyRhythm 公开 hill-climber 替换 Algorithm 1，重采 α 与 $a_i^{\max}$；比较 envelope 差异。

**跑量**：10 × 3 × 600 evals ≈ 5 h / 平台。

**分析**：i.MX 中位差 2.7%、最大 4.9%；Pi 5 3.4% / 6.1%；§6.3.2 violation 数应稳定 7/53 与 3/20。

### E3.2 MemPol shield 对比（§6.3.1）

**测试集**：§6.5 fault-injection workload。

**步骤**：把 §4.3 的 victim-priority activation rule 接到三种后端：(i) RAMPART MemGuard hook、(ii) MemPol PMU-overflow、(iii) standing MemGuard。

**跑量**：每平台 200 trials × 3 backends。

**分析**：
- 激活延迟：RAMPART 3.0/2.1 ?s、MemPol 4.2/3.3 ?s。
- $\Delta C_i^{e^\star}$ 是否 honored；24 h co-runner throughput cost。
- 产出 **Table 4**。

### E3.3 加性合成反例（§6.3.2，论文核心实证）

**测试集**：53 i.MX + 20 Pi 5 全集。

**步骤**：
1. 24 小时连续运行：每个 victim 在所有可能三任务 co-runner triple 下采 $C_i^{\text{obs\_max}}$（取 worst triple）。
2. 计算 $C_i^{\text{obs\_max}} / \widehat C_i^{\text{add}}$（去 $A_i$ 的 bound）。

**跑量**：24 h × 2 平台 = 48 h（论文规定）。

**分析**：
- 反例计数：i.MX 7/53 (13.2%)、Pi 5 3/20 (15%)。
- 最大 violation：i.MX 1.18× (`lift`)、Pi 5 1.22× (`adpcm_dec`)。
- 类别：MX/PV 集中。
- 同步检查 RAMPART-full：0 violation。

**产出**：**Figure 7（散点，两面板）**、**Table 5**。

---

## 实验 E4 — 24 h Safety Histogram（§6.3 Figure 6）

**目的**：在 §6.5 case study 部署上跑 ~10? hyperperiod，画 $C_i^{\text{obs\_max}} / \widehat C_i^{\text{RAMPART}}$ 直方图，证明 0 violation。

**测试集**：6 任务混合关键性集合（2 CS + 1 MX + 1 PV + 1 CH + 1 CB）。

**跑量**：24 h × 2 平台。

**分析**：右端 bin：i.MX 0.91、Pi 5 0.93；Lemma 1b online monitor 计数 0/10?。

**产出**：**Figure 6**。

---

## 实验 E5 — Ablation（§6.4）

**目的**：拆解每个 Optimization 的贡献。

**测试集**：E2/E3 同集。

**配置**：
1. Full isolation。
2. +Opt-1（type exclusion）。
3. +Opt-1+Opt-2（stall discount）。
4. RAMPART-additive（含 Opt-1–3，γ_PMU 已乘，无 $A_i$）。
5. RAMPART-full（再加 $A_i$）。

**步骤**：仅在 `03_bounding/` 离线计算，不重测板。

**分析**：每行报中位 bound 比 + ΔU vs 全隔离（i.MX 21%/19%、Pi 5 17%/15%）。

**产出**：**Table 6**。

---

## 实验 E6 — 调度质量评估（§6.5）

**目的**：把 $\widehat C_i$ 喂给 ILP / heuristic / WFD / FFD 调度器，比较 schedulable utilization。

**测试集**：每平台 200 个合成任务集（基于 TACLeBench 的 utilization 与 period mix；n ∈ {10, 20, 30, 40, 60, 80}）。

**步骤**：
1. `04_scheduling/ilp_scheduler.py`（Gurobi）解最优 π*，10 min time-limit。
2. `heuristic_scheduler.py` 跑 RAMPART-heuristic、WFD、FFD。
3. `schedulability.py` 用 RM/EDF 充分条件判定。

**跑量**：
- ILP 单解：n=10→0.3 s, n=20→4.1 s, n=30→47 s, n=40→12 min。
- 200 任务集 × 4 调度器 ≈ 主机 24 h。

**分析**：
- Schedulable U vs system U 曲线（4 条线）。
- ILP vs heuristic 差距：n≤40 平均 2.8%，n=80 4.1%。
- vs additive baseline：i.MX +27%（投影） / Pi 5 实测 **+17.4%**。

**产出**：**Figure 8（schedulability 曲线）**、**Figure 9（ILP runtime）**。

---

## 实验 E7 — Case Study + 故障注入（§6.5）

**目的**：在真实硬件上跑 6 任务集 24 h，并故障注入测 §4.3 shield。

**测试集**：6 任务（2 CS + 1 MX + 1 PV + 1 CH + 1 CB）。

**步骤**：
1. 按 RAMPART ILP 给定 π* 部署；安全网阈值 η=0.05。
2. 24 h 连续运行，记录 deadline miss、shield 激活次数、$C_i^{\text{obs\_max}}$。
3. **故障注入**：每 5×10? release 注入一次 Lemma-2-violating co-runner triple；记录 transient overshoot Δ$C_i^{e^\star}$，shield 是否在下一 release 恢复 bound。

**跑量**：24 h × 2 平台 + 200 trials × 2 平台 fault injection。

**分析**：
- 0 deadline miss；η=0.05 时 0 shield activation（无故障注入条件下）。
- HRT (Tier-A) 资格：i.MX 5/6（MX task 在 Pi 5 上落 Tier-B）。
- 故障注入：max transient i.MX 281 ?s / Pi 5 198 ?s（< 解析 318 / 226 ?s 上界）；throughput cost 0.7% / 0.9%。

**产出**：case-study 报告 + **Figure 6** 数据来源。

---

## 实验 E8 — RTC sup-convolution 对照（§7.1）

**目的**：在 i.MX 上测量 RAMPART $\sum + A_i$ 与形式化 $\otimes$ bound 的数值差距。

**测试集**：10 benchmark spot check。

**步骤**：
1. 4 小时 cross-stage service curve 标定（per-stage 服务曲线）。
2. 实例化 [14] 的 $\otimes$ bound。
3. 与 RAMPART 比较 per-benchmark 差距。

**跑量**：4 h 标定 + 10 benchmarks 计算。

**分析**：i.MX 中位差 3.1%、最大 6.4%；Pi 5 3.7% / 7.1%；RAMPART 在 4/10 benchmark 更紧、6/10 更松。

**产出**：**Figure 10（两面板）** + 补充材料中 4 h 协议表。

---

## 实验 E9 — Zephyr RTOS 迁移性（§6.1 末段）

**目的**：证明 Linux PREEMPT_RT → 分区 RTOS 不需重新标定。

**测试集**：10 benchmark Zephyr 端口。

**步骤**：在 Zephyr 下重测 α^L2, α^BUS。

**跑量**：每 bench R=1000，共 ~5 h。

**分析**：与 Linux 数据偏差 ≤ 2.3%。

**产出**：补充材料一行，正文一句话。

---

## 时间预算总览

| 实验 | i.MX 板时 | Pi 5 板时 | 主机时 |
|---|---|---|---|
| E1 静态预筛 | — | — | < 1 h |
| E2.1 stress 验证 | 1 h | 1 h | — |
| E2.2 adversary search | 22 h | 9 h | — |
| E2.3 五配置 PMC 全扫 | 81 h | 22 h | — |
| E2.4 Lemma 1a | 5 h | 2 h | — |
| E2.5 Lemma 2 (500 triples + phase 枚举) | 50 h | 32 h | — |
| E3 bound 计算 | — | — | 2 h |
| E3.1 PolyRhythm | 5 h | 5 h | — |
| E3.2 MemPol | 6 h | 6 h | — |
| E3.3 加性反例 24 h sweep | 24 h | 24 h | — |
| E4 safety histogram (与 E7 共享) | 24 h | 24 h | — |
| E5 ablation | — | — | 1 h |
| E6 调度评估 | — | — | 24 h |
| E7 case study + fault injection | 30 h | 30 h | — |
| E8 RTC 对照 | 4 h | — | 1 h |
| E9 Zephyr | 5 h | — | — |
| **合计板时** | **~257 h（11 天）** | **~155 h（6.5 天）** | **~30 h** |

> 提示：E2.3、E3.3、E4/E7 可串接同一 24 h 长跑窗口；E2.5 与 E3.1/E3.2 可并行（不同 victim 互不干扰）。

---

## 数据流与可重现性

```
Phase 1 静态  ─┐
              ├─→ feature_vectors.csv ─→ task_classes.csv
Phase 2 PMC ──┘                          │
              └─→ amplification_scalars.csv
                       │
                       ▼
            03_bounding ─→ bounds.csv ─→ Fig 4/5/6/7, Table 3/5/6
                       │
                       ▼
            04_scheduling ─→ assignment_*.json ─→ Fig 8/9
                       │
                       ▼
            05_evaluation ─→ 24 h logs ─→ Table 4, Fig 6/10
```

**随机种子**：所有 stratified sampling 与合成任务集均用 `numpy.random.default_rng(42)`。
**版本钉死**：Linux 内核、gcc 版本、`taclebench` commit hash、`perf` 版本写入 `experiments/REPRO.md`（待补）。
**原始数据归档**：`data/raw/` 只追加，发布时随补充材料上传。

---

## 与论文章节的最终映射

| 论文位置 | 对应实验 | 关键产出 |
|---|---|---|
| §3.1 Phase 1 | E1 | static_estimates.csv |
| §3.2 Phase 2 + Algorithm 1 | E2.1–E2.3 | feature_vectors, amplification_scalars |
| §3.3 6-class taxonomy | E2.3 后处理 | task_classes.csv |
| Lemma 1a / 1b / 2 验证 | E2.4 / E4 / E2.5 | violation 计数表 |
| §6.1 Setup | 共用前置 | Table 平台 |
| §6.2 Profiling Results | E1 + E2 | Fig 2, 3, 4 |
| §6.3 Bound Tightness | E3 | Fig 5, Table 3 |
| §6.3.1 Spot checks | E3.1, E3.2 | Table 4 |
| §6.3.2 加性反例 | E3.3 | Fig 7, Table 5 |
| Fig 6 24 h safety | E4 (≡ E7 piggy-back) | Fig 6 |
| §6.4 Ablation | E5 | Table 6 |
| §6.5 Scheduling | E6 + E7 | Fig 8, 9 + case study |
| §7.1 RTC ? 对照 | E8 | Fig 10 |
| §6.1 Zephyr 一行 | E9 | 一句话 + 补充材料 |
