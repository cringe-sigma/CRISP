#!/usr/bin/env python3
"""E3.3 - 24h sweep counting additive-bound violations.

For each victim, exhaustively (or up to --max-triples random) tries
real co-runner triples drawn from TACLeBench, recording the *worst*
observed C_obs.  A violation is flagged when

    C_obs_max  >  RAMPART-additive bound  (from e3_bounds.csv).

Output: data/processed/e33_additive_violations.csv +
        figs/e33_additive_scatter.svg
"""
from __future__ import annotations
import argparse
import csv
import os
import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args, FIG_DIR
from lib.common import run_pmu, parse_benchmarks_yaml
from lib.platform import load as load_platform
from lib.svg_plot import scatter


def main() -> int:
    p = parse_args("E3.3 additive-bound violations")
    p.add_argument("--bounds",
                   default=str(Path(__file__).resolve().parents[1] /
                               "data/processed/e3_bounds.csv"))
    p.add_argument("--R", type=int, default=100)
    p.add_argument("--max-triples", type=int, default=20,
                   help="random triples per victim (paper: 24h sweep, "
                        "use --max-triples 0 to enumerate all)")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--time-budget-s", type=int, default=0,
                   help="wall-clock cap; 0 disables the cap")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()
    rng = random.Random(args.seed)

    bm = parse_benchmarks_yaml()
    bounds = {}
    with open(args.bounds) as f:
        for r in csv.DictReader(f):
            bounds[r["bench"]] = r
    pool = [b for b in bm["taclebench_full"] if b in bounds]

    log = start_step("E33_additive_violations", params=vars(args))
    out = Path(args.out_dir) / "e33_additive_violations.csv"
    fields = ["victim", "i", "triple", "c_obs",
              "rampart_add", "rampart_full",
              "violates_add", "violates_full"]
    t0 = time.time()
    n_v_add = n_v_full = 0; n = 0
    obs_pairs: list[tuple[float, float]] = []
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader()
        for victim in pool:
            br = bounds[victim]
            b_add = float(br["rampart_add"])
            b_full = float(br["rampart_full"])
            n_per = args.max_triples
            triples = ([rng.sample(pool, 3) for _ in range(n_per)]
                       if n_per > 0 else
                       [[a, b, c] for a in pool for b in pool for c in pool])
            best = 0.0
            for i, tri in enumerate(triples):
                if args.time_budget_s and time.time() - t0 > args.time_budget_s:
                    break
                r = run_pmu(victim, tri, n=args.R,
                            sudo_pw=args.sudo_pw, timeout=1800)
                c_obs = r["metrics"].get("cycles", {}).get("median", 0)
                best = max(best, c_obs)
                v_add = c_obs > b_add; v_full = c_obs > b_full
                if v_add: n_v_add += 1
                if v_full: n_v_full += 1
                w.writerow({"victim": victim, "i": i,
                            "triple": "+".join(tri),
                            "c_obs": c_obs,
                            "rampart_add": b_add,
                            "rampart_full": b_full,
                            "violates_add": v_add,
                            "violates_full": v_full})
                f.flush(); n += 1
            obs_pairs.append((b_add, best))
            print(f"[{victim}] best_obs={best:.0f} bound_add={b_add:.0f} "
                  f"viol_add={int(best > b_add)}", file=sys.stderr)

    fig = FIG_DIR / "e33_additive_scatter.svg"
    scatter([("worst observed", "#d62728", obs_pairs)], str(fig),
            title="E3.3 worst observed cycles vs RAMPART-additive bound",
            xlabel="rampart_add bound (cycles)",
            ylabel="worst observed cycles", diagonal=True)
    end_step(outputs=[str(out), str(fig)],
             extra={"n": n, "v_add": n_v_add, "v_full": n_v_full})
    return 0


if __name__ == "__main__":
    sys.exit(main())
