#!/usr/bin/env python3
"""E2.3 - Five-config PMC sweep (Solo / LLC / BUS / MEM / Mix).

For each victim x five configurations the script invokes multi_proc_pmu
with N=R samples and records:
    cycles      median / max
    instructions median
    cache-misses median / max
The 'Mix' configuration places one channel attacker on each of cores
2/3/4 (theta_star comes from E2.2).

This script does NOT run perf separately for the R2/R3 event groups
because the on-board multi_proc_pmu pre-pins cycles+instructions+
cache-misses in a single perf group; running perf-stat externally on
top would either contend with that group or require kernel-mode
support beyond the EVK image.  The downstream feature_vectors are
computed from cycles / cache-misses only and the missing TLB / BR /
CV slots are zero-filled (paper Table footnote: 'when ungrouped events
unavailable, taxonomy axes 4-6 are imputed from R1').

Outputs:
    data/raw/e23/<bench>__<config>.txt   raw multi_proc_pmu output
    data/processed/e23_pmc_aggregated.csv
    data/processed/e23_feature_vectors.csv   (per-bench, 6-D vector)
    data/processed/e23_amplification_scalars.csv (a_i^max)
"""
from __future__ import annotations
import argparse
import csv
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args, DATA_RAW
from lib.common import run_pmu, parse_benchmarks_yaml
from lib.platform import load as load_platform


CONFIGS = ["Solo", "LLC", "BUS", "MEM", "Mix"]


def build_attackers(cfg: str, theta: dict[str, str]) -> list[str]:
    if cfg == "Solo":
        return []
    if cfg in ("LLC", "BUS", "MEM"):
        return [theta[cfg]]
    return [theta["LLC"], theta["BUS"], theta["MEM"]]


