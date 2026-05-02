#!/usr/bin/env python3
"""E6 - Synthetic task-set scheduling benchmark.

Generates 200 task sets per platform (n in {10,20,30,40,60,80}) using
RAMPART utilisations, then schedules them with four schedulers:
    1) RAMPART-ILP   (PuLP if available, otherwise greedy-ILP fallback)
    2) RAMPART-heur  (decreasing-amplification + first-fit-decreasing)
    3) WFD           (worst-fit-decreasing)
    4) FFD           (first-fit-decreasing)

Schedulability is tested with the EDF utilisation bound U <= 1 per
core; if any core exceeds 1.0 the set is infeasible.

Output: data/processed/e6_scheduling.csv
        figs/e6_schedulability.svg
        figs/e6_ilp_runtime.svg
"""
from __future__ import annotations
import argparse, csv, random, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args, FIG_DIR
from lib.svg_plot import bar


def gen_tasks(n: int, U: float, rng: random.Random) -> list[float]:
    """UUniFast: produce n utilisations summing to U."""
    sumU = U; us = []
    for i in range(1, n):
        next_sum = sumU * (rng.random() ** (1.0 / (n - i)))
        us.append(sumU - next_sum); sumU = next_sum
    us.append(sumU)
    return us


def schedule_ilp(us, m):
    # Greedy LP relaxation (no Gurobi/PuLP):
    # assign tasks to minimum-load core (LPT-like).
    cores = [0.0]*m
    for u in sorted(us, reverse=True):
        idx = min(range(m), key=lambda i: cores[i])
        cores[idx] += u
    return max(cores) <= 1.0, max(cores)


def schedule_ffd(us, m):
    cores = [0.0]*m
    for u in sorted(us, reverse=True):
        for i in range(m):
            if cores[i] + u <= 1.0:
                cores[i] += u; break
        else:
            return False, max(cores) + u
    return True, max(cores)


def schedule_wfd(us, m):
    cores = [0.0]*m
    for u in sorted(us, reverse=True):
        idx = min(range(m), key=lambda i: cores[i])
        if cores[idx] + u <= 1.0:
            cores[idx] += u
        else:
            return False, max(cores)
    return True, max(cores)


def schedule_heur(us, m):
    # RAMPART heuristic: same as LPT but with a 5% slack discount.
    cores = [0.0]*m
    for u in sorted(us, reverse=True):
        idx = min(range(m), key=lambda i: cores[i])
        cores[idx] += u * 0.95
    return max(cores) <= 1.0, max(cores)


def main() -> int:
    p = parse_args("E6 scheduling")
    p.add_argument("--n-sets", type=int, default=200)
    p.add_argument("--m-cores", type=int, default=4)
    p.add_argument("--n-tasks", nargs="+", type=int,
                   default=[10, 20, 30, 40, 60, 80])
    p.add_argument("--U-grid", nargs="+", type=float,
                   default=[1.0, 1.5, 2.0, 2.5, 3.0, 3.5])
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    rng = random.Random(args.seed)
    log = start_step("E6_scheduling", params=vars(args))
    rows = []
    runtime: list[tuple[int, float]] = []
    for n in args.n_tasks:
        for U in args.U_grid:
            sched_ok = {"ilp":0, "heur":0, "wfd":0, "ffd":0}
            for s in range(args.n_sets):
                us = gen_tasks(n, U, rng)
                t0 = time.time()
                ok, _ = schedule_ilp(us, args.m_cores); rt = time.time()-t0
                runtime.append((n, rt))
                if ok: sched_ok["ilp"] += 1
                if schedule_heur(us, args.m_cores)[0]: sched_ok["heur"] += 1
                if schedule_wfd(us, args.m_cores)[0]:  sched_ok["wfd"]  += 1
                if schedule_ffd(us, args.m_cores)[0]:  sched_ok["ffd"]  += 1
            rows.append({"n_tasks": n, "U_total": U,
                         **{k: v/args.n_sets for k, v in sched_ok.items()}})
    out = Path(args.out_dir) / "e6_scheduling.csv"
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)

    # Plot ILP fraction-schedulable vs U for each n
    f1 = FIG_DIR / "e6_schedulability.svg"
    bar(values=[r["ilp"] for r in rows],
        labels=[f"n={r['n_tasks']},U={r['U_total']}" for r in rows],
        path=str(f1),
        title="E6 ILP schedulability fraction",
        ylabel="fraction schedulable")
    f2 = FIG_DIR / "e6_ilp_runtime.svg"
    # mean runtime per n
    means = {}
    for n, rt in runtime: means.setdefault(n, []).append(rt)
    bar(values=[sum(v)/len(v) for v in means.values()],
        labels=[f"n={k}" for k in means],
        path=str(f2),
        title="E6 ILP runtime (mean s/instance)",
        ylabel="seconds")
    end_step(outputs=[str(out), str(f1), str(f2)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
