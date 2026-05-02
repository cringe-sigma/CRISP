# CRISP 实验结果分类摘要（按"测了什么"组织）

> 本文档把仓库中已有的所有实验结果，按 **被测对象 + 测试目的** 归档。
> 平台：i.MX 8M Mini EVK（4×Cortex-A53），主测试驱动 [`multi_proc_pmu`](multi_proc_pmu.c)（PMU cycles / instructions / cache-misses）。
> 配套索引：[`EXPERIMENTS_INDEX.md`](EXPERIMENTS_INDEX.md)。MANUAL.md 实测细节：[`results_manual/MEASUREMENTS.md`](results_manual/MEASUREMENTS.md)。

---

## 总览（按主题分类）

| # | 主题 | 测的是什么 | 数据位置 | 文件数 |
|---|---|---|---|---|
| A | 攻击者**强度筛选** | 哪个 attacker 在每个共享通道（LLC/BUS/MEM）造成的 victim 减速最大 | [`results_manual/e21/`](results_manual/e21) | 7 |
| B | 五配置 **PMC 全扫** | 8 个 victim 在 solo / 单通道×3 / 三通道 mix 下的 cycles 与放大率 a_mix | [`results_manual/e23/`](results_manual/e23) | 40 |
| C | **Lemma 1a 加性** | 双 attacker 是否满足 Δ\_{a+b} ≤ Δ\_a + Δ\_b | [`results_manual/e24/`](results_manual/e24) | 20 |
| D | **Lemma 2 上界** | mix(MAX\_*) 是否 ≥ 旧模板次强三元组 | [`results_manual/e25/`](results_manual/e25) | 8 |
| E | Per-victim **三阶段 hunt** | 每个 victim 的最坏 3-attacker 组合（SOLO3→MIX→CONFIRM） | [`results/hunt/`](results/hunt) + [`results/analysis/`](results/analysis) | 627 + 5 |
| F | Cache **参数 sweep** | LLC 攻击者 workset/stride/RW-ratio/freq-ratio 对 victim 的敏感度 | `results/cache_sweep*`, `results/cache_aggr/`, `results/cache_bus/` | 411 |
| G | Cache 干扰**矩阵** | victim × attacker 全 N×N 配对 | `results/pair_*_vs_*.txt` | 1045 |
| H | Champions **复测** | 各 sweep 胜出配置 N=20 大样本稳态 | [`results/champions/`](results/champions) | 110 |
| I | Mixed **多通道混合** | victim × {ALL\_BUS, ALL\_CACHE, ALL\_MEM, ALL\_PIPE, ALL\_PTR, MIX\_BROAD, MIX\_CACHEBUS, MIX\_HARSH …} | [`results/mixed/`](results/mixed) | 897 |
| J | TIM 模板族 | 自研 TIM 三元组（cache/bus/mem/pointer/pipeline）混合扫 | `results/tim_mix{,2,3}/`, `results/tim_others/` | 1330 |
| K | Phase-3/4 **hybrid hunt** | 20 种混合三元组 → worst-case N=30 复测 | `results/phase3/`, `results/phase4/` | 568 |
| L | Solo **基线** | 8 victim 独占基线（用于历史 ratio 计算） | `results/solo_*.txt` | 8 |
| M | E1–E9 **论文框架** | 静态预筛 / 对手搜索 / 上界对比 / 24h 安全 / 消融 / 调度 / 案例 / RTC / Zephyr | [`experiments/`](experiments) | 16 脚本 |
| N | E6 **10000-set UUniFast** | 6×6 网格 (n∈{10,20,30,40,60,80} × U∈{1.0,1.5,2.0,2.5,3.0,3.5})，每格 10000 任务集，比较 ILP / heur / WFD / FFD 可调度率 | [`experiments/data/processed/e6_scheduling.csv`](experiments/data/processed/e6_scheduling.csv) + [`figs/`](experiments/figs) | 36 行（=360k 任务集） |
| O | E4 **~10? hyperperiod 24h** | 6 victim × triple `MAX_LLC/MAX_BUS/MAX_MEM`，连续 24h 滚动测量 C_obs / C?_RAMPART | [`experiments/data/processed/e4_safety_histogram.csv`](experiments/data/processed/e4_safety_histogram.csv) | 累积写入中（~23 iter/min, 24h≈33k） |

合计：~5500 个 PMC raw dump + 摘要 CSV/MD。

---

## A. 攻击者强度筛选（results\_manual/e21）

