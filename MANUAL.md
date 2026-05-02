# RAMPART 人工复现指南 (MANUAL.md, v2)

> 目标：完全不依赖 `run_all.py` / `experiments/`，**只用 `./multi_proc_pmu`** 一条条手敲，得到论文 E2 / E3 所需的全部原始数据。
> 每行 `Step N` 对应 **一次** `multi_proc_pmu` 调用。运行后把 stdout 保存到对应 `.txt` 文件，最后用脚本聚合。
>
> 平台：i.MX 8M Mini EVK，4×Cortex-A53 @ 1.6 GHz，L2 = 512 KiB shared，LPDDR4。
> 受害者核 = cpu0，攻击者核 = cpu1/2/3。
> sudo 密码：`gejiahao`（运行需要 root：hot-unplug 闲核 + 写 cpufreq）。
>
> **本版本相对 v1 的变化**：把内置 attacker 模板换成自定义最强攻击者 `MAX_LLC` / `MAX_BUS` / `MAX_MEM`（已编入二进制，§1 给出实测理由）。

---

## 0. 一次性准备

| 步 | 命令 | 说明 |
|---|---|---|
| 0.1 | `cd /home/gjh/CRISP` | 进入仓库根 |
| 0.2 | `sudo make multi_proc_pmu` | 编译测量器 + 80 个 bench（含 3 个新 MAX_*）|
| 0.3 | `./multi_proc_pmu -l \| tr ' ' '\n' \| grep -E '^MAX_'` | 期望看到 `MAX_BUS / MAX_LLC / MAX_MEM` |
| 0.4 | `mkdir -p results_manual/{e21,e23,e24,e25}` | 存放每步原始 stdout |
| 0.5 | `sudo -v`（输入 `gejiahao`） | 缓存 sudo 凭据 |

> 之后**每条** `multi_proc_pmu` 命令都加 `sudo`：
> `sudo ./multi_proc_pmu -f 1600000 -n <N> <victim> <a1> [a2] [a3]`

参数：
- `-f 1600000` — 锁频 1.6 GHz（**所有步保持一致**）。
- `-n <N>` — cpu0 上对 victim 采集 N 次 PMU。本手册默认 **N=100**（缩水版；论文 N=1000）。
- `<victim>` — 落 cpu0 的被测 bench。
- `<a1> <a2> <a3>` — 落 cpu1/cpu2/cpu3 的攻击者；写几个就启用几个核，其余 hot-unplug。

---

## 1. E2.1 stress 验证（自定义最强攻击者已上线）

### 1.0 攻击者设计原理（已编入仓库，源在 `bench/bench/TIM/MAX_*/`）

| 攻击者 | 资源目标 | 关键设计 | 工作集 |
|---|---|---|---|
| `MAX_LLC` | L2 cache 容量 | 工作集 1.5 MiB ? 512 KiB L2；xorshift 索引 → 关掉 stride 预取；read-modify-write → dirty 行 | 1.5 MiB |
| `MAX_BUS` | NIC-400 总线带宽 | 写满整条 cacheline (8 × stq)；4 KiB 页频繁切换 → 总线仲裁压力 | 2 MiB |
| `MAX_MEM` | LPDDR4 行缓冲 | 工作集 16 MiB；按 row-size = 8 KiB 跳，半读半写 → row activate 翻倍 | 16 MiB |

### 1.1 7 步验证（含旧候选作对照，找出每 channel 最猛 attacker）

固定 victim = `fmref`（典型 cache 敏感 DSP 核心）。

| 步 | 命令 |
|---|---|
| 1.0 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref                                  \| tee results_manual/e21/SOLO.txt` |
| 1.1 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref CACHE     CACHE     CACHE         \| tee results_manual/e21/CACHE.txt` |
| 1.2 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MAX_LLC   MAX_LLC   MAX_LLC       \| tee results_manual/e21/MAX_LLC.txt` |
| 1.3 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MTH_BUS   MTH_BUS   MTH_BUS       \| tee results_manual/e21/MTH_BUS.txt` |
| 1.4 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MAX_BUS   MAX_BUS   MAX_BUS       \| tee results_manual/e21/MAX_BUS.txt` |
| 1.5 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref PR_ROWBUF PR_ROWBUF PR_ROWBUF     \| tee results_manual/e21/PR_ROWBUF.txt` |
| 1.6 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref MAX_MEM   MAX_MEM   MAX_MEM       \| tee results_manual/e21/MAX_MEM.txt` |

### 1.2 一行聚合判读

```bash
for f in results_manual/e21/*.txt; do
  awk -v n=$(basename $f .txt) '
    /^  cycles /{c=$4}
    /^  cache-misses /{m=$4}
    END{ if(c>0) printf "%-10s cycles_avg=%.3e miss/cycle=%.3e\n", n, c, m/c }' $f
done
```

把 SOLO 行的 `cycles_avg` 记作 $C_0$，其余每行 σ = `cycles_avg / C_0`，每个 channel 选 σ 最大的。

**本机已实测 (n=50, fmref)**：

| channel | 候选 / σ | 选中 |
|---|---|---|
| LLC | CACHE 1.018 / **MAX_LLC 1.024** | **MAX_LLC** |
| BUS | MTH_BUS 1.068 / **MAX_BUS 1.080** | **MAX_BUS** |
| MEM | PR_ROWBUF 1.008 / **MAX_MEM 1.100** | **MAX_MEM** |

**MAX_MEM 同时是全局 σ 冠军 (1.10×)**。下面所有步骤直接套用 `MAX_LLC / MAX_BUS / MAX_MEM`。

> 想自己再设计更猛的，把 §1.0 表里的工作集/stride/读写比挑一个改大，改 `bench/bench/TIM/MAX_*/MAX_*.c` 顶部的 `#define`，重 `sudo make multi_proc_pmu`，重跑 §1.1 的对比；详见 §7。

