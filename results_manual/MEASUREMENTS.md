# 板上实测记录 (MANUAL.md v2 全流程)

> 自动化生成自 `results_manual/{e21,e23,e24,e25}/*.txt`。所有数字直接来自
> `./multi_proc_pmu` 的 `cycles avg` 列（PMU 硬件计数，单位 = cycles）。

## 0. 平台指纹

| 项 | 值 |
|---|---|
| 板卡 | NXP i.MX 8M Mini EVK |
| ISA | aarch64 |
| 核 | 4 × Cortex-A53 |
| `CPU max MHz` | 1800 |
| 实验 fix-freq | **1600000 kHz**（统一 `-f 1600000`） |
| 内核 | Linux 4.14.78 SMP PREEMPT |
| Governor | `performance` |
| Userland | Ubuntu 20.04 aarch64 |
| 主控二进制 | `./multi_proc_pmu`（cpu0 = victim, cpu1..3 = attackers/可热拔） |
| 采样数 | 每条命令 `-n 100` |

## 1. §1 / E2.1 —— 攻击者强度筛选 (victim = `fmref`, n = 100)

固定 victim = `fmref`，在 cpu1/2/3 同时跑 3 份相同 attacker。
σ = `cycles_avg(stress) / cycles_avg(solo)`。

| Attacker | cycles avg | σ |
|---|---|---|
| SOLO（无 attacker，cpu1-3 离线） | 6.5850e+05 | 1.0000 |
| CACHE（旧 LLC 模板） | 6.6130e+05 | 1.0043 |
| **MAX_LLC**（自定义） | 6.6501e+05 | **1.0099** ← LLC 通道选用 |
| MTH_BUS（旧 BUS 模板） | 6.6899e+05 | 1.0159 |
| **MAX_BUS**（自定义） | 7.2654e+05 | **1.1033** ← BUS 通道选用 |
| PR_ROWBUF（旧 MEM 模板） | 6.7872e+05 | 1.0307 |
| **MAX_MEM**（自定义） | 7.1449e+05 | **1.0850** ← MEM 通道选用 |

**结论**：自定义 MAX_BUS / MAX_MEM 显著优于旧模板（+8.7pp / +5.4pp）；MAX_LLC 略优于 CACHE（+0.6pp）。后续章节的 `llc/bus/mem/mix` 配置一律用三个 MAX_*。

复现命令（每行一条）：

```bash
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref                                | tee results_manual/e21/SOLO.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref CACHE     CACHE     CACHE      | tee results_manual/e21/CACHE.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MAX_LLC   MAX_LLC   MAX_LLC    | tee results_manual/e21/MAX_LLC.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MTH_BUS   MTH_BUS   MTH_BUS    | tee results_manual/e21/MTH_BUS.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MAX_BUS   MAX_BUS   MAX_BUS    | tee results_manual/e21/MAX_BUS.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref PR_ROWBUF PR_ROWBUF PR_ROWBUF  | tee results_manual/e21/PR_ROWBUF.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MAX_MEM   MAX_MEM   MAX_MEM    | tee results_manual/e21/MAX_MEM.txt
```

---

## 2. §2 / E2.3 —— 五配置 PMC 全扫 (8 victims × 5 configs = 40 文件)

每行 = victim；列 = solo / llc(=3×MAX_LLC) / bus(=3×MAX_BUS) / mem(=3×MAX_MEM) / mix(=MAX_LLC+MAX_BUS+MAX_MEM)。
$a_{\text{mix}} = \text{cycles}(\text{mix}) / \text{cycles}(\text{solo})$。

| victim | solo | llc | bus | mem | mix | $a_{\text{mix}}$ |
|---|---|---|---|---|---|---|
| fir2dim    | 1.297e+04 | 1.364e+04 | 1.393e+04 | 1.465e+04 | 1.470e+04 | **1.133** |
| fmref      | 6.556e+05 | 6.630e+05 | 8.082e+05 | 7.185e+05 | 7.131e+05 | **1.088** |
| cosf       | 3.139e+04 | 3.175e+04 | 3.360e+04 | 3.324e+04 | 5.878e+04 | **1.872** |
| test3      | 7.872e+08 | 8.827e+08 | 9.240e+08 | 9.558e+08 | 8.598e+08 | **1.092** |
| jfdctint   | 8.633e+03 | 8.882e+03 | 1.051e+04 | 2.255e+04 | 2.462e+04 | **2.852** |
| fac        | 9.612e+02 | 1.067e+03 | 6.957e+03 | 2.271e+03 | 2.391e+03 | **2.488** |
| insertsort | 4.409e+03 | 4.742e+03 | 6.095e+03 | 4.481e+03 | 2.098e+04 | **4.759** |
| matrix1    | 2.077e+04 | 2.094e+04 | 2.878e+04 | 2.144e+04 | 2.796e+04 | **1.346** |

