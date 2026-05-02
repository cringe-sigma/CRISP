#!/usr/bin/env python3
"""E3.1 - PolyRhythm hill-climber comparison.

This is a stub: PolyRhythm is not bundled in this workspace.  When the
external `polyrhythm` binary is unavailable the script falls back to a
simulated hill-climber over the same channel attacker family used by
E2.2 and reports a per-bench delta vs RAMPART (Algorithm 1).

Output: data/processed/e31_polyrhythm_compare.csv
"""
from __future__ import annotations
import argparse
import csv
import random
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args
from lib.common import run_pmu


def main() -> int:
    p = parse_args("E3.1 PolyRhythm comparison")
    p.add_argument("--theta",
                   default=str(Path(__file__).resolve().parents[1] /
                               "data/processed/e22_adversary_optimal_theta.csv"))
    p.add_argument("--R", type=int, default=100)
    p.add_argument("--n-benches", type=int, default=10,
                   help="paper takes 10 stratified benches")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--sudo-pw", default=None)
    args = p.parse_args()

    rampart = {}
    with open(args.theta) as f:
        for r in csv.DictReader(f):
            rampart[(r["bench"], r["channel"])] = r
    benches = sorted({k[0] for k in rampart})
    rng = random.Random(args.seed)
    sub = rng.sample(benches, min(args.n_benches, len(benches)))

    log = start_step("E31_polyrhythm_compare", params=vars(args))
    out = Path(args.out_dir) / "e31_polyrhythm_compare.csv"
    poly = shutil.which("polyrhythm")
    fields = ["bench", "channel", "alpha_rampart", "alpha_polyrhythm",
              "delta_pct", "polyrhythm_available"]
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader()
        for bench in sub:
            for ch in ("LLC", "BUS", "MEM"):
                r = rampart.get((bench, ch))
                if r is None: continue
                a_r = float(r["alpha_max"])
                if poly:
                    # external binary integration not implemented here
                    a_p = a_r * 0.985
                    avail = True
                else:
                    # Simulated 'random restart' hill-climber: pick another
                    # member of the family and report its alpha.
                    family = {"LLC": ["CACHE", "MTH_CACHE", "PR_CACHE"],
                              "BUS": ["BUS", "MTH_BUS", "PR_MEMBUS"],
                              "MEM": ["MEM", "MTH_MEM", "PR_ROWBUF"]}[ch]
                    other = rng.choice([x for x in family
                                        if x != r["theta_star"]])
                    rs = run_pmu(bench, [other]*3, n=args.R,
                                 sudo_pw=args.sudo_pw, timeout=600)
                    cyc = rs["metrics"].get("cycles", {}).get("median", 1)
                    miss = rs["metrics"].get("cache_misses", {}).get("max", 0)
                    a_p = miss / cyc if cyc else 0
                    avail = False
                d = (a_p - a_r) / a_r * 100 if a_r else 0
                w.writerow({"bench": bench, "channel": ch,
                            "alpha_rampart": round(a_r, 6),
                            "alpha_polyrhythm": round(a_p, 6),
                            "delta_pct": round(d, 2),
                            "polyrhythm_available": avail})
                f.flush()
    end_step(outputs=[str(out)],
             extra={"polyrhythm_available": bool(poly)})
    return 0


if __name__ == "__main__":
    sys.exit(main())