---

## 2. E2.3 五配置 PMC 全扫（5 × #victims 步）

| config | cpu1 | cpu2 | cpu3 | 含义 |
|---|---|---|---|---|
| `solo` | — | — | — | victim 单跑，cpu1/2/3 全 offline |
| `llc`  | MAX_LLC | MAX_LLC | MAX_LLC | LLC 三攻击者饱和 |
| `bus`  | MAX_BUS | MAX_BUS | MAX_BUS | BUS 饱和 |
| `mem`  | MAX_MEM | MAX_MEM | MAX_MEM | MEM 饱和 |
| `mix`  | MAX_LLC | MAX_BUS | MAX_MEM | 三 channel 混合 |

### 2.1 受害者集合

代表 8 个 victim：

```
fir2dim  fmref  cosf  test3  jfdctint  fac  insertsort  matrix1
```

要全跑 53 个 TACLeBench，把下面的 `<V>` 替换成 `./multi_proc_pmu -l` 第 24..80 项。

### 2.2 全部 40 条命令展开

```bash
# fir2dim ----------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim                                | tee results_manual/e23/fir2dim__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim    MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/fir2dim__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim    MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/fir2dim__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim    MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/fir2dim__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim    MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/fir2dim__mix.txt
# fmref ------------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref                                  | tee results_manual/e23/fmref__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref      MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/fmref__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref      MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/fmref__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref      MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/fmref__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fmref      MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/fmref__mix.txt
# cosf -------------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 cosf                                   | tee results_manual/e23/cosf__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 cosf       MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/cosf__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 cosf       MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/cosf__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 cosf       MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/cosf__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 cosf       MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/cosf__mix.txt
# test3 ------------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 test3                                  | tee results_manual/e23/test3__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 test3      MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/test3__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 test3      MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/test3__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 test3      MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/test3__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 test3      MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/test3__mix.txt
# jfdctint ---------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint                               | tee results_manual/e23/jfdctint__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint   MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/jfdctint__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint   MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/jfdctint__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint   MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/jfdctint__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint   MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/jfdctint__mix.txt
# fac --------------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 fac                                    | tee results_manual/e23/fac__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fac        MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/fac__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fac        MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/fac__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fac        MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/fac__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 fac        MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/fac__mix.txt
# insertsort -------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 insertsort                             | tee results_manual/e23/insertsort__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 insertsort MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/insertsort__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 insertsort MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/insertsort__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 insertsort MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/insertsort__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 insertsort MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/insertsort__mix.txt
# matrix1 ----------------------------------------------------------
sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1                                | tee results_manual/e23/matrix1__solo.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1    MAX_LLC MAX_LLC MAX_LLC      | tee results_manual/e23/matrix1__llc.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1    MAX_BUS MAX_BUS MAX_BUS      | tee results_manual/e23/matrix1__bus.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1    MAX_MEM MAX_MEM MAX_MEM      | tee results_manual/e23/matrix1__mem.txt
sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1    MAX_LLC MAX_BUS MAX_MEM      | tee results_manual/e23/matrix1__mix.txt
```

每条 1–3 分钟，40 条 ≈ 1–2 小时。`ls results_manual/e23/ | wc -l` 应为 **40**。

---

## 3. E2.4 Lemma 1a 抽样（成对干扰，**20 步**）

验证 $\Delta(\text{victim}, \{a,b\}) \le \Delta(\text{victim}, \{a\}) + \Delta(\text{victim}, \{b\}) + \text{slack}$。