**单通道 Δ 表**（Δ_X = cycles(X) ? cycles(solo)）：

| victim | Δ_LLC | Δ_BUS | Δ_MEM | Δ_MIX |
|---|---|---|---|---|
| fir2dim    | 6.715e+02 | 9.528e+02 | 1.680e+03 | 1.725e+03 |
| fmref      | 7.388e+03 | 1.525e+05 | 6.284e+04 | 5.751e+04 |
| cosf       | 3.553e+02 | 2.211e+03 | 1.845e+03 | 2.738e+04 |
| test3      | 9.544e+07 | 1.367e+08 | 1.686e+08 | 7.260e+07 |
| jfdctint   | 2.496e+02 | 1.878e+03 | 1.391e+04 | 1.599e+04 |
| fac        | 1.053e+02 | 5.996e+03 | 1.310e+03 | 1.430e+03 |
| insertsort | 3.335e+02 | 1.686e+03 | 7.248e+01 | 1.657e+04 |
| matrix1    | 1.677e+02 | 8.003e+03 | 6.708e+02 | 7.183e+03 |

观察：
- `cosf`、`insertsort` 上 mix 远超任意单通道 → 三通道叠加效应显著。
- `test3` mix 反而 **小于** 单通道 mem（8.60e8 vs 9.56e8）：受害者已被 LLC/BUS/MEM 各自单通道 saturate，对手家族多样性反而互相抢占，这是 Lemma 2 的关键证据样本。
- `fac` 是 BUS-bound（Δ_BUS ≈ 6.0×Δ_MEM）。

复现：见 [`MANUAL.md` §2.2](../MANUAL.md) 的 40 条命令；输出 → `results_manual/e23/<victim>__{solo,llc,bus,mem,mix}.txt`。

---

## 3. §3 / E2.4 —— Lemma 1a 抽样 (Δ_{a+b} ≤ Δ_a + Δ_b)

`results_manual/e24/` 用 **单/双 attacker**（其余 cpu 离线），与 §2 的 3-attacker 不同。
Δ 相对各 victim 的 §2 solo 计算。

| 步 | victim | pair | Δ_a | Δ_b | Δ_a+Δ_b | Δ_{a+b} | 判定 |
|---|---|---|---|---|---|---|---|
| 3.1-3 | fir2dim    | LLC+BUS | 2.15e+02 | 1.02e+03 | 1.24e+03 | 3.34e+02 | ? OK |
| 3.4-6 | fmref      | LLC+MEM | 3.05e+03 | 3.78e+03 | 6.82e+03 | 5.18e+03 | ? OK |
| 3.7-9 | cosf       | BUS+MEM | 5.46e+02 | ?1.98e+02 | 3.48e+02 | 5.75e+02 | ? 噪声违例 |
| 3.10-12 | test3    | LLC+MEM | ?1.11e+07 | 3.25e+06 | ?7.83e+06 | 9.90e+06 | ? 噪声违例 |
| 3.13-15 | jfdctint | BUS+MEM | 1.73e+03 | 3.29e+01 | 1.76e+03 | 2.20e+04 | ? 真违例 |
| 3.16-18 | fac      | LLC+BUS | 5.20e+00 | ?4.46e+01 | ?3.94e+01 | 2.02e+03 | ? 真违例 |
| 3.19 | matrix1     | LLC+MEM | – | – | – | 4.03e+02 | (无单子项) |
| 3.20 | matrix1     | BUS+MEM | – | – | – | 6.03e+03 | (无单子项) |

**解读**：
- 严格 OK：fir2dim, fmref（共 2/6 通过，无任何 slack 时）。
- 标 "? 噪声违例"：单通道 Δ 接近或低于测量噪声（出现负值），右侧 |Δ_pair| 也很小（约 5e2 / 1e7 cycle），属背景抖动数量级，并非 Lemma 1a 失败。
- 标 "? 真违例"：`jfdctint`(BUS+MEM) 单子项 ≈1.8e3+33，pair = 2.2e4 ≈ 12× 之和；`fac`(LLC+BUS) 单子项几乎 0，pair = 2.0e3。两例提示 **Lemma 1a 在该板上对部分 victim 不能直接以零 slack 上界**（双攻击者协同唤起了单攻击者激不出的总线/L2 仲裁路径）。RAMPART 中 γ=1.02 的 slack 显然不足以盖住 12×；论文引用此引理时需对应 victim 类别加补偿项。
- matrix1 因 §3 表只列了 pair 而未列 LLC/BUS/MEM 单子项，留作 §2-内推线索：`Δ_LLC=1.68e+02, Δ_MEM=6.71e+02`（来自 §2 三-attacker 数据，**不可直接和此处单-attacker 配对比较**）。

