#!/usr/bin/env python3
"""E3 - Interference-bound calculator + 7 baselines + CDF figure.

Inputs (defaults under data/processed/):
    e23_pmc_aggregated.csv          per-bench x per-config medians
    e23_amplification_scalars.csv   a_i^max
    e23_feature_vectors.csv         (TLB / BR / CV may be 0)

Bounds implemented (paper Table 3):
  1. full-isolation       Cbar = gamma * max_attacker per channel summed
  2. Kim'16 (DRAM-tight)  Cbar = gamma * (Csolo + Delta_MEM_max)
  3. Hassan'18 (DRAM)     Cbar = gamma * (Csolo + 1.15 * Delta_MEM)
  4. Sullivan'24 (LLC)    Cbar = gamma * (Csolo + Delta_LLC + Delta_BUS)
  5. RAMPART-additive     Cbar = gamma * (Csolo + sum_k Delta_k)
  6. RAMPART-full         Cbar = gamma * (Csolo + sum_k Delta_k + A_i),
                          A_i = max(0, a_max - 1) * sum_k Delta_k
  7. Empirical-max        observed C_med under Mix (ground reference)

Output:
    data/processed/e3_bounds.csv
    figs/e3_bounds_cdf.svg
"""
from __future__ import annotations
import argparse
import csv
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args, FIG_DIR
from lib.platform import load as load_platform
from lib.svg_plot import cdf

BOUNDS = ["full_iso", "kim16", "hassan18", "sullivan24",
          "rampart_add", "rampart_full", "empirical_max"]


def read_csv(p):
    with open(p) as f:
        return list(csv.DictReader(f))


def main() -> int:
    p = parse_args("E3 bound calculator")
    p.add_argument("--pmc", default=str(Path(__file__).resolve().parents[1]
                                         / "data/processed/e23_pmc_aggregated.csv"))
    p.add_argument("--amp", default=str(Path(__file__).resolve().parents[1]
                                         / "data/processed/e23_amplification_scalars.csv"))
    p.add_argument("--safety-rho", type=float, default=1.0,
                   help="global multiplicative safety factor on rampart_full "
                        "(use apply_safety_factor.py for per-bench tail-aware rho)")
    args = p.parse_args()
    plat = load_platform()
    gamma = float(plat.get("gamma_pmu", 1.02))
    rho_safe = float(args.safety_rho)

    pmc = read_csv(args.pmc)
    amp = {r["bench"]: r for r in read_csv(args.amp)}
    by_bench: dict[str, dict[str, dict]] = {}
    for r in pmc:
        by_bench.setdefault(r["bench"], {})[r["config"]] = r

    log = start_step("E3_bounds",
                     params={"gamma": gamma,
                             "n_benches": len(by_bench)})
    rows: list[dict] = []
    for bench, cfgs in by_bench.items():
        if "Solo" not in cfgs:
            continue
        c_solo = float(cfgs["Solo"]["cycles_med"])
        d_llc = max(float(cfgs.get("LLC", cfgs["Solo"])["cycles_med"]) - c_solo, 0)
        d_bus = max(float(cfgs.get("BUS", cfgs["Solo"])["cycles_med"]) - c_solo, 0)
        d_mem = max(float(cfgs.get("MEM", cfgs["Solo"])["cycles_med"]) - c_solo, 0)
        d_sum = d_llc + d_bus + d_mem
        c_mix = float(cfgs.get("Mix", cfgs["Solo"])["cycles_med"])
        a_max = float(amp.get(bench, {}).get("a_max", 1.0))
        A_i = max(a_max - 1.0, 0.0) * d_sum

        bnd = {
            "full_iso":       gamma * (c_solo + 3 * max(d_llc, d_bus, d_mem)),
            "kim16":          gamma * (c_solo + d_mem),
            "hassan18":       gamma * (c_solo + 1.15 * d_mem),
            "sullivan24":     gamma * (c_solo + d_llc + d_bus),
            "rampart_add":    gamma * (c_solo + d_sum),
            "rampart_full":   rho_safe * gamma * (c_solo + d_sum + A_i),
            "empirical_max":  c_mix,
        }
        row = {"bench": bench, "c_solo": c_solo,
               "d_llc": d_llc, "d_bus": d_bus, "d_mem": d_mem,
               "a_max": a_max, "A_i": round(A_i, 1)}
        for k in BOUNDS:
            row[k] = round(bnd[k], 1)
            row[f"{k}_ratio"] = round(bnd[k] / c_solo if c_solo else 0, 4)
        rows.append(row)

    out = Path(args.out_dir) / "e3_bounds.csv"
    with out.open("w", newline="") as f:
        if rows:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader(); w.writerows(rows)

    palette = {"full_iso":      "#7f7f7f",
               "kim16":         "#1f77b4",
               "hassan18":      "#17becf",
               "sullivan24":    "#9467bd",
               "rampart_add":   "#2ca02c",
               "rampart_full":  "#d62728",
               "empirical_max": "#000000"}
    series = [(k, palette[k], [r[f"{k}_ratio"] for r in rows]) for k in BOUNDS]
    fig = FIG_DIR / "e3_bounds_cdf.svg"
    cdf(series, str(fig),
        title="E3 bound tightness CDF (Cbar / Csolo)",
        xlabel="bound ratio")
    print(f"[ok] {len(rows)} benches -> {out}, {fig}")
    end_step(outputs=[str(out), str(fig)],
             extra={"n_benches": len(rows)})
    return 0


if __name__ == "__main__":
    sys.exit(main())
