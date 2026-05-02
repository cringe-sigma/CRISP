#!/usr/bin/env python3
"""E2.5 - Lemma 2 check (Mix dominance).

For 500 uniformly-random three-task triples per platform, compare the
real amplification a_real = (C_obs - C_solo) / sum(Delta_k) against
a_max from E2.3.  A violation is counted whenever a_real > a_max.

Output: data/processed/e25_lemma2.csv
"""
from __future__ import annotations
import argparse
import csv
import os
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args
from lib.common import run_pmu, parse_benchmarks_yaml
from lib.platform import load as load_platform


def main() -> int:
    p = parse_args("E2.5 Lemma 2 check")
    p.add_argument("--amp",
                   default=str(Path(__file__).resolve().parents[1] /
                               "data/processed/e23_amplification_scalars.csv"))
    p.add_argument("--R", type=int, default=100,
                   help="paper uses 1000; scaled 10x down by user request")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--n-triples", type=int, default=500)
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()

    rng = random.Random(args.seed)
    plat = load_platform()
    bm = parse_benchmarks_yaml()
    amp: dict[str, dict] = {}
    with open(args.amp) as f:
        for row in csv.DictReader(f):
            amp[row["bench"]] = row
    benches = sorted(amp)
    pool = [b for b in bm["taclebench_full"] if b in benches]

    log = start_step("E25_lemma2", params=vars(args))
    out = Path(args.out_dir) / "e25_lemma2.csv"
    fields = ["i", "victim", "triple", "c_obs_med", "c_solo",
              "a_real", "a_max", "violation"]
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader()
        n_v = 0
        for i in range(args.n_triples):
            victim = rng.choice(pool)
            triple = [rng.choice(pool) for _ in range(3)]
            r = run_pmu(victim, triple, n=args.R,
                        sudo_pw=args.sudo_pw, timeout=900)
            c_obs = r["metrics"].get("cycles", {}).get("median", 0)
            c_solo = float(amp[victim]["c_solo_med"])
            d_sum = float(amp[victim]["delta_sum"])
            a_real = ((c_obs - c_solo) / d_sum) if d_sum > 0 else 0.0
            a_max = float(amp[victim]["a_max"])
            viol = a_real > a_max
            if viol: n_v += 1
            w.writerow({"i": i, "victim": victim,
                        "triple": "+".join(triple),
                        "c_obs_med": c_obs, "c_solo": c_solo,
                        "a_real": round(a_real, 4),
                        "a_max": round(a_max, 4),
                        "violation": viol})
            f.flush()
            if i % 25 == 0:
                print(f"  i={i} viol={n_v}", file=sys.stderr)
    end_step(outputs=[str(out)],
             extra={"n_triples": args.n_triples, "violations": n_v})
    return 0


if __name__ == "__main__":
    sys.exit(main())
