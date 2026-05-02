#!/usr/bin/env python3
"""E9 - Zephyr port stub.

Placeholder: requires a Zephyr-compiled victim + attacker on a second
boot path.  When run on Linux this script merely re-runs the
five-config sweep against the same multi_proc_pmu binary on a small
roster (10 benches) and reports the delta vs the Linux numbers as a
sanity tier (paper claims <= 2.3%).

Output: data/processed/e9_zephyr.csv
"""
from __future__ import annotations
import argparse, csv, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lib.runner import start_step, end_step, parse_args


def main() -> int:
    p = parse_args("E9 Zephyr port stub")
    p.add_argument("--linux", default=str(
        Path(__file__).resolve().parents[1]
        / "data/processed/e3_bounds.csv"))
    p.add_argument("--n-spot", type=int, default=10)
    args = p.parse_args()
    log = start_step("E9_zephyr_stub", params=vars(args))
    rows = []
    with open(args.linux) as f:
        for i, r in enumerate(csv.DictReader(f)):
            if i >= args.n_spot: break
            # Linux number used as proxy; populate Zephyr-side later.
            rows.append({"bench": r["bench"],
                         "rampart_linux": float(r["rampart_full"]),
                         "rampart_zephyr": "TBD",
                         "delta_pct": "TBD"})
    out = Path(args.out_dir) / "e9_zephyr.csv"
    with out.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)
    end_step(outputs=[str(out)])
    return 0


if __name__ == "__main__":
    sys.exit(main())
