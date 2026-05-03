#!/usr/bin/env python3
"""E4 24h safety histogram - RESUME from existing CSV.

Reads max t_s already recorded and runs only the remaining duration,
appending new rows to the same CSV (with t_s offset by prior elapsed).
"""
from __future__ import annotations
import argparse, csv, os, sys, time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.common import run_pmu

CSV_PATH = Path(__file__).resolve().parents[1] / "data/processed/e4_safety_histogram.csv"
BOUNDS_PATH = Path(__file__).resolve().parents[1] / "data/processed/e3_bounds.csv"

CASE_STUDY = ["fir2dim", "fmref", "cosf", "test3", "jfdctint", "fac"]
ATTACKERS = ["MAX_LLC", "MAX_BUS", "MAX_MEM"]  # MANUAL.md mix config


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--total-duration-s", type=int, default=86400,
                    help="target total duration including prior elapsed (default 24h)")
    ap.add_argument("--R", type=int, default=100)
    ap.add_argument("--sudo-pw", default=os.environ.get("SUDO_PW"))
    args = ap.parse_args()

    # Load bounds
    bounds: dict[str, dict[str, str]] = {}
    with open(BOUNDS_PATH) as f:
        for r in csv.DictReader(f):
            bounds[r["bench"]] = r

    # Determine prior elapsed
    prior_t = 0.0
    rows_existing = 0
    if CSV_PATH.exists():
        with CSV_PATH.open() as f:
            r = csv.DictReader(f)
            for row in r:
                rows_existing += 1
                try:
                    prior_t = max(prior_t, float(row["t_s"]))
                except (KeyError, ValueError):
                    pass
    remain = max(0, args.total_duration_s - prior_t)
    print(f"[E4-resume] prior_rows={rows_existing} prior_t={prior_t:.0f}s "
          f"remaining={remain:.0f}s target_total={args.total_duration_s}s",
          flush=True)
    if remain <= 0:
        print("[E4-resume] Already complete.")
        return 0

    fields = ["t_s", "victim", "c_obs", "rampart_full", "ratio", "violation"]
    write_header = not CSV_PATH.exists() or rows_existing == 0
    t0 = time.time()
    i = 0
    n_v = 0
    with CSV_PATH.open("a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        if write_header:
            w.writeheader()
        while time.time() - t0 < remain:
            victim = CASE_STUDY[i % len(CASE_STUDY)]
            r = run_pmu(victim, ATTACKERS, n=args.R,
                        sudo_pw=args.sudo_pw, timeout=600)
            c_obs = r["metrics"].get("cycles", {}).get("median", 0)
            b = float(bounds.get(victim, {}).get("rampart_full", c_obs))
            ratio = c_obs / b if b else 1.0
            v = ratio > 1.0
            if v:
                n_v += 1
            t_s = round(prior_t + (time.time() - t0), 2)
            w.writerow({"t_s": t_s, "victim": victim,
                        "c_obs": c_obs, "rampart_full": b,
                        "ratio": round(ratio, 4), "violation": v})
            f.flush()
            i += 1
    print(f"[E4-resume] done: appended {i} rows, violations={n_v}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
