# CRISP 项目实验结果索引

> 总览本仓库已有的所有实验、对应脚本、原始数据位置及关键结论。
> 平台：i.MX 8M Mini EVK（4×Cortex-A53），主控 `multi_proc_pmu`（PMU cycles/instructions/cache-misses）。

## 0. 顶层目录速览

| 目录/文件 | 说明 |
|---|---|
| [`multi_proc_pmu.c`](multi_proc_pmu.c) | 主测试驱动（cpu0=victim, cpu1..3=attackers/可热拔） |
| [`bench/`](bench) | TACLeBench + 自定义 TIM 攻击者源码树 |
| [`build/`](build) | 各 victim/attacker 的编译产物 + `bench_registry.h` |
| [`Makefile`](Makefile) | 一键 `make multi_proc_pmu` / `make benches` |
| [`MANUAL.md`](MANUAL.md) | **人工复现手册 v2**（83 步, 自定义 MAX_LLC/MAX_BUS/MAX_MEM） |
| [`results_manual/`](results_manual) | **MANUAL.md 实测数据**（本次新增；75 PMC dump + 报告） |
| [`results/`](results) | 历史所有自动化实验结果（共约 5000+ PMC dump） |
| [`experiments/`](experiments) | 论文 E1–E9 自动化框架（脚本+REPRO） |

---

## 1. MANUAL.md 实测结果（手册逐步对应）

文件：[`results_manual/`](results_manual)。每个子目录与 MANUAL.md 章节一一对应。

| 手册章节 | 输出目录 | 文件数 | 内容 |
|---|---|---|---|
| §1 E2.1 攻击者强度筛选 | [`results_manual/e21/`](results_manual/e21) | 7 | victim=fmref，对比 SOLO / CACHE / MAX_LLC / MTH_BUS / MAX_BUS / PR_ROWBUF / MAX_MEM 的 σ |
| §2 E2.3 五配置 PMC 全扫 | [`results_manual/e23/`](results_manual/e23) | 40 | 8 victim × {solo, llc, bus, mem, mix} |
| §3 E2.4 Lemma 1a 抽样 | [`results_manual/e24/`](results_manual/e24) | 20 | 单/双 attacker 配对，验证 Δ_{a+b} ≤ Δ_a+Δ_b |
| §4 E2.5 Lemma 2 抽样 | [`results_manual/e25/`](results_manual/e25) | 8 | mix(MAX_*) vs triple2(CACHE/MTH_BUS/PR_ROWBUF) |
| §5–§6 聚合与报告 | [`results_manual/MEASUREMENTS.md`](results_manual/MEASUREMENTS.md) | 1 | 平台指纹 + 全部表格 + 判定 + RAMPART 上界 |

**关键实测数据（本次执行）：**

- §1 σ：MAX_BUS=1.103 / MAX_MEM=1.085 / MAX_LLC=1.010（自定义 MAX_BUS/MAX_MEM 比旧模板高 8.7pp / 5.4pp）。
- §2 a_mix（按 mix/solo）：`insertsort=4.76×` (最坏), `jfdctint=2.85×`, `fac=2.49×`, `cosf=1.87×`, `matrix1=1.35×`, `fir2dim=1.13×`, `test3=1.09×`, `fmref=1.09×`。
- §3 Lemma 1a：6 三元组中 2 严格 OK，2 噪声违例，2 真违例（jfdctint/BUS+MEM, fac/LLC+BUS）→ RAMPART 引用 Lemma 1a 时需补 slack。
- §4 Lemma 2：**8/8 通过**，mix/triple2 比值 1.04–4.56。
- §5 RAMPART：$C^{\text{rampart}}/C^{\text{solo}}$ 全板上界 = **4.854×**（insertsort, γ=1.02）。

复现入口：直接按 [`MANUAL.md`](MANUAL.md) §0–§4 逐行执行；或用本次保留的批处理脚本（见 [`results_manual/MEASUREMENTS.md`](results_manual/MEASUREMENTS.md) §7）。

---