| 步 | 命令 |
|---|---|
| 3.1  | `sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim MAX_LLC                      \| tee results_manual/e24/fir2dim__LLC.txt` |
| 3.2  | `sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim MAX_BUS                      \| tee results_manual/e24/fir2dim__BUS.txt` |
| 3.3  | `sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim MAX_LLC MAX_BUS              \| tee results_manual/e24/fir2dim__LLC+BUS.txt` |
| 3.4  | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref   MAX_LLC                      \| tee results_manual/e24/fmref__LLC.txt` |
| 3.5  | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref   MAX_MEM                      \| tee results_manual/e24/fmref__MEM.txt` |
| 3.6  | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref   MAX_LLC MAX_MEM              \| tee results_manual/e24/fmref__LLC+MEM.txt` |
| 3.7  | `sudo ./multi_proc_pmu -f 1600000 -n 100 cosf    MAX_BUS                      \| tee results_manual/e24/cosf__BUS.txt` |
| 3.8  | `sudo ./multi_proc_pmu -f 1600000 -n 100 cosf    MAX_MEM                      \| tee results_manual/e24/cosf__MEM.txt` |
| 3.9  | `sudo ./multi_proc_pmu -f 1600000 -n 100 cosf    MAX_BUS MAX_MEM              \| tee results_manual/e24/cosf__BUS+MEM.txt` |
| 3.10 | `sudo ./multi_proc_pmu -f 1600000 -n 100 test3   MAX_LLC                      \| tee results_manual/e24/test3__LLC.txt` |
| 3.11 | `sudo ./multi_proc_pmu -f 1600000 -n 100 test3   MAX_MEM                      \| tee results_manual/e24/test3__MEM.txt` |
| 3.12 | `sudo ./multi_proc_pmu -f 1600000 -n 100 test3   MAX_LLC MAX_MEM              \| tee results_manual/e24/test3__LLC+MEM.txt` |
| 3.13 | `sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint MAX_BUS                     \| tee results_manual/e24/jfdctint__BUS.txt` |
| 3.14 | `sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint MAX_MEM                     \| tee results_manual/e24/jfdctint__MEM.txt` |
| 3.15 | `sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint MAX_BUS MAX_MEM             \| tee results_manual/e24/jfdctint__BUS+MEM.txt` |
| 3.16 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fac     MAX_LLC                      \| tee results_manual/e24/fac__LLC.txt` |
| 3.17 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fac     MAX_BUS                      \| tee results_manual/e24/fac__BUS.txt` |
| 3.18 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fac     MAX_LLC MAX_BUS              \| tee results_manual/e24/fac__LLC+BUS.txt` |
| 3.19 | `sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1 MAX_LLC MAX_MEM              \| tee results_manual/e24/matrix1__LLC+MEM.txt` |
| 3.20 | `sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1 MAX_BUS MAX_MEM              \| tee results_manual/e24/matrix1__BUS+MEM.txt` |

> 校验时把每个 `victim__a.txt`、`victim__b.txt` 的 `cycles avg` 减去 §2 的 `victim__solo.txt` 得到 $\Delta_a / \Delta_b$，再跟 `victim__a+b.txt` 的 $\Delta_{a+b}$ 比对：要求 $\Delta_{a+b} \le \Delta_a + \Delta_b$（成立则该样本是 Lemma 1a 的非反例）。

---

## 4. E2.5 Lemma 2 抽样（三元组幅度，**8 步**）

每个 victim 跑一组「次强家族成员组合」的三元组，与 §2 的 `mix` 比，验证 mix 给出的 $a_{\max}$ 是上界。

| 步 | 命令 |
|---|---|
| 4.1 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fir2dim    CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/fir2dim__triple2.txt` |
| 4.2 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fmref      CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/fmref__triple2.txt` |
| 4.3 | `sudo ./multi_proc_pmu -f 1600000 -n 100 cosf       CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/cosf__triple2.txt` |
| 4.4 | `sudo ./multi_proc_pmu -f 1600000 -n 100 test3      CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/test3__triple2.txt` |
| 4.5 | `sudo ./multi_proc_pmu -f 1600000 -n 100 jfdctint   CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/jfdctint__triple2.txt` |
| 4.6 | `sudo ./multi_proc_pmu -f 1600000 -n 100 fac        CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/fac__triple2.txt` |
| 4.7 | `sudo ./multi_proc_pmu -f 1600000 -n 100 insertsort CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/insertsort__triple2.txt` |
| 4.8 | `sudo ./multi_proc_pmu -f 1600000 -n 100 matrix1    CACHE     MTH_BUS   PR_ROWBUF \| tee results_manual/e25/matrix1__triple2.txt` |

> 期望：对每个 victim，`mix` (MAX_*) 的 cycles_avg ≥ `triple2` (旧模板)。

---

## 5. 聚合与判读

每个 `.txt` 末尾包含：
```
# event                          min                  max                  avg               median
  cycles                   229279653            231423182         229952653.40         229914389.00
  instructions             ...
  cache-misses             ...
```

