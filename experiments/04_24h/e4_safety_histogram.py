#!/usr/bin/env python3
"""E4 - 24h safety histogram.

For a small case-study task set (six benches, see --case-study),
repeatedly run multi_proc_pmu under the worst-triple discovered in
E3.3 (or a fixed Mix configuration) for a configurable duration and
record observed cycles.  Compute C_obs / Cbar_RAMPART_full and emit a
histogram + violation count.

Output: data/processed/e4_safety_histogram.csv
        figs/e4_safety_histogram.svg
"""
from __future__ import annotations
import argparse, csv, os, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args, FIG_DIR
from lib.common import run_pmu
from lib.svg_plot import bar


def main() -> int:
    p = parse_args("E4 24h safety histogram")
    p.add_argument("--bounds",
                   default=str(Path(__file__).resolve().parents[1] /
                               "data/processed/e3_bounds.csv"))
    p.add_argument("--case-study", nargs="+",
                   default=["fir2dim", "fmref", "cosf", "test3",
                            "jfdctint", "fac"])
    p.add_argument("--attackers", nargs="+",
                   default=["PR_CACHE", "PR_MEMBUS", "MTH_MEM"])
    p.add_argument("--R", type=int, default=100)
    p.add_argument("--duration-s", type=int, default=60,
                   help="paper uses 86400 (24h); default 60 for smoke test")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()

    bounds = {}
    with open(args.bounds) as f:
        for r in csv.DictReader(f): bounds[r["bench"]] = r
    log = start_step("E4_safety_histogram", params=vars(args))
    out = Path(args.out_dir) / "e4_safety_histogram.csv"
    fields = ["t_s", "victim", "c_obs", "rampart_full", "ratio",
              "violation"]
    t0 = time.time()
    n_v = 0
    ratios: list[float] = []
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader()
        i = 0
        while time.time() - t0 < args.duration_s:
            victim = args.case_study[i % len(args.case_study)]
            r = run_pmu(victim, args.attackers, n=args.R,
                        sudo_pw=args.sudo_pw, timeout=600)
            c_obs = r["metrics"].get("cycles", {}).get("median", 0)
            b = float(bounds.get(victim, {}).get("rampart_full", c_obs))
            ratio = c_obs / b if b else 1.0
            ratios.append(ratio)
            v = ratio > 1.0
            if v: n_v += 1
            w.writerow({"t_s": round(time.time() - t0, 2),
                        "victim": victim, "c_obs": c_obs,
                        "rampart_full": b, "ratio": round(ratio, 4),
                        "violation": v})
            f.flush(); i += 1
    # Histogram (10 bins) -> bar
    if ratios:
        lo, hi = min(ratios), max(ratios)
        if hi == lo: hi = lo + 1e-9
        nb = 12
        bins = [0]*nb
        for x in ratios:
            k = min(int((x - lo) / (hi - lo) * nb), nb - 1)
            bins[k] += 1
        labels = [f"{lo + (hi-lo)*k/nb:.3f}" for k in range(nb)]
        bar(bins, labels, str(FIG_DIR / "e4_safety_histogram.svg"),
            title=f"E4 C_obs / Cbar_RAMPART (n={len(ratios)}, viol={n_v})",
            ylabel="count")
    end_step(outputs=[str(out)],
             extra={"violations": n_v, "n": len(ratios)})
    return 0


if __name__ == "__main__":
    sys.exit(main())