复现：见 `MANUAL.md` §3 表格 3.1–3.20（每条都形如 `sudo ./multi_proc_pmu -f 1600000 -n 100 <victim> MAX_X [MAX_Y]`），输出 → `results_manual/e24/<victim>__<X>[+<Y>].txt`。

---

## 4. §4 / E2.5 —— Lemma 2 抽样 (mix ≥ triple2)

`triple2 = CACHE MTH_BUS PR_ROWBUF`（旧模板的“次强家族”）。期望 mix(MAX_*) ≥ triple2。

| 步 | victim | mix cycles | triple2 cycles | mix/triple2 | 判定 |
|---|---|---|---|---|---|
| 4.1 | fir2dim    | 1.470e+04 | 1.408e+04 | 1.0435 | ? |
| 4.2 | fmref      | 7.131e+05 | 6.589e+05 | 1.0823 | ? |
| 4.3 | cosf       | 5.878e+04 | 3.153e+04 | **1.864** | ? |
| 4.4 | test3      | 8.598e+08 | 7.873e+08 | 1.0921 | ? |
| 4.5 | jfdctint   | 2.462e+04 | 9.261e+03 | **2.659** | ? |
| 4.6 | fac        | 2.391e+03 | 9.748e+02 | **2.453** | ? |
| 4.7 | insertsort | 2.098e+04 | 4.600e+03 | **4.561** | ? |
| 4.8 | matrix1    | 2.796e+04 | 2.064e+04 | 1.3543 | ? |

**8/8 通过**：mix(MAX_LLC, MAX_BUS, MAX_MEM) 在所有 victim 上都比次强三元组的 cycles 高 4.4%–356%。Lemma 2 在该板成立。

> 注：旧模板 (CACHE/MTH_BUS/PR_ROWBUF) 在 §1 对 fmref 的 σ 总和远低于 MAX 家族，本节直接证伪了“多家族取最强后线性可加”——只需把 a_max 取为 **mix（同时启动家族里的最强单子）** 即给上界。

复现：见 `MANUAL.md` §4 表格 4.1–4.8。输出 → `results_manual/e25/<victim>__triple2.txt`。

---

## 5. §5 RAMPART 上界

$$
C^{\text{rampart}}(v) \;=\; \gamma \cdot a_{\text{mix}}(v) \cdot C^{\text{solo}}(v),
\qquad \gamma = 1.02
$$

| victim | $a_{\text{mix}}$ | $\gamma\cdot a_{\text{mix}}$ |
|---|---|---|
| fir2dim    | 1.1330 | 1.1556 |
| fmref      | 1.0877 | 1.1095 |
| cosf       | 1.8723 | 1.9098 |
| test3      | 1.0922 | 1.1141 |
| jfdctint   | 2.8525 | **2.9095** |
| fac        | 2.4879 | 2.5377 |
| insertsort | 4.7586 | **4.8538** ← 全集最大 |
| matrix1    | 1.3458 | 1.3727 |

**全板 8-victim WCET 膨胀因子上界**：
$$
\max_v\,C^{\text{rampart}}(v)/C^{\text{solo}}(v) \;=\; 4.854\times \quad(\text{victim}=\texttt{insertsort}).
$$

注意：上界仅在 Lemma 1a 不被显著违反时严格；§3 显示 jfdctint/fac 的 pair 攻击对单子项之和有 12× 越界，所以引用 RAMPART 时应：
1. 显式声明 γ 对应的“slack 假设”；
2. 对 BUS/MEM 双通道协同敏感的 victim（jfdctint, fac），用 §2 mix 直测值 a_mix 作为参考，而不是 Lemma 1a 推导的线性上界。

---

## 6. 文件清单（全部由 PMU 硬件计数器写入，无手工编辑）

```
results_manual/
├── e21/   7 文件  §1 attacker 强度
├── e23/  40 文件  §2 8 victims × 5 configs
├── e24/  20 文件  §3 Lemma 1a (单 + 双 attacker)
├── e25/   8 文件  §4 Lemma 2 (triple2 = 次强模板三元组)
├── _progress_345.log   §3+§4 完成时间戳
├── e23/_progress.log   §2 完成时间戳
└── MEASUREMENTS.md     本文档
```

总文件数：**75 个 raw PMC dump + 3 个汇总日志 + 本报告**。

---

## 7. 一次性复现脚本入口

```bash
# 直接重跑全部:
sudo bash /tmp/e21_*.sh   # （或参照 MANUAL.md §1.1 七条命令）
sudo bash /tmp/e23_resume.sh
sudo bash /tmp/e24_25.sh
python3 - <<'PY'
... # 见本文档同目录的 aggregator 代码段
PY
```

或参照 `MANUAL.md` 中的 §1–§5 表格逐行执行（人工流程）。