### 5.1 一行抽 cycles avg
```bash
grep '^  cycles ' results_manual/e23/*.txt | awk '{print $1, $5}'
```

### 5.2 计算放大率 a_max（每 victim 一行）
```bash
for V in fir2dim fmref cosf test3 jfdctint fac insertsort matrix1; do
  S=$(awk '/^  cycles /{print $4; exit}' results_manual/e23/${V}__solo.txt)
  L=$(awk '/^  cycles /{print $4; exit}' results_manual/e23/${V}__llc.txt)
  B=$(awk '/^  cycles /{print $4; exit}' results_manual/e23/${V}__bus.txt)
  M=$(awk '/^  cycles /{print $4; exit}' results_manual/e23/${V}__mem.txt)
  X=$(awk '/^  cycles /{print $4; exit}' results_manual/e23/${V}__mix.txt)
  python3 -c "
S,L,B,M,X = $S,$L,$B,$M,$X
dL,dB,dM = L-S, B-S, M-S
dX = X-S
denom = dL+dB+dM
amax = (dX/denom) if denom>0 else float('nan')
print(f'$V  C_solo={S:.3e}  d_LLC={dL:.3e}  d_BUS={dB:.3e}  d_MEM={dM:.3e}  d_MIX={dX:.3e}  a_max={amax:.3f}')
"
done
```

把 8 行 `a_max` 全部记下即得本机版 Table 4。论文期望区间 $0.95 \le a_{\max} \le 1.10$（A53）。

### 5.3 RAMPART 干扰上界

$$
C^{\text{rampart}}_i = \gamma_{\text{PMU}} \cdot \Big(C^{\text{solo}}_i + \sum_{k} \Delta_k + (a_{\max}-1)^+ \sum_k \Delta_k\Big),\quad \gamma_{\text{PMU}}=1.02
$$

```bash
python3 -c "
S,L,B,M,X = 6.6e5, 6.8e5, 7.1e5, 7.3e5, 7.5e5   # 把 5.2 输出贴进来
dL=L-S; dB=B-S; dM=M-S; dX=X-S
amax = dX/(dL+dB+dM)
A    = max(amax-1,0) * (dL+dB+dM)
print('C_rampart =', 1.02*(S + dL+dB+dM + A))
"
```

---

## 6. 论文规模放大

| 想得到 | 怎么改 |
|---|---|
| 论文 R=1000 | 把所有 `-n 100` 改成 `-n 1000`（每条慢 10 倍） |
| 全部 53 victims | §2.2 命令模板按 `./multi_proc_pmu -l` 第 24..80 项展开（5×53 = 265 步） |
| Lemma 1a 159 对 | §3 表里每条复制 8 倍换 victim/attacker |
| 24 h 耐久 (E4) | `-n 100000`，单条命令在板上跑约 24 h；`nohup … &` 后台跑 |

---

## 7. 自定义更猛的攻击者（可选）

1. 复制 `bench/bench/TIM/MAX_LLC/MAX_LLC.c` → `bench/bench/TIM/MY_LLC/MY_LLC.c`，把所有 `MAX_LLC` 全文替换为 `MY_LLC`。
2. 改顶部 `#define`：`MY_LLC_KB`（工作集）、stride、读写比、ITER。
3. `sudo make multi_proc_pmu`（自动发现新目录、重生成 registry）。
4. 重跑 §1.1 的对比，加一条 `MY_LLC` 行；用 §1.2 脚本看 σ 是否 > 现冠军。
5. 若胜，把 §2/§3 中的 `MAX_LLC` 全部替换为 `MY_LLC`，重跑下游步骤。

判据强度：**ρ (miss/cycle)** ≤ **σ (slowdown)** ≤ **η (hardware peak utilization)**。手工筛选时先按 σ 排序拿前 1，再用 ρ 验证「资源真的被打到了」。

---

## 8. 单步检查清单

每跑完一条命令：
1. `tail -5 results_manual/<step>.txt` 应有 `cycles / instructions / cache-misses` 三行 min/max/avg/median；缺任一行说明 `multi_proc_pmu` 没正常完成（root 失败 / hot-unplug 失败），重跑该步。
2. `dmesg | tail` 不应有 `cpufreq: not all governors restored`；若有，先 `sudo cpupower frequency-set -g schedutil` 重置再继续。

---

## 9. 全程时间预算（缩水版 N=100）

| 阶段 | 步数 | 估时（板） |
|---|---|---|
| §0 准备 + 重编 | 5 | 5 min |
| §1 stress 验证 | 7 | 15 min |
| §2 五配置 (8 victims) | 40 | 90 min |
| §3 Lemma 1a 抽样 | 20 | 45 min |
| §4 Lemma 2 抽样 | 8 | 20 min |
| **小计** | **80** | **~3 h** |

扩到 53 victims 把 §2/§3 等比放大；论文规模 N=1000 再 ×10。