- **测什么**：固定 victim=`fmref`，cpu1/2/3 同时跑 3 份相同 attacker；以 σ=cycles/cycles\_solo 比较各候选。
- **候选**：旧模板 `CACHE / MTH_BUS / PR_ROWBUF` vs 自定义 `MAX_LLC / MAX_BUS / MAX_MEM`。
- **结果**：MAX\_BUS σ=1.103，MAX\_MEM σ=1.085，MAX\_LLC σ=1.010；自定义版在 BUS/MEM 两通道领先 5–9pp。
- **驱动**：[MANUAL.md](MANUAL.md) §1（手工 7 步）。

## B. 五配置 PMC 全扫（results\_manual/e23）

- **测什么**：8 victim × {solo, llc=3×MAX\_LLC, bus=3×MAX\_BUS, mem=3×MAX\_MEM, mix=MAX\_LLC+MAX\_BUS+MAX\_MEM}。
- **得到**：每个 victim 的 Δ\_LLC / Δ\_BUS / Δ\_MEM / Δ\_MIX 与 a\_mix。
- **结果（a\_mix 排名）**：insertsort 4.76× · jfdctint 2.85× · fac 2.49× · cosf 1.87× · matrix1 1.35× · fir2dim 1.13× · test3 1.09× · fmref 1.09×。
- **驱动**：[MANUAL.md](MANUAL.md) §2.2（40 行命令）。

## C. Lemma 1a 加性检验（results\_manual/e24）

- **测什么**：6 个 (victim, a, b) 三元组，分别跑 a / b / a+b 三种配置（其余 cpu 离线），比较 Δ\_{a+b} vs Δ\_a+Δ\_b。
- **结果**：2 严格 OK（fir2dim/LLC+BUS, fmref/LLC+MEM）；2 噪声违例（cosf, test3，单子项 ≤ 测量噪声）；**2 真违例**（jfdctint/BUS+MEM 12×、fac/LLC+BUS）。
- **结论**：RAMPART 引用 Lemma 1a 时需对 BUS/MEM 协同敏感的 victim 加显式 slack。
- **驱动**：[MANUAL.md](MANUAL.md) §3（20 行命令）。

## D. Lemma 2 上界检验（results\_manual/e25）

- **测什么**：8 victim 在 triple2 = `CACHE MTH_BUS PR_ROWBUF`（旧模板"次强家族"）下的 cycles，与 §B 的 mix 比。
- **结果**：**8/8 通过** mix ≥ triple2，比值 1.04–4.56。
- **结论**：a\_max 取 mix(MAX\_*) 即可作为族内最强。
- **驱动**：[MANUAL.md](MANUAL.md) §4（8 行命令）。

## E. Per-victim 三阶段 hunt（results/hunt + results/analysis）

- **测什么**：56 个 TACLeBench victim，每个跑 20 个单源 attacker 的 SOLO3，挑 top-3 → 5 种 mix → N=20 confirm。
- **驱动**：[`run_per_victim_hunt.sh`](run_per_victim_hunt.sh) → [`analyze_hunt.py`](analyze_hunt.py)。
- **结果**（[`results/analysis/REPORT.md`](results/analysis/REPORT.md)）：52/56 验证通过、17 个 S≥1.05、最坏 `fir2dim × POINTER? = 1.55×`。
- **配套表**：[`validated_champions.csv`](results/analysis/validated_champions.csv), [`category_stats.csv`](results/analysis/category_stats.csv), [`attacker_template_stats.csv`](results/analysis/attacker_template_stats.csv)。

## F. Cache 参数 sweep（cache\_sweep / cache\_sweep2 / cache\_aggr / cache\_bus）

- **测什么**：把 CACHE/BUS 攻击者的 workset、stride、读写比、frequency ratio 等参数在大范围扫，量化对 victim cycles 的敏感度。
- **变体**：
  - [`cache_sweep`](results/cache_sweep) (48)：L1/L2/LLC/DRAM × stride × R/W；
  - [`cache_sweep2`](results/cache_sweep2) (115)：256K–64M、stride 32–128、wr=10/50/90；
  - [`cache_aggr`](results/cache_aggr) (124)：写主导 1M–16M, stride 1–1024；
  - [`cache_bus`](results/cache_bus) (124)：BUS 1M–4M × 多种 attacker 拓扑。
- **驱动**：`run_cache_sweep.sh`, `run_cache_sweep2.sh`, `run_cache_aggressive.sh`, `run_cache_bus_combo.sh`。每目录附 `baseline.tsv` + `configs.tsv` + `harness.txt`。
- **分析**：[`analyze_results.py`](analyze_results.py), [`extract_configs.py`](extract_configs.py)。

## G. Cache 干扰矩阵（results/pair\_*）