## 2. 论文 E1–E9 自动化框架

目录：[`experiments/`](experiments)。每一步都通过 [`experiments/run_all.py`](experiments/run_all.py) 串联，运行日志写入 `experiments/run_log/<step>__<UTC>.log` 和 `manifest.jsonl`。

| 步骤 | 脚本 | 目的 | 输出 |
|---|---|---|---|
| E1.a / E1.b | [`01_phase1/extract_loopbounds.py`](experiments/01_phase1/extract_loopbounds.py)<br>[`01_phase1/static_estimates.py`](experiments/01_phase1/static_estimates.py) | 静态预筛 victim：抽 `_Pragma loopbound`，估算 `alpha_l2_static` | `data/processed/loopbounds.json`<br>`data/processed/static_estimates.csv` |
| E2.1 | [`02_phase2/e21_stress_validate.py`](experiments/02_phase2/e21_stress_validate.py) | stress 验证（与 §1 同义） | – |
| E2.2 | [`02_phase2/e22_adversary_search.py`](experiments/02_phase2/e22_adversary_search.py) | 三元组对手搜索 | – |
| E2.3 | [`02_phase2/e23_five_config_sweep.py`](experiments/02_phase2/e23_five_config_sweep.py) | 五配置 sweep（与 §2 同义） | – |
| E2.4 | [`02_phase2/e24_lemma1a.py`](experiments/02_phase2/e24_lemma1a.py) | Lemma 1a 验证 | – |
| E2.5 | [`02_phase2/e25_lemma2.py`](experiments/02_phase2/e25_lemma2.py) | Lemma 2 验证 | – |
| E3.1 | [`03_bounding/e31_polyrhythm_compare.py`](experiments/03_bounding/e31_polyrhythm_compare.py) | 与 PolyRhythm 上界对比 | – |
| E3.2 | [`03_bounding/e32_mempol_compare.py`](experiments/03_bounding/e32_mempol_compare.py) | 与 MemPol 对比 | – |
| E3.3 | [`03_bounding/e33_additive_violations.py`](experiments/03_bounding/e33_additive_violations.py) | 加性违例统计 | – |
| E4 | [`04_24h/e4_safety_histogram.py`](experiments/04_24h/e4_safety_histogram.py) | 24h 安全裕度直方图 | – |
| E5 | [`05_ablation/e5_ablation.py`](experiments/05_ablation/e5_ablation.py) | 组件消融 | – |
| E6 | [`06_scheduling/e6_schedule.py`](experiments/06_scheduling/e6_schedule.py) | 调度器集成 | – |
| E7 | [`07_case_study/e7_case_study.py`](experiments/07_case_study/e7_case_study.py) | 案例研究 | – |
| E8 | [`08_rtc/e8_rtc.py`](experiments/08_rtc/e8_rtc.py) | RTC 端到端 | – |
| E9 | [`09_zephyr/e9_zephyr.py`](experiments/09_zephyr/e9_zephyr.py) | Zephyr 端口 | – |

详细复现说明：[`experiments/REPRO.md`](experiments/REPRO.md)。

---

## 3. 历史自动化实验结果（`results/`）

下表把 `results/` 子目录按主题归类，记录了**每一组实验测什么、用哪个脚本驱动、产生多少数据**。

### 3.1 Per-victim 攻击者三阶段 hunt

| 项 | 值 |
|---|---|
| 驱动脚本 | [`run_per_victim_hunt.sh`](run_per_victim_hunt.sh) |
| 测试内容 | 对每个 TACLeBench victim，先用 20 个单源 attacker 跑 SOLO3 (atk×3)，挑 top-3，再混 5 种组合，最后 N=20 复测最强者 |
| 攻击者集合 | `CACHE / BUS / MEM / POINTER / PIPELINE / MTH_* (6) / PR_* (9)` 共 20 个 |
| 原始数据 | [`results/hunt/`](results/hunt) （627 个 `.txt`，含 `__SOLO3_*`、`__MIX_*`、`__CONFIRM_*`） |
| 日志 | [`results/hunt_log.txt`](results/hunt_log.txt) |
| 分析 | [`analyze_hunt.py`](analyze_hunt.py) → [`results/analysis/`](results/analysis) |
| 摘要 | [`results/analysis/REPORT.md`](results/analysis/REPORT.md), [`validated_champions.csv`](results/analysis/validated_champions.csv), [`category_stats.csv`](results/analysis/category_stats.csv), [`attacker_template_stats.csv`](results/analysis/attacker_template_stats.csv) |

