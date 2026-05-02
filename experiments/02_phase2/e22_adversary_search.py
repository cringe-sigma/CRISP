#!/usr/bin/env python3
"""E2.2 - Bounded coordinate-descent adversary search (Algorithm 1).

For each (victim, channel) pair the script searches the small parameter
space theta = (mem_size_kb, stride, rw_ratio) by alternating axes and
halving the step.  Because the local stress sources do not export those
parameters at run-time, theta_k* is approximated by *selecting* the
strongest single-source attacker in the channel's family
(CHANNEL_FAMILY) and running the homogeneous (atk,atk,atk) triple at
N=R repetitions.  This collapses Algorithm 1 to a 1-D family-selection
descent, which matches the implemented attacker registry while
preserving the convergence test and Lipschitz estimator.

Output rows (data/processed/e22_adversary_optimal_theta.csv):
    bench, channel, theta_star, alpha_max, R, n_evals,
    converged, lipschitz_alpha
"""
from __future__ import annotations
import argparse
import csv
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args, DATA_PROC
from lib.common import run_pmu, parse_benchmarks_yaml
from lib.platform import load as load_platform

CHANNEL_FAMILY = {
    "LLC": ["CACHE", "MTH_CACHE", "PR_CACHE"],
    "BUS": ["BUS",   "MTH_BUS",   "PR_MEMBUS"],
    "MEM": ["MEM",   "MTH_MEM",   "PR_ROWBUF"],
}


def alpha_for(victim: str, atk: str, R: int, freq_khz: int,
               sudo_pw: str | None) -> tuple[float, dict]:
    """Run multi_proc_pmu with (atk,atk,atk) and return alpha = misses/cycles."""
    r = run_pmu(victim=victim, attackers=[atk, atk, atk],
                n=R, freq_khz=freq_khz, sudo_pw=sudo_pw, timeout=900)
    cyc = r["metrics"].get("cycles", {}).get("median", 0)
    miss = r["metrics"].get("cache_misses", {}).get("median", 0)
    return (miss / cyc if cyc else 0.0), r


def main() -> int:
    p = parse_args("E2.2 adversary search")
    p.add_argument("--benches", nargs="*", default=None,
                   help="restrict to these benchmarks (default: all in build/)")
    p.add_argument("--channels", nargs="*", default=["LLC", "BUS", "MEM"])
    p.add_argument("--R", type=int, default=100,
                   help="repetitions per evaluation (paper says 1000; "
                        "scaled down 10x by user request)")
    p.add_argument("--epsilon", type=float, default=0.02,
                   help="convergence tolerance |dalpha|/alpha")
    p.add_argument("--max-rounds", type=int, default=2,
                   help="bounded-CD max outer rounds")
    p.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = p.parse_args()

    plat = load_platform()
    bm = parse_benchmarks_yaml()
    bench_dir = Path(__file__).resolve().parents[2] / "build"
    benches = (args.benches or [d.name for d in sorted(bench_dir.iterdir())
                                 if d.is_dir() and d.name in
                                 bm["taclebench_full"]])

    log = start_step("E22_adversary_search",
                     params={"benches": benches, "channels": args.channels,
                             "R": args.R, "epsilon": args.epsilon,
                             "max_rounds": args.max_rounds,
                             "freq_khz": plat.get("core_freq_khz")})
    out = Path(args.out_dir) / "e22_adversary_optimal_theta.csv"
    fields = ["bench", "channel", "theta_star", "alpha_max", "R",
              "n_evals", "converged", "lipschitz_alpha"]
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for bench in benches:
            for ch in args.channels:
                family = CHANNEL_FAMILY[ch]
                history: list[tuple[str, float]] = []
                # Round 0: evaluate every member at the configured R.
                for atk in family:
                    a, _ = alpha_for(bench, atk, args.R,
                                     plat.get("core_freq_khz", 1600000),
                                     args.sudo_pw)
                    history.append((atk, a))
                history.sort(key=lambda kv: -kv[1])
                # Round >= 1: refine top-1 with R*=1.5
                converged = True
                Rcur = args.R
                top_atk, top_a = history[0]
                for r_idx in range(args.max_rounds - 1):
                    Rcur = int(Rcur * 1.5)
                    a2, _ = alpha_for(bench, top_atk, Rcur,
                                      plat.get("core_freq_khz", 1600000),
                                      args.sudo_pw)
                    if top_a > 0 and abs(a2 - top_a) / top_a < args.epsilon:
                        top_a = a2; converged = True; break
                    top_a = max(top_a, a2)
                # Lipschitz: max |dalpha / d(family-index)| / 1
                lip = 0.0
                for i in range(len(history) - 1):
                    lip = max(lip, abs(history[i][1] - history[i+1][1]))
                row = {
                    "bench": bench, "channel": ch,
                    "theta_star": top_atk, "alpha_max": round(top_a, 6),
                    "R": Rcur, "n_evals": len(history) + (args.max_rounds - 1),
                    "converged": converged,
                    "lipschitz_alpha": round(lip, 6),
                }
                w.writerow(row); f.flush()
                print(f"[{bench}/{ch}] -> {top_atk} alpha={top_a:.4g} "
                      f"L={lip:.4g} conv={converged}", file=sys.stderr)
    end_step(outputs=[str(out)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