- **测什么**：早期 victim×attacker 全 N×N 配对（包含 victim×victim 自互扰）。
- **驱动**：[`run_cache_interference.sh`](run_cache_interference.sh), [`run_interference_matrix.sh`](run_interference_matrix.sh)。
- **数据**：1045 个 `pair_<v>_vs_<a>.txt`。

## H. Champions 复测（results/champions）

- **测什么**：把 sweep / hunt / TIM 阶段中胜出的 attacker 配置取出，N=20 大样本复测稳态 ratio。
- **驱动**：[`run_champions.sh`](run_champions.sh)。
- **摘要**：[`detail.tsv`](results/champions/detail.tsv) 含 `solo_<v>` / `pair_<v>` / `ratio_<v>` 三列。

## I. Mixed 多通道混合（results/mixed）

- **测什么**：每个 victim 在 8 大类预设混合下的减速：`ALL_BUS` / `ALL_CACHE` / `ALL_MEM` / `ALL_PIPE` / `ALL_PTR` / `MIX_BROAD` / `MIX_CACHEBUS` / `MIX_HARSH`。
- **驱动**：[`run_mixed_attack_sweep.sh`](run_mixed_attack_sweep.sh) → [`analyze_mixed_sweep.py`](analyze_mixed_sweep.py)。
- **日志**：[`mixed_sweep.log`](results/mixed_sweep.log) 963 行。

## J. TIM 模板族扫（tim\_mix / tim\_mix2 / tim\_mix3 / tim\_others）

- **测什么**：自定义 TIM（CACHE/BUS/MEM/POINTER/PIPELINE）的三元组组合扫，不同版本递进加宽参数空间。
- **变体**：
  - [`tim_mix`](results/tim_mix) (238)：基础三元组；
  - [`tim_mix2`](results/tim_mix2) (358)：加宽参数；
  - [`tim_mix3`](results/tim_mix3) (352)：同/异类 TIM 混合；
  - [`tim_others`](results/tim_others) (382)：POINTER/PIPELINE 等非 cache/bus TIM。
- **驱动**：`run_tim_mix.sh`, `run_tim_mix2.sh`, `run_tim_mix3.sh`, `run_tim_others.sh`。

## K. Phase-3 / Phase-4 hybrid hunt（results/phase3, phase4）

- **测什么**：把 hunt + sweep 的洞察组合成 20 种 hybrid 三元组（含 PR\_*、MTH\_*），覆盖 multi-victim；Phase-4 把 worst-case 三元组 N=30 复测。
- **驱动**：[`run_phase3_hybrid.sh`](run_phase3_hybrid.sh), [`run_phase4_minhunt.sh`](run_phase4_minhunt.sh)。
- **摘要**：[`phase3/sweep_summary.csv`](results/phase3/sweep_summary.csv), [`phase4/sweep_summary.csv`](results/phase4/sweep_summary.csv)。
- **日志**：[`phase3_run.log`](results/phase3_run.log) 558 行；[`phase4_run.log`](results/phase4_run.log) 54 行。

## L. Solo 基线（results/solo\_*）

- **测什么**：8 个 victim 在 cpu1-3 离线的独占基线（adpcm\_dec, binarysearch, fir2dim, fmref, huff\_dec, iir, insertsort, statemate）。
- **用途**：早期所有 ratio 计算的分母。

## M. E1–E9 论文自动化框架（experiments/）

| 步 | 脚本 | 测什么 |
|---|---|---|
| E1.a | [01\_phase1/extract\_loopbounds.py](experiments/01_phase1/extract_loopbounds.py) | 抽 `_Pragma loopbound` 形成 victim 候选集 |
| E1.b | [01\_phase1/static\_estimates.py](experiments/01_phase1/static_estimates.py) | 估算 alpha\_l2\_static 静态特征 |
| E2.1 | [02\_phase2/e21\_stress\_validate.py](experiments/02_phase2/e21_stress_validate.py) | 攻击者强度验证（与 §A 对应） |
| E2.2 | [02\_phase2/e22\_adversary\_search.py](experiments/02_phase2/e22_adversary_search.py) | 自动三元组对手搜索 |
| E2.3 | [02\_phase2/e23\_five\_config\_sweep.py](experiments/02_phase2/e23_five_config_sweep.py) | 五配置全扫（与 §B 对应） |
| E2.4 | [02\_phase2/e24\_lemma1a.py](experiments/02_phase2/e24_lemma1a.py) | Lemma 1a 验证（与 §C 对应） |
| E2.5 | [02\_phase2/e25\_lemma2.py](experiments/02_phase2/e25_lemma2.py) | Lemma 2 验证（与 §D 对应） |
| E3.1 | [03\_bounding/e31\_polyrhythm\_compare.py](experiments/03_bounding/e31_polyrhythm_compare.py) | 与 PolyRhythm 上界对比 |
| E3.2 | [03\_bounding/e32\_mempol\_compare.py](experiments/03_bounding/e32_mempol_compare.py) | 与 MemPol 对比 |
| E3.3 | [03\_bounding/e33\_additive\_violations.py](experiments/03_bounding/e33_additive_violations.py) | 加性违例统计 |
| E4 | [04\_24h/e4\_safety\_histogram.py](experiments/04_24h/e4_safety_histogram.py) | 24h 安全裕度直方图 |
| E5 | [05\_ablation/e5\_ablation.py](experiments/05_ablation/e5_ablation.py) | 组件消融 |
| E6 | [06\_scheduling/e6\_schedule.py](experiments/06_scheduling/e6_schedule.py) | 调度器集成 |
| E7 | [07\_case\_study/e7\_case\_study.py](experiments/07_case_study/e7_case_study.py) | 案例研究 |
| E8 | [08\_rtc/e8\_rtc.py](experiments/08_rtc/e8_rtc.py) | RTC 端到端 |
| E9 | [09\_zephyr/e9\_zephyr.py](experiments/09_zephyr/e9_zephyr.py) | Zephyr 端口 |