**结论**：56 victim 中 52 验证成功，17 个 S≥1.05；最坏 `fir2dim` × `POINTER POINTER POINTER` = **1.55×**。

### 3.2 缓存 / 总线 sweep（细粒度参数扫）

| 实验 | 驱动脚本 | 数据 | 测试内容 |
|---|---|---|---|
| Cache sweep（L1/L2/LLC/DRAM × stride × R/W） | [`run_cache_sweep.sh`](run_cache_sweep.sh) | [`results/cache_sweep/`](results/cache_sweep) (48 文件) | 12 种 CACHE 模板 × 8 victim |
| Cache sweep v2（写比/容量更细） | [`run_cache_sweep2.sh`](run_cache_sweep2.sh) | [`results/cache_sweep2/`](results/cache_sweep2) (115 文件) | 容量 256K–64M、stride 32–128、wr=10/50/90 |
| Cache 最激进（写主导） | [`run_cache_aggressive.sh`](run_cache_aggressive.sh) | [`results/cache_aggr/`](results/cache_aggr) (124 文件) | 写主导 1M–16M, stride 1–1024 |
| Cache+BUS 组合 | [`run_cache_bus_combo.sh`](run_cache_bus_combo.sh) | [`results/cache_bus/`](results/cache_bus) (124 文件) | BUS 1M–4M × 多 attacker 拓扑 |
| Cache 干扰矩阵 | [`run_cache_interference.sh`](run_cache_interference.sh) | `results/pair_*_vs_*.txt` (1045 文件) | victim×attacker 全 N×N 对 |
| Cache 通用 sweep | [`run_interference_matrix.sh`](run_interference_matrix.sh) | 同上 | 与 cache_interference 配套 |

每组都附 `baseline.tsv` / `configs.tsv` / `harness.txt`。分析脚本：[`analyze_results.py`](analyze_results.py), [`extract_configs.py`](extract_configs.py)。

### 3.3 Champions 复测

| 项 | 值 |
|---|---|
| 驱动 | [`run_champions.sh`](run_champions.sh) |
| 数据 | [`results/champions/`](results/champions) (110 文件 + [`detail.tsv`](results/champions/detail.tsv)) |
| 测试内容 | 把 `cache_sweep*` / `cache_bus` / `tim_*` 中胜出的 attacker 配置做 N=20 大样本复测，给出最终 champion 表 |

### 3.4 Mixed sweep（多通道混合）

| 项 | 值 |
|---|---|
| 驱动 | [`run_mixed_attack_sweep.sh`](run_mixed_attack_sweep.sh) |
| 数据 | [`results/mixed/`](results/mixed) (897 文件) |
| 日志 | [`results/mixed_sweep.log`](results/mixed_sweep.log)（963 行） |
| 分析 | [`analyze_mixed_sweep.py`](analyze_mixed_sweep.py) |
| 测试内容 | victim × {ALL_BUS, ALL_CACHE, ALL_MEM, ALL_PIPE, ALL_PTR, MIX_BROAD, MIX_CACHEBUS, MIX_HARSH …} |

### 3.5 TIM 模板族扫描

