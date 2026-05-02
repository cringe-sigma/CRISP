#!/usr/bin/env python3
"""E7 - Case study + fault injection.

Runs the six-task case study for `--duration-s` seconds, recording
deadline-miss counts (a deadline miss is c_obs > Cbar_RAMPART_full).
Every `--inject-every` releases a synthetic 'Lemma-2-violating' triple
(MTH_MEM x3) is injected and the next-release recovery is checked.

Output: data/processed/e7_case_study.csv
"""
from __future__ import annotations
import argparse, csv, os, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args
from lib.common import run_pmu


def main() -> int:
    p = parse_args("E7 case study")
    p.add_argument("--bounds", default=str(
        Path(__file__).resolve().parents[1] / "data/processed/e3_bounds.csv"))
    p.add_argument("--case-study", nargs="+",
                   default=["fir2dim", "fmref", "cosf", "test3",
                            "jfdctint", "fac"])
    p.add_argument("--attackers", nargs="+",
                   default=["PR_CACHE", "PR_MEMBUS", "MTH_MEM"])
    p.add_argument("--inject-every", type=int, default=20)
    p.add_argument("--R", type=int, default=100)
    p.add_argument("--duration-s", type=int, default=120,
                   help="paper uses 86400 (24h); default 120 for smoke")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()

    bounds = {}
    with open(args.bounds) as f:
        for r in csv.DictReader(f): bounds[r["bench"]] = r

    log = start_step("E7_case_study", params=vars(args))
    out = Path(args.out_dir) / "e7_case_study.csv"
    fields = ["t_s", "release", "victim", "injected", "c_obs",
              "rampart_full", "deadline_miss", "shield_recovered"]
    t0 = time.time()
    n_miss = 0
    n_recover_ok = 0
    n_inject = 0
    last_after_inject: int | None = None
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader()
        rel = 0
        while time.time() - t0 < args.duration_s:
            victim = args.case_study[rel % len(args.case_study)]
            inject = (rel > 0 and rel % args.inject_every == 0)
            atks = (["MTH_MEM", "MTH_MEM", "MTH_MEM"] if inject
                     else args.attackers)
            r = run_pmu(victim, atks, n=args.R,
                        sudo_pw=args.sudo_pw, timeout=600)
            c_obs = r["metrics"].get("cycles", {}).get("median", 0)
            b = float(bounds.get(victim, {}).get("rampart_full", c_obs))
            miss = c_obs > b
            recovered = ""
            if inject:
                n_inject += 1; last_after_inject = rel
            elif last_after_inject is not None and rel == last_after_inject + 1:
                recovered = str(not miss)
                if not miss: n_recover_ok += 1
                last_after_inject = None
            if miss: n_miss += 1
            w.writerow({"t_s": round(time.time()-t0, 2),
                        "release": rel,
                        "victim": victim,
                        "injected": inject,
                        "c_obs": c_obs,
                        "rampart_full": b,
                        "deadline_miss": miss,
                        "shield_recovered": recovered})
            f.flush(); rel += 1
    end_step(outputs=[str(out)],
             extra={"misses": n_miss, "injections": n_inject,
                    "recoveries": n_recover_ok})
    return 0


if __name__ == "__main__":
    sys.exit(main())