入口：[experiments/run\_all.py](experiments/run_all.py)；复现说明：[experiments/REPRO.md](experiments/REPRO.md)；日志：`experiments/run_log/`。

---

## N. E6 10000-set UUniFast 调度（已完成）

- **测什么**：用 UUniFast 在 6×6 网格 (n∈{10,20,30,40,60,80} × U∈{1.0,1.5,2.0,2.5,3.0,3.5}) 上各生成 **10000** 任务集，对每个集运行 4 个调度器并统计可调度率。
- **总样本**：36 × 10000 = **360 000** 任务集。
- **驱动**：`python3 experiments/06_scheduling/e6_schedule.py --n-sets 10000`。
- **结果**（[`e6_scheduling.csv`](experiments/data/processed/e6_scheduling.csv)，36 行）：
  - 平均可调度率：ILP 0.9751，**heur 0.9802**（含 5% slack 折扣），WFD 0.9751，FFD 0.9752。
  - 紧场景：`n=10, U=3.5` 全部跌到 0.53–0.61，heur 仍领先 8pp。
  - 4-core 容量饱和点：U≥2.5 时 n=10 子集已显著难调度，n≥20 因任务粒度细而稳定 ≥0.96。
- **图**：[`figs/e6_schedulability.svg`](experiments/figs/e6_schedulability.svg), [`figs/e6_ilp_runtime.svg`](experiments/figs/e6_ilp_runtime.svg)。

## O. E4 ~10? hyperperiod 24h 安全直方图（运行中）

- **测什么**：6 个 case-study victim (`fir2dim, fmref, cosf, test3, jfdctint, fac`) 轮流在 cpu1-3 三攻击者 `MAX_LLC MAX_BUS MAX_MEM` 下连续测 24 h，记录每次 R=20 的 cycles 中位数与 RAMPART 上界 (γ=1.02) 的比值，形成安全直方图与违例计数。
- **驱动**：`python3 experiments/04_24h/e4_safety_histogram.py --duration-s 86400 --R 20 --attackers MAX_LLC MAX_BUS MAX_MEM`（已 nohup 后台启动）。
- **依赖**：[`experiments/data/processed/e3_bounds.csv`](experiments/data/processed/e3_bounds.csv) — 由本次实测 §B 的 mix cycles × γ=1.02 生成。
- **进度**：CSV 持续追加（前 4 分钟已写入 70 行）；预计 24 h ≈ 33000 iterations。
- **复盘命令**：
  ```bash
  wc -l experiments/data/processed/e4_safety_histogram.csv      # 当前迭代数
  awk -F, 'NR>1{n++; if($6=="True") v++} END{print n,v,v/n}' \
      experiments/data/processed/e4_safety_histogram.csv         # 总数 / 违例 / 违例率
  pgrep -af e4_safety_histogram                                  # 是否仍在跑
  ```
- **完成标志**：进程退出 + `figs/e4_safety_histogram.svg` 生成。

---

## 复现入口

| 想复现哪部分 | 怎么做 |
|---|---|
| MANUAL.md 75 个 PMC dump（A–D）| 照 [MANUAL.md](MANUAL.md) §0–§4 逐行；批处理见 [results\_manual/MEASUREMENTS.md](results_manual/MEASUREMENTS.md) §7 |
| 历史 sweep（E–L） | 单跑对应 `run_*.sh`，输出落到 `results/<name>/` |
| 论文 E1–E9（M） | `cd experiments && python3 run_all.py --all`，先 `export SUDO_PW=...` |