| 子目录 | 文件数 | 内容 |
|---|---|---|
| [`results/tim_mix/`](results/tim_mix) | 238 | TIM 三元组混合 v1（[`run_tim_mix.sh`](run_tim_mix.sh)） |
| [`results/tim_mix2/`](results/tim_mix2) | 358 | v2 加宽参数（[`run_tim_mix2.sh`](run_tim_mix2.sh)） |
| [`results/tim_mix3/`](results/tim_mix3) | 352 | v3 含同/异类 TIM 混合（[`run_tim_mix3.sh`](run_tim_mix3.sh)） |
| [`results/tim_others/`](results/tim_others) | 382 | 非 cache/bus 的 TIM（POINTER/PIPELINE 等，[`run_tim_others.sh`](run_tim_others.sh)） |

每个目录都带 `baseline.tsv` + `configs.tsv` + `harness.txt`。

### 3.6 Phase-3 / Phase-4 hybrid hunt

| 项 | Phase-3 | Phase-4 |
|---|---|---|
| 驱动 | [`run_phase3_hybrid.sh`](run_phase3_hybrid.sh) | [`run_phase4_minhunt.sh`](run_phase4_minhunt.sh) |
| 数据 | [`results/phase3/`](results/phase3) (525 raw + summary) | [`results/phase4/`](results/phase4) (43 raw + summary) |
| 摘要 | [`results/phase3/sweep_summary.csv`](results/phase3/sweep_summary.csv) | [`results/phase4/sweep_summary.csv`](results/phase4/sweep_summary.csv) |
| 日志 | [`results/phase3_run.log`](results/phase3_run.log) (558 行) | [`results/phase4_run.log`](results/phase4_run.log) (54 行) |
| 内容 | 20 种 hybrid 三元组 × 多 victim，N=8/20 | 最终 worst-case 三元组 N=30 复测 |

### 3.7 Pair 对照

`results/pair_*_vs_*.txt`（约 1045 个）：早期 victim×victim、victim×attacker 对照实验。
`results/solo_*.txt`（8 个）：单独 victim 基线。

---

## 4. 自定义攻击者（本次新增）

| 名称 | 源码 | 设计要点 |
|---|---|---|
| `MAX_LLC` | [`bench/bench/TIM/MAX_LLC/MAX_LLC.c`](bench/bench/TIM/MAX_LLC/MAX_LLC.c) | 1.5 MiB workset，xorshift32 索引，read-modify-write 让 L2 line dirty |
| `MAX_BUS` | [`bench/bench/TIM/MAX_BUS/MAX_BUS.c`](bench/bench/TIM/MAX_BUS/MAX_BUS.c) | 2 MiB workset，整 64 B 行写（8×stq），4 KiB 跨页 |
| `MAX_MEM` | [`bench/bench/TIM/MAX_MEM/MAX_MEM.c`](bench/bench/TIM/MAX_MEM/MAX_MEM.c) | 16 MiB workset，按行 8 KiB 跳，半读半写最大化 row-activate |

均已加入 [`build/bench_registry.h`](build/bench_registry.h)，`./multi_proc_pmu` 可以直接 `MAX_LLC|MAX_BUS|MAX_MEM` 当作 victim 或 attacker。

---

## 5. 索引脚本一览

| 脚本 | 作用 |
|---|---|
| [`analyze_results.py`](analyze_results.py) | 把 `results/pair_*` 转 baseline/ratio TSV |
| [`analyze_mixed_sweep.py`](analyze_mixed_sweep.py) | mixed/ 目录聚合 |
| [`analyze_hunt.py`](analyze_hunt.py) | hunt/ 三阶段 → champions |
| [`extract_configs.py`](extract_configs.py) | 把 sweep 的 configs.tsv 拍平 |

---

## 6. 该如何复现

1. **MANUAL.md 75 文件**：照 [`MANUAL.md`](MANUAL.md) §0→§4 的命令逐行执行；或参考 [`results_manual/MEASUREMENTS.md`](results_manual/MEASUREMENTS.md) §7 给出的批处理。
2. **E1–E9 论文版**：`cd experiments && python3 run_all.py --all`（按 [`experiments/REPRO.md`](experiments/REPRO.md)，需 `export SUDO_PW=...`）。
3. **历史 sweep**：每个 `run_*.sh` 都可独立 `bash run_xxx.sh`，输出会写入对应 `results/<name>/`。
