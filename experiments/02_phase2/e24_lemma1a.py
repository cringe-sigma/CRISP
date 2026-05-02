#!/usr/bin/env python3
"""E2.4 - Lemma 1a check (synthetic attacker dominates real co-runner).

For each (victim, channel) pair we compare alpha under the channel's
synthetic attacker S_k(theta*) (from E2.2) against alpha under a
uniformly-sampled real TACLeBench co-runner triple.  A violation is
counted whenever alpha_real > alpha_S.

Output: data/processed/e24_lemma1a.csv
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
    p = parse_args("E2.4 Lemma 1a check")
    p.add_argument("--theta",
                   default=str(Path(__file__).resolve().parents[1] /
                               "data/processed/e22_adversary_optimal_theta.csv"))
    p.add_argument("--R", type=int, default=100,
                   help="paper uses 1000; scaled 10x down by user request")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--n-pairs", type=int, default=159,
                   help="paper iMX uses 53*3=159")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()

    rng = random.Random(args.seed)
    plat = load_platform()
    bm = parse_benchmarks_yaml()
    theta_map: dict[tuple[str, str], str] = {}
    with open(args.theta) as f:
        for row in csv.DictReader(f):
            theta_map[(row["bench"], row["channel"])] = row["theta_star"]
    benches = sorted({k[0] for k in theta_map})
    real_pool = [b for b in bm["taclebench_full"] if b in benches]

    log = start_step("E24_lemma1a", params=vars(args))
    out = Path(args.out_dir) / "e24_lemma1a.csv"
    fields = ["bench", "channel", "alpha_S", "alpha_real", "ratio",
              "violation", "real_triple"]
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields); w.writeheader()
        n_v = 0
        n = 0
        for bench in benches:
            for ch in ("LLC", "BUS", "MEM"):
                if n >= args.n_pairs:
                    break
                atk = theta_map[(bench, ch)]
                rs = run_pmu(bench, [atk, atk, atk], n=args.R,
                             sudo_pw=args.sudo_pw, timeout=900)
                a_s = (rs["metrics"].get("cache_misses", {}).get("max", 0)
                       / max(rs["metrics"].get("cycles", {}).get("median", 1), 1))
                triple = rng.sample(real_pool, 3)
                rr = run_pmu(bench, triple, n=args.R,
                             sudo_pw=args.sudo_pw, timeout=900)
                a_r = (rr["metrics"].get("cache_misses", {}).get("max", 0)
                       / max(rr["metrics"].get("cycles", {}).get("median", 1), 1))
                ratio = a_r / a_s if a_s else float("inf")
                violation = a_r > a_s
                if violation: n_v += 1
                w.writerow({"bench": bench, "channel": ch,
                            "alpha_S": round(a_s, 6),
                            "alpha_real": round(a_r, 6),
                            "ratio": round(ratio, 4),
                            "violation": violation,
                            "real_triple": "+".join(triple)})
                f.flush(); n += 1
        print(f"[ok] {n} pairs, {n_v} violations", file=sys.stderr)
    end_step(outputs=[str(out)], extra={"violations": n_v, "n_pairs": n})
    return 0


if __name__ == "__main__":
    sys.exit(main())