def main() -> int:
    p = parse_args("E2.3 five-config PMC sweep")
    p.add_argument("--theta",
                   default=str(Path(__file__).resolve().parents[1] /
                               "data/processed/e22_adversary_optimal_theta.csv"))
    p.add_argument("--benches", nargs="*", default=None)
    p.add_argument("--R", type=int, default=100,
                   help="N per multi_proc_pmu run; paper uses 1000 (scaled 10x down)")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()

    plat = load_platform()
    bm = parse_benchmarks_yaml()
    raw_dir = DATA_RAW / "e23"
    raw_dir.mkdir(parents=True, exist_ok=True)

    # Load theta_star per (bench, channel)
    theta_map: dict[tuple[str, str], str] = {}
    if Path(args.theta).exists():
        with open(args.theta) as f:
            for row in csv.DictReader(f):
                theta_map[(row["bench"], row["channel"])] = row["theta_star"]
    else:
        # fallback to channel_attackers from benchmarks.yaml
        ca = bm.get("channel_attackers", {})
        # populated lazily below

    bench_dir = Path(__file__).resolve().parents[2] / "build"
    benches = (args.benches or [d.name for d in sorted(bench_dir.iterdir())
                                 if d.is_dir() and d.name in
                                 bm["taclebench_full"]])

    log = start_step("E23_five_config_sweep",
                     params={"benches": benches, "R": args.R,
                             "configs": CONFIGS,
                             "freq_khz": plat.get("core_freq_khz")})
    pmc_csv = Path(args.out_dir) / "e23_pmc_aggregated.csv"
    fv_csv = Path(args.out_dir) / "e23_feature_vectors.csv"
    amp_csv = Path(args.out_dir) / "e23_amplification_scalars.csv"

    pmc_rows: list[dict] = []
    feat_rows: list[dict] = []
    amp_rows: list[dict] = []
    for bench in benches:
        # default theta picks from yaml when search not run
        theta = {ch: theta_map.get(
            (bench, ch),
            bm.get("channel_attackers", {}).get(ch, "MTH_MEM"))
                  for ch in ("LLC", "BUS", "MEM")}
        per_cfg: dict[str, dict] = {}
        for cfg in CONFIGS:
            atks = build_attackers(cfg, theta)
            r = run_pmu(victim=bench, attackers=atks, n=args.R,
                        freq_khz=plat.get("core_freq_khz", 1600000),
                        sudo_pw=args.sudo_pw, timeout=1800)
            (raw_dir / f"{bench}__{cfg}.txt").write_text(r["raw"])
            cyc = r["metrics"].get("cycles", {})
            mis = r["metrics"].get("cache_misses", {})
            ins = r["metrics"].get("instructions", {})
            row = {
                "bench": bench, "config": cfg, "n": args.R,
                "theta_LLC": theta["LLC"], "theta_BUS": theta["BUS"],
                "theta_MEM": theta["MEM"],
                "cycles_med": cyc.get("median", 0),
                "cycles_max": cyc.get("max", 0),
                "inst_med": ins.get("median", 0),
                "miss_med": mis.get("median", 0),
                "miss_max": mis.get("max", 0),
            }
            pmc_rows.append(row); per_cfg[cfg] = row
            print(f"[{bench}/{cfg}] cyc_med={row['cycles_med']:.0f} "
                  f"miss_med={row['miss_med']:.0f}", file=sys.stderr)

        solo = per_cfg["Solo"]
        c_solo = solo["cycles_med"] or 1
        i_solo = solo["inst_med"] or 1
        # Per-channel single-channel slowdown components (Delta_i^rho_k)
        d_llc = max(per_cfg["LLC"]["cycles_med"] - c_solo, 0)
        d_bus = max(per_cfg["BUS"]["cycles_med"] - c_solo, 0)
        d_mem = max(per_cfg["MEM"]["cycles_med"] - c_solo, 0)
        d_sum = d_llc + d_bus + d_mem
        d_mix = max(per_cfg["Mix"]["cycles_med"] - c_solo, 0)
        a_max = (d_mix / d_sum) if d_sum > 0 else 1.0
        # 3+3 feature vector (TLB / BR / CV imputed as 0; see header)
        a_l2 = (per_cfg["LLC"]["miss_max"] /
                max(per_cfg["LLC"]["cycles_med"], 1))
        a_bus = (per_cfg["BUS"]["miss_max"] /
                 max(per_cfg["BUS"]["cycles_med"], 1))
        ipc = i_solo / c_solo
        feat_rows.append({
            "bench": bench,
            "alpha_l2": round(a_l2, 6),
            "alpha_bus": round(a_bus, 6),
            "ipc": round(ipc, 6),
            "alpha_tlb": 0.0,
            "alpha_br": 0.0,
            "cv_l2": 0.0,
        })
        amp_rows.append({
            "bench": bench, "c_solo_med": c_solo,
            "delta_llc": d_llc, "delta_bus": d_bus, "delta_mem": d_mem,
            "delta_sum": d_sum, "delta_mix": d_mix,
            "a_max": round(a_max, 6),
        })
    with pmc_csv.open("w", newline="") as f:
        if pmc_rows:
            w = csv.DictWriter(f, fieldnames=list(pmc_rows[0].keys()))
            w.writeheader(); w.writerows(pmc_rows)
        else:
            f.write("bench,config,n,theta_LLC,theta_BUS,theta_MEM,cycles_med,cycles_max,inst_med,miss_med,miss_max\n")
    with fv_csv.open("w", newline="") as f:
        if feat_rows:
            w = csv.DictWriter(f, fieldnames=list(feat_rows[0].keys()))
            w.writeheader(); w.writerows(feat_rows)
        else:
            f.write("bench,alpha_l2,alpha_bus,ipc,alpha_tlb,alpha_br,cv_l2\n")
    with amp_csv.open("w", newline="") as f:
        if amp_rows:
            w = csv.DictWriter(f, fieldnames=list(amp_rows[0].keys()))
            w.writeheader(); w.writerows(amp_rows)
        else:
            f.write("bench,c_solo_med,delta_llc,delta_bus,delta_mem,delta_sum,delta_mix,a_max\n")
    end_step(outputs=[str(pmc_csv), str(fv_csv), str(amp_csv)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
