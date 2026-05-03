# CRISP / RAMPART ¡ª One-shot reproducer

This repo contains:
- `multi_proc_pmu.c` ¡ª measurement harness that pins a victim on cpu0, runs attackers on cpu1..N-1, hot-unplugs the rest, locks cpufreq, and samples `cycles / instructions / cache-misses` via `perf_event_open`.
- `bench/bench/` ¡ª TACLeBench victim benches + custom attacker templates (`MAX_LLC`, `MAX_BUS`, `MAX_MEM`, plus PR_*, MTH_*, POLY).
- `experiments/` ¡ª Python pipelines E1..E9 (loop bounds ¡ú static estimates ¡ú stress validation ¡ú adversary search ¡ú 5-config sweep ¡ú Lemma 1a/2 ¡ú 24h safety histogram ¡ú ablation ¡ú schedulability ¡ú case study + fault injection).
- `crisp_run_all.sh` ¡ª single-script setup + run for any aarch64/x86_64 Linux with sudo.

## Quick start (Raspberry Pi / i.MX / generic Linux)

```bash
git clone https://github.com/cringe-sigma/CRISP.git
cd CRISP
chmod +x crisp_run_all.sh

# Quick mode (~3-5 h on a 4-core board)
SUDO_PW=yourpw ./crisp_run_all.sh

# Paper mode (~3-4 days; R=1000, 24h E4/E7)
SUDO_PW=yourpw ./crisp_run_all.sh --paper

# Single phase
SUDO_PW=yourpw ./crisp_run_all.sh --steps E1 E2 E3
```

The script:
1. detects arch/cores/distro,
2. installs apt packages it needs (`build-essential python3 cpufrequtils linux-libc-dev`),
3. lowers `perf_event_paranoid` to 1 if needed,
4. `sudo make -j$(nproc) multi_proc_pmu` (auto-discovers all 80+ benches),
5. runs a PMU smoke test,
6. invokes [experiments/run_all.py](experiments/run_all.py) with chosen scale,
7. writes a summary to `crisp_summary.txt`.

## Outputs

| Path | What |
|------|------|
| `experiments/data/processed/e21_stress_validation.csv` | Stress validation (per-channel ¦Ò) |
| `experiments/data/processed/e3_bounds.csv` | RAMPART_full upper bounds |
| `experiments/data/processed/e4_safety_histogram.csv` | 24h C_obs / RAMPART ratios |
| `experiments/data/processed/e6_scheduling.csv` | ILP runtime + schedulability |
| `experiments/data/processed/e7_case_study.csv` | Case-study deadline misses + shield recoveries |
| `experiments/run_log/*.log` | Per-step append-only logs with sha256 of every input/output |

## Hardware requirements

- ¡Ý4 cores (1 victim + 3 attackers; script warns and degrades gracefully if fewer).
- Linux ¡Ý4.x with `perf_event_open` and `/sys/devices/system/cpu/*/cpufreq/`.
- sudo (needed to hot-unplug cores and write cpufreq).
- Tested on aarch64 Cortex-A53; should work on Pi 4/5 (BCM2711/2712) and any x86_64 with `perf` events.

## Manual flow

If you want to run individual `multi_proc_pmu` invocations instead of the Python pipeline, see [MANUAL.md](MANUAL.md). It documents 80 individual `sudo ./multi_proc_pmu ¡­` commands matching the paper's E2.1/E2.3/E2.4/E2.5.

## License

See [bench/README.md](bench/README.md) for upstream TACLeBench licensing. Custom attacker code (`bench/bench/TIM/MAX_*`) and `multi_proc_pmu.c` are released under MIT.
